#!/usr/bin/env bash
# B1.6 smoke for `lforge install --prefix <path>`.

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT

mkdir -p "$PROJ/src/core" "$PROJ/src" "$PROJ/prefix"
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

# install --prefix <path>
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" install "$PROJ/prefix" > "$PROJ/install.log" 2>&1 || {
    echo "FAIL: install"; cat "$PROJ/install.log"; exit 1;
}

[ -x "$PROJ/prefix/bin/app" ]      || { echo "FAIL: bin not installed"; ls -la "$PROJ/prefix/bin/" 2>&1; exit 1; }
[ -f "$PROJ/prefix/lib/libcore.a" ] || { echo "FAIL: lib not installed"; ls -la "$PROJ/prefix/lib/" 2>&1; exit 1; }

# Permissions: bin executable, lib not.
[ -x "$PROJ/prefix/bin/app" ]    || { echo "FAIL: bin not executable"; exit 1; }

# Run installed bin.
"$PROJ/prefix/bin/app" && rc=$? || rc=$?
[ "$rc" = "42" ] || { echo "FAIL: installed bin returned $rc, want 42"; exit 1; }

echo "OK"
