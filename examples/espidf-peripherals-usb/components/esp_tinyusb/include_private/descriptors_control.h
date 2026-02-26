/*
 * SPDX-FileCopyrightText: 2020-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "tinyusb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_STRING_DESCRIPTOR_ARRAY_SIZE            8 // Max 8 string descriptors for a device. LANGID, Manufacturer, Product, Serial number + 4 user defined

/**
 * @brief Check the TinyUSB configuration
 *
 * @param[in] port TinyUSB port number
 * @param[in] config TinyUSB Descriptor configuration
 * @retval ESP_ERR_INVALID_ARG if config is NULL or port is invalid
 * @retval ESP_ERR_NOT_SUPPORTED if string_count is greater than USB_STRING_DESCRIPTOR_ARRAY_SIZE
 * @retval ESP_OK if config is valid
 */
esp_err_t tinyusb_descriptors_check(tinyusb_port_t port, const tinyusb_desc_config_t *config);

/**
 * @brief Parse tinyusb configuration and prepare the device configuration pointer list to configure tinyusb driver
 *
 * @attention All descriptors passed to this function must exist for the duration of USB device lifetime
 *
 * @param[in] port   TinyUSB port number
 * @param[in] config Tinyusb Descriptor configuration.
 * @retval ESP_ERR_INVALID_ARG Default configuration descriptor is provided only for CDC, MSC and NCM classes
 * @retval ESP_ERR_NO_MEM      Memory allocation error
 * @retval ESP_OK              Descriptors configured without error
 */
esp_err_t tinyusb_descriptors_set(tinyusb_port_t port, const tinyusb_desc_config_t *config);

/**
 * @brief Set specific string descriptor
 *
 * @attention The descriptor passed to this function must exist for the duration of USB device lifetime
 *
 * @param[in] str     UTF-8 string
 * @param[in] str_idx String descriptor index
 */
void tinyusb_descriptors_set_string(const char *str, int str_idx);

/**
 * @brief Free memory allocated during tinyusb_descriptors_set
 */
void tinyusb_descriptors_free(void);

#ifdef __cplusplus
}
#endif
