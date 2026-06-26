#!/usr/bin/env bash
# B0.5 smoke: lforge build/run/clean on a hello-world fixture.
#
# Args: $1 = path to lforge binary, $2 = path to logosc, $3 = LOGOS_LIB_DIR.

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT

mkdir -p "$PROJ/src"
cat > "$PROJ/lforge.writ" <<'EOF'
{
    name:    "smoke",
    version: "0.1.0",
    src:     "src",
    entry:   "main"
}
EOF
cat > "$PROJ/src/main.logos" <<'EOF'
package smoke;
fn main() -> i32 {
    return 7;
}
EOF

cd "$PROJ"

# build
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/build.log" 2>&1
if [ ! -x "$PROJ/.lforge/debug/out/smoke" ]; then
    echo "FAIL: build did not produce binary"
    cat "$PROJ/build.log"
    exit 1
fi

# run: should exit 7
"$PROJ/.lforge/debug/out/smoke" && rc=$? || rc=$?
if [ "$rc" != "7" ]; then
    echo "FAIL: produced binary returned $rc, want 7"
    exit 1
fi

# lforge run: same — exits with the binary's exit code
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" run > /dev/null 2>&1 && rc=$? || rc=$?
if [ "$rc" != "7" ]; then
    echo "FAIL: 'lforge run' returned $rc, want 7"
    exit 1
fi

# clean removes .lforge
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" clean
if [ -d "$PROJ/.lforge" ]; then
    echo "FAIL: clean did not remove .lforge"
    exit 1
fi

# Missing manifest → error exit
rm -f "$PROJ/lforge.writ"
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > /dev/null 2>&1 && rc=$? || rc=$?
if [ "$rc" = "0" ]; then
    echo "FAIL: build with no manifest should fail"
    exit 1
fi

echo "OK"
