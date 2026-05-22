# B148 — UI-surfaced gaps

Batch B148 imported 23 DISTINCT rustc UI run-pass tests (pinned SHA
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`) mined from genuinely less-tapped run-pass
areas — chiefly the barely-mined `structs-enums/` dir (only 1 prior import) and the
operator-overload / trait-inheritance surfaces:
structs-enums (9), traits (5), coercion (2), binop (1), overloaded (1), deref (1),
generics (1), for-loop-while (1), cast — *dropped as dup*.
Do NOT modify the compiler/stdlib. All 23 compile + link + exit 0.

Suffix `-b148` on every file (global ctest-name uniqueness). De-duplicated against
`RUSTC-PROVENANCE.md`, `pass/<area>/`, and per-file `Original path:` headers
(B3/B6/B7/B9/B100+/B106/B107/B133–B147/Bnever). FOUR candidate ports were written, then
DROPPED on discovering exact prior imports (see "Source dups dropped").

## NEW gaps surfaced (distinct from B136–B147 / Bnever known-open)

### G148-1 — DEFERRED (moderate): a struct pattern with a REFUTABLE field sub-pattern (an enum-variant pattern as a struct field) is rejected — `struct pattern: refutable field sub-pattern not yet supported`

A struct pattern in a `match` arm whose FIELD sub-pattern is itself refutable — an
enum-variant pattern — is rejected:

```
struct T2 { x: T1, y: i64 }   // T1 is an enum
match input {
    T2 { x: T1::A(v), .. }   => return v,        // error: refutable field sub-pattern not yet supported
    T2 { x: T1::B(v), y: y } => return (v as i64) + y,
}
```

The struct pattern matcher only admits IRREFUTABLE field sub-patterns (a binding `f: x`,
`..` rest, wildcard `_`); a refutable nested pattern (enum-variant / literal) in a field
position is unsupported. The same enum-variant patterns work fine at the TOP level of a
tuple/enum scrutinee — only the struct-FIELD position is rejected. Tractability:
MODERATE — the struct-pattern lowering must (a) treat a struct pattern containing any
refutable field sub-pattern as itself refutable (it currently assumes struct patterns are
infallible in `let`-context terms and reuses that path in `match`), and (b) recurse the
value-test into the field sub-pattern (already done for tuple elements per G146-1). This
is the struct-field counterpart of the now-fixed nested-tuple recursive matcher.
`structs-enums/record-pat.rs` DROPPED on this (its sole point is the deeply nested
`t3::c(T2 {x: t1::a(m), ..}, _)` struct-field-with-enum-subpattern match).

### G148-2 — ✅ FIXED (2026-05-22): a multi-type-param USER trait used as a supertrait at `Trait<Self, Self>` does not substitute the bound's type-args into the method signature when dispatched through the bound — `method 'add' arg 1: expected &RHS, got &T` / `return type mismatch — expected T, got Result`

**Fix**: the TypeVar-bound method resolver (`sema_expr.cpp`, `recv_is_tv` path) walked the supertrait DAG (`search_trait`) tracking only the trait NAME, so the chosen method's substitution carried `Self` alone. Now `search_trait` threads a `SemaSubst`: at each supertrait edge it binds the supertrait's formal type-params to its reference's type-args resolved through the current subst (incl. `Self`) — descending `MyNum → MyAdd<Self,Self>` yields `{RHS: T, Result: T}`. Both the arg-check and return-type substitutions seed from this `chosen_subst`. `traits/inheritance/subst.rs` re-imported as `inheritance-subst-b149`. ORIGINAL REPORT below:

A user trait with multiple type params, used as a supertrait that binds every position to
`Self`:

```
trait MyAdd<RHS, Result> { fn add(self: &Self, rhs: &RHS) -> Result; }
trait MyNum : MyAdd<Self, Self> { }                 // RHS = Self, Result = Self
fn f<T: MyNum>(x: T, y: T) -> T { return x.add(&y); }   // RHS,Result NOT bound to T
```

`f` calls `x.add(&y)` on the abstract `T: MyNum`. Logos does not propagate the supertrait
predicate `MyAdd<Self, Self>` into the method signature seen on `T`, so the method's
formal `&RHS` / `Result` stay as the trait's own unbound type-params instead of being
substituted to `&T` / `T`. Result: `expected &RHS, got &T` and `expected T, got Result`.
This is distinct from G148's siblings: the STD operator-trait supertrait form
(`MyNum: Add<Output=Self> + ...`) WORKS (kept `inheritance-overloading-b148`) — the gap is
specifically a USER multi-param trait whose supertrait reference binds those params to
`Self`. Tractability: MODERATE — when resolving a method through a trait-bound `T: Sub`
where `Sub: UserTrait<Self, Self>`, the supertrait's type-args must be substituted into
the inherited method signature (Self → the concrete dispatch type). Related to G147-2 area
(trait-type-arg-aware resolution) but here the failure is the omitted SUBSTITUTION of the
supertrait's args, not a mangling collision. `traits/inheritance/subst.rs` DROPPED on this.

## Re-confirmed known-open / blessed-divergence (NOT re-reported as new)

- **`..` rest in a tuple-VARIANT pattern** (`Colour::Red(..)`, `animal::cat(..)`) is a
  parse/lowering gap — `Red(_, _)` / `Cat(_)` used instead (B147 known-open: `..` in a
  tuple-STRUCT/tuple-VARIANT pattern). `tag-b148` and `issue-1701-b148` use the explicit
  `_, _` form.
- **a type ALIAS used as a struct-literal / struct-pattern name** — `type S2 = S; let s =
  S2{..}` LITERAL works, but `match s { S2{..} => }` PATTERN errors `unknown struct 'S2'`
  / `'S2' != scrutinee 'S'`, and a generic alias `type S4<U> = S3<U,char>` fails as BOTH
  literal and pattern (`unknown struct 'S4'`). `structs-enums/struct-aliases.rs` DROPPED
  (its whole point is using the alias name as a constructor/pattern; also used `mem::
  size_of_val`). (Type-alias-as-constructor — narrow facet, not re-reported as a brand-new
  root; record here.)
- **deferred-init `let v;` assigned once inside a `loop { v = 3; break; }` then read** is
  rejected `assignment to immutable variable 'v'` — same liveness-through-divergence facet
  as B147's assign-imm-local-after-loop. `for-loop-while/liveness-loop-break.rs` DROPPED.
- **turbofish on a TUPLE-STRUCT constructor** (`Foo::<u64>(5)`) errors `call to undefined
  function 'Foo'` — the type-annotated form `let f: Foo<u64> = Foo(5)` works. The
  `inherent-methods-same-name.rs` candidate (which needs this) was a DUP anyway (B7), but
  the facet is noted. (Turbofish-in-construction for tuple structs; the named-field generic
  struct turbofish form is separately fine.)
- **tuple-struct NUMERIC field-init** `S { 0: a, 1: b }` (named-field syntax over a
  positional tuple struct) is a parse error (`syntax error near '{'`) — positional
  `S(a, b)` is the supported form. `structs-enums/numeric-fields.rs` DROPPED.
- **`String` / `Vec` / `Box` / `format!` / `println!` / `mem::size_of[_val]` / `PhantomData`
  / `.clone()`** → i64/raw-pointer/fixed-array rewrites or facet-drop (B111/B135 + batch
  convention). Notably `inheritance-overloading`'s 3-tuple `(x+y,x-y,x*y)` needed Clone
  (each operand thrice) → split into single-op generic fns; `generic-derived-type`'s
  `T: Clone` → `T: Copy` (§B1 auto-copy).
- **unit struct FIELD** `bogus: ()` rejected → `bogus: i64` (B146 unit-field area;
  `multiple-inheritors`).

## Mechanical port rules applied (per batch conventions, not gaps)

- `package <name>;` header; `pub fn main()` → `fn main() -> i32 { …; return 0i32; }`;
  `assert!`/`assert_eq!`/`panic!`/`println!` → distinct nonzero returns.
- `isize`/`usize` → `i64`/`u64`; all integer/char literals suffixed; negatives via
  `0 - N`.
- `&self`/`&mut self` → `self: &<Type>` / `self: &mut <Type>`; `match self` → `match *self`.
- `#[derive(...)]` / `#[repr(...)]` dropped; hand-written `PartialEq` impls kept verbatim
  (with `self: &T`).

## Source dups dropped (checked by exact `Original path:` vs RUSTC-PROVENANCE.md + ls pass/)

- `cast/cast.rs` — ALREADY imported as `pass/cast/ui-cast.logos` (B106). `cast-char-int-b148`
  written + verified, then DROPPED.
- `generics/generic-fn.rs` — ALREADY imported (`pass/generics/generic-fn.logos`, B3).
  `generic-fn-b148` written + verified, then DROPPED.
- `methods/inherent-methods-same-name.rs` — ALREADY imported
  (`pass/methods/inherent-methods-same-name.logos`, B7). `-b148` written + verified, DROPPED.
- `traits/impl-implicit-trait.rs` — ALREADY imported (`pass/traits/impl-implicit-trait.logos`,
  B9). `impl-implicit-trait-b148` written + verified, then DROPPED.
- `structs-enums/record-pat.rs` — DROPPED on G148-1 (refutable struct-field sub-pattern).
- `traits/inheritance/subst.rs` — DROPPED on G148-2 (multi-param supertrait substitution).
- `structs-enums/struct-aliases.rs`, `numeric-fields.rs` — DROPPED on the type-alias-as-
  constructor / numeric-field-init facets above.
- `for-loop-while/liveness-loop-break.rs` — DROPPED on the deferred-init-in-loop facet.

## Final test set (23)

structs-enums (9): rec (plain struct construct/read/copy/pass-by-value); rec-tup
(tuple-of-structs type alias + destructuring let + tuple-index on a CALL result);
struct-field-shorthand (`Foo{x, y:y, z}` + trailing-comma + out-of-order shorthand);
expr-if-struct (`if` expr yielding a struct / a C-like enum compared via hand PartialEq);
expr-match-struct (same, `match` expr); rec-extend (functional-update `Point{x:.., ..origin}`);
borrow-tuple-fields (disjoint shared/exclusive borrows of tuple + tuple-struct fields);
tag (hand PartialEq for an enum, nested `match *self`/`match *other`); enum-discr (explicit
discriminants incl. negatives, `Variant as int`); issue-1701 (multi-shape enum match →
`Option<i64>` arms, #1701).
traits (5): inheritance-overloading (supertrait `MyNum: Add+Sub+Mul+PartialEq`, generic
`f<T:MyNum>` dispatching `+`/`-`/`*` through the bound); inheritance-call-bound-inherited
(call a supertrait method via a subtrait bound `gg<T:Bar>(&T)→a.f()`);
inheritance-multiple-inheritors (diamond `B:A`,`C:A`, `f<T:B+C>` calls a/b/c);
inheritance-cross-trait-call (impl `Bar::g` calls `self.f()` from supertrait `Foo`);
coercion-generic (coerce `&S`→`&dyn Trait<i64>` + dispatch the parameterized method).
coercion (2): coerce-reborrow-imm-ptr-arg (reborrow `&mut i64`→`&i64` at a call);
coerce-reborrow-imm-ptr-rcvr (call a `&self` method through a `&S` arg).
binop (1): augmented-assignment (user compound-assign overloads `+=`/`-=`/`*=`/`/=`/`%=`/
`&=`/`|=`/`^=` via the *Assign traits).
overloaded (1): overloaded-index (user `impl Index`/`impl IndexMut`: read `f[1]`, assign
`f[0]=3`, `&f[1]`/`&mut f[1]` place borrows).
deref (1): deref-newtype-method-call (user `impl Deref` with `type Target` → autoderef
reaches a method on the target type, #22992).
generics (1): generic-derived-type (generic fn builds a generic struct + forwards through a
NESTED turbofish `g::<Pair<T>>(x)`).
for-loop-while (1): break (`break`/`continue` across while/loop + for over a fixed-array
slice `&[i64;6]`).
