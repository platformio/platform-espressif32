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

import locale
import json
import os
import re
import semantic_version
import shlex
import subprocess
import sys
from os.path import isfile, join

from SCons.Script import (
    ARGUMENTS,
    COMMAND_LINE_TARGETS,
    AlwaysBuild,
    Builder,
    Default,
    DefaultEnvironment,
)

from platformio.project.helpers import get_project_dir
from platformio.package.version import pepver_to_semver
from platformio.util import get_serial_ports
from platformio.compat import IS_WINDOWS

# Python dependencies required for the build process
python_deps = {
    "uv": ">=0.1.0",
    "pyyaml": ">=6.0.2",
    "rich-click": ">=1.8.6",
    "zopfli": ">=0.2.2",
    "intelhex": ">=2.3.0",
    "rich": ">=14.0.0",
    "esp-idf-size": ">=1.6.1"
}

# Initialize environment and configuration
env = DefaultEnvironment()
platform = env.PioPlatform()
projectconfig = env.GetProjectConfig()
terminal_cp = locale.getpreferredencoding().lower()
PYTHON_EXE = env.subst("$PYTHONEXE")  # Global Python executable path

# Framework directory path
FRAMEWORK_DIR = platform.get_package_dir("framework-arduinoespressif32")

platformio_dir = projectconfig.get("platformio", "core_dir")
penv_dir = os.path.join(platformio_dir, "penv")

pip_path = os.path.join(
    penv_dir,
    "Scripts" if IS_WINDOWS else "bin",
    "pip" + (".exe" if IS_WINDOWS else ""),
)

def setup_pipenv_in_package():
    """
    Checks if 'penv' folder exists in platformio dir and creates virtual environment if not.
    """
    if not os.path.exists(penv_dir):
        env.Execute(
            env.VerboseAction(
                '"$PYTHONEXE" -m venv --clear "%s"' % penv_dir,
                "Creating a new virtual environment for Python dependencies",
            )
        )

        assert os.path.isfile(
            pip_path
        ), "Error: Failed to create a proper virtual environment. Missing the `pip` binary!"

    penv_python = os.path.join(penv_dir, "Scripts", "python.exe") if IS_WINDOWS else os.path.join(penv_dir, "bin", "python")
    env.Replace(PYTHONEXE=penv_python)
    print(f"PYTHONEXE updated to penv environment: {penv_python}")

setup_pipenv_in_package()
# Update global PYTHON_EXE variable after potential pipenv setup
PYTHON_EXE = env.subst("$PYTHONEXE")
python_exe = PYTHON_EXE

# Ensure penv Python directory is in PATH for subprocess calls
python_dir = os.path.dirname(PYTHON_EXE)
current_path = os.environ.get("PATH", "")
if python_dir not in current_path:
    os.environ["PATH"] = python_dir + os.pathsep + current_path

# Verify the Python executable exists
assert os.path.isfile(PYTHON_EXE), f"Python executable not found: {PYTHON_EXE}"

if os.path.isfile(python_exe):
    # Update sys.path to include penv site-packages
    if IS_WINDOWS:
        penv_site_packages = os.path.join(penv_dir, "Lib", "site-packages")
    else:
        # Find the actual site-packages directory in the venv
        penv_lib_dir = os.path.join(penv_dir, "lib")
        if os.path.isdir(penv_lib_dir):
            for python_dir in os.listdir(penv_lib_dir):
                if python_dir.startswith("python"):
                    penv_site_packages = os.path.join(penv_lib_dir, python_dir, "site-packages")
                    break
            else:
                penv_site_packages = None
        else:
            penv_site_packages = None

    if penv_site_packages and os.path.isdir(penv_site_packages) and penv_site_packages not in sys.path:
        sys.path.insert(0, penv_site_packages)

def add_to_pythonpath(path):
    """
    Add a path to the PYTHONPATH environment variable (cross-platform).
    
    Args:
        path (str): The path to add to PYTHONPATH
    """
    # Normalize the path for the current OS
    normalized_path = os.path.normpath(path)
    
    # Add to PYTHONPATH environment variable
    if "PYTHONPATH" in os.environ:
        current_paths = os.environ["PYTHONPATH"].split(os.pathsep)
        normalized_current_paths = [os.path.normpath(p) for p in current_paths]
        if normalized_path not in normalized_current_paths:
            os.environ["PYTHONPATH"] = normalized_path + os.pathsep + os.environ.get("PYTHONPATH", "")
    else:
        os.environ["PYTHONPATH"] = normalized_path
    
    # Also add to sys.path for immediate availability
    if normalized_path not in sys.path:
        sys.path.insert(0, normalized_path)

