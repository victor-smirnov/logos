#!/usr/bin/env bash
# incr_mutrec_refusal_gate.sh LOGOSC MUTREC_FIXTURE REC_FIXTURE
#
# THE THREE `fail/wql_incr_mutrec_*` DOORS ARE SATISFIED BY A STRICTLY STRONGER
# REFUSAL, AND THAT WAS MEASURED RATHER THAN SUSPECTED.
#
# ⚠⚠ THE MEASUREMENT, 2026-08-07. `run_test.sh`'s fail mode asserts that logosc
# FAILS and that its stderr CONTAINS the expected string. The three doors expect
# `call to undefined function 'oddq_retract'` / `'evenq_retract'` /
# `'oddq_incremental'`. Restore P3b's `nmem != 1` clause in
# `stamp_rel_incr_shape` — i.e. refuse the mutually recursive SCC ENTIRELY, so
# the query gets no handle, no `_epoch`, no `_retract` and no `_incremental` —
# drop the archives, rebuild the whole stdlib, and ALL THREE DOORS GO ON PASSING.
# `pass/wql_incr_rel_mutrec_epochs.logos` is what reds (`unknown type 'OddqIncr'`).
#
# So a green `no_retract_odd` is NOT evidence that the RETRACTION clause refused.
# It is evidence that the name is absent, and the name is absent under every
# refusal that reaches the query — including one that withdraws the whole
# capability. The doors prove the fn is not emitted; nothing in the same
# measurement proves the surface was otherwise ELIGIBLE. That is the shape of
# "an oracle's SILENCE is not an answer": no branch for the case reads as
# agreement.
#
# THIS GATE IS THAT MISSING CONTROL, AND IT IS ONE COMPILE SO THE TWO FACTS
# CANNOT DRIFT APART. Over the SAME trace of the SAME compile it asserts, for
# BOTH members' queries:
#
#   `[plan] incremental -> EMITTED`  — the handle EXISTS. This is the eligible
#                                      control the `fail/` files cannot carry:
#                                      a file that fails to compile has no room
#                                      for a positive assertion.
#   `[plan] retraction   -> declined` — and the retraction, specifically, is the
#                                      thing withheld, on the RECURSION ground.
#   `[plan] rel dred driver -> EMITTED` — and the four-phase DRed driver IS
#                                      emitted for BOTH members of the SCC, on a
#                                      ground that names the PUBLIC surface which
#                                      follows from it AND the price of using it.
#
# ⚠⚠ THE THIRD AXIS IS THE ONE THAT KEEPS THE OTHER TWO HONEST NOW THAT THE
# DRIVER EXISTS. `__wql_<q>_scc<c>_dred` is a PRIVATE item named by exactly one
# fixture (`pass/wql_incr_rel_dred_driver.logos`), and that fixture is N = 1 — a
# single-member SCC. Nothing else in the tree can see whether the driver is
# emitted for a MUTUALLY RECURSIVE SCC at all, and "emitted once per SCC rather
# than once per admitted member" is precisely the duplicate-definition class
# `incr_scc_driver_gate.sh` exists for. Reading the verdict here costs one grep
# over a trace this gate already takes.
#
# ⚠⚠ THE THIRD AXIS'S PHRASE CHANGED WHEN THE SURFACE LANDED, AND THAT IS THE
# GATE WORKING. It used to demand "NO RETRACTION SURFACE FOLLOWS", chosen because
# that clause CANNOT survive the surface round unedited — and it did not: the day
# `emit_incremental` began emitting a public `<q>_retract` from its `dred` block
# this gate went red and had to be re-pointed. What it demands now is the thing
# that becomes false if the surface is landed CARELESSLY:
#   • "THE PUBLIC RETRACTION SURFACE FOLLOWS" — the ground admits the surface it
#     causes, so a driver emitted with no surface, or a surface emitted from
#     somewhere else, stops matching; and
#   • "PESSIMIZATION" — the PRICE. DRed is a measured 1.15-1.9x a from-scratch
#     recompute with no crossover in the reachable range, and until the surface
#     existed that number was named ONLY in `incr_retract_eligible`'s REFUSAL
#     text, which is exactly the sentence a user of `<q>_retract` never reads. A
#     capability whose cost evaporates at the moment it becomes reachable is how
#     a user arrives at a pessimization by writing a name and reading nothing.
# ⚠⚠ WHAT THIS GATE CANNOT SEE, MEASURED AND STATED RATHER THAN LEFT IMPLIED.
# Remove the single `emit_fn_quote_blob` that emits `<q>_retract` from the `if
# dred` block, drop the archives, rebuild the stdlib → THIS GATE STAYS GREEN.
# Its rows read a CLAIM on a trace channel, and the claim goes on being made
# while the item it describes is gone. What reds are the three artifacts:
# `pass/wql_incr_rel_retract_surface.logos` and
# `pass/wql_incr_mutrec_retract_surface_{odd,even}.logos`, each with `call to
# undefined function '<q>_retract'`. The division of labour is deliberate — this
# gate pins the GROUND and the PRICE, the fixtures pin the NAME and the ANSWER —
# but a reader must not take a green run here as evidence the surface exists.
#
# ⚠ THE `retraction -> declined` AXIS IS UNCHANGED AND IS **NOT** OBSOLETE. The
# surface did NOT go through `retr`: `incr_retract_eligible` still declines every
# rel-backed query, so `<q>_apply` and `__wd` are still not emitted for one and
# the INVERSE-FOLD path is still refused on the recursion ground. This gate is
# what keeps "declined" and "EMITTED" describing two different things.
#
# Under the P3b restore the first pair vanishes and this gate reds. Under
# "disable `incr_retract_eligible`'s `rel_backed` clause" the second pair turns
# EMITTED and this gate reds — the same edit that makes the two `no_retract`
# doors compile (MEASURED: both go red as `fail/` tests, `outside_rel` stays
# green, which is what shows the two clauses are separable).
#
# ⚠ TWO QUERIES, NOT ONE, BECAUSE AN SCC OF N MEMBERS HAS N DOORS. `oddq`
# selects from `odd` and `evenq` from `even`; each reaches `incr_retract_eligible`
# through its own `rel_find_src`. Pinning one would leave the other's verdict
# unread.
#
# ⚠⚠ AND A SECOND FIXTURE, BECAUSE THE SINGLE-MEMBER SHAPE HAD NO GROUND-READING
# GATE AT ALL. Every row above is about a MUTUALLY RECURSIVE SCC; `tc` — one
# member, the shape `pass/wql_incr_rel_retract_surface.logos` and
# `pass/wql_incr_rel_dred_driver.logos` exercise — appeared in no axis anywhere
# in the tree, so its verdicts were readable only as artifacts. `nmem == 1` and
# `nmem > 1` take DIFFERENT paths through `stamp_rel_incr_shape`, so a ground
# reworded or lost on one is not visible from the other. `$3` is that fixture and
# it gets the same three axes over its own compile.
#
# ⚠ AND A GATE WHOSE VERDICT IS SILENCE MUST PROVE IT CAN SPEAK. `grep` finding
# nothing, a renamed channel and a query that stopped being walked all look
# exactly like a clean run. The same matcher therefore runs, in this same
# invocation, over a CANARY trace broken four ways — a missing EMITTED, a
# retraction flipped to EMITTED, a reworded ground, and a query that vanished —
# and every one must be named back. A clean canary fails this gate.
set -uo pipefail

