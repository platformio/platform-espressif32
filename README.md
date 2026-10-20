# Espressif 32: development platform for [PlatformIO](https://platformio.org)

[![Build Status](https://github.com/platformio/platform-espressif32/workflows/Examples/badge.svg)](https://github.com/platformio/platform-espressif32/actions)

ESP32 is a series of low-cost, low-power system on a chip microcontrollers with integrated Wi-Fi and Bluetooth. ESP32 integrates an antenna switch, RF balun, power amplifier, low-noise receive amplifier, filters, and power management modules.

* [Home](https://registry.platformio.org/platforms/platformio/espressif32) (home page in the PlatformIO Registry)
* [Documentation](https://docs.platformio.org/page/platforms/espressif32.html) (advanced usage, packages, boards, frameworks, etc.)

# Usage

1. [Install PlatformIO](https://platformio.org)
2. Create PlatformIO project and configure a platform option in [platformio.ini](https://docs.platformio.org/page/projectconf.html) file:

## Stable version

See `platform` [documentation](https://docs.platformio.org/en/latest/projectconf/sections/env/options/platform/platform.html#projectconf-env-platform) for details.

```ini
[env:stable]
; recommended to pin to a version, see https://github.com/platformio/platform-espressif32/releases
; platform = espressif32 @ ^6.0.1
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

## Crystal frequency (non-40 MHz boards)

The prebuilt Arduino libraries (`framework-arduinoespressif32-esp32-libs`, Arduino-ESP32 3.x) assume a 40 MHz crystal. Classic ESP32 boards with a 26 MHz crystal (check with `esptool flash-id`, which prints `Crystal frequency: 26MHz`) otherwise boot with wrong clocks: serial output is only readable at 74880 baud (115200 x 26/40) and Wi-Fi does not connect.

Set the crystal frequency in MHz with `board_build.f_xtal`. It is passed to the Arduino core as `-DF_XTAL_MHZ=<value>`, which re-programs the clocks at startup:

```ini
[env:esp32dev_26mhz]
platform = espressif32
board = esp32dev
framework = arduino
board_build.f_xtal = 26
monitor_speed = 115200
```

Not supported on ESP32-S2. Equivalent to `build_flags = -DF_XTAL_MHZ=26`.
