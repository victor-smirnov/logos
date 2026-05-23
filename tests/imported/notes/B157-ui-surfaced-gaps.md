# B157 — rustc UI run-pass import: surfaced gaps

Batch B157 imported **26 NEW DISTINCT run-pass tests** from `tests/ui/`, mined
for FEATURE COVERAGE across FRESH / lightly-mined areas:
generics (9), binding (5), autoref-autoderef (2), expr (2), tuple (2),
cast (2), self (1), fn (1), enum (1), structs-enums (1).

Workflow matches B149–B156: faithful ports, `pub fn main()` → `fn main() -> i32
{ …; return 0; }`, isize/usize → i64/u64, integer/float literals suffixed (float
literals written with a decimal point — `1.0f32`, not `1f32`, which the lexer
rejects), `assert!`/`assert_eq!` → early-return sentinels (distinct nonzero
codes), println!/derive/Box/Rc/RefCell/Vec/PhantomData/named-lifetimes/`#[repr]`
dropped or reshaped where incidental, nested fns/type decls hoisted, `&self`/
`&mut self` → `self: &Self` / `self: &mut Self`, by-value `self` → `self: Self`.
All 26 compile + link + exit 0 against the as-is `build/bin/logosc` (no compiler
changes). Link line uses `-Wl,--gc-sections` (as for B149–B156).

Coverage highlights: identity generic fn instantiated at isize/char/struct via
turbofish (generic-fn) and inferred from the arg (generic-fn-infer); generic
struct `Pair<T>`/`Triple<T>` field access + pass-through generic fn
(generic-type, generic-unique); mixed-width generic struct passed by value with
layout preserved (issue-1112); binding a generic fn as a fn-ptr value `let f =
id::<T>;` then calling it (issue-333); generic-newtype destructuring inside a
generic accessor fn (newtype-with-generics); a generic STATIC trait method
called via the concrete-type path `Holder::apply::<U>` with an `fn(&T)->U`
fn-ptr (generic-static-methods); type-turbofish associated-fn path
`S::<i64>::new(..)` with an inferred method type-param (mid-path-type-params);
generic T flowing through an `if`/`match` expression then a fn-ptr comparator
(if-generic, expr-match-generic); blanket `impl<T:Baz> Foo for T` whose default
method auto-refs `self` to a `&self` Baz method (auto-ref-bounded-ty-param);
auto-ref of a value receiver to a `&self` trait method (auto-ref); several
explicit-self receivers incl. a `&self`-returning-a-borrowed-field method
(explicit-self); Copy struct copied by `let mut y = x;` leaving the source
intact (copy); direct vs FnOnce-bounded indirect call (fun-call-variants); tuple
type alias + let-destructure + by-value tuple param (tup); nested tuple field
access `.1.1.1` (nested-index); `let ref y = x` reference binding
(let-destruct-ref); nested matches with a conditionally-assigned local
(nested-matchs); a diverging match arm unified with a value arm
(match-with-ret-arm); struct-like enum variant construct + match
(struct-like-variant-construct); tuple-struct built with out-of-order NUMERIC
field-name literals `S { 1:.., 0:.. }` (numeric-fields); numeric `as` casts
across int/float widths verified by value (supported-cast); u8-expr `as char`
(u8-to-char-cast-9918).

## Gaps surfaced

### ⚠️ G157-1 — ✅ FIXED: by-value tagged-enum fn-param `==` SEGFAULTS at runtime
**FIXED** (this session). A by-value tagged-enum param arrives as the heap ptr
(one level); the `==`→`eq` method takes `&Enum` (two-level). `&param` returned
the bare heap ptr because by-value enum params were not registered in
`var_tagged_enum_`, so EAddrOf's enum-spill path didn't fire → `eq` loaded the
i32 disc as a pointer → SIGSEGV. Fix: register by-value TAGGED-enum params (gated
on a resolvable TaggedEnumInfo so C-like i32 enums keep the scalar-spill path) in
`var_tagged_enum_` during param binding (`mlir_gen_fn.cpp`), so `&param` spills to
a slot. Regression: `tests/logos/pass/by_value_enum_param_eq.logos`.
`compare-generic-enums` can now be re-imported. (NOTE: custom enums still need an
explicit `Eq` impl for `==` — by design, separate from this fix.) Original
report follows.

