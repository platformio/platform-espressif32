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
from os.path import abspath, basename, isdir, isfile, join

from shutil import copy
import sys
from SCons.Script import DefaultEnvironment


env = DefaultEnvironment()
platform = env.PioPlatform()

env.SConscript("_bare.py", exports="env")
env.SConscript("_embedtxt_files.py", exports="env")

FRAMEWORK_DIR = platform.get_package_dir("framework-espidf")
assert FRAMEWORK_DIR and isdir(FRAMEWORK_DIR)


def parse_mk(path):
    result = {}
    variable = None
    multi = False
    with open(path) as fp:
        for line in fp.readlines():
            line = line.strip()
            if not line or line.startswith("#"):
                multi = False
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
                src_filter += " +<*.[sSc]*>"
            for f in src_dirs:
                src_filter += " +<%s/*.[sSc]*>" % f
        if params.get("COMPONENT_OBJEXCLUDE"):
            for f in params.get("COMPONENT_OBJEXCLUDE"):
                src_filter += " -<%s>" % f.replace(".o", ".c")

    return envsafe.BuildLibrary(
        join("$BUILD_DIR", "%s" % basename(path)), path,
        src_filter=src_filter
    )


def find_valid_config_file():
    search_path = join(
        platform.get_dir(), "examples", "*", "src", "sdkconfig.h")
    files = glob(search_path)
    if not files:
        sys.stderr.write(
            "Error: Could not find default \"sdkconfig.h\" file\n")
        env.Exit(1)
    return files[0]


