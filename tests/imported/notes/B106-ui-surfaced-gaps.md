# B106 — compiler gaps surfaced by the tests/ui run-pass import (2026-05-21)

Porting 49 `tests/ui` run-pass tests surfaced the following real Logos
compiler gaps. The tests that hit them were SKIPPED (per policy: don't
force/hack a broken port). Each is a candidate for a fundamental fix; when
fixed, the corresponding upstream test becomes a regression.

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

3. **Tuple or-pattern with cross-position rebinding** — `match t { (1,a,b) |
   (2,b,a) => … }` (same names bound at different tuple positions per
   alternative) → `func.return` / metacall MLIR failure. Blocks
   `match-pipe-binding`.

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
