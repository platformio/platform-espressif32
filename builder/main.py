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

#
# Helpers
#


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


def _parse_size(value):
    if isinstance(value, int):
        return value
    elif value.isdigit():
        return int(value)
    elif value.startswith("0x"):
        return int(value, 16)
    elif value[-1] in ("K", "M"):
        base = 1024 if value[-1] == "K" else 1024 * 1024
        return int(value[:-1]) * base
    return value


def _parse_partitions(env):
    partitions_csv = env.subst("$PARTITIONS_TABLE_CSV")
    if not isfile(partitions_csv):
        sys.stderr.write("Could not find the file %s with partitions "
                         "table.\n" % partitions_csv)
        env.Exit(1)
        return

    result = []
    next_offset = 0
    with open(partitions_csv) as fp:
        for line in fp.readlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            tokens = [t.strip() for t in line.split(",")]
            if len(tokens) < 5:
                continue
            partition = {
                "name": tokens[0],
                "type": tokens[1],
                "subtype": tokens[2],
                "offset": tokens[3] or next_offset,
                "size": tokens[4],
                "flags": tokens[5] if len(tokens) > 5 else None
            }
            result.append(partition)
            next_offset = (_parse_size(partition['offset']) +
                           _parse_size(partition['size']))
    return result


def _update_max_upload_size(env):
    if not env.get("PARTITIONS_TABLE_CSV"):
        return
    sizes = [
        _parse_size(p['size']) for p in _parse_partitions(env)
        if p['type'] in ("0", "app")
    ]
    if sizes:
        env.BoardConfig().update("upload.maximum_size", max(sizes))


#
# SPIFFS helpers
#


def fetch_spiffs_size(env):
    spiffs = None
    for p in _parse_partitions(env):
        if p['type'] == "data" and p['subtype'] == "spiffs":
            spiffs = p
    if not spiffs:
        sys.stderr.write(
            env.subst("Could not find the `spiffs` section in the partitions "
                      "table $PARTITIONS_TABLE_CSV\n"))
        env.Exit(1)
        return
    env["SPIFFS_START"] = _parse_size(spiffs['offset'])
    env["SPIFFS_SIZE"] = _parse_size(spiffs['size'])
    env["SPIFFS_PAGE"] = int("0x100", 16)
    env["SPIFFS_BLOCK"] = int("0x1000", 16)


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
        "-Os", "-Wall", "-nostdlib", "-Wpointer-arith",
        "-Wno-error=unused-but-set-variable", "-Wno-error=unused-variable",
        "-mlongcalls", "-ffunction-sections", "-fdata-sections",
        "-fstrict-volatile-bitfields"
    ],
    CXXFLAGS=["-fno-rtti", "-fno-exceptions", "-std=gnu++11"],
    CPPDEFINES=[
        "ESP32", "ESP_PLATFORM", ("F_CPU", "$BOARD_F_CPU"), "HAVE_CONFIG_H",
        ("MBEDTLS_CONFIG_FILE", '\\"mbedtls/esp_config.h\\"')
    ],
    LINKFLAGS=[
        "-nostdlib", "-Wl,-static", "-u", "call_user_start_cpu0",
        "-Wl,--undefined=uxTopUsedPriority", "-Wl,--gc-sections"
    ],

    SIZEPROGREGEXP=r"^(?:\.iram0\.text|\.dram0\.text|\.flash\.text|\.dram0\.data|\.flash\.rodata|)\s+(\d+).*",
    SIZEDATAREGEXP=r"^(?:\.dram0\.data|\.dram0\.bss)\s+(\d+).*",
    SIZECHECKCMD="$SIZETOOL -A -d $SOURCES",
    SIZEPRINTCMD="$SIZETOOL -B -d $SOURCES",

    MKSPIFFSTOOL="mkspiffs_${PIOPLATFORM}_${PIOFRAMEWORK}",
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
                '"$PYTHONEXE" "$OBJCOPY"', "--chip", "esp32", "elf2image",
                "--flash_mode", "$BOARD_FLASH_MODE", "--flash_freq",
                "${__get_board_f_flash(__env__)}", "--flash_size",
                env.BoardConfig().get("upload.flash_size",
                                      "detect"), "-o", "$TARGET", "$SOURCES"
            ]), "Building $TARGET"),
            suffix=".bin"),
        DataToBin=Builder(
            action=env.VerboseAction(" ".join([
                '"$MKSPIFFSTOOL"', "-c", "$SOURCES", "-p", "$SPIFFS_PAGE",
                "-b", "$SPIFFS_BLOCK", "-s", "$SPIFFS_SIZE", "$TARGET"
            ]), "Building SPIFFS image from '$SOURCES' directory to $TARGET"),
            emitter=__fetch_spiffs_size,
            source_factory=env.Dir,
            suffix=".bin")))

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

# update max upload size based on CSV file
if set(["checkprogsize", "upload"]) & set(COMMAND_LINE_TARGETS):
    _update_max_upload_size(env)

#
# Target: Print binary size
#

target_size = env.Alias("size", target_elf,
                        env.VerboseAction("$SIZEPRINTCMD",
                                          "Calculating size $SOURCE"))
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
        UPLOADEROTA=join(
            platform.get_package_dir("tool-espotapy") or "", "espota.py"),
        UPLOADERFLAGS=[
            "--chip", "esp32", "--port", '"$UPLOAD_PORT"', "--baud",
            "$UPLOAD_SPEED", "--before", "default_reset", "--after",
            "hard_reset", "write_flash", "-z", "--flash_mode",
            "${__get_board_flash_mode(__env__)}", "--flash_freq",
            "${__get_board_f_flash(__env__)}", "--flash_size", "detect"
        ],
        UPLOADEROTAFLAGS=[
            "--debug", "--progress", "-i", "$UPLOAD_PORT", "-p", "3232",
            "$UPLOAD_FLAGS"
        ],
        UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS 0x10000 $SOURCE',
        UPLOADOTACMD='"$PYTHONEXE" "$UPLOADEROTA" $UPLOADEROTAFLAGS -f $SOURCE',
    )
    for image in env.get("FLASH_EXTRA_IMAGES", []):
        env.Append(UPLOADERFLAGS=[image[0], "%s" % image[1]])

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
                "--chip", "esp32", "--port", '"$UPLOAD_PORT"', "--baud",
                "$UPLOAD_SPEED", "--before", "default_reset", "--after",
                "hard_reset", "write_flash", "-z", "--flash_mode",
                "$BOARD_FLASH_MODE", "--flash_size", "detect", "$SPIFFS_START"
            ],
            UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS $SOURCE',
        )
        env.Append(UPLOADEROTAFLAGS=["-s"])

    upload_actions = [
        env.VerboseAction(env.AutodetectUploadPort,
                          "Looking for upload port..."),
        env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")
    ]

elif upload_protocol in debug_tools:
    openocd_dir = platform.get_package_dir("tool-openocd-esp32") or ""
    uploader_flags = ["-s", openocd_dir]
    uploader_flags.extend(
        debug_tools.get(upload_protocol).get("server").get("arguments", []))
    uploader_flags.extend(["-c", 'program_esp32 "{{$SOURCE}}" 0x10000 verify'])
    for image in env.get("FLASH_EXTRA_IMAGES", []):
        uploader_flags.extend(
            ["-c", 'program_esp32 "%s" %s verify' % (image[1], image[0])])
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
