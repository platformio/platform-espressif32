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

"""
Arduino

Arduino Wiring-based Framework allows writing cross-platform software to
control devices attached to a wide range of Arduino boards to create all
kinds of creative coding, interactive objects, spaces or physical experiences.

http://arduino.cc/en/Reference/HomePage
"""

import subprocess
import json
import os
import sys
import shutil
import hashlib
import logging
import threading
from contextlib import suppress
from os.path import join, exists, isabs, splitdrive, commonpath, relpath
from pathlib import Path
from typing import Union, List

import semantic_version
from SCons.Script import DefaultEnvironment, SConscript
from platformio import fs
from platformio.package.version import pepver_to_semver
from platformio.package.manager.tool import ToolPackageManager

IS_WINDOWS = sys.platform.startswith("win")

python_deps = {
    "wheel": ">=0.35.1",
    "rich-click": ">=1.8.6",
    "zopfli": ">=0.2.2",
    "intelhex": ">=2.3.0",
    "rich": ">=14.0.0",
    "esp-idf-size": ">=1.6.1"
}


def setup_logging():
    """Setup logging with optional file output"""
    handlers = [logging.StreamHandler()]

    # Only add file handler if writable and not disabled
    log_file = os.environ.get('ARDUINO_FRAMEWORK_LOG_FILE')
    if log_file:
        with suppress(OSError, PermissionError):
            handlers.append(logging.FileHandler(log_file))

    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(levelname)s - %(message)s',
        handlers=handlers
    )


# Only setup logging if enabled via environment variable
if os.environ.get('ARDUINO_FRAMEWORK_ENABLE_LOGGING'):
    setup_logging()

# Constants for better performance
UNICORE_FLAGS = {
    "CORE32SOLO1",
    "CONFIG_FREERTOS_UNICORE=y"
}

# Thread-safe global flags to prevent message spam
_PATH_SHORTENING_LOCK = threading.Lock()
_PATH_SHORTENING_MESSAGES = {
    'shortening_applied': False,
    'no_framework_paths_warning': False,
    'long_path_warning_shown': False
}


def get_platform_default_threshold(mcu):
    """
    Platform-specific bleeding edge default values for 
    INCLUDE_PATH_LENGTH_THRESHOLD
    These values push the limits for maximum performance and minimal path 
    shortening

    Args:
        mcu: MCU type (esp32, esp32s2, esp32s3, etc.)

    Returns:
        int: Platform-specific bleeding edge default threshold
    """
    # Bleeding edge values - pushing Windows command line limits
    # Windows CMD has ~32768 character limit, we use aggressive values close
    # to this
    platform_defaults = {
        "esp32": 32000,      # Standard ESP32
        "esp32s2": 32000,    # ESP32-S2
        "esp32s3": 33200,    # ESP32-S3
        "esp32c3": 32000,    # ESP32-C3
        "esp32c2": 32000,    # ESP32-C2
        "esp32c6": 31600,    # ESP32-C6
        "esp32h2": 32000,    # ESP32-H2
        "esp32p4": 32000,    # ESP32-P4
    }

    default_value = platform_defaults.get(mcu, 31600)

    # Debug output only in verbose mode
    if logging.getLogger().isEnabledFor(logging.DEBUG):
        logging.debug(
            f"Bleeding edge platform default threshold for {mcu}: "
            f"{default_value}")

    return default_value