def build_espidf_bootloader():
    envsafe = env.Clone()
    envsafe.Append(CPPDEFINES=[("BOOTLOADER_BUILD", 1)])
    envsafe.Replace(
        LIBPATH=[
            join(FRAMEWORK_DIR, "components", "esp32", "ld"),
            join(FRAMEWORK_DIR, "components", "esp32", "lib"),
            join(FRAMEWORK_DIR, "components", "bootloader", "subproject", "main")
        ],

        LINKFLAGS=[
            "-nostdlib",
            "-Wl,-static",
            "-u", "call_user_start_cpu0",
            "-Wl,--gc-sections",
            "-T", "esp32.bootloader.ld",
            "-T", "esp32.rom.ld",
            "-T", "esp32.rom.spiram_incompatible_fns.ld",
            "-T", "esp32.peripherals.ld",
            "-T", "esp32.bootloader.rom.ld",
        ]
    ),

    envsafe.Append(
        CPPPATH=[
            join(FRAMEWORK_DIR, "components", "esp32"),
            join(FRAMEWORK_DIR, "components", "bootloader_support", "include_priv")
        ]
    )

    envsafe.Replace(
        LIBS=[
            envsafe.BuildLibrary(
                join("$BUILD_DIR", "bootloader", "bootloader_support"),
                join(FRAMEWORK_DIR, "components", "bootloader_support"),
                src_filter="+<*> -<test>"
            ),
            envsafe.BuildLibrary(
                join("$BUILD_DIR", "bootloader", "log"),
                join(FRAMEWORK_DIR, "components", "log")
            ),
            envsafe.BuildLibrary(
                join("$BUILD_DIR", "bootloader", "spi_flash"),
                join(FRAMEWORK_DIR, "components", "spi_flash"),
                src_filter="-<*> +<spi_flash_rom_patch.c>"
            ),
            envsafe.BuildLibrary(
                join("$BUILD_DIR", "bootloader", "micro-ecc"),
                join(FRAMEWORK_DIR, "components", "micro-ecc"),
                src_filter="+<*> -<micro-ecc/test>"
            ),
            envsafe.BuildLibrary(
                join("$BUILD_DIR", "bootloader", "soc"),
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
            join(FRAMEWORK_DIR, "components", "bootloader", "subproject", "main")
        )
    )


env.Prepend(
    CPPPATH=[
        join(FRAMEWORK_DIR, "components", "app_trace", "include"),
        join(FRAMEWORK_DIR, "components", "app_update", "include"),
        join(FRAMEWORK_DIR, "components", "aws_iot", "include"),
        join(FRAMEWORK_DIR, "components", "aws_iot",
             "aws-iot-device-sdk-embedded-C", "include"),
        join(FRAMEWORK_DIR, "components", "bootloader_support", "include"),
        join(FRAMEWORK_DIR, "components", "bootloader_support", "include_priv"),
        join(FRAMEWORK_DIR, "components", "bt", "include"),
        join(FRAMEWORK_DIR, "components", "bt", "bluedroid", "api", "include", "api"),
        join(FRAMEWORK_DIR, "components", "coap", "port", "include"),
        join(FRAMEWORK_DIR, "components", "coap", "port", "include", "coap"),
        join(FRAMEWORK_DIR, "components", "coap", "libcoap", "include"),
        join(FRAMEWORK_DIR, "components", "coap",
             "libcoap", "include", "coap"),
        join(FRAMEWORK_DIR, "components", "console"),
        join(FRAMEWORK_DIR, "components", "driver", "include"),
        join(FRAMEWORK_DIR, "components", "esp-tls"),
        join(FRAMEWORK_DIR, "components", "esp_adc_cal", "include"),
        join(FRAMEWORK_DIR, "components", "esp_http_client", "include"),
        join(FRAMEWORK_DIR, "components", "esp_https_ota", "include"),
        join(FRAMEWORK_DIR, "components", "esp32", "include"),
        join(FRAMEWORK_DIR, "components", "ethernet", "include"),
        join(FRAMEWORK_DIR, "components", "expat", "port", "include"),
        join(FRAMEWORK_DIR, "components", "expat", "include", "expat"),
        join(FRAMEWORK_DIR, "components", "fatfs", "src"),
        join(FRAMEWORK_DIR, "components", "freertos", "include"),
        join(FRAMEWORK_DIR, "components", "heap", "include"),
        join(FRAMEWORK_DIR, "components", "jsmn", "include"),
        join(FRAMEWORK_DIR, "components", "json", "cJSON"),
        join(FRAMEWORK_DIR, "components", "libsodium", "libsodium", "src",
             "libsodium", "include"),
        join(FRAMEWORK_DIR, "components", "libsodium", "port_include"),
        join(FRAMEWORK_DIR, "components", "log", "include"),
        join(FRAMEWORK_DIR, "components", "lwip", "include", "lwip"),
        join(FRAMEWORK_DIR, "components", "lwip", "include", "lwip", "port"),
        join(FRAMEWORK_DIR, "components", "lwip", "include", "lwip", "posix"),
        join(FRAMEWORK_DIR, "components", "lwip", "apps", "ping"),
        join("$PROJECTSRC_DIR"),
        join(FRAMEWORK_DIR, "components", "mbedtls", "port", "include"),
        join(FRAMEWORK_DIR, "components", "mbedtls", "mbedtls", "include"),
        join(FRAMEWORK_DIR, "components", "mdns", "include"),
        join(FRAMEWORK_DIR, "components", "micro-ecc", "micro-ecc"),
        join(FRAMEWORK_DIR, "components", "nghttp", "nghttp2", "lib", "includes"),
        join(FRAMEWORK_DIR, "components", "nghttp", "port", "include"),
        join(FRAMEWORK_DIR, "components", "newlib", "platform_include"),
        join(FRAMEWORK_DIR, "components", "newlib", "include"),
        join(FRAMEWORK_DIR, "components", "nvs_flash", "include"),
        join(FRAMEWORK_DIR, "components", "openssl", "include"),
        join(FRAMEWORK_DIR, "components", "pthread", "include"),
        join(FRAMEWORK_DIR, "components", "sdmmc", "include"),
        join(FRAMEWORK_DIR, "components", "smartconfig_ack", "include"),
        join(FRAMEWORK_DIR, "components", "soc", "esp32", "include"),
        join(FRAMEWORK_DIR, "components", "soc", "include"),
        join(FRAMEWORK_DIR, "components", "spi_flash", "include"),
        join(FRAMEWORK_DIR, "components", "spiffs", "include"),
        join(FRAMEWORK_DIR, "components", "tcpip_adapter", "include"),
        join(FRAMEWORK_DIR, "components", "ulp", "include"),
        join(FRAMEWORK_DIR, "components", "vfs", "include"),
        join(FRAMEWORK_DIR, "components", "wear_levelling", "include"),
        join(FRAMEWORK_DIR, "components", "wpa_supplicant", "include"),
        join(FRAMEWORK_DIR, "components", "wpa_supplicant", "port", "include"),
        join(FRAMEWORK_DIR, "components", "xtensa-debug-module", "include")
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
        "wpa", "wpa2", "espnow", "wps", "smartconfig", "mesh", "c", "m",
        "gcc", "stdc++"
    ]
)

for root, dirs, _ in walk(join(
        FRAMEWORK_DIR, "components", "bt", "bluedroid")):
    for d in dirs:
        if (d == "include"):
            env.Prepend(CPPPATH=[join(root, d)])


env.Prepend(
    CFLAGS=["-Wno-old-style-declaration"],

    CPPDEFINES=[
        "WITH_POSIX",
        ("IDF_VER", '\\"%s\\"' %
         platform.get_package_version("framework-espidf"))
    ],

    CCFLAGS=[
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
        "-u", "ld_include_panic_highint_hdl",
        "-T", "esp32.common.ld",
        "-T", "esp32.rom.ld",
        "-T", "esp32.peripherals.ld",
        "-T", "esp32.rom.libgcc.ld",
        "-T", "esp32.rom.spiram_incompatible_fns.ld"
    ],

    FLASH_EXTRA_IMAGES=[
        ("0x1000", join("$BUILD_DIR", "bootloader.bin")),
        ("0x8000", join("$BUILD_DIR", "partitions.bin"))
    ]
)


if "PIO_FRAMEWORK_ESP_IDF_ENABLE_EXCEPTIONS" in env.Flatten(
        env.get("CPPDEFINES", [])):

    # remove unnecessary flag defined in main.py that disables exceptions
    try:
        index = env['CXXFLAGS'].index("-fno-exceptions")
        if index > 0:
            env['CXXFLAGS'].remove("-fno-exceptions")
    except IndexError:
        pass

    env.Append(
        CPPDEFINES=[
            ("CONFIG_CXX_EXCEPTIONS", 1),
            ("CONFIG_CXX_EXCEPTIONS_EMG_POOL_SIZE", 0)
        ],

        CXXFLAGS=["-fexceptions"]
    )

else:
    env.Append(LINKFLAGS=["-u", "__cxx_fatal_exception"])


#
# ESP-IDF doesn't need assembler-with-cpp option
#

env.Replace(ASFLAGS=[])

#
# Handle missing sdkconfig.h
#

if not isfile(join(env.subst("$PROJECTSRC_DIR"), "sdkconfig.h")):
    print("Warning! Cannot find \"sdkconfig.h\" file. "
          "Default \"sdkconfig.h\" will be added to your project!")
    copy(find_valid_config_file(), join(env.subst("$PROJECTSRC_DIR"), "sdkconfig.h"))
else:
    is_new = False
    with open(join(env.subst("$PROJECTSRC_DIR"), "sdkconfig.h")) as fp:
        for l in fp.readlines():
            if "CONFIG_PARTITION_TABLE_OFFSET" in l:
                is_new = True
                break

    if not is_new:
        print("Warning! Detected an outdated \"sdkconfig.h\" file. "
              "The old \"sdkconfig.h\" will be replaced by the new one.")

        new_config = find_valid_config_file()
        copy(
            join(env.subst("$PROJECTSRC_DIR"), "sdkconfig.h"),
            join(env.subst("$PROJECTSRC_DIR"), "sdkconfig.h.bak")
        )
        copy(new_config, join(env.subst("$PROJECTSRC_DIR"), "sdkconfig.h"))


#
# Generate partition table
#

fwpartitions_dir = join(FRAMEWORK_DIR, "components", "partition_table")
partitions_csv = env.BoardConfig().get("build.partitions",
                                       "partitions_singleapp.csv")
env.Replace(
    PARTITIONS_TABLE_CSV=abspath(
        join(fwpartitions_dir, partitions_csv) if isfile(
            join(fwpartitions_dir, partitions_csv)) else partitions_csv))

partition_table = env.Command(
    join("$BUILD_DIR", "partitions.bin"),
    "$PARTITIONS_TABLE_CSV",
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
    "app_trace",
    "aws_iot",
    "espcoredump",
    "bootloader",
    "bootloader_support",
    "heap",
    "esptool_py",
    "freertos",
    "idf_test",
    "partition_table",
    "soc",
    "spi_flash",
    "libsodium",
    "wpa_supplicant"
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
    src_filter="+<*> -<test*> -<sim>"
))

