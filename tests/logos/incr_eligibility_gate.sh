#!/usr/bin/env bash
# incr_eligibility_gate.sh LOGOSC TEST_LOGOS
#
# THE POPULATION OF INCREMENTAL QUERIES IS DERIVED, AND THIS IS WHAT MAKES THAT
# CHECKABLE.
#
# `deem!` emits the incremental handle UNCONDITIONALLY-WHEN-ELIGIBLE. Nothing in
# the surface asks for it — the `deem` item head has no attribute slot and the
# query DSL has no modifier slot, so every opt-in spelling would be a grammar
# change — so the set of queries that GET a handle is a derivation, and a
# derivation nobody can read is a list nobody wrote down.
#
# The companion fixture (`pass/wql_incr_eligibility_matrix.logos`) pins the
# POSITIVE direction on its own: the eligible query's three derived fns are
# called there, so an emitter that stopped emitting them makes it fail to
# compile. It cannot pin the NEGATIVE direction, because a query whose handle is
# absent is a query with nothing to name. This gate reads the decision itself off
# `LOGOS_TRACE_PLAN` and asserts, per query:
#
#   • the VERDICT (EMITTED / declined), and
#   • that the GROUND names the antecedent that actually failed — not merely that
#     something failed. A refusal whose ground is generic is the one that never
#     gets revisited, and this project has already paid for a justification that
#     drifted from its mechanism.
#
# ⚠ AND THE POPULATION IS PINNED BY COUNT, NOT ONLY BY MEMBERSHIP. Checking that
# each expected query appears is an EXISTENTIAL over the trace and is satisfied by
# a trace with extra lines in it — but worse, it is satisfied when a query stops
# being walked at all only if that query is not in the list. So the total number
# of `[plan] incremental ->` lines is asserted too: a query that vanishes from the
# walk, or an eighth that appears from nowhere, is caught. That is the "predict
# the COUNT" rule this corpus enforces on its own suites.
#
# ⚠⚠ AND A GATE WHOSE VERDICT IS SILENCE MUST PROVE IT CAN SPEAK. Every check
# below is a `grep` over a trace; a renamed query, a reworded ground, a changed
# trace prefix and an empty capture all deliver the same "no violation found" as a
# genuinely clean run. So the same matcher is run, in the same invocation, over a
# CANARY trace broken four ways, and every one of the four must be named back. If
# the canary passes, this gate is not measuring anything and it fails.
set -uo pipefail

LOGOSC="$1"
TEST_LOGOS="$2"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

TRACE="$TMPD/trace.txt"
LOGOS_TRACE_PLAN=1 "$LOGOSC" "$TEST_LOGOS" -o "$TMPD/out.o" 2>"$TMPD/all.err" >/dev/null
RC=$?
if [ "$RC" != 0 ]; then
    echo "FAIL: the fixture must COMPILE — declining the incremental form is not a"
    echo "      refusal of the query, and if it ever became one this is where it shows."
    echo "      logosc exit $RC:"
    head -40 "$TMPD/all.err"
    exit 1
fi
grep -F '[plan] incremental ->' "$TMPD/all.err" > "$TRACE"
RTRACE="$TMPD/rtrace.txt"
grep -F '[plan] retraction ->' "$TMPD/all.err" > "$RTRACE"

