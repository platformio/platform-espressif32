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

import json
import os
import re
import site
import semantic_version
import subprocess
import sys
import socket

from platformio.package.version import pepver_to_semver
from platformio.compat import IS_WINDOWS

github_actions = os.getenv('GITHUB_ACTIONS')

PLATFORMIO_URL_VERSION_RE = re.compile(
    r'/v?(\d+\.\d+\.\d+(?:[.-]\w+)?(?:\.\d+)?)(?:\.(?:zip|tar\.gz|tar\.bz2))?$',
    re.IGNORECASE,
)

# Python dependencies required for the build process
python_deps = {
    "uv": ">=0.1.0",
    "platformio": "https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.18.zip",
    "pyyaml": ">=6.0.2",
    "rich-click": ">=1.8.6",
    "zopfli": ">=0.2.2",
    "intelhex": ">=2.3.0",
    "rich": ">=14.0.0",
    "cryptography": ">=45.0.3",
    "ecdsa": ">=0.19.1",
    "bitstring": ">=4.3.1",
    "reedsolo": ">=1.5.3,<1.8",
    "esp-idf-size": ">=1.6.1"
}


def has_internet_connection(host="1.1.1.1", port=53, timeout=2):
    """
    Checks if an internet connection is available (default: Cloudflare DNS server).
    Returns True if a connection is possible, otherwise False.
    """
    try:
        socket.setdefaulttimeout(timeout)
        socket.socket(socket.AF_INET, socket.SOCK_STREAM).connect((host, port))
        return True
    except Exception:
        return False


def get_executable_path(penv_dir, executable_name):
    """
    Get the path to an executable based on the penv_dir.
    """
    exe_suffix = ".exe" if IS_WINDOWS else ""
    scripts_dir = "Scripts" if IS_WINDOWS else "bin"
    
    return os.path.join(penv_dir, scripts_dir, f"{executable_name}{exe_suffix}")


def setup_pipenv_in_package(env, penv_dir):
    """
    Checks if 'penv' folder exists in platformio dir and creates virtual environment if not.
    """
    if not os.path.exists(penv_dir):
        env.Execute(
            env.VerboseAction(
                '"$PYTHONEXE" -m venv --clear "%s"' % penv_dir,
                "Creating pioarduino Python virtual environment: %s" % penv_dir,
            )
        )
        assert os.path.isfile(
            get_executable_path(penv_dir, "pip")
        ), "Error: Failed to create a proper virtual environment. Missing the `pip` binary!"


def setup_python_paths(penv_dir):
    """Setup Python module search paths using the penv_dir."""    
    # Add penv_dir to module search path
    site.addsitedir(penv_dir)
    
    # Add site-packages directory
    python_ver = f"python{sys.version_info.major}.{sys.version_info.minor}"
    site_packages = (
        os.path.join(penv_dir, "Lib", "site-packages") if IS_WINDOWS
        else os.path.join(penv_dir, "lib", python_ver, "site-packages")
    )
    
    if os.path.isdir(site_packages):
        site.addsitedir(site_packages)


def get_packages_to_install(deps, installed_packages):
    """
    Generator for Python packages that need to be installed.
    Compares package names case-insensitively.
    
    Args:
        deps (dict): Dictionary of package names and version specifications
        installed_packages (dict): Dictionary of currently installed packages (keys should be lowercase)
        
    Yields:
        str: Package name that needs to be installed
    """
    for package, spec in deps.items():
        name = package.lower()
        if name not in installed_packages:
            yield package
        elif name == "platformio":
            # Enforce the version from the direct URL if it looks like one.
            # If version can't be parsed, fall back to accepting any installed version.
            m = PLATFORMIO_URL_VERSION_RE.search(spec)
            if m:
                expected_ver = pepver_to_semver(m.group(1))
                if installed_packages.get(name) != expected_ver:
                    # Reinstall to align with the pinned URL version
                    yield package
            else:
                continue
        else:
            version_spec = semantic_version.SimpleSpec(spec)
            if not version_spec.match(installed_packages[name]):
                yield package


