#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

"""
USB CDC (COM Port) Throughput Tester
------------------------------------

This script measures throughput for USB CDC-ACM (virtual COM port) devices
connected to a Windows (or Linux/macOS) host. It can test:

    --tx     Host → Device (send continuously from host)
    --rx     Device → Host (read continuously on host)
    --rxtx   Full-duplex (simultaneous send & receive)

It prints instantaneous and average bit rates once per second.

Usage Examples:
---------------
List available COM ports:
    python cdc_throughput.py

TX test (host → device, 30 seconds, 16 KiB write blocks):
    python cdc_throughput.py --port COM7 --baud 12000000 --tx --size 16384 --seconds 30

RX test (device → host, run until Ctrl+C):
    python cdc_throughput.py --port COM7 --rx --size 32768

Full-duplex test for 20 seconds:
    python cdc_throughput.py --port COM7 --rxtx --size 65536 --seconds 20

Select device by VID/PID (instead of COM port):
    python cdc_throughput.py --vid 0x303A --pid 0x4002 --rx --size 32768

Parameters:
-----------
--port      COM port name (e.g., COM7 on Windows, /dev/ttyACM0 on Linux)
--vid       USB Vendor ID (hex or decimal) to search for device
--pid       USB Product ID (hex or decimal) to search for device
--baud      Baud rate to set on host side. Ignored by CDC but still required by OS.
--size      I/O block size in bytes (try 16–64 KiB for high throughput)
--seconds   Duration in seconds (0 = run until interrupted)
--rtscts    Enable RTS/CTS hardware flow control
--xonxoff   Enable XON/XOFF software flow control
--dsrdtr    Enable DSR/DTR flow control

Notes:
------
- For RX test, the device must continuously send data as fast as possible.
- For TX test, the device should read & discard incoming data to avoid backpressure.
- Full-duplex mode works best if the device echoes or transmits in parallel.
- CDC ignores the baud rate, but some OS drivers require a nonzero setting.
- Large write sizes can improve throughput by reducing system call overhead.
"""

import argparse
import threading
import time
import sys
try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial is required. Install with: pip install pyserial")
    sys.exit(1)


def human_bps(bps: float) -> str:
    if bps >= 1e9:
        return f"{bps/1e9:.2f} Gbit/s"
    if bps >= 1e6:
        return f"{bps/1e6:.2f} Mbit/s"
    if bps >= 1e3:
        return f"{bps/1e3:.2f} kbit/s"
    return f"{bps:.0f} bit/s"


def human_bytes(b: float) -> str:
    units = ["B", "KiB", "MiB", "GiB"]
    v = float(b)
    for u in units:
        if v < 1024.0 or u == units[-1]:
            return f"{v:.2f} {u}"
        v /= 1024.0
    return f"{b:.0f} B"


def list_com_ports() -> None:
    print("Available ports:")
    for p in list_ports.comports():
        print(f"  {p.device:10s}  {p.description}")


class Counter:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._bytes = 0

    def add(self, n: int) -> None:
        with self._lock:
            self._bytes += n

    def snapshot(self) -> int:
        with self._lock:
            return self._bytes


def find_port_by_vid_pid(vid: int, pid: int) -> str:
    for p in list_ports.comports():
        if p.vid is not None and p.pid is not None:
            if p.vid == vid and p.pid == pid:
                return p.device
    raise RuntimeError(f"No serial port found with VID=0x{vid:04X} PID=0x{pid:04X}")


def open_serial(args) -> serial.Serial:
    port = args.port
    # If port not given, but VID/PID are, search for port
    if not port and args.vid and args.pid:
        port = find_port_by_vid_pid(args.vid, args.pid)
        print(f"Using port {port} for VID=0x{args.vid:04X} PID=0x{args.pid:04X}")
    if not port:
        raise RuntimeError("No port specified and no VID/PID match found.")
    s = serial.Serial()
    s.port = port
    s.baudrate = args.baud
    s.bytesize = serial.EIGHTBITS
    s.parity = serial.PARITY_NONE
    s.stopbits = serial.STOPBITS_ONE
    s.timeout = 0
    s.write_timeout = 1  # Set a write timeout to avoid overfilling OS buffers
    s.rtscts = args.rtscts
    s.xonxoff = args.xonxoff
    s.dsrdtr = args.dsrdtr
    s.open()
    return s


