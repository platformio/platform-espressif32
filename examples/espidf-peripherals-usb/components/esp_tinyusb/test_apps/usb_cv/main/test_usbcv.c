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
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"
//
#include "class/hid/hid_device.h"
//
#include "unity.h"
#include "device_common.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "storage_common.h"


//
// ========================== Test Configuration Parameters =====================================
//

#define TEST_USBCV_TEST_TIMEOUT_MS   150000          // Timeout for USBCV compliance test
#define TEST_USBCV_REMOTE_WAKEUP_DELAY_MS 1000      // Delay for remote wakeup

static esp_timer_handle_t test_remote_wakeup_timer = NULL;

static void test_remote_wakeup_timer_callback(void *arg)
{
    printf("Remote wakeup...\n");
    tud_remote_wakeup();
}

static void test_remote_wakeup_timer_init(void)
{
    esp_timer_create_args_t timer_args = {
        .callback = &test_remote_wakeup_timer_callback,
        .name = "test_remote_wakeup_timer",
    };
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, esp_timer_create(&timer_args, &test_remote_wakeup_timer), "Failed to create remote wakeup timer");
}

static void test_remote_wakeup_timer_deinit(void)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(test_remote_wakeup_timer, "Remote wakeup timer is not initialized");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, esp_timer_delete(test_remote_wakeup_timer), "Failed to delete remote wakeup timer");
    test_remote_wakeup_timer = NULL;
}

//
// ========================== TinyUSB HID Device Descriptors ===============================
//

#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

/**
 * @brief HID report descriptor
 *
 * In this example we implement Keyboard + Mouse HID device,
 * so we must define both report descriptors
 */
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE))
};

/**
 * @brief Configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and 1 HID interface
 */
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

//
// ========================== TinyUSB MSC Storage Event Handling =================================
//

//
// ========================== TinyUSB MSC Storage Initialization Tests =============================
//

//
// =================================== TinyUSB callbacks ===========================================
//

// Invoked when received GET HID REPORT DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    // We use only one interface and one HID report descriptor, so we can ignore parameter 'instance'
    return hid_report_descriptor;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;

}



// Invoked when the device is suspended
void tud_suspend_cb(bool remote_wakeup_en)
{
    printf("Device suspended, remote wakeup enabled: %s\n", remote_wakeup_en ? "true" : "false");
    if (remote_wakeup_en) {
        // If remote wakeup is enabled, we can wake up the host by sending a resume signal
        // tinyusb_msc_storage_resume();
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, esp_timer_start_once(test_remote_wakeup_timer, TEST_USBCV_REMOTE_WAKEUP_DELAY_MS * 1000), "Failed to start remote wakeup timer");
    }

}

// Invoked when the device resumes from suspend
void tud_resume_cb(void)
{
    printf("Device resumed from suspend\n");
}

TEST_CASE("USBCV: HID Device", "[hid]")
{
    // Initialize the remote wakeup timer
    test_remote_wakeup_timer_init();
    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);

    tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor;
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.high_speed_config = hid_configuration_descriptor;
#endif // TUD_OPT_HIGH_SPEED

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));


    printf("Device is configured, launch the USBCV compliance test..\n");
    vTaskDelay(pdMS_TO_TICKS(TEST_USBCV_TEST_TIMEOUT_MS));

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    test_remote_wakeup_timer_deinit();
}

TEST_CASE("USBCV: MSC Device", "[msc]")
{
    // Initialize the remote wakeup timer
    test_remote_wakeup_timer_init();

    wl_handle_t wl_handle = WL_INVALID_HANDLE;
    storage_init_spiflash(&wl_handle);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(WL_INVALID_HANDLE, wl_handle, "Wear leveling handle is invalid, check the partition configuration");

    tinyusb_msc_storage_config_t config = {
        .medium.wl_handle = wl_handle,                      // Set the context to the wear leveling handle
    };

    // Initialize TinyUSB MSC storage
    tinyusb_msc_storage_handle_t storage_hdl = NULL;
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_new_storage_spiflash(&config, &storage_hdl), "Failed to initialize TinyUSB MSC storage with SPIFLASH");
    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));

    printf("Device is configured, launch the USBCV compliance test..\n");
    vTaskDelay(pdMS_TO_TICKS(TEST_USBCV_TEST_TIMEOUT_MS));

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    storage_deinit_spiflash(wl_handle);
    tinyusb_msc_delete_storage(storage_hdl);
    test_remote_wakeup_timer_deinit();
}

#if (SOC_USB_OTG_PERIPH_NUM > 1)
// ESP32-P4 has both Full-speed and High-speed USB OTG ports, so we can test both

TEST_CASE("USBCV: HID Device on Full-speed port", "[hid][full_speed]")
{
    // Initialize the remote wakeup timer
    test_remote_wakeup_timer_init();
    // Install TinyUSB driver on Full-speed port
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);

    tusb_cfg.port = TINYUSB_PORT_FULL_SPEED_0;
    tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor;

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));

    printf("Device is configured, launch the USBCV compliance test..\n");
    vTaskDelay(pdMS_TO_TICKS(TEST_USBCV_TEST_TIMEOUT_MS));

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
    test_remote_wakeup_timer_deinit();
}
#endif // (SOC_USB_OTG_PERIPH_NUM > 1)

#endif // SOC_USB_OTG_SUPPORTED
