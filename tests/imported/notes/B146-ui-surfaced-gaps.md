# B146 — UI-surfaced gaps

Batch B146 imported 24 DISTINCT rustc UI run-pass tests (pinned SHA
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`) mined from genuinely less-tapped run-pass
areas: pattern (2), inference (3), where-clauses (2), associated-consts (2), fn (3),
statics (3), recursion (2), coercion (1), enum-discriminant (1), enum (1), type (1),
type-inference (1), reborrow (1), autoref-autoderef (1).
Do NOT modify the compiler/stdlib. All 24 compile + link + exit 0.

Suffix `-b146` on every file (global ctest-name uniqueness). De-duplicated against
`RUSTC-PROVENANCE.md`, `pass/<area>/`, and per-file `Original path:` headers
(B6/B100+/B107/B133–B145/Bnever). The heavily-mined binding/match/expr/moves dirs were
re-checked file-by-file and skipped where already imported.

## NEW gaps surfaced

### G146-1 — ✅ FIXED 2026-05-22 (6bf2bd48): EMatchExpr recursive matcher (was stmt-only)

A top-level tuple pattern with literal/range elements works (verified, kept
`tuple-range-12582-b146`). But when an inner tuple sits as an ELEMENT of an enclosing
tuple pattern, the inner element value-tests are NOT applied — the arm matches on the
OUTER structure alone:

```
let p = ((1i64, 2i64), 9i64);
match p {
    ((1i64, 1i64), _) => 1i64,   // <-- WRONGLY matches ((1,2),9): inner (.,.) untested
    ((1i64, 2i64), _) => 3i64,
    _ => 4i64,
}
// returns 1, should return 3
```

This is a SILENT wrong-result (no error). It is the value-test counterpart of the B144
G144-1 nested-tuple BINDING gap (since fixed for bindings, 7e7a06c2): the recursive
pattern matcher now binds nested-tuple sub-patterns, but the nested-tuple element
*scalar/literal/range equality test* is still flat — a tuple sitting as a tuple element
contributes no inner comparison. Distinct from G145-2 (that was a LITERAL inside an
ENUM-PAYLOAD position; this is a TUPLE inside a TUPLE position).

Tractability: TRACTABLE — extend the recursive pattern TEST (not just bind) to recurse
into a tuple-typed element of an enclosing tuple, mirroring the now-working recursive
bind path (pat_test should call itself on tuple sub-elements). `pattern/issue-12582.rs`
was distilled to the top-level tuple-range form (the nested `((1..=2,2),)` arm dropped).

### G146-2 — ✅ FIXED 2026-05-22: <Type as Trait>::CONST and ::method() qualified path (grammar)

A trait associated const read through a fully-qualified `<i32 as Foo>::ID` path is a
syntax error (`syntax error near 'if'` — the parser bails on the `<...>::` qualified
path). The concrete-type path `i32::ID` resolves the same impl const fine (kept
`associated-const-trait-b146`), and inherent assoc consts `Foo::FOO` work (kept
`associated-const-access-31267-b146`). The generic form `T::ID` for `T: Foo` also
fails (`unknown enum 'T'` — assoc-const through a type parameter is not resolved).

Tractability: TRACTABLE for the qualified-path FACET — grammar/parser missing-case:
accept a `<Type as Trait>::ITEM` qualified path in expression position and lower it to
the same impl-const lookup the bare `Type::ITEM` path already performs. The `T::ID`
generic facet is a separate sema gap (resolve an assoc const off a type-parameter's
bound). `associated-const.rs` adapted to the concrete-type path.

### G146-3 — §A DIVERGENCE (const-eval in discriminant position → use metacall or explicit literal; NOT a gap)

An explicit enum discriminant that is an expression — `enum Foo { Bar = (5, 42).1 }`
(a tuple-index expression) — is a syntax error (`syntax error near 'enum'`). The
literal and `Variant as int` discriminant forms work (covered by existing
enum-discr / clike tests). `enum-discriminant/issue-50689.rs` DROPPED on this.

Tractability: arguably a Divergence (const-eval in discriminant position — see the
no-const-eval policy; the discriminant grammar admits only a literal / simple cast).
Recorded here as the surfaced limitation; the test is dropped rather than distilled
(its sole point is the discriminant-expression).

## Re-confirmed known-open / blessed-divergence (NOT re-reported as new)

- **a `()`-typed function PARAMETER is rejected** — `fn f(u: ())` errors `parameter 'u'
  has unit type '()'; unit-typed parameters carry no information` (a clean error, not a
  crash). `type/unit-type-basic-usages.rs` distilled to drop the unit PARAM (kept the
  `()` let / reassign / unit-return); `pattern/unit-pattern-matching-in-function-argument-7519.rs`
  (whose whole point is the unit param `foo(():())`) DROPPED. (Unit-type as a value/return
  works; only the param slot is rejected — a deliberate "carries no information" guard.)
- **a bare block `{ e }` as the WHOLE fn body** is not recognized as the value tail
  (`not all paths return a value`) — same as the Bnever "Other observation". `fn/expr-fn.rs`
  `f_block` rewritten as `let x = { e }; x` (block-in-let works).
- **a labeled BLOCK expression** `'blk: { break 'blk e }` is a parse error (`syntax error
  near ':'`) — only labeled LOOPS are supported. `loop-match/break-to-block.rs` DROPPED
  (its core is exactly the labeled-block state machine; the `#[loop_match]` attr is also
  nightly-only). Labeled-loop break-value already works (Bnever).
- **`let mut n;` (uninitialized let with no type annotation)** is a parse error (`syntax
  error near 'n'`) — deferred-init locals need a type or initializer. `inference/simple-infer.rs`
  DROPPED (its whole point is the bare deferred-init `let mut n; n = 1;`).
