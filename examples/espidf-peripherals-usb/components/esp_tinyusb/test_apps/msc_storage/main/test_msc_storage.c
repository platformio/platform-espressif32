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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
//
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
//
#include "unity.h"
#include "device_common.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "storage_common.h"
//
#include "test_msc_common.h"

//
// ========================== TinyUSB MSC Storage Initialization Tests =============================
//

/**
 * @brief Test case for checking the consistency of the public API functions
 *
 * This test case verifies that all public API functions are defined and have the expected signatures.
 */
TEST_CASE("MSC: Public API consistency", "[ci][driver]")
{
    // Check that the public API functions are consistent with the expected signatures
    TEST_ASSERT_NOT_NULL(tinyusb_msc_install_driver);
    TEST_ASSERT_NOT_NULL(tinyusb_msc_uninstall_driver);
    TEST_ASSERT_NOT_NULL(tinyusb_msc_new_storage_spiflash);
#if (SOC_SDMMC_HOST_SUPPORTED)
    TEST_ASSERT_NOT_NULL(tinyusb_msc_new_storage_sdmmc);
#endif // SOC_SDMMC_HOST_SUPPORTED
    TEST_ASSERT_NOT_NULL(tinyusb_msc_delete_storage);
    TEST_ASSERT_NOT_NULL(tinyusb_msc_set_storage_callback);
    TEST_ASSERT_NOT_NULL(tinyusb_msc_set_storage_mount_point);
    TEST_ASSERT_NOT_NULL(tinyusb_msc_config_storage_fat_fs);
    TEST_ASSERT_NOT_NULL(tinyusb_msc_format_storage);
    TEST_ASSERT_NOT_NULL(tinyusb_msc_get_storage_capacity);
    TEST_ASSERT_NOT_NULL(tinyusb_msc_get_storage_sector_size);
    TEST_ASSERT_NOT_NULL(tinyusb_msc_get_storage_mount_point);

    // Functions signatures should match the expected ones and do not fall during compilation
    // Driver
    tinyusb_msc_driver_config_t config = { 0 };
    tinyusb_msc_install_driver(&config);
    tinyusb_msc_uninstall_driver();

    // Storage
    tinyusb_msc_storage_handle_t storage_hdl = NULL;
    tinyusb_msc_storage_config_t storage_config = { 0 };
    tinyusb_msc_new_storage_spiflash(&storage_config, &storage_hdl);
#if (SOC_SDMMC_HOST_SUPPORTED)
    tinyusb_msc_new_storage_sdmmc(&storage_config, &storage_hdl);
#endif // SOC_SDMMC_HOST_SUPPORTED
    tinyusb_msc_delete_storage(storage_hdl);

    // Setters, Getters & Config
    tinyusb_msc_set_storage_callback(test_storage_event_cb, NULL);
    tinyusb_msc_set_storage_mount_point(storage_hdl, TINYUSB_MSC_STORAGE_MOUNT_USB);
    tinyusb_msc_config_storage_fat_fs(storage_hdl, NULL);
    tinyusb_msc_format_storage(storage_hdl);
    uint32_t sector_count = 0;
    tinyusb_msc_get_storage_capacity(storage_hdl, &sector_count);
    uint32_t sector_size = 0;
    tinyusb_msc_get_storage_sector_size(storage_hdl, &sector_size);
    tinyusb_msc_mount_point_t mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB;
    tinyusb_msc_get_storage_mount_point(storage_hdl, &mount_point);
}

/**
 * @brief Test case for installing TinyUSB MSC driver without storage
 *
 * Scenario:
 * 1. Install TinyUSB MSC driver without storage.
 * 2. Install TinyUSB driver with the MSC configuration.
 * 3. Wait for the device to be recognized.
 * 4. Uninstall TinyUSB driver and deinitialize the MSC driver.
 */
TEST_CASE("MSC: driver install & uninstall without storage", "[ci][driver]")
{
    tinyusb_msc_driver_config_t driver_cfg = {
        .callback = NULL,                           // Register the callback for mount changed events
        .callback_arg = NULL,                       // No additional argument for the callback
    };
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_install_driver(&driver_cfg), "Failed to install TinyUSB MSC driver");

    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();

    // No storage events without storage

    vTaskDelay(pdMS_TO_TICKS(TEST_DEVICE_PRESENCE_TIMEOUT_MS)); // Allow some time for the device to be recognized

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_uninstall_driver(), "Failed to uninstall TinyUSB MSC driver");
}

