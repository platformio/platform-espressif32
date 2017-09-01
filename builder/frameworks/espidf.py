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

from glob import glob
from os import listdir, walk
from os.path import basename, isdir, isfile, join

from shutil import copy
import sys
from SCons.Script import DefaultEnvironment


env = DefaultEnvironment()
platform = env.PioPlatform()

FRAMEWORK_DIR = platform.get_package_dir("framework-espidf")
assert FRAMEWORK_DIR and isdir(FRAMEWORK_DIR)
FRAMEWORK_VERSION = platform.get_package_version(
    "framework-espidf")


def parse_mk(path):
    result = {}
    variable = None
    multi = False
    with open(path) as fp:
        for line in fp.readlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            # remove inline comments
            if " # " in line:
                line = line[:line.index(" # ")]
            if not multi and "=" in line:
                variable, line = line.split("=", 1)
                if variable.endswith((":", "+")):
                    variable = variable[:-1]
                variable = variable.strip()
                line = line.strip()
            if not variable or not line:
                continue
            multi = line.endswith('\\')
            if multi:
                line = line[:-1].strip()
            if variable not in result:
                result[variable] = []
            result[variable].extend([l.strip() for l in line.split()])
            if not multi:
                variable = None
    return result


def build_component(path):
    envsafe = env.Clone()
    src_filter = "+<*> -<test*>"
    if isfile(join(path, "component.mk")):
        params = parse_mk(join(path, "component.mk"))
        if params.get("COMPONENT_PRIV_INCLUDEDIRS"):
            inc_dirs = params.get("COMPONENT_PRIV_INCLUDEDIRS")
            envsafe.Prepend(
                CPPPATH=[join(path, d) for d in inc_dirs])
        if params.get("CFLAGS"):
            envsafe.Append(CCFLAGS=params.get("CFLAGS"))
        if params.get("COMPONENT_OBJS"):
            src_filter = "-<*>"
            for f in params.get("COMPONENT_OBJS"):
                src_filter += " +<%s>" % f.replace(".o", ".c")
        elif params.get("COMPONENT_SRCDIRS"):
            src_filter = "-<*>"
            src_dirs = params.get("COMPONENT_SRCDIRS")
            if "." in src_dirs:
                src_dirs.remove(".")
                src_filter += " +<*.c*>"
            for f in src_dirs:
                src_filter += " +<%s/*.c*>" % f

    return envsafe.BuildLibrary(
        join("$BUILD_DIR", "%s" % basename(path)), path,
        src_filter=src_filter
    )


def build_espidf_bootloader():
    envsafe = env.Clone()
    envsafe.Append(CPPDEFINES=[("BOOTLOADER_BUILD", 1)])
    envsafe.Replace(
        LIBPATH=[
            join(FRAMEWORK_DIR, "components", "esp32", "ld"),
            join(FRAMEWORK_DIR, "components", "esp32", "lib"),
            join(FRAMEWORK_DIR, "components", "bootloader", "src", "main")
        ],

        LINKFLAGS=[
            "-nostdlib",
            "-Wl,-static",
            "-u", "call_user_start_cpu0",
            "-Wl,--gc-sections",
            "-T", "esp32.bootloader.ld",
            "-T", "esp32.rom.ld",
            "-T", "esp32.peripherals.ld",
            "-T", "esp32.bootloader.rom.ld"
        ]
    ),

    envsafe.Append(
        CPPPATH=[
            join(FRAMEWORK_DIR, "components", "esp32")
        ]
    )

    envsafe.Replace(
        LIBS=[
            envsafe.BuildLibrary(
                join("$BUILD_DIR", "bootloaderSupport"),
                join(FRAMEWORK_DIR, "components", "bootloader_support"),
                src_filter="+<*> -<test>"
            ),
            envsafe.BuildLibrary(
                join("$BUILD_DIR", "bootloaderLog"),
                join(FRAMEWORK_DIR, "components", "log")
            ),
            envsafe.BuildLibrary(
                join("$BUILD_DIR", "bootloaderSPIFlash"),
                join(FRAMEWORK_DIR, "components", "spi_flash"),
                src_filter="-<*> +<spi_flash_rom_patch.c>"
            ),
            envsafe.BuildLibrary(
                join("$BUILD_DIR", "bootloaderMicroEcc"),
                join(FRAMEWORK_DIR, "components", "micro-ecc"),
                src_filter="+<*> -<micro-ecc/test>"
            ),
            envsafe.BuildLibrary(
                join("$BUILD_DIR", "bootloaderSoc"),
                join(FRAMEWORK_DIR, "components", "soc"),
                src_filter="+<*> -<test> -<esp32/test>"
            ),
            "gcc", "stdc++"
        ]
    )

    return envsafe.Program(
        join("$BUILD_DIR", "bootloader.elf"),
        envsafe.CollectBuildFiles(
            join("$BUILD_DIR", "bootloader"),
            join(FRAMEWORK_DIR, "components", "bootloader", "src", "main")
        )
    )


