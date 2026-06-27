#!/usr/bin/env bash
# B1.8: lib targets can mix Logos + C + asm sources.
#
# Validates:
#   - manifest carries c_sources / asm_sources arrays
#   - cc -c is invoked per native source, in parallel with logosc
#   - native .o files are folded into the lib archive alongside .o + .writ0
#   - bin can call into the C symbols via Logos extern fn
#   - touching one C file rebuilds only that file
#   - pure-native lib (no Logos sources) is allowed

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT

mkdir -p "$PROJ/src/core" "$PROJ/src" "$PROJ/native"

# A mixed-source lib: Logos wrappers + C implementations.
cat > "$PROJ/lforge.writ" <<'EOF'
{
    name:    "demo",
    version: "0.1.0",
    targets: [
        { kind: "lib", name: "core",
          src: "src/core",
          c_sources:   ["native/answer.c"],
          asm_sources: ["native/triple.S"] },
        { kind: "bin", name: "app",  src: "src", entry: "main",
          deps: ["core"] }
    ]
}
EOF

cat > "$PROJ/native/answer.c" <<'EOF'
int demo_answer(void) { return 42; }
EOF

cat > "$PROJ/native/triple.S" <<'EOF'
        .globl  demo_triple
        .type   demo_triple, @function
demo_triple:
        leal    (%rdi,%rdi,2), %eax
        ret
        .size   demo_triple, .-demo_triple
        .section .note.GNU-stack,"",@progbits
EOF

cat > "$PROJ/src/core/wrap.logos" <<'EOF'
package core;
extern fn demo_answer() -> i32;
extern fn demo_triple(x: i32) -> i32;
pub fn answer() -> i32 { return unsafe { demo_answer() }; }
pub fn triple(x: i32) -> i32 { return unsafe { demo_triple(x) }; }
EOF

cat > "$PROJ/src/main.logos" <<'EOF'
package app;
use core;
fn main() -> i32 { return triple(answer()); }
EOF

cd "$PROJ"

# Cold build.
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/b1.log" 2>&1
grep -q "compiling 3 file(s) in parallel" "$PROJ/b1.log" || {
    echo "FAIL: cold build did not announce 3 files (1 logos + 1 c + 1 asm) in parallel"
    cat "$PROJ/b1.log"; exit 1;
}

# Per-file artifacts.
[ -f "$PROJ/.lforge/debug/_files/core/wrap.o" ]      || { echo "FAIL: wrap.o missing"; exit 1; }
[ -f "$PROJ/.lforge/debug/_files/core/wrap.writ0" ] || { echo "FAIL: wrap.writ0 missing"; exit 1; }
[ -f "$PROJ/.lforge/debug/_files/core/wrap.wr0" ]     || { echo "FAIL: wrap.wr0 missing"; exit 1; }
[ -f "$PROJ/.lforge/debug/_files/core/answer.c.o" ]  || { echo "FAIL: answer.c.o missing"; exit 1; }
[ -f "$PROJ/.lforge/debug/_files/core/triple.S.o" ]  || { echo "FAIL: triple.S.o missing"; exit 1; }

# Archive contains all four members.
ar_list=$(ar t "$PROJ/.lforge/debug/out/libcore.a")
echo "$ar_list" | grep -q "^wrap.o$"        || { echo "FAIL: archive missing wrap.o"; exit 1; }
echo "$ar_list" | grep -q "^wrap.wr0$"      || { echo "FAIL: archive missing wrap.wr0"; exit 1; }
echo "$ar_list" | grep -q "^answer.c.o$"    || { echo "FAIL: archive missing answer.c.o"; exit 1; }
echo "$ar_list" | grep -q "^triple.S.o$"    || { echo "FAIL: archive missing triple.S.o"; exit 1; }

# Run: triple(answer()) = triple(42) = 126.
"$PROJ/.lforge/debug/out/app" && rc=$? || rc=$?
[ "$rc" = "126" ] || { echo "FAIL: app returned $rc, want 126 (= 3 * 42)"; exit 1; }

# No-op rebuild → nothing recompiles.
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/b2.log" 2>&1
grep -q "compiling" "$PROJ/b2.log" && { echo "FAIL: no-op rebuild compiled something"; cat "$PROJ/b2.log"; exit 1; } || true

# Touch only the C file → only its .o gets rebuilt.
sleep 1.1
before_logos=$(stat -c%Y "$PROJ/.lforge/debug/_files/core/wrap.o")
before_asm=$(stat -c%Y   "$PROJ/.lforge/debug/_files/core/triple.S.o")
before_c=$(stat -c%Y     "$PROJ/.lforge/debug/_files/core/answer.c.o")
touch "$PROJ/native/answer.c"
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/b3.log" 2>&1
grep -q "compiling 1 file(s) in parallel" "$PROJ/b3.log" || {
    echo "FAIL: C-only edit did not result in 1-file rebuild"
    cat "$PROJ/b3.log"; exit 1;
}
after_logos=$(stat -c%Y "$PROJ/.lforge/debug/_files/core/wrap.o")
after_asm=$(stat -c%Y   "$PROJ/.lforge/debug/_files/core/triple.S.o")
after_c=$(stat -c%Y     "$PROJ/.lforge/debug/_files/core/answer.c.o")
[ "$before_logos" = "$after_logos" ] || { echo "FAIL: wrap.o rebuilt unnecessarily"; exit 1; }
[ "$before_asm"   = "$after_asm"   ] || { echo "FAIL: triple.S.o rebuilt unnecessarily"; exit 1; }
[ "$before_c"    != "$after_c"     ] || { echo "FAIL: answer.c.o was not rebuilt after touch"; exit 1; }

# ── Pure-native lib (no Logos sources) ─────────────────────────────────────
mkdir -p "$PROJ/pure_proj/native"
cat > "$PROJ/pure_proj/lforge.writ" <<'EOF'
{
    name:    "puredemo",
    version: "0.1.0",
    targets: [
        { kind: "lib", name: "rt", c_sources: ["native/only.c"] }
    ]
}
EOF
cat > "$PROJ/pure_proj/native/only.c" <<'EOF'
int demo_value(void) { return 7; }
EOF
cd "$PROJ/pure_proj"
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/pure.log" 2>&1
[ -f "$PROJ/pure_proj/.lforge/debug/out/librt.a" ] || { echo "FAIL: pure-native lib not built"; cat "$PROJ/pure.log"; exit 1; }
ar t "$PROJ/pure_proj/.lforge/debug/out/librt.a" | grep -q "^only.c.o$" || { echo "FAIL: pure-native archive missing only.c.o"; exit 1; }

echo "OK"
