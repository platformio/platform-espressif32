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
#include "esp_log.h"
#include "esp_err.h"
//
#include "unity.h"
#include "esp_idf_version.h"
#include "sdkconfig.h"
#include "storage_common.h"

void storage_init_spiflash(wl_handle_t *wl_handle)
{
    wl_handle_t wl;
    const esp_partition_t *data_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                     ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, wl_mount(data_partition, &wl), "Failed to mount wear levelling on storage partition");

    printf("SPIFLASH Wear Levelling initialized successfully\n");
    printf("\tSectors: %u\n", wl_size(wl) / wl_sector_size(wl));
    printf("\tSector Size: %u\n", wl_sector_size(wl));
    *wl_handle = wl;
}

void storage_erase_spiflash(void)
{
    const esp_partition_t *data_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                     ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
    TEST_ASSERT_NOT_NULL_MESSAGE(data_partition, "Storage partition not found");
    // Erase the data partition
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, esp_partition_erase_range(data_partition, 0, data_partition->size),
                              "Failed to erase storage partition");
    printf("Storage partition erased successfully (size in bytes: %ld)\n", data_partition->size);
}

void storage_deinit_spiflash(wl_handle_t wl_handle)
{
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, wl_unmount(wl_handle), "Failed to unmount wear levelling on data partition");
}

#endif // SOC_USB_OTG_SUPPORTED
