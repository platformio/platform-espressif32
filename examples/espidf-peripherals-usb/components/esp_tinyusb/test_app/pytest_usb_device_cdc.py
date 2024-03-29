# SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded_idf.dut import IdfDut
from time import sleep
from serial import Serial
from serial.tools.list_ports import comports


@pytest.mark.esp32s2
@pytest.mark.esp32s3
@pytest.mark.usb_device
def test_usb_device_cdc(dut) -> None:
    '''
    Running the test locally:
    1. Build the testa app for your DUT (ESP32-S2 or S3)
    2. Connect you DUT to your test runner (local machine) with USB port and flashing port
    3. Run `pytest --target esp32s3`

    Test procedure:
    1. Run the test on the DUT
    2. Expect 2 Virtual COM Ports in the system
    3. Open both comports and send some data. Expect echoed data
    '''
    dut.expect_exact('Press ENTER to see the list of tests.')
    dut.write('[esp_tinyusb]')
    dut.expect_exact('TinyUSB: TinyUSB Driver installed')
    sleep(2)  # Some time for the OS to enumerate our USB device

    # Find devices with Espressif TinyUSB VID/PID
    s = []
    ports = comports()
    for port, _, hwid in ports:
        if '303A:4002' in hwid:
            s.append(port)

    if len(s) != 2:
        raise Exception('TinyUSB COM port not found')

    with Serial(s[0]) as cdc0:
        with Serial(s[1]) as cdc1:
            # Write dummy string and check for echo
            cdc0.write('text\r\n'.encode())
            res = cdc0.readline()
            assert b'text' in res
            if b'novfs' in res:
                novfs_cdc = cdc0
                vfs_cdc = cdc1

            cdc1.write('text\r\n'.encode())
            res = cdc1.readline()
            assert b'text' in res
            if b'novfs' in res:
                novfs_cdc = cdc1
                vfs_cdc = cdc0

            # Write more than MPS, check that the transfer is not divided
            novfs_cdc.write(bytes(100))
            dut.expect_exact("Intf 0, RX 100 bytes")

            # Write more than RX buffer, check correct reception
            novfs_cdc.write(bytes(600))
            transfer_len1 = int(dut.expect(r'Intf 0, RX (\d+) bytes')[1].decode())
            transfer_len2 = int(dut.expect(r'Intf 0, RX (\d+) bytes')[1].decode())
            assert transfer_len1 + transfer_len2 == 600

            # The VFS is setup for CRLF RX and LF TX
            vfs_cdc.write('text\r\n'.encode())
            res = vfs_cdc.readline()
            assert b'text\n' in res

            return
