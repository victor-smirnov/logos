# 2026-09-09f `ergorefland` — the counter-example set, MEASURED both ways

Base binary `6e10b5ffbceba8b4 43` (HEAD e8a4c58b1, probes present and unarmed).
Landed binary `d903fbec773d7d16 43`. Every program is multi-line and returns a distinct
exit code. Run with `hand/`'s own runner shape: compile, link against the layer archives,
RUN, read rc.

## ILLEGAL UNDER 2024 — base rc 0, landed REFUSED (9 of 11 predicted)

    my03  struct-pattern TUPLE INDEX      match &t { TS { 0: ref a, 1: b } }     0 -> refused
    my04  ARRAY element, SHARED `&`       match &arr { [ref p, q] }              0 -> refused
    my05  nested variant `ref mut`        match &mut o { Some(Inner::A(ref mut v)) } 0 -> refused
    my07  DOUBLE reference scrutinee      match &r { Some(ref v) }, r: &Option   0 -> refused
    my08  struct sub `ref mut`, `&mut`    match &mut p { P { x: ref mut v, .. } } 0 -> refused
    my12  ⚠ CLASS HOLE, `mut` at a LEAF   match &p { P { x: mut v, .. } }        0 -> refused
    my13  ⚠ CLASS HOLE, `mut` at an ARRAY match &arr { [mut a, b] }              0 -> refused
    my06  struct-pat inside a TUPLE       match &t { (P { ref x, y }, z) }   refused -> refused,
          NEW SENTENCE: it was refused as "undefined variable 'x'" (queue row
          match_tuple_door_nested_struct_binds_nothing) and is now refused as the modifier
          rule, which is the earlier check. The ROW STILL REPRODUCES on its own program.

## ⚠ TWO PREDICTED REFUSALS THAT DID NOT HAPPEN — AND THE MEASUREMENT THAT EXPLAINS THEM

    my01  match &o { Option::Some(P { ref x, y }) }   rc 0 -> rc 0   NOT refused
    my02  match &t { TS(ref a, b) }                   rc 0 -> rc 0   NOT refused

RULE 2, DOORS IN SERIES. The modifier rule asks "is a modifier written under a SHIFTED
default binding mode". At both of these doors the mode NEVER SHIFTS, so there is nothing to
violate, and the rule is not the missing piece — the mode carry is.

  * my01 is `pat.binding.default-mode-carried-into-subpatterns`' own recorded divergence: a
    nested STRUCT/TUPLE sub of a variant payload is synthesized as a by-value binding plus a
    body destructure. Queue row `variant_payload_nested_struct_sub_double_drops`.
    CONTROL: `match &o { Some(P { mut x, y }) }` (hand/d3_enum_struct_sub_mut.logos) also
    compiles — the LANDED `mut` third misses it identically, so this is not a `ref` gap.
  * my02 is a door that has no default-binding-mode implementation AT ALL, and it is a NEW
    tier-1 soundness row (below).

## LEGAL — base rc 0, landed rc 0, ELEVEN shapes

    my11 for-header `(ref a, b)` over a by-value array   my20 `&`-pattern reset + struct shorthand
    my21 `match *self` in a GENERIC impl                 my22 THREE synthesized levels, no modifier
    my23 array elems, no modifier                        my24 struct shorthand, no modifier
    my25 tuple-struct, no modifier                       my26 top-level `ref w @ Some(_)` over `&`
    my27 a match GUARD, no modifier                      my28 `&mut`-pattern reset + `ref mut`
    my29 BY-VALUE scrutinee, `ref` at a leaf two levels down
    my30 `match *mp` through `&mut`, `ref mut` still writes through

## NOT MEMBERS — refused on the base already, unchanged (rule 14)

    my09  `let P { .. } = &p`  — "rhs must be a struct, got '&P'". The `let` door refuses a
          reference scrutinee outright, so no by-ref default mode exists there to violate.
    my10  fn-param struct pattern with `ref` — "undefined variable 'v'" (row
          fnparam_struct_ref_mut_field_binds_byvalue). Same reason.
    my14  `match &t { (mut a, b) }` — already refused by the landed `mut` third.

## THE NEW ROW — `tuplestruct_door_default_ref_mode_not_carried` (tier 1, `run 2`)

Found by my02, which is a MODIFIER program; the defect it exposed is a DOUBLE FREE with no
modifier in it.

  * hand/d2_ts_byref_type.logos — `match &t { TS(a, b) => { let x: i64 = a; } }` COMPILES.
    The binding is `i64` BY VALUE where every other container door binds `&i64`.
  * tests/soundness/open/tuplestruct_door_default_ref_mode_not_carried.logos — the same door
    with a move-only element runs `D::drop` TWICE (exit 2; Rust exits 1).
  * hand/tsdrop_structspelling.logos — `match &t { TS { 0: d, 1: k } }`, the STRUCT spelling
    of the SAME tuple struct, exits 1. hand/tsdrop_namedstruct.logos, a named-field struct,
    exits 1. TWO SPELLINGS OF ONE PATTERN DISAGREE.
  * hand/d1_ts_mut.logos — `match &t { TS(mut a, b) }` compiles, so the LANDED `mut` third
    misses this door too. INHERITED both ways: exit 2 on 6e10b5ffbceba8b4 and on
    d903fbec773d7d16.

## A FIFTH DOOR, FOUND AFTER THE FIRST BUILD BY WALKING THE DISPATCH

    my31  nested `@` binding    match &o { Option::Some(ref w @ 1i64..=9i64) }   0 -> refused

The payload loop's `@`-binding arm pushes `binding_is_ref = false` for `ref n @ sub`, so the
written `ref` is DISCARDED, not merely unchecked; `binding_is_mut` IS carried there, so the
`mut` spelling of the same shape was already refused. Control: `my26`, the TOP-LEVEL
`ref w @ Option::Some(_)` over `&o`, stays legal on both binaries — Rust 2024 leaves the
top-level mode `move`. Base binary 6e10b5ffbceba8b4: my31 compiles rc 0. Landed
4c4cc6a9cad1138c: refused. Every other verdict above is unchanged across that second build.
