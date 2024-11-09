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

import os
import urllib
import sys
import json
import re
import requests

from platformio.public import PlatformBase, to_unix_path


IS_WINDOWS = sys.platform.startswith("win")
# Set Platformio env var to use windows_amd64 for all windows architectures
# only windows_amd64 native espressif toolchains are available
# needs platformio core >= 6.1.16b2 or pioarduino core 6.1.16+test
if IS_WINDOWS:
    os.environ["PLATFORMIO_SYSTEM_TYPE"] = "windows_amd64"


class Espressif32Platform(PlatformBase):
    def configure_default_packages(self, variables, targets):
        if not variables.get("board"):
            return super().configure_default_packages(variables, targets)

        board_config = self.board_config(variables.get("board"))
        mcu = variables.get("board_build.mcu", board_config.get("build.mcu", "esp32"))
        board_sdkconfig = variables.get("board_espidf.custom_sdkconfig", board_config.get("espidf.custom_sdkconfig", ""))
        core_variant_board = ''.join(variables.get("board_build.extra_flags", board_config.get("build.extra_flags", "")))
        core_variant_board = core_variant_board.replace("-D", " ")
        core_variant_build = (''.join(variables.get("build_flags", []))).replace("-D", " ")
        frameworks = variables.get("pioframework", [])

        if "arduino" in frameworks and variables.get("custom_sdkconfig") is None and len(str(board_sdkconfig)) < 3:
            if "CORE32SOLO1" in core_variant_board or "FRAMEWORK_ARDUINO_SOLO1" in core_variant_build:
                self.packages["framework-arduino-solo1"]["optional"] = False
            elif "CORE32ITEAD" in core_variant_board or "FRAMEWORK_ARDUINO_ITEAD" in core_variant_build:
                self.packages["framework-arduino-ITEAD"]["optional"] = False
            else:
                self.packages["framework-arduinoespressif32"]["optional"] = False

        if variables.get("custom_sdkconfig") is not None or len(str(board_sdkconfig)) > 3:
            frameworks.append("espidf")
            self.packages["framework-espidf"]["optional"] = False
            self.packages["framework-arduinoespressif32"]["optional"] = False

        if "buildfs" in targets:
            filesystem = variables.get("board_build.filesystem", "littlefs")
            if filesystem == "littlefs":
                self.packages["tool-mklittlefs"]["optional"] = False
            elif filesystem == "fatfs":
                self.packages["tool-mkfatfs"]["optional"] = False
        if variables.get("upload_protocol"):
            self.packages["tool-openocd-esp32"]["optional"] = False
        if os.path.isdir("ulp"):
            self.packages["toolchain-esp32ulp"]["optional"] = False

        if "downloadfs" in targets:
            filesystem = variables.get("board_build.filesystem", "littlefs")
            if filesystem == "littlefs":
                # Use Tasmota mklittlefs v4.0.0 to unpack, older version is incompatible
                self.packages["tool-mklittlefs"]["version"] = "~4.0.0"

        # Currently only Arduino Nano ESP32 uses the dfuutil tool as uploader
        if variables.get("board") == "arduino_nano_esp32":
            self.packages["tool-dfuutil-arduino"]["optional"] = False
        else:
            del self.packages["tool-dfuutil-arduino"]

        # Starting from v12, Espressif's toolchains are shipped without
        # bundled GDB. Instead, it's distributed as separate packages for Xtensa
        # and RISC-V targets.
        for gdb_package in ("tool-xtensa-esp-elf-gdb", "tool-riscv32-esp-elf-gdb"):
            self.packages[gdb_package]["optional"] = False
            # if IS_WINDOWS:
                # Note: On Windows GDB v12 is not able to
                # launch a GDB server in pipe mode while v11 works fine
                # self.packages[gdb_package]["version"] = "~11.2.0"

        # Common packages for IDF and mixed Arduino+IDF projects
        if "espidf" in frameworks:
            self.packages["toolchain-esp32ulp"]["optional"] = False
            for p in self.packages:
                if p in ("tool-scons", "tool-cmake", "tool-ninja"):
                    self.packages[p]["optional"] = False
                elif p in ("tool-mconf", "tool-idf") and IS_WINDOWS:
                    self.packages[p]["optional"] = False

        if mcu in ("esp32", "esp32s2", "esp32s3"):
            self.packages["toolchain-xtensa-esp-elf"]["optional"] = False
        else:
            self.packages.pop("toolchain-xtensa-esp-elf", None)

        if mcu in ("esp32s2", "esp32s3", "esp32c2", "esp32c3", "esp32c6", "esp32h2", "esp32p4"):
            if mcu in ("esp32c2", "esp32c3", "esp32c6", "esp32h2", "esp32p4"):
                self.packages.pop("toolchain-esp32ulp", None)
            # RISC-V based toolchain for ESP32C3, ESP32C6 ESP32S2, ESP32S3 ULP
            self.packages["toolchain-riscv32-esp"]["optional"] = False

        return super().configure_default_packages(variables, targets)

    def get_boards(self, id_=None):
        result = super().get_boards(id_)
        if not result:
            return result
        if id_:
            return self._add_dynamic_options(result)
        else:
            for key, value in result.items():
                result[key] = self._add_dynamic_options(result[key])
        return result

    def _add_dynamic_options(self, board):
        # upload protocols
        if not board.get("upload.protocols", []):
            board.manifest["upload"]["protocols"] = ["esptool", "espota"]
        if not board.get("upload.protocol", ""):
            board.manifest["upload"]["protocol"] = "esptool"

        # debug tools
        debug = board.manifest.get("debug", {})
        non_debug_protocols = ["esptool", "espota"]
        supported_debug_tools = [
            "cmsis-dap",
            "esp-prog",
            "esp-bridge",
            "iot-bus-jtag",
            "jlink",
            "minimodule",
            "olimex-arm-usb-tiny-h",
            "olimex-arm-usb-ocd-h",
            "olimex-arm-usb-ocd",
            "olimex-jtag-tiny",
            "tumpa",
        ]

        # A special case for the Kaluga board that has a separate interface config
        if board.id == "esp32-s2-kaluga-1":
            supported_debug_tools.append("ftdi")
        if board.get("build.mcu", "") in ("esp32c3", "esp32c6", "esp32s3", "esp32h2"):
            supported_debug_tools.append("esp-builtin")

        upload_protocol = board.manifest.get("upload", {}).get("protocol")
        upload_protocols = board.manifest.get("upload", {}).get("protocols", [])
        if debug:
            upload_protocols.extend(supported_debug_tools)
        if upload_protocol and upload_protocol not in upload_protocols:
            upload_protocols.append(upload_protocol)
        board.manifest["upload"]["protocols"] = upload_protocols

        if "tools" not in debug:
            debug["tools"] = {}

        for link in upload_protocols:
            if link in non_debug_protocols or link in debug["tools"]:
                continue

            if link in ("jlink", "cmsis-dap"):
                openocd_interface = link
            elif link in ("esp-prog", "ftdi"):
                if board.id == "esp32-s2-kaluga-1":
                    openocd_interface = "ftdi/esp32s2_kaluga_v1"
                else:
                    openocd_interface = "ftdi/esp32_devkitj_v1"
            elif link == "esp-bridge":
                openocd_interface = "esp_usb_bridge"
            elif link == "esp-builtin":
                openocd_interface = "esp_usb_jtag"
            else:
                openocd_interface = "ftdi/" + link

            server_args = [
                "-s",
                "$PACKAGE_DIR/share/openocd/scripts",
                "-f",
                "interface/%s.cfg" % openocd_interface,
                "-f",
                "%s/%s"
                % (
                    ("target", debug.get("openocd_target"))
                    if "openocd_target" in debug
                    else ("board", debug.get("openocd_board"))
                ),
            ]

            debug["tools"][link] = {
                "server": {
                    "package": "tool-openocd-esp32",
                    "executable": "bin/openocd",
                    "arguments": server_args,
                },
                "init_break": "thb app_main",
                "init_cmds": [
                    "define pio_reset_halt_target",
                    "   monitor reset halt",
                    "   flushregs",
                    "end",
                    "define pio_reset_run_target",
                    "   monitor reset",
                    "end",
                    "target extended-remote $DEBUG_PORT",
                    "$LOAD_CMDS",
                    "pio_reset_halt_target",
                    "$INIT_BREAK",
                ],
                "onboard": link in debug.get("onboard_tools", []),
                "default": link == debug.get("default_tool"),
            }

            # Avoid erasing Arduino Nano bootloader by preloading app binary
            if board.id == "arduino_nano_esp32":
                debug["tools"][link]["load_cmds"] = "preload"
        board.manifest["debug"] = debug
        return board

    def configure_debug_session(self, debug_config):
        build_extra_data = debug_config.build_data.get("extra", {})
        flash_images = build_extra_data.get("flash_images", [])

        if "openocd" in (debug_config.server or {}).get("executable", ""):
            debug_config.server["arguments"].extend(
                ["-c", "adapter speed %s" % (debug_config.speed or "5000")]
            )

        ignore_conds = [
            debug_config.load_cmds != ["load"],
            not flash_images,
            not all([os.path.isfile(item["path"]) for item in flash_images]),
        ]

        if any(ignore_conds):
            return

        load_cmds = [
            'monitor program_esp "{{{path}}}" {offset} verify'.format(
                path=to_unix_path(item["path"]), offset=item["offset"]
            )
            for item in flash_images
        ]
        load_cmds.append(
            'monitor program_esp "{%s.bin}" %s verify'
            % (
                to_unix_path(debug_config.build_data["prog_path"][:-4]),
                build_extra_data.get("application_offset", "0x10000"),
            )
        )
        debug_config.load_cmds = load_cmds
