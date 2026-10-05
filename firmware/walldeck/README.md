# WallDeck

Thin-client touchscreen for **Overlink Core**. Hardware: **ESP32 Cheap Yellow Display (CYD 2.8")**.

WallDeck does **not** talk to WiZ/WLED/CyberDeck directly. It polls Core over HTTP (`overlink.local`, fallback `192.168.4.181`) and sends scene / device / AV / party commands through Core.

Legacy direct-bulb firmware lives in `legacy/` and is not built.

## Tabs

| Tab | Role |
|-----|------|
| **Home** | Status: Core online, last scene, deck peer |
| **Zones** | Zone list, then Basement CTRL or a room |
| **Scenes** | Home-scoped scenes from Core |
| **Devices** | Paged catalog (12 at a time), Core IP, touch cal, sleep |

Ops (connectors, relay enroll, arrival) stays on the **phone** Core portal — not on the wall.

## Hardware

- ESP32-2432S028R (CYD 2.8" resistive)
- 2.4 GHz Wi-Fi only
- USB-C boards are often ST7789 (v3); micro-USB v1/v2 are ILI9341 — swap the `-include` in `platformio.ini`

## Flash

```bash
cd firmware/walldeck
# USB first time:
pio run -e cyd -t upload
# After OTA hostname is up:
pio run -e cyd_ota -t upload   # upload_port = walldeck.local
```

Wi-Fi: join the house network via WallDeck setup / `wifi_config` (no hardcoded secrets in git).

## Limits

- Device RAM cap is still `MAX_DEVICES 12` / `MAX_SCENES 11` / `MAX_ZONES 8` (ESP32 DRAM). Devices → ZONE / PREV / NEXT walks the rest of the house, including every Hue light. A 48/24 image was tried and reverted the same day; do not re-OTA that build.
- Core is `overlink.local`, then the address saved on Devices → CORE, then `192.168.4.181`.
- Touch starts from the factory map (raw 200,3700 × 240,3800). Devices → CAL stores a new top-left / bottom-right pair in NVS.

## Troubleshooting

| Problem | Fix |
|---------|-----|
| No devices / “Core offline” | Ping `overlink.local`. On the wall, Devices → CORE and enter the Core IP. Same 2.4 GHz LAN. |
| Missing lights | Open Devices and press NEXT, or ZONE to switch `basement` / `home`. The wall only holds 12 devices at a time. |
| Touch is offset | Devices → CAL. Tap the top-left corner, then the bottom-right corner. |
| OTA fails | Deep sleep stays off unless Devices turns it on. `ping walldeck-<chip>.local`. Hold BOOT only for USB recovery. |
| Blank display | Wrong driver — ILI9341 vs ST7789 in `platformio.ini`. |

## Project structure

```
src/
  main.cpp        LVGL + touch + OTA
  ui.cpp          Tabs
  core_client.*   HTTP client to Core
  config.h        Caps, fallback IP, power
  wifi_*.*        Provisioning
legacy/           Old direct WiZ/WLED/CyberDeck controllers (do not flash)
```
