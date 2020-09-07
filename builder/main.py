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

from SCons.Script import (
    ARGUMENTS, COMMAND_LINE_TARGETS, AlwaysBuild, Builder, Default,
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
    elif value[-1].upper() in ("K", "M"):
        base = 1024 if value[-1].upper() == "K" else 1024 * 1024
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
        board.update("upload.maximum_size", max(sizes))


def _to_unix_slashes(path):
    return path.replace('\\', '/')


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
env.SConscript("compat.py", exports="env")
platform = env.PioPlatform()
board = env.BoardConfig()
mcu = board.get("build.mcu", "esp32")

env.Replace(
    __get_board_f_flash=_get_board_f_flash,
    __get_board_flash_mode=_get_board_flash_mode,

    AR="xtensa-%s-elf-ar" % mcu,
    AS="xtensa-%s-elf-as" % mcu,
    CC="xtensa-%s-elf-gcc" % mcu,
    CXX="xtensa-%s-elf-g++" % mcu,
    GDB="xtensa-%s-elf-gdb" % mcu,
    OBJCOPY=join(
        platform.get_package_dir("tool-esptoolpy") or "", "esptool.py"),
    RANLIB="xtensa-%s-elf-ranlib" % mcu,
    SIZETOOL="xtensa-%s-elf-size" % mcu,

    ARFLAGS=["rc"],

    SIZEPROGREGEXP=r"^(?:\.iram0\.text|\.iram0\.vectors|\.dram0\.data|\.flash\.text|\.flash\.rodata|)\s+([0-9]+).*",
    SIZEDATAREGEXP=r"^(?:\.dram0\.data|\.dram0\.bss|\.noinit)\s+([0-9]+).*",
    SIZECHECKCMD="$SIZETOOL -A -d $SOURCES",
    SIZEPRINTCMD="$SIZETOOL -B -d $SOURCES",

    ERASEFLAGS=[
        "--chip", mcu,
        "--port", '"$UPLOAD_PORT"'
    ],
    ERASECMD='"$PYTHONEXE" "$OBJCOPY" $ERASEFLAGS erase_flash',

    MKSPIFFSTOOL="mkspiffs_${PIOPLATFORM}_" + ("espidf" if "espidf" in env.subst(
        "$PIOFRAMEWORK") else "${PIOFRAMEWORK}"),
    ESP32_SPIFFS_IMAGE_NAME=env.get("ESP32_SPIFFS_IMAGE_NAME", "spiffs"),

    PROGSUFFIX=".elf"
)

# Allow user to override via pre:script
if env.get("PROGNAME", "program") == "program":
    env.Replace(PROGNAME="firmware")

env.Append(
    # copy CCFLAGS to ASFLAGS (-x assembler-with-cpp mode)
    ASFLAGS=env.get("CCFLAGS", [])[:],

    BUILDERS=dict(
        ElfToBin=Builder(
            action=env.VerboseAction(" ".join([
                '"$PYTHONEXE" "$OBJCOPY"',
                "--chip", mcu,
                "elf2image",
                "--flash_mode", "$BOARD_FLASH_MODE",
                "--flash_freq", "${__get_board_f_flash(__env__)}",
                "--flash_size", board.get("upload.flash_size", "detect"),
                "-o", "$TARGET", "$SOURCES"
            ]), "Building $TARGET"),
            suffix=".bin"
        ),
        DataToBin=Builder(
            action=env.VerboseAction(" ".join([
                '"$MKSPIFFSTOOL"',
                "-c", "$SOURCES",
                "-p", "$SPIFFS_PAGE",
                "-b", "$SPIFFS_BLOCK",
                "-s", "$SPIFFS_SIZE",
                "$TARGET"
            ]), "Building SPIFFS image from '$SOURCES' directory to $TARGET"),
            emitter=__fetch_spiffs_size,
            source_factory=env.Dir,
            suffix=".bin"
        )
    )
)

if not env.get("PIOFRAMEWORK"):
    env.SConscript("frameworks/_bare.py", exports="env")

#
# Target: Build executable and linkable firmware or SPIFFS image
#

target_elf = None
if "nobuild" in COMMAND_LINE_TARGETS:
    target_elf = join("$BUILD_DIR", "${PROGNAME}.elf")
    if set(["uploadfs", "uploadfsota"]) & set(COMMAND_LINE_TARGETS):
        fetch_spiffs_size(env)
        target_firm = join("$BUILD_DIR", "${ESP32_SPIFFS_IMAGE_NAME}.bin")
    else:
        target_firm = join("$BUILD_DIR", "${PROGNAME}.bin")
else:
    target_elf = env.BuildProgram()
    if set(["buildfs", "uploadfs", "uploadfsota"]) & set(COMMAND_LINE_TARGETS):
        target_firm = env.DataToBin(
            join("$BUILD_DIR", "${ESP32_SPIFFS_IMAGE_NAME}"), "$PROJECTDATA_DIR")
        env.NoCache(target_firm)
        AlwaysBuild(target_firm)
    else:
        target_firm = env.ElfToBin(
            join("$BUILD_DIR", "${PROGNAME}"), target_elf)

env.AddPlatformTarget("buildfs", target_firm, None, "Build Filesystem Image")
AlwaysBuild(env.Alias("nobuild", target_firm))
target_buildprog = env.Alias("buildprog", target_firm, target_firm)

# update max upload size based on CSV file
if env.get("PIOMAINPROG"):
    env.AddPreAction(
        "checkprogsize",
        env.VerboseAction(
            lambda source, target, env: _update_max_upload_size(env),
            "Retrieving maximum program size $SOURCES"))
# remove after PIO Core 3.6 release
elif set(["checkprogsize", "upload"]) & set(COMMAND_LINE_TARGETS):
    _update_max_upload_size(env)

#
# Target: Print binary size
#

target_size = env.AddPlatformTarget(
    "size",
    target_elf,
    env.VerboseAction("$SIZEPRINTCMD", "Calculating size $SOURCE"),
    "Program Size",
    "Calculate program size",
)

#
# Target: Upload firmware or SPIFFS image
#

upload_protocol = env.subst("$UPLOAD_PROTOCOL")
debug_tools = board.get("debug.tools", {})
upload_actions = []

# Compatibility with old OTA configurations
if (upload_protocol != "espota"
        and re.match(r"\"?((([0-9]{1,3}\.){3}[0-9]{1,3})|[^\\/]+\.local)\"?$",
                     env.get("UPLOAD_PORT", ""))):
    upload_protocol = "espota"
    sys.stderr.write(
        "Warning! We have just detected `upload_port` as IP address or host "
        "name of ESP device. `upload_protocol` is switched to `espota`.\n"
        "Please specify `upload_protocol = espota` in `platformio.ini` "
        "project configuration file.\n")

if upload_protocol == "espota":
    if not env.subst("$UPLOAD_PORT"):
        sys.stderr.write(
            "Error: Please specify IP address or host name of ESP device "
            "using `upload_port` for build environment or use "
            "global `--upload-port` option.\n"
            "See https://docs.platformio.org/page/platforms/"
            "espressif32.html#over-the-air-ota-update\n")
    env.Replace(
        UPLOADER=join(
            platform.get_package_dir("framework-arduinoespressif32") or "",
            "tools", "espota.py"),
        UPLOADERFLAGS=["--debug", "--progress", "-i", "$UPLOAD_PORT"],
        UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS -f $SOURCE'
    )
    if "uploadfs" in COMMAND_LINE_TARGETS:
        env.Append(UPLOADERFLAGS=["-s"])
    upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")]

elif upload_protocol == "esptool":
    env.Replace(
        UPLOADER=join(
            platform.get_package_dir("tool-esptoolpy") or "", "esptool.py"),
        UPLOADERFLAGS=[
            "--chip", mcu,
            "--port", '"$UPLOAD_PORT"',
            "--baud", "$UPLOAD_SPEED",
            "--before", "default_reset",
            "--after", "hard_reset",
            "write_flash", "-z",
            "--flash_mode", "${__get_board_flash_mode(__env__)}",
            "--flash_freq", "${__get_board_f_flash(__env__)}",
            "--flash_size", "detect"
        ],
        UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS 0x10000 $SOURCE'
    )
    for image in env.get("FLASH_EXTRA_IMAGES", []):
        env.Append(UPLOADERFLAGS=[image[0], env.subst(image[1])])

    if "uploadfs" in COMMAND_LINE_TARGETS:
        env.Replace(
            UPLOADERFLAGS=[
                "--chip", mcu,
                "--port", '"$UPLOAD_PORT"',
                "--baud", "$UPLOAD_SPEED",
                "--before", "default_reset",
                "--after", "hard_reset",
                "write_flash", "-z",
                "--flash_mode", "$BOARD_FLASH_MODE",
                "--flash_size", "detect",
                "$SPIFFS_START"
            ],
            UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS $SOURCE',
        )

    upload_actions = [
        env.VerboseAction(env.AutodetectUploadPort,
                          "Looking for upload port..."),
        env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")
    ]


elif upload_protocol == "mbctool":
    env.Replace(
        UPLOADER=join(
            platform.get_package_dir("tool-mbctool") or "", "bin", "mbctool"),
        UPLOADERFLAGS=[
            "--device", "esp",
            "--speed", "$UPLOAD_SPEED",
            "--port", '"$UPLOAD_PORT"',
            "--upload",
            "0x1000", join(
                platform.get_package_dir("framework-arduino-mbcwb"),
                "tools", "sdk", "bin", "bootloader_qio_80m.bin"),
            "0x8000", join("$BUILD_DIR", "partitions.bin"),
            "0xe000", join(
                platform.get_package_dir("framework-arduino-mbcwb"),
                "tools", "partitions", "boot_app0.bin"),
            "0x10000", join("$BUILD_DIR", "${PROGNAME}.bin"),
        ],
        UPLOADCMD='"$UPLOADER" $UPLOADERFLAGS'
    )
    upload_actions = [
        env.VerboseAction(env.AutodetectUploadPort,
                          "Looking for upload port..."),
        env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")
    ]


elif upload_protocol in debug_tools:
    openocd_args = ["-d%d" % (2 if int(ARGUMENTS.get("PIOVERBOSE", 0)) else 1)]
    openocd_args.extend(
        debug_tools.get(upload_protocol).get("server").get("arguments", []))
    openocd_args.extend([
        "-c",
        "program_esp32 {{$SOURCE}} %s verify" %
        board.get("upload.offset_address", "0x10000")
    ])
    for image in env.get("FLASH_EXTRA_IMAGES", []):
        openocd_args.extend([
            "-c",
            'program_esp32 {{%s}} %s verify' %
            (_to_unix_slashes(image[1]), image[0])
        ])
    openocd_args.extend(["-c", "reset run; shutdown"])
    openocd_args = [
        f.replace(
            "$PACKAGE_DIR",
            _to_unix_slashes(
                platform.get_package_dir("tool-openocd-esp32") or ""))
        for f in openocd_args
    ]
    env.Replace(UPLOADER="openocd",
                UPLOADERFLAGS=openocd_args,
                UPLOADCMD="$UPLOADER $UPLOADERFLAGS")
    upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")]

# custom upload tool
elif upload_protocol == "custom":
    upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")]

else:
    sys.stderr.write("Warning! Unknown upload protocol %s\n" % upload_protocol)

env.AddPlatformTarget("upload", target_firm, upload_actions, "Upload")
env.AddPlatformTarget("uploadfs", target_firm, upload_actions, "Upload Filesystem Image")
env.AddPlatformTarget(
    "uploadfsota", target_firm, upload_actions, "Upload Filesystem Image OTA")

#
# Target: Erase Flash
#

env.AddPlatformTarget(
    "erase",
    None,
    [
        env.VerboseAction(env.AutodetectUploadPort, "Looking for serial port..."),
        env.VerboseAction("$ERASECMD", "Erasing...")
    ],
    "Erase Flash",
)

#
# Information about obsolete method of specifying linker scripts
#

if any("-Wl,-T" in f for f in env.get("LINKFLAGS", [])):
    print("Warning! '-Wl,-T' option for specifying linker scripts is deprecated. "
          "Please use 'board_build.ldscript' option in your 'platformio.ini' file.")

#
# Default targets
#

Default([target_buildprog, target_size])
