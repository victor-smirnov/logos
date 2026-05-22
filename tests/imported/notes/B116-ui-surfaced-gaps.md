# B116 — surfaced gaps (tests/ui/pattern run-pass import) + RB115 residual gaps

Upstream: rust-lang/rust@4b0c9d76ae7d387229caea55cfa73c280b08b8a7

Two batches:
- **RB115** — re-import of associated-types tests previously skipped/trimmed for
  the 4 just-fixed gaps (qualified path, ref-projection, projection-method,
  equality-bound). 4 re-imported passing; 3 still fail with a residual gap.
- **B116** — 27 fresh `tests/ui/pattern/` run-pass tests, maximizing distinct
  pattern-feature coverage.

Every gap below is a **§B catch-up TODO** (Rust-parity grammar / sema /
mlir-gen work). NONE is a §A blessed divergence — no `const fn`, macro, derive,
proc-macro, or async item is involved (where an upstream test used one, that
incidental piece was dropped and the pattern feature kept).

---

## RB115 — residual associated-type gaps (after the 4 fixes)

### RB115-G1 — qualified path `<T as Trait>::Assoc` in **impl-self-type** position does not parse
PRECISE: `impl NonZero for <i32 as Int>::T { ... }` →
`syntax error near 'impl'` (parser bails at the impl). The fix for gap 1
enabled the qualified path in **type-annotation / return** position, but NOT in
the `impl ... for <...>::T` target position.
- Works instead: qualified path in a let-type, a generic-fn return, a generic-fn
  param (all imported faithfully).
- Needed: accept a qualified projection path as an `impl` target self-type.
- Blocks: `associated-types-projection-from-known-type-in-impl`.
- §B catch-up.

### RB115-G2 — projection on a **concrete** self-type does not normalize
PRECISE: `let x: <i64 as Foo>::T = 22i64;` (with `impl Foo for i64 { type T = i64; }`)
parses now (gap 1) but does NOT normalize the projection to `i64`:
`let 'x': type mismatch — expected i64::T, got i64`, then
`operator '*': left must be numeric, got i64::T`. The sugar form `i64::T`
fails identically.
- Works instead: projection through a **type-parameter** (`T::Value`, `<T as Get>::Value`)
  normalizes fine — only the concrete-self-type projection is unnormalized.
- Needed: normalize `<ConcreteType as Trait>::Assoc` / `ConcreteType::Assoc`
  to the impl's bound concrete type in type-checking.
- Blocks: `associated-types-basic`.
- §B catch-up.

### RB115-G3 — method call on a value of projection type → mlir-gen crash
PRECISE: `fn foo<G: GetToI32>(g: G) -> i32 { let r = g.get(); return r.to_i32(); }`
where `get(&self) -> <Self as GetToI32>::R` and `type R: ToI32`.
Sema now ACCEPTS this (the `type R: ToI32` bound is carried — gap 3 closed at
the sema layer), but mlir-gen emits
`mlir_gen: unsupported receiver kind for struct/class access` and the binary
**SIGSEGVs** at runtime.
- Needed: mlir-gen support for a method receiver whose static type is an
  associated-type projection bound to a concrete type (lower it as the
  normalized concrete type).
- Blocks: `associated-types-bound` (the `.to_i32()`-on-projection test).
- §B catch-up. (Sema-level gap 3 is closed; this is the residual codegen half.)

### RB115 — re-imported PASSING (faithful)
- `associated-types-in-fn-at` — `&<T as Get>::Value` ref-return + qualified
  path in a generic free fn (gaps 1 + 2).
- `associated-types-in-inherent-method-at` — same in an inherent generic method.
- `associated-types-simple-ref-at` — faithful `&Self::Value` ref-return (B115
  had trimmed this to by-value; gap 2).
- `associated-types-binding-in-where-clause-at` — equality bound `Foo<A = u64>`
  / `Foo<A = Bar>` normalizes the projection `T::A` (gap 4) + `<Self as Foo>::A`
  qualified return.

---

## B116 — pattern gaps (forced trims / skipped upstream tests)

### B116-G1 — slice patterns with a **named rest binding** `rest @ ..` do not parse
PRECISE: `[h, rest @ ..]` → `syntax error near '@'`.
- Works instead: positional rest `[h, ..]` and fixed `[a, b]` parse (B5).
- Needed: bind the rest sub-slice to a name in a slice pattern.
- Blocks faithful: `slice-pattern-recursion-15104`, `issue-15080`,
  bindings-after-at `slice-patterns`.
- §B catch-up (relates to DIVERGENCES B5).

