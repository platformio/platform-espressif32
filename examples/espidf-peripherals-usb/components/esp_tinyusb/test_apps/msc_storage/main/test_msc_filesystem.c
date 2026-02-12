/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "soc/soc_caps.h"

#if SOC_USB_OTG_SUPPORTED
//
#include <stdio.h>
#include <string.h>
//
#include "unity.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
//
#include "device_common.h"
#include "storage_common.h"
#include "test_msc_common.h"


/**
 * @brief Test case for verifying filesystem access when storage is mounted to both APP and USB
 *
 * Goal is to verify that filesystem available:
 * - via filesystem API in APP
 * - via USB MSC class to the Host
 */
TEST_CASE("MSC: Verify filesystem access APP and USB", "[storage][fs]")
{
    tinyusb_msc_driver_config_t driver_cfg = {
        .callback = test_storage_event_cb,  // Test callback
        .callback_arg = NULL,               // No additional argument for the callback
    };

    // Install the MSC driver
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_install_driver(&driver_cfg), "Failed to install TinyUSB MSC driver");

    // Create a storage on SPI Flash
    wl_handle_t wl_handle = WL_INVALID_HANDLE;
    storage_init_spiflash(&wl_handle);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(WL_INVALID_HANDLE, wl_handle, "Wear leveling handle is invalid, check the partition configuration");

    tinyusb_msc_storage_handle_t storage_hdl;
    tinyusb_msc_storage_config_t storage_cfg = {
        .medium = {
            .wl_handle = wl_handle,
        },
        .fat_fs = {
            .config = {
                .max_files = 4,
            },
            .do_not_format = false,
            .format_flags = FM_ANY,
            .base_path = "/msc_test", // Custom mount path
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP, // Initially mount to APP
    };
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_new_storage_spiflash(&storage_cfg, &storage_hdl), "Failed to create new MSC storage on SPI Flash");
    // Wait for the storage to be mounted
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);

    // As the filesystem on storage is mounted to APP, we can access it via VFS
    const char *file_path = "/msc_test/hello.txt";
    FILE *f = fopen(file_path, "w");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "Failed to open file for writing on MSC storage");
    const char *text = "Hello, TinyUSB MSC!";
    size_t written = fwrite(text, 1, strlen(text), f);
    TEST_ASSERT_EQUAL_MESSAGE(strlen(text), written, "Failed to write the complete text to the file");
    fclose(f);

    // Reopen the file for reading
    f = fopen(file_path, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "Failed to open file for reading on MSC storage");
    char read_buf[32] = {0};
    size_t read = fread(read_buf, 1, sizeof(read_buf) - 1, f);
    TEST_ASSERT_EQUAL_MESSAGE(written, read, "Failed to read the complete text from the file");
    fclose(f);

    // // Verify the read content
    TEST_ASSERT_EQUAL_STRING_MESSAGE(text, read_buf, "Read content does not match written content");

    // Mount the storage to USB
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_set_storage_mount_point(storage_hdl, TINYUSB_MSC_STORAGE_MOUNT_USB), "Failed to set storage mount point to USB");
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);

    // As the filesystem on storage is mounted to USB, we cannot access it via VFS
    f = fopen(file_path, "r");
    TEST_ASSERT_NULL_MESSAGE(f, "File should not be accessible when storage is mounted to USB");

    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));

    test_device_wait();

    vTaskDelay(pdMS_TO_TICKS(TEST_DEVICE_PRESENCE_TIMEOUT_MS));

    // Uninstall TinyUSB driver
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    // Cleanup
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_delete_storage(storage_hdl), "Failed to delete TinyUSB MSC storage");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_uninstall_driver(), "Failed to uninstall TinyUSB MSC driver");
    storage_deinit_spiflash(wl_handle);
}

#endif // SOC_USB_OTG_SUPPORTED
