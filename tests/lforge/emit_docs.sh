#!/usr/bin/env bash
# ADR 0014 slice-1: `logosc --emit-module M --emit-docs` writes a <out>.docwr
# Writ-SDN container of documentation facts extracted from /// //! /** */ comments.
#
# Validates that:
#   - the .docwr sidecar is written next to the normal module artifacts
#   - struct / field / trait / free-fn doc text is captured verbatim
#   - a doc-less pub item appears with an empty doc string
#   - trait-impl edges are emitted (the `implementors` raw material for Deem)
#
# Args: $1 = logosc path, $2 = LOGOS_LIB_DIR

set -euo pipefail

LOGOSC="${1:?logosc path}"
LIB="${2:?LOGOS_LIB_DIR}"

PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT

mkdir -p "$PROJ/src"
cat > "$PROJ/lib.module" <<EOF
module docdemo
version 0.1
root $PROJ/src/
EOF

cat > "$PROJ/src/lib.logos" <<'EOF'
package docdemo;

/// A 2D point in the plane.
pub struct Point {
    /// The horizontal coordinate.
    pub x: i32,
    pub y: i32,
}

impl Point {
    /// Construct a point at the origin.
    pub fn origin() -> Point { return Point { x: 0, y: 0 }; }
    /// Manhattan norm.
    pub fn norm(&self) -> i32 { return self.x + self.y; }
}

/// Things that have an area.
pub trait Area {
    fn area(&self) -> i32;
}

impl Area for Point {
    fn area(&self) -> i32 { return self.x * self.y; }
}

/// Add two integers together.
pub fn add(a: i32, b: i32) -> i32 { return a + b; }

pub fn undocumented_fn() -> i32 { return 1; }
EOF

export LOGOS_LIB_DIR="$LIB"
"$LOGOSC" --emit-module "$PROJ/lib.module" --emit-docs -o "$PROJ/out" >/dev/null 2>&1

DOC="$PROJ/out.docwr"
[ -f "$DOC" ] || { echo "FAIL: $DOC not written"; exit 1; }

fail() { echo "FAIL: $1"; echo "── .docwr ──"; cat "$DOC"; exit 1; }

grep -q '"A 2D point in the plane."'   "$DOC" || fail "struct doc missing"
grep -q '"The horizontal coordinate."' "$DOC" || fail "field doc missing"
grep -q '"Things that have an area."'  "$DOC" || fail "trait doc missing"
grep -q '"Add two integers together."' "$DOC" || fail "free-fn doc missing"

# The struct/trait/fn items themselves.
grep -q 'path: "docdemo.Point"' "$DOC" || fail "struct item missing"
grep -q 'path: "docdemo.Area"'  "$DOC" || fail "trait item missing"
grep -q 'path: "docdemo.add"'   "$DOC" || fail "free-fn item missing"

# Doc-less pub fn present with an empty doc string.
grep -q 'name: "undocumented_fn".*doc: ""' "$DOC" || fail "undocumented fn not captured with empty doc"

# Methods (incl. self-less associated fns) attach to their owner type, not the
# package root.
grep -q 'kind: "method", path: "docdemo.Point::norm"'   "$DOC" || fail "&self method not attached to owner"
grep -q 'kind: "method", path: "docdemo.Point::origin"' "$DOC" || fail "associated fn not attached to owner"
grep -q 'kind: "method", path: "docdemo.Point::area"'   "$DOC" || fail "trait-impl method not attached to owner"
# Free functions stay at the package root.
grep -q 'kind: "fn", path: "docdemo.add"' "$DOC" || fail "free fn misclassified"

# Trait-impl edge (raw material for Deem `implementors`).
grep -q 'trait: "Area", type: "Point"' "$DOC" || fail "impl edge missing"

echo "OK"
