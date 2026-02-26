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

#include "unity.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

static const char *TAG = "vendor_test";

char buffer_in[64];
#if (TUSB_VERSION_MINOR >= 17)
void tud_vendor_rx_cb(uint8_t itf, uint8_t const *buffer, uint16_t bufsize)
#else
void tud_vendor_rx_cb(uint8_t itf)
#endif // TUSB_VERSION_MINOR
{
    ESP_LOGI(TAG, "tud_vendor_rx_cb(itf=%d)", itf);
    int available = tud_vendor_n_available(itf);
    int read = tud_vendor_n_read(itf, buffer_in, available);
    ESP_LOGI(TAG, "actual read: %d. buffer message: %s", read, buffer_in);
}

// Invoked when a control transfer occurred on an interface of this class
// Driver response accordingly to the request and the transfer stage (setup/data/ack)
// return false to stall control endpoint (e.g unsupported request)
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    // nothing to with DATA & ACK stage
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }
    // stall unknown request
    return false;
}

/**
 * @brief TinyUSB Vendor specific testcase
 */
TEST_CASE("tinyusb_vendor", "[esp_tinyusb][vendor]")
{
    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
}

#endif
