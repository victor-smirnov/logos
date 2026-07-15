#!/usr/bin/env bash
# B1.9 dogfood: build stdlib_rt + stdlib_fibers via lforge and verify the
# resulting archives contain the same set of members that cmake produces.
#
# This isn't a replacement for cmake's stdlib build (the Logos parts of
# stdlib still need cmake for ast_only-aware emit and bootstrap order).
# It's a smoke test that the C / asm half of stdlib *can* be assembled
# by lforge — meaning lforge has reached cmake parity for native libs.

set -euo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"
RT_SRC="${4:?stdlib/rt source dir}"

PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT

mkdir -p "$PROJ/rt"
# Symlink each native source so we don't fight cmake for the build dir.
for f in atomic_ops.S clock.c env.c fiber_ctx.S fiber_stack.c fmt_native.c fs_meta.c metaprog_stubs.c test_recovery.c thread_uring.c; do
    ln -s "$RT_SRC/$f" "$PROJ/rt/$f"
done

cat > "$PROJ/lforge.writ" <<'EOF'
{
    name:    "stdlib_native",
    version: "0.1.0",
    targets: [
        { kind: "lib", name: "lstdlib_fibers",
          asm_sources: ["rt/fiber_ctx.S"] },
        { kind: "lib", name: "lstdlib_rt",
          c_sources:   ["rt/clock.c", "rt/env.c", "rt/fiber_stack.c", "rt/fmt_native.c", "rt/fs_meta.c", "rt/metaprog_stubs.c", "rt/test_recovery.c", "rt/thread_uring.c"],
          asm_sources: ["rt/atomic_ops.S"] }
    ]
}
EOF

cd "$PROJ"
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$PROJ/build.log" 2>&1 || {
    echo "FAIL: lforge build of stdlib native targets failed"
    cat "$PROJ/build.log"
    exit 1
}

[ -f "$PROJ/.lforge/debug/out/liblstdlib_rt.a" ]     || { echo "FAIL: lstdlib_rt.a missing"; exit 1; }
[ -f "$PROJ/.lforge/debug/out/liblstdlib_fibers.a" ] || { echo "FAIL: lstdlib_fibers.a missing"; exit 1; }

# Compare member sets with cmake's output.
sort_members() { ar t "$1" | sort; }
diff <(sort_members "$LIB/liblstdlib_rt.a") \
     <(sort_members "$PROJ/.lforge/debug/out/liblstdlib_rt.a") \
    || { echo "FAIL: lstdlib_rt member list differs from cmake's"; exit 1; }
diff <(sort_members "$LIB/liblstdlib_fibers.a") \
     <(sort_members "$PROJ/.lforge/debug/out/liblstdlib_fibers.a") \
    || { echo "FAIL: lstdlib_fibers member list differs from cmake's"; exit 1; }

# Sanity: lforge's lstdlib_rt has at least the expected symbols.
# (use a saved nm dump to avoid SIGPIPE under `set -o pipefail` from `grep -q`)
nm_out=$(nm "$PROJ/.lforge/debug/out/liblstdlib_rt.a" 2>/dev/null)
for sym in logos_path_exists logos_mkdir logos_path_size; do
    echo "$nm_out" | grep -q "T $sym" || { echo "FAIL: $sym not exported from lforge's lstdlib_rt.a"; exit 1; }
done

echo "OK"
