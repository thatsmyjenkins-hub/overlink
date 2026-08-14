# Overlink firmware assessment (Core · WallDeck · CyberDeck)

**Date:** 2026-08-14  
**Scope:** local repo `/Users/jenkins/repos/overlink` — design docs + headers + main loops. Not a live flash audit.

## Architecture (what’s already strong)

Overlink is the right shape: **Core owns the house**, thin clients talk HTTP, relay is dial-out only.

| Node | Role | Strengths |
|------|------|-----------|
| **Core** (`firmware/overlink-core`) | SD vault, device hub, scenes, automations, connectors, party, AV, relay client, phone portal | Single source of truth; seed-on-TF; OTA; Hue pair/sync; WLED proxy; camera snapshot hook |
| **WallDeck** (`firmware/walldeck`) | CYD LVGL thin client | CoreClient covers scenes/devices/AV/WLED/party/Grace; OTA; power/backlight; last-tab NVS in design |
| **CyberDeck** (`firmware/cyberdeck`) | IR + CC1101 peer + vault | Clear UX map (no dead ends); SoftAP + `.local`; serial IR keys; OTA |
| **Relay** (`services/overlink-relay`) | Cloud Run WSS + pull queue | No inbound home ports; enroll codes; grid heartbeat |

Design language is specified (`docs/design.md` CTRL teal/amber). Phone IA: Home / Zones / Scenes / Ops. WallDeck: no Ops.

---

## P0 — correctness / reliability

1. **WallDeck Core fallback IP is stale**  
   `firmware/walldeck/src/config.h` → `CORE_FALLBACK_IP "192.168.4.55"`.  
   Live Core is **`192.168.4.181` / `overlink.local`**. If mDNS fails, WallDeck talks to the wrong box (or nothing).  
   **Fix:** default `192.168.4.181`, persist last-good Core IP in NVS, probe hostname then fallback.

2. **WallDeck device/scene caps are too small for Home Sprawl**  
   `MAX_DEVICES 12`, `MAX_SCENES 11`, `MAX_ZONES 8`. Seed `devices.json` is far larger (many fallback IPs). Catalog will silently truncate → “missing lights” on the wall.  
   **Fix:** raise caps or paginate by zone (only keep devices for the active zone).

3. **Relay is in-memory only**  
   Cloud Run scale-to-zero / new revision **wipes enroll tokens + queues**. Core keeps token on SD, but a cold relay rejects it until re-enroll.  
   **Fix:** persist grids to GCS/Firestore, or Cloud Run `min-instances=1` + document re-enroll. Also mint a **stable grid token** instead of new token every enroll.

4. **Relay WebSocket auth is incomplete**  
   `/v1/session` mints a session, but `wss` only checks `session` is non-empty + grid exists — **it does not look up the minted session**. Any non-empty string works. Phone sessions aren’t stored with expiry.  
   **Fix:** `sessions.set(token, {gridId, exp})` and validate on upgrade.

5. **No phone remote UI on the relay**  
   Relay is API/WS only. Off-site you have ngrok (unstable URL) or must write a WS client.  
   **Fix:** small static page on relay (or Core portal “remote mode”) that does enroll-session → WS → device/scene buttons.

6. **CyberDeck + WallDeck have no auth**  
   CyberDeck banner: “no UI password”. Fine on LAN, bad if SoftAP is open in public or ngrok-like exposure.  
   **Fix:** optional PIN on SoftAP/UI; Core already more trusted.

---

## P1 — functional gaps (highest leverage)

