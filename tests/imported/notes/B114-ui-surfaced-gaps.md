# B114 — surfaced gaps (rustc ui run-pass import)

Upstream: rust-lang/rust@4b0c9d76ae7d387229caea55cfa73c280b08b8a7

Batch theme: leverage the now-default-on implicit prelude. All 25 imported
tests carry ZERO `use` lines — Option/Some/None/Result/Ok/Err/Vec/PartialEq/
Clone/FnMut/FnOnce all resolved implicitly via `logos.std.prelude`. No
prelude-resolution surprises: every prelude item resolved cleanly without a
`use`, exactly matching the Rust originals. (Notable: closures with `FnMut`/
`FnOnce` bounds, generic-struct + generic-method, `for x in &arr/&Vec/Vec`,
struct functional-update, range/or patterns — all worked prelude-only.)

## Skipped tests + precise gap

> **UPDATE 2026-05-21**: the first two entries below (closure-reform,
> match-vec-rvalue) are **FIXED and RE-IMPORTED** (batch RB114 — see
> RUSTC-PROVENANCE.md and the RESOLVED section at the bottom). Kept here for
> the gap-history record.

- **compiler-bug (genuine)** — generic `where F: FnOnce(T)->U` (or `FnMut`)
  called with a *closure literal* returns garbage. `tests/ui/functions-closures/closure-reform.rs`.
  Minimal repro: `fn call_it<F>(f: F)->i64 where F: FnOnce(i64)->i64 { f(10) }`
  then `call_it(|s| 100 + s)` — compiles clean, runs, but the returned value is
  wrong (program exits non-zero). NON-capturing closure literal reproduces it
  too (so it is not a capture bug). The same bound called with a *bare fn item*
  works (cf. fn-item-type-cast.logos, closure-mut-argument-6153.logos which pass).
  Symptom: wrong runtime value, no diagnostic. **Flagged for compiler attention.**

- **compiler-bug (genuine, SIGSEGV)** — matching an owned `Vec` value to a
  binding pattern then using it crashes. `tests/ui/binding/match-vec-rvalue.rs`.
  Repro: build a `Vec<i64>` via vec_new+push, then `match v { x => { x.length(); x.get(0); } }`
  → SIGSEGV (exit 139) at runtime. Likely a move/drop interaction on the
  match-bound Vec. **Flagged for compiler attention.**

- **unsupported-syntax** — reference-typed argument in the parenthesized Fn-trait
  sugar (`FnMut(&T) -> ()`, `FnOnce(T, T) -> bool` with a type-param arg) fails to
  parse (`syntax error near 'fn'` at the line after the where-clause / `unknown
  type 'T'`). Concrete non-ref arg + explicit return type works (`FnMut(i64)->i32`).
  Skipped: `tests/ui/closures/old-closure-iter-1.rs`,
  `tests/ui/functions-closures/capture-clauses-unboxed-closures.rs`,
  `tests/ui/binding/expr-match-generic-unique1.rs`.

- **unsupported-syntax** — empty-paren Fn sugar `FnMut()` / `FnOnce()` (no args)
  fails to parse. Skipped: `tests/ui/functions-closures/bare-fn-implements-fn-mut.rs`
  (the `call_f<F: FnMut()>` line). Workaround for the `closure-reform` thunk used
  `FnOnce() -> i64` — but that test was skipped for the FnOnce-closure bug above.

- **unsupported-syntax** — nullary closure mutable-capture: `|| { hit = true; }`
  capturing an outer `mut` local emits `mlir_gen: assign to undefined 'hit'`.
  (Surfaced while adapting closure-reform; closure-reform skipped for the worse
  FnOnce-closure-literal bug above.)

- **unsupported-feature** — refutable inner pattern inside a struct-shape variant
  pattern: `Meal::ForHere { o: Order::Hamburger }` / `T3::C { f0: T2 { x: T1::A {..} }, .. }`
  gives `refutable inner pattern not yet supported in struct-shape variant patterns
  (use bind + body match)`. Worked around in match-nested-enum-box-3121.logos via
  bind-then-inner-match. Fully skipped: `tests/ui/structs-enums/record-pat.rs`.

- **unsupported-feature** — refutable field sub-pattern in a struct pattern
  (literal in field: `Foo { f: 0 }`): `struct pattern: refutable field sub-pattern
  not yet supported`. Skipped: `tests/ui/binding/match-struct-0.rs`.

