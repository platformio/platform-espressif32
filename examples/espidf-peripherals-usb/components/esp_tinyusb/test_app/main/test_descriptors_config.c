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
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "unity.h"
#include "tinyusb.h"
#include "tusb_tasks.h"

#define DEVICE_MOUNT_TIMEOUT_MS         5000

// ========================= TinyUSB descriptors ===============================
#define TUSB_DESC_TOTAL_LEN         (TUD_CONFIG_DESC_LEN)

static uint8_t const test_fs_configuration_descriptor[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 0, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_SELF_POWERED | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
};

#if (TUD_OPT_HIGH_SPEED)
static uint8_t const test_hs_configuration_descriptor[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 0, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_SELF_POWERED | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
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

// ========================== Private logic ====================================
SemaphoreHandle_t desc_config_device_mounted = NULL;

static bool __test_prep(void)
{
    desc_config_device_mounted = xSemaphoreCreateBinary();
    return (desc_config_device_mounted != NULL);
}

static esp_err_t __test_wait_conn(void)
{
    if (!desc_config_device_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    return ( xSemaphoreTake(desc_config_device_mounted, pdMS_TO_TICKS(DEVICE_MOUNT_TIMEOUT_MS))
             ? ESP_OK
             : ESP_ERR_TIMEOUT );
}

static void __test_conn(void)
{
    if (desc_config_device_mounted) {
        xSemaphoreGive(desc_config_device_mounted);
    }
}

static void __test_free(void)
{
    if (desc_config_device_mounted) {
        vSemaphoreDelete(desc_config_device_mounted);
    }
}

// ========================== Callbacks ========================================
// Invoked when device is mounted
void test_descriptors_config_mount_cb(void)
{
    __test_conn();
}

void test_descriptors_config_umount_cb(void)
{

}

TEST_CASE("descriptors_config_all_default", "[esp_tinyusb][usb_device]")
{
    TEST_ASSERT_EQUAL(true, __test_prep());
    // Install TinyUSB driver
    const tinyusb_config_t tusb_cfg = {
        .external_phy = false,
        .device_descriptor = NULL,
        .configuration_descriptor = NULL,
#if (TUD_OPT_HIGH_SPEED)
        .hs_configuration_descriptor = NULL,
#endif // TUD_OPT_HIGH_SPEED
    };
    // Install
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    // Wait for mounted callback
    TEST_ASSERT_EQUAL(ESP_OK, __test_wait_conn());
    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    TEST_ASSERT_EQUAL(ESP_OK, tusb_stop_task());
    __test_free();
}

TEST_CASE("descriptors_config_device", "[esp_tinyusb][usb_device]")
{
    TEST_ASSERT_EQUAL(true, __test_prep());
    // Install TinyUSB driver
    const tinyusb_config_t tusb_cfg = {
        .external_phy = false,
        .device_descriptor = &test_device_descriptor,
        .configuration_descriptor = NULL,
#if (TUD_OPT_HIGH_SPEED)
        .hs_configuration_descriptor = NULL,
#endif // TUD_OPT_HIGH_SPEED
    };
    // Install
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    // Wait for mounted callback
    TEST_ASSERT_EQUAL(ESP_OK, __test_wait_conn());
    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    TEST_ASSERT_EQUAL(ESP_OK, tusb_stop_task());
    __test_free();
}

TEST_CASE("descriptors_config_device_and_config", "[esp_tinyusb][usb_device]")
{
    TEST_ASSERT_EQUAL(true, __test_prep());
    // Install TinyUSB driver
    const tinyusb_config_t tusb_cfg = {
        .external_phy = false,
        .device_descriptor = &test_device_descriptor,
        .configuration_descriptor = test_fs_configuration_descriptor,
#if (TUD_OPT_HIGH_SPEED)
        .hs_configuration_descriptor = NULL,
#endif // TUD_OPT_HIGH_SPEED
    };
    // Install
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    // Wait for mounted callback
    TEST_ASSERT_EQUAL(ESP_OK, __test_wait_conn());
    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    TEST_ASSERT_EQUAL(ESP_OK, tusb_stop_task());
    __test_free();
}

#if (TUD_OPT_HIGH_SPEED)
TEST_CASE("descriptors_config_device_and_fs_config_only", "[esp_tinyusb][usb_device]")
{
    TEST_ASSERT_EQUAL(true, __test_prep());
    // Install TinyUSB driver
    const tinyusb_config_t tusb_cfg = {
        .external_phy = false,
        .device_descriptor = &test_device_descriptor,
        .configuration_descriptor = test_fs_configuration_descriptor,
        .hs_configuration_descriptor = NULL,
    };
    // Install
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    // Wait for mounted callback
    TEST_ASSERT_EQUAL(ESP_OK, __test_wait_conn());
    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    TEST_ASSERT_EQUAL(ESP_OK, tusb_stop_task());
    __test_free();
}

TEST_CASE("descriptors_config_device_and_hs_config_only", "[esp_tinyusb][usb_device]")
{
    TEST_ASSERT_EQUAL(true, __test_prep());
    // Install TinyUSB driver
    const tinyusb_config_t tusb_cfg = {
        .external_phy = false,
        .device_descriptor = &test_device_descriptor,
        .configuration_descriptor = NULL,
        .hs_configuration_descriptor = test_hs_configuration_descriptor,
    };
    // Install
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    // Wait for mounted callback
    TEST_ASSERT_EQUAL(ESP_OK, __test_wait_conn());
    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    TEST_ASSERT_EQUAL(ESP_OK, tusb_stop_task());
    __test_free();
}

TEST_CASE("descriptors_config_all_configured", "[esp_tinyusb][usb_device]")
{
    TEST_ASSERT_EQUAL(true, __test_prep());
    // Install TinyUSB driver
    const tinyusb_config_t tusb_cfg = {
        .external_phy = false,
        .device_descriptor = &test_device_descriptor,
        .fs_configuration_descriptor = test_fs_configuration_descriptor,
        .hs_configuration_descriptor = test_hs_configuration_descriptor,
    };
    // Install
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    // Wait for mounted callback
    TEST_ASSERT_EQUAL(ESP_OK, __test_wait_conn());
    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    TEST_ASSERT_EQUAL(ESP_OK, tusb_stop_task());
    __test_free();
}
#endif // TUD_OPT_HIGH_SPEED

#endif // SOC_USB_OTG_SUPPORTED
