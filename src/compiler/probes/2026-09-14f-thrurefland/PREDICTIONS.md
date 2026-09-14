# 2026-09-14f-thrurefland — PREDICTIONS, written before any compiler edit (base baa2a38e8b650fb3 43, read)

Landing the pricing round 2026-09-14e-thruref's two funded arms, each widened to its class:

## D2 — an OVERWRITE of a place does not conflict with a field loan whose path crosses a REFERENCE below that place
Rust: places_conflict, a Shallow(None) write against a borrow whose remaining projection holds a Deref. Two readers
decide it from the same fact (a dotted loan under a reference-typed prefix names the pointee):
  (a) visit_stmt Assign -> field_borrow_conflicts "assign to" (a whole LOCAL overwritten; asgref = the root-is-a-reference case)
  (b) visit AddrOfTemp under a DerefWrite RETARGET (`h.r = &s2`, the place's own type a reference)
Helper: walk the loan path's types from the root (tuple index, struct field incl. a type-parameter field); any reference at a
prefix at or below the written place => the loan is behind it. An unknown step answers "not behind" (the conflict stands).

QUEUE rows closed (161 -> 159 before new rows):
  refvar_assign_conflicts_with_pointee_field_loan_refused        (reader a, root a reference)
  place_assign_ref_field_while_pointee_loan_refused              (reader b)
NOT closed, reason 1 (no carrier: VarState has no bit separating `&*r` from `&r`): refvar_reborrow_whole_then_assign_counter_refused
bc_admits: none (issue-51117 still needs D1 and D3).

HAND (land14f_d2), LEGAL — must compile and RUN: d01 d02 d03 d06 d09 d12 d16 d18 d22 d24 d29 d30 d31 g01 p02
HAND, ILLEGAL — must stay refused (sentence read): d04 d05 d08 d14 d15 d19 d20 d21 d23 d33 g02 p01
UNCERTAIN (a re-borrow of the SAME target while a loan taken through the old reference is live; the owner-side loan may have
been held only by `r`): d07 d17 d26 d27 d28 d32 — if any opens, it is repaired at its door or the landing of D2 is declined.
Fixture pins unchanged: pass bc_patloan_ergonomic_ref_local_reassign_admit, fail regions-pattern-typing-issue-19997.

## R2 — a comparison operator on pointer-like operands needs a COMMON type
`==` `!=` `<` `<=` `>` `>=`; `&`/`&mut` layers peel pairwise (PartialEq<&B> for &A); below them a raw or fn pointer pair meets:
regions under co/contravariant positions always meet, under an invariant position (a `*mut`/`&mut` pointee) they must be
equal or mutually outlive.
bc_admits CLOSED: type-check-pointer-comparisons (65 -> 64) — one error in each of compare_const, compare_mut, compare_fn_ptr,
none in compare_hr_fn_ptr, compare_const_fn_ptr.
HAND (land14f_r2), ILLEGAL — refused: q07 q10 q11 q13 q14 q15 q17 q19 q25 q31 q32 q34, and q01 q24 by the region sentence
(before the MLIR crash they reach today).
HAND, LEGAL — unchanged verdict: q06 q08 q09 q12 q16 q18 q20 q21 q26 q27 q28 q29 q30 q33 (q03 keeps its wrong run rc 1, a
separate defect; q04 q05 q22 q23 keep their unrelated refusals). UNCERTAIN: q08 (mutual outlives at an invariant position —
subtype()'s Inv arm compares names), q33 (mixed variance with a bound).

## Costs predicted
pass 0 · cfail 0 changed (no fail fixture's first text moves) · stdlib 4 of 4 · runtime 0 changed (cast-region-to-uint subtracted).

## New queue rows expected from the base battery (reproduced on baa2a38e8b650fb3 before any edit)
  nested_ref_eq_compares_addresses_run         `&&i64 == &&i64` / `&mut &i64 == &mut &i64` compare ADDRESSES (run 1; Rust 7)
  rawptr_int_zero_compare_mlir_verifier_refused `p == 0` on `*mut i64` (spec expr.binop.ptr-null-compare) dies in the verifier
  rawptr_tuple_eq_no_eq_impl_refused           `(p, 1) == (q, 1)` with `p: *mut i64`: raw pointers implement no Eq
Queue # TOTAL predicted 161 - 2 + 3 = 162.
