# Copyright 2020-present PlatformIO <contact@platformio.org>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Espressif IDF

Espressif IoT Development Framework for ESP32 MCU

https://github.com/espressif/esp-idf
"""

import copy
import json
import subprocess
import sys
from os import environ, listdir, makedirs, rename, pathsep
from os.path import (
    abspath,
    basename,
    dirname,
    getmtime,
    isabs,
    isdir,
    isfile,
    join,
    realpath,
    relpath,
)

import click

from SCons.Script import (
    ARGUMENTS,
    COMMAND_LINE_TARGETS,
    AlwaysBuild,
    DefaultEnvironment,
)

from platformio.builder.tools.piolib import ProjectAsLibBuilder
from platformio.fs import to_unix_path
from platformio.proc import exec_command, where_is_program
from platformio.util import get_systype

env = DefaultEnvironment()
platform = env.PioPlatform()

env.SConscript("_embed_files.py", exports="env")

FRAMEWORK_DIR = platform.get_package_dir("framework-espidf")
assert FRAMEWORK_DIR and isdir(FRAMEWORK_DIR)

if "arduino" in env.subst("$PIOFRAMEWORK"):
    ARDUINO_FRAMEWORK_DIR = platform.get_package_dir("framework-arduinoespressif32")
    # Possible package names in 'package@version' format is not compatible with CMake
    if "@" in basename(ARDUINO_FRAMEWORK_DIR):
        new_path = join(
            dirname(ARDUINO_FRAMEWORK_DIR),
            basename(ARDUINO_FRAMEWORK_DIR).replace("@", "-"),
        )
        rename(ARDUINO_FRAMEWORK_DIR, new_path)
        ARDUINO_FRAMEWORK_DIR = new_path
    assert ARDUINO_FRAMEWORK_DIR and isdir(ARDUINO_FRAMEWORK_DIR)

try:
    import future
    import pyparsing
    import cryptography
except ImportError:
    env.Execute(
        env.VerboseAction(
            '$PYTHONEXE -m pip install "cryptography>=2.1.4" "future>=0.15.2" "pyparsing>=2.0.3,<2.4.0"',
            "Installing ESP-IDF's Python dependencies",
        )
    )

platform = env.PioPlatform()
board = env.BoardConfig()

FRAMEWORK_DIR = platform.get_package_dir("framework-espidf")
assert isdir(FRAMEWORK_DIR)

BUILD_DIR = env.subst("$BUILD_DIR")
CMAKE_API_REPLY_PATH = join(".cmake", "api", "v1", "reply")


def get_project_lib_includes(env):
    project = ProjectAsLibBuilder(env, "$PROJECT_DIR")
    project.search_deps_recursive()

    paths = []
    for lb in env.GetLibBuilders():
        if not lb.dependent:
            continue
        lb.env.PrependUnique(CPPPATH=lb.get_include_dirs())
        paths.extend(lb.env["CPPPATH"])

    DefaultEnvironment().Replace(__PIO_LIB_BUILDERS=None)

    return paths


def is_cmake_reconfigure_required(cmake_api_reply_dir):
    cmake_cache_file = join(BUILD_DIR, "CMakeCache.txt")
    cmake_txt_files = [
        join(env.subst("$PROJECT_DIR"), "CMakeLists.txt"),
        join(env.subst("$PROJECT_SRC_DIR"), "CMakeLists.txt")
    ]
    cmake_preconf_dir = join(BUILD_DIR, "config")
    sdkconfig = join(env.subst("$PROJECT_DIR"), "sdkconfig")

    for d in (cmake_api_reply_dir, cmake_preconf_dir):
        if not isdir(d) or not listdir(d):
            return True
    if not isfile(cmake_cache_file):
        return True
    if not isfile(join(BUILD_DIR, "build.ninja")):
        return True
    if isfile(sdkconfig) and getmtime(sdkconfig) > getmtime(cmake_cache_file):
        return True
    if any(
        getmtime(f) > getmtime(cmake_cache_file)
        for f in cmake_txt_files + [cmake_preconf_dir]
    ):
        return True

    return False


def is_proper_idf_project():
    return all(
        isfile(path)
        for path in (
            join(env.subst("$PROJECT_DIR"), "CMakeLists.txt"),
            join(env.subst("$PROJECT_SRC_DIR"), "CMakeLists.txt"),
        )
    )


def collect_src_files():
    return [
        f
        for f in env.MatchSourceFiles("$PROJECT_SRC_DIR", env.get("SRC_FILTER"))
        if not f.endswith((".h", ".hpp"))
    ]


def normalize_path(path):
    project_dir = env.subst("$PROJECT_DIR")
    if project_dir in path:
        path = path.replace(project_dir, "${CMAKE_SOURCE_DIR}")
    return to_unix_path(path)


def create_default_project_files():
    root_cmake_tpl = """cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(%s)
