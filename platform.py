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

import fnmatch
import os
import json
import subprocess
import sys
import shutil
import logging
from typing import Optional, Dict, List, Any

from platformio.compat import IS_WINDOWS
from platformio.public import PlatformBase, to_unix_path
from platformio.proc import get_pythonexe_path
from platformio.project.config import ProjectConfig
from platformio.package.manager.tool import ToolPackageManager

# Constants
RETRY_LIMIT = 3
SUBPROCESS_TIMEOUT = 300
DEFAULT_DEBUG_SPEED = "5000"
DEFAULT_APP_OFFSET = "0x10000"
tl_install_name = "tool-esp_install"

# MCUs that support ESP-builtin debug
ESP_BUILTIN_DEBUG_MCUS = frozenset([
    "esp32c3", "esp32c5", "esp32c6", "esp32s3", "esp32h2", "esp32p4"
])

# MCU configuration mapping
MCU_TOOLCHAIN_CONFIG = {
    "xtensa": {
        "mcus": frozenset(["esp32", "esp32s2", "esp32s3"]),
        "toolchains": ["toolchain-xtensa-esp-elf"],
        "debug_tools": ["tool-xtensa-esp-elf-gdb"]
    },
    "riscv": {
        "mcus": frozenset([
            "esp32c2", "esp32c3", "esp32c5", "esp32c6", "esp32h2", "esp32p4"
        ]),
        "toolchains": ["toolchain-riscv32-esp"],
        "debug_tools": ["tool-riscv32-esp-elf-gdb"]
    }
}

COMMON_IDF_PACKAGES = [
    "tool-cmake",
    "tool-ninja",
    "tool-scons",
    "tool-esp-rom-elfs"
]

CHECK_PACKAGES = [
    "tool-cppcheck",
    "tool-clangtidy",
    "tool-pvs-studio"
]

# System-specific configuration
# Set Platformio env var to use windows_amd64 for all windows architectures
# only windows_amd64 native espressif toolchains are available
if IS_WINDOWS:
    os.environ["PLATFORMIO_SYSTEM_TYPE"] = "windows_amd64"

# Set IDF_TOOLS_PATH to Pio core_dir
PROJECT_CORE_DIR=ProjectConfig.get_instance().get("platformio", "core_dir")
IDF_TOOLS_PATH=os.path.join(PROJECT_CORE_DIR)
os.environ["IDF_TOOLS_PATH"] = IDF_TOOLS_PATH

# Global variables
python_exe = get_pythonexe_path()
pm = ToolPackageManager()

# Configure logger
logger = logging.getLogger(__name__)


def safe_file_operation(operation_func):
    """Decorator for safe filesystem operations with error handling."""
    def wrapper(*args, **kwargs):
        try:
            return operation_func(*args, **kwargs)
        except (OSError, IOError, FileNotFoundError) as e:
            logger.error(f"Filesystem error in {operation_func.__name__}: {e}")
            return False
        except Exception as e:
            logger.error(f"Unexpected error in {operation_func.__name__}: {e}")
            raise  # Re-raise unexpected exceptions
    return wrapper


@safe_file_operation
def safe_remove_file(path: str) -> bool:
    """Safely remove a file with error handling."""
    if os.path.exists(path) and os.path.isfile(path):
        os.remove(path)
        logger.debug(f"File removed: {path}")
    return True


@safe_file_operation
def safe_remove_directory(path: str) -> bool:
    """Safely remove directories with error handling."""
    if os.path.exists(path) and os.path.isdir(path):
        shutil.rmtree(path)
        logger.debug(f"Directory removed: {path}")
    return True


@safe_file_operation
def safe_remove_directory_pattern(base_path: str, pattern: str) -> bool:
    """Safely remove directories matching a pattern with error handling."""
    if not os.path.exists(base_path):
        return True
    # Find all directories matching the pattern in the base directory
    for item in os.listdir(base_path):
        item_path = os.path.join(base_path, item)
        if os.path.isdir(item_path) and fnmatch.fnmatch(item, pattern):
            shutil.rmtree(item_path)
            logger.debug(f"Directory removed: {item_path}")
    return True


