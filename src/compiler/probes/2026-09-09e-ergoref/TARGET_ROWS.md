# ROUND 2026-09-09e — `ergoref`: TARGET ROWS, named BEFORE the compiler is touched

## THE BLOCK — ONE ROW

    match_ergo_ref_modifier_ref_mode_admit   2   tests/soundness/open/match_ergo_ref_modifier_ref_mode_admit   admits

## WHY THIS ROW OVER THE OTHER 65

1. **It is the second half of a rule the tree already states.** `docs/spec/patterns.md`
   `pat.binding.modifier-requires-move-mode` says all THREE modifiers (`mut`, `ref`,
   `ref mut`) are an error under a non-move default binding mode. The `mut` third landed
   (`SemaChecker::modifier_under_ref_scrutinee`, sema_stmt.cpp; the sentence is minted once
   and asked at four doors). What stands in the tree today is therefore a **2021/2024
   hybrid**: one modifier refused, two admitted, with no rule stating the asymmetry. A
   hybrid is worse than either edition, because no program's verdict can be predicted from
   the spec.

2. **The mechanism is AN ARM THAT ALREADY EXISTS reached through a fact the code does not
   carry** — the shape the prompt says has paid every time.
   * `modifier_under_ref_scrutinee` is the arm. It exists, it is minted once, it emits the
     exact 2024 sentence, and it is already wired at the variant-payload, struct-field,
     tuple-element and nested-variant doors.
   * At the VARIANT-PAYLOAD door (sema_stmt.cpp, the `bind_ref_modes` loop) the separating
     fact `explicit_ref` — a WRITTEN `ref`, read off `la::IS_REF` on the AST binding node —
     is **already computed on the line above the arm** and is used only to choose a mode
     code. The `mut` half's `if` sits between them.
   * At the STRUCT-FIELD shorthand door (`fld_is_ref`) and the TUPLE-ELEMENT door
     (`push_ref_elem`'s `flag(la::IS_REF)`) the written-`ref` fact is likewise local, and
     `dbm_ref` — the by-ref default mode — is in scope in the same function.
   * ⚠ At the LEAF binder door (`build_pattern_impl`'s `PAT_WILD` + `IS_REF` arm) the fact
     is **NOT** carried: `dbm_sub_ty` deliberately hands a leaf binder the BARE component
     type ("a leaf binder consumes the mode at this door"), so `dbm_ref` is false there for
     `S { x: ref v }` / `[ref v]` / a tuple element sub. That is the one door where the fix
     is a carry and not a one-line ask, and it is the door that decides whether this is one
     mechanism or two in SERIES (rule 2).

3. **Its whole price is CORPUS, that price is now MEASURED and PAID, and it is separable.**
   The row was declined twice for cost, not for doubt. This round pays it FIRST, on the
   UNMODIFIED compiler, in its own commit: 10 stdlib lines re-spelled `match *self` /
   `match *other` and two pass fixtures re-spelled, each keeping its subject.

4. **Grouping test — does one candidate change move a second row?** Applied to the two
   ergonomics-adjacent rows in the queue and REFUTED for both, so this block is ONE row and
   not three:
   * `arrayelem_default_ref_mode_not_minted` (`refuses`) — its root is `mint_dbm_ref` /
     `dbm_sub_ty` REFUSING to mint for Array/Slice because no codegen ref-bind carries an
     array shape. That is a codegen capability, not a modifier check; a written-`ref`
     refusal neither reaches it nor repairs it.
   * `struct_pattern_name_check_skipped_under_ref` (`admits`) — a FIELD-NAME existence
     check skipped on a by-ref path. Same door, different predicate; the modifier arm fires
     after the field is resolved and cannot supply a missing name check.

## THE PREDICTION, BY NAME, BEFORE THE RUN

Written in `PREDICTION.md` in this directory: the counter-example set, with the verdict each
program must have BEFORE and AFTER, including the four LEGAL shapes an over-refusal would
break (`&`-pattern reset, by-value scrutinee, `*`-deref scrutinee, compiler-synthesised
sub-pattern with no modifier written anywhere) and the top-level-binder shape, where Rust
2024 keeps the mode `move` and the modifier LEGAL.
