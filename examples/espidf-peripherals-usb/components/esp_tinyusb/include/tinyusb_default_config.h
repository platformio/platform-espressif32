/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include "tinyusb.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GET_CONFIG_MACRO(dummy, arg1, arg2, arg3, name, ...)    name

/**
 * @brief Default TinyUSB Driver configuration structure initializer
 *
 * Default port:
 * - ESP32P4:       USB OTG 2.0 (High-speed)
 * - ESP32S2/S3:    USB OTG 1.1 (Full-speed)
 *
 * Default size:
 * - 4096 bytes
 * Default priority:
 * - 5
 *
 * Default task affinity:
 * - Multicore:     CPU1
 * - Unicore:       CPU0
 *
 */

#define TINYUSB_DEFAULT_CONFIG(...)              GET_CONFIG_MACRO(, ##__VA_ARGS__, \
                                                                    TINYUSB_CONFIG_INVALID,    \
                                                                    TINYUSB_CONFIG_EVENT_ARG,  \
                                                                    TINYUSB_CONFIG_EVENT,      \
                                                                    TINYUSB_CONFIG_NO_ARG      \
                                                                )(__VA_ARGS__)

#define TINYUSB_CONFIG_INVALID(...)              static_assert(false, "Too many arguments for TINYUSB_DEFAULT_CONFIG")

#if CONFIG_IDF_TARGET_ESP32P4
#define TINYUSB_CONFIG_NO_ARG()                  TINYUSB_CONFIG_HIGH_SPEED(NULL, NULL)
#define TINYUSB_CONFIG_EVENT(event_hdl)          TINYUSB_CONFIG_HIGH_SPEED(event_hdl, NULL)
#define TINYUSB_CONFIG_EVENT_ARG(event_hdl, arg) TINYUSB_CONFIG_HIGH_SPEED(event_hdl, arg)
#else
#define TINYUSB_CONFIG_NO_ARG()                  TINYUSB_CONFIG_FULL_SPEED(NULL, NULL)
#define TINYUSB_CONFIG_EVENT(event_hdl)          TINYUSB_CONFIG_FULL_SPEED(event_hdl, NULL)
#define TINYUSB_CONFIG_EVENT_ARG(event_hdl, arg) TINYUSB_CONFIG_FULL_SPEED(event_hdl, arg)
#endif

#if CONFIG_FREERTOS_UNICORE
#define TINYUSB_DEFAULT_TASK_AFFINITY  (0U)
#else
#define TINYUSB_DEFAULT_TASK_AFFINITY  (1U)
#endif // CONFIG_FREERTOS_UNICORE

// Default size for task stack used in TinyUSB task creation
#define TINYUSB_DEFAULT_TASK_SIZE      4096
// Default priority for task used in TinyUSB task creation
#define TINYUSB_DEFAULT_TASK_PRIO      5

#define TINYUSB_CONFIG_FULL_SPEED(event_hdl, arg)       \
    (tinyusb_config_t) {                                \
        .port = TINYUSB_PORT_FULL_SPEED_0,              \
        .phy = {                                        \
            .skip_setup = false,                        \
            .self_powered = false,                      \
            .vbus_monitor_io = -1,                      \
        },                                              \
        .task = TINYUSB_TASK_DEFAULT(),                 \
        .descriptor = {                                 \
            .device = NULL,                             \
            .qualifier = NULL,                          \
            .string = NULL,                             \
            .string_count = 0,                          \
            .full_speed_config = NULL,                  \
            .high_speed_config = NULL,                  \
        },                                              \
        .event_cb = (event_hdl),                        \
        .event_arg = (arg),                             \
    }

#define TINYUSB_CONFIG_HIGH_SPEED(event_hdl, arg)       \
    (tinyusb_config_t) {                                \
        .port = TINYUSB_PORT_HIGH_SPEED_0,              \
        .phy = {                                        \
            .skip_setup = false,                        \
            .self_powered = false,                      \
            .vbus_monitor_io = -1,                      \
        },                                              \
        .task = TINYUSB_TASK_DEFAULT(),                 \
        .descriptor = {                                 \
            .device = NULL,                             \
            .qualifier = NULL,                          \
            .string = NULL,                             \
            .string_count = 0,                          \
            .full_speed_config = NULL,                  \
            .high_speed_config = NULL,                  \
        },                                              \
        .event_cb = (event_hdl),                        \
        .event_arg = (arg),                             \
    }

#define TINYUSB_TASK_DEFAULT()                          \
    (tinyusb_task_config_t) {                           \
        .size = TINYUSB_DEFAULT_TASK_SIZE,              \
        .priority = TINYUSB_DEFAULT_TASK_PRIO,          \
        .xCoreID = TINYUSB_DEFAULT_TASK_AFFINITY,       \
    }

/**
 * @brief TinyUSB Task configuration structure initializer
 *
 * This macro is used to create a custom TinyUSB Task configuration structure.
 *
 * @param s Stack size of the task
 * @param p Task priority
 * @param a Task affinity (CPU core)
 */
#define TINYUSB_TASK_CUSTOM(s, p, a)                    \
    (tinyusb_task_config_t) {                           \
        .size = (s),                                    \
        .priority = (p),                                \
        .xCoreID = (a),                                 \
    }

#ifdef __cplusplus
}
#endif
