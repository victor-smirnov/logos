# B158 — rustc UI run-pass import: surfaced gaps

Batch B158 imported **25 NEW DISTINCT run-pass tests** from `tests/ui/`, mined
for FEATURE COVERAGE across FRESH / lightly-mined areas:
unboxed-closures (8), coercion (4), traits (4), closures (2), self (2),
reachable (1), dst (1), inference (1), type-inference (1), variance (1).
(`dst` and `variance` are NEW import area dirs.)

Workflow matches B149–B157: faithful ports, `pub fn main()` → `fn main() -> i32
{ …; return 0i32; }`, isize/usize → i64/u64, integer/float literals suffixed
(float literals written with a decimal point — `1.0f32`), `assert!`/`assert_eq!`
→ early-return sentinels (distinct nonzero codes), println!/derive/Box/Rc/
RefCell/Vec/PhantomData/named-lifetimes/`#[repr]` dropped or reshaped where
incidental, nested fns/type decls hoisted, `&self`/`&mut self` →
`self: &Self` / `self: &mut Self`, by-value `self` → `self: Self`, `mut self` →
`mut self: Self`. Drop modeled via a distilled local
`trait Drop { fn drop(self: &mut Self); }` + a `*mut i64` counter; module-level
`static mut` side-effect counters (no module statics — G153-3) likewise modeled
via a `*mut i64` threaded into the closure/fn. All 25 compile + link + exit 0
against the as-is `build/bin/logosc` (no compiler changes). Link line uses
`-Wl,--gc-sections` (as for B149–B157).

Coverage highlights: single-word closure environments captured by `move` across
Fn/FnMut/FnOnce-bounded helpers (single-word-env); a `move` FnMut closure
mutating its own captured copy, called twice, leaving the outer var untouched
(infer-fnmut-move); a fn item satisfying all three Fn-family bounds when passed
by value (extern-fn); FnOnce inference for a closure that *moves* a Drop value
into it and is called once — destructor fires exactly once (infer-fnonce-drop); a
move once-closure capturing two free variables (move-multi-capture-18652); FnOnce
capture semantics — by-ref closure mutating the stack counter vs a `move` closure
mutating only its copy (counter-not-moved); a closure built inside a generic
`outside<A,B>` then passed to `inside<F: Fn()>` monomorphizing correctly
(monomorphization-context); identity once-closure called once
(static-call-fn-once); nested FnMut closures accumulating over a cartesian product
(old-closure-iter-nested-fnmut); last-use move of a closure tail value through two
FnMut helpers (closure-last-use-move); LUB coercion of two distinct fn items in
if/else and match arms to a common fn-pointer (coerce-many-fn-items-lub); reborrow
of a `&mut` arg across multiple calls (coerce-reborrow-mut-ptr-arg); coercions
unifying the return type of a polymorphic method call returning `Option<fn()->T>`
(coerce-unify-return); coercing a bare fn returning a ZST struct into a `.map`
closure position then driving it (coerce-bare-fn-to-closure-zst); diverging
`return` used as a fn argument with unreachable code after it (diverging-
unreachable-code); a raw trait-object pointer `*const dyn Trait` / `*mut dyn
Trait` dispatched back through `(*z).foo()` under unsafe (dst-raw-trait-object);
generic `translate<S: POrd<S>>` unifying S=f32 from the let-annotation (float-
type-inference-unification); an RHS operator-visitor pattern with an associated
`Result` type dispatched through `impl<Res, Rhs: RhsOfVec2Mul<Result=Res>>`
(rhs-operator-visitor-3743); inherent method preferred over a same-named trait
method (impl-inherent-prefer, G156-5); `Self` as param/`&Self`/`-> Self` in an
inherent impl (self-impl-self-types); a trait default method taking `mut self:
Self` mutating self through a required `&mut self` method (self-in-mut-slot-
default-method); a 3-level supertrait chain (Matrix: Dimensional: Index) with a
`-> Self` leaf static method on a generic struct (supertrait-self-returning-4107);
a trait default method calling a *free generic fn* that calls back into the
trait's required method, plus a recursive `impl<T: Speak> Speak for Option<T>`
(trait-default-calls-free-generic-7183); a user `Equal` trait on a C-like enum
matched via a tuple of two dereferenced enum values (typeclasses-eq-example); a
struct holding both a `*const i64` and an `Option<*const i64>` built from two
references (variance-option-ref-intersection).

## Gaps surfaced

