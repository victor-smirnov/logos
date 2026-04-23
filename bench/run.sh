#!/usr/bin/env bash
# Phase-3 HTTP benchmark driver.
#
#   bench/run.sh                # hello-world scenario, ab driver
#
# Builds the server if missing, runs three ab configs (serial, c=10, c=50)
# at REQUESTS each (default 10k), writes results to
# bench/results/<timestamp>.txt.
#
# Note: higher concurrency or request counts can crash the server under the
# current single-thread serve_until path (UAF-under-load, not yet fixed).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

BIN="$ROOT/bench/bin/http_hello"
if [ ! -x "$BIN" ]; then
    echo "[bench] building server (first run)…"
    "$ROOT/bench/make.sh"
fi

PORT=18080
URL="http://127.0.0.1:$PORT/"
REQUESTS=${REQUESTS:-10000}

TS=$(date +%Y%m%d_%H%M%S)
OUTDIR="$ROOT/bench/results"
mkdir -p "$OUTDIR"
OUT="$OUTDIR/${TS}.txt"

run_ab () {
    local n=$1 c=$2 label=$3
    "$BIN" >/dev/null 2>&1 &
    local srv=$!
    sleep 0.3
    if ! curl -s -o /dev/null "$URL"; then
        echo "server did not come up for $label" >&2
        kill "$srv" 2>/dev/null || true
        wait 2>/dev/null || true
        return 1
    fi
    echo "--- $label (n=$n c=$c) ---"
    ab -n "$n" -c "$c" -q "$URL" 2>&1 \
        | grep -E "Requests per second|Time per request|Failed requests|Complete requests|Transfer rate"
    echo
    kill "$srv" 2>/dev/null || true
    wait 2>/dev/null || true
}

{
    echo "=== Logos HTTP bench — $TS ==="
    echo "server:      $BIN"
    echo "url:         $URL"
    echo "build:       -O3, serve_until single-thread"
    echo
    run_ab "$REQUESTS" 1  "serial" || true
    run_ab "$REQUESTS" 10 "moderate concurrency" || true
    run_ab "$REQUESTS" 50 "higher concurrency" || true
} | tee "$OUT"

echo
echo "[bench] results → $OUT"
