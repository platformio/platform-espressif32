/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <string.h>
#include "stdint.h"
#include "esp_err.h"
#include "device/usbd_pvt.h"
#include "diskio_impl.h"
#include "vfs_fat_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enumeration for Storage types.
 *
 * This enumeration defines the possible storage types for the TinyUSB MSC storage.
 */
typedef enum {
    STORAGE_MEDIUM_TYPE_SPIFLASH = 0, /*!< Storage type is SPI flash with wear leveling. */
    STORAGE_MEDIUM_TYPE_SDMMC,        /*!< Storage type is SDMMC card. */
} storage_medium_type_t;

/**
 * @brief Storage information structure
 *
 * This structure contains information about the storage medium, including the total number of sectors
 * and the size of each sector.
 */
typedef struct {
    uint32_t total_sectors;                     /*!< Total number of sectors in the storage medium. */
    uint32_t sector_size;                       /*!< Size of a single sector in bytes. */
} storage_info_t;

/**
 * @brief Storage medium structure
 *
 * This structure defines the function pointers for mounting, unmounting, reading, writing,
 * and getting information about the storage medium.
 */
typedef struct {
    const storage_medium_type_t type;                                                /*!< Type of the storage medium (SPI flash, SDMMC, etc.). */
    esp_err_t (*mount)(BYTE pdrv);                                                   /*!< Storage mount function pointer. */
    esp_err_t (*unmount)(void);                                                            /*!< Storage unmount function pointer. */
    esp_err_t (*read)(uint32_t lba, uint32_t offset, size_t size, void *dest);       /*!< Storage read function pointer. */
    esp_err_t (*write)(uint32_t lba, uint32_t offset, size_t size, const void *src); /*!< Storage write function pointer. */
    esp_err_t (*get_info)(storage_info_t *info);                                     /*!< Storage get information function pointer */
    void (*close)(void);                                                                        /*!< Storage close function pointer. */
} storage_medium_t;

/**
 * @brief Mount the storage to the application
 *
 * This function mounts the storage to the application, allowing it to access the storage medium.
 *
 * @note This function is for the tud_mount_cb() callback and should not be called directly.
 */
void msc_storage_mount_to_app(void);

/**
 * @brief Unmount the storage from the application
 *
 * This function unmounts the storage from the application, preventing further access to the storage medium.
 *
 * @note This function is for the tud_umount_cb() callback and should not be called directly.
 */
void msc_storage_mount_to_usb(void);

#ifdef __cplusplus
}
#endif
