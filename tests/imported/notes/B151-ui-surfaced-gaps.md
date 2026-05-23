# B151 — rustc UI run-pass import: surfaced gaps

Batch B151 imported 20 run-pass tests from `tests/ui/` (areas: binop, enum,
functions-closures, generics, pattern, self, structs-enums, traits, typeck,
where-clauses). Workflow: faithful ports, `pub fn main()` → `fn main() -> i32 {
…; return 0; }`, isize/usize → i64/u64, literals suffixed, `assert_eq!`/`assert!`
via `use logos.std.fmt;`, Box/dyn/PhantomData/derives/println! dropped where
incidental.

All 20 kept tests verified rc=0 against the as-is `logosc` binary (no compiler
changes). Tests that hit a gap were either re-shaped to preserve the essence
(noted inline + here) or DROPPED.

---

## ⚠️ HIGHEST PRIORITY — SILENT MISCOMPILE

### G151-1 — `ref`-binding a STRUCT enum payload reads garbage  ⚠️ SILENT MISCOMPILE
Binding a struct-typed enum payload field with `ref` in a match arm, then
reading a field off the ref binding, returns garbage (no error, no crash — wrong
value). By-value binding of the same payload is correct. Scalar payloads with
`ref` are also correct.

Minimal repro (returns nonzero garbage; should exit 0):
```
struct LM { size: i64 }
enum HM { Inner(LM, i64) }
fn len(m: &mut HM) -> i64 {
    match m { HM::Inner(ref l, _) => { return l.size; } }   // l.size garbage
}
fn main() -> i32 {
    let mut m = HM::Inner(LM { size: 0i64 }, 7i64);
    return len(&mut m) as i32;     // observed 156 / 76 / 228 across shapes
}
```
- by-value bind `HM::Inner(l, _) => l.size`  → CORRECT (exit 0). **Workaround.**
- scalar payload `HM::Inner(ref l, _) => *l` → CORRECT (exit 0).
- struct payload `ref l` then `l.field`     → **MISCOMPILE**.
- Reproduces with BOTH a non-generic enum and a generic enum, and with the
  receiver passed by-value, by `&mut`, or via `self: &mut Self`. The
  generic-enum-via-`self: &mut Self` shape additionally ABORTs at runtime (SIGABRT
  exit 134) instead of returning garbage — likely the same root manifesting after
  more downstream codegen.
- Tractability: moderate-to-deep (mlir-gen match-extract: `ref` of a struct
  payload appears to bind the wrong address / a copy at the wrong offset, so the
  subsequent field GEP reads garbage). §B catch-up.
- Surfaced by `tests/ui/self/explicit-self-generic.rs`; that test is KEPT, with
  the `ref l` rewritten to a by-value binding `l` (the upstream payload is Copy,
  so faithful).

---

## Parse / unsupported-syntax gaps (clean errors, not miscompiles)

### G151-2 — `..` rest pattern inside a tuple-struct / tuple-variant pattern
`A(..)` (tuple-struct) and `Variant(..)` (tuple enum variant) are parse errors
(`syntax error near '('`). Per-field wildcards (`A(_)`, `Variant(_, _)`) work.
- Repro: `match A(3i64) { A(..) => … }` → `syntax error near '('`.
- Workaround used in `pattern/struct-wildcard-pattern-14308` and
  `structs-enums/issue-1701`: rewrite `A(..)`/`Variant(..)` to explicit
  per-field `_` wildcards.
- Tractability: trivial-to-moderate (grammar: accept `..` as a rest element in
  the tuple-pattern element list, already accepted in slice/struct patterns).
  §B catch-up.

### G151-3 — closure→fn-pointer coercion only at a let/local binding, not at a return position
`fn f() -> fn() -> u32 { return || 42u32; }` errors `return type mismatch —
expected fn() -> u32, got || -> u32`. The identical coercion bound to a typed
local (`let bar: fn() -> u32 = || 42u32;`) works.
- Workaround in `functions-closures/closure-to-fn-coercion`: bind to a local.
- Tractability: moderate (sema return-coercion path should apply the same
  closure→fn-ptr coercion the let-annotation path applies). §B catch-up.

---

## Pre-existing / known gaps re-confirmed (not re-reported at length)

- Refutable struct-FIELD sub-pattern: `T2 { x: T1::A(mv), .. }` — binding `mv`
  fails (`undefined variable`). Already catalogued (B107). DROPPED `record-pat`.
- Compound assignment with a `&T` RHS: `x += &2i8` errors `expected i8, got
  &i8` (no auto-deref of the RHS reference in compound-assign). DROPPED
  `compound-assign-by-ref`. (Plain `x += 2i8` works; only the by-ref RHS form
  fails.) §B catch-up — moderate.