# ── the expected table: query, verdict, a phrase its GROUND must contain ─────
# The phrase is the ANTECEDENT, chosen so that a ground reworded into a generic
# "not supported" stops matching. Deliberately not the whole sentence: this gate
# pins what the ground is ABOUT, and `wql_incr_eligibility_matrix.logos` carries
# the prose.
EXPECT=(
  "ok_basic|EMITTED|one bare scan"
  "no_retract_avg|EMITTED|one bare scan"
  "ok_join|EMITTED|one join step over two owned relations"
  "no_join_strrow|declined|ROW TYPE is not OWNABLE AND IDENTIFIABLE"
  # ⚠ A PIN TRANSCRIBED OFF A DYING FIXTURE, NOT A NEW SHAPE.
  # `pass/wql_domain_incr_disagreement` block 3b holds the one live `KNOWN-WRONG`
  # of that family: an equi-join on an `f64` key is ACCEPTED by two tiers and
  # REFUSED by the incremental one. Both accepting halves there run through
  # `Query::compile` — the DYNAMIC interpreter — so P5's deletion takes away the
  # side that made the refusal a DISAGREEMENT rather than a policy. The surviving
  # accepting tier is the STATIC batch fn, which the companion fixture calls and
  # asserts the answer of; this row is the refusing side.
  #
  # ⚠ THE CONTROL IS `ok_join`, WHICH IS THE SAME SHAPE AND IS EMITTED — two owned
  # relations, one step, `group by` + `sum`. The two rows differ in the KEY'S TYPE
  # and in nothing else, which is what makes this row a measurement.
  #
  # ⚠ IT FAILS-IF-FIXED, AND THAT IS WHY IT IS TRANSCRIBED RATHER THAN DROPPED.
  # The day the incremental tier admits an f64 join key, this query traces
  # EMITTED, the verdict stops matching and this gate goes red — and so does the
  # DERIVED retract count, because a new EMITTED row is a new retract question.
  # A red here from a FIX is the pin doing its job: rewrite the row, do not
  # delete it.
  "no_join_f64key|declined|ROW TYPE is not OWNABLE AND IDENTIFIABLE"
  "no_join2|declined|TWO OR MORE STEPS"
  "no_join_avg|declined|INSERT-ONLY surface"
  "no_join_self|declined|SELF-JOIN"
  "no_where|declined|PRE-GROUP \`where\`"
  "no_order|declined|\`order by\`"
  "no_limit|declined|\`order by\`"
  "no_strkey|declined|GROUP KEY is \`str\`"
  "no_rowsel|declined|\`select\` reaches a name"
  # ⚠⚠ THIS GROUP IS NOT LIKE THE OTHERS. Every row above withholds a FEATURE;
  # the rel clause withheld a handle that EXISTED AND ANSWERED WRONG. Measured on
  # aaf16585 against each query's own batch fn: the recursive rel's handle said
  # group 1 had count 1 where the batch said 2 (it never derived the transitive
  # edge, because each epoch re-ran the fixpoint over the DELTA ALONE), and the
  # one-shot rel's handle said 2 where the batch said 1 (a rel is a SET and
  # dedups, and dedup is a property of the ACCUMULATED input). Both returned
  # `Ok` and both traced EMITTED.
  #
  # P3b answers the FIRST: `ok_rel_rec` is EMITTED again, and the ground says why
  # it is now sound — the handle owns the accumulated input and the fixpoint
  # TOTAL, and each epoch EXTENDS that total. ⚠ THE PHRASE ASSERTED IS
  # "RECURSIVE \`rel\`", not "one bare scan": if this query ever fell back onto
  # the plain bare-scan ground it would mean the rel arm stopped being taken
  # while the verdict stayed EMITTED, which is exactly the aaf16585 defect
  # wearing the right answer's clothes.
  #
  # The SECOND is still refused and `no_rel_oneshot` still pins it — and its
  # ground is now the SPECIFIC antecedent (NON-RECURSIVE) rather than "a source
  # is a rel", so a future slice that admits it cannot leave this row matching by
  # accident. `no_rel_join` pins the JOIN-POSITION clause, which until now was
  # pinned by nothing at all: both rel queries sat in BASE position, so that loop
  # could be deleted with every gate green.
  "ok_rel_rec|EMITTED|RECURSIVE \`rel\`"
  "no_rel_oneshot|declined|NON-RECURSIVE declared \`rel\`"
  "no_rel_join|declined|JOIN-POSITION source is a DECLARED \`rel\`"
  # ⚠ THE GROUND, NOT MERELY "some refusal". `fail/wql_incr_mutrec_outside_rel`
  # asserts only that a name is not found, and that is true under EVERY refusal
  # reaching the query — MEASURED: restoring P3b's `nmem != 1`, which withdraws
  # the whole capability, leaves that door GREEN. This row is the one that fails
  # if the clause stops being the OUTSIDE-SCC clause.
  "no_rel_outside|declined|OUTSIDE this recursive SCC"
  # ⚠ THE AGGREGATE-HEADED SCC, AND THE THIRD OF `stamp_rel_incr_shape`'s SIX
  # ANTECEDENTS TO GET A SENSOR. Until this row, `any_agg` had been READ and
  # never exercised: deleting the clause admits an aggregate-headed SCC and emits
  # a driver calling `_od`/`_odp`/`_cpt` that `emit_scc_od_fns` never emitted for
  # it, and NOTHING in the corpus went red. ⚠ CALIBRATED TWICE, BECAUSE THE
  # OBVIOUS CONTROL DOES NOT EXERCISE THIS ROW: deleting the `else if any_agg`
  # arm and rebuilding makes the FIXTURE FAIL TO COMPILE (`undefined variable
  # '__rel_dist_sl'` in `__wql_no_rel_agg_scc0_i`), so the gate reds at its
  # "must COMPILE" door and never reaches the matcher. The matcher itself was
  # calibrated by rewording the ground to "not supported yet", which answered
  # "VIOLATION: the ground for 'no_rel_agg' does not name 'AGGREGATE rel'" with
  # no other row moving. Both controls reverted, tree re-measured green.
  #
  # The phrase is "AGGREGATE rel" and not "min/max": the antecedent is that a
  # MEMBER OF THE SCC has a semilattice head whose total is MATERIALIZED after
  # the loop, so a ground reworded down to the aggregate's flavour would stop
  # naming the thing the `_i` variant cannot seed.
  #
  # ⚠ AND THE OTHER THREE UNPINNED ANTECEDENTS GET NO ROW HERE BECAUSE NO
  # COMPILING PROGRAM REACHES THEM — `mask_u != 0i64` requires a rel outside the
  # SCC and `nmem != prm.rel_n` refuses first; a native or streaming rel emits no
  # dep edge and cannot sit on a cycle. The derivation is recorded beside
  # `no_rel_agg` in the fixture; what pins them is the chain's ORDER, and a
  # reorder is what would make them observable.
  "no_rel_agg|declined|AGGREGATE rel"
)

