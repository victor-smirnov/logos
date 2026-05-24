# B166 — Adversarial Depth-Probe Gap Census

Provenance: rust-lang/rust@4b0c9d76ae7d387229caea55cfa73c280b08b8a7 (2026-05-24).
Compiler used as-is: `build/bin/logosc`. Repros live in `b166-repros/<slug>.logos`.

This batch deliberately probed fragile feature *intersections* rather than maximizing
green count. 13 distinct gaps surfaced (8 NEW, 5 KNOWN-confirmed). Two are silent
miscompiles (the most valuable), two are runtime SIGSEGVs, the rest are parse/sema
rejections or MLIR-gen failures.

Legend: **MISCOMPILE** = links + wrong result; **CRASH** = SIGSEGV/MLIR-gen fail on
legitimate code; **REJECT** = parse/sema rejection of code that should work per Rust.

---

## NEW gaps

### N1 — `arr[i].field = v` (index-then-field assignment LHS) — REJECT  [repro g3]
`a[i].field = v` is rejected: *"assignment target too deeply nested to assign in place
yet; bind an intermediate"*. This is the **single most common** missing place-write shape.
Place-write matrix established:
- `a[i][j] = v`  ✓ (nested index)
- `s.f[i] = v`   ✓ (field-then-index)
- `a[i].field = v`  ✗ **REJECTED** (index-then-field)  ← gap
- `rr.cells[i].v = v` (ref→field→index→field) ✗ REJECTED at any depth
- workaround `let r = &mut a[i]; r.field = v` ✓ at single level only.
Feature intersection: struct-array element as an assignable place. The G163-2 place-write
generalization covered index-then-index and field-then-index but not index-then-field.

### N2 — method call on a struct-array element receiver — MISCOMPILE  [repro g2]
`r.cells[1i64].get()` where `cells: [Cell;3]` is a struct field: the *field read*
`r.cells[1].v` is correct (=20), but calling an inherent `&self` method on the same place
passes a **wrong `self` pointer** → returns garbage (observed 176 / 232 on repeated builds).
Even the explicit-ref form `let c: &Cell = &r.cells[1]; c.get()` is wrong (=88).
A plain *local* array `cells[i].get()` (not a struct field) works fine — so the bug is the
address-of a *struct-field-array* element in the method-receiver / take-ref path. NEW, silent.

### N3 — block-expression inner shadow clobbers the outer slot — MISCOMPILE  [repro g7]
```
let x = 5;
let y = { let x = 100; x + 1 };   // y == 101 (correct) but x is now 100, not 5
```
The inner `let x` inside a **value-producing block expression** writes the OUTER `x`'s
storage slot instead of a fresh one. `x` ends up holding the inner value (=100 / =10 when
`x*2`). Requires ALL of: (1) block-as-expression, (2) same-name shadow, (3) — independent
of whether the initializer reads the outer. A *statement* block `{ let x = 99; }` and a
*flat* shadow `let x = x*2;` both work. NEW, silent — high value (corrupts surrounding scope).

### N4 — `[&dyn Trait; N]` arrays — CRASH + REJECT  [repros g6, g6b]
- Homogeneous `let arr: [&dyn Sh; 2] = [&a, &b]` (both `&Sq`) **compiles but SIGSEGVs**:
  the array stores thin `&Sq` pointers while `arr[i].area()` dispatches via fat-pointer
  vtable → garbage vtable. (g6, exit 139)
- Heterogeneous `[&Sq, &Ci]` is **rejected** at the array literal: *"element 1 has type
  &Ci, expected &Sq"* — no unsize coercion `&Concrete → &dyn` is applied per-element. (g6b)
A single `&dyn Sh` coercion in a fn arg works fine; the gap is specifically array-literal
elements. Feature intersection: trait objects × array literals/unsize coercion.

### N5 — `?` operator inside a closure — CRASH (MLIR-gen)  [repro g5]
`let f = |n| -> Result<..> { let v = check(n)?; ... }` fails MLIR generation:
`'func.return' op expects parent op 'func.func'`. The `?`-desugar's early-return targets
the enclosing `func.func` rather than the closure's own region. Explicit `return` inside a
closure works; only the `?`-desugared return is mis-targeted. NEW. Intersection: `?` × closures.

