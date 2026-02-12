## 2.1.0

- Added configurable Suspend/Resume device event support using TinyUSB callbacks `tud_suspend_cb` and `tud_resume_cb`

## 2.0.1~1

- esp_tinyusb: Claim forward compatibility with TinyUSB 0.19
- CDC: Added support for new VFS API (for esp-idf v5.4 and higher)

## 2.0.1

- esp_tinyusb: Added ESP32H4 support
- esp_tinyusb: Fixed an assertion failure on the GetOtherSpeedDescriptor() request for ESP32P4 when the OTG1.1 port is used
- MSC: Added dynamic member and storage operation multitask protection
- MSC: Used `esp_vfs_fat_register_cfg` function prototype for esp-idf v5.3 and higher

## 2.0.0

- esp_tinyusb: Added USB Compliance Verification results
- CDC-ACM: Added a configurable parameter for the endpoint DMA buffer

### Breaking changes

- esp_tinyusb: External PHY is no longer initialized automatically. If an external PHY is required, it must be explicitly initialized by the user with configuration parameter `phy.skip_setup = true`
- esp_tinyusb: Added run-time configuration for peripheral port selection, task settings, and descriptors. For more details, refer to the [Espressif's Addition to TinyUSB Mirgation guide v2](../../docs/device/migration-guides/v2/tinyusb.md)
- esp_tinyusb: Added USB Device event callback to handle different USB Device events. For the list of supported USB Device events, refer to to [Espressif's Addition to TinyUSB - README](../esp_tinyusb/README.md)
- esp_tinyusb: Removed configuration option to handle TinyUSB events outside of this driver
- NCM: Added possibility to deinit the driver
- NCM: Updated public API; refer to the [NCM Class Migration guide v2](../../docs/device/migration-guides/v2/tinyusb_ncm.md)
- MSC: Removed dedicated callbacks; introduced a single callback with an event ID for each storage
- MSC: Added storage format support
- MSC: Added dual storage support (SPI/Flash and SD/MMC)
- MSC: Updated public API; refer to the [MSC Class Migration guide v2](../../docs/device/migration-guides/v2/tinyusb_msc.md)
- Console: Updated public API; refer to the [Console Class Migration guide v2](../../docs/device/migration-guides/v2/tinyusb_console.md)
- CDC-ACM: Updated public API; refer to the [CDC-ACM Class Migration guide v2](../../docs/device/migration-guides/v2/tinyusb_cdc_acm.md)

## 1.7.6~1

- esp_tinyusb: Added documentation to README.md

## 1.7.6

- MSC: Fixed the possibility to use SD/MMC storage with large capacity (more than 4 GB)

## 1.7.5

- esp_tinyusb: Provide forward compatibility with IDF 6.0

## 1.7.4~1

- esp_tinyusb: Claim forward compatibility with IDF 6.0

## 1.7.4

- MSC: WL Sector runtime check during spiflash init (fix for build time error check)

## 1.7.3 [yanked]

- MSC: Improved transfer speed to SD cards and SPI flash

## 1.7.2

- esp_tinyusb: Fixed crash on logging from ISR
- PHY: Fixed crash with external_phy=true configuration

## 1.7.1

- NCM: Changed default NTB config to decrease DRAM memory usage (fix for DRAM overflow on ESP32S2)

## 1.7.0 [yanked]

- NCM: Added possibility to configure NCM Transfer Blocks (NTB) via menuconfig
- esp_tinyusb: Added option to select TinyUSB peripheral on esp32p4 via menuconfig (USB_PHY_SUPPORTS_P4_OTG11 in esp-idf is required)
- esp_tinyusb: Fixed uninstall tinyusb driver with not default task configuration

## 1.6.0

- CDC-ACM: Fixed memory leak on deinit
- esp_tinyusb: Added Teardown

## 1.5.0

- esp_tinyusb: Added DMA mode option to tinyusb DCD DWC2 configuration
- esp_tinyusb: Changed the default affinity mask of the task to CPU1

## 1.4.5

- CDC-ACM: Fixed memory leak at VFS unregister
- Vendor specific: Provided default configuration

## 1.4.4

- esp_tinyusb: Added HighSpeed and Qualifier device descriptors in tinyusb configuration
- CDC-ACM: Removed MIN() definition if already defined
- MSC: Fixed EP size selecting in default configuration descriptor

## 1.4.3

- esp_tinyusb: Added ESP32P4 support (HS only)

## 1.4.2

- MSC: Fixed maximum files open
- Added uninstall function

## 1.4.0

- MSC: Fixed integer overflows
- CDC-ACM: Removed intermediate RX ringbuffer
- CDC-ACM: Increased default FIFO size to 512 bytes
- CDC-ACM: Fixed Virtual File System binding

## 1.3.0

- Added NCM extension

## 1.2.1 - 1.2.2

- Minor bugfixes

## 1.2.0

- Added MSC extension for accessing SPI Flash on memory card https://github.com/espressif/idf-extra-components/commit/a8c00d7707ba4ceeb0970c023d702c7768dba3dc

## 1.1.0

- Added support for NCM, ECM/RNDIS, DFU and Bluetooth TinyUSB drivers https://github.com/espressif/idf-extra-components/commit/79f35c9b047b583080f93a63310e2ee7d82ef17b

## 1.0.4

- Cleaned up string descriptors handling https://github.com/espressif/idf-extra-components/commit/046cc4b02f524d5c7e3e56480a473cfe844dc3d6

## 1.0.2 - 1.0.3

- Minor bugfixes

## 1.0.1

- CDC-ACM: Return ESP_OK if there is nothing to flush https://github.com/espressif/idf-extra-components/commit/388ff32eb09aa572d98c54cb355f1912ce42707c

## 1.0.0

- Initial version based on [esp-idf v4.4.3](https://github.com/espressif/esp-idf/tree/v4.4.3/components/tinyusb)