def validate_threshold(threshold, mcu):
    """
    Validates threshold value with bleeding edge limits
    Uses aggressive boundaries for maximum performance

    Args:
        threshold: Threshold value to validate
        mcu: MCU type for context-specific validation

    Returns:
        int: Validated threshold value
    """
    # Bleeding edge absolute limits - pushing boundaries
    min_threshold = 25000   # Minimum reasonable value for complex projects
    # Maximum aggressive value (beyond Windows CMD limit for testing)
    max_threshold = 55000

    # MCU-specific bleeding edge adjustments - all values are aggressive
    mcu_adjustments = {
        "esp32c2": {"min": 30000, "max": 40000},
        "esp32c3": {"min": 30000, "max": 45000},
        "esp32": {"min": 30000, "max": 50000},
        "esp32s2": {"min": 30000, "max": 50000},
        "esp32s3": {"min": 30000, "max": 50000},
        "esp32p4": {"min": 30000, "max": 55000},
        "esp32c6": {"min": 30000, "max": 50000},
        "esp32h2": {"min": 30000, "max": 40000},
    }

    # Apply MCU-specific bleeding edge limits
    if mcu in mcu_adjustments:
        min_threshold = max(min_threshold, mcu_adjustments[mcu]["min"])
        max_threshold = min(max_threshold, mcu_adjustments[mcu]["max"])

    original_threshold = threshold

    if threshold < min_threshold:
        print(f"*** Warning: Include path threshold {threshold} too "
              f"conservative for {mcu}, using bleeding edge minimum "
              f"{min_threshold} ***")
        threshold = min_threshold
    elif threshold > max_threshold:
        print(f"*** Warning: Include path threshold {threshold} exceeds "
              f"bleeding edge maximum for {mcu}, using {max_threshold} ***")
        threshold = max_threshold

    # Warning for conservative values (opposite of original - warn if too low)
    platform_default = get_platform_default_threshold(mcu)
    if threshold < platform_default * 0.7:  # More than 30% below bleeding edge default
        print(f"*** Info: Include path threshold {threshold} is conservative "
              f"compared to bleeding edge default {platform_default} for "
              f"{mcu} ***")
        print("*** Consider using higher values for maximum performance ***")

    if original_threshold != threshold:
        logging.warning(f"Threshold adjusted from {original_threshold} to "
                        f"bleeding edge value {threshold} for {mcu}")

    return threshold


def get_include_path_threshold(env, config, current_env_section):
    """
    Determines Windows INCLUDE_PATH_LENGTH_THRESHOLD from various sources
    with priority order and bleeding edge validation

    Priority order:
    1. Environment variable PLATFORMIO_INCLUDE_PATH_THRESHOLD
    2. Environment-specific setting in platformio.ini
    3. Global setting in [env] section
    4. Setting in [platformio] section
    5. MCU-specific bleeding edge default value

    Args:
        env: PlatformIO Environment
        config: Project Configuration
        current_env_section: Current environment section

    Returns:
        int: Validated bleeding edge threshold value
    """
    mcu = env.BoardConfig().get("build.mcu", "esp32")
    default_threshold = get_platform_default_threshold(mcu)
    setting_name = "custom_include_path_length_threshold"

    try:
        # 1. Check environment variable (highest priority)
        env_var = os.environ.get("PLATFORMIO_INCLUDE_PATH_THRESHOLD")
        if env_var:
            try:
                threshold = int(env_var)
                threshold = validate_threshold(threshold, mcu)
                print(f"*** Using environment variable bleeding edge include "
                      f"path threshold: {threshold} (MCU: {mcu}) ***")
                return threshold
            except ValueError:
                print(f"*** Warning: Invalid environment variable "
                      f"PLATFORMIO_INCLUDE_PATH_THRESHOLD='{env_var}', "
                      f"ignoring ***")

        # 2. Check environment-specific setting
        if config.has_option(current_env_section, setting_name):
            threshold = config.getint(current_env_section, setting_name)
            threshold = validate_threshold(threshold, mcu)
            print(f"*** Using environment-specific bleeding edge include "
                  f"path threshold: {threshold} (MCU: {mcu}) ***")
            return threshold

        # 3. Check global setting in [env] section
        if config.has_option("env", setting_name):
            threshold = config.getint("env", setting_name)
            threshold = validate_threshold(threshold, mcu)
            print(f"*** Using global [env] bleeding edge include path "
                  f"threshold: {threshold} (MCU: {mcu}) ***")
            return threshold

        # 4. Check setting in [platformio] section
        if config.has_option("platformio", setting_name):
            threshold = config.getint("platformio", setting_name)
            threshold = validate_threshold(threshold, mcu)
            print(f"*** Using [platformio] section bleeding edge include "
                  f"path threshold: {threshold} (MCU: {mcu}) ***")
            return threshold

        # 5. Use MCU-specific bleeding edge default value
        threshold = validate_threshold(default_threshold, mcu)
        if env.get("VERBOSE"):
            print(f"*** Using platform-specific bleeding edge default "
                  f"include path threshold: {threshold} (MCU: {mcu}) ***")

        return threshold

    except (ValueError, TypeError) as e:
        print(f"*** Warning: Invalid include path threshold value, using "
              f"bleeding edge platform default {default_threshold} for "
              f"{mcu}: {e} ***")
        return validate_threshold(default_threshold, mcu)


