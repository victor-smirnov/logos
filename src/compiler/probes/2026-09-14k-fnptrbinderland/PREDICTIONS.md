# 2026-09-14k-fnptrbinderland — PREDICTIONS, by name, written before the first edit

Base: HEAD 31f0b0989, build/ build_hash 4bc75379010a5384 43 (read), gate-db build 1220 (-L bc 3443 passed / 0 failed / 2 other).

## The change (one fact: a fn value's OWN regions, carried by the type, not guessed from a name set)
  H   resolve_type(FN_PTR_TYPE) renames the written `for<'r>` binders apart to binder tokens `'%hN` (a registry keeps the
      written name; type_str prints the written name, and a variance sentence prefixes `for<'r>` only where the written
      name collides with a free name of the pair).
  M   every site that MINTS a FnPtr from a fn ITEM's signature renames that item's OWN lifetime parameters to binder
      tokens: the if-join and the match-join of two fn items (sema_expr lower_if_expr, sema_stmt match), the generic
      turbofish ref (fi.lifetime_params), the inherent/trait method path (mfi.lifetime_params; impl lifetimes stay names).
  E'' check_variance, sup a FnPtr and sub a fn value: the sup's elided slots are fresh rigid placeholders (an elided
      return is the sole elided input's); the sub's OWN regions — an elided slot, a binder token, a closure-minted
      region, every non-'static name of a FnItem — take the sup's region by position, a MEET when offered two.
      NO current_lt_binders() test (fnptrelide2's undeclared-name rule is dropped — rule 12).

## Ledger rows, by name
  bc_admits: issue-54124 (nllmoves.NEW-L1) and issue-101280 (lifereg.NEW-N4) REFUSED.  64 -> 62.
  soundness_queue CLOSED (4): fnptr_elided_param_let_from_named_admits (refused), fnptr_elided_param_return_from_named_admits
      (refused), fnptr_item_named_binder_vs_fnptr_type_refused (compiles, runs 32), fnptr_hrtb_sub_binder_to_named_return_refused
      (compiles).  176 -> 172.
  soundness_queue UNCHANGED: fnptr_elided_param_struct_literal_admits, fnptr_elided_param_call_arg_admits,
      fnptr_elided_param_nested_option_admits, fnptr_assign_named_to_elided_admits, fnptr_call_result_region_param_reads_static_refused,
      fnitem_to_fnptr_array_elem_admits, arraylit_closure_elem_fnptr_refused, and every other row.

## Hand battery (hb.dkj4, 52) — by name
  legal h01..h32 + h08a: ALL compile and run, INCLUDING h25 (the refinement's one refusal) and h20 h22 h27 h28 h08a.
  h08 (whole) and h08b: still refused (the call half, a different fact).
  illegal refused: i01 i02 i03 i04 i05 i06 i07 i12 i13 i14 i15 i16 i17 i18.  still admitted: i08 i09 i10 i11.

## Counter-examples written for this landing (cx, 36) — shapes the pricing did not use
  legal, compile and run: c01 (generic turbofish with a NAMED item region) c02 (method path, own region) c04 c06 c08 c09 c10
      c12 c13 c14 c15 c17 c19 c20 c21 c23 c25 c27 c30 (MATCH-join of items) c35 c36 (turbofish colliding with the scope's
      'a) c37 (if-join into a `for<>`) c38 c39 c40 c41 c42.
      ⚠ c01 c02 c36 c30 are refused by fnptrelide2's arm WITHOUT M (predicted: a FnPtr carrying an item's named region is
      rigid once the name-set rule is gone) — M is what they price.
  illegal (by reading), refused: x01 x03 x06 x07 x08 x09 x10.  still admitted: x05 (tuple element: nested, no carrier).
  unknown legality (recorded, not claimed): u01 (a callee region in a returned fn pointer, at a local borrow) — predicted refused.

## Cost columns
  fail-text: 0 changed (the three fixtures H re-worded in pricing print their written binder names again; no for<> prefix
      there — no written name collides with a free name in those pairs).
  pass / stdlib / runtime: 0.

## Neighbour extensions — two probe names in the same build, written before that build finished
  fnptrstrictf  (outlives.hpp permissive default: a sup placeholder '%f is never met by it) — the strict extension for the
      permissive doors. PREDICTED: closes fnptr_elided_param_struct_literal_admits (i08) and fnptr_elided_param_call_arg_admits
      (i09); legal c17 c20 c13 h09 h29 h11 keep compiling. Unknown: any pass fixture passing a named-region fn pointer to an
      elided-region fn-pointer parameter.
  fnptrnested   (check_variance: the same two views at a fn pointer nested in a Ref/Tuple/Array/Struct/Enum argument) —
      the strict extension for the nested door. PREDICTED: closes fnptr_elided_param_nested_option_admits (i10) and x05
      (tuple); legal c19 c41 c06 h10 h30 keep compiling. Unknown: an invariant container (Vec is read invariant here).
  fnptr_assign_named_to_elided_admits (i11): no probe — lower_assign asks no variance question outside a refuted probe.
  fnptr_call_result_region_param_reads_static_refused (h08b): not this fact; unchanged under every name.