/**
 * @brief Test case for installing TinyUSB MSC storage without installing the driver
 *
 * Scenario:
 * 1. Init SPI Flash storage to obtain the wear leveling handle.
 * 2. Create TinyUSB MSC Storage with SPI Flash.
 * 3. Wait for the device to be recognized.
 * 4. Uninstall TinyUSB driver, delete storage and cleanup test.
 */
TEST_CASE("MSC: enable storage without driver install", "[ci][driver]")
{
    wl_handle_t wl_handle = WL_INVALID_HANDLE;
    storage_init_spiflash(&wl_handle);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(WL_INVALID_HANDLE, wl_handle, "Wear leveling handle is invalid, check the partition configuration");

    tinyusb_msc_storage_config_t config = {
        .medium.wl_handle = wl_handle,                      // Set the context to the wear leveling handle
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,       // Initial mount point to USB
        .fat_fs = {
            .base_path = NULL,                              // Use default base path
            .config.max_files = 5,                          // Maximum number of files that can be opened simultaneously
            .format_flags = 0,                              // No special format flags
        },
    };

    // Initialize TinyUSB MSC storage
    tinyusb_msc_storage_handle_t storage_hdl = NULL;
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_new_storage_spiflash(&config, &storage_hdl), "Failed to initialize TinyUSB MSC storage with SPIFLASH");
    // Configure the callback for mount changed events
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_set_storage_callback(test_storage_event_cb, NULL), "Failed to set storage event callback");

    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    vTaskDelay(pdMS_TO_TICKS(TEST_DEVICE_PRESENCE_TIMEOUT_MS)); // Allow some time for the device to be recognized

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_delete_storage(storage_hdl), "Failed to delete TinyUSB MSC storage");
    storage_deinit_spiflash(wl_handle);
}

/**
 * @brief Test case for initializing TinyUSB MSC storage with SPIFLASH
 *
 * Scenario:
 * 1. Create a queue for storage events.
 * 2. Initialize SPIFLASH storage with wear levelling.
 * 3. Configure TinyUSB MSC with the SPIFLASH storage and mounting to APP.
 * 4. Install TinyUSB driver with the MSC configuration.
 * 5. Wait for the storage to be mounted, verify that re-mount callbacks are received.
 * 7. Uninstall TinyUSB driver and deinitialize SPIFLASH storage.
 * 8. Delete the storage event queue.
 */
TEST_CASE("MSC: storage SPI Flash", "[ci][storage][spiflash]")
{
    wl_handle_t wl_handle = WL_INVALID_HANDLE;
    storage_init_spiflash(&wl_handle);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(WL_INVALID_HANDLE, wl_handle, "Wear leveling handle is invalid, check the partition configuration");

    tinyusb_msc_driver_config_t driver_cfg = {
        .callback = test_storage_event_cb,                  // Register the callback for mount changed events
        .callback_arg = NULL,                               // No additional argument for the callback
    };
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_install_driver(&driver_cfg), "Failed to install TinyUSB MSC driver");

    tinyusb_msc_storage_config_t config = {
        .medium.wl_handle = wl_handle,                      // Set the context to the wear leveling handle
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,       // Initial mount point to APP
        .fat_fs = {
            .base_path = NULL,                              // Use default base path
            .config.max_files = 5,                          // Maximum number of files that can be opened simultaneously
            .format_flags = 0,                              // No special format flags
        },
    };

    // Initialize TinyUSB MSC storage
    tinyusb_msc_storage_handle_t storage_hdl = NULL;
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_new_storage_spiflash(&config, &storage_hdl), "Failed to initialize TinyUSB MSC storage with SPIFLASH");
    // Wait for the storage to be mounted
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);

    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);

    vTaskDelay(pdMS_TO_TICKS(TEST_DEVICE_PRESENCE_TIMEOUT_MS)); // Allow some time for the device to be recognized

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_delete_storage(storage_hdl), "Failed to delete TinyUSB MSC storage");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_uninstall_driver(), "Failed to uninstall TinyUSB MSC driver");
    storage_deinit_spiflash(wl_handle);
}

