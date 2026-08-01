#!/usr/bin/env bash
# plan_independence_gate.sh LOGOSC CORPUS_DIR RUN_TEST [JOBS]
#
# PLAN INDEPENDENCE AS A CORPUS-WIDE METAMORPHIC GATE (ADR 0024 S4r).
#
# THE PROPERTY. The Deem compiler carries several join orders per query and picks
# one at run time, from the data. The ANSWER may not depend on that pick — same
# rows, same SEQUENCE, whichever nest runs. Before this gate the property was
# checked by three hand-written fixtures (`wql_join_order_multi_e2e`,
# `wql_join_order_dyn_e2e`, `wql_join_order_key_fidelity_e2e`), each of which
# writes `order_ix` on a plan it holds. Every other query in the corpus that
# carries more than one order was checked only on the order the data selected.
#
# WHAT THIS DOES INSTEAD. It re-runs THE FIXTURES THAT ALREADY EXIST, unmodified,
# once per carried order, against their own committed `.expected`. The seam is a
# compile-time knob in the emitter (`LOGOS_WQL_FORCE_ORDER=k`, `codegen::
# force_order_ix`) that replaces the nest discriminant with the constant
# `k mod ncand` while leaving the plan object — `order_ix`, `cost_of`, `margin`,
# `agrees` — exactly as it was. So a fixture that asserts the DECISION keeps
# asserting the same decision while the nest beneath it changes; the run is a
# metamorphic transformation of the fixture, not a different test. No new
# fixtures, no second forcing mechanism: `order_ix` written by hand and this knob
# drive the SAME discriminant.
#
# ⚠ WHY A COMPILE-TIME KNOB AND NOT A RUN-TIME ONE. The discriminant is compiled
# in. A caller can write `order_ix` only on a plan it holds, and most of the
# corpus calls the direct fn, which prepares for itself — from outside the
# fixture there is no other seam every carried nest is reachable from.
#
# THE OTHER PLAN AXES, and why they are the same run:
#   • PREPARE-THEN-RUN vs THE DIRECT FN — the direct fn IS `prepare` then `run`,
#     emitted as one body by `emit_prepared_fns`, so agreement is structural. The
#     forced run exercises it: every fixture here calls the direct fn, and the
#     nest it reaches is chosen by this knob rather than by the plan it built.
#   • A STALE PLAN ("pessimal, never wrong") — a stale plan can only make `run`
#     take a nest that is not the argmin for the data in hand. That is exactly
#     the set this gate walks, and it walks ALL of it, including the orders no
#     staleness could ever name (`k` ranges over every carried candidate, not
#     over those some other data would have selected).
#   • THE DEFERRED HALF (decision point 2) — the deferred discriminant
#     `__defer_ix` is forced by the same knob at the same site, and the prelude
#     `len()` reads that bind it are KEPT (`(…) * 0 +`), so what is under test is
#     the nest, not the measurement.
#
# EXCLUSIONS ARE RECORDED ANSWERS. Every fixture lands in exactly one bucket and
# every bucket is printed with its count and its ground. A fixture with no join
# nest, or whose every nest carries one order, is EXCLUDED WITH A REASON — never
# a silent skip. The census that decides the bucket is the emitter's own
# (`LOGOS_WQL_ORDER_CENSUS=1`), so the gate cannot disagree with the compiler
# about how many orders a query carries.
#
# ⚠⚠ AND THE GATE DECLARES THE MINIMUM IT MUST OBSERVE. Every bucket here is
# decided by counting `[wql-order]` lines on the compiler's stderr, so a gate
# that only floors the FIXTURE COUNT is green the moment that channel stops
# speaking: with the census suppressed, every fixture reports zero nests, every
# fixture lands in "no join nest", `MULTI` is empty, `ncase` is 0, and the report
# prints "OK: 0 forced-order runs over 0 fixtures" and exits 0 — having compared
# nothing. Measured: with a wrapper filtering `[wql-order] ` out of stderr the
# gate passed.
#
# ⚠⚠⚠ AND FLOORS WERE NOT ENOUGH, BY MEASUREMENT. With a wrapper that did
# `unset LOGOS_WQL_FORCE_ORDER` before calling logosc, this gate stayed GREEN and
# printed "35 forced-order runs over 12 fixtures reproduced the committed answer"
# — while every "forced" compile emitted the nest the unforced one would have.
# Every floor it had counts FIXTURES, NESTS and RUNS; not one of them observes
# THAT FORCING FORCED ANYTHING. No list of floors ever would have: the list is
# written by the same mind that wrote the seam.
#
# SO THE GATE CARRIES CANARIES — deliberately broken inputs pushed through the
# SAME `force_one`, in the SAME invocation, which it MUST report as failures. If
# one is not caught the gate reports ITSELF broken and exits non-zero.
#
#   CANARY 1 — "FORCING FORCES". Two fixtures in this corpus assert WHICH nest
#     ran and declare so on their first line (see the OBSERVES-CHOICE block
#     below). Forcing a different nest makes those assertions FALSE by
#     construction, and each such run comes back at a DECLARED exit code. So the
#     gate now REQUIRES that every declared fixture trip on at least one forced
#     order. With the forcing seam inert those fixtures PASS — 0 declared trips
#     — and the gate is red naming the canary. That is exactly the mutation that
#     used to slip through. RIDES: the same `LOGOS_WQL_FORCE_ORDER=k` env, the
#     same `run_test.sh pass` invocation, the same `.expected` comparison, the
#     same PASS/DECL/FAIL classification. DOES NOT RIDE: the other ten
#     multi-order fixtures — a seam that forced only in these two shapes would
#     still pass. What it proves is that the knob reaches the emitted
#     discriminant at all, which is the property that was silently absent.
#
#   CANARY 2 — "A DIFFERENT ANSWER WOULD BE SEEN". A copy of a real multi-order
#     fixture whose `.expected` has been mutated is pushed through the same
#     `force_one` and MUST come back FAIL. Without it, a `run_test.sh` that had
#     stopped comparing would make all 35 runs green. RIDES: the compile, the
#     link, the run, run_test.sh's verdict, and this file's classification. DOES
#     NOT RIDE: a stdout diff — every fixture in this set asserts `exit: 0` and
#     keeps its ROW assertions inside the program, returning a distinct code per
#     row block, so the exit code IS the answer channel here. Canary 1 is what
#     shows those in-program assertions really fire (codes 11/12/18/19 come out
#     of them).
#
#   CANARY 3 — "THE CENSUS CHANNEL IS DRIVEN BY THE KNOB". The same census
#     compile is run once with `LOGOS_WQL_ORDER_CENSUS` UNSET and must report
#     ZERO nests, and once with it set and must report more. A gate that cannot
#     tell a blinded census from a clean one has no business floor-checking it.
#     RIDES: `census_one` itself — the same compile, the same `grep -c`.
#
# BLINDING MUTATIONS, RE-RUN against this file after the canaries landed:
#   * a wrapper doing `unset LOGOS_WQL_FORCE_ORDER` before exec'ing logosc —
#     was GREEN, now RED twice by name plus the total:
#       "FAIL (CANARY 'forcing forces' NOT CAUGHT): wql_deferred_plan_e2e
#        declares that it observes WHICH nest ran, and PASSED on all 4 forced
#        orders."  (+ wql_prepared_plan_e2e, + "0 forced runs stopped at a
#        declared choice-observing assertion, floor 6")
#   * a wrapper filtering `[wql-order] ` out of stderr — RED at the census floor
#     ("reported 0 join nests across 127 fixtures (want >= 102)"), with the
#     census-knob canary having already printed that a blinded census reads 0.
set -euo pipefail

