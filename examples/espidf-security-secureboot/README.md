How to build PlatformIO based project
=====================================

1. [Install PlatformIO Core](https://docs.platformio.org/page/core.html)
2. Download [development platform with examples](https://github.com/platformio/platform-espressif32/archive/develop.zip)
3. Extract ZIP archive
4. Follow the instructions on how to enable [Secure Boot](https://docs.platformio.org/en/latest/frameworks/espidf.html#secure-bootloader) 
5. Run these commands:

```shell
# Change directory to example
$ cd platform-espressif32/examples/espidf-security-secureboot

# Build project
$ pio run

# Upload firmware
$ pio run --target upload
```