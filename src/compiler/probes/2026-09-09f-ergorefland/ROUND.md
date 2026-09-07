# ROUND 2026-09-09f `ergorefland` — the `ref` / `ref mut` half LANDED, and the class it belongs to

Row CLOSED: `match_ergo_ref_modifier_ref_mode_admit` (tier 2, `admits`).
Row OPENED: `tuplestruct_door_default_ref_mode_not_carried` (tier 1, `run 2`).
`# TOTAL` 67 -> 66 -> 67, re-derived BY DIRECT LISTING at each step.

## THE CLASS, AND WHY THE ROW IS NOT IT

PROPERTY: *a modifier the PROGRAMMER WROTE, reached while the default binding mode is
by-reference*. Enumerating the doors from `build_pattern_impl`'s own `pc == la::PAT_*`
dispatch (not by grepping `IS_REF` — the compiler's refutable-sub synthesis sets that bit
with no modifier written anywhere) gave a table with TWO members that belong to the LANDED
`mut` third, not to the row: a written `mut` at a LEAF binder — `match &p { P { x: mut v } }`
and `match &arr { [mut a, b] }` — was ADMITTED, because `dbm_named_bind` rejects `IS_MUT`
and no other site sees a leaf. Closing only the row would have left two siblings failing the
same way. Both are closed here, by the same site, and pinned with their own pairs.

## A FIFTH DOOR, FOUND BY WALKING THE DISPATCH AFTER THE FIRST BUILD

`match &o { Option::Some(ref w @ 1i64..=9i64) }` compiled. The payload loop's `@`-binding
arm pushes `binding_is_ref = false` for `ref n @ sub` ("binds by value here"), so the
written `ref` is DISCARDED and nothing downstream can see the keyword; the `mut` spelling
was already refused because `binding_is_mut` IS carried. One line at that arm closes it,
and the TOP-LEVEL `ref w @ Some(_)` over `&o` stays legal (mode `move`).

## THE ONE STRUCTURAL CHANGE — FOUR ASKS, ONE MINTED SENTENCE

The three container doors (PAT_TUPLE, PAT_STRUCT, PAT_SLICE) funnel every sub-pattern
through ONE lambda, `dbm_sub_ty`. Asking there — for `IS_REF` **and** `IS_MUT` — answers the
struct-field sub, the struct-pattern tuple index, the tuple-element sub and the slice/array
element at a single site. Two SHORTHAND spellings are consumed at their own door and never
reach it (`S { ref x }`, `(ref a, b)`), and the variant payload has its own loop. So the
rule is asked at four places (the fourth is that `@`-binding arm), all calling
`SemaChecker::modifier_under_ref_scrutinee`.

## WHERE THE FIX DIFFERS FROM ITS PROBE

  * `ergorefvd` (the crude variant guard) is NOT landed. It asks `explicit_ref` alone and
    refuses the LEGAL `match &e { Outer::W(Option::Some(a)) }`, blaming the synthesized name
    `__refut_W_0_0`. The landed form is `ergorefall2`'s: `explicit_ref && binding_from_wild`.
  * `ergorefleaf` asked only `IS_REF`. The landed form asks `IS_REF || IS_MUT`, which is the
    class extension above and closes two shapes no probe in the pricing round measured.

## RULE 2 — TWO PREDICTED REFUSALS DID NOT HAPPEN, AND THE DOORS ARE IN SERIES

`match &o { Option::Some(P { ref x, y }) }` and `match &t { TS(ref a, b) }` still compile.
Neither is a gap in this rule: at both doors the default binding mode NEVER SHIFTS, so there
is no shifted mode for a modifier to violate. Controls, both measured: the LANDED `mut` third
misses both doors identically (`Some(P { mut x, y })` and `TS(mut a, b)` compile). The first
is `pat.binding.default-mode-carried-into-subpatterns`' recorded divergence (queue row
`variant_payload_nested_struct_sub_double_drops`). The second had no row at all — see below.

## THE NEW ROW — A DOUBLE FREE FOUND BY A MODIFIER COUNTER-EXAMPLE

`tuplestruct_door_default_ref_mode_not_carried` (tier 1, `run 2`). The TUPLE-STRUCT pattern
door implements no default binding mode: `match &t { TS(a, b) => { let x: i64 = a; } }`
COMPILES, so `a` is `i64` by value where every other door binds `&i64`. With a move-only
element the by-value binding is Drop-scheduled at arm exit AND the scrutinee drops it at
scope end — `D::drop` runs TWICE (exit 2; Rust exits 1), read through a `static mut` after
the scope has ended so the wrong answer is an EXIT CODE. TWO SPELLINGS OF ONE PATTERN
DISAGREE: `TS { 0: d, 1: k }` over the same tuple struct exits 1, and so does a named-field
struct. INHERITED — exit 2 on the base binary and on this one.

## THE CORPUS, PAID IN FULL

`06835bf24` paid ten stdlib lines and two fixtures on an untouched compiler. THREE MORE
sites, priced by 2026-09-09e and repaired HERE, each verified rc 0 on the BASE binary before
the compiler was touched, so no half hides inside the other:

  * `tests/spec/pass/pat_4.logos` — the spec-conformance fixture for `pat.struct.field` and
    `pat.ref.binding-mode`, lines 40, 41, 63, 64 → `match *p`. Its `@rule` comments describe
    the FIELD FORMS (`ref name`, `ref mut name`) and the BINDING MODES, both of which the
    repair still exercises at all four sites with the same exit codes; nothing is re-pinned.
  * `tests/logos/pass/match_struct_move_field_drop.logos` line 12 → `match *p`, and the
    header comment that spelled the shape `match &p` corrected.
  * `tests/imported/pass/binding/match-ref-binding-mut.logos` line 16 → `match *x`, which
    RESTORES the rust-lang/rust source: its own header recorded that the import had rewritten
    `match *x` to `match x`. The imported-Rust corpus had been edited AWAY from Rust into a
    shape Rust itself forbids.
