# B141 — UI-surfaced gaps

Batch B141 imported 23 DISTINCT rustc UI run-pass tests (pinned SHA
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`) across: self (5), traits (4),
match (3), closures (3), expr (3), for-loop-while (2), enum (1), methods (1),
numbers-arithmetic (1), structs-enums (2 — incl. one dup dropped, see below).
Do NOT modify the compiler/stdlib. All 23 compile + link + exit 0.

Suffix `-b141` on every file (global ctest-name uniqueness). Mined from
less-tapped run-pass areas: tests/ui/{self, expr, closures, methods, match,
for-loop-while, traits} that earlier batches had not exhausted.

## NEW gaps surfaced

### G141-1 — a diverging (`return`/`!`) if-branch is NOT accepted as never-typed in if-expr unification (TRACTABLE)

The upstream `expr/if-bot.rs` shape `let i: isize = if false { panic!() } else { 5 };`
relies on the never (`!`) type of the diverging branch unifying with the other
arm's type. In Logos, `let i: i64 = if cond { return 1i32; } else { 5i64 };`
errors:

```
if-expression branches have incompatible types: void vs i64
let 'i': type mismatch — expected i64, got void
```

A diverging branch (here `return` from the enclosing fn) is typed `void`, not a
bottom type that unifies with the other arm. When BOTH arms yield a value the
if-expr works fine (verified). So `if-bot-b141` keeps the if-as-expression
bound-to-`let` shape with both arms valued.

Tractability: TRACTABLE — missing-case in if-expr branch-type unification. A
branch whose tail diverges (ends in `return`/an unconditional control-flow exit,
or calls a `-> !`-typed fn) should be treated as the bottom/never type and unify
with the sibling arm's type rather than `void`. The both-arms-valued path already
unifies correctly; this is the never-type recognition for the diverging arm.
NOT a deep (representation/calling-convention) gap. (Logos has no surface `!`
return type either, which is the same family.)

### G141-2 — `Self::static_method()` does not resolve for an inherent static method (TRACTABLE)

Inside an inherent `impl Foo { .. }`, calling another *static* (no-receiver)
method of the same impl through the `Self` path — `Self::empty()` — errors:

```
call to undefined static method 'Self::empty'
```

Writing the concrete type `Foo::empty()` works. So `Self` resolves fine as a
TYPE in inherent-impl method bodies (params `x: Self`, `y: &Self`, return
`-> Self` all work — exercised by `self-impl-2-b141`), but `Self::` as the
RECEIVER PATH of a static method call is not threaded to the impl's concrete
type. Affected (worked around with `Foo::empty()`): `self/self-impl-2-b141`.

**INVESTIGATED 2026-05-22 — DEEPER THAN A MISSING-CASE, deferred.** First fix
attempt (resolve `Self` in lower_static_call via `current_type_params_["Self"]`,
mirroring resolve_type) FAILED: a debug probe showed `current_type_params_
["Self"]` during the lowering of `Foo::make` is `Struct(Vec)` — a STALE binding
leaking from a previously-lowered stdlib impl. Root: the Self binding is set in
sema_decl.cpp:187 ONLY `if (!current_type_params_.count("Self"))`, i.e. it is
not properly scoped/reset per impl-method — a prior impl's Self persists, so
`Self` in a later inherent impl can resolve to the wrong type. (The reason
`Self`-as-a-TYPE *appears* to work is likely that those sites resolve via a
different path or the leaked type happens not to matter; the static-call path
exposes the leak.) Real fix = make the Self binding an RAII save/restore scoped
to each fn/impl-method lowering (clear+set on entry, restore on exit), THEN
resolve `Self::method` to the concrete type. This is a Self-binding-scope
correctness fix (touches sema_decl lower_fn + the collection-phase Self set),
not a one-line static-call addition — defer to a focused session. Same family
as G140-3 (Self inside const-generic/blanket impl body). The fprintf probe +
the lower_static_call resolve_class edit were reverted; only never-type (G141-1)
landed.

Tractability: TRACTABLE — missing-case. This is the static-call-path facet of
the `Self`-alias family (cf. B140 G140-3, which covered `Self` as a self-param
TYPE in const-generic/blanket impls, and B140's note on return-type-driven trait
static dispatch `HasNew::new()`). Here the impl IS an ordinary inherent
`impl Foo`, and `Self` resolves as a type — only the `Self::method()` call-path
desugar to `<concrete>::method()` is missing. Parallel-mapping to the working
`Foo::empty()` form. NOT a deep gap.

## Re-confirmed known-open / blessed-divergence (NOT re-reported)

- **tuple-struct numeric-field literal/pattern** `S { 0: a, 1: b, .. }`
  (`structs-enums/numeric-fields.rs`) is a parse error (`syntax error near '{'`
  on the struct-literal). Tuple-struct *declarations* + numeric-field
  construction/destructuring are not on the surface. DROPPED (new observation;
  recorded as known-open going forward — same tuple-struct-surface family as the
  existing tuple-struct notes).
- **`match` over a TUPLE of enum REFERENCES** `(e1, e2)` mis-dispatches
  (B140 G140-2). `match/issue-5530.rs` (`(&Enum::Foo{..}, &Enum::Bar{..})` tuple
  of refs) DROPPED on this. Likewise a tuple of an enum VALUE + a char in
  `match/multiple-refutable-patterns-13867.rs` was DROPPED (its all-catch-all
  asserts would pass even under mis-dispatch, so it is not a faithful test of the
  feature).
- **`Variant(..)` payload-ignore pattern** (`issue-1701` `animal::cat(..)`) — a
  named `_x` binding is the working form (B135/B137/B139 known-open);
  `issue-1701-b141` uses `Animal::Cat(_x)`.
- **`Option<String>` / `Option<T>` equality + `String`/`format!`** avoided —
  `issue-1701` returns an i64 noise code instead of `Option<String>` (B111/B135).
- **bodyless unit struct `struct Foo;`** (B107/B137 known-open) — `self-impl-2`'s
  unit `struct Foo;` → `struct Foo { tag: i32 }`; `default_method_simple`'s
  `struct A;` → `struct A { tag: i32 }`.
- **`Box<T>` receivers** (`ufcs-explicit-self`'s `self: Box<Foo>`, `move-self`'s
  boxed self, `explicit-self-generic`'s `Box<HashMap>`) → plain `self: Type` /
  stack values (B111 known-open).
- **negative range-pattern bounds**: a bare negative literal `-128i8` parses fine
  in an inclusive-range PATTERN; a *parenthesized/computed* bound
  `(0i8 - 128i8)..=..` is a parse error (range-pattern bounds must be plain
  literals) — `match-on-negative-integer-ranges-b141` uses the bare-literal form
  and bare `-128i8` test args (`0i8 - 128i8` would overflow i8).
- **const-as-array-length** (`enum-vec-initializer.rs` `[0; Flopsy::Bunny as usize]`)
  — array repeat-count from a const/enum-discriminant expression is the §A
  const-eval/metacall area; DROPPED.

## Mechanical port rules applied (per batch conventions, not gaps)

- `package <name>;` header; `pub fn main()` → `fn main() -> i32 { …; return 0i32; }`;
  `assert!`/`assert_eq!`/`panic!`/`println!` → distinct nonzero return codes.
- `isize`/`usize` → `i64`/`u64`; all integer literals suffixed; negatives as the
  bare literal where the suffix allows, else `0 - n`.
- `&self`→`self: &Self`; `&mut self`→`self: &mut <Type>`; `mut self`→`mut self: <Type>`;
  by-value `self`→`self: <Type>`; `match self`→`match *self`.
- closures given block bodies (`|| { .. }`); `#[repr]`/`#[derive]`/`Box`/`Rc`/
  `RefCell`/`PhantomData`/`String`/`format!`/`mem::size_of`/`static` facets
  dropped or distilled where incidental to the test's point.
- custom `PartialEq` (`empty-tag`) → an inherent `eq` method on the C-like enum
  comparing `as i64` discriminants.

## Source dups dropped (checked by exact basename vs RUSTC-PROVENANCE.md + ls pass/)

- `structs-enums/class-typarams` — ALREADY imported (`pass/structs-enums/class-typarams.logos`,
  `se_class_typarams`). A b141 port was written then dropped on discovery.
- Also skimmed-and-skipped as already-present basenames: enum-discr,
  struct-field-shorthand, borrow-tuple-fields, small-enum-range-edge,
  nested-pattern, let-destruct-ref, match-with-ret-arm, match-enum-struct-1,
  deref-newtype-method-call, fun-call-variants, ufcs-type-params, numeric-fields
  (the last DROPPED on the tuple-struct-numeric-field parse error, see above),
  functional-struct-upd.

## Final test set (23)

self: self-in-mut-slot-immediate-value (`mut self` Copy-receiver leaves original
unchanged), ufcs-explicit-self (`self: Type` / `self: &Type` incl. on generic
`Bar<T>`), explicit-self-generic (generic-enum method via `match *self` + `ref`
bind), move-self (by-value `self` method chaining to another by-value `self`),
self-impl-2 (`Self` as param/`&Self`/return type in an inherent impl; G141-2 note).
traits: default-method-simple (default method delegating to required method on a
struct), region-pointer-simple (`&A as &dyn Foo` cast + dispatch), safety-ok
(`unsafe trait` + `unsafe impl` + generic-bound dispatch), impl-implicit-trait
(same-named inherent `foo` on a generic enum + a concrete enum).
match: match-range-char-const (inclusive `char` range pattern `'0'..='9'`),
match-char-range-guard-26251 (false-guarded char-range arm falls through to a
later literal arm), match-on-negative-integer-ranges (negative i8 inclusive
range `-128..=-101`).
closures: simple-capture-and-call (by-value capture), no-capture-closure-call,
nested-closure-call (`(|| || 42)()()` closure-returning-closure).
expr: copy (`let mut y = x;` copies a Copy struct; `&mut A` mutation isolation),
if-bot (if-as-expression bound to `let`; G141-1 note), block-generic (generic fn
binding a `{ block }`-expr value + invoking a `fn`-ptr comparator).
for-loop-while: while-with-break (conditional `break`), while-flow-graph
(always-false `&&` condition, body never runs).
enum: enum-u8-variant (single-variant enum payload match + `_` arm).
methods: inherent-methods-same-name (same-named `bar` on `Foo<u64>` vs `Foo<i64>`).
numbers-arithmetic: shift-various-types (shift amount of every int width).
structs-enums: issue-1701 (exhaustive multi-variant match mixing payload + unit
variants), empty-tag (no-arg block-body closure calling a fn that compares a
C-like enum via `as i64`).
