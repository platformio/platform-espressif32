/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
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
#include "tinyusb_cdc_acm.h"
#include "tusb_config.h"
#include "sdkconfig.h"

#define TINYUSB_CDC_RX_BUFSIZE                  CONFIG_TINYUSB_CDC_RX_BUFSIZE
#define SUSPEND_RESUME_TEST_ITERATIONS          5
#define DEVICE_EVENT_WAIT_MS                    5000

#define EVENT_BITS_ATTACHED                     (1U << 0)   /**< Device attached event */
#define EVENT_BITS_SUSPENDED_REMOTE_WAKE_EN     (1U << 1)   /**< Device suspended with remote wakeup enabled event */
#define EVENT_BITS_SUSPENDED_REMOTE_WAKE_DIS    (1U << 2)   /**< Device suspended with remote wakeup disabled event */
#define EVENT_BITS_RESUMED                      (1U << 3)   /**< Device resumed event */

static char err_msg_buf[128];
const static char *TAG = "PM_Device";
static TaskHandle_t main_task_hdl = NULL;

static const tusb_desc_device_t cdc_device_descriptor = {
    .bLength = sizeof(cdc_device_descriptor),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = TINYUSB_ESPRESSIF_VID,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
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

static tinyusb_config_cdcacm_t acm_cfg = {
    .cdc_port = TINYUSB_CDC_ACM_0,
    .callback_rx = NULL,
    .callback_rx_wanted_char = NULL,
    .callback_line_state_changed = NULL,
    .callback_line_coding_changed = NULL
};

static const uint16_t cdc_desc_config_len = TUD_CONFIG_DESC_LEN + CFG_TUD_CDC * TUD_CDC_DESC_LEN;
static const uint8_t cdc_desc_configuration_remote_wakeup[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, cdc_desc_config_len, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, (TUD_OPT_HIGH_SPEED ? 512 : 64)),
};

/**
 * @brief CDC Device RX callback for tinyusb_suspend_resume_events test case
 */
static void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    if (main_task_hdl != NULL) {
        ESP_LOGI(TAG, "RX data cb");
        xTaskNotifyGive(main_task_hdl);
    }
}

/**
 * @brief Device event handler for tinyusb_suspend_resume_events test case
 */
static void test_suspend_resume_event_handler(tinyusb_event_t *event, void *arg)
{
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        printf("TINYUSB_EVENT_ATTACHED\n");
        break;
    case TINYUSB_EVENT_DETACHED:
        printf("TINYUSB_EVENT_DETACHED\n");
        break;
    case TINYUSB_EVENT_SUSPENDED:
        printf("TINYUSB_EVENT_SUSPENDED\n");
        break;
    case TINYUSB_EVENT_RESUMED:
        printf("TINYUSB_EVENT_RESUMED\n");
        break;
    default:
        break;
    }
}

/**
 * @brief Tinyusb power management suspend/resume events
 *
 * Tests TINYUSB_EVENT_SUSPENDED and TINYUSB_EVENT_RESUMED esp_tinyusb events
 *
 * Pytest expects TINYUSB_EVENT_SUSPENDED event - because of auto suspend
 * Pytest sends data to device to resume it
 * Device resumes, receives and validates the data, sends a response and goes to suspended state (auto suspend)
 * Pytest expect TINYUSB_EVENT_SUSPENDED ...
 */
TEST_CASE("tinyusb_suspend_resume_events", "[esp_tinyusb][device_pm_suspend_resume]")
{
    // Get current task handle for task notification from the RX callback
    main_task_hdl = xTaskGetCurrentTaskHandle();

    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_suspend_resume_event_handler);
    tusb_cfg.descriptor.device = &cdc_device_descriptor;
    tusb_cfg.descriptor.full_speed_config = cdc_desc_configuration_remote_wakeup;
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.qualifier = &device_qualifier;
    tusb_cfg.descriptor.high_speed_config = cdc_desc_configuration_remote_wakeup;
#endif // TUD_OPT_HIGH_SPEED

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    acm_cfg.callback_rx = &tinyusb_cdc_rx_callback;

    // Init CDC device
    TEST_ASSERT_FALSE(tinyusb_cdcacm_initialized(TINYUSB_CDC_ACM_0));
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_cdcacm_init(&acm_cfg));
    TEST_ASSERT_TRUE(tinyusb_cdcacm_initialized(TINYUSB_CDC_ACM_0));

    uint8_t buf[TINYUSB_CDC_RX_BUFSIZE + 1];
    const char expect_reply[] = "Time to resume\r\n";
    const char send_message[] = "Time to suspend\r\n";

    int test_iterations = 0;
    do {
        // Wait for new data from the host (Sent by pytest)
        if (pdTRUE == ulTaskNotifyTake(true, pdMS_TO_TICKS(5000))) {

            size_t rx_size = 0;
            ESP_ERROR_CHECK(tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf, TINYUSB_CDC_RX_BUFSIZE, &rx_size));
            if (rx_size > 0) {
                ESP_LOGI(TAG, "Intf %d, RX %d bytes", TINYUSB_CDC_ACM_0, rx_size);
                // Check if received string is equal to expect_reply string
                TEST_ASSERT_EQUAL_UINT8_ARRAY(expect_reply, buf, sizeof(expect_reply) - 1);

                // Reply to the host with send_message string
                strncpy((char *)buf, send_message, sizeof(send_message) - 1);
                tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, buf, sizeof(send_message) - 1);
                tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
                test_iterations++;
            }
        } else {
            TEST_FAIL_MESSAGE("RX Data CB not received on time");
        }
    } while (test_iterations <= SUSPEND_RESUME_TEST_ITERATIONS);

    ESP_LOGI(TAG, "Cleanup");
    tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
    tinyusb_driver_uninstall();
}