LOGOSC="$1"
MUTREC="$2"
REC="$3"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

LOGOS_TRACE_PLAN=1 "$LOGOSC" "$MUTREC" -o "$TMPD/out.o" 2>"$TMPD/all.err" >/dev/null
RC=$?
if [ "$RC" != 0 ]; then
    echo "FAIL: the fixture must COMPILE — a mutually recursive SCC that stops"
    echo "      getting a handle is the P3b refusal returning, and every"
    echo "      fail/wql_incr_mutrec_* door would go on passing through it."
    echo "      logosc exit $RC:"
    head -40 "$TMPD/all.err"
    exit 1
fi
# ⚠ THE SINGLE-MEMBER SHAPE, ITS OWN COMPILE. `nmem == 1` and `nmem > 1` are
# different paths through `stamp_rel_incr_shape`; reading only the mutrec trace
# left `tc`'s verdicts unpinned by anything in the tree.
LOGOS_TRACE_PLAN=1 "$LOGOSC" "$REC" -o "$TMPD/rec.o" 2>"$TMPD/rec.err" >/dev/null
RRC=$?
if [ "$RRC" != 0 ]; then
    echo "FAIL: the single-member fixture must COMPILE — a recursive \`rel\` that"
    echo "      stops getting a handle takes its retraction surface with it and"
    echo "      the fail/ doors would go on passing through the loss."
    echo "      logosc exit $RRC:"
    head -40 "$TMPD/rec.err"
    exit 1
