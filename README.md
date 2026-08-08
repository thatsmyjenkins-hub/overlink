# OVERLINK

**Portable home control brain.** SoftAP arrival → phone CTRL → WallDeck → optional relay. Walk into any home. Own the room in an hour.

| Node | Path | Hardware |
|------|------|----------|
| **Overlink Core** | `firmware/overlink-core` | Waveshare ESP32-S3-LCD-1.47 |
| **WallDeck** | `firmware/walldeck` | CYD 2.8" thin client |
| **CyberDeck** | `firmware/cyberdeck` | IR + CC1101 room peer |
| **Relay** | `services/overlink-relay` | Node WSS + Core dial-out |
| **Product site** | `website/` | Cloud Run marketing + Web Serial flash |

**UI design:** CTRL teal/amber — see `docs/design.md`.

## Product site

Marketing site with setup, screenshots, roadmap, and **browser USB flash** (ESP Web Tools):

```bash
cd website && npm install && npm start
# http://localhost:8080
```

## Quick start (Core)

```bash
cd firmware/overlink-core
# Optional local overrides only:
# cp include/av_secrets.example.h include/av_secrets.h
pio run -t upload
```

1. TF card mounts (seed Home Sprawl or blank grids on SoftAP join).
2. Join open Wi‑Fi **`Overlink-Setup`**.
3. Phone → `http://192.168.44.1` → pick house Wi‑Fi.
4. On home LAN: `http://overlink.local`
5. Arrival Wizard / Ops → Party Tricks / Connectors / Expose as needed.

## WallDeck

```bash
cd firmware/walldeck
pio run -e cyd -t upload
# OTA: pio run -e cyd_ota -t upload
```

## Relay

```bash
cd services/overlink-relay && npm install && npm start
```

## Secrets

Never commit `av_secrets.h`, `firetv_adb_key.h`, or `tools/.ps5_*`. Use examples + SD vault / Arrival for portable homes.

## License

See repository for license terms.
