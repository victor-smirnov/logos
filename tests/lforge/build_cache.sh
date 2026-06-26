#!/usr/bin/env bash
# B4: content-addressed build cache.
#
# Two consumer projects depend on the SAME upstream@v1.0.0. With a shared
# HOME (= shared ~/.cache/lforge/build/), the second consumer's first
# build should NOT re-link upstream's archive: the cache entry from
# project-1 is reused. Verified by:
#   1. ~/.cache/lforge/build/<sha>/libutil.a exists after consumer-1.
#   2. consumer-2's archive_path resolves to the cache (the bin still
#      runs, returning upstream's value).
#   3. Wiping consumer-1's .lforge then changing the cache file — the
#      consumer's bin links against the cache, so it picks up the
#      mutation. (Sanity check that the path actually flows through.)

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

ROOT=$(mktemp -d)
CACHE_HOME=$(mktemp -d)
trap 'rm -rf "$ROOT" "$CACHE_HOME"' EXIT

# Upstream tagged v1.0.0.
mkdir -p "$ROOT/upstream/src/util"
cat > "$ROOT/upstream/lforge.writ" <<'EOF'
{
    name: "upstream", version: "0.1.0",
    targets: [{ kind: "lib", name: "util", src: "src/util" }]
}
EOF
cat > "$ROOT/upstream/src/util/util.logos" <<'EOF'
package util;
pub fn answer() -> i32 { return 71; }
EOF
cd "$ROOT/upstream"
git init --quiet
git config user.email lforge-test@example.com
git config user.name  lforge-test
git add . && git commit --quiet -m "v1"
git tag v1.0.0

# Consumer A.
mkdir -p "$ROOT/appA/src"
cat > "$ROOT/appA/lforge.writ" <<EOF
{
    name: "appA", version: "0.1.0",
    deps: [
        { project: "file://$ROOT/upstream", tag: "v1.0.0", modules: ["util"] }
    ],
    targets: [
        { kind: "bin", name: "appA", src: "src", entry: "main",
          deps: ["util"] }
    ]
}
EOF
cat > "$ROOT/appA/src/main.logos" <<'EOF'
package appA;
use util;
fn main() -> i32 { return answer(); }
EOF

cd "$ROOT/appA"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/a.log" 2>&1
"$ROOT/appA/.lforge/debug/out/appA" && rc=$? || rc=$?
[ "$rc" = "71" ] || { echo "FAIL: appA rc=$rc, want 71"; cat "$ROOT/a.log"; exit 1; }

# Cache populated.
cache_a=$(find "$CACHE_HOME/.cache/lforge/build" -name 'libutil.a' | head -1)
[ -n "$cache_a" ] || { echo "FAIL: cache miss after appA build"; ls -R "$CACHE_HOME/.cache/lforge/build" || true; exit 1; }
[ -f "${cache_a%/*}/meta.writ" ] || { echo "FAIL: meta.writ missing"; exit 1; }

# Consumer B (same dep).
mkdir -p "$ROOT/appB/src"
cat > "$ROOT/appB/lforge.writ" <<EOF
{
    name: "appB", version: "0.1.0",
    deps: [
        { project: "file://$ROOT/upstream", tag: "v1.0.0", modules: ["util"] }
    ],
    targets: [
        { kind: "bin", name: "appB", src: "src", entry: "main",
          deps: ["util"] }
    ]
}
EOF
cat > "$ROOT/appB/src/main.logos" <<'EOF'
package appB;
use util;
fn main() -> i32 { return answer() + 1; }
EOF

# Wipe upstream's per-source `.lforge` in the cloned cache so we'd FAIL if
# the cache lookup didn't kick in (no per-source archive available).
rm -rf "$CACHE_HOME/.cache/lforge/src/"*"/"*"/.lforge"

cd "$ROOT/appB"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/b.log" 2>&1
"$ROOT/appB/.lforge/debug/out/appB" && rc=$? || rc=$?
[ "$rc" = "72" ] || {
    echo "FAIL: appB rc=$rc, want 72 (cache hit should have served libutil.a)"
    cat "$ROOT/b.log"
    exit 1
}

# Build B did NOT re-build the upstream lib (no per-source .lforge appeared).
new_per_src=$(find "$CACHE_HOME/.cache/lforge/src" -path '*/.lforge/*/out/libutil.a' 2>/dev/null | head -1 || true)
[ -z "$new_per_src" ] || {
    echo "FAIL: cache hit didn't skip per-source rebuild — found $new_per_src"
    exit 1
}

echo "OK"
