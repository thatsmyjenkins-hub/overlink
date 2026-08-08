# CYBERDECK networking model

Two boot choices map to two runtime engines:

| Boot option | Engine | Web UI |
|-------------|--------|--------|
| **Join network** | LAN intel (when STA up) or setup portal | `index.html` or `config.html` |
| **Field recon** | Promiscuous Wi‑Fi + BLE + capture | `field.html` |

Uncheck **Ask every power-on** to remember your choice.

## Setup (no working Wi‑Fi)

CYBERDECK open AP at `192.168.4.1` → **config.html**:

- Scan, pick SSID, password
- **Keep CYBERDECK hotspot** (default on) — deck stays reachable at `192.168.4.1` after join
- Uncheck to join LAN only (AP turns off)

API: `GET /api/wifi/scan`, `POST /api/wifi/connect` (`ssid`, `pass`, `keepAp=1|0`), `GET /api/wifi/status`

## Field recon

CYBERDECK AP + `field.html` + `GET /api/recon/all`. On-device: ARM, CAP, BLE toggles.

## Connected (on LAN)

`GET /api/intel/all` — single dashboard poll. Detail on demand: `/api/intel/host`, `/api/intel/ap`.

Intel engine starts only after STA connects (saves RAM at boot / in field mode).

## Lean build flags (`src/config.h`)

| Flag | Effect |
|------|--------|
| `ENABLE_CVE_PROFILE 0` | Skip vuln rule DB (~12 KB when loaded) |
| `ENABLE_DEAUTH 0` | Disable field deauth engage |
| `INTEL_MAX_HOSTS` etc. | Smaller static tables |
