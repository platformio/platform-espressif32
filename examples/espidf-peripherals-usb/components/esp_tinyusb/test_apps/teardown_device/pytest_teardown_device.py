# SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded_idf.dut import IdfDut
import subprocess
from time import sleep, time
from pytest_embedded_idf.utils import idf_parametrize


class DeviceNotFoundError(Exception):
    """Custom exception for device not found within the timeout period."""
    pass

def tusb_dev_in_list(vid, pid):
    try:
        output = subprocess.check_output(["lsusb"], text=True)
        search_string = f"{vid}:{pid}"
        return search_string in output
    except Exception as e:
        print(f"Error while executing lsusb: {e}")
        raise

def wait_tusb_dev_appeared(vid, pid, timeout):
    start_time = time()
    while True:
        if tusb_dev_in_list(vid, pid):
            return True
        if time() - start_time > timeout:
            raise DeviceNotFoundError(f"Device with VID: 0x{vid:04x}, PID: 0x{pid:04x} not found within {timeout} seconds.")
        sleep(0.5)

def wait_tusb_dev_removed(vid, pid, timeout):
    start_time = time()
    while True:
        if not tusb_dev_in_list(vid, pid):
            return True
        if time() - start_time > timeout:
            raise DeviceNotFoundError(f"Device with VID: 0x{vid:04x}, PID: 0x{pid:04x} wasn't removed within {timeout} seconds.")
        sleep(0.5)

def tusb_device_teardown(iterations, timeout):
    TUSB_VID = "303a"  # Espressif TinyUSB VID
    TUSB_PID = "4002"  # Espressif TinyUSB VID

    for i in range(iterations):
        # Wait until the device is present
        print(f"Waiting for device ...")
        wait_tusb_dev_appeared(TUSB_VID, TUSB_PID, timeout)
        print("Device detected.")

        # Wait until the device is removed
        print("Waiting for the device to be removed...")
        wait_tusb_dev_removed(TUSB_VID, TUSB_PID, timeout)
        print("Device removed.")
    print("Monitoring completed.")

@pytest.mark.usb_device
@idf_parametrize('target', ['esp32s2', 'esp32s3', 'esp32p4'], indirect=['target'])
def test_usb_teardown_device(dut: IdfDut) -> None:
    dut.expect_exact('Press ENTER to see the list of tests.')
    dut.write('[teardown]')
    dut.expect_exact('TinyUSB: TinyUSB Driver installed')
    sleep(2)             # Some time for the OS to enumerate our USB device

    try:
        tusb_device_teardown(10, 10)  # Teardown tusb device: amount, timeout

    except DeviceNotFoundError as e:
        print(f"Error: {e}")
        raise

    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        raise