libs.append(env.BuildLibrary(
    join("$BUILD_DIR", "app_trace"),
    join(FRAMEWORK_DIR, "components", "app_trace"),
    src_filter="+<*> -<test> -<sys_view> -<gcov>"
))

libs.append(env.BuildLibrary(
    join("$BUILD_DIR", "bootloader_support"),
    join(FRAMEWORK_DIR, "components", "bootloader_support"),
    src_filter="+<*> -<test> -<src/bootloader_init.c>"
))

libs.append(env.BuildLibrary(
    join("$BUILD_DIR", "soc"),
    join(FRAMEWORK_DIR, "components", "soc"),
    src_filter="+<*> -<test> -<esp32/test>"
))

libs.append(env.BuildLibrary(
    join("$BUILD_DIR", "heap"),
    join(FRAMEWORK_DIR, "components", "heap"),
    src_filter="+<*> -<test*> -<multi_heap_poisoning.c>"
))

libs.append(env.BuildLibrary(
    join("$BUILD_DIR", "aws_iot"),
    join(FRAMEWORK_DIR, "components", "aws_iot"),
    src_filter="-<*> +<port> +<aws-iot-device-sdk-embedded-C/src>"
))

envsafe = env.Clone()
envsafe.Prepend(
    CPPDEFINES=["_ESP_FREERTOS_INTERNAL"],
    CPPPATH=[
        join(FRAMEWORK_DIR, "components", "freertos", "include", "freertos")
    ]
)

