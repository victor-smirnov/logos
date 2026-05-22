# B127 — UI-surfaced gaps

Batch B127 imported 30 run-pass tests distilled from `tests/ui/generics/`,
`tests/ui/regions/`, and `tests/ui/variance/` (pinned rustc
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`). All 30 imported files compile +
link + exit 0. The gaps below were surfaced while probing distinct features;
the affected facet was trimmed from the imported test (each test keeps only
the working subset), and each gap is recorded here for the grind.

Both new gaps are §B catch-up TODOs (must converge to Rust). No new §A blessed
divergences.

---

## G127-1 — impl-level lifetime param not substituted into a struct field's declared lifetime (method-return variance check)

**Symptom.** When an inherent `impl` binds the struct's lifetime under a
*different* name than the struct declaration, a method returning a borrowed
field at the impl's lifetime is rejected with a spurious variance error — the
field keeps its *declaration-site* lifetime name instead of being substituted
to the impl's binder.

```logos
struct E<'a> { f: &'a u8 }
impl<'b> E<'b> { fn m(self: &E<'b>) -> &'b u8 { return (*self).f; } }
// error [fn E__m]: return type mismatch: variance mismatch —
//   expected &'b u8, got &'a u8 — lifetime structure incompatible
```

Renaming the impl binder to match the struct's (`impl<'a> E<'a> { ... -> &'a u8 }`)
compiles + runs. So the field type `&'a u8` is not being rewritten through the
impl's `'a := 'b` substitution during the return-type check.

**Feature / §.** §B — when type-checking a method body, the struct's field
lifetimes must be substituted by the impl-block's lifetime-arg binding (the
struct's `'a` ↦ the impl's `'b`) before the return-type variance comparison.
A lifetime-substitution / freshening bug in impl-method type-checking, not
specific to any one struct shape.

**Where it bit.** `region-trait-object-from-struct-method-rg` (impl binder
renamed `'b`→`'a` to match the struct; feature otherwise tested).

---

## G127-2 — enum-variant ref payload returned at a longer struct lifetime not coerced to the shorter `&self` borrow (implicit `'a: 'b` outlives)

**Symptom.** A `&'b self` method on an enum `E<'a>` that returns the
`&'a`-payload of a reference-carrying variant is rejected: the arm yields
`&'a i64` but the return type is `&'b i64`, and the checker does not apply the
implicit `'a: 'b` outlives that holds because `&'b self` borrows an `E<'a>`
(so `'a` necessarily outlives `'b`).

```logos
enum Cached<'a> { Ref(&'a i64), Owned(i64) }
impl<'a> Cached<'a> {
    fn get_ref<'b>(self: &'b Cached<'a>) -> &'b i64 {
        match *self {
            Cached::Ref(r)      => { return r; }   // r: &'a i64, want &'b i64
            Cached::Owned(ref o) => { return o; }
        }
    }
}
// error: variance mismatch — expected &'b i64, got &'a i64
```

Collapsing both lifetimes to one (`fn get_ref(self: &'a Cached<'a>) -> &'a i64`)
compiles + runs.

**Feature / §.** §B — the well-formedness rule that a reference `&'b T<'a>`
implies `'a: 'b` (the referent must outlive the borrow) is not being fed into
region inference, so a longer payload region cannot be shortened to the `&self`
borrow region at a `return`. Distilled from rustc's LUB-coercion-across-arms
test (`regions-lub-ref-ref-rc.rs`).

**Where it bit.** `region-lub-match-deref-arm-rg` (single lifetime used for
self + struct param; the cross-region LUB shape noted in the test).

---

## Re-confirmed known-open (NOT re-reported)

- **`&[i64; N]` fixed-array borrow coerced to an unsized `&[T]` slice param**
  (`fn foo(x: &[i64]) ... ; foo(&p)` where `p: [i64;5]`) →
  `expected &[i64;5], got &[i64]`. Dynamic `&[T]`-as-value is already KNOWN-OPEN;
  `region-slice-param-index-rg` passes the array by value instead.
- **method-level turbofish on an associated-fn segment** (`S::<i64>::make::<bool>(..)`)
  → parser `syntax error near 'make'`. Type-level mid-path turbofish
  (`S::<i64>::make(..)`) works; method `::<U>` dropped, inferred from the arg
  (matches the B108 gap-#3 / prior-batch convention).
  `generic-method-extra-typaram-g3`.
- **deferred-init of an immutable `let z: &T;` then `z = ...` inside a match arm**
  → `assignment to immutable variable 'z'`. The let-binding-in-value-position
  form (`let z = match ... { ... => zz };`) works.
  `region-enum-bind-by-ref-match-rg`.
- **recursive enum payload by value** (`enum List<T> { Cons(T, List<T>), Nil }`)
  → `infinite-size enum 'List'; box the payload with '*const List'`. Logos
  enums are heap-ptr-to-struct but a *direct* self-payload still needs explicit
  `*const` indirection. `generic-recursive-list-enum-g3` uses `*const List<T>`.
- **top-level `static ITEM: T = ...;`** — not ported; `region-where-outlives-static-id-rg`
  takes the `&'static` source as a borrow of a fn-local instead.
