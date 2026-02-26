# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

from time import sleep
import pytest
import usb.core
import usb.util
from serial import Serial, SerialException
from serial.tools.list_ports import comports
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize

# Standard USB requests (USB 2.0 spec, Table 9-4)
USB_B_REQUEST_SET_FEATURE       = 0x03

# Standard feature selectors (USB 2.0 spec, Table 9-6)
USB_FEAT_DEVICE_REMOTE_WAKEUP   = 0x01

# Bit mask belonging to the bmAttributes field of a configuration descriptor
USB_BM_ATTRIBUTES_WAKEUP        = 0x20

# Device Under Test VID:PID
DUT_VID = 0x303A
DUT_PID = 0x4002

# Tinyusb device events from device event handler
TINYUSB_EVENTS = {
    "attached":                     "TINYUSB_EVENT_ATTACHED",
    "suspended":                    "TINYUSB_EVENT_SUSPENDED",
    "resumed":                      "TINYUSB_EVENT_RESUMED",
    "suspended_remote_wake_dis":    "TINYUSB_EVENT_SUSPENDED_REMOTE_WAKE_DIS",
    "suspended_remote_wake_en":     "TINYUSB_EVENT_SUSPENDED_REMOTE_WAKE_EN",
}

@pytest.mark.usb_device
@idf_parametrize('target', ['esp32s2', 'esp32s3', 'esp32p4'], indirect=['target'])
def test_usb_device_suspend_resume(dut: IdfDut) -> None:
    '''
    Running the test locally:
    1. Build the test_app for your DUT (ESP32-S2/S3/P4)
    2. Connect you DUT to your test runner (local machine) with USB port and flashing port
    3. Run `pytest --target esp32s3`

    Test procedure:
    1. Run the test on the DUT
    2. Expect one COM Port in the system
    3. Open it and and test power management of the USB device (Suspend/Resume)
    4. Suspend: Device enters suspended state after some time of inactivity
    5. Resume: Device is resumed by accessing it (sending some data to it)
    '''
    dut.expect_exact('Press ENTER to see the list of tests.')
    dut.write('[device_pm_suspend_resume]')
    dut.expect_exact('TinyUSB: TinyUSB Driver installed')
    sleep(2)  # Some time for the OS to enumerate our USB device

    # Find device with Espressif TinyUSB VID/PID
    ports = []
    for p in comports():
        if (p.vid == DUT_VID and p.pid == DUT_PID):
            ports.append(p.device)

    if len(ports) == 0:
        raise Exception('TinyUSB COM port not found')

    try:
        with Serial(ports[0], timeout=2) as cdc:
            dut.expect_exact(TINYUSB_EVENTS['attached'])

            # Wait for auto suspend (set to 3 seconds)
            dut.expect_exact(TINYUSB_EVENTS['suspended'])

            for i in range(0, 5):
                print(f"Power cycle iteration {i}.")

                # Resume the device by accessing it
                cdc.write(b'Time to resume\r\n')
                res = cdc.readline()
                assert b'Time to suspend\r\n' in res

                dut.expect_exact(TINYUSB_EVENTS['resumed'])

                # Wait for auto suspend (set to 3 seconds)
                dut.expect_exact(TINYUSB_EVENTS['suspended'])

                # Stay suspended for a while
                sleep(2)

    except SerialException as e:
        raise RuntimeError(f"Failed to open CDC device on {ports[0]}") from e


def set_remote_wake_on_device(VID: int, PID: int) -> None:
    '''
    Set remote wakeup on device by sending SET_FEATURE ctrl transfer to the device

    :param VID: VID of the device
    :param PID: PID of the device
    '''
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        raise ValueError("Device not found")

    bmRequestType = usb.util.build_request_type(
                    usb.util.CTRL_OUT,
                    usb.util.CTRL_TYPE_STANDARD,
                    usb.util.CTRL_RECIPIENT_DEVICE)

    dev.ctrl_transfer(
        bmRequestType=bmRequestType,
        bRequest=USB_B_REQUEST_SET_FEATURE,
        wValue=USB_FEAT_DEVICE_REMOTE_WAKEUP,
        wIndex=0,
        data_or_wLength=None
    )
    print("CTRL transfer sent")


def check_remote_wake_feature(VID: int, PID: int, has_remote_wake: bool) -> None:
    '''
    Check if the device reports remote wakeup feature from it's configuration descriptor

    :param VID: VID of the device
    :param PID: PID of the device
    :param has_remote_wake: Expect the device to does/does not feature with remote wakeup
    '''

    sleep(2)  # Some time for the OS to enumerate our USB device
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        raise ValueError("Device not found")

    cfg = dev.get_active_configuration()
    remote_wake_supported = bool(cfg.bmAttributes & USB_BM_ATTRIBUTES_WAKEUP)

    if remote_wake_supported:
        print("Device advertises remote wakeup feature in it's descriptor")
    else:
        print("Device does not advertise remote wakeup feature in it's descriptor")

    # Assertion to fail on mismatch
    assert remote_wake_supported == has_remote_wake, (
        f"Remote wakeup capability mismatch: "
        f"expected {has_remote_wake}, "
        f"device reports {remote_wake_supported}"
    )

@pytest.mark.usb_device
@idf_parametrize('target', ['esp32s2', 'esp32s3', 'esp32p4'], indirect=['target'])
def test_usb_device_remote_wakeup_en(dut: IdfDut) -> None:
    '''
    Running the test locally:
    1. Build the test_app for your DUT (ESP32-S2/S3/P4)
    2. Connect you DUT to your test runner (local machine) with USB port and flashing port
    3. Run `pytest --target esp32s3`

    Test procedure:
    1. Run the test on the DUT
    2. Expect one COM Port in the system
    3. Check the device's configuration descriptor, if it reports remote wakeup functionality
    4. Enable the remote wakeup by sending a ctrl transfer
    '''
    dut.expect_exact('Press ENTER to see the list of tests.')
    dut.write('[device_pm_remote_wake]')
    dut.expect_exact('TinyUSB: TinyUSB Driver installed')

    # Wait for device attach event
    dut.expect_exact(TINYUSB_EVENTS['attached'])
    # Check if the device reports remote wakeup feature
    check_remote_wake_feature(DUT_VID, DUT_PID, has_remote_wake=True)

    # Expect device suspend event (auto suspend) with remote wakeup disabled
    dut.expect_exact(TINYUSB_EVENTS['suspended_remote_wake_dis'])

    # Enable remote wakeup on the device
    set_remote_wake_on_device(DUT_VID, DUT_PID)

    # Expect device to resume (ctrl transfer sent)
    dut.expect_exact(TINYUSB_EVENTS['resumed'])
    # Expect device suspend event (auto suspend) with remote wakeup enabled
    dut.expect_exact(TINYUSB_EVENTS['suspended_remote_wake_en'])

    # Device called remote wakeup

    # Expect device to resume (remote wakeup)
    dut.expect_exact(TINYUSB_EVENTS['resumed'])
