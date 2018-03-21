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

        # Only FTDI based debug probes
        for link in ("olimex-arm-usb-tiny-h", "olimex-arm-usb-ocd-h"):
            if link not in upload_protocols or link in debug['tools']:
                continue

            server_args = [
                "-s", "$PACKAGE_DIR/share/openocd/scripts",
                "-f", "share/openocd/scripts/interface/ftdi/%s.cfg" % link,
                "-f", "share/openocd/scripts/board/%s" % debug.get("openocd_board")
            ]

            debug['tools'][link] = {
                "server": {
                    "package": "tool-openocd-esp32",
                    "executable": "bin/openocd",
                    "arguments": server_args
                }
            }

        board.manifest['debug'] = debug
        return board
