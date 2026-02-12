/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
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

static const char *TAG = "teardown";

SemaphoreHandle_t wait_mount = NULL;

#define TEARDOWN_DEVICE_INIT_DELAY_MS       1000
#define TEARDOWN_DEVICE_ATTACH_TIMEOUT_MS   1000
#define TEARDOWN_DEVICE_DETACH_DELAY_MS     1000

#define TEARDOWN_AMOUNT                     10

#define TUSB_DESC_TOTAL_LEN                 (TUD_CONFIG_DESC_LEN)
static uint8_t const test_configuration_descriptor[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 0, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_SELF_POWERED | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
};

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

#if (TUD_OPT_HIGH_SPEED)
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

/**
 * @brief TinyUSB callback for device event
 *
 * @note
 * For Linux-based Hosts: Reflects the SetConfiguration() request from the Host Driver.
 * For Win-based Hosts: SetConfiguration() request is present only with available Class in device descriptor.
 */
void test_teardown_event_handler(tinyusb_event_t *event, void *arg)
{
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        xSemaphoreGive(wait_mount);
        break;
    default:
        break;
    }
}

/**
 * @brief TinyUSB Teardown specific testcase
 *
 * Scenario:
 * 1. Install TinyUSB device without any class
 * 2. Wait SetConfiguration() (tud_mount_cb)
 * 3. If attempts == 0 goto step 8
 * 4.   Wait TEARDOWN_DEVICE_DETACH_DELAY_MS
 * 5.   Uninstall TinyUSB device
 * 6.   Wait TEARDOWN_DEVICE_INIT_DELAY_MS
 * 7.   Decrease attempts by 1, goto step 3
 * 8. Wait TEARDOWN_DEVICE_DETACH_DELAY_MS
 * 9. Uninstall TinyUSB device
 */
TEST_CASE("tinyusb_teardown", "[esp_tinyusb][teardown]")
{
    wait_mount = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_EQUAL(NULL, wait_mount);

    // TinyUSB driver configuration
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_teardown_event_handler);
    tusb_cfg.descriptor.device = &test_device_descriptor;
    tusb_cfg.descriptor.full_speed_config = test_configuration_descriptor;
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.qualifier = &device_qualifier;
    tusb_cfg.descriptor.high_speed_config = test_configuration_descriptor;
#endif // TUD_OPT_HIGH_SPEED

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    // Wait for the usb event
    ESP_LOGD(TAG, "wait mount...");
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(wait_mount, pdMS_TO_TICKS(TEARDOWN_DEVICE_ATTACH_TIMEOUT_MS)));
    ESP_LOGD(TAG, "mounted");

    // Teardown routine
    int attempts = TEARDOWN_AMOUNT;
    while (attempts--) {
        // Keep device attached
        vTaskDelay(pdMS_TO_TICKS(TEARDOWN_DEVICE_DETACH_DELAY_MS));
        TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
        // Teardown
        vTaskDelay(pdMS_TO_TICKS(TEARDOWN_DEVICE_INIT_DELAY_MS));
        // Reconnect
        TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
        // Wait for the usb event
        ESP_LOGD(TAG, "wait mount...");
        TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(wait_mount, pdMS_TO_TICKS(TEARDOWN_DEVICE_ATTACH_TIMEOUT_MS)));
        ESP_LOGD(TAG, "mounted");
    }

    // Teardown
    vTaskDelay(pdMS_TO_TICKS(TEARDOWN_DEVICE_DETACH_DELAY_MS));
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    // Remove primitives
    vSemaphoreDelete(wait_mount);
}

#endif
