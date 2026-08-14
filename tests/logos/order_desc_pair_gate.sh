#!/usr/bin/env bash
# order_desc_pair_gate.sh LOGOSC PASS_DIR
#
# THE BACKWARD WALK, PINNED IN BOTH DIRECTIONS — ADR 0025 S3-desc (Victor's
# traversal axis, §3).
#
# ── WHAT THE FIXTURES CANNOT SEE, AND THIS GATE CAN ────────────────────────
#
# `order by <c> desc` over a source that declares its rows arrive sorted by
# `<c>` emits no Sort node: it lands at the end (ONE `land_end()`, which IS
# `RandomAccess::seek_nth(size())`) and pulls backward per leaf. The claim has
# two failure directions and the fixtures cover exactly one each:
#
#   • REVERSING WRONGLY is a wrong ANSWER, and `deem_order_desc_elision` catches
#     it at run time — the full descending sequence is asserted row by row, so a
#     walk that pulled leaves backward but read rows forward INSIDE each leaf
#     (right at both ends, wrong in the middle) exits 7.
#
#   • NOT ELIDING AT ALL IS INVISIBLE TO BOTH FIXTURES. A materialized,
#     insertion-sorted buffer returns the identical descending sequence — that
#     is the entire premise. Every runtime assertion in both files is green on a
#     compiler where S3-desc does nothing. So the ABSENCE of the sort, and the
#     PRESENCE of the backward walk, are read off the artifact and the plan
#     trace, which is where an absence can be told from an oversight.
#
# ⚠ AND THE COST IS NOT HERE EITHER. That a backward scan costs one descent per
# LEAF — not one per row, not the skip loop a `seek_nth` exists to replace — is
# not visible in any artifact: the emitted text is identical either way. It is
# measured by `logos_09_ctr_leaf_descent` / `_vec` under callgrind (clauses 7-9,
# both families). This gate pins the SHAPE; that gate pins the PRICE; neither
# substitutes for the other.
#
# ── THE SEVEN QUERIES, OVER TWO SOURCES, AND WHY IT TAKES TWO ──────────────
#
# FAMILY (`deem_order_desc_elision`) — OrderedBy + Bidirectional + RandomAccess
# + batches, i.e. every capability the elision asks for:
#   ADMIT asc `desc_asc`    — S3's ascending elision, unchanged: `next_batch()`.
#   ADMIT desc `desc_all`   — `land_end()` + `prev_batch()`, no landing, no
#                             permutation, no key vector.
#   HARVEST   `top_three`   — the same backward walk with the `limit` break in
#                             BOTH loops: `top-n` answered by landing at the end
#                             and pulling ONE leaf.
#   NARROWED  `tail_desc`   — the same over a pushdown landing, where
#                             `retreat()`'s clamp to `base` is what stops it
#                             reading rows the `where` excluded.
#   REFUSE    `desc_val`    — the SAME source and direction over a column the
#                             rows are NOT sorted by: the whole Sort apparatus
#                             stays.
#
# FORWARD-ONLY (`deem_order_desc_forward_only`) — `OrderedBy` and NOTHING else:
#   ADMIT asc  `tick_asc`   — ordered is enough for ascending.
#   REFUSE desc `tick_desc` — ordered is NOT enough for descending. THIS IS THE
#                             CLAUSE, and it needs a second fixture because
#                             every ordered producer in the tree except this one
#                             is a container family that has all three
#                             capabilities. Without it the three traversal
#                             clauses in `ap_order_is_noop` are a branch nothing
#                             executes and every green above them is vacuous.
#
# ⚠ THE TWO FIXTURES ARE APART FOR A MECHANICAL REASON, RECORDED: a module
# holding a `static mut`, a container-family deem AND a native-source deem fails
# to compile — `logosc-metaprog: jit add_module: Duplicate definition of symbol`
# — a metaprog/JIT defect independent of S3, with a minimal repro of exactly
# that trio. The split is a fixture boundary, not a fix.
#
# ── COUNTS ARE PREDICTED, NOT DISCOVERED ───────────────────────────────────
#
# Every clause asserts an exact count. The reversed ground must appear EXACTLY
# THREE times in the family trace (`desc_all`, `top_three`, `tail_desc`) and the
# ascending one EXACTLY ONCE — a fourth reversal would mean the refuse twin
# stopped refusing, and a second sort node would mean an admitted query started
# sorting. `__ix0` is 8 in the refused body and 0 in every admitted one, and
# `__rel_m_sl` is 4 rather than 3 for the reason S3's gate recorded: the binding
# counts alongside its three uses.
#
# ⚠ A COMPILE FAILURE IS A PARTICIPANT-COUNT CHANGE, NOT A DIFF. The gate exits
# 2 (never clean) when either fixture does not compile or produces no dump.
#
# ── PROVED TO BITE — measured, one perturbation at a time, each restored to a
# byte-identical source (md5 checked) with a green checkpoint after ────────────
#
#   P1  FIXTURE ONLY, no stdlib rebuild. `desc_val`'s order column swapped
#       `val` -> `key`, turning the refuse twin into a second admit: 10 claims
#       failed — all six REFUSE artifact clauses (`Buffer<`, `__ks.push(`,
#       `__ix0.push(`, `__rel_m_sl`, and the two "never lands / never pulls
#       backward" clauses, which went 0 -> 1), the reversed ground count
#       (3 -> 4), its sentence (3 -> 4), and both Sort-node trace counts
#       (1 -> 0). ⚠ NINE WERE PREDICTED AND TEN FIRED — the ground's SENTENCE
#       clause was left out of the prediction. Recorded rather than quietly
#       renumbered: an under-predicted count is a reader's error, and hiding it
#       would make the next prediction less trustworthy, not more.
#
#   P2  THE TRAVERSAL CLAUSES THEMSELVES, at the decision site, with a full
#       stdlib rebuild: the three `desc` clauses deleted from `ap_order_is_noop`
#       (`rel_bidir` / `rel_randacc` / `rel_batch`), i.e. exactly the permissive
#       defect this pair exists to close. Result: 5 gate claims failed (the
#       three FWD-ONLY artifact clauses, the reversed ground appearing where it
#       cannot, the Sort node gone) AND — the part that matters —
#       `deem_order_desc_forward_only` EXITED 8: the descending query returned
#       its rows in ASCENDING order. A WRONG ANSWER, not a slow one, which is
#       the whole reason these clauses are checked in the abuse direction. The
#       mechanism is the one the clause comments name: `TicksIter` is not a
#       batch producer, so with the clauses gone the emitter cleared `has_sort`
#       and then emitted the FORWARD row loop, there being no row-at-a-time
#       backward shape to emit.
#
# EXIT: 0 clean · 1 a claim failed · 2 the gate could not look (never clean).
set -uo pipefail

