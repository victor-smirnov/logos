# 2026-09-13b-selfregion-land — PREDICTIONS, WRITTEN BEFORE THE LANDING BUILT

⚠ Written AFTER the source edit and BEFORE any build or measurement of it (the prompt asks for
before-the-edit; the edit came first this round, stated here rather than hidden).
Base build `a5088a6875e092aa 43`, HEAD `c3370b6fc`. Landing = the union of `implanonx` + `selfvg` +
`selfvee` + `selfveu` (2026-09-13a), un-gated, refactored into one helper
(`sema_impl.hpp::name_impl_anon_lts_`), PLUS three named differences from the probes:
  X1 lower_impl_block seeds the RENAMED header type where it seeded `target_resolved` (ref / tuple /
     fn targets and a concrete-type-arg target) — the probes left those `'_` unrenamed in the body.
  X2 `Self::V { .. }` / any ENUM_LIT_DATA spelled `Self` is checked like `Self::V(..)` (lower_enum_lit_data)
     — a neighbour the pricing round never enumerated.
  X3 rendering: `'__anonN` prints as `'_` (type_str), a mismatch that mentions one appends
     "`'_` in the impl's self type is a region of the whole impl, not an elided lifetime of this
     function", and two distinct anonymous binders get their own sentence (`'_` #i and #j).
  The Self-literal check reads a LOCAL captured at the top of lower_struct_lit, not `hint_struct_type_`.

## bc_admits.ledger — NUMBER 2
  closed: {issue-55394--b (nllmoves.NEW-3), issue-98170 (nllmoves.R13)}. bc_admits_blocked: none moves.
## soundness_queue.ledger — NUMBER 0
  self_literal_local_borrow_pins_impl_region_admits stays admits (doors in series);
  self_tuple_struct_ctor_refused stays; enum_variant_value_into_scalar_let_admits stays;
  closure_elided_param_called_with_field_ref_refuses stays.
## HAND BATTERY (scratchpad land/hb, this round's own shapes; base verdicts in base_hb.txt)
  ILLEGAL newly REFUSED: B02 B03(X2) B04(X3 sentence) B06 B08 B09 B10 B11 B12.  B01 B07 stay refused.
  LEGAL newly COMPILING (refused on base): A09 (run 19), A10 (run 20)  — both via X1, medium confidence.
  LEGAL unchanged, compile+run: A01 11 A02 12 A05 15 A06 16 A07 17 A08 18 A11 21 A12 22 A13 23 A14 24
        A16 26 A17 27 A18 28 A19 29 A20 31 A21 32 A22 33 A23 36.
  LEGAL refused on base and PREDICTED STILL REFUSED (low confidence; the value carries the
        declaration's `'s` from a pattern binding, a different fact): A03 A03b A04 A04b.
## PRICING-ROUND BATTERY (scratchpad ctl ctl2 ctl3 ty)
  = selfvee ∪ selfveu of 2026-09-13a: refused s1 s3 r13a r13b n3a I1 I3 I5 e5 e6 h1 s5 s12 r13d I2 h2 h15;
  L1 (61) L14 (75) compile; h5 admitted; t9 refused; every recorded legal program unchanged.
## COLUMNS
  L4 bc: only the two `logos_00_bc_admit_*` tests of the closed rows turn (they move to fail fixtures).
  run_oracle: 0 changed of 6703. fail_text_oracle: 0 un-refusals; text changes only where a pinned
  diagnostic prints a header `'_` type (expected small, list not predicted). stdlib: ok.
