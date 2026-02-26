/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <string.h>
#include "stdint.h"
#include "esp_err.h"
#include "msc_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the storage medium for SDMMC
 *
 * This function returns a storage API that can be used to interact with the SDMMC storage.
 *
 * @note Only one SDMMC card can be opened at a time.
 * To open a new SDMMC card, the previous one must be closed first.
 *
 * @param[in] card Pointer to `sdmmc_card_t` structure.
 * @param[out] medium Pointer to the storage medium.
 *
 * @return
 *    - ESP_OK: Storage API returned successfully.
 *    - ESP_ERR_INVALID_ARG: Invalid argument, ctx or storage_api is NULL.
 */
esp_err_t storage_sdmmc_open_medium(sdmmc_card_t *card, const storage_medium_t **medium);



#ifdef __cplusplus
}
#endif
