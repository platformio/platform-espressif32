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

ulp_lib = None
ulp_dir = join(env.subst("$PROJECT_DIR"), "ulp")
if isdir(ulp_dir) and listdir(ulp_dir):
    ulp_lib = env.SConscript("ulp.py", exports="env")

FRAMEWORK_DIR = platform.get_package_dir("framework-espidf")
assert FRAMEWORK_DIR and isdir(FRAMEWORK_DIR)


def get_toolchain_version():

    def get_original_version(version):
        if version.count(".") != 2:
            return None
        _, y = version.split(".")[:2]
        if int(y) < 100:
            return None
        if len(y) % 2 != 0:
            y = "0" + y
        parts = [str(int(y[i * 2:i * 2 + 2])) for i in range(int(len(y) / 2))]
        return ".".join(parts)

    return get_original_version(
        platform.get_package_version("toolchain-xtensa32"))


def is_ulp_enabled(sdk_params):
    ulp_memory = int(sdk_params.get("CONFIG_ULP_COPROC_RESERVE_MEM", 0))
    ulp_enabled = int(sdk_params.get("CONFIG_ULP_COPROC_ENABLED", 0))
    return ulp_memory > 0 and ulp_enabled != 0


def is_arduino_autostart(sdk_params):
    arduino_auto_start = int(sdk_params.get("CONFIG_AUTOSTART_ARDUINO", 0))
    return arduino_auto_start > 0


def is_arduino_enabled(sdk_params):
    arduino_enabled = int(sdk_params.get("CONFIG_ENABLE_ARDUINO_DEPENDS", 0))
    return arduino_enabled > 0


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


def extract_component_config(path):
    inc_dirs = []
    cc_flags = []
    src_filter = "+<*> -<test*>"  # default src_filter
    if isfile(join(path, "component.mk")):
        params = parse_mk(join(path, "component.mk"))
        if params.get("COMPONENT_PRIV_INCLUDEDIRS"):
            inc_dirs.extend(params.get("COMPONENT_PRIV_INCLUDEDIRS"))
        if params.get("CFLAGS"):
            cc_flags.extend(params.get("CFLAGS"))
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
                src_filter += " -<%s>" % f.replace(".o", ".[sSc]*")

    return {
        "inc_dirs": inc_dirs,
        "cc_flags": cc_flags,
        "src_filter": src_filter
    }


def build_component(path, component_config):

    envsafe = env.Clone()
    envsafe.Prepend(
        CPPPATH=[join(path, d) for d in component_config.get("inc_dirs", [])],
        CCFLAGS=component_config.get("cc_flags", []),
        CPPDEFINES=component_config.get("cpp_defines", []),
    )

    return envsafe.BuildLibrary(
        join("$BUILD_DIR", "%s" % basename(path)), path,
        src_filter=component_config.get("src_filter", "+<*> -<test*>")
    )


def get_sdk_configuration(config_path):
    if not isfile(config_path):
        sys.stderr.write(
            "Error: Could not find \"sdkconfig.h\" file\n")
        env.Exit(1)

    config = {}
    with open(config_path) as fp:
        for l in fp.readlines():
            if not l.startswith("#define"):
                continue
            values = l.split()
            config[values[1]] = values[2]

    return config


def is_set(parameter, configuration):
    if int(configuration.get(parameter, 0)):
        return True
    return False


def find_valid_config_file():
    search_path = join(
        platform.get_dir(), "examples", "*", "src", "sdkconfig.h")
    files = glob(search_path)
    if not files:
        sys.stderr.write(
            "Error: Could not find default \"sdkconfig.h\" file\n")
        env.Exit(1)
    return files[0]


def build_lwip_lib(sdk_params):
    src_dirs = [
        "apps/dhcpserver",
        "apps/ping",
        "lwip/src/api",
        "lwip/src/apps/sntp",
        "lwip/src/core",
        "lwip/src/core/ipv4",
        "lwip/src/core/ipv6",
        "lwip/src/netif",
        "port/esp32",
        "port/esp32/freertos",
        "port/esp32/netif",
        "port/esp32/debug"
    ]

    # PPP support can be enabled in sdkconfig.h
    if int(sdk_params.get("CONFIG_PPP_SUPPORT", 0)):
        src_dirs.extend(
            ["lwip/src/netif/ppp", "lwip/src/netif/ppp/polarssl"])

    src_filter = "-<*>"
    for d in src_dirs:
        src_filter += " +<%s>" % d

    config = {
        "cc_flags": ["-Wno-address"],
        "src_filter": src_filter
    }

    return build_component(
        join(FRAMEWORK_DIR, "components", "lwip"), config)


