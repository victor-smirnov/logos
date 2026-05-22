# B144 — UI-surfaced gaps

Batch B144 imported 23 DISTINCT rustc UI run-pass tests (pinned SHA
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`) across: mir (4), or-patterns (3),
self (2), match (2), array-slice-vec (2), traits (1), associated-types (1),
binop (1), let-else (1), lifetimes (1), where-clauses (1), structs-enums (1),
typeck (1), inference (1).
Do NOT modify the compiler/stdlib. All 23 compile + link + exit 0.

Suffix `-b144` on every file (global ctest-name uniqueness). Mined from
less-tapped run-pass areas: tests/ui/{or-patterns, let-else, mir, associated-types,
self, where-clauses, lifetimes, typeck, binop} run-pass tests that earlier batches
had not exhausted, de-duplicated against `RUSTC-PROVENANCE.md`, `pass/<area>/`, and
per-file `Original path:` headers (B6/B100+/B107/B133–B143/Bnever).

## NEW gaps surfaced

> **STATUS 2026-05-22:** G144-2 ✅ FIXED (or-pattern wildcard alt — dispatch +
> dead-block sweep), G144-5 ✅ FIXED (tuple ordering), G144-6 ✅ FIXED
> (tuple-index on call result), G144-3 facet-B ✅ FIXED (let-else continue/break
> else codegen — dead-block sweep), G144-3 facet-A ✅ FIXED (or-pattern in
> `let-else` — SLetElse codegen now OR's the alt discriminants + extracts via
> the first alt; also fixed a bonus pre-existing C-like-enum let-else soundness
> bug where disc_val was null → unconditional match). 
> **G144-1 — ✅ FIXED 2026-05-22 (7e7a06c2): recursive pattern matcher.** Added
> pat_test (pure i1) + pat_bind (per-alt shared-alloca dispatch for or) +
> collect_pat_bindings; wired into gen_match's tuple test/bind for nested
> tuple / structural-or / variant elements; sema bind_pattern_ref recurses into
> Or/Tuple subs. `((true,y)|(y,true),z)`, `((a,b),w)`, `(A(x)|B(x),k)`, and deep
> combos all work. 4415/4415. (Original investigation below kept for the record.)
> ~~REMAINS (clean error): or-pattern with BINDINGS nested as a tuple element~~ (`((true,y)|(y,true), z)`, `(A(x)|B(x),
> k)`). Investigated 2026-05-22: the sema side is trivial (declare the alt
> bindings), but doing so exposes that the tuple-match CODEGEN is FLAT — the
> tuple-arm dispatch tests each element with a single scalar/variant/range
> comparison and has no machinery to (a) dispatch an or-pattern element's alts
> nor (b) extract a binding from whichever alt matched at its (possibly
> per-alt-different) position. Even the same-position form `(A(x)|B(x), k)`
> SIGSEGVs if sema is allowed through; the position-dependent `(true,y)|(y,true)`
> miscomputes. This needs a RECURSIVE pattern-test+bind codegen for tuple
> elements (the flat dispatch can't express nested or-of-tuples) — a real
> feature, not a localized fix. Left as the clean "undefined variable" error
> (sema rejects) until that codegen lands; top-level or-patterns (incl.
> position-dependent `(true,y)|(y,true)` as the WHOLE scrutinee) already work.

### G144-1 — an or-pattern nested INSIDE another tuple position drops its binding (TRACTABLE)

A top-level or-pattern binding the same name from either tuple alternative works:
`match x { (true, y) | (y, true) => .. }` is fine (verified). But the SAME or-pattern
nested inside a larger tuple position fails:

```
match x {
    ((true, y) | (y, true), z) => { /* uses y */ }   // error: undefined variable 'y'
    _ => ..
}
```

errors `undefined variable 'y'` — the binding produced by the inner or-pattern is not
threaded out when the or-pattern sits in a non-top tuple element. The non-nested form
and `(1, y) | (y, 1)` (literal+binding) both work.

Tractability: TRACTABLE — missing-case. The or-pattern binding-collection already works
at the top level of a match scrutinee; it isn't recursing into the bindings of an
or-subpattern that occupies one element of an enclosing tuple pattern. Parallel-mapping
to the working top-level case. `or-patterns/bindings-runpass-1-b144` was distilled to the
top-level form. Upstream `bindings-runpass-1.rs` exercises the nested form.

### G144-2 — an or-pattern mixing a literal with a WILDCARD inside an enum payload mis-counts bindings (TRACTABLE)

`Some(1 | 2)` (two literal alternatives in a payload) works (verified). But
`Some(0 | _)` (literal-or-wildcard) errors:

```
match x { Option::Some(0i64 | _) => true, _ => false }
// error: pattern Option::Some: expected 1 bindings, got 0
```

The wildcard alternative contributes 0 bindings while the (effectively redundant)
literal alternative contributes its own count, and the two are reconciled as a binding
mismatch. Upstream `or-patterns/mix-with-wild.rs` DROPPED. (`struct-like.rs` was also
dropped — see the parse-error note below.)

Tractability: TRACTABLE — missing-case. The or-pattern-alternative binding-consistency
check should treat a literal alternative the same as a wildcard (both bind nothing).
Localized to the payload-or-pattern binding-count reconciliation.

### G144-3 — let-else with an OR-pattern binding fails / a `continue`/`break` else-block crashes codegen (TRACTABLE)

Two distinct facets, both in `let-else`:
- An or-pattern in let-else binding the same name fails the same way as G144-1:
  `let MyEnum::A(x) | MyEnum::B(x) = v else { .. };` → `undefined variable 'x'`
  (and the PARENTHESIZED form `let (A(x) | B(x)) = v else ..;` is a parse error
  `syntax error near ')'`).
- A let-else whose else-block diverges with `continue`/`break` (rather than `return`)
  crashes codegen: even `loop { let 3i64 = n else { n += 1; continue; }; break; }`
  fails with `cannot be converted to LLVM IR: missing LLVMTranslationDialectInterface
  registration for ... cf.br` / `arith.constant`. A let-else whose else `return`s works
  (kept test `let-else-bindings-b144`).

Tractability: TRACTABLE. The or-pattern facet is the same root as G144-1. The
`continue`/`break`-in-else facet is a let-else lowering producing an unregistered
control-flow op when the else-block diverges via a loop-control terminator (vs an
early `return`); the `return`-else path already lowers correctly. Upstream
`let-else-run-pass.rs` (or-pattern + nested-let-else-in-loop) DROPPED on these.

### G144-5 — tuple ORDERING comparison (`<`/`<=`/`>`/`>=`) emits a pointer where an integer is expected (TRACTABLE)

Tuple `==`/`!=` (incl. NESTED tuples) works (verified, kept in `structured-compare-b144`).
But the lexicographic ordering comparisons fail codegen:

```
let a = (1i64, 2i64, 3i64);
if a < (1i64, 2i64, 4i64) { .. }
// error: 'arith.cmpi' op operand #0 must be signless-integer-like, but got '!llvm.ptr'
```

The derived/structural tuple `PartialOrd` lowering passes an aggregate pointer into an
`arith.cmpi` instead of the element value. Upstream `binop/structured-compare.rs`
exercises `<`/`<=`/`>`/`>=` on a 3-tuple; distilled to `==`/`!=` + a user struct
`PartialEq`.

Tractability: TRACTABLE — the tuple `==` path already loads element values correctly;
the ordering path needs to load each element before the integer compare (parallel-map
to the working `==` element-load). Tuple-Ord codegen.

### G144-6 — a TUPLE-field access on a function CALL RESULT fails codegen (TRACTABLE)

A struct-field access on a call result works (`mk().x`, verified). But a tuple-index on a
call result fails:

```
fn tuple2() -> (u16, u8) { (1u16, 2u8) }
fn test2() -> u8 { tuple2().1 }
// error: 'llvm.getelementptr' op operand #0 must be LLVM pointer type ... but got '!llvm.struct<(i16,i8)>'
```

The tuple-index lowering GEPs directly off the SSA struct return value rather than
spilling it to a slot first (the struct-field path already spills). Binding the result to
a `let` first works (kept in `mir_cast_fn_ret-b144`).

Tractability: TRACTABLE — missing-case. Spill the call-result aggregate to an alloca
before the tuple-index GEP, mirroring the struct-field-on-call-result path. Localized to
tuple-index codegen.

## Re-confirmed known-open / blessed-divergence (NOT re-reported as new)

- **`@`-binding combined with an or-subpattern** `z @ (0 | 4)` — parse error
  `syntax error near '4i64'`. Same as B143's at-binding-with-subpattern note;
  `or-patterns/bindings-runpass-1.rs` distilled to drop it.
- **a struct-VARIANT field with an or-pattern subpattern** `Foo { first: 1024 | 2048 }`
  — parse error `syntax error near '1024i64'`; `or-patterns/struct-like.rs` DROPPED.
- **payload nested patterns in `if let` / `while let`** — `if let Some(4|5|6) = o2`
  errors `nested patterns inside enum-variant payloads are not yet supported`; the SAME
  literal-or arm works in `match` position. Distinct surface from the match path
  (B135/B137/B139 known-open nested-in-payload). `if-let-payload-or-b144` uses `match`.
- **`!` (never) as a MATCH SCRUTINEE with bool arms** — `match f() { true => .., false => .. }`
  where `f() -> !` errors `bool pattern requires bool scrutinee, got '!'`; the never type
  doesn't coerce in match-scrutinee position. `match/match-disc-bot.rs` DROPPED. (Same
  never-type-representation family as Bnever Gnever-1/Gnever-2; divergence in match-ARM
  value position DOES work — kept `expr-match-panic-fn-b144`.)
- **closure capturing mutable outer state through a generic `F: FnMut` METHOD on a user
  trait** — the captured local's mutation does not persist across the call
  (closure returns the wrong value); `traits/assignability-trait.rs` DROPPED. Same
  fn-family / `&mut dyn FnMut`-capture family as B142/B143 (B107+).
- **two impls for `(T,&T)` vs `(&T,T)`** — method dispatch binds `T=&i64` wrong on the
  `(&T,T)` impl (`expected &(&i64,&&i64), got &(&i64,i64)`); `typeck/tuple-ref-order-distinct-impls.rs`
  DROPPED. (Coherence/inference for ref-position-distinguished tuple impls.)
- **bodyless unit struct `struct S;`** → `struct S { _z: i64 }` (B107/B137 known-open);
  `where-clause-bounds-inconsistency-b144`.
- **`Box<T>`** receivers/fields → stack `T` / concrete receiver (autoderef-method,
  recursive-enum-box, issue-89935, mir_drop_order) — B111 known-open.
- **`Vec<T>` / `String` / `format!`** → fixed `[T; N]` / i64-accumulator fields
  (assignability-trait, move-out-of-field, dynamic-dispatch) — B111/B135 known-open.
- **in-fn `enum`/`fn` items (in-language `mod`-like nesting)** → module-scope items
  (tag-in-block) — B-mod known-open.
- **turbofish `Result::Err::<i64,i64>(..)`** produced a stray `Result__<error>__i64`
  mlir-gen warning + a wrong value; routed through a typed helper fn (let-else-bindings).

## Mechanical port rules applied (per batch conventions, not gaps)

- `package <name>;` header; `pub fn main()` → `fn main() -> i32 { …; return 0i32; }`;
  `assert!`/`assert_eq!`/`panic!`/`println!`/`unreachable!` → distinct nonzero returns.
- `isize`/`usize` → `i64`/`u64`; all integer literals suffixed; negatives `0 - n`.
- `&self`→`self: &Self`/`self: &<Type>`; `&mut self`→`self: &mut <Type>`;
  `match self`→`match *self`; explicit lifetime params kept verbatim where present.
- tuple-struct/`Box`/`Vec`/`String`/`format!`/`#[repr]`/`#[derive]`/`extern "C"`/`static`
  facets dropped or distilled where incidental to the test's point.

## Source dups dropped (checked by exact `Original path:` vs RUSTC-PROVENANCE.md + ls pass/)

- structs-enums/{rec-extend, expr-if-struct, expr-match-struct, rec, rec-tup, issue-1701,
  small-enum-range-edge}, expr/{early-return-in-binop, return-in-block-tuple, block-generic},
  tuple/{tuple-index, nested-index}, functions-closures/{closure-immediate,
  closure-inference2}, inference/simple-infer, pattern/{ignore-all-the-things, inc-range-pat,
  integer-range-binding}, coercion/{coerce-reborrow-*-{arg,rcvr}, coerce-unify-return,
  basic-ptr-coercions}, binop/binops, autoref-autoderef/auto-ref-bounded-ty-param,
  traits/{copy-trait-implicit-copy, composition-trivial} — ALL already imported (verified
  by `Original path:` header match); candidates skipped.

## Final test set (23)

mir (4): mir_codegen_switch (enum match, mixed payload/unit variants, REORDERED arms),
mir_match_test (inclusive `..=` AND exclusive `..` range arms with bool guards),
mir_codegen_calls_diverging (call to a `-> !` fn from another fn, on a dead branch),
mir_cast_fn_ret (functions returning tuples + tuple-field read via a `let` binding).
or-patterns (3): bindings-runpass-1 (top-level `(true,y)|(y,true)` binding the same name
from either alt), bindings-runpass-2 (payload-binding through `Ok(x)|Err(x)` + inclusive-
range arms + a guard on the bound payload), if-let-payload-or (`while let` accumulator +
a `match` literal-or payload arm `Some(4|5|6)`).
self (2): self-type-param (trait method returning `Self`, impl returns the concrete type),
explicit-self-closures (`&mut self` method iterating a `&[i64]` slice param, mutating self).
match (2): expr-match-panic-fn (`-> !` diverging match-ARM coerces to the value-arm's type),
match-wildcards (wildcard arm selection over a tuple of `Option`s).
array-slice-vec (2): destructure-array-1 (`let [a,b,c,e] = arr;` array-destructure of a
fixed `[D;4]`, return one element by value), vec-matching-fixed (fixed-array match patterns:
leading/trailing `..` rest + full element binding).
traits (1): conditional-dispatch (conditional blanket impl `impl<T:MyCopy> Get for T`
dispatched through a generic `get_it<T:Get>(&T)->T`).
associated-types (1): associated-types-ref-from-struct (assoc type `Test::V` used as a
struct field type AND a `&Self::V` method param, via a generic `TesterPair<T:Test>`).
binop (1): structured-compare (tuple `==`/`!=` incl. NESTED tuples + a user struct `PartialEq`).
let-else (1): let-else-bindings (irrefutable-success binds; refutable-failure runs the
diverging `else { return .. }`; incl. `None`/`Err` failure paths).
lifetimes (1): struct-lifetime-field-assignment-13405 (lifetime-parametric struct with
reference fields, method building a new instance from the receiver's borrowed field).
where-clauses (1): where-clause-bounds-inconsistency (a method declared `where T:Bound`
implemented with inline `<T:Bound>`, and vice versa, consistently dispatched).
structs-enums (1): tag-in-block (an enum threaded through a small free-fn call chain).
typeck (1): issue-89935 (supertrait chain `Foo:Baz:Bar` with blanket impls + a method
through the chain on a concrete `Foo` receiver).
inference (1): return-block-type-inference-15965 (`return { return e; }` nested-return /
never-propagation through a returned block expression).
