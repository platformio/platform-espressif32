"""
Component manager for ESP32 Arduino framework builds in PlatformIO.

This module provides the ComponentManager class for handling IDF component
addition/removal, library ignore processing, and build script modifications.
It supports managing ESP-IDF components within Arduino framework projects,
allowing developers to add or remove specific components and handle library
dependencies efficiently.
"""

import os
import shutil
import re
import yaml
from yaml import SafeLoader
from os.path import join
from typing import Set, Optional, Dict, Any, List, Tuple


class ComponentManagerConfig:
    """
    Handles configuration and environment setup for component management.
    
    This class centralizes all configuration-related operations and provides
    a unified interface for accessing PlatformIO environment settings,
    board configurations, and framework paths.
    """
    
    def __init__(self, env):
        """
        Initialize the configuration manager with PlatformIO environment.
        
        Extracts and stores essential configuration parameters from the PlatformIO
        environment including platform details, board configuration, MCU type,
        and framework paths. This initialization ensures all dependent classes
        have consistent access to configuration data.
        
        Args:
            env: PlatformIO environment object containing project configuration,
                 board settings, and platform information
        """
        self.env = env
        self.platform = env.PioPlatform()
        self.config = env.GetProjectConfig()
        self.board = env.BoardConfig()
        # Extract MCU type from board configuration, defaulting to esp32
        self.mcu = self.board.get("build.mcu", "esp32").lower()
        # Get project source directory path
        self.project_src_dir = env.subst("$PROJECT_SRC_DIR")
        # Get Arduino framework installation directory
        self.arduino_framework_dir = self.platform.get_package_dir("framework-arduinoespressif32")
        # Get MCU-specific Arduino libraries directory
        self.arduino_libs_mcu = join(self.arduino_framework_dir, "tools", "esp32-arduino-libs", self.mcu)


class ComponentLogger:
    """
    Simple logging functionality for component operations.
    
    Provides centralized logging for all component management operations,
    tracking changes made during the build process and offering summary
    reporting capabilities.
    """
    
    def __init__(self):
        """
        Initialize the logger with empty change tracking.
        
        Sets up internal data structures for tracking component changes
        and modifications made during the build process.
        """
        # List to store all change messages for summary reporting
        self.component_changes: List[str] = []
    
    def log_change(self, message: str) -> None:
        """
        Log a change message with immediate console output.
        
        Records the change message internally for summary reporting and
        immediately prints it to the console with a component manager prefix
        for real-time feedback during build operations.
        
        Args:
            message: Descriptive message about the change or operation performed
        """
        self.component_changes.append(message)
        print(f"[ComponentManager] {message}")
    
    def get_changes_summary(self) -> List[str]:
        """
        Get a copy of all changes made during the session.
        
        Returns a defensive copy of the change log to prevent external
        modification while allowing access to the complete change history.
        
        Returns:
            List of change messages in chronological order
        """
        return self.component_changes.copy()
    
    def print_changes_summary(self) -> None:
        """
        Print a formatted summary of all changes made.
        
        Outputs a nicely formatted summary of all component changes if any
        were made, or a simple message indicating no changes occurred.
        Useful for end-of-build reporting and debugging.
        """
        if self.component_changes:
            print("\n=== Component Manager Changes ===")
            for change in self.component_changes:
                print(f"  {change}")
            print("=" * 35)
        else:
            print("[ComponentManager] No changes made")


