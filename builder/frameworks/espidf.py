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
import os

import click

from SCons.Script import (
    ARGUMENTS,
    COMMAND_LINE_TARGETS,
    DefaultEnvironment,
)

from platformio import fs
from platformio.proc import exec_command
from platformio.util import get_systype
from platformio.builder.tools.piolib import ProjectAsLibBuilder

env = DefaultEnvironment()
env.SConscript("_embed_files.py", exports="env")

platform = env.PioPlatform()
board = env.BoardConfig()
mcu = board.get("build.mcu", "esp32")
idf_variant = board.get(
    "build.esp-idf.variant", "esp32s2beta" if mcu == "esp32s2" else "esp32"
)

FRAMEWORK_DIR = platform.get_package_dir("framework-espidf")
TOOLCHAIN_DIR = platform.get_package_dir(
    "toolchain-xtensa%s" % ("32s2" if mcu == "esp32s2" else "32")
)
assert os.path.isdir(FRAMEWORK_DIR)

# Arduino framework as a component is not compatible with ESP-IDF >=4.1
if "arduino" in env.subst("$PIOFRAMEWORK"):
    ARDUINO_FRAMEWORK_DIR = platform.get_package_dir("framework-arduinoespressif32")
    # Possible package names in 'package@version' format is not compatible with CMake
    if "@" in os.path.basename(ARDUINO_FRAMEWORK_DIR):
        new_path = os.path.join(
            os.path.dirname(ARDUINO_FRAMEWORK_DIR),
            os.path.basename(ARDUINO_FRAMEWORK_DIR).replace("@", "-"),
        )
        os.rename(ARDUINO_FRAMEWORK_DIR, new_path)
        ARDUINO_FRAMEWORK_DIR = new_path
    assert ARDUINO_FRAMEWORK_DIR and os.path.isdir(ARDUINO_FRAMEWORK_DIR)

BUILD_DIR = env.subst("$BUILD_DIR")
CMAKE_API_REPLY_PATH = os.path.join(".cmake", "api", "v1", "reply")

try:
    import future
    import pyparsing
    import cryptography
except ImportError:
    env.Execute(
        env.VerboseAction(
            '$PYTHONEXE -m pip install "cryptography>=2.1.4" "future>=0.15.2" "pyparsing>=2.0.3,<2.4.0" ',
            "Installing ESP-IDF's Python dependencies",
        )
    )

# a special "esp-windows-curses" python package is required on Windows for Menuconfig
if "windows" in get_systype():
    import pkg_resources

    if "esp-windows-curses" not in {pkg.key for pkg in pkg_resources.working_set}:
        env.Execute(
            env.VerboseAction(
                '$PYTHONEXE -m pip install "file://%s/tools/kconfig_new/esp-windows-curses" windows-curses'
                % FRAMEWORK_DIR,
                "Installing windows-curses package",
            )
        )


def get_project_lib_includes(env):
    project = ProjectAsLibBuilder(env, "$PROJECT_DIR")
    project.install_dependencies()
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
    cmake_cache_file = os.path.join(BUILD_DIR, "CMakeCache.txt")
    cmake_txt_files = [
        os.path.join(env.subst("$PROJECT_DIR"), "CMakeLists.txt"),
        os.path.join(env.subst("$PROJECT_SRC_DIR"), "CMakeLists.txt"),
    ]
    cmake_preconf_dir = os.path.join(BUILD_DIR, "config")
    sdkconfig = os.path.join(env.subst("$PROJECT_DIR"), "sdkconfig")

    for d in (cmake_api_reply_dir, cmake_preconf_dir):
        if not os.path.isdir(d) or not os.listdir(d):
            return True
    if not os.path.isfile(cmake_cache_file):
        return True
    if not os.path.isfile(os.path.join(BUILD_DIR, "build.ninja")):
        return True
    if os.path.isfile(sdkconfig) and os.path.getmtime(sdkconfig) > os.path.getmtime(
        cmake_cache_file
    ):
        return True
    if any(
        os.path.getmtime(f) > os.path.getmtime(cmake_cache_file)
        for f in cmake_txt_files + [cmake_preconf_dir]
    ):
        return True

    return False