def writer_thread(s: serial.Serial, block: bytes, total: Counter, stop_evt: threading.Event):
    # Throttle writes: if OS buffer is full, SerialTimeoutException will be raised and we sleep
    view = memoryview(block)
    while not stop_evt.is_set():
        remaining = len(block)
        offset = 0
        while remaining and not stop_evt.is_set():
            try:
                n = s.write(view[offset:offset+remaining])
                if n is None:
                    n = 0
            except serial.SerialTimeoutException:
                # OS buffer is full, wait a bit longer before retrying
                time.sleep(0.0005)
                n = 0
            except Exception as e:
                print(f"\n[writer] Exception: {e}")
                stop_evt.set()
                return
            if n > 0:
                total.add(n)
                offset += n
                remaining -= n
            else:
                time.sleep(0.0005)
    # Optionally flush once at the end if needed
    # try:
    #     s.flush()
    # except Exception:
    #     pass


def reader_thread(s: serial.Serial, bufsize: int, total: Counter, stop_evt: threading.Event):
    buf = bytearray(bufsize)
    mv = memoryview(buf)
    while not stop_evt.is_set():
        try:
            n = s.readinto(mv)
        except Exception as e:
            print(f"\n[reader] Exception: {e}")
            stop_evt.set()
            return
        if n:
            total.add(n)
        else:
            time.sleep(0.0005)


