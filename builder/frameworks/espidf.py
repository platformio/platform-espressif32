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
import shutil
import os
from os.path import join
import re
import requests
import platform as sys_platform

import click
import semantic_version

from SCons.Script import (
    ARGUMENTS,
    COMMAND_LINE_TARGETS,
    DefaultEnvironment,
)

from platformio import fs
from platformio.compat import IS_WINDOWS
from platformio.proc import exec_command
from platformio.builder.tools.piolib import ProjectAsLibBuilder
from platformio.project.config import ProjectConfig
from platformio.package.version import get_original_version, pepver_to_semver


env = DefaultEnvironment()
env.SConscript("_embed_files.py", exports="env")

# remove maybe existing old map file in project root
map_file = os.path.join(env.subst("$PROJECT_DIR"), env.subst("$PROGNAME") + ".map")
if os.path.exists(map_file):
    os.remove(map_file)

# Allow changes in folders of managed components
os.environ["IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS"] = "1"

platform = env.PioPlatform()
config = env.GetProjectConfig()
board = env.BoardConfig()
mcu = board.get("build.mcu", "esp32")
flash_speed = board.get("build.f_flash", "40000000L")
flash_frequency = str(flash_speed.replace("000000L", "m"))
flash_mode = board.get("build.flash_mode", "dio")
idf_variant = mcu.lower()
flag_custom_sdkonfig = False
flag_custom_component_add = False
flag_custom_component_remove = False

IDF5 = (
    platform.get_package_version("framework-espidf")
    .split(".")[1]
    .startswith("5")
)
IDF_ENV_VERSION = "1.0.0"
FRAMEWORK_DIR = platform.get_package_dir("framework-espidf")
TOOLCHAIN_DIR = platform.get_package_dir(
    "toolchain-xtensa-esp-elf"
    if mcu in ("esp32", "esp32s2", "esp32s3")
    else "toolchain-riscv32-esp"
)


assert os.path.isdir(FRAMEWORK_DIR)
assert os.path.isdir(TOOLCHAIN_DIR)

def create_silent_action(action_func):
    """Create a silent SCons action that suppresses output"""
    silent_action = env.Action(action_func)
    silent_action.strfunction = lambda target, source, env: ''
    return silent_action

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
    arduino_libs_mcu = join(ARDUINO_FRAMEWORK_DIR,"tools","esp32-arduino-libs",mcu)

BUILD_DIR = env.subst("$BUILD_DIR")
PROJECT_DIR = env.subst("$PROJECT_DIR")
PROJECT_SRC_DIR = env.subst("$PROJECT_SRC_DIR")
CMAKE_API_REPLY_PATH = os.path.join(".cmake", "api", "v1", "reply")
SDKCONFIG_PATH = os.path.expandvars(board.get(
        "build.esp-idf.sdkconfig_path",
        os.path.join(PROJECT_DIR, "sdkconfig.%s" % env.subst("$PIOENV")),
))

def contains_path_traversal(url):
    """Check for Path Traversal patterns"""
    dangerous_patterns = [
        '../', '..\\',  # Standard Path Traversal
        '%2e%2e%2f', '%2e%2e%5c',  # URL-encoded
        '..%2f', '..%5c',  # Mixed
        '%252e%252e%252f',  # Double encoded
    ]
    
    url_lower = url.lower()
    return any(pattern in url_lower for pattern in dangerous_patterns)

#
# generate modified Arduino IDF sdkconfig, applying settings from "custom_sdkconfig"
#
if config.has_option("env:"+env["PIOENV"], "custom_component_add"):
    flag_custom_component_add = True
if config.has_option("env:"+env["PIOENV"], "custom_component_remove"):
    flag_custom_component_remove = True

if config.has_option("env:"+env["PIOENV"], "custom_sdkconfig"):
    flag_custom_sdkonfig = True
if "espidf.custom_sdkconfig" in board:
    flag_custom_sdkonfig = True