def setup_python_paths():
    """
    Setup Python paths based on the actual Python executable being used.
    """    
    # Get the directory containing the Python executable
    python_dir = os.path.dirname(PYTHON_EXE)
    add_to_pythonpath(python_dir)
    
    # Try to find site-packages directory using the actual Python executable
    result = subprocess.run(
        [PYTHON_EXE, "-c", "import site; print(site.getsitepackages()[0])"],
        capture_output=True,
        text=True,
        timeout=5
    )
    if result.returncode == 0:
        site_packages = result.stdout.strip()
        if os.path.isdir(site_packages):
            add_to_pythonpath(site_packages)

# Setup Python paths based on the actual Python executable
setup_python_paths()

def _get_executable_path(python_exe, executable_name):
    """
    Get the path to an executable binary (esptool, uv, etc.) based on the Python executable path.
    
    Args:
        python_exe (str): Path to Python executable
        executable_name (str): Name of the executable to find (e.g., 'esptool', 'uv')
        
    Returns:
        str: Path to executable or fallback to executable name
    """
    
    python_dir = os.path.dirname(python_exe)
    
    if IS_WINDOWS:
        executable_path = os.path.join(python_dir, f"{executable_name}.exe")
    else:
        # For Unix-like systems, executables are typically in the same directory as python
        # or in a bin subdirectory
        executable_path = os.path.join(python_dir, executable_name)
        
        # If not found in python directory, try bin subdirectory
        if not os.path.isfile(executable_path):
            bin_dir = os.path.join(python_dir, "bin")
            executable_path = os.path.join(bin_dir, executable_name)
    
    if os.path.isfile(executable_path):
        return executable_path
    
    return executable_name  # Fallback to command name


def _get_esptool_executable_path(python_exe):
    """
    Get the path to the esptool executable binary.
    
    Args:
        python_exe (str): Path to Python executable
        
    Returns:
        str: Path to esptool executable
    """
    return _get_executable_path(python_exe, "esptool")


def _get_uv_executable_path(python_exe):
    """
    Get the path to the uv executable binary.
    
    Args:
        python_exe (str): Path to Python executable
        
    Returns:
        str: Path to uv executable
    """
    return _get_executable_path(python_exe, "uv")


def get_packages_to_install(deps, installed_packages):
    """
    Generator for Python packages that need to be installed.
    
    Args:
        deps (dict): Dictionary of package names and version specifications
        installed_packages (dict): Dictionary of currently installed packages
        
    Yields:
        str: Package name that needs to be installed
    """
    for package, spec in deps.items():
        if package not in installed_packages:
            yield package
        else:
            version_spec = semantic_version.Spec(spec)
            if not version_spec.match(installed_packages[package]):
                yield package


def install_python_deps():
    """
    Ensure uv package manager is available and install required Python dependencies.
    
    Returns:
        bool: True if successful, False otherwise
    """
    # Get uv executable path
    uv_executable = _get_uv_executable_path(PYTHON_EXE)
    
    try:
        result = subprocess.run(
            [uv_executable, "--version"],
            capture_output=True,
            text=True,
            timeout=3
        )
        uv_available = result.returncode == 0
    except (FileNotFoundError, subprocess.TimeoutExpired):
        uv_available = False
    
    if not uv_available:
        try:
            result = subprocess.run(
                [PYTHON_EXE, "-m", "pip", "install", "uv>=0.1.0", "-q", "-q", "-q"],
                capture_output=True,
                text=True,
                timeout=30,  # 30 second timeout
                env=os.environ  # Use modified environment with custom PYTHONPATH
            )
            if result.returncode != 0:
                if result.stderr:
                    print(f"Error output: {result.stderr.strip()}")
                return False
            
            # Update uv executable path after installation
            uv_executable = _get_uv_executable_path(PYTHON_EXE)
            
            # Add Scripts directory to PATH for Windows
            if IS_WINDOWS:
                python_dir = os.path.dirname(PYTHON_EXE)
                scripts_dir = os.path.join(python_dir, "Scripts")
                if os.path.isdir(scripts_dir):
                    os.environ["PATH"] = scripts_dir + os.pathsep + os.environ.get("PATH", "")
                    
        except subprocess.TimeoutExpired:
            print("Error: uv installation timed out")
            return False
        except FileNotFoundError:
            print("Error: Python executable not found")
            return False
        except Exception as e:
            print(f"Error installing uv package manager: {e}")
            return False

    
    def _get_installed_uv_packages():
        """
        Get list of installed packages using uv.
        
        Returns:
            dict: Dictionary of installed packages with versions
        """
        result = {}
        try:
            cmd = [uv_executable, "pip", "list", "--format=json"]
            result_obj = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                encoding='utf-8',
                timeout=30,  # 30 second timeout
                env=os.environ  # Use modified environment with custom PYTHONPATH
            )
            
            if result_obj.returncode == 0:
                content = result_obj.stdout.strip()
                if content:
                    packages = json.loads(content)
                    for p in packages:
                        result[p["name"]] = pepver_to_semver(p["version"])
            else:
                print(f"Warning: pip list failed with exit code {result_obj.returncode}")
                if result_obj.stderr:
                    print(f"Error output: {result_obj.stderr.strip()}")
                
        except subprocess.TimeoutExpired:
            print("Warning: uv pip list command timed out")
        except (json.JSONDecodeError, KeyError) as e:
            print(f"Warning: Could not parse package list: {e}")
        except FileNotFoundError:
            print("Warning: uv command not found")
        except Exception as e:
            print(f"Warning! Couldn't extract the list of installed Python packages: {e}")

        return result

    installed_packages = _get_installed_uv_packages()
    packages_to_install = list(get_packages_to_install(python_deps, installed_packages))
    
    if packages_to_install:
        packages_list = [f"{p}{python_deps[p]}" for p in packages_to_install]
        
        cmd = [
            uv_executable, "pip", "install",
            f"--python={PYTHON_EXE}",
            "--quiet", "--upgrade"
        ] + packages_list
        
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=30,  # 30 second timeout for package installation
                env=os.environ  # Use modified environment with custom PYTHONPATH
            )
            
            if result.returncode != 0:
                print(f"Error: Failed to install Python dependencies (exit code: {result.returncode})")
                if result.stderr:
                    print(f"Error output: {result.stderr.strip()}")
                return False
                
        except subprocess.TimeoutExpired:
            print("Error: Python dependencies installation timed out")
            return False
        except FileNotFoundError:
            print("Error: uv command not found")
            return False
        except Exception as e:
            print(f"Error installing Python dependencies: {e}")
            return False
    
    return True