def run_tx(args) -> None:
    s = open_serial(args)
    block = (bytes([0xAA, 0x55]) * (args.size // 2 + 1))[:args.size]
    total = Counter()
    stop_evt = threading.Event()
    t = threading.Thread(target=writer_thread, args=(s, block, total, stop_evt), daemon=True)
    t.start()

    start = last_t = time.perf_counter()
    last_bytes = 0
    try:
        while True:
            time.sleep(1.0)
            now = time.perf_counter()
            cur = total.snapshot()
            delta_b = cur - last_bytes
            delta_t = now - last_t
            inst_bps = (delta_b * 8) / delta_t if delta_t > 0 else 0.0
            elapsed = now - start
            avg_bps = (cur * 8) / elapsed if elapsed > 0 else 0.0
            print(f"[TX] +{human_bytes(delta_b)}/s  inst={human_bps(inst_bps):>10s}  avg={human_bps(avg_bps):>10s}  total={human_bytes(cur)}")
            last_t = now
            last_bytes = cur
            if args.seconds and elapsed >= args.seconds:
                break
    finally:
        stop_evt.set()
        t.join(timeout=1.0)
        s.close()


def run_rx(args) -> None:
    s = open_serial(args)
    total = Counter()
    stop_evt = threading.Event()
    t = threading.Thread(target=reader_thread, args=(s, args.size, total, stop_evt), daemon=True)
    t.start()

    start = last_t = time.perf_counter()
    last_bytes = 0
    try:
        while True:
            time.sleep(1.0)
            now = time.perf_counter()
            cur = total.snapshot()
            delta_b = cur - last_bytes
            delta_t = now - last_t
            inst_bps = (delta_b * 8) / delta_t if delta_t > 0 else 0.0
            elapsed = now - start
            avg_bps = (cur * 8) / elapsed if elapsed > 0 else 0.0
            print(f"[RX] +{human_bytes(delta_b)}/s  inst={human_bps(inst_bps):>10s}  avg={human_bps(avg_bps):>10s}  total={human_bytes(cur)}")
            last_t = now
            last_bytes = cur
            if args.seconds and elapsed >= args.seconds:
                break
    finally:
        stop_evt.set()
        t.join(timeout=1.0)
        s.close()


def run_rxtx(args) -> None:
    s = open_serial(args)
    tx_block = (bytes([0xA5]) * args.size)
    rx_total = Counter()
    tx_total = Counter()
    stop_evt = threading.Event()

    rt = threading.Thread(target=reader_thread, args=(s, args.size, rx_total, stop_evt), daemon=True)
    wt = threading.Thread(target=writer_thread, args=(s, tx_block, tx_total, stop_evt), daemon=True)
    rt.start()
    wt.start()

    start = last_t = time.perf_counter()
    last_rx = 0
    last_tx = 0
    try:
        while True:
            time.sleep(1.0)
            now = time.perf_counter()
            rx = rx_total.snapshot()
            tx = tx_total.snapshot()

            d_rx = rx - last_rx
            d_tx = tx - last_tx
            dt = now - last_t

            rx_inst = (d_rx * 8) / dt if dt > 0 else 0.0
            tx_inst = (d_tx * 8) / dt if dt > 0 else 0.0
            elapsed = now - start
            rx_avg = (rx * 8) / elapsed if elapsed > 0 else 0.0
            tx_avg = (tx * 8) / elapsed if elapsed > 0 else 0.0

            print(f"[RX/TX] rx:+{human_bytes(d_rx)}/s ({human_bps(rx_inst)})  tx:+{human_bytes(d_tx)}/s ({human_bps(tx_inst)})"
                  f"  avg rx={human_bps(rx_avg)} tx={human_bps(tx_avg)}  totals rx={human_bytes(rx)} tx={human_bytes(tx)}")

            last_t = now
            last_rx = rx
            last_tx = tx
            if args.seconds and elapsed >= args.seconds:
                break
    finally:
        stop_evt.set()
        rt.join(timeout=1.0)
        wt.join(timeout=1.0)
        s.close()


def main():
    parser = argparse.ArgumentParser(description="USB CDC (COM port) throughput tester")
    parser.add_argument("--port", help="COM port (e.g., COM7). If omitted, prints available ports.", type=str)
    parser.add_argument("--vid", help="USB VID (hex or dec) to search for device.", type=lambda x: int(x, 0))
    parser.add_argument("--pid", help="USB PID (hex or dec) to search for device.", type=lambda x: int(x, 0))
    parser.add_argument("--baud", help="Baud rate to set (may be ignored by CDC).", type=int, default=12_000_000)
    parser.add_argument("--size", help="I/O block size in bytes.", type=int, default=16384)
    parser.add_argument("--seconds", help="Stop after this many seconds (default: run until Ctrl+C).", type=int, default=0)

    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--tx", action="store_true", help="Host -> Device throughput test")
    mode.add_argument("--rx", action="store_true", help="Device -> Host throughput test")
    mode.add_argument("--rxtx", action="store_true", help="Full-duplex test")

    parser.add_argument("--rtscts", action="store_true", help="Enable RTS/CTS hardware flow control")
    parser.add_argument("--xonxoff", action="store_true", help="Enable XON/XOFF software flow control")
    parser.add_argument("--dsrdtr", action="store_true", help="Enable DSR/DTR flow control")

    args = parser.parse_args()

    if not args.port and not (args.vid and args.pid):
        list_com_ports()
        print("\nRe-run with --port COMx or --vid VID --pid PID and a mode (--tx/--rx/--rxtx).")
        return

    try:
        if args.tx:
            run_tx(args)
        elif args.rx:
            run_rx(args)
        elif args.rxtx:
            run_rxtx(args)
        else:
            print("Pick a mode: --tx, --rx, or --rxtx.")
    except KeyboardInterrupt:
        print("\nStopped by user.")
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(2)


if __name__ == "__main__":
    main()


try:
    # Running this test from pytest slows down the performance to 50%
    # Compared to running this test from clean CLI
    # That is why we do not export throughput results from here
    import pytest
    from pytest_embedded_idf.dut import IdfDut
    from pytest_embedded_idf.utils import idf_parametrize
    @pytest.mark.usb_device
    @idf_parametrize('target', ['esp32s2', 'esp32s3', 'esp32p4'], indirect=['target'])
    def test_tusb_cdc_throughput(dut: IdfDut) -> None:
        dut.expect_exact('Press ENTER to see the list of tests.')
        dut.write('[cdc_throughput]')
        dut.expect_exact('TinyUSB: TinyUSB Driver installed')
        time.sleep(2)  # Some time for the OS to enumerate our USB device
        class Args:
            port = None  # Use VID/PID instead
            vid = 0x303A
            pid = 0x4002
            baud = 480000000
            size = 32768
            seconds = 10
            tx = False
            rx = True
            rxtx = False
            rtscts = False
            xonxoff = False
            dsrdtr = False

        run_rx(Args)
        dut.close()
except ImportError:
    print("pytest is missing. No test exported.")