@safe_file_operation
def safe_copy_file(src: str, dst: str) -> bool:
    """Safely copy files with error handling."""
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    logger.debug(f"File copied: {src} -> {dst}")
    return True


@safe_file_operation
def safe_copy_directory(src: str, dst: str) -> bool:
    """Safely copy directories with error handling."""
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copytree(src, dst, dirs_exist_ok=True)
    logger.debug(f"Directory copied: {src} -> {dst}")
    return True


class Espressif32Platform(PlatformBase):
    """ESP32 platform implementation for PlatformIO with optimized toolchain management."""

    def __init__(self, *args, **kwargs):
        """Initialize the ESP32 platform with caching mechanisms."""
        super().__init__(*args, **kwargs)
        self._packages_dir = None
        self._tools_cache = {}
        self._mcu_config_cache = {}

    @property
    def packages_dir(self) -> str:
        """Get cached packages directory path."""
        if self._packages_dir is None:
            config = ProjectConfig.get_instance()
            self._packages_dir = config.get("platformio", "packages_dir")
        return self._packages_dir

    def _check_tl_install_version(self) -> bool:
        """
        Check if tool-esp_install is installed in the correct version.
        Install the correct version only if version differs.
        
        Returns:
            bool: True if correct version is available, False on error
        """
        
        # Get required version from platform.json
        required_version = self.packages.get(tl_install_name, {}).get("version")
        if not required_version:
            logger.debug(f"No version check required for {tl_install_name}")
            return True
        
        # Check if tool is already installed
        tl_install_path = os.path.join(self.packages_dir, tl_install_name)
        package_json_path = os.path.join(tl_install_path, "package.json")
        
        if not os.path.exists(package_json_path):
            logger.info(f"{tl_install_name} not installed, installing version {required_version}")
            return self._install_tl_install(required_version)
        
        # Read installed version
        try:
            with open(package_json_path, 'r', encoding='utf-8') as f:
                package_data = json.load(f)
            
            installed_version = package_data.get("version")
            if not installed_version:
                logger.warning(f"Installed version for {tl_install_name} unknown, installing {required_version}")
                return self._install_tl_install(required_version)
            
            # IMPORTANT: Compare versions correctly
            if self._compare_tl_install_versions(installed_version, required_version):
                logger.debug(f"{tl_install_name} version {installed_version} is already correctly installed")
                # IMPORTANT: Set package as available, but do NOT reinstall
                self.packages[tl_install_name]["optional"] = True
                return True
            else:
                logger.info(
                    f"Version mismatch for {tl_install_name}: "
                    f"installed={installed_version}, required={required_version}, installing correct version"
                )
                return self._install_tl_install(required_version)
            
        except (json.JSONDecodeError, FileNotFoundError) as e:
            logger.error(f"Error reading package data for {tl_install_name}: {e}")
            return self._install_tl_install(required_version)

    def _compare_tl_install_versions(self, installed: str, required: str) -> bool:
        """
        Compare installed and required version of tool-esp_install.
        
        Args:
            installed: Currently installed version string
            required: Required version string from platform.json
            
        Returns:
            bool: True if versions match, False otherwise
        """
        # For URL-based versions: Extract version string from URL
        installed_clean = self._extract_version_from_url(installed)
        required_clean = self._extract_version_from_url(required)
        
        logger.debug(f"Version comparison: installed='{installed_clean}' vs required='{required_clean}'")
        
        return installed_clean == required_clean

    def _extract_version_from_url(self, version_string: str) -> str:
        """
        Extract version information from URL or return version directly.
        
        Args:
            version_string: Version string or URL containing version
            
        Returns:
            str: Extracted version string
        """
        if version_string.startswith(('http://', 'https://')):
            # Extract version from URL like: .../v5.1.0/esp_install-v5.1.0.zip
            import re
            version_match = re.search(r'v(\d+\.\d+\.\d+)', version_string)
            if version_match:
                return version_match.group(1)  # Returns "5.1.0"
            else:
                # Fallback: Use entire URL
                return version_string
        else:
            # Direct version number
            return version_string.strip()

    def _install_tl_install(self, version: str) -> bool:
        """
        Install tool-esp_install ONLY when necessary
        and handles backwards compatibility for tl-install.

        Args:
            version: Version string or URL to install
   
        Returns:
            bool: True if installation successful, False otherwise
        """
        tl_install_path = os.path.join(self.packages_dir, tl_install_name)
        old_tl_install_path = os.path.join(self.packages_dir, "tl-install")

        try:
            old_tl_install_exists = os.path.exists(old_tl_install_path)
            if old_tl_install_exists:
                # remove outdated tl-install
                safe_remove_directory(old_tl_install_path)

            if os.path.exists(tl_install_path):
                logger.info(f"Removing old {tl_install_name} installation")
                safe_remove_directory(tl_install_path)

            logger.info(f"Installing {tl_install_name} version {version}")
            self.packages[tl_install_name]["optional"] = False
            self.packages[tl_install_name]["version"] = version
            pm.install(version)
            # Ensure backward compatibility by removing pio install status indicator
            tl_piopm_path = os.path.join(tl_install_path, ".piopm")
            safe_remove_file(tl_piopm_path)

            if os.path.exists(os.path.join(tl_install_path, "package.json")):
                logger.info(f"{tl_install_name} successfully installed and verified")
                self.packages[tl_install_name]["optional"] = True
            
                # Handle old tl-install to keep backwards compatibility
                if old_tl_install_exists:
                    # Copy tool-esp_install content to tl-install location
                    if safe_copy_directory(tl_install_path, old_tl_install_path):
                        logger.info(f"Content copied from {tl_install_name} to old tl-install location")
                    else:
                        logger.warning("Failed to copy content to old tl-install location")
                return True
            else:
                logger.error(f"{tl_install_name} installation failed - package.json not found")
                return False
        
        except Exception as e:
            logger.error(f"Error installing {tl_install_name}: {e}")
            return False

    def _cleanup_versioned_tool_directories(self, tool_name: str) -> None:
        """
        Clean up versioned tool directories containing '@' or version suffixes.
        This function should be called during every tool version check.
        
        Args:
            tool_name: Name of the tool to clean up
        """
        if not os.path.exists(self.packages_dir) or not os.path.isdir(self.packages_dir):
            return
            
        try:
            # Remove directories with '@' in their name (e.g., tool-name@version, tool-name@src)
            safe_remove_directory_pattern(self.packages_dir, f"{tool_name}@*")
            
            # Remove directories with version suffixes (e.g., tool-name.12345)
            safe_remove_directory_pattern(self.packages_dir, f"{tool_name}.*")
            
            # Also check for any directory that starts with tool_name and contains '@'
            for item in os.listdir(self.packages_dir):
                if item.startswith(tool_name) and '@' in item:
                    item_path = os.path.join(self.packages_dir, item)
                    if os.path.isdir(item_path):
                        safe_remove_directory(item_path)
                        logger.debug(f"Removed versioned directory: {item_path}")
                        
        except OSError as e:
            logger.error(f"Error cleaning up versioned directories for {tool_name}: {e}")

    def _get_tool_paths(self, tool_name: str) -> Dict[str, str]:
        """Get centralized path calculation for tools with caching."""
        if tool_name not in self._tools_cache:
            tool_path = os.path.join(self.packages_dir, tool_name)
            
            self._tools_cache[tool_name] = {
                'tool_path': tool_path,
                'package_path': os.path.join(tool_path, "package.json"),
                'tools_json_path': os.path.join(tool_path, "tools.json"),
                'piopm_path': os.path.join(tool_path, ".piopm"),
                'idf_tools_path': os.path.join(
                    self.packages_dir, tl_install_name, "tools", "idf_tools.py"
                )
            }
        return self._tools_cache[tool_name]

    def _check_tool_status(self, tool_name: str) -> Dict[str, bool]:
        """Check the installation status of a tool."""
        paths = self._get_tool_paths(tool_name)
        return {
            'has_idf_tools': os.path.exists(paths['idf_tools_path']),
            'has_tools_json': os.path.exists(paths['tools_json_path']),
            'has_piopm': os.path.exists(paths['piopm_path']),
            'tool_exists': os.path.exists(paths['tool_path'])
        }

    def _run_idf_tools_install(self, tools_json_path: str, idf_tools_path: str) -> bool:
        """Execute idf_tools.py install command with timeout and error handling."""
        cmd = [
            python_exe,
            idf_tools_path,
            "--quiet",
            "--non-interactive",
            "--tools-json",
            tools_json_path,
            "install"
        ]

        try:
            result = subprocess.run(
                cmd,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=SUBPROCESS_TIMEOUT,
                check=False
            )

            if result.returncode != 0:
                logger.error("idf_tools.py installation failed")
                return False

            logger.debug("idf_tools.py executed successfully")
            return True

        except subprocess.TimeoutExpired:
            logger.error(f"Timeout in idf_tools.py after {SUBPROCESS_TIMEOUT}s")
            return False
        except (subprocess.SubprocessError, OSError) as e:
            logger.error(f"Error in idf_tools.py: {e}")
            return False

    def _check_tool_version(self, tool_name: str) -> bool:
        """Check if the installed tool version matches the required version."""
        # Clean up versioned directories FIRST, before any version checks
        self._cleanup_versioned_tool_directories(tool_name)
        
        paths = self._get_tool_paths(tool_name)

        try:
            with open(paths['package_path'], 'r', encoding='utf-8') as f:
                package_data = json.load(f)

            required_version = self.packages.get(tool_name, {}).get("package-version")
            installed_version = package_data.get("version")

            if not required_version:
                logger.debug(f"No version check required for {tool_name}")
                return True

            if not installed_version:
                logger.warning(f"Installed version for {tool_name} unknown")
                return False

            version_match = required_version == installed_version
            if not version_match:
                logger.info(
                    f"Version mismatch for {tool_name}: "
                    f"{installed_version} != {required_version}"
                )

            return version_match

        except (json.JSONDecodeError, FileNotFoundError) as e:
            logger.error(f"Error reading package data for {tool_name}: {e}")
            return False

    def install_tool(self, tool_name: str, retry_count: int = 0) -> bool:
        """Install a tool with optimized retry mechanism."""
        if retry_count >= RETRY_LIMIT:
            logger.error(
                f"Installation of {tool_name} failed after {RETRY_LIMIT} attempts"
            )
            return False

        self.packages[tool_name]["optional"] = False
        paths = self._get_tool_paths(tool_name)
        status = self._check_tool_status(tool_name)

        # Case 1: New installation with idf_tools
        if status['has_idf_tools'] and status['has_tools_json']:
            return self._install_with_idf_tools(tool_name, paths)

        # Case 2: Tool already installed, version check
        if (status['has_idf_tools'] and status['has_piopm'] and
                not status['has_tools_json']):
            return self._handle_existing_tool(tool_name, paths, retry_count)

        logger.debug(f"Tool {tool_name} already configured")
        return True

    def _install_with_idf_tools(self, tool_name: str, paths: Dict[str, str]) -> bool:
        """Install tool using idf_tools.py installation method."""
        if not self._run_idf_tools_install(
            paths['tools_json_path'], paths['idf_tools_path']
        ):
            return False

        # Copy tool files
        target_package_path = os.path.join(
            IDF_TOOLS_PATH, "tools", tool_name, "package.json"
        )

        if not safe_copy_file(paths['package_path'], target_package_path):
            return False

        safe_remove_directory(paths['tool_path'])

        tl_path = f"file://{os.path.join(IDF_TOOLS_PATH, 'tools', tool_name)}"
        pm.install(tl_path)

        logger.info(f"Tool {tool_name} successfully installed")
        return True

    def _handle_existing_tool(
        self, tool_name: str, paths: Dict[str, str], retry_count: int
    ) -> bool:
        """Handle already installed tools with version checking."""
        if self._check_tool_version(tool_name):
            # Version matches, use tool
            self.packages[tool_name]["version"] = paths['tool_path']
            self.packages[tool_name]["optional"] = False
            logger.debug(f"Tool {tool_name} found with correct version")
            return True

        # Wrong version, reinstall - cleanup is already done in _check_tool_version
        logger.info(f"Reinstalling {tool_name} due to version mismatch")

        # Remove the main tool directory (if it still exists after cleanup)
        safe_remove_directory(paths['tool_path'])

        return self.install_tool(tool_name, retry_count + 1)

    def _configure_arduino_framework(self, frameworks: List[str]) -> None:
        """Configure Arduino framework dependencies."""
        if "arduino" not in frameworks:
            return

        self.packages["framework-arduinoespressif32"]["optional"] = False

    def _configure_espidf_framework(
        self, frameworks: List[str], variables: Dict, board_config: Dict, mcu: str
    ) -> None:
        """Configure ESP-IDF framework based on custom sdkconfig settings."""
        custom_sdkconfig = variables.get("custom_sdkconfig")
        board_sdkconfig = variables.get(
            "board_espidf.custom_sdkconfig",
            board_config.get("espidf.custom_sdkconfig", "")
        )

        if custom_sdkconfig is not None or len(str(board_sdkconfig)) > 3:
            frameworks.append("espidf")
            self.packages["framework-espidf"]["optional"] = False

    def _get_mcu_config(self, mcu: str) -> Optional[Dict]:
        """Get MCU configuration with optimized caching and search."""
        if mcu in self._mcu_config_cache:
            return self._mcu_config_cache[mcu]

        for _, config in MCU_TOOLCHAIN_CONFIG.items():
            if mcu in config["mcus"]:
                # Dynamically add ULP toolchain
                result = config.copy()
                result["ulp_toolchain"] = ["toolchain-esp32ulp"]
                if mcu != "esp32":
                    result["ulp_toolchain"].append("toolchain-riscv32-esp")
                self._mcu_config_cache[mcu] = result
                return result
        return None

    def _needs_debug_tools(self, variables: Dict, targets: List[str]) -> bool:
        """Check if debug tools are needed based on build configuration."""
        return bool(
            variables.get("build_type") or
            "debug" in targets or
            variables.get("upload_protocol")
        )

    def _configure_mcu_toolchains(
        self, mcu: str, variables: Dict, targets: List[str]
    ) -> None:
        """Configure MCU-specific toolchains with optimized installation."""
        mcu_config = self._get_mcu_config(mcu)
        if not mcu_config:
            logger.warning(f"Unknown MCU: {mcu}")
            return

        # Install base toolchains
        for toolchain in mcu_config["toolchains"]:
            self.install_tool(toolchain)

        # ULP toolchain if ULP directory exists
        if mcu_config.get("ulp_toolchain") and os.path.isdir("ulp"):
            for toolchain in mcu_config["ulp_toolchain"]:
                self.install_tool(toolchain)

        # Debug tools when needed
        if self._needs_debug_tools(variables, targets):
            for debug_tool in mcu_config["debug_tools"]:
                self.install_tool(debug_tool)
            self.install_tool("tool-openocd-esp32")

    def _configure_installer(self) -> None:
        """Configure the ESP-IDF tools installer with proper version checking."""
        
        # Check version - installs only when needed
        if not self._check_tl_install_version():
            logger.error("Error during tool-esp_install version check / installation")
            return

        # Remove pio install marker to avoid issues when switching versions
        old_tl_piopm_path = os.path.join(self.packages_dir, "tl-install", ".piopm")
        if os.path.exists(old_tl_piopm_path):
            safe_remove_file(old_tl_piopm_path)
        
        # Check if idf_tools.py is available
        installer_path = os.path.join(
            self.packages_dir, tl_install_name, "tools", "idf_tools.py"
        )
        
        if os.path.exists(installer_path):
            logger.debug(f"{tl_install_name} is available and ready")
            self.packages[tl_install_name]["optional"] = True
        else:
            logger.warning(f"idf_tools.py not found in {installer_path}")

    def _install_esptool_package(self) -> None:
        """Install esptool package required for all builds."""
        self.install_tool("tool-esptoolpy")

    def _install_common_idf_packages(self) -> None:
        """Install common ESP-IDF packages required for all builds."""
        for package in COMMON_IDF_PACKAGES:
            self.install_tool(package)

    def _configure_check_tools(self, variables: Dict) -> None:
        """Configure static analysis and check tools based on configuration."""
        check_tools = variables.get("check_tool", [])
        self.install_tool("contrib-piohome")
        if not check_tools:
            return

        for package in CHECK_PACKAGES:
            if any(tool in package for tool in check_tools):
                self.install_tool(package)

    def _ensure_mklittlefs_version(self) -> None:
        """Ensure correct mklittlefs version is installed."""
        piopm_path = os.path.join(self.packages_dir, "tool-mklittlefs", ".piopm")

        if os.path.exists(piopm_path):
            try:
                with open(piopm_path, 'r', encoding='utf-8') as f:
                    package_data = json.load(f)
                version = package_data.get('version', '')
                if not version.startswith("3."):
                    os.remove(piopm_path)
                    logger.info(f"Incompatible mklittlefs version {version} removed (required: 3.x)")
            except (json.JSONDecodeError, KeyError) as e:
                logger.error(f"Error reading mklittlefs package  {e}")

    def _setup_mklittlefs_for_download(self) -> None:
        """Setup mklittlefs for download functionality with version 4.x."""
        mklittlefs_dir = os.path.join(self.packages_dir, "tool-mklittlefs")
        mklittlefs4_dir = os.path.join(
            self.packages_dir, "tool-mklittlefs4"
        )

        # Ensure mklittlefs 3.x is installed
        if not os.path.exists(mklittlefs_dir):
            self.install_tool("tool-mklittlefs")
        if os.path.exists(os.path.join(mklittlefs_dir, "tools.json")):
            self.install_tool("tool-mklittlefs")

        # Install mklittlefs 4.x
        if not os.path.exists(mklittlefs4_dir):
            self.install_tool("tool-mklittlefs4")
        if os.path.exists(os.path.join(mklittlefs4_dir, "tools.json")):
            self.install_tool("tool-mklittlefs4")

        # Copy mklittlefs 4.x over 3.x
        if os.path.exists(mklittlefs4_dir):
            package_src = os.path.join(mklittlefs_dir, "package.json")
            package_dst = os.path.join(mklittlefs4_dir, "package.json")
            safe_copy_file(package_src, package_dst)
            shutil.copytree(mklittlefs4_dir, mklittlefs_dir, dirs_exist_ok=True)
            self.packages.pop("tool-mkfatfs", None)

    def _handle_littlefs_tool(self, for_download: bool) -> None:
        """Handle LittleFS tool installation with special download configuration."""
        if for_download:
            self._setup_mklittlefs_for_download()
        else:
            self._ensure_mklittlefs_version()
            self.install_tool("tool-mklittlefs")

    def _install_filesystem_tool(self, filesystem: str, for_download: bool = False) -> None:
        """Install filesystem-specific tools based on the filesystem type."""
        tool_mapping = {
            "default": lambda: self._handle_littlefs_tool(for_download),
            "fatfs": lambda: self.install_tool("tool-mkfatfs")
        }

        handler = tool_mapping.get(filesystem, tool_mapping["default"])
        handler()

    def _configure_filesystem_tools(self, variables: Dict, targets: List[str]) -> None:
        """Configure filesystem tools based on build targets and filesystem type."""
        filesystem = variables.get("board_build.filesystem", "littlefs")

        if any(target in targets for target in ["buildfs", "uploadfs"]):
            self._install_filesystem_tool(filesystem, for_download=False)

        if "downloadfs" in targets:
            self._install_filesystem_tool(filesystem, for_download=True)

    def configure_default_packages(self, variables: Dict, targets: List[str]) -> Any:
        """Main configuration method with optimized package management."""
        if not variables.get("board"):
            return super().configure_default_packages(variables, targets)

        # Base configuration
        board_config = self.board_config(variables.get("board"))
        mcu = variables.get("board_build.mcu", board_config.get("build.mcu", "esp32"))
        frameworks = list(variables.get("pioframework", []))  # Create copy

        try:
            # Configuration steps
            self._configure_installer()
            self._install_esptool_package()
            self._configure_arduino_framework(frameworks)
            self._configure_espidf_framework(frameworks, variables, board_config, mcu)
            self._configure_mcu_toolchains(mcu, variables, targets)
            self._handle_littlefs_tool(for_download=False)  # Ensure mklittlefs is installed

            if "espidf" in frameworks:
                self._install_common_idf_packages()

            self._configure_check_tools(variables)
            self._configure_filesystem_tools(variables, targets)

            logger.info("Package configuration completed successfully")

        except Exception as e:
            logger.error(f"Error in package configuration: {type(e).__name__}: {e}")
            # Don't re-raise to maintain compatibility

        return super().configure_default_packages(variables, targets)

    def get_boards(self, id_=None):
        """Get board configuration with dynamic options."""
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
        """Add dynamic board options for upload protocols and debug tools."""
        # Upload protocols
        if not board.get("upload.protocols", []):
            board.manifest["upload"]["protocols"] = ["esptool", "espota"]
        if not board.get("upload.protocol", ""):
            board.manifest["upload"]["protocol"] = "esptool"

        # Debug tools
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
            "tumpa"
        ]

        # Special configuration for Kaluga board
        if board.id == "esp32-s2-kaluga-1":
            supported_debug_tools.append("ftdi")

        # ESP-builtin for certain MCUs
        mcu = board.get("build.mcu", "")
        if mcu in ESP_BUILTIN_DEBUG_MCUS:
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

        # Debug tool configuration
        for link in upload_protocols:
            if link in non_debug_protocols or link in debug["tools"]:
                continue

            openocd_interface = self._get_openocd_interface(link, board)
            server_args = self._get_debug_server_args(openocd_interface, debug)

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

    def _get_openocd_interface(self, link: str, board) -> str:
        """Determine OpenOCD interface configuration for debug link."""
        if link in ("jlink", "cmsis-dap"):
            return link
        if link in ("esp-prog", "ftdi"):
            if board.id == "esp32-s2-kaluga-1":
                return "ftdi/esp32s2_kaluga_v1"
            return "ftdi/esp32_devkitj_v1"
        if link == "esp-bridge":
            return "esp_usb_bridge"
        if link == "esp-builtin":
            return "esp_usb_jtag"
        return f"ftdi/{link}"

    def _get_debug_server_args(self, openocd_interface: str, debug: Dict) -> List[str]:
        """Generate debug server arguments for OpenOCD configuration."""
        if 'openocd_target' in debug:
            config_type = 'target'
            config_name = debug.get('openocd_target')
        else:
            config_type = 'board'
            config_name = debug.get('openocd_board')
        return [
            "-s", "$PACKAGE_DIR/share/openocd/scripts",
            "-f", f"interface/{openocd_interface}.cfg",
            "-f", f"{config_type}/{config_name}"
        ]

    def configure_debug_session(self, debug_config):
        """Configure debug session with flash image loading."""
        build_extra_data = debug_config.build_data.get("extra", {})
        flash_images = build_extra_data.get("flash_images", [])

        if "openocd" in (debug_config.server or {}).get("executable", ""):
            debug_config.server["arguments"].extend([
                "-c", f"adapter speed {debug_config.speed or DEFAULT_DEBUG_SPEED}"
            ])

        ignore_conds = [
            debug_config.load_cmds != ["load"],
            not flash_images,
            not all([os.path.isfile(item["path"]) for item in flash_images]),
        ]

        if any(ignore_conds):
            return

        load_cmds = [
            f'monitor program_esp "{to_unix_path(item["path"])}" '
            f'{item["offset"]} verify'
            for item in flash_images
        ]
        load_cmds.append(
            f'monitor program_esp '
            f'"{to_unix_path(debug_config.build_data["prog_path"][:-4])}.bin" '
            f'{build_extra_data.get("application_offset", DEFAULT_APP_OFFSET)} verify'
        )
        debug_config.load_cmds = load_cmds
