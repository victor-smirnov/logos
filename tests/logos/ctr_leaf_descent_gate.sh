#!/usr/bin/env bash
# ctr_leaf_descent_gate.sh LOGOSC FIXTURE LIB_DIR EXTRACTOR DESCENT_RE ROWCUR_RE
#
# ⚠ THE LAST TWO ARGUMENTS ARE REQUIRED AND HAVE NO DEFAULT, ON PURPOSE (S1b).
# The gate now runs over TWO container families, and they descend through
# different primitives: the ordered_map's leaves hang off `bt_seek_at` walked by
# `bt_cur_next`, the vector's off `btvec_seek` walked by `btvec_cur_next`. A
# default would let a caller point the gate at a family whose descent edge it
# then fails to find — and an absent edge exits 2 here, which is a red, but a
# default that silently matched the WRONG primitive would exit 0 having counted
# calls that belong to some other code path. The names are therefore passed in,
# once per registration, beside the fixture they describe.
#
# ADR 0025 §5's ASYMPTOTICS, AS AN ASSERTION.
#
# §5 says the leaf-batch producer descends ONCE PER LEAF where the deleted
# per-row walk called a container method PER ROW — "n/fanout descents across a
# full scan instead of n". Every fixture in this tree asserted the ANSWER of a
# scan: the rows, their order, the sums, the batch count. A batch stream that
# re-descended for every row would produce THE SAME ANSWER, and the entire
# argument for deleting the per-row form would have been pinned by nothing. That
# is the defect class here — a silent cost regression behind a correct result —
# and it is invisible to the corpus by construction.
#
# ⚠ NOT A TIMING TEST. This box is shared; a duration measured here is not
# evidence about anything (`feedback_shared_box_timeouts_and_perf`). What is
# asserted is a COUNT of function entries — a deterministic property of the
# program, identical under load — read out of callgrind, which counts them
# without the program knowing it is being counted. No counter was added to the
# stdlib for this: an instrument welded into `advance()` would be a production
# edit on a hot path, and worse, it would be measuring itself.
#
# ⚠ THE DESCENTS ARE SEPARATED BY CALLER, NOT SUMMED. `pass/ctr_leaf_descent_
# count` performs exactly two full traversals — the container's own per-row
# cursor (the oracle) and one leaf-batch scan in §1's shape — so every call to
# `bt_seek_at` (the root-to-leaf descent, ops.logos) belongs to exactly one of
# them, and the two are counted apart. Summing them would let a regression in
# one hide under the other. (`bt_seek_at` is the ordered arm's spelling; the
# vector arm's is `btvec_seek`, which is why the primitive is a PARAMETER — see
# the header, and `pass/ctr_vec_leaf_descent_count` for the second subject.)
#
# THE INDEPENDENT LEAF COUNT. The oracle yields it without asking the batch
# plane anything: a CoW tree has no sibling pointer, so the ROW cursor crosses a
# leaf boundary by descending again, and the number of `bt_cur_next ->
# bt_seek_at` calls inside the row walk IS the container's leaf count. The batch
# plane's `advance -> seek` count must equal it. Two measurements of the same
# structural number, from two code paths that share nothing but the container.
#
# THE CLAIMS (all exact — a tolerance here would absorb exactly the regression
# this exists to catch):
#   1. batch descents  == leaf count            (per leaf …)
#   2. leaf count      == the row walk's own boundary crossings (independent)
#   3. leaf count      == LEAVES declared by the fixture, and >= 3
#   4. row-cursor steps == N declared by the fixture           (… not per row)
#   5. batch pulls     == leaves + 1            (the terminating None, no more)
#   6. TOTAL descents in the whole program == 3*leaves + 3 — the forward batch
#      scan's landing + its per-leaf advances, the oracle's `seek(0)` + its
#      boundary crossings, and the BACKWARD scan's landing + its per-leaf
#      retreats. This is the clause that closes "something else descends": any
#      third route to the descent primitive breaks it — INCLUDING a `land_end`
#      that finds the end by descending instead of by ordinal arithmetic, which
#      was run as a control and reds HERE (28 vs 27 ordered, 40 vs 39
#      positional) and not at clause 9. See clause 9's note.
#
# ── THE BACKWARD CLAIMS (ADR 0025 S3-desc), 7-9 ────────────────────────────
#
# `order by … desc` over a source that declares its order emits no Sort node
# either — it emits a walk that LANDS AT THE END and pulls backward. §3's claim
# for it is the same one as for the forward pull and it is a different piece of
# code: ONE descent per leaf, and the landing is ONE `seek_nth`, NEVER a skip
# loop. Neither half is visible from inside the program: a backward walk that
# re-descended per row, and one that walked forward from the base to position n
# for every row, both return the identical descending sequence.
#
#   7. retreat descents == leaf count           (per leaf, BACKWARD)
#   8. backward pulls   == leaves + 1           (the terminating None)
#   9. landing descents == 2, i.e. ONE per constructed walk and NOT ONE MORE.
#      The CONSTRUCTOR's clause: `#browsfn` descends once to land the walk, and
#      a second constructed walk costs a second, and nothing else may add to
#      this edge.
#
#      ⚠ CORRECTED BY MEASUREMENT, not by argument. This clause was written
#      claiming that a `land_end` which descended to find the end "moves THIS
#      number and only this number". IT DOES NOT, and the abuse was RUN: with
#      both families' `land_end` rewritten to `c.seek(endr-1)` before assigning
#      the fields (same landing, one descent nobody asked for), the measured
#      landing count stayed 2 on both families and the reds came from CLAUSE 6 —
#      total 28 against 27 (ordered, LEAVES=8) and 40 against 39 (positional,
#      LEAVES=12), both exit 1. The reason is attribution: this edge is
#      caller-qualified (`__ctr_brows_` -> seek), and a descent paid inside
#      `land_end` is a DIFFERENT caller, so it lands in the unqualified total
#      and nowhere else. Clause 9 is kept exactly as strict as it was — it is
#      the clause that catches a THIRD constructed walk, and clause 6 is the one
#      that catches a landing that descends. Neither claim is now made by the
#      other's message.
#
# N and LEAVES are READ OUT OF THE FIXTURE, not repeated here — one source of
# truth, so the fixture and the gate cannot drift into disagreeing about which
# container they are describing.
#
# EXIT: 0 clean · 1 a claim failed · 2 the gate could not look (missing tool,
# compile/link failure, the program did not exit 0, or an expected call edge is
# absent — which means inlining moved it, not that the count is fine).
set -euo pipefail

