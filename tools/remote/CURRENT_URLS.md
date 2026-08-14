# Overlink remote access (live)

| Surface | URL |
|---------|-----|
| **Core UI (remote tunnel)** | https://nonfallacious-jokingly-delilah.ngrok-free.dev |
| **Core UI (LAN)** | http://overlink.local / http://192.168.4.181 |
| **Relay (Cloud Run)** | https://overlink-relay-m5dqbxglga-uc.a.run.app |
| **Product site** | https://overlink-web-m5dqbxglga-uc.a.run.app |

## Notes

- ngrok free may show a browser interstitial once.
- Tunnel process on Jenkins Mac: `ngrok http http://192.168.4.181:80` (must stay running).
- Relay admin secret: vault key `jv:overlink-relay-admin`.
- Core enrolled as grid `home` with expose enabled (dials out to relay).
