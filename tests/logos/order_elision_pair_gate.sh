#!/usr/bin/env bash
# order_elision_pair_gate.sh LOGOSC PASS_DIR
#
# THE ORDER ELISION, PINNED IN BOTH DIRECTIONS — ADR 0025 S3.
#
# ── WHY A PAIR, AND WHY THE FIXTURE'S OWN ASSERTIONS ARE NOT ENOUGH ─────────
#
# The claim is that `order by <c>` over a source whose rows already arrive
# sorted by `<c>` emits NO Sort node. That claim has two failure directions and
# the fixture can only see one of them:
#
#   • ELIDING TOO MUCH is a wrong ANSWER, and `deem_order_elision` catches it —
#     its rows are seeded so `val` DECREASES in `key`, so an elision that fired
#     on `order by e.val` returns the exact reverse sequence and the fixture
#     exits 7. MEASURED: dropping the column-match clause from
#     `ap_order_is_noop` (`if !str_eq(f.name, prm.rel_ordcol[ri])` -> `if
#     false`), rebuilding the stdlib, re-running — exit 7, restored to green.
#
#   • ELIDING NOTHING IS INVISIBLE TO THE FIXTURE, and that is what this gate
#     is for. `by_key` returns the identical eight rows in the identical order
#     whether the sort ran or not — the permutation is the identity, which is
#     the entire premise. Every runtime assertion in that fixture is green on a
#     compiler where S3 does nothing at all. So the ABSENCE of the sort is read
#     off the artifact and off the plan trace, which is where an absence can be
#     distinguished from an oversight.
#
# ── THE THREE QUERIES, FROM ONE SOURCE DECLARATION ─────────────────────────
#
#   ADMIT   `by_key`     — `order by e.key`, the column the family declares
#                          (`order entry = key;`) and whose walk implements
#                          `OrderedBy`. Artifact: a streamed `next_batch()`
#                          loop, no `Buffer<`, no `__ix0`, no `__ks`.
#                          Trace:    `no materialization on ordered source`.
#   REFUSE  `by_val`     — the SAME query, one token different. Artifact: the
#                          `Buffer<(u64, u64)>` landing, `__ix0`, `__ks` and the
#                          insertion sort. Trace: `sort on order by`.
#   HARVEST `head_three` — `order by e.key limit 3`. Elided AND bounded: the
#                          streamed arm's `__out.len() < (3i64)` break with no
#                          landing in front of it. This is the traversal axis's
#                          offset/limit claim in the only form the surface can
#                          spell today (there is no `offset` keyword in
#                          `wql.peg` — RECORDED, not invented).
#
# One `pub container Led`, one declaration, three queries: a pass is evidence
# about the elision and not about three unrelated compilations.
#
# ── COUNTS ARE PREDICTED, NOT DISCOVERED ───────────────────────────────────
#
# Every clause below asserts an exact count, because "at least one" is what
# makes a gate survive the thing it was written to catch. The elision ground
# must appear EXACTLY TWICE in the trace (`by_key` and `head_three`, once each)
# and the sort node EXACTLY ONCE (`by_val`) — a third elision would mean the
# refuse twin stopped refusing, and a second sort would mean an admitted query
# started sorting.
#
# ⚠ A COMPILE FAILURE IS A PARTICIPANT-COUNT CHANGE, NOT A DIFF. The gate exits
# 2 (never clean) when the fixture does not compile or produces no dump, so a
# tree where the fixture stopped existing cannot pass by having nothing to fail.
#
# PROVED TO BITE — measured, not argued, and on the FIXTURE alone (no stdlib
# rebuild involved), restored to a byte-identical source (md5
# 301fc3411452b484d80caa6580ab4ecb) with a green checkpoint after:
#
#   P1  `by_val`'s order column swapped `val` -> `key` (refuse twin turned into
#       a second admit): 8 claims failed — all four REFUSE artifact clauses
#       (`Buffer<`, `__ks.push(`, `__ix0.push(`, `__rel_m_sl`), both trace
#       elision counts (2 -> 3), and both trace Sort-node counts (1 -> 0).
#
# ⚠ `desc` ELISION HAS LANDED, AND THE COUNTS HERE DID NOT MOVE — recorded,
# because the prediction above said they would. It needed exactly what was
# predicted (`r` AND `n`: a fresh walk is at its base and `retreat()` refuses,
# so backward iteration must first `seek_nth(size())`, spelled `land_end()`) —
# but it landed on its OWN subject rather than by adding a `desc` query to
# `deem_order_elision`, so this file's three queries, its artifact and its trace
# are untouched. The counts moving was a property of the fixture, not of the
# compiler, and this gate's three queries stayed ascending.
#
# The descending pair is `order_desc_pair_gate.sh` over
# `pass/deem_order_desc_elision` (family: admit asc, admit desc, bounded desc,
# narrowed desc, refuse wrong-column) and `pass/deem_order_desc_forward_only`
# (an `OrderedBy`-only source: admit asc, REFUSE desc — the clause no family
# fixture can reach). The standing instruction is unchanged: re-predict, never
# relax a count into an inequality.
#
# EXIT: 0 clean · 1 a claim failed · 2 the gate could not look (never clean).
set -uo pipefail

LOGOSC=${1:?usage: order_elision_pair_gate.sh <logosc> <pass dir>}
PASSD=${2:?usage: order_elision_pair_gate.sh <logosc> <pass dir>}

export LC_ALL=C
TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

fails=0
note() { echo "FAIL: $*"; fails=$((fails+1)); }

B=deem_order_elision

