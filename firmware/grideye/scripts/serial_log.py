#!/usr/bin/env python3
"""Minimal non-interactive serial logger for capturing crash/backtrace output."""
import sys, time
import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-10"
baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
out = sys.argv[3] if len(sys.argv) > 3 else "serial.log"

ser = serial.Serial(port, baud, timeout=1)
with open(out, "a", buffering=1) as f:
    f.write(f"\n===== serial_log started {time.strftime('%H:%M:%S')} on {port}@{baud} =====\n")
    while True:
        try:
            line = ser.readline()
            if not line:
                continue
            text = line.decode("utf-8", errors="replace").rstrip("\n")
            stamp = time.strftime("%H:%M:%S")
            f.write(f"[{stamp}] {text}\n")
        except KeyboardInterrupt:
            break
        except Exception as e:
            f.write(f"[ERR] {e}\n")
            time.sleep(0.5)
