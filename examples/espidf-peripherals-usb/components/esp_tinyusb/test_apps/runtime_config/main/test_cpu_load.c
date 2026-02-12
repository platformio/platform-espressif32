/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "soc/soc_caps.h"

#if SOC_USB_OTG_SUPPORTED

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "unity.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "device_handling.h"

// Enable this, if you need to see the stat for all tasks
#define STAT_CONFIG_ALL_TASKS               0
// Increase this if test_cpu_load_init or test_cpu_load_measure fail due to insufficient array size
#define STAT_CONFIG_ARRAY_SIZE_OFFSET       5

// TinyUSB task related variables
const static char *TINYUSB_TASK_NAME = "TinyUSB";
static uint32_t _tinyusb_run_time = 0;
static uint32_t _tinyusb_cpu_load = 0;

// Arrays and variables for CPU load measurement
static TaskStatus_t *start_array = NULL;
static TaskStatus_t *end_array = NULL;
static UBaseType_t start_array_size;
static UBaseType_t end_array_size;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
static configRUN_TIME_COUNTER_TYPE start_run_time;
static configRUN_TIME_COUNTER_TYPE end_run_time;
#else
static uint32_t start_run_time;
static uint32_t end_run_time;
#endif // ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#define CPU_AMOUNT_OF_CORES    CONFIG_FREERTOS_NUMBER_OF_CORES
#else
#define CPU_AMOUNT_OF_CORES    portNUM_PROCESSORS
#endif // ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)

/**
 * @brief Initialize CPU load measurement
 */
static void test_cpu_load_init(void)
{
    printf("Starting TinyUSB load measurement test...\n");

    // Allocate array to store current task states
    start_array_size = uxTaskGetNumberOfTasks() + STAT_CONFIG_ARRAY_SIZE_OFFSET;
    start_array = malloc(sizeof(TaskStatus_t) * start_array_size);
    TEST_ASSERT_NOT_NULL_MESSAGE(start_array, "Insufficient memory to allocate internal arrays");

    // Get current task states
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    TEST_ASSERT_MESSAGE(start_array_size != 0, "Insufficient array size for uxTaskGetSystemState. Try increasing STAT_CONFIG_ARRAY_SIZE_OFFSET");
}

/**
 * @brief Measure CPU load since initialization
 *
 * Note: test_cpu_load_init() must be called before this function
 */
static void test_cpu_load_measure(void)
{
    end_array_size = uxTaskGetNumberOfTasks() + STAT_CONFIG_ARRAY_SIZE_OFFSET;
    end_array = malloc(sizeof(TaskStatus_t) * end_array_size);
    TEST_ASSERT_NOT_NULL_MESSAGE(end_array, "Insufficient memory to allocate internal arrays");

    // Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    TEST_ASSERT_MESSAGE(end_array_size != 0, "Insufficient array size for uxTaskGetSystemState. Try increasing STAT_CONFIG_ARRAY_SIZE_OFFSET");

    // Calculate total_elapsed_time in units of run time stats clock period.
    uint32_t total_elapsed_time = (end_run_time - start_run_time);
    TEST_ASSERT_MESSAGE(total_elapsed_time != 0, "Delay duration too short");

#if (STAT_CONFIG_ALL_TASKS)
    printf("\n%-20s %10s %8s\n", "Name", "Run time", "CPU load");
#endif // STAT_CONFIG_ALL_TASKS
    // Print TinyUSB statistics only
    for (int i = 0; i < start_array_size; i++) {
        int k = -1;
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {
                k = j;
                // Mark that task have been matched by overwriting their handles
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
        // Check if matching task found
        if (k >= 0) {
            uint32_t task_elapsed_time = end_array[k].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
            uint32_t percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * CPU_AMOUNT_OF_CORES);
#if (STAT_CONFIG_ALL_TASKS)
            printf("%-20.20s %10" PRIu32 " %7" PRIu32 "%%\n",
                   start_array[i].pcTaskName,   // left-aligned, max 20 chars
                   task_elapsed_time,           // right-aligned, width 10
                   percentage_time);            // right-aligned, width 7 + '%' char
#endif // STAT_CONFIG_ALL_TASKS
            // Save the TinyUSB task stats for test validation
            if (strcmp(start_array[i].pcTaskName, TINYUSB_TASK_NAME) == 0) {
                _tinyusb_run_time = task_elapsed_time;
                _tinyusb_cpu_load = percentage_time;
            }
        }
    }
#if (STAT_CONFIG_ALL_TASKS)
    // Print unmatched tasks
    for (int i = 0; i < start_array_size; i++) {
        if (start_array[i].xHandle != NULL) {
            printf("%-20.20s %10s\n",
                   start_array[i].pcTaskName,   // left-aligned, max 20 chars
                   "Deleted");                  // right-aligned, width 10
        }
    }
    for (int i = 0; i < end_array_size; i++) {
        if (end_array[i].xHandle != NULL) {
            printf("%-20.20s %10s\n",
                   end_array[i].pcTaskName,     // left-aligned, max 20 chars
                   "Created");                  // right-aligned, width 10
        }
    }
#endif // STAT_CONFIG_ALL_TASKS
    free(start_array);
    free(end_array);
    printf("CPU load measurement test completed.\n");
}

/**
 * @brief Test TinyUSB CPU load measurement
 *
 * Scenario:
 * - Install TinyUSB driver with default configuration
 * - wait for device connection
 * - measure CPU load
 * - uninstall driver
 * - show results
 */
TEST_CASE("[CPU load] Install & Uninstall, default configuration", "[cpu_load]")
{
#if (!CONFIG_FREERTOS_UNICORE)
    // Allow other core to finish initialization
    vTaskDelay(pdMS_TO_TICKS(100));
#endif // (!CONFIG_FREERTOS_UNICORE)

    // Install TinyUSB driver
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(test_device_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_install(&tusb_cfg));

    // Initialize CPU load measurement
    test_cpu_load_init();

    // Wait for the device to be mounted and enumerated by the Host
    test_device_wait();
    printf("\t -> Device connected\n");

    // Measure CPU load
    test_cpu_load_measure();

    // Uninstall TinyUSB driver
    TEST_ASSERT_EQUAL(ESP_OK, tinyusb_driver_uninstall());

    // Show results
    printf("TinyUSB Run time: %" PRIu32 " ticks\n", _tinyusb_run_time);
    printf("TinyUSB CPU load: %" PRIu32 " %%\n", _tinyusb_cpu_load);
}

#endif // SOC_USB_OTG_SUPPORTED
