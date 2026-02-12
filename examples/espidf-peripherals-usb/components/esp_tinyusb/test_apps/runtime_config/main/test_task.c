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

// ============================= Tests =========================================

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Invalid configuration
 * Awaiting: Install returns ESP_ERR_INVALID_ARG
 */
TEST_CASE("Task: Invalid params", "[runtime_config][default]")
{
    // TinyUSB driver default configuration
    tinyusb_config_t tusb_cfg = { 0 };
    // Install TinyUSB driver - Invalid Task size
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, tinyusb_driver_install(&tusb_cfg));
    tusb_cfg.task.size = 4096;
    // Install TinyUSB driver - Invalid Task priority
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, tinyusb_driver_install(&tusb_cfg));
    tusb_cfg.task.priority = 5;
    // Install TinyUSB driver - Invalid Task affinity
    tusb_cfg.task.xCoreID = 0xff;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, tinyusb_driver_install(&tusb_cfg));
}

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Initialise with Internal task
 * Awaiting: Install returns ESP_OK, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("Task: Default configuration", "[task][default]")
{
    // TinyUSB driver default configuration
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    // Install TinyUSB driver
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Initialise with Internal task and CPU0 affinity
 * Awaiting: Install returns ESP_OK, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("Task: Default configuration, pin to CPU0", "[task][default]")
{
    // TinyUSB driver default configuration
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.task.xCoreID = 0;
    // Install TinyUSB driver
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

#if !CONFIG_FREERTOS_UNICORE
/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Initialise with Internal task and CPU1 affinity
 * Awaiting: Install returns ESP_OK, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("Task: Default configuration, pin to CPU1", "[task][default]")
{
    // TinyUSB driver default configuration
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.task.xCoreID = 1;
    // Install TinyUSB driver
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}
#endif // CONFIG_FREERTOS_UNICORE

#endif // SOC_USB_OTG_SUPPORTED