- **G158-1** — `&F` / `&mut F` does not satisfy a `Fn` / `FnMut` bound, and
  calling through such a reference does not autoderef-invoke. Repro:
  `fn c<F: Fn()->i64>(f: &F)->i64 { a(f) }` → `type '&F' does not implement trait
  'Fn' required by parameter 'F'`; likewise `f(x)` where `f: &F` → `call to
  undefined function 'f'`. Workaround: pass `F` by value (lost the `&F`/`&mut F`
  receiver facet in unboxed-closures-extern-fn). DROPPED: unboxed-closures-
  blanket-fn, unboxed-closures-blanket-fn-mut (the `&F`-as-Fn form IS their
  point). TRACTABLE-ish: needs blanket `impl<F: Fn> Fn for &F` (+ &mut) plus
  call-operator autoderef.

- **G158-2** ✅ CLOSED (2026-05-23) — `&fn(T)->R` now parses. Grammar:
  `ref_pointee` gained `fn_ptr_type` (before `simple_type`, which can't start
  with `fn`). Regression `pass/typeck/fn-ptr-type-ref-and-unsafe`.

- **G158-3** — no module-level `static` / `static mut` (re-confirms G153-3).
  Repro: `static mut HIT: i64 = 0i64;` at module scope → `syntax error near
  'static'`. Workaround: thread a `*mut i64` counter through the fn/closure (used
  in monomorphization-context, counter-not-moved).

