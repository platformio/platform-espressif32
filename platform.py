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

import copy
import os

from platformio import fs
from platformio.managers.platform import PlatformBase
from platformio.util import get_systype


class Espressif32Platform(PlatformBase):
    def configure_default_packages(self, variables, targets):
        if not variables.get("board"):
            return PlatformBase.configure_default_packages(self, variables, targets)

        board_config = self.board_config(variables.get("board"))
        mcu = variables.get("board_build.mcu", board_config.get("build.mcu", "esp32"))
        frameworks = variables.get("pioframework", [])
        if "buildfs" in targets:
            self.packages["tool-mkspiffs"]["optional"] = False
        if variables.get("upload_protocol"):
            self.packages["tool-openocd-esp32"]["optional"] = False
        if os.path.isdir("ulp"):
            self.packages["toolchain-esp32ulp"]["optional"] = False
        if "espidf" in frameworks:
            for p in self.packages:
                if p in ("tool-cmake", "tool-ninja", "toolchain-%sulp" % mcu):
                    self.packages[p]["optional"] = False
                elif p in ("tool-mconf", "tool-idf") and "windows" in get_systype():
                    self.packages[p]["optional"] = False
            self.packages["toolchain-xtensa32"]["version"] = "~2.80400.0"
            if "arduino" in frameworks:
                # Arduino component is not compatible with ESP-IDF >=4.1
                self.packages["framework-espidf"]["version"] = "~3.40001.0"
        # ESP32-S2 toolchain is identical for both Arduino and ESP-IDF
        if mcu == "esp32s2":
            self.packages.pop("toolchain-xtensa32", None)
            self.packages["toolchain-xtensa32s2"]["optional"] = False
            self.packages["toolchain-esp32s2ulp"]["optional"] = False

        build_core = variables.get(
            "board_build.core", board_config.get("build.core", "arduino")
        ).lower()
        if build_core == "mbcwb":
            self.packages["framework-arduinoespressif32"]["optional"] = True
            self.packages["framework-arduino-mbcwb"]["optional"] = False
            self.packages["tool-mbctool"]["type"] = "uploader"
            self.packages["tool-mbctool"]["optional"] = False

        return PlatformBase.configure_default_packages(self, variables, targets)

    def get_boards(self, id_=None):
        result = PlatformBase.get_boards(self, id_)
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
        non_debug_protocols = ["esptool", "espota", "mbctool"]
        supported_debug_tools = [
            "esp-prog",
            "iot-bus-jtag",
            "jlink",
            "minimodule",
            "olimex-arm-usb-tiny-h",
            "olimex-arm-usb-ocd-h",
            "olimex-arm-usb-ocd",
            "olimex-jtag-tiny",
            "tumpa",
        ]

        upload_protocol = board.manifest.get("upload", {}).get("protocol")
        upload_protocols = board.manifest.get("upload", {}).get("protocols", [])
        if debug:
            upload_protocols.extend(supported_debug_tools)
        if upload_protocol and upload_protocol not in upload_protocols:
            upload_protocols.append(upload_protocol)
        board.manifest["upload"]["protocols"] = upload_protocols

        if "tools" not in debug:
            debug["tools"] = {}

        # Only FTDI based debug probes
        for link in upload_protocols:
            if link in non_debug_protocols or link in debug["tools"]:
                continue

            if link == "jlink":
                openocd_interface = link
            elif link in ("esp-prog", "ftdi"):
                if board.id == "esp32-s2-kaluga-1":
                    openocd_interface = "ftdi/esp32s2_kaluga_v1"
                else:
                    openocd_interface = "ftdi/esp32_devkitj_v1"
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

        board.manifest["debug"] = debug
        return board

    def configure_debug_session(self, debug_config):
        build_extra_data = debug_config.build_data.get("extra", {})
        flash_images = build_extra_data.get("flash_images", [])

        if "openocd" in (debug_config.server or {}).get("executable", ""):
            debug_config.server["arguments"].extend(
                ["-c", "adapter_khz %s" % (debug_config.speed or "5000")]
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
                path=fs.to_unix_path(item["path"]), offset=item["offset"]
            )
            for item in flash_images
        ]
        load_cmds.append(
            'monitor program_esp "{%s.bin}" %s verify'
            % (
                fs.to_unix_path(debug_config.build_data["prog_path"][:-4]),
                build_extra_data.get("application_offset", "0x10000"),
            )
        )
        debug_config.load_cmds = load_cmds

    def configure_debug_options(self, initial_debug_options, ide_data):
        """
        Deprecated. Remove method when PlatformIO Core 5.2 is released
        """
        ide_extra_data = ide_data.get("extra", {})
        flash_images = ide_extra_data.get("flash_images", [])
        debug_options = copy.deepcopy(initial_debug_options)

        if "openocd" in debug_options["server"].get("executable", ""):
            debug_options["server"]["arguments"].extend(
                ["-c", "adapter_khz %s" % (initial_debug_options.get("speed") or "5000")]
            )

        ignore_conds = [
            initial_debug_options["load_cmds"] != ["load"],
            not flash_images,
            not all([os.path.isfile(item["path"]) for item in flash_images]),
        ]

        if any(ignore_conds):
            return debug_options

        load_cmds = [
            'monitor program_esp "{{{path}}}" {offset} verify'.format(
                path=fs.to_unix_path(item["path"]), offset=item["offset"]
            )
            for item in flash_images
        ]
        load_cmds.append(
            'monitor program_esp "{%s.bin}" %s verify'
            % (
                fs.to_unix_path(ide_data["prog_path"][:-4]),
                ide_extra_data.get("application_offset", "0x10000"),
            )
        )
        debug_options["load_cmds"] = load_cmds
        return debug_options
