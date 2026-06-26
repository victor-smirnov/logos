#!/usr/bin/env bash
# B5: `replace:` directive + `requires_logos:` ABI floor.
#
# Three checks:
#   1. replace: redirects a git project to a local path. The original
#      git URL never gets resolved (no ls-remote happens), proving the
#      replace took effect; build returns the LOCAL fork's value.
#   2. requires_logos higher than logosc's actual version → error.
#   3. requires_logos lower than logosc's actual version → pass.

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

ROOT=$(mktemp -d)
CACHE_HOME=$(mktemp -d)
trap 'rm -rf "$ROOT" "$CACHE_HOME"' EXIT

# ── upstream is a real git repo at v1.0.0 returning 10
mkdir -p "$ROOT/upstream/src/util"
cat > "$ROOT/upstream/lforge.writ" <<'EOF'
{
    name: "upstream", version: "0.1.0",
    targets: [{ kind: "lib", name: "util", src: "src/util" }]
}
EOF
cat > "$ROOT/upstream/src/util/util.logos" <<'EOF'
package util;
pub fn answer() -> i32 { return 10; }
EOF
cd "$ROOT/upstream"
git init --quiet
git config user.email lforge-test@example.com
git config user.name  lforge-test
git add . && git commit --quiet -m "v1"
git tag v1.0.0

# ── local fork (NOT a git repo, just a directory) returning 99
mkdir -p "$ROOT/forked/src/util"
cat > "$ROOT/forked/lforge.writ" <<'EOF'
{
    name: "upstream", version: "0.1.0-fork",
    targets: [{ kind: "lib", name: "util", src: "src/util" }]
}
EOF
cat > "$ROOT/forked/src/util/util.logos" <<'EOF'
package util;
pub fn answer() -> i32 { return 99; }
EOF

# ── consumer that depends on upstream@v1.0.0 BUT replaces it with the fork.
mkdir -p "$ROOT/app/src"
# Use a fake URL nothing can resolve — proves replace short-circuits the fetch.
cat > "$ROOT/app/lforge.writ" <<EOF
{
    name: "app", version: "0.1.0",
    deps: [
        { project: "https://nonexistent.invalid/foo/bar", tag: "v1.0.0",
          modules: ["util"] }
    ],
    replace: [
        { project: "https://nonexistent.invalid/foo/bar",
          path:    "$ROOT/forked" }
    ],
    targets: [
        { kind: "bin", name: "app", src: "src", entry: "main",
          deps: ["util"] }
    ]
}
EOF
cat > "$ROOT/app/src/main.logos" <<'EOF'
package app;
use util;
fn main() -> i32 { return answer(); }
EOF

cd "$ROOT/app"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/r.log" 2>&1 || {
    echo "FAIL: build with replace"
    cat "$ROOT/r.log"
    exit 1
}
"$ROOT/app/.lforge/debug/out/app" && rc=$? || rc=$?
[ "$rc" = "99" ] || {
    echo "FAIL: replace not applied — got $rc, want 99"
    cat "$ROOT/r.log"
    exit 1
}
grep -q "lforge: replace" "$ROOT/r.log" || {
    echo "FAIL: missing replace announcement"
    cat "$ROOT/r.log"; exit 1
}
grep -q "lforge: fetching" "$ROOT/r.log" && {
    echo "FAIL: replace did NOT short-circuit fetch — replace short-circuit failed"
    cat "$ROOT/r.log"; exit 1
} || true

# ── requires_logos: floor too HIGH → error
mkdir -p "$ROOT/floor_high/src"
cat > "$ROOT/floor_high/lforge.writ" <<'EOF'
{
    name: "fh", version: "0.1.0",
    requires_logos: "999.0",
    targets: [
        { kind: "bin", name: "fh", src: "src", entry: "main" }
    ]
}
EOF
cat > "$ROOT/floor_high/src/main.logos" <<'EOF'
package fh;
fn main() -> i32 { return 0; }
EOF
cd "$ROOT/floor_high"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/fh.log" 2>&1 && {
    echo "FAIL: high-floor manifest unexpectedly built"
    cat "$ROOT/fh.log"; exit 1
} || true
grep -q "requires logosc >=" "$ROOT/fh.log" || {
    echo "FAIL: missing floor diagnostic"
    cat "$ROOT/fh.log"; exit 1
}

# ── requires_logos: floor LOW (0.0.1) → pass
mkdir -p "$ROOT/floor_low/src"
cat > "$ROOT/floor_low/lforge.writ" <<'EOF'
{
    name: "fl", version: "0.1.0",
    requires_logos: "0.0.1",
    targets: [
        { kind: "bin", name: "fl", src: "src", entry: "main" }
    ]
}
EOF
cat > "$ROOT/floor_low/src/main.logos" <<'EOF'
package fl;
fn main() -> i32 { return 7; }
EOF
cd "$ROOT/floor_low"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/fl.log" 2>&1 || {
    echo "FAIL: low-floor manifest failed"
    cat "$ROOT/fl.log"; exit 1
}
"$ROOT/floor_low/.lforge/debug/out/fl" && rc=$? || rc=$?
[ "$rc" = "7" ] || {
    echo "FAIL: low-floor bin rc=$rc, want 7"
    exit 1
}

echo "OK"
