/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "esp_partition.h"
#include "esp_memory_utils.h"
#include "soc/soc_caps.h"
#include "sdkconfig.h"
#include "vfs_fat_internal.h"
#include "tinyusb.h"
#include "device/usbd_pvt.h"
#include "class/msc/msc_device.h"

#include "storage_spiflash.h"
#include "msc_storage.h"
#include "tinyusb_msc.h"

#if (SOC_SDMMC_HOST_SUPPORTED)
#include "storage_sdmmc.h"
#include "diskio_sdmmc.h"
#endif // SOC_SDMMC_HOST_SUPPORTED

static const char *TAG = "tinyusb_msc_storage";

#define MSC_STORAGE_MEM_ALIGN 4
#define MSC_STORAGE_BUFFER_SIZE CONFIG_TINYUSB_MSC_BUFSIZE /*!< Size of the buffer, configured via menuconfig (MSC FIFO size) */

#if ((MSC_STORAGE_BUFFER_SIZE) % MSC_STORAGE_MEM_ALIGN != 0)
#error "CONFIG_TINYUSB_MSC_BUFSIZE must be divisible by MSC_STORAGE_MEM_ALIGN. Adjust your configuration (MSC FIFO size) in menuconfig."
#endif

#define TINYUSB_MSC_STORAGE_MAX_LUNS    2                               /*!< Maximum number of LUNs supported by TinyUSB MSC storage. Dafult value is 2 */
#define TINYUSB_DEFAULT_BASE_PATH       CONFIG_TINYUSB_MSC_MOUNT_PATH   /*!< Default base path for the filesystem, configured via menuconfig */

/**
 * @brief Structure representing a single write buffer for MSC operations.
 */
typedef struct {
    uint8_t data_buffer[MSC_STORAGE_BUFFER_SIZE]; /*!< Buffer to store write data. The size is defined by MSC_STORAGE_BUFFER_SIZE. */
    uint8_t lun;                           /*!< Logical Unit Number (LUN) for the current write operation. */
    uint32_t lba;                          /*!< Logical Block Address for the current WRITE10 operation. */
    uint32_t offset;                       /*!< Offset within the specified LBA for the current write operation. */
    uint32_t bufsize;                      /*!< Number of bytes to be written in this operation. */
} msc_storage_buffer_t;

/**
 * @brief Handle for TinyUSB MSC storage interface.
 *
 * This structure holds metadata and function pointers required to
 * manage the underlying storage medium (SPI flash, SDMMC).
 */
typedef struct {
    // Storage related
    const storage_medium_t *medium;             /*!< Pointer to the storage medium. */
    tinyusb_msc_mount_point_t mount_point;      /*!< Current mount point type (application or USB host). */
    // Optimisation purpose
    uint32_t sector_count;                      /*!< Total number of sectors in the storage medium. */
    uint32_t sector_size;                       /*!< Size of a single sector in bytes. */
    // FS related
    struct {
        const char *base_path;                  /*!< Base path where the filesystem is mounted. */
        int max_files;                          /*!< Maximum number of files that can be open simultaneously. */
        bool do_not_format;                     /*!< If true, do not format the drive if filesystem is not present. */
        BYTE format_flags;                      /*!< Flags for formatting the filesystem, can be 0 to use default settings. */
    } fat_fs;
    // Buffer for storage operations
    msc_storage_buffer_t storage_buffer;        /*!< Buffer for storing data during write operations. */
    uint32_t deffered_writes;                   /*!< Number of deferred writes pending in the buffer. */
    SemaphoreHandle_t mux_lock;                 /**< Mutex for storage operations */
} tinyusb_msc_storage_s;

typedef tinyusb_msc_storage_s msc_storage_obj_t;

typedef struct {
    struct {
        msc_storage_obj_t *storage[TINYUSB_MSC_STORAGE_MAX_LUNS]; /*!< Storage objects */
        uint8_t lun_count;              /*!< Number of logical units (LUNs) supported by the storage. */
        tusb_msc_callback_t event_cb;   /*!< Callback for mount changed events. */
        void *event_arg;                /*!< Argument to pass to the event callback. */
    } dynamic;

    struct {
        union {
            struct {
                // User config - 16 bits
                uint32_t auto_mount_off: 1;         /**< If true, turn off automatically mount on USB host connection */
                uint32_t user_reserved15: 15;       /**< Reserved for future use */
                // Internal config - 16 bits
                uint32_t internal_reserved15: 15;   /**< Reserved for intenral use */
                uint32_t internally_installed: 1;   /**< Driver was internally installed. Uninstall driver on last storage removal */
            };
            uint32_t val;                           /**< MSC Driver configuration flag value */
        } flags;
    } constant;
} tinyusb_msc_driver_t;

static tinyusb_msc_driver_t *p_msc_driver;

static portMUX_TYPE msc_lock = portMUX_INITIALIZER_UNLOCKED;
#define MSC_ENTER_CRITICAL()   portENTER_CRITICAL(&msc_lock)
#define MSC_EXIT_CRITICAL()    portEXIT_CRITICAL(&msc_lock)

#define MSC_GOTO_ON_FALSE_CRITICAL(cond, err)    \
    do {                                        \
        if(!(cond)) {                            \
            MSC_EXIT_CRITICAL();                \
            ret = err;                          \
            goto fail;                          \
        }                                       \
    } while(0)

#define MSC_CHECK_ON_CRITICAL(cond, err)  \
    do {                                        \
        if(!(cond)) {                            \
            MSC_EXIT_CRITICAL();                \
            return err;                         \
        }                                       \
    } while(0)

//
// ========================== TinyUSB MSC Storage Event Handling =================================
//

static inline void tinyusb_event_cb(msc_storage_obj_t *storage, tinyusb_msc_event_id_t event_id)
{
    assert(p_msc_driver != NULL);
    assert(storage != NULL);

    MSC_ENTER_CRITICAL();
    tusb_msc_callback_t cb = p_msc_driver->dynamic.event_cb;
    void *cb_arg = p_msc_driver->dynamic.event_arg;
    MSC_EXIT_CRITICAL();

    tinyusb_msc_event_t event = {
        .id = event_id,
        .mount_point = storage->mount_point,
    };
    cb((tinyusb_msc_storage_handle_t)storage, &event, cb_arg);
}

//
// ========================== TinyUSB MSC Storage Operations =================================
//

