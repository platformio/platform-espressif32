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

import json
import subprocess
import sys
from os import environ, listdir, makedirs, pathsep
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


def is_cmake_reconfigure_required(cmake_api_reply_dir):
    cmake_cache_file = join(BUILD_DIR, "CMakeCache.txt")
    cmake_txt_file = join(env.subst("$PROJECT_DIR"), "CMakeLists.txt")
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
        for f in (cmake_txt_file, cmake_preconf_dir)
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
    return env.MatchSourceFiles("$PROJECT_SRC_DIR", env.get("SRC_FILTER"))


def create_default_project_files():
    root_cmake_tpl = """cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(%s)
"""
    prj_cmake_tpl = """# Warning! This code was automatically generated for projects
# without default 'CMakeLists.txt' file.

set(app_sources
%s)

idf_component_register(SRCS ${app_sources})
"""

    if not listdir(join(env.subst("$PROJECT_SRC_DIR"))):
        # create an empty file to make CMake happy during first init
        open(join(env.subst("$PROJECT_SRC_DIR"), "empty.c"), "a").close()

    project_dir = env.subst("$PROJECT_DIR")
    if not isfile(join(project_dir, "CMakeLists.txt")):
        with open(join(project_dir, "CMakeLists.txt"), "w") as fp:
            fp.write(root_cmake_tpl % basename(project_dir))

    project_src_dir = env.subst("$PROJECT_SRC_DIR")
    if not isfile(join(project_src_dir, "CMakeLists.txt")):
        with open(join(project_src_dir, "CMakeLists.txt"), "w") as fp:
            fp.write(prj_cmake_tpl % "".join(
                '\t"%s"\n' % to_unix_path(f) for f in collect_src_files()))


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


def get_app_flags(app_config):
    app_flags = {}
    for cg in app_config["compileGroups"]:
        app_flags[cg["language"]] = []
        for ccfragment in cg["compileCommandFragments"]:
            fragment = ccfragment.get("fragment", "")
            if not fragment.strip() or fragment.startswith("-D"):
                continue
            app_flags[cg["language"]].extend(
                click.parser.split_arg_string(fragment.strip())
            )

    cflags = app_flags.get("C", [])
    cxx_flags = app_flags.get("CXX", [])
    ccflags = set(cflags).intersection(cxx_flags)

    # Flags are sorted because CMake randomly populates build flags in code model
    return {
        "ASFLAGS": sorted(app_flags.get("ASM", [])),
        "CFLAGS": sorted(list(set(cflags) - ccflags)),
        "CCFLAGS": sorted(list(ccflags)),
        "CXXFLAGS": sorted(list(set(cxx_flags) - ccflags)),
    }


def find_framework_service_files(search_path):
    result = {}
    result["lf_files"] = list()
    result["kconfig_files"] = list()
    result["kconfig_build_files"] = list()
    for d in listdir(search_path):
        for f in listdir(join(search_path, d)):
            if f == "linker.lf":
                result["lf_files"].append(join(search_path, d, f))
            elif f == "Kconfig.projbuild":
                result["kconfig_build_files"].append(join(search_path, d, f))
            elif f == "Kconfig":
                result["kconfig_files"].append(join(search_path, d, f))

    result["lf_files"].append(
        join(FRAMEWORK_DIR, "components", "esp32", "ld", "esp32_fragments.lf")
    )

    return result


def create_custom_libraries_list(orignial_ldgen_libraries_file, project_target_name):
    if not isfile(orignial_ldgen_libraries_file):
        sys.stderr.write("Error: Couldn't find the list of framework libraries\n")
        env.Exit(1)

    pio_libraries_file = orignial_ldgen_libraries_file + "_pio"

    if isfile(pio_libraries_file):
        return pio_libraries_file

    lib_paths = []
    with open(orignial_ldgen_libraries_file, "r") as fp:
        lib_paths = fp.readlines()

    with open(pio_libraries_file, "w") as fp:
        for lib_path in lib_paths:
            if "lib%s.a" % project_target_name.replace("__idf_", "") not in lib_path:
                fp.write(lib_path)

    return pio_libraries_file


