# B108 — surfaced gaps (tests/ui run-pass: methods/coercion/overloaded/expr/cast/deref)

Batch B108 mined run-pass tests from `tests/ui/{methods,coercion,overloaded,expr,cast,deref}`.
Upstream commit `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`.

16 tests landed (cast 3, coercion 3, methods 3, overloaded 7). The remaining
run-pass pool in these categories is heavily skewed toward features Logos does
not yet support; every skip below is recorded with its minimal trigger.

Categories: `compiler-bug` (crash / wrong runtime value / verifier fail),
`missing-feature` (language feature absent or unparsed), `missing-stdlib`
(library API absent), `intentional-divergence` (Logos design differs from Rust).

---

## compiler-bug

### 1. Place-borrow `&mut x[i]` through user `IndexMut` does not write back
- Upstream: `overloaded/overloaded-index-autoderef.rs` (and the `&mut f[1]` block of `overloaded/overloaded-index.rs`).
- Minimal trigger:
  ```
  struct Foo { x: i64, y: i64 }
  impl Index<i64, i64> for Foo { fn index(&self, z: i64) -> &i64 { ... } }
  impl IndexMut<i64, i64> for Foo { fn index_mut(&mut self, z: i64) -> &mut i64 { ... } }
  let mut f = Foo { x: 1, y: 2 };
  let p: &mut i64 = &mut f[1];   // compiles
  *p = 4;                        // does NOT update f.y
  // f[1] still reads 2  →  observable wrong value
  ```
- Symptom: compiles + links, but the write through the borrowed `&mut f[i]`
  place is lost (the `index_mut` result is treated as a temporary, not the
  underlying place). Direct `f[i] = v` (no intermediate borrow) works.
- The landed `overloaded/overloaded-index.logos` therefore drops the
  `&mut f[1]` / `&f[1]` place-borrow rows; only direct index read/write are
  exercised. `overloaded-index-autoderef.rs` (whose point IS the place borrow,
  plus Box autoderef) is skipped entirely.

---

## missing-feature

### 2. Method-call autoderef through a user `Deref`
- Upstream: `deref/deref-newtype-method-call.rs`, `deref/dereferenceable-type-behavior-22992.rs`,
  `methods/inherent-method-resolution-on-deref-type-53843.rs`,
  `overloaded/overloaded-autoderef.rs`, `overloaded/overloaded-autoderef-order.rs` (method part),
  `overloaded/fixup-deref-mut.rs`, `coercion/coerce-overloaded-autoderef.rs`,
  `methods/method-recursive-blanket-impl.rs`, `methods/method-probe-no-guessing-dyn-trait.rs`.
- Minimal trigger:
  ```
  struct B { v: i32 }
  struct A { inner: B }
  impl B { fn foo(&self) -> i32 { self.v } }
  impl Deref<B> for A { fn deref(&self) -> &B { &self.inner } }
  let a = A { inner: B { v: 7 } };
  a.foo();   // error: 'A' has no method 'foo'
  ```
- Method resolution does NOT autoderef through a user `Deref` impl to find a
  method on the target type. (`*a` then `.foo()` works; the implicit autoderef
  does not.) Field access does autoderef-select correctly (kept in
  `overloaded-autoderef-order.logos`).

### 3. Trait-name UFCS dispatch (`Trait::method(receiver)`)
- Upstream: `methods/method-self-arg-trait.rs` (original uses `Bar::foo1(&x)`).
- Trigger: `Bar::foo1(&x)` → `call to undefined static method 'Bar::foo1'`.
  Only concrete-type UFCS (`Foo::method(...)`) resolves. Method-call syntax
  `x.foo1()` works and was used in the port.

### 4. `where`-clause that bounds a type *expression* (`where Option<T>: Foo`)
- Upstream: `methods/method-where-clause.rs`.
- Trigger: `fn check<T>(x: Option<T>) where Option<T>: Foo { ... }` → syntax
  error at the `fn`. Only bare-type-param bounds (`where T: Foo`) parse.
  Port kept the concept by impl'ing `Foo` on a concrete `Option<u32>` and
  calling directly.

### 5. Associated types on traits (`type F: Bound;` + `Self::F`)
- Upstream: `methods/method-projection.rs`, `methods/method-normalize-bounds-issue-20604.rs`,
  `methods/method-argument-inference-associated-type.rs`, `methods/call_method_unknown_referent2.rs`.
- Trigger: `trait Foo { type F: MakeString; fn get(&self) -> &Self::F; }` →
  syntax error near `Self` / unsupported associated-type declaration.

### 6. Two trait impls on different instantiations of one generic collide
- Upstream: `methods/method-where-clause.rs` (would need both `Option<i32>` and
  `Option<u32>`), `methods/method-two-trait-defer-resolution-2.rs`
  (`Vec<T>` vs `Vec<Box<i32>>`).
- Trigger: `impl Foo for Option<i32> {}` + `impl Foo for Option<u32> {}` →
  `duplicate function 'Option__foo'` / `conflicting implementations`. Trait
  method mangling does not include the impl's concrete type-args.

### 7. Method calls on raw-pointer / primitive `*const T` receiver types
- Upstream: `methods/method-two-traits-distinguished-via-where-clause.rs`.
- Trigger: `impl<T> A for *const T { ... }` + `xptr.foo()` →
  `method call through raw pointer requires unsafe context` /
  `receiver is not a struct`. Also `Self` is unknown inside a `*const T` impl.

