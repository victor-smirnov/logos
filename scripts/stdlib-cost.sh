#!/usr/bin/env bash
# stdlib-cost.sh [probe-name] — does the STDLIB still compile under this probe?
#
# ⚠ THE DAMAGE SHAPE NO ctest SELECTION CAN SEE. On 2026-08-30 the wide
# `recvselfderef` probe priced COST 0 over `-L bc -L pass` plus three `pass`
# directories and then refused NINE `logos.mem` functions on its first real
# build — `ssrle_encode_run`, `ssrle_finish_segment`, `ssrle_compactify` (x2),
# `SsrleRun__pattern_ranks_up_to`, `__full_ranks`, `__ranks_up_to`,
# `index_push_block`, `pack_runs_push` — so `liblogos-mem.a` did not link. The
# corpus does not compile the stdlib, and the stdlib asserts legality by BEING
# BUILT: no fixture author's opinion is involved, which makes it the hardest
# cost oracle in the tree and the one that was outside the instrument.
#
# ── WHY NOT `ninja lib/logos/liblogos-*.a`, WHICH IS THE OBVIOUS FORM ────────
# `scripts/pass-probe.sh` does exactly that: `rm` the four archives, rebuild
# them under the probe, rebuild them again to restore. MEASURED today, both
# halves are expensive and one of them is unsafe:
#   · 141 s for the four archives at the build's own `-O2` — MORE than the
#     whole rest of a per-mechanism price (128 s), and serialised by the layer
#     dependency chain lang -> lcm -> mem -> std;
#   · and it MOVES `scripts/build_hash.py`. A stdlib rebuild changes the archive
#     bytes with no source changed (the module's version string carries a
#     timestamp), 63678a4d6a5f87d9 -> e3eed1c6515e4486 measured — and that hash
#     is the STORE'S BUILD IDENTITY. Rebuilding the stdlib inside a price
#     therefore invalidates every verdict the store holds for this build and
#     makes the NEXT gate re-run 1388 tests it had already measured.
#
# So this does not touch `build/lib/logos` at all. Each layer is compiled from
# SOURCE with its output sent to a scratch directory, reading the four archives
# already in the tree as its dependencies. That makes the four layers mutually
# INDEPENDENT — they are no longer a chain — so they run in parallel:
#
#     -O2, ninja, chained, archives replaced   141 s   + a moved build identity
#     -O0, here,  parallel, scratch output      48 s   + hash untouched
#
# ⚠ AND `-O0` IS NOT A WEAKENING FOR THIS QUESTION, but it IS a narrowing and
# the narrowing is named: the shape being priced is a FRONT-END REFUSAL (sema,
# borrow check, mono), which no `-O` level reaches. An LLVM-level failure that
# only appears at `-O2` is outside this check by construction, and the per-batch
# `cmake --build` is what covers it.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
NAME="${1:-}"
BIN=build/bin/logosc
SCRATCH=build/probe/stdlib-${NAME:-unarmed}
mkdir -p "$SCRATCH"
LAYERS="lang lcm mem std"
rc=0; bad=""
for L in $LAYERS; do
    ( LOGOS_PROBE=${NAME:-} LOGOS_PROBE_FIRE=${LOGOS_PROBE_FIRE:-/dev/null} \
      "$BIN" -O0 --emit-module "stdlib/$L/logos.module" -L build/lib/logos \
             -o "$SCRATCH/liblogos-$L.a" > "$SCRATCH/$L.log" 2>&1
      echo $? > "$SCRATCH/$L.rc" ) &
done
wait
for L in $LAYERS; do
    r=$(cat "$SCRATCH/$L.rc" 2>/dev/null || echo 99)
    [ "$r" = 0 ] || { rc=1; bad="$bad $L"; }
done
if [ "$rc" = 0 ]; then
    echo "stdlib: all four layers compile under '${NAME:-nothing armed}'"
else
    echo "stdlib: ⛔ REFUSED:$bad — the hardest COST there is. The stdlib asserts"
    echo "stdlib:   legality by being built; nothing downstream of it is meaningful."
    for L in $bad; do
        n=$(grep -cE '^error' "$SCRATCH/$L.log" 2>/dev/null || echo 0)
        echo "stdlib:   $L: $n refusals; first four:"
        grep -m4 -E '^error|cannot|refus' "$SCRATCH/$L.log" | sed 's/^/stdlib:     /'
    done
fi
exit $rc  # lint:exit-ok — 0 or 1, assigned literally on the two branches
          # above and never computed from a child status, so the 8-bit
          # ceiling cannot reach it. It IS the answer this tool exists
          # to give: did the stdlib still compile under the probe.
