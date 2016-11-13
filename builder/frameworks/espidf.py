# Copyright 2014-present PlatformIO <contact@platformio.org>
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

import sys
from os import listdir, makedirs
from os.path import isdir, join

from SCons.Script import DefaultEnvironment

from platformio.util import exec_command

env = DefaultEnvironment()
platform = env.PioPlatform()

FRAMEWORK_DIR = platform.get_package_dir("framework-espidf")
FRAMEWORK_VERSION = platform.get_package_version(
    "framework-espidf")
assert isdir(FRAMEWORK_DIR)


def generate_ld_script():
    if not isdir(env.subst("$BUILD_DIR")):
        makedirs(env.subst("$BUILD_DIR"))
    result = exec_command([
        join(platform.get_package_dir("toolchain-xtensa32")
             or "", "bin", env.subst("$CC")),
        "-I", env.subst("$PROJECTSRC_DIR"),
        "-C", "-P", "-x", "c", "-E",
        join(env.subst("$ESPIDF_DIR"), "components",
             "esp32", "ld", "esp32.ld"),
        "-o", join(env.subst("$BUILD_DIR"), "esp32_out.ld")
    ])

    if result['returncode'] != 0:
        sys.stderr.write(
            "Cannot create linker script! %s" % result['err'])
        env.Exit(1)


def generate_ptable():
    if not isdir(env.subst("$BUILD_DIR")):
        makedirs(env.subst("$BUILD_DIR"))

    result = exec_command([
        env.subst("$PYTHONEXE"),
        join(env.subst("$ESPIDF_DIR"), "components",
             "partition_table", "gen_esp32part.py"),
        "-q", join(env.subst("$ESPIDF_DIR"), "components",
                   "partition_table", "partitions_singleapp.csv"),
        join(env.subst("$BUILD_DIR"), "partitions_table.bin"),
    ])

    if result['returncode'] != 0:
        sys.stderr.write(
            "Cannot create partition table! %s" % result['err'])
        env.Exit(1)

env.Prepend(
    CPPPATH=[
        join("$PROJECTSRC_DIR"),
        join(FRAMEWORK_DIR, "components", "nghttp", "include"),
        join(FRAMEWORK_DIR, "components", "nghttp", "port", "include"),
        join(FRAMEWORK_DIR, "components", "bt", "include"),
        join(FRAMEWORK_DIR, "components", "driver", "include"),
        join(FRAMEWORK_DIR, "components", "esp32", "include"),
        join(FRAMEWORK_DIR, "components", "freertos", "include"),
        join(FRAMEWORK_DIR, "components", "freertos", "include", "freertos"),
        join(FRAMEWORK_DIR, "components", "log", "include"),
        join(FRAMEWORK_DIR, "components", "newlib", "include"),
        join(FRAMEWORK_DIR, "components", "nvs_flash", "include"),
        join(FRAMEWORK_DIR, "components", "spi_flash", "include"),
        join(FRAMEWORK_DIR, "components", "tcpip_adapter", "include"),
        join(FRAMEWORK_DIR, "components", "lwip", "include", "lwip"),
        join(FRAMEWORK_DIR, "components", "lwip", "include", "lwip", "port"),
        join(FRAMEWORK_DIR, "components", "lwip", "include", "lwip", "posix"),
        join(FRAMEWORK_DIR, "components", "expat", "include", "expat"),
        join(FRAMEWORK_DIR, "components", "expat", "port", "include"),
        join(FRAMEWORK_DIR, "components", "json", "include"),
        join(FRAMEWORK_DIR, "components", "json", "port", "include"),
        join(FRAMEWORK_DIR, "components", "mbedtls", "include"),
        join(FRAMEWORK_DIR, "components", "mbedtls", "port", "include")
    ],

    LIBPATH=[
        join(FRAMEWORK_DIR, "components", "esp32"),
        join(FRAMEWORK_DIR, "components", "esp32", "ld"),
        join(FRAMEWORK_DIR, "components", "esp32", "lib"),
        join(FRAMEWORK_DIR, "components", "bt", "lib"),
        join(FRAMEWORK_DIR, "components", "newlib", "lib"),
        "$BUILD_DIR"
    ],

    LIBS=[
        "hal", "crypto", "core", "net80211", "phy", "rtc", "pp", "wpa",
        "smartconfig", "btdm_app", "m", "c", "gcc"
    ]
)

env.Append(
    LIBSOURCE_DIRS=[
        join(FRAMEWORK_DIR, "libraries")
    ],

    LINKFLAGS=[
        "-T", "esp32.common.ld",
        "-T", "esp32.rom.ld",
        "-T", "esp32.peripherals.ld"
    ],
)

#
# Generate a specific linker script
#

generate_ld_script()
generate_ptable()

#
# Target: Build Core Library
#

libs = []

ignore_dirs = (
    "bootloader", "esptool_py", "idf_test", "newlib", "partition_table")

for d in listdir(join(FRAMEWORK_DIR, "components")):
    if d in ignore_dirs:
        continue
    if isdir(join(FRAMEWORK_DIR, "components", d)):
        libs.append(env.BuildLibrary(
            join("$BUILD_DIR", "%s" % d),
            join(FRAMEWORK_DIR, "components", d),
            src_filter="+<*> -<test>"
        ))

env.Prepend(LIBS=libs)
