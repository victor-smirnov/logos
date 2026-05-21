# B113 — surfaced gaps (tests/ui run-pass: traits / default-method / inheritance / coercion / regions / ptr / methods)

Batch B113 mined run-pass tests from fresh `tests/ui/` source categories not
covered in B1–B112 — primarily the `traits/{default-method,inheritance,object,
bound}` subtrees plus `coercion/`, `regions/`, `ptr_ops/`, and `methods/`.
Upstream commit `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`.

**30 tests landed:**
- traits/default-method (7): trivial, self, bound, supervtable, bound-subst,
  bound-subst2, bound-subst3
- traits/inheritance (7): basic, call-bound-inherited, call-bound-inherited2,
  cross-trait-call, multiple-inheritors, num2, overloading-simple
- traits (other, 8): with-bounds-default, cycle-type-trait,
  trait-implementation-for-primitive-type-5280,
  trait-implementation-for-usize-5321, bug-7295, issue-6334,
  trait-implementation-restriction-5988, composition-trivial
- coercion (2): issue-3794, coerce-reborrow-imm-vec-arg
- regions (3): regions-self-impls, regions-infer-call-2, regions-dependent-autofn
- ptr (2): raw-pointer-type-basic, ptr-swap-basic
- methods (1): method-recursive-blanket-impl

Mechanical adaptations across the batch: `isize`/`usize`→`i64`/`u64`; all int
literals suffixed; `pub fn main()`→`fn main()->i32` with explicit `return 0`;
`assert!`/`assert_eq!`/`panic!`→return-code checks; by-value `self`→`self: Self`,
`&self`→`self: &T`; `Box<T>`/`Box<dyn T>`→stack values / `&dyn T`; unit structs
(`struct S;`)→`struct S {}`; `()` payload fields→`i64`; tuple compared
field-wise (`.0`/`.1`); 1-char generic params widened where they collide; the
user-`Add`/`PartialEq` traits mapped to Logos's two-type-param shape or the
`Eq` trait; `#[derive(Debug)]`+`{:?}` bodies replaced with field returns; UFCS
+ extern-crate halves of multi-feature tests dropped (kept the method-call /
generic-dispatch core).

Categories below: `compiler-bug` (crash / wrong runtime value / verifier fail),
`missing-feature` (language feature absent or unparsed), `missing-stdlib`
(library API absent), `intentional-divergence` (Logos design differs from Rust).

---

## compiler-bug

### 1. Method-generic trait method on a generic struct — NOT REPRODUCIBLE (already fixed; 2026-05-21)
Re-checked: the minimal trigger now compiles AND runs correctly (incl. an
observable-recursion variant). Likely closed by the in-session generic-struct
mono method-pinning work (c2659452). No action needed.

_Original:_
- Surfaced while porting `traits/monomorphized-callees-with-ty-params-3314.rs`
  (SKIPPED).
- Minimal trigger (MLIR verify abort at compile time):
  ```
  trait Serializer {}
  trait Serializable { fn serialize<S: Serializer>(self: &Self, s: S); }
  impl Serializable for i64 { fn serialize<S: Serializer>(self: &i64, s: S) {} }
  struct F<A> { a: A }
  impl<A: Serializable> Serializable for F<A> {
      fn serialize<S: Serializer>(self: &F<A>, s: S) { self.a.serialize(s); }
  }
  impl Serializer for i64 {}
  fn main() -> i32 { let foo = F { a: 1i64 }; foo.serialize(1i64); return 0; }
  ```
- Symptom: `'func.call' op 'F$G1$i64__serialize' does not reference a valid
  function` → module verification failed. A method-generic trait method
  (`serialize<S>`) implemented on a *generic struct* `F<A>` is never emitted as
  a specialized function when called on a concrete `F<i64>`. The non-generic-
  struct path (`i64::serialize<S>`) works. Adjacent to the closed
  iter-method-generic mono baghunts but distinct (trait method on generic struct).

### 2. Tuple-by-value through `&dyn` — FALSE POSITIVE (exit-code truncation; 2026-05-21)
`run(&u)` returns **303** correctly. The agent read the process EXIT CODE
(303 & 0xFF = **47**) and mistook the truncation for a wrong value. Verified
with `return run(&u) - 303` → exit 0. Not a bug.

_Original:_
- Surfaced while porting `traits/virtual-call-parameter-handling.rs` (SKIPPED).
- Minimal trigger (compiles + links; wrong runtime value):
  ```
  trait Trait { fn m(self: &Self, v1: (i32,i32,i32), v2: (i32,i32,i32)) -> i32; }
  struct Unit {}
  fn sum(v: (i32,i32,i32)) -> i32 { return v.0 + v.1 + v.2; }
  impl Trait for Unit {
      fn m(self: &Unit, v1: (i32,i32,i32), v2: (i32,i32,i32)) -> i32 {
          return sum(v1) * 100i32 + sum(v2);
      }
  }
  fn run(t: &dyn Trait) -> i32 {
      let v1 = (1i32,1i32,1i32); let v2 = (1i32,1i32,1i32); return t.m(v1, v2);
  }
  fn main() -> i32 { let u = Unit {}; return run(&u); }  // returns 47, want 303
  ```
