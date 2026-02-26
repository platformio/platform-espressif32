/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "soc/soc_caps.h"
#include "esp_err.h"
#include "wear_levelling.h"
#include "esp_vfs_fat.h"
#if (SOC_SDMMC_HOST_SUPPORTED)
#include "driver/sdmmc_host.h"
#endif // SOC_SDMMC_HOST_SUPPORTED

/**
 * @brief Handle for TinyUSB MSC storage
 */
typedef struct tinyusb_msc_storage_s *tinyusb_msc_storage_handle_t;

/**
 * @brief Storage mount point types
 */
typedef enum {
    TINYUSB_MSC_STORAGE_MOUNT_USB = 0,             /*!< Storage is exclusively used by USB host */
    TINYUSB_MSC_STORAGE_MOUNT_APP,                 /*!< Storage is used by the application */
} tinyusb_msc_mount_point_t;

/**
 * @brief MSC event IDs for mount and unmount operations
 *
 * These events are used to notify the application about the status of the storage mount or unmount operations.
 */
typedef enum {
    TINYUSB_MSC_EVENT_MOUNT_START,               /*!< Called BEFORE mounting or unmounting the filesystem */
    TINYUSB_MSC_EVENT_MOUNT_COMPLETE,            /*!< Called AFTER the mount or unmount operation is complete */
    TINYUSB_MSC_EVENT_MOUNT_FAILED,              /*!< Called if the mount operation failed */
    TINYUSB_MSC_EVENT_FORMAT_REQUIRED,           /*!< Called when the storage needs to be formatted */
    TINYUSB_MSC_EVENT_FORMAT_FAILED,             /*!< Called if the format operation failed */
} tinyusb_msc_event_id_t;

/**
 * @brief Describes an event passing to the input of a callbacks
 */
typedef struct {
    tinyusb_msc_event_id_t id;                      /*!< Event id */
    tinyusb_msc_mount_point_t mount_point;          /*!< Mount point type */
    union {
        struct {

        } event_data;                /*!< Placeholder for future event data, currently unused */
        // Deprecated in v2.0.0, could be removed in future releases
        struct {
            bool is_mounted;                                /*!< Flag indicating if the storage is mounted or not */
        } mount_changed_data __attribute__((deprecated));   /*!< Data for mount changed events */
    } ;
} tinyusb_msc_event_t;


typedef struct {
    char *base_path;                        /*!< Filesystem mount path.
                                            *   - If NULL, a default mount path from the Kconfig is used.
                                            */
    esp_vfs_fat_mount_config_t config;      /*!< FAT filesystem mount configuration.
                                                *   Controls auto-formatting, max open files, allocation unit size, etc.
                                                */
    bool do_not_format;                     /*!< If true, do not format the drive if filesystem is not present.
                                            *   - Default is false, meaning the drive will be formatted if no filesystem is found.
                                            */
    BYTE format_flags;                      /*!< Flags for FAT formatting.
                                            *   - A bitwise combination of:
                                            *       - FM_FAT (FAT12/16)
                                            *       - FM_FAT32
                                            *       - FM_EXFAT (ignored if exFAT not enabled)
                                            *       - FM_ANY (default; auto-select based on volume size)
                                            *       - FM_SFD (Single FAT partition)
                                            *   - Set to 0 to use the default (FM_ANY).
                                            */
} tinyusb_msc_fatfs_config_t;

/**
 * @brief MSC event callback function type
 *
 * This callback is invoked when a storage mount or unmount operation is initiated or completed.
 */
typedef void(*tusb_msc_callback_t)(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg);

/**
 * @brief Configuration structure for TinyUSB MSC (Mass Storage Class).
 */
typedef struct {
    union {
        wl_handle_t wl_handle;              /*!< Wear leveling handle for SPI Flash storage. */
#if (SOC_SDMMC_HOST_SUPPORTED)
        sdmmc_card_t *card;                 /*!< Pointer to the SD/MMC card structure. */
#endif // SOC_SDMMC_HOST_SUPPORTED
    } medium;                               /*!< Storage medium configuration.
                                             *   - For SPI Flash, this is a wear leveling handle.
                                             *   - For SD/MMC, this is a pointer to the sdmmc_card_t structure.
                                             */
    tinyusb_msc_fatfs_config_t fat_fs;      /*!< FAT filesystem configuration. */
    tinyusb_msc_mount_point_t mount_point;  /*!< Specifies who initially owns access to the storage:

                                             *   - TINYUSB_MSC_STORAGE_MOUNT_USB: USB host initially owns the storage (MSC mode).
                                             *   - TINYUSB_MSC_STORAGE_MOUNT_APP: Application code initially owns and accesses the storage.
                                             *
                                             *  This affects whether the filesystem is mounted for local use or exposed over USB on startup.
                                             *  Default value is TINYUSB_MSC_STORAGE_MOUNT_USB.
                                             */
} tinyusb_msc_storage_config_t;

