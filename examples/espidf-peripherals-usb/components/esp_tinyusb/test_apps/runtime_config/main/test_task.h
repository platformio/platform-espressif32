/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "sdkconfig.h"

/**
 * Test Task Configuration setup
 */

// Default size for task stack used in TinyUSB task creation
#define TUSB_TASK_SIZE          4096
// Default priority for task used in TinyUSB task creation
#define TUSB_TASK_PRIO          5
// Affinity for task used in TinyUSB task creation
#define TUSB_TASK_AFFINITY_NO   0x7FFFFFFF /* FREERTOS_NO_AFFINITY */
#define TUSB_TASK_AFFINITY_CPU0 0x00
#if (!CONFIG_FREERTOS_UNICORE)
#define TUSB_TASK_AFFINITY_CPU1 0x01
#endif // !CONFIG_FREERTOS_UNICORE
