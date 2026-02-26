/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Redirect output to the USB serial
 * @param cdc_intf - interface number of TinyUSB's CDC
 *
 * @return esp_err_t - ESP_OK, ESP_FAIL or an error code
 */
esp_err_t tinyusb_console_init(int cdc_intf);

/**
 * @brief Switch log to the default output
 * @param cdc_intf - interface number of TinyUSB's CDC
 *
 * @return esp_err_t
 */
esp_err_t tinyusb_console_deinit(int cdc_intf);

#ifdef __cplusplus
}
#endif
