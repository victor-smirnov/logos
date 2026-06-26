#!/usr/bin/env bash
# B3a: transitive external deps + lockfile.
#
# Layout:
#   leaf:  pure lib `prim`
#   mid:   lib `mid_lib` that pulls leaf via project-level dep
#   root:  bin that pulls mid via local path
#
# When root builds:
#   - mid's build sees leaf's archive (via lforge resolving leaf transitively)
#   - root's bin links the closure (root + mid + leaf)
#   - lforge.lock is written for any git deps in the closure (none here,
#     so the file may be empty or absent — test the local-path path)

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

ROOT=$(mktemp -d)
trap 'rm -rf "$ROOT"' EXIT

# ── Leaf: pure lib that exposes prim::base().
mkdir -p "$ROOT/leaf/src/prim"
cat > "$ROOT/leaf/lforge.writ" <<'EOF'
{
    name:    "leaf",
    version: "0.1.0",
    targets: [
        { kind: "lib", name: "prim", src: "src/prim" }
    ]
}
EOF
cat > "$ROOT/leaf/src/prim/prim.logos" <<'EOF'
package prim;
pub fn base() -> i32 { return 30; }
EOF

# ── Mid: declares leaf as a project-level dep.
mkdir -p "$ROOT/mid/src/mid_lib"
cat > "$ROOT/mid/lforge.writ" <<'EOF'
{
    name:    "mid",
    version: "0.1.0",
    deps: [
        { path: "../leaf", modules: ["prim"] }
    ],
    targets: [
        { kind: "lib", name: "mid_lib", src: "src/mid_lib",
          deps: ["prim"] }
    ]
}
EOF
cat > "$ROOT/mid/src/mid_lib/mid_lib.logos" <<'EOF'
package mid_lib;
use prim;
pub fn topup() -> i32 { return base() + 7; }   // 30 + 7 = 37
EOF

# ── Root: bin pulling mid only; gets leaf transitively.
mkdir -p "$ROOT/root/src"
cat > "$ROOT/root/lforge.writ" <<'EOF'
{
    name:    "myapp",
    version: "0.1.0",
    deps: [
        { path: "../mid", modules: ["mid_lib"] }
    ],
    targets: [
        { kind: "bin", name: "app", src: "src", entry: "main",
          deps: ["mid_lib"] }
    ]
}
EOF
cat > "$ROOT/root/src/main.logos" <<'EOF'
package myapp;
use mid_lib;
use prim;
fn main() -> i32 { return topup() + base(); }   // 37 + 30 = 67
EOF

cd "$ROOT/root"
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/b1.log" 2>&1 || {
    echo "FAIL: cold build"
    cat "$ROOT/b1.log"
    exit 1
}

# Both transitive and direct archives built.
[ -f "$ROOT/mid/.lforge/debug/out/libmid_lib.a" ] || { echo "FAIL: mid_lib not built"; exit 1; }
[ -f "$ROOT/leaf/.lforge/debug/out/libprim.a"   ] || { echo "FAIL: leaf prim not built"; exit 1; }

"$ROOT/root/.lforge/debug/out/app" && rc=$? || rc=$?
[ "$rc" = "67" ] || { echo "FAIL: app returned $rc, want 67"; exit 1; }

# ── Lockfile is a no-op for local-path-only closures (no git pins to record).
[ ! -s "$ROOT/root/lforge.lock" ] || {
    LOCK_CONTENTS=$(cat "$ROOT/root/lforge.lock")
    # If a lockfile was written, it must have empty `pinned`.
    echo "$LOCK_CONTENTS" | grep -q "project:" && {
        echo "FAIL: lockfile wrote pinned entries for local-path-only closure"
        cat "$ROOT/root/lforge.lock"
        exit 1
    } || true
}

echo "OK"
