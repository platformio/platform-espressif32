/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tinyusb_msc.h"


void test_storage_event_queue_setup(void);

void test_storage_event_queue_teardown(void);

/**
 * @brief Callback to handle MSC Storage mount changed events
 *
 * This callback is triggered when the storage mount state changes.
 *
 * @param event Pointer to the event data structure containing mount state information.
 */
void test_storage_event_cb(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg);

/**
 * @brief Wait for a specific storage event to be generated
 *
 * @param event_id The expected event ID to wait for
 */
void test_storage_event_wait_callback(tinyusb_msc_event_id_t expected_event_id);
