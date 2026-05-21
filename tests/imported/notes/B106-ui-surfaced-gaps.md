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
- **#2 (or-pattern binding), #3 (Some-guard-return), #4 (enum-in-tuple),
  open-`..` range pattern — DEFERRED** as a precise match-codegen baghunt
  (details below). Each is a multi-hour codegen/lowering excursion; deferred
  per the draw-the-boundary discipline rather than thrash the heavily-used
  match path unsupervised. Scoping notes added inline.

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

3. **Or-pattern with ANY variable binding** (broader than first thought) —
   `match t { (1,a) | (2,a) => … }` already fails with `'func.return' op
   expects parent op 'func.func'` / MLIR lowering failure; cross-position
   rebind `(1,a,b)|(2,b,a)` is the same root. Value-only or-patterns
   (`1 | 2 =>`) work. The scalar-discriminant or-pattern codegen
   (mlir_gen_stmt.cpp ~2420) explicitly bails on non-scalar alts ("Callers
   must not pass PatOr with non-scalar alts"), and the binding-extraction
   takes only the first alt (~2297). **Recommended fundamental fix:** desugar
   a PAT_OR arm into N separate arms (one per alternative, body cloned) at
   sema match-lowering — handles scalar/structural/cross-rebind uniformly and
   lets the fragile scalar-or codegen be retired. Risk: touches the
   heavily-used match-lowering path → wants a dedicated, full-ctest pass.

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
