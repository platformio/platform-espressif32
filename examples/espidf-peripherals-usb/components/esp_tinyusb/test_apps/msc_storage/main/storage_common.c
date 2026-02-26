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

#if (SOC_SDMMC_HOST_SUPPORTED)
#include "driver/sdmmc_host.h"
#endif // SOC_SDMMC_HOST_SUPPORTED

// SDMMC GPIO configuration
#define TEST_SDMMC_PIN_CMD              CONFIG_TEST_SDMMC_PIN_CMD
#define TEST_SDMMC_PIN_CLK              CONFIG_TEST_SDMMC_PIN_CLK
#define TEST_SDMMC_PIN_D0               CONFIG_TEST_SDMMC_PIN_D0
#define TEST_SDMMC_PIN_D1               CONFIG_TEST_SDMMC_PIN_D1
#define TEST_SDMMC_PIN_D2               CONFIG_TEST_SDMMC_PIN_D2
#define TEST_SDMMC_PIN_D3               CONFIG_TEST_SDMMC_PIN_D3

// IDF VERSION
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#if (SOC_SDMMC_IO_POWER_EXTERNAL)
// Some boards required internal LDO to be enabled for SDMMC initialization
// To understand if your board requires this, please refer to the board's documentation
#define TEST_SDMMC_INIT_INTERNAL_LDO    1 // Enable internal LDO for SDMMC initialization
#define TEST_SDMMC_LDO_CHAN_ID          4
#endif // (SOC_SDMMC_IO_POWER_EXTERNAL)
#endif // ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 3, 0)

#if (TEST_SDMMC_INIT_INTERNAL_LDO)
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

static sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;
#endif // TEST_SDMMC_INIT_INTERNAL_LDO

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

#if (SOC_SDMMC_HOST_SUPPORTED)
void storage_init_sdmmc(sdmmc_card_t **card)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
#if TEST_SDMMC_INIT_INTERNAL_LDO
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = TEST_SDMMC_LDO_CHAN_ID,
    };

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle),
                              "Failed to create a new on-chip LDO power control driver");
    host.pwr_ctrl_handle = pwr_ctrl_handle;
#endif // TEST_SDMMC_INIT_INTERNAL_LDO

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    // Set the slot configuration parameters
    slot_config.width = 4;
#if (SOC_SDMMC_USE_GPIO_MATRIX)
    slot_config.cmd = TEST_SDMMC_PIN_CMD;
    slot_config.clk = TEST_SDMMC_PIN_CLK;
    slot_config.d0 = TEST_SDMMC_PIN_D0;
    slot_config.d1 = TEST_SDMMC_PIN_D1;
    slot_config.d2 = TEST_SDMMC_PIN_D2;
    slot_config.d3 = TEST_SDMMC_PIN_D3;
#endif // SOC_SDMMC_USE_GPIO_MATRIX

    // Enable internal pullups on enabled pins. The internal pullups
    // are insufficient however, please make sure 10k external pullups are
    // connected on the bus. This is for debug / example purpose only.
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // not using ff_memalloc here, as allocation in internal RAM is preferred
    sdmmc_card_t *sd_card = (sdmmc_card_t *)malloc(sizeof(sdmmc_card_t));
    TEST_ASSERT_NOT_NULL_MESSAGE(sd_card, "Failed to allocate memory for sdmmc_card_t");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, sdmmc_host_init(), "SDMMC Host Config Init fail");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, sdmmc_host_init_slot(host.slot, (const sdmmc_slot_config_t *) &slot_config), "Host init slot fail");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, sdmmc_card_init(&host, sd_card), "SDMMC Card init fail");

    *card = sd_card;

    printf("SDMMC Card initialized successfully\n");
    printf("\tSectors: %u\n", sd_card->csd.capacity);
    printf("\tSector Size: %u\n", sd_card->csd.sector_size);
}

void storage_erase_sdmmc(sdmmc_card_t *card)
{
    // Erase the SDMMC card
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, sdmmc_full_erase(card), "Failed to erase SDMMC card");
    printf("SDMMC Card erased successfully\n");
}

void storage_deinit_sdmmc(sdmmc_card_t *card)
{
    // Deinit the host
    sdmmc_host_deinit();
    // Delete the sd_card pointer
    free(card);
#if TEST_SDMMC_INIT_INTERNAL_LDO
    sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl_handle);
#endif // TEST_SDMMC_INIT_INTERNAL_LDO
}
#endif // SOC_SDMMC_HOST_SUPPORTED


#endif // SOC_USB_OTG_SUPPORTED