# ── FLOORS: MEASURED VALUES, WITH THE MEASUREMENT ────────────────────────────
# Read off THIS gate's own report on 2026-07-31 at `62835ad3` (build clean,
# L4 3119/3119):
#
#   fixtures examined            : 127
#   join nests the census named  : 102
#   carrying > 1 join order      : 12      (forced cases: 35)
#   of those, 6 runs stopped at a DECLARED choice-observing assertion
#     wql_deferred_plan_e2e  orders 0..3 → codes 11,12,11,12
#     wql_prepared_plan_e2e  orders 0,1  → codes 18,19
#
# Floors are those values, not fractions of them. The previous MIN_FIXTURES was
# 100 against a measured 127 — a fifth of the corpus could vanish unremarked.
MIN_FIXTURES=127
MIN_NESTS=102
MIN_MULTI=12
MIN_FORCED=35
# CANARY 1's floor. `MIN_DECL_FIXTURES` is how many fixtures declare that they
# observe the choice; `MIN_DECL` is how many forced runs actually tripped one.
# Both must hold, and additionally EVERY declared fixture must trip at least
# once — a total alone would let one of the two go quiet behind the other.
MIN_DECL_FIXTURES=2
MIN_DECL=6
# CANARY 2's carrier: a real multi-order fixture, copied and given a WRONG
# `.expected`. Named rather than picked, so the gate says what it broke.
CANARY_FIXTURE=wql_join_order_multi_e2e