def build_protocomm_lib(sdk_params):
    src_dirs = [
        "src/common",
        "src/security",
        "proto-c",
        "src/simple_ble",
        "src/transports"
    ]

    src_filter = "-<*>"
    for d in src_dirs:
        src_filter += " +<%s>" % d

    if not (is_set("CONFIG_BT_ENABLED", sdk_params) and is_set(
            "CONFIG_BLUEDROID_ENABLED", sdk_params)):
        src_filter += " -<src/simple_ble/simple_ble.c>"
        src_filter += " -<src/transports/protocomm_ble.c>"

    inc_dirs = ["proto-c", join("src", "common"), join("src", "simple_ble")]

    config = {
        "inc_dirs": inc_dirs,
        "src_filter": src_filter
    }

    return build_component(
        join(FRAMEWORK_DIR, "components", "protocomm"), config)


def build_rtos_lib():
    config = {
        "cpp_defines": ["_ESP_FREERTOS_INTERNAL"],
        "inc_dirs": [join("include", "freertos")]
    }

    return build_component(
        join(FRAMEWORK_DIR, "components", "freertos"), config)


def build_libsodium_lib():
    defines = ["CONFIGURED", "NATIVE_LITTLE_ENDIAN", "HAVE_WEAK_SYMBOLS",
               "__STDC_LIMIT_MACROS", "__STDC_CONSTANT_MACROS",
               "RANDOMBYTES_DEFAULT_IMPLEMENTATION"]

    inc_dirs = [
        "port",
        join("port_include", "sodium"),
        join("libsodium", "src", "libsodium", "include", "sodium")
    ]

    config = {
        "cpp_defines": defines,
        "cc_flags": ["-Wno-type-limits", "-Wno-unknown-pragmas"],
        "inc_dirs": inc_dirs,
        "src_filter": "-<*> +<libsodium/src> +<port>"
    }

    return build_component(
        join(FRAMEWORK_DIR, "components", "libsodium"), config)


