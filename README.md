[![Build_special_firmware](https://raw.githubusercontent.com/vshymanskyy/StandWithUkraine/main/banner-direct.svg)](https://github.com/vshymanskyy/StandWithUkraine/blob/main/docs/README.md)


# Tasmota Espressif 32: development platform for [PlatformIO](http://platformio.org)

[![Examples](https://github.com/Jason2866/platform-espressif32/actions/workflows/examples.yml/badge.svg)](https://github.com/Jason2866/platform-espressif32/actions/workflows/examples.yml)[![GitHub Releases](https://img.shields.io/github/downloads/tasmota/platform-espressif32/total?label=downloads)](https://github.com/tasmota/platform-espressif32/releases/latest)

Espressif Systems is a privately held fabless semiconductor company. They provide wireless communications chips which are widely used.

* [Home](http://platformio.org/platforms/espressif32) (home page in PlatformIO Platform Registry)
* [Documentation](http://docs.platformio.org/page/platforms/espressif32.html) (advanced usage, packages, boards, frameworks, etc.)

# Usage

1. [Install PlatformIO](http://platformio.org)
2. Create PlatformIO project and configure a platform option in [platformio.ini](http://docs.platformio.org/page/projectconf.html) file:

### Development build Arduino 3.1.3+ and IDF 5.3.3+ (build from development branches)
Support for the ESP32/ESP32solo1, ESP32C2, ESP32C3, ESP32C6, ESP32S2, ESP32S3 and ESP32-H2
```                  
[platformio]
platform = https://github.com/Jason2866/platform-espressif32.git#Arduino/IDF53
framework = arduino
```
for ESP32 Solo1
```
[env:esp32solo1]
board = esp32-solo1
```
The released frameworks can be downloaded [here](https://github.com/tasmota/arduino-esp32/releases)

# Configuration

Please navigate to [documentation](http://docs.platformio.org/page/platforms/espressif32.html).