/**
 * @brief Get the storage object by LUN
 * This function retrieves the storage object associated with the specified LUN.
 *
 * @note This function must be called from a critical section.
 *
 * @param[in] lun The logical unit number (LUN) to retrieve the storage for.
 * @param[out] storage Pointer to the storage object pointer that will be set.
 * @return
 *   - true if storage object is found for the specified LUN and not NULL, false otherwise.
 */
static inline bool _msc_storage_get_by_lun(uint8_t lun, msc_storage_obj_t **storage)
{
    if ((lun < TINYUSB_MSC_STORAGE_MAX_LUNS) &&
            (p_msc_driver->dynamic.storage[lun] != NULL)) {
        *storage = p_msc_driver->dynamic.storage[lun];
        return true;
    }
    return false;
}

/**
 * @brief Map a storage object to a specific LUN
 * This function associates a storage object with a logical unit number (LUN).
 *
 * @note This function must be called from a critical section.
 *
 * Storage should have the medium initialized before calling this function.
 *
 * @param[in] storage Pointer to the storage object to be mapped.
 * @return
 *  - true if storage is successfully mapped to a LUN, false otherwise.
 */
static inline bool _msc_storage_map_to_lun(msc_storage_obj_t *storage)
{
    if (storage == NULL || storage->medium == NULL) {
        return false;
    }

    if (p_msc_driver->dynamic.lun_count >= TINYUSB_MSC_STORAGE_MAX_LUNS) {
        return false;
    }

    p_msc_driver->dynamic.storage[p_msc_driver->dynamic.lun_count] = storage;
    p_msc_driver->dynamic.lun_count++;
    return true;
}

/**
 * @brief Unmap a storage object from a specific LUN
 * This function disassociates a storage object from a logical unit number (LUN).
 *
 * @note This function must be called from a critical section.
 *
 * @param[in] lun The logical unit number (LUN) to unmap the storage from.
 * @return
 *  - true if storage is successfully unmapped from a LUN, false otherwise.
 */
static inline bool _msc_storage_unmap_from_lun(msc_storage_obj_t *storage)
{
    if (storage == NULL) {
        return false;
    }

    for (uint8_t i = 0; i < TINYUSB_MSC_STORAGE_MAX_LUNS; i++) {
        if (p_msc_driver->dynamic.storage[i] == storage) {
            p_msc_driver->dynamic.storage[i] = NULL;
            p_msc_driver->dynamic.lun_count--;
            return true;
        }
    }
    return false;
}

/**
 * @brief Read a sector from the storage medium
 *
 * This function reads a sector from the storage medium associated with the specified LUN.
 *
 * @param[in] lun The logical unit number (LUN) to read from.
 * @param[in] lba Logical Block Address of the sector to read.
 * @param[in] offset Offset within the sector to read from.
 * @param[in] size Number of bytes to read.
 * @param[out] dest Pointer to the destination buffer where the read data will be stored.
 *
 * @return
 *   - ESP_OK: Read operation successful
 *   - ESP_ERR_NOT_FOUND: Storage not found for the specified LUN
 */
static inline esp_err_t msc_storage_read_sector(uint8_t lun, uint32_t lba, uint32_t offset, size_t size, void *dest)
{
    msc_storage_obj_t *storage = NULL;
    esp_err_t ret;

    MSC_ENTER_CRITICAL();
    bool found = _msc_storage_get_by_lun(lun, &storage);
    MSC_EXIT_CRITICAL();

    if (!found || storage == NULL) {
        ESP_LOGE(TAG, "Storage not found for LUN %d", lun);
        return ESP_ERR_NOT_FOUND;
    }
    // Otherwise, take the lock and proceed with the read
    xSemaphoreTake(storage->mux_lock, portMAX_DELAY);
    ret = storage->medium->read(lba, offset, size, dest);
    xSemaphoreGive(storage->mux_lock);
    return ret;
}

/**
 * @brief Write a sector to the storage medium
 *
 * This function writes a sector to the storage medium associated with the specified LUN.
 *
 * @param[in] lun The logical unit number (LUN) to write to.
 * @param[in] lba Logical Block Address of the sector to write to.
 * @param[in] offset Offset within the sector to write to.
 * @param[in] size Number of bytes to write.
 * @param[in] src Pointer to the source buffer containing the data to write.
 *
 * @return
 *  - ESP_OK: Write operation successful
 *  - ESP_ERR_NOT_FOUND: Storage not found for the specified LUN
 */
static inline esp_err_t msc_storage_write_sector(uint8_t lun, uint32_t lba, uint32_t offset, size_t size, const void *src)
{
    msc_storage_obj_t *storage = NULL;
    esp_err_t ret;

    MSC_ENTER_CRITICAL();
    bool found = _msc_storage_get_by_lun(lun, &storage);
    MSC_EXIT_CRITICAL();

    if (!found || storage == NULL) {
        ESP_LOGE(TAG, "Storage not found for LUN %d", lun);
        return ESP_ERR_NOT_FOUND;
    }
    // Otherwise, take the lock and proceed with the write
    xSemaphoreTake(storage->mux_lock, portMAX_DELAY);
    ret = storage->medium->write(lba, offset, size, src);
    xSemaphoreGive(storage->mux_lock);
    return ret;
}

/**
 * @brief Handles deferred USB MSC write operations.
 *
 * This function is invoked via TinyUSB's deferred execution mechanism to perform
 * write operations to the underlying storage. It writes data from the
 * `storage_buffer` stored within the `s_storage_handle`.
 *
 * @param param Pointer to the storage object containing the write parameters.
 */
static void tusb_write_func(void *param)
{
    assert(param); // Ensure storage is not NULL
    msc_storage_obj_t *storage = (msc_storage_obj_t *)param;

    esp_err_t err = msc_storage_write_sector(
                        storage->storage_buffer.lun,
                        storage->storage_buffer.lba,
                        storage->storage_buffer.offset,
                        storage->storage_buffer.bufsize,
                        (const void *)storage->storage_buffer.data_buffer
                    );

    // Decrement the deferred writes counter
    MSC_ENTER_CRITICAL();
    assert(storage->deffered_writes > 0); // Ensure there are deferred writes pending
    storage->deffered_writes--;
    MSC_EXIT_CRITICAL();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Write failed, error=0x%x", err);
    }
}

