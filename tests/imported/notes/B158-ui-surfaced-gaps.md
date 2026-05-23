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

- **G158-1** ◑ PARTIAL (2026-05-23) — TWO facets:
  - ✅ **autoderef-invoke** `f()` / `f(x)` where `f: &F` / `f: &mut F` (F a
    type-param bounded Fn/FnMut). Was "call to undefined function 'f'". Fix
    (sema_expr.cpp lower_call): the fn-bound detection peels a single
    `&`/`&mut` wrapper before the TypeVar→bound lookup; the reference points
    straight at the closure {fn_ptr, env_ptr}, so the call lowers to a
    ClosureCall on the ref value directly (NO extra deref — the param IS the
    closure ptr). Regression `pass/unboxed-closures/call-through-ref-to-fn-bound`.
  - ❌ **`&F` satisfying a `Fn` bound** — passing `&f` to `a<F2: Fn>(f2: F2)`
    (`'a': type '&F' does not implement trait 'Fn' required by parameter 'F'`).
    Needs blanket `impl<F: Fn> Fn for &F` (+ &mut) AND a closure-by-ref ABI
    bridge (a `&closure` flowing into a by-value-`F` param). Distinct harder
    gap; unboxed-closures-blanket-fn / -fn-mut still DROPPED pending it.

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

- **G158-7** ✅ CLOSED (2026-05-23, the coercion) — a `&mut C` / `&C` arg (C a
  type-param bounded by the trait, or a concrete type implementing it) now
  coerces to a `&mut dyn Trait` / `&dyn Trait` parameter. Sema:
  `ref_arg_satisfies_dyn(at, pt)` admits the unsizing in the free-call arg
  check (both the exact-`fi` and fallback-`fi` paths) without rewriting the
  arg, so mlir-gen's existing call-site `coerce_to_dyn` builds the fat pointer.
  mlir-gen: that coercion now keys the vtable on the mono-mangled concrete name
  (`Gen$G1$i64`) instead of the angle-bracket `type_str` form (`Gen<i64>`) —
  the generic-struct case was a latent SIGSEGV (same root as G158-10).
  Regression `pass/self/coerce-ref-generic-to-dyn` (concrete + generic struct).
  REMAINING (distinct gap, see `baghunt_qsized_dyn_passthrough_dispatch`): the
  `?Sized` generic passthrough — `tick_generic<C: ?Sized + Counter>(c: &mut C)`
  invoked with a `&mut dyn Counter` then calling `c.tick()`. That needs (a)
  `dyn T: T` bound satisfaction AND (b) trait-object method dispatch through a
  receiver that monomorphises to a trait object (mono vtable-slot resolution +
  `&mut dyn` receiver normalization). Kept as a clean sema rejection for now
  (not a compile-then-crash). dyn-compatibility-sized-self-* re-import waits on
  it.

- **G158-8** ✅ CLOSED (2026-05-23) — `fn new() -> Self where Self: Sized;`
  now parses. The true root was NOT the no-param shape (the original diagnosis)
  but the method NAME: `new` is a keyword (`KW_NEW`), and the `KW_FN KW_NEW`
  (and `KW_FN KW_NULL`) body-less grammar alts used a plain `SEMI` while the
  IDENT-named alts used `where_clause? SEMI`. Added `where_clause?` to the
  keyword-named body-less decls. (An IDENT-named `fn make() -> Self where
  Self: Sized;` already parsed.) Regression
  `pass/self/sized-self-new-where-clause`; the full
  dyn-compatibility-sized-self-return-Self re-import waits on G158-7 + G158-9.

- **G158-9** ✅ CLOSED (2026-05-23) — trait-path static `Trait::method()` with
  `Self` from the let-annotation now resolves. Fix in `lower_static_call`: when
  the named trait declares the static method, use `hint_call_return_type_` (the
  `let f: Foo = …` annotation) as Self and, if that concrete type implements
  the trait (`impls_` keyed on bare base `Trait::Foo` OR concrete-spec
  `Trait::Box2$G1$i64`), emit the concrete `<Type>__<method>` — bridging the
  trait path to the already-working concrete path. Takes priority over the
  type-param-bound search. Regression
  `pass/self/trait-path-static-self-from-let-anno` (plain struct + concrete-spec
  generic).

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

- **G158-12** ✅ CLOSED (2026-05-23) — `.map(untyped-closure).sum()` over a
  range now works. Root was NOT MapIter's Sum path but closure-PARAM inference:
  the `.map(|i| …)` closure param `i` stayed `<error>` (→ mlir-gen "unknown
  struct MapIter$…$|<error>| -> …"). The `map` formal is a bare type-param
  `MapFn: FnMut(Item) -> MapOut`, so `preload_formals` produced no closure
  shape to hint with. Fix (sema_expr.cpp preload_formals): (1) bind the
  method's owning-trait type-params (`Iterator<Item>`) into `recv_subst` from
  the receiver's impl (`impls_[Iterator::RangeI64].trait_type_args` →
  `Item=i64`); (2) when a formal is an Fn-family-bounded type-param, synthesize
  a `Closure` hint from the bound's `fn_params`/`fn_ret` (substituted) so the
  untyped closure infers its param types. Generalizes to chained
  `.map().map().sum()`. Re-imported iterator-sum-array-15673 (issue #15673).
  (Separate pre-existing limitation, NOT G158-12: `.filter` over a range fails
  even with an explicitly-typed closure — `'RangeI64' has no method 'filter'`.)

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
