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
build_one () {
    local src=$1 name=$2
    "$LOGOSC" "$ROOT/bench/$src" \
        "-$OPT" \
        -o "$OUT/$name.o" \
        -I "$STDLIB" -L "$STDLIB"
    cc "-$OPT" "$OUT/$name.o" "$STDLIB"/*.a -lpthread -lm \
        -o "$OUT/$name"
    echo "built: $OUT/$name"
}

build_one http_hello.logos    http_hello
build_one http_hello_mt.logos http_hello_mt
build_one garden/origin.logos garden_origin