LOGOSC="${1:?logosc path}"
FIXTURE="${2:?fixture .logos}"
LIB_DIR="${3:?stdlib archive dir}"
EXTRACTOR="${4:?callgrind_calls.py path}"
# The DESCENT primitive (root-to-leaf) and the ROW CURSOR's forward step, as
# regexes over the callgrind function names. See the header: no defaults.
DESCENT_RE="${5:?descent fn regex (bt_seek_at | btvec_seek)}"
ROWCUR_RE="${6:?row-cursor step fn regex (bt_cur_next | btvec_cur_next)}"

for f in "$LOGOSC" "$FIXTURE" "$EXTRACTOR"; do
    if [ ! -e "$f" ]; then echo "FAIL(2): missing input: $f"; exit 2; fi
done
if [ ! -d "$LIB_DIR" ]; then echo "FAIL(2): no archive dir: $LIB_DIR"; exit 2; fi
if ! command -v valgrind > /dev/null 2>&1; then
    # NOT skipped. A gate that reports clean because it could not run is the
    # first recorded way a gate lies; valgrind is this gate's instrument and its
    # absence is a red with a name, not a silent pass.
    echo "FAIL(2): valgrind is not installed. This gate MEASURES call counts;"
    echo "         without the instrument it has no verdict. Install valgrind"
    echo "         (Debian/Ubuntu: apt install valgrind) or take this test out"
    echo "         deliberately — do not let it pass by not looking."
    exit 2
fi

# ── the fixture's own constants are the expectation ─────────────────────────
WANT_ROWS=$(sed -n 's/^const N: u64 = \([0-9]*\)u64;.*/\1/p' "$FIXTURE" | head -1)
WANT_LEAVES=$(sed -n 's/^const LEAVES: u64 = \([0-9]*\)u64;.*/\1/p' "$FIXTURE" | head -1)
if [ -z "$WANT_ROWS" ] || [ -z "$WANT_LEAVES" ]; then
    echo "FAIL(2): could not read \`const N\` / \`const LEAVES\` from $FIXTURE."
    echo "         The gate takes its expectation from the fixture; without them"
    echo "         it would be asserting numbers of its own invention."
    exit 2
fi

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

if ! "$LOGOSC" "$FIXTURE" -o "$TMPD/t.o" > "$TMPD/cc.log" 2>&1; then
    echo "FAIL(2): logosc failed on $FIXTURE:"; cat "$TMPD/cc.log"; exit 2