LOGOSC=${1:?usage: order_desc_pair_gate.sh <logosc> <pass dir>}
PASSD=${2:?usage: order_desc_pair_gate.sh <logosc> <pass dir>}

export LC_ALL=C
TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

fails=0
note() { echo "FAIL: $*"; fails=$((fails+1)); }

# ── compile both, keeping stdout/stderr/rc apart ───────────────────────────
# `rc` comes from a REDIRECT, never from a pipeline: `cmd | tail` returns TAIL's
# status, which is the recorded gate-lie form this file must not repeat.
compile() {   # compile <basename>
    local B="$1"
    LOGOS_TRACE_PLAN=1 "$LOGOSC" "$PASSD/$B.logos" --gen-dir "$TMPD/gen_$B" \
        -o "$TMPD/$B.o" > "$TMPD/$B.out" 2> "$TMPD/$B.err"
    local rc=$?
    if [ "$rc" != "0" ]; then
        echo "FAIL(2): $B does not compile (rc=$rc) — there is nothing to read."
        grep -m3 -i 'error' "$TMPD/$B.out" "$TMPD/$B.err" 2>/dev/null | head -3
        exit 2
    fi
    shopt -s nullglob
    local DUMPS=("$TMPD/gen_$B"/*.gen.logos)
    if [ "${#DUMPS[@]}" -lt 1 ]; then
        echo "FAIL(2): no --gen-dir dump for $B; the artifact clauses have no subject."
        exit 2
    fi
    cat "${DUMPS[@]}" > "$TMPD/art_$B.txt"
}

FAM=deem_order_desc_elision
FWD=deem_order_desc_forward_only
compile "$FAM"
compile "$FWD"

# The run bodies, sliced apart so a clause about one query cannot be satisfied
# by text living in another. Without this every "absent" clause is vacuous — the
# sort machinery IS in the file, just not in this fn.
slice_fn() {   # slice_fn <fn-name> <artifact> <outfile>
    # ⚠ NO `exit` IN THE awk PROGRAM: awk's `exit` and the shell's are spelled
    # identically and a static reader cannot tell which one a line means. A
    # `done` flag leaves exactly one meaning of the word in this file.
    awk -v want="pub fn $1(" '
        !done && index($0, want) == 1 { inside = 1 }
        inside && !done { print }
        inside && $0 == "}" { inside = 0; done = 1 }
    ' "$2" > "$3"
    [ -s "$3" ]
}
for f in desc_asc_run desc_all_run top_three_run tail_desc_run desc_val_run; do
    if ! slice_fn "$f" "$TMPD/art_$FAM.txt" "$TMPD/$f.txt"; then
        echo "FAIL(2): could not slice \`$f\` out of $FAM's dump — the emitted fn"
        echo "         names moved, so every clause below would read an empty file."
        exit 2
    fi
done
for f in tick_asc_run tick_desc_run; do
    if ! slice_fn "$f" "$TMPD/art_$FWD.txt" "$TMPD/$f.txt"; then
        echo "FAIL(2): could not slice \`$f\` out of $FWD's dump."
        exit 2
    fi
done

cnt() { grep -c -F -- "$2" "$1" 2>/dev/null || true; }
want() {   # want <file> <needle> <expected-count> <what>
    local got; got=$(cnt "$1" "$2")
    [ "$got" = "$3" ] || note "$4: expected $3 occurrence(s) of '$2', found $got"
}

# ── ADMIT, ascending: unchanged by this stage, and still forward ───────────
want "$TMPD/desc_asc_run.txt" 'next_batch()' 1 "ADMIT asc pulls FORWARD"
want "$TMPD/desc_asc_run.txt" 'land_end()'   0 "ADMIT asc does not land at the end"
want "$TMPD/desc_asc_run.txt" 'prev_batch()' 0 "ADMIT asc does not pull backward"
want "$TMPD/desc_asc_run.txt" 'Buffer<'      0 "ADMIT asc builds no landing"
want "$TMPD/desc_asc_run.txt" '__ix0'        0 "ADMIT asc builds no permutation"

# ── ADMIT, descending: THE SUBJECT ────────────────────────────────────────
# Positive AND negative: a fn that stopped being emitted at all cannot pass by
# containing none of the sort spellings.
want "$TMPD/desc_all_run.txt" 'land_end()'   1 "ADMIT desc lands at the end ONCE"
want "$TMPD/desc_all_run.txt" 'prev_batch()' 1 "ADMIT desc pulls BACKWARD"
want "$TMPD/desc_all_run.txt" 'next_batch()' 0 "ADMIT desc never pulls forward"
want "$TMPD/desc_all_run.txt" 'Buffer<'      0 "ADMIT desc builds no landing"
want "$TMPD/desc_all_run.txt" '__ix0'        0 "ADMIT desc builds no permutation"
want "$TMPD/desc_all_run.txt" '__ks'         0 "ADMIT desc collects no sort keys"
# The INNER reversal, spelled out. Reversing the leaf order alone yields a
# sequence right at both ends and wrong in the middle; this is the line that
# says the rows inside each leaf are walked down. `> 0u64` and a decrement
# before the read — the shape `>= 0u64` over an unsigned counter would spell as
# an infinite loop.
want "$TMPD/desc_all_run.txt" '> 0u64'       1 "ADMIT desc counts the leaf DOWN"
want "$TMPD/desc_all_run.txt" '- 1u64'       1 "ADMIT desc decrements before it reads"

# ── HARVEST: the bounded BACKWARD walk (top-n) ────────────────────────────
# Two breaks, not one: the outer loop stops pulling LEAVES and the inner stops
# walking rows inside the leaf it already has. A single break still answers
# correctly and still pulls one leaf too many.
want "$TMPD/top_three_run.txt" 'land_end()'            1 "HARVEST top-n lands at the end"
want "$TMPD/top_three_run.txt" 'prev_batch()'          1 "HARVEST top-n pulls backward"
want "$TMPD/top_three_run.txt" '__out.len() < (3i64)'  2 "HARVEST top-n breaks on the limit in BOTH loops"
want "$TMPD/top_three_run.txt" 'Buffer<'               0 "HARVEST top-n builds no landing"
want "$TMPD/top_three_run.txt" '__ix0'                 0 "HARVEST top-n builds no permutation"

# ── NARROWED + descending: the pushdown landing is walked backward ────────
want "$TMPD/tail_desc_run.txt" 'land_end()'   1 "NARROWED desc lands at the end"
want "$TMPD/tail_desc_run.txt" 'prev_batch()' 1 "NARROWED desc pulls backward"
want "$TMPD/tail_desc_run.txt" 'Buffer<'      0 "NARROWED desc builds no landing"

# ── REFUSE, wrong column: the SAME source and direction keeps everything ──
# Without this half the gate is green on a compiler that reverses
# unconditionally — the wrong-ANSWER bug the fixture catches at run time and
# this gate would otherwise vouch for.
want "$TMPD/desc_val_run.txt" 'Buffer<'      1 "REFUSE wrong-column keeps its landing"
want "$TMPD/desc_val_run.txt" '__ks.push('   1 "REFUSE wrong-column collects sort keys"
want "$TMPD/desc_val_run.txt" '__ix0.push('  1 "REFUSE wrong-column builds a permutation"
# 4, not 3 — the binding (`let __rel_m_sl: &[…] =`) counts alongside its three
# uses. The same prediction S3's gate got wrong on its first run and recorded
# rather than quietly corrected.
want "$TMPD/desc_val_run.txt" '__rel_m_sl'   4 "REFUSE wrong-column subscripts the sorted slice"
want "$TMPD/desc_val_run.txt" 'land_end()'   0 "REFUSE wrong-column never lands at the end"
want "$TMPD/desc_val_run.txt" 'prev_batch()' 0 "REFUSE wrong-column never pulls backward"

# ── THE FORWARD-ONLY PAIR: the traversal clause itself ────────────────────
# Ordered is enough for ascending …
want "$TMPD/tick_asc_run.txt" 'Buffer<'      0 "FWD-ONLY asc is elided (no landing)"
want "$TMPD/tick_asc_run.txt" '__ix0'        0 "FWD-ONLY asc builds no permutation"
# … and NOT enough for descending. This is the whole reason the second fixture
# exists: the source cannot retreat, so the plan must keep the Sort node.
want "$TMPD/tick_desc_run.txt" 'Buffer<'     1 "FWD-ONLY desc keeps its landing"
want "$TMPD/tick_desc_run.txt" '__ks.push('  1 "FWD-ONLY desc collects sort keys"
want "$TMPD/tick_desc_run.txt" '__ix0.push(' 1 "FWD-ONLY desc builds a permutation"
want "$TMPD/tick_desc_run.txt" 'land_end()'  0 "FWD-ONLY desc never lands at the end"
want "$TMPD/tick_desc_run.txt" 'prev_batch()' 0 "FWD-ONLY desc never pulls backward"

# ── THE TRACE: each absence carries ITS OWN ground ────────────────────────
# Two grounds and not one. Both say "no Sort node"; only one of them says the
# walk runs backward, and a reader comparing two elided plans has nothing else
# to go on.
want "$TMPD/$FAM.err" 'no materialization on ordered source, reversed: order by desc is a backward walk' 3 \
     "TRACE the reversed elision has its own ground, on all three desc queries"
want "$TMPD/$FAM.err" 'no materialization on ordered source: order by is a no-op' 1 \
     "TRACE the ascending elision keeps the ascending ground"
want "$TMPD/$FAM.err" '`desc` reverses the WALK instead of the rows' 3 \
     "TRACE the reversed ground carries its sentence"
want "$TMPD/$FAM.err" 'sort on order by' 1 \
     "TRACE only the wrong-column query reports a Sort node"
want "$TMPD/$FAM.err" 'key vector on' 1 \
     "TRACE only the sorting query collects a key vector"
want "$TMPD/$FWD.err" 'no materialization on ordered source: order by is a no-op' 1 \
     "TRACE FWD-ONLY ascending is elided"
want "$TMPD/$FWD.err" 'no materialization on ordered source, reversed' 0 \
     "TRACE FWD-ONLY descending is NOT reversed — it cannot be"
want "$TMPD/$FWD.err" 'sort on order by' 1 \
     "TRACE FWD-ONLY descending keeps its Sort node"

if [ "$fails" -ne 0 ]; then
    echo "order_desc_pair_gate: $fails claim(s) failed."
    exit 1
fi
echo "order_desc_pair_gate: OK — backward walk admitted, bounded, narrowed,"
echo "                      refused on the wrong column AND on a source that"
echo "                      cannot retreat, and explained by its own ground."
exit 0
