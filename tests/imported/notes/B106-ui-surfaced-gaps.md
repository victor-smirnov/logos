# B106 — compiler gaps surfaced by the tests/ui run-pass import (2026-05-21)

Porting 49 `tests/ui` run-pass tests surfaced the following real Logos
compiler gaps. The tests that hit them were SKIPPED (per policy: don't
force/hack a broken port). Each is a candidate for a fundamental fix; when
fixed, the corresponding upstream test becomes a regression.

## Resolution status (2026-05-21 gap-closing pass)

- **#1 str relational compare — FIXED** (353385ec): `str_cmp` in
  logos.lang.str + lower_binop routes `<`/`<=`/`>`/`>=` on Slice<u8>.
- **leading `|` in match arm — FIXED**: grammar `pattern <- PIPE? pat_single
  …`. Regression: tests/logos/pass/match_leading_pipe.logos.
- **unit struct `struct S;` — DIVERGENCE, not a fix.** `struct Name;` is
  Logos's *explicit-instantiation* directive (`struct_inst`), not a
  zero-field struct. Logos's zero-field struct is `struct S {}` (verified
  works). Tests using `struct S;` should adapt to `{}` or skip.
- **#2 or-pattern WITH binding — FIXED (2026-05-21).** Implemented the
  recommended fundamental fix: sema fans out a binding/structural or-pattern
  arm into one arm per alternative (extended `or_needs_fanout` from
  variant-only to "any non-scalar-literal alt"; added the same fan-out to
  `lower_match_expr`, which previously had none). The match-expression
  codegen `extract_arm_payload` gained Tuple/At/RefBind/RefPat cases so the
  fanned-out alts' bindings are extracted. Pure scalar or-patterns (`1|2|3`)
  stay merged. Covers cross-position rebind (`(1,a,b)|(2,b,a)`) and
  enum-variant-or (`A(x)|B(x)`) in both statement and expression position.
  Regression: tests/logos/pass/match_or_pattern_binding.logos.
- **#3 (Some-guard-return), #4 (enum-in-tuple), open-`..` range pattern —
  DEFERRED** as a precise match-codegen baghunt (details below). Each is a
  multi-hour codegen/lowering excursion; deferred per the draw-the-boundary
  discipline rather than thrash the heavily-used match path unsupervised.
  Scoping notes added inline.

## Codegen / mlir-gen

1. **str relational compare emits a pointer `arith.cmpi`** — `s1 < s2` /
   `<=` / `>` on `&str` type-checks but mlir-gen lowers it to a pointer
   comparison (MLIR verify failure), instead of a lexicographic byte
   compare. `==`/`!=` work. Blocks: `estr-slice` relational arms,
   `match-str`, str ordering coretest pieces. Fix: route str `<`/`<=`/`>`
   through a `str_cmp`-style lexicographic compare in sema/mlir-gen.

2. **Custom-discriminant C-like enum matched inside a tuple aborts at
   runtime** — `enum E { A = 5, B = 9 }` plain `as i32` cast works, but
   `match (n, e) { (_, E::A) => … }` aborts. Plain (non-tuple) discriminant
   match is fine. Blocks: `mir_adt_construction`, `enum-clike` tuple cases.

## Pattern-match lowering

3. **Or-pattern with ANY variable binding — FIXED (2026-05-21, 609afbf5).**
   The recommended desugar landed: a binding/structural top-level PAT_OR arm
   is fanned out into N arms (one per alternative) at sema match-lowering
   (`lower_match` + `lower_match_expr`), and the match-expr codegen now
   extracts Tuple/At/RefBind/RefPat bindings. `(1,a)|(2,a)`, cross-rebind
   `(1,a,b)|(2,b,a)`, and enum-variant-or `A(x)|B(x)` all work in statement
   and expression position. Pure scalar or-patterns (`1|2|3`) stay merged.
   **Follow-up gap (NOT yet done):** NESTED or-patterns — an or-pattern inside
   a tuple element (`((true,y)|(y,true), z)`), inside a variant payload
   (`Err(x @ (6 | 8))`), or inside an `@`-binding (`z @ (0 | 4)`). The fan-out
   only triggers on a top-level PAT_OR arm; nested ones need recursive
   expansion (cartesian product of alternatives) or a sub-pattern OR-guard.
   Upstream `or-patterns/bindings-runpass-{1,2}.rs` exercise these and remain
   un-ported. Scope: another dedicated match-lowering pass.

4. **Multi-arm `Some(_) if guard` / `Some(_)` over `Option<i64>` that
   returns** — a 2+ arm match on `Option<i64>` where guarded and unguarded
   `Some` arms both `return` aborts at runtime. Blocks
   `match-value-binding-in-guard-3291`.

## Unsupported syntax (parser / sema — may be intentional divergences)

5. Assorted forms not accepted; each blocks a handful of ui tests:
   - unit struct `struct S;` (no fields, no braces)
   - 1-tuple literal/pattern `(y,)`
   - struct / array patterns in **fn parameter** position
   - leading `|` in a match arm (`| A => …`)
   - bare `(..)` tuple pattern and open-ended `0..` range pattern
   - by-value `self` methods in some shapes (interacts with Box absence)

These were all worked around by adapting the test (Logos-wins) where the
feature was incidental, or skipped where it was the test's whole point.
