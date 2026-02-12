/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "tinyusb_msc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Types of MSC events
 * @deprecated Deprecated and may be removed in future releases.
 */
typedef enum {
    TINYUSB_MSC_EVENT_MOUNT_CHANGED,        /*!< Event type AFTER mount/unmount operation is successfully finished */
    TINYUSB_MSC_EVENT_PREMOUNT_CHANGED      /*!< Event type BEFORE mount/unmount operation is started */
} tinyusb_msc_event_type_t;

/**
 * @brief Configuration structure for spiflash initialization
 *
 * @deprecated Deprecated and may be removed in future releases.
 *
 * User configurable parameters that are used while
 * initializing the SPI Flash media.
 */
typedef struct {
    wl_handle_t wl_handle;                                  /*!< Pointer to spiflash wear-levelling handle */
    tusb_msc_callback_t callback_mount_changed;             /*!< Pointer to the function callback that will be delivered AFTER storage mount/unmount operation is successfully finished */
    tusb_msc_callback_t callback_premount_changed;          /*!< Pointer to the function callback that will be delivered BEFORE storage mount/unmount operation is started */
    tusb_msc_callback_t callback_device_mount_changed;      /*!< Pointer to the function callback that will be delivered when a device is unmounted, from tud_umount_cb()  */
    const esp_vfs_fat_mount_config_t mount_config;          /*!< FATFS mount config */
} tinyusb_msc_spiflash_config_t;

/**
 * @brief Register storage type SPI Flash with tinyusb driver
 *
 * @deprecated Deprecated and may be removed in future releases.
 *
 *
 * @param config pointer to the SPI Flash configuration
 * @return
 *    - ESP_OK: SPI Flash storage initialized successfully
 *    - ESP_ERR_NO_MEM: There was no memory to allocate storage components;
 */
esp_err_t tinyusb_msc_storage_init_spiflash(const tinyusb_msc_spiflash_config_t *config)
{
    return tinyusb_msc_new_storage_spiflash(&(tinyusb_msc_storage_config_t) {
        .medium.wl_handle = config->wl_handle,
        .fat_fs = {
            .base_path = NULL,
            .do_not_format = false,
            .config = config->mount_config,
        },
    }, NULL);
}

#if SOC_SDMMC_HOST_SUPPORTED
/**
 * @brief Configuration structure for sdmmc initialization
 *
 * @deprecated Deprecated and may be removed in future releases.
 *
 * User configurable parameters that are used while
 * initializing the sdmmc media.
 */
typedef struct {
    sdmmc_card_t *card;                                     /*!< Pointer to sdmmc card configuration structure */
    tusb_msc_callback_t callback_mount_changed;             /*!< Pointer to the function callback that will be delivered AFTER storage mount/unmount operation is successfully finished */
    tusb_msc_callback_t callback_premount_changed;          /*!< Pointer to the function callback that will be delivered BEFORE storage mount/unmount operation is started */
    tusb_msc_callback_t callback_device_mount_changed;      /*!< Pointer to the function callback that will be delivered when a device mounted/unmounted */
    const esp_vfs_fat_mount_config_t mount_config;          /*!< FATFS mount config */
} tinyusb_msc_sdmmc_config_t;

/**
 * @brief Register storage type sd-card with tinyusb driver
 *
 * @deprecated Deprecated and may be removed in future releases.
 *
 * @param config pointer to the sd card configuration
 * @return
 *    - ESP_OK: SDMMC storage initialized successfully
 *    - ESP_ERR_NO_MEM: There was no memory to allocate storage components;
 */
esp_err_t tinyusb_msc_storage_init_sdmmc(const tinyusb_msc_sdmmc_config_t *config)
{
    return tinyusb_msc_new_storage_sdmmc(&(tinyusb_msc_storage_config_t) {
        .medium.card = config->card,
        .fat_fs = {
            .base_path = NULL,
            .do_not_format = false,
            .config = config->mount_config,
        },
    }, NULL);
}
#endif

/**
 * @brief Deinitialize TinyUSB MSC storage
 *
 * This function deinitializes the TinyUSB MSC storage interface.
 * It releases any resources allocated during initialization.
 *
 * @deprecated Deprecated and may be removed in future releases.
 * Please use the recommended alternative tinyusb_msc_storage_delete().
 */
void tinyusb_msc_storage_deinit(void)
{
    tinyusb_msc_delete_storage(NULL);
}

/**
 * @brief Register a callback invoking on MSC event. If the callback had been
 *        already registered, it will be overwritten
 *
 * @deprecated Deprecated and may be removed in future releases.
 * Please, update the callback function and use the recommended alternative tinyusb_msc_set_storage_callback().
 *
 * @param event_type - type of registered event for a callback
 * @param callback  - callback function
 * @return esp_err_t - ESP_OK or ESP_ERR_INVALID_ARG
 */