- Symptom: passing two tuple-by-value args through a virtual (`&dyn Trait`)
  method mangles the values (got 47 vs expected 303). Likely the dyn-dispatch
  thunk's tuple-by-value ABI handling. Direct (non-dyn) calls work; tuple-of-3
  by value through a normal fn works.

### 3. String moved through nested FnMut-bounded closures → double-free
- Surfaced while porting `closures/closure-last-use-move.rs` (SKIPPED).
- Minimal trigger (compiles + links; aborts at runtime):
  ```
  fn g<Ty, F: FnMut(String) -> Ty>(s: String, f: F) -> Ty { return f(s); }
  fn apply<Ty, F: FnMut(String) -> Ty>(s: String, f: F) -> Ty {
      return g(s, |v: String| -> Ty { let r = f(v); return r; });
  }
  fn main() -> i32 {
      let result = apply(String::from("test"), |s: String| -> i64 { return s.len(); });
      if result != 4i64 { return 1; } return 0;
  }   // "free(): double free detected", abort (exit 134)
  ```
- Symptom: a heap `String` threaded through an inner closure (capturing another
  `FnMut`) and into a generic `g` is dropped twice. Move/drop tracking through a
  closure that captures an `F: FnMut` and forwards a moved `String`.

### 4. Struct `{ .. }` rest-pattern in a `match` arm mis-codegens (MLIR verify fail) — FIXED (2026-05-21)
- Surfaced while porting `pattern/struct-wildcard-pattern-14308.rs` (SKIPPED).
- Minimal trigger:
  ```
  struct A { f0: i64 }
  fn main() -> i32 { let a = A { f0: 3i64 };
      let x = match a { A { .. } => 1i64, }; if x != 1i64 { return 1; } return 0; }
  ```
- Symptom: `'arith.cmpi' op operand #0 must be signless-integer-like, but got
  '!llvm.ptr'` → verify fail. A `Struct { .. }` rest-pattern as a match arm
  emits a discriminant compare against a pointer.
  **FIXED:** the match-EXPRESSION codegen had no Struct case (only the
  match-statement path did); a struct pattern is now treated as irrefutable in
  match-expr dispatch and its field bindings extracted. Regression
  `tests/logos/pass/match_expr_struct_pattern.logos`. (Refutable struct-field
  literal tests in match-expr remain unsupported — separate feature.)

### 5. `format!` in two functions → moved `__buf` — FIXED (2026-05-21); + a separate operand-count facet
**FIXED (the reported symptom):** two functions each using `format!` made the
second report a spurious `use of moved variable '__buf'`. format!'s synthetic
`__buf` is moved by `return format!(...)`, and `moved_vars_` was never reset at
the function boundary, so the next function's `__buf` was seen moved. Fix:
`moved_vars_.clear()` in `lower_fn`. Regression `tests/logos/pass/format_buf_two_fns.logos`.
**STILL OPEN (separate, deeper):** the full TRAIT + generic-bound form
(`print<T: Stringify>(x)` calling `x.stringify()` whose body uses `format!`)
errors `'func.call' op incorrect number of operands for callee` — a distinct
generic-bound-dispatch-to-format!-method codegen bug, deferred.

_Original:_
- Surfaced while porting `traits/issue-23825.rs` (SKIPPED).
- Two impls of a `stringify(&self) -> String` method (on `u32` and `f32`) each
  using `format!("…: {}", *self)`, then a generic `print<T: Stringify>(x: T)`:
  the two-impl form errors `use of moved variable '__buf'` in the f32 impl; the
  single-impl form fails MLIR verify with `'func.call' op incorrect number of
  operands` (in the `Vec<i32>::fmt` / Formatter path). The format-engine's
  `__buf` desugar isn't re-entrant across multiple primitive Display impls
  reached via a generic bound.

---

## missing-feature

### 6. Destructuring assignment to a tuple/struct/slice pattern not parsed
- Upstream: all of `destructuring-assignment/{tuple,struct,slice,nested}_destructure.rs`.
- Trigger: `(a, b) = (0i64, 1i64);` → `syntax error near ')'`. Assignment whose
  LHS is a pattern (tuple/struct/slice) is not parsed. Whole dir skipped.

### 7. Tuple `<` / `<=` / `>` ordering comparison mis-codegens (MLIR verify fail)
- Upstream: `binop/structured-compare.rs` (`(1,2,3) < (1,2,4)`).
- Trigger: `let a = (1i64,2i64,3i64); if !(a < (1i64,2i64,4i64)) {…}` →
  `'arith.cmpi' op operand #0 must be signless-integer-like, but got '!llvm.ptr'`.
  Tuple equality (`==`/`!=`) works; lexicographic ordering does not. Skipped.