def get_threshold_info(env, config, current_env_section):
    """
    Helper function for debug information about bleeding edge threshold 
    configuration

    Args:
        env: PlatformIO Environment
        config: Project Configuration
        current_env_section: Current environment section

    Returns:
        dict: Information about threshold configuration
    """
    mcu = env.BoardConfig().get("build.mcu", "esp32")
    setting_name = "custom_include_path_length_threshold"

    info = {
        "mcu": mcu,
        "platform_default": get_platform_default_threshold(mcu),
        "env_variable": os.environ.get("PLATFORMIO_INCLUDE_PATH_THRESHOLD"),
        "env_specific": None,
        "global_env": None,
        "platformio_section": None,
        "final_threshold": None,
        "source": "bleeding_edge_platform_default",
        "is_bleeding_edge": True
    }

    # Collect all possible sources
    if config.has_option(current_env_section, setting_name):
        with suppress(ValueError):
            info["env_specific"] = config.getint(current_env_section,
                                                 setting_name)

    if config.has_option("env", setting_name):
        with suppress(ValueError):
            info["global_env"] = config.getint("env", setting_name)

    if config.has_option("platformio", setting_name):
        with suppress(ValueError):
            info["platformio_section"] = config.getint("platformio",
                                                       setting_name)

    # Determine final threshold and source
    info["final_threshold"] = get_include_path_threshold(env, config,
                                                         current_env_section)

    # Determine source
    if info["env_variable"]:
        info["source"] = "environment_variable"
    elif info["env_specific"] is not None:
        info["source"] = "env_specific"
    elif info["global_env"] is not None:
        info["source"] = "global_env"
    elif info["platformio_section"] is not None:
        info["source"] = "platformio_section"

    return info


# Cache class for frequently used paths
class PathCache:
    def __init__(self, platform, mcu):
        self.platform = platform
        self.mcu = mcu
        self._framework_dir = None
        self._sdk_dir = None

    @property
    def framework_dir(self):
        if self._framework_dir is None:
            self._framework_dir = self.platform.get_package_dir(
                "framework-arduinoespressif32")
        return self._framework_dir

    @property
    def sdk_dir(self):
        if self._sdk_dir is None:
            self._sdk_dir = fs.to_unix_path(
                join(self.framework_dir,
                     "tools",
                     "esp32-arduino-libs",
                     self.mcu,
                     "include"
                )
            )
        return self._sdk_dir


def check_and_warn_long_path_support():
    """Checks Windows long path support and issues warning if disabled"""
    with _PATH_SHORTENING_LOCK:  # Thread-safe access
        if not IS_WINDOWS or _PATH_SHORTENING_MESSAGES[
                'long_path_warning_shown']:
            return

        try:
            import winreg
            key = winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE,
                r"SYSTEM\CurrentControlSet\Control\FileSystem"
            )
            value, _ = winreg.QueryValueEx(key, "LongPathsEnabled")
            winreg.CloseKey(key)

            if value != 1:
                print("*** WARNING: Windows Long Path Support is disabled ***")
                print("*** Enable it for better performance: ***")
                print("*** 1. Run as Administrator: gpedit.msc ***")
                print("*** 2. Navigate to: Computer Configuration > "
                      "Administrative Templates > System > Filesystem ***")
                print("*** 3. Enable 'Enable Win32 long paths' ***")
                print("*** OR run PowerShell as Admin: ***")
                print("*** New-ItemProperty -Path "
                      "'HKLM:\\SYSTEM\\CurrentControlSet\\Control\\FileSystem' "
                      "-Name 'LongPathsEnabled' -Value 1 -PropertyType DWORD "
                      "-Force ***")
                print("*** Restart required after enabling ***")
        except Exception:
            print("*** WARNING: Could not check Long Path Support status ***")
            print("*** Consider enabling Windows Long Path Support for "
                  "better performance ***")

        _PATH_SHORTENING_MESSAGES['long_path_warning_shown'] = True


