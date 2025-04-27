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
import subprocess
import sys
import shutil
from os.path import join

from platformio.public import PlatformBase, to_unix_path
from platformio.proc import get_pythonexe_path
from platformio.project.config import ProjectConfig
from platformio.package.manager.tool import ToolPackageManager


IS_WINDOWS = sys.platform.startswith("win")
# Set Platformio env var to use windows_amd64 for all windows architectures
# only windows_amd64 native espressif toolchains are available
# needs platformio/pioarduino core >= 6.1.17
if IS_WINDOWS:
    os.environ["PLATFORMIO_SYSTEM_TYPE"] = "windows_amd64"

python_exe = get_pythonexe_path()
pm = ToolPackageManager()

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

        def install_tool(TOOL):
            self.packages[TOOL]["optional"] = False
            TOOL_PATH = os.path.join(ProjectConfig.get_instance().get("platformio", "packages_dir"), TOOL)
            TOOL_PACKAGE_PATH = os.path.join(TOOL_PATH, "package.json")
            TOOLS_PATH_DEFAULT = os.path.join(os.path.expanduser("~"), ".platformio")
            IDF_TOOLS = os.path.join(ProjectConfig.get_instance().get("platformio", "packages_dir"), "tl-install", "tools", "idf_tools.py")
            TOOLS_JSON_PATH = os.path.join(TOOL_PATH, "tools.json")
            TOOLS_PIO_PATH = os.path.join(TOOL_PATH, ".piopm")
            IDF_TOOLS_CMD = (
                python_exe,
                IDF_TOOLS,
                "--quiet",
                "--non-interactive",
                "--tools-json",
                TOOLS_JSON_PATH,
                "install"
            )

            tl_flag = bool(os.path.exists(IDF_TOOLS))
            json_flag = bool(os.path.exists(TOOLS_JSON_PATH))
            pio_flag = bool(os.path.exists(TOOLS_PIO_PATH))
            if tl_flag and json_flag:
                rc = subprocess.run(IDF_TOOLS_CMD).returncode
                if rc != 0:
                    sys.stderr.write("Error: Couldn't execute 'idf_tools.py install'\n")
                else:
                    tl_path = "file://" + join(TOOLS_PATH_DEFAULT, "tools", TOOL)
                    if not os.path.exists(join(TOOLS_PATH_DEFAULT, "tools", TOOL, "package.json")):
                        shutil.copyfile(TOOL_PACKAGE_PATH, join(TOOLS_PATH_DEFAULT, "tools", TOOL, "package.json"))
                    self.packages.pop(TOOL, None)
                    if os.path.exists(TOOL_PATH) and os.path.isdir(TOOL_PATH):
                        try:
                            shutil.rmtree(TOOL_PATH)
                        except Exception as e:
                            print(f"Error while removing the tool folder: {e}")                   
                    pm.install(tl_path)
            # tool is already installed, just activate it
            if tl_flag and pio_flag and not json_flag:
                self.packages[TOOL]["version"] = TOOL_PATH
                self.packages[TOOL]["optional"] = False
            return

        # Installer only needed for setup, deactivate when installed
        if bool(os.path.exists(os.path.join(ProjectConfig.get_instance().get("platformio", "packages_dir"), "tl-install", "tools", "idf_tools.py"))):
            self.packages["tl-install"]["optional"] = True

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

        # Enable check tools only when "check_tool" is active
        for p in self.packages:
            if p in ("tool-cppcheck", "tool-clangtidy", "tool-pvs-studio"):
                self.packages[p]["optional"] = False if str(variables.get("check_tool")).strip("['']") in p else True

        if "buildfs" in targets:
            filesystem = variables.get("board_build.filesystem", "littlefs")
            if filesystem == "littlefs":
                self.packages["tool-mklittlefs"]["optional"] = False
            elif filesystem == "fatfs":
                self.packages["tool-mkfatfs"]["optional"] = False
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

        # install GDB and OpenOCD when debug mode or upload_protocol is set
        if (variables.get("build_type") or "debug" in "".join(targets)) or variables.get("upload_protocol"):
            for gdb_package in ("tool-xtensa-esp-elf-gdb", "tool-riscv32-esp-elf-gdb"):
                self.packages[gdb_package]["optional"] = False
            install_tool("tool-openocd-esp32")

        # Common packages for IDF and mixed Arduino+IDF projects
        if "espidf" in frameworks:
            self.packages["toolchain-esp32ulp"]["optional"] = False
            for p in self.packages:
                if p in (
                    "tool-cmake",
                    "tool-ninja",
                    "tool-scons",
                 ):
                    self.packages[p]["optional"] = False

        if mcu in ("esp32", "esp32s2", "esp32s3"):
            self.packages["toolchain-xtensa-esp-elf"]["optional"] = False
        else:
            self.packages.pop("toolchain-xtensa-esp-elf", None)

        if mcu in ("esp32s2", "esp32s3", "esp32c2", "esp32c3", "esp32c5", "esp32c6", "esp32h2", "esp32p4"):
            if mcu in ("esp32c2", "esp32c3", "esp32c5", "esp32c6", "esp32h2", "esp32p4"):
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
