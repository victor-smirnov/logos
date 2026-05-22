# B142 — UI-surfaced gaps

Batch B142 imported 27 DISTINCT rustc UI run-pass tests (pinned SHA
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`) across: regions (8), mir (5),
overloaded (3), structs-enums (2), pattern (2), moves (1), traits (1),
typeck (1), match (1), enum (1), unboxed-closures (1).
Do NOT modify the compiler/stdlib. All 27 compile + link + exit 0.

Suffix `-b142` on every file (global ctest-name uniqueness). Mined from
less-tapped run-pass areas: tests/ui/{regions, mir, overloaded,
unboxed-closures, pattern, typeck} run-pass tests that earlier batches had not
exhausted, heavily de-duplicated against existing `pass/` + the per-file
`Original path:` headers.

## NEW gaps surfaced

### G142-1 — a reference-typed enum payload `Option<&S>` reads garbage on bind (TRACTABLE)

Matching an `Option<&S>` (the payload is a REFERENCE to a struct) and binding the
inner ref, then reading a field through it, returns garbage. Isolated repro:

```
struct S { tag: i32 }
let s1 = S { tag: 5 };
let o1: Option<&S> = Option::Some(&s1);
match o1 { Option::Some(r) => return r.tag, Option::None => return 99 }
// returns 152, not 5
```

The same field read works directly off a `&S` local (`let r: &S = &s1; r.tag` → 5),
and an `Option<S>` (by-VALUE payload) binding reads correctly (→ 5). So the bug is
specific to a *reference-typed* enum payload: the payload slot holding a `&S` is
mis-read on extract (likely an extra/missing level of indirection — the binding is
treated as if the payload were the struct itself rather than a pointer to it).

The upstream `match/struct-reference-patterns-12285.rs` (`match Some(&S) { Some(&S) => .. }`)
was DROPPED on this — both its `&S`-payload-pattern form (nested-in-payload
known-open) and the name-bound workaround hit this gap.

Tractability: TRACTABLE — wrong-indirection-level on extract of a reference-typed
enum payload. The by-value payload extract and the plain `&S`-local field-read
paths both work; this is the missing case where the payload's own type is itself a
reference. Parallel-mapping to the by-value payload extract (skip one deref).
NOT a deep (representation) gap.

### G142-2 — float `is_nan()` / `is_infinite()` methods are not on the surface (catch-up, §B)

`f.is_nan()` / `f.is_infinite()` on an `f32`/`f64` error: *"method call: receiver is
not a struct (got f64)"*. In Rust these are inherent methods on the float
primitives. `mir/mir_temp_promotions-b142` rewrites the upstream
`!(f.is_nan() || f.is_infinite())` to the NaN self-inequality form `!(f != f)`
(which works) to keep the temp-promotion / negated-disjunction shape.

Tractability: TRACTABLE — missing inherent-method impls on the float primitives
(same family as the char:Copy / primitive-method catch-ups). A stdlib add (intrinsic
classify), not a deep gap.

## Re-confirmed known-open / blessed-divergence (NOT re-reported)

- **UFCS turbofish trait-static call** `Foo::<i32>::get(&x)` errors
  *"call to undefined static method 'Foo::get'"* — the equivalent method-call form
  `x.get()` works. `typeck/ufcs-type-params-b142` uses `x.get()`. Same Self/trait
  static-call-by-path family as B141 G141-2 / B140 G140-3 (static-method-by-return-type).
- **call-sugar autoderef on a generic FnMut bound through `&mut F`** — `x(2)` where
  `x: &mut F, F: FnMut(i64)->i64` errors *"call to undefined function 'x'"*
  (`unboxed-closures-call-sugar-autoderef.rs` DROPPED). The `&mut dyn FnMut(..)`
  trait-OBJECT form works (overloaded-calls-object-{zero,one,two}-args-b142); only
  the generic-`&mut F`-bound call-sugar autoderef is missing. Fn-family area (B107+).
- **closure literal directly as a `fn(_)` argument** — `foo(|x| x + 1)` where
  `foo(f: fn(i64)->i64)` is a parse error (`syntax error near '|'`). Binding the
  closure to a `let` first / passing a fn ITEM works (`closure_to_fn_coercion-expected-types.rs`
  DROPPED). Closure-to-fn-ptr-at-call-site coercion family.
- **`if (return) {}`** — `return` as an if-condition is a parse error
  (`syntax error`); `expr/if/if-ret.rs` DROPPED. Same diverging/never-as-expression
  family as B141 G141-1 (void-typed diverging branch).
- **`Box<T>`** receivers/fields → stack `T` (move-4, regions-borrow-uniq, generic-tag,
  enum-nullable, regions-link-fn-args) — B111 known-open.
- **`Vec<T>`** → fixed `[T; N]` borrowed as `&[T]` (regions-dependent-autoslice,
  regions-link-fn-args) — B111 known-open.
- **tuple-struct declarations + numeric-field surface** (`struct Foo(isize)`) →
  named-field structs; the constructor-as-fn-pointer point (tuple-struct-constructor-pointer)
  kept via a free constructor fn bound to a `fn`-ptr — B107/B141 known-open.
- **bodyless unit struct `struct S;`** → `struct S { tag: i32 }` — B107/B137 known-open.
- **nested patterns inside enum-variant payloads** (`Some(&S)`) → bind a name +
  match in the body — B135/B137/B139 known-open.

## Mechanical port rules applied (per batch conventions, not gaps)

- `package <name>;` header; `pub fn main()` → `fn main() -> i32 { …; return 0i32; }`;
  `assert!`/`assert_eq!`/`panic!`/`println!`/`unreachable!` → distinct nonzero return codes.
- `isize`/`usize` → `i64`/`u64`; all integer literals suffixed; negatives `0 - n`.
- `&self`→`self: &Self` / `&'a self`→`self: &'a Type`; `match self`→`match *self`.
- closures given block bodies (`|x| { .. }`); explicit lifetime params on receivers /
  fn signatures (`'a`/`'b`/`'r`) kept verbatim.
- `Box`/`Vec`/`String`/`format!`/`#[repr]`/`#[derive]`/`type_ascribe!` facets dropped
  or distilled where incidental to the test's point.

## Source dups dropped (checked by exact basename vs RUSTC-PROVENANCE.md + per-file headers)

- `fn/expr-fn`, `fn/fun-call-variants`, `inference/auto-instantiate` — ALREADY
  imported (found in per-file `Original path:` headers); candidates skipped.
- `match/issue-11940` (const `&str` literal match arm) — DROPPED: string-literal
  match patterns are a parse error (`syntax error near '{'`); recorded as known-open
  going forward (same str-pattern family as G142 closure-related notes — distinct
  surface).
- `traits/inheritance/num1` — DROPPED: relies on a trait-static call through a type
  parameter `T::from_i32(1)` which errors *"call to undefined static method 'T::from_i32'"*
  (Self/trait-static family).

## Final test set (27)

regions: regions-simple (`&mut i64` write-through), regions-addr-of-ret (reborrow
`&*x` return of a `&i64`), regions-infer-call (two distinct-lifetime `&i64` args
linked through a call), regions-borrow-uniq (`&u64`-derived-from-local into a deref
fn), regions-dependent-autoslice (lifetimes linked through nested `&[u64]`
passthrough fns), regions-expl-self (explicit lifetime on the self-receiver
`fn foo<'a>(self: &'a Foo)`), regions-creating-enums5 (self-referential
lifetime-parametric enum `Ast<'a>` with `&'a Ast<'a>` payloads), regions-link-fn-args
(region-linking through a `FnOnce(&'a [i64])->&'a [i64]` arg), regions-issue-21422
(`(self as *const P) == (other as *const P)` raw-ptr identity compare).
mir: mir_small_agg_arg (tuple-destructuring fn PARAM `fn foo((x,y):(i8,i8))`),
mir_coercion_casts (fn item reified via `as fn(i64)` cast then invoked),
mir_temp_promotions (negated boolean disjunction tail over an f32 param; G142-2 note),
mir_void_return_2 (void fn tail-called from another void fn), mir_ascription_coercion
(`&[i32;3]` → `&[i32]` slice coercion in a typed `let`).
overloaded: overloaded-calls-object-zero-args / -one-arg / -two-args (calling a
`&mut dyn FnMut(..)->i64` trait-object of 0/1/2 args through a fn param).
structs-enums: issue-50731 (uninhabited `enum Void {}` declared + present + a fn-item
→ `fn(i64)` pointer coercion), tuple-struct-constructor-pointer (variant constructor
as a `fn`-ptr value, via a free ctor fn).
pattern: irrefutable-unit (nested-unit tuple `let ((),()) = ((),())`), issue-8351-2
(struct-variant match arms with LITERAL field sub-patterns incl. a field-permuted arm).
moves: move-4 (chain of move-rebinds of a struct value through a fn).
traits: bound-multiple (fn generic over `T: PartialEq + PartialOrd` at i64).
typeck: ufcs-type-params (trait `Foo<T>` impl for i32, called via `x.get()`; G142-3 note).
match: match-usize-min-max-pattern (2-tuple arm with u64 corner-value element patterns).
enum: enum-nullable-simplifycfg-misopt (switch over a payload-variant with a
literal-payload arm + `Nil` + catch-all).
unboxed-closures: unboxed-closures-direct-sugary-call (`mut`-bound no-arg closure
called by sugary `()` syntax).
