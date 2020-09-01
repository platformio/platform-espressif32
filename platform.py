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

from os.path import isdir

from platformio.managers.platform import PlatformBase
from platformio.util import get_systype


class Espressif32Platform(PlatformBase):

    def configure_default_packages(self, variables, targets):
        if not variables.get("board"):
            return PlatformBase.configure_default_packages(
                self, variables, targets)

        board_config = self.board_config(variables.get("board"))
        mcu = variables.get("board_build.mcu", board_config.get("build.mcu", "esp32"))
        frameworks = variables.get("pioframework", [])
        if "buildfs" in targets:
            self.packages['tool-mkspiffs']['optional'] = False
        if variables.get("upload_protocol"):
            self.packages['tool-openocd-esp32']['optional'] = False
        if isdir("ulp"):
            self.packages['toolchain-esp32ulp']['optional'] = False
        if "espidf" in frameworks:
            for p in self.packages:
                if p in ("tool-cmake", "tool-ninja", "toolchain-%sulp" % mcu):
                    self.packages[p]["optional"] = False
                elif p in ("tool-mconf", "tool-idf") and "windows" in get_systype():
                    self.packages[p]['optional'] = False
            self.packages['toolchain-xtensa32']['version'] = "~2.80200.0"
            if "arduino" in frameworks:
                # Arduino component is not compatible with ESP-IDF >=4.1
                self.packages['framework-espidf']['version'] = "~3.40001.0"
        # ESP32-S2 toolchain is identical for both Arduino and ESP-IDF
        if mcu == "esp32s2":
            self.packages.pop("toolchain-xtensa32", None)
            self.packages['toolchain-xtensa32s2']['optional'] = False
            self.packages['toolchain-esp32s2ulp']['optional'] = False
            self.packages['tool-esptoolpy']['version'] = "~1.30000.0"

        build_core = variables.get(
            "board_build.core", board_config.get("build.core", "arduino")).lower()
        if build_core == "mbcwb":
            self.packages['framework-arduinoespressif32']['optional'] = True
            self.packages['framework-arduino-mbcwb']['optional'] = False
            self.packages['tool-mbctool']['type'] = "uploader"
            self.packages['tool-mbctool']['optional'] = False

        return PlatformBase.configure_default_packages(self, variables,
                                                       targets)

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
            board.manifest['upload']['protocols'] = ["esptool", "espota"]
        if not board.get("upload.protocol", ""):
            board.manifest['upload']['protocol'] = "esptool"

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
            "tumpa"
        ]

        upload_protocol = board.manifest.get("upload", {}).get("protocol")
        upload_protocols = board.manifest.get("upload", {}).get(
            "protocols", [])
        if debug:
            upload_protocols.extend(supported_debug_tools)
        if upload_protocol and upload_protocol not in upload_protocols:
            upload_protocols.append(upload_protocol)
        board.manifest['upload']['protocols'] = upload_protocols

        if "tools" not in debug:
            debug['tools'] = {}

        # Only FTDI based debug probes
        for link in upload_protocols:
            if link in non_debug_protocols or link in debug['tools']:
                continue

            if link == "jlink":
                openocd_interface = link
            elif link in ("esp-prog", "ftdi"):
                openocd_interface = "ftdi/esp32_devkitj_v1"
            else:
                openocd_interface = "ftdi/" + link

            server_args = [
                "-s", "$PACKAGE_DIR/share/openocd/scripts",
                "-f", "interface/%s.cfg" % openocd_interface,
                "-f", "board/%s" % debug.get("openocd_board")
            ]

            debug['tools'][link] = {
                "server": {
                    "package": "tool-openocd-esp32",
                    "executable": "bin/openocd",
                    "arguments": server_args
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
                    "$INIT_BREAK"
                ],
                "onboard": link in debug.get("onboard_tools", []),
                "default": link == debug.get("default_tool")

            }

        board.manifest['debug'] = debug
        return board