fi
grep -F '[plan] incremental ->' "$TMPD/all.err" > "$TMPD/itrace.txt"
grep -F '[plan] retraction ->'  "$TMPD/all.err" > "$TMPD/rtrace.txt"
grep -F '[plan] rel dred driver ->' "$TMPD/all.err" > "$TMPD/dtrace.txt"
grep -F '[plan] incremental ->' "$TMPD/rec.err" > "$TMPD/ritrace.txt"
grep -F '[plan] retraction ->'  "$TMPD/rec.err" > "$TMPD/rrtrace.txt"
grep -F '[plan] rel dred driver ->' "$TMPD/rec.err" > "$TMPD/rdtrace.txt"

# query | verdict | a phrase the GROUND must contain (the ANTECEDENT, so a
# ground reworded into a generic "unsupported" stops matching).
IEXPECT=(
  "oddq|EMITTED|RECURSIVE \`rel\`"
  "evenq|EMITTED|RECURSIVE \`rel\`"
)
REXPECT=(
  "oddq|declined|support ITSELF through a cycle"
  "evenq|declined|support ITSELF through a cycle"
)
# ⚠ THE PHRASE IS THE ANTECEDENT AGAIN. Two of them here, checked as two
# separate rows over the same line, because they fail independently: the ground
# must admit the SURFACE it causes, and it must carry the PRICE of that surface.
# See the header for why the previous phrase ("NO RETRACTION SURFACE FOLLOWS")
# had to be replaced rather than kept.
DEXPECT=(
  "oddq|EMITTED|THE PUBLIC RETRACTION SURFACE FOLLOWS"
  "evenq|EMITTED|THE PUBLIC RETRACTION SURFACE FOLLOWS"
  "oddq|EMITTED|PESSIMIZATION"
  "evenq|EMITTED|PESSIMIZATION"
)
# ── THE SINGLE-MEMBER SHAPE — the same three axes, its own compile, one query.
RIEXPECT=( "tc|EMITTED|RECURSIVE \`rel\`" )
RREXPECT=( "tc|declined|support ITSELF through a cycle" )
RDEXPECT=(
  "tc|EMITTED|THE PUBLIC RETRACTION SURFACE FOLLOWS"
  "tc|EMITTED|PESSIMIZATION"
)

check_rows() {
    local file="$1"; shift
    local row name rest verdict phrase line
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
        # ⚠ INTO A FILE, THEN MATCH — never `printf … | grep -q`. Under
        # `set -o pipefail` grep exits at the first match, the writer takes
        # SIGPIPE 141, and pipefail reports the MATCH as a failed pipeline.
        printf '%s' "$line" > "$file.line"
        if ! grep -qF -- "$phrase" "$file.line"; then
            echo "VIOLATION: the ground for '${name}' does not name '${phrase}': ${line}"
        fi
    done
}

# ⚠ THE NEWLINE IS LOAD-BEARING. `$(…)` strips the trailing newline, so a bare
# concatenation glues the last violation of the first axis onto the first of the
# second — and the canary counts LINES. Measured: four breakages reported as
# three, which would have hidden one real violation per run too.
VIOL=$(check_rows "$TMPD/itrace.txt" "${IEXPECT[@]}")
VIOL="${VIOL}
$(check_rows "$TMPD/rtrace.txt" "${REXPECT[@]}")"
VIOL="${VIOL}
$(check_rows "$TMPD/dtrace.txt" "${DEXPECT[@]}")"
VIOL="${VIOL}
$(check_rows "$TMPD/ritrace.txt" "${RIEXPECT[@]}")"
VIOL="${VIOL}
$(check_rows "$TMPD/rrtrace.txt" "${RREXPECT[@]}")"
VIOL="${VIOL}
$(check_rows "$TMPD/rdtrace.txt" "${RDEXPECT[@]}")"
VIOL=$(printf '%s' "$VIOL" | grep 'VIOLATION' || true)