/**
 * @brief Dummy CDC Device RX callback for tinyusb_remote_wakeup_reporting test case
 */
static void tinyusb_cdc_rx_callback_dmy(int itf, cdcacm_event_t *event)
{
}

/**
 * @brief Device event handler for tinyusb_remote_wakeup_reporting test case
 */
static void test_remote_wake_event_handler(tinyusb_event_t *event, void *arg)
{
    uint32_t event_bits = UINT32_MAX;

    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        printf("TINYUSB_EVENT_ATTACHED\n");
        event_bits = EVENT_BITS_ATTACHED;
        break;
    case TINYUSB_EVENT_DETACHED:
        printf("TINYUSB_EVENT_DETACHED\n");
        return;
    case TINYUSB_EVENT_SUSPENDED:
        if (event->suspended.remote_wakeup) {
            printf("TINYUSB_EVENT_SUSPENDED_REMOTE_WAKE_EN\n");
            event_bits = EVENT_BITS_SUSPENDED_REMOTE_WAKE_EN;
        } else {
            printf("TINYUSB_EVENT_SUSPENDED_REMOTE_WAKE_DIS\n");
            event_bits = EVENT_BITS_SUSPENDED_REMOTE_WAKE_DIS;
        }
        break;
    case TINYUSB_EVENT_RESUMED:
        printf("TINYUSB_EVENT_RESUMED\n");
        event_bits = EVENT_BITS_RESUMED;
        break;
    default:
        return;
    }

    if (main_task_hdl) {
        xTaskNotify(main_task_hdl, event_bits, eSetBits);
    }
}

/**
 * @brief Expect device event
 *
 * @param[in] expected_event Expected device event
 * @param[in] ticks time to expect the event
 * @param[in] file file from which the function was called
 * @param[in] line line from which the function was called
 */
static void expect_device_event_impl(const uint32_t expected_event, TickType_t ticks, const char *file, int line)
{
    uint32_t notify_bits = 0;
    if (pdTRUE == xTaskNotifyWait(0, UINT32_MAX, &notify_bits, ticks)) {
        if (expected_event != notify_bits) {
            snprintf(err_msg_buf, sizeof(err_msg_buf),
                     "Unexpected event at %s:%d\n %ld expected, %ld delivered\n",
                     file, line, expected_event, notify_bits);
            TEST_FAIL_MESSAGE(err_msg_buf);
        }
    } else {
        snprintf(err_msg_buf, sizeof(err_msg_buf),
                 "Event %ld at %s:%d\n was not delivered on time",
                 expected_event, file, line);
        TEST_FAIL_MESSAGE(err_msg_buf);
    }
}
#define expect_device_event(expected_event, ticks) expect_device_event_impl((expected_event), (ticks), __FILE__, __LINE__)


/**
 * @brief Tinyusb power management remote wakeup
 *
 * Tests device reporting remote wakeup capability
 *
 * - Install device with remote wakeup allowed in it's configuration descriptor, but disabled (by default after reset)
 * - Expect auto suspend device event with remote wakeup disabled
 * - Pytest enables the remote wakeup feature by a ctrl transfer
 * - Expect device resume event (because of ctrl transfer)
 * - Expect auto suspend device event with remote wakeup enabled
 * - Signalize remote wakeup and expect resume event
 */
TEST_CASE("tinyusb_remote_wakeup_reporting", "[esp_tinyusb][device_pm_remote_wake]")
{
    // Get current tak handle for the device event handler
    main_task_hdl = xTaskGetCurrentTaskHandle();

    // Install TinyUSB driver, device with remote wakeup enabled
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_remote_wake_event_handler);
    tusb_cfg.descriptor.device = &cdc_device_descriptor;
    tusb_cfg.descriptor.full_speed_config = cdc_desc_configuration_remote_wakeup;
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.qualifier = &device_qualifier;
    tusb_cfg.descriptor.high_speed_config = cdc_desc_configuration_remote_wakeup;
#endif // TUD_OPT_HIGH_SPEED

    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));
    acm_cfg.callback_rx = &tinyusb_cdc_rx_callback_dmy;

    // Init CDC device
    TEST_ASSERT_FALSE(tinyusb_cdcacm_initialized(TINYUSB_CDC_ACM_0));
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_cdcacm_init(&acm_cfg));
    TEST_ASSERT_TRUE(tinyusb_cdcacm_initialized(TINYUSB_CDC_ACM_0));

    // Expect attach event and auto suspend event with remote wakeup disabled by default
    expect_device_event(EVENT_BITS_ATTACHED, pdMS_TO_TICKS(DEVICE_EVENT_WAIT_MS));
    expect_device_event(EVENT_BITS_SUSPENDED_REMOTE_WAKE_DIS, pdMS_TO_TICKS(DEVICE_EVENT_WAIT_MS));

    // Try to signalize remote wakeup, when the host did not enable it
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, tinyusb_remote_wakeup());

    // Pytest enables remote wakeup on the device by sending a ctrl transfer to the the device
    // Expect the device to:
    //  - resumed (because of the ctrl transfer)
    //  - auto suspended with remote wakeup enabled

    expect_device_event(EVENT_BITS_RESUMED, pdMS_TO_TICKS(DEVICE_EVENT_WAIT_MS));
    expect_device_event(EVENT_BITS_SUSPENDED_REMOTE_WAKE_EN, pdMS_TO_TICKS(DEVICE_EVENT_WAIT_MS));

    // Signalize remote wakeup and expect resume event
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_remote_wakeup());
    expect_device_event(EVENT_BITS_RESUMED, pdMS_TO_TICKS(DEVICE_EVENT_WAIT_MS));

    ESP_LOGI(TAG, "Cleanup");
    tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
    tinyusb_driver_uninstall();
}

#endif
