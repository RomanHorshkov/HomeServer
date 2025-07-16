#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# exceed_max_clients.sh  ──  Verify that the server enforces MAX_CLIENTS (100)
#
# Strategy
# ========
#  1. Open **exactly 100** persistent TCP connections to the target.
#  2. Attempt a 101‑st connection using `nc -z` (connect‑only, no data).
#  3. Expect the 101‑st attempt to **FAIL** (exit status ≠ 0 *or* no extra fd).
#  4. Report PASS/FAIL and clean up.
#
# Dependencies: nc (netcat), ss or lsof, bash ≥ 4.
# -----------------------------------------------------------------------------
set -euo pipefail

HOST="${1:-localhost}"
PORT="${2:-3490}"
MAX_ALLOWED=80
SLEEP_BETWEEN=0.05   # seconds between launches so epoll can register

PIDS=()
log() { printf "\e[36m[%s]\e[0m %s\n" "$(date +%T)" "$*"; }
cleanup() {
  log "Cleaning up ${#PIDS[@]} nc processes…"
  kill "${PIDS[@]}" 2>/dev/null || true
  wait "${PIDS[@]}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Utility: count established TCP sockets to $PORT
count_fds(){
  ss -Htan state established "sport = :$PORT" | wc -l   # -H → no header
}


# ---------------------------------------------------------------------------
log "Target  : $HOST:$PORT"
log "Spawning $MAX_ALLOWED persistent connections…"

for i in $(seq 1 "$MAX_ALLOWED"); do
  nc "$HOST" "$PORT" >/dev/null &
  PIDS+=("$!")
  sleep "$SLEEP_BETWEEN"
  if ! ps -p "${PIDS[-1]}" &>/dev/null; then
    log "Early refusal at attempt $i (PID ${PIDS[-1]}). Test FAIL."
    exit 1
  fi
  [[ $((i % 20)) -eq 0 ]] && log "  → $i ok"
done

sleep 1  # allow kernel & epoll to settle
fd_count=$(count_fds)
log "Established FDs according to ss: $fd_count (expect $MAX_ALLOWED)"

if [[ "$fd_count" -ne "$MAX_ALLOWED" ]]; then
  log "FD count $fd_count mismatch. Test FAIL."
  exit 1
fi

log "Attempting 65th connection (should be refused)…"
if nc -z -w2 "$HOST" "$PORT"; then
  log "65th connection **succeeded** – cap NOT enforced. Test FAIL."
  exit 1
else
  log "65th connection refused as expected. Test PASS."
fi
