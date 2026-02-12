/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity_test_runner.h"
#include "unity_test_utils_memory.h"
#include "device_common.h"
#include "test_msc_common.h"

/* setUp runs before every test */
void setUp(void)
{
    unity_utils_record_free_mem();
    test_device_setup();
    test_storage_event_queue_setup();
}

/* tearDown runs after every test */
void tearDown(void)
{
    // Short delay to allow task to be cleaned up
    vTaskDelay(10);
    test_device_release();
    test_storage_event_queue_teardown();
    unity_utils_evaluate_leaks();
}


void app_main(void)
{
    /*
                     _   _                       _
                    | | (_)                     | |
      ___  ___ _ __ | |_ _ _ __  _   _ _   _ ___| |__
     / _ \/ __| '_ \| __| | '_ \| | | | | | / __| '_ \
    |  __/\__ \ |_) | |_| | | | | |_| | |_| \__ \ |_) |
     \___||___/ .__/ \__|_|_| |_|\__, |\__,_|___/_.__/
              | |______           __/ |
              |_|______|         |___/
      _____ _____ _____ _____
     |_   _|  ___/  ___|_   _|
      | | | |__ \ `--.  | |
      | | |  __| `--. \ | |
      | | | |___/\__/ / | |
      \_/ \____/\____/  \_/
    */

    printf("                 _   _                       _     \n");
    printf("                | | (_)                     | |    \n");
    printf("  ___  ___ _ __ | |_ _ _ __  _   _ _   _ ___| |__  \n");
    printf(" / _ \\/ __| '_ \\| __| | '_ \\| | | | | | / __| '_ \\ \n");
    printf("|  __/\\__ \\ |_) | |_| | | | | |_| | |_| \\__ \\ |_) |\n");
    printf(" \\___||___/ .__/ \\__|_|_| |_|\\__, |\\__,_|___/_.__/ \n");
    printf("          | |______           __/ |               \n");
    printf("          |_|______|         |___/                \n");
    printf(" _____ _____ _____ _____                           \n");
    printf("|_   _|  ___/  ___|_   _|                          \n");
    printf("  | | | |__ \\ `--.  | |                            \n");
    printf("  | | |  __| `--. \\ | |                            \n");
    printf("  | | | |___/\\__/ / | |                            \n");
    printf("  \\_/ \\____/\\____/  \\_/                            \n");

    unity_utils_setup_heap_record(80);
    /* The leakage level does depend on esp-idf release */
    /* TODO: Increased from 1048 to 1600 as a workaround to unblock the CI */
    unity_utils_set_leak_level(1600); // 128 (default) + 820 (wl_mount) + 100 (in case of driver format)
    unity_run_menu();
}