#if (SOC_SDMMC_HOST_SUPPORTED)
/**
 * @brief Test case for initializing TinyUSB MSC storage with SDMMC
 *
 * Scenario:
 * 1. Create a queue for storage events.
 * 2. Initialize SDMMC storage.
 * 3. Configure TinyUSB MSC with the SDMMC storage mounted to APP.
 * 4. Verify that mount callbacks are registered correctly.
 * 5. Install TinyUSB driver with the MSC configuration.
 * 6. Wait for the storage to be mounted, verify that re-mount callbacks are received.
 * 7. Uninstall TinyUSB driver and deinitialize SDMMC storage.
 * 8. Delete the storage event queue.
 */
TEST_CASE("MSC: storage SD/MMC", "[storage][sdmmc]")
{
    sdmmc_card_t *card = NULL;
    storage_init_sdmmc(&card);
    TEST_ASSERT_NOT_NULL_MESSAGE(card, "SD/MMC card handle is NULL, check the SDMMC configuration");

    tinyusb_msc_driver_config_t driver_cfg = {
        .callback = test_storage_event_cb,                  // Register the callback for mount changed events
        .callback_arg = NULL,                               // No additional argument for the callback
    };
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_install_driver(&driver_cfg), "Failed to install TinyUSB MSC driver");

    tinyusb_msc_storage_config_t config = {
        .medium.card = card,                                // Set the context to the SDMMC card handle
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,       // Initial mount point to APP
        .fat_fs = {
            .base_path = NULL,                              // Use default base path
            .config.max_files = 5,                          // Maximum number of files that can be opened simultaneously
            .format_flags = 0,                              // No special format flags
        },
    };

    // Initialize TinyUSB MSC storage
    tinyusb_msc_storage_handle_t storage_hdl = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_new_storage_sdmmc(&config, &storage_hdl));
    // Wait for the storage to be mounted
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);

    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);

    // Verify card capacity and sector size
    uint32_t capacity = 0;
    uint32_t sector_size = 0;

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_get_storage_capacity(storage_hdl, &capacity));
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_get_storage_sector_size(storage_hdl, &sector_size));

    TEST_ASSERT_EQUAL_MESSAGE(card->csd.capacity, capacity, "SDMMC card capacity does not match TinyUSB MSC storage sector count");
    TEST_ASSERT_EQUAL_MESSAGE(card->csd.sector_size, sector_size, "SDMMC card sector size does not match TinyUSB MSC storage sector size");

    vTaskDelay(pdMS_TO_TICKS(TEST_DEVICE_PRESENCE_TIMEOUT_MS)); // Allow some time for the device to be recognized

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_delete_storage(storage_hdl), "Failed to delete TinyUSB MSC storage");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_uninstall_driver(), "Failed to uninstall TinyUSB MSC driver");
    storage_deinit_sdmmc(card);
}

/**
 * @brief Test case for initializing TinyUSB MSC storage with SPIFLASH
 *
 * Scenario:
 * 1. Create a queue for storage events.
 * 2. Initialize SPIFLASH storage1 with wear levelling.
 * 3. Initialize SDMMC storage2 with SD/MMC.
 * 3. Configure TinyUSB MSC with the SPIFLASH storage and mounting to USB.
 * 4. Install TinyUSB driver with the MSC configuration.
 * 5. Wait for the storages to be mounted.
 * 7. Uninstall TinyUSB driver and deinitialize storages.
 * 8. Delete the storage event queue.
 */
