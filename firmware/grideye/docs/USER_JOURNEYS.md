# CYBERDECK user journey test matrix

## Device (CYD touchscreen)

| # | Journey | Steps | Pass criteria |
|---|---------|-------|---------------|
| D1 | Cold boot + saved WiFi | Power on | Network mode (or auto) → connect; main menu without re-entering password |
| D2 | Live intel no flicker | Stay on main menu 30s during LAN sweep | Progress updates without full-screen flash; only intel band changes |
| D3 | Intel detail + back | Main menu → tap LIVE INTEL band → BACK footer | Detail screen shows phase, %, activity line; BACK returns to menu |
| D4 | WiFi scan back | Menu → WiFi → BACK footer | Returns to main menu |
| D5 | WiFi password cancel | WiFi → network → CANCEL footer | Returns to scan list |
| D6 | Connecting cancel | Connect → CANCEL footer or tap | Returns to scan |
| D7 | Status back | Menu → Status → BACK footer | Returns to main menu |
| D8 | Forget WiFi | Status → FORGET | Clears saved network; scan screen |

## Web (browser on LAN)

| # | Journey | Steps | Pass criteria |
|---|---------|-------|---------------|
| W1 | Home load | Open `http://<ip>/` | COMMAND loads; scan status panel updates |
| W2 | Tab navigation | COMMAND → SPECTRUM → NETMAP → PHANTOM → PROFILES | Each tab renders; nav highlights |
| W3 | Back from host | NETMAP → host row → ← BACK | Returns to NETMAP; stack correct |
| W4 | Breadcrumb back | Drill 2 levels → click breadcrumb | Jumps to correct view |
| W5 | APIs | Run `./scripts/test_intel.sh <ip>` | All endpoints HTTP 200 |

## What the progress bar means (device)

- **LAN_SWEEP** — Probing each IP on your subnet (one address per tick).
- **RF_SCAN** — Scanning nearby Wi-Fi access points.
- **PROFILE** — MAC/vendor/banner enrichment for discovered hosts.
- **MONITOR** — Idle between sweeps; cached counts shown.
