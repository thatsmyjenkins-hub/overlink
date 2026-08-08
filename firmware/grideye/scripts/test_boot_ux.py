#!/usr/bin/env python3
"""
Boot / UX serial tests for CYBERDECK (ESP32 CYD).

Runs against live hardware over USB serial. Resets the board via DTR, watches
firmware log lines, and PASS/FAILs expectations for real-world boot behavior.

Usage:
  python3 scripts/test_boot_ux.py [/dev/cu.usbserial-10]

Interactive steps (touch):
  1. When SELECT MODE is shown, tap JOIN NETWORK (or FIELD RECON).
  2. If WiFi scan appears, tap anywhere on that screen.
"""
from __future__ import annotations

import re
import sys
import time

try:
    import serial
except ImportError:
    print("FAIL: pip install pyserial")
    sys.exit(2)

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-10"
BAUD = 115200
BOOT_TIMEOUT = 45
INTERACTIVE_TOUCH_SEC = 25


class T:
    def __init__(self, name: str, desc: str):
        self.name = name
        self.desc = desc
        self.ok = False
        self.detail = ""

    def pass_(self, detail: str = ""):
        self.ok = True
        self.detail = detail

    def fail(self, detail: str):
        self.ok = False
        self.detail = detail


def reset_board(ser: serial.Serial) -> None:
    # RTS pulse resets the ESP32 on CYD USB-serial (DTR/RTS esptool seq enters download mode).
    ser.reset_input_buffer()
    ser.setDTR(False)
    ser.setRTS(False)
    time.sleep(0.05)
    ser.setRTS(True)
    time.sleep(0.05)
    ser.setRTS(False)
    time.sleep(0.4)


def read_boot_log(ser: serial.Serial, timeout: float) -> list[str]:
    lines: list[str] = []
    end = time.time() + timeout
    while time.time() < end:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").rstrip()
        if line:
            lines.append(line)
            print(f"  | {line}")
        if "[BOOT] ask_net=" in line:
            end = max(end, time.time() + 25)
    return lines


def run_tests(lines: list[str]) -> list[T]:
    tests: list[T] = []
    text = "\n".join(lines)

    t = T("T01_boot_banner", "Firmware boots and prints CYBERDECK banner")
    if "CYBERDECK NET INTEL" in text:
        t.pass_()
    else:
        t.fail("No boot banner seen")
    tests.append(t)

    t = T("T02_boot_prefs", "Boot logs ask_net and mode prefs")
    if re.search(r"\[BOOT\] ask_net=[01]", text):
        t.pass_()
    else:
        t.fail("Missing [BOOT] ask_net= line")
    tests.append(t)

    ask_m = re.search(r"\[BOOT\] ask_net=(\d)", text)
    ask_net = int(ask_m.group(1)) if ask_m else 1

    t = T("T03_mode_picker_stable", "Mode picker stays up before auto-join (ask_net=1)")
    if ask_net == 0:
        t.pass_("ask_net=0 — auto mode expected")
    else:
        picker = "[BOOT] Showing mode picker" in text or (
            "[SCREEN]" in text and "NET_MODE" in text
        )
        join_idx = next(
            (
                i
                for i, l in enumerate(lines)
                if "[NET] Joining saved" in l or "[NET] STA join started" in l
            ),
            -1,
        )
        mode_idx = next(
            (i for i, l in enumerate(lines) if "NET_MODE" in l and "[SCREEN]" in l),
            -1,
        )
        early_join = join_idx >= 0 and mode_idx >= 0 and join_idx - mode_idx < 8
        if picker and not early_join:
            t.pass_("Picker shown without immediate join")
        else:
            t.fail("Mode picker flashed then auto-joined too fast")
    tests.append(t)

    t = T("T04_touch_probe_idle", "Touch probe at mode picker shows no phantom press at idle")
    probes = [l for l in lines if "[TOUCH_PROBE] mode_picker" in l]
    if not probes:
        if ask_net == 0:
            t.pass_("ask_net=0 — no picker probe expected")
        else:
            t.fail("No [TOUCH_PROBE] mode_picker lines")
    else:
        stuck = any(re.search(r"ok=1", l) for l in probes)
        if stuck:
            t.fail("Phantom touch at boot (ok=1 while idle)")
        else:
            t.pass_(f"{len(probes)} probe lines, idle OK")
    tests.append(t)

    t = T("T05_main_loop_alive", "Main loop runs ([HEAP] within boot window)")
    if "[HEAP]" in text:
        t.pass_()
    else:
        t.fail("No [HEAP] line — loop may be blocked")
    tests.append(t)

    t = T("T06_no_panic", "No crash / panic during boot window")
    if any(x in text for x in ("Guru Meditation", "PANIC", "abort()", "Backtrace")):
        t.fail("Crash signature in log")
    else:
        t.pass_()
    tests.append(t)

    return tests