def build_wpa_supplicant_lib():
    defines = ["EMBEDDED_SUPP", "IEEE8021X_EAPOL", "EAP_PEER_METHOD",
               "EAP_MSCHAPv2", "EAP_TTLS", "EAP_TLS", "EAP_PEAP",
               "USE_WPA2_TASK", "CONFIG_WPS2", "CONFIG_WPS_PIN",
               "USE_WPS_TASK", "ESPRESSIF_USE", "ESP32_WORKAROUND",
               "CONFIG_ECC", "__ets__"]

    config = {
        "cpp_defines": defines,
        "cc_flags": ["-Wno-strict-aliasing"]
    }

    return build_component(
        join(FRAMEWORK_DIR, "components", "wpa_supplicant"), config)


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
        join(FRAMEWORK_DIR, "components", "asio", "asio", "asio", "include"),
        join(FRAMEWORK_DIR, "components", "asio", "port", "include"),
        join(FRAMEWORK_DIR, "components", "aws_iot", "include"),
        join(FRAMEWORK_DIR, "components", "aws_iot",
             "aws-iot-device-sdk-embedded-C", "include"),
        join(FRAMEWORK_DIR, "components", "bootloader_support", "include"),
        join(FRAMEWORK_DIR, "components", "bootloader_support", "include_bootloader"),
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
        join(FRAMEWORK_DIR, "components", "esp_event", "include"),
        join(FRAMEWORK_DIR, "components", "esp_http_client", "include"),
        join(FRAMEWORK_DIR, "components", "esp_http_server", "include"),
        join(FRAMEWORK_DIR, "components", "esp_https_ota", "include"),
        join(FRAMEWORK_DIR, "components", "esp_ringbuf", "include"),
        join(FRAMEWORK_DIR, "components", "esp32", "include"),
        join(FRAMEWORK_DIR, "components", "ethernet", "include"),
        join(FRAMEWORK_DIR, "components", "expat", "expat", "lib"),
        join(FRAMEWORK_DIR, "components", "expat", "port", "include"),
        join(FRAMEWORK_DIR, "components", "fatfs", "src"),
        join(FRAMEWORK_DIR, "components", "freemodbus", "modbus", "include"),
        join(FRAMEWORK_DIR, "components", "freemodbus", "modbus_controller"),
        join(FRAMEWORK_DIR, "components", "freertos", "include"),
        join(FRAMEWORK_DIR, "components", "heap", "include"),
        join(FRAMEWORK_DIR, "components", "jsmn", "include"),
        join(FRAMEWORK_DIR, "components", "json", "cJSON"),
        join(FRAMEWORK_DIR, "components", "libsodium", "libsodium", "src",
             "libsodium", "include"),
        join(FRAMEWORK_DIR, "components", "libsodium", "port_include"),
        join(FRAMEWORK_DIR, "components", "log", "include"),
        join(FRAMEWORK_DIR, "components", "lwip", "include", "apps"),
        join(FRAMEWORK_DIR, "components", "lwip", "lwip", "src", "include"),
        join(FRAMEWORK_DIR, "components", "lwip", "port", "esp32", "include"),
        join(FRAMEWORK_DIR, "components", "lwip", "port", "esp32", "include", "arch"),
        join(FRAMEWORK_DIR, "components", "include_compat"),
        join("$PROJECTSRC_DIR"),
        join(FRAMEWORK_DIR, "components", "mbedtls", "port", "include"),
        join(FRAMEWORK_DIR, "components", "mbedtls", "mbedtls", "include"),
        join(FRAMEWORK_DIR, "components", "mdns", "include"),
        join(FRAMEWORK_DIR, "components", "micro-ecc", "micro-ecc"),
        join(FRAMEWORK_DIR, "components", "mqtt", "esp-mqtt", "include"),
        join(FRAMEWORK_DIR, "components", "nghttp", "nghttp2", "lib", "includes"),
        join(FRAMEWORK_DIR, "components", "nghttp", "port", "include"),
        join(FRAMEWORK_DIR, "components", "newlib", "platform_include"),
        join(FRAMEWORK_DIR, "components", "newlib", "include"),
        join(FRAMEWORK_DIR, "components", "nvs_flash", "include"),
        join(FRAMEWORK_DIR, "components", "openssl", "include"),
        join(FRAMEWORK_DIR, "components", "protobuf-c", "protobuf-c"),
        join(FRAMEWORK_DIR, "components", "protocomm", "include", "common"),
        join(FRAMEWORK_DIR, "components", "protocomm", "include", "security"),
        join(FRAMEWORK_DIR, "components", "protocomm", "include", "transports"),
        join(FRAMEWORK_DIR, "components", "pthread", "include"),
        join(FRAMEWORK_DIR, "components", "sdmmc", "include"),
        join(FRAMEWORK_DIR, "components", "smartconfig_ack", "include"),
        join(FRAMEWORK_DIR, "components", "soc", "esp32", "include"),
        join(FRAMEWORK_DIR, "components", "soc", "include"),
        join(FRAMEWORK_DIR, "components", "spi_flash", "include"),
        join(FRAMEWORK_DIR, "components", "spiffs", "include"),
        join(FRAMEWORK_DIR, "components", "tcp_transport", "include"),
        join(FRAMEWORK_DIR, "components", "tcpip_adapter", "include"),
        join(FRAMEWORK_DIR, "components", "ulp", "include"),
        join(FRAMEWORK_DIR, "components", "vfs", "include"),
        join(FRAMEWORK_DIR, "components", "wear_levelling", "include"),
        join(FRAMEWORK_DIR, "components", "wifi_provisioning", "include"),
        join(FRAMEWORK_DIR, "components", "wpa_supplicant", "include"),
        join(FRAMEWORK_DIR, "components", "wpa_supplicant", "port", "include"),
        join(FRAMEWORK_DIR, "components", "xtensa-debug-module", "include")
    ],

    LIBPATH=[
        join(FRAMEWORK_DIR, "components", "esp32"),
        join(FRAMEWORK_DIR, "components", "esp32", "ld"),
        join(FRAMEWORK_DIR, "components", "esp32", "ld", "wifi_iram_opt"),
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
    ],

    CPPDEFINES=[
        ("GCC_NOT_5_2_0", "%d" % 1 if get_toolchain_version() != "5.2.0" else 0)
    ]
)

cppdefines = env.Flatten(env.get("CPPDEFINES", []))

if "PIO_FRAMEWORK_ESP_IDF_ENABLE_EXCEPTIONS" in cppdefines:

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

sdk_config_file = join(env.subst("$PROJECTSRC_DIR"), "sdkconfig.h")

if not isfile(sdk_config_file):
    print("Warning! Cannot find \"sdkconfig.h\" file. "
          "Default \"sdkconfig.h\" will be added to your project!")
    copy(find_valid_config_file(), sdk_config_file)
else:
    is_new = False
    with open(sdk_config_file) as fp:
        for l in fp.readlines():
            if "CONFIG_PTHREAD_STACK_MIN" in l:
                is_new = True
                break

    if not is_new:
        print("Warning! Detected an outdated \"sdkconfig.h\" file. "
              "The old \"sdkconfig.h\" will be replaced by the new one.")

        new_config = find_valid_config_file()
        copy(
            sdk_config_file,
            join(env.subst("$PROJECTSRC_DIR"), "sdkconfig.h.bak")
        )
        copy(new_config, sdk_config_file)

