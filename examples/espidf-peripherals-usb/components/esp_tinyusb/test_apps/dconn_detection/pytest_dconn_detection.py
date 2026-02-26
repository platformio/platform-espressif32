# SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

import pytest
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize

#@pytest.mark.usb_device             Disable in CI: unavailable teardown for P4
@idf_parametrize('target', ['esp32s2', 'esp32s3', 'esp32p4'], indirect=['target'])
def test_usb_device_dconn_detection(dut: IdfDut) -> None:
    dut.run_all_single_board_cases(group='dconn')
