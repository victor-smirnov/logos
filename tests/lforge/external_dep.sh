#!/usr/bin/env bash
# B2: project-level external deps via local paths.
#
# Validates:
#   - manifest top-level `deps: [{ path, modules }]` parses
#   - lforge chdirs into the external project, builds the listed lib
#     targets, and exposes their archives by name to the consumer's
#     per-target deps
#   - bin target in consumer can `use` external module + link against it
#   - non-listed external modules are NOT built (only `modules: [...]`)

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

ROOT=$(mktemp -d)
trap 'rm -rf "$ROOT"' EXIT

# ── External project: provides `util` lib and an unrelated `extra` lib.
mkdir -p "$ROOT/extlib/src/util" "$ROOT/extlib/src/extra"
cat > "$ROOT/extlib/lforge.hermes" <<'EOF'
{
    name:    "extlib",
    version: "0.1.0",
    targets: [
        { kind: "lib", name: "util",  src: "src/util" },
        { kind: "lib", name: "extra", src: "src/extra" }
    ]
}
EOF

cat > "$ROOT/extlib/src/util/util.logos" <<'EOF'
package util;
pub fn answer() -> i32 { return 42; }
EOF

cat > "$ROOT/extlib/src/extra/extra.logos" <<'EOF'
package extra;
pub fn pi_int() -> i32 { return 3; }
EOF

# ── Consumer project: pulls only `util`, not `extra`.
mkdir -p "$ROOT/app/src"
cat > "$ROOT/app/lforge.hermes" <<'EOF'
{
    name:    "myapp",
    version: "0.1.0",
    deps: [
        { path: "../extlib", modules: ["util"] }
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

# ── Build app (which transitively builds extlib's util).
cd "$ROOT/app"
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/build.log" 2>&1 || {
    echo "FAIL: lforge build"
    cat "$ROOT/build.log"
    exit 1
}

# Util got built in the external project's tree.
[ -f "$ROOT/extlib/.lforge/debug/out/libutil.a" ] || {
    echo "FAIL: util.a not built in extlib's tree"
    cat "$ROOT/build.log"
    exit 1
}

# Extra was NOT built — it wasn't listed in the consumer's `modules`.
[ ! -f "$ROOT/extlib/.lforge/debug/out/libextra.a" ] || {
    echo "FAIL: extra.a was built despite not being listed in consumer deps"
    exit 1
}

# Consumer bin built and runs; util.answer() = 42.
"$ROOT/app/.lforge/debug/out/app" && rc=$? || rc=$?
[ "$rc" = "42" ] || { echo "FAIL: app returned $rc, want 42"; exit 1; }

# ── Negative: a non-existent module name on a real path is rejected.
mkdir -p "$ROOT/app2/src"
cat > "$ROOT/app2/lforge.hermes" <<EOF
{
    name:    "app2",
    version: "0.1.0",
    deps: [
        { path: "../extlib", modules: ["nonexistent"] }
    ],
    targets: [
        { kind: "bin", name: "app2", src: "src", entry: "main" }
    ]
}
EOF
cat > "$ROOT/app2/src/main.logos" <<'EOF'
package app2;
fn main() -> i32 { return 0; }
EOF
cd "$ROOT/app2"
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$ROOT/build2.log" 2>&1 && {
    echo "FAIL: build with bogus module name unexpectedly succeeded"
    exit 1
} || true
grep -q "no lib target named nonexistent" "$ROOT/build2.log" || {
    echo "FAIL: missing-module diagnostic not produced"
    cat "$ROOT/build2.log"
    exit 1
}

echo "OK"