def HandleArduinoIDFsettings(env):
    """
    Handles Arduino IDF settings configuration with custom sdkconfig support.
    """
    
    def get_MD5_hash(phrase):
        """Generate MD5 hash for checksum validation."""
        import hashlib
        return hashlib.md5(phrase.encode('utf-8')).hexdigest()[:16]

    def load_custom_sdkconfig_file():
        """Load custom sdkconfig from file or URL if specified."""
        if not config.has_option("env:" + env["PIOENV"], "custom_sdkconfig"):
            return ""
        
        sdkconfig_entries = env.GetProjectOption("custom_sdkconfig").splitlines()
        
        for file_entry in sdkconfig_entries:
            # Handle HTTP/HTTPS URLs
            if "http" in file_entry and "://" in file_entry:
                url = file_entry.split(" ")[0]
                # Path Traversal protection
                if contains_path_traversal(url):
                    print(f"Path Traversal detected: {url} check your URL path")
                else:
                    try:
                        response = requests.get(file_entry.split(" ")[0], timeout=10)
                        if response.ok:
                            return response.content.decode('utf-8')
                    except requests.RequestException as e:
                        print(f"Error downloading {file_entry}: {e}")
                    except UnicodeDecodeError as e:
                        print(f"Error decoding response from {file_entry}: {e}")
                        return ""
            
            # Handle local files
            if "file://" in file_entry:
                file_ref = file_entry[7:] if file_entry.startswith("file://") else file_entry
                filename = os.path.basename(file_ref)
                file_path = join(PROJECT_DIR, filename)
                if os.path.exists(file_path):
                    try:
                        with open(file_path, 'r') as f:
                            return f.read()
                    except IOError as e:
                        print(f"Error reading file {file_path}: {e}")
                        return ""
                else:
                    print("File not found, check path:", file_path)
                    return ""
        
        return ""

    def extract_flag_name(line):
        """Extract flag name from sdkconfig line."""
        line = line.strip()
        if line.startswith("#") and "is not set" in line:
            return line.split(" ")[1]
        elif not line.startswith("#") and "=" in line:
            return line.split("=")[0]
        return None

    def build_idf_config_flags():
        """Build complete IDF configuration flags from all sources."""
        flags = []
        
        # Add board-specific flags first
        if "espidf.custom_sdkconfig" in board:
            board_flags = board.get("espidf.custom_sdkconfig", [])
            if board_flags:
                flags.extend(board_flags)
        
        # Add custom sdkconfig file content
        custom_file_content = load_custom_sdkconfig_file()
        if custom_file_content:
            flags.append(custom_file_content)
        
        # Add project-level custom sdkconfig
        if config.has_option("env:" + env["PIOENV"], "custom_sdkconfig"):
            custom_flags = env.GetProjectOption("custom_sdkconfig").rstrip("\n")
            if custom_flags:
                flags.append(custom_flags)
        
        return "\n".join(flags) + "\n" if flags else ""

    def add_flash_configuration(config_flags):
        """Add flash frequency and mode configuration."""
        if flash_frequency != "80m":
            config_flags += "# CONFIG_ESPTOOLPY_FLASHFREQ_80M is not set\n"
            config_flags += f"CONFIG_ESPTOOLPY_FLASHFREQ_{flash_frequency.upper()}=y\n"
            config_flags += f"CONFIG_ESPTOOLPY_FLASHFREQ=\"{flash_frequency}\"\n"
        
        if flash_mode != "qio":
            config_flags += "# CONFIG_ESPTOOLPY_FLASHMODE_QIO is not set\n"
        
        flash_mode_flag = f"CONFIG_ESPTOOLPY_FLASHMODE_{flash_mode.upper()}=y\n"
        if flash_mode_flag not in config_flags:
            config_flags += flash_mode_flag
        
        # ESP32 specific SPIRAM configuration
        if mcu == "esp32" and "CONFIG_FREERTOS_UNICORE=y" in config_flags:
            config_flags += "# CONFIG_SPIRAM is not set\n"
        
        return config_flags

    def write_sdkconfig_file(idf_config_flags, checksum_source):
        if "arduino" not in env.subst("$PIOFRAMEWORK"):
            print("Error: Arduino framework required for sdkconfig processing")
            return
        """Write the final sdkconfig.defaults file with checksum."""
        sdkconfig_src = join(ARDUINO_FRAMEWORK_DIR, "tools", "esp32-arduino-libs", mcu, "sdkconfig")
        sdkconfig_dst = join(PROJECT_DIR, "sdkconfig.defaults")
        
        # Generate checksum for validation (maintains original logic)
        checksum = get_MD5_hash(checksum_source.strip() + mcu)
        
        with open(sdkconfig_src, 'r', encoding='utf-8') as src, open(sdkconfig_dst, 'w', encoding='utf-8') as dst:
            # Write checksum header (critical for compilation decision logic)
            dst.write(f"# TASMOTA__{checksum}\n")
            
            processed_flags = set()
            
            # Process each line from source sdkconfig
            for line in src:
                flag_name = extract_flag_name(line)
                
                if flag_name is None:
                    dst.write(line)
                    continue
                
                # Check if we have a custom replacement for this flag
                flag_replaced = False
                for custom_flag in idf_config_flags[:]:  # Create copy for safe removal
                    custom_flag_name = extract_flag_name(custom_flag.replace("'", ""))
                    
                    if flag_name == custom_flag_name:
                        cleaned_flag = custom_flag.replace("'", "")
                        dst.write(cleaned_flag + "\n")
                        print(f"Replace: {line.strip()} with: {cleaned_flag}")
                        idf_config_flags.remove(custom_flag)
                        processed_flags.add(custom_flag_name)
                        flag_replaced = True
                        break
                
                if not flag_replaced:
                    dst.write(line)
            
            # Add any remaining new flags
            for remaining_flag in idf_config_flags:
                cleaned_flag = remaining_flag.replace("'", "")
                print(f"Add: {cleaned_flag}")
                dst.write(cleaned_flag + "\n")

    # Main execution logic
    has_custom_config = (
        config.has_option("env:" + env["PIOENV"], "custom_sdkconfig") or
        "espidf.custom_sdkconfig" in board
    )
    
    if not has_custom_config:
        return
    
    print("*** Add \"custom_sdkconfig\" settings to IDF sdkconfig.defaults ***")
    
    # Build complete configuration
    idf_config_flags = build_idf_config_flags()
    idf_config_flags = add_flash_configuration(idf_config_flags)
    
    # Convert to list for processing
    idf_config_list = [line for line in idf_config_flags.splitlines() if line.strip()]
    
    # Write final configuration file with checksum
    custom_sdk_config_flags = ""
    if config.has_option("env:" + env["PIOENV"], "custom_sdkconfig"):
        custom_sdk_config_flags = env.GetProjectOption("custom_sdkconfig").rstrip("\n") + "\n"
    
    write_sdkconfig_file(idf_config_list, custom_sdk_config_flags)



def HandleCOMPONENTsettings(env):
    from component_manager import ComponentManager
    component_manager = ComponentManager(env)

    if flag_custom_component_add or flag_custom_component_remove:
        actions = [action for flag, action in [
            (flag_custom_component_add, "select"),
            (flag_custom_component_remove, "deselect")
        ] if flag]
        action_text = " and ".join(actions)
        print(f"*** \"custom_component\" is used to {action_text} managed idf components ***")

        component_manager.handle_component_settings(
            add_components=flag_custom_component_add,
            remove_components=flag_custom_component_remove
        )
        return
    return

if "arduino" in env.subst("$PIOFRAMEWORK"):
    HandleCOMPONENTsettings(env)

if flag_custom_sdkonfig == True and "arduino" in env.subst("$PIOFRAMEWORK") and "espidf" not in env.subst("$PIOFRAMEWORK"):
    HandleArduinoIDFsettings(env)
    LIB_SOURCE = os.path.join(ProjectConfig.get_instance().get("platformio", "platforms_dir"), "espressif32", "builder", "build_lib")
    if not bool(os.path.exists(os.path.join(PROJECT_DIR, ".dummy"))):
        shutil.copytree(LIB_SOURCE, os.path.join(PROJECT_DIR, ".dummy"))
    PROJECT_SRC_DIR = os.path.join(PROJECT_DIR, ".dummy")
    env.Replace(
        PROJECT_SRC_DIR=PROJECT_SRC_DIR,
        BUILD_FLAGS="",
        BUILD_UNFLAGS="",
        LINKFLAGS="",
        PIOFRAMEWORK="arduino",
        ARDUINO_LIB_COMPILE_FLAG="Build",
    )
    env["INTEGRATION_EXTRA_DATA"].update({"arduino_lib_compile_flag": env.subst("$ARDUINO_LIB_COMPILE_FLAG")})

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
    idf_deps_lock = os.path.join(PROJECT_DIR, "dependencies.lock")
    ninja_buildfile = os.path.join(BUILD_DIR, "build.ninja")

    for d in (cmake_api_reply_dir, cmake_preconf_dir):
        if not os.path.isdir(d) or not os.listdir(d):
            return True
    if not os.path.isfile(cmake_cache_file):
        return True
    if not os.path.isfile(ninja_buildfile):
        return True
    if not os.path.isfile(SDKCONFIG_PATH) or os.path.getmtime(
        SDKCONFIG_PATH
    ) > os.path.getmtime(cmake_cache_file):
        return True
    if os.path.isfile(deafult_sdk_config) and os.path.getmtime(
        deafult_sdk_config
    ) > os.path.getmtime(cmake_cache_file):
        return True
    if os.path.isfile(idf_deps_lock) and os.path.getmtime(
        idf_deps_lock
    ) > os.path.getmtime(ninja_buildfile):
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
    idf_env["IDF_PATH"] = fs.to_unix_path(FRAMEWORK_DIR)
    additional_packages = [
        os.path.join(TOOLCHAIN_DIR, "bin"),
        platform.get_package_dir("tool-ninja"),
        os.path.join(platform.get_package_dir("tool-cmake"), "bin"),
        os.path.dirname(get_python_exe()),
    ]

    idf_env["PATH"] = os.pathsep.join(additional_packages + [idf_env["PATH"]])

    # Some users reported that the `IDF_TOOLS_PATH` var can seep into the
    # underlying build system. Unsetting it is a safe workaround.
    if "IDF_TOOLS_PATH" in idf_env:
        del idf_env["IDF_TOOLS_PATH"]

    idf_env["ESP_ROM_ELF_DIR"] = platform.get_package_dir("tool-esp-rom-elfs")


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


