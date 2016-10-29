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

from os.path import join

from SCons.Script import (AlwaysBuild, Builder, Default, DefaultEnvironment)


def _get_board_f_flash(env):
    frequency = env.subst("$BOARD_F_FLASH")
    frequency = str(frequency).replace("L", "")
    return str(int(int(frequency) / 1000000)) + "m"


def build_espidf_bootloader():
    envsafe = env.Clone()
    framework_dir = env.subst("$ESPIDF_DIR")
    envsafe.Replace(
        CPPDEFINES=["ESP_PLATFORM", "BOOTLOADER_BUILD=1"],

        LIBPATH=[
            join(framework_dir, "components", "esp32", "ld"),
            join(framework_dir, "components", "bootloader", "src", "main")
        ],

        LINKFLAGS=[
            "-Os",
            "-nostdlib",
            "-Wl,-static",
            "-u", "call_user_start_cpu0",
            "-Wl,-static",
            "-Wl,--gc-sections",
            "-T", "esp32.bootloader.ld",
            "-T", "esp32.rom.ld"
        ]
    ),

    envsafe.Append(CCFLAGS=["-fstrict-volatile-bitfields"])

    envsafe.Replace(
        LIBS=[
            envsafe.BuildLibrary(
                join("$BUILD_DIR", "bootloaderLog"),
                join(framework_dir, "components", "log")
            ), "gcc"
        ]
    )

    return envsafe.Program(
        join("$BUILD_DIR", "bootloader.elf"),
        envsafe.CollectBuildFiles(
            join("$BUILD_DIR", "bootloader"),
            join(framework_dir, "components", "bootloader", "src", "main")
        )
    )


env = DefaultEnvironment()
platform = env.PioPlatform()

env.Replace(
    __get_board_f_flash=_get_board_f_flash,

    AR="xtensa-esp32-elf-ar",
    AS="xtensa-esp32-elf-as",
    CC="xtensa-esp32-elf-gcc",
    CXX="xtensa-esp32-elf-g++",
    OBJCOPY=join(
        platform.get_package_dir("tool-esptoolpy") or "", "esptool.py"),
    RANLIB="xtensa-esp32-elf-ranlib",
    SIZETOOL="xtensa-esp32-elf-size",

    ARFLAGS=["rcs"],

    ASFLAGS=["-x", "assembler-with-cpp"],

    CFLAGS=["-std=gnu99"],

    CCFLAGS=[
        "-Os",  # optimize for size
        "-nostdlib",
        "-Wpointer-arith",
        "-Wno-error=unused-function",
        "-Wno-error=unused-but-set-variable",
        "-Wno-error=unused-variable",
        "-mlongcalls",
        "-ffunction-sections",
        "-fdata-sections"
    ],

    CXXFLAGS=[
        "-fno-rtti",
        "-fno-exceptions",
        "-std=gnu++11"
    ],

    CPPDEFINES=[
        "ESP32",
        "ESP_PLATFORM",
        "F_CPU=$BOARD_F_CPU",
        "HAVE_CONFIG_H",
        "MBEDTLS_CONFIG_FILE=\\\"mbedtls/esp_config.h\\\""
    ],

    LINKFLAGS=[
        "-Os",
        "-nostdlib",
        "-Wl,-static",
        "-u", "call_user_start_cpu0",
        "-Wl,-static",
        "-Wl,--undefined=uxTopUsedPriority",
        "-Wl,--gc-sections"
    ],

    #
    # Packages
    #

    FRAMEWORK_ARDUINOESP32_DIR=platform.get_package_dir(
        "framework-arduinoespressif32"),
    ESPIDF_DIR=platform.get_package_dir("framework-espidf"),

    #
    # Upload
    #

    UPLOADER=join(
        platform.get_package_dir("tool-esptoolpy") or "", "esptool.py"),

    UPLOADERFLAGS=[
        "--chip", "esp32",
        "--port", '"$UPLOAD_PORT"',
        "--baud", "$UPLOAD_SPEED",
        "write_flash", "-z",
        "--flash_mode", "$BOARD_FLASH_MODE",
        "--flash_freq", "${__get_board_f_flash(__env__)}"
    ],

    UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS $SOURCE',

    PROGNAME="firmware",
    PROGSUFFIX=".elf"
)