TEST_CASE("MSC: dual storage SPIFLASH + SDMMC", "[storage][spiflash][sdmmc]")
{
    wl_handle_t wl_handle = WL_INVALID_HANDLE;
    storage_init_spiflash(&wl_handle);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(WL_INVALID_HANDLE, wl_handle, "Wear leveling handle1 is invalid, check the partition configuration");

    sdmmc_card_t *card = NULL;
    storage_init_sdmmc(&card);
    TEST_ASSERT_NOT_NULL_MESSAGE(card, "SDMMC card handle is NULL, check the SDMMC configuration");

    tinyusb_msc_driver_config_t driver_cfg = {
        .callback = test_storage_event_cb,                  // Register the callback for mount changed events
        .callback_arg = NULL,                               // No additional argument for the callback
    };
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_install_driver(&driver_cfg), "Failed to install TinyUSB MSC driver");

    tinyusb_msc_storage_config_t config = {
        .medium.wl_handle = wl_handle,                      // Set the context to the wear leveling handle
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,       // Initial mount point to APP
        .fat_fs = {
            .base_path = "/custom1",                              // Use default base path
            .config.max_files = 5,                          // Maximum number of files that can be opened simultaneously
            .format_flags = 0,                              // No special format flags
        },
    };

    // Initialize TinyUSB MSC storage1
    tinyusb_msc_storage_handle_t storage1_hdl = NULL;
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_new_storage_spiflash(&config, &storage1_hdl), "Failed to initialize TinyUSB MSC storage with SPIFLASH");
    // Wait for the storage to be mounted
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);


    // Initialize TinyUSB MSC storage2
    tinyusb_msc_storage_handle_t storage2_hdl = NULL;
    config.medium.card = card; // Change the medium to the SDMMC card handle
    config.fat_fs.base_path = "/custom2"; // Use a different base path for the second storage
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_new_storage_sdmmc(&config, &storage2_hdl), "Failed to initialize second TinyUSB MSC storage with SDMMC");
    // Wait for the storage to be mounted
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);

    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));

    test_device_wait();

    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);

    vTaskDelay(pdMS_TO_TICKS(TEST_DEVICE_PRESENCE_TIMEOUT_MS)); // Allow some time for the device to be recognized
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_delete_storage(storage2_hdl), "Failed to delete TinyUSB MSC storage2");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_delete_storage(storage1_hdl), "Failed to delete TinyUSB MSC storage1");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_uninstall_driver(), "Failed to uninstall TinyUSB MSC driver");
    storage_deinit_spiflash(wl_handle);
    storage_deinit_sdmmc(card);
}
#endif // SOC_SDMMC_HOST_SUPPORTED

/**
 * @brief Test case for initializing TinyUSB MSC storage with a specific base path
 *
 * Scenario:
 * 1. Create a queue for storage events.
 * 2. Initialize SPIFLASH storage with wear levelling.
 * 3. Configure TinyUSB MSC with the SPIFLASH storage and mounting to APP with a custom base path.
 * 4. Install TinyUSB driver with the MSC configuration.
 * 5. Wait for the storage to be mounted, verify that re-mount callbacks are received.
 * 6. Uninstall TinyUSB driver and deinitialize SPIFLASH storage.
 * 7. Delete the storage event queue.
 */
TEST_CASE("MSC: storage specific base path", "[ci][storage][spiflash]")
{
    wl_handle_t wl_handle = WL_INVALID_HANDLE;
    storage_init_spiflash(&wl_handle);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(WL_INVALID_HANDLE, wl_handle, "Wear leveling handle is invalid, check the partition configuration");

    tinyusb_msc_driver_config_t driver_cfg = {
        .callback = test_storage_event_cb,                  // Register the callback for mount changed events
        .callback_arg = NULL,                               // No additional argument for the callback
    };
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_install_driver(&driver_cfg), "Failed to install TinyUSB MSC driver");

    tinyusb_msc_storage_config_t config = {
        .medium.wl_handle = wl_handle,                      // Set the context to the wear leveling handle
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,       // Initial mount point to APP
        .fat_fs = {
            .base_path = "/custom1",                        // Use specific base path
            .config.max_files = 5,                          // Maximum number of files that can be opened simultaneously
            .format_flags = 0,                              // No special format flags
        },
    };

    // Initialize TinyUSB MSC storage
    tinyusb_msc_storage_handle_t storage_hdl = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_new_storage_spiflash(&config, &storage_hdl));
    // Wait for the storage to be mounted
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);

    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);

    vTaskDelay(pdMS_TO_TICKS(TEST_DEVICE_PRESENCE_TIMEOUT_MS)); // Allow some time for the device to be recognized

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_delete_storage(storage_hdl), "Failed to delete TinyUSB MSC storage");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_uninstall_driver(), "Failed to uninstall TinyUSB MSC driver");
    storage_deinit_spiflash(wl_handle);
}

