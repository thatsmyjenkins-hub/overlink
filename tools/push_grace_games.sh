#!/usr/bin/env bash
# Upload Grace Party Pack decks from data/games/grace to Overlink Core TF.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORE="${CORE_URL:-http://192.168.4.55}"
DATA="$ROOT/data/games/grace"

if [[ ! -f "$DATA/meta.json" ]]; then
  echo "missing $DATA/meta.json — export decks first (ask agent or re-run export)"
  exit 1
fi

upload() {
  local rel="$1"
  local file="$DATA/$rel"
  echo "→ $rel ($(du -h "$file" | awk '{print $1}'))"
  curl -sS -m 300 -F "file=@${file}" \
    "${CORE}/api/games/grace/upload?name=${rel}"
  echo
}

echo "Pushing Grace decks to $CORE"
upload "meta.json"
upload "decks/heads.json"
for f in "$DATA"/decks/*.json; do
  base="$(basename "$f")"
  [[ "$base" == "heads.json" ]] && continue
  upload "decks/$base"
done
echo "OK — open ${CORE}/games/grace/"
