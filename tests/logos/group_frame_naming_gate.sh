#!/usr/bin/env bash
# group_frame_naming_gate.sh LOGOSC PASS_DIR
#
# THE GROUP FRAME HAS A NAME, A GROUND AND A PRICE — ADR 0025 §8 (S4-naming).
#
# ── WHAT WAS UNNAMED, AND WHY A CENSUS ALONE IS NOT THE GATE ────────────────
#
# The aggregate emitter materializes a family of per-group columns —
# `__g_key`, `__g_cnt`, `__g_row`, one `__ga_<out>` per aggregate — and until
# this stage the plan named NOT ONE of them. `MG_REGROUP` names the aggregate's
# DRAIN of its base source; the state the grouping itself builds had no line at
# all, which made it the largest unnamed class in criterion 1 (1,122 bindings
# when first counted, 380 on this tree after the S4 fold deleted `__gf_*` and
# put `__g_cnt`/`__g_row` behind their consumers).
#
# `plan_ground_census_gate.sh` measures that class corpus-wide, and it is
# `tier_full`. This gate is the L2 half and it asserts something the census
# cannot: the PER-QUERY ATTRIBUTION. A corpus total of 2 representative rows is
# equally consistent with "the two queries that name a source row have one each"
# and with "one query has two and the class predicate is broken" — and the
# second is exactly the failure S4c's predicate can have. So the trace is SLICED
# AT ITS `group frame` LINES and each query's frame is asserted whole.
#
# ── THE PAIR, AND IT IS INSIDE ONE FIXTURE ─────────────────────────────────
#
# `pass/wql_group_single_pass_fold_e2e` is six `deem` queries over ONE source,
# written as refuse/admit twins for the S4c class predicate. Each direction of
# each ground is therefore a clause here, and neither direction can be satisfied
# by the other query's text:
#
#   Q1 pure_totals   4 accumulators, NO rep row   — the pure class
#   Q2 rep_totals    3 accumulators, ONE rep row  — the SAME query, one output
#                                                   column swapped for `s.amount`
#   Q3 pure_sorted   1 accumulator,  NO rep row   — pure through `having`+`order by`
#   Q4 ord_by_row    1 accumulator,  ONE rep row  — representative by its ORDER KEY alone
#   Q5 pure_avg      2 accumulators, THE count    — the only reader of `__g_cnt` (S4b)
#   Q6 none_survive  1 accumulator,  NO rep row
#
# A `group count` line on any query but Q5 is S4b's gate leaking; a
# `representative row` on Q1/Q3/Q5/Q6 is the class predicate over-admitting;
# its absence on Q2/Q4 is the predicate over-refusing — which the fixture's own
# run catches as a wrong ANSWER, so the two instruments meet.
#
# ── AND THE PLAN IS COMPARED WITH THE ARTIFACT, NOT ONLY WITH A LITERAL ────
#
# Every count below is asserted TWICE: against a predicted literal, and against
# the emitted dump's own `let mut` bindings. A justification sentence that is
# false about the artifact it justifies is the defect this whole vocabulary
# exists to refuse, and a gate that only compared the trace with a number
# written beside it would vouch for exactly that.
#
# ⚠ A COMPILE FAILURE IS A PARTICIPANT-COUNT CHANGE, NOT A DIFF. The gate exits
# 2 (never clean) when the fixture does not compile or produces no dump.
#
# PROVED TO BITE — measured on the stdlib, one perturbation at a time, each
# reverted to a byte-identical source with a green checkpoint between:
#
#   P1  `group_rep_row_field` moved OUT of its `if !pure_group` branch in
#       `rexpr_walk::emit_aggregate` (fired unconditionally, the shape a naming
#       layer takes when it is derived from the query rather than from the
#       declaration): 3 claims failed — the trace total (2 -> 6), the per-query
#       signature (`4:0:1 3:0:1 1:0:1 1:0:1 2:1:1 1:0:1`, a rep row on all six
#       where the emitter declares two), and the plan/artifact identity for
#       `__g_row` (6 plan lines vs 2 emitted bindings).
#   P2  `group_count_field` called for every aggregate instead of once per
#       `needs_cnt` group table: 3 claims failed — the total (1 -> 12), the
#       signature (`4:4:0 3:3:1 …`), and the identity against `let mut
#       __g_cnt:` (12 vs 1).
#
# ⚠ THE PREDICTION FOR P1 WAS 4 AND THE MEASUREMENT WAS 3 — recorded rather
# than quietly corrected. The two per-query frames that change are ONE clause
# here (the signature is compared as a single string, deliberately, so that a
# column moved between queries cannot cancel in a total), not two.
#
# EXIT: 0 clean · 1 a claim failed · 2 the gate could not look (never clean).
set -uo pipefail