/**
 * @brief Test case for initializing TinyUSB MSC storage with empty SPIFLASH
 *
 * Scenario:
 * 1. Create a queue for storage events.
 * 2. Erase the SPIFLASH storage.
 * 3. Initialize SPIFLASH storage with wear levelling.
 * 4. Configure TinyUSB MSC with the SPIFLASH storage and mounting to APP.
 * 5. Verify callback presence for mount START and COMPLETE events.
 * 6. Uninstall TinyUSB MSC driver and deinitialize SPIFLASH storage.
 * 7. Delete the storage event queue.
 */
TEST_CASE("MSC: auto format storage SPI Flash", "[ci][storage][spiflash]")
{
    storage_erase_spiflash();

    wl_handle_t wl_handle = WL_INVALID_HANDLE;
    storage_init_spiflash(&wl_handle);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(WL_INVALID_HANDLE, wl_handle, "Wear leveling handle is invalid, check the partition configuration");


    tinyusb_msc_driver_config_t driver_cfg = {
        .callback = test_storage_event_cb,                  // Register the callback for mount changed events
        .callback_arg = NULL,                               // No additional argument for the callback
    };
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_install_driver(&driver_cfg), "Failed to install TinyUSB MSC driver");

    tinyusb_msc_storage_config_t config = {
        .medium.wl_handle = wl_handle,                      // Set the context to the wear leveling handle
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,       // Initial mount point to APP
        .fat_fs = {
            .base_path = NULL,                              // Use default base path
            .config.max_files = 5,                          // Maximum number of files that can be opened simultaneously
            .format_flags = 0,                              // No special format flags
        },
    };

    // Initialize TinyUSB MSC storage
    tinyusb_msc_storage_handle_t storage_hdl = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_new_storage_spiflash(&config, &storage_hdl));
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);
    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);

    vTaskDelay(pdMS_TO_TICKS(TEST_DEVICE_PRESENCE_TIMEOUT_MS)); // Allow some time for the device to be recognized

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_delete_storage(storage_hdl), "Failed to delete TinyUSB MSC storage");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_uninstall_driver(), "Failed to uninstall TinyUSB MSC driver");
    storage_deinit_spiflash(wl_handle);
}

/**
 * @brief Test case for initializing TinyUSB MSC storage with SPIFLASH and do not format option when initial mount point is APP
 *
 * Scenario:
 * 1. Create a queue for storage events.
 * 2. Erase the SPIFLASH storage.
 * 3. Initialize SPIFLASH storage with wear levelling.
 * 4. Configure TinyUSB MSC with the SPIFLASH storage and mounting to APP with do not format option.
 * 5. Verify callback presence for mount START and FORMAT_REQUIRED events.
 * 6. Check that the storage is still mounted to APP.
 * 7. Format the storage.
 * 8. Install TinyUSB driver.
 * 9. Wait for the storage to be mounted, verify that re-mount callbacks are received.
 * 10. Uninstall TinyUSB MSC driver and deinitialize SPIFLASH storage.
 * 11. Delete the storage event queue.
 */