fi
shopt -s nullglob
ARCHIVES=("$LIB_DIR"/liblstdlib*.a "$LIB_DIR"/liblogos-*.a)
for a in "$LIB_DIR"/*.a; do
    case "$(basename "$a")" in
        liblstdlib*|liblogos-*) ;;
        *) ARCHIVES+=("$a") ;;
    esac
done
if [ "${#ARCHIVES[@]}" -lt 1 ]; then
    echo "FAIL(2): no archives in $LIB_DIR"; exit 2
fi
if ! cc "$TMPD/t.o" -Wl,--start-group "${ARCHIVES[@]}" -Wl,--end-group \
        -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition \
        -o "$TMPD/t" > "$TMPD/link.log" 2>&1; then
    echo "FAIL(2): link failed:"; cat "$TMPD/link.log"; exit 2
fi

# The program must be the one the corpus runs — i.e. it must PASS. Counting the
# calls of a program that returned 4 would be measuring a different execution
# from the one the fixture asserts.
set +e
"$TMPD/t" > "$TMPD/prog.out" 2>&1
PROG_RC=$?
set -e
if [ "$PROG_RC" != 0 ]; then
    echo "FAIL(2): $FIXTURE exited $PROG_RC — its own assertions failed, so the"
    echo "         call counts below would describe a different execution."
    cat "$TMPD/prog.out"
    exit 2
fi

if ! valgrind --tool=callgrind --callgrind-out-file="$TMPD/cg.out" \
        "$TMPD/t" > "$TMPD/cg.stdout" 2> "$TMPD/cg.err"; then
    echo "FAIL(2): callgrind run failed:"; tail -20 "$TMPD/cg.err"; exit 2
fi

# ⚠ NOT `python3 … | grep`: the extractor's exit code IS the "I could not look"
# channel (3), and at the head of a pipe it is thrown away.
if ! python3 "$EXTRACTOR" "$TMPD/cg.out" > "$TMPD/edges" 2> "$TMPD/edges.err"; then
    echo "FAIL(2): the call-edge extractor could not read the callgrind file:"
    cat "$TMPD/edges.err"
    exit 2
fi

# ── the edges, by name ──────────────────────────────────────────────────────
# Names are mangled and carry the generated family's hash, so they are matched
# by the parts that are STABLE: the module-qualified stdlib function, and the
# emitted member name after the family hash.
edge() {   # edge CALLER_RE CALLEE_RE  -> summed count, or "" if no such edge
    awk -F'\t' -v ca="$1" -v ce="$2" \
        '$2 ~ ca && $3 ~ ce { n += $1 } END { if (n > 0) print n }' "$TMPD/edges"
}
callee_total() {
    awk -F'\t' -v ce="$1" '$3 ~ ce { n += $1 } END { if (n > 0) print n }' "$TMPD/edges"
}

SEEK_RE='[.]gen[.]Hs[0-9a-f]+__seek__f__'
NEXT_RE='[.]gen[.]Hs[0-9a-f]+__next__f__'

BATCH_DESCENTS=$(edge 'LeafWalk__advance' "$SEEK_RE")
LAND_DESCENTS=$(edge '__ctr_brows_' "$SEEK_RE")
ROW_DESCENTS=$(edge "$ROWCUR_RE" "$DESCENT_RE")
ROW_STEPS=$(callee_total "$NEXT_RE")
BATCH_PULLS=$(callee_total 'LeafWalk__next_batch')
TOTAL_DESCENTS=$(callee_total "$DESCENT_RE")
# ADR 0025 S3-desc — the backward pull's own two edges, counted apart from the
# forward ones for the same reason the forward ones are counted apart from the
# oracle's: summed, a regression in one hides under the other.
REV_DESCENTS=$(edge 'LeafWalk__retreat' "$SEEK_RE")
REV_PULLS=$(callee_total 'LeafWalk__prev_batch')

for pair in "BATCH_DESCENTS:$BATCH_DESCENTS" "LAND_DESCENTS:$LAND_DESCENTS" \
            "ROW_DESCENTS:$ROW_DESCENTS" "ROW_STEPS:$ROW_STEPS" \
            "BATCH_PULLS:$BATCH_PULLS" "TOTAL_DESCENTS:$TOTAL_DESCENTS" \
            "REV_DESCENTS:$REV_DESCENTS" "REV_PULLS:$REV_PULLS"; do
    if [ -z "${pair#*:}" ]; then
        echo "FAIL(2): the call edge for ${pair%%:*} is ABSENT from the profile."
        echo "         An absent edge is not a count of zero — it means the callee"
        echo "         was inlined away or renamed, so this gate cannot decide."
        echo "         Edges seen (top 20):"
        head -20 "$TMPD/edges"
        exit 2
    fi
done

echo "measured: leaves(batch)=$BATCH_DESCENTS leaves(row-oracle)=$ROW_DESCENTS" \
     "rows=$ROW_STEPS batch_pulls=$BATCH_PULLS landing=$LAND_DESCENTS" \
     "rev_descents=$REV_DESCENTS rev_pulls=$REV_PULLS" \
     "total_descents=$TOTAL_DESCENTS  (fixture says N=$WANT_ROWS LEAVES=$WANT_LEAVES)"

fail() { echo "FAIL(1): $1"; exit 1; }

# 1/3 — per leaf, and the leaf count is the one the fixture is about.
[ "$BATCH_DESCENTS" = "$WANT_LEAVES" ] || fail \
    "the batch scan descended $BATCH_DESCENTS times over $WANT_LEAVES leaves —
         ADR 0025 §5 says ONE descent per leaf. $ROW_STEPS would be per ROW."
[ "$WANT_LEAVES" -ge 3 ] 2>/dev/null || fail \
    "the fixture spans $WANT_LEAVES leaves; below 3 the per-leaf claim is vacuous."
# 2 — the independent leaf count agrees.
[ "$ROW_DESCENTS" = "$WANT_LEAVES" ] || fail \
    "the row cursor crossed $ROW_DESCENTS leaf boundaries but the container has
         $WANT_LEAVES leaves — the two independent leaf counts disagree, so one of
         them is not measuring the structure this gate thinks it is."
# 4 — the contrast §5 is actually about.
[ "$ROW_STEPS" = "$WANT_ROWS" ] || fail \
    "the per-row oracle stepped $ROW_STEPS times, expected $WANT_ROWS rows."
[ "$BATCH_DESCENTS" -lt "$ROW_STEPS" ] || fail \
    "the batch scan paid $BATCH_DESCENTS descents for $ROW_STEPS rows — that is
         per row, not per leaf, which is the collapse's whole justification."
# 5 — one pull per leaf plus the terminating None, and nothing else.
EXP_PULLS=$((WANT_LEAVES + 1))
[ "$BATCH_PULLS" = "$EXP_PULLS" ] || fail \
    "the scan pulled $BATCH_PULLS batches; expected $EXP_PULLS (one per leaf plus
         the terminating None)."
# 7 — the BACKWARD pull is per leaf too (ADR 0025 S3-desc).
[ "$REV_DESCENTS" = "$WANT_LEAVES" ] || fail \
    "the backward scan descended $REV_DESCENTS times over $WANT_LEAVES leaves —
         §3 says ONE descent per leaf in this direction as well. $ROW_STEPS would
         be per ROW, and $((WANT_ROWS * WANT_ROWS / 2)) would be the skip loop a
         \`seek_nth\` exists to replace."
[ "$REV_DESCENTS" -lt "$ROW_STEPS" ] || fail \
    "the backward scan paid $REV_DESCENTS descents for $ROW_STEPS rows — that is
         per row, not per leaf."
# 8 — one backward pull per leaf plus the terminating None, and nothing else.
EXP_REV_PULLS=$((WANT_LEAVES + 1))
[ "$REV_PULLS" = "$EXP_REV_PULLS" ] || fail \
    "the backward scan pulled $REV_PULLS batches; expected $EXP_REV_PULLS (one per
         leaf plus the terminating None). \`retreat\` un-consumes rather than
         skips, so a count above this means a leaf was handed out twice."
# 9 — THE `seek_nth` CLAUSE: landing costs one descent per walk, and land_end
# costs none. Two walks are constructed (forward and backward), so 2.
[ "$LAND_DESCENTS" = "2" ] || fail \
    "the two constructed walks cost $LAND_DESCENTS landing descents, expected 2 —
         one each. This edge is CALLER-QUALIFIED (the walk constructor), so a
         third here is a third constructed walk, not a landing that descends: a
         \`land_end\` re-derived as a descent or a skip loop was MEASURED to
         leave this number at 2 and to red clause 6 instead. See the header."
# 6 — no third route descends.
EXP_TOTAL=$((3 * WANT_LEAVES + 3))
[ "$TOTAL_DESCENTS" = "$EXP_TOTAL" ] || fail \
    "the program made $TOTAL_DESCENTS root-to-leaf descents in total; expected
         $EXP_TOTAL = the forward batch scan's landing (1) + its $WANT_LEAVES
         per-leaf advances, the oracle's seek(0) (1) + its $WANT_LEAVES boundary
         crossings, and the backward scan's landing (1) + its $WANT_LEAVES
         per-leaf retreats. A different total means some OTHER path descends, and
         the per-caller numbers above no longer account for the cost."

echo "OK: ADR 0025 §5 — $ROW_STEPS rows scanned in $BATCH_DESCENTS descents"
echo "    ($WANT_LEAVES leaves), against $ROW_STEPS per-row container calls on the"
echo "    oracle side; §3 — the same $WANT_LEAVES descents BACKWARD after a landing"
echo "    that cost none; total descents $TOTAL_DESCENTS = 3*$WANT_LEAVES+3, fully accounted."
exit 0