def install_esptool():
    """
    Install esptool from package folder "tool-esptoolpy" using uv package manager.
    Also determines the path to the esptool executable binary.
    
    Returns:
        str: Path to esptool executable, or 'esptool' as fallback
    """
    try:
        subprocess.check_call(
            [PYTHON_EXE, "-c", "import esptool"], 
            stdout=subprocess.DEVNULL, 
            stderr=subprocess.DEVNULL,
            env=os.environ
        )
        esptool_binary_path = _get_esptool_executable_path(PYTHON_EXE)
        return esptool_binary_path
    except (subprocess.CalledProcessError, FileNotFoundError):
        pass

    esptool_repo_path = env.subst(platform.get_package_dir("tool-esptoolpy") or "")
    if esptool_repo_path and os.path.isdir(esptool_repo_path):
        uv_executable = _get_uv_executable_path(PYTHON_EXE)
        try:
            subprocess.check_call([
                uv_executable, "pip", "install", "--quiet",
                f"--python={PYTHON_EXE}",
                "-e", esptool_repo_path
            ], env=os.environ)

            esptool_binary_path = _get_esptool_executable_path(PYTHON_EXE)
            return esptool_binary_path
            
        except subprocess.CalledProcessError as e:
            print(f"Warning: Failed to install esptool: {e}")
            return 'esptool'  # Fallback
    
    return 'esptool'  # Fallback


# Install Python dependencies
install_python_deps()

# Install esptool after dependencies
esptool_binary_path = install_esptool()


def BeforeUpload(target, source, env):
    """
    Prepare the environment before uploading firmware.
    Handles port detection and special upload configurations.
    
    Args:
        target: SCons target
        source: SCons source
        env: SCons environment object
    """
    upload_options = {}
    if "BOARD" in env:
        upload_options = env.BoardConfig().get("upload", {})

    if not env.subst("$UPLOAD_PORT"):
        env.AutodetectUploadPort()

    before_ports = get_serial_ports()
    if upload_options.get("use_1200bps_touch", False):
        env.TouchSerialPort("$UPLOAD_PORT", 1200)

    if upload_options.get("wait_for_upload_port", False):
        env.Replace(UPLOAD_PORT=env.WaitForNewSerialPort(before_ports))


def _get_board_memory_type(env):
    """
    Determine the memory type configuration for the board.
    
    Args:
        env: SCons environment object
        
    Returns:
        str: The appropriate memory type string based on board configuration
    """
    board_config = env.BoardConfig()
    default_type = "%s_%s" % (
        board_config.get("build.flash_mode", "dio"),
        board_config.get("build.psram_type", "qspi"),
    )

    return board_config.get(
        "build.memory_type",
        board_config.get(
            "build.%s.memory_type"
            % env.subst("$PIOFRAMEWORK").strip().replace(" ", "_"),
            default_type,
        ),
    )


def _normalize_frequency(frequency):
    """
    Convert frequency value to normalized string format (e.g., "40m").
    
    Args:
        frequency: Frequency value to normalize
        
    Returns:
        str: Normalized frequency string with 'm' suffix
    """
    frequency = str(frequency).replace("L", "")
    return str(int(int(frequency) / 1000000)) + "m"


