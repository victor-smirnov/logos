# B110 — surfaced gaps (tests/ui run-pass: drop/structs/generics/box/closures/borrowck/moves)

Batch B110 mined run-pass tests from `tests/ui/{drop, structs, generics, box,
closures, borrowck, moves}`, picking fresh categories that prior batches
(B1-B109) had not yet covered (drop and box are entirely new; structs/generics/
moves had a few items left). Upstream commit
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`.

21 tests landed:
- drop (3): issue-979, destructor-run-for-let-ignore-6892, conditional-drop-10734
- structs (4): struct-order-of-eval-3, struct-order-of-eval-4, struct-partial-move-1,
  struct-partial-move-2, mutable-unit-struct-borrow-11267  *(5)*
- generics (3): mid-path-type-params, issue-94923, generic-recursive-tag
- box (6): unique-generic-assign, unique-in-tag, self-assignment, basic-operations,
  unique-pat-2, unique-object-move
- closures (1): nested-closure-call
- borrowck (2): ref-mut-rebind-does-not-affect-outer, fsu-moves-and-copies
- moves (1): move-3-unique

The drop/closures/box pools are heavily gap-skewed (Box semantics, FnMut closures
with `mut` params, enum drop-glue); every skip below is recorded with its minimal
trigger.

Categories: `compiler-bug` (crash / wrong runtime value / verifier fail),
`missing-feature` (language feature absent or unparsed), `missing-stdlib`
(library API absent), `intentional-divergence` (Logos design differs from Rust).

---

## The observable-drop idiom (important context for all drop ports)

The stdlib `Drop` trait (`impl Drop for X { fn drop(&mut self) }`) does **not**
fire drop-glue — a struct/enum with such an impl is dropped without calling its
`drop`. The idiom that DOES fire drop-glue is a **locally-declared** trait with a
by-value receiver: `trait Drop { fn drop(self: Self); }` + `impl Drop for X { fn
drop(self: X) { .. } }` (this is the form used by the in-repo
`tests/logos/pass/drop_*.logos` regression tests). All B110 drop ports use this
idiom over a `*mut i64` counter (a `&Cell<i64>` counter cannot be combined with
the local `trait Drop` because `use logos.lang.cell` pulls in `logos.lang.drop`'s
`Drop`, which then collides with the local trait — B-mv-02 same-name-trait clobber).

This stdlib-Drop-vs-local-Drop split is itself a latent inconsistency (see
compiler-bug #1).

---

## compiler-bug

### 1. stdlib `Drop` (`&mut self`) does not fire drop-glue — FIXED (2026-05-21)
**Fixed.** `drop_fn_for` matched only a by-value `drop(self: Self)` param
(`types_equal(pt, t)`), missing the canonical `&mut self` shape (param is
`&mut T`). Now it also matches a `&mut self` / `&self` param by peeling one ref
level — no codegen change needed, since the SDrop call already passes the
value's address (structs pass by pointer; `&mut self` is the same pointer).
Regression: `tests/logos/pass/drop_mut_self_and_enum.logos`. **Follow-up (NOT
done):** a GENERIC struct with `&mut self` Drop (`impl<T> Drop for Box2<T> {
fn drop(&mut self) }`) — drop_fn_for resolves it (`&mut Box2<T>` base-name
match) and the SDrop is emitted + re-mangled, but mono/clone_struct_def does
not INSTANTIATE the `&mut self` drop method for the concrete spec (the by-value
generic form IS instantiated and works). Distinct clone-instantiation facet.

Original report:
- Surfaced while porting every `drop/` test.
- Minimal trigger (exits with the drop NOT having run):
  ```
  struct R { p: *mut i64 }
  impl Drop for R { fn drop(&mut self) { unsafe { *self.p = *self.p + 1i64; } } }
  fn main() -> i32 {
      let mut c: i64 = 0i64;
      { let _r = R { p: &mut c as *mut i64 }; }   // R dropped here in Rust
      unsafe { if *(&mut c as *mut i64) != 1i64 { return 1; } }  // FAILS: c == 0
      return 0;
  }
  ```
- Symptom: the `Drop` impl's `drop` is never called when the value goes out of
  scope. Holds for nested-block scope, function scope, and a single-field struct
  (raw-ptr or value field). Switching the impl to the local-`trait Drop { fn
  drop(self: Self) }` by-value form makes drop-glue fire correctly. So the
  drop-glue dispatcher keys on the by-value `fn drop(self: Self)` signature and
  misses the canonical `&mut self` stdlib-`Drop` shape. HIGH-VALUE: this is the
  Rust-idiomatic Drop form.

### 2. Enum Drop-glue does not fire (even with the local-`trait Drop` idiom) — FIXED (2026-05-21)
**Fixed.** `drop_fn_for` only set the `__drop` lookup key for `Struct` types —
for an `Enum` the type-name stayed empty, so it returned no drop fn and the
enum's `Drop` impl was never resolved. Now it keys enum drops by enum name
(`E__drop`). No SDrop codegen change needed: an enum variable's slot holds the
heap-struct pointer directly (same one-level shape as a struct value), so the
existing `call drop_fn(it->second)` is correct. Covers by-value and `&mut self`
enum Drop. Regression: `tests/logos/pass/drop_mut_self_and_enum.logos`.
**Follow-up (NOT done):** recursive drop of an enum variant's *payload* (a
`Foo` with its own Drop inside `Bar::V(Foo)`) is still not walked.

Original report:
- Surfaced while porting `drop/destructor-run-for-let-ignore-6892.rs`.
- Minimal trigger:
  ```
  trait Drop { fn drop(self: Self); }
  enum E { A(*mut i64), B(*mut i64) }
  impl Drop for E { fn drop(self: E) { match self { E::A(c)=>{unsafe{*c=*c+1i64;}},
                                                     E::B(c)=>{unsafe{*c=*c+1i64;}} } } }
  fn main() -> i32 {
      let mut c: i64 = 0i64;
      { let _x = E::A(&mut c as *mut i64); }   // E dropped here
      unsafe { return *(&mut c as *mut i64) as i32; }  // returns 0, expected 1
  }
  ```
- Symptom: an enum's `Drop` impl is never called on scope-exit, even with the
  by-value local-`trait Drop` idiom that works for structs (#1). Also, an enum
  variant's *payload* (e.g. a `Foo` inside `FooBar::VFoo(Foo)`) is not recursively
  dropped. The destructor-run-for-let port was reduced to the three struct cases.

### 3. Reassigning a generic enum from a payload variant to a no-payload variant, then matching, SIGSEGVs — FIXED (2026-05-21)
**Fixed.** `a = Opt::VNone` was lowered without the expected type, so the
literal carried a bare `Opt` (no type-args) and mlir-gen emitted a C-style i32
discriminant — storing the integer `1` into `a`'s pointer slot, so the next
match deref'd address `1` → SIGSEGV. Fix in `lower_assign`: retype an
incompletely-typed generic enum literal (no type-args, or any `<error>`
type-arg) to the LHS's concrete enum spec, provided the known type-args match
(so a genuine mismatch still errors). Also covers the multi-param sibling
(`b = Res::Err(true)` over `Res<i64,bool>` → `Res<error,bool>` → "unknown
tagged enum"). Regression: `tests/logos/pass/enum_reassign_generic_variant.logos`.
Mirrors finish_generic_call's retype_bare_enum_arg
([[baghunt-replace-ref-option-cascade]]). NOTE: an INVALID program
(`Res::Err(true)` into `Res<i64,i64>`) still produces a poor "unknown tagged
enum" message instead of a clean type error — a separate diagnostic gap, left
as-is (invalid input, no miscompile of valid code).

Original report:
- Surfaced while porting `generics/generic-tag.rs`
  (`a = option::none::<isize>;` after `a = option::some(Box::new(10))`).
- Minimal trigger (compiles + links, segfaults at runtime):
  ```
  enum Opt<T> { VSome(T), VNone }
  fn main() -> i32 {
      let mut a: Opt<i64> = Opt::VSome(10i64);
      a = Opt::VNone;
      match a { Opt::VSome(_) => { return 1; }, Opt::VNone => { return 0; } }
  }
  ```
- Symptom: `exit 139` (SIGSEGV). Narrowed: `let a = Opt::VNone` then match → OK;
  reassign `a = Opt::VNone` WITHOUT a following match → OK; reassign THEN match →
  crash. The match over the reassigned generic-enum variable reads a stale
  payload/heap slot (the old `VSome(10)` allocation) after the discriminant was
  overwritten to the no-payload `VNone`. HIGH-VALUE fix candidate.

### 4. Closure capturing a `&dyn Trait` parameter and calling a method through it
       SIGSEGVs
- Surfaced while porting `closures/closure-type-inference-in-context-9129.rs`.
- Minimal trigger (compiles + links, segfaults at runtime):
  ```
  trait Bomb { fn boom(&self) -> i64; }
  struct S {}
  impl Bomb for S { fn boom(&self) -> i64 { return 5i64; } }
  fn light_fuse(fld: &dyn Bomb) -> i64 {
      let f = || -> i64 { return fld.boom(); };   // closure captures the &dyn param
      return f();
  }
  fn main() -> i32 { let s = S {}; if light_fuse(&s) != 5i64 { return 1; } return 0; }
  ```
- Symptom: `exit 139` (SIGSEGV). Narrowed: a local `&dyn Bomb` + direct method
  call → OK; `&dyn Bomb` as a fn param + direct method call → OK; `&dyn Bomb` as a
  fn param CAPTURED by a closure + method call inside the closure → crash. The
  captured trait-object handle (fat pointer) is corrupted across the closure
  boundary. Likely the closure capture copies only the data pointer or mis-lays
  the captured fat pointer.

---

## missing-feature

### 5. Method-call turbofish (`x.method::<T>()` / `Type::method::<T>(..)`)
- Upstream: `generics/mid-path-type-params.rs` (`S::<isize>::new::<f64>(..)`).
- Trigger: `S2::new::<f64>(2i64, 1.0f64)` → `syntax error near 'new'`. The
  method-level turbofish does not parse. TYPE-level turbofish `S::<i64>::new(..)`
  works (kept in the port); the method's `<U>` is inferred from the argument.

### 6. `mut` closure parameter / `where F: FnMut(..)` generic-fn parameter
- Upstream: `structs/struct-partial-move-1.rs`/`-2.rs` (`mut f: F where F: FnMut`),
  `closures/old-closure-iter-1.rs` (`|mut x| ..`).
- Trigger: `fn f<T, F>(mut g: F) where F: FnMut(T)->T { .. }` → `syntax error near
  'fn'` (the `mut F` param token sequence breaks the parse). Also a generic struct
  combined with FRU `..p` and an `FnMut(T)->T` bound fails to resolve the type-param
  (`unknown type 'T'`). Worked around in both struct-partial-move ports by
  specializing to a concrete `Partial { x:i64, y:i64 }` + an unannotated closure
  value param `g: |i64| -> i64`.

### 7. Generic default type parameters (`struct Foo<A = i64>`)
- Upstream: `generics/generic-default-type-params.rs`,
  `generics/generic-default-type-params-cross-crate.rs`,
  `structs/struct-path-self-2.rs` (`struct S<T, U = u16>`).
- Trigger: `struct Foo<A = i64> { .. }` → `syntax error near 'struct'`. Default
  type-param bindings on a generic don't parse. The test's whole point is the
  default, so it is skipped (not reframed).

### 8. In-fn-body item declarations (`fn`/`struct`/`enum` nested inside a fn)
- Upstream: `box/unit/basic-operations.rs` (nested `struct J`, `fn f`),
  `closures/local-enums-in-closure-2074.rs` (enum inside a closure body).
- Trigger: a `struct`/`fn`/`enum` declaration inside a function (or closure) body
  → `syntax error near '{'` / near the keyword. Worked around in basic-operations
  by hoisting the nested items to top level; the local-enums-in-closure test (whose
  point IS two distinct local enums of the same name in two closures) is skipped.

### 9. `&mut [N]`-array → `&mut [T]`-slice coercion at a struct-literal / call
       boundary (re-confirm of B108 gap #10)
- Upstream: `borrowck/borrowck-freeze-frozen-mut.rs`.
- Trigger: `struct MutSlice { data: &mut [i64] }` + `MutSlice { data: &mut arr }`
  → `expected &[i64], got &mut i64` (`&mut arr` coerces to `&mut i64`, not the mut
  slice). Skipped (same root as B108 #10).

---

## intentional-divergence / out-of-scope skips

- `box/unit/{unique-send,unique-send-2,unique-swap}.rs` — need `std::sync::mpsc`
  channels / threads / `mem::swap`; out of scope.
- `box/unit/{expr-block-generic-unique1,expr-block-generic-unique2}.rs`,
  `closures/closure-mut-argument-6153.rs`, `generics/generic-static-methods.rs`,
  `generics/newtype-with-generics.rs`, `closures/old-closure-iter-{1,2}.rs` — need
  `derive(Clone)` + `FnMut`/`FnOnce` bounds over `Vec` (the `for x in &v` binding
  yields `T` not `&T` in a generic context, mismatching an `F: FnMut(&T)` bound).
- `dst/*.rs` (all 13) — `?Sized` unsized `[T]` struct fields / fat-pointer Deref /
  `dyn`-typed Index Output / box patterns. No `?Sized` field support; out of scope.
- `dyn-keyword/methods-with-mut-trait-and-polymorphic-objects-issue-8401.rs`,
  `structs/btree-struct-usage-8044.rs`,
  `generics/generic-default-type-params-cross-crate.rs` — need `//@ aux-build`
  cross-crate fixtures; the import harness builds single files only.
- `closures/{issue-22864-2,issue-87097,multilevel-path-*}.rs`,
  `closures/issue-42463.rs` — threads / `Deref`+`DerefMut` over `Vec` closures /
  empty upstream files; out of scope.
- `generics/issue-32498.rs`, `recursion/instantiable.rs` — `size_of::<T>()` /
  `ptr::null()` recursive-type instantiability; the size-assert point has no Logos
  analogue (no `size_of` on arbitrary types).
- `generics/generic-default-type-params.rs`, `structs/destructuring-struct-type-
  inference-8783.rs`, `structs/struct-path-self-2.rs` — `Default` derive / default
  type params / `Default::default()` inference; absent (see #7).
