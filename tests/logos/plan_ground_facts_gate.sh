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
check() {   # fixture, expected facts line
    local f="$1" want="$2"
    LOGOS_TRACE_PLAN=facts "$LOGOSC" "$SRC/$f.logos" -o "$TMPD/x.o" 2>"$TMPD/err" >/dev/null || {
        echo "FAIL: $f did not compile"; head -3 "$TMPD/err"; fail=1; return; }
    if ! grep -qxF -- "$want" "$TMPD/err"; then
        echo "FAIL: $f — expected the facts line"
        echo "    $want"
        echo "  got:"; grep '^\[facts\]' "$TMPD/err" | sed 's/^/    /'
        fail=1
    fi
    LOGOS_TRACE_PLAN=1 "$LOGOSC" "$SRC/$f.logos" -o "$TMPD/x.o" 2>"$TMPD/err0" >/dev/null
    if grep -q '^\[facts\]' "$TMPD/err0"; then
        echo "FAIL: $f — a [facts] line was printed with the facts channel off"; fail=1
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
[ "$fail" = 0 ] && echo "plan ground facts: 6 cases (3 join, 3 access), rule + held + negative explanation pinned"
exit $fail
