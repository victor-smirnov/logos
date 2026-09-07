# 2026-09-09f `ergorefland` — PREDICTION, written BEFORE the armed binary was measured

Written after the source edit and BEFORE any build of it. Nothing below was read off a run.

## THE CLASS, ENUMERATED BY THE PROPERTY

PROPERTY: *a pattern node carrying a modifier the PROGRAMMER WROTE (`mut`, `ref`,
`ref mut`) that is reached while the default binding mode is by-reference.* Not "a node with
`la::IS_REF` set" — that is a SPELLING, and it certifies what it cannot see: the compiler's
own refutable-sub synthesis sets the same bit with no modifier written anywhere.

The doors are the pattern-construction sites that can hand a NAMED binder a component of a
by-reference scrutinee. Enumerated from `build_pattern_impl`'s own `pc == la::PAT_*`
dispatch plus `build_pattern_variant_data`:

| door | written `ref` / `ref mut` | written `mut` |
|---|---|---|
| VARIANT payload `Some(ref v)` | admitted (row) | REFUSED (landed) |
| STRUCT field SHORTHAND `{ ref x }` | admitted | REFUSED (landed, `pat_mut_name`) |
| TUPLE element `(ref a, b)` | admitted | REFUSED (landed, `pat_mut_name`) |
| STRUCT field SUB `{ x: ref v }` — a LEAF | admitted | **ADMITTED** |
| TUPLE element SUB — a LEAF | admitted | REFUSED (landed) |
| SLICE / array element `[ref a]` — a LEAF | admitted | **ADMITTED** |
| struct-pattern TUPLE INDEX `{ 0: ref a }` — a LEAF | admitted | (leaf, same arm) |
| PAT_REF `&pat` | mode RESET — legal, must stay legal |
| PAT_AT / PAT_WILD at TOP LEVEL | mode is `move` — legal, must stay legal |
| `let` struct/tuple door, fn-param door, closure-param door | not a member: those doors REFUSE a reference scrutinee outright today (queue rows `letstruct_*`, `fnparam_*`) — no by-ref default mode exists there to violate |

⚠ **THE ROW IS NOT THE CLASS.** Two members are holes in the *landed `mut` third*, not in the
`ref` two thirds the row names: a written `mut` at a LEAF binder is admitted, because
`dbm_named_bind` rejects `IS_MUT` and no other site sees it. Fixing only the row would close
the instance and leave two siblings failing the same way.

## THE ONE STRUCTURAL CHANGE

The three container doors (PAT_TUPLE, PAT_STRUCT, PAT_SLICE) all funnel their sub-patterns
through ONE lambda, `dbm_sub_ty`. Asking the rule in its LEAF arm — for `IS_REF` **and**
`IS_MUT` — answers four spellings at one site. Two shorthand spellings never reach it
(`{ ref x }` and `(ref a, b)` are consumed at their own doors) and the variant payload has
its own loop, so the rule is asked at exactly THREE sites, all calling the one minted
sentence `SemaChecker::modifier_under_ref_scrutinee`.

## PREDICTED CLOSED SET — A NUMBER AND A LIST

**ONE queue row**, by name:

    match_ergo_ref_modifier_ref_mode_admit   2   admits

`# TOTAL` 67 -> 66, tier2 count -1. No other row moves. Specifically PREDICTED NOT TO MOVE:
`arrayelem_default_ref_mode_not_minted` (codegen capability, `mint_dbm_ref`),
`struct_pattern_name_check_skipped_under_ref` (a field-name check, different predicate),
`match_tuple_door_nested_struct_binds_nothing`, `let_tuple_destructure_ref_scrutinee`,
`fnparam_struct_ref_mut_field_binds_byvalue`, `toplevel_refbind_over_ref_scrutinee_segv`.

## PREDICTED VERDICTS — the counter-example set, MEASURED before, PREDICTED after

Base binary: `build/bin/logosc` at `6e10b5ffbceba8b4 43` (HEAD e8a4c58b1, probes unarmed).

