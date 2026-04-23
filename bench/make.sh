#!/usr/bin/env bash
# Build the Phase-3 HTTP benchmark binary with -O3 (logosc + cc).
# Output: bench/bin/http_hello
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

OUT="$ROOT/bench/bin"
mkdir -p "$OUT"

LOGOSC="$ROOT/build/src/compiler/logosc"
STDLIB="$ROOT/build/stdlib_bin"

if [ ! -x "$LOGOSC" ]; then
    echo "error: logosc not built — run 'cmake --build build' first" >&2
    exit 1
fi

OPT=${OPT:-O3}
"$LOGOSC" "$ROOT/bench/http_hello.logos" \
    "-$OPT" \
    -o "$OUT/http_hello.o" \
    -I "$STDLIB" -L "$STDLIB"

cc "-$OPT" "$OUT/http_hello.o" "$STDLIB"/*.a -lpthread -lm \
    -o "$OUT/http_hello"

echo "built: $OUT/http_hello"
