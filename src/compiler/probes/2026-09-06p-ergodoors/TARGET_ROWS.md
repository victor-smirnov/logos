# ROUND 2026-09-06p (PRICING, soundness queue) — TARGET ROWS, NAMED BEFORE THE COMPILER IS TOUCHED
# Selected on HEAD 0a5e73b05 (clean), build hash READ 95d01d3ae0a1858d 43, queue gate rc 0 on 62.

## THE ROWS
    match_ergo_ref_modifier_ref_mode_admit   tier 2  admits
    match_ergo_nested_tuple_mut_admit        tier 2  admits

## THE ROOT (one predicate, two arguments it is not given)
`SemaChecker::modifier_under_ref_scrutinee` (sema_stmt.cpp) IS the Rust-2024 sentence,
minted once and already asked at four doors. Both rows are that ONE predicate not asked,
for two different reasons — and each reason is a fact that is COMPUTED WITHIN SIGHT OF THE
CALL SITE and simply not passed:

  * ref_modifier: in `build_pattern_variant_data` the loop over payload bindings has
    `default_ref` and `explicit_ref` in adjacent locals; the 2024 check is guarded on
    `!explicit_ref`, so a WRITTEN `ref`/`ref mut` under a by-reference default binding mode
    falls straight into the 2021 arm six lines below. Same shape at the tuple door
    (`push_ref_elem`), the struct door (`fld_is_ref`) and the slice door — each has its own
    IS_REF read with `dbm_ref` in scope.
  * nested_tuple_mut: `build_pattern_impl`'s TUPLE door computes `default_ref`/`default_mut`
    from the scrutinee and then hands a PAT_VARIANT_DATA element `elem_ty` — the BARE element
    type. `build_pattern_variant_data` re-derives the default binding mode from the type it is
    given, sees no reference, and the check early-returns. The mode is a fact of the WALK, not
    of the element's type, and the walk does not carry it.

## WHY THIS BLOCK OVER THE OTHERS (groupings tested by reading, both refuted)
  * {fnparam_array_pattern_binds_nothing, fnparam_tuple_mut_modifier_dropped,
     for_header_pattern_tuple_only, closure_param_struct_pattern_syntax}: four DIFFERENT
     walkers — `emit_for_pattern_destructure` is an SLet-emitting lowerer with its own
     whitelist, the closure spelling is refused by the PEG grammar (rc 4, pre-sema), the
     fn-param binder is a third site. No single change moves two. REFUTED.
  * {let_at_binding_tuple_sub_unsupported, let_at_binding_type_annot_syntax}: the headers
     themselves say one is `lower_let_pat_bound`'s whitelist and the other is the PARSER.
     REFUTED.
  * {array_typed_field_binding_shape_lost, arrayelem_default_ref_mode_not_minted}: a real
     shared root ("the binder carries no ARRAY shape") and a good next block — but its repair
     is a NEW codegen shape class, not an arm that exists. Deferred, named here so the next
     round inherits it.

The chosen block is the shape the prompt says has paid every time: THE ARM EXISTS
(`modifier_under_ref_scrutinee`, already emitting the exact 2024 sentence at four doors) and
is reached through a fact the code does not carry.

## THE OWNER DECISION THAT UNBLOCKS IT
2026-09-09b DECLINED `ergoref` BY NAME because its price was a corpus decision: 12 stdlib
sites and 2 pass fixtures assert the 2021 form. This round's prompt carries the owner's
answer — "match ergonomics follow RUST 2024 ... a `mut`/`ref` binding modifier under a
non-move default binding mode is an ERROR". The decision that blocked the row is made; what
remains is a PRICE, and pricing it is this round's job. Rule 8: that price was read on build
a4e09d1260502d78 and is re-measured here, not copied.

## PREDICTIONS (declared before the batch runs)
  P1 ergoref alone does NOT close match_ergo_ref_modifier_ref_mode_admit: its row header
     names FIVE doors (payload, struct field, if-let/while-let, slice, tuple) and ergoref is
     the payload door only. Predicted: the row's own program refuses, the struct/tuple/slice
     hand twins still compile.
  P2 The cost is NOT additive over the four door probes (rule 13) — the stdlib's 12 sites are
     all payload-door, so ergoref carries the whole stdlib cost and the other three price ~0
     on the corpus while each closing a hand twin.
  P3 ergonest (crude, wrap the element type) has a NON-ZERO runtime cost: wrapping makes every
     nested payload bind by reference, which is a behaviour change for legal programs, not
     just a refusal. ergonestchk (narrow, wrap only when the sub writes `mut`) prices 0.
     Rule 9: the two are predicted to separate on the CORPUS columns, not on hand programs.

## ROW ADDED MID-ROUND (found by an abuse-direction hand program, before any probe was armed)
    nested_variant_payload_under_ref_double_drops   tier 1  run 2
Measured on the BASE binary 95d01d3ae0a1858d, destructor count on stdout, Rust = 1 each:
    tuple door   `match &p { (Option::Some(a), b) }`         2   n07 n08
    struct door  `match &w { W { o: Option::Some(a), k } }`  2   n10
    slice door   `match &arr { [Option::Some(a)] }`          2   n11
    if-let tuple `if let (Option::Some(a), b) = &p`          2   n13
    variant door `match &e { Outer::W(Option::Some(a)) }`    1   n12  CORRECT
    top level    `match &o { Option::Some(a) }`              1   n09  CORRECT
This is the SAME lost fact as match_ergo_nested_tuple_mut_admit — a container door computes
the default binding mode and hands its sub-pattern the bare element type — showing up as a
DOUBLE DESTRUCTOR CALL instead of a missing diagnostic. It is the double free the mode exists
to prevent, at the doors that do not carry it. The block's two rows are therefore THREE, and
the grouping test the prompt demands PASSES for rows 2 and 3: one candidate change (carry the
mode at the container doors) moves both, by construction.
