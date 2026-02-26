/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "sdkconfig.h"
#include "wear_levelling.h"
#include "diskio_wl.h"
#include "msc_storage.h"

static const char *TAG = "storage_spiflash";

static wl_handle_t _wl_handle = WL_INVALID_HANDLE; // Global variable to hold the wear-levelling handle

static esp_err_t storage_spiflash_mount(BYTE pdrv)
{
    assert(_wl_handle != WL_INVALID_HANDLE);
    return ff_diskio_register_wl_partition(pdrv, _wl_handle);
}

static esp_err_t storage_spiflash_unmount(void)
{
    assert(_wl_handle != WL_INVALID_HANDLE);
    BYTE pdrv;
    pdrv = ff_diskio_get_pdrv_wl(_wl_handle);
    if (pdrv == 0xff) {
        return ESP_ERR_INVALID_STATE;
    }
    ff_diskio_clear_pdrv_wl(_wl_handle);

    char drv[3] = {(char)('0' + pdrv), ':', 0};
    f_mount(0, drv, 0);
    ff_diskio_unregister(pdrv);

    return ESP_OK;
}

static size_t storage_spiflash_get_sector_count(void)
{
    assert(_wl_handle != WL_INVALID_HANDLE);
    size_t result = 0;
    size_t size = wl_sector_size(_wl_handle);
    if (size == 0) {
        result = 0;
    } else {
        result = (size_t)(wl_size(_wl_handle) / size);
    }
    return result;
}

static size_t storage_spiflash_get_sector_size(void)
{
    assert(_wl_handle != WL_INVALID_HANDLE);
    return (size_t)wl_sector_size(_wl_handle);
}

static esp_err_t storage_spiflash_sector_read(uint32_t lba, uint32_t offset, size_t size, void *dest)
{
    assert(_wl_handle != WL_INVALID_HANDLE);
    size_t temp = 0;
    size_t addr = 0; // Address of the data to be read, relative to the beginning of the partition.
    size_t sector_size = storage_spiflash_get_sector_size();

    ESP_RETURN_ON_FALSE(!__builtin_umul_overflow(lba, sector_size, &temp), ESP_ERR_INVALID_SIZE, TAG, "overflow lba %lu sector_size %u", lba, sector_size);
    ESP_RETURN_ON_FALSE(!__builtin_uadd_overflow(temp, offset, &addr), ESP_ERR_INVALID_SIZE, TAG, "overflow addr %u offset %lu", temp, offset);

    return wl_read(_wl_handle, addr, dest, size);
}

static esp_err_t storage_spiflash_sector_write(uint32_t lba, uint32_t offset, size_t size, const void *src)
{
    assert(_wl_handle != WL_INVALID_HANDLE);
    (void) lba;         // lba is not used in this implementation
    (void) offset;      // offset is not used in this implementation

    size_t temp = 0;
    size_t addr = 0; // Address of the data to be read, relative to the beginning of the partition.
    size_t sector_size = storage_spiflash_get_sector_size();

    ESP_RETURN_ON_FALSE(!__builtin_umul_overflow(lba, sector_size, &temp), ESP_ERR_INVALID_SIZE, TAG, "overflow lba %lu sector_size %u", lba, sector_size);
    ESP_RETURN_ON_FALSE(!__builtin_uadd_overflow(temp, offset, &addr), ESP_ERR_INVALID_SIZE, TAG, "overflow addr %u offset %lu", temp, offset);
    ESP_RETURN_ON_ERROR(wl_erase_range(_wl_handle, addr, size), TAG, "Failed to erase");

    return wl_write(_wl_handle, addr, src, size);
}

static esp_err_t storage_spiflash_get_info(storage_info_t *info)
{
    ESP_RETURN_ON_FALSE(info, ESP_ERR_INVALID_ARG, TAG, "Storage info pointer can't be NULL");

    info->total_sectors = (uint32_t) storage_spiflash_get_sector_count();
    info->sector_size = (uint32_t) storage_spiflash_get_sector_size();

    return ESP_OK;
}

static void storage_spiflash_close(void)
{
    _wl_handle = WL_INVALID_HANDLE; // Reset the global wear-levelling handle
}

// Constant struct of function pointers
const storage_medium_t spiflash_medium = {
    .type = STORAGE_MEDIUM_TYPE_SPIFLASH,
    .mount = &storage_spiflash_mount,
    .unmount = &storage_spiflash_unmount,
    .read = &storage_spiflash_sector_read,
    .write = &storage_spiflash_sector_write,
    .get_info = &storage_spiflash_get_info,
    .close = &storage_spiflash_close,
};

esp_err_t storage_spiflash_open_medium(wl_handle_t wl_handle, const storage_medium_t **medium)
{
    ESP_RETURN_ON_FALSE(wl_handle != WL_INVALID_HANDLE, ESP_ERR_INVALID_ARG, TAG, "Invalid wear-levelling handle");
    ESP_RETURN_ON_FALSE(medium != NULL, ESP_ERR_INVALID_ARG, TAG, "Storage API pointer can't be NULL");

    _wl_handle = wl_handle;
    *medium = &spiflash_medium;

    return ESP_OK;
}