/**
 * @brief Write a sector to the storage medium using deferred execution.
 *
 * This function copies the data to be written into an internal buffer and
 * defers the actual write operation to be executed in the TinyUSB task context.
 *
 * @param[in] lun The logical unit number (LUN) to write to.
 * @param[in] lba Logical Block Address of the sector to write to.
 * @param[in] offset Offset within the sector to write to.
 * @param[in] size Number of bytes to write.
 * @param[in] src Pointer to the source buffer containing the data to write.
 *
 * @return
 * - ESP_OK: Write operation successfully deferred
 * - ESP_ERR_NOT_FOUND: Storage not found for the specified LUN
 * - ESP_ERR_INVALID_SIZE: Address calculation overflow for SPI Flash storage medium
 */
static inline esp_err_t msc_storage_write_sector_deferred(uint8_t lun, uint32_t lba, uint32_t offset, size_t size, const void *src)
{
    msc_storage_obj_t *storage = NULL;

    MSC_ENTER_CRITICAL();
    bool found = _msc_storage_get_by_lun(lun, &storage);
    MSC_EXIT_CRITICAL();

    if (!found || storage == NULL) {
        ESP_LOGE(TAG, "LUN %d is not mapped to any storage", lun);
        return ESP_ERR_NOT_FOUND;
    }

    // As we defer the write operation to the TinyUSB task, we need to ensure that
    // the address does not overflow for SPI Flash storage medium
    if (storage->medium->type == STORAGE_MEDIUM_TYPE_SPIFLASH) {
        size_t addr = 0; // Address of the data to be read, relative to the beginning of the partition.
        size_t temp = 0;
        size_t sector_size = storage->sector_size;
        ESP_RETURN_ON_FALSE(!__builtin_umul_overflow(lba, sector_size, &temp), ESP_ERR_INVALID_SIZE, TAG, "overflow lba %lu sector_size %u", lba, sector_size);
        ESP_RETURN_ON_FALSE(!__builtin_uadd_overflow(temp, offset, &addr), ESP_ERR_INVALID_SIZE, TAG, "overflow addr %u offset %lu", temp, offset);
    }

    // Copy data to the buffer
    memcpy((void *)storage->storage_buffer.data_buffer, src, size);
    storage->storage_buffer.lun = lun;
    storage->storage_buffer.lba = lba;
    storage->storage_buffer.offset = offset;
    storage->storage_buffer.bufsize = size;

    // Increment the deferred writes counter
    MSC_ENTER_CRITICAL();
    storage->deffered_writes++;
    MSC_EXIT_CRITICAL();

    // Defer execution of the write to the TinyUSB task
    usbd_defer_func(tusb_write_func, (void *)storage, false);

    return ESP_OK;
}

static esp_err_t vfs_fat_format(BYTE format_flags)
{
    esp_err_t ret;
    FRESULT fresult;
    // Drive does not have a filesystem, try to format it
    const size_t workbuf_size = 4096;
    void *workbuf = ff_memalloc(workbuf_size);
    if (workbuf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t alloc_unit_size = esp_vfs_fat_get_allocation_unit_size(CONFIG_WL_SECTOR_SIZE, workbuf_size);

    ESP_LOGD(TAG, "Format drive, allocation unit size=%d", alloc_unit_size);

    const MKFS_PARM opt = {format_flags, 0, 0, 0, alloc_unit_size};
    fresult = f_mkfs("", &opt, workbuf, workbuf_size); // Use default volume
    if (fresult != FR_OK) {
        ret = ESP_FAIL;
        ESP_LOGE(TAG, "Unable to create default volume, (%d)", fresult);
        goto fail;
    }
    ff_memfree(workbuf);
    workbuf = NULL;
    return ESP_OK;
fail:
    if (workbuf) {
        ff_memfree(workbuf);
    }
    return ret;
}

static esp_err_t vfs_fat_mount(char *drv, FATFS *fs, bool force)
{
    esp_err_t ret;
    // Try to mount the drive
    FRESULT fresult = f_mount(fs, drv, force ? 1 : 0);
    switch (fresult) {
    case FR_OK:
        ESP_LOGD(TAG, "Mounted drive %s successfully", drv);
        ret = ESP_OK;
        break;
    case FR_NO_FILESYSTEM:
    case FR_INT_ERR:
        // These are recoverable errors, which can be fixed by formatting the drive
        ESP_LOGD(TAG, "Drive %s does not have a filesystem, need to format", drv);
        ret = ESP_ERR_NOT_FOUND; // No filesystem or internal error, need to format
        break;
    default:
        ESP_LOGE(TAG, "Failed to mount drive %s (%d)", drv, fresult);
        ret = ESP_FAIL; // Other errors
        break;
    }
    return ret;
}

/**
 * @brief Mount the storage medium
 *
 * Registers the FAT filesystem on the storage medium under the base_path via ESP VFS.
 *
 * @param[in] storage Pointer to the storage object, with filesystem on medium to be mounted.
 *
 * @return
 *  - ESP_OK: Storage mounted successfully
 *  - ESP_ERR_INVALID_STATE: Unable to register the FATFS object to VFS
 *  - ESP_ERR_NOT_FOUND: Filesystem not found on the mounted drive
 */
static esp_err_t msc_storage_mount(msc_storage_obj_t *storage)
{
    esp_err_t ret;
    FATFS *fs = NULL;
    const char *base_path = storage->fat_fs.base_path;
    int max_files = storage->fat_fs.max_files;

    if (storage->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP) {
        // If the storage is already mounted to APP, no need to unmount
        return ESP_OK;
    }

    tinyusb_event_cb(storage, TINYUSB_MSC_EVENT_MOUNT_START);

    // Get the vacant driver number
    BYTE pdrv = 0xFF;
    ESP_RETURN_ON_ERROR(ff_diskio_get_drive(&pdrv), TAG, "The maximum count of volumes is already mounted");

    // Lock the storage
    xSemaphoreTake(storage->mux_lock, portMAX_DELAY);

    // Register the partition under the drive number
    ret = storage->medium->mount(pdrv);
    if (ret != ESP_OK) {
        xSemaphoreGive(storage->mux_lock);
        ESP_LOGE(TAG, "Failed to mount the storage medium, pdrv=%d, err=0x%x", pdrv, ret);
        goto exit;
    }

    // Register FATFS object to VFS
    char drv[3] = {(char)('0' + pdrv), ':', 0};

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    esp_vfs_fat_conf_t conf = {
        .base_path = base_path,
        .fat_drive = drv,
        .max_files = max_files,
    };
    ret = esp_vfs_fat_register_cfg(&conf, &fs);
#else
    ret = esp_vfs_fat_register(base_path, drv, max_files, &fs);
#endif
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGD(TAG, "VFS FAT already registered");
    } else if (ret != ESP_OK) {
        xSemaphoreGive(storage->mux_lock);
        ESP_LOGE(TAG, "VFS FAT register failed, %s", esp_err_to_name(ret));
        tinyusb_event_cb(storage, TINYUSB_MSC_EVENT_MOUNT_FAILED);
        goto exit;
    }

    // Registering the FATFS object was done successfully; change the mount point.
    // All subsequent errors depend on the filesystem.
    storage->mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP;

    ret = vfs_fat_mount(drv, fs, true);
    if (ret == ESP_ERR_NOT_FOUND) {
        // If mount failed, try to format the drive
        if (storage->fat_fs.do_not_format) {
            xSemaphoreGive(storage->mux_lock);
            ESP_LOGE(TAG, "Mount failed and do not format is set");
            tinyusb_event_cb(storage, TINYUSB_MSC_EVENT_FORMAT_REQUIRED);
            ret = ESP_OK;
            goto exit;
        }
        ESP_LOGW(TAG, "Mount failed, trying to format the drive");
        BYTE format_flags = storage->fat_fs.format_flags;
        ESP_GOTO_ON_ERROR(vfs_fat_format(format_flags), fail, TAG, "Failed to format the drive");
        ESP_GOTO_ON_ERROR(vfs_fat_mount(drv, fs, false), fail, TAG, "Failed to mount FAT filesystem");
        ESP_LOGD(TAG, "Format completed, FAT mounted successfully");
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount drive, %s", esp_err_to_name(ret));
        goto fail;
    }

    xSemaphoreGive(storage->mux_lock);
    tinyusb_event_cb(storage, TINYUSB_MSC_EVENT_MOUNT_COMPLETE);
    return ESP_OK;
fail:
    xSemaphoreGive(storage->mux_lock);
    tinyusb_event_cb(storage, TINYUSB_MSC_EVENT_FORMAT_FAILED);
exit:
    storage->medium->unmount();
    if (fs) {
        esp_vfs_fat_unregister_path(base_path);
    }
    return ret;
}