- **nested-enum match exhaustiveness** — two arms `Other1(Foo::Baz)` + `Other1(Foo::Bar(_))`
  are not recognized as together covering the `Other1` variant; a `_` catch-all is needed.
  `pattern/issue-6449.rs` added a `_` arm (kept the nested dispatch). (Exhaustiveness for
  nested-enum payload patterns — narrower facet of the match-exhaustiveness surface.)
- **`Box<T>`** payloads/receivers/fields → raw-pointer `*const T` / stack `T` / `&dyn T`
  (mutually-recursive-types, recursive-raw-pointer-field, reborrow-mutable-reference,
  dyn-trait-object-coercion-3794) — B111 known-open.
- **`Vec<T>` / `vec!` / `String`** → fixed arrays / field reads (auto-instantiate,
  static-list-initialization) — B111/B135 known-open.
- **`static` / `static mut`** → returned values / locals (refer-to-other-statics-by-value,
  static-shift-init-1660, static-list-initialization) — batch convention, not a gap.
- **in-language nested `fn` items** → module-scope fns (expr-fn, nested-function-names-8587)
  — B-mod known-open.
- **`#[repr(u8/i8)]` enums + niche layout** → dropped (get-discr distilled to the generic
  `match_e<X>` discriminant read; the 254-variant repr enums are layout-only).

## Mechanical port rules applied (per batch conventions, not gaps)

- `package <name>;` header; `pub fn main()` → `fn main() -> i32 { …; return 0i32; }`;
  `assert!`/`assert_eq!`/`panic!`/`println!` → distinct nonzero returns.
- `isize`/`usize` → `i64`/`u64`; all integer + float literals suffixed.
- `&self`→`self: &Self`/`self: &<Type>`; explicit lifetime params kept where present.
- empty unit struct `struct S` / `struct S;` → `struct S { _z: i64 }`; assoc types kept.

## Source dups dropped (checked by exact `Original path:` vs RUSTC-PROVENANCE.md + ls pass/)

- binding/{let-destruct-ref, match-pipe-binding, nullary-or-pattern, match-with-ret-arm,
  match-phi, match-tag, match-pattern-bindings, exhaustive-bool-match-sanity, match-join,
  multi-let}, match/{match-usize-min-max-pattern, match-on-negative-integer-ranges,
  match-range-char-const, issue-18060, issue-26251}, expr/{scope, copy, if-bot},
  moves/{move-nullary-fn, move-3-unique} — ALL already imported (verified by `Original
  path:` header match in `pass/<area>/`); candidates skipped.
- `enum-discriminant/issue-50689.rs` — DROPPED on G146-3 (discriminant-expression).
- `loop-match/break-to-block.rs` — DROPPED on the labeled-block parse error.
- `inference/simple-infer.rs` — DROPPED on the bare `let mut n;` parse error.
- `pattern/unit-pattern-matching-in-function-argument-7519.rs` — DROPPED on the
  unit-typed-parameter rejection.

## Final test set (24)

pattern (2): tuple-range-12582 (a `(1..=2, 2)` tuple range pattern selected over literal-
tuple arms, #12582 top-level form), nested-enum-match-6449 (nested enum-variant matching
with literal arms, false guards, and bindings, #6449).
inference (3): auto-instantiate-45 (type-param inference of `f<T,U>(T,U)->Pair<T,U>` from
its args, #45), lub-glb-fn-type (LUB of two `fn(i64)` items selected by a `match` then
called), mul-rhs-visitor-3743 (associated-type RHS-visitor `mul_vec2_by` dispatch, #3743).
where-clauses (2): where-clauses-method (a method `where T: Eq` adding an out-of-scope
constraint), where-clause-method-substitution (a generic method whose where-clause
`A: Foo<B>` discharges through impls).
associated-consts (2): associated-const-access-31267 (inherent assoc const that is a fixed
array `Foo::FOO`, #31267), associated-const-trait (a trait assoc const `const ID` read via
the concrete `i32::ID`).
fn (3): expr-fn (fn bodies whose tail is a literal / match / if / block-in-let / explicit
return / let / generic identity), fun-call-variants (a fn item passed by value to a higher-
order `F: FnOnce(i64)->i64`), nested-function-names-8587 (same-named helper fns in
different methods stay distinct, #8587).
statics (3): refer-to-other-statics-by-value (one value initialized by reference to another,
modeled via fns), static-list-initialization-5917 (a struct holding a fixed list, read its
first element, #5917), static-shift-init-1660 (a shift-expression initializer `1 << 2`,
#1660).
recursion (2): instantiable (an instantiable self-referential struct via a raw-pointer
field), recursive-raw-pointer-field-19001 (a struct with a `*mut Self` field, #19001).
coercion (1): dyn-trait-object-coercion-3794 (coerce `&S` to `&dyn T` and dispatch the
trait method, #3794).
enum-discriminant (1): get-discr-generic-match (a generic enum `E<X>` match returning a
per-variant tag, instantiated at i64/bool).
enum (1): mutually-recursive-types (a self-recursive enum via a raw-pointer payload + a
C-like enum).
type (1): unit-type-basic-usages (`()` unit type as a let binding, reassignment, and a
unit-returning fn).
type-inference (1): float-unification-14382 (generic `translate<S: POrd<S>>(S)->Matrix4<S>`
with `impl POrd<f32> for f32`, #14382).
reborrow (1): reborrow-mutable-reference-28839 (reborrow a `&mut Foo` rather than move,
#28839).
autoref-autoderef (1): auto-ref-trait-method (call a `&self` trait method on a by-value
receiver via autoref).