### ⚠️ G157-1 — by-value `Option<T>` fn-param `==` SEGFAULTS at runtime (silent crash)
Passing an `Option<i64>` (or any enum) by value into a function and comparing it
with `==` inside the callee crashes at runtime (SIGSEGV), even when both operands
are `Some`. The INLINE form (compare two `Option<i64>` locals in the same fn
body) works fine — only the across-a-fn-boundary by-value-param form crashes.
Minimal repro:
```
fn cmp(x: Option<i64>, y: Option<i64>) -> bool { return x == y; }
fn main() -> i32 {
    let s: Option<i64> = Some(3i64);
    let s2: Option<i64> = Some(4i64);
    if cmp(s, s2) { return 1i32; }   // SIGSEGV inside cmp
    return 0i32;
}
```
Likely a by-value enum-parameter ABI / receiver-form mismatch when the `==`
desugars to the enum `eq` method (the operand is the by-value param, not a
place). DROPPED `structs-enums/compare-generic-enums` (whose whole point is
`Option<i64> == Option<i64>` across a `cmp` fn). TRACTABLE-looking — the inline
path already works, so the fix is localized to how by-value enum params are
materialized for the `==`→`eq` call. Highest priority.

### G157-2 — two impls of a generic trait `Trait<T>` for the same type collide
`impl MyTrait<u64> for MyType` + `impl MyTrait<u8> for MyType` (selected by the
result type) emit the same mangled symbol `MyType__get` → `error: duplicate
function 'MyType__get'`. Method mangling does not include the trait type-arg, so
result-type-driven dispatch between two instantiations of one generic trait on
one type is unsupported. DROPPED `traits/multidispatch1`. (Related to the
trait-aware-method-mangling work — that keyed on trait NAME collisions; this is a
same-trait-different-type-arg collision.)

### G157-3 — numeric-field MATCH pattern `S { 0: a, 1: b }` is a parse error
The numeric tuple-struct field-name LITERAL `S { 1: v, 0: w }` works (G150-3),
but the corresponding MATCH PATTERN `match s { S { 0: a, 1: b } => … }` is a
parse error (`syntax error near '{'`). Workaround in `numeric-fields`: matched
with the positional tuple pattern `S(a, b)` (binds the same fields). TRACTABLE —
the literal grammar already accepts numeric field keys; the pattern grammar needs
the same numeric-key rule.

### G157-4 — method-level turbofish in an associated-fn path is a parse error
`Type::method::<U>(..)` (a `::<…>` after the method name in an associated-fn
call path) is a parse error (`syntax error near 'new'`). The TYPE-level turbofish
`Type::<T>::method(..)` parses fine, and method-call turbofish on a VALUE receiver
`v.method::<U>()` works. Workaround in `mid-path-type-params`: dropped the
`::new::<f64>` method turbofish (U is inferred from the arg), kept
`S::<i64>::new(..)`. TRACTABLE — needs the path-parser to accept a turbofish on
the final segment of an associated-fn path.

### G157-5 — `let StructLit(x) = v;` at a CONCRETE generic instance binds `x` as bare `T`
Destructuring a generic tuple-struct value of a CONCRETE type
(`let MyVal(direct) = w2;` where `w2: MyVal<i64>`) binds `direct` with the
struct's bare type-param `T` rather than the concrete `i64`, so a later
`direct != 9i64` is a `type mismatch (T vs i64)`. The same destructure INSIDE a
generic fn (where `T` is bound by the call) works. Workaround in
`newtype-with-generics`: route the destructure through a generic accessor fn.
TRACTABLE — sema needs to substitute the let-binding's pattern-field types using
the scrutinee's concrete type-args, not the struct's declared params.

### G157-6 — static trait method via the trait-name path can't infer Self
`Mapper::apply(&h, inc)` (calling a static/no-self trait method through the trait
name) → `error: call to undefined static method 'Mapper::apply'`. This mirrors
Rust's own E0790 (`static-method-generic-inference`) — Self is not inferable from
the args alone. Not a true Logos-only gap (Rust rejects the same shape). The
concrete-type path `Holder::apply(&h, inc)` works and is used in
`generic-static-methods`. Recorded for completeness; NOT tractable as a "bug"
(matches Rust semantics).

## Dropped tests (and why)
- ~~`structs-enums/compare-generic-enums`~~ — **NOW IMPORTED**
  (`compare-generic-enums-b157.logos`) after the G157-1 fix; `Option<i64> ==
  Option<i64>` across a `cmp(x, y)` fn now works.
- `traits/multidispatch1` — G157-2: relies on two `MyTrait<T>` impls for one type
  at different T; they collide on the mangled method symbol at compile time.