env.Prepend(
    CPPPATH=[
        join("$PROJECTSRC_DIR"),
        join(FRAMEWORK_DIR, "components", "aws_iot", "include"),
        join(FRAMEWORK_DIR, "components", "aws_iot",
             "aws-iot-device-sdk-embedded-C", "include"),
        join(FRAMEWORK_DIR, "components", "app_trace", "include"),
        join(FRAMEWORK_DIR, "components", "app_update", "include"),
        join(FRAMEWORK_DIR, "components", "xtensa-debug-module", "include"),
        join(FRAMEWORK_DIR, "components", "bootloader_support", "include"),
        join(FRAMEWORK_DIR, "components",
             "bootloader_support", "include_priv"),
        join(FRAMEWORK_DIR, "components", "bt", "include"),
        join(FRAMEWORK_DIR, "components", "coap", "port", "include"),
        join(FRAMEWORK_DIR, "components", "coap", "port", "include", "coap"),
        join(FRAMEWORK_DIR, "components", "coap", "libcoap", "include"),
        join(FRAMEWORK_DIR, "components", "coap",
             "libcoap", "include", "coap"),
        join(FRAMEWORK_DIR, "components", "cxx", "include"),
        join(FRAMEWORK_DIR, "components", "driver", "include"),
        join(FRAMEWORK_DIR, "components", "driver", "include", "driver"),
        join(FRAMEWORK_DIR, "components", "esp32", "include"),
        join(FRAMEWORK_DIR, "components", "ethernet", "include"),
        join(FRAMEWORK_DIR, "components", "expat", "include", "expat"),
        join(FRAMEWORK_DIR, "components", "expat", "port", "include"),
        join(FRAMEWORK_DIR, "components", "fatfs", "src"),
        join(FRAMEWORK_DIR, "components", "freertos", "include"),
        join(FRAMEWORK_DIR, "components", "jsmn", "include"),
        join(FRAMEWORK_DIR, "components", "json", "include"),
        join(FRAMEWORK_DIR, "components", "json", "port", "include"),
        join(FRAMEWORK_DIR, "components", "libsodium", "libsodium", "src",
             "libsodium", "include"),
        join(FRAMEWORK_DIR, "components", "libsodium", "port_include",
             "sodium"),
        join(FRAMEWORK_DIR, "components", "libsodium", "libsodium", "src",
             "libsodium", "include", "sodium"),
        join(FRAMEWORK_DIR, "components", "log", "include"),
        join(FRAMEWORK_DIR, "components", "lwip", "include", "lwip"),
        join(FRAMEWORK_DIR, "components", "lwip", "include", "lwip", "port"),
        join(FRAMEWORK_DIR, "components", "lwip", "include", "lwip", "posix"),
        join(FRAMEWORK_DIR, "components", "lwip", "apps", "ping"),
        join(FRAMEWORK_DIR, "components", "mbedtls", "port", "include"),
        join(FRAMEWORK_DIR, "components", "mbedtls", "include"),
        join(FRAMEWORK_DIR, "components", "mdns", "include"),
        join(FRAMEWORK_DIR, "components", "micro-ecc", "micro-ecc"),
        join(FRAMEWORK_DIR, "components", "newlib", "include"),
        join(FRAMEWORK_DIR, "components", "newlib", "platform_include"),
        join(FRAMEWORK_DIR, "components", "nghttp", "include"),
        join(FRAMEWORK_DIR, "components", "nghttp", "port", "include"),
        join(FRAMEWORK_DIR, "components", "nvs_flash", "include"),
        join(FRAMEWORK_DIR, "components", "openssl", "include"),
        join(FRAMEWORK_DIR, "components", "openssl", "include", "internal"),
        join(FRAMEWORK_DIR, "components", "openssl", "include", "platform"),
        join(FRAMEWORK_DIR, "components", "openssl", "include", "openssl"),
        join(FRAMEWORK_DIR, "components", "sdmmc", "include"),
        join(FRAMEWORK_DIR, "components", "spi_flash", "include"),
        join(FRAMEWORK_DIR, "components", "tcpip_adapter", "include"),
        join(FRAMEWORK_DIR, "components", "soc", "esp32", "include"),
        join(FRAMEWORK_DIR, "components", "soc", "include"),
        join(FRAMEWORK_DIR, "components", "heap", "include"),
        join(FRAMEWORK_DIR, "components", "ulp", "include"),
        join(FRAMEWORK_DIR, "components", "vfs", "include"),
        join(FRAMEWORK_DIR, "components", "wear_levelling", "include"),
        join(FRAMEWORK_DIR, "components", "wpa_supplicant", "include"),
        join(FRAMEWORK_DIR, "components", "wpa_supplicant", "port", "include")
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
        "btdm_app", "hal", "coexist", "core", "net80211", "phy", "rtc", "pp",
        "wpa", "wpa2", "wps", "smartconfig", "m", "c", "gcc", "stdc++"
    ]
)