sdk_params = get_sdk_configuration(sdk_config_file)

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
    join("$BUILD_DIR", "partitions.bin"), "$PARTITIONS_TABLE_CSV",
    env.VerboseAction(
        '"$PYTHONEXE" "%s" -q --flash-size "%s" $SOURCE $TARGET' % (join(
            FRAMEWORK_DIR, "components",
            "partition_table", "gen_esp32part.py"), env.BoardConfig().get(
                "upload.flash_size", "detect")),
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

if ulp_lib:
    if not is_ulp_enabled(sdk_params):
        print("Warning! ULP is not properly configured."
              "Add next configuration lines to your sdkconfig.h:")
        print ("    #define CONFIG_ULP_COPROC_ENABLED 1")
        print ("    #define CONFIG_ULP_COPROC_RESERVE_MEM 1024")

    libs.append(ulp_lib)
    env.Append(
        CPPPATH=[join("$BUILD_DIR", "ulp_app")],
        LIBPATH=[join("$BUILD_DIR", "ulp_app")],
        LINKFLAGS=["-T", "ulp_main.ld"]
    )

if is_arduino_enabled(sdk_params):
    ARDUINO_FRAMEWORK_DIR = platform.get_package_dir("framework-arduinoespressif32")
    if ARDUINO_FRAMEWORK_DIR is None:
        platform.install_packages(["framework-arduinoespressif32"])
        ARDUINO_FRAMEWORK_DIR = platform.get_package_dir("framework-arduinoespressif32")
    assert ARDUINO_FRAMEWORK_DIR and isdir(ARDUINO_FRAMEWORK_DIR)
    print('Including Arduino-ESP32 from %s' % ARDUINO_FRAMEWORK_DIR)
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
            ("ARDUINO_BOARD", '\\"%s\\"' % env.BoardConfig().get("name").replace('"', "")),
            ("ARDUINO_ESP32_VER", '\\"%s\\"' % platform.get_package_version("framework-arduinoespressif32"))
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
                join(ARDUINO_FRAMEWORK_DIR, "variants", env.BoardConfig().get("build.variant"))
            ]
        )
        libs.append(env.BuildLibrary(
            join("$BUILD_DIR", "FrameworkArduinoVariant"),
            join(ARDUINO_FRAMEWORK_DIR, "variants", env.BoardConfig().get("build.variant"))
        ))
    src_filter = "+<cores/%s/*>" % env.BoardConfig().get("build.core")
    if not is_arduino_autostart(sdk_params):
        src_filter += " -<cores/%s/main.cpp>" % env.BoardConfig().get("build.core")
    libs.append(
        env.BuildLibrary(join("$BUILD_DIR", "ArduinoFramework"), ARDUINO_FRAMEWORK_DIR, src_filter=src_filter)
    )

ignore_dirs = (
    "bootloader",
    "esptool_py",
    "espcoredump",
    "idf_test",
    "partition_table"
)

special_src_filter = {
    "app_trace": "+<*> -<test> -<sys_view> -<gcov>",
    "aws_iot": "-<*> +<port> +<aws-iot-device-sdk-embedded-C/src>",
    "esp32": "-<*> +<*.[sSc]*> +<hwcrypto>",
    "bootloader_support": "+<*> -<test> -<src/bootloader_init.c>",
    "soc": "+<*> -<test> -<esp32/test>",
    "spi_flash": "+<*> -<test*> -<sim>"
}

special_env = (
    "heap",
    "freertos",
    "lwip",
    "protocomm",
    "libsodium",
    "wpa_supplicant"
)

for d in listdir(join(FRAMEWORK_DIR, "components")):
    if d in special_src_filter or d in special_env or d in ignore_dirs:
        continue
    component_dir = join(FRAMEWORK_DIR, "components", d)
    if isdir(component_dir):
        libs.append(
            build_component(component_dir,
                            extract_component_config(component_dir)))


for component, src_filter in special_src_filter.items():
    config = {"src_filter": src_filter}
    libs.append(
        build_component(
            join(FRAMEWORK_DIR, "components", component), config))

# The heap component needs special handling for the heap poisioning
# support. If the heap poisioning is disabled we can exclude the
# compilation unit, otherwise we need to include it to avoid link
# errors.
if int(sdk_params.get("CONFIG_HEAP_POISONING_DISABLED", 0)) > 0:
    libs.append(
        build_component(
            join(FRAMEWORK_DIR, "components", "heap"),
            {"src_filter": "+<*> -<test*> -<multi_heap_poisoning.c>"}))
else:
    libs.append(
        build_component(
            join(FRAMEWORK_DIR, "components", "heap"),
            extract_component_config(join(FRAMEWORK_DIR, "components", "heap"))))

libs.extend((
    build_lwip_lib(sdk_params),
    build_protocomm_lib(sdk_params),
    build_rtos_lib(),
    build_libsodium_lib(),
    build_wpa_supplicant_lib()
))

env.Prepend(LIBS=libs)
