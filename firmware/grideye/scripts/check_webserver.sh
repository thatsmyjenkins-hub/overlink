#!/usr/bin/env bash
# Usage: ./scripts/check_webserver.sh [device-ip]
# Probes CYBERDECK HTTP server (static + API). Exits non-zero on failure.
set -euo pipefail

IP="${1:-}"
if [[ -z "$IP" ]]; then
  echo "Usage: $0 <device-ip>"
  echo "  Tip: IP is shown on the CYD Status screen or serial log [CYBERDECK] http://..."
  exit 1
fi

BASE="http://${IP}"
CURL="${CURL:-curl}"
if ! command -v "$CURL" >/dev/null 2>&1; then
  echo "curl not found; using python3"
  exec python3 - "$IP" <<'PY'
import sys, urllib.request, json
base = f"http://{sys.argv[1]}"
paths = ["/api/health", "/", "/app.css", "/app.js",
         "/api/intel/summary", "/api/intel/wifi", "/api/intel/hosts",
         "/api/intel/profiles", "/api/intel/events"]
for p in paths:
    r = urllib.request.urlopen(base + p, timeout=8)
    print(f"  {r.status}  {len(r.read())}B  {p}")
h = json.loads(urllib.request.urlopen(base + "/api/health", timeout=8).read())
print(f"health: ok={h.get('ok')} ip={h.get('ip')} fs={h.get('fs')} hosts={h.get('hosts')}")
if not h.get("ok"):
    sys.exit(1)
PY
fi

echo "CYBERDECK web server @ $BASE"
for path in /api/health / /app.css /app.js \
  /api/intel/summary /api/intel/wifi /api/intel/hosts \
  /api/intel/profiles /api/intel/events; do
  code=$("$CURL" -s -o /dev/null -w "%{http_code}" --connect-timeout 8 "${BASE}${path}")
  echo "  $code  $path"
  [[ "$code" == "200" ]] || { echo "FAIL: $path"; exit 1; }
done

health=$("$CURL" -s --connect-timeout 8 "${BASE}/api/health")
echo "  health: $health"
echo "$health" | grep -q '"ok":true' || { echo "FAIL: health ok!=true"; exit 1; }
echo "OK — web server responding"