def install_python_deps(python_exe, uv_executable):
    """
    Ensure uv package manager is available and install required Python dependencies.
    
    Returns:
        bool: True if successful, False otherwise
    """
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
                [python_exe, "-m", "pip", "install", "uv>=0.1.0", "-q", "-q", "-q"],
                capture_output=True,
                text=True,
                timeout=30  # 30 second timeout
            )
            if result.returncode != 0:
                if result.stderr:
                    print(f"Error output: {result.stderr.strip()}")
                return False

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
        Get list of installed packages in virtual env 'penv' using uv.
        
        Returns:
            dict: Dictionary of installed packages with versions
        """
        result = {}
        try:
            cmd = [uv_executable, "pip", "list", f"--python={python_exe}", "--format=json"]
            result_obj = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                encoding='utf-8',
                timeout=30  # 30 second timeout
            )
            
            if result_obj.returncode == 0:
                content = result_obj.stdout.strip()
                if content:
                    packages = json.loads(content)
                    for p in packages:
                        result[p["name"].lower()] = pepver_to_semver(p["version"])
            else:
                print(f"Warning: uv pip list failed with exit code {result_obj.returncode}")
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
        packages_list = []
        for p in packages_to_install:
            spec = python_deps[p]
            if spec.startswith(('http://', 'https://', 'git+', 'file://')):
                packages_list.append(spec)
            else:
                packages_list.append(f"{p}{spec}")
        
        cmd = [
            uv_executable, "pip", "install",
            f"--python={python_exe}",
            "--quiet", "--upgrade"
        ] + packages_list
        
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=30  # 30 second timeout for package installation
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


def install_esptool(env, platform, python_exe, uv_executable):
    """
    Install esptool from package folder "tool-esptoolpy" using uv package manager.
    Ensures esptool is installed from the specific tool-esptoolpy package directory.
    
    Args:
        env: SCons environment object
        platform: PlatformIO platform object  
        python_exe (str): Path to Python executable in virtual environment
        uv_executable (str): Path to uv executable
    
    Raises:
        SystemExit: If esptool installation fails or package directory not found
    """
    esptool_repo_path = env.subst(platform.get_package_dir("tool-esptoolpy") or "")
    if not esptool_repo_path or not os.path.isdir(esptool_repo_path):
        sys.stderr.write(
            f"Error: 'tool-esptoolpy' package directory not found: {esptool_repo_path!r}\n"
        )
        sys.exit(1)

    # Check if esptool is already installed from the correct path
    try:
        result = subprocess.run(
            [
                python_exe,
                "-c",
                (
                    "import esptool, os, sys; "
                    "expected_path = os.path.normcase(os.path.realpath(sys.argv[1])); "
                    "actual_path = os.path.normcase(os.path.realpath(os.path.dirname(esptool.__file__))); "
                    "print('MATCH' if actual_path.startswith(expected_path) else 'MISMATCH')"
                ),
                esptool_repo_path,
            ],
            capture_output=True,
            check=True,
            text=True,
            timeout=5
        )
        
        if result.stdout.strip() == "MATCH":
            return
            
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError):
        pass

    try:
        subprocess.check_call([
            uv_executable, "pip", "install", "--quiet", "--force-reinstall",
            f"--python={python_exe}",
            "-e", esptool_repo_path
        ])

    except subprocess.CalledProcessError as e:
        sys.stderr.write(
            f"Error: Failed to install esptool from {esptool_repo_path} (exit {e.returncode})\n"
        )
        sys.exit(1)


def setup_python_environment(env, platform, platformio_dir):
    """
    Main function to setup the Python virtual environment and dependencies.
    
    Args:
        env: SCons environment object
        platform: PlatformIO platform object
        platformio_dir (str): Path to PlatformIO core directory
    
    Returns:
        tuple[str, str]: (Path to penv Python executable, Path to esptool script)
        
    Raises:
        SystemExit: If Python version < 3.10 or dependency installation fails
    """
    # Check Python version requirement
    if sys.version_info < (3, 10):
        sys.stderr.write(
            f"Error: Python 3.10 or higher is required. "
            f"Current version: {sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}\n"
            f"Please update your Python installation.\n"
        )
        sys.exit(1)

    penv_dir = os.path.join(platformio_dir, "penv")
    
    # Setup virtual environment if needed
    setup_pipenv_in_package(env, penv_dir)
    
    # Set Python Scons Var to env Python
    penv_python = get_executable_path(penv_dir, "python")
    env.Replace(PYTHONEXE=penv_python)
    
    # check for python binary, exit with error when not found
    assert os.path.isfile(penv_python), f"Python executable not found: {penv_python}"
    
    # Setup Python module search paths
    setup_python_paths(penv_dir)
    
    # Set executable paths from tools
    esptool_binary_path = get_executable_path(penv_dir, "esptool")
    uv_executable = get_executable_path(penv_dir, "uv")

    # Install espressif32 Python dependencies
    if has_internet_connection() or github_actions:
        if not install_python_deps(penv_python, uv_executable):
            sys.stderr.write("Error: Failed to install Python dependencies into penv\n")
            sys.exit(1)
    else:
        print("Warning: No internet connection detected, Python dependency check will be skipped.")

    # Install esptool after dependencies
    install_esptool(env, platform, penv_python, uv_executable)

    return penv_python, esptool_binary_path
