# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize


@pytest.mark.usb_device
@idf_parametrize('target', ['esp32s2', 'esp32s3', 'esp32p4'], indirect=['target'])
def test_usb_device_runtime_config(dut: IdfDut) -> None:
    peripherals = [
        'default',
        'high_speed',
        # 'full_speed', TODO: Enable this after the P4 USB OTG 1.1 periph is connected
    ] if dut.target == 'esp32p4' else [
        'default',
        'full_speed',
    ]

    for periph in peripherals:
        dut.run_all_single_board_cases(group=periph)

# The threshold values for TinyUSB Task Run time (in cycles) for different targets
TASK_RUN_TIME_LIMITS = {
    'esp32s2': 7000,
    'esp32s3': 3000,
    'esp32p4': 1800,
}

def _get_run_time_th(target: str) -> int:
    assert target in TASK_RUN_TIME_LIMITS
    return TASK_RUN_TIME_LIMITS.get(target)

@pytest.mark.usb_device
@idf_parametrize('target', ['esp32s2', 'esp32s3', 'esp32p4'], indirect=['target'])
def test_cpu_load_task_stat_print(dut: IdfDut) -> None:
    '''
    Test to verify that Run time and CPU load measurement for TinyUSB task is working.
    This test runs only on runtime_config test app.

    Test procedure:
    1. Run the test on the DUT
    2. Expect to see TinyUSB task CPU load printed in the output
    3. Expect TinyUSB task CPU load to be not greater than 0%
    '''
    dut.expect_exact('Press ENTER to see the list of tests.')
    dut.write('[cpu_load]')
    dut.expect_exact('Starting TinyUSB load measurement test...')
    dut.expect_exact('CPU load measurement test completed.')

    line = dut.expect(r'TinyUSB Run time: (\d+) ticks')
    run_time = int(line.group(1))
    run_time_max = _get_run_time_th(dut.target)

    assert 0 < run_time < run_time_max, f'Unexpected TinyUSB Run time: {run_time}'
