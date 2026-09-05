# Round 2026-09-07a (pricing, soundness queue) — TARGET ROWS, named before any edit

Block: THE MUTABLE-PLACE CONTEXT DOES NOT REACH THE Deref STEP.
  boxbox_mut_deref          (tier 3, refuses)  `&mut **bb`  — the ADDR_OF-mut/DEREF arm lowers its
                                               operand `*bb` with `mut_place_ctx_` FALSE, so the inner
                                               step is `bb.deref()` (a `&Box<i64>`) and the outer
                                               `deref_mut` crosses a `&` — "behind a `&` reference".
  box_deref_assign_mut_let  (tier 3, refuses)  `*bb = 7`    — DEREF_WRITE asks a hand-rolled
                                               `impls_["DerefMut::Box"]` + `find_func_by_base_and_signature`
                                               (concrete symbols only) instead of `emit_generic_deref_call`
                                               (which DEREF_COMPOUND already uses at the same site), so a
                                               generic stdlib impl is never found and the Box falls to
                                               the raw-pointer arm.
  box_write_raw_ptr_diag    (tier 4, diag)     `*b = 7`, b not mut — same site, same fall-through; the
                                               right sentence is the borrow checker's (E0594/E0596),
                                               reached only once the step is a `deref_mut` call.

Why this block: every row is an ARM THAT EXISTS (`emit_generic_deref_call(want_mut=true)` — the
`&mut *b` and `*b += 1` spellings compile today) reached through a FACT THE CODE DOES NOT CARRY
(the mutable-use context of a NESTED deref / the generic impl at the write site). Not one site —
two doors, possibly in series for the nested-write shape (`**bb = 7`) — so priced whole first,
then decomposed (Rule 13).

Rejected for this round, with the reason:
  for_range_mut_var_syntax + for_range_var_assign_admit + match_tmp_wild_mut_addrof + at_binding_mut_syntax —
    a plausible shared root ("a by-value binding from a non-`let` binder is an SSA value, not a
    slot; its `mut` is unspellable or dropped") but TWO of the four are grammar holes and the
    admit needs a REFUSAL plus a codegen slot — three mechanisms, not one fact. Second choice.
  trait_default_body_{type,call} — one root, priced 09-04 at cost 0 and declined on the wrong
    criterion; it is a LANDING candidate, not a pricing one.
  tier 1 rows — five roots (measured 09-06c/d), none shared with this block.