# Secure deletion functions
def safe_delete_file(file_path: Union[str, Path],
                     force: bool = False) -> bool:
    """
    Secure file deletion

    Args:
        file_path: Path to file to be deleted
        force: Forces deletion even for write-protected files

    Returns:
        bool: True if successfully deleted
    """
    file_path = Path(file_path)

    try:
        # Check existence
        if not file_path.exists():
            logging.warning(f"File does not exist: {file_path}")
            return False

        # Remove write protection if necessary
        if force and not os.access(file_path, os.W_OK):
            file_path.chmod(0o666)

        # Delete file
        file_path.unlink()
        logging.info(f"File deleted: {file_path}")
        return True

    except PermissionError:
        logging.error(f"No permission to delete: {file_path}")
        return False
    except Exception as e:
        logging.error(f"Error deleting {file_path}: {e}")
        return False


def safe_delete_directory(dir_path: Union[str, Path]) -> bool:
    """
    Secure directory deletion
    """
    dir_path = Path(dir_path)

    try:
        if not dir_path.exists():
            logging.warning(f"Directory does not exist: {dir_path}")
            return False

        shutil.rmtree(dir_path)
        logging.info(f"Directory deleted: {dir_path}")
        return True

    except Exception as e:
        logging.error(f"Error deleting {dir_path}: {e}")
        return False


def validate_platformio_path(path: Union[str, Path]) -> bool:
    """
    Enhanced validation for PlatformIO package paths
    """
    try:
        path = Path(path).resolve()
        path_str = str(path)

        # Must be within .platformio directory structure
        if ".platformio" not in path_str:
            return False

        # Must be a packages directory
        if "packages" not in path_str:
            return False

        # Must be framework-related
        framework_indicators = [
            "framework-arduinoespressif32"
        ]

        if not any(indicator in path_str for indicator in framework_indicators):
            return False

        # Must not be a critical system path
        critical_paths = ["/usr", "/bin", "/sbin", "/etc", "/boot",
                          "C:\\Windows", "C:\\Program Files"]
        return not any(critical in path_str for critical in critical_paths)

    except Exception as e:
        logging.error(f"Path validation error: {e}")
        return False


def validate_deletion_path(path: Union[str, Path],
                           allowed_patterns: List[str]) -> bool:
    """
    Validates if a path can be safely deleted

    Args:
        path: Path to be checked
        allowed_patterns: Allowed path patterns

    Returns:
        bool: True if deletion is safe
    """
    path = Path(path).resolve()

    # Check against critical system paths
    critical_paths = [
        Path.home(),
        Path("/"),
        Path("C:\\") if IS_WINDOWS else None,
        Path("/usr"),
        Path("/etc"),
        Path("/bin"),
        Path("/sbin")
    ]

    for critical in filter(None, critical_paths):
        try:
            normalized_path = path.resolve()
            normalized_critical = critical.resolve()
            if (normalized_path == normalized_critical or
                    normalized_critical in normalized_path.parents):
                logging.error(f"Critical system path detected: {path}")
                return False
        except (OSError, ValueError):
            # Path comparison failed, reject for safety
            logging.error(f"Path comparison failed for: {path}")
            return False

    # Check against allowed patterns
    path_str = str(path)
    is_allowed = any(pattern in path_str for pattern in allowed_patterns)

    if not is_allowed:
        logging.error(f"Path does not match allowed patterns: {path}")
        logging.error(f"Allowed patterns: {allowed_patterns}")
    else:
        logging.info(f"Path validation successful: {path}")

    return is_allowed


