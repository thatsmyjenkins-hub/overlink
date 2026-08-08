#!/usr/bin/env bash
# Usage:
#   ./scripts/test_intel.sh <device-ip>   # live hardware smoke test
#   ./scripts/test_intel.sh web           # API + browser tests (mock server, no device)
set -euo pipefail

if [[ "${1:-}" == "web" ]]; then
  NODE="${CYD_NODE:-/Applications/Cursor.app/Contents/Resources/app/resources/helpers/node}"
  if [[ ! -x "$NODE" ]]; then
    NODE="$(command -v node)"
  fi
  export PATH="$(dirname "$NODE"):$PATH"
  ROOT="$(cd "$(dirname "$0")/.." && pwd)"
  cd "$ROOT"
  [[ -d node_modules ]] || npm install
  npx playwright install chromium 2>/dev/null || true
  exec "$NODE" scripts/run_web_tests.mjs
fi

IP="${1:-}"
if [[ -z "$IP" ]]; then
  echo "Usage: $0 <device-ip> | web"
  exit 1
fi
BASE="http://${IP}"
echo "Testing CYBERDECK at $BASE"
for path in /api/health / /app.css /app.js /api/intel/summary /api/intel/wifi /api/intel/hosts /api/intel/profiles /api/intel/events; do
  code=$(curl -s -o /dev/null -w "%{http_code}" "${BASE}${path}")
  echo "  $code  $path"
  [[ "$code" == "200" ]] || { echo "FAIL: $path"; exit 1; }
done
echo "OK — all endpoints returned 200"