LOGOSC="$1"
CORPUS="$2"
RUN_TEST="$3"
JOBS="${4:-12}"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

# The one fixture in this corpus that links a local archive (ADR 0016
# cross-module mapping fusion). Derived from the same list `logos_pass_extra_args`
# keeps; a fixture needing flags this gate does not know about would fail its
# BASELINE compile and be reported, never skipped.
WQL_MAP_LIB="${LOGOS_TEST_LIB_DIR:-}/libwql_map_lib.a"
PUB_LIB="${LOGOS_TEST_LIB_DIR:-}/libpub_lib.a"

extra_args() {
    case "$1" in
        wql_mapping_cross_module_e2e) printf '%s\n' -l "$WQL_MAP_LIB" ;;
        wql_wref_field_pkg)           printf '%s\n' -l "$PUB_LIB" ;;
        *) : ;;
    esac
}

# ── Phase 1: the emitter's own census of carried orders, one compile each ────
shopt -s nullglob
FIXTURES=()
for f in "$CORPUS"/deem_*.logos "$CORPUS"/wql_*.logos; do
    b=$(basename "$f" .logos)
    [ -f "$CORPUS/$b.expected" ] || continue
    FIXTURES+=("$b")
done
if [ "${#FIXTURES[@]}" -lt "$MIN_FIXTURES" ]; then
    echo "FAIL: only ${#FIXTURES[@]} deem/wql fixtures found under $CORPUS (want >= $MIN_FIXTURES) — the corpus glob is wrong"
    exit 1
fi

census_one() {
    b="$1"
    # CANARY 3 runs the SAME function with the knob blinded; `CENSUS_KNOB` is the
    # only difference, so the compile, the parse and the counting are identical.
    mapfile -t ex < <(extra_args "$b")
    if ! env ${CENSUS_KNOB:+LOGOS_WQL_ORDER_CENSUS=1} \
            "$LOGOSC" "$CORPUS/$b.logos" -o "$TMPD/$b.census.o" \
            "${ex[@]}" 2>"$TMPD/$b.census.err"; then
        printf '%s ERR 0 0\n' "$b"
        return 0
    fi
    # `[wql-order] <query> ncand=<n> deferred=<0|1>`, one line per join nest.
    n=$(grep -c '^\[wql-order\] ' "$TMPD/$b.census.err" || true)
    mx=$(sed -n 's/^\[wql-order\] .* ncand=\([0-9]*\) .*/\1/p' "$TMPD/$b.census.err" \
         | sort -n | tail -1)
    [ -n "$mx" ] || mx=0
    df=$(grep -c 'deferred=1' "$TMPD/$b.census.err" || true)
    printf '%s OK %s %s %s\n' "$b" "$n" "$mx" "$df"
}
export -f census_one
export LOGOSC CORPUS TMPD WQL_MAP_LIB PUB_LIB
export -f extra_args

# ── CANARY 3, FIRST: the census channel is DRIVEN BY THE KNOB ────────────────
# The same `census_one`, on a fixture known to carry nests, with the knob unset.
# It must report ZERO. If it reports nests anyway the channel is unconditional
# and the "blinded census" failure this gate floors for cannot be produced; if
# the SET run below reports zero too, the floor catches it. Between them the gate
# can tell a blind census from a clean one, which a floor alone cannot.
CENSUS_KNOB=
export CENSUS_KNOB
read -r _ blind_st blind_n _ _ < <(census_one "$CANARY_FIXTURE")
if [ "${blind_st:-ERR}" != "OK" ]; then
    echo "FAIL: $CANARY_FIXTURE does not compile with LOGOS_WQL_ORDER_CENSUS unset —"
    echo "      the canary cannot be read, so the census floor is unproven."
    exit 1
