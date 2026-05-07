#!/usr/bin/env bash
# B1.1 multi-target smoke: lib + bin with deps, topo-sorted build, --release profile.
#
# Args: $1 = lforge, $2 = logosc, $3 = LOGOS_LIB_DIR.

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT

# Project layout:
#   lforge.hermes       — multi-target manifest
#   src/core/core.logos — library, exposes greet()
#   src/main.logos      — bin, calls greet()

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
fn main() -> i32 {
    return answer();
}
EOF

cd "$PROJ"

# ── debug build (default) ──────────────────────────────────────────────────
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/build.log" 2>&1 || {
    echo "FAIL: debug build failed"; cat "$PROJ/build.log"; exit 1;
}

# Lib target produced an archive, bin target produced an executable.
[ -f "$PROJ/.lforge/debug/out/libcore.a" ] || { echo "FAIL: libcore.a missing"; exit 1; }
[ -x "$PROJ/.lforge/debug/out/app" ]      || { echo "FAIL: app binary missing"; exit 1; }
[ -f "$PROJ/.lforge/debug/_gen/core.module" ] || { echo "FAIL: generated .module missing"; exit 1; }

# Run produces the expected exit code (42 from core::answer).
"$PROJ/.lforge/debug/out/app" && rc=$? || rc=$?
[ "$rc" = "42" ] || { echo "FAIL: app returned $rc, want 42"; exit 1; }

# ── lforge run picks the bin target automatically ──────────────────────────
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" run > /dev/null 2>&1 && rc=$? || rc=$?
[ "$rc" = "42" ] || { echo "FAIL: 'lforge run' returned $rc, want 42"; exit 1; }

# ── --release profile lives in a separate directory ────────────────────────
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build --release > /dev/null 2>&1 || {
    echo "FAIL: release build failed"; exit 1;
}
[ -f "$PROJ/.lforge/release/out/libcore.a" ] || { echo "FAIL: release libcore.a missing"; exit 1; }
[ -x "$PROJ/.lforge/release/out/app" ]      || { echo "FAIL: release app missing"; exit 1; }
# debug artifacts still around (separate profiles).
[ -x "$PROJ/.lforge/debug/out/app" ]        || { echo "FAIL: debug profile clobbered"; exit 1; }

# ── manifest validation: unknown dep ───────────────────────────────────────
cat > "$PROJ/lforge.hermes" <<'EOF'
{
    name: "demo", version: "0.1.0",
    targets: [
        { kind: "bin", name: "app", src: "src", entry: "main", deps: ["nope"] }
    ]
}
EOF
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/err.log" 2>&1 && rc=$? || rc=$?
[ "$rc" != "0" ] || { echo "FAIL: unknown-dep build should fail"; exit 1; }
grep -q "unknown dep" "$PROJ/err.log" || { echo "FAIL: missing 'unknown dep' diagnostic"; cat "$PROJ/err.log"; exit 1; }

# ── target selection: `lforge build <name>` builds only that target + deps.
cat > "$PROJ/lforge.hermes" <<'EOF'
{
    name:    "demo",
    version: "0.1.0",
    targets: [
        { kind: "lib", name: "core", src: "src/core" },
        { kind: "bin", name: "app",  src: "src", entry: "main", deps: ["core"] },
        { kind: "bin", name: "tool", src: "src", entry: "main", deps: ["core"] }
    ]
}
EOF
rm -rf "$PROJ/.lforge"
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build app > "$PROJ/sel.log" 2>&1
[ -x "$PROJ/.lforge/debug/out/app" ]   || { echo "FAIL: app not built when selected"; cat "$PROJ/sel.log"; exit 1; }
[ -f "$PROJ/.lforge/debug/out/libcore.a" ] || { echo "FAIL: core not built (transitive dep)"; exit 1; }
[ ! -e "$PROJ/.lforge/debug/out/tool" ] || { echo "FAIL: tool was built but only app was selected"; exit 1; }

# ── unknown target name → error ───────────────────────────────────────────
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build no-such-target > "$PROJ/err.log" 2>&1 && rc=$? || rc=$?
[ "$rc" != "0" ] || { echo "FAIL: unknown target should fail"; exit 1; }
grep -q "unknown target" "$PROJ/err.log" || { echo "FAIL: missing 'unknown target' diagnostic"; cat "$PROJ/err.log"; exit 1; }

# ── manifest validation: cycle ─────────────────────────────────────────────
cat > "$PROJ/lforge.hermes" <<'EOF'
{
    name: "demo", version: "0.1.0",
    targets: [
        { kind: "lib", name: "a", src: "src/core", deps: ["b"] },
        { kind: "lib", name: "b", src: "src/core", deps: ["a"] }
    ]
}
EOF
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/err.log" 2>&1 && rc=$? || rc=$?
[ "$rc" != "0" ] || { echo "FAIL: cycle build should fail"; exit 1; }
grep -q "dependency cycle" "$PROJ/err.log" || { echo "FAIL: missing cycle diagnostic"; cat "$PROJ/err.log"; exit 1; }

echo "OK"