libs.append(
    envsafe.BuildLibrary(
        join("$BUILD_DIR", "freertos"),
        join(FRAMEWORK_DIR, "components", "freertos"),
        src_filter="+<*> -<test*>"
    )
)

envsafe = env.Clone()
envsafe.Prepend(
    CPPDEFINES=[
        "CONFIGURED", "NATIVE_LITTLE_ENDIAN", "HAVE_WEAK_SYMBOLS",
        "__STDC_LIMIT_MACROS", "__STDC_CONSTANT_MACROS",
        "RANDOMBYTES_DEFAULT_IMPLEMENTATION"
    ],
    CCFLAGS=["-Wno-type-limits", "-Wno-unknown-pragmas"],
    CPPPATH=[
        join(FRAMEWORK_DIR, "components", "libsodium", "port"),
        join(FRAMEWORK_DIR, "components", "libsodium", "port_include", "sodium"),
        join(FRAMEWORK_DIR, "components", "libsodium", "libsodium", "src",
             "libsodium", "include", "sodium")
    ]
)

libs.append(
    envsafe.BuildLibrary(
        join("$BUILD_DIR", "libsodium"),
        join(FRAMEWORK_DIR, "components", "libsodium"),
        src_filter="-<*> +<libsodium/src> +<port>"
    )
)

envsafe = env.Clone()
envsafe.Prepend(
    CPPDEFINES=[
        "EMBEDDED_SUPP", "IEEE8021X_EAPOL", "EAP_PEER_METHOD", "EAP_MSCHAPv2",
        "EAP_TTLS", "EAP_TLS", "EAP_PEAP", "USE_WPA2_TASK", "CONFIG_WPS2",
        "CONFIG_WPS_PIN", "USE_WPS_TASK", "ESPRESSIF_USE", "ESP32_WORKAROUND",
        "__ets__"
    ],
    CCFLAGS=["-Wno-strict-aliasing"]
)

