# WallDeck

Thin-client touchscreen for **Overlink Core**. Hardware: **ESP32 Cheap Yellow Display (CYD 2.8")**.

WallDeck does **not** talk to WiZ/WLED/CyberDeck directly. It polls Core over HTTP (`overlink.local`, fallback `192.168.4.181`) and sends scene / device / AV / party commands through Core.

Legacy direct-bulb firmware lives in `legacy/` and is not built.

## Tabs

| Tab | Role |
|-----|------|
| **Home** | Status: Core online, last scene, deck peer |
| **Zones** | Devices in the active zone (on/off, dim) |
| **Scenes** | Home-scoped scenes from Core |
| **Now / AV** | Fire / Vizio / Sony via Core |
| **Settings** | Power / sleep (deep sleep default **off** so OTA works) |

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

## Limits (2026-08)

- `MAX_DEVICES 48`, `MAX_SCENES 24`, `MAX_ZONES 16`
- Core fallback IP: `192.168.4.181`
- Touch map is still hardcoded in `main.cpp` (`touchscreen_read`) — recalibrate if your panel is off

## Troubleshooting

| Problem | Fix |
|---------|-----|
| No devices / “Core offline” | Ping `overlink.local`. Confirm Core at `.181`. Check WallDeck and Core on same 2.4 GHz LAN. |
| Missing tiles | Device/scene count exceeded old cap of 12 — this build is 48/24. Re-OTA. |
| OTA fails | Disable deep sleep. `ping walldeck.local`. Hold BOOT only for USB recovery. |
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
