#!/usr/bin/env bash
# ---------------------------------------------------------------------
# firestorm.sh – Aggressive test to validate server MAX_CLIENTS limit
#
# Strategy:
#  1. Fire 1024 simultaneous connections using GNU parallel.
#  2. Attempt 10 extra connections beyond the limit.
#  3. Report how many connections the server actually accepted.
#  4. Clean up all backgrounded nc processes.
#
# Dependencies: nc, ss, parallel
# ---------------------------------------------------------------------

set -euo pipefail

HOST="${1:-localhost}"
PORT="${2:-3490}"
MAX_ALLOWED=1024
EXTRA=10
TOTAL=$((MAX_ALLOWED + EXTRA))
PARALLEL_JOBS=$MAX_ALLOWED

PIDS=()
log() { printf "\e[36m[%s]\e[0m %s\n" "$(date +%T)" "$*"; }
cleanup() {
  log "Cleaning up ${#PIDS[@]} nc processes…"
  kill "${PIDS[@]}" 2>/dev/null || true
  wait "${PIDS[@]}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# ---------------------------------------------------------------------
log "Target  : $HOST:$PORT"
log "Firing $MAX_ALLOWED parallel connections…"

# Fire MAX_ALLOWED in parallel, blocking call
seq 1 "$MAX_ALLOWED" | parallel -j"$PARALLEL_JOBS" "nc $HOST $PORT >/dev/null" &

# Meanwhile, launch EXTRA connections (testing server rejection)
log "Firing $EXTRA extra connections…"
for i in $(seq 1 "$EXTRA"); do
  nc "$HOST" "$PORT" >/dev/null &
  PIDS+=("$!")
done

sleep 2  # Allow system to stabilize

# Count established connections
fd_count=$(ss -Htan state established "sport = :$PORT" | wc -l)
log "Established connections: $fd_count (expected: $MAX_ALLOWED)"

# Check if cap was enforced
if [[ "$fd_count" -le "$MAX_ALLOWED" ]]; then
  log "Connection cap enforced. Test PASS."
else
  log "Too many connections accepted. Test FAIL."
  exit 1
fi

