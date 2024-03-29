/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
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
#include "tusb_tasks.h"

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

void test_bvalid_sig_mount_cb(void)
{
    dev_mounted++;
}

void test_bvalid_sig_umount_cb(void)
{
    dev_umounted++;
}

TEST_CASE("bvalid_signal", "[esp_tinyusb][usb_device]")
{
    unsigned int rounds = DEVICE_DETACH_TEST_ROUNDS;

    // Install TinyUSB driver
    const tinyusb_config_t tusb_cfg = {
        .external_phy = false,
        .device_descriptor = &test_device_descriptor,
        .configuration_descriptor = test_configuration_descriptor,
    };
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
    TEST_ASSERT_EQUAL(ESP_OK, tusb_stop_task());
}
#endif // SOC_USB_OTG_SUPPORTED