esp_err_t tinyusb_msc_register_callback(tinyusb_msc_event_type_t event_type,
                                        tusb_msc_callback_t callback)
{
    return ESP_OK; // This function is deprecated and does nothing
}

/**
 * @brief Unregister a callback invoking on MSC event.
 *
 * @deprecated Deprecated and may be removed in future releases.
 *
 * @param event_type - type of registered event for a callback
 * @return esp_err_t - ESP_OK or ESP_ERR_INVALID_ARG
 */
esp_err_t tinyusb_msc_unregister_callback(tinyusb_msc_event_type_t event_type)
{
    return ESP_OK; // This function is deprecated and does nothing
}

/**
 * @brief Mount the storage partition locally on the firmware application.
 *
 * Get the available drive number. Register spi flash partition.
 * Connect POSIX and C standard library IO function with FATFS.
 * Mounts the partition.
 * This API is used by the firmware application. If the storage partition is
 * mounted by this API, host (PC) can't access the storage via MSC.
 * When this function is called from the tinyusb callback functions, care must be taken
 * so as to make sure that user callbacks must be completed within a
 * specific time. Otherwise, MSC device may re-appear again on Host.
 *
 * @deprecated Deprecated and may be removed in future releases.
 * Please use the recommended alternative tinyusb_msc_set_storage_mount_point().
 *
 * @param base_path  path prefix where FATFS should be registered
 * @return esp_err_t
 *       - ESP_OK, if success;
 *       - ESP_ERR_NOT_FOUND if the maximum count of volumes is already mounted
 *       - ESP_ERR_NO_MEM if not enough memory or too many VFSes already registered;
 */
esp_err_t tinyusb_msc_storage_mount(const char *base_path)
{
    tinyusb_msc_config_storage_fat_fs(NULL, &(tinyusb_msc_fatfs_config_t) {
        .base_path = (char *)base_path,
        .config = {
            .max_files = 2,                  // Default max files
            .format_if_mount_failed = false, // Do not format if mount fails
        },
        .do_not_format = false,              // Allow formatting
        .format_flags = FM_ANY,              // Auto-select FAT type based on volume size
    });
    return tinyusb_msc_set_storage_mount_point(NULL, TINYUSB_MSC_STORAGE_MOUNT_APP);
}

/**
 * @brief Get number of sectors in storage media
 *
 * @deprecated Deprecated and may be removed in future releases.
 * Please use the recommended alternative tinyusb_msc_get_storage_capacity().
 *
 * @return usable size, in bytes
 */
uint32_t tinyusb_msc_storage_get_sector_count(void)
{
    uint32_t capacity = 0;
    tinyusb_msc_get_storage_capacity(NULL, &capacity);
    return capacity;
}

/**
 * @brief Get sector size of storage media
 *
 * @deprecated Deprecated and may be removed in future releases.
 * Please use the recommended alternative tinyusb_msc_storage_get_sector_size().
 *
 * @return sector count
 */
uint32_t tinyusb_msc_storage_get_sector_size(void)
{
    uint32_t sector_size = 0;
    tinyusb_msc_get_storage_sector_size(NULL, &sector_size);
    return sector_size;
}

/**
 * @brief Unmount the storage partition from the firmware application.
 *
 * Unmount the partition. Unregister diskio driver.
 * Unregister the SPI flash partition.
 * Finally, Un-register FATFS from VFS.
 * After this function is called, storage device can be seen (recognized) by host (PC).
 * When this function is called from the tinyusb callback functions, care must be taken
 * so as to make sure that user callbacks must be completed within a specific time.
 * Otherwise, MSC device may not appear on Host.
 *
 * @deprecated Deprecated and may be removed in future releases.
 * Please use the recommended alternative tinyusb_msc_set_storage_mount_point().
 *
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if FATFS is not registered in VFS
 */
esp_err_t tinyusb_msc_storage_unmount(void)
{
    return tinyusb_msc_set_storage_mount_point(NULL, TINYUSB_MSC_STORAGE_MOUNT_USB);
}

/**
 * @brief Get status if storage media is exposed over USB to Host
 *
 * This function checks if the storage media is currently mounted on the application or exposed to the Host.
 *
 * @deprecated Deprecated and may be removed in future releases.
 * Please use the recommended alternative tinyusb_msc_storage_get_mount_point().
 *
 * @return bool
 *      - true, if the storage media is exposed to Host
 *      - false, if the stoarge media is mounted on application (not exposed to Host)
 */
bool tinyusb_msc_storage_in_use_by_usb_host(void)
{
    bool exposed_to_host = false;
    tinyusb_msc_mount_point_t mp;
    if (tinyusb_msc_get_storage_mount_point(NULL, &mp) == ESP_OK) {
        exposed_to_host = (mp == TINYUSB_MSC_STORAGE_MOUNT_USB);
    }
    return exposed_to_host;
}

#ifdef __cplusplus
}
#endif
