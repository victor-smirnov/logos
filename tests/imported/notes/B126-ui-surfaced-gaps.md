# B126 — UI-surfaced gaps

Batch B126 imported 26 run-pass tests distilled from `tests/ui/issues/`
(scattered regression tests, pinned rustc
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`). All 26 imported files
compile + link + exit 0. The gaps below were surfaced while probing distinct
features; the affected facets were trimmed from the imported tests (each test
keeps only the working subset), or the whole candidate was dropped, and each
gap is recorded here for the grind.

All five are §B catch-up TODOs (must converge to Rust). No new §A blessed
divergences.

---

## G126-1 — `Some(B { a: A { v: x } })` spurious "use of moved variable"

**Symptom.** Moving a generic-typed value into a struct literal **that is
itself the directly-inlined payload argument of an enum-variant constructor**
trips a false-positive move error in the borrow checker.

```logos
fn foo<T>(x: T) -> Option<B<T>> {
    return Some(B { a: A { v: x } });   // error: use of moved variable 'x'
}
```

Every let-pinned step works:
```logos
let inner = A { v: x };
let outer = B { a: inner };
return Some(outer);                     // OK
```
`Some(A { v: x })` (single-level struct under the variant) also works; the bug
needs the **doubly-nested struct literal** under the variant ctor.

**Feature / §.** §B — move analysis of nested rvalue struct literals under an
enum-variant constructor argument double-counts the inner move.

**Where it bit.** `issue-28550-nested-generic-ctor-is` (uses the let-pinned
form; original inline form noted in the test).

---

## G126-2 — nested fieldless-variant pattern in `if let`

**Symptom.** A nested variant pattern (`Mode::Bar(BarMode::Check)`) works in a
`match` arm (synthesized inner-variant guard) but is rejected in an `if let`:

```logos
if let Mode::Bar(BarMode::Check) = *mode { ... }
// error: nested patterns inside enum-variant payloads are not yet supported;
//        bind to a name and match in the body
```

The same pattern in a `match` (with a catch-all) compiles + runs.

**Feature / §.** §B — extend the `match`-arm nested-fieldless-variant lowering
(synthesized payload guard) to the `if let` desugaring. Sibling of the
already-tracked "payload-binding nested variant" item.

**Where it bit.** `issue-26468-enum-discr-nested-iflet-is` (rewritten to
`match`; explicit-hex-discriminant + outer payload-carrying enum still tested).

---

## G126-3 — fixed-size array passed by value into a closure/fn-bound param crashes mlir-gen

**Symptom.** A closure/fn whose parameter is a fixed-size array `[T; N]`
by value emits invalid IR:

```logos
fn bar<F: Fn([i64; 1]) -> i64>(f: F) -> i64 { return f([2i64]); }
// mlir_gen: 'llvm.getelementptr' op operand #0 must be LLVM pointer type ...
//           but got '!llvm.array<1 x i64>'
```

The array argument is GEP'd as if it were a pointer. Array fields in structs
work (B104 fix); the by-value array **parameter** path is the gap.

**Feature / §.** §B — codegen for a fixed-size-array value as a fn/closure
parameter (spill-to-alloca before the indexing GEP, mirroring the struct-field
array fix).

**Where it bit.** `issue-28181` (dropped from the batch).

---

## G126-4 — `if/else`-as-value with a postfix index crashes mlir-gen

**Symptom.** Using an `if/else` block as a value and immediately indexing the
result crashes MLIR generation:

```logos
let x: i64 = if true { [1i64, 2i64, 3i64] } else { [2i64, 3i64, 4i64] }[0i64];
// logosc: MLIR generation failed
```

The companion postfix-CALL form works:
```logos
let x: i64 = if true { i1 as Fp } else { i2 as Fp }();   // OK
```

**Feature / §.** §B — mlir-gen for a postfix index applied to an
if-expression-as-value (the array temporary produced by the if-arms is not
materialized to an addressable place before the index GEP). Related to the
B104 array-place work and the G124-2/G124-3 if/block-as-value items.

**Where it bit.** `issue-29071-if-value-as-callee-is` (keeps only the
postfix-call form; the index form noted in the test).

---

## G126-5 — top-level `type` alias named `F` (or any 1-letter generic-param name) breaks the stdlib

**Symptom.** Declaring a top-level type alias whose name collides with a
generic **type-parameter** name used inside the stdlib (e.g. `F` in
`MapIter<I, T, R, F>`) makes the entire `logos.lang.iter` module fail to
type-check (cascade of "struct 'MapIter': not generic — cannot accept 4 type
arg(s)", "unknown field 'inner'/'f'", etc.):

```logos
type F = fn() -> i64;   // poisons every stdlib generic that uses param `F`
```

Renaming the alias (`type Fp = ...`) fixes it entirely. So a user-declared
top-level type-alias name is being resolved into stdlib generic-parameter
scope rather than staying in the user package.

**Feature / §.** §B — type-alias name resolution must be package-scoped and
must NOT shadow a generic type-parameter binding inside an unrelated (stdlib)
declaration. A name-resolution / scoping bug, not specific to `iter`.

**Where it bit.** `issue-29071-if-value-as-callee-is` (alias renamed to `Fp`).

---

## Re-confirmed known-open (NOT re-reported)

- **tuple-struct constructor as a first-class fn value** (`let f = A; f(true)`
  where `struct A(bool)`) → `undefined variable 'A'`. The ctor name is not a
  value binding. `issue-5315` dropped.
- **string-literal `match` patterns** (`match s { "foo" => ..., _ => ... }`)
  → parser `syntax error near '{'`. `issue-3574` / `issue-22008` dropped (str
  `==` is otherwise well-covered).
- **generic `T: Add` operator bound on primitives** — primitives don't `impl
  Add` (operator traits are user-impl-only in Logos; the method-call /
  primitive-`+` surface differs). `issue-22258` / `issue-21922` dropped.
- **digit-group underscores inside `'\u{...}'`** (`'\u{10__FFFF}'`) → parser
  error. `issue-43692` uses the plain-escape form.