ILLEGAL — measured COMPILES rc 0 on the base, predicted REFUSED with the 2024 sentence:

    my01  variant payload THEN struct shorthand   match &o { Some(P { ref x, y }) }
    my02  TUPLE-STRUCT pattern                    match &t { TS(ref a, b) }
    my03  struct-pattern TUPLE INDEX              match &t { TS { 0: ref a, 1: b } }
    my04  ARRAY element under SHARED `&`          match &arr { [ref p, q] }
    my05  nested variant `ref mut` under `&mut`   match &mut o { Some(Inner::A(ref mut v)) }
    my07  DOUBLE reference scrutinee `&&Option`   match &r { Some(ref v) }
    my08  struct sub `ref mut` under `&mut`       match &mut p { P { x: ref mut v, .. } }
    my12  ⚠ CLASS HOLE, `mut` at a LEAF           match &p { P { x: mut v, .. } }
    my13  ⚠ CLASS HOLE, `mut` at an ARRAY elem    match &arr { [mut a, b] }

LEGAL — measured COMPILES rc 0 on the base, predicted STILL COMPILES rc 0:

    my11  for-header `(ref a, b)` over a by-value array — the mode is `move`
    my20  `&`-pattern reset with a struct shorthand under it
    my21  `match *self` inside a GENERIC impl — the stdlib repair's shape
    my22  THREE synthesized container levels, NO modifier written anywhere
    my23  array elements, no modifier          my24  struct shorthand, no modifier
    my25  tuple-struct, no modifier            my26  top-level `ref w @ Some(_)` over `&`
    my27  a match GUARD, no modifier           my28  `&mut`-pattern reset with `ref mut`
    my29  BY-VALUE scrutinee, `ref` at a leaf TWO levels down
    my30  `match *mp` through a `&mut`, `ref mut` still writes through

NOT MEMBERS — measured REFUSED on the base already, must be UNCHANGED (rule 14: an
inherited refusal buys nothing):

    my06  struct pattern inside a tuple binds nothing (row match_tuple_door_nested_struct_binds_nothing)
    my09  `let P { .. } = &p` — the let door refuses a reference rhs
    my10  fn-param struct pattern with `ref` binds nothing (row fnparam_struct_ref_mut_field_binds_byvalue)
    my14  `(mut a, b)` under `&` — already refused by the landed `mut` third

## PREDICTED COST

`pass` 0, `stdlib` four layers, fail-text 0 differing, runtime only-base 0 / only-armed 0 /
CHANGED 0 — the four corpus sites (10 stdlib lines, `lifetime_match_ref_option`,
`ref_struct_enum_payload`, `pat_4`, `match_struct_move_field_drop`,
`match-ref-binding-mut`) are repaired FIRST. A non-zero names a site the repair missed.

## ⚠ WHAT THIS PREDICTION GOT WRONG, RECORDED RATHER THAN REWRITTEN

Two of the nine predicted refusals did not happen — `my01` (variant payload then struct
shorthand) and `my02` (the tuple-struct call spelling `TS(ref a, b)`). Both are RULE 2: the
doors are in SERIES and the default binding mode never shifts at either, so there is no
shifted mode for a modifier to violate. Controls measured on the same binary: the LANDED
`mut` third misses both doors identically. `my02`'s root became a new tier-1 queue row
(`tuplestruct_door_default_ref_mode_not_carried`, a DOUBLE FREE).

And the enumeration missed a door: the nested `@` binding, `Some(ref w @ 1..=9)`. It is not
in the table above because the table was built from `build_pattern_impl`'s dispatch and this
arm lives in `build_pattern_variant_data`'s payload loop, where `binding_is_ref` is pushed
FALSE for `ref n @ sub` — the keyword is discarded, so no downstream site can see it. Found
by a hand program written after the first build, closed in the same round, pinned with its
own pair. `# TOTAL` 67 -> 66 -> 67; ten fixtures, five pairs.
