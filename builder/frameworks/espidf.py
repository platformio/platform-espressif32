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
import semantic_version

from SCons.Script import (
    ARGUMENTS,
    COMMAND_LINE_TARGETS,
    DefaultEnvironment,
)

from platformio import fs
from platformio.proc import exec_command
from platformio.util import get_systype
from platformio.builder.tools.piolib import ProjectAsLibBuilder
from platformio.package.version import get_original_version, pepver_to_semver

env = DefaultEnvironment()
env.SConscript("_embed_files.py", exports="env")

platform = env.PioPlatform()
board = env.BoardConfig()
mcu = board.get("build.mcu", "esp32")
idf_variant = mcu.lower()

FRAMEWORK_DIR = platform.get_package_dir("framework-espidf")
TOOLCHAIN_DIR = platform.get_package_dir(
    "toolchain-%s"
    % (
        "riscv32-esp"
        if mcu == "esp32c3"
        else ("xtensa-%s" % mcu)
    )
)

# Legacy toolchains for mixed IDF/Arduino projects
if "arduino" in env.subst("$PIOFRAMEWORK"):
    TOOLCHAIN_DIR = platform.get_package_dir("toolchain-xtensa32")

assert os.path.isdir(FRAMEWORK_DIR)
assert os.path.isdir(TOOLCHAIN_DIR)

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
PROJECT_DIR = env.subst("$PROJECT_DIR")
PROJECT_SRC_DIR = env.subst("$PROJECT_SRC_DIR")
CMAKE_API_REPLY_PATH = os.path.join(".cmake", "api", "v1", "reply")
SDKCONFIG_PATH = board.get(
    "build.esp-idf.sdkconfig_path",
    os.path.join(PROJECT_DIR, "sdkconfig.%s" % env.subst("$PIOENV")),
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
        os.path.join(PROJECT_DIR, "CMakeLists.txt"),
        os.path.join(PROJECT_SRC_DIR, "CMakeLists.txt"),
    ]
    cmake_preconf_dir = os.path.join(BUILD_DIR, "config")
    deafult_sdk_config = os.path.join(PROJECT_DIR, "sdkconfig.defaults")

    for d in (cmake_api_reply_dir, cmake_preconf_dir):
        if not os.path.isdir(d) or not os.listdir(d):
            return True
    if not os.path.isfile(cmake_cache_file):
        return True
    if not os.path.isfile(os.path.join(BUILD_DIR, "build.ninja")):
        return True
    if not os.path.isfile(SDKCONFIG_PATH) or os.path.getmtime(
        SDKCONFIG_PATH
    ) > os.path.getmtime(cmake_cache_file):
        return True
    if os.path.isfile(deafult_sdk_config) and os.path.getmtime(
        deafult_sdk_config
    ) > os.path.getmtime(cmake_cache_file):
        return True
    if any(
        os.path.getmtime(f) > os.path.getmtime(cmake_cache_file)
        for f in cmake_txt_files + [cmake_preconf_dir, FRAMEWORK_DIR]
    ):
        return True

    return False


def is_proper_idf_project():
    return all(
        os.path.isfile(path)
        for path in (
            os.path.join(PROJECT_DIR, "CMakeLists.txt"),
            os.path.join(PROJECT_SRC_DIR, "CMakeLists.txt"),
        )
    )


def collect_src_files():
    return [
        f
        for f in env.MatchSourceFiles("$PROJECT_SRC_DIR", env.get("SRC_FILTER"))
        if not f.endswith((".h", ".hpp"))
    ]


