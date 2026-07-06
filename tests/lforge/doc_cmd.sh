#!/usr/bin/env bash
# ADR 0014 slice-3: `lforge doc` — build lib targets, --emit-docs, resolve to
# docs.json under .lforge/<profile>/doc/<name>.json.
#
# Args: $1 = lforge path, $2 = logosc path, $3 = LOGOS_LIB_DIR

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT

mkdir -p "$PROJ/src/core"

cat > "$PROJ/lforge.writ" <<'EOF'
{
    name:    "demo",
    version: "0.1.0",
    targets: [
        { kind: "lib", name: "core", src: "src/core" }
    ]
}
EOF

cat > "$PROJ/src/core/core.logos" <<'EOF'
package core;

/// The universal answer.
pub struct Answer { pub value: i32 }

/// Types that can be summed.
pub trait Summable { fn total(&self) -> i32; }

impl Summable for Answer {
    fn total(&self) -> i32 { return self.value; }
}

/// Return the answer.
pub fn answer() -> i32 { return 42; }

pub fn helper_undoc() -> i32 { return 0; }
EOF

cd "$PROJ"
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" doc > "$PROJ/doc.log" 2>&1 && rc=$? || rc=$?

if [ "$rc" != "0" ]; then
    echo "FAIL: 'lforge doc' exited $rc"
    cat "$PROJ/doc.log"
    exit 1
fi

JSON="$PROJ/.lforge/debug/doc/core.json"
[ -f "$JSON" ] || { echo "FAIL: $JSON not produced"; cat "$PROJ/doc.log"; exit 1; }

fail() { echo "FAIL: $1"; echo "── log ──"; cat "$PROJ/doc.log"; echo "── docs.json ──"; cat "$JSON"; exit 1; }

if command -v jq >/dev/null 2>&1; then
    jq . "$JSON" >/dev/null || fail "not valid JSON"
    [ "$(jq -r '.package' "$JSON")" = "core" ] || fail "wrong package"
    jq -e '.items[] | select(.name=="answer") | select(.doc=="Return the answer.")' "$JSON" >/dev/null \
        || fail "answer doc not captured"
    jq -e '.items[] | select(.kind=="trait" and .name=="Summable") | .implementors | index("Answer")' "$JSON" >/dev/null \
        || fail "Summable missing implementor Answer"
    jq -e '.items[] | select(.kind=="struct" and .name=="Answer") | .implements | index("Summable")' "$JSON" >/dev/null \
        || fail "Answer missing implements Summable"
    jq -e '.undocumented | index("core.helper_undoc")' "$JSON" >/dev/null \
        || fail "helper_undoc not in undocumented"
else
    grep -q '"package":"core"' "$JSON"          || fail "package missing"
    grep -q 'Return the answer.' "$JSON"        || fail "answer doc missing"
    grep -q '"implementors":\["Answer"\]' "$JSON" || fail "implementors missing"
    grep -q 'core.helper_undoc' "$JSON"         || fail "undocumented missing"
fi

echo "OK"