fi
if [ "${blind_n:-0}" -ne 0 ]; then
    echo "FAIL (CANARY 'census knob' NOT CAUGHT): with LOGOS_WQL_ORDER_CENSUS unset,"
    echo "      $CANARY_FIXTURE still reported $blind_n '[wql-order]' lines. The"
    echo "      channel is not the knob's, so this gate's census floor is not"
    echo "      measuring what it says. GATE BROKEN, not the tree."
    exit 1
fi
CENSUS_KNOB=1
export CENSUS_KNOB
echo "[plan-indep] canary 'census knob': caught — with LOGOS_WQL_ORDER_CENSUS unset"
echo "             $CANARY_FIXTURE reports 0 nests, so a blinded census is"
echo "             distinguishable from a clean one and the floor below is real"

printf '%s\n' "${FIXTURES[@]}" | xargs -P "$JOBS" -I{} bash -c 'census_one "$@"' _ {} \
    > "$TMPD/census.txt"

if [ "$(wc -l < "$TMPD/census.txt")" -ne "${#FIXTURES[@]}" ]; then
    echo "FAIL: census produced $(wc -l < "$TMPD/census.txt") lines for ${#FIXTURES[@]} fixtures"
    exit 1
fi

fail=0

# ── Phase 2: buckets ────────────────────────────────────────────────────────
EX_COMPILE=(); EX_NONEST=(); EX_ONE=(); MULTI=()
declare -A NCAND=(); declare -A NNEST=(); declare -A NDEFER=()
TOT_NESTS=0
while read -r b st n mx df; do
    case "$st" in
        ERR) EX_COMPILE+=("$b"); continue ;;
    esac
    NNEST["$b"]=$n; NCAND["$b"]=$mx; NDEFER["$b"]=$df
    TOT_NESTS=$((TOT_NESTS + n))
    if [ "$n" -eq 0 ];  then EX_NONEST+=("$b"); continue; fi
    if [ "$mx" -le 1 ]; then EX_ONE+=("$b");    continue; fi
    MULTI+=("$b")
done < "$TMPD/census.txt"

# ⚠ THE CENSUS CHANNEL MUST HAVE SPOKEN. Every bucket above is a count of
# `[wql-order]` lines; if the emitter stops printing them the buckets are all
# "no join nest" and the gate has nothing to compare, which is not the same fact
# as "nothing needs comparing".
if [ "$TOT_NESTS" -lt "$MIN_NESTS" ]; then
    echo "FAIL: the emitter's census reported $TOT_NESTS join nests across ${#FIXTURES[@]} fixtures (want >= $MIN_NESTS)."
    echo "      Either LOGOS_WQL_ORDER_CENSUS stopped printing '[wql-order]' lines, or the"
    echo "      corpus lost its join queries. This gate buckets on those lines and cannot"
    echo "      tell 'no nest carries a choice' from 'nobody told me about the nests'."
    fail=1
fi

# A fixture that does not compile under the census env is a FINDING: the knob is
# supposed to be inert when it only prints.
if [ "${#EX_COMPILE[@]}" -gt 0 ]; then
    echo "FAIL: ${#EX_COMPILE[@]} fixture(s) failed to compile under LOGOS_WQL_ORDER_CENSUS=1:"
    printf '  %s\n' "${EX_COMPILE[@]}"
    fail=1
fi

# ── Phase 3: force every carried order, assert the fixture's own .expected ──
# ── A FIXTURE MAY OBSERVE THE CHOICE, and then it must SAY SO ───────────────
#
# Two fixtures in this corpus read a pull log and assert WHICH nest ran
# (`wql_prepared_plan_e2e`, `wql_deferred_plan_e2e`) — that is their whole point,
# and forcing a nest makes those assertions false by construction. A skip list
# would hide them. What is required instead is a DECLARATION in the fixture, on
# its own first lines, next to the assertions that make it necessary:
#
#   // PLAN-INDEPENDENCE: OBSERVES-CHOICE codes=18,19,24,25,36,38 — <ground>
#
# and the gate then demands that the forced run exit at 0 or at one of THOSE
# codes. That is strictly stronger than an exclusion: a fixture whose exit code
# is its verdict has already run every assertion above the one that tripped, so
# the declaration pins that its ROW block survived the forced nest, and any other
# code — including a row mismatch — is a gate failure. A fixture that lands in
# this position without a declaration is a FAILURE, so the set cannot grow
# silently.
declared_codes() {
    sed -n 's/^\/\/ PLAN-INDEPENDENCE: OBSERVES-CHOICE codes=\([0-9,]*\).*/\1/p' \
        "$1/$2.logos" | head -1
}

