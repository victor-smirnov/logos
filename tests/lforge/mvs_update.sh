#!/usr/bin/env bash
# B3b: MVS conflict detection + `lforge update` re-resolves.
#
# Two cases:
#   1. Same project requested at different versions in closure (root pins v1.0,
#      transitive consumer pins v0.9). Walk order matters: we use post-order
#      (deepest first), so v0.9 is chosen first; encountering v1.0 later is
#      an UPGRADE — v1 errors out.
#   2. Update flow: build with v1.0, then bump the manifest tag, run
#      `lforge update` — lockfile gets the new SHA without needing to
#      delete .lforge first.

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

ROOT=$(mktemp -d)
CACHE_HOME=$(mktemp -d)
trap 'rm -rf "$ROOT" "$CACHE_HOME"' EXIT

# Create upstream with two tagged versions.
mkdir -p "$ROOT/upstream/src/util"
cat > "$ROOT/upstream/lforge.writ" <<'EOF'
{
    name: "upstream", version: "0.1.0",
    targets: [{ kind: "lib", name: "util", src: "src/util" }]
}
EOF
cat > "$ROOT/upstream/src/util/util.logos" <<'EOF'
package util;
pub fn answer() -> i32 { return 50; }
EOF
cd "$ROOT/upstream"
git init --quiet
git config user.email lforge-test@example.com
git config user.name  lforge-test
git add . && git commit --quiet -m "v1.0.0"
git tag v1.0.0
SHA_V1=$(git rev-parse HEAD)
sed -i 's/50/60/' src/util/util.logos
git commit --quiet -am "v1.1.0"
git tag v1.1.0
SHA_V11=$(git rev-parse HEAD)

# ── Case 2 first (simpler): cmd_update on a shifting manifest.
mkdir -p "$ROOT/app1/src"
cat > "$ROOT/app1/lforge.writ" <<EOF
{
    name: "app1", version: "0.1.0",
    deps: [
        { project: "file://$ROOT/upstream", tag: "v1.0.0",
          modules: ["util"] }
    ],
    targets: [
        { kind: "bin", name: "app1", src: "src", entry: "main",
          deps: ["util"] }
    ]
}
EOF
cat > "$ROOT/app1/src/main.logos" <<'EOF'
package app1;
use util;
fn main() -> i32 { return answer(); }
EOF
cd "$ROOT/app1"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/u1.log" 2>&1
"$ROOT/app1/.lforge/debug/out/app1" && rc=$? || rc=$?
[ "$rc" = "50" ] || { echo "FAIL: case2 v1 rc=$rc, want 50"; exit 1; }
grep -q "$SHA_V1" "$ROOT/app1/lforge.lock" || { echo "FAIL: lock missing v1.0.0 SHA"; exit 1; }

# Now bump tag in manifest, run `lforge update` (lockfile change WITHOUT
# needing to wipe .lforge).
sed -i 's/v1.0.0/v1.1.0/' "$ROOT/app1/lforge.writ"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" update > "$ROOT/u2.log" 2>&1 || {
    echo "FAIL: lforge update"; cat "$ROOT/u2.log"; exit 1;
}
"$ROOT/app1/.lforge/debug/out/app1" && rc=$? || rc=$?
[ "$rc" = "60" ] || { echo "FAIL: post-update rc=$rc, want 60"; exit 1; }
grep -q "$SHA_V11" "$ROOT/app1/lforge.lock" || { echo "FAIL: lock missing v1.1.0 SHA after update"; exit 1; }
grep -q "$SHA_V1" "$ROOT/app1/lforge.lock"  && {
    echo "FAIL: lock still pins old v1.0.0 SHA after update"
    cat "$ROOT/app1/lforge.lock"
    exit 1
} || true

# ── Case 1: MVS conflict.
# mid pins upstream v1.0.0, root pins upstream v1.1.0 directly.
# Walk order: root's deps in order. First we descend into mid (its dep is
# upstream v1.0.0 -> winners[upstream]=v1.0.0), then come back to root's
# direct dep upstream v1.1.0 -> upgrade required -> error.
mkdir -p "$ROOT/mid/src/mid_lib"
cat > "$ROOT/mid/lforge.writ" <<EOF
{
    name: "mid", version: "0.1.0",
    deps: [
        { project: "file://$ROOT/upstream", tag: "v1.0.0",
          modules: ["util"] }
    ],
    targets: [
        { kind: "lib", name: "mid_lib", src: "src/mid_lib",
          deps: ["util"] }
    ]
}
EOF
cat > "$ROOT/mid/src/mid_lib/mid_lib.logos" <<'EOF'
package mid_lib;
use util;
pub fn topup() -> i32 { return answer() + 1; }
EOF

mkdir -p "$ROOT/app2/src"
cat > "$ROOT/app2/lforge.writ" <<EOF
{
    name: "app2", version: "0.1.0",
    deps: [
        { path: "../mid", modules: ["mid_lib"] },
        { project: "file://$ROOT/upstream", tag: "v1.1.0",
          modules: ["util"] }
    ],
    targets: [
        { kind: "bin", name: "app2", src: "src", entry: "main",
          deps: ["mid_lib", "util"] }
    ]
}
EOF
cat > "$ROOT/app2/src/main.logos" <<'EOF'
package app2;
use mid_lib;
fn main() -> i32 { return topup(); }
EOF
cd "$ROOT/app2"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/conf.log" 2>&1 && {
    echo "FAIL: MVS conflict build unexpectedly succeeded"
    exit 1
} || true
grep -q "lower-version pin is already" "$ROOT/conf.log" || {
    echo "FAIL: missing MVS conflict diagnostic"
    cat "$ROOT/conf.log"
    exit 1
}

echo "OK"