def is_proper_idf_project():
    return all(
        os.path.isfile(path)
        for path in (
            os.path.join(env.subst("$PROJECT_DIR"), "CMakeLists.txt"),
            os.path.join(env.subst("$PROJECT_SRC_DIR"), "CMakeLists.txt"),
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
    return fs.to_unix_path(path)


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

    if not os.listdir(os.path.join(env.subst("$PROJECT_SRC_DIR"))):
        # create a default main file to make CMake happy during first init
        with open(os.path.join(env.subst("$PROJECT_SRC_DIR"), "main.c"), "w") as fp:
            fp.write("void app_main() {}")

    project_dir = env.subst("$PROJECT_DIR")
    if not os.path.isfile(os.path.join(project_dir, "CMakeLists.txt")):
        with open(os.path.join(project_dir, "CMakeLists.txt"), "w") as fp:
            fp.write(root_cmake_tpl % os.path.basename(project_dir))

    project_src_dir = env.subst("$PROJECT_SRC_DIR")
    if not os.path.isfile(os.path.join(project_src_dir, "CMakeLists.txt")):
        with open(os.path.join(project_src_dir, "CMakeLists.txt"), "w") as fp:
            fp.write(prj_cmake_tpl % normalize_path(env.subst("$PROJECT_SRC_DIR")))


def get_cmake_code_model(src_dir, build_dir, extra_args=None):
    cmake_api_dir = os.path.join(build_dir, ".cmake", "api", "v1")
    cmake_api_query_dir = os.path.join(cmake_api_dir, "query")
    cmake_api_reply_dir = os.path.join(cmake_api_dir, "reply")
    query_file = os.path.join(cmake_api_query_dir, "codemodel-v2")

    if not os.path.isfile(query_file):
        os.makedirs(os.path.dirname(query_file))
        open(query_file, "a").close()  # create an empty file

    if not is_proper_idf_project():
        create_default_project_files()

    if is_cmake_reconfigure_required(cmake_api_reply_dir):
        run_cmake(src_dir, build_dir, extra_args)

    if not os.path.isdir(cmake_api_reply_dir) or not os.listdir(cmake_api_reply_dir):
        sys.stderr.write("Error: Couldn't find CMake API response file\n")
        env.Exit(1)

    codemodel = {}
    for target in os.listdir(cmake_api_reply_dir):
        if target.startswith("codemodel-v2"):
            with open(os.path.join(cmake_api_reply_dir, target), "r") as fp:
                codemodel = json.load(fp)

    assert codemodel["version"]["major"] == 2
    return codemodel


def populate_idf_env_vars(idf_env):
    idf_env["IDF_PATH"] = FRAMEWORK_DIR
    additional_packages = [
        os.path.join(TOOLCHAIN_DIR, "bin"),
        os.path.join(platform.get_package_dir("toolchain-%sulp" % mcu), "bin"),
        platform.get_package_dir("tool-ninja"),
        os.path.join(platform.get_package_dir("tool-cmake"), "bin"),
        os.path.dirname(env.subst("$PYTHONEXE")),
    ]

    if "windows" in get_systype():
        additional_packages.append(platform.get_package_dir("tool-mconf"))

    idf_env["PATH"] = os.pathsep.join(additional_packages + [idf_env["PATH"]])


def get_target_config(project_configs, target_index, cmake_api_reply_dir):
    target_json = project_configs.get("targets")[target_index].get("jsonFile", "")
    target_config_file = os.path.join(cmake_api_reply_dir, target_json)
    if not os.path.isfile(target_config_file):
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
        lib_path = os.path.join(prepend_dir, lib_path)
    lib_objects = compile_source_files(
        lib_config, default_env, project_src_dir, prepend_dir
    )
    return default_env.Library(
        target=os.path.join("$BUILD_DIR", lib_path, lib_name), source=lib_objects
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
            elif os.path.isfile(fragment) and os.path.isabs(fragment):
                # In case of precompiled archives from framework package
                lib_path = os.path.dirname(fragment)
                if lib_path not in link_args["LIBPATH"]:
                    link_args["LIBPATH"].append(os.path.dirname(fragment))
                link_args["LIBS"].extend(
                    [os.path.basename(lib) for lib in args if lib.endswith(".a")]
                )
            elif fragment.endswith(".a"):
                link_args["__LIB_DEPS"].extend(
                    [os.path.basename(lib) for lib in args if lib.endswith(".a")]
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
    config_path = os.path.join(BUILD_DIR, "config", "sdkconfig.json")
    if not os.path.isfile(config_path):
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
    for d in os.listdir(search_path):
        path = os.path.join(search_path, d)
        if not os.path.isdir(path):
            continue
        for f in os.listdir(path):
            # Skip hardware specific files as they will be added later
            if f == "linker.lf" and not os.path.basename(path).startswith("esp32"):
                result["lf_files"].append(os.path.join(path, f))
            elif f == "Kconfig.projbuild":
                result["kconfig_build_files"].append(os.path.join(path, f))
            elif f == "Kconfig":
                result["kconfig_files"].append(os.path.join(path, f))

    result["lf_files"].extend(
        [
            os.path.join(
                FRAMEWORK_DIR,
                "components",
                idf_variant,
                "ld",
                "%s_fragments.lf" % idf_variant,
            ),
            os.path.join(
                FRAMEWORK_DIR,
                "components",
                idf_variant,
                "linker.lf",
            ),
            os.path.join(FRAMEWORK_DIR, "components", "newlib", "newlib.lf"),
        ]
    )

    if sdk_config.get("SPIRAM_CACHE_WORKAROUND", False):
        result["lf_files"].append(
            os.path.join(
                FRAMEWORK_DIR, "components", "newlib", "esp32-spiram-rom-functions-c.lf"
            )
        )

    return result


def create_custom_libraries_list(ldgen_libraries_file, ignore_targets):
    if not os.path.isfile(ldgen_libraries_file):
        sys.stderr.write("Error: Couldn't find the list of framework libraries\n")
        env.Exit(1)

    pio_libraries_file = ldgen_libraries_file + "_pio"

    if os.path.isfile(pio_libraries_file):
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
        os.path.join(FRAMEWORK_DIR, "components"), sdk_config
    )

    # Create a new file to avoid automatically generated library entry as files from
    # this library are built internally by PlatformIO
    libraries_list = create_custom_libraries_list(
        os.path.join(BUILD_DIR, "ldgen_libraries"), ignore_targets
    )

    args = {
        "script": os.path.join(FRAMEWORK_DIR, "tools", "ldgen", "ldgen.py"),
        "config": os.path.join(env.subst("$PROJECT_DIR"), "sdkconfig"),
        "fragments": " ".join(['"%s"' % f for f in project_files.get("lf_files")]),
        "kconfig": os.path.join(FRAMEWORK_DIR, "Kconfig"),
        "env_file": os.path.join("$BUILD_DIR", "config.env"),
        "libraries_list": libraries_list,
        "objdump": os.path.join(
            TOOLCHAIN_DIR, "bin", env.subst("$CC").replace("-gcc", "-objdump"),
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
        os.path.join("$BUILD_DIR", "%s.project.ld" % idf_variant),
        os.path.join(
            FRAMEWORK_DIR,
            "components",
            idf_variant,
            "ld",
            "%s.project.ld.in" % idf_variant,
        ),
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
            if not os.path.isabs(src_path):
                # For cases when sources are located near CMakeLists.txt
                src_path = os.path.join(project_src_dir, src_path)
            local_path = config["paths"]["source"]
            if not os.path.isabs(local_path):
                local_path = os.path.join(project_src_dir, config["paths"]["source"])
            obj_path = os.path.join(
                "$BUILD_DIR", prepend_dir or "", config["paths"]["build"]
            )
            objects.append(
                build_envs[compile_group_idx].StaticObject(
                    target=os.path.join(
                        obj_path, os.path.relpath(src_path, local_path) + ".o"
                    ),
                    source=os.path.realpath(src_path),
                )
            )

    return objects


def run_tool(cmd):
    idf_env = os.environ.copy()
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
    idf_env = os.environ.copy()
    populate_idf_env_vars(idf_env)

    rc = subprocess.call(
        [
            os.path.join(platform.get_package_dir("tool-cmake"), "bin", "cmake"),
            "--build",
            BUILD_DIR,
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
        os.path.join(platform.get_package_dir("tool-cmake") or "", "bin", "cmake"),
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
    bootloader_src_dir = os.path.join(
        FRAMEWORK_DIR, "components", "bootloader", "subproject"
    )
    code_model = get_cmake_code_model(
        bootloader_src_dir,
        os.path.join(BUILD_DIR, "bootloader"),
        [
            "-DIDF_TARGET=" + idf_variant,
            "-DPYTHON_DEPS_CHECKED=1",
            "-DPYTHON=" + env.subst("$PYTHONEXE"),
            "-DIDF_PATH=" + FRAMEWORK_DIR,
            "-DSDKCONFIG=" + os.path.join(env.subst("$PROJECT_DIR"), "sdkconfig"),
            "-DLEGACY_INCLUDE_COMMON_HEADERS=",
            "-DEXTRA_COMPONENT_DIRS="
            + os.path.join(FRAMEWORK_DIR, "components", "bootloader"),
        ],
    )

    if not code_model:
        sys.stderr.write("Error: Couldn't find code model for bootloader\n")
        env.Exit(1)

    target_configs = load_target_configurations(
        code_model,
        os.path.join(BUILD_DIR, "bootloader", ".cmake", "api", "v1", "reply"),
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
        os.path.join("$BUILD_DIR", "bootloader"),
        bootloader_env.Program(
            os.path.join("$BUILD_DIR", "bootloader.elf"), bootloader_libs
        ),
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
    dummy_component_path = os.path.join(BUILD_DIR, "__pio_env")
    if not os.path.isdir(dummy_component_path):
        os.makedirs(dummy_component_path)

    for ext in (".cpp", ".c", ".S"):
        dummy_file = os.path.join(dummy_component_path, "__dummy" + ext)
        if not os.path.isfile(dummy_file):
            open(dummy_file, "a").close()

    component_cmake = os.path.join(dummy_component_path, "CMakeLists.txt")
    if not os.path.isfile(component_cmake):
        with open(component_cmake, "w") as fp:
            fp.write(prj_cmake_tpl)

    return dummy_component_path


def find_default_component(target_configs):
    for config in target_configs:
        if "__pio_env" in config:
            return config
    return ""


def create_verion_file():
    version_file = os.path.join(FRAMEWORK_DIR, "version.txt")
    if not os.path.isfile(version_file):
        with open(version_file, "w") as fp:
            fp.write(platform.get_package_version("framework-espidf"))


#
# ESP-IDF package doesn't contain .git folder, instead package version is specified
# in a special file "version.h" in the root folder of the package
#

create_verion_file()

#
# Generate final linker script
#

if not board.get("build.ldscript", ""):
    linker_script = env.Command(
        os.path.join("$BUILD_DIR", "%s_out.ld" % idf_variant),
        board.get(
            "build.esp-idf.ldscript",
            os.path.join(
                FRAMEWORK_DIR, "components", idf_variant, "ld", "%s.ld" % idf_variant
            ),
        ),
        env.VerboseAction(
            '$CC -I"$BUILD_DIR/config" -C -P -x  c -E $SOURCE -o $TARGET',
            "Generating LD script $TARGET",
        ),
    )

    env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", linker_script)
    env.Replace(LDSCRIPT_PATH="%s_out.ld" % idf_variant)

#
# Generate partition table
#

fwpartitions_dir = os.path.join(FRAMEWORK_DIR, "components", "partition_table")
partitions_csv = board.get("build.partitions", "partitions_singleapp.csv")
env.Replace(
    PARTITIONS_TABLE_CSV=os.path.abspath(
        os.path.join(fwpartitions_dir, partitions_csv)
        if os.path.isfile(os.path.join(fwpartitions_dir, partitions_csv))
        else partitions_csv
    )
)

partition_table = env.Command(
    os.path.join("$BUILD_DIR", "partitions.bin"),
    "$PARTITIONS_TABLE_CSV",
    env.VerboseAction(
        '"$PYTHONEXE" "%s" -q --flash-size "%s" $SOURCE $TARGET'
        % (
            os.path.join(
                FRAMEWORK_DIR, "components", "partition_table", "gen_esp32part.py"
            ),
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

if os.path.isfile(os.path.join(env.subst("$PROJECT_SRC_DIR"), "sdkconfig.h")):
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
if env.subst("$PROJECT_SRC_DIR") != os.path.join(env.subst("$PROJECT_DIR"), "main"):
    extra_components.append(env.subst("$PROJECT_SRC_DIR"))
    if "arduino" in env.subst("$PIOFRAMEWORK"):
        extra_components.append(ARDUINO_FRAMEWORK_DIR)

print("Reading CMake configuration...")
project_codemodel = get_cmake_code_model(
    env.subst("$PROJECT_DIR"),
    BUILD_DIR,
    [
        "-DIDF_TARGET=" + idf_variant,
        "-DEXTRA_COMPONENT_DIRS:PATH=" + ";".join(extra_components),
    ]
    + click.parser.split_arg_string(board.get("build.cmake_extra_args", "")),
)

if not project_codemodel:
    sys.stderr.write("Error: Couldn't find code model generated by CMake\n")
    env.Exit(1)

target_configs = load_target_configurations(
    project_codemodel, os.path.join(BUILD_DIR, CMAKE_API_REPLY_PATH)
)

sdk_config = get_sdk_configuration()


project_target_name = "__idf_%s" % os.path.basename(env.subst("$PROJECT_SRC_DIR"))
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
    sdk_config, [project_target_name, "__pio_env"]
)
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

env.AddPlatformTarget(
    "menuconfig",
    None,
    [env.VerboseAction(RunMenuconfig, "Running menuconfig...")],
    "Run Menuconfig",
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
    ld_index = extra_flags.index("%s_out.ld" % idf_variant)
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
        ("0x1000", os.path.join("$BUILD_DIR", "bootloader.bin")),
        ("0x8000", os.path.join("$BUILD_DIR", "partitions.bin")),
    ],
)

#
# To embed firmware checksum a special argument for esptool.py is required
#

action = copy.deepcopy(env["BUILDERS"]["ElfToBin"].action)
action.cmd_list = env["BUILDERS"]["ElfToBin"].action.cmd_list.replace(
    "-o", "--elf-sha256-offset 0xb0 -o"
)
env["BUILDERS"]["ElfToBin"].action = action

#
# Compile ULP sources in 'ulp' folder
#

ulp_dir = os.path.join(env.subst("$PROJECT_DIR"), "ulp")
if os.path.isdir(ulp_dir) and os.listdir(ulp_dir):
    env.SConscript("ulp.py", exports="env project_config idf_variant")
