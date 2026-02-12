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
#include "tinyusb_cdc_acm.h"

/**
 * @brief Initialize CDC ACM. Initialization will be finished with
 *          the `tud_cdc_line_state_cb` callback
 *
 * @deprecated Deprecated and may be removed in future releases.
 *
 * @param[in] cfg Configuration structure
 * @return esp_err_t
 */
#define tusb_cdc_acm_init(cfg) tinyusb_cdcacm_init((cfg))

/**
 * @brief De-initialize CDC ACM.
 *
 * @deprecated Deprecated and may be removed in future releases.
 *
 * @param[in] itf Index of CDC interface
 * @return esp_err_t
 */
#define tusb_cdc_acm_deinit(itf) tinyusb_cdcacm_deinit((itf))

/**
 * @brief Check if the CDC interface is initialized
 *
 * @deprecated Deprecated and may be removed in future releases.
 *
 * @param[in] itf  Index of CDC interface
 * @return - true  Initialized
 *         - false Not Initialized
 */
#define tusb_cdc_acm_initialized(itf) tinyusb_cdcacm_initialized((itf))

#ifdef __cplusplus
}
#endif