def _get_board_f_flash(env):
    """
    Get the flash frequency for the board.
    
    Args:
        env: SCons environment object
        
    Returns:
        str: Flash frequency string
    """
    frequency = env.subst("$BOARD_F_FLASH")
    return _normalize_frequency(frequency)


def _get_board_f_image(env):
    """
    Get the image frequency for the board, fallback to flash frequency.
    
    Args:
        env: SCons environment object
        
    Returns:
        str: Image frequency string
    """
    board_config = env.BoardConfig()
    if "build.f_image" in board_config:
        return _normalize_frequency(board_config.get("build.f_image"))

    return _get_board_f_flash(env)


def _get_board_f_boot(env):
    """
    Get the boot frequency for the board, fallback to flash frequency.
    
    Args:
        env: SCons environment object
        
    Returns:
        str: Boot frequency string
    """
    board_config = env.BoardConfig()
    if "build.f_boot" in board_config:
        return _normalize_frequency(board_config.get("build.f_boot"))

    return _get_board_f_flash(env)


def _get_board_flash_mode(env):
    """
    Determine the appropriate flash mode for the board.
    Handles special cases for OPI memory types.
    
    Args:
        env: SCons environment object
        
    Returns:
        str: Flash mode string
    """
    if _get_board_memory_type(env) in ("opi_opi", "opi_qspi"):
        return "dout"

    mode = env.subst("$BOARD_FLASH_MODE")
    if mode in ("qio", "qout"):
        return "dio"
    return mode


def _get_board_boot_mode(env):
    """
    Determine the boot mode for the board.
    Handles special cases for OPI memory types.
    
    Args:
        env: SCons environment object
        
    Returns:
        str: Boot mode string
    """
    memory_type = env.BoardConfig().get("build.arduino.memory_type", "")
    build_boot = env.BoardConfig().get("build.boot", "$BOARD_FLASH_MODE")
    if memory_type in ("opi_opi", "opi_qspi"):
        build_boot = "opi"
    return build_boot


def _parse_size(value):
    """
    Parse size values from various formats (int, hex, K/M suffixes).
    
    Args:
        value: Size value to parse
        
    Returns:
        int: Size in bytes as an integer
    """
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
    """
    Parse the partition table CSV file and return partition information.
    Also sets the application offset for the environment.
    
    Args:
        env: SCons environment object
        
    Returns:
        list: List of partition dictionaries
    """
    partitions_csv = env.subst("$PARTITIONS_TABLE_CSV")
    if not isfile(partitions_csv):
        sys.stderr.write(
            "Could not find the file %s with partitions table.\n"
            % partitions_csv
        )
        env.Exit(1)
        return

    result = []
    next_offset = 0
    app_offset = 0x10000  # Default address for firmware

    with open(partitions_csv) as fp:
        for line in fp.readlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            tokens = [t.strip() for t in line.split(",")]
            if len(tokens) < 5:
                continue
            bound = 0x10000 if tokens[1] in ("0", "app") else 4
            calculated_offset = (next_offset + bound - 1) & ~(bound - 1)
            partition = {
                "name": tokens[0],
                "type": tokens[1],
                "subtype": tokens[2],
                "offset": tokens[3] or calculated_offset,
                "size": tokens[4],
                "flags": tokens[5] if len(tokens) > 5 else None,
            }
            result.append(partition)
            next_offset = _parse_size(partition["offset"])
            if partition["subtype"] == "ota_0":
                app_offset = next_offset
            next_offset = next_offset + _parse_size(partition["size"])

    # Configure application partition offset
    env.Replace(ESP32_APP_OFFSET=str(hex(app_offset)))
    # Propagate application offset to debug configurations
    env["INTEGRATION_EXTRA_DATA"].update(
        {"application_offset": str(hex(app_offset))}
    )
    return result


def _update_max_upload_size(env):
    """
    Update the maximum upload size based on partition table configuration.
    Prioritizes user-specified partition names.
    
    Args:
        env: SCons environment object
    """
    if not env.get("PARTITIONS_TABLE_CSV"):
        return

    sizes = {
        p["subtype"]: _parse_size(p["size"])
        for p in _parse_partitions(env)
        if p["type"] in ("0", "app")
    }

    partitions = {p["name"]: p for p in _parse_partitions(env)}

    # User-specified partition name has the highest priority
    custom_app_partition_name = board.get("build.app_partition_name", "")
    if custom_app_partition_name:
        selected_partition = partitions.get(custom_app_partition_name, {})
        if selected_partition:
            board.update(
                "upload.maximum_size", _parse_size(selected_partition["size"])
            )
            return
        else:
            print(
                "Warning! Selected partition `%s` is not available in the "
                "partition table! Default partition will be used!"
                % custom_app_partition_name
            )

    for p in partitions.values():
        if p["type"] in ("0", "app") and p["subtype"] in ("ota_0"):
            board.update("upload.maximum_size", _parse_size(p["size"]))
            break