TEST_CASE("MSC: format storage SPI Flash, mounted to APP", "[ci][storage][spiflash]")
{
    storage_erase_spiflash();

    wl_handle_t wl_handle = WL_INVALID_HANDLE;
    storage_init_spiflash(&wl_handle);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(WL_INVALID_HANDLE, wl_handle, "Wear leveling handle is invalid, check the partition configuration");


    tinyusb_msc_driver_config_t driver_cfg = {
        .callback = test_storage_event_cb,                  // Register the callback for mount changed events
        .callback_arg = NULL,                               // No additional argument for the callback
    };
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_install_driver(&driver_cfg), "Failed to install TinyUSB MSC driver");

    tinyusb_msc_storage_config_t config = {
        .medium.wl_handle = wl_handle,                      // Set the context to the wear leveling handle
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,       // Initial mount point to APP
        .fat_fs = {
            .base_path = NULL,                              // Use default base path
            .do_not_format = true,                          // Do not format the drive if filesystem is not present
            .config.max_files = 5,                          // Maximum number of files that can be opened simultaneously
            .format_flags = 0,                              // No special format flags
        },
    };

    // Initialize TinyUSB MSC storage
    tinyusb_msc_storage_handle_t storage_hdl = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_new_storage_spiflash(&config, &storage_hdl));
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_FORMAT_REQUIRED);
    // Mount wasn't completed, so the storage mount point is APP
    tinyusb_msc_mount_point_t mount_point;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_get_storage_mount_point(storage_hdl, &mount_point));
    TEST_ASSERT_EQUAL(TINYUSB_MSC_STORAGE_MOUNT_APP, mount_point);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_format_storage(storage_hdl));
    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);
    vTaskDelay(pdMS_TO_TICKS(TEST_DEVICE_PRESENCE_TIMEOUT_MS)); // Allow some time for the device to be recognized

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_delete_storage(storage_hdl), "Failed to delete TinyUSB MSC storage");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_uninstall_driver(), "Failed to uninstall TinyUSB MSC driver");
    storage_deinit_spiflash(wl_handle);
}

/**
 * @brief Test case for initializing TinyUSB MSC storage with SPIFLASH and do not format option when initial mount point is USB
 *
 * Scenario:
 * 1. Create a queue for storage events.
 * 2. Erase the SPIFLASH storage.
 * 3. Initialize SPIFLASH storage with wear levelling.
 * 4. Configure TinyUSB MSC with the SPIFLASH storage and mounting to USB with do not format option.
 * 6. Check that the storage is still mounted to USB.
 * 7. Install TinyUSB driver.
 * 8. Change the mount point to APP.
 * 9. Verify callback presence for mount START and FORMAT_REQUIRED events.
 * 10. Check that the storage is still mounted to USB.
 * 11. Uninstall TinyUSB MSC driver and deinitialize SPIFLASH storage.
 * 12. Delete the storage event queue.
 */
TEST_CASE("MSC: format storage SPI Flash, mounted to USB", "[ci][storage][spiflash]")
{
    storage_erase_spiflash();

    wl_handle_t wl_handle = WL_INVALID_HANDLE;
    storage_init_spiflash(&wl_handle);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(WL_INVALID_HANDLE, wl_handle, "Wear leveling handle is invalid, check the partition configuration");


    tinyusb_msc_driver_config_t driver_cfg = {
        .callback = test_storage_event_cb,                  // Register the callback for mount changed events
        .callback_arg = NULL,                               // No additional argument for the callback
    };
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_install_driver(&driver_cfg), "Failed to install TinyUSB MSC driver");

    tinyusb_msc_storage_config_t config = {
        .medium.wl_handle = wl_handle,                      // Set the context to the wear leveling handle
        .fat_fs = {
            .base_path = NULL,                              // Use default base path
            .do_not_format = true,                          // Do not format the drive if filesystem is not present
            .config.max_files = 5,                          // Maximum number of files that can be opened simultaneously
            .format_flags = 0,                              // No special format flags
        },
    };

    // Initialize TinyUSB MSC storage
    tinyusb_msc_storage_handle_t storage_hdl = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_new_storage_spiflash(&config, &storage_hdl));
    // We don't get the mount start event, since the storage is not mounted to the APP
    // The storage should be mounted to USB
    tinyusb_msc_mount_point_t mount_point;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_get_storage_mount_point(storage_hdl, &mount_point));
    TEST_ASSERT_EQUAL(TINYUSB_MSC_STORAGE_MOUNT_USB, mount_point);
    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    // We don't expect to receive the mount start event again, since the storage was exposed to the USB
    vTaskDelay(pdMS_TO_TICKS(TEST_DEVICE_PRESENCE_TIMEOUT_MS)); // Allow some time for the device to be recognized
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    // Remount the storage to APP
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_set_storage_mount_point(storage_hdl, TINYUSB_MSC_STORAGE_MOUNT_APP));
    // Wait for the storage to be mounted
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_FORMAT_REQUIRED);
    // Mount was completed with ESP_OK, so the storage mount point should be APP now
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_get_storage_mount_point(storage_hdl, &mount_point));
    TEST_ASSERT_EQUAL_MESSAGE(TINYUSB_MSC_STORAGE_MOUNT_APP, mount_point, "Storage mount point is not APP after the format");
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_format_storage(storage_hdl));
    // Remount the storage to USB once again
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_set_storage_mount_point(storage_hdl, TINYUSB_MSC_STORAGE_MOUNT_USB));
    // Wait for the storage to be mounted
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);
    // Check the mount point again
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_get_storage_mount_point(storage_hdl, &mount_point));
    TEST_ASSERT_EQUAL_MESSAGE(TINYUSB_MSC_STORAGE_MOUNT_USB, mount_point, "Storage mount point is not USB");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_delete_storage(storage_hdl), "Failed to delete TinyUSB MSC storage");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_uninstall_driver(), "Failed to uninstall TinyUSB MSC driver");
    storage_deinit_spiflash(wl_handle);
}

