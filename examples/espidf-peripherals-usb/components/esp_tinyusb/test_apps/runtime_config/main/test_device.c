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
// TinyUSB Public
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "test_task.h"
// TinyUSB Private
#include "descriptors_control.h"
// Test common
#include "device_handling.h"


// ========================== TinyUSB General Device Descriptors ===============================

#define TUSB_CFG_DESC_TOTAL_LEN                 (TUD_CONFIG_DESC_LEN + CFG_TUD_CDC * TUD_CDC_DESC_LEN)

static const uint8_t test_fs_configuration_descriptor[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, CFG_TUD_CDC * 2, 0, TUSB_CFG_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_SELF_POWERED | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64),
#if CFG_TUD_CDC > 1
    TUD_CDC_DESCRIPTOR(2, 4, 0x83, 8, 0x04, 0x84, 64),
#endif // CFG_TUD_CDC > 1
};

#if (TUD_OPT_HIGH_SPEED)

static const uint8_t test_hs_configuration_descriptor[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, CFG_TUD_CDC * 2, 0, TUSB_CFG_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_SELF_POWERED | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 512),
#if CFG_TUD_CDC > 1
    TUD_CDC_DESCRIPTOR(2, 4, 0x83, 8, 0x04, 0x84, 512),
#endif // CFG_TUD_CDC > 1
};

static const tusb_desc_device_qualifier_t device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved = 0
};
#endif // TUD_OPT_HIGH_SPEED

static const tusb_desc_device_t test_device_descriptor = {
    .bLength = sizeof(test_device_descriptor),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A, // This is Espressif VID. This needs to be changed according to Users / Customers
    .idProduct = 0x4002,
    .bcdDevice = 0x100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

/**
 * @brief String descriptor
 */
const char *test_string_descriptor[USB_STRING_DESCRIPTOR_ARRAY_SIZE + 1] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    "TinyUSB",             // 1: Manufacturer
    "TinyUSB Device",      // 2: Product
    "123456",              // 3: Serials, should use chip ID
    "TinyUSB CDC",         // 4: CDC String descriptor
    "String 5",            // 5: Test string #6
    "String 6",            // 6: Test string #7
    "String 7",            // 7: Test string #8
    "String 8",            // 8: Test string #9
};


// ========================== Callbacks ========================================

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Provide more string descriptors than is supported
 * Awaiting: Install returns ESP_ERR_NOT_SUPPORTED
 */
TEST_CASE("Device: String Descriptors overflow", "[runtime_config][default]")
{
    // TinyUSB driver default configuration
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.descriptor.string = test_string_descriptor;
    tusb_cfg.descriptor.string_count = USB_STRING_DESCRIPTOR_ARRAY_SIZE + 1;
    // Install TinyUSB driver
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, tinyusb_driver_install(&tusb_cfg));
}

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Provide maximum supported string descriptors
 * Awaiting: Install returns ESP_OK, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("Device: String Descriptors maximum value", "[runtime_config][default]")
{
    // TinyUSB driver default configuration
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.descriptor.string = test_string_descriptor;
    tusb_cfg.descriptor.string_count = USB_STRING_DESCRIPTOR_ARRAY_SIZE;
    // Install TinyUSB driver
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Provide Device & Configuration descriptors
 * Awaiting: Install returns ESP_OK, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("Device: Device & Configuration", "[runtime_config][default]")
{
    // TinyUSB driver configuration
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    // Set descriptors
    tusb_cfg.descriptor.device = &test_device_descriptor;
    tusb_cfg.descriptor.string = test_string_descriptor;
    tusb_cfg.descriptor.string_count = 5; // 5 string descriptors as we report string index 4 for CDC in the configuration descriptor
    tusb_cfg.descriptor.full_speed_config = test_fs_configuration_descriptor;
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.qualifier = &device_qualifier;
    tusb_cfg.descriptor.high_speed_config = test_hs_configuration_descriptor;
#endif // TUD_OPT_HIGH_SPEED

    // Install TinyUSB driver
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Provide no descriptors (All default for Full-speed)
 * Awaiting: Install returns ESP_OK, default descriptors are being used, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("Device: Full-speed default", "[runtime_config][full_speed]")
{
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    // Install TinyUSB driver
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Provide only device descriptor
 * Awaiting: Install returns ESP_OK, default descriptor is being used, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("Device: Device Descriptor only", "[runtime_config][default]")
{
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);

    tusb_cfg.descriptor.device = &test_device_descriptor;
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.qualifier = &device_qualifier;
#endif // TUD_OPT_HIGH_SPEED
    // Install TinyUSB driver
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Provide only device & FS configuration descriptor
 * Awaiting: Install returns ESP_OK, default FS configuration descriptor is being used, device is enumerated, tusb_mount_cb() is called
 *
 * Note: HS configuration descriptor is not provided by user (legacy compatibility) and default configuration descriptor for HS is used.
 */
TEST_CASE("Device: Device & Full-speed config only", "[runtime_config][default]")
{
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);

    tusb_cfg.descriptor.device = &test_device_descriptor;
    tusb_cfg.descriptor.full_speed_config = test_fs_configuration_descriptor;
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.qualifier = &device_qualifier;
    // tusb_cfg.descriptor.high_speed_config = NULL;
#endif // TUD_OPT_HIGH_SPEED
    // Install TinyUSB driver
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

#if (SOC_USB_OTG_PERIPH_NUM > 1)
/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Provide no descriptors (All default for High-speed)
 * Awaiting: Install returns ESP_OK, default descriptors are being used, device is enumerated, tusb_mount_cb() is called
 */
TEST_CASE("Device: High-speed default", "[runtime_config][high_speed]")
{
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    // Install TinyUSB driver
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Provide only device & HS configuration descriptor
 * Awaiting: Install returns ESP_OK, default FS configuration descriptor is being used, device is enumerated, tusb_mount_cb() is called
 *
 * Note: FS configuration descriptor is not provided by user (legacy compatibility) and default configuration descriptor for FS is used.
 */
TEST_CASE("Device: Device and High-speed config only", "[runtime_config][high_speed]")
{
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);

    tusb_cfg.descriptor.device = &test_device_descriptor;
    tusb_cfg.descriptor.qualifier = &device_qualifier;
    tusb_cfg.descriptor.high_speed_config = test_hs_configuration_descriptor;

    // Install TinyUSB driver
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}
#endif // SOC_USB_OTG_PERIPH_NUM > 1

#endif // SOC_USB_OTG_SUPPORTED
