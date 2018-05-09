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

from platformio.managers.platform import PlatformBase


class Espressif32Platform(PlatformBase):

    def configure_default_packages(self, variables, targets):
        if "arduino" in variables.get("pioframework"):
            self.packages['toolchain-xtensa32']['version'] = "~2.50200.0"
        return PlatformBase.configure_default_packages(
            self, variables, targets)

    def get_boards(self, id_=None):
        result = PlatformBase.get_boards(self, id_)
        if not result:
            return result
        if id_:
            return self._add_default_debug_tools(result)
        else:
            for key, value in result.items():
                result[key] = self._add_default_debug_tools(result[key])
        return result

    def _add_default_debug_tools(self, board):
        debug = board.manifest.get("debug", {})

        upload_protocols = board.manifest.get("upload", {}).get(
            "protocols", [])
        if "tools" not in debug:
            debug['tools'] = {}

        available_tools = (
            "olimex-arm-usb-ocd-h", "olimex-arm-usb-tiny-h", "minimodule"
        )

        # Only FTDI based debug probes
        for link in available_tools:
            if link not in upload_protocols or link in debug['tools']:
                continue

            ftdi_interface = "mbftdi" if link == "minimodule" else link

            server_args = [
                "-s", "$PACKAGE_DIR/share/openocd/scripts",
                "-f", "share/openocd/scripts/interface/ftdi/%s.cfg" % ftdi_interface,
                "-f", "share/openocd/scripts/board/%s" % debug.get("openocd_board")
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
                    "   mon reset halt",
                    "   x $a1=0",
                    "end",
                    "define pio_reset_target",
                    "   mon reset",
                    "end",
                    "target extended-remote $DEBUG_PORT",
                    "$INIT_BREAK",
                    "mon reset halt",
                    "$LOAD_CMD",
                    "pio_reset_halt_target"
                ],
                # "load_cmd": 'preload'
                "load_cmd": 'mon program_esp32 "$PROG_DIR/firmware.bin" 0x10000 verify'
            }

        board.manifest['debug'] = debug
        return board