def safe_framework_cleanup():
    """Secure cleanup of Arduino Framework with enhanced error handling"""
    success = True

    # Framework directory cleanup
    if exists(FRAMEWORK_DIR):
        logging.info(f"Attempting to validate framework path: "
                     f"{FRAMEWORK_DIR}")

        if validate_platformio_path(FRAMEWORK_DIR):
            logging.info(f"Framework path validated successfully: "
                         f"{FRAMEWORK_DIR}")

            if safe_delete_directory(FRAMEWORK_DIR):
                print("Framework successfully removed")
            else:
                print("Error removing framework")
                success = False
        else:
            logging.error(f"PlatformIO path validation failed: "
                          f"{FRAMEWORK_DIR}")
            success = False

    return success


def safe_remove_sdkconfig_files():
    """Secure removal of SDKConfig files"""
    envs = [section.replace("env:", "") for section in config.sections()
            if section.startswith("env:")]
    for env_name in envs:
        file_path = join(project_dir, f"sdkconfig.{env_name}")
        if exists(file_path):
            safe_delete_file(file_path)


# Initialization
env = DefaultEnvironment()
pm = ToolPackageManager()
platform = env.PioPlatform()
config = env.GetProjectConfig()
board = env.BoardConfig()

# Cached values
mcu = board.get("build.mcu", "esp32")
pioenv = env["PIOENV"]
project_dir = env.subst("$PROJECT_DIR")
path_cache = PathCache(platform, mcu)
current_env_section = f"env:{pioenv}"

# Board configuration
board_sdkconfig = board.get("espidf.custom_sdkconfig", "")
entry_custom_sdkconfig = "\n"
flag_custom_sdkconfig = False
flag_custom_component_remove = False
flag_custom_component_add = False
flag_lib_ignore = False

# pio lib_ignore check
if config.has_option(current_env_section, "lib_ignore"):
    flag_lib_ignore = True

# Custom Component remove check
if config.has_option(current_env_section, "custom_component_remove"):
    flag_custom_component_remove = True

# Custom SDKConfig check
if config.has_option(current_env_section, "custom_sdkconfig"):
    entry_custom_sdkconfig = env.GetProjectOption("custom_sdkconfig")
    flag_custom_sdkconfig = True

if board_sdkconfig:
    flag_custom_sdkconfig = True

extra_flags_raw = board.get("build.extra_flags", [])
if isinstance(extra_flags_raw, list):
    extra_flags = " ".join(extra_flags_raw).replace("-D", " ")
else:
    extra_flags = str(extra_flags_raw).replace("-D", " ")

framework_reinstall = False

FRAMEWORK_DIR = path_cache.framework_dir

SConscript("_embed_files.py", exports="env")

flag_any_custom_sdkconfig = exists(join(
    FRAMEWORK_DIR, "tools", "esp32-arduino-libs",
    "sdkconfig"))


def has_unicore_flags():
    """Check if any UNICORE flags are present in configuration"""
    return any(flag in extra_flags or flag in entry_custom_sdkconfig
               or flag in board_sdkconfig for flag in UNICORE_FLAGS)


# Esp32-solo1 libs settings
if flag_custom_sdkconfig and has_unicore_flags():
    if not env.get('BUILD_UNFLAGS'):  # Initialize if not set
        env['BUILD_UNFLAGS'] = []

    build_unflags = (" ".join(env['BUILD_UNFLAGS']) +
                     " -mdisable-hardware-atomics -ustart_app_other_cores")
    new_build_unflags = build_unflags.split()
    env.Replace(BUILD_UNFLAGS=new_build_unflags)


def get_packages_to_install(deps, installed_packages):
    """Generator for packages to install"""
    for package, spec in deps.items():
        if package not in installed_packages:
            yield package
        else:
            version_spec = semantic_version.Spec(spec)
            if not version_spec.match(installed_packages[package]):
                yield package


