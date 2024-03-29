/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "tinyusb.h"
#include "tusb_tasks.h"
#include "test_bvalid_sig.h"
#include "test_descriptors_config.h"

// Invoked when device is mounted
void tud_mount_cb(void)
{
    printf("%s\n", __FUNCTION__);
    test_bvalid_sig_mount_cb();
    test_descriptors_config_mount_cb();
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    printf("%s\n", __FUNCTION__);
    test_bvalid_sig_umount_cb();
    test_descriptors_config_umount_cb();
}
