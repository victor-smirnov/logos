# 2026-09-09b — PREDICTION, WRITTEN BEFORE ANY SOURCE EDIT

Landing `bindmut` (the 09-09a block, minus ergoref): grammar (for mut ×2, `mut x @` / `ref mut x @`),
SFor var_mut carriage + the cap-9 map, lower_for define(hdr_mut), gen_for own per-iteration slot (always),
BC For arm var_mut, PatAt mint into the side-set, the side-set consulted at EVERY define in the pattern
binder family (Tuple / At / struct field / let-else) — the CLASS, enumerated by property on HEAD
(hand b01–b17: match tuple `(mut a, b)`, match struct `P { mut x }`, `let Some(mut x) = o else`
all REFUSED "assignment to immutable variable" — legal Rust), the 2024 `mut`-under-by-ref sentence at
the variant door and the struct door, lower_place_assign paren unwrap.

QUEUE ROWS CLOSED — predicted 5, by name:
  for_range_var_assign_admit
  for_range_mut_var_syntax
  at_binding_mut_syntax
  match_ergo_modifier_ref_mode_diag
  paren_place_assign_target
NOT closed (untouched doors, by name): at_binding_top_addrof_nomut_admit (BC At arm has no mode),
  match_ergo_nested_tuple_mut_admit (tuple door has no default mode), labeled_foreach_label_lost,
  let_tuple_destructure_ref_scrutinee, paren_var_assign_target, match_tmp_wild_mut_addrof.
bc_admits / bc_admits_blocked: 0 closed, 0 opened (98 / 25).
Cost prediction: pass 0, cfail 0 (rc AND text), stdlib four layers ok, runtime 0 of 6423.
Rows to ADD if they reproduce after the landing: `let mut n @ _` (let has no `@`), ergoref admit
  (`match &o { Some(ref v) }` legal today, 2024-illegal; cost = 12 stdlib sites + 2 pass fixtures).