### 8. `where` clause on a *constructed* type (not a bare type-param) not parsed
- Upstream: `traits/false-ambiguity-where-clause-builtin-bound.rs`
  (`fn foo<K>(x: Option<K>) where Option<K>: Sized`).
- Trigger: `fn foo<K>(x: Option<K>) where Option<K>: Sized { … }` → `syntax error
  near 'fn'`. A where-clause whose subject is `Option<K>` (constructed type)
  rather than a type parameter is rejected. (Bare `T: Bound` where-clauses work.)
  Skipped.

### 9. Trait impl on a bare `fn(A, B) -> C` type not parsed
- Upstream: `traits/fn-type-trait-impl-15444.rs`.
- Trigger: `impl<Ax, Bx, Cx> MyTrait for fn(Ax, Bx) -> Cx { … }` → `syntax error
  near 'impl'`. An impl whose target type is a function-pointer type is not
  parsed. Skipped.

### 10. `&mut a[..]` (mutable full-slice of an array) not parsed
- Surfaced while porting `coercion/coerce-reborrow-imm-vec-arg.rs`.
- Trigger: `let sl: &mut [i64] = &mut a[..];` → `syntax error near '['`. `&a`
  (array→slice) coerces fine and `&v`/`&mut v` of a `Vec` coerce to slices, but
  the explicit `&mut a[..]` full-slice form on an array is unparsed. (Test
  landed using the `&a` array form; `sum_mut` kept but not called from main.)

### 11. `mut` closure parameter (`|mut x: T|`) not parsed (re B110/B111)
- Upstream: `closures/closure-mut-argument-6153.rs`.
- Trigger: `|mut x: Vec<i64>| -> Vec<i64> { … }` → `syntax error near '|'`.
  Re-confirms the `mut`-closure-param gap. Skipped.

### 12. `(&s).bar()` does not resolve a blanket-impl method when only `impl Foo for &S` exists
- Upstream: `traits/impl-trait-chain-14229.rs`.
- Trigger: with `impl Foo for &S {}` and blanket `impl<T: Foo> Bar for T {}`,
  the call `(&s).bar()` → `'S' has no method 'bar'`. The blanket should apply
  with `T = &S`, but method resolution on `&s` doesn't find `bar`. Skipped.
  (The dyn-object form of a blanket impl — issue-5988 — *does* work and landed.)

### 13. Unit-type `()` as value/payload/pattern (re B109/B112)
- Upstream: `pattern/unit-pattern-matching-in-function-argument-7519.rs`
  (`fn foo((): ())`), `pattern/usefulness/irrefutable-unit.rs`
  (`let ((),()) = ((),())`), `traits/bound/generic_trait.rs` (`type X = ()`).
  `()` as a first-class value/type is out of scope. Skipped.

---

## intentional-divergence / out-of-scope skips

- **Static trait method via a bound (UFCS-static), `MyNum::from_int(1)`** —
  `inheritance/{static2,num0,num1,num3,num5}.rs`: `call to undefined static
  method 'MyNum::from_int'`. Calling a static trait method through a generic
  bound is unsupported (UFCS-static, re B108). Skipped.
- **Supertrait method through a multi-type-param supertrait bound** —
  `inheritance/{subst,subst2}.rs`: `impl MyNum where MyNum: Add<Self,Self>` then
  `x.add(&y)` in a generic fn → `method 'add' arg 1: expected &RHS, got &T`.
  The bound's concrete type-args (`Add<MyInt,MyInt>`) aren't substituted when
  dispatching through the `T: MyNum` supertrait. Skipped.
- **Supertrait method dispatch on a `&dyn Sub` object** — `inheritance/cast.rs`:
  `(a as &dyn Bar).f()` → `trait 'Bar' has no method 'f'`. A supertrait method
  isn't reachable through a subtrait trait object. Skipped (the generic-bound
  form — `inheritance-call-bound-inherited` — works and landed).
- **`T: Copy` is not auto-copy** — `traits/copy-trait-implicit-copy.rs`:
  `fn f<T: Copy>(v: T) { let _a = v; let _b = v; }` → `use of moved variable
  'v'`. Logos's `Copy` bound does not make a value implicitly copyable
  (move-by-default semantics). Skipped (intentional divergence).
- **`Box<dyn Write>` / io / Box-dyn-array / `auto trait` / negative impls /
  `?Sized` unsized coercions / assoc types / `extern crate` (aux-build) /
  const-generics / `!` never type / `PhantomData`-heavy lifetime tests** —
  `traits/coercion.rs`, `dynamic-dispatch-trait-objects-5666.rs`, `object/*`,
  `auto-traits/*`, `dst/*`, `never_type/*`, `coherence/*` (most are aux-build),
  `object-lifetime/*`, `pointee-deduction.rs`, `assoc-type-in-supertrait.rs` —
  all out of scope for the current language surface. Skipped.
- **`mod {}` + UFCS-static** — `traits/static-method-overwriting.rs`,
  `inheritance/visibility.rs`; in-file modules are out of scope (re B111).