for root, dirs, _ in walk(join(
        FRAMEWORK_DIR, "components", "bt", "bluedroid")):
    for d in dirs:
        if (d == "include"):
            env.Append(CPPPATH=[join(root, d)])


env.Prepend(
    CFLAGS=["-Wno-old-style-declaration"],

    CPPDEFINES=[
        "WITH_POSIX",
        ("IDF_VER", '\\"%s\\"' %
         platform.get_package_version("framework-espidf"))
    ],

    CCFLAGS=[
        "-Wall",
        "-Werror=all",
        "-Wno-error=deprecated-declarations",
        "-Wextra",
        "-Wno-unused-parameter",
        "-Wno-sign-compare",
        "-Wno-error=unused-function"
    ],

    LIBSOURCE_DIRS=[join(FRAMEWORK_DIR, "libraries")]
)

env.Append(
    LINKFLAGS=[
        "-u", "__cxa_guard_dummy",
        "-T", "esp32.common.ld",
        "-T", "esp32.rom.ld",
        "-T", "esp32.peripherals.ld"
    ],

    UPLOADERFLAGS=[
        "0x1000", join("$BUILD_DIR", "bootloader.bin"),
        "0x8000", join("$BUILD_DIR", "partitions_table.bin"),
        "0x10000"
    ]
)


#
# Handle missing sdkconfig.h
#

