#!/usr/bin/env bash
# B3: lockfile records git pins, second build re-uses them without ls-remote.
#
# Strategy: build once with a git-pinned dep (file:// upstream, real git
# repo); confirm `lforge.lock` is written with the SHA. Then move the
# upstream's HEAD forward and add a new tag — the second build should
# still pick the OLD locked SHA (no ls-remote happened) because the
# manifest's tag still matches what's in the lockfile.

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

ROOT=$(mktemp -d)
CACHE_HOME=$(mktemp -d)
trap 'rm -rf "$ROOT" "$CACHE_HOME"' EXIT

# Upstream: real git repo with one tagged release.
mkdir -p "$ROOT/upstream/src/util"
cat > "$ROOT/upstream/lforge.writ" <<'EOF'
{
    name: "upstream", version: "0.1.0",
    targets: [{ kind: "lib", name: "util", src: "src/util" }]
}
EOF
cat > "$ROOT/upstream/src/util/util.logos" <<'EOF'
package util;
pub fn answer() -> i32 { return 41; }
EOF
cd "$ROOT/upstream"
git init --quiet
git config user.email lforge-test@example.com
git config user.name  lforge-test
git add . && git commit --quiet -m "v0.1.0"
git tag v0.1.0
PINNED_SHA=$(git rev-parse HEAD)

# Consumer pulls upstream by tag.
mkdir -p "$ROOT/app/src"
cat > "$ROOT/app/lforge.writ" <<EOF
{
    name: "myapp", version: "0.1.0",
    deps: [
        { project: "file://$ROOT/upstream", tag: "v0.1.0",
          modules: ["util"] }
    ],
    targets: [
        { kind: "bin", name: "app", src: "src", entry: "main",
          deps: ["util"] }
    ]
}
EOF
cat > "$ROOT/app/src/main.logos" <<'EOF'
package myapp;
use util;
fn main() -> i32 { return answer() + 1; }
EOF

cd "$ROOT/app"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/b1.log" 2>&1 || {
    echo "FAIL: cold build"; cat "$ROOT/b1.log"; exit 1;
}
"$ROOT/app/.lforge/debug/out/app" && rc=$? || rc=$?
[ "$rc" = "42" ] || { echo "FAIL: cold rc=$rc, want 42"; exit 1; }

# Lockfile written with the pinned SHA.
[ -f "$ROOT/app/lforge.lock" ] || { echo "FAIL: no lockfile written"; exit 1; }
grep -q "$PINNED_SHA" "$ROOT/app/lforge.lock" || {
    echo "FAIL: lockfile missing the pinned SHA"
    cat "$ROOT/app/lforge.lock"
    exit 1
}
grep -q '"v0.1.0"' "$ROOT/app/lforge.lock" || {
    echo "FAIL: lockfile missing the tag for diff readability"
    exit 1
}

# Move upstream forward and add a NEW tag. Manifest still says v0.1.0,
# so lockfile's pinned SHA must keep winning.
cd "$ROOT/upstream"
sed -i 's/41/80/' src/util/util.logos
git commit --quiet -am "bump"
git tag v0.1.0-newer  # different tag, doesn't affect what we asked for

# Wipe .lforge but keep cache + lockfile.
rm -rf "$ROOT/app/.lforge"
cd "$ROOT/app"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/b2.log" 2>&1 || {
    echo "FAIL: warm build"; cat "$ROOT/b2.log"; exit 1;
}
# Lockfile pin held — still got 41+1.
"$ROOT/app/.lforge/debug/out/app" && rc=$? || rc=$?
[ "$rc" = "42" ] || {
    echo "FAIL: warm rc=$rc, want 42 (lockfile pin should have held; got the bumped value)"
    exit 1
}
# No fetch happened (cache + lockfile combo).
grep -q "lforge: fetching" "$ROOT/b2.log" && {
    echo "FAIL: warm build re-fetched despite lockfile + cache"
    cat "$ROOT/b2.log"; exit 1
} || true

# Manifest dep change → re-resolve.
sed -i 's/v0.1.0/v0.1.0-newer/' "$ROOT/app/lforge.writ"
rm -rf "$ROOT/app/.lforge"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/b3.log" 2>&1 || {
    echo "FAIL: re-resolve build"; cat "$ROOT/b3.log"; exit 1;
}
"$ROOT/app/.lforge/debug/out/app" && rc=$? || rc=$?
[ "$rc" = "81" ] || {
    echo "FAIL: re-resolve rc=$rc, want 81 (80+1, post-bump)"
    exit 1
}

echo "OK"