class ComponentHandler:
    """
    Handles IDF component addition and removal operations.
    
    Manages the core functionality for adding and removing ESP-IDF components
    from Arduino framework projects, including YAML file manipulation,
    component validation, and cleanup operations.
    """
    
    def __init__(self, config: ComponentManagerConfig, logger: ComponentLogger):
        """
        Initialize the component handler with configuration and logging.
        
        Sets up the component handler with necessary dependencies for
        configuration access and change logging. Initializes tracking
        for removed components to enable proper cleanup operations.
        
        Args:
            config: Configuration manager instance providing access to paths and settings
            logger: Logger instance for recording component operations
        """
        self.config = config
        self.logger = logger
        # Track removed components for cleanup operations
        self.removed_components: Set[str] = set()
    
    def handle_component_settings(self, add_components: bool = False, remove_components: bool = False) -> None:
        """
        Handle adding and removing IDF components based on project configuration.
        
        Main entry point for component management operations. Processes both
        component additions and removals based on project configuration options,
        manages backup creation, and handles cleanup of removed components.
        
        Args:
            add_components: Whether to process component additions from custom_component_add
            remove_components: Whether to process component removals from custom_component_remove
        """
        # Create backup before first component removal and on every add of a component
        if remove_components and not self.removed_components or add_components:
            self._backup_pioarduino_build_py()
            self.logger.log_change("Created backup of build file")
        
        # Check if env and GetProjectOption are available
        if hasattr(self.config, 'env') and hasattr(self.config.env, 'GetProjectOption'):
            component_yml_path = self._get_or_create_component_yml()
            component_data = self._load_component_yml(component_yml_path)
            
            if remove_components:
                self._process_component_removals(component_data)
            
            if add_components:
                self._process_component_additions(component_data)
            
            self._save_component_yml(component_yml_path, component_data)
            
            # Clean up removed components
            if self.removed_components:
                self._cleanup_removed_components()
    
    def _process_component_removals(self, component_data: Dict[str, Any]) -> None:
        """
        Process component removal requests from project configuration.
        
        Reads the custom_component_remove option from platformio.ini and
        processes each component for removal from the dependency list.
        Handles errors gracefully and logs all operations.
        
        Args:
            component_data: Component configuration data dictionary containing dependencies
        """
        try:
            remove_option = self.config.env.GetProjectOption("custom_component_remove", None)
            if remove_option:
                # Split multiline option into individual components
                components_to_remove = remove_option.splitlines()
                self._remove_components(component_data, components_to_remove)
        except Exception as e:
            self.logger.log_change(f"Error removing components: {str(e)}")
    
    def _process_component_additions(self, component_data: Dict[str, Any]) -> None:
        """
        Process component addition requests from project configuration.
        
        Reads the custom_component_add option from platformio.ini and
        processes each component for addition to the dependency list.
        Handles errors gracefully and logs all operations.
        
        Args:
            component_data: Component configuration data dictionary containing dependencies
        """
        try:
            add_option = self.config.env.GetProjectOption("custom_component_add", None)
            if add_option:
                # Split multiline option into individual components
                components_to_add = add_option.splitlines()
                self._add_components(component_data, components_to_add)
        except Exception as e:
            self.logger.log_change(f"Error adding components: {str(e)}")
    
    def _get_or_create_component_yml(self) -> str:
        """
        Get path to idf_component.yml, creating it if necessary.
        
        Searches for existing idf_component.yml files in the Arduino framework
        directory first, then in the project source directory. If no file
        exists, creates a new one in the project source directory with
        default content.
        
        Returns:
            Absolute path to the component YAML file
        """
        # Try Arduino framework first
        framework_yml = join(self.config.arduino_framework_dir, "idf_component.yml")
        if os.path.exists(framework_yml):
            self._create_backup(framework_yml)
            return framework_yml
        
        # Try project source directory
        project_yml = join(self.config.project_src_dir, "idf_component.yml")
        if os.path.exists(project_yml):
            self._create_backup(project_yml)
            return project_yml
        
        # Create new file in project source
        self._create_default_component_yml(project_yml)
        return project_yml
    
    def _create_backup(self, file_path: str) -> None:
        """
        Create backup of a file with .orig extension.
        
        Creates a backup copy of the specified file by appending .orig
        to the filename. Only creates the backup if it doesn't already
        exist to preserve the original state.
        
        Args:
            file_path: Absolute path to the file to backup
        """
        backup_path = f"{file_path}.orig"
        if not os.path.exists(backup_path):
            shutil.copy(file_path, backup_path)
    
    def _create_default_component_yml(self, file_path: str) -> None:
        """
        Create a default idf_component.yml file with basic ESP-IDF dependency.
        
        Creates a new component YAML file with minimal default content
        specifying ESP-IDF version 5.1 or higher as the base dependency.
        This ensures compatibility with modern ESP-IDF features.
        
        Args:
            file_path: Absolute path where to create the new YAML file
        """
        default_content = {
            "dependencies": {
                "idf": ">=5.1"
            }
        }
        
        with open(file_path, 'w', encoding='utf-8') as f:
            yaml.dump(default_content, f)
    
    def _load_component_yml(self, file_path: str) -> Dict[str, Any]:
        """
        Load and parse idf_component.yml file safely.
        
        Attempts to load and parse the YAML file using SafeLoader for
        security. Returns a default structure with empty dependencies
        if the file cannot be read or parsed.
        
        Args:
            file_path: Absolute path to the YAML file to load
            
        Returns:
            Parsed YAML data as dictionary, or default structure on failure
        """
        try:
            with open(file_path, "r", encoding='utf-8') as f:
                return yaml.load(f, Loader=SafeLoader) or {"dependencies": {}}
        except Exception:
            return {"dependencies": {}}
    
    def _save_component_yml(self, file_path: str, data: Dict[str, Any]) -> None:
        """
        Save component data to YAML file safely.
        
        Attempts to write the component data dictionary to the specified
        YAML file. Handles errors gracefully by silently failing to
        prevent build interruption.
        
        Args:
            file_path: Absolute path to the YAML file to write
            data: Component data dictionary to serialize
        """
        try:
            with open(file_path, "w", encoding='utf-8') as f:
                yaml.dump(data, f)
        except Exception:
            pass
    
    def _remove_components(self, component_data: Dict[str, Any], components_to_remove: list) -> None:
        """
        Remove specified components from the configuration.
        
        Iterates through the list of components to remove, checking if each
        exists in the dependencies and removing it if found. Tracks removed
        components for later cleanup operations and logs all actions.
        
        Args:
            component_data: Component configuration data dictionary
            components_to_remove: List of component names to remove
        """
        dependencies = component_data.setdefault("dependencies", {})
        
        for component in components_to_remove:
            component = component.strip()
            if not component:
                continue
                
            if component in dependencies:
                self.logger.log_change(f"Removed component: {component}")
                del dependencies[component]
                
                # Track for cleanup - convert to filesystem-safe name
                filesystem_name = self._convert_component_name_to_filesystem(component)
                self.removed_components.add(filesystem_name)
            else:
                self.logger.log_change(f"Component not found: {component}")
    
    def _add_components(self, component_data: Dict[str, Any], components_to_add: list) -> None:
        """
        Add specified components to the configuration.
        
        Processes each component entry, parsing name and version information,
        and adds new components to the dependencies. Skips components that
        already exist and filters out entries that are too short to be valid.
        
        Args:
            component_data: Component configuration data dictionary
            components_to_add: List of component entries to add (format: name@version or name)
        """
        dependencies = component_data.setdefault("dependencies", {})
        
        for component in components_to_add:
            component = component.strip()
            if len(component) <= 4:  # Skip too short entries
                continue
            
            component_name, version = self._parse_component_entry(component)
            
            if component_name not in dependencies:
                dependencies[component_name] = {"version": version}
                self.logger.log_change(f"Added component: {component_name} ({version})")
            else:
                self.logger.log_change(f"Component already exists: {component_name}")
    
    def _parse_component_entry(self, entry: str) -> Tuple[str, str]:
        """
        Parse component entry into name and version components.
        
        Splits component entries that contain version information (format: name@version)
        and returns both parts. If no version is specified, defaults to "*" for
        latest version.
        
        Args:
            entry: Component entry string (e.g., "espressif/esp_timer@1.0.0" or "espressif/esp_timer")
            
        Returns:
            Tuple containing (component_name, version)
        """
        if "@" in entry:
            name, version = entry.split("@", 1)
            return (name.strip(), version.strip())
        return (entry.strip(), "*")
    
    def _convert_component_name_to_filesystem(self, component_name: str) -> str:
        """
        Convert component name from registry format to filesystem format.
        
        Converts component names from ESP Component Registry format (using forward slashes)
        to filesystem-safe format (using double underscores) for directory operations.
        
        Args:
            component_name: Component name in registry format (e.g., "espressif/esp_timer")
            
        Returns:
            Filesystem-safe component name (e.g., "espressif__esp_timer")
        """
        return component_name.replace("/", "__")
    
    def _backup_pioarduino_build_py(self) -> None:
        """
        Create backup of the original pioarduino-build.py file.
        
        Creates a backup of the Arduino framework's build script before
        making modifications. Only operates when Arduino framework is active
        and creates MCU-specific backup names to avoid conflicts.
        """
        if "arduino" not in self.config.env.subst("$PIOFRAMEWORK"):
            return
        
        build_py_path = join(self.config.arduino_libs_mcu, "pioarduino-build.py")
        backup_path = join(self.config.arduino_libs_mcu, f"pioarduino-build.py.{self.config.mcu}")
        
        if os.path.exists(build_py_path) and not os.path.exists(backup_path):
            shutil.copy2(build_py_path, backup_path)
    
    def _cleanup_removed_components(self) -> None:
        """
        Clean up removed components and restore original build file.
        
        Performs cleanup operations for all components that were removed,
        including removing include directories and cleaning up CPPPATH
        entries from the build script.
        """
        for component in self.removed_components:
            self._remove_include_directory(component)
        
        self._remove_cpppath_entries()
    
    def _remove_include_directory(self, component: str) -> None:
        """
        Remove include directory for a specific component.
        
        Removes the component's include directory from the Arduino framework
        libraries to prevent compilation errors and reduce build overhead.
        
        Args:
            component: Component name in filesystem format
        """
        include_path = join(self.config.arduino_libs_mcu, "include", component)
        
        if os.path.exists(include_path):
            shutil.rmtree(include_path)
    
    def _remove_cpppath_entries(self) -> None:
        """
        Remove CPPPATH entries for removed components from pioarduino-build.py.
        
        Scans the Arduino build script and removes include path entries
        for all components that were removed from the project. Uses
        multiple regex patterns to catch different include path formats.
        """
        build_py_path = join(self.config.arduino_libs_mcu, "pioarduino-build.py")
        
        if not os.path.exists(build_py_path):
            return
        
        try:
            with open(build_py_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            original_content = content
            
            # Remove CPPPATH entries for each removed component
            for component in self.removed_components:
                patterns = [
                    rf'.*join\([^,]*,\s*"include",\s*"{re.escape(component)}"[^)]*\),?\n',
                    rf'.*"include/{re.escape(component)}"[^,\n]*,?\n',
                    rf'.*"[^"]*include[^"]*{re.escape(component)}[^"]*"[^,\n]*,?\n'
                ]
                
                for pattern in patterns:
                    content = re.sub(pattern, '', content)
            
            if content != original_content:
                with open(build_py_path, 'w', encoding='utf-8') as f:
                    f.write(content)
                
        except Exception:
            pass


class LibraryIgnoreHandler:
    """
    Handles lib_ignore processing and include removal.
    
    Manages the processing of lib_ignore entries from platformio.ini,
    converting library names to include paths and removing corresponding
    entries from the build script while protecting critical components.
    """
    
    def __init__(self, config: ComponentManagerConfig, logger: ComponentLogger):
        """
        Initialize the library ignore handler.
        
        Sets up the handler with configuration and logging dependencies,
        initializes tracking for ignored libraries, and prepares caching
        for Arduino library mappings.
        
        Args:
            config: Configuration manager instance for accessing paths and settings
            logger: Logger instance for recording library operations
        """
        self.config = config
        self.logger = logger
        # Track ignored libraries for processing
        self.ignored_libs: Set[str] = set()
        # Cache for Arduino library mappings (lazy loaded)
        self._arduino_libraries_cache = None
    
    def handle_lib_ignore(self) -> None:
        """
        Handle lib_ignore entries from platformio.ini and remove corresponding includes.
        
        Main entry point for library ignore processing. Creates backup if needed,
        processes lib_ignore entries from the current environment, and removes
        corresponding include paths from the build script.
        """
        # Create backup before processing lib_ignore
        if not self.ignored_libs:
            self._backup_pioarduino_build_py()
        
        # Get lib_ignore entries from current environment only
        lib_ignore_entries = self._get_lib_ignore_entries()
        
        if lib_ignore_entries:
            self.ignored_libs.update(lib_ignore_entries)
            self._remove_ignored_lib_includes()
            self.logger.log_change(f"Processed {len(lib_ignore_entries)} ignored libraries")
    
    def _get_lib_ignore_entries(self) -> List[str]:
        """
        Get lib_ignore entries from current environment configuration only.
        
        Extracts and processes lib_ignore entries from the platformio.ini
        configuration, converting library names to include directory names
        and filtering out critical ESP32 components that should never be ignored.
        
        Returns:
            List of processed library names ready for include path removal
        """
        try:
            # Get lib_ignore from current environment only
            lib_ignore = self.config.env.GetProjectOption("lib_ignore", [])
            
            if isinstance(lib_ignore, str):
                lib_ignore = [lib_ignore]
            elif lib_ignore is None:
                lib_ignore = []
            
            # Clean and normalize entries
            cleaned_entries = []
            for entry in lib_ignore:
                entry = str(entry).strip()
                if entry:
                    # Convert library names to potential include directory names
                    include_name = self._convert_lib_name_to_include(entry)
                    cleaned_entries.append(include_name)
            
            # Filter out critical ESP32 components that should never be ignored
            critical_components = [
                'lwip',           # Network stack
                'freertos',       # Real-time OS
                'esp_system',     # System functions
                'esp_common',     # Common ESP functions
                'driver',         # Hardware drivers
                'nvs_flash',      # Non-volatile storage
                'spi_flash',      # Flash memory access
                'esp_timer',      # Timer functions
                'esp_event',      # Event system
                'log'             # Logging system
            ]
            
            filtered_entries = []
            for entry in cleaned_entries:
                if entry not in critical_components:
                    filtered_entries.append(entry)
            
            return filtered_entries
            
        except Exception:
            return []
    
    def _has_bt_ble_dependencies(self) -> bool:
        """
        Check if lib_deps contains any BT/BLE related dependencies.
        
        Scans the lib_deps configuration option for Bluetooth or BLE
        related keywords to determine if BT components should be protected
        from removal even if they appear in lib_ignore.
        
        Returns:
            True if BT/BLE dependencies are found in lib_deps
        """
        try:
            # Get lib_deps from current environment
            lib_deps = self.config.env.GetProjectOption("lib_deps", [])
            
            if isinstance(lib_deps, str):
                lib_deps = [lib_deps]
            elif lib_deps is None:
                lib_deps = []
            
            # Convert to string and check for BT/BLE keywords
            lib_deps_str = ' '.join(str(dep) for dep in lib_deps).upper()
            
            bt_ble_keywords = ['BLE', 'BT', 'NIMBLE', 'BLUETOOTH']

            return any(keyword in lib_deps_str for keyword in bt_ble_keywords)
            
        except Exception:
            return False
    
    def _is_bt_related_library(self, lib_name: str) -> bool:
        """
        Check if a library name is related to Bluetooth/BLE functionality.
        
        Examines library names for Bluetooth and BLE related keywords
        to determine if the library should be protected when BT dependencies
        are present in the project.
        
        Args:
            lib_name: Library name to check for BT/BLE relation
            
        Returns:
            True if library name contains BT/BLE related keywords
        """
        lib_name_upper = lib_name.upper()
        
        bt_related_names = [
            'BT',
            'BLE', 
            'BLUETOOTH',
            'NIMBLE',
            'ESP32_BLE',
            'ESP32BLE',
            'BLUETOOTHSERIAL',
            'BLE_ARDUINO',
            'ESP_BLE',
            'ESP_BT'
        ]

        return any(bt_name in lib_name_upper for bt_name in bt_related_names)
    
    def _get_arduino_core_libraries(self) -> Dict[str, str]:
        """
        Get all Arduino core libraries and their corresponding include paths.
        
        Scans the Arduino framework libraries directory to build a mapping
        of library names to their corresponding include paths. Reads
        library.properties files to get official library names.
        
        Returns:
            Dictionary mapping library names to include directory names
        """
        libraries_mapping = {}
        
        # Path to Arduino Core Libraries
        arduino_libs_dir = join(self.config.arduino_framework_dir, "libraries")
        
        if not os.path.exists(arduino_libs_dir):
            return libraries_mapping
        
        try:
            for entry in os.listdir(arduino_libs_dir):
                lib_path = join(arduino_libs_dir, entry)
                if os.path.isdir(lib_path):
                    lib_name = self._get_library_name_from_properties(lib_path)
                    if lib_name:
                        include_path = self._map_library_to_include_path(lib_name, entry)
                        libraries_mapping[lib_name.lower()] = include_path
                        libraries_mapping[entry.lower()] = include_path  # Also use directory name as key
        except Exception:
            pass
        
        return libraries_mapping
    
    def _get_library_name_from_properties(self, lib_dir: str) -> Optional[str]:
        """
        Extract library name from library.properties file.
        
        Reads the library.properties file in the given directory and
        extracts the official library name from the 'name=' field.
        
        Args:
            lib_dir: Path to library directory containing library.properties
            
        Returns:
            Official library name or None if not found or readable
        """
        prop_path = join(lib_dir, "library.properties")
        if not os.path.isfile(prop_path):
            return None
        
        try:
            with open(prop_path, 'r', encoding='utf-8') as f:
                for line in f:
                    line = line.strip()
                    if line.startswith('name='):
                        return line.split('=', 1)[1].strip()
        except Exception:
            pass
        
        return None
    
    def _map_library_to_include_path(self, lib_name: str, dir_name: str) -> str:
        """
        Map library name to corresponding include path.
        
        Converts Arduino library names to their corresponding ESP-IDF
        component include paths using an extensive mapping table.
        Handles common Arduino libraries and their ESP-IDF equivalents.
        
        Args:
            lib_name: Official library name from library.properties
            dir_name: Directory name of the library
            
        Returns:
            Corresponding ESP-IDF component include path name
        """
        lib_name_lower = lib_name.lower().replace(' ', '').replace('-', '_')
        dir_name_lower = dir_name.lower()
        
        # Extended mapping list with Arduino Core Libraries
        extended_mapping = {
            # Core ESP32 mappings
            'wifi': 'esp_wifi',
            'bluetooth': 'bt',
            'bluetoothserial': 'bt',
            'ble': 'bt',
            'bt': 'bt',
            'ethernet': 'esp_eth',
            'websocket': 'esp_websocket_client',
            'http': 'esp_http_client',
            'https': 'esp_https_ota',
            'ota': 'esp_https_ota',
            'spiffs': 'spiffs',
            'fatfs': 'fatfs',
            'mesh': 'esp_wifi_mesh',
            'smartconfig': 'esp_smartconfig',
            'mdns': 'mdns',
            'coap': 'coap',
            'mqtt': 'mqtt',
            'json': 'cjson',
            'mbedtls': 'mbedtls',
            'openssl': 'openssl',
            
            # Arduino Core specific mappings (safe mappings that don't conflict with critical components)
            'esp32blearduino': 'bt',
            'esp32_ble_arduino': 'bt',
            'esp32': 'esp32',
            'wire': 'driver',
            'spi': 'driver',
            'i2c': 'driver',
            'uart': 'driver',
            'serial': 'driver',
            'analogwrite': 'driver',
            'ledc': 'driver',
            'pwm': 'driver',
            'dac': 'driver',
            'adc': 'driver',
            'touch': 'driver',
            'hall': 'driver',
            'rtc': 'driver',
            'timer': 'esp_timer',
            'preferences': 'arduino_preferences',
            'eeprom': 'arduino_eeprom',
            'update': 'esp_https_ota',
            'httpupdate': 'esp_https_ota',
            'httpclient': 'esp_http_client',
            'httpsclient': 'esp_https_ota',
            'wifimanager': 'esp_wifi',
            'wificlientsecure': 'esp_wifi',
            'wifiserver': 'esp_wifi',
            'wifiudp': 'esp_wifi',
            'wificlient': 'esp_wifi',
            'wifiap': 'esp_wifi',
            'wifimulti': 'esp_wifi',
            'esp32webserver': 'esp_http_server',
            'webserver': 'esp_http_server',
            'asyncwebserver': 'esp_http_server',
            'dnsserver': 'lwip',
            'netbios': 'netbios',
            'simpletime': 'lwip',
            'fs': 'vfs',
            'sd': 'fatfs',
            'sd_mmc': 'fatfs',
            'littlefs': 'esp_littlefs',
            'ffat': 'fatfs',
            'camera': 'esp32_camera',
            'esp_camera': 'esp32_camera',
            'arducam': 'esp32_camera',
            'rainmaker': 'esp_rainmaker',
            'esp_rainmaker': 'esp_rainmaker',
            'provisioning': 'wifi_provisioning',
            'wifiprovisioning': 'wifi_provisioning',
            'espnow': 'esp_now',
            'esp_now': 'esp_now',
            'esptouch': 'esp_smartconfig',
            'ping': 'lwip',
            'netif': 'lwip',
            'tcpip': 'lwip'
        }
        
        # Check extended mapping first
        if lib_name_lower in extended_mapping:
            return extended_mapping[lib_name_lower]
        
        # Check directory name
        if dir_name_lower in extended_mapping:
            return extended_mapping[dir_name_lower]
        
        # Fallback: Use directory name as include path
        return dir_name_lower
    
    def _convert_lib_name_to_include(self, lib_name: str) -> str:
        """
        Convert library name to potential include directory name.
        
        Converts library names from platformio.ini lib_ignore entries
        to their corresponding include directory names. Uses Arduino
        core library mappings and common naming conventions.
        
        Args:
            lib_name: Library name from lib_ignore configuration
            
        Returns:
            Converted include directory name for path removal
        """
        # Load Arduino Core Libraries on first call
        if not hasattr(self, '_arduino_libraries_cache'):
            self._arduino_libraries_cache = self._get_arduino_core_libraries()
        
        lib_name_lower = lib_name.lower()
        
        # Check Arduino Core Libraries first
        if lib_name_lower in self._arduino_libraries_cache:
            return self._arduino_libraries_cache[lib_name_lower]
        
        # Remove common prefixes and suffixes
        cleaned_name = lib_name_lower
        
        # Remove common prefixes
        prefixes_to_remove = ['lib', 'arduino-', 'esp32-', 'esp-']
        for prefix in prefixes_to_remove:
            if cleaned_name.startswith(prefix):
                cleaned_name = cleaned_name[len(prefix):]
        
        # Remove common suffixes
        suffixes_to_remove = ['-lib', '-library', '.h']
        for suffix in suffixes_to_remove:
            if cleaned_name.endswith(suffix):
                cleaned_name = cleaned_name[:-len(suffix)]
        
        # Check again with cleaned name
        if cleaned_name in self._arduino_libraries_cache:
            return self._arduino_libraries_cache[cleaned_name]
        
        # Direct mapping for common cases not in Arduino libraries
        direct_mapping = {
            'ble': 'bt',
            'bluetooth': 'bt',
            'bluetoothserial': 'bt'
        }
        
        if cleaned_name in direct_mapping:
            return direct_mapping[cleaned_name]
        
        return cleaned_name
    
    def _remove_ignored_lib_includes(self) -> None:
        """
        Remove include entries for ignored libraries from pioarduino-build.py.
        
        Processes the Arduino build script to remove CPPPATH entries for
        all ignored libraries. Implements protection for BT/BLE and DSP
        components when dependencies are detected. Uses multiple regex
        patterns to catch different include path formats.
        """
        build_py_path = join(self.config.arduino_libs_mcu, "pioarduino-build.py")
        
        if not os.path.exists(build_py_path):
            self.logger.log_change("Build file not found")
            return
        
        # Check if BT/BLE dependencies exist in lib_deps
        bt_ble_protected = self._has_bt_ble_dependencies()
        if bt_ble_protected:
            self.logger.log_change("BT/BLE protection enabled")
        
        try:
            with open(build_py_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            original_content = content
            total_removed = 0
            
            # Remove CPPPATH entries for each ignored library
            for lib_name in self.ignored_libs:
                # Skip BT-related libraries if BT/BLE dependencies are present
                if bt_ble_protected and self._is_bt_related_library(lib_name):
                    self.logger.log_change(f"Protected BT library: {lib_name}")
                    continue
                
                # Hard protection for DSP components
                if lib_name.lower() in ['dsp', 'esp_dsp', 'dsps', 'fft2r', 'dsps_fft2r']:
                    self.logger.log_change(f"Protected DSP component: {lib_name}")
                    continue
                    
                # Multiple patterns to catch different include formats
                patterns = [
                    rf'.*join\([^,]*,\s*"include",\s*"{re.escape(lib_name)}"[^)]*\),?\n',
                    rf'.*"include/{re.escape(lib_name)}"[^,\n]*,?\n',
                    rf'.*"[^"]*include[^"]*{re.escape(lib_name)}[^"]*"[^,\n]*,?\n',
                    rf'.*"[^"]*/{re.escape(lib_name)}/include[^"]*"[^,\n]*,?\n',
                    rf'.*"[^"]*{re.escape(lib_name)}[^"]*include[^"]*"[^,\n]*,?\n',
                    rf'.*join\([^)]*"include"[^)]*"{re.escape(lib_name)}"[^)]*\),?\n',
                    rf'.*"{re.escape(lib_name)}/include"[^,\n]*,?\n',
                    rf'\s*"[^"]*/{re.escape(lib_name)}/[^"]*",?\n'
                ]
                
                removed_count = 0
                for pattern in patterns:
                    matches = re.findall(pattern, content)
                    if matches:
                        content = re.sub(pattern, '', content)
                        removed_count += len(matches)
                
                if removed_count > 0:
                    self.logger.log_change(f"Ignored library: {lib_name} ({removed_count} entries)")
                    total_removed += removed_count
            
            # Clean up empty lines and trailing commas
            content = re.sub(r'\n\s*\n', '\n', content)
            content = re.sub(r',\s*\n\s*\]', '\n]', content)
            
            # Validate and write changes
            if self._validate_changes(original_content, content) and content != original_content:
                with open(build_py_path, 'w', encoding='utf-8') as f:
                    f.write(content)
                self.logger.log_change(f"Updated build file ({total_removed} total removals)")
                
        except (IOError, OSError) as e:
            self.logger.log_change(f"Error processing libraries: {str(e)}")
        except Exception as e:
            self.logger.log_change(f"Unexpected error processing libraries: {str(e)}")
    
    def _validate_changes(self, original_content: str, new_content: str) -> bool:
        """
        Validate that the changes are reasonable and safe.
        
        Performs sanity checks on the modified content to ensure that
        the changes don't remove too much content or create invalid
        modifications that could break the build process.
        
        Args:
            original_content: Original file content before modifications
            new_content: Modified file content after processing
            
        Returns:
            True if changes are within acceptable limits and safe to apply
        """
        original_lines = len(original_content.splitlines())
        new_lines = len(new_content.splitlines())
        removed_lines = original_lines - new_lines
        
        # Don't allow removing more than 50% of the file or negative changes
        return not (removed_lines > original_lines * 0.5 or removed_lines < 0)
    
    def _backup_pioarduino_build_py(self) -> None:
        """
        Create backup of the original pioarduino-build.py file.
        
        Creates a backup copy of the Arduino build script before making
        modifications. Only operates when Arduino framework is active
        and uses MCU-specific backup naming to avoid conflicts.
        """
        if "arduino" not in self.config.env.subst("$PIOFRAMEWORK"):
            return
        
        build_py_path = join(self.config.arduino_libs_mcu, "pioarduino-build.py")
        backup_path = join(self.config.arduino_libs_mcu, f"pioarduino-build.py.{self.config.mcu}")
        
        if os.path.exists(build_py_path) and not os.path.exists(backup_path):
            shutil.copy2(build_py_path, backup_path)


class BackupManager:
    """
    Handles backup and restore operations for build files.
    
    Manages the creation and restoration of backup files for the Arduino
    framework build scripts, ensuring that original files can be restored
    when needed or when builds are cleaned.
    """
    
    def __init__(self, config: ComponentManagerConfig):
        """
        Initialize the backup manager with configuration access.
        
        Sets up the backup manager with access to configuration paths
        and settings needed for backup and restore operations.
        
        Args:
            config: Configuration manager instance providing access to paths
        """
        self.config = config
    
    def backup_pioarduino_build_py(self) -> None:
        """
        Create backup of the original pioarduino-build.py file.
        
        Creates a backup copy of the Arduino framework's build script
        with MCU-specific naming to prevent conflicts between different
        ESP32 variants. Only creates backup if it doesn't already exist.
        """
        if "arduino" not in self.config.env.subst("$PIOFRAMEWORK"):
            return
        
        build_py_path = join(self.config.arduino_libs_mcu, "pioarduino-build.py")
        backup_path = join(self.config.arduino_libs_mcu, f"pioarduino-build.py.{self.config.mcu}")
        
        if os.path.exists(build_py_path) and not os.path.exists(backup_path):
            shutil.copy2(build_py_path, backup_path)
    
    def restore_pioarduino_build_py(self, target=None, source=None, env=None) -> None:
        """
        Restore the original pioarduino-build.py from backup.
        
        Restores the original Arduino build script from the backup copy
        and removes the backup file. This is typically called during
        clean operations or when resetting the build environment.
        
        Args:
            target: Build target (unused, for PlatformIO compatibility)
            source: Build source (unused, for PlatformIO compatibility)
            env: Environment (unused, for PlatformIO compatibility)
        """
        build_py_path = join(self.config.arduino_libs_mcu, "pioarduino-build.py")
        backup_path = join(self.config.arduino_libs_mcu, f"pioarduino-build.py.{self.config.mcu}")
        
        if os.path.exists(backup_path):
            shutil.copy2(backup_path, build_py_path)
            os.remove(backup_path)


class ComponentManager:
    """
    Main component manager that orchestrates all operations.
    
    Primary interface for component management operations, coordinating
    between specialized handlers for components, libraries, and backups.
    Uses composition pattern to organize functionality into focused classes.
    """
    
    def __init__(self, env):
        """
        Initialize the ComponentManager with composition pattern.
        
        Creates and configures all specialized handler instances using
        the composition pattern for better separation of concerns and
        maintainability. Each handler focuses on a specific aspect
        of component management.
        
        Args:
            env: PlatformIO environment object containing project configuration
        """
        self.config = ComponentManagerConfig(env)
        self.logger = ComponentLogger()
        self.component_handler = ComponentHandler(self.config, self.logger)
        self.library_handler = LibraryIgnoreHandler(self.config, self.logger)
        self.backup_manager = BackupManager(self.config)
    
    def handle_component_settings(self, add_components: bool = False, remove_components: bool = False) -> None:
        """
        Handle component operations by delegating to specialized handlers.
        
        Main entry point for component management operations. Coordinates
        component addition/removal and library ignore processing, then
        provides a summary of all changes made during the session.
        
        Args:
            add_components: Whether to process component additions from configuration
            remove_components: Whether to process component removals from configuration
        """
        self.component_handler.handle_component_settings(add_components, remove_components)
        self.library_handler.handle_lib_ignore()
        
        # Print summary
        changes = self.logger.get_changes_summary()
        if changes:
            self.logger.log_change(f"Session completed with {len(changes)} changes")
    
    def handle_lib_ignore(self) -> None:
        """
        Delegate lib_ignore handling to specialized handler.
        
        Provides direct access to library ignore processing for cases
        where only library handling is needed without component operations.
        """
        self.library_handler.handle_lib_ignore()
    
    def restore_pioarduino_build_py(self, target=None, source=None, env=None) -> None:
        """
        Delegate backup restoration to backup manager.
        
        Provides access to backup restoration functionality, typically
        used during clean operations or build environment resets.
        
        Args:
            target: Build target (unused, for PlatformIO compatibility)
            source: Build source (unused, for PlatformIO compatibility)
            env: Environment (unused, for PlatformIO compatibility)
        """
        self.backup_manager.restore_pioarduino_build_py(target, source, env)
    
    def get_changes_summary(self) -> List[str]:
        """
        Get summary of changes from logger.
        
        Provides access to the complete list of changes made during
        the current session for reporting or debugging purposes.
        
        Returns:
            List of change messages in chronological order
        """
        return self.logger.get_changes_summary()
    
    def print_changes_summary(self) -> None:
        """
        Print changes summary via logger.
        
        Outputs a formatted summary of all changes made during the
        session, useful for build reporting and debugging.
        """
        self.logger.print_changes_summary()
