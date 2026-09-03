#!/usr/bin/env bash
#
# Launch a calendae binary, wait for it to settle, then dump its memory
# footprint (RSS / PSS / largest mappings). Linux only.
#
# Usage:
#   scripts/measure-memory.sh [path-to-binary] [settle-seconds]
#
#   path-to-binary   default: <repo>/build/calendae
#   settle-seconds   how long to wait before sampling (default 45). Sign in
#                    and land on the month view within that window.
#
# The binary is killed when this script exits.
#
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN="${1:-$ROOT/build/calendae}"
SETTLE="${2:-45}"

[ -x "$BIN" ] || { echo "not an executable: $BIN" >&2; exit 1; }

"$BIN" &
PID=$!
trap 'kill "$PID" 2>/dev/null || true' EXIT

echo ">> $BIN  (pid $PID)"
echo ">> sign in and open the month view; sampling in ${SETTLE}s"
sleep "$SETTLE"

kill -0 "$PID" 2>/dev/null || { echo "process exited before sampling" >&2; exit 1; }

echo
echo "=== /proc/$PID/smaps_rollup ==="
grep -E '^(Rss|Pss|Pss_Dirty|Shared_Clean|Private_Dirty|Swap):' \
    "/proc/$PID/smaps_rollup" || true

echo
echo "=== /proc/$PID/status ==="
grep -E '^(VmRSS|RssAnon|RssFile|Threads):' "/proc/$PID/status" || true

echo
echo "=== 25 largest mappings by resident KB (pmap -x) ==="
pmap -x "$PID" | sort -k3 -n | tail -25
