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

import re
import sys
from os.path import isfile, join

from SCons.Script import (COMMAND_LINE_TARGETS, AlwaysBuild, Builder, Default,
                          DefaultEnvironment)


def _get_board_f_flash(env):
    frequency = env.subst("$BOARD_F_FLASH")
    frequency = str(frequency).replace("L", "")
    return str(int(int(frequency) / 1000000)) + "m"


def _get_board_flash_mode(env):
    mode = env.subst("$BOARD_FLASH_MODE")
    if mode == "qio":
        return "dio"
    elif mode == "qout":
        return "dout"
    return mode

#
# SPIFFS helpers
#

def fetch_spiffs_size(env):
    path_to_patition_table = env.get("PARTITION_TABLE_CSV")
    if not isfile(path_to_patition_table):
        sys.stderr.write("Could not find the file %s with paritions table." %
                         path_to_patition_table)
        env.Exit(1)

    with open(path_to_patition_table) as fp:
        for l in fp.readlines():
            if l.startswith("spiffs"):
                spiffs_config = [s.strip() for s in l.split(",")]
                env["SPIFFS_START"] = spiffs_config[3]
                env["SPIFFS_SIZE"] = spiffs_config[4]
                env["SPIFFS_PAGE"] = "0x100"
                env["SPIFFS_BLOCK"] = "0x1000"
                return

    sys.stderr.write("Could not find the spiffs section in the paritions "
                     "file %s" % path_to_patition_table)
    env.Exit(1)


def __fetch_spiffs_size(target, source, env):
    fetch_spiffs_size(env)
    return (target, source)


env = DefaultEnvironment()
platform = env.PioPlatform()

env.Replace(
    __get_board_f_flash=_get_board_f_flash,
    __get_board_flash_mode=_get_board_flash_mode,

    AR="xtensa-esp32-elf-ar",
    AS="xtensa-esp32-elf-as",
    CC="xtensa-esp32-elf-gcc",
    CXX="xtensa-esp32-elf-g++",
    GDB="xtensa-esp32-elf-gdb",
    OBJCOPY=join(
        platform.get_package_dir("tool-esptoolpy") or "", "esptool.py"),
    RANLIB="xtensa-esp32-elf-ranlib",
    SIZETOOL="xtensa-esp32-elf-size",

    ARFLAGS=["rc"],

    ASFLAGS=["-x", "assembler-with-cpp"],

    CFLAGS=["-std=gnu99"],

    CCFLAGS=[
        "%s" % "-Os" if env.subst("$PIOFRAMEWORK") == "arduino" else "-Og",
        "-g3",
        "-nostdlib",
        "-Wpointer-arith",
        "-Wno-error=unused-but-set-variable",
        "-Wno-error=unused-variable",
        "-mlongcalls",
        "-ffunction-sections",
        "-fdata-sections",
        "-fstrict-volatile-bitfields"
    ],

    CXXFLAGS=[
        "-fno-rtti",
        "-fno-exceptions",
        "-std=gnu++11"
    ],

    CPPDEFINES=[
        "ESP32",
        "ESP_PLATFORM",
        ("F_CPU", "$BOARD_F_CPU"),
        "HAVE_CONFIG_H",
        ("MBEDTLS_CONFIG_FILE", '\\"mbedtls/esp_config.h\\"')
    ],

    LINKFLAGS=[
        "-nostdlib",
        "-Wl,-static",
        "-u", "call_user_start_cpu0",
        "-Wl,--undefined=uxTopUsedPriority",
        "-Wl,--gc-sections"
    ],


    MKSPIFFSTOOL="mkspiffs_${PIOPLATFORM}_${PIOFRAMEWORK}",
    SIZEPRINTCMD='$SIZETOOL -B -d $SOURCES',

    PROGSUFFIX=".elf"
)

# Allow user to override via pre:script
if env.get("PROGNAME", "program") == "program":
    env.Replace(PROGNAME="firmware")

env.Append(
    # Clone actual CCFLAGS to ASFLAGS
    ASFLAGS=env.get("CCFLAGS", [])[:],

    BUILDERS=dict(
        ElfToBin=Builder(
            action=env.VerboseAction(" ".join([
                '"$PYTHONEXE" "$OBJCOPY"',
                "--chip", "esp32",
                "elf2image",
                "--flash_mode", "$BOARD_FLASH_MODE",
                "--flash_freq", "${__get_board_f_flash(__env__)}",
                "--flash_size",
                env.BoardConfig().get("upload.flash_size", "detect"),
                "-o", "$TARGET", "$SOURCES"
            ]), "Building $TARGET"),
            suffix=".bin"
        ),

        DataToBin=Builder(
            action=env.VerboseAction(" ".join([
                '"$MKSPIFFSTOOL"',
                "-c", "$SOURCES",
                "-p", "${int(SPIFFS_PAGE, 16)}",
                "-b", "${int(SPIFFS_BLOCK, 16)}",
                "-s", "${int(SPIFFS_SIZE, 16)}",
                "$TARGET"
            ]), "Building SPIFFS image from '$SOURCES' directory to $TARGET"),
            emitter=__fetch_spiffs_size,
            source_factory=env.Dir,
            suffix=".bin"
        )
    )
)


