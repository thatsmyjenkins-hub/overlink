# TF card bring-up

Overlink Core talks to the TF slot over **SD_MMC** (not USB). The Mac will never show the card in Finder while the stick is plugged in.

## Format

1. Remove the TF card from the stick.
2. On a Mac/PC card reader, erase as **MS-DOS (FAT) / FAT32** (not exFAT).
3. Optional: copy `data/seed/` contents into a top-level `homes/` folder on the card later (firmware will also create `/homes`).
4. Seat the card firmly in the Waveshare slot (click).
5. Replug USB and watch serial — expect `TF uplink OK` and size ~14–16 GB.

## Pins (Waveshare ESP32-S3-LCD-1.47)

| SD | GPIO |
|----|------|
| CLK | 14 |
| CMD | 15 |
| D0 | 16 |
| D1 | 18 |
| D2 | 17 |
| D3 | 21 |

## Serial check

```bash
cd firmware/overlink-core
pio run -t upload
python3 - <<'PY'
import serial,time
s=serial.Serial('/dev/cu.usbmodem1401',115200,timeout=0.2)
s.dtr=False; s.rts=True; time.sleep(0.1); s.rts=False
time.sleep(2); print(s.read(8000).decode('utf-8','replace')); s.close()
PY
```
