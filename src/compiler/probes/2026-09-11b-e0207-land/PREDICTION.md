# 2026-09-11b — PREDICTION, WRITTEN BEFORE THE COMPILER WAS TOUCHED

## THE ARM
`lower_impl_block` (src/compiler/sema_decl.cpp), one predicate:

    an impl LIFETIME binder that is named by one of the impl's own
    ASSOCIATED-TYPE definitions and is mentioned NOWHERE in the impl HEADER
    (self type, trait arguments, where clause) is E0207-unconstrained.

"Mentioned in the header" is ONE walk over the impl node with the BINDER LIST
and the BODY excluded, at any depth — so `W<'a>`, `Tr2<W<'a>>` and `&'a mut S`
all constrain, and the door-2 carrier (a bare lifetime at trait-arg position,
discarded at `sema_decl.cpp` ~2971 by design) is carried by construction rather
than by a second special case.

`WHERE` is counted as CONSTRAINING. Rust's rule is narrower — an outlives
predicate does not constrain, only a projection predicate does — and I cannot
tell the two apart here with no rustc on the box. The conservative direction is
ADMIT, so this is a deliberate under-refusal, named, not a divergence claim.

## PREDICTED CLOSED SET — A NUMBER AND A LIST OF ROWS
THREE bc_admits rows, and nothing else in either ledger:

    missing-lifetime-in-assoc-type-1
    missing-lifetime-in-assoc-type-5
    missing-lifetime-in-assoc-type-6

## PREDICTED COST
0 on every column: `-L bc`, `fail_text_oracle.py`, `stdlib-cost.sh`,
`run_oracle.py`. The stdlib zero is STRUCTURAL — `grep -rnE "^ *impl<[^>]*'"
--include=*.logos stdlib` = 0 headers — and is claimed as such, not as evidence.

## HAND PROGRAMS — SHAPES THE PRICING PHASE DID NOT USE
MUST STILL COMPILE (all rc 0 on the base binary today, checked):
  n1   'a only at a TRAIT-ARG position          impl<'a> Tr3<'a> for S
  n2   MUT_REF self type                        impl<'a> Tr for &'a mut S
  n3   binder named by NO assoc type            impl<'a> Tr for &S { type Item = i64; }
  n4   assoc type names only 'static            impl<'a> Tr for &'a S
  n9   ALPHA-RENAMED binder ('zz)               rule 12
  n10  'a NESTED inside a trait argument        impl<'a> Tr2<W<'a>> for S
  n11  'a NESTED inside the self type's args    impl<'a> Tr for W<'a>
  n12  'a named by the assoc type NESTED        type Item = W<'a>
MUST BE REFUSED (all rc 0 — admitted — on the base binary today, checked):
  x2   named self type, target_resolved null    impl<'a> Tr for S
  x3   TWO binders, only the SECOND unconstrained
  x4   the mention in the assoc type is NESTED  type Item = W<'a>

## THE CLASS, AND THE HALF I EXPECT TO DECLINE
The class by property is "an impl generic parameter constrained by neither the
trait ref nor the self type nor a predicate". It has a TYPE-parameter half
(`impl<U> Tr for S { type Item = U; }`, measured rc 0 = admitted today, x5ty),
which Rust refuses ALWAYS rather than only when an assoc type names it. I
predict the type half is NOT landable in this round and will be declined with a
census number, not with a reading.

## ⚠ WHAT THIS FILE GOT WRONG, LEFT HERE RATHER THAN CORRECTED
The predicted closed set and the predicted cost were both right. The PREDICATE
description above is not: "ONE walk over the impl node with the BINDER LIST and
the BODY excluded, at any depth" is exactly the sentence I then implemented as a
skip mask carried INTO the recursion, which is not the same thing — `ITEMS` names
the body at the top and a generic's own argument list one level down. The first
armed build refused `impl<'a> Tr3<'a> for S`, `impl<'a> Tr2<W<'a>> for S` and
`impl<'a> Tr for W<'a>`, all three LEGAL, and only the counter-examples said so.
A prediction written in prose can be right about the answer and wrong about the
mechanism, and it is the mechanism a later round would reuse.
