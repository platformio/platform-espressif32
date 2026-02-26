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
#include "device_common.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_net.h"

//
// ========================== Test Configuration Parameters =====================================
//

#define TEST_DEVICE_PRESENCE_TIMEOUT_MS     5000 // Timeout for checking device presence

//
// ========================== TinyUSB General Device Descriptors ===============================
//


//
// ============================= TinyUSB NCM Initialization Tests =============================
//

static esp_err_t usb_recv_callback(void *buffer, uint16_t len, void *ctx)
{
    return ESP_OK;
}

static void wifi_pkt_free(void *eb, void *ctx)
{

}

/**
 * @brief Test case for installing TinyUSB NCM driver
 *
 * Scenario:
 * 1. Install TinyUSB NCM.
 * 3. Wait for the device to be recognized.
 * 4. Uninstall TinyUSB driver and deinitialize the NCM driver.
 */
TEST_CASE("NCM: driver install & uninstall", "[ci][driver]")
{
    tinyusb_net_config_t net_config = {
        .on_recv_callback = usb_recv_callback,
        .free_tx_buffer = wifi_pkt_free,
        .user_context = NULL,
    };
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_net_init(&net_config), "Failed to initialize TinyUSB NCM driver");

    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    test_device_wait();
    // Allow some time for the device to be recognized
    vTaskDelay(pdMS_TO_TICKS(TEST_DEVICE_PRESENCE_TIMEOUT_MS));

    tinyusb_net_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());
}

#endif // SOC_USB_OTG_SUPPORTED