# ⚠ ONE function runs the real cases AND both canaries. The source directory is
# part of the job so a canary copy can be pushed through it unchanged: same env,
# same `run_test.sh pass`, same `.expected` comparison, same classification. A
# canary judged by a different mechanism than the real work proves nothing about
# the real work.
force_one() {
    IFS=' ' read -r dir b k <<< "$1"
    mapfile -t ex < <(extra_args "$b")
    if LOGOS_WQL_FORCE_ORDER="$k" "$RUN_TEST" pass "$LOGOSC" \
            "$dir/$b.logos" "$dir/$b.expected" "${ex[@]}" > "$TMPD/$b.$k.out" 2>&1; then
        printf 'PASS %s %s\n' "$b" "$k"
        return 0
    fi
    codes=$(declared_codes "$dir" "$b")
    if [ -n "$codes" ]; then
        # The run_test.sh verdict line is `FAIL: exit code N (expected M)`. Anything
        # else — a compile failure, a link failure, a stdout mismatch — is not an
        # exit-code verdict and is never covered by a declaration.
        got=$(sed -n 's/^FAIL: exit code \([0-9]*\) (expected .*/\1/p' "$TMPD/$b.$k.out" | head -1)
        if [ -n "$got" ] && [[ ",$codes," == *",$got,"* ]]; then
            printf 'DECL %s %s %s\n' "$b" "$k" "$got"
            return 0
        fi
        printf 'FAIL %s %s\n' "$b" "$k"
        return 0
    fi
    printf 'FAIL %s %s\n' "$b" "$k"
}
export -f declared_codes
export -f force_one
export RUN_TEST

JOBLIST="$TMPD/jobs.txt"
: > "$JOBLIST"
ncase=0
for b in "${MULTI[@]}"; do
    k=0
    while [ "$k" -lt "${NCAND[$b]}" ]; do
        printf '%s %s %s\n' "$CORPUS" "$b" "$k" >> "$JOBLIST"
        k=$((k + 1))
        ncase=$((ncase + 1))
    done
done

if [ "$ncase" -gt 0 ]; then
    xargs -P "$JOBS" -I{} bash -c 'force_one "$@"' _ {} < "$JOBLIST" > "$TMPD/forced.txt"
else
    : > "$TMPD/forced.txt"
fi

if [ "$(wc -l < "$TMPD/forced.txt")" -ne "$ncase" ]; then
    echo "FAIL: $ncase forced cases dispatched, $(wc -l < "$TMPD/forced.txt") results came back"
    fail=1
fi

# ── CANARY 2: A DIFFERENT ANSWER WOULD BE SEEN ──────────────────────────────
# A real multi-order fixture, copied verbatim, with its `.expected` given an exit
# code the program does not return — the fixture keeps its ROW assertions inside
# itself and surfaces them as exit codes, so this IS the answer channel. Pushed
# through `force_one` exactly as the 35 real cases are; it must come back FAIL.
# If it comes back PASS then `run_test.sh` is not comparing and all 35 greens
# above are the harness agreeing with itself.
CANDIR="$TMPD/canary"
mkdir -p "$CANDIR"
if [ ! -f "$CORPUS/$CANARY_FIXTURE.logos" ] || [ ! -f "$CORPUS/$CANARY_FIXTURE.expected" ]; then
    echo "FAIL: canary carrier $CANARY_FIXTURE is not in the corpus — the gate"
    echo "      cannot prove its comparison live."
    exit 1
fi
CANARY_NAME="${CANARY_FIXTURE}_expected_canary"
cp "$CORPUS/$CANARY_FIXTURE.logos" "$CANDIR/$CANARY_NAME.logos"
# 91 is not a code any fixture here returns, and the declaration parser must find
# nothing in the copy — the canary is judged FAIL, never DECL.
sed 's/^exit: .*/exit: 91/' "$CORPUS/$CANARY_FIXTURE.expected" > "$CANDIR/$CANARY_NAME.expected"
if ! grep -q '^exit: 91$' "$CANDIR/$CANARY_NAME.expected"; then
    echo "FAIL: could not build the canary expectation from"
    echo "      $CORPUS/$CANARY_FIXTURE.expected — the mutation did not apply."
    exit 1