def _to_unix_slashes(path):
    """
    Convert Windows-style backslashes to Unix-style forward slashes.
    
    Args:
        path (str): Path to convert
        
    Returns:
        str: Path with Unix-style slashes
    """
    return path.replace("\\", "/")


def fetch_fs_size(env):
    """
    Extract filesystem size and offset information from partition table.
    Sets FS_START, FS_SIZE, FS_PAGE, and FS_BLOCK environment variables.
    
    Args:
        env: SCons environment object
    """
    fs = None
    for p in _parse_partitions(env):
        if p["type"] == "data" and p["subtype"] in (
            "spiffs",
            "fat",
            "littlefs",
        ):
            fs = p
    if not fs:
        sys.stderr.write(
            "Could not find the any filesystem section in the partitions "
            "table %s\n" % env.subst("$PARTITIONS_TABLE_CSV")
        )
        env.Exit(1)
        return
    
    env["FS_START"] = _parse_size(fs["offset"])
    env["FS_SIZE"] = _parse_size(fs["size"])
    env["FS_PAGE"] = int("0x100", 16)
    env["FS_BLOCK"] = int("0x1000", 16)

    # FFat specific offsets, see:
    # https://github.com/lorol/arduino-esp32fatfs-plugin#notes-for-fatfs
    if filesystem == "fatfs":
        env["FS_START"] += 4096
        env["FS_SIZE"] -= 4096


def __fetch_fs_size(target, source, env):
    """
    Wrapper function for fetch_fs_size to be used as SCons emitter.
    
    Args:
        target: SCons target
        source: SCons source
        env: SCons environment object
        
    Returns:
        tuple: (target, source) tuple
    """
    fetch_fs_size(env)
    return (target, source)


def check_lib_archive_exists():
    """
    Check if lib_archive is set in platformio.ini configuration.
    
    Returns:
        bool: True if found, False otherwise
    """
    for section in projectconfig.sections():
        if "lib_archive" in projectconfig.options(section):
            return True
    return False


def switch_off_ldf():
    """
    Disables LDF (Library Dependency Finder) for uploadfs, uploadfsota, and buildfs targets.

    This optimization prevents unnecessary library dependency scanning and compilation
    when only filesystem operations are performed.
    """
    fs_targets = {"uploadfs", "uploadfsota", "buildfs", "erase"}
    if fs_targets & set(COMMAND_LINE_TARGETS):
        # Disable LDF by modifying project configuration directly
        env_section = "env:" + env["PIOENV"]
        if not projectconfig.has_section(env_section):
            projectconfig.add_section(env_section)
        projectconfig.set(env_section, "lib_ldf_mode", "off")


# Initialize board configuration and MCU settings
board = env.BoardConfig()
mcu = board.get("build.mcu", "esp32")
toolchain_arch = "xtensa-%s" % mcu
filesystem = board.get("build.filesystem", "littlefs")

# Set toolchain architecture for RISC-V based ESP32 variants
if mcu in ("esp32c2", "esp32c3", "esp32c5", "esp32c6", "esp32h2", "esp32p4"):
    toolchain_arch = "riscv32-esp"

# Initialize integration extra data if not present
if "INTEGRATION_EXTRA_DATA" not in env:
    env["INTEGRATION_EXTRA_DATA"] = {}

# Take care of possible whitespaces in path
objcopy_value = (
    f'"{esptool_binary_path}"' 
    if ' ' in esptool_binary_path 
    else esptool_binary_path
)
# Configure build tools and environment variables
env.Replace(
    __get_board_boot_mode=_get_board_boot_mode,
    __get_board_f_flash=_get_board_f_flash,
    __get_board_f_image=_get_board_f_image,
    __get_board_f_boot=_get_board_f_boot,
    __get_board_flash_mode=_get_board_flash_mode,
    __get_board_memory_type=_get_board_memory_type,
    AR="%s-elf-gcc-ar" % toolchain_arch,
    AS="%s-elf-as" % toolchain_arch,
    CC="%s-elf-gcc" % toolchain_arch,
    CXX="%s-elf-g++" % toolchain_arch,
    GDB=join(
        platform.get_package_dir(
            "tool-riscv32-esp-elf-gdb"
            if mcu in (
                "esp32c2",
                "esp32c3",
                "esp32c5",
                "esp32c6",
                "esp32h2",
                "esp32p4",
            )
            else "tool-xtensa-esp-elf-gdb"
        )
        or "",
        "bin",
        "%s-elf-gdb" % toolchain_arch,
    ),
    OBJCOPY=objcopy_value,
    RANLIB="%s-elf-gcc-ranlib" % toolchain_arch,
    SIZETOOL="%s-elf-size" % toolchain_arch,
    ARFLAGS=["rc"],
    SIZEPROGREGEXP=r"^(?:\.iram0\.text|\.iram0\.vectors|\.dram0\.data|"
    r"\.flash\.text|\.flash\.rodata|)\s+([0-9]+).*",
    SIZEDATAREGEXP=r"^(?:\.dram0\.data|\.dram0\.bss|\.noinit)\s+([0-9]+).*",
    SIZECHECKCMD="$SIZETOOL -A -d $SOURCES",
    SIZEPRINTCMD="$SIZETOOL -B -d $SOURCES",
    ERASEFLAGS=["--chip", mcu, "--port", '"$UPLOAD_PORT"'],
    ERASECMD='"$OBJCOPY" $ERASEFLAGS erase-flash',
    MKFSTOOL="mk%s" % filesystem,

    ESP32_FS_IMAGE_NAME=env.get(
        "ESP32_FS_IMAGE_NAME",
        env.get("ESP32_SPIFFS_IMAGE_NAME", filesystem),
    ),
    ESP32_APP_OFFSET=env.get("INTEGRATION_EXTRA_DATA").get(
        "application_offset"
    ),
    ARDUINO_LIB_COMPILE_FLAG="Inactive",
    PROGSUFFIX=".elf",
)