- **G158-4** ✅ CLOSED (2026-05-23, 449a5421) — array-element Drop now fires. The
  fix GENERALIZED all aggregate Drop: the three ad-hoc SDrop branches (struct/
  tuple/enum) collapsed into one recursive `gen_drop_value(value_ptr, ty)` +
  `value_needs_drop(ty)` in mlir-gen that handles Struct/Tuple/Enum/**Array** and
  arbitrary nesting; sema `has_droppable_fields`/`is_move_type` gained Copy-aware
  Array branches; `gen_tuple_lit` loads inline array element aggregates (was
  storing a pointer → SIGSEGV on tuple-of-array). Re-imported
  `zero-size-type-destructors`; regression `pass/array_and_nested_drop`
  ([R;3]/struct-[R;2]/tuple-[R;2] → exactly 3/2/2 drops). Full suite 4799/4799.

- **G158-5** ✅ CLOSED (2026-05-23) — a `&T` / `&mut T` / `*T` receiver now
  auto-derefs to a by-value-`self` method (`fn m(self: T, …)`). Fix in sema
  method resolution (sema_expr.cpp): added a self-param match case for
  `actual=&T`, `formal=T` (by value) → mark `auto_deref_recv`, then wrap the
  receiver in `builder().deref(...)` (mirrors the explicit `(*recv).m()`
  workaround / Rust autoderef). CRUCIAL ordering: a deref-only candidate is
  LOWER priority than any exact / auto-ref match (Rust tries `T`/`&T`/`&mut T`
  at the current deref level before stepping) — recorded as a `deref_fallback`
  and only used when no non-deref candidate matched, so `inherent-method-order`
  (a `&Foo` receiver picking the trait `val(self:&Foo)` over inherent
  `val(self:Foo)`) still resolves correctly. Workaround removed from
  inference-rhs-operator-visitor-3743; regression
  `pass/methods/autoderef-ref-recv-byvalue-self`. Full suite 4801/4801.

- **G158-6** — a reference-typed subject in a `where` clause `where &T: Trait`
  (incl. `for<'a> &'a T: Trait`) is a parse error. Repro:
  `fn foo<T>(x: &T)->i64 where &T: TheTrait { x.val() }` → `syntax error near
  'fn'`; plain `where T: Trait` parses fine. DROPPED: where-clause-ref-bound (the
  reference-typed bound IS its point). TRACTABLE: grammar — allow a reference type
  as a where-clause bounded type.

- **G158-7** — a generic `&mut C` (C: Trait) does not coerce to `&mut dyn Trait`
  at a call site. Repro: `fn tick1<C:Counter>(mut c:C){ tick2(&mut c) }` where
  `tick2(c: &mut dyn Counter)` → `expected &dyn Counter, got &mut C`, and `dyn
  Counter` is then rejected as not implementing `Counter` for a `?Sized` bound.
  DROPPED: dyn-compatibility-sized-self-by-value (this unsizing coercion IS its
  point).

- **G158-8** — a `where Self: Sized` clause on a *no-parameter* body-less trait
  method declaration is a parse error. Repro: `trait C { fn new() -> Self where
  Self: Sized; }` → `syntax error near 'Self'`. NOTE: the same `where Self: Sized`
  on a method decl that HAS parameters parses + works fine; the failure is
  specific to the no-param/`()` declaration form. DROPPED: dyn-compatibility-
  sized-self-return-Self. TRACTABLE: parser — accept a where-clause after a
  no-arg method-signature declaration.

- **G158-9** — a trait static method called via the *trait* path with `Self`
  resolved from the let-binding annotation is unresolved. Repro:
  `let f: Foo = HasNew::new();` (with `trait HasNew { fn new()->Self; }` impl'd
  for Foo) → `call to undefined static method 'HasNew::new'`; the concrete path
  `Foo::new()` works. Workaround: use the concrete path `Mat2::identity(..)`
  (supertrait-self-returning-4107). DROPPED: trait-static-method-by-let-anno (the
  trait-path Self-from-annotation IS its point).

- **G158-10** ✅ CLOSED (2026-05-23) — parameterized trait-object dispatch
  `&dyn Trait<A>` now works. Root was a NULL vtable slot (the `&dyn` fat pointer
  was built with field 1 / vtable left uninitialized → garbage fn-ptr → SIGSEGV
  on the indirect call). Two-part fix in mlir-gen: (1) `emit_trait_vtables` —
  a generic impl's `target_type` is the *pattern* `Foo$G1$A`, but the concrete-
  instantiation index keys under the bare base `Foo`; strip the `$G…` suffix so
  `collect_concrete_targets` finds `Foo$G1$i64` and registers `Clam::Foo$G1$i64`.
  (2) the `let d: &dyn …` coercion site (mlir_gen_stmt) keyed on
  `type_str(src)` = the angle-bracket form `Foo<i64>` (never matches); switched
  to `concrete_struct_name(src)` = the mono-mangled `Foo$G1$i64`. Re-imported
  `generic-trait-object-call-2288` (issue #2288); verified across multiple type
  args (i64/bool) + multi-method traits. Full suite green.

- **G158-11** ✅ CLOSED (2026-05-23) — `unsafe fn(...)->R` pointer type now
  parses. Grammar: `fn_ptr_type` gained `KW_UNSAFE`-prefixed alternatives
  (4 arities) producing `FN_PTR_TYPE` with `IS_UNSAFE: 1`; the qualifier is
  parsed-and-captured but structurally identical to the safe form. Regression
  `pass/typeck/fn-ptr-type-ref-and-unsafe`.

- **G158-12** — `.map(closure).sum()` over a range fails MLIR generation.
  Repro: `(0i64..4i64).map(|i| -> i64 { return i*2i64; }).sum::<i64>()` →
  `MLIR generation failed` (a stray `}) : () -> ()` printed). A bare
  `(0..4).sum()` (no map) works. DROPPED: iterator-sum-array-15673 (map-over-range
  -then-sum IS its point). NOTE distinct from G150-4 (which covered `Iterator::sum`
  on plain ranges — that still works); the breakage is `MapIter::sum` over a range
  source. TRACTABLE-ish: MapIter's `Sum` path emits an ill-typed closure call.

## Dropped tests (and why)

- `unboxed-closures-blanket-fn` / `unboxed-closures-blanket-fn-mut` — G158-1
  (`&F`/`&mut F` not accepted where `F: Fn`/`FnMut`).
- `unboxed-closures-call-fn-autoderef` — G158-2 (`&fn(T)->R` type parse error).
- `zero-size-type-destructors` — G158-4 ⚠️ (array-element Drop never fires).
- `where-clause-ref-bound` — G158-6 (`where &T: Trait` parse error).
- `dyn-compatibility-sized-self-by-value` — G158-7 (`&mut C` → `&mut dyn Trait`).
- `dyn-compatibility-sized-self-return-Self` — G158-8 (`where Self: Sized` on a
  no-arg trait-method decl parse error).
- `trait-static-method-by-let-anno` — G158-9 (trait-path static-method Self from
  let-annotation unresolved).
- `generic-trait-object-call-2288` — G158-10 ⚠️ (parameterized trait-object
  dispatch SIGSEGV).
- `typeck-fn-to-unsafe-fn-ptr` — G158-11 (`unsafe fn(...)` pointer type parse
  error).
- `iterator-sum-array-15673` — G158-12 (`.map(closure).sum()` over a range MLIR
  failure).
