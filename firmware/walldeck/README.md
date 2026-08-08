# Basement Controller

Touchscreen remote for basement lighting, built for the **ESP32 Cheap Yellow Display (CYD 2.8")**.

Controls **5 WiZ smart bulbs**, **1 WLED strip**, and a **CyberDeck IR/RF peer** over your local WiFi — no cloud required.

## Features

| Tab | Controls |
|-----|----------|
| **Scenes** | Normal · Party · Night · All Off |
| **Lights** | Individual on/off + dimming slider for each bulb and the WLED strip |
| **Deck** | CyberDeck remote: Vizio IR + CC1101 sniff/replay (full peer API) |

### Scene behavior

- **Normal** — All WiZ bulbs at warm white (3000K, 100%). WLED solid warm white.
- **Party** — WiZ bulbs on Rainbow scene. WLED Rainbow effect, bright and fast.
- **Night** — WiZ bulbs at minimum brightness (10%) and warmest white (2200K). WLED slow, dim Breathe pulse.
- **All Off** — Everything off.

## Hardware

- ESP32-2432S028R (CYD 2.8" touchscreen)
- USB-C CYD boards are usually **v3** (ST7789 display) — already configured in `platformio.ini`
- v1/v2 boards (micro-USB only) use ILI9341 — swap the `-include` line in `platformio.ini`

The CYD runs on battery or USB power. Connect it to your home WiFi (2.4 GHz — ESP32 does not support 5 GHz).

## Setup

### 1. Install PlatformIO

Use [VS Code + PlatformIO](https://platformio.org/install/ide?install=vscode) or the PlatformIO CLI.

### 2. Configure WiFi

```bash
cp src/secrets.h.example src/secrets.h
```

Edit `src/secrets.h` with your WiFi SSID and password.

### 3. Configure device IPs

**Easy way — run the setup tool** (discovers devices, blinks each bulb so you can identify it, writes `config.h`):

```bash
python3 tools/setup_devices.py
```

For each bulb: press **b** to blink it, then **n** to name it once you've spotted which one lit up. The tool also finds WLED strips automatically.

**Manual way** — edit `src/config.h` directly:

1. **WiZ bulbs** — Find each bulb's IP in the WiZ app (Device info). Give each bulb a **static/reserved IP** in your router.
2. **WLED strip** — Set `WLED_HOST` to your strip's IP.
3. **Rename bulbs** — Update the `name` fields to match your layout (Overhead, Corner, etc.).

### 4. Build and flash

```bash
pio run -t upload
pio device monitor
```

Hold the CYD **BOOT** button while plugging in USB if upload fails.

### 5. Touch calibration

If touches are misaligned, adjust the `map()` ranges in `main.cpp` → `touchscreen_read()`.

If the display is upside-down, try `touchscreen.setRotation(0)` or change `lv_display_set_rotation`.

## Display board version

In `platformio.ini`, one of these lines must be active:

```ini
; v3 (USB-C):
-include src/Setup_ESP32_2432S028R_ST7789.h

; v1/v2 (micro-USB):
; -include src/Setup_ESP32_2432S028R_ILI9341.h
```

Also set `CYD_DISPLAY_V3` in `config.h` to match (informational only).

## TVs (future)

Your Vizio SmartCast and Android TV aren't controlled in this firmware yet. They need different APIs (Cast/AirPlay triggers, IR, or manufacturer apps). The light controller is fully local; TV integration can be added as a separate tab once you pick an approach.

## Troubleshooting

| Problem | Fix |
|---------|-----|
| Lights don't respond | Confirm CYD and bulbs are on the same subnet. Ping bulb IPs from a computer. |
| WLED effect wrong | Open `http://<wled-ip>/json/eff` and update effect IDs in `config.h` if yours differ. |
| WiFi won't connect | ESP32 needs 2.4 GHz WiFi. Check `secrets.h`. |
| Blank display | Wrong display driver — swap ILI9341 ↔ ST7789 in `platformio.ini`. |

## Project structure

```
src/
  main.cpp              CYD display + WiFi + LVGL setup
  ui.cpp                Touchscreen interface
  wiz_controller.cpp    WiZ UDP control (port 38899)
  wled_controller.cpp   WLED HTTP JSON API
  config.h              IPs, names, scene presets
  secrets.h             WiFi credentials (not committed)
```
