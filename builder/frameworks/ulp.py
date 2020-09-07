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

import os

from SCons.Script import Import

from platformio.util import get_systype
from platformio.proc import where_is_program

Import("env project_config idf_variant")

ulp_env = env.Clone()
platform = ulp_env.PioPlatform()
FRAMEWORK_DIR = platform.get_package_dir("framework-espidf")
BUILD_DIR = ulp_env.subst("$BUILD_DIR")
ULP_BUILD_DIR = os.path.join(
    BUILD_DIR, "esp-idf", project_config["name"].replace("__idf_", ""), "ulp_main")


def prepare_ulp_env_vars(env):
    ulp_env.PrependENVPath("IDF_PATH", platform.get_package_dir("framework-espidf"))

    additional_packages = [
        os.path.join(platform.get_package_dir("toolchain-xtensa32"), "bin"),
        os.path.join(platform.get_package_dir("toolchain-esp32ulp"), "bin"),
        platform.get_package_dir("tool-ninja"),
        os.path.join(platform.get_package_dir("tool-cmake"), "bin"),
        os.path.dirname(where_is_program("python")),
    ]

    if "windows" in get_systype():
        additional_packages.append(platform.get_package_dir("tool-mconf"))

    for package in additional_packages:
        ulp_env.PrependENVPath("PATH", package)


def collect_ulp_sources():
    return [
        os.path.join(ulp_env.subst("$PROJECT_DIR"), "ulp", f)
        for f in os.listdir(os.path.join(ulp_env.subst("$PROJECT_DIR"), "ulp"))
    ]


def get_component_includes(target_config):
    for source in target_config.get("sources", []):
        if source["path"].endswith("ulp_main.bin.S"):
            return [
                inc["path"]
                for inc in target_config["compileGroups"][source["compileGroupIndex"]][
                    "includes"
                ]
            ]

    return [os.path.join(BUILD_DIR, "config")]


def generate_ulp_config(target_config):
    ulp_sources = collect_ulp_sources()
    cmd = (
        os.path.join(platform.get_package_dir("tool-cmake"), "bin", "cmake"),
        "-DCMAKE_GENERATOR=Ninja",
        "-DCMAKE_TOOLCHAIN_FILE="
        + os.path.join(
            platform.get_package_dir("framework-espidf"),
            "components",
            "ulp",
            "cmake",
            "toolchain-%s-ulp.cmake" % idf_variant,
        ),
        '-DULP_S_SOURCES="%s"' % ";".join(ulp_sources),
        "-DULP_APP_NAME=ulp_main",
        "-DCOMPONENT_DIR=" + os.path.join(ulp_env.subst("$PROJECT_DIR"), "ulp"),
        '-DCOMPONENT_INCLUDES="%s"' % ";".join(get_component_includes(target_config)),
        "-DIDF_PATH=" + FRAMEWORK_DIR,
        "-DSDKCONFIG=" + os.path.join(BUILD_DIR, "config", "sdkconfig.h"),
        "-DPYTHON=" + env.subst("$PYTHONEXE"),
        "-GNinja",
        "-B",
        ULP_BUILD_DIR,
        os.path.join(FRAMEWORK_DIR, "components", "ulp", "cmake"),
    )

    return ulp_env.Command(
        os.path.join(ULP_BUILD_DIR, "build.ninja"),
        ulp_sources,
        ulp_env.VerboseAction(" ".join(cmd), "Generating ULP configuration"),
    )


def compile_ulp_binary():
    cmd = (
        os.path.join(platform.get_package_dir("tool-cmake"), "bin", "cmake"),
        "--build",
        ULP_BUILD_DIR,
        "--target",
        "build",
    )

    return ulp_env.Command(
        [
            os.path.join(ULP_BUILD_DIR, "ulp_main.h"),
            os.path.join(ULP_BUILD_DIR, "ulp_main.ld"),
            os.path.join(ULP_BUILD_DIR, "ulp_main.bin"),
            os.path.join(ULP_BUILD_DIR, "esp32.ulp.ld"),
        ],
        None,
        ulp_env.VerboseAction(" ".join(cmd), "Generating ULP project files $TARGETS"),
    )


def generate_ulp_assembly():
    cmd = (
        os.path.join(platform.get_package_dir("tool-cmake"), "bin", "cmake"),
        "-DDATA_FILE=$SOURCE",
        "-DSOURCE_FILE=$TARGET",
        "-DFILE_TYPE=BINARY",
        "-P",
        os.path.join(
            FRAMEWORK_DIR, "tools", "cmake", "scripts", "data_file_embed_asm.cmake"
        ),
    )

    return ulp_env.Command(
        os.path.join(BUILD_DIR, "ulp_main.bin.S"),
        os.path.join(ULP_BUILD_DIR, "ulp_main.bin"),
        ulp_env.VerboseAction(" ".join(cmd), "Generating ULP assembly file $TARGET"),
    )


prepare_ulp_env_vars(ulp_env)
ulp_assembly = generate_ulp_assembly()

ulp_env.Depends(compile_ulp_binary(), generate_ulp_config(project_config))
ulp_env.Depends(os.path.join("$BUILD_DIR", "${PROGNAME}.elf"), ulp_assembly)
ulp_env.Requires(os.path.join("$BUILD_DIR", "${PROGNAME}.elf"), ulp_assembly)

env.AppendUnique(CPPPATH=ULP_BUILD_DIR, LIBPATH=ULP_BUILD_DIR)
