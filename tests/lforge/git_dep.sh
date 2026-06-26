#!/usr/bin/env bash
# B2.5: project-level external deps via git URLs.
#
# Validates:
#   - manifest `deps: [{ project, tag, modules }]` parses
#   - lforge resolves the tag to a SHA via `git ls-remote`, clones into
#     ~/.cache/lforge/src/<safe-id>/<sha>/, and builds the listed libs
#   - second build is a cache hit (no re-clone)
#   - SHA-pinned form `{ project, sha, modules }` works equivalently
#   - manifest with both `path` and `project` is rejected

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

ROOT=$(mktemp -d)
# Use a private cache dir so we don't pollute the user's real cache.
CACHE_HOME=$(mktemp -d)
trap 'rm -rf "$ROOT" "$CACHE_HOME"' EXIT

# ── External "remote" project (a real git repo on the local fs).
mkdir -p "$ROOT/upstream/src/util"
cat > "$ROOT/upstream/lforge.writ" <<'EOF'
{
    name:    "upstream",
    version: "0.1.0",
    targets: [
        { kind: "lib", name: "util", src: "src/util" }
    ]
}
EOF
cat > "$ROOT/upstream/src/util/util.logos" <<'EOF'
package util;
pub fn answer() -> i32 { return 42; }
EOF

cd "$ROOT/upstream"
git init --quiet
git config user.email lforge-test@example.com
git config user.name  lforge-test
git add .
git commit --quiet -m "v0.1.0"
git tag v0.1.0
TAG_SHA=$(git rev-parse HEAD)

# ── Consumer pulls upstream by tag via file:// URL.
mkdir -p "$ROOT/app/src"
cat > "$ROOT/app/lforge.writ" <<EOF
{
    name:    "myapp",
    version: "0.1.0",
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
fn main() -> i32 { return answer(); }
EOF

# Cold build → clones into the test cache.
cd "$ROOT/app"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/b1.log" 2>&1 || {
    echo "FAIL: cold build"
    cat "$ROOT/b1.log"
    exit 1
}
grep -q "lforge: fetching" "$ROOT/b1.log" || {
    echo "FAIL: cold build did not fetch"
    cat "$ROOT/b1.log"
    exit 1
}
"$ROOT/app/.lforge/debug/out/app" && rc=$? || rc=$?
[ "$rc" = "42" ] || { echo "FAIL: tag-pinned app returned $rc, want 42"; exit 1; }

# Cache layout: ~/.cache/lforge/src/<dirname>/<sha>/lforge.writ
# dirname = display_to_dirname(canonical_url(...).0). For file:// it strips
# the scheme, so display = "$ROOT/upstream", and `/` → `_`.
DIRNAME=$(printf '%s' "$ROOT/upstream" | tr '/' '_')
[ -f "$CACHE_HOME/.cache/lforge/src/$DIRNAME/$TAG_SHA/lforge.writ" ] || {
    echo "FAIL: cached clone manifest not at $CACHE_HOME/.cache/lforge/src/$DIRNAME/$TAG_SHA/"
    ls -R "$CACHE_HOME/.cache/lforge/src/" || true
    exit 1
}

# Warm rebuild (clean .lforge but keep cache) → no re-clone.
rm -rf "$ROOT/app/.lforge"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/b2.log" 2>&1
grep -q "lforge: fetching" "$ROOT/b2.log" && {
    echo "FAIL: warm build re-cloned"
    cat "$ROOT/b2.log"
    exit 1
} || true

# ── SHA-pinned form
mkdir -p "$ROOT/app2/src"
cat > "$ROOT/app2/lforge.writ" <<EOF
{
    name:    "myapp2",
    version: "0.1.0",
    deps: [
        { project: "file://$ROOT/upstream", sha: "$TAG_SHA",
          modules: ["util"] }
    ],
    targets: [
        { kind: "bin", name: "app2", src: "src", entry: "main",
          deps: ["util"] }
    ]
}
EOF
cp "$ROOT/app/src/main.logos" "$ROOT/app2/src/main.logos"
sed -i 's/myapp/myapp2/' "$ROOT/app2/src/main.logos"
cd "$ROOT/app2"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/b3.log" 2>&1 || {
    echo "FAIL: SHA-pinned build"; cat "$ROOT/b3.log"; exit 1;
}
"$ROOT/app2/.lforge/debug/out/app2" && rc=$? || rc=$?
[ "$rc" = "42" ] || { echo "FAIL: SHA-pinned app returned $rc, want 42"; exit 1; }
# SHA-pinned MUST also be a cache hit (same SHA as tag).
grep -q "lforge: fetching" "$ROOT/b3.log" && {
    echo "FAIL: SHA-pinned re-cloned despite cache hit"
    cat "$ROOT/b3.log"; exit 1
} || true

# ── Negative: both 'path' and 'project' present.
mkdir -p "$ROOT/bad/src"
cat > "$ROOT/bad/lforge.writ" <<EOF
{
    name: "bad", version: "0.1.0",
    deps: [
        { path: "../upstream", project: "file://$ROOT/upstream", tag: "v0.1.0",
          modules: ["util"] }
    ],
    targets: [{ kind: "bin", name: "bad", src: "src", entry: "main" }]
}
EOF
cat > "$ROOT/bad/src/main.logos" <<'EOF'
package bad;
fn main() -> i32 { return 0; }
EOF
cd "$ROOT/bad"
HOME="$CACHE_HOME" LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/bad.log" 2>&1 && {
    echo "FAIL: build with both path+project unexpectedly succeeded"
    exit 1
} || true
grep -q "both 'path' and 'project'" "$ROOT/bad.log" || {
    echo "FAIL: missing 'pick one' diagnostic"
    cat "$ROOT/bad.log"
    exit 1
}

echo "OK"
