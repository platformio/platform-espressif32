# Espressif 32: development platform for [PlatformIO](https://platformio.org)

[![Build Status](https://github.com/platformio/platform-espressif32/workflows/Examples/badge.svg)](https://github.com/platformio/platform-espressif32/actions)

Espressif Systems is a privately held fabless semiconductor company. They provide wireless communications and Wi-Fi chips which are widely used in mobile devices and the Internet of Things applications.

* [Home](https://registry.platformio.org/platforms/platformio/espressif32) (home page in the PlatformIO Registry)
* [Documentation](https://docs.platformio.org/page/platforms/espressif32.html) (advanced usage, packages, boards, frameworks, etc.)

# Usage

1. [Install PlatformIO](https://platformio.org)
2. Create PlatformIO project and configure a platform option in [platformio.ini](https://docs.platformio.org/page/projectconf.html) file:

## Stable version

```ini
[env:stable]
platform = espressif32
board = ...
...
```

## Development version

```ini
[env:development]
platform = https://github.com/platformio/platform-espressif32.git
board = ...
...
```

# Configuration

Please navigate to [documentation](https://docs.platformio.org/page/platforms/espressif32.html).