# Check if lib_archive is set in platformio.ini and set it to False
# if not found. This makes weak defs in framework and libs possible.
if not check_lib_archive_exists():
    env_section = "env:" + env["PIOENV"]
    projectconfig.set(env_section, "lib_archive", "False")

# Allow user to override via pre:script
if env.get("PROGNAME", "program") == "program":
    env.Replace(PROGNAME="firmware")

# Configure build actions and builders
env.Append(
    BUILDERS=dict(
        ElfToBin=Builder(
            action=env.VerboseAction(
                " ".join(
                    [
                        "$OBJCOPY",
                        "--chip",
                        mcu,
                        "elf2image",
                        "--flash-mode",
                        "${__get_board_flash_mode(__env__)}",
                        "--flash-freq",
                        "${__get_board_f_image(__env__)}",
                        "--flash-size",
                        board.get("upload.flash_size", "4MB"),
                        "-o",
                        "$TARGET",
                        "$SOURCES",
                    ]
                ),
                "Building $TARGET",
            ),
            suffix=".bin",
        ),
        DataToBin=Builder(
            action=env.VerboseAction(
                " ".join(
                    ['"$MKFSTOOL"', "-c", "$SOURCES", "-s", "$FS_SIZE"]
                    + (
                        ["-p", "$FS_PAGE", "-b", "$FS_BLOCK"]
                        if filesystem in ("littlefs", "spiffs")
                        else []
                    )
                    + ["$TARGET"]
                ),
                "Building FS image from '$SOURCES' directory to $TARGET",
            ),
            emitter=__fetch_fs_size,
            source_factory=env.Dir,
            suffix=".bin",
        ),
    )
)

# Load framework-specific configuration
if not env.get("PIOFRAMEWORK"):
    env.SConscript("frameworks/_bare.py", exports="env")


# Disable LDF for filesystem operations
switch_off_ldf()


def firmware_metrics(target, source, env):
    """
    Custom target to run esp-idf-size with support for command line parameters.
    Usage: pio run -t metrics -- [esp-idf-size arguments]
    
    Args:
        target: SCons target
        source: SCons source
        env: SCons environment object
    """
    if terminal_cp != "utf-8":
        print("Firmware metrics can not be shown. Set the terminal codepage to \"utf-8\"")
        return

    map_file = os.path.join(env.subst("$BUILD_DIR"), env.subst("$PROGNAME") + ".map")
    if not os.path.isfile(map_file):
        # map file can be in project dir
        map_file = os.path.join(get_project_dir(), env.subst("$PROGNAME") + ".map")

    if not os.path.isfile(map_file):
        print(f"Error: Map file not found: {map_file}")
        print("Make sure the project is built first with 'pio run'")
        return

    try:        
        cmd = [PYTHON_EXE, "-m", "esp_idf_size", "--ng"]
        
        # Parameters from platformio.ini
        extra_args = env.GetProjectOption("custom_esp_idf_size_args", "")
        if extra_args:
            cmd.extend(shlex.split(extra_args))
        
        # Command Line Parameter, after --
        cli_args = []
        if "--" in sys.argv:
            dash_index = sys.argv.index("--")
            if dash_index + 1 < len(sys.argv):
                cli_args = sys.argv[dash_index + 1:]
                cmd.extend(cli_args)

        # Add CLI arguments before the map file
        if cli_args:
            cmd.extend(cli_args)

        # Map-file as last argument
        cmd.append(map_file)
        
        # Debug-Info if wanted
        if env.GetProjectOption("custom_esp_idf_size_verbose", False):
            print(f"Running command: {' '.join(cmd)}")
        
        # Call esp-idf-size with modified environment
        result = subprocess.run(cmd, check=False, capture_output=False, env=os.environ)
        
        if result.returncode != 0:
            print(f"Warning: esp-idf-size exited with code {result.returncode}")
            
    except ImportError:
        print("Error: esp-idf-size module not found.")
        print("Install with: pip install esp-idf-size")
    except FileNotFoundError:
        print("Error: Python executable not found.")
        print("Check your Python installation.")
    except Exception as e:
        print(f"Error: Failed to run firmware metrics: {e}")
        print("Make sure esp-idf-size is installed: pip install esp-idf-size")