/**
 * @brief Unmount the storage medium
 *
 * Unregisters the FAT filesystem on the storage medium and unregister the base_path via ESP VFS.
 * After, the FS is not available under base_path and can be exposed to USB Host.
 *
 * @param[in] storage Pointer to the storage object, with filesystem on medium to be unmounted.
 *
 * @return
 *  ESP_OK if storage unmounted successfully, otherwise an error code indicating the failure reason.
 */
static esp_err_t msc_storage_unmount(msc_storage_obj_t *storage)
{
    if (storage->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB) {
        // If the storage is already mounted to USB, no need to unmount
        return ESP_OK;
    }

    tinyusb_event_cb(storage, TINYUSB_MSC_EVENT_MOUNT_START);

    xSemaphoreTake(storage->mux_lock, portMAX_DELAY);

    // Unregister the partition
    esp_err_t ret = storage->medium->unmount();
    if (ret != ESP_OK) {
        xSemaphoreGive(storage->mux_lock);
        ESP_LOGE(TAG, "Failed to unmount the storage medium");
        return ret;
    }
    // Unregister FATFS object from VFS
    ret = esp_vfs_fat_unregister_path(storage->fat_fs.base_path);
    if (ret != ESP_OK) {
        xSemaphoreGive(storage->mux_lock);
        ESP_LOGE(TAG, "Failed to unregister VFS FAT");
        return ret;
    }

    storage->mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB;
    xSemaphoreGive(storage->mux_lock);

    tinyusb_event_cb(storage, TINYUSB_MSC_EVENT_MOUNT_COMPLETE);
    return ESP_OK;
}

static void msc_storage_event_default_cb(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg)
{
    (void) handle;
    (void) event;
    (void) arg;

    // Default callback does nothing
    // This is used when no user-defined callback is provided
    ESP_LOGW(TAG, "Default MSC event callback called, event ID: %d, mount point: %d", event->id, event->mount_point);
}

/**
 * @brief Create a new MSC storage object
 *
 * This function allocates and initializes a new MSC storage object based on the provided configuration
 * and storage medium.
 *
 * @param[in] config Pointer to the MSC storage configuration structure.
 * @param[in] medium Pointer to the storage medium interface.
 * @param[out] storage_hdl Pointer to the location where the created storage object handle will be stored.
 *
 * @return
 * - ESP_OK: Storage object created successfully
 * - ESP_ERR_NO_MEM: Memory allocation failed
 */
