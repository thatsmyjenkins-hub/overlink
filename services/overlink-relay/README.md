# Overlink Relay

Small always-on host that lets a phone reach an Overlink Core off-site **without inbound router ports**.

- **Core** dials out over HTTP (`/v1/grids/:id/heartbeat` + `/pull`)
- **Phone / app** attaches with a short-lived session over **WebSocket** (`/v1/ws`)

## Run

```bash
cd services/overlink-relay
npm install
RELAY_ADMIN_SECRET=change-me PORT=8787 npm start
```

## Pair a Core

1. Create an enroll code (admin):

```bash
curl -s -X POST localhost:8787/v1/admin/enroll-code \
  -H 'content-type: application/json' \
  -d '{"secret":"change-me"}'
```

2. On the Core Ops → Expose panel: set relay URL (`http://your-host:8787`), enter the code, Enroll.

3. Toggle **Expose this grid** on. Core heartbeats every ~2.5s.

## Phone session

```bash
# session (use grid token from enroll response, or admin secret)
curl -s -X POST localhost:8787/v1/session \
  -H 'content-type: application/json' \
  -d '{"gridId":"my-grid","secret":"change-me"}'
# then connect WebSocket to ws://host:8787/v1/ws?session=…&grid=…
# send: {"type":"command","op":"device.set","deviceId":"…","on":true}
```
