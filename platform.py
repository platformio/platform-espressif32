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
import json
import os
import sys
import re
import urllib

import requests

from platformio import fs
from platformio.managers.platform import PlatformBase
from platformio.util import get_systype


class Espressif32Platform(PlatformBase):
    def configure_default_packages(self, variables, targets):
        if not variables.get("board"):
            return PlatformBase.configure_default_packages(self, variables, targets)

        # Force PlatformIO to process custom platform packages before
        # invoking base implementation
        self._custom_packages = variables.get("platform_packages")

        board_config = self.board_config(variables.get("board"))
        mcu = variables.get("board_build.mcu", board_config.get("build.mcu", "esp32"))
        build_core = variables.get(
            "board_build.core", board_config.get("build.core", "arduino")
        ).lower()
        frameworks = variables.get("pioframework", [])
        if "buildfs" in targets:
            self.packages["tool-mkspiffs"]["optional"] = False
        if variables.get("upload_protocol"):
            self.packages["tool-openocd-esp32"]["optional"] = False
        if os.path.isdir("ulp"):
            self.packages["toolchain-esp32ulp"]["optional"] = False

        # This logic is temporary as the platform is gradually being switched to the
        # toolchain packages from the Espressif organization.
        xtensa32_toolchain = "toolchain-xtensa32"
        xtensa32s2_toolchain = "toolchain-xtensa32s2"
        riscv_toolchain = "toolchain-riscv-esp"
        if len(frameworks) == 1 and "arduino" in frameworks and build_core == "esp32":
            # Remove default toolchains so they won't conflict with upstream
            self.packages.pop(xtensa32_toolchain, None)
            self.packages.pop(xtensa32s2_toolchain, None)
            self.packages.pop(riscv_toolchain, None)

            xtensa32_toolchain = "toolchain-xtensa-esp32"
            xtensa32s2_toolchain = "toolchain-xtensa-esp32s2"
            riscv_toolchain = "toolchain-riscv32-esp"

            # In case the upstream Arduino framework is specified in configuration file
            # then we need to dynamically extract toolchain versions from Arduino index
            # file. This feature can be disabled via a special option:
            if (
                variables.get(
                    "board_build.arduino.upstream_packages",
                    board_config.get("build.arduino.upstream_packages", "yes"),
                ).lower()
                == "yes"
            ):
                package_version = self.packages["framework-arduinoespressif32"][
                    "version"
                ]

                url_items = urllib.parse.urlparse(package_version)
                # Only GitHub repositories support dynamic packages
                if (
                    url_items.scheme in ("http", "https")
                    and url_items.netloc.startswith("github")
                    and url_items.path.endswith(".git")
                ):
                    self.configure_upstream_arduino_packages(url_items)
        else:
            # Remove upstream packages
            self.packages.pop("toolchain-xtensa-esp32", None)
            self.packages.pop("toolchain-xtensa-esp32s2", None)
            self.packages.pop("toolchain-riscv32-esp", None)

        if "espidf" in frameworks:
            for p in self.packages:
                if p in ("tool-cmake", "tool-ninja", "toolchain-%sulp" % mcu):
                    self.packages[p]["optional"] = False
                elif p in ("tool-mconf", "tool-idf") and "windows" in get_systype():
                    self.packages[p]["optional"] = False
            self.packages[xtensa32_toolchain]["version"] = "~2.80400.0"
            self.packages[xtensa32_toolchain]["optional"] = False

            if "arduino" in frameworks:
                # Arduino component is not compatible with ESP-IDF >=4.1
                self.packages["framework-espidf"]["version"] = "~3.40001.0"

        if mcu in ("esp32s2", "esp32c3"):
            self.packages.pop(xtensa32_toolchain, None)
            self.packages.pop("toolchain-esp32ulp", None)
            # RISC-V based toolchain for ESP32C3 and ESP32S2 ULP
            self.packages[riscv_toolchain]["optional"] = False
            if mcu == "esp32s2":
                self.packages[xtensa32s2_toolchain]["optional"] = False
                self.packages["toolchain-esp32s2ulp"]["optional"] = False

        if "arduino" in frameworks and build_core == "mbcwb":
            # Briki MCB core packages depend on previous toolchain packages
            self.packages.pop("toolchain-xtensa-esp32", None)
            self.packages["toolchain-xtensa32"]["optional"] = False
            self.packages["toolchain-xtensa32"]["version"] = "~2.50200.0"
            self.packages["framework-arduinoespressif32"]["optional"] = True
            self.packages["framework-arduino-mbcwb"]["optional"] = False
            self.packages["tool-mbctool"]["type"] = "uploader"
            self.packages["tool-mbctool"]["optional"] = False

        if set(("simba", "pumbaa")) & set(frameworks):
            # Legacy frameworks depend on previous toolchain packages
            self.packages["toolchain-xtensa32"]["optional"] = False
            self.packages["toolchain-xtensa32"]["version"] = "~2.50200.0"

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

    @staticmethod
    def get_tool_dependencies(index_data):
        for p in index_data.get("packages", []):
            if p["name"] == "esp32":
                for p in p["platforms"]:
                    if p["name"] == "esp32":
                        return p["toolsDependencies"]

        return []

    @staticmethod
    def extract_toolchain_versions(tool_deps):
        def _parse_version(original_version):
            assert original_version
            match = re.match(r"^gcc(\d+)_(\d+)_(\d+)\-esp\-(.+)$", original_version)
            if not match:
                raise ValueError("Bad package version `%s`" % original_version)
            assert len(match.groups()) == 4
            return "%s.%s.%s+%s" % (match.groups())

        if not tool_deps:
            return {}

        toolchain_remap = {
            "xtensa-esp32-elf-gcc": "toolchain-xtensa-esp32",
            "xtensa-esp32s2-elf-gcc": "toolchain-xtensa-esp32s2",
            "riscv32-esp-elf-gcc": "toolchain-riscv32-esp",
        }

        result = dict()
        for tool in tool_deps:
            if tool["name"] in toolchain_remap:
                result[toolchain_remap[tool["name"]]] = _parse_version(tool["version"])

        return result

    @staticmethod
    def download_remote_package_index(url_items):
        def _prepare_url_for_index_file(url_items):
            tag = "master"
            if url_items.fragment:
                tag = url_items.fragment
            return (
                "https://raw.githubusercontent.com/%s/"
                "%s/package/package_esp32_index.template.json"
                % (url_items.path.replace(".git", ""), tag)
            )

        index_file_url = _prepare_url_for_index_file(url_items)
        if not index_file_url:
            return {}

        r = requests.get(index_file_url, timeout=10)
        if r.status_code == 200:
            return r.json()
        return {}

    def configure_arduino_toolchains(self, package_index):
        if not package_index:
            return
        toolchain_packages = self.extract_toolchain_versions(
            self.get_tool_dependencies(package_index)
        )
        for toolchain_package, version in toolchain_packages.items():
            if not version:
                print("Broken package version for `%s`" % toolchain_package)
                continue
            if toolchain_package not in self.packages:
                self.packages[toolchain_package] = dict()
            print("* Adding toolchain %s with version %s" % (toolchain_package, version))
            self.packages[toolchain_package]["version"] = version
            self.packages[toolchain_package]["owner"] = "espressif"

    def configure_upstream_arduino_packages(self, url_itmes):
        try:
            framework_index_file = os.path.join(
                self.get_package_dir("framework-arduinoespressif32") or "",
                "package",
                "package_esp32_index.template.json",
            )
            if os.path.isfile(framework_index_file):
                with open(framework_index_file) as fp:
                    self.configure_arduino_toolchains(json.load(fp))
            else:
                print("Configuring from remote")
                self.configure_arduino_toolchains(
                    self.download_remote_package_index(url_itmes)
                )
        except Exception as e:
            sys.stderr.write(
                "Error! Failed to extract upstream toolchain configurations:\n%s\n"
                % str(e)
            )
            sys.stderr.write(
                "You can disable this feature via the "
                "`board_build.arduino.upstream_packages = no` setting in your "
                "`platformio.ini` file.\n"
            )
            sys.exit(1)