"""
    prj_cmake_tpl = """# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources %s/*.*)

idf_component_register(SRCS ${app_sources})
"""

    if not listdir(join(env.subst("$PROJECT_SRC_DIR"))):
        # create a default main file to make CMake happy during first init
        with open(join(env.subst("$PROJECT_SRC_DIR"), "main.c"), "w") as fp:
            fp.write("void app_main() {}")

    project_dir = env.subst("$PROJECT_DIR")
    if not isfile(join(project_dir, "CMakeLists.txt")):
        with open(join(project_dir, "CMakeLists.txt"), "w") as fp:
            fp.write(root_cmake_tpl % basename(project_dir))

    project_src_dir = env.subst("$PROJECT_SRC_DIR")
    if not isfile(join(project_src_dir, "CMakeLists.txt")):
        with open(join(project_src_dir, "CMakeLists.txt"), "w") as fp:
            fp.write(
                prj_cmake_tpl
                % normalize_path(env.subst("$PROJECT_SRC_DIR"))
            )


def get_cmake_code_model(src_dir, build_dir, extra_args=None):
    cmake_api_dir = join(build_dir, ".cmake", "api", "v1")
    cmake_api_query_dir = join(cmake_api_dir, "query")
    cmake_api_reply_dir = join(cmake_api_dir, "reply")
    query_file = join(cmake_api_query_dir, "codemodel-v2")

    if not isfile(query_file):
        makedirs(dirname(query_file))
        open(query_file, "a").close()  # create an empty file

    if not is_proper_idf_project():
        create_default_project_files()

    if is_cmake_reconfigure_required(cmake_api_reply_dir):
        run_cmake(src_dir, build_dir, extra_args)

    if not isdir(cmake_api_reply_dir) or not listdir(cmake_api_reply_dir):
        sys.stderr.write("Error: Couldn't find CMake API response file\n")
        env.Exit(1)

    codemodel = {}
    for target in listdir(cmake_api_reply_dir):
        if target.startswith("codemodel-v2"):
            with open(join(cmake_api_reply_dir, target), "r") as fp:
                codemodel = json.load(fp)

    assert codemodel["version"]["major"] == 2
    return codemodel


def populate_idf_env_vars(idf_env):
    idf_env["IDF_PATH"] = platform.get_package_dir("framework-espidf")

    additional_packages = [
        join(platform.get_package_dir("toolchain-xtensa32"), "bin"),
        join(platform.get_package_dir("toolchain-esp32ulp"), "bin"),
        platform.get_package_dir("tool-ninja"),
        join(platform.get_package_dir("tool-cmake"), "bin"),
        dirname(where_is_program("python")),
    ]

    if "windows" in get_systype():
        additional_packages.append(platform.get_package_dir("tool-mconf"))

    idf_env["PATH"] = pathsep.join(additional_packages + [idf_env["PATH"]])


def get_target_config(project_configs, target_index, cmake_api_reply_dir):
    target_json = project_configs.get("targets")[target_index].get("jsonFile", "")
    target_config_file = join(cmake_api_reply_dir, target_json)
    if not isfile(target_config_file):
        sys.stderr.write("Error: Couldn't find target config %s\n" % target_json)
        env.Exit(1)

    with open(target_config_file) as fp:
        return json.load(fp)


def load_target_configurations(cmake_codemodel, cmake_api_reply_dir):
    configs = {}
    project_configs = cmake_codemodel.get("configurations")[0]
    for config in project_configs.get("projects", []):
        for target_index in config.get("targetIndexes", []):
            target_config = get_target_config(
                project_configs, target_index, cmake_api_reply_dir
            )
            configs[target_config["name"]] = target_config

    return configs


def build_library(default_env, lib_config, project_src_dir, prepend_dir=None):
    lib_name = lib_config["nameOnDisk"]
    lib_path = lib_config["paths"]["build"]
    if prepend_dir:
        lib_path = join(prepend_dir, lib_path)
    lib_objects = compile_source_files(
        lib_config, default_env, project_src_dir, prepend_dir
    )
    return default_env.Library(
        target=join("$BUILD_DIR", lib_path, lib_name), source=lib_objects
    )


def get_app_includes(app_config):
    plain_includes = []
    sys_includes = []
    cg = app_config["compileGroups"][0]
    for inc in cg.get("includes", []):
        inc_path = inc["path"]
        if inc.get("isSystem", False):
            sys_includes.append(inc_path)
        else:
            plain_includes.append(inc_path)

    return {"plain_includes": plain_includes, "sys_includes": sys_includes}


def extract_defines(compile_group):
    result = []
    result.extend(
        [
            d.get("define").replace('"', '\\"').strip()
            for d in compile_group.get("defines", [])
        ]
    )
    for f in compile_group.get("compileCommandFragments", []):
        if f.get("fragment", "").startswith("-D"):
            result.append(f["fragment"][2:])
    return result


def get_app_defines(app_config):
    return extract_defines(app_config["compileGroups"][0])


def extract_link_args(target_config):
    link_args = {"LINKFLAGS": [], "LIBS": [], "LIBPATH": [], "__LIB_DEPS": []}

    for f in target_config.get("link", {}).get("commandFragments", []):
        fragment = f.get("fragment", "").strip()
        fragment_role = f.get("role", "").strip()
        if not fragment or not fragment_role:
            continue
        args = click.parser.split_arg_string(fragment)
        if fragment_role == "flags":
            link_args["LINKFLAGS"].extend(args)
        elif fragment_role == "libraries":
            if fragment.startswith("-l"):
                link_args["LIBS"].extend(args)
            elif fragment.startswith("-L"):
                lib_path = fragment.replace("-L", "").strip()
                if lib_path not in link_args["LIBPATH"]:
                    link_args["LIBPATH"].append(lib_path)
            elif fragment.startswith("-") and not fragment.startswith("-l"):
                # CMake mistakenly marks LINKFLAGS as libraries
                link_args["LINKFLAGS"].extend(args)
            elif isfile(fragment) and isabs(fragment):
                # In case of precompiled archives from framework package
                lib_path = dirname(fragment)
                if lib_path not in link_args["LIBPATH"]:
                    link_args["LIBPATH"].append(dirname(fragment))
                link_args["LIBS"].extend(
                    [basename(l) for l in args if l.endswith(".a")]
                )
            elif fragment.endswith(".a"):
                link_args["__LIB_DEPS"].extend(
                    [basename(l) for l in args if l.endswith(".a")]
                )

    return link_args


def filter_args(args, allowed, ignore=None):
    if not allowed:
        return []

    ignore = ignore or []
    result = []
    i = 0
    length = len(args)
    while i < length:
        if any(args[i].startswith(f) for f in allowed) and not any(
            args[i].startswith(f) for f in ignore
        ):
            result.append(args[i])
            if i + 1 < length and not args[i + 1].startswith("-"):
                i += 1
                result.append(args[i])
        i += 1
    return result


def get_app_flags(app_config, default_config):
    def _extract_flags(config):
        flags = {}
        for cg in config["compileGroups"]:
            flags[cg["language"]] = []
            for ccfragment in cg["compileCommandFragments"]:
                fragment = ccfragment.get("fragment", "")
                if not fragment.strip() or fragment.startswith("-D"):
                    continue
                flags[cg["language"]].extend(
                    click.parser.split_arg_string(fragment.strip())
                )

        return flags

    app_flags = _extract_flags(app_config)
    default_flags = _extract_flags(default_config)

    # Flags are sorted because CMake randomly populates build flags in code model
    return {
        "ASFLAGS": sorted(app_flags.get("ASM", default_flags.get("ASM"))),
        "CFLAGS": sorted(app_flags.get("C", default_flags.get("C"))),
        "CXXFLAGS": sorted(app_flags.get("CXX", default_flags.get("CXX"))),
    }


def get_sdk_configuration():
    config_path = join(env.subst("$BUILD_DIR"), "config", "sdkconfig.json")
    if not isfile(config_path):
        print('Warning: Could not find "sdkconfig.json" file\n')

    try:
        with open(config_path, "r") as fp:
            return json.load(fp)
    except:
        return {}


def find_framework_service_files(search_path, sdk_config):
    result = {}
    result["lf_files"] = list()
    result["kconfig_files"] = list()
    result["kconfig_build_files"] = list()
    for d in listdir(search_path):
        path = join(search_path, d)
        if not isdir(path):
            continue
        for f in listdir(path):
            if f == "linker.lf":
                result["lf_files"].append(join(path, f))
            elif f == "Kconfig.projbuild":
                result["kconfig_build_files"].append(join(path, f))
            elif f == "Kconfig":
                result["kconfig_files"].append(join(path, f))

    result["lf_files"].extend([
            join(FRAMEWORK_DIR, "components", "esp32", "ld", "esp32_fragments.lf"),
        join(FRAMEWORK_DIR, "components", "newlib", "newlib.lf")
    ])

    if sdk_config.get("SPIRAM_CACHE_WORKAROUND", False):
        result["lf_files"].append(join(
            FRAMEWORK_DIR, "components", "newlib", "esp32-spiram-rom-functions-c.lf"))

    return result


def create_custom_libraries_list(ldgen_libraries_file, ignore_targets):
    if not isfile(ldgen_libraries_file):
        sys.stderr.write("Error: Couldn't find the list of framework libraries\n")
        env.Exit(1)

    pio_libraries_file = ldgen_libraries_file + "_pio"

    if isfile(pio_libraries_file):
        return pio_libraries_file

    lib_paths = []
    with open(ldgen_libraries_file, "r") as fp:
        lib_paths = fp.readlines()

    with open(pio_libraries_file, "w") as fp:
        for lib_path in lib_paths:
            if all(
                "lib%s.a" % t.replace("__idf_", "") not in lib_path
                for t in ignore_targets
            ):
                fp.write(lib_path)

    return pio_libraries_file


def generate_project_ld_script(sdk_config, ignore_targets=None):
    ignore_targets = ignore_targets or []
    project_files = find_framework_service_files(
        join(FRAMEWORK_DIR, "components"), sdk_config
    )

    # Create a new file to avoid automatically generated library entry as files from
    # this library are built internally by PlatformIO
    libraries_list = create_custom_libraries_list(
        join(env.subst("$BUILD_DIR"), "ldgen_libraries"), ignore_targets
    )

    args = {
        "script": join(FRAMEWORK_DIR, "tools", "ldgen", "ldgen.py"),
        "config": join(env.subst("$PROJECT_DIR"), "sdkconfig"),
        "fragments": " ".join(['"%s"' % f for f in project_files.get("lf_files")]),
        "kconfig": join(FRAMEWORK_DIR, "Kconfig"),
        "env_file": join("$BUILD_DIR", "config.env"),
        "libraries_list": libraries_list,
        "objdump": join(
            platform.get_package_dir("toolchain-xtensa32"),
            "bin",
            env.subst("$CC").replace("-gcc", "-objdump"),
        ),
    }

    cmd = (
        '"$PYTHONEXE" "{script}" --input $SOURCE '
        '--config "{config}" --fragments {fragments} --output $TARGET '
        '--kconfig "{kconfig}" --env-file "{env_file}" '
        '--libraries-file "{libraries_list}" '
        '--objdump "{objdump}"'
    ).format(**args)

    return env.Command(
        join("$BUILD_DIR", "esp32.project.ld"),
        join(FRAMEWORK_DIR, "components", "esp32", "ld", "esp32.project.ld.in"),
        env.VerboseAction(cmd, "Generating project linker script $TARGET"),
    )


def prepare_build_envs(config, default_env):
    build_envs = []
    target_compile_groups = config.get("compileGroups")
    is_build_type_debug = (
        set(["debug", "sizedata"]) & set(COMMAND_LINE_TARGETS)
        or default_env.GetProjectOption("build_type") == "debug"
    )

    for cg in target_compile_groups:
        includes = []
        sys_includes = []
        for inc in cg.get("includes", []):
            inc_path = inc["path"]
            if inc.get("isSystem", False):
                sys_includes.append(inc_path)
            else:
                includes.append(inc_path)

        defines = extract_defines(cg)
        compile_commands = cg.get("compileCommandFragments", [])
        build_env = default_env.Clone()
        for cc in compile_commands:
            build_flags = cc.get("fragment")
            if not build_flags.startswith("-D"):
                build_env.AppendUnique(**build_env.ParseFlags(build_flags))
        build_env.AppendUnique(CPPDEFINES=defines, CPPPATH=includes)
        if sys_includes:
            build_env.Append(CCFLAGS=[("-isystem", inc) for inc in sys_includes])
        build_env.Append(ASFLAGS=build_env.get("CCFLAGS", [])[:])
        build_env.ProcessUnFlags(default_env.get("BUILD_UNFLAGS"))
        if is_build_type_debug:
            build_env.ConfigureDebugFlags()
        build_envs.append(build_env)

    return build_envs


def compile_source_files(config, default_env, project_src_dir, prepend_dir=None):
    build_envs = prepare_build_envs(config, default_env)
    objects = []
    for source in config.get("sources", []):
        if source["path"].endswith(".rule"):
            continue
        compile_group_idx = source.get("compileGroupIndex")
        if compile_group_idx is not None:
            src_path = source.get("path")
            if not isabs(src_path):
                # For cases when sources are located near CMakeLists.txt
                src_path = join(project_src_dir, src_path)
            local_path = config["paths"]["source"]
            if not isabs(local_path):
                local_path = join(project_src_dir, config["paths"]["source"])
            obj_path = join("$BUILD_DIR", prepend_dir or "", config["paths"]["build"])
            objects.append(
                build_envs[compile_group_idx].StaticObject(
                    target=join(obj_path, relpath(src_path, local_path) + ".o"),
                    source=realpath(src_path),
                )
            )

    return objects


def run_tool(cmd):
    idf_env = environ.copy()
    populate_idf_env_vars(idf_env)

    result = exec_command(cmd, env=idf_env)
    if result["returncode"] != 0:
        sys.stderr.write(result["out"] + "\n")
        sys.stderr.write(result["err"] + "\n")
        env.Exit(1)

    if int(ARGUMENTS.get("PIOVERBOSE", 0)):
        print(result["out"])
        print(result["err"])


def RunMenuconfig(target, source, env):
    idf_env = environ.copy()
    populate_idf_env_vars(idf_env)

    rc = subprocess.call(
        [
            join(platform.get_package_dir("tool-cmake"), "bin", "cmake"),
            "--build",
            env.subst("$BUILD_DIR"),
            "--target",
            "menuconfig",
        ],
        env=idf_env,
    )

    if rc != 0:
        sys.stderr.write("Error: Couldn't execute 'menuconfig' target.\n")
        env.Exit(1)


def run_cmake(src_dir, build_dir, extra_args=None):
    cmd = [
        join(platform.get_package_dir("tool-cmake") or "", "bin", "cmake"),
        "-S",
        src_dir,
        "-B",
        build_dir,
        "-G",
        "Ninja",
    ]

    if extra_args:
        cmd.extend(extra_args)

    run_tool(cmd)


def find_lib_deps(components_map, elf_config, link_args, ignore_components=None):
    ignore_components = ignore_components or []
    result = [
        components_map[d["id"]]["lib"]
        for d in elf_config.get("dependencies", [])
        if components_map.get(d["id"], {})
        and not d["id"].startswith(tuple(ignore_components))
    ]

    implicit_lib_deps = link_args.get("__LIB_DEPS", [])
    for component in components_map.values():
        component_config = component["config"]
        if (
            component_config["type"] not in ("STATIC_LIBRARY", "OBJECT_LIBRARY")
            or component_config["name"] in ignore_components
        ):
            continue
        if (
            component_config["nameOnDisk"] in implicit_lib_deps
            and component["lib"] not in result
        ):
            result.append(component["lib"])

    return result


def build_bootloader():
    bootloader_src_dir = join(FRAMEWORK_DIR, "components", "bootloader", "subproject")
    code_model = get_cmake_code_model(
        bootloader_src_dir,
        join(BUILD_DIR, "bootloader"),
        [
            "-DIDF_TARGET=esp32",
            "-DPYTHON_DEPS_CHECKED=1",
            "-DIDF_PATH=" + platform.get_package_dir("framework-espidf"),
            "-DSDKCONFIG=" + join(env.subst("$PROJECT_DIR"), "sdkconfig"),
            "-DEXTRA_COMPONENT_DIRS=" + join(FRAMEWORK_DIR, "components", "bootloader"),
        ],
    )

    if not code_model:
        sys.stderr.write("Error: Couldn't find code model for bootloader\n")
        env.Exit(1)

    target_configs = load_target_configurations(
        code_model, join(BUILD_DIR, "bootloader", CMAKE_API_REPLY_PATH)
    )

    elf_config = get_project_elf(target_configs)
    if not elf_config:
        sys.stderr.write(
            "Error: Couldn't load the main firmware target of the project\n"
        )
        env.Exit(1)

    bootloader_env = env.Clone()
    components_map = get_components_map(
        target_configs, ["STATIC_LIBRARY", "OBJECT_LIBRARY"]
    )

    build_components(bootloader_env, components_map, bootloader_src_dir, "bootloader")
    link_args = extract_link_args(elf_config)
    extra_flags = filter_args(link_args["LINKFLAGS"], ["-T", "-u"])
    link_args["LINKFLAGS"] = sorted(
        list(set(link_args["LINKFLAGS"]) - set(extra_flags))
    )

    bootloader_env.MergeFlags(link_args)
    bootloader_env.Append(LINKFLAGS=extra_flags)
    bootloader_libs = find_lib_deps(components_map, elf_config, link_args)

    bootloader_env.Prepend(__RPATH="-Wl,--start-group ")
    bootloader_env.Append(
        CPPDEFINES=["__BOOTLOADER_BUILD"], _LIBDIRFLAGS=" -Wl,--end-group"
    )

    return bootloader_env.ElfToBin(
        join("$BUILD_DIR", "bootloader"),
        bootloader_env.Program(join("$BUILD_DIR", "bootloader.elf"), bootloader_libs),
    )


def get_targets_by_type(target_configs, target_types, ignore_targets=None):
    ignore_targets = ignore_targets or []
    result = []
    for target_config in target_configs.values():
        if (
            target_config["type"] in target_types
            and target_config["name"] not in ignore_targets
        ):
            result.append(target_config)

    return result


def get_components_map(target_configs, target_types, ignore_components=None):
    result = {}
    for config in get_targets_by_type(target_configs, target_types, ignore_components):
        result[config["id"]] = {"config": config}

    return result


def build_components(env, components_map, project_src_dir, prepend_dir=None):
    for k, v in components_map.items():
        components_map[k]["lib"] = build_library(
            env, v["config"], project_src_dir, prepend_dir
        )


def get_project_elf(target_configs):
    exec_targets = get_targets_by_type(target_configs, ["EXECUTABLE"])
    if len(exec_targets) > 1:
        print(
            "Warning: Multiple elf targets found. The %s will be used!"
            % exec_targets[0]["name"]
        )

    return exec_targets[0]


def generate_default_component():
    # Used to force CMake generate build environments for all supported languages

    prj_cmake_tpl = """# Warning! Do not delete this auto-generated file.
