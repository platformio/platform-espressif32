/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "tinyusb.h"

//
// ========================== Test Configuration Parameters =====================================
//

#define TEST_DEVICE_PRESENCE_TIMEOUT_MS   5000 // Timeout for checking device presence

/**
 * @brief Test device setup
 */
void test_device_setup(void);

/**
 * @brief Test device release
 */
void test_device_release(void);

/**
 * @brief Test device wait
 *
 * Waits the tusb_mount_cb() which indicates the device connected to the Host and enumerated.
 */
void test_device_wait(void);

/**
 * @brief TinyUSB callback for device mount.
 *
 * @note
 * For Linux-based Hosts: Reflects the SetConfiguration() request from the Host Driver.
 * For Win-based Hosts: SetConfiguration() request is present only with available Class in device descriptor.
 */
void test_device_event_handler(tinyusb_event_t *event, void *arg);
