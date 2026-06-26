#!/usr/bin/env bash
# B1.3: lforge fans out per-file lib compilation in parallel.
#
# Validates that:
#   - a multi-file lib target produces one .o + .writ0 per source file
#     under .lforge/<profile>/_files/<lib>/
#   - the final lib<name>.a contains all per-file members
#   - editing one file rebuilds only that file's per-file artifacts
#   - editing nothing rebuilds nothing (incremental)
#   - a consumer linking against the per-file-aggregated archive works

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT

mkdir -p "$PROJ/src/core" "$PROJ/src"

# A 4-file lib + a bin that exercises all of them.
cat > "$PROJ/lforge.writ" <<'EOF'
{
    name:    "demo",
    version: "0.1.0",
    targets: [
        { kind: "lib", name: "core", src: "src/core" },
        { kind: "bin", name: "app",  src: "src", entry: "main", deps: ["core"] }
    ]
}
EOF

cat > "$PROJ/src/core/a.logos" <<'EOF'
package core;
pub fn fa() -> i32 { return 1; }
EOF
cat > "$PROJ/src/core/b.logos" <<'EOF'
package core;
pub fn fb() -> i32 { return 2; }
EOF
cat > "$PROJ/src/core/c.logos" <<'EOF'
package core;
pub fn fc() -> i32 { return 4; }
EOF
cat > "$PROJ/src/core/d.logos" <<'EOF'
package core;
pub fn fd() -> i32 { return 8; }
EOF
cat > "$PROJ/src/main.logos" <<'EOF'
package app;
use core;
fn main() -> i32 { return fa() + fb() + fc() + fd(); }
EOF

cd "$PROJ"

# 1. Cold build.
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/b1.log" 2>&1
grep -q "compiling 4 file(s) in parallel" "$PROJ/b1.log" || {
    echo "FAIL: cold build did not announce 4 files in parallel"
    cat "$PROJ/b1.log"; exit 1;
}

# Per-file artifacts present.
for stem in a b c d; do
    [ -f "$PROJ/.lforge/debug/_files/core/$stem.o"       ] || { echo "FAIL: $stem.o missing"; exit 1; }
    [ -f "$PROJ/.lforge/debug/_files/core/$stem.writ0" ] || { echo "FAIL: $stem.writ0 missing"; exit 1; }
    [ -f "$PROJ/.lforge/debug/_files/core/$stem.hm0"     ] || { echo "FAIL: $stem.hm0 missing"; exit 1; }
done

# Final archive exists, contains every per-file .o and .writ0.
[ -f "$PROJ/.lforge/debug/out/libcore.a" ] || { echo "FAIL: libcore.a missing"; exit 1; }
ar_list=$(ar t "$PROJ/.lforge/debug/out/libcore.a")
for stem in a b c d; do
    echo "$ar_list" | grep -q "^$stem.o$"       || { echo "FAIL: archive missing $stem.o"; exit 1; }
    echo "$ar_list" | grep -q "^$stem.hm0$"     || { echo "FAIL: archive missing $stem.hm0"; exit 1; }
done

# Consumer behaves correctly: 1+2+4+8 = 15.
"$PROJ/.lforge/debug/out/app" && rc=$? || rc=$?
[ "$rc" = "15" ] || { echo "FAIL: app returned $rc, want 15"; exit 1; }

# 2. No-op rebuild — nothing should be re-compiled, lib stays cached.
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/b2.log" 2>&1
grep -q "compiling" "$PROJ/b2.log" && { echo "FAIL: no-op rebuild compiled something"; cat "$PROJ/b2.log"; exit 1; } || true
grep -q "lib core up-to-date" "$PROJ/b2.log" || {
    echo "FAIL: no-op rebuild did not report up-to-date"
    cat "$PROJ/b2.log"; exit 1;
}

# 3. Touch one file → only that file's per-file artifacts get rebuilt.
# Sleep > 1s so mtime second-granularity from `stat -c%Y` actually differs
# between the original .o and the rebuild.
sleep 1.1
# Capture per-file mtimes before.
before_a=$(stat -c%Y "$PROJ/.lforge/debug/_files/core/a.o")
before_c=$(stat -c%Y "$PROJ/.lforge/debug/_files/core/c.o")
touch "$PROJ/src/core/c.logos"

LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/b3.log" 2>&1
grep -q "compiling 1 file(s) in parallel" "$PROJ/b3.log" || {
    echo "FAIL: single-file edit did not result in 1-file rebuild"
    cat "$PROJ/b3.log"; exit 1;
}

# Per-file mtimes — only c.o should have moved.
after_a=$(stat -c%Y "$PROJ/.lforge/debug/_files/core/a.o")
after_c=$(stat -c%Y "$PROJ/.lforge/debug/_files/core/c.o")
[ "$before_a" = "$after_a" ] || { echo "FAIL: a.o was rebuilt unnecessarily"; exit 1; }
[ "$before_c" != "$after_c" ] || { echo "FAIL: c.o was not rebuilt after touching c.logos"; exit 1; }

# Result still works.
"$PROJ/.lforge/debug/out/app" && rc=$? || rc=$?
[ "$rc" = "15" ] || { echo "FAIL: app returned $rc after rebuild, want 15"; exit 1; }

echo "OK"