def normalize_path(path):
    if PROJECT_DIR in path:
        path = path.replace(PROJECT_DIR, "${CMAKE_SOURCE_DIR}")
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

    if not os.listdir(PROJECT_SRC_DIR):
        # create a default main file to make CMake happy during first init
        with open(os.path.join(PROJECT_SRC_DIR, "main.c"), "w") as fp:
            fp.write("void app_main() {}")

    project_dir = PROJECT_DIR
    if not os.path.isfile(os.path.join(project_dir, "CMakeLists.txt")):
        with open(os.path.join(project_dir, "CMakeLists.txt"), "w") as fp:
            fp.write(root_cmake_tpl % os.path.basename(project_dir))

    project_src_dir = PROJECT_SRC_DIR
    if not os.path.isfile(os.path.join(project_src_dir, "CMakeLists.txt")):
        with open(os.path.join(project_src_dir, "CMakeLists.txt"), "w") as fp:
            fp.write(prj_cmake_tpl % normalize_path(PROJECT_SRC_DIR))


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
        platform.get_package_dir("tool-ninja"),
        os.path.join(platform.get_package_dir("tool-cmake"), "bin"),
        os.path.dirname(env.subst("$PYTHONEXE")),
    ]

    if mcu != "esp32c3":
        additional_packages.append(
            os.path.join(
                platform.get_package_dir(
                    "toolchain-%sulp"
                    % ("esp32s2" if mcu == "esp32s3" else mcu)
                ),
                "bin"
              ),
        )

    if "windows" in get_systype():
        additional_packages.append(platform.get_package_dir("tool-mconf"))

    idf_env["PATH"] = os.pathsep.join(additional_packages + [idf_env["PATH"]])

    # Some users reported that the `IDF_TOOLS_PATH` var can seep into the
    # underlying build system. Unsetting it is a safe workaround.
    if "IDF_TOOLS_PATH" in idf_env:
        del idf_env["IDF_TOOLS_PATH"]


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
    def _add_to_libpath(lib_path, link_args):
        if lib_path not in link_args["LIBPATH"]:
            link_args["LIBPATH"].append(lib_path)

    def _add_archive(archive_path, link_args):
        archive_name = os.path.basename(archive_path)
        if archive_name not in link_args["LIBS"]:
            _add_to_libpath(os.path.dirname(archive_path), link_args)
            link_args["LIBS"].append(archive_name)

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
                lib_path = fragment.replace("-L", "").strip().strip('"')
                _add_to_libpath(lib_path, link_args)
            elif fragment.startswith("-") and not fragment.startswith("-l"):
                # CMake mistakenly marks LINKFLAGS as libraries
                link_args["LINKFLAGS"].extend(args)
            elif fragment.endswith(".a"):
                archive_path = fragment
                # process static archives
                if archive_path.startswith(FRAMEWORK_DIR):
                    # In case of precompiled archives from framework package
                    _add_archive(archive_path, link_args)
                else:
                    # In case of archives within project
                    if archive_path.startswith(".."):
                        # Precompiled archives from project component
                        _add_archive(
                            os.path.normpath(os.path.join(BUILD_DIR, archive_path)),
                            link_args,
                        )
                    else:
                        # Internally built libraries used for dependency resolution
                        link_args["__LIB_DEPS"].append(os.path.basename(archive_path))

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
            if f == "linker.lf" and not os.path.basename(path).startswith(
                ("esp32", "riscv")
            ):
                result["lf_files"].append(os.path.join(path, f))
            elif f == "Kconfig.projbuild":
                result["kconfig_build_files"].append(os.path.join(path, f))
            elif f == "Kconfig":
                result["kconfig_files"].append(os.path.join(path, f))

    if mcu == "esp32c3":
        result["lf_files"].append(
            os.path.join(FRAMEWORK_DIR, "components", "riscv", "linker.lf")
        )

    result["lf_files"].extend(
        [
            os.path.join(FRAMEWORK_DIR, "components", "esp_common", "common.lf"),
            os.path.join(FRAMEWORK_DIR, "components", "esp_common", "soc.lf"),
            os.path.join(FRAMEWORK_DIR, "components", "esp_system", "app.lf"),
            os.path.join(FRAMEWORK_DIR, "components", "newlib", "newlib.lf"),
            os.path.join(FRAMEWORK_DIR, "components", "newlib", "system_libs.lf"),
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
    # Rework the memory template linker script, following components/esp_system/ld.cmake
    args = {
        "preprocess" : os.path.join(
            TOOLCHAIN_DIR,
            "bin",
            env.subst("$CC")),
        "ld_output": os.path.join("$BUILD_DIR", "memory.ld"),
        "ld_dir": os.path.join(
            FRAMEWORK_DIR,
            "components",
            "esp_system",
            "ld"),
        "ld_input": os.path.join(
            FRAMEWORK_DIR,
            "components",
            "esp_system",
            "ld",
            idf_variant,
            "memory.ld.in",
        ),
        "config": os.path.join("$BUILD_DIR", "config"),
        "flags" : '-C -P -x c -E -o '
    }

    cmd = (
        '"{preprocess}" {flags} "{ld_output}" -I "{config}" -I "{ld_dir}" "{ld_input}"'
    ).format(**args)

    env.Command(
        os.path.join("$BUILD_DIR", "memory.ld"),
        os.path.join(
            FRAMEWORK_DIR,
            "components",
            "esp_system",
            "ld",
            idf_variant,
            "memory.ld.in",
        ),
        env.VerboseAction(cmd, "Generating memory linker script $TARGET"),
    )

    args = {
        "script": os.path.join(FRAMEWORK_DIR, "tools", "ldgen", "ldgen.py"),
        "config": SDKCONFIG_PATH,
        "fragments": " ".join(['"%s"' % f for f in project_files.get("lf_files")]),
        "kconfig": os.path.join(FRAMEWORK_DIR, "Kconfig"),
        "env_file": os.path.join("$BUILD_DIR", "config.env"),
        "libraries_list": libraries_list,
        "objdump": os.path.join(
            TOOLCHAIN_DIR,
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
        os.path.join("$BUILD_DIR", "sections.ld"),
        os.path.join(
            FRAMEWORK_DIR,
            "components",
            "esp_system",
            "ld",
            idf_variant,
            "sections.ld.in",
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
    components_dir = fs.to_unix_path(os.path.join(FRAMEWORK_DIR, "components"))
    for source in config.get("sources", []):
        if source["path"].endswith(".rule"):
            continue
        compile_group_idx = source.get("compileGroupIndex")
        if compile_group_idx is not None:
            src_dir = config["paths"]["source"]
            if not os.path.isabs(src_dir):
                src_dir = os.path.join(project_src_dir, config["paths"]["source"])
            src_path = source.get("path")
            if not os.path.isabs(src_path):
                # For cases when sources are located near CMakeLists.txt
                src_path = os.path.join(project_src_dir, src_path)

            obj_path = os.path.join("$BUILD_DIR", prepend_dir or "")
            if src_path.startswith(components_dir):
                obj_path = os.path.join(
                    obj_path, os.path.relpath(src_path, components_dir)
                )
            else:
                if not os.path.isabs(source["path"]):
                    obj_path = os.path.join(obj_path, source["path"])
                else:
                    obj_path = os.path.join(obj_path, os.path.basename(src_path))

            objects.append(
                build_envs[compile_group_idx].StaticObject(
                    target=os.path.splitext(obj_path)[0] + ".o",
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

def fix_ld_paths(extra_flags):
    peripheral_framework_path = os.path.join(FRAMEWORK_DIR, "components", "soc", idf_variant, "ld")
    rom_framework_path = os.path.join(FRAMEWORK_DIR, "components", "esp_rom", idf_variant, "ld")
    bl_framework_path = os.path.join(FRAMEWORK_DIR, "components", "bootloader", "subproject", "main", "ld", idf_variant)

    # ESP linker scripts changed path in ESP-IDF 4.4+, so add missing paths to linker's search path
    try:
        ld_index = extra_flags.index("%s.peripherals.ld" % idf_variant)
        extra_flags[ld_index-1:ld_index-1] = [ "-L", peripheral_framework_path, "-L", rom_framework_path, "-L", bl_framework_path]
    except:
        print("Error while parsing the flags")

    return extra_flags


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
            "-DSDKCONFIG=" + SDKCONFIG_PATH,
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
    extra_flags = fix_ld_paths(extra_flags)
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


def create_version_file():
    version_file = os.path.join(FRAMEWORK_DIR, "version.txt")
    if not os.path.isfile(version_file):
        with open(version_file, "w") as fp:
            package_version = platform.get_package_version("framework-espidf")
            fp.write(get_original_version(package_version) or package_version)


def generate_empty_partition_image(binary_path, image_size):
    empty_partition = env.Command(
        binary_path,
        None,
        env.VerboseAction(
            '"$PYTHONEXE" "%s" %s $TARGET'
            % (
                os.path.join(
                    FRAMEWORK_DIR,
                    "components",
                    "partition_table",
                    "gen_empty_partition.py",
                ),
                image_size,
            ),
            "Generating an empty partition $TARGET",
        ),
    )

    env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", empty_partition)


def get_partition_info(pt_path, pt_offset, pt_params):
    if not os.path.isfile(pt_path):
        sys.stderr.write(
            "Missing partition table file `%s`\n" % os.path.basename(pt_path)
        )
        env.Exit(1)

    cmd = [
        env.subst("$PYTHONEXE"),
        os.path.join(FRAMEWORK_DIR, "components", "partition_table", "parttool.py"),
        "-q",
        "--partition-table-offset",
        hex(pt_offset),
        "--partition-table-file",
        pt_path,
        "get_partition_info",
        "--info",
        "size",
        "offset",
    ]

    if pt_params["name"] == "boot":
        cmd.append("--partition-boot-default")
    else:
        cmd.extend(
            [
                "--partition-type",
                pt_params["type"],
                "--partition-subtype",
                pt_params["subtype"],
            ]
        )

    result = exec_command(cmd)
    if result["returncode"] != 0:
        sys.stderr.write(
            "Couldn't extract information for %s/%s from the partition table\n"
            % (pt_params["type"], pt_params["subtype"])
        )
        sys.stderr.write(result["out"] + "\n")
        sys.stderr.write(result["err"] + "\n")
        env.Exit(1)

    size = offset = 0
    if result["out"].strip():
        size, offset = result["out"].strip().split(" ", 1)

    return {"size": size, "offset": offset}


def get_app_partition_offset(pt_table, pt_offset):
    # Get the default boot partition offset
    app_params = get_partition_info(pt_table, pt_offset, {"name": "boot"})
    return app_params.get("offset", "0x10000")


def generate_mbedtls_bundle(sdk_config):
    bundle_path = os.path.join("$BUILD_DIR", "x509_crt_bundle")
    if os.path.isfile(env.subst(bundle_path)):
        return

    default_crt_dir = os.path.join(
        FRAMEWORK_DIR, "components", "mbedtls", "esp_crt_bundle"
    )

    cmd = [env.subst("$PYTHONEXE"), os.path.join(default_crt_dir, "gen_crt_bundle.py")]

    crt_args = ["--input"]
    if sdk_config.get("MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL", False):
        crt_args.append(os.path.join(default_crt_dir, "cacrt_all.pem"))
    elif sdk_config.get("MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN", False):
        crt_args.append(os.path.join(default_crt_dir, "cacrt_all.pem"))
        cmd.extend(
            ["--filter", os.path.join(default_crt_dir, "cmn_crt_authorities.csv")]
        )

    if sdk_config.get("MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE", False):
        cert_path = sdk_config.get("MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH", "")
        if os.path.isfile(cert_path) or os.path.isdir(cert_path):
            crt_args.append(os.path.abspath(cert_path))
        else:
            print("Warning! Couldn't find custom certificate bundle %s" % cert_path)

    crt_args.append("-q")

    # Use exec_command to change working directory
    exec_command(cmd + crt_args, cwd=BUILD_DIR)
    bundle_path = os.path.join("$BUILD_DIR", "x509_crt_bundle")
    env.Execute(
        env.VerboseAction(
            " ".join(
                [
                    os.path.join(
                        env.PioPlatform().get_package_dir("tool-cmake"),
                        "bin",
                        "cmake",
                    ),
                    "-DDATA_FILE=" + bundle_path,
                    "-DSOURCE_FILE=%s.S" % bundle_path,
                    "-DFILE_TYPE=BINARY",
                    "-P",
                    os.path.join(
                        FRAMEWORK_DIR,
                        "tools",
                        "cmake",
                        "scripts",
                        "data_file_embed_asm.cmake",
                    ),
                ]
            ),
            "Generating assembly for certificate bundle...",
        )
    )


def install_python_deps():
    def _get_installed_pip_packages():
        result = {}
        packages = {}
        pip_output = subprocess.check_output(
            [env.subst("$PYTHONEXE"), "-m", "pip", "list", "--format=json"]
        )
        try:
            packages = json.loads(pip_output)
        except:
            print("Warning! Couldn't extract the list of installed Python packages.")
            return {}
        for p in packages:
            result[p["name"]] = pepver_to_semver(p["version"])

        return result

    deps = {
        # https://github.com/platformio/platform-espressif32/issues/635
        "cryptography": ">=2.1.4,<35.0.0",
        "future": ">=0.15.2",
        "pyparsing": ">=2.0.3,<2.4.0",
        "kconfiglib": "==13.7.1",
    }

    installed_packages = _get_installed_pip_packages()
    packages_to_install = []
    for package, spec in deps.items():
        if package not in installed_packages:
            packages_to_install.append(package)
        else:
            version_spec = semantic_version.Spec(spec)
            if not version_spec.match(installed_packages[package]):
                packages_to_install.append(package)

    if packages_to_install:
        env.Execute(
            env.VerboseAction(
                (
                    '"$PYTHONEXE" -m pip install -U --force-reinstall '
                    + " ".join(['"%s%s"' % (p, deps[p]) for p in packages_to_install])
                ),
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


#
# ESP-IDF requires Python packages with specific versions
#

install_python_deps()


# ESP-IDF package doesn't contain .git folder, instead package version is specified
# in a special file "version.h" in the root folder of the package

create_version_file()

#
# Generate final linker script
#

if not board.get("build.ldscript", ""):
    linker_script = env.Command(
        os.path.join("$BUILD_DIR", "memory.ld"),
        board.get(
            "build.esp-idf.ldscript",
            os.path.join(
                FRAMEWORK_DIR, "components", "esp_system", "ld", idf_variant, "memory.ld.in"
            ),
        ),
        env.VerboseAction(
            '$CC -I"$BUILD_DIR/config" -I"' +
            os.path.join(FRAMEWORK_DIR, "components", "esp_system", "ld") +
            '" -C -P -x  c -E $SOURCE -o $TARGET',
            "Generating LD script $TARGET",
        ),
    )

    env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", linker_script)
    env.Replace(LDSCRIPT_PATH="memory.ld")

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

if not os.path.isdir(PROJECT_SRC_DIR):
    sys.stderr.write(
        "Error: Missing the `%s` folder with project sources.\n"
        % os.path.basename(PROJECT_SRC_DIR)
    )
    env.Exit(1)

if env.subst("$SRC_FILTER"):
    print(
        (
            "Warning: the 'src_filter' option cannot be used with ESP-IDF. Select source "
            "files to build in the project CMakeLists.txt file.\n"
        )
    )

if os.path.isfile(os.path.join(PROJECT_SRC_DIR, "sdkconfig.h")):
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
if PROJECT_SRC_DIR != os.path.join(PROJECT_DIR, "main"):
    extra_components.append(PROJECT_SRC_DIR)
if "arduino" in env.subst("$PIOFRAMEWORK"):
    print(
        "Warning! Arduino framework as an ESP-IDF component doesn't handle "
        "the `variant` field! The default `esp32` variant will be used."
    )
    extra_components.append(ARDUINO_FRAMEWORK_DIR)

print("Reading CMake configuration...")
project_codemodel = get_cmake_code_model(
    PROJECT_DIR,
    BUILD_DIR,
    [
        "-DIDF_TARGET=" + idf_variant,
        "-DPYTHON_DEPS_CHECKED=1",
        "-DEXTRA_COMPONENT_DIRS:PATH=" + ";".join(extra_components),
        "-DPYTHON=" + env.subst("$PYTHONEXE"),
        "-DSDKCONFIG=" + SDKCONFIG_PATH,
    ]
    + click.parser.split_arg_string(board.get("build.cmake_extra_args", "")),
)

# At this point the sdkconfig file should be generated by the underlying build system
assert os.path.isfile(SDKCONFIG_PATH), (
    "Missing auto-generated SDK configuration file `%s`" % SDKCONFIG_PATH
)

if not project_codemodel:
    sys.stderr.write("Error: Couldn't find code model generated by CMake\n")
    env.Exit(1)

target_configs = load_target_configurations(
    project_codemodel, os.path.join(BUILD_DIR, CMAKE_API_REPLY_PATH)
)

sdk_config = get_sdk_configuration()

project_target_name = "__idf_%s" % os.path.basename(PROJECT_SRC_DIR)
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

build_components(env, framework_components_map, PROJECT_DIR)

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
extra_flags = fix_ld_paths(extra_flags)
link_args["LINKFLAGS"] = sorted(list(set(link_args["LINKFLAGS"]) - set(extra_flags)))

# remove the main linker script flags '-T memory.ld' since it already appears later on
try:
    ld_index = extra_flags.index("memory.ld")
    extra_flags.pop(ld_index)
    extra_flags.pop(ld_index - 1)
    pass
except:
    print("Warning! Couldn't find the main linker script in the CMake code model.")

#
# Process project sources
#


# Remove project source files from following build stages as they're
# built as part of the framework
def _skip_prj_source_files(node):
    if node.srcnode().get_path().lower().startswith(PROJECT_SRC_DIR.lower()):
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

partition_table_offset = sdk_config.get("PARTITION_TABLE_OFFSET", 0x8000)
project_flags.update(link_args)
env.MergeFlags(project_flags)
env.Prepend(
    CPPPATH=app_includes["plain_includes"],
    CPPDEFINES=project_defines,
    LINKFLAGS=extra_flags,
    LIBS=libs,
    FLASH_EXTRA_IMAGES=[
        (
            board.get(
                "upload.bootloader_offset", "0x0" if mcu == "esp32c3" else "0x1000"
            ),
            os.path.join("$BUILD_DIR", "bootloader.bin"),
        ),
        (
            board.get("upload.partition_table_offset", hex(partition_table_offset)),
            os.path.join("$BUILD_DIR", "partitions.bin"),
        ),
    ],
)

#
# Generate mbedtls bundle
#

if sdk_config.get("MBEDTLS_CERTIFICATE_BUNDLE", False):
    generate_mbedtls_bundle(sdk_config)

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

ulp_dir = os.path.join(PROJECT_DIR, "ulp")
if os.path.isdir(ulp_dir) and os.listdir(ulp_dir) and mcu != "esp32c3":
    env.SConscript("ulp.py", exports="env sdk_config project_config idf_variant")

#
# Process OTA partition and image
#

ota_partition_params = get_partition_info(
    env.subst("$PARTITIONS_TABLE_CSV"),
    partition_table_offset,
    {"name": "ota", "type": "data", "subtype": "ota"},
)

if ota_partition_params["size"] and ota_partition_params["offset"]:
    # Generate an empty image if OTA is enabled in partition table
    ota_partition_image = os.path.join("$BUILD_DIR", "ota_data_initial.bin")
    generate_empty_partition_image(ota_partition_image, ota_partition_params["size"])

    env.Append(
        FLASH_EXTRA_IMAGES=[
            (
                board.get(
                    "upload.ota_partition_offset", ota_partition_params["offset"]
                ),
                ota_partition_image,
            )
        ]
    )

#
# Configure application partition offset
#

env.Replace(
    ESP32_APP_OFFSET=get_app_partition_offset(
        env.subst("$PARTITIONS_TABLE_CSV"), partition_table_offset
    )
)

# Propagate application offset to debug configurations
env["IDE_EXTRA_DATA"].update({"application_offset": env.subst("$ESP32_APP_OFFSET")})
