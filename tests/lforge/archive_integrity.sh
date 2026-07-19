#!/usr/bin/env bash
# Archive integrity: no package may vanish from a module archive silently.
#
# Regression for the "silent package drop" class. Three properties:
#
#   1. SIZE IS NOT A LIMIT. A single source file far past the size at which a
#      package was once believed to drop (~3600 lines) still publishes its
#      package, and the package is usable by a consumer. The reported threshold
#      never existed; this test keeps it that way.
#
#   2. NO STALE MEMBERS. `ar r` inserts, it never truncates. Rebuilding an
#      archive whose module was renamed used to leave the previous build's
#      whole `<oldname>.{o,wr0,pkgi,imp}` quartet in place, so the loader saw
#      TWO .pkgi members and could index the STALE package set first-wins —
#      shipping dead code with exit 0. The archive must be exactly the members
#      the writer wrote.
#
#   3. THE WRITER VERIFIES ITSELF. emit_module reads the finished .a back
#      through the consumer's own reader and fails loudly, naming the package,
#      if anything it meant to publish did not arrive.
#
# Args: $1 = logosc path, $2 = LOGOS_LIB_DIR

set -euo pipefail

LOGOSC="${1:?logosc path}"
LIB="${2:?LOGOS_LIB_DIR}"

PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT

# ── 1. A module far past the claimed size threshold ────────────────────────
# ~6000 lines / ~150KB in one file: comfortably past the ~3600 lines at which
# the package was reported to disappear.
mkdir -p "$PROJ/src"
cat > "$PROJ/big.module" <<EOF
module bigmod
version 0.1
root $PROJ/src/
EOF

{
    echo "package bigmod.grow;"
    echo
    for i in $(seq 0 999); do
        echo "pub fn grow_${i}(x: i64) -> i64 {"
        echo "    let a: i64 = x + ${i};"
        echo "    let b: i64 = a * 3;"
        echo "    return b - ${i};"
        echo "}"
    done
} > "$PROJ/src/grow.logos"

LINES=$(wc -l < "$PROJ/src/grow.logos")
[ "$LINES" -gt 5000 ] || { echo "FAIL: fixture only $LINES lines, want >5000"; exit 1; }

"$LOGOSC" --emit-module "$PROJ/big.module" -L "$LIB" -o "$PROJ/libbig.a" >/dev/null 2>&1

# The package must be advertised in the .pkgi the loader consults.
ar p "$PROJ/libbig.a" bigmod.pkgi 2>/dev/null | strings | grep -qx "bigmod.grow" || {
    echo "FAIL: bigmod.grow ($LINES lines) missing from libbig.a package index"
    ar t "$PROJ/libbig.a"
    exit 1
}

# ── …and it is actually USABLE, not merely listed ──────────────────────────
cat > "$PROJ/use.logos" <<'EOF'
package use_bigmod;
use bigmod.grow;
fn main() -> i32 {
    return grow_999(0) as i32;
}
EOF

"$LOGOSC" "$PROJ/use.logos" -l "$PROJ/libbig.a" -o "$PROJ/use.o" >/dev/null 2>&1
cc "$PROJ/use.o" "$PROJ/libbig.a" "$LIB"/lib*.a -lpthread -lm -o "$PROJ/use" 2>/dev/null
"$PROJ/use" && rc=$? || rc=$?
# grow_999(0) = ((0 + 999) * 3) - 999 = 1998; 1998 mod 256 = 206.
[ "$rc" = "206" ] || { echo "FAIL: consumer returned $rc, want 206"; exit 1; }

# ── 2. Rebuild under a new module name leaves no stale members ─────────────
cat > "$PROJ/big.module" <<EOF
module bigmod2
version 0.1
root $PROJ/src/
EOF
cat > "$PROJ/src/grow.logos" <<'EOF'
package bigmod2.grow;
pub fn only_fn(x: i64) -> i64 {
    return x + 1;
}
EOF

"$LOGOSC" --emit-module "$PROJ/big.module" -L "$LIB" -o "$PROJ/libbig.a" >/dev/null 2>&1

if ar t "$PROJ/libbig.a" | grep -q '^bigmod\.'; then
    echo "FAIL: stale members from the previous build survived the rebuild"
    ar t "$PROJ/libbig.a"
    exit 1
fi
NPKGI=$(ar t "$PROJ/libbig.a" | grep -c '\.pkgi$')
[ "$NPKGI" = "1" ] || { echo "FAIL: $NPKGI .pkgi members in archive, want 1"; exit 1; }

# The stale package must no longer be advertised by this archive.
if ar p "$PROJ/libbig.a" bigmod2.pkgi 2>/dev/null | strings | grep -qx "bigmod.grow"; then
    echo "FAIL: stale package bigmod.grow still advertised after rebuild"
    exit 1
fi

# ── 3. The writer's self-verification is wired and reports package counts ──
LOGOS_EMIT_VERBOSE=1 "$LOGOSC" --emit-module "$PROJ/big.module" -L "$LIB" \
    -o "$PROJ/libbig.a" >"$PROJ/verbose.log" 2>&1 || true
grep -q "verified .* published package" "$PROJ/verbose.log" || {
    echo "FAIL: emit_module did not verify published packages after ar"
    tail -20 "$PROJ/verbose.log"
    exit 1
}

echo "OK"