#
# Target: Build executable and linkable firmware or FS image
#

target_elf = None
if "nobuild" in COMMAND_LINE_TARGETS:
    target_elf = join("$BUILD_DIR", "${PROGNAME}.elf")
    if set(["uploadfs", "uploadfsota"]) & set(COMMAND_LINE_TARGETS):
        fetch_fs_size(env)
        target_firm = join("$BUILD_DIR", "${ESP32_FS_IMAGE_NAME}.bin")
    else:
        target_firm = join("$BUILD_DIR", "${PROGNAME}.bin")
else:
    target_elf = env.BuildProgram()
    silent_action = env.Action(firmware_metrics)
    # Hack to silence scons command output
    silent_action.strfunction = lambda target, source, env: ""
    env.AddPostAction(target_elf, silent_action)
    if set(["buildfs", "uploadfs", "uploadfsota"]) & set(COMMAND_LINE_TARGETS):
        target_firm = env.DataToBin(
            join("$BUILD_DIR", "${ESP32_FS_IMAGE_NAME}"), "$PROJECT_DATA_DIR"
        )
        env.NoCache(target_firm)
        AlwaysBuild(target_firm)
    else:
        target_firm = env.ElfToBin(join("$BUILD_DIR", "${PROGNAME}"), target_elf)
        env.Depends(target_firm, "checkprogsize")

# Configure platform targets
env.AddPlatformTarget(
    "buildfs", target_firm, target_firm, "Build Filesystem Image"
)
AlwaysBuild(env.Alias("nobuild", target_firm))
target_buildprog = env.Alias("buildprog", target_firm, target_firm)

# Update max upload size based on CSV file
if env.get("PIOMAINPROG"):
    env.AddPreAction(
        "checkprogsize",
        env.VerboseAction(
            lambda source, target, env: _update_max_upload_size(env),
            "Retrieving maximum program size $SOURCES",
        ),
    )

# Target: Print binary size
target_size = env.AddPlatformTarget(
    "size",
    target_elf,
    env.VerboseAction("$SIZEPRINTCMD", "Calculating size $SOURCE"),
    "Program Size",
    "Calculate program size",
)

# Target: Upload firmware or FS image
upload_protocol = env.subst("$UPLOAD_PROTOCOL")
debug_tools = board.get("debug.tools", {})
upload_actions = []

# Compatibility with old OTA configurations
if upload_protocol != "espota" and re.match(
    r"\"?((([0-9]{1,3}\.){3}[0-9]{1,3})|[^\\/]+\.local)\"?$",
    env.get("UPLOAD_PORT", ""),
):
    upload_protocol = "espota"
    sys.stderr.write(
        "Warning! We have just detected `upload_port` as IP address or host "
        "name of ESP device. `upload_protocol` is switched to `espota`.\n"
        "Please specify `upload_protocol = espota` in `platformio.ini` "
        "project configuration file.\n"
    )

# Configure upload protocol: ESP OTA
if upload_protocol == "espota":
    if not env.subst("$UPLOAD_PORT"):
        sys.stderr.write(
            "Error: Please specify IP address or host name of ESP device "
            "using `upload_port` for build environment or use "
            "global `--upload-port` option.\n"
            "See https://docs.platformio.org/page/platforms/"
            "espressif32.html#over-the-air-ota-update\n"
        )
    env.Replace(
        UPLOADER=join(FRAMEWORK_DIR, "tools", "espota.py"),
        UPLOADERFLAGS=["--debug", "--progress", "-i", "$UPLOAD_PORT"],
        UPLOADCMD=f'"{PYTHON_EXE}" "$UPLOADER" $UPLOADERFLAGS -f $SOURCE',
    )
    if set(["uploadfs", "uploadfsota"]) & set(COMMAND_LINE_TARGETS):
        env.Append(UPLOADERFLAGS=["--spiffs"])
    upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")]