static esp_err_t msc_storage_new(const tinyusb_msc_storage_config_t *config,
                                 const storage_medium_t *medium,
                                 msc_storage_obj_t **storage_hdl)
{
    esp_err_t ret;

    // Create mutex for storage operations
    SemaphoreHandle_t mux_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(mux_lock != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create mutex for storage operations");
    // Create storage object
    msc_storage_obj_t *storage_obj = (msc_storage_obj_t *)heap_caps_aligned_calloc(MSC_STORAGE_MEM_ALIGN, sizeof(msc_storage_obj_t), sizeof(uint32_t), MALLOC_CAP_DMA);
    if (storage_obj == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for MSC storage");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    // Configure the storage object
    storage_obj->mux_lock = mux_lock;
    storage_obj->medium = medium;
    storage_obj->mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB; // Default mount point is USB host
    storage_obj->deffered_writes = 0;
    // In case the user does not set mount_config.max_files
    // and for backward compatibility with versions <1.4.2
    // max_files is set to 2
    const int max_files = config->fat_fs.config.max_files;
    storage_obj->fat_fs.max_files = max_files > 0 ? max_files : 2;
    storage_obj->fat_fs.do_not_format = config->fat_fs.do_not_format;
    storage_obj->fat_fs.format_flags = config->fat_fs.format_flags;
    if (storage_obj->fat_fs.format_flags == 0) {
        // Use default format flags if not provided
        storage_obj->fat_fs.format_flags = FM_ANY; // Auto-select FAT type based on volume size
    }
    if (config->fat_fs.base_path == NULL) {
        // Use default base path if not provided
        storage_obj->fat_fs.base_path = TINYUSB_DEFAULT_BASE_PATH;
    } else {
        // Use the provided base path
        storage_obj->fat_fs.base_path = config->fat_fs.base_path;
    }

    // Set sector count and size
    storage_info_t storage_info;
    ret = storage_obj->medium->get_info(&storage_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get storage info");
        goto fail;
    }

    storage_obj->sector_count = storage_info.total_sectors;
    storage_obj->sector_size = storage_info.sector_size;

    ESP_LOGD(TAG, "Storage type: , sectors count: %"PRIu32", sector size: %"PRIu32"",
             storage_obj->sector_count,
             storage_obj->sector_size);

    *storage_hdl = storage_obj;
    return ESP_OK;
fail:
    if (storage_obj) {
        heap_caps_free(storage_obj);
    }
    if (mux_lock) {
        vSemaphoreDelete(mux_lock);
    }
    return ret;
}

/**
 * @brief Delete an MSC storage object
 *
 * This function frees the resources associated with an MSC storage object.
 *
 * @param[in] storage Pointer to the storage object to be deleted.
 */
static void msc_storage_delete(msc_storage_obj_t *storage)
{
    storage->medium = NULL;

    if (storage->mux_lock) {
        vSemaphoreDelete(storage->mux_lock);
    }
    heap_caps_free(storage);
}

/**
 * @brief Install the MSC driver
 *
 * This function initializes the MSC driver with the provided configuration.
 *
 * @param[in] config Pointer to the MSC driver configuration structure.
 * @param[in] internally_installed Boolean flag indicating if the driver is installed internally during the first storage creation.
 *
 */
static esp_err_t msc_driver_install(const tinyusb_msc_driver_config_t *config, bool internally_installed)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "Config can't be NULL");

    MSC_ENTER_CRITICAL();
    MSC_CHECK_ON_CRITICAL(p_msc_driver == NULL, ESP_ERR_INVALID_STATE);
    MSC_EXIT_CRITICAL();

    esp_err_t ret;
    tinyusb_msc_driver_t *msc_driver = NULL;
    msc_driver = (tinyusb_msc_driver_t *)heap_caps_aligned_calloc(MSC_STORAGE_MEM_ALIGN, sizeof(tinyusb_msc_driver_t), sizeof(uint32_t), MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(msc_driver != NULL, ESP_ERR_NO_MEM, TAG, "Failed to allocate memory for MSC driver");

    // Default callback
    if (config->callback == NULL) {
        msc_driver->dynamic.event_cb = msc_storage_event_default_cb;
        msc_driver->dynamic.event_arg = NULL;
    } else {
        msc_driver->dynamic.event_cb = config->callback;
        msc_driver->dynamic.event_arg = config->callback_arg;
    }

    msc_driver->dynamic.lun_count = 0; // LUN will be added with storage initialization
    msc_driver->constant.flags.val = (uint16_t) config->user_flags.val; // Config flags for the MSC driver
    msc_driver->constant.flags.internally_installed = internally_installed;

    MSC_ENTER_CRITICAL();
    MSC_GOTO_ON_FALSE_CRITICAL(p_msc_driver == NULL, ESP_ERR_INVALID_STATE);
    p_msc_driver = msc_driver;
    MSC_EXIT_CRITICAL();

    return ESP_OK;
fail:
    heap_caps_free(msc_driver);
    return ret;
}

//
// ============================ TinyUSB MSC Storage Private Functions ==========================
//

void msc_storage_mount_to_app(void)
{
    if (p_msc_driver == NULL) {
        return;
    }

    for (uint8_t i = 0; i < TINYUSB_MSC_STORAGE_MAX_LUNS; i++) {
        if (p_msc_driver->dynamic.storage[i] != NULL && !p_msc_driver->constant.flags.auto_mount_off) {
            if (msc_storage_mount(p_msc_driver->dynamic.storage[i]) != ESP_OK) {
                ESP_LOGW(TAG, "Unable to mount storage to app");
                tinyusb_event_cb(p_msc_driver->dynamic.storage[i], TINYUSB_MSC_EVENT_MOUNT_FAILED);
            }
        }
    }
}

void msc_storage_mount_to_usb(void)
{
    if (p_msc_driver == NULL) {
        return;
    }

    for (uint8_t i = 0; i < TINYUSB_MSC_STORAGE_MAX_LUNS; i++) {
        if (p_msc_driver->dynamic.storage[i] != NULL && !p_msc_driver->constant.flags.auto_mount_off) {
            if (msc_storage_unmount(p_msc_driver->dynamic.storage[i]) != ESP_OK) {
                ESP_LOGW(TAG, "Unable to mount storage to usb");
                tinyusb_event_cb(p_msc_driver->dynamic.storage[i], TINYUSB_MSC_EVENT_MOUNT_FAILED);
            }
        }
    }
}

//
// ========================== TinyUSB MSC Public API Functions =================================
//

esp_err_t tinyusb_msc_set_storage_callback(tusb_msc_callback_t callback, void *arg)
{
    ESP_RETURN_ON_FALSE(p_msc_driver != NULL, ESP_ERR_INVALID_STATE, TAG, "Storage handle is not initialized");
    ESP_RETURN_ON_FALSE(callback != NULL, ESP_ERR_INVALID_ARG, TAG, "Callback can't be NULL");

    MSC_ENTER_CRITICAL();
    p_msc_driver->dynamic.event_cb = callback;
    p_msc_driver->dynamic.event_arg = arg;
    MSC_EXIT_CRITICAL();
    return ESP_OK;
}

esp_err_t tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *config)
{
    return msc_driver_install(config, false);
}

esp_err_t tinyusb_msc_uninstall_driver(void)
{
    MSC_ENTER_CRITICAL();
    MSC_CHECK_ON_CRITICAL(p_msc_driver != NULL, ESP_ERR_NOT_SUPPORTED);
    MSC_CHECK_ON_CRITICAL(p_msc_driver->dynamic.lun_count == 0, ESP_ERR_INVALID_STATE);
    tinyusb_msc_driver_t *msc_driver = p_msc_driver;
    p_msc_driver = NULL;
    MSC_EXIT_CRITICAL();

    // Free the driver memory
    heap_caps_free(msc_driver);
    return ESP_OK;
}