file(GLOB component_sources *.c* *.S)
idf_component_register(SRCS ${component_sources})
"""
    dummy_component_path = join(BUILD_DIR, "__pio_env")
    if not isdir(dummy_component_path):
        makedirs(dummy_component_path)

    for ext in (".cpp", ".c", ".S"):
        dummy_file = join(dummy_component_path, "__dummy" + ext)
        if not isfile(dummy_file):
            open(dummy_file, "a").close()

    component_cmake = join(dummy_component_path, "CMakeLists.txt")
    if not isfile(component_cmake):
        with open(component_cmake, "w") as fp:
            fp.write(prj_cmake_tpl)

    return dummy_component_path


def find_default_component(target_configs):
    for config in target_configs:
        if "__pio_env" in config:
            return config
    return ""


#
# Generate final linker script
#

if not board.get("build.ldscript", ""):
    linker_script = env.Command(
        join("$BUILD_DIR", "esp32_out.ld"),
        board.get(
            "build.esp-idf.ldscript",
            join(FRAMEWORK_DIR, "components", "esp32", "ld", "esp32.ld"),
        ),
        env.VerboseAction(
            '$CC -I"$BUILD_DIR/config" -C -P -x  c -E $SOURCE -o $TARGET',
            "Generating LD script $TARGET",
        ),
    )

    env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", linker_script)
    env.Replace(LDSCRIPT_PATH="esp32_out.ld")

#
# Generate partition table
#

fwpartitions_dir = join(FRAMEWORK_DIR, "components", "partition_table")
partitions_csv = board.get("build.partitions", "partitions_singleapp.csv")
env.Replace(
    PARTITIONS_TABLE_CSV=abspath(
        join(fwpartitions_dir, partitions_csv)
        if isfile(join(fwpartitions_dir, partitions_csv))
        else partitions_csv
    )
)

partition_table = env.Command(
    join("$BUILD_DIR", "partitions.bin"),
    "$PARTITIONS_TABLE_CSV",
    env.VerboseAction(
        '"$PYTHONEXE" "%s" -q --flash-size "%s" $SOURCE $TARGET'
        % (
            join(FRAMEWORK_DIR, "components", "partition_table", "gen_esp32part.py"),
            board.get("upload.flash_size", "4MB"),
        ),
        "Generating partitions $TARGET",
    ),
)

env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", partition_table)

#
# Current build script limitations
#

if any(" " in p for p in (FRAMEWORK_DIR, BUILD_DIR)):
    sys.stderr.write("Error: Detected a whitespace character in project paths.\n")
    env.Exit(1)


if env.subst("$SRC_FILTER"):
    print(
        (
            "Warning: the 'src_filter' option cannot be used with ESP-IDF. Select source "
            "files to build in the project CMakeLists.txt file.\n"
        )
    )

if isfile(join(env.subst("$PROJECT_SRC_DIR"), "sdkconfig.h")):
    print(
        "Warning! Starting with ESP-IDF v4.0, new project structure is required: \n"
        "https://docs.platformio.org/en/latest/frameworks/espidf.html#project-structure"
    )

#
# Initial targets loading
#

# By default 'main' folder is used to store source files. In case when a user has
# default 'src' folder we need to add this as an extra component. If there is no 'main'
# folder CMake won't generate dependencies properly
extra_components = [generate_default_component()]
if env.subst("$PROJECT_SRC_DIR") != join(env.subst("$PROJECT_DIR"), "main"):
    extra_components.append(env.subst("$PROJECT_SRC_DIR"))
    if "arduino" in env.subst("$PIOFRAMEWORK"):
        extra_components.append(ARDUINO_FRAMEWORK_DIR)

print("Reading CMake configuration...")
project_codemodel = get_cmake_code_model(
    env.subst("$PROJECT_DIR"),
    BUILD_DIR,
    ["-DEXTRA_COMPONENT_DIRS:PATH=" + ";".join(extra_components)] +
    click.parser.split_arg_string(board.get("build.cmake_extra_args", ""))
)

if not project_codemodel:
    sys.stderr.write("Error: Couldn't find code model generated by CMake\n")
    env.Exit(1)

target_configs = load_target_configurations(
    project_codemodel, join(BUILD_DIR, CMAKE_API_REPLY_PATH)
)

sdk_config = get_sdk_configuration()


project_target_name = "__idf_%s" % basename(env.subst("$PROJECT_SRC_DIR"))
if project_target_name not in target_configs:
    sys.stderr.write("Error: Couldn't find the main target of the project!\n")
    env.Exit(1)

if project_target_name != "__idf_main" and "__idf_main" in target_configs:
    sys.stderr.write(
        (
            "Warning! Detected two different targets with project sources. Please use "
            "either %s or specify 'main' folder in 'platformio.ini' file.\n"
            % project_target_name
        )
    )
    env.Exit(1)

project_ld_scipt = generate_project_ld_script(
    sdk_config, [project_target_name, "__pio_env"])
env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", project_ld_scipt)

elf_config = get_project_elf(target_configs)
default_config_name = find_default_component(target_configs)
framework_components_map = get_components_map(
    target_configs,
    ["STATIC_LIBRARY", "OBJECT_LIBRARY"],
    [project_target_name, default_config_name],
)

build_components(env, framework_components_map, env.subst("$PROJECT_DIR"))

if not elf_config:
    sys.stderr.write("Error: Couldn't load the main firmware target of the project\n")
    env.Exit(1)

for component_config in framework_components_map.values():
    env.Depends(project_ld_scipt, component_config["lib"])

project_config = target_configs.get(project_target_name, {})
default_config = target_configs.get(default_config_name, {})
project_defines = get_app_defines(project_config)
project_flags = get_app_flags(project_config, default_config)
link_args = extract_link_args(elf_config)
app_includes = get_app_includes(elf_config)
project_lib_includes = get_project_lib_includes(env)

#
# Compile bootloader
#

env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", build_bootloader())

#
# Target: ESP-IDF menuconfig
#

env.AddPlatformTarget("menuconfig", None, [env.VerboseAction(
    RunMenuconfig, "Running menuconfig...")], "Run Menuconfig")

#
# Process main parts of the framework
#

libs = find_lib_deps(
    framework_components_map, elf_config, link_args, [project_target_name]
)

# Extra flags which need to be explicitly specified in LINKFLAGS section because SCons
# cannot merge them correctly
extra_flags = filter_args(link_args["LINKFLAGS"], ["-T", "-u"])
link_args["LINKFLAGS"] = sorted(list(set(link_args["LINKFLAGS"]) - set(extra_flags)))

# remove the main linker script flags '-T esp32_out.ld'
try:
    ld_index = extra_flags.index("esp32_out.ld")
    extra_flags.pop(ld_index)
    extra_flags.pop(ld_index - 1)
except:
    print("Warning! Couldn't find the main linker script in the CMake code model.")

#
# Process project sources
#

# Remove project source files from following build stages as they're
# built as part of the framework
def _skip_prj_source_files(node):
    if (
        node.srcnode()
        .get_path()
        .lower()
        .startswith(env.subst("$PROJECT_SRC_DIR").lower())
    ):
        return None
    return node


env.AddBuildMiddleware(_skip_prj_source_files)

# Project files should be compiled only when a special
# option is enabled when running 'test' command
if "__test" not in COMMAND_LINE_TARGETS or env.GetProjectOption(
    "test_build_project_src"
):
    project_env = env.Clone()
    if project_target_name != "__idf_main":
        # Manually add dependencies to CPPPATH since ESP-IDF build system doesn't generate
        # this info if the folder with sources is not named 'main'
        # https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#rename-main
        project_env.AppendUnique(CPPPATH=app_includes["plain_includes"])

    # Add include dirs from PlatformIO build system to project CPPPATH so
    # they're visible to PIOBUILDFILES
    project_env.Append(
        CPPPATH=["$PROJECT_INCLUDE_DIR", "$PROJECT_SRC_DIR"] + project_lib_includes
    )

    env.Append(
        PIOBUILDFILES=compile_source_files(
            target_configs.get(project_target_name),
            project_env,
            project_env.subst("$PROJECT_DIR"),
        )
    )

project_flags.update(link_args)
env.MergeFlags(project_flags)
env.Prepend(
    CPPPATH=app_includes["plain_includes"],
    CPPDEFINES=project_defines,
    LINKFLAGS=extra_flags,
    LIBS=libs,
    FLASH_EXTRA_IMAGES=[
        ("0x1000", join("$BUILD_DIR", "bootloader.bin")),
        ("0x8000", join("$BUILD_DIR", "partitions.bin")),
    ],
)

#
# To embed firmware checksum a special argument for esptool.py is required
#

action = copy.deepcopy(env["BUILDERS"]["ElfToBin"].action)
action.cmd_list = env["BUILDERS"]["ElfToBin"].action.cmd_list.replace(
    "-o", "--elf-sha256-offset 0xb0 -o")
env["BUILDERS"]["ElfToBin"].action = action

#
# Compile ULP sources in 'ulp' folder
#

ulp_dir = join(env.subst("$PROJECT_DIR"), "ulp")
if isdir(ulp_dir) and listdir(ulp_dir):
    env.SConscript("ulp.py", exports="env project_config")
