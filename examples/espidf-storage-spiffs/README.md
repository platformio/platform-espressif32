How to build PlatformIO based project
=====================================

1. [Install PlatformIO Core](http://docs.platformio.org/page/core.html)
2. Download [development platform with examples](https://github.com/platformio/platform-espressif32/archive/develop.zip)
3. Extract ZIP archive
4. Run these commands:

```shell
# Change directory to example
$ cd platform-espressif32/examples/espidf-storage-spiffs

# Build project
$ pio run

# Upload firmware
$ pio run --target upload

# Upload SPIFFS image
$ pio run --target uploadfs

# Clean build files
$ pio run --target clean
```
