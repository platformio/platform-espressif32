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
#include "freertos/event_groups.h"
//
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
//
#include "unity.h"
#include "tinyusb.h"
#include "tinyusb_msc.h"

//
// ========================== Test Configuration Parameters =====================================
//

#define MULTIPLE_THREADS_TASKS_NUM 5

#define BIT_0   ( 1 << 0 )

EventGroupHandle_t xCreatedEventGroup;

static int nb_of_success;
static SemaphoreHandle_t nb_of_success_mutex = NULL;

void init_success_counter(void)
{
    nb_of_success = 0;
    nb_of_success_mutex = xSemaphoreCreateMutex();
    TEST_ASSERT_NOT_NULL_MESSAGE(nb_of_success_mutex, "Failed to create mutex");
}

static inline void increase_nb_of_success(void)
{
    xSemaphoreTake(nb_of_success_mutex, portMAX_DELAY);
    nb_of_success++;
    xSemaphoreGive(nb_of_success_mutex);
}

static inline int get_nb_of_success(void)
{
    int value;
    xSemaphoreTake(nb_of_success_mutex, portMAX_DELAY);
    value = nb_of_success;
    xSemaphoreGive(nb_of_success_mutex);
    return value;
}

void delete_success_counter(void)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(nb_of_success_mutex, "Mutex is NULL");
    vSemaphoreDelete(nb_of_success_mutex);
}

static void test_task_install(void *arg)
{
    TaskHandle_t parent_task_handle = (TaskHandle_t)arg;

    tinyusb_msc_driver_config_t driver_cfg = {
        .callback = NULL,                   // No callback for mount changed events
        .callback_arg = NULL,               // No additional argument for the callback
    };

    EventBits_t uxBits = xEventGroupWaitBits(
                             xCreatedEventGroup,   /* The event group being tested. */
                             BIT_0,         /* The bits within the event group to wait for. */
                             pdTRUE,        /* BIT_0 should be cleared before returning. */
                             pdFALSE,       /* Don't wait for both bits, either bit will do. */
                             pdMS_TO_TICKS(1000)); /* Wait a maximum of 1s for either bit to be set. */

    if (uxBits) {
        if (tinyusb_msc_install_driver(&driver_cfg) == ESP_OK) {
            vTaskDelay(10); // Let the other tasks to run
            TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_msc_uninstall_driver(), "Failed to uninstall TinyUSB MSC driver");
            increase_nb_of_success();
        }
        // Notify the parent task that the task completed the job
        xTaskNotifyGive(parent_task_handle);
    }

    // Delete task
    vTaskDelete(NULL);
}


/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Trying to install MSC driver from several tasks
 */
TEST_CASE("MSC: driver multitask access", "[ci][driver]")
{
    init_success_counter();

    xCreatedEventGroup = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL_MESSAGE(xCreatedEventGroup, "Failed to create event group");

    // Create tasks that will start the driver
    for (int i = 0; i < MULTIPLE_THREADS_TASKS_NUM; i++) {
        TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(test_task_install,
                                              "InstallTask",
                                              4096,
                                              (void *) xTaskGetCurrentTaskHandle(),
                                              4 + i,
                                              NULL));
    }
    // Set the event group bits to start the tasks
    xEventGroupSetBits(xCreatedEventGroup, BIT_0);
    // Wait until all tasks are finished
    vTaskDelay(pdMS_TO_TICKS(2000));
    // Check if all tasks finished, we should get all notification from the tasks
    TEST_ASSERT_EQUAL_MESSAGE(MULTIPLE_THREADS_TASKS_NUM, ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)), "Not all tasks finished");
    // There should be only one task that was able to install the driver
    TEST_ASSERT_EQUAL_MESSAGE(1, get_nb_of_success(), "Only one task should be able to install the driver");
    vEventGroupDelete(xCreatedEventGroup);
    delete_success_counter();
}

#endif // SOC_USB_OTG_SUPPORTED