typedef struct {
    union {
        struct {
            uint16_t auto_mount_off: 1;
            uint16_t reserved15: 15;
        };
        uint16_t val;
    } user_flags;                           /*!< Configuration flags for the MSC driver.
                                             *   - auto_mount_off: If true, filesystem will not be automatically re-mounted when device connects to or disconnects from USB Host.
                                             *     This allows manual control over when the storage is exposed to the USB host.
                                             */
    tusb_msc_callback_t callback;           /*!< Callback function invoked on storage events.
                                             *   - Called before and after switching access to the storage between USB and App.
                                             *   - Called when a mount or unmount operation has not been completed.
                                             */

    void *callback_arg;                     /*!< Argument passed to the user callback.
                                             *   - Can be used to pass user context or additional data.
                                             */
} tinyusb_msc_driver_config_t;

// ----------------------------- Driver API ---------------------------------

/**
 * @brief Install TinyUSB MSC driver
 *
 * This function initializes the TinyUSB MSC driver with the provided configuration.
 *
 * @param[in] config Pointer to the configuration structure for TinyUSB MSC driver
 * @return
 *   - ESP_OK: Installation successful
 *   - ESP_ERR_INVALID_STATE: Driver is already installed
 *   - ESP_ERR_INVALID_ARG: Invalid input argument
 *   - ESP_ERR_NO_MEM: Not enough memory to install the driver
 */
esp_err_t tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *config);

/**
 * @brief Uninstall TinyUSB MSC driver
 *
 * This function deinitializes the TinyUSB MSC driver and releases any resources allocated during initialization.
 *
 * @return
 *    - ESP_OK: Uninstallation successful
 *    - ESP_ERR_NOT_SUPPORTED: Driver is not installed
 *    - ESP_ERR_INVALID_STATE: Driver is in incorrect state: storage is still mounted
 */
esp_err_t tinyusb_msc_uninstall_driver(void);

/**
 * @brief Initialize TinyUSB MSC SPI Flash storage
 *
 * This function initializes the TinyUSB MSC storage interface with SPI Flash as a storage medium.
 *
 * @param[in] config Pointer to the configuration structure for TinyUSB MSC storage
 * @param[out] handle Pointer to the storage handle
 *
 * @return
 *    - ESP_OK: Initialization successful
 *    - ESP_ERR_INVALID_ARG: Invalid input argument
 *    - ESP_ERR_NOT_SUPPORTED: TinyUSB buffer size is less than the Wear Levelling sector size
 *    - ESP_ERR_NO_MEM: Not enough memory to initialize storage
 *    - ESP_FAIL: Failed to map storage to LUN or mount storage
 */
esp_err_t tinyusb_msc_new_storage_spiflash(const tinyusb_msc_storage_config_t *config, tinyusb_msc_storage_handle_t *handle);

#if (SOC_SDMMC_HOST_SUPPORTED)
/**
 * @brief Initialize TinyUSB MSC with SD/MMC storage
 *
 * This function initializes the TinyUSB MSC storage interface with SD/MMC as the storage medium.
 *
 * @param[in] config Pointer to the configuration structure for TinyUSB MSC storage with SD/MMC
 * @param[out] handle Pointer to the storage handle
 *
 * @return
 *    - ESP_OK: Initialization successful
 *    - ESP_ERR_INVALID_ARG: Invalid input argument
 *    - ESP_ERR_NO_MEM: Not enough memory to initialize storage
 *    - ESP_FAIL: Failed to map storage to LUN or mount storage
 */
esp_err_t tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *config, tinyusb_msc_storage_handle_t *handle);
#endif // SOC_SDMMC_HOST_SUPPORTED

/**
 * @brief Delete TinyUSB MSC Storage
 *
 * This function deinitializes the TinyUSB MSC storage interface.
 * It releases any resources allocated during initialization.
 *
 * @param[in] handle Storage handle, obtained during storage creation.
 *
 * @return
 *   - ESP_OK: Deletion successful
 *   - ESP_ERR_INVALID_STATE: Driver is not installed, no storage was created, or there are pending deferred writes
 *   - ESP_ERR_INVALID_ARG: Invalid input argument, handle is NULL
 *   - ESP_ERR_NOT_FOUND: Storage not found in any LUN
 */
esp_err_t tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t handle);