- Nested/loop-driven FnMut closures capturing a mut local: runtime SIGABRT
  (134). Already catalogued (B107). DROPPED `old-closure-iter-2`.
- 1-tuple `let (y,) = x;` destructure: `'let <pattern> = expr;' supports struct
  patterns only`. Already catalogued (B106). DROPPED `one-tuple` (the
  `match ('c',) { (x,) => … }` half works).
- `.iter()` on a `&[T]` slice: `slice has no method 'iter'` — slice iteration
  goes through `unsafe { iter_over_slice(&s) }`, not `.iter()`. DROPPED
  `iter-cloned-type-inference` (the `.iter().cloned().sum()` chain is its point).

## Divergences re-confirmed (BLESSED — not catch-up)

- Operator-overload traits are single-type: `impl Mul<RHS> for T` with a
  distinct `Output` associated type is unsupported — `logos.lang.ops` `Mul` is
  `fn mul(self: Self, rhs: Self) -> Self` (Self = RHS = Output, documented "for
  now" in ops.logos). Same-type user `impl Mul for V` DOES dispatch through `*`.
  DROPPED `binops-issue-22743`. (If RHS/Output type params are wanted later this
  becomes a §B item; presently the single-type model is the deliberate design.)
- `if (return) { … }`: an `if` condition of type `!` (never) is rejected
  (`if condition must be bool, got !`). Niche; not imported.

---

## Imported tests (20)

| Test | rustc source | Feature exercised |
|------|--------------|-------------------|
| binop/binops-bool-b151 | binop/binops.rs | bool relational + bitwise (`& \| ^`) operators; field-mutation changing a comparison |
| enum/recursive-nested-enum-42747-b151 | enum/issue-42747.rs | deeply-nested recursive enum types (each carrying the previous), outermost construct + match |
| functions-closures/capture-clauses-boxed-closures-b151 | functions-closures/capture-clauses-boxed-closures.rs | `FnMut(&T)` closure mutating a captured accumulator, driven over `&[T]` |
| functions-closures/closure-inference2-b151 | functions-closures/closure-inference2.rs | identity closure inside a block expr, called twice |
| functions-closures/closure-to-fn-coercion-b151 | functions-closures/closure-to-fn-coercion.rs | non-capturing closure → `fn(u8)->u8` / `fn()->u32` pointer at a local |
| functions-closures/fn-coerce-field-b151 | functions-closures/fn-coerce-field.rs | `FnOnce`-bounded generic-struct field holding a fn item coerced to `fn()` |
| functions-closures/fn-item-type-coerce-b151 | functions-closures/fn-item-type-coerce.rs | fn item → fn-ptr type alias; two fn items unified at `eq::<IntMap>` |
| generics/type-params-in-for-each-b151 | generics/type-params-in-for-each.rs | method-generic higher-order fn `F: FnMut(i64)` whose closure body declares `Vec<T>` |
| pattern/struct-wildcard-pattern-14308-b151 | pattern/struct-wildcard-pattern-14308.rs | tuple-struct wildcard `A(_)` + literal-field fall-through |
| pattern/unit-pattern-in-fn-arg-7519-b151 | pattern/unit-pattern-matching-in-function-argument-7519.rs | unit-typed fn argument (rustc #7519 regression) |
| self/explicit-self-generic-b151 | self/explicit-self-generic.rs | 2-param generic enum `&mut self` method matching `self`, struct-payload field read |
| structs-enums/empty-struct-braces-b151 | structs-enums/empty-struct-braces.rs | empty-brace struct `S {}` + brace-style empty enum variant, `{}` / `{ .. }` patterns |
| structs-enums/issue-1701-b151 | structs-enums/issue-1701.rs | mixed enum (1-payload / 2-payload / unit variants) dispatch by variant |
| traits/alignment-gep-tup-like-1-b151 | traits/alignment-gep-tup-like-1.rs | generic struct method returning a tuple `(A, u16)` mixing generic + u16 fields |
| traits/anon-static-method-b151 | traits/anon-static-method.rs | inherent static method `Foo::new()` |
| traits/default-method-bound-b151 | traits/default-method/bound.rs | trait default method reached via empty `impl A for i64 {}` + `T: A` bound |
| traits/generic-trait-bound-b151 | traits/bound/generic_trait.rs | trait `ConnFactory<C: Conn>` with a trait-bounded type param; `create()` result dispatch |
| traits/impl-implicit-trait-b151 | traits/impl-implicit-trait.rs | inherent `impl` on a generic enum + a non-generic enum |
| typeck/unify-return-ty-b151 | typeck/unify-return-ty.rs | generic `null::<T>() -> *const T` instantiated at a concrete type |
| where-clauses/where-clauses-method-b151 | where-clauses/where-clauses-method.rs | generic-struct method with a method-level `where T: Eq`, `==` on the field |
