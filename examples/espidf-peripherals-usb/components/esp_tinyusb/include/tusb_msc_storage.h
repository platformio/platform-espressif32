/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "esp_err.h"
#include "wear_levelling.h"
#include "esp_vfs_fat.h"
#if SOC_SDMMC_HOST_SUPPORTED
#include "driver/sdmmc_host.h"
#endif

/**
 * @brief Data provided to the input of the `callback_mount_changed` and `callback_premount_changed` callback
 */
typedef struct {
    bool is_mounted;                        /*!< Flag if storage is mounted or not */
} tinyusb_msc_event_mount_changed_data_t;

/**
 * @brief Types of MSC events
 */
typedef enum {
    TINYUSB_MSC_EVENT_MOUNT_CHANGED,        /*!< Event type AFTER mount/unmount operation is successfully finished */
    TINYUSB_MSC_EVENT_PREMOUNT_CHANGED      /*!< Event type BEFORE mount/unmount operation is started */
} tinyusb_msc_event_type_t;

/**
 * @brief Describes an event passing to the input of a callbacks
 */
typedef struct {
    tinyusb_msc_event_type_t type; /*!< Event type */
    union {
        tinyusb_msc_event_mount_changed_data_t mount_changed_data; /*!< Data input of the callback */
    };
} tinyusb_msc_event_t;

/**
 * @brief MSC callback that is delivered whenever a specific event occurs.
 */
typedef void(*tusb_msc_callback_t)(tinyusb_msc_event_t *event);

#if SOC_SDMMC_HOST_SUPPORTED
/**
 * @brief Configuration structure for sdmmc initialization
 *
 * User configurable parameters that are used while
 * initializing the sdmmc media.
 */
typedef struct {
    sdmmc_card_t *card;                             /*!< Pointer to sdmmc card configuration structure */
    tusb_msc_callback_t callback_mount_changed;     /*!< Pointer to the function callback that will be delivered AFTER mount/unmount operation is successfully finished */
    tusb_msc_callback_t callback_premount_changed;  /*!< Pointer to the function callback that will be delivered BEFORE mount/unmount operation is started */
    const esp_vfs_fat_mount_config_t mount_config; /*!< FATFS mount config */
} tinyusb_msc_sdmmc_config_t;
#endif

/**
 * @brief Configuration structure for spiflash initialization
 *
 * User configurable parameters that are used while
 * initializing the SPI Flash media.
 */
typedef struct {
    wl_handle_t wl_handle;                          /*!< Pointer to spiflash wera-levelling handle */
    tusb_msc_callback_t callback_mount_changed;     /*!< Pointer to the function callback that will be delivered AFTER mount/unmount operation is successfully finished */
    tusb_msc_callback_t callback_premount_changed;  /*!< Pointer to the function callback that will be delivered BEFORE mount/unmount operation is started */
    const esp_vfs_fat_mount_config_t mount_config; /*!< FATFS mount config */
} tinyusb_msc_spiflash_config_t;

/**
 * @brief Register storage type spiflash with tinyusb driver
 *
 * @param config pointer to the spiflash configuration
 * @return esp_err_t
 *       - ESP_OK, if success;
 *       - ESP_ERR_NO_MEM, if there was no memory to allocate storage components;
 */
esp_err_t tinyusb_msc_storage_init_spiflash(const tinyusb_msc_spiflash_config_t *config);

#if SOC_SDMMC_HOST_SUPPORTED
/**
 * @brief Register storage type sd-card with tinyusb driver
 *
 * @param config pointer to the sd card configuration
 * @return esp_err_t
 *       - ESP_OK, if success;
 *       - ESP_ERR_NO_MEM, if there was no memory to allocate storage components;
 */
esp_err_t tinyusb_msc_storage_init_sdmmc(const tinyusb_msc_sdmmc_config_t *config);
#endif
/**
 * @brief Deregister storage with tinyusb driver and frees the memory
 *
 */
void tinyusb_msc_storage_deinit(void);

/**
 * @brief Register a callback invoking on MSC event. If the callback had been
 *        already registered, it will be overwritten
 *
 * @param event_type - type of registered event for a callback
 * @param callback  - callback function
 * @return esp_err_t - ESP_OK or ESP_ERR_INVALID_ARG
 */
esp_err_t tinyusb_msc_register_callback(tinyusb_msc_event_type_t event_type,
                                        tusb_msc_callback_t callback);


/**
 * @brief Unregister a callback invoking on MSC event.
 *
 * @param event_type - type of registered event for a callback
 * @return esp_err_t - ESP_OK or ESP_ERR_INVALID_ARG
 */
esp_err_t tinyusb_msc_unregister_callback(tinyusb_msc_event_type_t event_type);

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
 * @param base_path  path prefix where FATFS should be registered
 * @return esp_err_t
 *       - ESP_OK, if success;
 *       - ESP_ERR_NOT_FOUND if the maximum count of volumes is already mounted
 *       - ESP_ERR_NO_MEM if not enough memory or too many VFSes already registered;
 */
esp_err_t tinyusb_msc_storage_mount(const char *base_path);

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
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if FATFS is not registered in VFS
 */
esp_err_t tinyusb_msc_storage_unmount(void);

/**
 * @brief Get number of sectors in storage media
 *
 * @return usable size, in bytes
 */
uint32_t tinyusb_msc_storage_get_sector_count(void);

/**
 * @brief Get sector size of storage media
 *
 * @return sector count
 */
uint32_t tinyusb_msc_storage_get_sector_size(void);

/**
 * @brief Get status if storage media is exposed over USB to Host
 *
 * @return bool
 *      - true, if the storage media is exposed to Host
 *      - false, if the stoarge media is mounted on application (not exposed to Host)
 */
bool tinyusb_msc_storage_in_use_by_usb_host(void);

#ifdef __cplusplus
}
#endif
