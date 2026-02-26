/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "soc/soc_caps.h"

#if SOC_USB_OTG_SUPPORTED

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "unity.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

#define DEVICE_DETACH_TEST_ROUNDS       10
#define DEVICE_DETACH_ROUND_DELAY_MS    1000

#if (CONFIG_IDF_TARGET_ESP32P4)
#define USB_SRP_BVALID_IN_IDX       USB_SRP_BVALID_PAD_IN_IDX
#endif // CONFIG_IDF_TARGET_ESP32P4

/* TinyUSB descriptors
   ********************************************************************* */
#define TUSB_DESC_TOTAL_LEN         (TUD_CONFIG_DESC_LEN)

static unsigned int dev_mounted = 0;
static unsigned int dev_umounted = 0;

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
void test_dconn_event_handler(tinyusb_event_t *event, void *arg)
{
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        printf("%s\n", __FUNCTION__);
        dev_mounted++;
        break;
    case TINYUSB_EVENT_DETACHED:
        printf("%s\n", __FUNCTION__);
        dev_umounted++;
        break;
    default:
        break;
    }
}

/**
 * @brief TinyUSB Disconnect Detection test case
 *
 * This is specific artificial test for verifying the disconnection detection event.
 * Normally, this event comes as a result of detaching USB device from the port and disappearing the VBUS voltage.
 * In this test case, we use GPIO matrix and connect the signal to the ZERO or ONE constant inputs.
 * Connection to constant ONE input emulates the attachment to the USB Host port (appearing VBUS).
 * Connection to constant ZERO input emulates the detachment from the USB Host port (removing VBUS).
 *
 * Test logic:
 * - Install TinyUSB Device stack without any class
 * - In cycle:
 *      - Emulate the detachment, get the tud_umount_cb(), increase the dev_umounted value
 *      - Emulate the attachment, get the tud_mount_cb(), increase the dev_mounted value
 * - Verify that dev_umounted == dev_mounted
 * - Verify that dev_mounted == DEVICE_DETACH_TEST_ROUNDS, where DEVICE_DETACH_TEST_ROUNDS - amount of rounds
 * - Uninstall TinyUSB Device stack
 *
 */
TEST_CASE("dconn_detection", "[esp_tinyusb][dconn]")
{
    unsigned int rounds = DEVICE_DETACH_TEST_ROUNDS;

    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_dconn_event_handler);
    tusb_cfg.descriptor.device = &test_device_descriptor;
    tusb_cfg.descriptor.full_speed_config = test_configuration_descriptor;
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.qualifier = &device_qualifier;
    tusb_cfg.descriptor.high_speed_config = test_configuration_descriptor;
#endif // TUD_OPT_HIGH_SPEED

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));

    dev_mounted = 0;
    dev_umounted = 0;

    while (rounds--) {
        // LOW to emulate disconnect USB device
        esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, USB_SRP_BVALID_IN_IDX, false);
        vTaskDelay(pdMS_TO_TICKS(DEVICE_DETACH_ROUND_DELAY_MS));
        // HIGH to emulate connect USB device
        esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, USB_SRP_BVALID_IN_IDX, false);
        vTaskDelay(pdMS_TO_TICKS(DEVICE_DETACH_ROUND_DELAY_MS));
    }

    // Verify
    TEST_ASSERT_EQUAL(dev_umounted, dev_mounted);
    TEST_ASSERT_EQUAL(DEVICE_DETACH_TEST_ROUNDS, dev_mounted);

    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}
#endif // SOC_USB_OTG_SUPPORTED
