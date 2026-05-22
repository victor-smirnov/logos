# B115 — surfaced gaps (associated-types run-pass UI tests)

Source: `tests/ui/associated-types/*.rs` @ rustc `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`.
18 tests imported (passing). The gaps below blocked or forced trimming of the
rest. None of these is a §A blessed divergence — every one is a §B catch-up
TODO (associated-type / projection normalization is plain Rust parity, not a
`const fn`/macro/async item).

## Gap kind 1 — qualified associated-type path `<T as Trait>::Assoc` does not parse
PRECISE: `fn boo(self: &Self) -> <Self as Foo>::A;` and a local
`let x: <i32 as Foo>::T = 22;` both produce
`syntax error near 'trait'/'impl'` (the parser bails at the *next* item, so the
real offender is the angle-bracket qualified path).
- Works instead: the sugar form `Self::A` / `T::A` (parses and normalizes fine).
- Needed feature: grammar + sema for the fully-qualified projection path
  `<Type as Trait>::Assoc` in both type position and expression-type position.
- Forced trims: `associated-types-basic` (whole test is `<i32 as Foo>::T`),
  the qualified-path local in `same-name-assoc-type-and-method`
  (`let x: <() as Foo>::bar`), the qualified free-fn return in
  `associated-types-constant-type` (`-> <isize as SignedUnsigned>::Opposite`),
  `associated-types-projection-from-known-type-in-impl`
  (`impl NonZero for <i32 as Int>::T`).

## Gap kind 2 — reference to a (non-generic) associated type `&Self::Value` does not parse
PRECISE: `fn get(self: &Self) -> &Self::Value;` →
`syntax error near 'Self'` (and `&'a Self::Value` likewise fails).
- Works instead: the GAT form `&'a Self::Item<'a>` (with explicit type-args)
  parses; and the *by-value* `Self::Value` return parses.
- Needed feature: allow a reference type whose pointee is a bare
  associated-type projection path (`&Self::Value`, `&'a T::Value`) — the
  parser only accepts a projection after `&'a` when it has a `<...>` arg list.
- Workaround used: ported `Get::get`/`grab` to return `Self::Value` *by value*
  (the upstream returns `&Self::Value`). Affected:
  `associated-types-simple`, `-in-default-method`, `-in-impl-generics`,
  `-in-fn`, `-in-inherent-method`. (The latter two were trimmed in favor of the
  inherent/default variants since the &-return is the only delta and the
  feature being tested — projection through a generic — is already covered.)

## Gap kind 3 — projection type does not normalize for method/operator use
PRECISE (method): `fn foo<G: GetToI32>(g: G) -> i32 { let r = g.get(); r.to_i32() }`
where `get(&self)->Self::R` →
`error [fn foo]: method call: receiver is not a struct (got G::R)`.
PRECISE (operator): `fn c<T: Trait>(x: T::Item) -> i64 { x + 1 }` →
`error: operator '+': left must be numeric, got T::Item` and
`return type mismatch — expected i64, got T::Item`.
PRECISE (.clone on projection): `node.value.clone()` where `node.value: K::Value`
→ `method call: receiver is not a struct (got K::Value)`.
- Root: inside a generic body, a value typed as the *projection* `T::Assoc`
  is not normalized to the concrete associated type bound by the impl, so it
  carries the un-normalized `T::Assoc` type into method/operator resolution.
- Needed feature: normalize `T::Assoc` to the impl's concrete `type Assoc = …`
  (or, where unresolvable, to the assoc's declared bound, e.g. `T::R: ToI32`)
  before method/operator dispatch.
- Forced trims: `associated-types-bound` (calls `.to_i32()` on the projected
  `g.get()`); the `_x + 1` body of
  `associated-types-project-from-type-param-via-bound-in-where` (ported with
  the param passed through unused — the *type-position* use of `T::Item` it
  was testing still compiles); the `.clone()` on `K::Value` in
  `associated-types-struct-field-named`/`-numbered` (ported with a direct
  field read + retyped to a non-Option value so no clone is needed).

## Gap kind 4 — associated-type equality bound `Foo<A=Bar>` does not normalize the projection
PRECISE: `fn foo1<I: Foo<A=i64>>(x: I) -> i64 { x.boo() }` where
`boo(&self)->Self::A` → `return type mismatch — expected i64, got I::A`.
- The `Foo<A=Bar>` syntax *parses*, but the equality binding is not used to
  normalize the method's `Self::A` return to the bound concrete type.
- Needed feature: thread the assoc-type equality binding into projection
  normalization for the bounded type-param.
- Forced trims: the `foo1`/`foo_bar` halves of `associated-types-return` and
  `associated-types-binding-in-where-clause` (the `Foo<A=...>`-bounded fns);
  the generic `foo<K: UnifyKey<Value=Option<V>>, V: Clone>` in
  `associated-types-struct-field-{named,numbered}`. The `I::A`-return generic
  half (`foo2`) is kept and passing.

## Tests skipped for non-assoc-type reasons (need heavier prelude / stdlib surface)
- `associated-types-iterator-binding` / `-issue-20220` / `-binding-in-trait` —
  need `DoubleEndedIterator<Item=…>` / `vec::IntoIter` / `.enumerate()` from the
  prelude with assoc-type bindings; not a parse gap, but a deep stdlib-iterator
  surface dependency. Deferred (not trimmed — would import once the iterator
  assoc-binding path is exercised).
- `associated-types-cc` — `aux-build` cross-crate (extern crate); out of scope.
- `associated-types-conditional-dispatch` — `Deref<Target=[A]>` + `PhantomData`
  + slice `MyEq` blanket winnowing; trait-object/Deref-heavy.
- `associated-types-nested-projections` / `-projection-in-object-type` /
  `-doubleendediterator-object` / `-eq-obj` — `&dyn`/object + nested
  `<<X as IntoIterator>::Iter as Iterator>::Item` projection chains.
- `default-associated-types` — `associated_type_defaults` (assoc type with a
  `= T` default) + `Default`/`ToString`; needs assoc-type-default support.
- `assoc-type-add-output-via-operator-syntax` — `<i32 as Add<T>>::Output`
  qualified projection (gap kind 1) + operator-trait Output.

## Classification
All four gap kinds are §B catch-up TODOs (Rust-parity associated-type / trait
projection normalization + the qualified-path / ref-to-projection grammar).
No §A (const fn / macro / async) divergence is involved.