### B116-G2 — dynamic-slice patterns over `&[T]` returning a value → mlir-gen verify fail
PRECISE: `fn f(v: &[i64]) -> i64 { match v { [a, b] => a + b, [h, ..] => h, [] => 0 } }`
→ in match-as-expression position: `'func.return' op expects parent op 'func.func'`;
in statement position: `cannot be converted to LLVM IR: missing
'LLVMTranslationDialectInterface' ... for op: arith.constant`.
- Works instead: fixed-size **array** `[x, y, z]` patterns over `[T; N]` (imported
  as `array-fixed-pattern-pat`).
- Needed: codegen for dynamic `&[T]` slice patterns that bind elements AND
  produce/return a value out of the arm.
- §B catch-up (relates to DIVERGENCES B5).

### B116-G3 — nested patterns inside an enum-variant payload not supported
PRECISE: `match o { Some(Color::Red) => .., Some(Color::Green) => .. }` →
`pattern Option::Some: nested patterns inside enum-variant payloads are not yet
supported; bind to a name and match in the body` (clean diagnostic, not a crash).
- Works instead: a single binding/wildcard payload (`Some(n)`, `Some(_)`).
- Needed: recurse into a constructor sub-pattern inside an enum-variant payload.
- Blocks faithful: `bindings-after-at/or-patterns` (`Some(Foo | Bar)`),
  `nested-exhaustive-match` (`Foo{ bar: Some(_), ..}`).
- §B catch-up.

### B116-G4 — refutable struct-field sub-pattern not supported
PRECISE: `match a { A { v: 1i64 } => .., A { .. } => .. }` →
`struct pattern: refutable field sub-pattern not yet supported`.
- Works instead: irrefutable field bindings (`A { v: _w }`, `A { x, y }`) and a
  full rest `A { .. }`.
- Needed: refutable (literal/constructor) sub-pattern in a struct-pattern field.
- Blocks faithful: `struct-wildcard-pattern-14308` (kept via the `A{..}` /
  `A{v:_w}` halves), `bindings-after-at/nested-patterns` (`A { a, b: 20 }` — kept
  as `A { a, b }`).
- §B catch-up.

### B116-G5 — match over `&Struct` with field bindings → mlir-gen verify fail
PRECISE: `fn f(p: &P) -> i64 { match p { P { x, y } => *x + *y } }` →
`'llvm.load' op operand #0 must be LLVM pointer type, but got 'i64'`.
- Works instead: match over an owned `P` value, and `ref` field bindings on an
  owned value (`P { x: ref r }` — imported as `ref-binding-in-struct-pat`).
- Needed: match-ergonomics binding modes when the scrutinee is `&Struct`
  (auto-ref the bound fields).
- §B catch-up.

### B116-G6 — nested-tuple pattern in a `let` / `match` arm does not bind inner names
PRECISE (let): `let (a, (b, c)) = (1, (2, 3));` →
`'let <pattern> = expr;' currently supports struct patterns only (other shapes
are refutable; use 'match' or 'let-else')` — and `match` form `(a, (b, c)) => ..`
gives `undefined variable 'b'`.
- Works instead: a FLAT tuple let `let (a, b, c) = ..` (imported as
  `tuple-destructure-let-pat`) and a flat tuple match arm (`(a, _, c)`).
- Needed: recurse into nested tuple sub-patterns to declare inner bindings
  (relates to DIVERGENCES B5's "named-nested-in-tuple" remaining case).
- §B catch-up.

### B116 — skipped upstream (feature/surface, not new gaps)
- `box-pattern-nested` (`box` patterns — `Box<T>` heap, DIVERGENCES B3),
  `match-struct-var-having-boxed-field`, bindings-after-at `box-patterns`,
  `or-patterns-box-patterns`.
- `issue-22546` (turbofish-in-pattern `Foo::<i32>(a, b)` + `<Foo<_> as Tr>::U`
  qualified path in pattern — overlaps RB115-G1/G2 in pattern position).
- `issue-27320`, `rfc-3637 macro-rules` (`macro_rules!` — §A, macros via
  metaprog; the or-/guard pattern features themselves imported separately).
- `slice-patterns-irrefutable`, `slice-array-infer` (`Into<[T;N]>` inference +
  `try_into()?` — stdlib surface, not a pattern gap; the fixed-array pattern
  itself imported as `array-fixed-pattern-pat`).
- `pattern-match-arc-move`, `move-ref-patterns-dynamic-semantics` (Arc / move-ref
  semantics — ownership surface).

### B116 — imported PASSING (27 distinct features)
unit-pattern-fn-arg, struct-wildcard-pattern, integer-range-binding,
char-range, range-exclusive, negative-literal, match-bool, match-guard,
guard-two-bindings, enum-tuple-payload, enum-struct-variant, match-result,
or-pattern-literals, or-pattern-shared-binding, at-binding-range,
bindings-after-at-struct, bindings-after-at-enum, array-fixed-pattern,
struct-destructure-let, tuple-destructure-let, nested-struct-destructure,
tuple-wildcard-mid, ref-binding-in-struct, if-let-binding, while-let-pop,
mut-binding, wildcard-let.