fi
canary2=$(force_one "$CANDIR $CANARY_NAME 0")
if [ "${canary2%% *}" != "FAIL" ]; then
    echo "FAIL (CANARY 'wrong answer' NOT CAUGHT): $CANARY_FIXTURE was run against"
    echo "      an .expected asserting exit 91 and came back '${canary2}'. The"
    echo "      comparison every forced run is judged by is not comparing, so"
    echo "      'reproduced the committed answer' is empty. GATE BROKEN."
    sed -n '1,20p' "$TMPD/$CANARY_NAME.0.out"
    exit 1
fi
echo "[plan-indep] canary 'a mutated .expected is seen': caught (FAIL, as required)"

# ⚠ AND THE COMPARISONS MUST ACTUALLY HAVE BEEN MADE. "0 of 0 forced runs
# disagreed" is not a green gate, it is a gate that ran nothing; the two floors
# below are what separate them. They are the recorded baseline, so a corpus that
# loses its multi-order queries — or an emitter that stops carrying a second
# nest — is red and says which of the two it could not see.
if [ "${#MULTI[@]}" -lt "$MIN_MULTI" ]; then
    echo "FAIL: only ${#MULTI[@]} fixture(s) carry more than one join order (want >= $MIN_MULTI)."
    echo "      The property under test is 'the answer does not depend on the pick'; with"
    echo "      fewer carriers than the recorded baseline there is less of it being tested"
    echo "      than the day this floor was written."
    fail=1
fi
if [ "$ncase" -lt "$MIN_FORCED" ]; then
    echo "FAIL: $ncase forced-order runs were dispatched (want >= $MIN_FORCED) — the gate"
    echo "      compared fewer answers than its recorded baseline."
    fail=1
fi

# ── CANARY 1: FORCING FORCED SOMETHING ──────────────────────────────────────
# ⚠ THIS IS THE ASSERTION THE GATE DID NOT HAVE. Every number above counts
# fixtures, nests or runs; a forcing knob that silently did nothing produced all
# of them unchanged, and the gate printed "reproduced the committed answer on
# every carried order" about 35 compiles of the SAME nest. Measured: with a
# wrapper doing `unset LOGOS_WQL_FORCE_ORDER`, green.
#
# The declared fixtures are the corpus's own witnesses that a nest CHANGED: they
# read a pull log and assert which one ran, so a genuinely forced order makes
# them stop at a declared code. If forcing is inert they PASS, and every one of
# the three checks below goes red. They are judged by the same `force_one` — the
# same env, the same `run_test.sh pass`, the same `.expected`, the same
# classification — as the 35 real cases; only the expectation is inverted.
DECLARED=()
for b in "${MULTI[@]}"; do
    if [ -n "$(declared_codes "$CORPUS" "$b")" ]; then DECLARED+=("$b"); fi
done
ndecl=$(grep -c '^DECL ' "$TMPD/forced.txt" || true)
if [ "${#DECLARED[@]}" -lt "$MIN_DECL_FIXTURES" ]; then
    echo "FAIL (CANARY 'forcing forces' UNAVAILABLE): only ${#DECLARED[@]} multi-order"
    echo "      fixture(s) declare OBSERVES-CHOICE (want >= $MIN_DECL_FIXTURES). Those"
    echo "      declarations are the only witnesses in this corpus that a forced"
    echo "      nest is a DIFFERENT nest. Without them the gate cannot prove the"
    echo "      forcing seam is live and its 35 green runs mean nothing."
    fail=1
fi
for b in "${DECLARED[@]}"; do
    n=$(grep -c "^DECL $b " "$TMPD/forced.txt" || true)
    if [ "$n" -lt 1 ]; then
        echo "FAIL (CANARY 'forcing forces' NOT CAUGHT): $b declares that it observes"
        echo "      WHICH nest ran, and PASSED on all ${NCAND[$b]} forced orders. Either"
        echo "      LOGOS_WQL_FORCE_ORDER no longer reaches the emitted discriminant —"
        echo "      in which case every forced run above compiled the same nest and"
        echo "      this gate is measuring nothing — or the fixture stopped observing"
        echo "      the choice and must drop its declaration. GATE BROKEN, not the tree."
        fail=1
    fi