# ── THE SECOND AXIS: may the handle be run BACKWARDS? ───────────────────────
# `retraction` is asked ONLY of a query that already has a handle, so this table
# is a SUBSET of the one above and its count is the number of EMITTED rows there
# — which is why both counts are asserted rather than one. The ground for the
# decline must name the FORK, not "unsupported": `<q>_retract` for an f64
# `sum`/`avg` would take an open decision ((S+x)−x != S) by accident, and a
# refusal whose ground has been reworded into a generic one is a refusal nobody
# will revisit when the fork closes.
#
# ⚠ THE AXIS NAME IS `retraction` AND IT IS THE SAME CHANNEL
# `incr_retraction_gate.sh` READS. Two names for one decision means one of the
# two gates greps a channel that does not exist, sees zero lines, and reports
# nothing while looking green.
#
# ⚠ AND `no_join_avg` IS *NOT* IN THIS TABLE THOUGH ITS NAME LOOKS LIKE
# `no_retract_avg`'s. The retraction axis is asked only of a query that HAS a
# handle, and `no_join_avg` has none — it is declined ON THE FIRST AXIS, with the
# ground that a join delta must name its source and the tag lives on
# `<q>_apply`, which only the retracting surface emits. The order of the two
# questions is what makes that a single verdict rather than two, and the derived
# count below is what would catch it becoming two.
#
# ⚠ `ok_rel_rec` IS ON THE FIRST LIST AND OFF THIS ONE, WITH A GROUND THAT IS A
# FACT RATHER THAN AN OPEN DECISION. A derived fact may support ITSELF through a
# cycle, so "subtract what it added" names no amount; the mechanism that works is
# DRed and it is a provenance state machine, not an inverse fold. The refusal is
# also STRUCTURAL — it selects the insert-only surface, so `<q>_retract` is not
# emitted and `fail/wql_incr_rel_no_retract.logos` requires the compile to fail on
# the missing name. Two gates, two directions: this one catches the ground being
# reworded, that one catches the fn being emitted anyway.
REXPECT=(
  "ok_basic|EMITTED|invert exactly"
  "ok_join|EMITTED|invert exactly"
  "no_retract_avg|declined|(S+x)-x != S"
  "ok_rel_rec|declined|support ITSELF through a cycle"
)

# Check one expected row against a trace file. Echoes one violation line per
# failure and nothing when the row holds — so the SAME function serves the real
# trace and the canary, which is what makes the canary meaningful.
check_rows() {
    local file="$1"; shift
    local row name verdict phrase line
    for row in "$@"; do
        name="${row%%|*}"
        rest="${row#*|}"
        verdict="${rest%%|*}"
        phrase="${rest#*|}"
        line=$(grep -F -- " -> ${verdict} on ${name}   (" "$file")
        if [ -z "$line" ]; then
            echo "VIOLATION: no '${verdict}' verdict traced for query '${name}'"
            continue
        fi
        # ⚠ INTO A FILE, THEN MATCH IT — never `printf … | grep -q`. Under
        # `set -o pipefail` grep exits at the first match, the writer takes
        # SIGPIPE 141, and pipefail reports the MATCH as a failed pipeline. That
        # is a recorded lesson in this corpus and `logos_00_gate_lint` caught
        # this very line writing it again.
        printf '%s' "$line" > "$file.line"
        if ! grep -qF -- "$phrase" "$file.line"; then
            echo "VIOLATION: the ground for '${name}' does not name '${phrase}': ${line}"
        fi
    done
}

