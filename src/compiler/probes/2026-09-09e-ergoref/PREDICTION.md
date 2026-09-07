# 2026-09-09e `ergoref` — PREDICTION, written BEFORE the arms were installed

Row: `match_ergo_ref_modifier_ref_mode_admit` (tier 2, `admits`).
Rule: `pat.binding.modifier-requires-move-mode` — a written `mut` / `ref` / `ref mut` is an
ERROR under a non-move default binding mode (Rust 2024, owner decision 2026-09-05). The
`mut` third is landed; this is the `ref` / `ref mut` two thirds.

## THE ARMS

  ergorefvd    variant-payload door — `bind_ref_modes` loop, ask when `default_ref &&
               explicit_ref`. The separating fact (`explicit_ref`, off `la::IS_REF`) is
               already on the line above.
  ergorefsf    the two SHORTHAND container doors — struct field `S { ref x }`
               (`fld_is_ref`) and tuple element `(ref a, b)` (`push_ref_elem`'s
               `flag(la::IS_REF)`). `dbm_ref` is in scope at both.
  ergorefleaf  the LEAF binder door, reached through `dbm_sub_ty` — `S { x: ref v }`,
               `[ref v]`, a tuple-element sub. ⚠ THE FACT IS NOT CARRIED HERE: `dbm_sub_ty`
               hands a leaf binder the BARE component type by design, so `dbm_ref` is false
               at the leaf's own door and the ask has to happen at the container, where the
               mode is still known.
  ergorefall   all three at once (rule 13 — check additivity, do not assume it).

## THE COUNTER-EXAMPLE SET — verdict BEFORE / AFTER, by name

Measured on the base binary 77beab6f631806d5 43 (the corpus-repair commit). BEFORE column
is the MEASUREMENT, not a guess.

  ILLEGAL under 2024 — must go COMPILES -> REFUSED with the 2024 sentence:
    cx04_variant_ref_under_ref          variant payload, `&`      rc 0  ->  refused
    cx05_variant_refmut_under_mutref    variant payload, `&mut`   rc 0  ->  refused
    cx06_struct_field_shorthand_ref     `P { ref x, y }`          rc 0  ->  refused
    cx07_struct_field_sub_ref           `P { x: ref v, y: _ }`    rc 0  ->  refused
    cx08_tuple_elem_ref                 `(ref a, b)`              rc 0  ->  refused
    cx09_tuple_elem_refmut              `(ref mut a, b)`, `&mut`  rc 0  ->  refused
    cx10_slice_elem_refmut              `[ref mut p, q]`, `&mut`  rc 0  ->  refused
    cx11_iflet_ref                      `if let Some(ref v) = &o` rc 0  ->  refused
    cx19_or_pattern_ref                 both or-alternatives      rc 0  ->  refused
    cx20_struct_payload_ref_under_ref   struct-typed payload      rc 0  ->  refused

  LEGAL — must STAY compiling and keep the same exit code (an over-refusal here is the
  most expensive thing in this queue to get wrong, and rule 5 says the set must vary the
  SHAPE, not the count):
    cx01_amp_pattern_ref     `match &o { &Some(ref v) }`  — the `&`-pattern RESETS the mode
    cx02_byvalue_ref         by-value scrutinee, written `ref`
    cx03_deref_scrutinee     `match *o { Some(ref v) }`   — the stdlib repair's own shape
    cx13_synth_nested_variant `match &e { Outer::W(Option::Some(a)) }` — NO modifier written
                              anywhere; the inner `a` is the COMPILER's own by-ref synthesis,
                              and `binding_is_ref` cannot tell it from a written `ref`. This
                              is the exact program an earlier price for this door refused.
    cx14_nested_tuple_no_modifier `match &p { (Some(a), b) }` — same trap at the tuple door
    cx15b_toplevel_ref_under_ref  `match &o { ref w => }` — Rust 2024 leaves the TOP-LEVEL
                              default binding mode `move` (the mode shifts only when a
                              non-reference pattern is matched THROUGH the reference), so
                              this modifier is legal and `w` is `&&Option<i64>`
    cx16_toplevel_mut_under_ref   the same shape for the LANDED `mut` half — a control on
                              the half already in the tree

## PREDICTED COST COLUMNS

`pass` and `stdlib` must both be 0 for `ergorefall` AFTER the corpus repair, and the repair
is committed FIRST and separately precisely so that a non-zero reading names a site the
repair missed rather than being absorbed into a co-landing.