# ── THE CANARY: the same matcher over a trace broken four ways ──────────────
#  1. `oddq`'s incremental EMITTED line is MISSING   (the P3b restore's shape);
#  2. `evenq`'s retraction says EMITTED, not declined (the disabled clause);
#  3. `oddq`'s retraction ground is REWORDED generically;
#  4. `evenq` is absent from the incremental trace entirely (never walked);
#  5. `oddq`'s dred driver says `declined`  (the driver stopped being emitted);
#  6. `evenq`'s dred ground admits the surface but DROPS THE PRICE — the
#     pessimization clause gone, which is a capability whose cost evaporated at
#     the moment it became reachable;
#  7. `tc`'s single-member rows are absent from all three of its own traces (the
#     shape that had no gate at all until this round).
# ⚠ ROWS 5 AND 6 SIT ON THE SAME AXIS BUT SPEAK SEPARATELY, which is why the
# surface phrase and the price phrase are two DEXPECT rows and not one: a ground
# can admit the surface and still lose the number.
CI="$TMPD/can_i.txt"
CR="$TMPD/can_r.txt"
CD="$TMPD/can_d.txt"
CRI="$TMPD/can_ri.txt"
CRR="$TMPD/can_rr.txt"
CRD="$TMPD/can_rd.txt"
echo "[plan] incremental -> declined on oddq   (something else entirely)" > "$CI"
{
  echo "[plan] retraction -> declined on oddq   (not supported at this time)"
  echo "[plan] retraction -> EMITTED on evenq   (invert exactly)"
} > "$CR"
{
  echo "[plan] rel dred driver -> declined on oddq   (an aggregate is min/max)"
  echo "[plan] rel dred driver -> EMITTED on evenq   (the four-phase DRed epoch driver. THE PUBLIC RETRACTION SURFACE FOLLOWS)"
} > "$CD"
# `tc` never appears — the single-member shape silently stopped being walked.
echo "[plan] incremental -> EMITTED on somethingelse   (RECURSIVE \`rel\`)" > "$CRI"
echo "[plan] retraction -> declined on somethingelse   (support ITSELF through a cycle)" > "$CRR"
echo "[plan] rel dred driver -> EMITTED on somethingelse   (THE PUBLIC RETRACTION SURFACE FOLLOWS. PESSIMIZATION)" > "$CRD"
CANV=$(check_rows "$CI" "${IEXPECT[@]}")
CANV="${CANV}
$(check_rows "$CR" "${REXPECT[@]}")"
CANV="${CANV}
$(check_rows "$CD" "${DEXPECT[@]}")"
CANV="${CANV}
$(check_rows "$CRI" "${RIEXPECT[@]}")"
CANV="${CANV}
$(check_rows "$CRR" "${RREXPECT[@]}")"
CANV="${CANV}
$(check_rows "$CRD" "${RDEXPECT[@]}")"
# Ten independent breakages ⇒ at least ten violation lines: four on the mutrec
# axes (rows 1-4), two on the mutrec dred axis (oddq declined ⇒ both its phrase
# rows speak; evenq's price gone ⇒ one), and four on the single-member traces
# (one incremental, one retraction, two dred phrases). Fewer means the matcher is
# not looking at what it claims to look at.
CANN=$(printf '%s' "$CANV" | grep -c 'VIOLATION')
if [ "$CANN" -lt 10 ]; then
    echo "FAIL: THE CANARY DID NOT SPEAK. A trace with oddq's handle missing,"
    echo "      evenq's retraction EMITTED, oddq's ground reworded, evenq"
    echo "      absent, oddq's dred driver declined, evenq's dred ground"
    echo "      stripped of its PRICE, and the single-member \`tc\` query gone"
    echo "      from all three of its own traces produced only ${CANN}"
    echo "      violations (expected >= 10), so the"
    echo "      matcher above is not comparing anything and every green run of"
    echo "      this gate so far means nothing."
    printf '%s\n' "$CANV"
    exit 1
fi

if [ -n "$VIOL" ]; then
    echo "MUTREC REFUSAL GATE — the handle is EMITTED and the RETRACTION, specifically, is declined"
    printf '%s\n' "$VIOL"
    echo ""
    echo "See stdlib/mem/wql/rexpr_walk.logos, incr_retract_eligible, and"
    echo "stdlib/mem/wql/params.logos, stamp_rel_incr_shape."
    exit 1
fi
exit 0