def install_python_deps():
    def _get_installed_pip_packages():
        result = {}
        try:
            pip_output = subprocess.check_output([
                env.subst("$PYTHONEXE"),
                "-m", "pip", "list", "--format=json",
                "--disable-pip-version-check"
            ])
            packages = json.loads(pip_output)
            for p in packages:
                result[p["name"]] = pepver_to_semver(p["version"])
        except Exception:
            print("Warning! Couldn't extract the list of installed Python "
                  "packages.")

        return result

    installed_packages = _get_installed_pip_packages()
    packages_to_install = list(get_packages_to_install(python_deps,
                                                       installed_packages))

    if packages_to_install:
        packages_str = " ".join(f'"{p}{python_deps[p]}"'
                                for p in packages_to_install)
        env.Execute(
            env.VerboseAction(
                f'"$PYTHONEXE" -m pip install -U -q -q -q {packages_str}',
                "Installing Arduino Python dependencies",
            )
        )


install_python_deps()


def get_MD5_hash(phrase):
    return hashlib.md5(phrase.encode('utf-8')).hexdigest()[:16]


def matching_custom_sdkconfig():
    """Checks if current environment matches existing sdkconfig"""
    cust_sdk_is_present = False

    if not flag_any_custom_sdkconfig:
        return True, cust_sdk_is_present

    last_sdkconfig_path = join(project_dir, "sdkconfig.defaults")
    if not exists(last_sdkconfig_path):
        return False, cust_sdk_is_present

    if not flag_custom_sdkconfig:
        return False, cust_sdk_is_present

    try:
        with open(last_sdkconfig_path) as src:
            line = src.readline()
            if line.startswith("# TASMOTA__"):
                cust_sdk_is_present = True
                custom_options = entry_custom_sdkconfig
                expected_hash = get_MD5_hash(custom_options.strip() + mcu)
                if line.split("__")[1].strip() == expected_hash:
                    return True, cust_sdk_is_present
    except (IOError, IndexError):
        pass

    return False, cust_sdk_is_present


def check_reinstall_frwrk():
    if not flag_custom_sdkconfig and flag_any_custom_sdkconfig:
        # case custom sdkconfig exists and an env without "custom_sdkconfig"
        return True

    if flag_custom_sdkconfig:
        matching_sdkconfig, _ = matching_custom_sdkconfig()
        if not matching_sdkconfig:
            # check if current custom sdkconfig is different from existing
            return True

    return False


def call_compile_libs():
    print(f"*** Compile Arduino IDF libs for {pioenv} ***")
    SConscript("espidf.py")


FRAMEWORK_SDK_DIR = path_cache.sdk_dir
IS_INTEGRATION_DUMP = env.IsIntegrationDump()


def is_framework_subfolder(potential_subfolder):
    """Check if a path is a subfolder of the framework SDK directory"""
    # carefully check before change this function
    if not isabs(potential_subfolder):
        return False
    if (splitdrive(FRAMEWORK_SDK_DIR)[0] !=
            splitdrive(potential_subfolder)[0]):
        return False
    return (commonpath([FRAMEWORK_SDK_DIR]) ==
            commonpath([FRAMEWORK_SDK_DIR, potential_subfolder]))


# Performance optimization with caching
def calculate_include_path_length(includes):
    """Calculate total character count of all include paths with caching"""
    if not hasattr(calculate_include_path_length, '_cache'):
        calculate_include_path_length._cache = {}

    cache_key = tuple(includes)
    if cache_key not in calculate_include_path_length._cache:
        calculate_include_path_length._cache[cache_key] = sum(
            len(str(inc)) for inc in includes)

    return calculate_include_path_length._cache[cache_key]


def analyze_path_distribution(includes):
    """Analyze the distribution of include path lengths for optimization 
    insights"""
    if not includes:
        return {}

    lengths = [len(str(inc)) for inc in includes]
    framework_lengths = [len(str(inc)) for inc in includes
                         if is_framework_subfolder(inc)]

    return {
        'total_paths': len(includes),
        'total_length': sum(lengths),
        'average_length': sum(lengths) / len(lengths),
        'max_length': max(lengths),
        'min_length': min(lengths),
        'framework_paths': len(framework_lengths),
        'framework_total_length': sum(framework_lengths),
        'framework_avg_length': (sum(framework_lengths) /
                                 len(framework_lengths)
                                 if framework_lengths else 0)
    }


