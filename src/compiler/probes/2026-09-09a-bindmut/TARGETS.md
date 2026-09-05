# 2026-09-09a — TARGET ROWS, NAMED BEFORE ANY EDIT

Block = THE `mut`/`ref` BINDING MODIFIER AT BINDER SITES THAT ARE NOT `let`.
`let mut x` mints the fact and every consumer reads it; three other binder sites either have
NO SPELLING for it (the range-for header, the `@`-binding) or DROP it on the floor (a modifier
under a by-reference default binding mode). In every case an ARM THAT EXISTS
(`assignment to immutable variable`, BC's `not declared as mut`, the by-value-`mut` arm
`bind_ref_modes = 0x10`) is reachable only through a fact the code does not carry.

  for_range_mut_var_syntax        3 refuses   grammar (for_stmt takes IDENT) + lower_for hard-codes define(..., true)
                                              + gen_for makes the NAMED var the induction slot (a body write steers the
                                              loop: measured acc = 10, NOT the header's "lost, acc = 3") + BC's For arm
                                              never sets is_mut_binding (`&mut i` refused today, hand h_for_addrmut)
  for_range_var_assign_admit      2 admits    the same define(..., true): the arm exists, the fact is not carried
  at_binding_mut_syntax           3 refuses   grammar (pat_single: `IDENT AT` / `KW_REF IDENT AT`, no `KW_MUT IDENT AT`)
                                              + the payload path pushes binding_is_mut=false for PAT_AT unconditionally
                                              + the top-level PatAt arm of bind_pattern_ref defines with is_mut=false
  match_ergo_modifier_ref_mode_diag 4 diag    bind_pattern's default_ref branch ignores binding_is_mut (and explicit_ref
                                              wins BEFORE the by-ref default is consulted): Rust 2024 rule, one sentence

Aside, same batch because the build is shared, NOT the block's root:
  paren_place_assign_target       3 refuses   lower_place_assign does not unwrap_paren_node (the helper exists, 2 callers)

Grouping test (does ONE change move BOTH members?):
  formutbit alone moves for_range_var_assign_admit (admits -> refused) AND, with the ungated grammar, lets
  for_range_mut_var_syntax COMPILE — but it then runs wrong (acc = 10) until formutslot; so the pair is in SERIES
  (rule 13) and `formutall` is the whole. atmutbit alone closes at_binding_mut_syntax (its program is the payload
  shape); atmuttop is the same fact at the second door. ergomut/ergoref are the two names of one inner predicate
  (rule 9): the modifier kind; ergoref's stdlib cost is EXPECTED non-zero (option/result/cmp write
  `match self { Some(ref v) }` with self: &Self — 2024-illegal by the owner decision).

Why this block over the others: tier 1 = five roots, none shared (09-06c/d); trait_default_body_* is priced (09-04,
cost 0) and waits for a LANDING round; the four remaining tier-3 rows (outlives, struct_binder, dangle_join, closure
two-insts, box_vec_new_infer, dyn_paren, layout_verify) have seven distinct roots. This block is the only one where
one FACT feeds several existing arms, and 09-07a rejected it as "three mechanisms" without measuring — a decline decays.

Predictions BY NAME (queue rows closed, per arm):
  formutbit   -> for_range_var_assign_admit            (1)
  formutslot  -> none (for_range_mut_var_syntax compiles under the ungated grammar but define(true) already did)
  formutbc    -> none alone
  formutall   -> for_range_var_assign_admit, for_range_mut_var_syntax   (2)
  atmutbit    -> at_binding_mut_syntax                  (1)
  atmuttop    -> none alone (the row's program is the payload shape)
  ergomut     -> match_ergo_modifier_ref_mode_diag     (1)
  ergoref     -> none (cost only; stdlib 3 files + lifetime_match_ref_option expected refused)
  parenplace  -> paren_place_assign_target              (1)
  bc ledger: 0 predicted for every arm.