esp_err_t tinyusb_msc_new_storage_spiflash(const tinyusb_msc_storage_config_t *config,
                                           tinyusb_msc_storage_handle_t *handle)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "Config can't be NULL");
    ESP_RETURN_ON_FALSE(config->medium.wl_handle != WL_INVALID_HANDLE, ESP_ERR_INVALID_ARG, TAG, "Wear levelling handle should be valid");

    ESP_RETURN_ON_FALSE(CONFIG_TINYUSB_MSC_BUFSIZE >= CONFIG_WL_SECTOR_SIZE,
                        ESP_ERR_NOT_SUPPORTED,
                        TAG,
                        "TinyUSB buffer size (%d) must be at least the size of Wear Levelling sector size (%d), please reconfigure the project.",
                        (int)(CONFIG_TINYUSB_MSC_BUFSIZE), (int)(CONFIG_WL_SECTOR_SIZE));

    bool need_to_install_driver = false;
    const storage_medium_t *medium = NULL;
    msc_storage_obj_t *storage = NULL;
    esp_err_t ret;

    MSC_ENTER_CRITICAL();
    if (p_msc_driver == NULL) {
        need_to_install_driver = true;
    }
    MSC_EXIT_CRITICAL();

    // Driver was not installed, install it now
    if (need_to_install_driver) {
        tinyusb_msc_driver_config_t default_cfg = {
            .callback = msc_storage_event_default_cb,
        };
        ret = msc_driver_install(&default_cfg, true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to install MSC driver");
            goto driver_err;
        }
    }

    // Create a medium for storage
    ret = storage_spiflash_open_medium(config->medium.wl_handle, &medium);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open SPI Flash medium");
        goto medium_err;
    }
    // Create a storage object
    ret = msc_storage_new(config, medium, &storage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MSC storage object");
        goto storage_err;
    }
    // Map the storage object to the MSC Lun
    MSC_ENTER_CRITICAL();
    if (!_msc_storage_map_to_lun(storage)) {
        MSC_EXIT_CRITICAL();
        ESP_LOGE(TAG, "Failed to map storage to LUN");
        ret = ESP_FAIL;
        goto map_err;
    }
    MSC_EXIT_CRITICAL();

    // Mount the storage if it is configured to be mounted to application
    if (config->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP) {
        ret = msc_storage_mount(storage);
        if (ret != ESP_OK) {
            // Unrecoverable error
            ESP_LOGE(TAG, "Failed to mount storage to application");
            goto map_err;
        }
    }

    // Return the handle to the storage
    if (handle != NULL) {
        *handle = (tinyusb_msc_storage_handle_t)storage;
    }
    return ESP_OK;

map_err:
    msc_storage_delete(storage);
storage_err:
    medium->close();
medium_err:
    if (need_to_install_driver) {
        tinyusb_msc_uninstall_driver();
    }
driver_err:
    return ret;
}

#if (SOC_SDMMC_HOST_SUPPORTED)
esp_err_t tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *config,
                                        tinyusb_msc_storage_handle_t *handle)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "Config can't be NULL");
    ESP_RETURN_ON_FALSE(config->medium.card != NULL, ESP_ERR_INVALID_ARG, TAG, "Card handle should be valid");

    bool need_to_install_driver = false;
    const storage_medium_t *medium = NULL;
    msc_storage_obj_t *storage = NULL;
    esp_err_t ret;

    MSC_ENTER_CRITICAL();
    if (p_msc_driver == NULL) {
        need_to_install_driver = true;
    }
    MSC_EXIT_CRITICAL();

    // Driver was not installed, install it now
    if (need_to_install_driver) {
        tinyusb_msc_driver_config_t default_cfg = {
            .callback = msc_storage_event_default_cb,
        };
        ret = msc_driver_install(&default_cfg, true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to install MSC driver");
            goto driver_err;
        }
    }

    // Create a medium for storage
    ret = storage_sdmmc_open_medium(config->medium.card, &medium);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open SD/MMC medium");
        goto medium_err;
    }
    // Create a storage object
    ret = msc_storage_new(config, medium, &storage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MSC storage object");
        goto storage_err;
    }
    // Map the storage object to the MSC Lun
    MSC_ENTER_CRITICAL();
    if (!_msc_storage_map_to_lun(storage)) {
        MSC_EXIT_CRITICAL();
        ESP_LOGE(TAG, "Failed to map storage to LUN");
        ret = ESP_FAIL;
        goto map_err;
    }
    MSC_EXIT_CRITICAL();

    // Mount the storage if it is configured to be mounted to application
    if (config->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP) {
        ret = msc_storage_mount(storage);
        if (ret != ESP_OK) {
            // Unrecoverable error
            ESP_LOGE(TAG, "Failed to mount storage to application");
            goto map_err;
        }
    }

    // Return the handle to the storage
    if (handle != NULL) {
        *handle = (tinyusb_msc_storage_handle_t)storage;
    }
    return ESP_OK;

map_err:
    msc_storage_delete(storage);
storage_err:
    medium->close();
medium_err:
    if (need_to_install_driver) {
        tinyusb_msc_uninstall_driver();
    }
driver_err:
    return ret;
}
#endif // SOC_SDMMC_HOST_SUPPORTED

esp_err_t tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Storage handle can't be NULL");
    msc_storage_obj_t *storage = (msc_storage_obj_t *)handle;
    bool no_more_luns = false;

    MSC_ENTER_CRITICAL();
    MSC_CHECK_ON_CRITICAL(p_msc_driver != NULL, ESP_ERR_INVALID_STATE);
    MSC_CHECK_ON_CRITICAL(p_msc_driver->dynamic.lun_count > 0, ESP_ERR_INVALID_STATE);
    MSC_CHECK_ON_CRITICAL(storage->deffered_writes == 0, ESP_ERR_INVALID_STATE);
    MSC_EXIT_CRITICAL();

    if (storage->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP) {
        // Unmount the storage if it is mounted to application
        ESP_ERROR_CHECK(msc_storage_unmount(storage));
    }

    // Unmap the storage from the MSC Lun
    MSC_ENTER_CRITICAL();
    if (!_msc_storage_unmap_from_lun(storage)) {
        MSC_EXIT_CRITICAL();
        ESP_LOGE(TAG, "Storage not found in any LUN");
        return ESP_ERR_NOT_FOUND;
    };
    no_more_luns = (p_msc_driver->dynamic.lun_count == 0);
    MSC_EXIT_CRITICAL();

    // Close the storage medium
    storage->medium->close();

    // If no LUNs left and driver was installed internally, uninstall the driver
    if (no_more_luns && p_msc_driver->constant.flags.internally_installed) {
        ESP_ERROR_CHECK(tinyusb_msc_uninstall_driver()); // Should never fail
    }
    // Free the storage object and related resources
    msc_storage_delete(storage);
    return ESP_OK;
}

