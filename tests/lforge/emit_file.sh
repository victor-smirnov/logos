#!/usr/bin/env bash
# B1.7: per-file emit mode for logosc.
#
# Validates that:
#   - `logosc --emit-module M --only-file F -o P` writes only P.o + P.writ0
#   - per-file objects, aggregated by `ar`, link into a working library
#   - the result is observably equivalent to a monolithic --emit-module build
#
# Args: $1 = logosc path, $2 = LOGOS_LIB_DIR

set -euo pipefail

LOGOSC="${1:?logosc path}"
LIB="${2:?LOGOS_LIB_DIR}"

PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT

# ── Multi-file library: two .logos files in one package ────────────────────
mkdir -p "$PROJ/src"
cat > "$PROJ/lib.module" <<EOF
module mylib
version 0.1
root $PROJ/src/
EOF

cat > "$PROJ/src/a.logos" <<'EOF'
package mylib;
pub fn from_a() -> i32 { return 100; }
EOF

cat > "$PROJ/src/b.logos" <<'EOF'
package mylib;
pub fn from_b() -> i32 { return 200; }
EOF

# ── Monolithic build (existing path) ───────────────────────────────────────
"$LOGOSC" --emit-module "$PROJ/lib.module" -o "$PROJ/mono.a" >/dev/null 2>&1

# ── Per-file build: invoke once per source file ────────────────────────────
mkdir -p "$PROJ/perfile"
"$LOGOSC" --emit-module "$PROJ/lib.module" --only-file "$PROJ/src/a.logos" \
    -o "$PROJ/perfile/a" >/dev/null 2>&1
"$LOGOSC" --emit-module "$PROJ/lib.module" --only-file "$PROJ/src/b.logos" \
    -o "$PROJ/perfile/b" >/dev/null 2>&1

# Each invocation writes exactly two files with the requested prefix.
[ -f "$PROJ/perfile/a.o" ]       || { echo "FAIL: a.o missing"; exit 1; }
[ -f "$PROJ/perfile/a.writ0" ] || { echo "FAIL: a.writ0 missing"; exit 1; }
[ -f "$PROJ/perfile/a.hm0" ]     || { echo "FAIL: a.hm0 missing"; exit 1; }
[ -f "$PROJ/perfile/b.o" ]       || { echo "FAIL: b.o missing"; exit 1; }
[ -f "$PROJ/perfile/b.writ0" ] || { echo "FAIL: b.writ0 missing"; exit 1; }
[ -f "$PROJ/perfile/b.hm0" ]     || { echo "FAIL: b.hm0 missing"; exit 1; }

# Per-file emit must NOT produce a .a (no `ar` step).
[ ! -f "$PROJ/perfile/a.a" ] || { echo "FAIL: per-file emitted a .a"; exit 1; }

# ── Aggregate per-file outputs into a library archive ──────────────────────
# Use the ELF-wrapped .hm0 (not the raw .writ0) so ld.lld doesn't warn
# about non-ET_REL archive members at downstream link time.
ar rcs "$PROJ/per.a" \
    "$PROJ/perfile/a.o" "$PROJ/perfile/a.hm0" \
    "$PROJ/perfile/b.o" "$PROJ/perfile/b.hm0"

# ── Consumer that uses both fns; built against per-file archive ────────────
cat > "$PROJ/use.logos" <<'EOF'
package use_mylib;
use mylib;
fn main() -> i32 {
    return from_a() + from_b();
}
EOF

"$LOGOSC" "$PROJ/use.logos" -l "$PROJ/per.a" -o "$PROJ/use.o" >/dev/null 2>&1
cc "$PROJ/use.o" "$PROJ/per.a" "$LIB"/lib*.a -lpthread -lm -o "$PROJ/use" 2>/dev/null
"$PROJ/use" && rc=$? || rc=$?
[ "$rc" = "44" ] || { echo "FAIL: per-file build returned $rc, want 44 (100+200=300, low byte)"; exit 1; }
# Note: i32 returned as exit code is truncated to low byte; 300 mod 256 = 44.

# ── Same consumer linked against monolithic archive — same result ──────────
"$LOGOSC" "$PROJ/use.logos" -l "$PROJ/mono.a" -o "$PROJ/use.o" >/dev/null 2>&1
cc "$PROJ/use.o" "$PROJ/mono.a" "$LIB"/lib*.a -lpthread -lm -o "$PROJ/use_mono" 2>/dev/null
"$PROJ/use_mono" && rc_mono=$? || rc_mono=$?
[ "$rc_mono" = "$rc" ] || { echo "FAIL: per-file ($rc) and mono ($rc_mono) produce different results"; exit 1; }

# ── Bad --only-file: source not in the manifest ────────────────────────────
"$LOGOSC" --emit-module "$PROJ/lib.module" --only-file "$PROJ/nope.logos" \
    -o "$PROJ/perfile/x" >/tmp/lf_emit_file_err 2>&1 && rc=$? || rc=$?
[ "$rc" != "0" ] || { echo "FAIL: bogus --only-file should fail"; exit 1; }
grep -q "did not match" /tmp/lf_emit_file_err || {
    echo "FAIL: missing diagnostic for unmatched --only-file"
    cat /tmp/lf_emit_file_err
    exit 1
}

echo "OK"
