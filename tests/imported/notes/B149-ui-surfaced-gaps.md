# B149 — UI-surfaced gaps

Batch B149 imported **14 DISTINCT rustc UI run-pass tests** (pinned SHA
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`), mined for FEATURE COVERAGE across
associated-types, default-methods, where-clauses, multi-arg-trait dispatch, lifetime
params, slice/struct-variant destructuring, char-range guards, and large enum
discriminants. Areas: traits (5), associated-types (1), generics (1), pattern (1),
regions (1), functions-closures (1), mir (1), match (1), enum (1), where-clauses (1).
Do NOT modify the compiler/stdlib. All 14 compile + link + exit 0.

Suffix `-b149` on every file (global ctest-name uniqueness). De-duplicated against
`RUSTC-PROVENANCE.md` and per-file `Original path:` headers (no source-path overlap with
prior batches).

## NEW gaps surfaced

### G149-1 — ✅ FIXED (2026-05-22): generic fn dispatching a method on an ASSOCIATED-TYPE PROJECTION return value crashes mlir-gen — "mlir_gen: unsupported receiver kind for struct/class access" → runtime SIGSEGV

**Fix**: mono's EMethodCall retargeting (mono_clone.cpp) — which rewrites a method call on a generic receiver to the concrete `<cname>__<method>` symbol — was gated on the ORIGINAL receiver being a TypeVar. An associated-type-projection receiver (`r: G::R`) was skipped, so the method stayed unresolved and codegen hit gen_recv_struct with an AssocType receiver. `subst_type` already normalizes `G::R`→concrete via assoc_impls_; the gate now also fires for an AssocType orig receiver, but only when the SUBSTITUTED receiver is concrete (not still TypeVar/AssocType — an unresolved projection is left as-is). Re-import `associated-types-bound-b149` (both i32+u32 instantiations). 4515/4515. ORIGINAL REPORT below:

A generic fn whose body calls a method on the result of another method that returns an
associated-type projection (`Self::R`, with `R: Bound`):

```
trait ToI32 { fn to_i32(self: &Self) -> i32; }
impl ToI32 for i32 { fn to_i32(self: &Self) -> i32 { return *self; } }
trait GetToI32 { type R: ToI32; fn get(self: &Self) -> Self::R; }
impl GetToI32 for i32 { type R = i32; fn get(self: &Self) -> i32 { return *self; } }

fn foo<G: GetToI32>(g: G) -> i32 {
    let r = g.get();        // r : G::R  (= i32 once G=i32, but stays a projection here)
    return r.to_i32();      // CRASH: "unsupported receiver kind for struct/class access"
}
```

The DIRECT form `let r: i32 = x.get(); ...` works fine — only inside the GENERIC fn, where
`g.get()`'s return type is the unresolved associated-type projection `G::R`, does codegen
emit a struct/class field/method access on a receiver whose kind it doesn't recognise.
logosc still writes an output binary (only an mlir_gen diagnostic, exit 0), so the result
is a **silent miscompile → runtime segfault**. Tractability: MODERATE — the assoc-type
projection on a generic type-param's method return must be normalized to the bound's
concrete/abstract receiver kind before method dispatch. `associated-types/associated-types-bound.rs`
DROPPED on this (its `foo<G: GetToI32>(g) { g.get().to_i32() }` is exactly the trigger).
A NON-crashing faithful subset of the same area was imported as
`factory-assoc-type-b149` (assoc-type projection works fine when the impl is concrete,
incl. the recursive tuple-impl, just not when routed through a generic-param method-chain).

### G149-2 — ✅ FIXED (2026-05-22): passing `&<int-literal>` to a `&self` method of a 3-FIELD GENERIC struct AFTER mutating it via `&mut self` reads garbage / corrupts the value

**Fix**: ROOT CAUSE was not aliasing — it was integer-literal width. `&<int-literal>` lowers to an `AddrOfTemp` whose inner literal keeps its default width (IntLit→i32); codegen allocated an i32 temp and stored i32, but the callee loaded `i64` through the `&i64` pointer → 4 bytes of value + 4 bytes of adjacent stack garbage (non-deterministic). `widen_int_expr` (sema_impl.hpp) bailed for ref-vs-ref. Now it recurses into an `AddrOfTemp` against `&T`/`&mut T`: when the inner is an integer expr/literal widenable to the pointee, it re-casts the inner and rebuilds the temp sized to T. Regression `tests/logos/pass/addr_of_int_literal_widen` + re-import `class-impl-very-parameterized-trait-b149`. 4514/4514. ORIGINAL REPORT below:

A generic struct with THREE fields (the generic field last) mutated through a `&mut self`
method in a loop, then queried by a `&self` method that takes a `&i64` arg:

```
struct C<T> { meows: i64, how_hungry: i64, name: T }
impl<T> C<T> {
    fn bump(self: &mut C<T>) { self.meows = self.meows + 1; }
    fn ck(self: &C<T>, k: &i64) -> bool { return *k <= self.meows; }
}
fn main() -> i64 {
    let mut c: C<i64> = C { meows: 2, how_hungry: 57, name: 9 };
    let mut i: i64 = 0;
    while i < 6 { c.bump(); i = i + 1; }   // meows now 8 (verified correct via c.meows)
    let m: i64 = c.meows;                  // m == 8  ✓
    if m != 8 { return 50 + m; }
    if c.ck(&2) { return 0; }              // 2 <= 8 is TRUE, but ck returns FALSE  ✗
    return 9;                              // ← taken
}
```

`c.meows` reads back correctly as 8, but `c.ck(&2)` (which compares `*k <= self.meows`)
returns the WRONG answer — and a sibling probe `fn pk(&self, k:&i64)->i64 { return *k; }`
returns garbage (the test crashed with SIGILL from an out-of-range exit code, i.e. `*k` was
not 2). The fault is FRAGILE: it disappears when the struct has 2 fields, when the generic
`name` field is removed, when the surrounding `let m` block is absent, or when the arg is a
NAMED local (`&two`) instead of a literal-ref (`&2`). It depends on the interaction of
(a) a 3+-field GENERIC struct, (b) prior `&mut self` mutation, and (c) a `&<literal>` arg to
a later `&self` method — strongly suggesting an alloca/stack-slot aliasing bug in the
literal-temporary's address vs the struct's mutated field slot. Tractability: DEEP (codegen
place/temporary allocation). This is the HIGHEST-priority item for the maintainer — a
**silent wrong-answer + crash** with no diagnostic. `structs-enums/class-impl-very-parameterized-trait.rs`
DROPPED on this (`spotty.contains_key(&2)` after the `speak()` loop is the exact shape).

### G149-3 — DIVERGENCE/gap (moderate): a `move` closure does NOT isolate a captured Copy local from the original — it mutates the outer variable

```
let mut counter: i64 = 0;
let mut tick = move || -> i64 { counter = counter + 1; return counter; };
tick(); let v: i64 = tick();
// v == 2  ✓   but Rust guarantees the OUTER `counter` stays 0 (move copies a Copy capture);
// Logos leaves outer counter == 2  ✗
```

The closure's RETURN values are correct (1 then 2), but `move` over a Copy type (`i64`) is
not making a private copy — the outer `counter` is mutated too. In Rust, `move` with a Copy
capture copies the value into the closure, so the outer binding is untouched. This is a §B
catch-up gap (move-by-copy semantics for Copy captures), not a blessed divergence.
`unboxed-closures/unboxed-closures-infer-fnmut-move.rs` DROPPED (its `assert_eq!(counter, 0)`
after the move closure is precisely this check). The non-`move` borrowing form works
correctly and is what `return-from-closure-b149` uses.

### G149-4 — DEFERRED (moderate): a NAMED slice-rest binding `[_, xs @ ..]` / `[_, .. xs]` is unsupported (parse error near `@` / `..`)

Anonymous middle/trailing `..` rest patterns work (`[0, ..]`, `[0, .., 3]`, `[..]` — all
imported in prior batches), but BINDING the rest slice to a name does not parse:

```
match *v {
    [] => 0,
    [_] => 1,
    [_, xs @ ..] => 1 + count_members(xs)   // syntax error near '@'
}
```

This is the slice-pattern counterpart of memory's open B-pt-13 ("named slice-bind in
tuple"). Tractability: MODERATE (grammar + a binding for the rest sub-slice, which dynamic
`&[T]` patterns already length-track per B-pt-12). `pattern/slice-pattern-recursion-15104.rs`
DROPPED on this (its whole point is the recursive `[_, ref xs @ ..]` tail-bind).

### G149-5 — DEFERRED (moderate): a STATIC trait method called on a generic type-param (`T::from_int(..)` or `Trait::from_int(..)`) whose result type drives selection is unresolved — "call to undefined static method 'T::from_int'"

```
trait MyNum { fn from_int(i: i64) -> Self; }
trait NumExt: MyNum { }
fn make_one<T: NumExt>() -> T { return T::from_int(1); }   // undefined static method 'T::from_int'
//                                     ^ also fails as `MyNum::from_int(1)`
```

A no-receiver (static / associated) trait fn invoked through a generic type-param bound,
where the impl is selected by the return type (`-> Self` = T), is not resolved in either
the `T::method(..)` or the `Trait::method(..)` form. (Method-style `recv.method(..)`
dispatch through a bound DOES work — see the imported default-method tests.) Tractability:
MODERATE (static-method resolution on a bound type-param + return-type-driven Self
inference). `traits/inheritance/static.rs` (and num0/num1/num5) DROPPED on this.

### G149-6 — DEFERRED (moderate): `impl Trait for fn(A, B) -> C` (implementing a trait for a function-pointer type) is a parse error near `impl`

```
impl<A, B, C> MyTrait for fn(A, B) -> C { ... }   // syntax error near 'impl'
```

Implementing a trait on a bare fn-pointer type isn't accepted by the grammar.
Tractability: MODERATE (grammar: allow a fn-ptr type in impl target position + the
corresponding coherence/dispatch). `traits/fn-type-trait-impl-15444.rs` DROPPED.

### G149-7 — DEFERRED (moderate, broad): DESTRUCTURING ASSIGNMENT (`[a, b] = …`, `Struct { a, b } = …`) is unsupported — syntax error

Both the slice form and the struct form of destructuring ASSIGNMENT (assigning into
already-declared bindings, distinct from a `let` pattern) are parse errors:

```
let mut a; let mut b;
[a, b] = [0, 1];                 // syntax error near ']'
Struct { a, b } = Struct { .. }; // syntax error near '}'
```

This affects the whole `destructuring-assignment/` upstream dir (`slice_destructure.rs`,
`struct_destructure.rs`, `nested_destructure.rs`, `tuple_struct_destructure.rs` all need
it). Tractability: MODERATE (grammar + lowering: treat a pattern on the LHS of `=` as a
multi-place assignment). All four DROPPED.

## Re-confirmed known-open (NOT re-reported as new)

- **1-tuple `(x,)` pattern in `let`** — `let (y,) = x` errors "let supports struct patterns
  only" and `(x,)` 1-tuple itself is the B106 known gap. `tuple/one-tuple.rs` DROPPED.
- **`let x @ Pattern = …` (at-binding in a `let`)** — "field read: receiver is not a struct
  (got <error>)". `pattern/bindings-after-at/bind-by-copy-or-pat.rs` DROPPED (also needs
  or-patterns + tuple-struct ctors).
- **string-literal match ARMS** (`"foo" => …`) — still a syntax error (B107 known-open).
  `pattern/usefulness/issue-30240-rpass.rs` and `match/issue-11940.rs` DROPPED.
- **`const X: &str = "..."`** — const-of-str typing mismatch ("expected &&[u8], got &[u8]").
  `match/issue-11940.rs` also needs this.
- **`mod name { … }` inline modules** — "syntax error near 'mod'" (Logos uses `package`).
  `structs-enums/enum-export-inheritance.rs`, `generics/generic-fn-twice.rs` DROPPED.
- **`size_of` / `align_of` size assertions, `#[repr(...)]` layout tests** — out of scope
  (enum-discrim-autosizing, multiple-reprs, enum-discrim-manual-sizing, generics/issue-32498).
- **closure body type-param inference from an `FnOnce(T)->T` bound** — `apply(x, |y|
  region_identity(y))` errors "expected &u64, got T" (closure arg `y`'s type-var not bound
  to the call-site `T`). `regions/regions-params.rs` DROPPED.

## Mechanical port rules applied (per batch conventions, not gaps)

- `package <name>;` header; `pub fn main()` → `fn main() -> i64 { …; return 0; }`;
  `assert!`/`assert_eq!`/`panic!`/`println!` → distinct nonzero returns / dropped.
- `isize`/`usize` → `i64`/`u64`; integer/char literals suffixed.
- `&self`/`&mut self` → `self: &<Type>` / `self: &mut <Type>`; `<Self as Tr>::T` → `Self::T`.
- `#[derive(...)]` / `#[repr(...)]` dropped where incidental.

## Final imported set (14)

- regions/**regions-lifetime-param** (issue-5243): lifetime params on struct + fn, borrowed-field read.
- pattern/**destructure-struct-variant** (issue-11577): `let Foo::VBar { num } = …` struct-enum-variant + plain-struct let-destructure.
- generics/**generic-tup** (generic-tup): `get_third<T>((T,T,T))->T` with `let (_,_,x)`, two instantiations.
- associated-types/**factory-assoc-type** (issue-18655): `type Product`, method returning `Self::Product`, recursive tuple impl `(A::Product, B::Product)`.
- traits/**default-method-bound-subst** (default-method/bound-subst): default method `g<U>` w/ own type-param dispatched through `f<T,U,V:A<T>>`.
- traits/**default-method-trivial** (default-method/trivial): transitive default-method call chain on a primitive.
- traits/**default-method-supervtable** (default-method/supervtable): default method calling a supertrait-bounded free fn.
- functions-closures/**return-from-closure** (return-from-closure): `return` scopes to the closure, not main.
- mir/**assoc-type-in-signature** (mir_call_with_associated_type): fully-qualified `<u8 as Trait>::Type` projection in param + return position.
- match/**match-char-range-guard** (issue-26251): overlapping inclusive char-range patterns + false guard + arm ordering.
- enum/**enum-discrim-large** (enum-discrim-range-overflow): large explicit i64 discriminant + variant match.
- traits/**multi-rhs-eq** (issue-26339): two impls of one generic trait with different type-args on one Self, arg-type-driven dispatch.
- traits/**inherent-over-blanket** (multidispatch-conditional-impl-not-considered): inherent method preferred over an inapplicable blanket trait impl.
- where-clauses/**where-clauses-method** (where-clauses-method): method-level `where T: Eq` constraint + `==` on the constrained field.