def debug_framework_paths(env, include_count, total_length):
    """Debug framework paths to understand the issue (verbose mode only)"""
    if not env.get("VERBOSE"):
        return

    print("*** Debug Framework Paths ***")
    print(f"*** MCU: {mcu} ***")
    print(f"*** FRAMEWORK_DIR: {FRAMEWORK_DIR} ***")
    print(f"*** FRAMEWORK_SDK_DIR: {FRAMEWORK_SDK_DIR} ***")
    print(f"*** SDK exists: {exists(FRAMEWORK_SDK_DIR)} ***")
    print(f"*** Include count: {include_count} ***")
    print(f"*** Total path length: {total_length} ***")

    includes = env.get("CPPPATH", [])
    framework_count = 0
    longest_paths = sorted(includes, key=len, reverse=True)[:5]

    print("*** Longest include paths: ***")
    for i, inc in enumerate(longest_paths):
        is_fw = is_framework_subfolder(inc)
        if is_fw:
            framework_count += 1
        print(f"***   {i+1}: {inc} (length: {len(str(inc))}) -> "
              f"Framework: {is_fw} ***")

    print(f"*** Framework includes found: {framework_count}/"
          f"{len(includes)} ***")

    # Show path distribution analysis
    analysis = analyze_path_distribution(includes)
    print(f"*** Path Analysis: Avg={analysis.get('average_length', 0):.1f}, "
          f"Max={analysis.get('max_length', 0)}, "
          f"Framework Avg={analysis.get('framework_avg_length', 0):.1f} ***")


def apply_include_shortening(env, node, includes, total_length):
    """Applies include path shortening technique"""
    env_get = env.get
    to_unix_path = fs.to_unix_path
    ccflags = env["CCFLAGS"]
    asflags = env["ASFLAGS"]

    includes = [to_unix_path(inc) for inc in env_get("CPPPATH", [])]
    shortened_includes = []
    generic_includes = []

    original_length = total_length
    saved_chars = 0

    for inc in includes:
        if is_framework_subfolder(inc):
            relative_path = to_unix_path(relpath(inc, FRAMEWORK_SDK_DIR))
            shortened_path = "-iwithprefix/" + relative_path
            shortened_includes.append(shortened_path)

            # Calculate character savings
            # Original: full path in -I flag
            # New: -iprefix + shortened relative path
            original_chars = len(f"-I{inc}")
            new_chars = len(shortened_path)
            saved_chars += max(0, original_chars - new_chars)
        else:
            generic_includes.append(inc)

    # Show result message only once with thread safety
    with _PATH_SHORTENING_LOCK:
        if not _PATH_SHORTENING_MESSAGES['shortening_applied']:
            if shortened_includes:
                # Each -I is 2 chars
                removed_i_flags = len(shortened_includes) * 2
                new_total_length = (original_length - saved_chars +
                                    len(f"-iprefix{FRAMEWORK_SDK_DIR}") -
                                    removed_i_flags)
                print(f"*** Applied include path shortening for "
                      f"{len(shortened_includes)} framework paths ***")
                print(f"*** Path length reduced from {original_length} to "
                      f"~{new_total_length} characters ***")
                print(f"*** Estimated savings: {saved_chars} characters ***")
            else:
                if not _PATH_SHORTENING_MESSAGES[
                        'no_framework_paths_warning']:
                    print("*** Warning: Path length high but no framework "
                          "paths found for shortening ***")
                    print("*** This may indicate an architecture-specific "
                          "issue ***")
                    print("*** Run with -v (verbose) for detailed path "
                          "analysis ***")
                    _PATH_SHORTENING_MESSAGES[
                        'no_framework_paths_warning'] = True
            _PATH_SHORTENING_MESSAGES['shortening_applied'] = True

    common_flags = ["-iprefix", FRAMEWORK_SDK_DIR] + shortened_includes

    return env.Object(
        node,
        CPPPATH=generic_includes,
        CCFLAGS=ccflags + common_flags,
        ASFLAGS=asflags + common_flags,
    )