esp_err_t tinyusb_msc_get_storage_capacity(tinyusb_msc_storage_handle_t handle, uint32_t *sector_count)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_STATE, TAG, "MSC storage is not initialized");
    ESP_RETURN_ON_FALSE(sector_count != NULL, ESP_ERR_INVALID_ARG, TAG, "Sector count pointer can't be NULL");

    MSC_ENTER_CRITICAL();
    MSC_CHECK_ON_CRITICAL(p_msc_driver != NULL, ESP_ERR_INVALID_STATE);
    MSC_EXIT_CRITICAL();

    msc_storage_obj_t *storage = (msc_storage_obj_t *) handle;
    *sector_count = storage->sector_count;

    return ESP_OK;
}

esp_err_t tinyusb_msc_get_storage_sector_size(tinyusb_msc_storage_handle_t handle, uint32_t *sector_size)
{
    ESP_RETURN_ON_FALSE(p_msc_driver != NULL, ESP_ERR_INVALID_STATE, TAG, "MSC driver is not initialized");
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_STATE, TAG, "MSC storage is not initialized");
    ESP_RETURN_ON_FALSE(sector_size != NULL, ESP_ERR_INVALID_ARG, TAG, "Sector size pointer can't be NULL");

    msc_storage_obj_t *storage = (msc_storage_obj_t *) handle;
    *sector_size = storage->sector_size;

    return ESP_OK;
}

esp_err_t tinyusb_msc_set_storage_mount_point(tinyusb_msc_storage_handle_t handle,
                                              tinyusb_msc_mount_point_t mount_point)
{
    ESP_RETURN_ON_FALSE(p_msc_driver != NULL, ESP_ERR_INVALID_STATE, TAG, "MSC driver is not initialized");

    msc_storage_obj_t *storage = (msc_storage_obj_t *)handle;

    if (storage->mount_point == mount_point) {
        // If the storage is already mounted to the requested mount point, do nothing
        return ESP_OK;
    }

    if (mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP) {
        // If the storage is mounted to application, mount it
        msc_storage_mount(storage);
    } else {
        // If the storage is mounted to USB host, unmount it
        msc_storage_unmount(storage);
    }
    storage->mount_point = mount_point;

    return ESP_OK;
}

esp_err_t tinyusb_msc_config_storage_fat_fs(tinyusb_msc_storage_handle_t handle,
                                            tinyusb_msc_fatfs_config_t *fatfs_config)
{
    ESP_RETURN_ON_FALSE(p_msc_driver != NULL, ESP_ERR_INVALID_STATE, TAG, "MSC driver is not initialized");
    ESP_RETURN_ON_FALSE(fatfs_config != NULL, ESP_ERR_INVALID_ARG, TAG, "FatFS config pointer can't be NULL");

    msc_storage_obj_t *storage = (msc_storage_obj_t *) handle;
    ESP_RETURN_ON_FALSE(storage != NULL, ESP_ERR_INVALID_STATE, TAG, "MSC storage is not initialized");
    // In case the user does not set mount_config.max_files
    // and for backward compatibility with versions <1.4.2
    // max_files is set to 2
    const int max_files = fatfs_config->config.max_files;
    storage->fat_fs.max_files = max_files > 0 ? max_files : 2;
    storage->fat_fs.do_not_format = fatfs_config->do_not_format;
    storage->fat_fs.format_flags = fatfs_config->format_flags;
    if (storage->fat_fs.format_flags == 0) {
        // Use default format flags if not provided
        storage->fat_fs.format_flags = FM_ANY; // Auto-select FAT type based on volume size
    }
    if (fatfs_config->base_path == NULL) {
        // Use default base path if not provided
        storage->fat_fs.base_path = TINYUSB_DEFAULT_BASE_PATH;
    } else {
        // Use the provided base path
        storage->fat_fs.base_path = fatfs_config->base_path;
    }

    return ESP_OK;
}

esp_err_t tinyusb_msc_get_storage_mount_point(tinyusb_msc_storage_handle_t handle,
                                              tinyusb_msc_mount_point_t *mount_point)
{
    ESP_RETURN_ON_FALSE(p_msc_driver != NULL, ESP_ERR_INVALID_STATE, TAG, "MSC driver is not initialized");
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_STATE, TAG, "MSC storage is not initialized");
    ESP_RETURN_ON_FALSE(mount_point != NULL, ESP_ERR_INVALID_ARG, TAG, "Mount point pointer can't be NULL");

    MSC_ENTER_CRITICAL();
    msc_storage_obj_t *storage = (msc_storage_obj_t *) handle;
    *mount_point = storage->mount_point;
    MSC_EXIT_CRITICAL();

    return ESP_OK;
}

esp_err_t tinyusb_msc_format_storage(tinyusb_msc_storage_handle_t handle)
{
    ESP_RETURN_ON_FALSE(p_msc_driver != NULL, ESP_ERR_INVALID_STATE, TAG, "MSC driver is not initialized");
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "Storage handle can't be NULL");
    msc_storage_obj_t *storage = (msc_storage_obj_t *) handle;

    esp_err_t ret;
    FATFS *fs = NULL;
    BYTE pdrv = 0xFF;
    const char *base_path = storage->fat_fs.base_path;
    int max_files = storage->fat_fs.max_files;

    ESP_RETURN_ON_FALSE(storage->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP, ESP_ERR_INVALID_ARG, TAG, "Storage must be mounted to APP to format it");
    // Register the diskio driver on the storage medium
    ESP_RETURN_ON_ERROR(ff_diskio_get_drive(&pdrv), TAG, "The maximum count of volumes is already mounted");
    ESP_RETURN_ON_ERROR(storage->medium->mount(pdrv), TAG, "Failed pdrv=%d", pdrv);

    // Register FAT FS with VFS component
    char drv[3] = {(char)('0' + pdrv), ':', 0}; // FATFS drive specificator; if only one drive is used, can be an empty string
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    esp_vfs_fat_conf_t conf = {
        .base_path = base_path,
        .fat_drive = drv,
        .max_files = max_files,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_fat_register_cfg(&conf, &fs), TAG, "VFS FAT register failed");
#else
    ESP_RETURN_ON_ERROR(esp_vfs_fat_register(base_path, drv, max_files, &fs), TAG, "VFS FAT register failed");