### N6 — `impl Trait` sugar `impl Fn(A)->R` in param/return position — REJECT (parse)  [repro g4]
`fn apply(f: impl Fn(i64)->i64, ...)` and `fn adder(...) -> impl Fn(i64)->i64` both fail to
parse (*syntax error*, reported at the following item). Note:
- `impl T` (a plain named trait) in return position works.
- `<F: Fn(i64)->i64>` as a *generic bound* works.
So the gap is the **`Fn(...)->...` parenthesized-trait sugar specifically in `impl Trait`
position** (param and return). NEW. (Param-position `impl Trait` generally also gives a
deliberate "use explicit generic" diagnostic — see K-misc below — but the *Fn-sugar* form
fails earlier in the parser.)

### N7 — calling a fn-pointer from a field/index expression — REJECT  [repros g8, g8b]
- `h.f(21)` where `f: fn(i64)->i64` is a struct field → *"'Handler' has no method 'f'"*
  (treated as a method call). `(h.f)(21)` → parse error. Workaround: bind to a local first
  (`let g = h.f; g(21)` ✓ — banked as a green test).
- `ops[i](x)` (call an indexed array element) parses as a statement/return head but is a
  **syntax error as a binary operand** (`if ops[0](5) != 6 {…}` → "syntax error near ']'").
NEW. Intersection: callable values obtained from non-identifier primary expressions.

### N8 — generic-enum payload type-param inference through a generic-struct wrapper — REJECT  [repro g12]
`enum Holder<T> { Full(Pair<T>) }`; `Holder::Full(Pair{a:3i64,b:4i64})` → *"expected
Pair<T>, got Pair<i64>"*. `T` is not unified through the `Pair<T>` payload wrapper.
Workarounds (both banked green): plain `Holder<T>{Full(T)}` infers fine, and the turbofish
`Holder::<i64>::Full(pr)` works. NEW. Intersection: nested generics × enum-variant inference.

---

## KNOWN gaps — confirmed with tight repros this batch

### K1 — generic associated-const projection `T::CONST` (= B121 / G163-3)  [repro g9]
`fn sides<T: Shape>() -> i64 { return T::SIDES; }` → *"unknown enum 'T'"*. Matches the
OPEN B121 note exactly. Workaround: concrete path or a `&self` method.

### K2 — two impls of `Trait<T>` for one type at distinct `T` (= G156-1 / G157-2)  [repro g10]
`impl Add<i64> for Meters` + `impl Add<Meters> for Meters` → *"duplicate associated type
'Output'"* (mangling keys on trait NAME only). Matches the OPEN G156-1 note.

### K3 — struct-array-element field READ, 2-level — SIGSEGV (= B121 note (b))  [repro g1]
`g.rows[i].cells[j].v` (struct-field array, two index levels) → runtime SIGSEGV (exit 139).
Matches the OPEN B121 follow-up "struct-ARRAY-element-field `g.rows[i].cells[j]` crashes on
READ". Note the *single*-level field-array read `r.cells[i].v` in a loop works (banked green
`struct/struct-array-field-read-loop-b166`); the crash needs the two-level index nest.

### K4 — nested patterns inside enum-variant payloads — REJECT  [repro g11]
`Option::Some(Option::Some(v))` → *"nested patterns inside enum-variant payloads are not yet
supported; bind to a name and match in the body"*. (Related to but distinct from the
G162-1 fix, which handled `Variant(n @ range)` but not a nested *enum* sub-pattern.)

### K5 — `impl Trait` at parameter position (general) — REJECT (deliberate diagnostic)
`fn show(x: impl Sh)` → clean *"'impl Trait' is not yet supported at parameter position; use
an explicit generic … or '&dyn Sh'"*. Documented restriction (distinct from N6's parse-level
Fn-sugar failure); listed for completeness.

### K-misc — `_` inference placeholder in turbofish — REJECT  [repro g13]
`apply::<i64, _>(...)` → *"unknown type '_'"*. Rust permits partial turbofish with `_`.
Minor; full turbofish (`::<i64, i64>`) or full inference both work. (Borderline NEW/known —
recorded here as a small surface gap.)

---

## Summary
- **NEW gaps: 8** (N1–N8) — incl. 2 silent miscompiles (N2, N3) and 2 SIGSEGV/CRASH (N4, N5).
- **KNOWN-confirmed: 5** (K1–K5; plus the minor `_`-turbofish K-misc).
- Adversarial intersections surfaced **far more** than the ~1.5/batch of B162–B165: the low
  recent rate was **sampling**, not code maturity. Broad single-feature ports are mature;
  the cracks are at feature *boundaries* — place-expressions × struct-array elements,
  trait-objects × arrays, `?`/shadowing × closures-and-block-exprs, and generic inference
  through nested type constructors.
