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
#include "esp_private/usb_phy.h"
//
#include "unity.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "test_task.h"
#include "sdkconfig.h"
#include "device_handling.h"

#define MULTIPLE_THREADS_TASKS_NUM 5

static SemaphoreHandle_t sem_done = NULL;
TaskHandle_t test_task_handles[MULTIPLE_THREADS_TASKS_NUM];

// Unlocked spinlock, ready to use
static portMUX_TYPE _spinlock = portMUX_INITIALIZER_UNLOCKED;
static volatile int nb_of_success = 0;

static void test_task_install(void *arg)
{
    (void) arg;
    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.phy.skip_setup = true; // Skip phy setup to allow multiple tasks to install the driver

    // Wait to be started by main thread
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (tinyusb_driver_install(&tusb_cfg) == ESP_OK) {
        test_device_wait();
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_driver_uninstall(), "Unable to uninstall driver after install in worker");
        taskENTER_CRITICAL(&_spinlock);
        nb_of_success++;
        taskEXIT_CRITICAL(&_spinlock);
    }

    // Notify the parent task that the task completed the job
    xSemaphoreGive(sem_done);
    // Delete task
    vTaskDelete(NULL);
}


static void test_task_uninstall(void *arg)
{
    (void) arg;
    // Wait to be started by main thread
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (tinyusb_driver_uninstall() == ESP_OK) {
        taskENTER_CRITICAL(&_spinlock);
        nb_of_success++;
        taskEXIT_CRITICAL(&_spinlock);
    }

    // Notify the parent task that the task completed the job
    xSemaphoreGive(sem_done);
    // Delete task
    vTaskDelete(NULL);
}

// USB PHY

static usb_phy_handle_t test_init_phy(void)
{
    usb_phy_handle_t phy_hdl = NULL;
    usb_phy_config_t phy_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_DEVICE,
    };
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, usb_new_phy(&phy_conf, &phy_hdl), "Unable to install USB PHY ");
    return phy_hdl;
}

static void test_deinit_phy(usb_phy_handle_t phy_hdl)
{
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, usb_del_phy(phy_hdl), "Unable to delete USB PHY ");
}

// ============================= Tests =========================================

/**
 * @brief TinyUSB Task specific testcase
 *
 * Scenario: Trying to install driver from several tasks
 * Note: when phy.skip_setup = false, the task access will be determined by the first task install the phy
 */
TEST_CASE("Multitask: Install", "[runtime_config][default]")
{
    usb_phy_handle_t phy_hdl = test_init_phy();

    // Create counting semaphore to wait for all tasks to complete
    sem_done = xSemaphoreCreateCounting(MULTIPLE_THREADS_TASKS_NUM, 0);
    TEST_ASSERT_NOT_NULL(sem_done);

    // No task are running yet
    nb_of_success = 0;

    // Create tasks that will start the driver
    for (int i = 0; i < MULTIPLE_THREADS_TASKS_NUM; i++) {
        TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(test_task_install,
                                              "InstallTask",
                                              4096,
                                              NULL,
                                              4 + i,
                                              &test_task_handles[i]));
    }

    // Start all tasks
    for (int i = 0; i < MULTIPLE_THREADS_TASKS_NUM; i++) {
        xTaskNotifyGive(test_task_handles[i]);
    }

    // Wait for all tasks to complete
    for (int i = 0; i < MULTIPLE_THREADS_TASKS_NUM; i++) {
        TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, xSemaphoreTake(sem_done, pdMS_TO_TICKS(5000)), "Not all tasks completed in time");
    }

    // There should be only one task that was able to install the driver
    TEST_ASSERT_EQUAL_MESSAGE(1, nb_of_success, "Only one task should be able to install the driver");
    // Clean-up
    test_deinit_phy(phy_hdl);
    vSemaphoreDelete(sem_done);
}

TEST_CASE("Multitask: Uninstall", "[runtime_config][default]")
{
    usb_phy_handle_t phy_hdl = test_init_phy();
    // Create counting semaphore to wait for all tasks to complete
    sem_done = xSemaphoreCreateCounting(MULTIPLE_THREADS_TASKS_NUM, 0);
    TEST_ASSERT_NOT_NULL(sem_done);

    // No task are running yet
    nb_of_success = 0;

    // Install the driver once
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    tusb_cfg.phy.skip_setup = true; // Skip phy setup to allow multiple tasks
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, tinyusb_driver_install(&tusb_cfg), "Unable to install TinyUSB driver ");
    // Create tasks that will uninstall the driver
    for (int i = 0; i < MULTIPLE_THREADS_TASKS_NUM; i++) {
        TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(test_task_uninstall,
                                              "UninstallTask",
                                              4096,
                                              NULL,
                                              4 + i,
                                              &test_task_handles[i]));
    }

    // Start all tasks
    for (int i = 0; i < MULTIPLE_THREADS_TASKS_NUM; i++) {
        xTaskNotifyGive(test_task_handles[i]);
    }
    // Wait for all tasks to complete
    for (int i = 0; i < MULTIPLE_THREADS_TASKS_NUM; i++) {
        TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, xSemaphoreTake(sem_done, pdMS_TO_TICKS(5000)), "Not all tasks completed in time");
    }

    // There should be only one task that was able to uninstall the driver
    TEST_ASSERT_EQUAL_MESSAGE(1, nb_of_success, "Only one task should be able to uninstall the driver");

    // Clean-up
    test_deinit_phy(phy_hdl);
    vSemaphoreDelete(sem_done);
}


#endif // SOC_USB_OTG_SUPPORTED