### 8. `unsafe fn` pointer type / safe→unsafe coercion
- Upstream: `coercion/unsafe-coercion.rs` (already landed in B106 as a divergence).
  Logos has no `unsafe fn` type component; noted here for completeness.

### 9. Nested ref-pattern binding in a `for`-loop (`for &&x in ...`)
- Upstream: `deref/deref-in-for-loop.rs`.
- Trigger: `for &x in &arr { ... }` → syntax error near `for`. Ref-patterns in
  the loop binding don't parse (the test's point was the nested `&&x` regionck
  regression).

### 10. `&mut [N]`-array → `&mut [T]` slice coercion at a call boundary
- Upstream: `coercion/coerce-reborrow-mut-vec-arg.rs`, `coercion/coerce-reborrow-mut-vec-rcvr.rs`,
  `coercion/coerce-reborrow-imm-vec-arg.rs`, `coercion/coerce-reborrow-imm-vec-rcvr.rs`.
- Trigger: `fn rev(x: &mut [i64]) {...}; let mut a:[i64;4]=...; rev(&mut a);`
  → `expected &[i64], got &mut i64` (`&mut arr` coerces to `&mut i64`, not the
  mut slice). The immutable `&arr` → `&[i64]` coercion DOES work.

### 11. `if (return) {}` — diverging/return expr in `if` condition
- Upstream: `expr/if-ret.rs`. Trigger: `if (return) { }` → syntax error.

### 12. Trait-object / `Box<dyn T>` coercions and casts
- Upstream: `coercion/issue-3794.rs`, `coercion/issue-14589.rs`,
  `coercion/coerce-expect-unsized.rs`, `coercion/issue-26905-rpass.rs` (CoerceUnsized),
  `coercion/method-return-trait-object-14399.rs`, `coercion/trait-object-*.rs`,
  `coercion/any-trait-object-debug-12744.rs`, `cast/cast-rfc0401-vtable-kinds.rs`,
  `cast/codegen-object-shim.rs`, `cast/owned-struct-to-trait-cast-6318.rs`,
  `cast/generic-trait-object-call.rs`, `cast/trait-object-cast-segfault-4333.rs`,
  `cast/cast-to-box-arr.rs`, `cast/coercion-as-explicit-cast.rs` (DST/trait rows).
- Various forms of `x as &dyn T`, `Box<S> as Box<dyn T>`, array/slice unsize
  (`&[u32;3] as &[u32]`), `CoerceUnsized` / `Unsize`, fn/closure → `&dyn Fn(..)`.
  The numeric + &/&mut→rawptr subset of `coercion-as-explicit-cast.rs` was
  landed; the DST/trait-object rows are dropped.

### 13. `extern "rust-call"` Fn/FnMut/FnOnce impls + sugar call
- Upstream: `overloaded/overloaded-calls-simple.rs`, `overloaded/overloaded-calls-zero-args.rs`,
  `overloaded/overloaded-calls-object-*.rs`, `overloaded/overloaded-calls-param-vtables.rs`,
  `overloaded/issue-14958.rs`.
- `impl FnMut<(i32,)> for S { extern "rust-call" fn call_mut(...) }` + `s(3)`
  sugar. Logos closures/Fn-family don't expose the `extern "rust-call"`
  unboxed-closure surface for user structs.

### 14. supertrait item shadowing
- Upstream: `methods/supertrait-shadowing/{common-ancestor,common-ancestor-2,common-ancestor-3,
  trivially-false-subtrait,out-of-scope,assoc-const,type-dependent}.rs`.
- Trigger: blanket `impl<T> A for T {}` + `impl<T> B for T {}` where `B: A` and
  both declare `hello` → `ambiguous blanket impl: both A and B apply`. Logos
  (correctly, conservatively) rejects rather than shadowing the supertrait item.

---

## missing-stdlib

### 15. slice `.reverse()` / slice `.to_vec()`
- Upstream: `coercion/coerce-reborrow-mut-vec-*.rs`, `coercion/coerce-reborrow-imm-vec-rcvr.rs`.
- `&mut [T]` has no `reverse`; `&[T]` has no `to_vec`. (`Vec` has `reverse`.)

### 16. `Vec<T>` element access returning `&T` + `[]` indexing + `.len()`
- Upstream: `overloaded/overloaded-index-assoc-list.rs`.
- `Vec` exposes `.get(i) -> T` (by value) and `.length()`, but no
  `&`-returning element accessor nor `v[i]` operator indexing, so an
  `impl Index for AssociationList` that must `return &pair.value` can't be
  written over a `Vec`-backed map.

### 17. `Option::iter()`
- Upstream: `deref/deref-in-for-loop.rs`. `Some(0).iter()` — Option has no
  `iter()` adaptor here. (Also blocked by gap #9.)

---

## intentional-divergence

- `coercion/unsafe-coercion.rs` — see gap #8 (no `unsafe fn` type).
- `overloaded/subtyping-both-lhs-and-rhs-in-add-impl.rs` — Logos `Add` is
  fixed-shape (`fn add(self, rhs: Self) -> Self`, no distinct RHS/Output) and
  lifetime subtyping is a non-issue; landed as a same-type custom-`Add` dispatch
  test.
- `expr/eval-order.rs`, `expr/weird-exprs.rs` — rely on `matches!`, coroutines,
  `AddAssign` place-eval-order and a large pile of exotic exprs; out of scope.