VIOL=$(check_rows "$TRACE" "${EXPECT[@]}")
RVIOL=$(check_rows "$RTRACE" "${REXPECT[@]}")
VIOL="${VIOL}${RVIOL}"

# ── the POPULATION, by count ────────────────────────────────────────────────
WANT_N=${#EXPECT[@]}
GOT_N=$(grep -c -F '[plan] incremental ->' "$TRACE")
if [ "$GOT_N" != "$WANT_N" ]; then
    VIOL="${VIOL}
VIOLATION: the walk reported ${GOT_N} incremental decisions, expected ${WANT_N} —
           a query that stops being walked leaves no trace line and no failure
           anywhere else in the corpus."
fi
# ── and the RETRACT population, by count, DERIVED from the first table ──────
# It is not a second hand-written number: the retract axis is asked exactly once
# per query the first axis EMITTED, so the expected count is COMPUTED from
# EXPECT. A query that gains a handle and is then never asked the second question
# — or one that is asked it without having a handle — moves these two apart.
WANT_R=0
for row in "${EXPECT[@]}"; do
    case "$row" in *"|EMITTED|"*) WANT_R=$((WANT_R + 1)) ;; esac
done
if [ "$WANT_R" != "${#REXPECT[@]}" ]; then
    VIOL="${VIOL}