LOGOSC=${1:?usage: group_frame_naming_gate.sh <logosc> <pass dir>}
PASSD=${2:?usage: group_frame_naming_gate.sh <logosc> <pass dir>}

export LC_ALL=C
TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

fails=0
note() { echo "FAIL: $*"; fails=$((fails+1)); }

B=wql_group_single_pass_fold_e2e

# ── compile once; rc from a REDIRECT, never from a pipeline ────────────────
LOGOS_TRACE_PLAN=1 "$LOGOSC" "$PASSD/$B.logos" --gen-dir "$TMPD/gen" \
    -o "$TMPD/$B.o" > "$TMPD/out" 2> "$TMPD/err"
rc=$?
if [ "$rc" != "0" ]; then
    echo "FAIL(2): $B does not compile (rc=$rc) — there is nothing to read."
    grep -m3 -i 'error' "$TMPD/out" "$TMPD/err" 2>/dev/null | head -3
    exit 2
fi

shopt -s nullglob
# The USER module's dumps only — `logos.gen.*` holds the family DEFINITIONS and
# says nothing about what THIS query builds.
ALLD=("$TMPD/gen"/*.gen.logos)
UD=()
for x in "${ALLD[@]}"; do
    case "$(basename "$x")" in logos.gen.*) ;; *) UD+=("$x");; esac
done
if [ "${#UD[@]}" -lt 1 ]; then
    echo "FAIL(2): no user-module dump for $B; the artifact clauses have no subject."
    exit 2
fi
cat "${UD[@]}" > "$TMPD/art.txt"

if ! grep -q '^\[plan\] ' "$TMPD/err"; then
    echo "FAIL(2): the plan trace is empty — LOGOS_TRACE_PLAN produced no lines,"
    echo "         so every trace clause below would be vacuously satisfied."
    exit 2
fi

cnt() { grep -c -F -- "$2" "$1" 2>/dev/null || true; }

want() {   # want <file> <needle> <expected> <what>
    local got; got=$(cnt "$1" "$2")
    [ "$got" = "$3" ] || note "$4: expected $3 occurrence(s) of '$2', found $got"
}

# ── 1. THE FOUR GROUNDS ARE SAID, AND SAID THE PREDICTED NUMBER OF TIMES ───
# The verdict word AND the ground token together, because either alone can move
# without the other: a ground reused under a new verdict would still be
# witnessed by the census, and a verdict whose ground drifted to a neighbouring
# sentence is the drift this vocabulary refuses.
want "$TMPD/err" '-> group frame on group table: one row per distinct key'          6 \
     "TRACE every emitted group table is named"
want "$TMPD/err" '-> accumulator on accumulated: one cell per group per aggregate' 12 \
     "TRACE every accumulator column is named"
want "$TMPD/err" '-> group count on group rows: avg'\''s denominator'               1 \
     'TRACE the count column is named where `avg` reads it'
want "$TMPD/err" '-> representative row on re-bound: the projection names a source row' 2 \
     "TRACE the representative row is named where a clause above the group needs one"

# ── 2. EVERY GROUND LINE CARRIES A SENTENCE ────────────────────────────────
# FACT F's rule, applied to the new family at its own tier: a ground naming a
# mechanism with an empty account is a materialization named and not explained.
nempty=$(grep -cE '^\[plan\] [a-z_0-9]+ -> (group frame|accumulator|group count|representative row) on [^(]*\(\)$' "$TMPD/err" 2>/dev/null || true)
[ "$nempty" = "0" ] || note "TRACE $nempty group-frame line(s) carry an EMPTY justification"

# ── 3. THE FRAME'S WIDTH IS PRICED ON THE TABLE'S OWN LINE ────────────────
# A group table costs one row per key TIMES the columns hanging off it. The
# width is therefore on the `group frame` line itself, so a reader pricing the
# state does not have to count `accumulator` lines to do it.
want "$TMPD/err" 'carries 4 accumulator column(s)' 1 "TRACE Q1's frame is 4 wide"
want "$TMPD/err" 'carries 3 accumulator column(s)' 1 "TRACE Q2's frame is 3 wide"
want "$TMPD/err" 'carries 2 accumulator column(s)' 1 "TRACE Q5's frame is 2 wide"
want "$TMPD/err" 'carries 1 accumulator column(s)' 3 "TRACE Q3/Q4/Q6's frames are 1 wide"

# ── 4. PER-QUERY ATTRIBUTION — the class predicate, both directions ───────
# The trace is sliced at its `group frame` lines: everything between two of them
# is one query's frame. The signature is `<accumulators>:<count>:<rep row>` per
# query, in emission order, and it is compared as ONE string so that a defect
# which moves a column from one query to another cannot cancel in a total.
sig=$(awk '
    / -> group frame on /          { n++; a[n]=0; c[n]=0; r[n]=0; next }
    / -> accumulator on /          { if (n) a[n]++ ; next }
    / -> group count on /          { if (n) c[n]++ ; next }
    / -> representative row on /   { if (n) r[n]++ ; next }
    END { s=""; for (i = 1; i <= n; i++) s = s (i > 1 ? " " : "") a[i] ":" c[i] ":" r[i]; print s }
' "$TMPD/err")
WANT_SIG='4:0:0 3:0:1 1:0:0 1:0:1 2:1:0 1:0:0'
if [ "$sig" != "$WANT_SIG" ]; then
    note "TRACE per-query frame signatures are '$sig', predicted '$WANT_SIG'"
    echo "       (order: pure_totals, rep_totals, pure_sorted, ord_by_row, pure_avg, none_survive;"
    echo "        each is <accumulators>:<group count>:<representative row>)"
fi

# ── 5. THE PLAN'S COUNT IS THE ARTIFACT'S COUNT ──────────────────────────
# Not a second literal: the emitted dump is counted and compared with the trace.
# This is the clause that would catch a naming layer derived from the QUERY
# rather than fired inside the branch that declares the column — the two agree
# on this fixture only because each call site sits inside its own declaration.
ident() {   # ident <trace-needle> <artifact-needle> <what>
    local t a
    t=$(cnt "$TMPD/err" "$1")
    a=$(grep -cE "$2" "$TMPD/art.txt" 2>/dev/null || true)
    [ "$t" = "$a" ] || note "IDENTITY $3: $t plan line(s) vs $a emitted binding(s)"
}
ident '-> group frame on '        'let mut __g_key:'          "group tables vs \`__g_key\` bindings"
ident '-> accumulator on '        'let mut __ga_[a-z_0-9]*:'  "accumulator lines vs \`__ga_*\` bindings"
ident '-> group count on '        'let mut __g_cnt:'          "group-count lines vs \`__g_cnt\` bindings"
ident '-> representative row on ' 'let mut __g_row:'          "representative-row lines vs \`__g_row\` bindings"

# ── 6. THE ABSENCES ARE REAL ABSENCES ────────────────────────────────────
# `__gf_*` — the finalize pass's dense parallel copy — is DELETED (S4c), and the
# single-pass claim in the `accumulator` sentence is false if it comes back.
want "$TMPD/art.txt" 'let mut __gf_' 0 \
     "ARTIFACT the finalize pass's \`__gf_*\` family is gone and stays gone"
# The group table is NOT an Arrange, and the plan must not say it is: the census
# pins `#(arrange nodes) == #(emitted index bindings)`, and a group table
# spelled `arrange` would make that identity false for every aggregate.
want "$TMPD/err" '-> arrange on group table' 0 \
     "TRACE the group table is a FIELD of the aggregate, never an Arrange node"
nix=$(grep -cE 'let mut __(hm|hs|bt)[0-9]+:' "$TMPD/art.txt" 2>/dev/null || true)
narr=$(grep -cE '^\[plan\] [a-z_0-9]+ -> arrange on ' "$TMPD/err" 2>/dev/null || true)
[ "$narr" = "$nix" ] || note "IDENTITY the arrange/index identity moved under this stage: $narr arrange node(s) vs $nix emitted index binding(s)"

if [ "$fails" -ne 0 ]; then
    echo "group_frame_naming_gate: $fails claim(s) failed."
    exit 1
fi
echo "group_frame_naming_gate: OK — 6 group frames, 12 accumulators, 1 count and"
echo "2 representative rows, each named with its ground, priced on its own line,"
echo "attributed to the query that declares it, and equal to what the artifact builds."
exit 0
