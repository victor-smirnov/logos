#!/usr/bin/env bash
# agg_frag_single_site_gate.sh REPO_ROOT
#
# THE SHARED ACCUMULATOR FRAGMENTS HAVE ONE DEFINITION SITE — ADR 0025 S4.
#
# ── WHAT THIS GATE IS FOR, AND WHY THE CORPUS CANNOT BE IT ──────────────────
#
# `emit_aggregate` (batch) and `emit_incremental` (incremental) used to spell the
# same 14 fragment arms twice, character-for-character apart from the `__h.`
# receiver: 5 group-creation seeds and 9 insert-only per-row folds. They now come
# from `agg_seed_frag` / `agg_fold_frag` in stdlib/mem/wql/rexpr_walk.logos, and
# the corpus snapshot proved the move emits byte-identical text (165 dumps,
# 7,067,309 bytes, `diff -r` empty).
#
# ⚠ THE CORPUS ORACLE IS BLIND TO THE THING THIS CHANGE BOUGHT. Byte-identity
# says the two emitters AGREE TODAY. It says nothing about whether they agree
# because there is one rule or because there are two copies of one rule — and a
# re-inlined copy is byte-identical on the day it is written. The defect only
# appears on the day someone fixes a seed in the emitter they were reading and
# not in the other one, at which point the corpus moves and blames the fix. That
# is not hypothetical in this file: its own headers record paying for a seed
# defect twice, once for floats (`f64::MAX` is not the top of the order) and once
# for `u64` (i64::MIN's bits are the MIDDLE of the range), and paying twice is
# exactly what two copies of a rule cost.
#
# So the property is STRUCTURAL — "this rule is written once" — and there is no
# composer and no stored key to hang it on. It is a fact about one source file,
# so it is read off that source file, and the gate claims nothing more.
#
# ⚠ WHAT THIS GATE DOES NOT CLAIM. It does not check that the fragments are
# CORRECT (the corpus and the behavioural fixtures do that), that they are the
# only shared code (they are the measured intersection, not a closure), or that a
# determined edit cannot defeat it by respelling a literal. It checks that the
# distinctive text of each shared rule appears ONCE, which is the cheapest true
# statement that goes red when a copy comes back.

set -uo pipefail
ROOT="${1:?repo root}"
F="$ROOT/stdlib/mem/wql/rexpr_walk.logos"
[ -f "$F" ] || { echo "FAIL: $F missing — the gate is blind."; exit 2; }

rc=0

# ── THE SHARED RULES, one distinctive string each ───────────────────────────
# Each must appear EXACTLY ONCE: inside `agg_seed_frag` or `agg_fold_frag`.
# The float seeds are the canonical-NaN / -inf bit patterns; the integer seeds
# are the accumulator's own extrema helpers; the min/max folds are the
# `f64_data_key` total order. Two of anything here is a second copy of a rule.
check_once() {
    local label="$1" pat="$2" want="$3"
    local n; n=$(grep -cF -- "$pat" "$F")
    if [ "$n" -ne "$want" ]; then
        echo "FAIL: $label — expected $want occurrence(s) of [$pat], found $n."
        echo "      A shared accumulator rule has been copied back into an emitter."
        rc=1
    fi
}

check_once "min float seed (canonical NaN)"  'f64_from_bits(0x7FF8000000000000u64)' 1
check_once "max float seed (-inf)"           'f64_from_bits(0xFFF0000000000000u64)' 1
check_once "min integer seed (greatest)"     'agg_acc_greatest'                     1
check_once "max integer seed (least)"        'agg_acc_least'                        1
check_once "min float fold (data-key order)" 'f64_data_key(__v) <'                  1
check_once "max float fold (data-key order)" 'f64_data_key(__v) >'                  1

# ── AND BOTH EMITTERS MUST ACTUALLY ROUTE THROUGH THEM ──────────────────────
# A single definition site is trivially satisfied by a builder NOBODY CALLS, with
# the arms re-inlined at both call sites and the shared literals spelled
# differently. So the exemption is checked in the abuse direction: the seed and
# fold builders must each be CALLED at least twice — once from the batch emitter
# and once from the incremental — and the handle-qualified cell name must exist,
# since it is the incremental's only way in.
for fn in agg_seed_frag agg_fold_frag; do
    # definition + call sites; the definition is one `fn <name>(` line.
    defs=$(grep -cE "^fn $fn\(" "$F")
    calls=$(grep -cE "$fn\(" "$F")
    calls=$((calls - defs))
    if [ "$defs" -ne 1 ]; then
        echo "FAIL: $fn — expected exactly 1 definition, found $defs."
        rc=1
    fi
    if [ "$calls" -lt 2 ]; then
        echo "FAIL: $fn — found $calls call site(s), need >= 2 (batch AND incremental)."
        echo "      A builder with one caller is not shared code."
        rc=1
    fi
done

if ! grep -qE '^fn hga_name\(' "$F"; then
    echo "FAIL: hga_name missing — the incremental emitter's receiver-qualified"
    echo "      cell name is how it reaches the shared fragments at all."
    rc=1
fi

if [ "$rc" -eq 0 ]; then
    echo "PASS: 6 distinctive shared-fragment literals pinned at one site (8 of the 14 arms have no count-of-1 literal - their text legitimately recurs in the weighted incremental folds; pinned only via the builders' definition/call counts),"
    echo "      and both aggregate emitters route through it."
fi
# `rc` is a two-valued FLAG, not a count: it is initialised to 0 and the only
# other assignment anywhere in this file is the literal `rc=1`, so the 8-bit
# ceiling that turns `exit 256` into a green run is unreachable. The failure
# COUNT is deliberately not carried in the status — that would be the computed
# exit the rule is about, and the number is already on stdout above.
exit $rc  # lint:exit-ok — `rc` is set only to the literals 0 and 1, see above
