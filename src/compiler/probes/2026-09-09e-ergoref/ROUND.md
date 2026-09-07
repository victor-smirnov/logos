# ROUND 2026-09-09e `ergoref` — the `ref` / `ref mut` half of the Rust-2024 binding-modifier rule

Row: `match_ergo_ref_modifier_ref_mode_admit` (tier 2, `admits`) — PRICED, NOT LANDED.
Corpus repair: LANDED, alone, in `06835bf24`, on a compiler that was not touched.

## THE FOUR ARMS AND THE FACT EACH ONE NEEDS

| arm           | door                                              | the fact, and whether the code carries it |
|---------------|---------------------------------------------------|-------------------------------------------|
| `ergorefvd`   | variant payload (`bind_ref_modes` loop)           | CARRIED: `explicit_ref` + `default_ref` are on adjacent lines. ⚠ CRUDE — see rule 9 below. |
| `ergorefvd2`  | same door, refined                                | `explicit_ref && binding_from_wild[k]` |
| `ergorefsf`   | struct-field SHORTHAND `{ ref x }` + tuple element `(ref a, b)` | CARRIED: `fld_is_ref` / `flag(la::IS_REF)` beside `dbm_ref` |
| `ergorefleaf` | the LEAF binder — `{ x: ref v }`, `[ref v]`, tuple-element sub | ⚠ **NOT CARRIED.** `dbm_sub_ty` hands a leaf binder the BARE component type by design, so `build_pattern_impl`'s `dbm_ref` is FALSE at the leaf's own door. Asked at the CONTAINER, which still knows the mode. |

## RULE 9 — TWO NAMES FOR ONE PREDICATE, AND THE CORPUS COULD NOT TELL THEM APART

`binding_is_ref` at the variant-payload door is set by TWO producers: a written `ref`
(`la::IS_REF`, pushed with `binding_from_wild = true`) and the compiler's OWN nested-variant
synthesis (`synth_wants_ref`, pushed with `binding_from_wild = false`). The crude arm asks
`explicit_ref` alone and REFUSES A LEGAL PROGRAM:

    match &e { Outer::W(Option::Some(a)) => … }     // no modifier written anywhere

    error: binding modifiers may only be written when the default binding mode is `move`:
           '__refut_W_0_0' is bound under a by-reference scrutinee

— blamed under a name the programmer never wrote. `binding_from_wild[k]` separates them, and
it is the guard the LANDED `mut` half already asks.

⚠ **AND NO COST COLUMN WOULD HAVE SAID SO.** `ergorefvd` and `ergorefvd2` are IDENTICAL in
every column the harness owns — ceiling 0, cost 0, fail-text 0 of 1436, stdlib all four
layers — because the variant-payload door FIRED ZERO TIMES over the whole ledger+legal
population. Only a hand program separates them, and only because the set varied the SHAPE.

## RULE 1 — THREE OF THE FOUR ZEROS ARE "NEVER ASKED", NOT "NO COST"

`fired 0 times` for `ergorefvd`, `ergorefvd2` and `ergorefsf`. Those sites are proven LIVE by
hand (cx04/05/11/19/20 for the variant door, cx06/08/09 for the shorthand doors) — the zero
is a statement about the CORPUS, not about the mechanism.

## RULE 13 — ADDITIVITY, CHECKED AND NOT ASSUMED

whole (`ergorefall`) = 4 fires / cost 1; parts = 0 + 0 + 4 fires, 0 + 0 + 1 cost. ADDITIVE
here, and the whole cost is carried by the ONE door where the fact is not carried.

## RULE 17 — THE HANDED-DOWN CORPUS LIST WAS WRONG IN THREE WAYS

The row header and the prompt record "12 stdlib sites … 2 pass fixtures". Re-derived by
direct listing on the tree:
  · TEN stdlib LINES, not twelve, carrying FIFTEEN written binders at TEN scrutinees;
  · the `ref mut` spelling is at THREE lines, not two (option 202, result 211, 212);
  · and a THIRD corpus site is missing from the list entirely — `tests/spec/pass/pat_4`,
    the SPEC CONFORMANCE fixture for `pat.struct.field` and `pat.ref.binding-mode`, with
    FOUR written-`ref` sites under `&Pt` / `&mut Pt` scrutinees (lines 40, 41, 63, 64). It
    is the entire measured cost of the mechanism.
