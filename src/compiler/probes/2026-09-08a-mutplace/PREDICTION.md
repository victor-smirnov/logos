# Round 2026-09-08a (landing, soundness queue) — PREDICTION, written before any edit

CLASS (by property, not spelling): a PLACE EXPRESSION in a MUTABLE-USE POSITION whose projection
chain crosses a trait step (Deref / Index) must take the MUTABLE trait step (DerefMut / IndexMut),
refuse when only the shared trait is implemented, and ask the receiver's binding-mut question.

Mutable-use positions (the Rust reference's mutable place expression contexts), enumerated:
  1. LHS of `=`            — PLACE_ASSIGN (FIELD_READ / INDEX_READ / TUPLE_INDEX / DEREF place), DEREF_WRITE
  2. LHS of `op=`          — lower_place_compound_assign, DEREF_COMPOUND
  3. operand of `&mut`     — the addr-of-mut arm (`&mut *x`, `&mut x.f`, `&mut x[i]`, `&mut <place>`)
  4. `&mut self` receiver  — lower_method_call (already: target_method_wants_mut_self) — NOT touched
  5. match scrutinee with `ref mut` — lower_match / lower_match_expr (already armed) — NOT touched
  6. `let ref mut x = place` — not enumerated in the tree today; noted, not this round
Projections that must PROPAGATE the position to their base: DEREF, FIELD_READ, INDEX_READ,
TUPLE_INDEX, PAREN_EXPR. Trait steps that CONSUME it: the Deref step (lower_deref, the field
auto-deref loop, the index auto-deref loop) and the Index step (lower_index_read).

Today (HEAD a9c7b67fd, measured on 32 hand programs c01-c32 in scratchpad/hand2/before.txt):
  the position reaches only: `&mut x.f` (FIELD_READ receiver chain), `&mut *x` (one level),
  DEREF_COMPOUND (one level), match scrutinee. Everything else lowers the trait step SHARED.

PREDICTED CLOSED, by name (7 queue rows):
  boxbox_mut_deref, box_deref_assign_mut_let, box_write_raw_ptr_diag,
  derefonly_compound_write_admit, user_derefmut_write_leaks_old,
  derefmut_compound_recv_immut_admit, box_in_vec_deref_write
PREDICTED NOT CLOSED: every other row (21).
NEW DEFECTS FOUND by the counter-examples, predicted CLOSED by the same change (become fail pairs):
  c03 `(*w).f = 7` Deref-only ADMITTED · c04 `w.f = 7` Deref-only ADMITTED ·
  c26 `w.f += 6` Deref-only ADMITTED · c17 `h.m[0] = 9` Index-only through a field ADMITTED
NEW DEFECT NOT this class: c22 `(*b) = 7` "invalid assignment target" (a PAREN place on the LHS) — queue row.
DEBT: c30 / tests/imported/fail/borrowck/borrowck-issue-14498--box-mut-ref (E0506 pinned as
  "behind a `&`"): must STAY refused, with E0506.
bc ledger: borrowck_many-mutable-borrows (E0596) if still a row — the check_recv_conflict binding-mut arm.
COST prediction: 0 pass / 0 cfail rc / stdlib four layers / runtime 0.
