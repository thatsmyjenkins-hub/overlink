#!/bin/bash
# Exercise LOCAL mode dashboard APIs for 60s and report success/failure rates.
BASE="${1:-http://192.168.4.43}"
DURATION="${2:-60}"
echo "Testing $BASE for ${DURATION}s..."
ok=0 fail=0 busy=0
end=$((SECONDS + DURATION))
while [ $SECONDS -lt $end ]; do
  code=$(curl -s -m 4 -o /tmp/cyd_poll.json -w "%{http_code}" "$BASE/api/intel/all")
  bytes=$(wc -c < /tmp/cyd_poll.json | tr -d ' ')
  if [ "$code" = "200" ] && [ "$bytes" -gt 100 ]; then
    ok=$((ok+1))
    echo "[OK] intel/all $bytes bytes"
  elif [ "$code" = "503" ]; then
    busy=$((busy+1))
    echo "[BUSY] deferred"
  else
    fail=$((fail+1))
    echo "[FAIL] HTTP $code"
  fi
  hcode=$(curl -s -m 3 -o /dev/null -w "%{http_code}" "$BASE/api/health")
  [ "$hcode" = "200" ] || echo "[WARN] health HTTP $hcode"
  sleep 3
done
echo "---"
echo "intel/all: ok=$ok busy=$busy fail=$fail"
total=$((ok+busy+fail))
if [ $total -gt 0 ]; then
  pct=$((ok * 100 / total))
  echo "success rate: ${pct}%"
fi
