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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
//
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
//
#include "unity.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "test_task.h"
#include "sdkconfig.h"
#include "device_handling.h"

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Initialise TinyUSB on Full-speed peripheral (USB OTG 1.1)
 * Awaiting: Install returns ESP_OK, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("Periph: Full-speed", "[periph][full_speed]")
{
    // TinyUSB driver default configuration
    const tinyusb_config_t tusb_cfg = TINYUSB_CONFIG_FULL_SPEED(test_device_event_handler, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

#if (SOC_USB_OTG_PERIPH_NUM > 1)
/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Initialise TinyUSB on High-speed peripheral (USB OTG 2.0)
 * Awaiting: Install returns ESP_OK, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("Periph: High-speed", "[periph][high_speed]")
{
    // TinyUSB driver default configuration
    const tinyusb_config_t tusb_cfg = TINYUSB_CONFIG_HIGH_SPEED(test_device_event_handler, NULL);
    // Install TinyUSB driver
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}
#endif // SOC_USB_OTG_PERIPH_NUM > 1

#endif // SOC_USB_OTG_SUPPORTED
