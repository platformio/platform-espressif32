/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "unity_test_runner.h"
#include "unity_test_utils_memory.h"

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
    unity_utils_set_leak_level(128);
    unity_run_menu();
}

/* setUp runs before every test */
void setUp(void)
{
    unity_utils_record_free_mem();
}

/* tearDown runs after every test */
void tearDown(void)
{
    unity_utils_evaluate_leaks();
}
