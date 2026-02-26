/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "soc/soc_caps.h"

#if SOC_USB_OTG_SUPPORTED

#include "unity.h"
#include "test_msc_common.h"


#define TEST_QUEUE_LEN                          6 // Length of the queue for storage events
#define TEST_STORAGE_EVENT_TIMEOUT_MS           5000 // Timeout for waiting storage events

typedef struct {
    tinyusb_msc_event_id_t event_id;  /*!< Event ID */
} test_storage_event_t;

const char *storage_event_str[] = {
    "Mount Start",
    "Mount Complete",
    "Mount Failed",
    "Format Required",
};

static QueueHandle_t _test_storage_event_queue = NULL;

void test_storage_event_queue_setup(void)
{
    _test_storage_event_queue = xQueueCreate(TEST_QUEUE_LEN, sizeof(test_storage_event_t));
    TEST_ASSERT_NOT_NULL(_test_storage_event_queue);
}

void test_storage_event_queue_teardown(void)
{
    if (_test_storage_event_queue) {
        vQueueDelete(_test_storage_event_queue);
        _test_storage_event_queue = NULL;
    }
}

void test_storage_event_cb(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg)
{
    printf("Storage event\n");

    switch (event->id) {
    case TINYUSB_MSC_EVENT_MOUNT_START:
    case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
        printf("\t-> %s, mounted to %s\n", storage_event_str[event->id], (event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB) ? "USB" : "APP");
        break;
    case TINYUSB_MSC_EVENT_MOUNT_FAILED:
    case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
        printf("\t-> %s\n", storage_event_str[event->id]);
        break;
    default:
        printf("Unknown storage event: %d\n", event->id);
        TEST_ASSERT_MESSAGE(0, "Unknown storage event received");
        break;
    }

    test_storage_event_t msg = {
        .event_id = event->id,
    };
    xQueueSend(_test_storage_event_queue, &msg, portMAX_DELAY);
}

void test_storage_event_wait_callback(tinyusb_msc_event_id_t event_id)
{
    TEST_ASSERT_NOT_NULL(_test_storage_event_queue);
    // Wait for port callback to send an event message
    test_storage_event_t msg;
    BaseType_t ret = xQueueReceive(_test_storage_event_queue, &msg, pdMS_TO_TICKS(TEST_STORAGE_EVENT_TIMEOUT_MS));
    TEST_ASSERT_EQUAL_MESSAGE(pdPASS, ret, "MSC storage event not generated on time");
    // Check the contents of that event message
    printf("\tMSC storage event: %s\n", storage_event_str[msg.event_id]);
    TEST_ASSERT_EQUAL_MESSAGE(event_id, msg.event_id, "Unexpected MSC storage event type received");
}

#endif // SOC_USB_OTG_SUPPORTED