def smart_include_length_shorten(env, node):
    """
    Include path shortening based on bleeding edge configurable threshold 
    with enhanced MCU support
    Uses aggressive thresholds for maximum performance
    """
    if IS_INTEGRATION_DUMP:
        return node

    if not IS_WINDOWS:
        return env.Object(node)

    # Get dynamically configurable bleeding edge threshold
    include_path_threshold = get_include_path_threshold(env, config,
                                                        current_env_section)

    check_and_warn_long_path_support()

    includes = env.get("CPPPATH", [])
    include_count = len(includes)
    total_path_length = calculate_include_path_length(includes)

    # Debug information in verbose mode
    if env.get("VERBOSE"):
        debug_framework_paths(env, include_count, total_path_length)

        # Extended debug information about bleeding edge threshold 
        # configuration
        threshold_info = get_threshold_info(env, config, current_env_section)
        print("*** Bleeding Edge Threshold Configuration Debug ***")
        print(f"***   MCU: {threshold_info['mcu']} ***")
        print(f"***   Bleeding Edge Platform Default: "
              f"{threshold_info['platform_default']} ***")
        print(f"***   Final Bleeding Edge Threshold: "
              f"{threshold_info['final_threshold']} ***")
        print(f"***   Source: {threshold_info['source']} ***")
        print("***   Performance Mode: Maximum Aggressive ***")
        if threshold_info['env_variable']:
            print(f"***   Env Variable: {threshold_info['env_variable']} ***")
        if threshold_info['env_specific']:
            print(f"***   Env Specific: {threshold_info['env_specific']} ***")
        if threshold_info['global_env']:
            print(f"***   Global Env: {threshold_info['global_env']} ***")
        if threshold_info['platformio_section']:
            print(f"***   PlatformIO Section: "
                  f"{threshold_info['platformio_section']} ***")

    # Use the configurable and validated bleeding edge threshold
    if total_path_length <= include_path_threshold:
        return env.Object(node)

    return apply_include_shortening(env, node, includes, total_path_length)


def get_frameworks_in_current_env():
    """Determines the frameworks of the current environment"""
    if "framework" in config.options(current_env_section):
        return config.get(current_env_section, "framework", "")
    return []


# Framework check
current_env_frameworks = get_frameworks_in_current_env()
if "arduino" in current_env_frameworks and "espidf" in current_env_frameworks:
    # Arduino as component is set, switch off Hybrid compile
    flag_custom_sdkconfig = False

# Framework reinstallation if required - Enhanced with secure deletion and 
# error handling
if check_reinstall_frwrk():
    # Secure removal of SDKConfig files
    safe_remove_sdkconfig_files()

    print("*** Reinstall Arduino framework ***")

    # Secure framework cleanup with enhanced error handling
    if safe_framework_cleanup():
        arduino_frmwrk_url = str(platform.get_package_spec(
            "framework-arduinoespressif32")).split("uri=", 1)[1][:-1]
        pm.install(arduino_frmwrk_url)

        if flag_custom_sdkconfig:
            call_compile_libs()
            flag_custom_sdkconfig = False
    else:
        logging.error("Framework cleanup failed - installation aborted")
        sys.exit(1)

if flag_custom_sdkconfig and not flag_any_custom_sdkconfig:
    call_compile_libs()

# Main logic for Arduino Framework
pioframework = env.subst("$PIOFRAMEWORK")
arduino_lib_compile_flag = env.subst("$ARDUINO_LIB_COMPILE_FLAG")

if ("arduino" in pioframework and "espidf" not in pioframework and
        arduino_lib_compile_flag in ("Inactive", "True")):
    # try to remove not needed include path if an lib_ignore entry exists
    from component_manager import ComponentManager
    component_manager = ComponentManager(env)
    component_manager.handle_component_settings()
    silent_action = env.Action(component_manager.restore_pioarduino_build_py)
    # hack to silence scons command output
    silent_action.strfunction = lambda target, source, env: ''
    env.AddPostAction("checkprogsize", silent_action)

    if IS_WINDOWS:
        # Smart include path optimization based on bleeding edge configurable 
        # threshold
        env.AddBuildMiddleware(smart_include_length_shorten)

    build_script_path = join(FRAMEWORK_DIR, "tools", "pioarduino-build.py")
    SConscript(build_script_path)
