# Boot & touch UX test plan

Automated serial tests: `python3 scripts/test_boot_ux.py [/dev/cu.usbserial-10]`

## Automated (serial)

| ID | Expectation |
|----|-------------|
| T01 | `CYBERDECK NET INTEL` banner after reset |
| T02 | `[BOOT] ask_net=0/1 mode=…` logged |
| T03 | If `ask_net=1`: mode picker stays up; no join within ~3s |
| T04 | `[HEAP]` every 5s (main loop not blocked) |
| T05 | `[NET] WiFi scan started (async)` then `Device scan: N networks` |
| T06 | No Guru Meditation / panic |
| T07 | **Interactive** — tap WiFi scan → `[TOUCH] screen=WIFI_SCAN` or `[BTN] wifi_scan:` |

## Manual (device)

1. **Mode picker** — Reboot; picker stays until you tap JOIN or FIELD (≥3s).
2. **Field path** — Tap FIELD RECON → CYBERDECK AP → `http://192.168.4.1/` field dashboard.
3. **Join fail path** — Tap JOIN away from home → CONNECTING (touch works) → fail → WiFi list.
4. **WiFi list touch** — Tap your SSID → password screen. Tap SCAN → rescan. Tap `< BACK` → mode picker.
5. **Portal banner** — When CYBERDECK AP is up, AP rows sit *below* the blue portal hint (not under it).

## Fixes in this build

- Boot callbacks registered **before** mode picker; `finishBootFlow()` gates `poll()`.
- Legacy `ask_net=false` from old auto-connect bug **migrated back to true** once.
- WiFi connect/scan **non-blocking** (touch alive during CONNECTING).
- WiFi scan **touch targets** aligned when portal banner is shown.
- `vspiSelectTouch()` each UI loop (SD/WiFi can steal VSPI MISO).
