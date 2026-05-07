#!/usr/bin/env bash
# B1.2: incremental rebuild — second `lforge build` skips up-to-date targets.

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT

mkdir -p "$PROJ/src/core" "$PROJ/src"
cat > "$PROJ/lforge.hermes" <<'EOF'
{
    name:    "demo",
    version: "0.1.0",
    targets: [
        { kind: "lib", name: "core", src: "src/core" },
        { kind: "bin", name: "app",  src: "src", entry: "main", deps: ["core"] }
    ]
}
EOF
cat > "$PROJ/src/core/core.logos" <<'EOF'
package core;
pub fn answer() -> i32 { return 42; }
EOF
cat > "$PROJ/src/main.logos" <<'EOF'
package app;
use core;
fn main() -> i32 { return answer(); }
EOF

cd "$PROJ"

# 1. First build — both targets compile.
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/build1.log" 2>&1
grep -q "build lib core"  "$PROJ/build1.log" || { echo "FAIL: first build skipped lib"; cat "$PROJ/build1.log"; exit 1; }
grep -q "build bin app"   "$PROJ/build1.log" || { echo "FAIL: first build skipped bin"; cat "$PROJ/build1.log"; exit 1; }

# 2. Second build with no changes — both targets up-to-date.
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/build2.log" 2>&1
grep -q "lib core up-to-date" "$PROJ/build2.log" || { echo "FAIL: lib not skipped"; cat "$PROJ/build2.log"; exit 1; }
grep -q "bin app up-to-date" "$PROJ/build2.log" || { echo "FAIL: bin not skipped"; cat "$PROJ/build2.log"; exit 1; }
# logosc and cc were not invoked in this build:
grep -q "logosc: wrote" "$PROJ/build2.log" && { echo "FAIL: logosc ran on incremental build"; cat "$PROJ/build2.log"; exit 1; } || true

# 3. Touching a lib source forces lib + bin to rebuild (bin depends on lib's .a).
sleep 0.05
echo "// touch" >> "$PROJ/src/core/core.logos"
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/build3.log" 2>&1
grep -q "build lib core"  "$PROJ/build3.log" || { echo "FAIL: lib not rebuilt after edit"; cat "$PROJ/build3.log"; exit 1; }
grep -q "build bin app"   "$PROJ/build3.log" || { echo "FAIL: bin not rebuilt after dep edit"; cat "$PROJ/build3.log"; exit 1; }

# 4. Touching only the bin source rebuilds bin only (lib stays cached).
sleep 0.05
echo "// touch bin" >> "$PROJ/src/main.logos"
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/build4.log" 2>&1
grep -q "lib core up-to-date" "$PROJ/build4.log" || { echo "FAIL: lib should be cached on bin-only edit"; cat "$PROJ/build4.log"; exit 1; }
grep -q "build bin app"        "$PROJ/build4.log" || { echo "FAIL: bin not rebuilt"; cat "$PROJ/build4.log"; exit 1; }

# 5. Result still works.
"$PROJ/.lforge/debug/out/app" && rc=$? || rc=$?
[ "$rc" = "42" ] || { echo "FAIL: app returned $rc, want 42"; exit 1; }

echo "OK"
