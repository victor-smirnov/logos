# 2026-09-14j-fnptrbinder — TARGET ROWS, written by name before the compiler was touched

Base: HEAD ff347770b, build_hash c52dcb19dff987ff 43 (read). Queue gate rc 0 (167 rows). bc_admits 64.

## Target rows (bc_admits.ledger)
  issue-54124           nllmoves.NEW-L1   `let f: fn(&i64) -> i64 = |x: &'a i64| ...`      ADMITTED on base
  issue-101280          lifereg.NEW-N4    `fn f<'r>(g: fn(&'r i64, &i64)) -> for<'r> fn(&'r i64, &'r i64) { g }`  ADMITTED on base

## The property (one fact, two doors)
A fn-POINTER type's own regions — every elided `&` in its parameters and every name in its `for<..>`
binder — are LATE-BOUND binders of that type. On the SUPER side of `sub <: sup` they are rigid
placeholders; on the SUB side they are existential. The code carries neither:
  door E  an elided region inside a fn-pointer type is `""`, and `lt_eq` / `outlives` read `""` as a
          WILDCARD, so `fn(&'r i64) <: fn(&i64)` is decided at subtype()'s first line
          (types_equal_with_lifetimes) and the FnPtr contra arm is never asked.   issue-54124
  door H  resolve_type(FN_PTR_TYPE) never reads HRTB_BINDERS (docs/spec/types.md type.fn-ptr.hrtb:
          "parsed and captured ... not yet semantically enforced"), so `for<'r>` is the plain string 'r
          and meets the enclosing fn's own 'r BY NAME (rule 12).                  issue-101280
The arm that decides it EXISTS: subtype()'s FnPtr/FnItem/Closure case (contra params, co ret) and
check_variance's strict let/return sites — measured refusing `fn(&'a i64) <- |x: &'static i64|` (F05)
and `for<'x> fn(&'x) -> fn(&'r)` (F10) on base.

## Neighbour found before any build (hand control, base)
  F09  `fn f<'r>(g: for<'x> fn(&'x i64) -> i64) -> fn(&'r i64) -> i64 { return g; }` — LEGAL Rust
       (by reading, no rustc binary), REFUSED on base "return type mismatch: variance mismatch".
       Same fact in the other direction: a SUB-side binder read as a rigid name. Door H + E's
       sub-side half should admit it.

## Why this block over the others
  * Both roots have NO site-bearing record naming their program (derived by set, see the round
    record); issue-54124 was closed ONCE by `letnamed` (2026-09-12q) "right verdict for a reason the
    arm does not state" — the EMPTY side was the fn pointer's higher-ranked parameter.
  * Not any excluded plane: not Self/impl-header, not 'static demand, not declaration arrival,
    not cross-kind ptr coercion / Vec store, not the meet token, not pointer comparison / D2.
  * Declined alternatives, measured today on base:
      bck.NEW-L buffer-reuse — the E0597 arm exists (block form L02 refuses, loop-assign L03 refuses,
        loop-push L07 and the row admitted): an arm reached through a missing fact too, but the loop
        plane carries three open over-refusal queue rows and the push plane two; one row. Next round.
      lifereg.NEW-E0226 — no arm (spec: "recorded but not yet enforced"); a new check, not a carrier.
      nllmoves.NEW-4 var-appears-twice — `_` offered two regions under invariance: the meet plane.
      lifereg.NEW-N2 — the port moved upstream's where-clause projection into an ARGUMENT, where
        implied bounds may make it legal; legality unreadable with confidence.
      nllmoves.NEW-N1 impl-trait-captures — legal under Rust 2024 capture rules (13c report).
