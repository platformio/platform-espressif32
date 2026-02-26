/*
 * SPDX-FileCopyrightText: 2020-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "tinyusb_console.h"

/**
 * @brief Redirect output to the USB serial
 *
 * @deprecated Deprecated and may be removed in future releases.
 *
 * @param cdc_intf - interface number of TinyUSB's CDC
 *
 * @return esp_err_t - ESP_OK, ESP_FAIL or an error code
 */
#define esp_tusb_init_console(cdc_intf) tinyusb_console_init((cdc_intf))

/**
 * @brief Switch log to the default output
 *
 * @deprecated Deprecated and may be removed in future releases.
 *
 * @param cdc_intf - interface number of TinyUSB's CDC
 *
 * @return esp_err_t
 */
#define esp_tusb_deinit_console(cdc_intf) tinyusb_console_deinit((cdc_intf))

#ifdef __cplusplus
}
#endif
