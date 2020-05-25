# Espressif 32: development platform for [PlatformIO](http://platformio.org)

![alt text](https://github.com/platformio/platform-espressif32/workflows/Examples/badge.svg "Espressif 32 development platform")

Espressif Systems is a privately held fabless semiconductor company. They provide wireless communications and Wi-Fi chips which are widely used in mobile devices and the Internet of Things applications.

* [Home](http://platformio.org/platforms/espressif32) (home page in PlatformIO Platform Registry)
* [Documentation](http://docs.platformio.org/page/platforms/espressif32.html) (advanced usage, packages, boards, frameworks, etc.)

# Usage

1. [Install PlatformIO](http://platformio.org)
2. Create PlatformIO project and configure a platform option in [platformio.ini](http://docs.platformio.org/page/projectconf.html) file:

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

Please navigate to [documentation](http://docs.platformio.org/page/platforms/espressif32.html).