def build_library(
    default_env, lib_config, project_src_dir, prepend_dir=None, debug_allowed=True
):
    lib_name = lib_config["nameOnDisk"]
    lib_path = lib_config["paths"]["build"]
    if prepend_dir:
        lib_path = os.path.join(prepend_dir, lib_path)
    lib_objects = compile_source_files(
        lib_config, default_env, project_src_dir, prepend_dir, debug_allowed
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
    def _normalize_define(define_string):
        define_string = define_string.strip()
        if "=" in define_string:
            define, value = define_string.split("=", maxsplit=1)
            if define == "OPENTHREAD_BUILD_DATETIME":
                return None
            if any(char in value for char in (' ', '<', '>')):
                value = f'"{value}"'
            elif '"' in value and not value.startswith("\\"):
                value = value.replace('"', '\\"')
            return (define, value)
        return define_string

    result = [
        _normalize_define(d.get("define", ""))
        for d in compile_group.get("defines", []) if d
    ]

    for f in compile_group.get("compileCommandFragments", []):
        fragment = f.get("fragment", "").strip()
        if fragment.startswith('"'):
            fragment = fragment.strip('"')
        if fragment.startswith("-D"):
            result.append(_normalize_define(fragment[2:]))

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
        elif fragment_role in ("libraries", "libraryPath"):
            if fragment.startswith("-l"):
                link_args["LIBS"].extend(args)
            elif fragment.startswith("-L"):
                lib_path = fragment.replace("-L", "").strip(" '\"")
                _add_to_libpath(lib_path, link_args)
            elif fragment.startswith("-") and not fragment.startswith("-l"):
                # CMake mistakenly marks LINKFLAGS as libraries
                link_args["LINKFLAGS"].extend(args)
            elif fragment.endswith(".a"):
                archive_path = fragment
                # process static archives
                if os.path.isabs(archive_path):
                    # In case of precompiled archives
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
                fragment = ccfragment.get("fragment", "").strip("\" ")
                if not fragment or fragment.startswith("-D"):
                    continue
                flags[cg["language"]].extend(
                    click.parser.split_arg_string(fragment.strip())
                )

        return flags

    app_flags = _extract_flags(app_config)
    default_flags = _extract_flags(default_config)

    # Flags are sorted because CMake randomly populates build flags in code model
    return {
        "ASPPFLAGS": sorted(app_flags.get("ASM", default_flags.get("ASM"))),
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


def load_component_paths(framework_components_dir, ignored_component_prefixes=None):
    def _scan_components_from_framework():
        result = []
        for component in os.listdir(framework_components_dir):
            component_path = os.path.join(framework_components_dir, component)
            if component.startswith(ignored_component_prefixes) or not os.path.isdir(
                component_path
            ):
                continue
            result.append(component_path)

        return result

    # First of all, try to load the list of used components from the project description
    components = []
    ignored_component_prefixes = ignored_component_prefixes or []
    project_description_file = os.path.join(BUILD_DIR, "project_description.json")
    if os.path.isfile(project_description_file):
        with open(project_description_file) as fp:
            try:
                data = json.load(fp)
                for path in data.get("build_component_paths", []):
                    if not os.path.basename(path).startswith(
                        ignored_component_prefixes
                    ):
                        components.append(path)
            except:
                print(
                    "Warning: Could not find load components from project description!\n"
                )

    return components or _scan_components_from_framework()


def extract_linker_script_fragments_backup(framework_components_dir, sdk_config):
    # Hardware-specific components are excluded from search and added manually below
    project_components = load_component_paths(
        framework_components_dir, ignored_component_prefixes=("esp32", "riscv")
    )

    result = []
    for component_path in project_components:
        linker_fragment = os.path.join(component_path, "linker.lf")
        if os.path.isfile(linker_fragment):
            result.append(linker_fragment)

    if not result:
        sys.stderr.write("Error: Failed to extract paths to linker script fragments\n")
        env.Exit(1)

    if mcu not in ("esp32", "esp32s2", "esp32s3"):
        result.append(os.path.join(framework_components_dir, "riscv", "linker.lf"))

    # Add extra linker fragments
    for fragment in (
        os.path.join("esp_system", "app.lf"),
        os.path.join("esp_common", "common.lf"),
        os.path.join("esp_common", "soc.lf"),
        os.path.join("newlib", "system_libs.lf"),
        os.path.join("newlib", "newlib.lf"),
    ):
        result.append(os.path.join(framework_components_dir, fragment))

    if sdk_config.get("SPIRAM_CACHE_WORKAROUND", False):
        result.append(
            os.path.join(
                framework_components_dir, "newlib", "esp32-spiram-rom-functions-c.lf"
            )
        )

    if board.get("build.esp-idf.extra_lf_files", ""):
        result.extend(
            [
                lf if os.path.isabs(lf) else os.path.join(PROJECT_DIR, lf)
                for lf in board.get("build.esp-idf.extra_lf_files").splitlines()
                if lf.strip()
            ]
        )

    return result


def extract_linker_script_fragments(
    ninja_buildfile, framework_components_dir, sdk_config
):
    def _normalize_fragment_path(base_dir, fragment_path):
        if not os.path.isabs(fragment_path):
            fragment_path = os.path.abspath(
                os.path.join(base_dir, fragment_path)
            )
        if not os.path.isfile(fragment_path):
            print("Warning! The `%s` fragment is not found!" % fragment_path)

        return fragment_path

    assert os.path.isfile(
        ninja_buildfile
    ), "Cannot extract linker fragments! Ninja build file is missing!"

    result = []
    with open(ninja_buildfile, encoding="utf8") as fp:
        for line in fp.readlines():
            if "sections.ld: CUSTOM_COMMAND" not in line:
                continue
            for fragment_match in re.finditer(r"(\S+\.lf\b)+", line):
                result.append(_normalize_fragment_path(
                    BUILD_DIR, fragment_match.group(0).replace("$:", ":")
                ))

            break

    # Fall back option if the new algorithm didn't work
    if not result:
        result = extract_linker_script_fragments_backup(
            framework_components_dir, sdk_config
        )

    if board.get("build.esp-idf.extra_lf_files", ""):
        for fragment_path in board.get(
            "build.esp-idf.extra_lf_files"
        ).splitlines():
            if not fragment_path.strip():
                continue
            result.append(_normalize_fragment_path(PROJECT_DIR, fragment_path))

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
    linker_script_fragments = extract_linker_script_fragments(
        os.path.join(BUILD_DIR, "build.ninja"),
        os.path.join(FRAMEWORK_DIR, "components"),
        sdk_config
    )

    # Create a new file to avoid automatically generated library entry as files
    # from this library are built internally by PlatformIO
    libraries_list = create_custom_libraries_list(
        os.path.join(BUILD_DIR, "ldgen_libraries"), ignore_targets
    )

    args = {
        "script": os.path.join(FRAMEWORK_DIR, "tools", "ldgen", "ldgen.py"),
        "config": SDKCONFIG_PATH,
        "fragments": " ".join(
            ['"%s"' % fs.to_unix_path(f) for f in linker_script_fragments]
        ),
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
        '"$ESPIDF_PYTHONEXE" "{script}" --input $SOURCE '
        '--config "{config}" --fragments {fragments} --output $TARGET '
        '--kconfig "{kconfig}" --env-file "{env_file}" '
        '--libraries-file "{libraries_list}" '
        '--objdump "{objdump}"'
    ).format(**args)

    initial_ld_script = os.path.join(
        FRAMEWORK_DIR,
        "components",
        "esp_system",
        "ld",
        idf_variant,
        "sections.ld.in",
    )

    framework_version = [int(v) for v in get_framework_version().split(".")]
    if framework_version[:2] > [5, 2]:
        initial_ld_script = preprocess_linker_file(
            initial_ld_script,
            os.path.join(
                BUILD_DIR,
                "esp-idf",
                "esp_system",
                "ld",
                "sections.ld.in",
            )
        )

    return env.Command(
        os.path.join("$BUILD_DIR", "sections.ld"),
        initial_ld_script,
        env.VerboseAction(cmd, "Generating project linker script $TARGET"),
    )


# A temporary workaround to avoid modifying CMake mainly for the "heap" library.
# The "tlsf.c" source file in this library has an include flag relative
# to CMAKE_CURRENT_SOURCE_DIR which breaks PlatformIO builds that have a
# different working directory
def _fix_component_relative_include(config, build_flags, source_index):
    source_file_path = config["sources"][source_index]["path"]
    build_flags = build_flags.replace("..", os.path.dirname(source_file_path) + "/..")
    return build_flags


def prepare_build_envs(config, default_env, debug_allowed=True):
    build_envs = []
    target_compile_groups = config.get("compileGroups", [])
    if not target_compile_groups:
        print("Warning! The `%s` component doesn't register any source files. "
            "Check if sources are set in component's CMakeLists.txt!" % config["name"]
        )

    is_build_type_debug = "debug" in env.GetBuildType() and debug_allowed
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
        build_env.SetOption("implicit_cache", 1)
        for cc in compile_commands:
            build_flags = cc.get("fragment", "").strip("\" ")
            if not build_flags.startswith("-D"):
                if build_flags.startswith("-include") and ".." in build_flags:
                    source_index = cg.get("sourceIndexes")[0]
                    build_flags = _fix_component_relative_include(
                        config, build_flags, source_index)
                parsed_flags = build_env.ParseFlags(build_flags)
                build_env.AppendUnique(**parsed_flags)
                if cg.get("language", "") == "ASM":
                    build_env.AppendUnique(ASPPFLAGS=parsed_flags.get("CCFLAGS", []))
        build_env.AppendUnique(CPPDEFINES=defines, CPPPATH=includes)
        if sys_includes:
            build_env.Append(CCFLAGS=[("-isystem", inc) for inc in sys_includes])
        build_env.ProcessUnFlags(default_env.get("BUILD_UNFLAGS"))
        if is_build_type_debug:
            build_env.ConfigureDebugFlags()
        build_envs.append(build_env)

    return build_envs


def compile_source_files(
    config, default_env, project_src_dir, prepend_dir=None, debug_allowed=True
):
    build_envs = prepare_build_envs(config, default_env, debug_allowed)
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
            if src_path.lower().startswith(components_dir.lower()):
                obj_path = os.path.join(
                    obj_path, os.path.relpath(src_path, components_dir)
                )
            else:
                if not os.path.isabs(source["path"]):
                    obj_path = os.path.join(obj_path, source["path"])
                else:
                    obj_path = os.path.join(obj_path, os.path.basename(src_path))

            preserve_source_file_extension = board.get(
                "build.esp-idf.preserve_source_file_extension", "yes"
            ) == "yes"

            objects.append(
                build_envs[compile_group_idx].StaticObject(
                    target=(
                        obj_path
                        if preserve_source_file_extension
                        else os.path.splitext(obj_path)[0]
                    ) + ".o",
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


def build_bootloader(sdk_config):
    bootloader_src_dir = os.path.join(
        FRAMEWORK_DIR, "components", "bootloader", "subproject"
    )
    code_model = get_cmake_code_model(
        bootloader_src_dir,
        os.path.join(BUILD_DIR, "bootloader"),
        [
            "-DIDF_TARGET=" + idf_variant,
            "-DPYTHON_DEPS_CHECKED=1",
            "-DPYTHON=" + get_python_exe(),
            "-DIDF_PATH=" + FRAMEWORK_DIR,
            "-DSDKCONFIG=" + SDKCONFIG_PATH,
            "-DPROJECT_SOURCE_DIR=" + PROJECT_DIR,
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

    # Note: By default the size of bootloader is limited to 0x2000 bytes,
    # in debug mode the footprint size can be easily grow beyond this limit
    build_components(
        bootloader_env,
        components_map,
        bootloader_src_dir,
        "bootloader",
        debug_allowed=sdk_config.get("BOOTLOADER_COMPILER_OPTIMIZATION_DEBUG", False),
    )
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
        if "nameOnDisk" not in config:
            config["nameOnDisk"] = "lib%s.a" % config["name"]
        result[config["id"]] = {"config": config}

    return result


def build_components(
    env, components_map, project_src_dir, prepend_dir=None, debug_allowed=True
):
    for k, v in components_map.items():
        components_map[k]["lib"] = build_library(
            env, v["config"], project_src_dir, prepend_dir, debug_allowed
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
    dummy_component_path = os.path.join(FRAMEWORK_DIR, "components", "__pio_env")
    if os.path.isdir(dummy_component_path):
        return

    os.makedirs(dummy_component_path)

    for ext in (".cpp", ".c", ".S"):
        dummy_file = os.path.join(dummy_component_path, "__dummy" + ext)
        if not os.path.isfile(dummy_file):
            open(dummy_file, "a").close()

    component_cmake = os.path.join(dummy_component_path, "CMakeLists.txt")
    if not os.path.isfile(component_cmake):
        with open(component_cmake, "w") as fp:
            fp.write(prj_cmake_tpl)


def find_default_component(target_configs):
    for config in target_configs:
        if "__pio_env" in config:
            return config
    sys.stderr.write(
        "Error! Failed to find the default IDF component with build information for "
        "generic files.\nCheck that the `EXTRA_COMPONENT_DIRS` option is not overridden "
        "in your CMakeLists.txt.\nSee an example with an extra component here "
        "https://docs.platformio.org/en/latest/frameworks/espidf.html#esp-idf-components\n"
    )
    env.Exit(1)


def get_framework_version():
    def _extract_from_cmake_version_file():
        version_cmake_file = os.path.join(
            FRAMEWORK_DIR, "tools", "cmake", "version.cmake"
        )
        if not os.path.isfile(version_cmake_file):
            return

        with open(version_cmake_file, encoding="utf8") as fp:
            pattern = r"set\(IDF_VERSION_(MAJOR|MINOR|PATCH) (\d+)\)"
            matches = re.findall(pattern, fp.read())
            if len(matches) != 3:
                return
            # If found all three parts of the version
            return ".".join([match[1] for match in matches])

    pkg = platform.get_package("framework-espidf")
    version = get_original_version(str(pkg.metadata.version.truncate()))
    if not version:
        # Fallback value extracted directly from the cmake version file
        version = _extract_from_cmake_version_file()
        if not version:
            version = "0.0.0"

    return version


def create_version_file():
    version_file = os.path.join(FRAMEWORK_DIR, "version.txt")
    if not os.path.isfile(version_file):
        with open(version_file, "w") as fp:
            fp.write(get_framework_version())


def generate_empty_partition_image(binary_path, image_size):
    empty_partition = env.Command(
        binary_path,
        None,
        env.VerboseAction(
            '"$ESPIDF_PYTHONEXE" "%s" %s $TARGET'
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

    if flag_custom_sdkonfig == False:
        env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", empty_partition)


def get_partition_info(pt_path, pt_offset, pt_params):
    if not os.path.isfile(pt_path):
        sys.stderr.write(
            "Missing partition table file `%s`\n" % pt_path
        )
        env.Exit(1)

    cmd = [
        get_python_exe(),
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

    if pt_params.get("name") == "boot":
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
    ota_app_params = get_partition_info(pt_table, pt_offset, {"type": "app", "subtype": "ota_0"})
    if ota_app_params.get("offset"):
        return ota_app_params["offset"]
    factory_app_params = get_partition_info(pt_table, pt_offset, {"type": "app", "subtype": "factory"})
    return factory_app_params.get("offset", "0x10000")


def preprocess_linker_file(src_ld_script, target_ld_script):
    return env.Command(
        target_ld_script,
        src_ld_script,
        env.VerboseAction(
            " ".join(
                [
                    os.path.join(
                        platform.get_package_dir("tool-cmake"),
                        "bin",
                        "cmake",
                    ),
                    "-DCC=%s"
                    % os.path.join(
                        TOOLCHAIN_DIR,
                        "bin",
                        "$CC",
                    ),
                    "-DSOURCE=$SOURCE",
                    "-DTARGET=$TARGET",
                    "-DCONFIG_DIR=%s" % os.path.join(BUILD_DIR, "config"),
                    "-DLD_DIR=%s"
                    % os.path.join(
                        FRAMEWORK_DIR, "components", "esp_system", "ld"
                    ),
                    "-P",
                    os.path.join(
                        "$BUILD_DIR",
                        "esp-idf",
                        "esp_system",
                        "ld",
                        "linker_script_generator.cmake",
                    ),
                ]
            ),
            "Generating LD script $TARGET",
        ),
    )


def generate_mbedtls_bundle(sdk_config):
    bundle_path = os.path.join("$BUILD_DIR", "x509_crt_bundle")
    if os.path.isfile(env.subst(bundle_path)):
        return

    default_crt_dir = os.path.join(
        FRAMEWORK_DIR, "components", "mbedtls", "esp_crt_bundle"
    )

    cmd = [get_python_exe(), os.path.join(default_crt_dir, "gen_crt_bundle.py")]

    crt_args = ["--input"]
    if sdk_config.get("MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL", False):
        crt_args.append(os.path.join(default_crt_dir, "cacrt_all.pem"))
        crt_args.append(os.path.join(default_crt_dir, "cacrt_local.pem"))
    elif sdk_config.get("MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN", False):
        crt_args.append(os.path.join(default_crt_dir, "cacrt_all.pem"))
        crt_args.append(os.path.join(default_crt_dir, "cacrt_local.pem"))
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
    PYTHON_EXE = env.subst("$PYTHONEXE")
    UV_EXE = os.path.join(os.path.dirname(PYTHON_EXE), "uv" + (".exe" if IS_WINDOWS else ""))
    def _get_installed_uv_packages(python_exe_path):
        result = {}
        try:
            uv_output = subprocess.check_output([
                UV_EXE, "pip", "list", "--python", python_exe_path, "--format=json"
            ])
            packages = json.loads(uv_output)
        except (subprocess.CalledProcessError, json.JSONDecodeError, OSError) as e:
            print(f"Warning! Couldn't extract the list of installed Python packages: {e}")
            return {}
        
        for p in packages:
            result[p["name"]] = pepver_to_semver(p["version"])

        return result

    skip_python_packages = os.path.join(FRAMEWORK_DIR, ".pio_skip_pypackages")
    if os.path.isfile(skip_python_packages):
        return

    deps = {
        "uv": ">=0.1.0",
        # https://github.com/platformio/platformio-core/issues/4614
        "urllib3": "<2",
        # https://github.com/platformio/platform-espressif32/issues/635
        "cryptography": "~=44.0.0",
        "pyparsing": ">=3.1.0,<4",
        "idf-component-manager": "~=2.2.2",
        "esp-idf-kconfig": "~=2.5.0"
    }

    if sys_platform.system() == "Darwin" and "arm" in sys_platform.machine().lower():
        deps["chardet"] = ">=3.0.2,<4"

    python_exe_path = get_python_exe()
    installed_packages = _get_installed_uv_packages(python_exe_path)
    packages_to_install = []
    for package, spec in deps.items():
        if package not in installed_packages:
            packages_to_install.append(package)
        elif spec:
            version_spec = semantic_version.Spec(spec)
            if not version_spec.match(installed_packages[package]):
                packages_to_install.append(package)

    if packages_to_install:
        packages_str = " ".join(['"%s%s"' % (p, deps[p]) for p in packages_to_install])
        
        # Use uv to install packages in the specific Python environment
        env.Execute(
            env.VerboseAction(
                f'"{UV_EXE}" pip install --python "{python_exe_path}" {packages_str}',
                "Installing ESP-IDF's Python dependencies with uv",
            )
        )

    if IS_WINDOWS and "windows-curses" not in installed_packages:
        # Install windows-curses in the IDF Python environment
        env.Execute(
            env.VerboseAction(
                f'"{UV_EXE}" pip install --python "{python_exe_path}" windows-curses',
                "Installing windows-curses package with uv",
            )
        )


def get_idf_venv_dir():
    # The name of the IDF venv contains the IDF version to avoid possible conflicts and
    # unnecessary reinstallation of Python dependencies in cases when Arduino
    # as an IDF component requires a different version of the IDF package and
    # hence a different set of Python deps or their versions
    idf_version = get_framework_version()
    return os.path.join(
        env.subst("$PROJECT_CORE_DIR"), "penv", ".espidf-" + idf_version
    )


def ensure_python_venv_available():

    def _get_idf_venv_python_version():
        try:
            version = subprocess.check_output(
                [
                    get_python_exe(),
                    "-c",
                    "import sys;print('{0}.{1}.{2}-{3}.{4}'.format(*list(sys.version_info)))"
                ], text=True
            )
            return version.strip()
        except subprocess.CalledProcessError as e:
            print("Failed to extract Python version from IDF virtual env!")
            return None

    def _is_venv_outdated(venv_data_file):
        try:
            with open(venv_data_file, "r", encoding="utf8") as fp:
                venv_data = json.load(fp)
                if venv_data.get("version", "") != IDF_ENV_VERSION:
                    print(
                        "Warning! IDF virtual environment version changed!"
                    )
                    return True
                if (
                    venv_data.get("python_version", "")
                    != _get_idf_venv_python_version()
                ):
                    print(
                        "Warning! Python version in the IDF virtual environment"
                        " differs from the current Python!"
                    )
                    return True
                return False
        except:
            return True

    def _create_venv(venv_dir):
        pip_path = os.path.join(
            venv_dir,
            "Scripts" if IS_WINDOWS else "bin",
            "pip" + (".exe" if IS_WINDOWS else ""),
        )

        if os.path.isdir(venv_dir):
            try:
                print("Removing an outdated IDF virtual environment")
                shutil.rmtree(venv_dir)
            except OSError:
                print(
                    "Error: Cannot remove an outdated IDF virtual environment. " \
                    "Please remove the `%s` folder manually!" % venv_dir
                )
                env.Exit(1)

        # Use the built-in PlatformIO Python to create a standalone IDF virtual env
        env.Execute(
            env.VerboseAction(
                '"$PYTHONEXE" -m venv --clear "%s"' % venv_dir,
                "Creating a new virtual environment for IDF Python dependencies",
            )
        )

        assert os.path.isfile(
            pip_path
        ), "Error: Failed to create a proper virtual environment. Missing the `pip` binary!"

    venv_dir = get_idf_venv_dir()
    venv_data_file = os.path.join(venv_dir, "pio-idf-venv.json")
    if not os.path.isfile(venv_data_file) or _is_venv_outdated(venv_data_file):
        _create_venv(venv_dir)
        install_python_deps()
        with open(venv_data_file, "w", encoding="utf8") as fp:
            venv_info = {
                "version": IDF_ENV_VERSION,
                "python_version": _get_idf_venv_python_version()
            }
            json.dump(venv_info, fp, indent=2)


def get_python_exe():
    python_exe_path = os.path.join(
        get_idf_venv_dir(),
        "Scripts" if IS_WINDOWS else "bin",
        "python" + (".exe" if IS_WINDOWS else ""),
    )

    assert os.path.isfile(python_exe_path), (
        "Error: Missing Python executable file `%s`" % python_exe_path
    )

    return python_exe_path


#
# Ensure Python environment contains everything required for IDF
#

ensure_python_venv_available()

# ESP-IDF package doesn't contain .git folder, instead package version is specified
# in a special file "version.h" in the root folder of the package

create_version_file()

# Generate a default component with dummy C/C++/ASM source files in the framework
# folder. This component is used to force the IDF build system generate build
# information for generic C/C++/ASM sources regardless of whether such files are used in project

generate_default_component()

#
# Generate final linker script
#

if not board.get("build.ldscript", ""):
    initial_ld_script = board.get("build.esp-idf.ldscript", os.path.join(
        FRAMEWORK_DIR,
        "components",
        "esp_system",
        "ld",
        idf_variant,
        "memory.ld.in",
    ))

    framework_version = [int(v) for v in get_framework_version().split(".")]
    if framework_version[:2] > [5, 2]:
        initial_ld_script = preprocess_linker_file(
            initial_ld_script,
            os.path.join(
                BUILD_DIR,
                "esp-idf",
                "esp_system",
                "ld",
                "memory.ld.in",
            )
        )

    linker_script = env.Command(
        os.path.join("$BUILD_DIR", "memory.ld"),
        initial_ld_script,
        env.VerboseAction(
            '$CC -I"$BUILD_DIR/config" -I"%s" -C -P -x c -E $SOURCE -o $TARGET'
            % os.path.join(FRAMEWORK_DIR, "components", "esp_system", "ld"),
            "Generating LD script $TARGET",
        ),
    )

    env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", linker_script)
    env.Replace(LDSCRIPT_PATH="memory.ld")


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
extra_components = []
if PROJECT_SRC_DIR != os.path.join(PROJECT_DIR, "main"):
    extra_components.append(PROJECT_SRC_DIR)
if "arduino" in env.subst("$PIOFRAMEWORK"):
    print(
        "Warning! Arduino framework as an ESP-IDF component doesn't handle "
        "the `variant` field! The default `esp32` variant will be used."
    )
    extra_components.append(ARDUINO_FRAMEWORK_DIR)
    # Add path to internal Arduino libraries so that the LDF will be able to find them
    env.Append(
        LIBSOURCE_DIRS=[os.path.join(ARDUINO_FRAMEWORK_DIR, "libraries")]
    )

# Set ESP-IDF version environment variables (needed for proper Kconfig processing)
framework_version = get_framework_version()
major_version = framework_version.split('.')[0] + '.' + framework_version.split('.')[1]
os.environ["ESP_IDF_VERSION"] = major_version

# Configure CMake arguments with ESP-IDF version
extra_cmake_args = [
    "-DIDF_TARGET=" + idf_variant,
    "-DPYTHON_DEPS_CHECKED=1",
    "-DEXTRA_COMPONENT_DIRS:PATH=" + ";".join(extra_components),
    "-DPYTHON=" + get_python_exe(),
    "-DSDKCONFIG=" + SDKCONFIG_PATH,
    f"-DESP_IDF_VERSION={major_version}",
    f"-DESP_IDF_VERSION_MAJOR={framework_version.split('.')[0]}",
    f"-DESP_IDF_VERSION_MINOR={framework_version.split('.')[1]}",
]

# This will add the linker flag for the map file
extra_cmake_args.append(
    f'-DCMAKE_EXE_LINKER_FLAGS=-Wl,-Map={fs.to_unix_path(os.path.join(BUILD_DIR, env.subst("$PROGNAME") + ".map"))}'
)

# Add any extra args from board config
extra_cmake_args += click.parser.split_arg_string(board.get("build.cmake_extra_args", ""))

print("Reading CMake configuration...")
project_codemodel = get_cmake_code_model(
    PROJECT_DIR,
    BUILD_DIR,
    extra_cmake_args
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

#
# Compile bootloader
#

if flag_custom_sdkonfig == False:
    env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", build_bootloader(sdk_config))

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
extra_flags = filter_args(
    link_args["LINKFLAGS"],
    [
        "-T",
        "-u",
        "-Wl,--start-group",
        "-Wl,--end-group",
        "-Wl,--whole-archive",
        "-Wl,--no-whole-archive",
    ],
)
link_args["LINKFLAGS"] = sorted(list(set(link_args["LINKFLAGS"]) - set(extra_flags)))

# remove the main linker script flags '-T memory.ld'
try:
    ld_index = extra_flags.index("memory.ld")
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
    if node.srcnode().get_path().lower().startswith(PROJECT_SRC_DIR.lower()):
        return None
    return node


env.AddBuildMiddleware(_skip_prj_source_files)

#
# Generate partition table
#

fwpartitions_dir = os.path.join(FRAMEWORK_DIR, "components", "partition_table")
partitions_csv = board.get("build.partitions", "partitions_singleapp.csv")
partition_table_offset = sdk_config.get("PARTITION_TABLE_OFFSET", 0x8000)

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
        '"$ESPIDF_PYTHONEXE" "%s" -q --offset "%s" --flash-size "%s" $SOURCE $TARGET'
        % (
            os.path.join(
                FRAMEWORK_DIR, "components", "partition_table", "gen_esp32part.py"
            ),
            partition_table_offset,
            board.get("upload.flash_size", "4MB"),
        ),
        "Generating partitions $TARGET",
    ),
)

env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", partition_table)

#
# Main environment configuration
#

project_flags.update(link_args)
env.MergeFlags(project_flags)
env.Prepend(
    CPPPATH=app_includes["plain_includes"],
    CPPDEFINES=project_defines,
    ESPIDF_PYTHONEXE=get_python_exe(),
    LINKFLAGS=extra_flags,
    LIBS=libs,
    FLASH_EXTRA_IMAGES=[
        (
            board.get(
                "upload.bootloader_offset",
                "0x1000" if mcu in ["esp32", "esp32s2"] else ("0x2000" if mcu in ["esp32c5", "esp32p4"] else "0x0"),
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
# Propagate Arduino defines to the main build environment
#

if "arduino" in env.subst("$PIOFRAMEWORK"):
    arduino_config_name = list(
        filter(
            lambda config_name: config_name.startswith(
                "__idf_framework-arduinoespressif32"
            ),
            target_configs,
        )
    )[0]
    env.AppendUnique(
        CPPDEFINES=extract_defines(
            target_configs.get(arduino_config_name, {}).get("compileGroups", [])[0]
        )
    )

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
    project_env.AppendUnique(
        CPPPATH=["$PROJECT_INCLUDE_DIR", "$PROJECT_SRC_DIR", "$PROJECT_DIR"]
        + get_project_lib_includes(env)
    )

    project_env.ProcessFlags(env.get("SRC_BUILD_FLAGS"))
    env.Append(
        PIOBUILDFILES=compile_source_files(
            target_configs.get(project_target_name),
            project_env,
            project_env.subst("$PROJECT_DIR"),
        )
    )

#
# Generate mbedtls bundle
#

if sdk_config.get("MBEDTLS_CERTIFICATE_BUNDLE", False):
    generate_mbedtls_bundle(sdk_config)

#
# Check if flash size is set correctly in the IDF configuration file
#

board_flash_size = board.get("upload.flash_size", "4MB")
idf_flash_size = sdk_config.get("ESPTOOLPY_FLASHSIZE", "4MB")
if board_flash_size != idf_flash_size:
    print(
        "Warning! Flash memory size mismatch detected. Expected %s, found %s!"
        % (board_flash_size, idf_flash_size)
    )
    print(
        "Please select a proper value in your `sdkconfig.defaults` "
        "or via the `menuconfig` target!"
    )

#
# To embed firmware checksum a special argument for esptool.py is required
#

extra_elf2bin_flags = "--elf-sha256-offset 0xb0"
# https://github.com/espressif/esp-idf/blob/master/components/esptool_py/project_include.cmake#L58
# For chips that support configurable MMU page size feature
# If page size is configured to values other than the default "64KB" in menuconfig,
mmu_page_size = "64KB"
if sdk_config.get("MMU_PAGE_SIZE_8KB", False):
    mmu_page_size = "8KB"
elif sdk_config.get("MMU_PAGE_SIZE_16KB", False):
    mmu_page_size = "16KB"
elif sdk_config.get("MMU_PAGE_SIZE_32KB", False):
    mmu_page_size = "32KB"
else:
    mmu_page_size = "64KB"

if sdk_config.get("SOC_MMU_PAGE_SIZE_CONFIGURABLE", False):
    if board_flash_size == "2MB":
        mmu_page_size = "32KB"
    elif board_flash_size == "1MB":
        mmu_page_size = "16KB"

if mmu_page_size != "64KB":
    extra_elf2bin_flags += " --flash-mmu-page-size %s" % mmu_page_size

action = copy.deepcopy(env["BUILDERS"]["ElfToBin"].action)

action.cmd_list = env["BUILDERS"]["ElfToBin"].action.cmd_list.replace(
    "-o", extra_elf2bin_flags + " -o"
)
env["BUILDERS"]["ElfToBin"].action = action

#
# Compile ULP sources in 'ulp' folder
#

ulp_dir = os.path.join(PROJECT_DIR, "ulp")
if os.path.isdir(ulp_dir) and os.listdir(ulp_dir) and mcu not in ("esp32c2", "esp32c3", "esp32h2"):
    env.SConscript("ulp.py", exports="env sdk_config project_config app_includes idf_variant")

#
# Compile Arduino IDF sources
#

if ("arduino" in env.subst("$PIOFRAMEWORK")) and ("espidf" not in env.subst("$PIOFRAMEWORK")):
    def idf_lib_copy(source, target, env):
        env_build = join(env["PROJECT_BUILD_DIR"],env["PIOENV"])
        sdkconfig_h_path = join(env_build,"config","sdkconfig.h")
        arduino_libs = join(ARDUINO_FRAMEWORK_DIR,"tools","esp32-arduino-libs")
        lib_src = join(env_build,"esp-idf")
        lib_dst = join(arduino_libs,mcu,"lib")
        ld_dst = join(arduino_libs,mcu,"ld")
        mem_var = join(arduino_libs,mcu,board.get("build.arduino.memory_type", (board.get("build.flash_mode", "dio") + "_qspi")))
        src = [join(lib_src,x) for x in os.listdir(lib_src)]
        src = [folder for folder in src if not os.path.isfile(folder)] # folders only
        for folder in src:
            files = [join(folder,x) for x in os.listdir(folder)]
            for file in files:
                if file.strip().endswith(".a"):
                    shutil.copyfile(file,join(lib_dst,file.split(os.path.sep)[-1]))

        shutil.move(join(lib_dst,"libspi_flash.a"),join(mem_var,"libspi_flash.a"))
        shutil.move(join(env_build,"memory.ld"),join(ld_dst,"memory.ld"))
        if mcu == "esp32s3":
            shutil.move(join(lib_dst,"libesp_psram.a"),join(mem_var,"libesp_psram.a"))
            shutil.move(join(lib_dst,"libesp_system.a"),join(mem_var,"libesp_system.a"))
            shutil.move(join(lib_dst,"libfreertos.a"),join(mem_var,"libfreertos.a"))
            shutil.move(join(lib_dst,"libbootloader_support.a"),join(mem_var,"libbootloader_support.a"))
            shutil.move(join(lib_dst,"libesp_hw_support.a"),join(mem_var,"libesp_hw_support.a"))
            shutil.move(join(lib_dst,"libesp_lcd.a"),join(mem_var,"libesp_lcd.a"))

        shutil.copyfile(sdkconfig_h_path,join(mem_var,"include","sdkconfig.h"))
        if not bool(os.path.isfile(join(arduino_libs,mcu,"sdkconfig.orig"))):
            shutil.move(join(arduino_libs,mcu,"sdkconfig"),join(arduino_libs,mcu,"sdkconfig.orig"))
        shutil.copyfile(join(env.subst("$PROJECT_DIR"),"sdkconfig."+env["PIOENV"]),join(arduino_libs,mcu,"sdkconfig"))
        shutil.copyfile(join(env.subst("$PROJECT_DIR"),"sdkconfig."+env["PIOENV"]),join(arduino_libs,"sdkconfig"))
        try:
            os.remove(join(env.subst("$PROJECT_DIR"),"dependencies.lock"))
            os.remove(join(env.subst("$PROJECT_DIR"),"CMakeLists.txt"))
        except:
            pass
        print("*** Copied compiled %s IDF libraries to Arduino framework ***" % idf_variant)

        PYTHON_EXE = env.subst("$PYTHONEXE")
        pio_exe_path = os.path.join(os.path.dirname(PYTHON_EXE), "pio" + (".exe" if IS_WINDOWS else ""))
        pio_cmd = env["PIOENV"]
        env.Execute(
            env.VerboseAction(
                (
                    '"%s" run -e ' % pio_exe_path
                    + " ".join(['"%s"' % pio_cmd])
                ),
                "*** Starting Arduino compile %s with custom libraries ***" % pio_cmd,
            )
        )
        if flag_custom_component_add == True or flag_custom_component_remove == True:
            try:
                shutil.copy(join(ARDUINO_FRAMEWORK_DIR,"idf_component.yml.orig"),join(ARDUINO_FRAMEWORK_DIR,"idf_component.yml"))
                print("*** Original Arduino \"idf_component.yml\" restored ***")
            except:
                print("*** Original Arduino \"idf_component.yml\" couldnt be restored ***")
            # Restore original pioarduino-build.py
            from component_manager import ComponentManager
            component_manager = ComponentManager(env)
            component_manager.restore_pioarduino_build_py()
    silent_action = create_silent_action(idf_lib_copy)
    env.AddPostAction("checkprogsize", silent_action)

if "espidf" in env.subst("$PIOFRAMEWORK") and (flag_custom_component_add == True or flag_custom_component_remove == True):
    def idf_custom_component(source, target, env):
        try:
            shutil.copy(join(ARDUINO_FRAMEWORK_DIR,"idf_component.yml.orig"),join(ARDUINO_FRAMEWORK_DIR,"idf_component.yml"))
            print("*** Original Arduino \"idf_component.yml\" restored ***")
        except:
            try:
                shutil.copy(join(PROJECT_SRC_DIR,"idf_component.yml.orig"),join(PROJECT_SRC_DIR,"idf_component.yml"))
                print("*** Original \"idf_component.yml\" restored ***")
            except: # no "idf_component.yml" in source folder
                try:
                    os.remove(join(PROJECT_SRC_DIR,"idf_component.yml"))
                    print("*** pioarduino generated \"idf_component.yml\" removed ***")
                except:
                    print("*** no custom \"idf_component.yml\" found for removing ***")
        if "arduino" in env.subst("$PIOFRAMEWORK"):
            # Restore original pioarduino-build.py, only used with Arduino
            from component_manager import ComponentManager
            component_manager = ComponentManager(env)
            component_manager.restore_pioarduino_build_py()
    silent_action = create_silent_action(idf_custom_component)
    env.AddPostAction("checkprogsize", silent_action)
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
    if "arduino" in env.subst("$PIOFRAMEWORK"):
        ota_partition_image = os.path.join(ARDUINO_FRAMEWORK_DIR, "tools", "partitions", "boot_app0.bin")
    else:
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
    EXTRA_IMG_DIR = join(env.subst("$PROJECT_DIR"), "variants", "tasmota")
    env.Append(
        FLASH_EXTRA_IMAGES=[
            (offset, join(EXTRA_IMG_DIR, img)) for offset, img in board.get("upload.arduino.flash_extra_images", [])
        ]
    )

def _parse_size(value):
    if isinstance(value, int):
        return value
    elif value.isdigit():
        return int(value)
    elif value.startswith("0x"):
        return int(value, 16)
    elif value[-1].upper() in ("K", "M"):
        base = 1024 if value[-1].upper() == "K" else 1024 * 1024
        return int(value[:-1]) * base
    return value

#
# Configure application partition offset
#

app_offset = get_app_partition_offset(
    env.subst("$PARTITIONS_TABLE_CSV"),
    partition_table_offset
)

env.Replace(ESP32_APP_OFFSET=app_offset)

#
# Propagate application offset to debug configurations
#

env["INTEGRATION_EXTRA_DATA"].update(
    {"application_offset": env.subst("$ESP32_APP_OFFSET")}
)