- **unsupported-syntax** — `ref _y @ Pat` ref-binding with `@` fails to parse
  (`syntax error near '_y'`). Skipped: `tests/ui/binding/match-with-at-binding-8391.rs`.

- **unsupported-syntax** — destructuring-let of a tuple of references
  (`let (&x, &y) = (&3, &c);`): `let <pattern> = expr; currently supports struct
  patterns only`. Skipped: `tests/ui/binding/borrowed-ptr-pattern-infallible.rs`.

- **unsupported-syntax** — bare-rest tuple pattern `(..)` and 1-tuple `(z,)`
  fail to parse (`syntax error near '..'` / near ','). Skipped:
  `tests/ui/binding/pat-tuple-2.rs`, `tests/ui/match/issue-72680.rs` `h` case
  (the `g` case is already imported as match/issue-72680-g.logos).

- **unsupported-syntax** — empty struct-variant `enum Foo { A {} }` (empty braces)
  fails to parse. Worked around in issue-38002.logos by using a unit variant `A`.

- ~~**divergence** — typed reference-to-array `&[T; N]` does not unify with `&[T]`~~
  **RECLASSIFIED 2026-05-21 → §B5 catch-up + part CAUGHT UP.** The `&[T;N]`→`&[T]`
  *coercion* now WORKS (verified: `&named_arr`, `&[lit,…]`, and inside tuple/struct
  fields — `(i64, &[i64;2])` unifies with `(i64, &[i64])`). What actually blocks
  `match-tuple-slice` is **dynamic-slice PATTERNS** (`[a, b]` / `[a, ..]` over a
  `&[T]` scrutinee) — a codegen gap with 3 distinct symptoms (match-expr →
  `arith.cmpi`-on-ptr; match-stmt → stray `arith.constant`; `&[_,_]` ref-pattern →
  sema "reference pattern requires reference scrutinee"). See docs/DIVERGENCES.md §B5
  for the full repro + fix-location. **UPDATE 2026-05-22: dynamic-slice patterns
  IMPLEMENTED** (top-level `[a,b]`/`[h,..]` + nested-in-tuple length discrimination)
  — `match-tuple-slice` is now imported + passing (tests/imported/pass/match/). Only
  named slice bindings nested inside a tuple pattern (`(2, [a,b])`) remain (clean
  error, see §B5).
- **FIXED 2026-05-21 (47413e65)** — array pattern `[x, y]` in match-AS-EXPRESSION
  was a silent miscompile (bindings read garbage). Now correct for fixed-size arrays.

- **divergence (literal)** — negative integer literal at suffix-edge `-128i8` is
  parsed as `neg(128i8)` and `128i8` is out-of-range for i8. Worked around in
  small-enum-range-edge.logos via `0i64 - 128i64` comparison.

- **divergence (no method)** — `wrapping_add` is an inherent method on i32 but not
  on u8/i8 (`method call: receiver is not a struct (got u8)`). Dropped from
  small-enum-range-edge.logos; the discriminant-edge regression point preserved.

- **str-literal patterns / byte-string patterns** — not attempted (out of prelude
  theme); `tests/ui/match/match-tuple-slice.rs` str arm and
  `tests/ui/match/issue-46920-byte-array-patterns.rs`, `tests/ui/binding/match-byte-array-patterns.rs`
  left for a future string-focused batch.

## RESOLVED (2026-05-21, follow-up compiler fixes)
- **FIXED** closure-literal through generic `where F: FnOnce/FnMut(T)->U` returns
  garbage → sema now hints un-annotated closure-arg types from the Fn-family
  bound (commit 6c28d185). closure-reform now portable.
- **FIXED** match an owned Vec to a binding pattern → SIGSEGV (double-free):
  mlir-gen whole-value struct binding aliased a pointer-as-struct + sema didn't
  mark the by-value-match scrutinee moved (commit 181aba3f). match-vec-rvalue
  now portable.
- **FIXED** `where F: FnOnce(T, T) -> bool` (Fn-sugar bound with type-param args)
  → "unknown type 'T'": where-clause bounds now resolve with sibling type-params
  in scope (commit da3d71f5). Unblocks the ref-typed/type-param Fn-sugar bounds
  in where clauses (old-closure-iter-1, capture-clauses, expr-match-generic-unique1
  — re-check on re-import).