### Core
- **Connectors are import-only-ish** (Hue / Hubspace / Wink). No first-class **Shelly / ESPHome / Matter / Home Assistant two-way sync** beyond import. HA import without live state polling will drift.
- **Automations** exist (`automation_engine`) but no condition debugger / last-fired log in Ops. Hard to trust.
- **Arrival wizard** is the product moment — if Wi-Fi/SD/seed fails, recovery path should be one button (“wipe seed + rescan”) on the 1.47\" LCD, not only serial.
- **AV stack** is Fire/Vizio/Sony specific. Good for this house; abstract to `av_provider` so a second home doesn’t fork firmware.
- **Party tricks** (BLE advertise, printer, Chromecast) are fun but can lock the radio / annoy neighbors. Need a hard timeout + “party off” on Core LCD.
- **GridEye lite** on Core vs full GridEye firmware — unclear which is canonical; risk of two recon stacks.

### WallDeck
- README is still **“Basement Controller / direct WiZ+WLED”**. Code is thin-client. Docs + `legacy/` will confuse the next flash. Rewrite README to Overlink thin client; keep legacy in `legacy/` only.
- **Touch calibration** is hardcoded `map(p.x, 200, 3700, …)` — no on-device 3-point calibrate. First hardware complaint.
- **No Core discovery UI** if `overlink.local` fails (only fallback IP). Add “scan / enter IP” settings page.
- Design says **resume last tab/zone from NVS** — verify `ui.cpp` actually writes it; if not, implement.
- **MAX_DEVICES 12** makes zone pages incomplete (see P0).
- Deep sleep default-off is correct for OTA; document “enable sleep only after OTA works”.

### CyberDeck
- **Peer protocol with Core** (`deviceHubDeckVizio` / IR replay / RF) — confirm CyberDeck still exposes the same HTTP API WallDeck/Core expect. If CyberDeck moved to a hash-router SPA (`#/ir`) and Core still posts `/vizio`, that’s a silent break.
- Vault is local-only. **Export to Core SD** (`tools/export_cyberdeck_vault.md`) should be a one-tap “push vault to Core” so learned remotes survive a CyberDeck flash.
- SoftAP SSID `CyberDeck-IRRF` vs Overlink `Overlink-Setup` — fine, but phone CTRL should list **nearby peers** (mDNS `cyberdeck.local`) instead of hardcoded IPs in seed (`192.168.4.x`).
- CC1101: persist last-good freq + modulation; don’t make the user re-tune every boot.

### Relay / remote
- Phone WS can queue `device.set` / scenes only. Missing: AV keys, party, identify, WLED JSON — so remote is a subset of LAN CTRL.
- ngrok URL churn: either Cloudflare Tunnel (stable hostname) or **stop exposing Core UI** and only use relay+remote shell.

---

## P2 — polish / power features

| Idea | Why |
|------|-----|
| **Presence → scenes** | GridEye / phone mDNS / BLE of Aaron’s watch as automation trigger |
| **WallDeck “Now” = Core summary** | online/total, last scene, deck online already in `fetchSummary` — make Home tab a real dashboard, not another scene grid |
| **Shared IR/RF library on Core** | CyberDeck learns, Core stores, any deck replays |
| **Multi-home grids** | `grid_store` exists; WallDeck should pick home if >1 |
| **Health in Mission Control** | ping Overlink Core via ngrok + relay `/health` (dashboard Ping button now exists) |
| **OTA channel from product site** | website already has Web Serial manifests — add “check firmware” from Core Ops |
| **Structured logging** | Core serial is noisy; JSONL on SD (`/homes/home/logs/`) for last 200 events |

---

## Suggested build order (after dashboard ship)

1. Fix WallDeck fallback IP + raise/paginate device cap.  
2. Persist relay grid tokens; validate WS sessions.  
3. Confirm CyberDeck HTTP API vs Core `deviceHubDeck*` paths; add vault push.  
4. Relay mini remote UI (scenes + toggles) so ngrok isn’t required.  
5. Rewrite WallDeck README; delete or clearly mark legacy direct-WiZ path.  
6. Touch calibration + Core IP settings on WallDeck.

---

## Don’t do yet

- Matter controller on ESP32-S3 (RAM/flash fight with portal).  
- Replacing LVGL WallDeck with a browser kiosk.  
- Public-auth on Core LAN (friction > risk at home). Optional PIN is enough.
