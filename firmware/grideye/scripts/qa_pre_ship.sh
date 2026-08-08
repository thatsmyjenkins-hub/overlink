#!/usr/bin/env bash
# Build, flash, and run boot/touch QA against connected CYD hardware.
set -euo pipefail
cd "$(dirname "$0")/.."

PORT="${1:-/dev/cu.usbserial-10}"

echo "== Build (cyd28) =="
pio run -e cyd28

echo "== Flash =="
pio run -e cyd28 -t upload

echo "== Boot UX tests (serial) =="
python3 scripts/test_boot_ux.py "$PORT"
