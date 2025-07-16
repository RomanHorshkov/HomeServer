#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# server-test-suite.sh  ──  One‑stop stress & behaviour harness for your HTTP server
#
# Usage:
#   ./server-test-suite.sh <target-host> <port>
#   (defaults: localhost 3490)
#
# Requirements:
#   * wrk2       – high‑throughput HTTP generator
#   * nc         – netcat (for idle fd test)
#   * ss / lsof  – socket counters
#   * jq         – pretty JSON (optional for reports)
# -----------------------------------------------------------------------------
set -euo pipefail

HOST="${1:-localhost}"
PORT="${2:-3490}"
BASE_URL="http://${HOST}:${PORT}"

WRK_THREADS=2        # wrk2 -t
WRK_CONN=4000        # wrk2 -c
WRK_RATE=0           # 0 → max possible
WRK_DURATION="30s"  # wrk2 -d

IDLE_FD_TIMEOUT=20   # seconds allowed for worker to reap idle sockets

REPORT_DIR="test-reports/$(date +%F_%H-%M-%S)"
mkdir -p "$REPORT_DIR"

log() { printf "\e[36m[%s]\e[0m %s\n" "$(date +%T)" "$*"; }
sep() { printf '\n%s\n' "$(printf '─%.0s' {1..80})"; }

#---------------------------------------------------------------------------
# helper: count current established fds to PORT (IPv4+IPv6)
count_fds() {
  ss -n state established "( sport = :$PORT or dport = :$PORT )" | awk 'END{print NR-1}'
}

#---------------------------------------------------------------------------
run_wrk_load() {
  log "Running wrk2 load: -t$WRK_THREADS -c$WRK_CONN -d$WRK_DURATION --rate $WRK_RATE $BASE_URL/"
  wrk2 -t$WRK_THREADS -c$WRK_CONN -d$WRK_DURATION --rate $WRK_RATE "$BASE_URL/" >"$REPORT_DIR/wrk.txt" 2>&1 || true
  log "wrk2 completed – results stored in $REPORT_DIR/wrk.txt"
}

#---------------------------------------------------------------------------
run_idle_evap_test() {
  sep; log "[idle‑fd] Spawning 15 dummy clients with netcat; then measuring evaporation"
  local NUM=15
  local pids=()
  for i in $(seq 1 $NUM); do
    (exec nc "$HOST" "$PORT" >/dev/null &)&
    pids+=("$!")
    sleep 0.05
  done
  log "Spawned ${#pids[@]} netcat clients."
  sleep 1
  local alive_before=$(count_fds)
  log "Established fds after 1 s: $alive_before"
  printf '%s\n' "${pids[@]}" | xargs kill -s SIGTERM 2>/dev/null || true
  log "Sent SIGTERM to nc clients – waiting for server to reap..."
  for s in $(seq 1 $IDLE_FD_TIMEOUT); do
    sleep 1
    local n=$(count_fds)
    printf "\r⌚ %2ds  %3d fds still alive" "$s" "$n"
    [[ $n -eq 0 ]] && break
  done; echo
  local alive_after=$(count_fds)
  if [[ $alive_after -eq 0 ]]; then
    log "PASS: idle fds evaporated within $IDLE_FD_TIMEOUT s"
  else
    log "FAIL: $alive_after idle fds still present after $IDLE_FD_TIMEOUT s"
  fi
}

#---------------------------------------------------------------------------
run_slow_loris() {
  sep; log "[slow‑loris] Sending 100 slow headers and verifying timeout"
  for i in {1..100}; do
    (printf "GET / HTTP/1.1\r\nHost: $HOST\r\n"; sleep 5) | nc "$HOST" "$PORT" &
  done
  sleep 10
  local n=$(count_fds)
  log "Server currently holds $n half‑open header sockets (expect some but not exploding)."
}

#---------------------------------------------------------------------------
run_suite() {
  sep; log "Target: $BASE_URL"; sep
  run_wrk_load
  run_idle_evap_test
  run_slow_loris
  sep; log "All tests done – see $REPORT_DIR for artefacts"
}

run_suite