libs.append(
    envsafe.BuildLibrary(
        join("$BUILD_DIR", "wpa_supplicant"),
        join(FRAMEWORK_DIR, "components", "wpa_supplicant")
    )
)

build_arduino_framework = False
build_arduino_main = False
if isfile(join(env.subst("$PROJECTSRC_DIR"), "sdkconfig.h")):
    with open(join(env.subst("$PROJECTSRC_DIR"), "sdkconfig.h")) as fp:
        for l in fp.readlines():
            if "ENABLE_ARDUINO_DEPENDS" in l and l.rstrip('\r\n').split(' ')[2] == '1':
                build_arduino_framework = True
            if "CONFIG_AUTOSTART_ARDUINO" in l and l.rstrip('\r\n').split(' ')[2] == '1':
                build_arduino_main = True

if build_arduino_framework:
    ARDUINO_FRAMEWORK_DIR = platform.get_package_dir("framework-arduinoespressif32")
    assert isdir(ARDUINO_FRAMEWORK_DIR)
    print 'Including Arduino-ESP32 from %s' % ARDUINO_FRAMEWORK_DIR
    env.Prepend(
        ASFLAGS=["-x", "assembler-with-cpp"],

        CFLAGS=[
            "-std=gnu99"
        ],

        CCFLAGS=[
            "-nostdlib",
            "-Wpointer-arith",
            "-Wno-error=unused-but-set-variable",
            "-Wno-error=unused-variable",
            "-mlongcalls",
            "-ffunction-sections",
            "-fdata-sections",
            "-fstrict-volatile-bitfields",
            "-fexceptions"
        ],

        CXXFLAGS=[
            "-fno-rtti",
            "-std=gnu++11"
        ],

        LINKFLAGS=[
            "-nostdlib",
            "-Wl,-static",
            "-u", "call_user_start_cpu0",
            "-Wl,--undefined=uxTopUsedPriority",
            "-Wl,--gc-sections",
            "-Wl,-EL",
            "-u", "__cxx_fatal_exception"
        ],

        CPPDEFINES=[
            "ESP32",
            "ESP_PLATFORM",
            ("F_CPU", "$BOARD_F_CPU"),
            ("ARDUINO", 10805),
            "ARDUINO_ARCH_ESP32",
            ("ARDUINO_VARIANT", '\\"%s\\"' % env.BoardConfig().get("build.variant").replace('"', "")),
            ("ARDUINO_BOARD", '\\"%s\\"' % env.BoardConfig().get("name").replace('"', ""))
        ],
        LIBSOURCE_DIRS=[
            join(ARDUINO_FRAMEWORK_DIR, "libraries")
        ],
        CPPPATH=[
            join(ARDUINO_FRAMEWORK_DIR, "cores", env.BoardConfig().get("build.core")),
            join(ARDUINO_FRAMEWORK_DIR, "cores", env.BoardConfig().get("build.core"), "libb64"),
        ]
    )
    if "build.variant" in env.BoardConfig():
        env.Append(
            CPPPATH=[
                join(ARDUINO_FRAMEWORK_DIR, "variants",
                    env.BoardConfig().get("build.variant"))
            ]
        )
        libs.append(env.BuildLibrary(
            join("$BUILD_DIR", "FrameworkArduinoVariant"),
            join(ARDUINO_FRAMEWORK_DIR, "variants", env.BoardConfig().get("build.variant"))
        ))
    src_filter = "+<cores/%s/*>" % env.BoardConfig().get("build.core")
    if not build_arduino_main:
        src_filter += " -<cores/%s/main.cpp>" % env.BoardConfig().get("build.core")
    else:
        env.Append(
            CPPDEFINES=[
                ("CONFIG_AUTOSTART_ARDUINO", "1")
            ]
        )
    libs.append(
        env.BuildLibrary(join("$BUILD_DIR", "ArduinoFramework"),
            ARDUINO_FRAMEWORK_DIR,
            src_filter=src_filter)
    )

env.Prepend(LIBS=libs)