done
if [ "$ndecl" -lt "$MIN_DECL" ]; then
    echo "FAIL (CANARY 'forcing forces'): $ndecl forced runs stopped at a declared"
    echo "      choice-observing assertion, floor $MIN_DECL (MEASURED 2026-07-31 at"
    echo "      62835ad3: 4 from wql_deferred_plan_e2e, 2 from wql_prepared_plan_e2e)."
    echo "      Fewer trips means the knob is reaching fewer nests than it did."
    fail=1
fi

# ── The report ──────────────────────────────────────────────────────────────
echo "── plan independence, corpus-wide ─────────────────────────────────────"
echo "fixtures examined            : ${#FIXTURES[@]}   (floor $MIN_FIXTURES)"
echo "join nests the census named  : $TOT_NESTS   (floor $MIN_NESTS)"
echo "carrying > 1 join order      : ${#MULTI[@]}   (floor $MIN_MULTI; forced cases: $ncase, floor $MIN_FORCED)"
echo "EXCLUDED — no join nest      : ${#EX_NONEST[@]}   (single-source query, or no deem query at all: nothing to reorder)"
echo "EXCLUDED — one order carried : ${#EX_ONE[@]}   (the derivation refused every permutation, or proved none cheaper: one nest is emitted, so there is no second answer to compare)"
echo "EXCLUDED — did not compile   : ${#EX_COMPILE[@]}"
echo

if [ "${#MULTI[@]}" -gt 0 ]; then
    echo "multi-order fixtures (name / nests / max candidates / deferred nests):"
    for b in "${MULTI[@]}"; do
        printf '  %-46s %2s %2s %2s\n' "$b" "${NNEST[$b]}" "${NCAND[$b]}" "${NDEFER[$b]}"
    done
    echo
fi

# ⚠ THE EXCLUDED SET IS PRINTED IN FULL, not counted. An excluded case is a
# recorded answer, and a reader has to be able to see WHICH fixture went into
# which bucket without re-running the gate.
if [ "${#EX_ONE[@]}" -gt 0 ]; then
    echo "excluded — one order carried:"
    printf '  %s\n' "${EX_ONE[@]}" | paste -sd' ' - | fold -s -w 100 | sed 's/^/  /'
    echo
fi
if [ "${#EX_NONEST[@]}" -gt 0 ]; then
    echo "excluded — no join nest:"
    printf '  %s\n' "${EX_NONEST[@]}" | paste -sd' ' - | fold -s -w 100 | sed 's/^/  /'
    echo
fi

nfail=$(grep -c '^FAIL ' "$TMPD/forced.txt" || true)
if [ "$nfail" -gt 0 ]; then
    echo "FAIL: $nfail of $ncase forced-order runs did not reproduce the fixture's own answer."
    echo "      The plan's choice changed the ANSWER — either the reorder was licensed"
    echo "      where it is not sound, or a nest is wrong."
    grep '^FAIL ' "$TMPD/forced.txt" | while read -r _ b k; do
        echo "  ── $b, forced order $k ──"
        sed 's/^/     /' "$TMPD/$b.$k.out" | head -20
    done
    fail=1
else
    echo "OK: $ncase forced-order runs over ${#MULTI[@]} fixtures reproduced the committed"
    echo "    answer — rows, sequence and exit code — on every carried order."
    if [ "$ndecl" -gt 0 ]; then
        echo
        echo "    CANARY 'forcing forces' CAUGHT: $ndecl runs stopped at a DECLARED"
        echo "    choice-observing assertion (floor $MIN_DECL over ${#DECLARED[@]} declaring"
        echo "    fixtures, each of which had to trip at least once). That is the"
        echo "    proof the knob moved the nest: with the seam inert these PASS."
        echo "    The fixture reads a pull log and asserts which nest ran; every"
        echo "    assertion above it, including its row block, held on the forced nest:"
        grep '^DECL ' "$TMPD/forced.txt" | sort | while read -r _ b k c; do
            printf '      %-34s order %s → code %s\n' "$b" "$k" "$c"
        done
    fi
fi

exit $fail  # lint:exit-ok — `fail` is set only to the literals 0 and 1
