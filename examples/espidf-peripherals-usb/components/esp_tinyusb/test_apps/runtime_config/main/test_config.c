/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "soc/soc_caps.h"

#if SOC_USB_OTG_SUPPORTED

#include "unity.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"


// Enable to verify static assert during the build
#define RUNTIME_CONFIG_CHECK_STATIC_ASSERTS         0

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Verifies the default macros arguments
 * Awaiting:
 * - Default macros should configure to NULL when no arguments are provided
 * - Default macros should configure to the provided arguments when one or two arguments are provided
 */
TEST_CASE("Config: Default macros arguments", "[runtime_config][default]")
{
    void *dummy_event_hdl = (void *) 0xDEADBEEF;
    void *dummy_event_arg = (void *) 0xBEEFDEAD;

    const tinyusb_config_t tusb_cfg_arg0 = TINYUSB_DEFAULT_CONFIG();
    const tinyusb_config_t tusb_cfg_arg1 = TINYUSB_DEFAULT_CONFIG(dummy_event_hdl);
    const tinyusb_config_t tusb_cfg_arg2 = TINYUSB_DEFAULT_CONFIG(dummy_event_hdl, dummy_event_arg);
#if (RUNTIME_CONFIG_CHECK_STATIC_ASSERTS)
    const tinyusb_config_t tusb_cfg_arg3 = TINYUSB_DEFAULT_CONFIG(dummy_event_hdl, dummy_event_arg, NULL);
#endif // RUNTIME_CONFIG_CHECK_STATIC_ASSERTS

    TEST_ASSERT_EQUAL_MESSAGE(NULL, tusb_cfg_arg0.event_cb, "Event callback should be NULL when no arguments provided");
    TEST_ASSERT_EQUAL_MESSAGE(NULL, tusb_cfg_arg0.event_arg, "Event argument should be NULL when no arguments provided");

    TEST_ASSERT_EQUAL_MESSAGE(dummy_event_hdl, tusb_cfg_arg1.event_cb, "Event callback was not set correctly");
    TEST_ASSERT_EQUAL_MESSAGE(NULL, tusb_cfg_arg1.event_arg, "Event argument should be NULL when one argument is provided");

    TEST_ASSERT_EQUAL_MESSAGE(dummy_event_hdl, tusb_cfg_arg2.event_cb, "Event callback was not set correctly");
    TEST_ASSERT_EQUAL_MESSAGE(dummy_event_arg, tusb_cfg_arg2.event_arg, "Event argument was not set correctly");
}

#if (SOC_USB_OTG_PERIPH_NUM == 1)
/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Verifies default configuration values (Default config for Full-speed only target)
 * Awaiting: Install returns ESP_OK, default descriptors are being used, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("Config: Full-speed default (Full-speed)", "[runtime_config][full_speed]")
{
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();

    TEST_ASSERT_EQUAL_MESSAGE(TINYUSB_PORT_FULL_SPEED_0, tusb_cfg.port, "Wrong default port number");
    TEST_ASSERT_EQUAL_MESSAGE(false, tusb_cfg.phy.skip_setup, "Wrong default skip_setup value");
    TEST_ASSERT_EQUAL_MESSAGE(false, tusb_cfg.phy.self_powered, "Wrong default self-powered flag");
    TEST_ASSERT_EQUAL_MESSAGE(-1, tusb_cfg.phy.vbus_monitor_io, "Wrong default VBUS monitor IO");
    TEST_ASSERT_EQUAL_MESSAGE(TINYUSB_DEFAULT_TASK_SIZE, tusb_cfg.task.size, "Wrong default task size");
    TEST_ASSERT_EQUAL_MESSAGE(TINYUSB_DEFAULT_TASK_PRIO, tusb_cfg.task.priority, "Wrong default task priority");
#if CONFIG_FREERTOS_UNICORE
    TEST_ASSERT_EQUAL_MESSAGE(0, tusb_cfg.task.xCoreID, "Wrong default task affinity, should be 0 on unicore");
#else
    TEST_ASSERT_EQUAL_MESSAGE(1, tusb_cfg.task.xCoreID, "Wrong default task affinity, should be 1 on multicore");
#endif // CONFIG_FREERTOS_UNICORE
}

#else
/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Verifies Full-speed configuration values (Full-speed config for High-speed target)
 * Awaiting: Install returns ESP_OK, default descriptors are being used, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("Config: Full-speed (High-speed)", "[runtime_config][full_speed]")
{
    const tinyusb_config_t tusb_cfg = TINYUSB_CONFIG_FULL_SPEED(NULL, NULL);

    TEST_ASSERT_EQUAL_MESSAGE(TINYUSB_PORT_FULL_SPEED_0, tusb_cfg.port, "Wrong default port number");
    TEST_ASSERT_EQUAL_MESSAGE(false, tusb_cfg.phy.skip_setup, "Wrong default skip_setup value");
    TEST_ASSERT_EQUAL_MESSAGE(false, tusb_cfg.phy.self_powered, "Wrong default self-powered flag");
    TEST_ASSERT_EQUAL_MESSAGE(-1, tusb_cfg.phy.vbus_monitor_io, "Wrong default VBUS monitor IO");
    TEST_ASSERT_EQUAL_MESSAGE(TINYUSB_DEFAULT_TASK_SIZE, tusb_cfg.task.size, "Wrong default task size");
    TEST_ASSERT_EQUAL_MESSAGE(TINYUSB_DEFAULT_TASK_PRIO, tusb_cfg.task.priority, "Wrong default task priority");
    TEST_ASSERT_EQUAL_MESSAGE(1, tusb_cfg.task.xCoreID, "Wrong default task affinity, should be 1 on multicore");
}

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Verifies High-speed configuration values (High-speed config for High-speed target)
 * Awaiting: Install returns ESP_OK, default descriptors are being used, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("Config: High-speed default (High-speed)", "[runtime_config][high_speed]")
{
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();

    TEST_ASSERT_EQUAL_MESSAGE(TINYUSB_PORT_HIGH_SPEED_0, tusb_cfg.port, "Wrong default port number");
    TEST_ASSERT_EQUAL_MESSAGE(false, tusb_cfg.phy.skip_setup, "Wrong default skip_setup value");
    TEST_ASSERT_EQUAL_MESSAGE(false, tusb_cfg.phy.self_powered, "Wrong default self-powered flag");
    TEST_ASSERT_EQUAL_MESSAGE(-1, tusb_cfg.phy.vbus_monitor_io, "Wrong default VBUS monitor IO");
    TEST_ASSERT_EQUAL_MESSAGE(TINYUSB_DEFAULT_TASK_SIZE, tusb_cfg.task.size, "Wrong default task size");
    TEST_ASSERT_EQUAL_MESSAGE(TINYUSB_DEFAULT_TASK_PRIO, tusb_cfg.task.priority, "Wrong default task priority");
    TEST_ASSERT_EQUAL_MESSAGE(1, tusb_cfg.task.xCoreID, "Wrong default task affinity, should be 1 on multicore");
}
#endif // SOC_USB_OTG_PERIPH_NUM > 1

#endif // SOC_USB_OTG_SUPPORTED