/**
 * @brief Set a callback function for MSC storage events
 *
 * This function allows the user to set a callback that will be invoked
 * when a storage event occurs, such as a mount or unmount operation.
 *
 * @param[in] callback Pointer to the callback function to be invoked on storage events
 * @param[in] arg Pointer to an argument that will be passed to the callback function
 *
 * @return
 *   - ESP_OK: Callback set successfully
 *   - ESP_ERR_INVALID_STATE: Driver is not installed
 *   - ESP_ERR_INVALID_ARG: Invalid input argument, callback is NULL
 */
esp_err_t tinyusb_msc_set_storage_callback(tusb_msc_callback_t callback, void *arg);

/**
 * @brief Format the storage
 *
 * This function formats the storage media with a FAT filesystem.
 *
 * @note This function should be called with caution, as it will erase all data on the storage media.
 * @note Could be called when only when the storage media is mounted to the application.
 *
 * @param[in] handle    Storage handle, obtained during storage creation.
 *
 * @return
 *   - ESP_OK: Storage formatted successfully
 *   - ESP_ERR_INVALID_STATE: MSC driver is not initialized or storage is not initialized
 *   - ESP_ERR_INVALID_ARG: Invalid input argument, handle is NULL
 *   - ESP_ERR_NOT_FOUND: Unexpected filesystem found on the drive
 */
esp_err_t tinyusb_msc_format_storage(tinyusb_msc_storage_handle_t handle);

/**
 * @brief Configure FAT filesystem parameters for the storage media
 *
 * This function sets the FAT filesystem parameters for the storage media which will be used while mounting and formatting.
 *
 * @param[in] handle Storage handle, obtained from creating the storage.
 * @param[in] fatfs_config Pointer to the FAT filesystem configuration structure.
 *
 * @return
 *   - ESP_OK: Base path set successfully
 *   - ESP_ERR_INVALID_ARG: Invalid input argument
 *   - ESP_ERR_INVALID_STATE: MSC driver is not initialized or storage is not initialized
 */
esp_err_t tinyusb_msc_config_storage_fat_fs(tinyusb_msc_storage_handle_t handle, tinyusb_msc_fatfs_config_t *fatfs_config);

/**
 * @brief Set the mount point for the storage media
 *
 * This function sets the mount point for the storage media, which determines whether the storage is exposed to the USB host or used by the application.
 *
 * @param[in] handle Storage handle, obtained during storage creation.
 * @param[in] mount_point The mount point to set, either TINYUSB_MSC_STORAGE_MOUNT_USB or TINYUSB_MSC_STORAGE_MOUNT_APP.
 *
 * @return
 *   - ESP_OK: Mount point set successfully
 *   - ESP_ERR_INVALID_STATE: Driver is not installed or storage wasn't created
 */
esp_err_t tinyusb_msc_set_storage_mount_point(tinyusb_msc_storage_handle_t handle,
                                              tinyusb_msc_mount_point_t mount_point);

// ------------------------------------ Getters ------------------------------------

/**
 * @brief Get storage capacity in sectors
 *
 * @param[in] handle Storage handle, obtained during storage creation.
 * @param[out] sector_count Pointer to store the number of sectors in the storage media.
 *
 * @return
 *    - ESP_OK: Sector count retrieved successfully
 *    - ESP_ERR_INVALID_ARG: Invalid input argument, sector_count pointer is NULL
 *    - ESP_ERR_INVALID_STATE: MSC driver is not initialized or storage is not initialized
 */
esp_err_t tinyusb_msc_get_storage_capacity(tinyusb_msc_storage_handle_t handle, uint32_t *sector_count);

/**
 * @brief Get sector size of storage
 *
 * @param[in] handle Storage handle, obtained during storage creation.
 * @param[out] sector_size Pointer to store the size of the sector in the storage media.
 *
 * @return
 *    - ESP_OK: Sector size retrieved successfully
 *    - ESP_ERR_INVALID_ARG: Invalid input argument, sector_size pointer is NULL
 *    - ESP_ERR_INVALID_STATE: MSC driver is not initialized or storage is not initialized
 */
esp_err_t tinyusb_msc_get_storage_sector_size(tinyusb_msc_storage_handle_t handle, uint32_t *sector_size);

/**
 * @brief Get status if storage media is exposed over USB to USB Host
 *
 * @param[in] handle Storage handle, obtained during storage creation.
 * @param[out] mount_point Pointer to store the current mount point of the storage media.
 *
 * @return
 *    - ESP_OK: Mount point retrieved successfully
 *    - ESP_ERR_INVALID_ARG: Invalid input argument, mount_point pointer is NULL
 *    - ESP_ERR_INVALID_STATE: MSC driver is not installed or storage is not initialized
 */
esp_err_t tinyusb_msc_get_storage_mount_point(tinyusb_msc_storage_handle_t handle,
                                              tinyusb_msc_mount_point_t *mount_point);

#ifdef __cplusplus
}
#endif
