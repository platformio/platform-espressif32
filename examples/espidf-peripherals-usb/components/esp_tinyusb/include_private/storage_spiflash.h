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
 * @brief Open the storage medium for SPI Flash
 *
 * This function returns a storage API that can be used to interact with the SPI Flash storage.
 *
 * @param[in] wl_handle Wear-leveling handle: `wl_handle_t`
 * @param[out] medium Pointer to the storage API.
 *
 * @return
 *    - ESP_OK: Storage API returned successfully.
 *    - ESP_ERR_INVALID_ARG: Invalid argument, ctx or storage_api is NULL.
 */
esp_err_t storage_spiflash_open_medium(wl_handle_t wl_handle, const storage_medium_t **medium);

#ifdef __cplusplus
}
#endif
