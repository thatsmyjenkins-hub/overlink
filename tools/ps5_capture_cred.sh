#!/usr/bin/env bash
# Capture PS5 Remote Play DDP user-credential (one-time).
# Requires: PS5 in Rest Mode, Remote Play paired with this Mac, sudo for tcpdump.
set -euo pipefail

PS5_IP="${PS5_IP:-192.168.4.69}"
OUT="${TMPDIR:-/tmp}/ps5_wake_cap.txt"
SECRETS="$(cd "$(dirname "$0")/.." && pwd)/firmware/overlink-core/include/av_secrets.h"

echo "==> Probing PS5 at $PS5_IP:9302 (want HTTP 620 Standby)..."
python3 - <<PY
import socket
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.settimeout(2); s.bind(('',0))
s.sendto(b"SRCH * HTTP/1.1\ndevice-discovery-protocol-version:00030010\n", ("$PS5_IP",9302))
try:
    data,_=s.recvfrom(2048)
    print(data.decode("utf-8","replace").strip())
except Exception as e:
    print("No DDP reply — is PS5 in Rest Mode on LAN?", e)
    raise SystemExit(1)
finally:
    s.close()
PY

IFACE=$(route get "$PS5_IP" 2>/dev/null | awk '/interface:/{print $2}')
IFACE=${IFACE:-en0}
rm -f "$OUT"

echo "==> Starting tcpdump on $IFACE (udp/9302). macOS will ask for your password."
echo "    Then Remote Play will open — select your PS5 so it sends WAKEUP."
sudo tcpdump -i "$IFACE" -n -s0 -l -A udp port 9302 >"$OUT" 2>/tmp/ps5_tcpdump_log.txt &
TPID=$!
sleep 1
open -a RemotePlay || true

echo "==> Waiting up to 45s for user-credential in capture..."
CRED=""
for _ in $(seq 1 45); do
  if CRED=$(rg -o 'user-credential:[0-9]+' "$OUT" 2>/dev/null | head -1 | cut -d: -f2); then
    if [[ -n "$CRED" ]]; then break; fi
  fi
  sleep 1
done
sudo kill "$TPID" 2>/dev/null || true
wait "$TPID" 2>/dev/null || true

if [[ -z "${CRED:-}" ]]; then
  echo "No user-credential captured."
  echo "Tips: PS5 in Rest Mode; Remote Play already linked; click the console in the app while capturing."
  echo "Raw capture: $OUT"
  exit 1
fi

echo ""
echo "Captured credential: $CRED"
if [[ -f "$SECRETS" ]]; then
  if rg -q 'PS5_USER_CREDENTIAL' "$SECRETS"; then
    sed -i '' -E "s|#define PS5_USER_CREDENTIAL \".*\"|#define PS5_USER_CREDENTIAL \"$CRED\"|" "$SECRETS"
  else
    printf '\n#define PS5_USER_CREDENTIAL "%s"\n' "$CRED" >>"$SECRETS"
  fi
  echo "Wrote $SECRETS"
  echo "Next: cd firmware/overlink-core && pio run -e overlink-core_ota -t upload"
else
  echo "Add to av_secrets.h:"
  echo "  #define PS5_USER_CREDENTIAL \"$CRED\""
fi
