# SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize
import usb.core
import usb.util
from time import sleep


def find_interface_by_index(device, interface_index):
    '''
    Function to find the interface by index
    '''
    for cfg in device:
        for intf in cfg:
            if intf.bInterfaceNumber == interface_index:
                return intf
    return None


def send_data_to_intf(VID, PID, interface_index):
    '''
    Find a device, its interface and dual BULK endpoints
    Send some data to it
    '''
    # Find the USB device by VID and PID
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        raise ValueError("Device not found")

    # Find the interface by index
    intf = find_interface_by_index(dev, interface_index)
    if intf is None:
        raise ValueError(f"Interface with index {interface_index} not found")

    if intf:
        def ep_read(len):
            try:
                return ep_in.read(len, 100)
            except:
                return None
        def ep_write(buf):
            try:
                ep_out.write(buf, 100)
            except:
                pass

        maximum_packet_size = 64

        ep_in  = usb.util.find_descriptor(intf, custom_match = \
        lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)

        ep_out = usb.util.find_descriptor(intf, custom_match = \
        lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)

        #print(ep_in)
        #print(ep_out)
        buf = "IF{}\n".format(interface_index).encode('utf-8')
        ep_write(bytes(buf))

        ep_read(maximum_packet_size)
    else:
        print("NOT found")


#@pytest.mark.usb_device                        Disable in CI, for now, not possible to run this test in Docker container
@idf_parametrize('target', ['esp32s2', 'esp32s3', 'esp32p4'], indirect=['target'])
def test_usb_device_vendor(dut: IdfDut) -> None:
    '''
    Running the test locally:
    1. Build the test app for your DUT
    2. Connect you DUT to your test runner (local machine) with USB port and flashing port
    3. Run `pytest --target esp32s3`

    Important note: On Windows you must manually assign a driver the device, otherwise it will never be configured.
                    On Linux this is automatic

    Test procedure:
    1. Run the test on the DUT
    2. Expect 2 Vendor specific interfaces in the system
    3. Send some data to it, check log output
    '''
    dut.run_all_single_board_cases(group='vendor')

    sleep(2)  # Wait until the device is enumerated

    VID = 0x303A  # Replace with your device's Vendor ID
    PID = 0x4040  # Replace with your device's Product ID

    send_data_to_intf(VID, PID, 0)
    dut.expect_exact('vendor_test: actual read: 4. buffer message: IF0')
    send_data_to_intf(VID, PID, 1)
    dut.expect_exact('vendor_test: actual read: 4. buffer message: IF1')