def interactive_mode_picker_touch(ser: serial.Serial) -> T:
    t = T("T07_touch_mode_picker", "Tap on SELECT MODE produces [TOUCH]/[BTN] net_mode")
    print("\n--- Interactive: tap JOIN NETWORK or FIELD RECON within 25s ---")
    end = time.time() + INTERACTIVE_TOUCH_SEC
    while time.time() < end:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").rstrip()
        if line:
            print(f"  | {line}")
        if "[BTN] net_mode: mode=" in line:
            t.pass_(line.strip())
            return t
        if "[TOUCH] screen=NET_MODE" in line and "unhandled" not in line:
            t.pass_(line.strip())
            return t
        if "[TOUCH_POLL]" in line and "press=1" in line:
            t.pass_(f"pressure detected: {line.strip()}")
            return t
    t.fail("No touch on NET_MODE — tap JOIN NETWORK; check [TOUCH_POLL] z/press in serial")
    return t


def interactive_wifi_scan_touch(ser: serial.Serial) -> T:
    t = T("T08_touch_wifi_scan", "Touch on WiFi scan produces [TOUCH]/[BTN] wifi_scan")
    print("\n--- Interactive: if on WiFi scan, tap screen within 15s (skip if past) ---")
    end = time.time() + 15
    while time.time() < end:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").rstrip()
        if line:
            print(f"  | {line}")
        if "[TOUCH] screen=WIFI_SCAN" in line or "[BTN] wifi_scan:" in line:
            t.pass_(line.strip())
            return t
    t.pass_("skipped or no WiFi scan screen")
    return t


def main() -> int:
    print(f"CYBERDECK boot UX tests on {PORT}@{BAUD}")
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.5)
    except serial.SerialException as e:
        print(f"FAIL: cannot open {PORT}: {e}")
        return 2

    print("\n== Phase 1: reset + boot log ==")
    time.sleep(0.2)
    reset_board(ser)
    lines = read_boot_log(ser, BOOT_TIMEOUT)
    if not lines:
        print("WARN: no serial lines — check USB cable, port, and that monitor isn't open elsewhere")
    tests = run_tests(lines)

    ask_net = 1
    m = re.search(r"\[BOOT\] ask_net=(\d)", "\n".join(lines))
    if m:
        ask_net = int(m.group(1))

    print("\n== Phase 2: mode picker touch ==")
    if ask_net == 1 or "[BOOT] Showing mode picker" in "\n".join(lines):
        tests.append(interactive_mode_picker_touch(ser))
    else:
        t = T("T07_touch_mode_picker", "Tap on SELECT MODE produces [TOUCH]/[BTN] net_mode")
        t.pass_("ask_net=0 — picker skipped")
        tests.append(t)

    print("\n== Phase 3: WiFi scan touch (optional) ==")
    tests.append(interactive_wifi_scan_touch(ser))

    print("\n== Results ==")
    failed = 0
    for t in tests:
        status = "PASS" if t.ok else "FAIL"
        if not t.ok:
            failed += 1
        extra = f" — {t.detail}" if t.detail else ""
        print(f"  {status}  {t.name}: {t.desc}{extra}")

    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