VIOLATION: ${WANT_R} queries are EMITTED on the incremental axis but the retract
           table lists ${#REXPECT[@]} — the two tables describe the same queries
           and one of them was edited alone."
fi
GOT_R=$(grep -c -F '[plan] retraction ->' "$RTRACE")
if [ "$GOT_R" != "$WANT_R" ]; then
    VIOL="${VIOL}
VIOLATION: the walk reported ${GOT_R} retraction decisions, expected ${WANT_R} —
           the retract axis is asked once per query that HAS a handle, so a
           mismatch means the second derivation stopped tracking the first."
fi

# ── THE CANARY: the same matcher, over a trace broken five ways ─────────────
# 1. a MISSING query (no_join_strrow has no line at all);
# 2. a FLIPPED verdict (no_where reads EMITTED);
# 3. a GENERIC ground (no_strkey's antecedent replaced by "not supported yet");
# 4. a RENAMED query (no_rowsel spelled no_rowsel2);
# 5. THE REL ARM SILENTLY NOT TAKEN (ok_rel_rec reads EMITTED with the PLAIN
#    bare-scan ground) — and this one is not a fifth flavour of the same mode, it
#    is THE EXACT REGRESSION the rel clause exists to catch, in the shape it now
#    takes. Before P3b the defect wore the verdict `EMITTED` where `declined` was
#    right; now the verdict is right and the GROUND is what separates "the handle
#    owns the accumulated input and the fixpoint total" from "one bare scan over
#    whatever the prelude left in scope", which is the aaf16585 behaviour that
#    returned `Ok` and answered wrong. A matcher that stopped reading grounds
#    would be caught here rather than by the corpus answering wrong.
#    `no_rel_oneshot` and `no_rel_join` are left CLEAN in the same trace so the
#    canary stays a controlled experiment: one row broken, its neighbours intact.
CAN="$TMPD/canary.txt"
{
  echo "[plan] incremental -> EMITTED on ok_basic   (one bare scan, group by, insert-only aggregates over self-contained types)"
  echo "[plan] incremental -> EMITTED on no_retract_avg   (one bare scan, group by, insert-only aggregates over self-contained types)"
  echo "[plan] incremental -> EMITTED on ok_join   (one join step over two owned relations, group by, weighted aggregates over self-contained types)"
  echo "[plan] incremental -> declined on no_join2   (the input is a JOIN CHAIN OF TWO OR MORE STEPS)"
  echo "[plan] incremental -> declined on no_join_avg   (the query JOINS and keeps the INSERT-ONLY surface)"
  echo "[plan] incremental -> declined on no_join_self   (the join is a SELF-JOIN)"
  echo "[plan] incremental -> EMITTED on no_where   (one bare scan, group by, insert-only aggregates over self-contained types)"
  echo "[plan] incremental -> declined on no_order   (\`order by\` / \`limit\` / \`distinct\` act on the SNAPSHOT)"
  echo "[plan] incremental -> declined on no_limit   (\`order by\` / \`limit\` / \`distinct\` act on the SNAPSHOT)"
  echo "[plan] incremental -> declined on no_strkey   (not supported yet)"
  echo "[plan] incremental -> declined on no_rowsel2   (\`select\` reaches a name that is neither \`key\` nor an aggregate output)"
  echo "[plan] incremental -> EMITTED on ok_rel_rec   (one bare scan, group by, insert-only aggregates over self-contained types)"
  echo "[plan] incremental -> declined on no_rel_oneshot   (the source is a NON-RECURSIVE declared \`rel\` — it materializes through the one-shot helper)"
  echo "[plan] incremental -> declined on no_rel_join   (a JOIN-POSITION source is a DECLARED \`rel\` — the handle's joined side is stored as a weighted Z-set)"
  echo "[plan] incremental -> declined on no_rel_outside   (the program declares a \`rel\` OUTSIDE this recursive SCC — the entry prelude then has several materialization steps)"
  # CLEAN on purpose — the canary is a controlled experiment, and a row missing
  # from it would be indistinguishable from a row this matcher cannot read.
  echo "[plan] incremental -> declined on no_rel_agg   (a member of the recursive SCC is an AGGREGATE rel (a min/max semilattice head) — its total is MATERIALIZED after the loop)"
} > "$CAN"
CANV=$(check_rows "$CAN" "${EXPECT[@]}")
printf '%s' "$CANV" > "$TMPD/canary.violations"
# ⚠ THE SECOND AXIS GETS ITS OWN CANARY, broken the two ways that matter for a
# decision whose whole content is a REFUSAL: the verdict FLIPPED (the fork gets
# taken silently, which is the failure this axis exists to prevent) and the
# ground made GENERIC (the refusal survives but stops naming what would close
# it). A gate that only ever sees a clean trace cannot tell either from a pass.
RCAN="$TMPD/rcanary.txt"
{
  echo "[plan] retraction -> EMITTED on ok_basic   (every aggregate in the list runs backwards exactly — count and integer sum invert exactly)"
  echo "[plan] retraction -> EMITTED on ok_join   (every aggregate in the list runs backwards exactly — count and integer sum invert exactly)"
  echo "[plan] retraction -> EMITTED on no_retract_avg   (every aggregate in the list runs backwards exactly — count and integer sum invert exactly)"
  echo "[plan] retraction -> EMITTED on ok_rel_rec   (every aggregate in the list runs backwards exactly — count and integer sum invert exactly)"
} > "$RCAN"
RCANV=$(check_rows "$RCAN" "${REXPECT[@]}")
printf '%s' "$RCANV" >> "$TMPD/canary.violations"
RCAN2="$TMPD/rcanary2.txt"
{
  echo "[plan] retraction -> EMITTED on ok_basic   (every aggregate in the list runs backwards exactly — count and integer sum invert exactly)"
  echo "[plan] retraction -> EMITTED on ok_join   (every aggregate in the list runs backwards exactly — count and integer sum invert exactly)"
  echo "[plan] retraction -> declined on no_retract_avg   (not supported yet)"
  echo "[plan] retraction -> declined on ok_rel_rec   (not supported yet)"
} > "$RCAN2"
RCANV2=$(check_rows "$RCAN2" "${REXPECT[@]}")
printf '%s' "$RCANV2" >> "$TMPD/canary.violations"
if [ -z "$(printf '%s' "$RCANV" | tr -d '[:space:]')" ] || \
   [ -z "$(printf '%s' "$RCANV2" | tr -d '[:space:]')" ]; then
    echo "FAIL: THE RETRACT AXIS'S CANARY DID NOT FIRE."
    echo "      A trace that takes the open f64 fork (EMITTED where the ground says"
    echo "      declined), or one whose ground has been reworded into a generic"
    echo "      'not supported yet', passed the same matcher that judges the real"
    echo "      trace. Canary output was:"
    printf 'flipped-verdict: %s\n' "$RCANV"
    printf 'generic-ground : %s\n' "$RCANV2"
    exit 1
fi
for want in no_join_strrow no_where no_strkey no_rowsel ok_rel_rec; do
    if ! grep -qF -- "'${want}'" "$TMPD/canary.violations"; then
        echo "FAIL: THE GATE'S CANARY DID NOT FIRE for '${want}'."
        echo "      A broken trace passed the same matcher that judges the real one,"
        echo "      so a clean verdict from this gate means nothing. Canary output:"
        printf '%s\n' "$CANV"
        exit 1
    fi
done

if [ -n "$VIOL" ]; then
    echo "FAIL: the incremental-eligibility population does not match the derivation."
    printf '%s\n' "$VIOL"
    echo "--- traced decisions ---"
    cat "$TRACE"
    exit 1
fi
echo "OK: ${WANT_N} incremental decisions, each with the ground that decided it."
exit 0