if env.subst("$PIOFRAMEWORK") == "arduino":
    framework_dir = platform.get_package_dir("framework-arduinoespressif32")
    env.Append(
        UPLOADERFLAGS=[
            "0x1000", join("$FRAMEWORK_ARDUINOESP32_DIR", "tools",
                           "sdk", "bin", "bootloader.bin"),
            "0x4000", join("$FRAMEWORK_ARDUINOESP32_DIR", "tools",
                           "sdk", "bin", "partitions_singleapp.bin"),
            "0x10000"
        ]
    )
if env.subst("$PIOFRAMEWORK") == "espidf":
    framework_dir = platform.get_package_dir("framework-espidf")
    env.Append(
        UPLOADERFLAGS=[
            "0x1000", join("$BUILD_DIR", "bootloader.bin"),
            "0x4000", join("$BUILD_DIR", "partitions_singleapp.bin"),
            "0x10000"
        ],

        PTABLE_SCRIPT=join("$ESPIDF_DIR", "components",
                           "partition_table", "gen_esp32part.py"),

        PTABLE_FLAGS=[
            "-q", join("$ESPIDF_DIR", "components",
                       "partition_table", "partitions_singleapp.csv"),
            join(env.subst("$BUILD_DIR"), "partitions_singleapp.bin")

        ],

        PTABLE_CMD='"$PYTHONEXE" "$PTABLE_SCRIPT" $PTABLE_FLAGS'
    )


env.Append(
    ASFLAGS=env.get("CCFLAGS", [])[:]
)

#
# Framework and SDK specific configuration
#

env.Append(
    BUILDERS=dict(
        ElfToBin=Builder(
            action=env.VerboseAction(" ".join([
                '"$PYTHONEXE" "$OBJCOPY"',
                "--chip", "esp32",
                "elf2image",
                "--flash_mode", "$BOARD_FLASH_MODE",
                "--flash_freq", "${__get_board_f_flash(__env__)}",
                "-o", "$TARGET", "$SOURCES"
            ]), "Building $TARGET"),
            suffix=".bin"
        )
    )
)

#
# Target: Build executable and linkable firmware or SPIFFS image
#


def __tmp_hook_before_pio_3_2():
    env.ProcessFlags(env.get("BUILD_FLAGS"))
    # append specified LD_SCRIPT
    if ("LDSCRIPT_PATH" in env and
            not any(["-Wl,-T" in f for f in env['LINKFLAGS']])):
        env.Append(LINKFLAGS=['-Wl,-T"$LDSCRIPT_PATH"'])


target_elf = env.BuildProgram()
if "PIOFRAMEWORK" in env:
    target_firm = env.ElfToBin(join("$BUILD_DIR", "firmware"), target_elf)

if "espidf" in env.subst("$PIOFRAMEWORK"):
    bootloader_bin = env.ElfToBin(
        join("$BUILD_DIR", "bootloader"), build_espidf_bootloader())
    target_buildprog = env.Alias(
        "buildprog", [target_firm, bootloader_bin], "$PTABLE_CMD")
else:
    target_buildprog = env.Alias("buildprog", target_firm, target_firm)


#
# Target: Print binary size
#

target_size = env.Alias(
    "size", target_elf,
    env.VerboseAction("$SIZEPRINTCMD", "Calculating size $SOURCE"))
AlwaysBuild(target_size)

#
# Target: Upload firmware or SPIFFS image
#

target_upload = env.Alias(
    "upload", target_firm,
    [env.VerboseAction(env.AutodetectUploadPort, "Looking for upload port..."),
     env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")])
env.AlwaysBuild(target_upload)


#
# Default targets
#

Default([target_buildprog, target_size])