#if (SOC_SDMMC_HOST_SUPPORTED)
/**
 * @brief Test case for initializing TinyUSB MSC storage with SD/MMC and do not format option when initial mount point is APP
 *
 * Scenario:
 * 1. Create a queue for storage events.
 * 2. Initialize SD/MMC storage.
 * 3. Erase the SD/MMC storage.
 * 4. Configure TinyUSB MSC with the SD/MMC storage and mounting to APP with do not format option.
 * 5. Verify callback presence for mount START and FORMAT_REQUIRED events.
 * 6. Uninstall TinyUSB MSC driver and deinitialize SD/MMC storage.
 * 7. Delete the storage event queue.
 */
TEST_CASE("MSC: format storage SD/MMC, mounted to APP", "[storage][sdmmc]")
{
    sdmmc_card_t *card = NULL;
    storage_init_sdmmc(&card);
    TEST_ASSERT_NOT_NULL_MESSAGE(card, "SD/MMC card handle is NULL, check the SDMMC configuration");
    storage_erase_sdmmc(card);

    tinyusb_msc_driver_config_t driver_cfg = {
        .callback = test_storage_event_cb,                  // Register the callback for mount changed events
        .callback_arg = NULL,                               // No additional argument for the callback
    };
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_install_driver(&driver_cfg), "Failed to install TinyUSB MSC driver");

    tinyusb_msc_storage_config_t config = {
        .medium.card = card,                                // Set the context to the SDMMC card handle
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,       // Initial mount point to APP
        .fat_fs = {
            .base_path = NULL,                              // Use default base path
            .config.max_files = 5,                          // Maximum number of files that can be opened simultaneously
            .format_flags = 0,                              // No special format flags
            .do_not_format = true,                          // Do not format the drive if filesystem is not present
        },
    };

    // Initialize TinyUSB MSC storage
    tinyusb_msc_storage_handle_t storage_hdl = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_new_storage_sdmmc(&config, &storage_hdl));
    // Wait for the storage to be mounted
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_FORMAT_REQUIRED);
    // Mount was completed with ESP_OK, so the storage mount point should be APP now
    tinyusb_msc_mount_point_t mount_point;
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_get_storage_mount_point(storage_hdl, &mount_point));
    TEST_ASSERT_EQUAL(TINYUSB_MSC_STORAGE_MOUNT_APP, mount_point);
    // Format the storage
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_msc_format_storage(storage_hdl));
    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    // Wait for the storage to be mounted
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_START);
    test_storage_event_wait_callback(TINYUSB_MSC_EVENT_MOUNT_COMPLETE);

    vTaskDelay(pdMS_TO_TICKS(TEST_DEVICE_PRESENCE_TIMEOUT_MS)); // Allow some time for the device to be recognized

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_delete_storage(storage_hdl), "Failed to delete TinyUSB MSC storage");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_uninstall_driver(), "Failed to uninstall TinyUSB MSC driver");
    storage_deinit_sdmmc(card);
}
#endif // SOC_SDMMC_HOST_SUPPORTED

#endif // SOC_USB_OTG_SUPPORTED
