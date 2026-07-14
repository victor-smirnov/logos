#!/usr/bin/env bash
# ADR 0014 slice-2: the .docwr → docs.json converter (tools/docgen/docgen.logos).
#
# End-to-end: `logosc --emit-docs` produces a doc-facts container; docgen folds
# the trait-impl edges into per-item implementors/implements cross-refs, collects
# the undocumented public surface, and emits a resolved docs.json (schema_version 1).
#
# Validates that:
#   - docgen builds and runs
#   - the output is well-formed JSON (when jq is present)
#   - a trait carries its implementors and the type carries `implements`
#   - the undocumented list names exactly the doc-less pub items
#
# Args: $1 = logosc path, $2 = LOGOS_LIB_DIR

set -euo pipefail

LOGOSC="${1:?logosc path}"
LIB="${2:?LOGOS_LIB_DIR}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DOCGEN_SRC="$HERE/tools/docgen/docgen.logos"

PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT
export LOGOS_LIB_DIR="$LIB"

# ── Build the docgen tool ──────────────────────────────────────────────────
"$LOGOSC" "$DOCGEN_SRC" -o "$PROJ/docgen.o" >/dev/null 2>&1
# --start-group: the layer archives have inter-archive references (std's
# reactor/thread → lcm's fiber core) that a single left-to-right pass cannot
# resolve; group them like run_test.sh and the docgen CMake link do.
cc "$PROJ/docgen.o" -Wl,--start-group "$LIB"/lib*.a -Wl,--end-group -lpthread -lm -o "$PROJ/docgen" 2>/dev/null

# ── Fixture package → .docwr ───────────────────────────────────────────────
mkdir -p "$PROJ/src"
cat > "$PROJ/lib.module" <<EOF
module docdemo
version 0.1
root $PROJ/src/
EOF
cat > "$PROJ/src/lib.logos" <<'EOF'
package docdemo;

/// A 2D point in the plane.
pub struct Point { pub x: i32, pub y: i32 }

/// Things that have an area.
pub trait Area { fn area(&self) -> i32; }

impl Area for Point {
    fn area(&self) -> i32 { return self.x * self.y; }
}

/// Add two integers.
pub fn add(a: i32, b: i32) -> i32 { return a + b; }

pub fn undocumented_fn() -> i32 { return 1; }
EOF

"$LOGOSC" --emit-module "$PROJ/lib.module" --emit-docs -o "$PROJ/out" >/dev/null 2>&1
[ -f "$PROJ/out.docwr" ] || { echo "FAIL: .docwr not produced"; exit 1; }

# ── Convert ────────────────────────────────────────────────────────────────
"$PROJ/docgen" "$PROJ/out.docwr" "$PROJ/docs.json" || { echo "FAIL: docgen exit $?"; exit 1; }
JSON="$PROJ/docs.json"
[ -f "$JSON" ] || { echo "FAIL: docs.json not written"; exit 1; }

fail() { echo "FAIL: $1"; echo "── docs.json ──"; cat "$JSON"; exit 1; }

# Well-formed JSON (best-effort; skipped if jq absent).
if command -v jq >/dev/null 2>&1; then
    jq . "$JSON" >/dev/null || fail "not valid JSON"
    [ "$(jq -r '.schema_version' "$JSON")" = "1" ] || fail "schema_version != 1"
    [ "$(jq -r '.package' "$JSON")" = "docdemo" ] || fail "wrong package"
    # Trait implementors / type implements.
    jq -e '.items[] | select(.kind=="trait" and .name=="Area") | .implementors | index("Point")' "$JSON" >/dev/null \
        || fail "Area missing implementor Point"
    jq -e '.items[] | select(.kind=="struct" and .name=="Point") | .implements | index("Area")' "$JSON" >/dev/null \
        || fail "Point missing implements Area"
    # Undocumented list contains the doc-less pub fn but NOT the documented one.
    jq -e '.undocumented | index("docdemo.undocumented_fn")' "$JSON" >/dev/null \
        || fail "undocumented list missing undocumented_fn"
    jq -e '.undocumented | index("docdemo.add") | not' "$JSON" >/dev/null \
        || fail "documented add wrongly listed as undocumented"
else
    grep -q '"schema_version":1' "$JSON" || fail "schema_version missing"
    grep -q '"implementors":\["Point"\]' "$JSON" || fail "implementors missing"
    grep -q '"implements":\["Area"\]' "$JSON" || fail "implements missing"
    grep -q 'docdemo.undocumented_fn' "$JSON" || fail "undocumented missing"
fi

echo "OK"
