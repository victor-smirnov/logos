#!/usr/bin/env bash
# A PLAN DECISION'S GROUND IS A RULE OVER FACTS — asserted on the FACTS, not on a
# substring of the sentence (why.logos: "THE JOIN STRATEGY'S GROUND", "THE ACCESS
# PATH'S GROUND").
#
# Each case compiles one fixture under LOGOS_TRACE_PLAN=facts and requires the
# exact `[facts]` line of one decision: the rule that fired, the antecedents that
# held, and the negative explanation (`not_hash` for a join step that is not a
# hash join; `failed` for an access path). A change to a cascade, to a type's or
# a source's facts, or to a rule's body moves one of these lines; a change to the
# WORDING moves none.
#
# Each case reads the fixture's `[facts]` line from the compile its own corpus
# test already ran, under `LOGOS_TRACE_PLAN=facts` (`run_test.sh` ->
# `facts_emit.sh` -> `<facts>/<fixture>/plan.err`). The gate declares
# FIXTURES_REQUIRED on those tests and FOLDS the files; `facts_require` refuses a
# missing or stale one. It compiles nothing of its own: it used to compile every
# fixture twice itself — serially (260-300 s against a 300 s budget), then in
# background jobs, a second scheduler inside a test `lt` already schedules
# (`logos_00_one_scheduler_lint`).
#
# CONTROL: ONE compile with the facts channel OFF (`LOGOS_TRACE_PLAN=1`) must
# print no `[facts]` line at all, so a gate that matched stale output is
# distinguishable. One serial compile, of the smallest fixture here.
set -u
LOGOSC="$1"; SRC="$2"; FACTS="$3"
. "$(dirname "$0")/facts_fold.sh"
TMPD=$(mktemp -d); trap 'rm -rf "$TMPD"' EXIT
fail=0
CF=(); CW=()
check() {   # fixture, expected facts line — recorded, verified below
    CF+=("$1"); CW+=("$2")
}
# A user struct with Hash + Eq + Copy: the hash rule fires.
check wql_named_key_e2e \
  '[facts] p join rule=hash key=Sku held=[equi_key,Hash,Eq,eq_op,key_store,self_ident,self_ord] asked=[equi_key,Hash,Eq,Ord,eq_op,key_store,self_ident,self_ord,force_tree]'
# PartialEq without Eq: loop_eq, and the one antecedent hash lacked is Eq.
check wql_named_key_loop_e2e \
  '[facts] p join rule=loop_eq key=Sku held=[equi_key,Hash,eq_op,key_store,self_ident,self_ord] asked=[equi_key,Hash,Eq,Ord,eq_op,key_store,self_ident,self_ord,force_tree] not_hash=[Eq]'
# f64: no Hash/Eq impl, and its `==` is not faithful (NaN).
check wql_join_float_key_e2e \
  '[facts] d join rule=loop_eq key=f64 held=[equi_key,eq_op,key_store] asked=[equi_key,Hash,Eq,Ord,eq_op,key_store,self_ident,self_ord,force_tree] not_hash=[Hash,Eq,self_ident]'
# ── access paths (E2) ──
# An exact declared operation, the filter belongs to this source alone: dropped.
check deem_hashmap_source \
  '[facts] m access rule=exact_drop held=[where,ops,col,own_col,bound,cmp,cover,exact,may_drop] failed=[]'
# The source declares operations, none covers this comparison: the one failed fact.
check deem_hashmap_source \
  '[facts] m access rule=scan_no_cover held=[where,ops,col,own_col,bound,cmp] failed=[cover]'
# No filter at all.
check deem_source_size \
  '[facts] s access rule=scan_all held=[] failed=[where]'
# ── how a rel travels (E3) ──
# A drain node over an iterator: the mode is the node's, and `ordered` failed.
check deem_batch_scan_drain \
  '[facts] m mode rule=drained held=[iter,drain] failed=[ordered]'
# `order by … desc` over the column the rows arrive sorted by: a backward pull.
check deem_batch_scan_drain \
  '[facts] m mode rule=ordered_rev held=[iter,ordered,desc] failed=[drain]'
# A plain single read of an iterator.
check deem_source_size \
  '[facts] s mode rule=stream held=[iter] failed=[ordered,drain]'
# ── may the query have an incremental handle (E3): the FIRST FAILED antecedent ──
# The matrix fixture names its queries after the shape they test; the rule must agree.
check wql_incr_eligibility_matrix '[facts] ok_join incremental rule=emit_join'
check wql_incr_eligibility_matrix '[facts] no_join_self incremental rule=self_join'
check wql_incr_eligibility_matrix '[facts] no_where incremental rule=pre_where'
check wql_incr_eligibility_matrix '[facts] no_rel_agg incremental rule=rel_not_incr'
# ── may the handle run backwards (E3) ──
check wql_incr_eligibility_matrix '[facts] no_retract_avg retraction rule=float_acc'
check wql_incr_eligibility_matrix '[facts] ok_rel_rec retraction rule=rec_rel'
check wql_incr_eligibility_matrix '[facts] ok_join retraction rule=exact'
# ── the DRed driver and the aggregate's group-frame class (E3) ──
check wql_incr_rel_dred_driver '[facts] tc dred rule=emit'
check wql_aggregate_e2e '[facts] dept_stats aggclass rule=PURE'
# ── compile every fixture once per channel, in parallel ──
declare -A seen=()
FIXS=()
for f in "${CF[@]}"; do
    [ -n "${seen[$f]:-}" ] && continue
    seen[$f]=1
    FIXS+=("$SRC/$f.logos")
done
facts_require "$FACTS" "$LOGOSC" "plan ground facts" "${FIXS[@]}"
for f in "${!seen[@]}"; do
    if [ "$(cat "$FACTS/$f/rc")" != 0 ]; then
        echo "FAIL: $f did not compile"
        grep -v -e '^\[plan\] ' -e '^\[facts\] ' "$FACTS/$f/plan.err" | head -3
        fail=1
    fi
done
# The channel-off control (see the header).
CTL=deem_source_size
LOGOS_TRACE_PLAN=1 "$LOGOSC" "$SRC/$CTL.logos" -o "$TMPD/ctl.o" 2>"$TMPD/ctl.err" >/dev/null
if ! grep -q '^\[plan\] ' "$TMPD/ctl.err"; then
    echo "FAIL: control $CTL printed no [plan] line under LOGOS_TRACE_PLAN=1 — it measured nothing"; fail=1
elif grep -q '^\[facts\]' "$TMPD/ctl.err"; then
    echo "FAIL: $CTL — a [facts] line was printed with the facts channel off"; fail=1
fi
i=0
while [ "$i" -lt "${#CF[@]}" ]; do
    f="${CF[$i]}"; want="${CW[$i]}"
    if ! grep -qxF -- "$want" "$FACTS/$f/plan.err"; then
        echo "FAIL: $f — expected the facts line"
        echo "    $want"
        echo "  got:"; grep '^\[facts\]' "$FACTS/$f/plan.err" | sed 's/^/    /'
        fail=1
    fi
    i=$((i + 1))
done
[ "$fail" = 0 ] && echo "plan ground facts: 18 cases (3 join, 3 access, 3 mode, 4 incremental, 3 retraction, 1 dred, 1 aggclass), rule + held + negative explanation pinned"
exit $fail  # lint:exit-ok — `fail` is set only to the literals 0 and 1