def generate_project_ld_script(project_target_name):
    project_files = find_framework_service_files(join(FRAMEWORK_DIR, "components"))

    # Create a new file to avoid automatically generated library entry as files from
    # this library are built internally by PlatformIO
    libraries_list = create_custom_libraries_list(
        join(env.subst("$BUILD_DIR"), "ldgen_libraries"), project_target_name
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
    bootloader_env.Append(_LIBDIRFLAGS=" -Wl,--end-group")

    return bootloader_env.Program(
        join("$BUILD_DIR", "bootloader.elf"),
        bootloader_libs,
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


#
# Generate final linker script
#

if not env.BoardConfig().get("build.ldscript", ""):
    linker_script = env.Command(
        join("$BUILD_DIR", "esp32_out.ld"),
        env.BoardConfig().get(
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
partitions_csv = env.BoardConfig().get("build.partitions", "partitions_singleapp.csv")
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
            env.BoardConfig().get("upload.flash_size", "detect"),
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
    sys.stderr.write(
        (
            "Error: the 'src_filter' option cannot be used with ESP-IDF. Select source "
            "files to build in the project CMakeLists.txt file.\n"
        )
    )
    env.Exit(1)

#
# Initial targets loading
#

# By default 'main' folder is used to store source files. In case when a user has
# default 'src' folder we need to add this as an extra component. If there is no 'main'
# folder CMake won't generate dependencies properly

extra_components = []
if env.subst("$PROJECT_SRC_DIR") != join(env.subst("$PROJECT_DIR"), "main"):
    extra_components = [env.subst("$PROJECT_SRC_DIR")]
    if "arduino" in env.subst("$PIOFRAMEWORK"):
        extra_components.append(ARDUINO_FRAMEWORK_DIR)

print("Reading CMake configuration...")
project_codemodel = get_cmake_code_model(
    env.subst("$PROJECT_DIR"),
    BUILD_DIR,
    ["-DEXTRA_COMPONENT_DIRS:PATH=" + ";".join(extra_components)]
    if extra_components
    else [],
)

if not project_codemodel:
    sys.stderr.write("Error: Couldn't find code model generated by CMake\n")
    env.Exit(1)

target_configs = load_target_configurations(
    project_codemodel, join(BUILD_DIR, CMAKE_API_REPLY_PATH)
)

if all(t in target_configs for t in ("__idf_src", "__idf_main")):
    sys.stderr.write(
        (
            "Warning! Detected two different targets with project sources. Please use "
            "either 'src' or specify 'main' folder in 'platformio.ini' file.\n"
        )
    )
    env.Exit(1)


project_target_name = "__idf_main" if "__idf_main" in target_configs else "__idf_src"
if project_target_name not in target_configs:
    sys.stderr.write("Error: Couldn't find the main target of the project!\n")
    env.Exit(1)

project_ld_scipt = generate_project_ld_script(project_target_name)
env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", project_ld_scipt)

elf_config = get_project_elf(target_configs)
framework_components_map = get_components_map(
    target_configs, ["STATIC_LIBRARY", "OBJECT_LIBRARY"], [project_target_name]
)

build_components(env, framework_components_map, env.subst("$PROJECT_DIR"))

if not elf_config:
    sys.stderr.write("Error: Couldn't load the main firmware target of the project\n")
    env.Exit(1)

for component_config in framework_components_map.values():
    env.Depends(project_ld_scipt, component_config["lib"])

project_config = target_configs.get(project_target_name, {})
project_includes = get_app_includes(project_config)
project_defines = get_app_defines(project_config)
project_flags = get_app_flags(project_config)
link_args = extract_link_args(elf_config)

app_includes = get_app_includes(elf_config)

#
# Compile bootloader
#

env.Depends(
    "$BUILD_DIR/$PROGNAME$PROGSUFFIX",
    env.ElfToBin(join("$BUILD_DIR", "bootloader"), build_bootloader()),
)

#
# Target: ESP-IDF menuconfig
#

AlwaysBuild(
    env.Alias(
        "menuconfig", None, [env.VerboseAction(RunMenuconfig, "Running menuconfig...")]
    )
)

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

envsafe = env.Clone()
if project_target_name != "__idf_main":
    # Manually add dependencies to CPPPATH since ESP-IDF build system doesn't generate
    # this info if the folder with sources is not named 'main'
    # https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#rename-main
    envsafe.AppendUnique(CPPPATH=app_includes["plain_includes"])

# Add default include dirs to global CPPPATH so they're visible to PIOBUILDFILES
envsafe.Append(CPPPATH=["$PROJECT_INCLUDE_DIR", "$PROJECT_SRC_DIR"])

env.Replace(SRC_FILTER="-<*>")
env.Append(
    PIOBUILDFILES=compile_source_files(
        target_configs.get(project_target_name), envsafe, envsafe.subst("$PROJECT_DIR")
    )
)

project_flags.update(link_args)
env.MergeFlags(project_flags)
env.Prepend(
    CPPPATH=app_includes["plain_includes"],
    LINKFLAGS=extra_flags,
    LIBS=libs,
    FLASH_EXTRA_IMAGES=[
        ("0x1000", join("$BUILD_DIR", "bootloader.bin")),
        ("0x8000", join("$BUILD_DIR", "partitions.bin")),
    ],
)

#
# Compile ULP sources in 'ulp' folder
#

ulp_dir = join(env.subst("$PROJECT_DIR"), "ulp")
if isdir(ulp_dir) and listdir(ulp_dir):
    env.SConscript("ulp.py", exports="env project_config")