#endif
    // to make format, we need to mount the fs
    // Mount the FAT FS
    ret = vfs_fat_mount(drv, fs, true);
    ESP_RETURN_ON_FALSE(ret == ESP_ERR_NOT_FOUND, ESP_ERR_NOT_FOUND, TAG, "Unexpected filesystem found on the drive");
    ESP_RETURN_ON_ERROR(vfs_fat_format(storage->fat_fs.format_flags), TAG, "Failed to format the drive");
    ESP_RETURN_ON_ERROR(vfs_fat_mount(drv, fs, false), TAG, "Failed to mount FAT filesystem");

    ESP_LOGD(TAG, "Storage formatted successfully");
    return ESP_OK;
}

/* TinyUSB MSC callbacks
   ********************************************************************* */

/** SCSI ASC/ASCQ codes. **/
/** User can add and use more codes as per the need of the application **/
#define SCSI_CODE_ASC_MEDIUM_NOT_PRESENT                0x3A /** SCSI ASC code for 'MEDIUM NOT PRESENT' **/
#define SCSI_CODE_ASC_INVALID_COMMAND_OPERATION_CODE    0x20 /** SCSI ASC code for 'INVALID COMMAND OPERATION CODE' **/
#define SCSI_CODE_ASCQ                                  0x00

// Invoked when received GET_MAX_LUN request, required for multiple LUNs implementation
uint8_t tud_msc_get_maxlun_cb(void)
{
    uint8_t msc_lun = 1; // Default 1 LUN, even when the storage is not initialized, report 1 LUN, but without a media

    MSC_ENTER_CRITICAL();
    if (p_msc_driver != NULL && p_msc_driver->dynamic.lun_count) {
        msc_lun = p_msc_driver->dynamic.lun_count;
    }
    MSC_EXIT_CRITICAL();

    return msc_lun;
}

// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void) lun;
    const char vid[] = "TinyUSB";
    const char pid[] = "TEST MSC Storage";
    const char rev[] = "0.1";

    memcpy(vendor_id, vid, strlen(vid));
    memcpy(product_id, pid, strlen(pid));
    memcpy(product_rev, rev, strlen(rev));
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    msc_storage_obj_t *storage = NULL;

    MSC_ENTER_CRITICAL();
    bool found = _msc_storage_get_by_lun(lun, &storage);
    MSC_EXIT_CRITICAL();

    if (found && (storage != NULL) && (storage->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB)) {
        // Storage media is ready for access by USB host
        return true;
    }
    // Storage media is not ready for access by USB host
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, SCSI_CODE_ASC_MEDIUM_NOT_PRESENT, SCSI_CODE_ASCQ);
    return false;
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    msc_storage_obj_t *storage = NULL;

    uint32_t capacity = 0;
    uint32_t sec_size = 0;

    MSC_ENTER_CRITICAL();
    bool found = _msc_storage_get_by_lun(lun, &storage);
    MSC_EXIT_CRITICAL();

    if (found && storage != NULL) {
        // Do not verify the error, if the storage is not initialized
        tinyusb_msc_get_storage_capacity((tinyusb_msc_storage_handle_t) storage, &capacity);
        tinyusb_msc_get_storage_sector_size((tinyusb_msc_storage_handle_t) storage, &sec_size);
    }

    *block_count = capacity;
    *block_size  = (uint16_t)sec_size;
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void) lun;
    (void) power_condition;

    if (load_eject && !start) {
        // Eject media from the storage
        msc_storage_mount_to_app();
    }
    return true;
}

// Invoked when received SCSI READ10 command
// - Address = lba * BLOCK_SIZE + offset
// - Application fill the buffer (up to bufsize) with address contents and return number of read byte.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    esp_err_t err = msc_storage_read_sector(lun, lba, offset, bufsize, buffer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "READ(10) command failed, %s", esp_err_to_name(err));
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_CODE_ASC_INVALID_COMMAND_OPERATION_CODE, SCSI_CODE_ASCQ);
        return -1; // Indicate an error occurred
    }
    return bufsize;
}

// Invoked when received SCSI WRITE10 command
// - Address = lba * BLOCK_SIZE + offset
// - Application write data from buffer to address contents (up to bufsize) and return number of written byte.
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    // There is no way to return the error from the deferred function, so we need to check everything here
    if (bufsize > MSC_STORAGE_BUFFER_SIZE) {
        ESP_LOGE(TAG, "Buffer size %"PRIu32" exceeds maximum allowed size %d", bufsize, MSC_STORAGE_BUFFER_SIZE);
        goto error;
    }
    esp_err_t err = msc_storage_write_sector_deferred(lun, lba, offset, bufsize, buffer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WRITE(10) command failed, %s", esp_err_to_name(err));
        goto error;
    }
    // Return the number of bytes accepted
    return bufsize;

error:
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_CODE_ASC_INVALID_COMMAND_OPERATION_CODE, SCSI_CODE_ASCQ);
    return -1; // Indicate an error occurred
}

/**
 * Invoked when received an SCSI command not in built-in list below.
 * - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, TEST_UNIT_READY, START_STOP_UNIT, MODE_SENSE6, REQUEST_SENSE
 * - READ10 and WRITE10 has their own callbacks
 *
 * \param[in]   lun         Logical unit number
 * \param[in]   scsi_cmd    SCSI command contents which application must examine to response accordingly
 * \param[out]  buffer      Buffer for SCSI Data Stage.
 *                            - For INPUT: application must fill this with response.
 *                            - For OUTPUT it holds the Data from host
 * \param[in]   bufsize     Buffer's length.
 *
 * \return      Actual bytes processed, can be zero for no-data command.
 * \retval      negative    Indicate error e.g unsupported command, tinyusb will \b STALL the corresponding
 *                          endpoint and return failed status in command status wrapper phase.
 */
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    int32_t ret;

    switch (scsi_cmd[0]) {
    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
        /* SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL is the Prevent/Allow Medium Removal
        command (1Eh) that requests the library to enable or disable user access to
        the storage media/partition. */
        ret = 0;
        break;
    default:
        ESP_LOGW(TAG, "tud_msc_scsi_cb() invoked: %d", scsi_cmd[0]);
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_CODE_ASC_INVALID_COMMAND_OPERATION_CODE, SCSI_CODE_ASCQ);
        ret = -1;
        break;
    }
    return ret;
}

/*********************************************************************** TinyUSB MSC callbacks*/
