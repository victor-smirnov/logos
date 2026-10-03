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
# CONTROL: the same three compiles with the facts channel OFF must print no
# `[facts]` line at all, so a gate that matched stale output is distinguishable.
set -u
LOGOSC="$1"; SRC="$2"
TMPD=$(mktemp -d); trap 'rm -rf "$TMPD"' EXIT
fail=0
# Each fixture is compiled ONCE per channel and every case reads the cached
# output — a fixture with several pinned decisions costs two compiles, not two
# per case (the gate timed out under the full run at two per case).
compiled() {   # fixture -> ensures $TMPD/<f>.facts and $TMPD/<f>.plain exist
    local f="$1"
    [ -f "$TMPD/$f.facts" ] && return 0
    if ! LOGOS_TRACE_PLAN=facts "$LOGOSC" "$SRC/$f.logos" -o "$TMPD/$f.o" 2>"$TMPD/$f.facts" >/dev/null; then
        echo "FAIL: $f did not compile"; head -3 "$TMPD/$f.facts"; fail=1; return 1
    fi
    LOGOS_TRACE_PLAN=1 "$LOGOSC" "$SRC/$f.logos" -o "$TMPD/$f.o" 2>"$TMPD/$f.plain" >/dev/null
    if grep -q '^\[facts\]' "$TMPD/$f.plain"; then
        echo "FAIL: $f — a [facts] line was printed with the facts channel off"; fail=1
    fi
}
check() {   # fixture, expected facts line
    local f="$1" want="$2"
    compiled "$f" || return
    if ! grep -qxF -- "$want" "$TMPD/$f.facts"; then
        echo "FAIL: $f — expected the facts line"
        echo "    $want"
        echo "  got:"; grep '^\[facts\]' "$TMPD/$f.facts" | sed 's/^/    /'
        fail=1
    fi
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
[ "$fail" = 0 ] && echo "plan ground facts: 18 cases (3 join, 3 access, 3 mode, 4 incremental, 3 retraction, 1 dred, 1 aggclass), rule + held + negative explanation pinned"
exit $fail