#
# Target: Build executable and linkable firmware or SPIFFS image
#

target_elf = env.BuildProgram()
if "nobuild" in COMMAND_LINE_TARGETS:
    if set(["uploadfs", "uploadfsota"]) & set(COMMAND_LINE_TARGETS):
        fetch_spiffs_size(env)
        target_firm = join("$BUILD_DIR", "spiffs.bin")
    else:
        target_firm = join("$BUILD_DIR", "${PROGNAME}.bin")
else:
    if set(["buildfs", "uploadfs", "uploadfsota"]) & set(COMMAND_LINE_TARGETS):
        target_firm = env.DataToBin(
            join("$BUILD_DIR", "spiffs"), "$PROJECTDATA_DIR")
        AlwaysBuild(target_firm)
        AlwaysBuild(env.Alias("buildfs", target_firm))
    else:
        target_firm = env.ElfToBin(
            join("$BUILD_DIR", "${PROGNAME}"), target_elf)

AlwaysBuild(env.Alias("nobuild", target_firm))
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

upload_protocol = env.subst("$UPLOAD_PROTOCOL")
debug_tools = env.BoardConfig().get("debug.tools", {})
upload_actions = []

if upload_protocol == "esptool":
    env.Replace(
        UPLOADER=join(
            platform.get_package_dir("tool-esptoolpy") or "", "esptool.py"),
        UPLOADEROTA=join(platform.get_package_dir("tool-espotapy") or "",
                         "espota.py"),
        UPLOADERFLAGS=[
            "--chip", "esp32",
            "--port", '"$UPLOAD_PORT"',
            "--baud", "$UPLOAD_SPEED",
            "--before", "default_reset",
            "--after", "hard_reset",
            "write_flash", "-z",
            "--flash_mode", "${__get_board_flash_mode(__env__)}",
            "--flash_freq", "${__get_board_f_flash(__env__)}",
            "--flash_size", "detect"
        ],
        UPLOADEROTAFLAGS=[
            "--debug",
            "--progress",
            "-i", "$UPLOAD_PORT",
            "-p", "3232",
            "$UPLOAD_FLAGS"
        ],

        UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS $FLASH_EXTRA_IMAGES 0x10000 $SOURCE',
        UPLOADOTACMD='"$PYTHONEXE" "$UPLOADEROTA" $UPLOADEROTAFLAGS -f $SOURCE',
    )

    if env.subst("$PIOFRAMEWORK") == "arduino":
        # Handle uploading via OTA
        ota_port = None
        if env.get("UPLOAD_PORT"):
            ota_port = re.match(
                r"\"?((([0-9]{1,3}\.){3}[0-9]{1,3})|.+\.local)\"?$",
                env.get("UPLOAD_PORT"))
        if ota_port:
            env.Replace(UPLOADCMD="$UPLOADOTACMD")

    if "uploadfs" in COMMAND_LINE_TARGETS:
        env.Replace(
            UPLOADERFLAGS=[
                "--chip", "esp32",
                "--port", '"$UPLOAD_PORT"',
                "--baud", "$UPLOAD_SPEED",
                "--before", "default_reset",
                "--after", "hard_reset",
                "write_flash", "-z",
                "--flash_mode", "$BOARD_FLASH_MODE",
                "--flash_size", "detect",
                "${int(SPIFFS_START, 16)}"
            ]
        )
        env.Append(UPLOADEROTAFLAGS=["-s"])

    upload_actions = [
        env.VerboseAction(
            env.AutodetectUploadPort, "Looking for upload port..."),
        env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")
    ]

elif upload_protocol in debug_tools:
    openocd_dir = platform.get_package_dir("tool-openocd-esp32") or ""
    uploader_flags = ["-s", openocd_dir]
    uploader_flags.extend(debug_tools.get(upload_protocol).get(
        "server").get("arguments", []))
    uploader_flags.extend([
        "-c", "program_esp32 {{$SOURCE}} 0x10000 verify"
    ])
    for image in env.get("FLASH_EXTRA_IMAGES", []):
        uploader_flags.extend([
            "-c", "program_esp32 %s %s verify" % (image[1], image[0])
        ])
    uploader_flags.extend(["-c", "reset run; shutdown"])
    for i, item in enumerate(uploader_flags):
        if "$PACKAGE_DIR" in item:
            uploader_flags[i] = item.replace("$PACKAGE_DIR", openocd_dir)

    env.Replace(
        UPLOADER="openocd",
        UPLOADERFLAGS=uploader_flags,
        UPLOADCMD="$UPLOADER $UPLOADERFLAGS")
    upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")]

# custom upload tool
elif "UPLOADCMD" in env:
    upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")]

else:
    sys.stderr.write("Warning! Unknown upload protocol %s\n" % upload_protocol)

AlwaysBuild(env.Alias(["upload", "uploadfs"], target_firm, upload_actions))

#
# Default targets
#

Default([target_buildprog, target_size])