# Configure upload protocol: esptool
elif upload_protocol == "esptool":
    env.Replace(
        UPLOADER=objcopy_value,
        UPLOADERFLAGS=[
            "--chip",
            mcu,
            "--port",
            '"$UPLOAD_PORT"',
            "--baud",
            "$UPLOAD_SPEED",
            "--before",
            board.get("upload.before_reset", "default-reset"),
            "--after",
            board.get("upload.after_reset", "hard-reset"),
            "write-flash",
            "-z",
            "--flash-mode",
            "${__get_board_flash_mode(__env__)}",
            "--flash-freq",
            "${__get_board_f_image(__env__)}",
            "--flash-size",
            "detect",
        ],
        UPLOADCMD='$UPLOADER $UPLOADERFLAGS $ESP32_APP_OFFSET $SOURCE'
    )
    for image in env.get("FLASH_EXTRA_IMAGES", []):
        env.Append(UPLOADERFLAGS=[image[0], env.subst(image[1])])

    if "uploadfs" in COMMAND_LINE_TARGETS:
        env.Replace(
            UPLOADERFLAGS=[
                "--chip",
                mcu,
                "--port",
                '"$UPLOAD_PORT"',
                "--baud",
                "$UPLOAD_SPEED",
                "--before",
                board.get("upload.before_reset", "default-reset"),
                "--after",
                board.get("upload.after_reset", "hard-reset"),
                "write-flash",
                "-z",
                "--flash-mode",
                "${__get_board_flash_mode(__env__)}",
                "--flash-freq",
                "${__get_board_f_image(__env__)}",
                "--flash-size",
                "detect",
                "$FS_START",
            ],
            UPLOADCMD='"$UPLOADER" $UPLOADERFLAGS $SOURCE',
        )

    upload_actions = [
        env.VerboseAction(BeforeUpload, "Looking for upload port..."),
        env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE"),
    ]

# Configure upload protocol: Debug tools (OpenOCD)
elif upload_protocol in debug_tools:
    _parse_partitions(env)
    openocd_args = ["-d%d" % (2 if int(ARGUMENTS.get("PIOVERBOSE", 0)) else 1)]
    openocd_args.extend(
        debug_tools.get(upload_protocol).get("server").get("arguments", [])
    )
    openocd_args.extend(
        [
            "-c",
            "adapter speed %s" % env.GetProjectOption("debug_speed", "5000"),
            "-c",
            "program_esp {{$SOURCE}} %s verify"
            % (
                "$FS_START"
                if "uploadfs" in COMMAND_LINE_TARGETS
                else env.get("INTEGRATION_EXTRA_DATA").get("application_offset")
            ),
        ]
    )
    if "uploadfs" not in COMMAND_LINE_TARGETS:
        for image in env.get("FLASH_EXTRA_IMAGES", []):
            openocd_args.extend(
                [
                    "-c",
                    "program_esp {{%s}} %s verify"
                    % (_to_unix_slashes(image[1]), image[0]),
                ]
            )
    openocd_args.extend(["-c", "reset run; shutdown"])
    openocd_args = [
        f.replace(
            "$PACKAGE_DIR",
            _to_unix_slashes(
                platform.get_package_dir("tool-openocd-esp32") or ""
            ),
        )
        for f in openocd_args
    ]
    env.Replace(
        UPLOADER="openocd",
        UPLOADERFLAGS=openocd_args,
        UPLOADCMD="$UPLOADER $UPLOADERFLAGS",
    )
    upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")]

# Configure upload protocol: Custom
elif upload_protocol == "custom":
    upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")]

else:
    sys.stderr.write("Warning! Unknown upload protocol %s\n" % upload_protocol)

# Register upload targets
env.AddPlatformTarget("upload", target_firm, upload_actions, "Upload")
env.AddPlatformTarget(
    "uploadfs", target_firm, upload_actions, "Upload Filesystem Image"
)
env.AddPlatformTarget(
    "uploadfsota",
    target_firm,
    upload_actions,
    "Upload Filesystem Image OTA",
)

# Target: Erase Flash and Upload
env.AddPlatformTarget(
    "erase_upload",
    target_firm,
    [
        env.VerboseAction(BeforeUpload, "Looking for upload port..."),
        env.VerboseAction("$ERASECMD", "Erasing..."),
        env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE"),
    ],
    "Erase Flash and Upload",
)

# Target: Erase Flash
env.AddPlatformTarget(
    "erase",
    None,
    [
        env.VerboseAction(BeforeUpload, "Looking for upload port..."),
        env.VerboseAction("$ERASECMD", "Erasing..."),
    ],
    "Erase Flash",
)

# Register Custom Target for firmware metrics
env.AddCustomTarget(
    name="metrics",
    dependencies="$BUILD_DIR/${PROGNAME}.elf",
    actions=firmware_metrics,
    title="Firmware Size Metrics",
    description="Analyze firmware size using esp-idf-size "
    "(supports CLI args after --)",
    always_build=True,
)

# Additional Target without Build-Dependency when already compiled
env.AddCustomTarget(
    name="metrics-only",
    dependencies=None,
    actions=firmware_metrics,
    title="Firmware Size Metrics (No Build)",
    description="Analyze firmware size without building first",
    always_build=True,
)

# Override memory inspection behavior
env.SConscript("sizedata.py", exports="env")

# Set default targets
Default([target_buildprog, target_size])
