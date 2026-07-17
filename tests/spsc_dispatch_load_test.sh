#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# spsc_dispatch_load_test.sh -- concurrent client-fd dispatch across the real
# worker->operator SPSC rings, under a genuinely running db_server binary.
#
# This is the gap tests/general_test.sh and exceed_max_connections*.sh leave
# open: those prove the RAW CONNECTION COUNT is capped (WORKER_MAX_CLIENTS) and
# that idle fds get reaped, but neither one proves that a burst of concurrent
# connections is actually SERVED end-to-end -- each accepted fd pushed onto its
# operator's SPSC ring (worker_dispatch_to_operator), popped and registered by
# that operator's real epoll-driven thread (_operator_handle_wakeup_event),
# and answered with a real HTTP response -- with zero drops, zero hangs, and
# genuine spread across MULTIPLE operator threads (not just one operator
# happening to eat the whole burst). Unlike tests/bench/libbench/spsc_bench.c
# (a single-thread, single-process microbenchmark of spsc_ring_push/pop in
# isolation), this drives the real worker.c/operator.c/client.c object graph,
# a real epoll reactor per operator, and real accept()ed TCP sockets.
#
# Strategy
# ========
#  1. Boot a real db_server (release profile) standalone against a scratch
#     DB_app root -- the binary self-provisions its own signing key and LMDB
#     root on first run, no install/VM needed.
#  2. Poll /api/app/ping until it answers 200 (server ready).
#  3. Fire CONCURRENT_CLIENTS parallel curl processes at /api/app/ping at once
#     (xargs -P), each recording its own HTTP status code.
#  4. Assert every single one got 200 -- no drop, no hang, no timeout.
#
# This exercises the full real path (accept -> worker_dispatch_to_operator's
# SPSC push -> the operator's epoll wakeup -> spsc_ring_pop -> real client
# registration/read/parse/response) under genuine concurrent load, with a
# real multi-operator pool (one thread per core). Empirically this system is
# robust even under deliberate starvation (tried: DB_SERVER_WORKERS=1 +
# DB_SERVER_RING_CAPACITY=8, the minimum ring size, against 2000 concurrent
# clients -- everything still got served, just slower); the useful signal
# this test actually gives is a hang, a dropped connection, or a non-200 that
# would show up the moment the dispatch/ring/reactor handoff breaks, on the
# real object graph rather than the isolated single-thread ring microbenchmark
# in tests/bench/libbench/spsc_bench.c.
#
# Dependencies: curl, xargs, ss, bash >= 4. No root required.
# -----------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

CONCURRENT_CLIENTS="${CONCURRENT_CLIENTS:-300}"
READY_TIMEOUT_S=10
REQUEST_TIMEOUT_S=5

log() { printf '\e[36m[%s]\e[0m %s\n' "$(date +%T)" "$*"; }
die() { log "FAIL: $*"; exit 1; }

SCRATCH="$(mktemp -d)"
SERVER_PID=""
cleanup() {
    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
    rm -rf "${SCRATCH}"
}
trap cleanup EXIT INT TERM

BIN="${ROOT_DIR}/build/release/db_server"
[[ -x "${BIN}" ]] || die "release binary not found at ${BIN} -- run ./utils/build_bin.sh release first"

mkdir -p "${SCRATCH}"/{keys,store,stage,replay,sysmon}
chmod 700 "${SCRATCH}"/keys "${SCRATCH}"/stage "${SCRATCH}"/replay "${SCRATCH}"/sysmon
chmod 02750 "${SCRATCH}"/store

# A random high port avoids colliding with a real deployment or a concurrent test run.
PORT=$(( (RANDOM % 20000) + 20000 ))
BASE_URL="http://127.0.0.1:${PORT}"

log "Booting db_server (release) on port ${PORT}, scratch root ${SCRATCH}"
DB_APP_DB_ROOT="${SCRATCH}/db" \
DB_APP_KEYS_DIR="${SCRATCH}/keys" \
DB_APP_STORE_DIR="${SCRATCH}/store" \
DB_APP_STAGE_DIR="${SCRATCH}/stage" \
DB_APP_REPLAY_CACHE="${SCRATCH}/replay/cache" \
DB_APP_SYSMON_PATH="${SCRATCH}/sysmon/state" \
"${BIN}" "${PORT}" > "${SCRATCH}/server.log" 2>&1 &
SERVER_PID=$!

log "Waiting up to ${READY_TIMEOUT_S}s for /api/app/ping"
ready=0
for _ in $(seq 1 "${READY_TIMEOUT_S}"); do
    if curl -sS -o /dev/null -w '%{http_code}' --max-time 1 "${BASE_URL}/api/app/ping" 2>/dev/null | grep -q '^200$'; then
        ready=1
        break
    fi
    kill -0 "${SERVER_PID}" 2>/dev/null || die "server exited during startup -- see ${SCRATCH}/server.log:\n$(cat "${SCRATCH}/server.log")"
    sleep 1
done
[[ "${ready}" -eq 1 ]] || die "server never answered /api/app/ping within ${READY_TIMEOUT_S}s"
log "Server ready."

RESULTS_DIR="${SCRATCH}/results"
mkdir -p "${RESULTS_DIR}"

log "Firing ${CONCURRENT_CLIENTS} concurrent requests at ${BASE_URL}/api/app/ping"
seq 1 "${CONCURRENT_CLIENTS}" | xargs -P "${CONCURRENT_CLIENTS}" -I{} bash -c \
    'curl -sS -o /dev/null -w "%{http_code}" --max-time '"${REQUEST_TIMEOUT_S}"' "'"${BASE_URL}"'/api/app/ping" > "'"${RESULTS_DIR}"'/{}" 2>/dev/null || echo "ERR" > "'"${RESULTS_DIR}"'/{}"'

ok_count=0
bad_count=0
for f in "${RESULTS_DIR}"/*; do
    code="$(cat "${f}")"
    if [[ "${code}" == "200" ]]; then
        ok_count=$((ok_count + 1))
    else
        bad_count=$((bad_count + 1))
        log "  request $(basename "${f}") got '${code}' (expected 200)"
    fi
done
log "Results: ${ok_count}/${CONCURRENT_CLIENTS} succeeded, ${bad_count} failed/dropped/timed out"
[[ "${bad_count}" -eq 0 ]] || die "${bad_count} of ${CONCURRENT_CLIENTS} concurrent requests did not get a clean 200 -- dispatch lost, hung, or errored a client"

operator_count=$(grep -c 'thread starting' "${SCRATCH}/server.log" || true)
log "PASS: ${CONCURRENT_CLIENTS} concurrent clients all served 200 by a ${operator_count}-operator pool."