if not isfile(join(env.subst("$PROJECTSRC_DIR"), "sdkconfig.h")):
    search_path = join(
        env.subst("$PIOHOME_DIR"), "platforms",
        env.subst("$PIOPLATFORM"), "examples", "*", "src", "sdkconfig.h"
    )

    files = glob(search_path)
    if not files:
        sys.stderr.write(
            "Error: \"sdkconfig.h\" file is required for esp-idf framework!\n")
        env.Exit(1)

    print("Warning! Cannot find \"sdk_config.h\" file. "
          "Default \"sdk_config.h\" will be added to your project!")
    copy(files[0], join(env.subst("$PROJECTSRC_DIR"), "sdkconfig.h"))


#
# Generate partition table
#

if isfile(join(env.subst("$PROJECTSRC_DIR"), "partitions_table.csv")):
    partition_table = env.Command(
        join("$BUILD_DIR", "partitions_table.bin"),
        join(env.subst("$PROJECTSRC_DIR"), "partitions_table.csv"),
        env.VerboseAction('"$PYTHONEXE" "%s" -q $SOURCE $TARGET' % join(
            FRAMEWORK_DIR, "components", "partition_table", "gen_esp32part.py"),
            "Generating partitions $TARGET"))
else:
    partition_table = env.Command(
        join("$BUILD_DIR", "partitions_table.bin"),
        join(FRAMEWORK_DIR, "components", "partition_table",
            "partitions_singleapp.csv"),
        env.VerboseAction('"$PYTHONEXE" "%s" -q $SOURCE $TARGET' % join(
            FRAMEWORK_DIR, "components", "partition_table", "gen_esp32part.py"),
            "Generating partitions $TARGET"))




env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", partition_table)

#
# Generate linker script
#

linker_script = env.Command(
    join("$BUILD_DIR", "esp32_out.ld"),
    join(FRAMEWORK_DIR, "components", "esp32", "ld", "esp32.ld"),
    env.VerboseAction(
        '$CC -I"$PROJECTSRC_DIR" -C -P -x  c -E $SOURCE -o $TARGET',
        "Generating LD script $TARGET"))

env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", linker_script)

#
# Compile bootloader
#

env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", env.ElfToBin(
    join("$BUILD_DIR", "bootloader"), build_espidf_bootloader()))

#
# Target: Build Core Library
#

libs = []

ignore_dirs = (
    "bootloader",
    "bootloader_support",
    "esptool_py",
    "idf_test",
    "partition_table",
    "nghttp",
    "soc",
    "spi_flash",
    "libsodium"
)

for d in listdir(join(FRAMEWORK_DIR, "components")):
    if d in ignore_dirs:
        continue
    component_dir = join(FRAMEWORK_DIR, "components", d)
    if isdir(component_dir):
        libs.append(build_component(component_dir))


# component.mk contains configuration for bootloader
libs.append(env.BuildLibrary(
    join("$BUILD_DIR", "spi_flash"),
    join(FRAMEWORK_DIR, "components", "spi_flash"),
    src_filter="+<*> -<test*>"
))

libs.append(env.BuildLibrary(
    join("$BUILD_DIR", "app_trace"),
    join(FRAMEWORK_DIR, "components", "app_trace"),
    src_filter="+<*> -<test> -<sys_view>"
))

libs.append(env.BuildLibrary(
    join("$BUILD_DIR", "soc"),
    join(FRAMEWORK_DIR, "components", "soc"),
    src_filter="+<*> -<test> -<esp32/test>"
))

envsafe = env.Clone()
envsafe.Append(
    CPPDEFINES=[
        "CONFIGURED", "NATIVE_LITTLE_ENDIAN", "HAVE_WEAK_SYMBOLS",
        "__STDC_LIMIT_MACROS", "__STDC_CONSTANT_MACROS"
    ],
    CCFLAGS=["-Wno-type-limits", "-Wno-unknown-pragmas"]
)

libs.append(
    envsafe.BuildLibrary(
        join("$BUILD_DIR", "libsodium"),
        join(FRAMEWORK_DIR, "components", "libsodium", "libsodium", "src",
             "libsodium")
    )
)

env.Prepend(LIBS=libs)