# ── compile once, keeping stdout/stderr/rc in separate files ────────────────
# `rc` comes from a REDIRECT and not from a pipeline: `cmd | tail` returns
# TAIL's status, which is the recorded gate-lie form this file must not repeat.
LOGOS_TRACE_PLAN=1 "$LOGOSC" "$PASSD/$B.logos" --gen-dir "$TMPD/gen" \
    -o "$TMPD/$B.o" > "$TMPD/out" 2> "$TMPD/err"
rc=$?
if [ "$rc" != "0" ]; then
    echo "FAIL(2): $B does not compile (rc=$rc) — there is nothing to read."
    grep -m3 -i 'error' "$TMPD/out" "$TMPD/err" 2>/dev/null | head -3
    exit 2
fi

shopt -s nullglob
DUMPS=("$TMPD/gen"/*.gen.logos)
if [ "${#DUMPS[@]}" -lt 1 ]; then
    echo "FAIL(2): no --gen-dir dump for $B; the artifact clauses have no subject."
    exit 2
fi
cat "${DUMPS[@]}" > "$TMPD/art.txt"

# The three run bodies, sliced apart so a clause about `by_key` cannot be
# satisfied by text that lives in `by_val`. Without this every "absent" clause
# below is vacuous — the sort machinery IS in the file, just not in this fn.
slice_fn() {   # slice_fn <fn-name> <outfile>
    # ⚠ NO `exit` IN THE awk PROGRAM, and not because the gate lint asked: awk's
    # `exit` and the shell's `exit` are spelled identically and a static reader
    # (the lint, or a person) cannot tell which one a line means. A `done` flag
    # says the same thing and leaves exactly one meaning of the word in this
    # file — the one that ends the gate.
    awk -v want="pub fn $1(" '
        !done && index($0, want) == 1 { inside = 1 }
        inside && !done { print }
        inside && $0 == "}" { inside = 0; done = 1 }
    ' "$TMPD/art.txt" > "$2"
    [ -s "$2" ]
}
for f in by_key_run by_val_run head_three_run; do
    if ! slice_fn "$f" "$TMPD/$f.txt"; then
        echo "FAIL(2): could not slice \`$f\` out of the dump — the emitted fn"
        echo "         names moved, so every clause below would read an empty file."
        exit 2
    fi
done

cnt() { grep -c -F -- "$2" "$1" 2>/dev/null || true; }

want() {   # want <file> <needle> <expected-count> <what>
    local got; got=$(cnt "$1" "$2")
    [ "$got" = "$3" ] || note "$4: expected $3 occurrence(s) of '$2', found $got"
}

# ── ADMIT: `by_key_run` streams, and NOTHING is built in front of it ────────
# Checked positively as well as negatively, so a fn that stopped being emitted
# at all cannot pass by containing none of the sort spellings.
want "$TMPD/by_key_run.txt" 'next_batch()'   1 "ADMIT by_key streams the walk"
want "$TMPD/by_key_run.txt" 'Buffer<'        0 "ADMIT by_key builds no landing"
want "$TMPD/by_key_run.txt" '__ix0'          0 "ADMIT by_key builds no permutation"
want "$TMPD/by_key_run.txt" '__ks'           0 "ADMIT by_key collects no sort keys"
want "$TMPD/by_key_run.txt" '__rel_m_sl'     0 "ADMIT by_key subscripts no slice"

# ── REFUSE: the SAME query over a non-ordered column keeps every one of them ─
# This half is what says the elision discriminates. Without it the gate is green
# on a compiler that elides unconditionally — which is the wrong-answer bug the
# fixture catches at run time and this gate would otherwise vouch for.
want "$TMPD/by_val_run.txt" 'Buffer<'        1 "REFUSE by_val keeps its landing"
want "$TMPD/by_val_run.txt" '__ks.push('     1 "REFUSE by_val collects sort keys"
want "$TMPD/by_val_run.txt" '__ix0.push('    1 "REFUSE by_val builds a permutation"
# 4, not 3, and the prediction was wrong on the first run — RECORDED rather
# than quietly corrected: the binding itself (`let __rel_m_sl: &[…] =`) counts
# alongside the three uses (the collect loop's bound, the collect loop's
# subscript, the projection loop's permuted subscript).
want "$TMPD/by_val_run.txt" '__rel_m_sl'     4 "REFUSE by_val subscripts the sorted slice"

# ── HARVEST: elided AND bounded ────────────────────────────────────────────
# Two breaks, not one: the outer loop stops pulling LEAVES and the inner loop
# stops walking rows inside the leaf it already has. A single break would still
# answer correctly and would still pull one leaf too many.
want "$TMPD/head_three_run.txt" '__out.len() < (3i64)' 2 "HARVEST bounded walk breaks on the limit"
want "$TMPD/head_three_run.txt" 'Buffer<'              0 "HARVEST bounded walk builds no landing"
want "$TMPD/head_three_run.txt" '__ix0'                0 "HARVEST bounded walk builds no permutation"

# ── THE TRACE: the absence is EXPLAINED, not silent ────────────────────────
# The requirement S3 exists to meet. An elision that emitted the right code and
# said nothing about it is indistinguishable, to a reader of the plan, from a
# planner that never considered sorting — so the ground is asserted by its own
# token and counted.
want "$TMPD/err" 'no materialization on ordered source: order by is a no-op' 2 \
     "TRACE the elided sort has a positive ground"
want "$TMPD/err" 'no sort node: the query orders by the column this source declares' 2 \
     "TRACE the ground carries its sentence"
want "$TMPD/err" 'sort on order by' 1 \
     "TRACE the refuse twin still reports its Sort node"
want "$TMPD/err" 'key vector on' 1 \
     "TRACE only the sorting query collects a key vector"

if [ "$fails" -ne 0 ]; then
    echo "order_elision_pair_gate: $fails claim(s) failed."
    exit 1
fi
echo "order_elision_pair_gate: OK — elision admitted, refused, bounded, explained."
exit 0
