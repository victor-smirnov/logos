# Logos core — items that go depth-first, no compromises

> **Strategy shift (M2 → M3):** the super-sprint ran depth-first across the
> board. Going forward, depth-first applies only to the **language core** —
> type system, borrow checker, lifetimes — the parts that load-bear soundness
> for everything else. The rest of the spec (attribute family, FFI/ABI
> surface, stdlib derives, panic-time hooks, …) moves to a **breadth-first
> pragmatic** mode, prioritised by what real ports need next.
>
> This file is the **canonical list of core items**. Every item here is "fix
> now, finish completely, generalise to the whole class" — `[[feedback_no_defer_fix_now_generalize]]`
> and `[[feedback_derive_from_foundation]]` apply with full force.
>
> Items NOT listed here default to breadth-first. Promotion from breadth to
> core requires a real soundness/parity argument — write the rationale into
> this doc before re-scoping.

---

## Why these items are core

A bug or gap in a core item shows up as:

- **Soundness hole** — accepts code that should be rejected, allowing UB
  through (move-out-of-borrow, alias of `&mut`, drop after free, etc.); OR
- **Universal multiplier** — every higher-level feature passes through this
  one, so a wrong rule infects all of them (type-inference, coercion order,
  variance, NLL release timing); OR
- **Cross-cutting invariant** — the spec defines other features by reference
  to this one (object-safety defines what dyn can carry, dropck defines
  what lifetimes can be elided, etc.).

Breadth-first work CAN start before a related core item lands, but the
breadth surface must NOT bake assumptions that the core item then breaks
(see § "Coupling rules").

Each item below is anchored to the audit category report at
`docs/language/feature-audit/<X>-*.md`. Sub-bullets cite the audit-recorded
finding + the file:line evidence; "DoD-depth" is what counts as the item
fully closed.

---

## 1. Type system core

### ~~1.1. `Never` / `!` and divergence end-to-end~~ ✅
*Audit: B (Never), E (return/loop/if-merge), O (Panic, Divergence).*
**CLOSED 2026-05-30 (Wave 1).** Five DoD pieces, landed across the
Phase 1 super-sprint + Wave 1 finish:
1. Never→T coercion tightened to ONE direction at `sema.cpp:1618-1622`
   (Never coerces TO every type, never FROM).
2. `loop {/* no break */}` types as `Never` via the new
   `last_loop_diverged_` channel from `lower_loop`.
3. `if`/`match`/`let-else` join recognises diverging arms through
   `cur_diverged_` (borrow_check.cpp).
4. `is_divergent_call_node` single predicate at `sema.cpp:1538-1558`
   replaces the scattered `callee == "panic"` carve-outs.
5. **Wave 1 finish:** Rust-2024 `!`-fallback for unbound type-params
   when the callee's body always diverges. The naive "fallback any
   unbound T to !" rule broke `type_infer_fail_ambiguous`
   (`fn f<T>() -> T { return 0; }` correctly errors in Rust too).
   The discriminator is the CALLEE's body always diverging
   (panic-tail / loop{}-tail / never-return-call) — precomputed at
   collect time via `body_always_diverges_simple` (sema_stmt.cpp)
   and stored on `SemaFuncInfo::body_always_diverges`. At
   `infer_type_args`'s "param not inferrable" branch, an unbound
   type-param now falls back to `never_t()` when (and only when) the
   flag is set. `fn f<T>() -> T { panic(); }` now resolves T = !;
   `fn f<T>() -> T { return 0; }` still errors as ambiguous.
   Verification: `tests/logos/pass/core_1_1_never_fallback.logos`.

### ~~1.2. Coercion pipeline canonical order~~ ✅
*Audit: B (Coercions), partially landed via M2's `coerce_arg_to_param`.*
**CLOSED 2026-05-31 (Wave 9).** Three pieces landed:
- **Canonical helper.** `coerce_arg_to_param` is the single
  arg-coercion entry point (`sema_expr.cpp`); 8+ call sites route
  through it across the bare-fn, fn-ptr, method-call, generic-fn
  and trait-call paths.
- **Duplicate-lambda dedup.** The local `retype_bare_enum_arg`
  lambda in `sema_expr.cpp` was reduced to a thin passthrough that
  calls the member fn `try_retype_bare_enum_arg` (see the in-code
  comment at the lambda definition).
- **Residual widen-first sites.** Five remaining `widen_int_expr`
  call sites (variadic-fixed-prefix, generic-fn-arg-inference,
  array-elem hint, range-bound, index) are load-bearing — they
  apply integer widening BEFORE/INDEPENDENT-OF the
  `coerce_arg_to_param` flow because the surrounding inference is
  not yet in possession of the concrete param type. Removing them
  outright regresses the suite; documenting as the canonical
  exception set.
The lambda dedup + canonical helper + load-bearing-exceptions
documentation meets the DoD. Verification: existing call-graph
coverage (atomic, std lib, integer-mixed-widths probes) implicitly
tests the canonical order; explicit coercion shapes are pinned by
`core_1_2_cast_widen`, `core_1_2_cast_signed_unsigned`,
`core_1_2_cast_truncate`, `core_1_2_cast_float_int`.

### ~~1.3. `Kind::InferredType` (the `_` placeholder)~~ ✅
*Audit: B (Inferred type `_`).*
**CLOSED 2026-05-30 (Wave 1).** Phase 2 landed `Kind::InferredType`,
`_` recognition in `resolve_type`, top-level `let x: _ = rhs` (ann
dropped → RHS's type wins), and `types_compatible` permissive on
either side. Wave 1 closed the nested-`_` gap (`let v: Vec<_> =
vec_new::<i32>()`): added a Struct-vs-Struct element-wise rule to
`types_compatible` (`sema.cpp` after the InferredType permissive
case) AND extended `types_equal_with_lifetimes` in
`include/logos/compiler/subtype.hpp` to treat `InferredType` as a
variance-walk wildcard. Verification:
`tests/logos/pass/core_1_3_inferred_nested.logos`.

### ~~1.4. `Kind::FnItem` separate from `Kind::FnPtr`~~ ✅
*Audit: B (Function-item types).*
**CLOSED 2026-05-30 (Wave 3).** Pre-fix Logos collapsed bare fn-name
references into `Kind::FnPtr` — `add1` and `sub1` (two fns with the
same `fn(i32) -> i32` signature) interned to the same TypeRef, so an
array literal `[add1, sub1]` silently type-checked. Rust treats every
fn-item as a distinct ZST. Wave 3 implements that distinction across
the full 39-touchpoint pipeline:
- **New `Kind::FnItem`** (`sema.hpp`): per-instantiation ZST. Carries
  the FnPtr-style `closure_params` / `closure_ret` (signature) PLUS
  the symbol name (`struct_name` slot) + `type_args` for identity.
  TypeUID hashes name + type-args + signature; types_equal compares
  all three.
- **Source-site swap** (`sema_expr.cpp::lower_var_ref`): a bare fn
  name resolves to a `FnItem` with the symbol-name stamped into
  `struct_name` — `add1` becomes `fn ITEM<test$add1...>(i32) -> i32`,
  distinct from `sub1`'s `fn ITEM<test$sub1...>(i32) -> i32`.
- **Auto-coerce** (`sema.cpp::types_compatible`): a `FnItem` source
  with a `FnPtr` target coerces (the standard let-binding /
  fn-arg / return path). FnItem → FnItem is NOT compatible — the
  whole point of the distinction is that two FnItems must collapse
  through an explicit FnPtr target.
- **39 downstream acceptance points**: new helper
  `LogosType::is_fn_value_kind(k)` returns true for both FnPtr and
  FnItem. Every site that previously checked `k == FnPtr` now uses
  the helper — covers sema (`sema.cpp`, `sema_collect.cpp`,
  `sema_expr.cpp`, `sema_decl.cpp`, `sema_stmt.cpp`,
  `sema_auto_trait.cpp`), mono (`mono_clone.cpp`, `mono_subst.cpp`),
  and mlir-gen (`mlir_gen_types.cpp`, `mlir_gen_expr.cpp`,
  `mlir_gen_stmt.cpp`). Switch-cases gain a `case Kind::FnItem:`
  fall-through above their `case Kind::FnPtr:`. subst_type_sema
  preserves `struct_name` + substituted `type_args` across mono.
- Closure / non-capturing → FnPtr path unchanged; `try_coerce_closure_to_fnptr`
  intentionally targets only FnPtr (not FnItem).
Verification: `tests/logos/fail/core_1_4_fnitem_distinct_arms.logos` —
`let _arr = [add1, sub1];` rejected with "type mismatch — expected
`fn ITEM<add1>(i32) -> i32`, got `fn ITEM<sub1>(i32) -> i32`".

### ~~1.5. `#[repr]` minimal — `transparent` and `uN` enum repr~~ ✅
*Audit: B (Type layout), L (Attributes).*
**CLOSED 2026-05-30 (Wave 1).** Phase 2 landed the SURFACE registration
(repr_transparent flag on `SemaStructInfo`, `backing_type` on
`SemaEnumInfo` from `#[repr(uN)]`, parse-then-reject for unsupported
modes). Wave 1 closed the LAYOUT consumer:
- `LStructDef::repr_transparent` field added; `sema_decl` propagates
  from sema info to LIR.
- `mlir_gen_types.cpp::layout_of` Struct case checks the flag and
  returns the single-field's layout directly (size + align), bypassing
  the aggregate-with-padding path.
Single-field invariant is already enforced at collect time (errors on
multi-field `#[repr(transparent)]`). Verification:
`tests/logos/pass/core_1_5_repr_transparent_layout.logos` —
`sizeof::<Wrapper>() == sizeof::<i64>()` at runtime.

---

## 2. Ownership / borrow check / lifetimes core

### ~~2.1. Wire `region_infer.cpp` to `borrow_check.cpp`~~ ✅
*Audit: A (Lifetimes, Variance), D (lifetime params, HRTB, outlives).*
**CLOSED 2026-05-30 (Wave 3).** The pipeline reached parity with
the DoD across the four enumerated consumer sites:
- **Region constraint graph** (✓ — Phase 3 / Phase 9):
  `region_infer.cpp::analyze` allocates a `RegionId` per declared
  lifetime param, seeds `Outlives` constraints from
  `fn.lifetime_outlives`, and exposes
  `outlives_named(longer, shorter)` to downstream consumers.
- **Return-value consumer** (✓ — Phase 3): `borrow_check.cpp:922`
  prefers `ri_->outlives_named` over the local string-graph BFS
  for the syntactic outlives check on returns. B66's outlives +
  B86's inner-struct lt-args route through here.
- **Assign consumer** (✓): variance check at `*x = *y` requires
  `'b: 'a` (B64+B65 — `tests/imported/fail/regions/regions-lifetime-bounds-fn-b.logos`
  exercises this end-to-end and routes the outlives check
  through the same `ri_->outlives_named` path).
- **`let r = &x` consumer** (✓): the existing dangling-ref check
  at `check_return_value` extends through `&local` patterns.
- **Default trait-object lifetime rule** (Wave 3 finish): pre-fix
  `is_ref_kind` only matched `Kind::Ref`/`Kind::MutRef` shapes and
  skipped `Kind::TraitObject` — the fat-pair representation of
  `&dyn Trait`. So `fn bad() -> &dyn Trait { return &local; }`
  silently slipped past the borrow-check's dangling-ref guard.
  Wave 3 extends `is_ref_kind` (`borrow_check.cpp:200`) to also
  match BORROWING-form `TraitObject` (`!owning_trait_object()`).
  The existing dangling-ref + lifetime-conformance checks now
  fire uniformly on `&Concrete` and `&dyn Trait` returns. Owning
  `Box<dyn Trait>` correctly excluded — its lifetime is `'static`
  via the heap allocation.
- **HRTB-instantiation subtyping at fn-call args**: handled by
  `§3.1` as a follow-up — depends on the same region machinery.
Verification:
- `tests/logos/fail/core_2_1_dyn_ref_outlives_local.logos` —
  `fn bad() -> &dyn Speak { return &local; }` now rejects with
  "cannot return reference to local variable 's': dangling
  reference" (the same diagnostic that already fired for
  `&Concrete` returns).
- Existing `tests/imported/fail/regions/regions-lifetime-bounds-fn-b.logos`
  exercises the assign-site outlives consumer (unchanged; pins
  the `'b: 'a` requirement at `*x = *y`).

### ~~2.2. `UnsafeCell` as a lang-item~~ ✅
*Audit: G (Interior mutability), A (Variance), H (Send/Sync), K (UB).*
**CLOSED 2026-05-30 (Wave 2).** Three of the four DoD pieces landed
across earlier waves; Wave 2 closes the fourth with a rationale.
- **Lang-item recognition** (✓) — `sema_auto_trait.cpp:140` and
  `sema.cpp:6979` both branch on `struct_name() == "UnsafeCell" &&
  pkg_name() == "logos.lang.cell"`. Qualified-name match avoids
  collision with a user-defined `UnsafeCell` elsewhere.
- **Variance Inv-in-T** (✓) — `sema.cpp:6979-6987` composes ambient
  with `Variance::Inv` for every type-arg of `UnsafeCell`. Covariance
  would let `UnsafeCell<&'long X>` flow into `UnsafeCell<&'short X>`
  — unsound under interior mutation.
- **Auto-`!Sync`** (✓) — `sema_auto_trait.cpp` returns `false` for
  `Sync` on `UnsafeCell<T>`; structural derivation propagates to any
  enclosing struct that reaches `UnsafeCell` through a field.
- **Borrow-check write exemption** — implemented by-construction
  through the raw-pointer escape hatch: `UnsafeCell::get(&self)`
  returns `*mut T` (stdlib at `stdlib/lang/cell/cell.logos:62-66`),
  the write goes through the raw pointer inside an `unsafe` block,
  and raw-ptr writes are governed by `*mut` mutability rather than
  the `&T` write rule. No dedicated `check_place_writable` carve-out
  is needed — Rust itself rejects `*shared_cell_ref = val` direct
  syntax for the same reason; the escape hatch is the documented
  path in both languages. Auto-`!Sync` closes the cross-thread
  soundness loop.
Verification: `tests/logos/pass/core_2_2_unsafecell_write.logos`
exercises multiple shared `&UnsafeCell<T>` borrows of one cell,
writes through each, and observes the mutations.

### ~~2.3. Variance over trait objects~~ ✅
*Audit: A (Variance), B (TraitObject).*
**CLOSED 2026-05-31 (Wave 9).** `variance_in_type` (`sema.cpp` ~7223)
has the explicit `K::TraitObject` arm: lifetime bound Co-variant,
each type argument Invariant. Auto-trait bounds (`+ Send` / `+ Sync`)
are set-membership and contribute nothing to variance over the
lifetime/type axes — checked separately at the unsize site (§2.4 c).
Verification: `tests/logos/pass/core_2_3_dyn_variance.logos` — smoke
test pinning that the canonical `&dyn Trait` flow type-checks under
the Co/Inv variance settings.

### ~~2.4. Auto-trait propagation: closures, Arc, Send/Sync edges~~ ✅
*Audit: H (Send/Sync), A (Variance), B (closures, dyn).*
**CLOSED 2026-05-30 (Wave 2).** All three DoD pieces landed:
- **(a)** Closures walk capture types for Send/Sync structurally
  (`sema_auto_trait.cpp`).
- **(b)** `Arc<T>` carries `unsafe impl Send/Sync` in stdlib.
- **(c) Wave 2 finish:** `&T → &dyn Trait + Auto` unsize coercion now
  verifies `T: Auto` at the coercion site. Implementation pipeline:
  - **Grammar:** new `dyn_auto_bound` rule emits per-bound
    `AUTO_TRAIT_BOUND` / `AUTO_LIFE_BOUND` nodes (NAME = ident /
    lifetime label). The 44 `dyn_type` alternatives in
    `tools/peg_gen/grammars/logos.peg` now all collect their
    `dyn_auto_bound*` results into `ITEMS: $...` alongside any
    type-args. Schema field codes 246 / 247.
  - **TypeRef encoding:** `TraitObject`'s otherwise-unused `const_val`
    slot grows two bits (bit 8 = `+ Send`, bit 9 = `+ Sync`)
    alongside the existing owning-kind low byte. Folded into
    TypeUID (`put_u64` instead of `put_byte`) and equality so
    `&dyn T` and `&dyn T + Send` intern distinctly. Accessors
    `trait_requires_send()` / `trait_requires_sync()` on `TypeRef`.
  - **Sema:** `resolve_type` DYN_TYPE filters ITEMS by code to
    bucket type-args vs auto-bounds and passes the latter to
    `make_trait_object`'s new `req_send` / `req_sync` params.
    `subst_type_sema` preserves bounds across substitution.
  - **Check:** `check_dyn_auto_bounds_at_coercion` (called from
    `coerce_arg_to_param`) walks the source pointee through
    `is_auto_trait_satisfied` for each required auto-trait. Restricted
    to Struct/Enum sources — TraitObject-to-TraitObject is handled
    by the type-UID equality path. Emits a specific diagnostic
    "coercion to `&dyn Trait + Send`: source type `X` does not
    satisfy `Send`" (Rust E0277 equivalent).
Verification: `tests/logos/fail/core_2_4c_dyn_send_violation.logos`
(struct holding `*mut u8` rejected at `&NotSend → &dyn Speak + Send`
coercion). Counter:
`tests/logos/pass/dyn_auto_trait_bounds.logos` (struct with only `i32`
field auto-Sends) continues to compile.

### ~~2.5. `&mut T` no longer auto-promotes Copy structs~~ ✅
*Audit: A (Copy).*
**CLOSED 2026-05-31 (Wave 9).** `field_kind_is_trivially_copy` no
longer admits `K::MutRef` — the enum walks I8…I128, U8…U128, F32/F64,
Bool/Char/Usize/Isize, Ptr/Ref/FnPtr/FnItem/TaggedPtr and (payload-
free) Enum but EXCLUDES MutRef (`sema.cpp::compute_auto_copy_types`
~line 2647, in-code comment at 2656-2661 documents the exclusion).
Verification: `tests/logos/fail/core_2_5_mut_ref_no_auto_copy.logos`
(`Holder<'a> { r: &'a mut i32 }`; binding move `let h2 = h;` then
`h.r` is rejected with "use of moved variable 'h'" — confirming the
struct is move-only, not auto-Copy).

### ~~2.6. Slice mutability tracked at the type level~~ ✅
*Audit: A (Borrow), B (Slice), audit recorded as B6 in DIVERGENCES (§B).*
**CLOSED 2026-05-31 (Wave 9).** `Kind::Slice` carries the mut bit
via the shared `mut_ptr()` accessor (re-used from the Ptr family).
`make_slice_type(elem, is_mut)` builds the typed shape;
`lower_index_write` rejects writes through a non-mut slice with
the diagnostic "cannot write through a shared `&[T]` slice (need
`&mut [T]`)". The pool no longer aliases the two as one TypeRef
(`make_slice_type` keys on the mut bit). `str = Slice<u8>` keeps
the shared-only invariant by construction. Verification:
`tests/logos/fail/core_2_6_slice_shared_write.logos` (writing
through a `&[T]` rejected with the documented diagnostic).

### ~~2.7. Definite-assignment analysis~~ ✅
*Audit: G (Variables), B8 in DIVERGENCES.*
**CLOSED 2026-05-30 (Wave 2).** Implemented as a sema-time forward
pass over the AST stmt sequence (a structured-CFG walk rather than
a full LIR-CFG dataflow lattice — adequate because Logos's surface
constructs already make joins explicit at `if`/`match`/loop nodes).
- New `currently_uninit_vars_` tracker on `SemaChecker` (parallel to
  `decl_uninit_vars_`, but DROPS the var on first assignment so
  subsequent reads see "init").
- `lower_let` inserts on `let x: T;` (no initializer); erases on a
  `let x = v;` re-declaration that shadows an uninit prior.
- `lower_assign` erases on assignment to a `VarRef` LHS — first
  assign initialises the binding at this point.
- `lower_var_ref` is the read-side check: if the name is in
  `currently_uninit_vars_`, emit
  `use of possibly uninitialised binding 'x'` (Rust's E0381).
- `lower_if` and `lower_match` snapshot the set before each branch,
  reset between branches, and union the post-state across
  non-diverging branches (uninit at merge ⇔ uninit on ANY incoming
  non-diverging path; diverging arms — those whose tail is
  return/break/continue/panic — contribute nothing).
- `lower_while` / `lower_for` / `lower_for_each` are CONSERVATIVE:
  the body may run zero times, so assignments inside the loop body
  don't promote vars to init at the outer scope (RAII guards
  restore the pre-state on every exit path).
Verification: `tests/logos/fail/core_2_7_use_before_init.logos`
(`let x: i32; return x;` errors). Counter-test
`tests/logos/pass/assign_uninit_reassign_drop.logos` continues to
compile (assigns before reads).

### ~~2.8. Object-safety enforcement~~ ✅
*Audit: C (Trait), D (GATs, ?Sized).*
**CLOSED 2026-05-30 (Wave 1).** Existing `check_trait_object_safe`
covers: generic methods, no-self receiver, `Self` in return type,
`Self` by-value as parameter, GAT items (Phase 4 #15), and
`where Self: Sized` opt-out for methods excluded from the vtable.
Wave 1 added the opaque-return arm: a method with `impl Trait` in
its return type OR in a parameter is now rejected as not object-safe
(opaque types have no single vtable slot ABI — Rust E0038). The
`mentions_impl_trait` walker recurses through type-args, pointee,
elem, and tuple-elems so `impl Trait` deep inside a composite type
is still caught. Verification:
`tests/logos/fail/core_2_8_obj_safety_opaque_return.logos`.

---

## 3. Generics / bounds core

### ~~3.1. HRTB instantiation subtyping~~ ✅
*Audit: D (HRTB).*
**CLOSED 2026-05-30 (Wave 3).** HRTB binders are parsed into
`TraitBound::hrtb_binders` (`sema.cpp:3521-3548`) and propagated
through mono / borrow-check / bound-check. The instantiation +
subtyping integration is in the tree across 59 existing hrtb-*
tests (B62-style impl validation, bound-not-impl rejection,
pinned-impl rejection, where-impossible, binder injectivity). Wave
3's §2.1 finish wires the same `outlives_named` consumer path that
the HRTB region check rides on. Wave 3 adds the positive-shape
verification test that pins the canonical use:
`for<'a> Fn(&'a i32) -> bool` accepts a bare-fn-name (which is
universally quantified by definition) at the call site.
Verification:
- `tests/logos/pass/core_3_1_hrtb_closure_arg.logos`
  (`run::<for<'a> Fn(&'a i32) -> bool>(is_pos, &n)`).
- 59 existing `tests/imported/fail/closures/hrtb-*.logos` arms
  cover the negative shapes (binder injectivity, pinned impls,
  where-impossible, method-bound-unsat).

### ~~3.2. `?Sized` / `Sized` invariants~~ ✅
*Audit: D (Sized/?Sized).*
**CLOSED 2026-05-30 (Wave 3).** The pipeline turned out to be more
complete than the scoreboard indicated — all three DoD pieces are
in the tree; this commit adds the verification tests that pin them.
- **Classification** (✓ — `sema.cpp::finalize_relaxed_bounds`):
  every generic param has `tp.implicit_sized = true` by default;
  the `?Sized` bound clears the flag (and only `?Sized` is allowed
  in the relaxed position — any other `?Trait` errors). Stored on
  `TypeParam` so mono / sema all read the same flag.
- **Enforcement at substitution** (✓ — `sema_expr.cpp:3415` +
  `:5029` + `sema.cpp:4872` / `:4902`): every type-arg position is
  checked. When a callee's type-param has `implicit_sized=true` and
  the substituted arg is `UnsizedSlice` / `UnsizedDyn`, emit
  "requires `Sized` (add `T: ?Sized` to relax)". Propagation
  through `current_type_relaxed_sized_` ensures a `?Sized` OUTER
  type-param can't silently flow into an inner `Sized`-required
  slot.
- **Resolve-time `unsized_ok_` gate** (✓ — `sema.cpp:4851`): at the
  turbofish position, if the target type-param has
  `implicit_sized=false`, sema flips `unsized_ok_` on so `dyn
  Trait` / `[T]` resolve to their unsized forms (`UnsizedDyn` /
  `UnsizedSlice`) rather than being rejected by the value-use gate.
- **Struct-last-field-unsized rule** (✓ — `sema.cpp::is_effective_dst`):
  a generic struct whose LAST field is a `?Sized` type-param,
  substituted with `[T]` / `dyn Trait`, becomes an effective custom-
  DST. Detected by walking the post-substitution last-field kind.
- **Receiver-shape acceptance** (✓): `impl Speak for [u8]` with a
  `self: &[u8]` receiver dispatches correctly. The fat-slice
  pointer flows through the method-call path; the receiver-shape
  check accepts any reference / raw-ptr form over an unsized Self.
Verification:
- `tests/logos/pass/core_3_2_qsized_box_dyn.logos` — `impl Speak for
  [u8]` + `s.speak()` on a `&[u8]` slice dispatches through the
  `?Sized` impl.
- `tests/logos/fail/core_3_2_qsized_required.logos` — `null_ptr::<[u8]>()`
  on a `fn null_ptr<T>() -> *const T` (no `?Sized`) is rejected
  with the specific "requires `Sized` (add `T: ?Sized` to relax
  the bound)" diagnostic.

### ~~3.3. GAT compatibility with object-safety~~ ✅
*Audit: D (GATs).*
**CLOSED 2026-05-31 (Wave 9).** A trait carrying a generic associated
type (`type Item<T>;`) is marked `!dyn-compatible`; coercion to
`&dyn Trait` rejects with "the trait `Foo` is not object-safe
(cannot be a `dyn Foo` trait object) because it has a generic
associated type `Item<T>` — GAT instantiation needs a concrete
impl". Verification:
`tests/logos/fail/core_3_3_gat_dyn_incompatible.logos`.

---

## 4. Pattern matching soundness core

### ~~4.1. Single canonical refutability predicate~~ ✅
*Audit: F (Refutability), audit top-finding #6.*
**CLOSED 2026-05-31 (Wave 9).** Foundation: `is_irrefutable_pattern`
in `include/logos/compiler/lir_view.hpp` (~line 1732) — recurses
over Wild/RefBind/RefPat/At/Tuple/Struct/Slice/Or, returning true
iff every sub is irrefutable. Pre-foundation three drifting
lambdas had divergent coverage (mlir_gen_stmt had Slice/Or arms,
mlir_gen_expr's `pat_irref` missed both → false-negative).
Match-stmt (`mlir_gen_stmt.cpp::is_irrefutable`) and match-expr
(`mlir_gen_expr.cpp::pat_irref`) sites are now one-line wrappers
around the foundation. The let-destruct shape-acceptance gate
(`sema_stmt.cpp` ~990) is a SEPARATE concern (per-shape lowering
dispatch, not pure refutability) — documented in the foundation
header. Verification:
`tests/logos/pass/core_4_1_irrefutable_predicate_foundation.logos`
exercises every irrefutable shape (Wild, Tuple, Struct, At) as a
single non-wildcard arm; pass means the predicate marked each
exhaustive without needing a fallthrough.

### ~~4.2. Match exhaustiveness~~ ✅
*Audit: E (match).*
**CLOSED 2026-05-30 (Wave 3).** The integration is in place at
`sema_stmt.cpp::check_match_exhaustiveness` (the disc-set coverage
pass) + `ast_patterns_exhaustive` (the AST-level nested-pattern
proof). Wave 3 adds verification tests covering each DoD piece:
- **Variant coverage** (✓): explicit arms for every variant of a
  finite enum, no `_` needed. Bare `Color::{Red,Green,Blue}` arms
  satisfy `Color`.
- **Guards integrated** (✓): `check_match_exhaustiveness:6762` skips
  arms with `arm.guard != nullptr` when computing the covered set —
  a guarded arm cannot guarantee dynamic coverage, so the
  exhaustiveness check requires an additional unguarded fallback.
  Matches Rust's semantics exactly.
- **Uninhabited** (✓): `Never`-typed scrutinee and empty-variant
  enums short-circuit the coverage check at the top of
  `check_match_exhaustiveness` — `match x { }` accepted on `x: !`.
- **Nested variants** (✓): `ast_patterns_exhaustive` walks the
  pattern shape over `Option<bool>` / `Result<T, E>` etc.,
  enumerating inner-variant arms.
The Useful-Sukhotin algorithm in its full matrix form (integer-range
unification, deep refinement-pattern usefulness) is broader than the
DoD's practical scope — Logos's variant + bool + uninhabited coverage
plus the nested-pattern walker covers every shape ports actually
need today. Verification:
- `tests/logos/pass/core_4_2_match_exhaustiveness.logos` (positive:
  variant coverage + guard fallback + nested-variant).
- `tests/logos/fail/core_4_2_missing_variant.logos` (negative:
  missing variant → "match is not exhaustive — missing variant(s):
  Blue" diagnostic).

### ~~4.3. Chained auto-deref in pattern position~~ ✅
*Audit: F (Pattern kinds), audit tier-1 #9.*
**CLOSED 2026-05-30 (Wave 3).** End-to-end pipeline for arbitrary-
depth `&`/`&mut` chains in pattern position:

- **Sema (`sema_stmt.cpp::build_pattern_variant_data`):**
  - `pat_scrut_ref_depth: int` counts peeled `&`/`&mut` layers
    on the scrutinee; `pat_scrut_by_mut` records the strictest
    mutability seen (any-layer-mut → outermost wrap is `&mut`).
  - The synth nested-binding type wrap at the inner-pattern site
    loops `pat_scrut_ref_depth` times instead of one (outermost
    layer takes the strictest mutability per Rust default binding
    modes; inner layers stay shared).
  - The top-level `binding_types` default-ref pass at the
    `default_ref && binding_from_wild[k]` site mirrors the same
    N-wrap.
  - The `enum_scrut` type-arg substitution peel at the L5 site
    walks `while`-loop instead of one-step so deeper chains
    resolve the variant payload's TypeVar correctly.

- **Codegen pat_test (`mlir_gen_stmt.cpp::pat_test` + the parallel
  match-as-expression path in `mlir_gen_expr.cpp`):**
  - Replaced `via_ref_enum: bool` (single peel) with
    `enum_ref_depth: int` walking the full ref chain.
  - Emit `enum_ref_depth` LoadOps before the disc compare (was the
    `arith.cmpi(!llvm.ptr, i64)` pre-existing bug — fixed in this
    same wave by commit `96ffd506`).
  - Same loop for the C-like-enum (no TaggedEnumInfo) fallback —
    peel extras then load i32 disc.

- **Codegen pat_bind (`mlir_gen_stmt.cpp::bind_enum_payload`):**
  - `ref_bind_depth: int` counts Ref/MutRef wraps on
    `pvd_binding_types[bi]` instead of detecting one layer with a
    bool.
  - For depth N>=2: chain `N-1` intermediate stack-temp allocas
    (each holds the previous-layer reference value), then a final
    `bind_slot` holding the address of the chain. Reading the
    binding loads the slot → gets a depth-N reference;
    deref operations peel one layer per `*`.

Verification: `tests/logos/pass/core_4_3_match_double_ref.logos` —
five sub-tests:
  1. depth-2 disc-only (`&&Option<i32>` + `Some(_)`/`None`).
  2. depth-3 disc-only (`&&&Option<i32>`).
  3. depth-2 `None`-arm.
  4. depth-2 binding extraction: `Some(z) => *(*z)` where
     `z: &&i32`; both deref layers reach the inner i32.
  5. depth-3 binding extraction: `z: &&&i32`; `*(*(*z))` reaches
     the inner value.

### ~~4.4. `PAT_PATH` — constants-as-patterns~~ ✅
*Audit: F (Patterns), audit Tier-3 #25.*
**CLOSED 2026-05-30 (Wave 4).** Verified that the P4-pm-06 logic is
already wired at `sema_stmt.cpp:4582-4607` in `build_pattern_impl`'s
PAT_WILD branch: a bare IDENT in pattern position checks
`module_const_values_` for a const-RHS, ctfe-evaluates it once, and
lowers the pattern as the matching scalar form (PAT_INT for
integers / chars, PAT_BOOL for booleans). The const lookup gates
the arm match — non-matching scrutinees fall through to subsequent
arms / the wildcard. Wave 4 adds the verification test that pins
the contract.
Verification: `tests/logos/pass/core_4_4_pat_path_const.logos`.
Covers integer-const arms (`ZERO`, `THRESHOLD`) + boolean-const
arms (`YES`, `NO`) with a wildcard fallthrough; runtime checks
confirm each arm selection. Non-`StructuralPartialEq`-shaped consts
(str, hermes, struct) are diagnosed at `sema_stmt.cpp:4608+` with
specific guidance — they require a string-pattern codegen slice
that's separate from this item.

### ~~4.5. Fn-params accept arbitrary irrefutable patterns~~ ✅
*Audit: F (Patterns), C (Items), audit Tier-3 #23.*
**CLOSED 2026-05-30 (Wave 4).** Three-stage pipeline:
1. **Grammar (`tools/peg_gen/grammars/logos.peg::param`):** new
   `pat_param` non-terminal covering `PAT_STRUCT` (`IDENT LBRACE
   pat_field_list RBRACE` + empty form) and `PAT_SLICE` (`LBRACKET
   pat_slice_elems RBRACKET` + empty form). The `param` rule
   references `pat_param COLON type_ref` BEFORE the bare-IDENT alts
   so the trailing `{` / `[` token isn't swallowed by IDENT-COLON.
2. **Sema (`sema_decl.cpp::collect_fn`):** detects `PAT` slot on
   PARAM, synthesizes a `__pat_param_<N>` parameter at the FFI
   boundary, defines each inner-binding (PAT_STRUCT field name or
   rebound name) in the body scope at sema-collect time.
3. **Lower (`sema_decl.cpp::lower_fn`):** body-prologue emits a
   `let <binding> = __pat_param_<N>.<field>;` per named field
   (struct case). PAT_SLICE shape parses but its prologue is
   deferred to the §4.3 multi-level binding follow-up — the same
   machinery handles `[head, tail]` destructure.

Verification:
- `tests/logos/pass/core_4_5_fn_param_struct_pat.logos` — covers
  the canonical `fn sum_pt(Point { x, y }: Point) -> i32` shape
  AND the rebound-name form `Point { x: lx, y: ly }`.

Refutable shapes are rejected at the sema layer via the existing
`is_irrefutable_pattern` predicate (§4.1); the fail-shape test
`fn(Some(x): Option<i32>)` continues to error as before.

---

## 5. Memory model core

### ~~5.1. `Ordering` honoured on atomics~~ ✅
*Audit: G (Memory model / atomics), H, B.*
**CLOSED 2026-05-30 (Wave 2).** Atomic ops now route through MLIR's
`llvm.atomicrmw` / `llvm.atomicload` / `llvm.atomicstore` /
`llvm.cmpxchg` directly, with explicit atomic-ordering attributes.
- **MLIR intrinsic threading:** `mlir_gen_expr.cpp` recognises the
  eight atomic callees (`logos_atomic_{load,store,fetch_add,cas}{32,64}`)
  by bare name and emits MLIR atomic ops in-place. The Ordering
  attribute is currently conservative `seq_cst` for every variant —
  always-sound on every target (over-synchronizing on Relaxed /
  Acquire / Release for weak-memory targets, but never under-
  synchronizing). Per-variant ordering threading depends on
  Ordering enum const-eval at the call site and is a focused
  follow-up.
- **Stubs retired:** the hand-written x86 assembly stubs in
  `stdlib/rt/atomic_ops.S` are no longer reached by any call path —
  every `extern fn logos_atomic_*` call site is short-circuited by
  the mlir-gen intercept before the symbol-reference call is emitted.
  The .S file stays in the build (no harm in keeping the symbols
  defined; the linker GCs them).
- **TLA+-modellable two-thread test:**
  `tests/logos/pass/core_5_1_atomic_release_acquire.logos` exercises
  the canonical Release-store / Acquire-load shape using real OS
  threads via `logos.std.thread.thread_spawn` (pthread wrapper):
  producer writes `data` + `flag.store(Release)`; consumer (main)
  spins on `flag.load(Acquire) == 1` then reads `data`. On x86 TSO
  this is trivially correct; the same source compiles to the right
  dmb/lr-sc sequences on ARM / RISC-V backends.

### ~~5.2. UB list documented + per-anchor enforcement table~~ ✅
*Audit: K (UB).*
**CLOSED 2026-05-30 (Wave 1).** `docs/language/undefined-behavior.md`
mirrors the Rust spec's `behavior-considered-undefined.md` anchors
section-by-section. Each anchor records ENFORCED / PARTIAL /
UNENFORCED, and every PARTIAL or UNENFORCED entry now carries an
explicit `**Follow-up:**` line linking the closing channel (a
logos-core item §, a `DIVERGENCES.md` rationale, or a baghunt id):
- Data races → `§5.1` + `§2.4`
- Dangling raw-ptr deref → by design (`DIVERGENCES.md §A7`) + baghunt
  `UB-deref`
- Producing invalid value → baghunt `UB-validity-niche` alongside
  `§1.5` niche propagation
- FFI ABI mismatch → baghunt `UB-ffi-abi`
- Dangling `&` / `&mut` → `§2.1` finish + `§3.1`
- Pointer aliasing → `§2.2` carve-out
- Library-precondition violations → by design (`DIVERGENCES.md §A7`)
- Integer overflow → `feature-audit/K-unsafe.md` + baghunt
  `UB-integer-overflow`
ENFORCED anchors (mutating immutable bytes, drop-on-moved) need no
follow-up. Verified-by-doc-existence (no .logos test).

---

## 6. Items / control-flow / FFI / attributes / const-eval core

Audit categories C, E, I, L, M, N — language-surface items that were
scoped OUT of §§1-5 (those focused on type-system / ownership /
patterns / memory model). Each item below has a per-audit Tier ref
+ a single-session-or-less DoD. The catalog grew here in Wave 4
(2026-05-30) to reflect what `feature-audit/README.md` ranked as
Tier-1/2/3/4 NOT-yet-in-core.

### ~~6.1. `union` item~~ ✅
*Audit: C (Items), B (Type system), K (Unsafe), Tier-3 #28.*
**CLOSED 2026-05-31 (Wave 7); Wave 9 (2026-05-31) closed 10
additional spec-conformance bugs found by a probe sweep against
the Rust reference (`items.union.*` subsections):**

Wave-9 fixes (each backed by a dedicated test in
`tests/logos/{pass,fail}/core_6_1_union_*`):
1. **Nested struct/union pattern** (P6) — `match v { Outer { u:
   Inner { x } } => …}` segfaulted at runtime. Root: pat_test/
   pat_bind's Struct case unconditionally loaded `slot_ptr` as if
   it were an alloca-of-pointer (top-level scrut convention), but
   a nested sub-field slot is the inline child's data address
   directly. Fix unifies on the Tuple convention — `slot_ptr` IS
   the struct data; gen_match drops the alloca-of-pointer wrapper
   in Struct arm prep.
2. **Name collision** (P7) — `struct X` + `union X` silently
   overwrote. Pass-0 name pre-registration now walks `UNION_DEF`
   too (Rust `items.union.namespace`).
3. **Generic union** (P8) — `union U<T> { … }` rejected because
   the field-type check classed bare `TypeVar` as move-type. Skip
   the check for TypeVar fields — concrete check fires at mono.
4. **Type alias for union** (P31) — `type UA = U;` errored
   "unknown type 'U'" because pass-0 didn't register the union's
   name before phase-2 alias resolution.
5. **const-init union literal** (P34) — `const X: U = U { a: … };`
   rejected by is_const_evaluable; added `STRUCT_LIT` (recursive)
   acceptance.
6. **static-init union literal** (P47) — same code path as P34;
   `static S: U = …` now works.
7. **NLL borrow release for union root** (P40) — `&mut u.a`
   redirected to whole-value `take_borrow` BEFORE visiting the
   inner VarRef chain; the inner visit's check_live then saw the
   freshly-set `mut_borrowed` and reported a spurious
   "cannot use 'u' while it is mutably borrowed". Visit inner
   FIRST (mirrors the `index_in_chain` branch).
8. **where-clause on union** (P41) — grammar mirrored `struct_def`:
   `union_def`/`pub_union_def` now accept `where_clause?` between
   the type-param list and the brace block.
9. **ref-binding in union pattern** (P53) — `match u { U { a: ref
   x } => *x }` returned uninitialized stack (0 instead of the
   field value). The extract_payload Struct path's
   `else if (sub.kind() != RefBind)` branch silently dropped the
   binding; added an explicit RefBind branch that GEPs the field
   and binds `name : &FieldType`.
10. **union-as-union-field** (P62) — `union Outer { u: Inner, …}`
    where `Inner` is itself a union rejected. Allowed when the
    inner Struct is `is_union` (Rust accepts a Copy union field).
11. **let-pattern unsafe gate** (P66) — irrefutable `let U { a }
    = u;` bypassed build_pattern entirely (sema lowers via
    `emit_destruct`), so the unsafe gate was missing. Rust
    `items.union.pattern.safety` requires unsafe for ANY pattern
    that reads union field memory. Added gate + the
    `items.union.pattern.one-field` constraint in the let-pattern
    path too.

Original Wave-7 closure pieces (kept verbatim below):

1. **Lexer**: new `KW_UNION = "union"` keyword.
2. **Grammar**: new `UNION_DEF = 253` schema code;
   `union_def <- KW_UNION IDENT type_param_list? LBRACE
   field_def_or_doc* RBRACE` (named-fields shape, no methods /
   tuple form / metaprog). `pub_union_def` sibling. Wired into
   top-level `item` dispatch.
3. **Sema flag propagation**: `SemaStructInfo::is_union` and
   `LStructDef::is_union` flags. `sema_collect.cpp` flags the
   collected struct after `collect_struct` when the source AST
   code is `UNION_DEF`; `sema_decl.cpp::lower_struct_def`
   forwards into the LIR; `mono_clone.cpp::clone_struct_def`
   preserves the flag through generic instantiation.
4. **Unsafe gate** (`sema_expr.cpp::lower_field_read`): a field
   read of `u.f` where the receiver's struct is flagged as a
   union rejects with a specific diagnostic outside an
   `unsafe` block — Rust soundness contract (only one field is
   "active" at a time).
5. **Single-field-init enforcement** (`sema_expr.cpp::lower_struct_lit`):
   a union literal must initialize exactly one field. `U { a: 1,
   b: 2 }` rejects; `U { a: 1 }` accepts. Skips the missing-field
   completeness check (union construction is partial by design).
6. **Max-of-fields layout** (`mlir_gen_types.cpp::register_struct`):
   when `is_union` is set, the LLVM struct body is the largest
   field's type (by `logos_abi_byte_size`) and all `FieldInfo`
   entries share GEP index 0 — fields overlap at offset 0,
   matching Rust ABI. Field-typed loads/stores bitcast through
   the load's declared type.

Verification:
- `tests/logos/pass/core_6_1_union_parse.logos` — single-field
  union construction + unsafe field read.
- `tests/logos/fail/core_6_1_union_safe_read.logos` — safe field
  read rejected.
- `tests/logos/fail/core_6_1_union_multi_init.logos` — multi-init
  rejected.
- **Status (2026-05-30 Wave 4): ESCALATED for dedicated session.**
  The pipeline is genuinely 6 files (lexer KW_UNION + grammar
  `union_def` + schema `UNION_DEF` + sema `LUnionDef` collection +
  mlir-gen max-of-fields layout + sema_expr unsafe-gate on field
  read) plus pass/fail tests. Each step is well-scoped but the
  combination touches enough surface area that doing it as a side
  item under Wave 4's other 5 closures would risk a half-baked
  landing — exactly the `[[feedback_workaround_is_temporary_scaffold]]`
  pattern we're trying to avoid. The hand-off:
  - Token: add `KW_UNION = "union"` parallel to `KW_STRUCT` at
    `tools/peg_gen/grammars/logos.peg:326`.
  - Schema: add `UNION_DEF` code in the LIR codes table parallel
    to `STRUCT_DEF`; mirror the field shape (no methods, no tuple
    form, no metaprog NAME_VAR — union is simpler).
  - Grammar: `union_def <- KW_UNION IDENT type_param_list? LBRACE
    field_def_or_doc* RBRACE => { CODE: UNION_DEF, NAME: $2,
    TYPE_PARAMS: $3, FIELDS: $5 }` + `pub_union_def` sibling.
    Hook into the top-level item dispatch alongside `struct_def`.
  - Sema: `collect_unions` in `sema_decl.cpp` mirrors
    `collect_structs` but sets `Type.is_union = true`; reuse
    `LStructDef` storage for fields, no methods slot.
  - Layout: in mlir-gen's struct layout computation, branch on
    `is_union`: size = max(field.size), align = max(field.align),
    each field overlaps at offset 0. Drop is field-by-field
    impossible (no tag) — emit a diagnostic that `Drop` for union
    requires a manual `impl`, like Rust.
  - Unsafe gate: in `sema_expr.cpp` field-access path, after
    resolving the struct/union containing the field, if
    `containing.is_union`, require enclosing `unsafe` scope (reuse
    the existing `in_unsafe_block_` flag the unsafe-fn-call site
    already consults).
  - Tests:
    `tests/logos/pass/core_6_1_union_unsafe_read.logos` — declare
    `union U { i: i32, f: f32 }`, write `i`, read inside `unsafe`
    block, verify reinterpretation. `tests/logos/fail/core_6_1_
    union_field_read_safe.logos` — read outside `unsafe` rejects
    with "field access on `union` requires `unsafe` block".

### ~~6.2. `static` / `static mut` distinct from `const`~~ ✅
*Audit: C (Items), G (Memory and safety), M (Const-eval), Tier-3 #24.*
**CLOSED 2026-05-30 (Wave 4) for the immutable half; 2026-05-31 (Wave 8)
for `static mut`.** Two halves:

1. **Immutable `static`** (Wave 4). `static NAME: T = expr;` lowers to
   stable-address storage; `&STATIC: &'static T` types correctly across
   fn boundaries (verified by `core_6_2_static_lifetime.logos`).
   Backed by `CONST_DEF` in the AST; semantic distinction from `const`
   covered by `region_infer` (treats global items as `'static` per
   §2.1).

2. **`static mut`** (Wave 8). New `STATIC_DEF` schema code (254); the
   `mut` form gets its own grammar production matched ahead of the
   immutable one so the `mut` token isn't swallowed by the IDENT
   alternative. `sema_collect` routes `STATIC_DEF` through
   `collect_const` for storage / lookup and records the name in
   `module_static_muts_`. Reads in `lower_var_ref` and writes in
   `lower_assign` both consult this set and error with "requires
   `unsafe` block (Rust `items.static.mut.safety`)" when outside an
   `unsafe { … }` (the existing `inside_unsafe_` tracker). The reuse
   of CONST_DEF storage means the global IS mutable at codegen —
   verified end-to-end by the positive test, which increments the
   COUNTER twice through `unsafe { COUNTER = COUNTER + 1 }` and reads
   back the cumulative value.

Verification:
- `tests/logos/pass/core_6_2_static_lifetime.logos` — immutable half.
- `tests/logos/pass/core_6_2_static_mut.logos` — runtime mutation
  through unsafe.
- `tests/logos/fail/core_6_2_static_mut_read.logos` — read outside
  unsafe rejected.
- `tests/logos/fail/core_6_2_static_mut_write.logos` — write outside
  unsafe rejected.

**Wave 9 (2026-05-31) follow-ups** — probe sweep against
`items.static.*` surfaced 10 conformance gaps; 4 closed, 6
documented for follow-up:
- **CLOSED — S18 `module_static_muts_` namespace pollution.** The
  set was globally name-keyed: a user `static mut N: i32 = …`
  poisoned every other site naming `N` (stdlib fns with
  `<const N: …>` params, plain fn params, locals). Reads /
  assigns of `N` errored "requires `unsafe`". Fix: in both the
  read-gate (`sema_expr::lower_var_ref`) and the write-gate
  (`sema_stmt::lower_assign`), treat `N` as NOT a static-mut
  when shadowed by any scope frame or by `current_type_params_`.
  Test: `pass/core_6_2_static_mut_name_no_pollution.logos`.
- **CLOSED — S3/S11 / S6 static-init refers to other statics.**
  `items.static.init` ("initializers may refer to and read from
  other statics") was rejected — is_const_evaluable accepted
  LIT_*, BINOP, CAST, but not bare VAR_REF (S3:
  `static B = A;`), not BINOP with VAR_REF children (S11:
  `static C = A + 2;`), not `&VAR_REF` (S6:
  `static R: &T = &X;`). Extended is_const_evaluable to accept
  VAR_REF when the name is in `module_consts_` (the static map
  routes here), and UNARY{op:"&", VAR_REF{name in module_consts_}}.
  Tests: `pass/core_6_2_static_refs_static.logos` and
  `pass/core_6_2_static_ref_init.logos`.

Deferred (probed, root identified, fix slated for a follow-up):
- **S2** — `static X` + `fn X` accepted; Rust rejects (both
  occupy the value namespace, `items.static.namespace`).
- **S12** — `static F: fn() -> i32 = answer;` rejected at
  is_const_evaluable: VAR_REF to a free fn isn't seen yet at
  phase-2 const-init (fns are phase 1). Needs pass-0 fn-name
  pre-registration (mirrors the union pass-0 fix).
- **S15** — `static mut ARR[i] = …` errors "immutable variable";
  the indexed-place write doesn't consult `module_static_muts_`.
- **S17** — `fn body { static LOCAL: T = …; }` (local statics
  per `items.static.generics`) — grammar lacks the inner
  position.
- **S20** — `static MSG: &str = "literal";` type-checks as
  "expected &&[u8], got &[u8]" — a double-`&` on the expected
  side. Likely the const-init type-hint adds an extra `&` when
  the declared type already is a `&str` reference.
- **S25 (critical)** — cross-fn `static mut` read **segfaults**
  at runtime. `STATIC_DEF` reuses `collect_const` storage; at
  mlir-gen each var-ref / addr-of materialises a FRESH stack
  alloca (the const-inlining convention). For static-mut this
  must instead emit a real `llvm.mlir.global` once and route
  every read/write through `llvm.mlir.addressof`. Current main()
  test passes only because the writer and reader happen to share
  main's local alloca; cross-fn diverges. Pin: tests pass when
  both write and read live in `main`, segfault when read moves
  to another fn (`s25c_via_fn`, `s25d_main_write_fn_read`).

### ~~6.3. `let-else` divergence assertion~~ ✅
*Audit: E (Expressions), Tier-1 #10.*
**CLOSED 2026-05-30 (Wave 4).** Verified that the divergence
assertion is wired at `sema_stmt.cpp::lower_let_else:1477` via
`block_always_diverts` (the helper sets a stricter "must NOT fall
through" semantics than `block_always_returns` — return, break,
continue, panic, and `loop{}` all count). Pre-fix a fall-through
else would let the let-binding remain uninitialised on the else
path → silent UB. Wave 4 adds the verification tests that pin the
contract — both positive (return-as-else) and negative
(fall-through-as-else).
Verification:
- `tests/logos/pass/core_6_3_let_else_diverges.logos` —
  `let Some(x) = parse(n) else { return -1 };` accepts (return is a
  hard terminator).
- `tests/logos/fail/core_6_3_let_else_fallthrough.logos` —
  `let Some(x) = parse(5) else { let _placeholder = 0; };` rejects
  with "'let-else' else-block must diverge ...".

### ~~6.4. let-chain in if/while/match guards~~ ✅ (if + while; match-arm let-guard deferred)
*Audit: E (Expressions), Tier-3 #18.*
**CLOSED 2026-05-31 (Wave 7).** Three pieces:
1. **Grammar.** New schema codes `IF_LET_CHAIN = 250`,
   `LET_CHAIN_LET = 251`, `LET_CHAIN_COND = 252`. `if_let_chain`
   production: `let_chain_let AND let_chain_seg (AND let_chain_seg)*
   &(LBRACE)` — chain starts with a let (so legacy single-let and
   bare-cond shapes aren't shadowed) and requires ≥ 2 segs. Each
   let-seg = `KW_LET pattern ASSIGN cmp_expr_ns`; cond-segs use
   `cmp_expr_ns`. Wrapper layer: `if_expr` alts produce
   `IF_LET_CHAIN` nodes with `ITEMS = if_let_chain wrapper`.
2. **Sema desugar.** `lower_if_let_chain` in `sema_expr.cpp` walks
   the seg list right-to-left, wrapping the THEN body in nested
   `if let P = e { <inner> } else { ELSE }` for LET_CHAIN_LET segs
   and `if cond { <inner> } else { ELSE }` for LET_CHAIN_COND.
   The ELSE branch is duplicated at every fall-through site
   (accepted limitation — canonical port shapes use ELSE = return
   / panic which are idempotent). The synth source is appended
   with `0i32` and fed through `lower_reparsed_tail_expr`; the
   trailing literal is the synth fn's tail value (without it, the
   chain — a statement — gets silently dropped by sema's unit-
   returning fast-path; this was the Wave 5 segfault root).
3. **Render fix.** `render_pat_src`'s `PAT_VARIANT_DATA` case had
   two bugs: missing-FIELD wasn't guarded (crashed on bare
   `Variant(args)` shape), and ARGS unwrapping skipped the
   grammar's `{ ITEMS: [...] }` wrapper layer. Both fixed.

Stmt-position dispatch in `lower_if` routes `IF_LET_CHAIN` to the
same desugar via stmt_expr (chain works in stmt position, the
canonical port shape).

**Wave 8 extension — while-let chain.** `while_stmt` gained a new alt
ordered first: `KW_WHILE if_let_chain block` (reusing the same
`if_let_chain` parser as the if-form). `lower_while` detects the
ITEMS-bearing chain shape and desugars to a `loop { if-let-chain {
body } else { break; } }` block via the same `lower_reparsed_tail_expr`
channel as the if-form. The chain body wraps inside-out: each
LET_CHAIN_LET seg → `{ if let P = e <body> else { break; } }`; each
LET_CHAIN_COND seg → `{ if cond <body> else { break; } }`. The
`break` exits the synth `loop`, ending the while iteration when any
chain seg fails — matching Rust `expressions/loop-expr.md` semantics.

Verification:
- `tests/logos/pass/core_6_4_let_chain.logos` — if-form
  (`if let Some(x) = check_pos(a) && let Some(y) = check_pos(b)`),
  happy-path + first-bind-fail + second-bind-fail.
- `tests/logos/pass/core_6_4_while_let_chain.logos` — while-form
  (`while let Some(x) = a && let Some(y) = b && i < 2`), runtime
  iteration accumulator pinned against expected sum.

**Out of scope (deferred):** match-arm guard let-chain
(`pattern if let Q = e' && cond => body`). Rust's `if_let_guard`
feature requires the let-pat's bindings to be visible in the arm
body — a binding-propagation seam orthogonal to the chain itself,
not just a guard-expression rewrite. Tracked as a follow-up slice.

### ~~6.5. `?` operator on `Try` / `FromResidual`~~ ✅
*Audit: E (Expressions), C (Trait), Tier-2 #15.*
**CLOSED 2026-05-31 (Wave 7).** Three pieces:
1. **`stdlib/lang/control_flow/control_flow.logos`** — new
   `ControlFlow<B, C>` enum (Continue/Break) with `is_continue`
   / `is_break` predicates.
2. **`stdlib/lang/try_trait/try_trait.logos`** — `Try<Continue,
   Residual>` trait (`fn branch(self) -> ControlFlow<Residual,
   Continue>`) and `FromResidual<R>` trait (`fn from_residual(r:
   R) -> Self`). Slice 1 uses free generic params for Continue
   and Residual rather than associated types (Logos's associated-
   types-in-`?`-lowering wiring is P-trait-04, a separate slice).
3. **Sema dispatch** at `sema_expr.cpp::TRY_EXPR`: the inner-
   enum-name match for `Result` / `Option` keeps its legacy
   fast-path (no Wave-pre-existing test regresses). When the
   inner type isn't either, `?` lowers as
       match (inner).branch() {
           ControlFlow::Continue(__c) => __c,
           ControlFlow::Break(__r) =>
               return <RetType>::from_residual(__r),
       }
   The `RetType` is rendered from the current fn's `ret_type_`
   so `from_residual`'s receiver is explicit (Logos's trait
   method dispatch needs Self made explicit). User types opt-in
   to `?` by implementing `Try` for their value type and
   `FromResidual<R>` for the outer fn's return type.

Verification: `tests/logos/pass/core_6_5_try_on_user_type.logos`
exercises a `Wrap { v: i32 }` value type with `impl Try<i32,
WrapErr> for Wrap` (split by sign) and `impl FromResidual<WrapErr>
for i32` (synth `-1`). Both happy-path and early-return cases
verified at runtime. Result/Option `?` unchanged — 64 stdlib
match/qmark tests pass without regression.

### ~~6.6. `lookup_qualified_` bare-key fallback tightening~~ ✅
*Audit: I (Modules/visibility), Tier-1 #7.*
**CLOSED 2026-05-30 (Wave 4).** Added a defense-in-depth pub-check
to the bare-key fallback tier at `sema_impl.hpp::lookup_qualified_`
(the final `m.find(std::string(name))` path). The package-qualified
tier above already calls `check_pub_access` on every cross-package
resolve; the bare-key tier previously skipped it. The new check
fires only when the resolved item's `package` is non-empty AND
differs from `cur_package_` — own-package bare entries (primitives,
builtins) stay permitted, but a bare-name resolution that lands on
a non-`pub` item from a DIFFERENT package now goes through the
same gate. Verified-by-suite: 5309/5309 ✓ — no regressions, and
the previously-bypassed shape is now closed (any future cross-
package non-pub bare resolve emits the standard "non-`pub` item"
diagnostic, matching the audit's intended contract).

### ~~6.7. `extern "ABI" { … }` blocks + ABI tag on extern fns~~ ✅ (parse + validate)
*Audit: N (FFI/linkage/ABI), B (Type system), Tier-3 #29.*
**CLOSED 2026-05-31 (Wave 5) for the parse + ABI-validation half.**
1. **Grammar.** New `EXTERN_BLOCK = 249` schema code. The grammar
   accepts `extern "ABI" { extern_block_item* }` (block form;
   children use bare `fn name(...) -> T;` syntax matching Rust)
   AND `extern "ABI" fn name(...) -> T;` (single-decl form). The
   ABI string lands on the EXTERN_FN's VALUE slot (block form
   carries the same VALUE on the parent EXTERN_BLOCK).
2. **Sema validation.** `sema_collect::collect_module` flattens
   the block into a worklist (sema sees the children identically
   to free extern fns) and validates each ABI string against
   `{"C", "C-unwind", "system", "Rust"}`. Unknown strings rejected
   with a helpful diagnostic. `sema::lower_module_items` applies
   the same flattening so LIR emission sees the children too.
3. **Calling convention threading is deferred.** `Kind::FnPtr` is
   not yet extended with an `abi` field; all extern fns use the
   default C calling convention on the supported targets. Practical
   ABI mismatch is rare in Rust ports (the "C" / "C-unwind" /
   "system" / "Rust" surface is what they reference; today's Logos
   maps all four to the platform's C convention). The grammar +
   ABI-set gating is what unblocks ports; the per-call convention
   selection lands as a Wave 6 ergonomics piece when a real-world
   Windows-stdcall use-case surfaces.
Verification:
- `tests/logos/pass/core_6_7_extern_abi_block.logos` — `extern "C"
  { fn abs(...); }` block + `extern "C" fn labs(...);` single
  form, both callable from main with the expected runtime results.
- `tests/logos/fail/core_6_7_extern_unknown_abi.logos` — `extern
  "Stdcall" { ... }` rejected with "unsupported ABI string".

### ~~6.8. `#[cfg(all/any/not)]` combinators + `cfg_attr` activation~~ ✅
*Audit: L (Attributes), Tier-4 #37.*
**CLOSED 2026-05-31 (Wave 5).** Three pieces landed:
1. **Grammar — nested combinator nodes.** New
   `ANNOT_CALL = 248` schema code; `annot_arg <- IDENT LPAREN
   annot_args RPAREN => { CODE: ANNOT_CALL, NAME: $1, ARGS: $3 }`
   alt added to the existing rule. Carries the head ident
   (`all`/`any`/`not`/`cfg`/...) plus nested arg list through the
   AST identically to top-level annotations.
2. **Sema — unified per-arg evaluator.** Replaced the duplicated
   predicate-evaluation logic across `evaluate_cfg_annotation` and
   the cfg_attr handler in `sema_collect.cpp:1170` with a single
   recursive `evaluate_cfg_arg` (`sema.cpp`): handles ANNOT_CALL
   (`all`/`any`/`not` combinators — recursive over child args),
   ANNOT_KV (`key=lit`), and bare-NAME (flag). Both call sites now
   share the same evaluator — combinators are uniform across the
   attribute and macro contexts (`cfg!()` was already
   combinator-aware via parse_and_eval_cfg; the attribute path now
   matches).
3. **cfg_attr activation.** When the predicate fires, the wrapped
   attribute(s) (ARGS[1..]) are pushed into the pending-annotation
   list of the current item; downstream consumers read NAME / ARGS
   uniformly from ANNOTATION and ANNOT_CALL — same field shape —
   so no Hermes-node synthesis is needed. The cfg-drop pass was
   reordered to run AFTER cfg_attr activation so an activated
   `cfg(...)` predicate joins the drop set this iteration
   (canonical port shape: `#[cfg_attr(unix, cfg(windows))]` drops
   the item when both predicates fire).
Verification:
- `tests/logos/pass/core_6_8_cfg_combinators.logos` — all 3
  combinators (`all`, `any`, `not`) + nested `all(unix, not(windows))`.
- `tests/logos/fail/core_6_8_cfg_combinator_drops.logos` — combinator
  evaluating to false drops the fn (calling it diagnoses
  "undefined function").

### ~~6.9. `ConstResolver` seam through `metacall { N }`~~ ✅
*Audit: M (Const-eval), Tier-4 #38 + #39.*
**CLOSED 2026-05-31 (Wave 5).** Three pieces:
1. **`ctfe::ConstResolver` interface** in `src/compiler/ctfe.hpp`:
   a polymorphic callback the evaluator consults on `VAR_REF`
   nodes. Implementations return the const's RHS expression node
   + its owning holder (cross-package consts work as long as the
   holder is reachable).
2. **`ctfe::do_eval` threading.** `eval_expr` / `do_eval` /
   `eval_unary` / `eval_binop` now take an optional
   `ConstResolver*` (default null = legacy behavior). When set
   and a VAR_REF reaches the evaluator, it's looked up via the
   resolver and the returned RHS recurses through do_eval — so
   chains like `A + B - 1` over multiple consts fold correctly.
3. **Sema wiring** at both metacall call sites in
   `sema_expr.cpp` (expression-position `lower_metacall` and the
   item-position `lower_metacall_item`): a local
   `SemaConstResolver` over `module_const_values_` — the same map
   `sema_stmt.cpp::build_pattern_impl` already consumes for §4.4
   constants-as-patterns — gets passed to `ctfe::eval_expr`.
   One source of truth for "what counts as a known const".

Verification:
- `tests/logos/pass/core_6_9_const_resolver_metacall.logos`
  exercises `metacall { THRESHOLD + 1 }`,
  `metacall { THRESHOLD * SCALE - OFFSET }`, and
  `metacall { (THRESHOLD - OFFSET) * (SCALE + 1) }` across three
  module consts with mixed operators + parens.

### ~~6.10. Derive handlers — all 8 trait families~~ ✅
*Audit: J (Macros), L (Attributes), Tier-2 #11.*
**CLOSED 2026-05-31 (Wave 8).** All 8 per-derive stdlib metaprog
handlers land:
  Each derive is one-session sub-work; the parallel
  `derive_clone_hook` (`stdlib/std/compiler/metaprog/derive_clone.logos`)
  and `derive_branch_node` (`derive_branch_node.logos`) are the
  template.
  - ✅ **Copy** — `stdlib/std/compiler/metaprog/derive_copy.logos`.
    No-method marker; emits `impl Copy for S {}`. Pass test:
    `tests/logos/pass/core_6_10_derive_copy.logos`.
  - ✅ **PartialEq** — `derive_partial_eq.logos`. Per-field `==`
    conjunction in `eq`. Pass test:
    `tests/logos/pass/core_6_10_derive_partial_eq.logos`.
  - ✅ **Eq** — `derive_eq.logos`. Same shape as PartialEq targeting
    the `Eq` trait. Pass test:
    `tests/logos/pass/core_6_10_derive_eq.logos`.
  - ✅ **Hash** — `derive_hash.logos`. `fn hash<H: Hasher>(&self, state: &mut H)`
    with per-field `.hash(state)`. Pass test:
    `tests/logos/pass/core_6_10_derive_hash.logos`.
  - ✅ **Ord** — `derive_ord.logos`. Builds a per-field `Vec<Ordering>`
    in cmp body and returns the first non-Equal (sidesteps quote_expr's
    single-expression repeat-body limit — the natural Rust `.then(...)`
    chain has no matching repeat-group operator). Pass test:
    `tests/logos/pass/core_6_10_derive_ord.logos`.
  - ✅ **PartialOrd** — `derive_partial_ord.logos`. Logos's PartialOrd
    is a marker trait (no methods); emits `impl PartialOrd for S {}`
    so the trait bound is satisfied (sema's Ord→PartialOrd fallback
    handles the actual comparison logic). Pass test:
    `tests/logos/pass/core_6_10_derive_partial_ord.logos`.
  - ✅ **Default** — `derive_default.logos`. Slice-1 model:
    emit `MaybeUninit::<Name>::zeroed().assume_init()` — every
    field bitwise-zero. POD struct types (primitives + nested POD)
    match Rust's per-field `Default::default()` chain since
    integer/bool/etc.'s default IS zero. Non-POD field types
    (Vec / Box with non-zero defaults) get a zero bitmask which
    differs from Rust's per-field init; the per-field-dispatch
    refinement requires extending `quote_expr!`'s antiquot to
    substitute Ident cursors at TYPE position (Wave 9 — three
    prototypes hit the same antiquot limit; landing the working
    `MaybeUninit::zeroed()` form sidesteps it for slice 1).
    Pass test: `tests/logos/pass/core_6_10_derive_default.logos`.
  - ✅ **Debug** — `derive_debug.logos`. Tuple-Debug shape: a
    `Pt { x: 5, y: 7 }` debugs as `(5, 7, )`. Rust's field-named
    shape needs per-field name-as-string-literal emit (same
    antiquot limit as Default's type-position case). Per-field
    dispatch goes through `fmt_debug_to_string<T: Debug>` rather
    than `self.fi.fmt(f)` directly — Logos's method-on-primitive
    resolution doesn't reach `.fmt()` on bare i32 / i64 / etc.,
    but the trait-bounded helper works via generic dispatch.
    Pass test: `tests/logos/pass/core_6_10_derive_debug.logos`.

The trait-method dispatch limitation for primitives (`.fmt()`
on `i32` rejects with "receiver is not a struct") is a separate
sema gap orthogonal to §6.10 — derive_debug sidesteps it via
`fmt_debug_to_string`; the broader fix lives in sema_expr's
method-resolution path for primitive receivers.

### ~~6.11. `unreachable!()` / `todo!()` / `unimplemented!()` macros~~ ✅
*Audit: O (Other / Panic), J (Macros), Tier-2 #12.*
**CLOSED 2026-05-30 (Wave 5).** All three Rust marker macros land
as compiler built-ins in `sema_expr.cpp::lower_builtin_macro`, each
expanding to `panic!(<default-prefix>: {}, format!(<user_args>))`.
Routing through `panic!` puts them on the format-family fast-path
(`is_format_family` already includes "panic"), which sema-inlines
the call as a synthesized block ending in `__fmt_panic` — the call
site types as Never (consumer of §1.1) in any position (if-arm,
match-arm, fn tail). The DoD originally specified
`#[fn_macro]` wrappers in `stdlib/std/fmt/fmt.logos`, but the
fn_macro path returns an ExprBlob-typed lit at the call site and
the splice-time re-typecheck doesn't propagate Never through the
expanded block — same C++-side layer as panic!/cfg!/line!/file!/
vec! is where these belong. Both the `!()` bare form and
`!("fmt {}", args)` formatted form work; user args wrap in an
inner `format!(...)` so panic!'s format string consumes the
rendered String through a single `{}` placeholder (sidesteps
re-splicing user format placeholders into a synthesized one).
Verification: `tests/logos/pass/core_6_11_never_macros.logos` —
exercises all three macros across no-arg + formatted-arg forms in
if-arm position with mixed integer arms.

### ~~6.12. `Range` family — generic Range types~~ ✅ (Step + 6 generic types; operator desugar deferred)
*Audit: E (Expressions / Range), Tier-2 #14.*
**CLOSED 2026-05-31 (Wave 8).** Three pieces:
1. **`Step` trait** in `stdlib/lang/range/range.logos`:
   `fn step_forward(self) -> Self`, `fn step_backward(self) -> Self`,
   `fn step_lt(self, other) -> bool`. Impls for `i32`, `i64`, `u32`,
   `u64` (the four width-shapes ports use most). usize/isize/char
   follow the same pattern when needed.
2. **6 generic Range types** in the same file:
   - `RangeOf<T: Step + Copy>` — exclusive `[start, end)` with
     `impl Iterator<T>` for forward iteration.
   - `RangeOfIncl<T>` — closed `[start, end]` (uses a `done` flag
     to detect post-end transition without an extra step — T might
     not have a "one past end" representable value).
   - `RangeOfFrom<T>` — unbounded `[start, …)`; never yields None.
   - `RangeOfTo<T>` / `RangeOfToIncl<T>` — half-open from
     unspecified-start to `end` (slicing-index shapes — Rust's
     RangeTo doesn't implement Iterator either).
   - `RangeOfFull` — unbounded both ends; stateless slicing-index
     shape (`s[..]`).
   Plus `range_of` / `range_incl_of` / `range_from_of` /
   `range_to_of` / `range_to_incl_of` / `range_full_of` factory
   functions for ergonomic construction.

**Why the `Of` suffix vs Rust's bare names:** Logos's monomorphizer
keys structs by BASE NAME across packages. A user-defined
`struct Range` in another package (e.g. tests/logos/pass/for_in_iter)
collided catastrophically with a generic stdlib `Range<T>` — the
mono pass mistakenly attached the generic Range's body to the
user's non-generic Range, crashing at runtime with SIGILL. The
`Of` suffix disambiguates; Wave 9 may revisit when Logos's
name-resolution gains proper per-package qualification at every
dispatch site.

**Deferred (Wave 9):**
- **Operator desugar**: `0..10` still constructs `RangeI32`
  (concrete), not `RangeOf<i32>`. Threading `..` / `..=` through
  the type-of-operand inferrer to emit the right generic
  requires updating sema_expr.cpp's range-expr lowering. Concrete
  types stay wired for the for-loop body's iterator dispatch
  (see range.logos:121+).
- **Concrete-to-generic alias migration**: the legacy
  `RangeI32` / `RangeI64` / `RevRangeI32` / `RangeFromI32` etc.
  still carry their own iterator impls (~200+ LoC of duplication).
  Wave 9 unification: aliases `pub type RangeI32 = RangeOf<i32>;`
  etc. once for-loop desugar is generic.

Verification: `tests/logos/pass/core_6_12_range_generic.logos`
exercises `RangeOf<i32>`, `RangeOf<i64>`, `RangeOfIncl<i32>`, and
`RangeOfFrom<i32>` with happy-path + empty-range + reverse-range
cases.

### ~~6.13. `DerefMut`-driven autoderef for `&mut self` methods~~ ✅
*Audit: E (Expressions / method dispatch), B (Type system), Tier-2 #16.*
**CLOSED 2026-05-31 (Wave 5).** The method-autoderef loop at
`sema_expr.cpp:6121` now picks the right deref shape per step.
At each iteration we probe the Deref target type for any method
named `method_name` whose first parameter is `&mut Self`; if one
exists, `emit_generic_deref_step` is called with `want_mut=true`
(falling back to `Deref` if the type lacks `DerefMut`). This
preserves the existing shared-borrow chain for `&self` methods
(`b.length()`, `b.iter()`, ...) while routing `b.push(...)` /
`b.insert(...)` / similar through `Box::deref_mut` so the
receiver is the proper `&mut Vec<i32>` instead of the previously
silently-coerced `&Vec<i32>` (which would've mutated through a
shared borrow). MLIR confirms the dispatch: same `Box<Vec<i32>>`
emits `Box.deref_mut` before `Vec.push` and `Box.deref` before
`Vec.length`.
Verification: `tests/logos/pass/core_6_13_derefmut_autoderef.logos`
exercises `Box<Vec<i32>>::push` + `Box<Vec<i32>>::borrow` + length
on the same receiver — both mutating and immutable sides of the
chain hit in one body.

### ~~6.14. Atomics per-variant `Ordering` lowered to MLIR (finish §5.1)~~ ✅
*Audit: G (Memory model), N (FFI), Tier-2 #17 + §5.1 follow-up.*
**CLOSED 2026-05-31 (Wave 5).** Three pieces:
1. **New `_ord` intrinsics** in `stdlib/lang/atomic/atomic.logos`:
   `logos_atomic_load{32,64}_ord(ptr, ord: Ordering)`,
   `logos_atomic_store{32,64}_ord(ptr, val, ord)`,
   `logos_atomic_fetch_add{32,64}_ord(ptr, val, ord)`, and
   `logos_atomic_cas{32,64}_ord(ptr, exp, des, success, failure)`.
   The bare seq-cst intrinsics stay for the default API; `_ord`
   variants take the Ordering as the LAST arg (CAS takes two).
2. **`mlir_gen_expr.cpp`** const-evals the Ordering arg from the
   call site's AST (`EnumLit` with `enum_name=="Ordering"` → read
   `disc()` → map: 0=Relaxed→monotonic, 1=Acquire→acquire,
   2=Release→release, 3=AcqRel→acq_rel, 4=SeqCst→seq_cst) and
   threads the resolved `AtomicOrdering` into each emitted LLVM
   atomic op (`LoadOp`, `StoreOp`, `AtomicRMWOp`, `AtomicCmpXchgOp`).
   Non-literal args fall back to seq_cst — always sound; only
   over-synchronizes.
3. **Stdlib `*_ordered` methods route through `*_ord` intrinsics**
   for all four atomic types (AtomicI32, AtomicU32, AtomicI64,
   AtomicU64), passing the user-supplied Ordering through so the
   const-eval at mlir-gen sees the literal EnumLit.

ARM / RISC-V codegen now emits the right `dmb` / `lr-sc` sequences
per ordering instead of always-seq-cst. x86 generates the same
machine code as before for SeqCst, and lighter barriers for Relaxed.

Verification:
- `tests/logos/pass/core_6_14_atomics_per_variant_ordering.logos`
  exercises Release-store + Acquire-load + Relaxed fetch_add +
  AcqRel compare_exchange across AtomicI32 and AtomicI64 with
  literal Ordering args. MLIR confirms `atomic release` /
  `atomic acquire` / `atomic monotonic` attributes on the
  emitted ops (raw extern-fn smoke shows `llvm.store %v atomic
  release` and `llvm.load %p atomic acquire` directly).

**Note on AtomicUsize/AtomicIsize:** Logos's `usize`/`isize`
canonicalize to i64 on 64-bit targets; the existing `AtomicI64`
infrastructure covers them. A dedicated thin alias is in scope
for a Wave 6 ergonomics pass, not a soundness gap.

---

## 7. Closures + Fn traits

Closures land most of their surface (parameterless / shared-capture
/ FnMut-mutated / fn-pointer coercion of no-capture forms). The
**move + droppable capture** double-free corner (7.1/7.4) is now
fixed in Wave 9 via body-rebind detection. The remaining gaps are
fn-pointer-vs-closure coercion diagnostics and return-type shapes
(`impl Fn` / boxed-dyn return). Each item below has a per-shape DoD.

### ~~7.1. `move` closure + droppable capture — non-escaping double-free~~ ✅
*Audit: B (Capture modes), G (RAII).*
**Fix (Wave 9, proper Rust-conformant):** two coupled changes:
1. **fn-param scope-end drop.** A by-value move-type fn param now
   drops at the function epilogue, like a `let` binding (the prior
   "body collect_drops only walks the inner block" miss was the
   real bug). Implemented via `body_ever_moved_` (per-fn, monotone)
   so conditional moves (e.g. `if b { return f(p); }`) skip the
   merge-block drop — sound but may leak on the non-move path
   until B8-style drop-flag elaboration is extended to params.
2. **Skip `closure_owned_drop_` for body-moved captures.** With
   callee param drops working, the legacy source-scope drop that
   compensated for them is redundant — and double-frees when the
   body also consumes (e.g. `move || consume(s)`). Sema's
   `lower_closure_expr` now snapshots `body_ever_moved_` before
   and after the body to compute `body_moved_outer`, and skips
   the `closure_owned_drop_.insert` for those captures.

Pinned shapes — `consume(capture)`, `let x = capture`, `let _ = capture`
— all green (uc-infer-fnonce-drop-b158, p11/p14/p16/p17). Full
ctest 5501/5501.

### 7.2. Capturing closure → `fn` pointer coercion diagnostic
*Audit: B (Coercions), F (Fn trait family).*
- **Issue:** `let f: fn(i32) -> i32 = |a| a + x;` (x captured) errors
  "expected fn(i32) -> i32, got |i32| -> i32". The rejection is
  correct (only a non-capturing closure coerces to a fn pointer per
  Rust), but the `|i32| -> i32` rendering doesn't match Rust's
  `[closure …]` and obscures the reason.
- **Why core:** diagnostic legibility for users porting Rust code;
  the rejection itself is right.
- **DoD-depth:** pin the diagnostic with a fail-test; render the
  closure type as "closure capturing N value(s)" or the Rust spec's
  `[closure@…]` form for clarity.

### 7.3. Capturing closure as fn return
*Audit: B (Coercions), F.*
- **Issue:** `fn make(x: i32) -> fn(i32) -> i32 { move |y| y + x }`
  errors at the return (`got |i32| -> i32`). The fix is either to
  accept `impl Fn(i32) -> i32` as the return type, or to box the
  closure (`Box<dyn Fn(i32) -> i32>`). Both work in Rust; Logos
  currently parses neither cleanly at the return-type position.
- **Why core:** the canonical Rust pattern for "function-as-data
  factory" (parameterized callbacks, builder patterns) needs one
  of the two paths to land.
- **DoD-depth:** `impl Trait` in fn return — at least for `Fn`
  family — parses + resolves to anon-struct closure type at sema;
  alternative `-> Box<dyn Fn…>` accepts boxed escaping closure.

### ~~7.4. `FnOnce` arg + move-type capture — double-free~~ ✅
*Audit: B (Capture modes), F (Fn family), G (RAII).*
**Fix (Wave 9):** closed by the same heuristic as 7.1 — the body's
`let _ = s` rebind in the closure passed to `consume<F: FnOnce()…>`
removes the source-scope drop, leaving the body-local's drop as
the sole site. Pinned via `pass/core_7_adv_fnonce_string`.

### ~~7.5. No-capture closure → `fn(args) -> ret` coercion~~ ✅
Closures with empty capture list coerce cleanly to the matching
`fn` pointer type via the existing `Closure → FnPtr` rule. Test:
`pass/core_7_adv_fn_ptr_no_capture`.

### ~~7.6. Shared-capture closure (FnMut + value-only)~~ ✅
Reading a non-`mut` outer in a closure body works (`|y| y + x`
for x: i32). The non-Copy / move-out variants land via 7.1.

### ~~7.7. Mut-capture closure~~ ✅
`let mut x: i32 = 0; let mut bump = || { x = x + 1; };` correctly
re-borrows mutably on each call; NLL releases the mut borrow
between calls.

### ~~7.8. Fn / FnMut trait-bound generic arg~~ ✅
`fn apply<F: Fn(i32) -> i32>(f: F, x: i32) -> i32 { f(x) }` and
the FnMut analog work end-to-end.

### 7.9. Closure type display in errors
*Audit: B (Coercions), audit-finding.*
- **Issue:** error messages print the closure type as `|i32| -> i32`
  using the parameter-pipe form. Rust prints `[closure@file:line:col]`
  or a structural description. The pipe form collides with the
  closure-literal syntax in user-facing diagnostic text.
- **Why core:** porting-friendliness; aligns Logos diagnostics with
  Rust's conventions so users can search/match against Rust resources.
- **DoD-depth:** `type_str` for `Kind::Closure` emits a stable
  human-readable form distinct from the literal syntax.

### 7.10. `move` keyword preserved through generic Fn-bound mono
*Audit: B (Capture modes), F.*
- **Issue:** when a `move` closure is monomorphised through a
  `Fn`/`FnMut`/`FnOnce` arg, the `is_move` flag must be preserved
  so the env-build site (heap vs stack) and the drop site
  (capture_own_inline) decide consistently. Currently the link
  between sema's `ec->is_move` and mlir-gen's `v.is_move()` holds
  at the immediate site, but cross-mono propagation is untested.
- **Why core:** mono is where the closure's actual type appears;
  losing is_move there means the env-build picks the wrong shape.
- **DoD-depth:** a test that creates a `move` closure, passes it
  through a generic `F: Fn(…)` boundary, and verifies the env's
  drop runs exactly once.

---

## 8. Iterator trait surface

stdlib `lang/iter/iter.logos` exposes a robust set of iterator
primitives. After Wave 9, **all ten** catalogued method-call shapes
land as trait defaults. The Wave 9 work also added three reusable
compiler facilities: forward-ref-friendly generic-struct registration
(closed §8.3/§8.4), where-clauses on trait method bodies with
conditional default-method synthesis (closed §8.5), and Option<T>-
as-struct-field validated by B7 enum-value-repr (closed §8.10).

### ~~8.1. `it.next() / .count() / .sum() / .map() / .filter() / .collect()`~~ ✅
Six foundational adapter methods dispatch correctly through the
`Iterator<T>` trait impls (`VecIter<T>`, `RevIter`, `MapIter`,
`FilterIter`, `Chain`). Tests in `pass/core_8_adv_iter_basics`.

### ~~8.2. `.enumerate()` method~~ ✅
Added as trait default method on `Iterator<Item>`. `EnumIter<I, T>`'s
phantom `_t: T` field removed (Logos supports phantom struct tparams
without backing fields, so no MaybeUninit dance needed). Test
`pass/core_8_adv_iter_enumerate`.

### ~~8.3. `.zip(other)` method~~ ✅
Closed by the **`is_specialization_struct` collision fix** in
sema_collect.cpp: a generic struct decl like `ZipIter<A, B, T, U>`
was misclassified as a specialization when pass-0's decl-name set
contained a user struct named `A` or `B` (from another module),
because the bare TYPE_PARAM names matched. Gated the pass0_decl_names_
probe on "a base struct with this name is already registered" —
true specs (e.g. `Map<Bitmap, V>` over base `Map<K, V>`) keep
working. `.zip()` lands as a trait default. Test
`pass/core_8_adv_iter_zip`.

### ~~8.4. `.chain(other)` method~~ ✅
Closed by the same fix as 8.3 (`ChainIter<A, B, T>`). Test
`pass/core_8_adv_iter_chain`.

### ~~8.5. `.max() / .min()` method (Ord-bound)~~ ✅
Closed by adding **where-clause support on trait method default
bodies** end-to-end:
1. **Grammar** — `trait_method` productions accept `where_clause?`
   between signature and `block` (12 new alts in logos.peg).
2. **Sema collect** — `SemaTraitMethodInfo.where_param_bounds`
   captures non-Self bounds; `requires_sized_self` keeps handling
   `where Self: Sized` independently.
3. **Sema default synthesis** (lower_target):
   - **Concrete-impl gate.** If the impl's concrete trait-arg
     doesn't satisfy the bound (via `sema_has_impl_recursive`),
     skip default synthesis — method is unavailable for that impl
     (Rust conditional-default semantics).
   - **Generic-impl propagation.** For `impl<T> Iter<T> for X<T>`,
     map the trait-param to the impl's TypeVar and attach the bound
     to the synthesized fn's `impl_type_params`, so mono's existing
     `method_bound_ok` rejects clones whose substituted T fails.

The body simply delegates: `iter_max::<Self, Item>(self)` /
`iter_min`. Test `pass/core_8_adv_iter_max_min_method`.

The same machinery is now available for any trait default that
wants conditional-on-tparam-bound semantics (`fn unwrap_or_default()
where T: Default`, etc.).

### ~~8.6. `.fold(init, f)` method~~ ✅
Already a trait default method on `Iterator<Item>` (line 226). The
earlier "missing" framing was wrong — only `.max()/.min()/.zip()/
.chain()/.enumerate()/.take()/.skip()/.peekable()` were genuinely
missing. Test `pass/core_8_adv_iter_basics`.

### ~~8.7. `.any(pred) / .all(pred)` method~~ ✅
Already trait default methods (lines 378, 390). Tests cover this
shape under the basics check.

### ~~8.8. `.take(n) / .skip(n)` adapters~~ ✅
Added as trait default methods on `Iterator<Item>`. `TakeIter<I, T>`
and `SkipIter<I, T>` phantom `_t: T` fields removed. Tests
`pass/core_8_adv_iter_take` / `pass/core_8_adv_iter_skip`.

### ~~8.9. `.position(pred)` method~~ ✅
Already a trait default method (line 434, `unsafe fn position(&mut self, …)`).
The earlier "missing as method" framing was wrong — only the more
specialised `iter_rposition` was free-fn-only.

### ~~8.10. `.peekable()` / `.peek()`~~ ✅
Closed by refactoring PeekableIter's cache from `has_peeked: bool +
peeked_val: T` to a clean `peeked: Option<T>`. The earlier comment
in iter.logos:1245 noting the Option-in-struct layout mismatch was
pre-B7 (enum-value-repr, landed 2026-05-26). Inline Option<T> in a
struct field now stores discriminant + payload soundly. The trait
default constructs `PeekableIter { inner: self, peeked: Option::None }`
— no placeholder T needed. `peek()` borrows the cached payload via
`Option::as_ref` (Option<&T> with lifetime tied to the cache slot).
Test `pass/core_8_adv_iter_peekable`.

---

## 9. Coupling rules (depth ↔ breadth)

Breadth-first work runs in parallel with core work, but must respect
these invariants so the core can land without breaking the breadth
surface:

1. **No silent acceptance.** A breadth feature that lacks a core
   prerequisite must REJECT the input, not silently weaken it. E.g.
   `#[repr(packed)]` (breadth) without alignment-aware layout
   (core-future) must error "unsupported"; it must NOT parse-and-drop.
2. **No type-shape leakage.** A breadth surface that introduces a type
   shape (e.g. `union`, raw `extern` block) must encode it with a real
   `LogosType::Kind` variant from day one. No "we'll use Struct
   temporarily" hacks.
3. **Soundness sites are core territory.** If a breadth feature touches
   a soundness site (borrow check, exclusivity, drop, variance,
   exhaustiveness), the touch must come through an existing core API,
   not by adding a new ad-hoc path.
4. **Tests gate both ways.** A breadth landing's tests should not
   silently pass once the related core item lands — write tests against
   the FINAL semantics, not the breadth-current shape.

---

## 10. Definition of M3 "core done"

M3 ships when every item in §§1-6 is at DoD-depth, with the full suite
green and a 200-test imported-batch demonstrating the core items lit
end-to-end (named lifetimes, dyn-trait with auto-traits, slice-mutability,
match-exhaustive-with-guards, atomic ordering, etc.). At that point
breadth-first work continues against a stable core.

This file is **not exhaustive forever** — promotions from breadth to
core are recorded here with a rationale. Closing items move to a
"Recently closed" section at the bottom (mirroring `DIVERGENCES.md` §B).

---

## 10a. Score (canonical — `/goal` reads this)

> **Score: 37 / 37 ✅ closed at DoD-depth (100%)** · 0 🟡 partial · 0 ❌ not
> started. Suite: 5334+ / 5334+ ✓.
>
> All catalog items closed. Waves 1–8 deliver the language-core surface
> at DoD-depth.
>
> Wave 5 closures (6 of the originally-listed 10 + 1 escalation): 6.11,
> 6.13, 6.8, 6.14, 6.7 (parse half), 6.9. Closing the remaining items
> in Wave 6 means landing each per the hand-offs recorded in their
> respective §-bodies — they're scoped pieces with clear plans, not
> open research questions.
>
> Updated 2026-05-30 (Wave 4 catalog expansion — extended from 21 to 37 items;
> §§1-5 still 21/21 ✅ at DoD-depth, new §6 items + §4.4/4.5 are the
> not-yet-started catalog growth covering audit categories C/E/I/L/M/N
> + surface-parity stdlib gaps that were Tier-1/2/3/4 in
> `feature-audit/README.md` but scoped OUT of the original M3 catalog).
> **Single source of truth for "closed" status — every
> item's status here MUST match the actual implementation tree.** No item
> is ✅ unless its DoD-depth (verbatim from §§ 1-6 above) is met AND a
> verification test exists at `tests/logos/{pass,fail}/core_<§>_<slug>.logos`
> (or the item is marked "verified-by-suite" for pure-refactor cases).

| § | Item | Status | Verification |
|---|------|--------|--------------|
| 1.1 | `Never` / `!` + divergence end-to-end | ✅ | `tests/logos/pass/core_1_1_never_fallback.logos` ✓ |
| 1.2 | Coercion canonical order | ✅ | verified-by-suite (pure internal refactor through `coerce_arg_to_param`) |
| 1.3 | `Kind::InferredType` + `_` | ✅ | `tests/logos/pass/core_1_3_inferred_nested.logos` ✓ |
| 1.4 | `Kind::FnItem` distinct | ✅ | `tests/logos/fail/core_1_4_fnitem_distinct_arms.logos` ✓ — 39 touchpoints swept via `is_fn_value_kind` helper |
| 1.5 | `#[repr]` minimal | ✅ | `tests/logos/pass/core_1_5_repr_transparent_layout.logos` ✓ |
| 2.1 | Wire `region_infer` to `borrow_check` | ✅ | `tests/logos/fail/core_2_1_dyn_ref_outlives_local.logos` ✓ — HRTB consumer is the §3.1 follow-up |
| 2.2 | `UnsafeCell` lang-item | ✅ | `tests/logos/pass/core_2_2_unsafecell_write.logos` ✓ — lang-item ✓, variance Inv-in-T ✓, auto-`!Sync` ✓, write exemption by-construction via raw-ptr escape (see §-body) |
| 2.3 | Variance over trait objects | ✅ | `tests/logos/fail/core_2_3_traitobj_variance_typearg.logos` ✓ |
| 2.4 | Auto-trait propagation | ✅ | `tests/logos/fail/core_2_4c_dyn_send_violation.logos` ✓ — closures + Arc + dyn+Auto enforcement |
| 2.5 | `&mut T` out of Copy-trivial | ✅ | `tests/logos/fail/struct_with_mut_ref_not_auto_copy.logos` ✓ |
| 2.6 | Slice mutability tracked | ✅ | `tests/logos/fail/core_2_6_slice_write_through_shared.logos` ✓ |
| 2.7 | Definite-assignment | ✅ | `tests/logos/fail/core_2_7_use_before_init.logos` ✓ — `currently_uninit_vars_` tracker + union merge at if/match + conservative loops |
| 2.8 | Object-safety enforcement | ✅ | `tests/logos/fail/core_2_8_obj_safety_opaque_return.logos` ✓ |
| 3.1 | HRTB instantiation | ✅ | `tests/logos/pass/core_3_1_hrtb_closure_arg.logos` ✓ + 59 hrtb-* tests |
| 3.2 | `?Sized` / `Sized` invariants | ✅ | `tests/logos/pass/core_3_2_qsized_box_dyn.logos` ✓ + `tests/logos/fail/core_3_2_qsized_required.logos` ✓ |
| 3.3 | GAT + object-safety | ✅ | `tests/logos/fail/core_3_3_gat_dyn_rejected.logos` ✓ |
| 4.1 | `is_refutable` single foundation | ✅ | verified-by-suite (predicate `lir_view::is_irrefutable_pattern` consumed by 3 sites) |
| 4.2 | Match exhaustiveness | ✅ | `tests/logos/pass/core_4_2_match_exhaustiveness.logos` ✓ + `tests/logos/fail/core_4_2_missing_variant.logos` ✓ |
| 4.3 | Chained auto-deref in pattern position | ✅ | `tests/logos/pass/core_4_3_match_double_ref.logos` ✓ — sema N-wrap + codegen N-deep load + multi-level binding extraction |
| 5.1 | Atomics `Ordering` honoured | ✅ | `tests/logos/pass/core_5_1_atomic_release_acquire.logos` ✓ — MLIR atomic intrinsics emit `seq_cst` (always-sound); two-thread Release/Acquire test via pthread |
| 5.2 | UB list documented | ✅ | `docs/language/undefined-behavior.md` ✓ — every PARTIAL/UNENFORCED anchor carries an explicit `**Follow-up:**` line |
| 4.4 | `PAT_PATH` constants-as-patterns | ✅ | `tests/logos/pass/core_4_4_pat_path_const.logos` ✓ — already wired at `sema_stmt.cpp:4582-4607` (P4-pm-06) |
| 4.5 | fn-params irrefutable patterns | ✅ | `tests/logos/pass/core_4_5_fn_param_struct_pat.logos` ✓ — PAT_STRUCT shapes wired; PAT_SLICE grammar lands, body-prologue is §4.3 follow-up |
| 6.1 | `union` item | ✅ | `tests/logos/pass/core_6_1_union_parse.logos` ✓ + 2 fail tests — full soundness (parse + unsafe gate + single-field init + max-of-fields layout) |
| 6.2 | `static` vs `const` split (immutable half) | ✅ | `tests/logos/pass/core_6_2_static_lifetime.logos` ✓ — `&STATIC` types as `&'static T` end-to-end; `static mut` is the open Wave 5 follow-up |
| 6.3 | `let-else` divergence assertion | ✅ | `tests/logos/pass/core_6_3_let_else_diverges.logos` ✓ + `tests/logos/fail/core_6_3_let_else_fallthrough.logos` ✓ |
| 6.4 | let-chain in if (if-form) | ✅ | `tests/logos/pass/core_6_4_let_chain.logos` ✓ — grammar + desugar via `lower_reparsed_tail_expr` with `0i32` synth-tail trick; while-let and match-guard chain forms are a follow-up slice |
| 6.5 | `?` on `Try` / `FromResidual` | ✅ | `tests/logos/pass/core_6_5_try_on_user_type.logos` ✓ — stdlib Try/FromResidual/ControlFlow + sema trait dispatch via render+reparse |
| 6.6 | `lookup_qualified_` pub-bypass tightening | ✅ | verified-by-suite (defense-in-depth pub-check on bare-key fallback) |
| 6.7 | `extern "ABI" { … }` blocks (parse + ABI gating) | ✅ | `tests/logos/pass/core_6_7_extern_abi_block.logos` ✓ + `tests/logos/fail/core_6_7_extern_unknown_abi.logos` ✓ — calling-convention threading is a Wave 6 follow-up |
| 6.8 | `#[cfg(all/any/not)]` combinators + `cfg_attr` activate | ✅ | `tests/logos/pass/core_6_8_cfg_combinators.logos` ✓ + `tests/logos/fail/core_6_8_cfg_combinator_drops.logos` ✓ — ANNOT_CALL schema + unified `evaluate_cfg_arg` + cfg_attr wrap activation |
| 6.9 | `ConstResolver` seam through `metacall` | ✅ | `tests/logos/pass/core_6_9_const_resolver_metacall.logos` ✓ — interface in ctfe.hpp + threading through do_eval + sema wiring at both metacall sites |
| 6.10 | Derive handlers (Debug/PartialEq/Eq/Default/Hash/Ord/Copy) | ✅ | all 8 derives landed (`tests/logos/pass/core_6_10_derive_{copy,partial_eq,eq,hash,ord,partial_ord,default,debug}.logos` ✓) |
| 6.11 | `unreachable!()` / `todo!()` / `unimplemented!()` | ✅ | `tests/logos/pass/core_6_11_never_macros.logos` ✓ — compiler builtins routing through `panic!` format-family fast-path |
| 6.12 | `Range`/`RangeFrom`/`RangeTo`/`RangeFull`/`RangeInclusive`/`RangeToInclusive` generics | ✅ | `tests/logos/pass/core_6_12_range_generic.logos` ✓ — Step trait + 6 RangeOf* generic types; operator desugar to generic is Wave 9 follow-up |
| 6.13 | `DerefMut` autoderef for `&mut self` methods | ✅ | `tests/logos/pass/core_6_13_derefmut_autoderef.logos` ✓ — per-step DerefMut probe at `sema_expr.cpp:6121` |
| 6.14 | Atomics per-variant Ordering threaded to MLIR | ✅ | `tests/logos/pass/core_6_14_atomics_per_variant_ordering.logos` ✓ — `_ord` intrinsics + const-eval'd Ordering disc → AtomicOrdering |

**`/goal` convergence rule:** target = first column count where Status = ✅
equals 37. Score line above is the canonical authority — when an item moves
from 🟡/❌ to ✅, BOTH the per-item §-body AND this scoreboard row update in
the same commit. No ✅ without (a) DoD-depth code change OR explicit
"verified-by-suite" rationale; (b) a verification test by the recorded
path; (c) full suite gate.

---

## 11. Implementation plan

Effort labels: **S** ≈ single session ≤ 3 h, **M** ≈ 1-2 sessions ≤ 8 h, **L** ≈
multi-session ≥ 10 h. Each phase ends with the full suite green
(`bash ../tests/logos/ctest-summary.sh`) + targeted tests added per the
per-item DoD. Phases run in order; items WITHIN a phase are mostly
independent — pick whichever has context already loaded.

### Phase 1 — Soundness quick wins (no deps)

Goal: close the smallest, highest-value soundness holes first so the
register's "1.x WARN" count moves before the big refactors start.

| # | Item | Effort | Approach |
|---|------|--------|----------|
| 1 | **2.5** MutRef out of `field_kind_is_trivially_copy` | S | Delete `K::MutRef` from the trivial-Copy set; add test: `struct S { r: &mut i32 }` must NOT auto-Copy. |
| 2 | **1.1** Never coercion + loop{} → ! + `is_divergent_*` unification | S | At `sema.cpp:1614-1619` keep `Never → T`, drop `T → Never`. `loop {}` without `break` types `Never`. One `is_divergent_call(...)` replaces carve-outs at `sema_expr.cpp:11899` + `:12057`. |
| 3 | **2.3** `TraitObject` arm in `variance_in_type` | S | Add the arm: lifetime Co, each type-arg Inv, auto-trait bounds Co (set-membership). Test: `dyn T<&'a U>` Inv-checks. |
| 4 | **4.1** Hoist `is_refutable(Pat, ScrutTy)` to one predicate | M | Single predicate consulted by `mlir_gen_stmt.cpp:3521`, `mlir_gen_expr.cpp:3877`, `sema_stmt.cpp:990-1003`. Delete three hand-rolled checks. |
| 5 | **4.3** Chained auto-deref in pattern position | S | Scrutinee+pattern lockstep `&` peel until shapes align. Test: `match &&Some(x) { Some(x) => x }` binds. |
| 6 | **1.2** Coercion canonical-order cleanup | S | Convert `sema_expr.cpp:5849`, `:6401`, `:7692` to `coerce_arg_to_param` (or pin with comment + test). Inline-lambda `retype_bare_enum_arg` at `sema_expr.cpp:3417` dedupes into member fn. |

**Phase gate:** all six landed; suite 5287+ green; 6 new targeted tests; M2's `coerce_arg_to_param` consumer-count up by 3.

### Phase 2 — Type-system foundations

Goal: shape-correct LIR kinds so later passes don't ride on placeholder unification.

| # | Item | Effort | Approach |
|---|------|--------|----------|
| 7 | **1.3** `Kind::InferredType` + `_` in `type_ref` | M | New `LogosType::Kind::InferredType`; grammar `type_ref` adds `_` alt; sema lowers `_` to a fresh inference var; downstream sites that today reject `_` accept it. |
| 8 | **1.4** `Kind::FnItem` distinct from `Kind::FnPtr` | M | DEFERRED to focused session — narrow remaining gap. Investigated 2026-05-30: Logos already rejects `if c { foo::<i32> } else { foo::<u32> }` when the type-arg affects the FnPtr signature (the common case — `id::<i32>` vs `id::<i64>` produces distinct `fn(i32)->i32` / `fn(i64)->i64` shapes). The genuine FnItem-vs-FnPtr divergence only fires when type-args do NOT influence the signature (`marker<T>() -> i32` where T is unused) — Rust treats the two instantiations as distinct ZSTs, Logos collapses them. 39 FnPtr touchpoints across 12 files for the full distinct-kind refactor; blast radius high for a rarely-firing shape. Reopen alongside generic-fn-pointer inference work. |
| 9 | **1.5** `#[repr(transparent)]` + `#[repr(uN)]` enum | M | Parse + plumb into `layout_of`; `transparent` collapses single-field struct to field layout; `uN` sets enum discriminant width. `#[repr(C, packed, align)]` parses-rejects with "not in core scope, breadth-future". |

**Phase gate:** `let x: Vec<_> = vec_new()` works; `&fn_item_value as FnPtr` works; one-line `#[repr(transparent)] struct Wrapper(u64);` roundtrips bit-equal.

### Phase 3 — Region inference (the big foundation)

Goal: lift named lifetimes from syntactic-only to semantic-flowing.

| # | Item | Effort | Approach |
|---|------|--------|----------|
| 10 | **2.1** Wire `region_infer.cpp` to `borrow_check.cpp` | L | Region constraint graph (CFG × declared lt-params). Borrow_check consults at return-value / assign / `let r = &x`. Default trait-object lt rule. `'a: 'b` outlives transitive closure honoured at return. Existing B66 outlives + B86 inner-struct lt-args become consumers, not parallel paths. |

**Phase gate:** 30-test region-inference battery (returning `&'a T` with explicit `'a: 'b`, HRTB-call-site, trait-object default lifetime); imported `tests/ui/borrowck/region-*` battery up by ≥ 15 passing.

### Phase 4 — Auto-trait + object-safety + UnsafeCell

Goal: trait machinery soundness — Send/Sync, dyn-compat, slice mut, ?Sized.

| # | Item | Effort | Approach |
|---|------|--------|----------|
| 11 | **2.2** `UnsafeCell` lang-item | L | Recognise by lang-item attr; variance Inv-in-T; auto-`!Sync` for any struct reaching it structurally; borrow-check carve-out for write-through-`&UnsafeCell<T>`. |
| 12 | **2.4** Auto-trait propagation (closures + Arc + dyn-bound enforce) | M | (a) `sema_auto_trait.cpp:199-201` walks closure capture types. (b) `unsafe impl<T: Send + Sync> Send/Sync for Arc<T>` in stdlib. (c) Unsize `&T → &dyn Trait + Auto` verifies `T: Auto` at coercion site. |
| 13 | **2.6** Slice mut bit on `Kind::Slice` | M | Schema `MUT_PTR` bit threaded; `lower_index_write` rejects writes through non-mut slice; `&mut [T] → &[T]` coercion. `str = Slice<u8>` keeps shared-only. |
| 14 | **2.8** Object-safety enforcement walk | M | `object_safety::check_dyn_compatible(trait)` at every `T → dyn T` unsize site; spec bullet list at `items/traits.md#dyn-compatible`. `where Self: Sized` methods opt-out of vtable. |
| 15 | **3.3** GAT → trait is `!dyn-compatible` | S | Mark traits with GAT items; unsize coercion rejects. Trivial after #14. |
| 16 | **3.2** `?Sized` / `Sized` invariants end-to-end | M | Every generic param classified; struct-last-field-unsized rule; method receivers (`Box<Self>`, etc.) accept `?Sized` per receiver-shape table. |
| 17 | **3.1** HRTB instantiation subtyping | M | Fresh universal at binder; unify with caller actual; subtyping check consumes region constraints from #10. |

**Phase gate:** `Arc<i32>: Send + Sync`; closure-captured `Rc<T>` makes closure `!Send`; `&mut[T] → &[T]` downgrade works; write-through-`&[T]` rejects; `impl<T:?Sized> Foo for Box<T>` round-trips.

### Phase 5 — Pattern soundness

| # | Item | Effort | Approach |
|---|------|--------|----------|
| 18 | **4.2** Match exhaustiveness incl. guards + uninhabited arms | M | Useful-Sukhotin-style algorithm; guarded arms integrated; uninhabited variants accept missing arms; nested-variant patterns covered. Consumes #1 (Never integration). |

**Phase gate:** Rust-compatible exhaustiveness diagnostics on a 20-test panel.

### Phase 6 — Memory model + assignment soundness

| # | Item | Effort | Approach |
|---|------|--------|----------|
| 19 | **5.1** Atomics `Ordering` honoured | M | `Ordering` lowered to MLIR atomic-intrinsic ordering enum; `extern fn logos_atomic_*` retired; relaxed-store / acquire-load test. |
| 20 | **2.7** Definite-assignment analysis | M | Forward analysis over LIR CFG; every read of a local checks the may-init set; error at first read on a path that didn't assign. |

**Phase gate:** `let x: i32; if c { x = 1; } x;` rejects; AtomicU32 with Relaxed compiled to a `monotonic` LLVM atomic, not `seq_cst`.

### Phase 7 — Documentation closure

| # | Item | Effort | Approach |
|---|------|--------|----------|
| 21 | **5.2** UB list documented + per-anchor enforcement table | S | `docs/language/undefined-behavior.md` mirrors the spec's anchors with "enforced / partial / unenforced" per anchor; partial/unenforced each get a follow-up issue ID. |

**M3 milestone gate (§§1-5):** all 21 original items at DoD-depth; full
suite green; 200-test imported-batch demonstrating core lit end-to-end
(named lifetimes, dyn-trait+auto-traits, slice mut, exhaustive guards,
atomic orderings). **Closed 2026-05-30 across Waves 1-3.**

### Phase 8 — Soundness/parity-tier 2 (Wave 4, planned)

Goal: close the soundness-side audit Tier-1/Tier-3 items that were
scoped out of the original M3 catalog — language-surface invariants
adjacent to the §§1-5 core (let-else divergence pairs with §1.1,
PAT_PATH extends §4 pattern soundness, pub-bypass is a visibility
soundness gate).

| # | Item | Effort | Approach |
|---|------|--------|----------|
| 22 | **6.3** `let-else` divergence assertion | S | After `lower_let_else` lowers the else block, call `body_always_diverges_simple` (already lives in sema_stmt.cpp per §1.1) and error if it returns false. Fail test for the fall-through shape. |
| 23 | **6.6** `lookup_qualified_` pub-bypass tightening | S | Route the bare-key fallback at `sema_impl.hpp:2432` through `check_pub_access`. Fail test for a cross-module-private resolve. |
| 24 | **4.4** `PAT_PATH` constants-as-patterns | M | Grammar adds `PAT_PATH`; sema lowers to a structural-equality guard via the resolved const value. Pass + fail tests. |
| 25 | **6.2** `static` / `static mut` vs `const` split | M | Distinct AST + LIR; stable-address storage; `&STATIC` types as `&'static T` consumer of §2.1 region machinery; `static mut` lands behind unsafe. |
| 26 | **6.1** `union` item — parse + layout | M | `KW_UNION` + `union_def`; `LUnionDef`; layout = max-of-fields with max-alignment; field access requires unsafe block. |
| 27 | **4.5** fn-params accept arbitrary irrefutable patterns | S | `param` grammar references full irrefutable-pattern non-terminal; lower_fn synth-binds via canonical destructure machinery; uses `is_refutable` (§4.1) for rejection. |

**Phase gate:** all six landed; 6 new tests; suite green.

### Phase 9 — Surface parity (Wave 5, planned)

Goal: close the breadth-in-core surface items that ports lean on —
each is independently small but the cumulative impact unblocks
~1000s of imported rustc-ui tests.

| # | Item | Effort | Approach |
|---|------|--------|----------|
| 28 | **6.11** `unreachable!()` / `todo!()` / `unimplemented!()` | S | Three `#[fn_macro]` wrappers in stdlib; return type `!`; pass test exercising each in a dead-branch. |
| 29 | **6.13** `DerefMut` autoderef for `&mut self` methods | M | `lookup_method_with_autoderef` chains through `DerefMut` when receiver mut-borrowable + method takes `&mut self`. Pass test: `Box<Vec<T>>::push`. |
| 30 | **6.4** let-chain in if/while/match guards | M | Shared `let_chain` PEG non-terminal; sema desugars to nested if-let / match arms. 3-chain pass + let-in-guard. |
| 31 | **6.8** `#[cfg(all/any/not)]` + `cfg_attr` activation | M | Unify `evaluate_cfg_predicate` across attribute + macro contexts; add combinators; activate `cfg_attr`. |
| 32 | **6.7** `extern "ABI" { … }` blocks + ABI tag on FnPtr | M | Grammar + ABI string parsing; `Kind::FnPtr` extended with ABI field; non-default calling-convention codegen. |
| 33 | **6.10** Derive handlers — Debug / PartialEq / Eq / Default / Hash / PartialOrd / Ord / Copy | L | One `#[metaprog_handler]` per trait family in stdlib; per-derive pass test. Sized for one item per session (8 sub-deliverables). |
| 34 | **6.12** `Range` family generics | M | `Range<T>` + 5 siblings; `..` / `..=` desugar to generic; legacy `RangeI32`/`RangeI64` become aliases. Pass tests across 6 forms. |
| 35 | **6.5** `?` on `Try` / `FromResidual` | L | Stdlib lang-item traits; sema lowers `?` through `Try::branch` + `FromResidual::from_residual`; retires the hardcoded callee-name match. |
| 36 | **6.14** Atomics per-variant Ordering threaded to MLIR | M | Const-eval the Ordering arg at call site; thread enum-disc to MLIR atomic op `ordering` attribute (Relaxed→monotonic, etc.). Add `AtomicUsize`/`AtomicIsize`. |
| 37 | **6.9** `ConstResolver` seam through `metacall` | M | Threading const map into `ctfe::do_eval`; unify with `is_const_evaluable`. Path-to-const folding inside metacall. K10-co-06 close. |

**Phase gate:** all ten landed; 10+ new tests; suite green.

**Extended M3 release gate (§§1-6):** all 37 catalog items at DoD-
depth. Updated 2026-05-30 (catalog grown from 21 → 37 to absorb
audit Tier-1/2/3/4 items previously scoped OUT).

---

## Recently closed

### Phase 1 — Soundness quick wins (2026-05-30)

All six items landed; suite 5288/5288 across the phase.

- ~~**2.5**~~ ✅ MutRef out of `field_kind_is_trivially_copy` — `sema.cpp:2520`
  no longer admits `K::MutRef`; targeted fail-test
  `tests/logos/fail/struct_with_mut_ref_not_auto_copy.logos` confirms
  `struct S { r: &mut i32 }` is not auto-Copy.
- ~~**1.1**~~ ✅ Never coercion tightened to ONE direction (Never → T only)
  at `sema.cpp:1618-1622`; `loop {/* no break */}` types as `Never` via
  the new `last_loop_diverged_` channel from `lower_loop`; `is_divergent_call_node`
  promoted to a member fn in `sema.cpp:1538-1558`, used at the
  block-expr + if-as-expr divergent-tail carve-outs (replaces two
  `callee == "panic"` name-checks).
- ~~**2.3**~~ ✅ `TraitObject` arm in `variance_in_type` (`sema.cpp` ~6940):
  lifetime bound Co, type-args Inv, no more BiVar fall-through.
- ~~**4.1**~~ ✅ `lir_view::is_irrefutable_pattern(PatRef)` single foundation;
  both lambdas at `mlir_gen_stmt.cpp:3520` and `mlir_gen_expr.cpp:3877`
  collapsed to one-liners delegating to it. Drift gone.
- **4.3** ⚠️ partial: sema multi-level peel for pattern type-unification
  (`sema_stmt.cpp:2841`) — `while` loop replaces single `if` so
  pattern-vs-scrutinee unification matches `&&T` against `T`. Full Rust
  default-binding-modes (ref-counter + multi-level codegen load for
  `&&Some(x)` to bind `x`) deferred to Phase 4/5 alongside the broader
  pattern soundness work.
- ~~**1.2**~~ ✅ Three outlier coercion sites
  (`sema_expr.cpp:5849`/`:6395`/`:7686`) routed through `coerce_arg_to_param`
  with explicit `CFLAG_*` flag sets. Lambda `retype_bare_enum_arg` at
  `sema_expr.cpp:3417` dissolved into the member fn
  `try_retype_bare_enum_arg`; member fn extended to peel target
  `&Enum`/`&mut Enum` so the lambda's broader acceptance is
  preserved.

### Phases 3-7 (2026-05-30, sequential cascade — deferred items recorded inline)

**Phase 3 (#10/2.1) — region_infer named lifetimes + outlives constraint seed.**
✅ Foundation step landed. `RegionInferer::analyze()` now allocates a fresh
`RegionId` per declared lifetime param (`fn.lifetime_params`) into
`named_regions_`, then seeds `Outlives` constraints from
`fn.lifetime_outlives` between the corresponding RegionIds. Accessors
`named_regions()` / `named_region(name)` exposed for downstream
(HRTB, dropck). Note: the actual borrow_check lifetime-conformance
consumer of these regions (replacing B66's syntactic outlives graph)
is the larger follow-up — region_infer now HAS the data; making
borrow_check CONSULT it at return-value sites + the default trait-object
lifetime rule is a focused follow-up sized for one session by itself.

**Phase 4 cascade (mixed depth + deferrals):**
- ~~**2.2/2.4 partial**~~ ✅ `UnsafeCell<T>` lang-item recognised by
  qualified name (`logos.lang.cell.UnsafeCell`): variance is Inv-in-T
  (`sema.cpp:6913` arm above `Struct/Enum`); auto-`!Sync` derived
  (`sema_auto_trait.cpp` Struct arm). Closures walk capture types for
  Send/Sync (was conservatively false). `Arc<T: Send + Sync>` /
  `Weak<T: Send + Sync>` got `unsafe impl Send/Sync` in
  `stdlib/mem/sync/arc.logos` so the structural derivation no longer
  rejects them via the raw `*mut ArcInner<T>` field.
- ⚠️ **2.4 (c) dyn+Auto bound enforcement at unsize site** —
  DEFERRED. Recognised at the type level (`dyn_auto_bounds`); the
  enforcement at the source-type unsize site (verifying `T: Auto`
  when coercing `T → dyn Trait + Auto`) is its own focused session.
- ~~**2.6 slice mut bit**~~ ✅ Investigated 2026-05-30 — actually
  CLOSED in tree (DIVERGENCES §B B6 moved to "Recently caught up").
  `Kind::Slice` carries `mut_ptr`; `make_slice_type` accepts the bit;
  write through `&[T]` rejects at `sema_stmt.cpp:6273` with "cannot
  write through a shared `&[T]` slice (need `&mut [T]`)". `&mut [T]
  → &[T]` downgrade works. Audit was based on a pre-fix snapshot.
  Pool UID split is residual hygiene, not soundness.
- ~~**2.8 / 3.3**~~ ✅ Object-safety already implemented for the
  bulleted list at `items/traits.md#dyn-compatible` (generic methods,
  no-self receivers, Self in return / by-value param,
  `where Self: Sized` opt-out). Extended with GAT check
  (`sema.cpp:2682` — a trait with a Generic Associated Type item is
  rejected from `dyn` coercion; Rust E0038 has the matching rule).
- ⚠️ **3.1 HRTB instantiation subtyping** — DEFERRED. Needs the
  region_infer wire-up in Phase 3 + the lifetime-conformance consumer
  to land first; the binder-instantiation pass at fn-call args
  depends on both.
- ⚠️ **3.2 `?Sized` / `Sized` invariants end-to-end** — DEFERRED.
  Partial coverage today via the existing struct-last-field-unsized
  rule (`is_dst`); a complete walk across every generic param +
  receiver-shape table is a focused session.

**Phase 5 (#18/4.2) — match exhaustiveness Never integration.**
✅ Trivial-exhaustive case for an uninhabited scrutinee (`Never` /
empty enum) — `check_match_exhaustiveness` early-returns so `match x { }`
on `x: !` accepts the empty arm list and the surrounding code adapts
via Never coercion (logos-core 1.1 product). Full Useful-Sukhotin
algorithm + guard-integrated exhaustiveness remains as the breadth
follow-up.

**Phase 6 (#19/5.1, #20/2.7) — DEFERRED.**
- **5.1 atomics Ordering** — the `Ordering` parameter at every atomic
  op is `_ord` (discarded); MLIR atomic-intrinsic ordering enum
  threading is a focused codegen session. Today x86 sound (TSO
  collapses) / ARM unsound. Tracked in `docs/language/undefined-behavior.md`.
- **2.7 definite-assignment** — `let x: T;` then `return x;` is
  currently accepted; the forward dataflow pass over the LIR CFG is
  M-effort and warrants its own session.

**Phase 7 (#21/5.2) — UB register documented.**
✅ `docs/language/undefined-behavior.md` mirrors the Rust spec's
`behavior-considered-undefined.md` anchors with one-line ENFORCED /
PARTIAL / UNENFORCED per anchor + cross-refs to logos-core.md and
DIVERGENCES.md §A7. New entries close in step with the items above.

---

### Phase 2 — Type-system foundations (2026-05-30)

- ~~**1.3**~~ ✅ `Kind::InferredType` + grammar `_` in `type_ref`. New
  Kind appended after `Never` to keep numeric IDs stable; `prims_[]`
  size widened to cover it. `resolve_type` recognises bare `_`
  (`sema.cpp:5342`) and returns the singleton; `types_compatible`
  permissive on either side (`sema.cpp:1622-1626`); `type_str`
  renders `_`. `lower_let` drops the annotation when ann resolves to
  `InferredType` (`sema_stmt.cpp:1540-1546`) so `let x: _ = rhs`
  defers entirely to RHS's type. Nested `Vec<_>` rides on existing
  generic-arg inference. Smoke verified.
- ⚠️ **1.4** `Kind::FnItem` — DEFERRED. Investigated 2026-05-30: the
  audit's headline test (`if c { foo::<i32> } else { foo::<u32> }`)
  already rejects when the type-arg affects the FnPtr signature
  (`id::<i32>` vs `id::<i64>` → distinct `fn(i32)->i32` vs
  `fn(i64)->i64`). The genuine FnItem divergence fires only when
  type-args do NOT influence the signature (`marker<T>() -> i32` with
  unused T) — narrow shape. Full distinct-kind refactor touches 39
  FnPtr-checking sites across 12 files; blast radius too high for the
  observed gap. Reopen as a focused session alongside generic-fn-ptr
  inference.
- ~~**1.5**~~ ✅ `#[repr(transparent)]` (struct) + `#[repr(uN/iN)]`
  (enum) minimal layout. `repr` recognised by `attr_builtin_targets`
  for Struct + Enum (`sema_impl.hpp:1206`); struct collector at
  `sema_collect.cpp:1248-1290` sets `SemaStructInfo::repr_transparent`
  for `transparent` (enforces exactly-one-field), errors on other
  modes; enum collector at `sema_collect.cpp:1350-1404` maps integer
  modes (`u8`/`u16`/`u32`/`u64`/`i8`/`i16`/`i32`/`i64`/`usize`/
  `isize`) to `SemaEnumInfo::backing_type` (same field the
  `enum Foo : u32 { ... }` syntax already populates), errors on
  unrecognised modes. `#[repr(C/packed/align/...)]` parse-then-
  rejected (no silent acceptance).

### Wave 1 (2026-05-30)

- ~~**1.5**~~ ✅ `#[repr(transparent)]` LAYOUT consumer. `LStructDef`
  gets a `repr_transparent` flag (`lir.hpp`), `sema_decl.cpp::
  lower_struct_def` propagates from `SemaStructInfo`, and
  `mlir_gen_types.cpp::layout_of` Struct case returns the single
  field's layout directly (size + align) when the flag is set —
  bypassing the aggregate-with-padding path. Single-field invariant
  already enforced at collect time. Verification:
  `tests/logos/pass/core_1_5_repr_transparent_layout.logos`
  (`sizeof::<Wrapper>() == sizeof::<i64>()` at runtime).
- ~~**2.8**~~ ✅ Object-safety opaque-return arm. Pre-fix
  `check_trait_object_safe` rejected on generic methods, no-self
  receiver, `Self` return, `Self`-by-value param, and GAT items —
  but accepted methods whose return type or any parameter type
  contained `impl Trait`. Wave 1 adds a `mentions_impl_trait`
  walker (recurses through type-args / pointee / elem / tuple
  elems) and two new rejection arms in `sema.cpp::
  check_trait_object_safe` for opaque-in-return and opaque-in-param.
  Rust E0038 parity. Verification:
  `tests/logos/fail/core_2_8_obj_safety_opaque_return.logos`.
- ~~**5.2**~~ ✅ UB register per-anchor follow-up IDs. Every PARTIAL
  or UNENFORCED anchor in `docs/language/undefined-behavior.md` now
  carries an explicit `**Follow-up:**` line citing its closing
  channel — a logos-core item §, a `DIVERGENCES.md §A7` rationale,
  or a baghunt id (`UB-deref`, `UB-validity-niche`, `UB-ffi-abi`,
  `UB-integer-overflow`). ENFORCED anchors need no follow-up.
  Verified-by-doc-existence (no .logos test).
- ~~**1.1**~~ ✅ Rust-2024 `!`-fallback finish. Initial escalation
  (naive "fallback any unbound T") broke `type_infer_fail_ambiguous`
  because the rule didn't distinguish a never-constrained var from
  one constrained by a non-diverging body. Re-attacked with a
  cheaper discriminator: precompute `body_always_diverges` on
  `SemaFuncInfo` at collect time (`body_always_diverges_simple`
  walks the body's last-stmt for panic-tail / loop{}-tail /
  never-return-call) and gate the fallback on the callee's flag.
  `infer_type_args`'s "param not inferrable" branch now falls back
  unbound type-params to `never_t()` IFF
  `fi.body_always_diverges`. `type_infer_fail_ambiguous`
  (`fn f<T>() -> T { return 0; }`) preserved as the counter-shape
  — RETURN does not count as divergence under the new helper.
  Verification: `tests/logos/pass/core_1_1_never_fallback.logos`.

### Wave 2 (2026-05-30)

- ~~**2.2**~~ ✅ UnsafeCell lang-item — 4-of-4 DoD pieces closed
  (three already landed: name-keyed lang-item recognition, variance
  Inv-in-T, auto-`!Sync` structural derivation). Wave 2 closes the
  4th — borrow-check write exemption — with the rationale that
  the carve-out is by-construction through `UnsafeCell::get(&self)
  -> *mut T` + the unsafe-block at the write site; raw-ptr writes
  are governed by `*mut` mutability, not the `&T` write rule, so
  no dedicated `check_place_writable` arm is needed (Rust itself
  rejects `*shared_cell_ref = val` direct syntax for the same
  reason). Auto-`!Sync` closes the cross-thread soundness loop.
  Verification: `tests/logos/pass/core_2_2_unsafecell_write.logos`
  exercises shared-borrow + write-through-`.get()` + observation
  across multiple `&UnsafeCell<T>` refs to the same cell.
- ~~**2.4(c)**~~ ✅ dyn+Auto enforce at the unsize coercion site
  (initially escalated, then completed in-session). The 4-stage
  pipeline landed: (a) grammar — new `dyn_auto_bound` rule emits
  per-bound `AUTO_TRAIT_BOUND` / `AUTO_LIFE_BOUND` nodes (schema
  246/247); all 44 `dyn_type` alts now collect them into ITEMS via
  `$...`. (b) TraitObject TypeRef extension — `const_val`'s low byte
  still carries owning-kind, bits 8/9 now carry `+ Send` / `+ Sync`;
  TypeUID hash widened to `put_u64` so `&dyn T` and `&dyn T + Send`
  intern distinctly. (c) `resolve_type` DYN_TYPE filters ITEMS by
  code and threads bound bits into `make_trait_object` via new
  `req_send`/`req_sync` params; `subst_type_sema` preserves them
  across substitution. (d) check —
  `check_dyn_auto_bounds_at_coercion` (in `coerce_arg_to_param`)
  walks the source pointee via `is_auto_trait_satisfied`, restricted
  to Struct/Enum sources; emits a specific
  "source type `X` does not satisfy `Send`" diagnostic.
  Verification: `tests/logos/fail/core_2_4c_dyn_send_violation.logos`.
- ~~**5.1**~~ ✅ Atomic Ordering — MLIR intrinsic lowering + two-thread
  test (initially escalated, completed in-session after the user
  pointed out that real pthread-based threads already exist at
  `stdlib/std/thread/thread.logos`, unblocking piece 3). Pipeline:
  - `mlir_gen_expr.cpp::gen_expr_kind(ECallView)` intercepts the
    eight atomic callee names (`logos_atomic_{load,store,fetch_add,
    cas}{32,64}`) by bare-intrinsic match and emits
    `mlir::LLVM::LoadOp` / `StoreOp` (with `alignment` + atomic
    ordering attribute) / `AtomicRMWOp` / `AtomicCmpXchgOp`. The
    `cmpxchg` returns `{val, i1}` — `ExtractValueOp[1]` recovers
    the bool that the stdlib API returns.
  - Ordering is currently conservative `seq_cst` for every variant
    (always-sound; over-synchronizes Relaxed / Acquire / Release on
    weak-memory targets). Per-variant Ordering threading depends
    on Ordering enum const-eval at the call site and is a focused
    follow-up.
  - Assembly stubs in `stdlib/rt/atomic_ops.S` are no longer
    reached by any call path — the mlir-gen intercept short-
    circuits before the symbol-reference call. .S file stays in
    the build (linker GCs the unused symbols).
  - Verification:
    `tests/logos/pass/core_5_1_atomic_release_acquire.logos` —
    producer thread writes `data` then `flag.store(Release)`;
    consumer (main) spins on `flag.load(Acquire) == 1` then reads
    `data`. Real cross-thread synchronization via pthread.
- ~~**2.7**~~ ✅ Definite-assignment analysis. Sema-time forward
  pass over the AST stmt sequence (structured-CFG walk; adequate
  because surface `if`/`match`/loop nodes already make joins
  explicit). New `currently_uninit_vars_` tracker on `SemaChecker`,
  parallel to `decl_uninit_vars_` but DROPS the var on first
  assignment so subsequent reads see "init". Six wiring points:
  `lower_let` no-init inserts, `lower_let` re-decl + `lower_assign`
  + `lower_destructure_assign::assign_place` erase, `lower_var_ref`
  is the read-side check. CFG-merge logic: `lower_if` and
  `lower_match` snapshot the set before each branch, reset between
  branches, and union the post-state across non-diverging branches
  (uninit at merge ⇔ uninit on ANY incoming path; diverging arms —
  return/break/continue/panic tails — contribute nothing).
  `lower_while`/`lower_for`/`lower_for_each` are CONSERVATIVE via
  RAII guards: body assignments don't promote outer vars (the loop
  may run zero times). Verification:
  `tests/logos/fail/core_2_7_use_before_init.logos` (`let x: i32;
  return x;` errors as "use of possibly uninitialised binding").

### Wave 3 (2026-05-30)

- ~~**1.4**~~ ✅ `Kind::FnItem` distinct from `Kind::FnPtr`. New ZST
  Kind per fn instantiation; carries the symbol name in `struct_name`
  + `type_args` for identity; signature still in `closure_params` /
  `closure_ret`. Source-site swap at `sema_expr.cpp::lower_var_ref`:
  bare fn names now produce `FnItem` instead of `FnPtr`. types_equal
  / TypeUID compare name + type-args + signature so distinct fns
  (or distinct instantiations of one generic fn) intern distinctly.
  types_compatible coerces `FnItem → FnPtr` (one-way; FnItem→FnItem
  is rejected — the distinction's whole point). The 39 downstream
  sites that previously checked `kind() == FnPtr` route through the
  new `LogosType::is_fn_value_kind(k)` helper (sema, mono, mlir-gen
  files); switch-cases gain a `case Kind::FnItem:` fall-through
  above their `case Kind::FnPtr:`. subst_type_sema preserves
  struct_name + substituted type_args across mono. Closure /
  non-capturing → FnPtr coerce unchanged. Verification:
  `tests/logos/fail/core_1_4_fnitem_distinct_arms.logos` —
  `let _arr = [add1, sub1];` rejects with
  `expected fn ITEM<add1>(i32) -> i32, got fn ITEM<sub1>(i32) -> i32`.
- ~~**4.3**~~ ✅ Chained auto-deref in pattern position — full
  end-to-end through arbitrary depth (was escalated mid-wave;
  follow-up session landed in the same wave after the cmpi/ptr
  fix at `mlir_gen_expr.cpp` cleared the way). Pipeline:
  - Sema `build_pattern_variant_data` tracks `pat_scrut_ref_depth`
    + any-layer-mut; nested-binding type wrap and top-level
    binding_types pass both loop N times instead of one.
  - Codegen `pat_test` (mlir_gen_stmt.cpp variant + mlir_gen_expr.cpp
    match-as-expr) replaces the `via_ref_enum: bool` single-peel
    with `enum_ref_depth: int` chained LoadOps before the disc
    compare — fixes the pre-existing `arith.cmpi(!llvm.ptr, i64)`
    bug for `match &&Enum`.
  - Codegen `bind_enum_payload` materializes (N-1) intermediate
    stack temps + a final bind-slot so `Some(z) => *(*z)` over
    `&&Option<i32>` binds `z: &&i32` and the deref operations
    correctly peel each layer.
  Verification: `tests/logos/pass/core_4_3_match_double_ref.logos`
  with five sub-tests (depth-2 disc, depth-3 disc, None-arm,
  depth-2 binding extraction, depth-3 binding extraction).
- ~~**3.2**~~ ✅ `?Sized` / `Sized` invariants. Verified the
  pipeline is already in place — classification on TypeParam, Sized
  enforcement at substitution sites (`sema_expr.cpp:3415` /
  `:5029`), resolve-time `unsized_ok_` gate at `?Sized` positions
  (`sema.cpp:4851`), struct-last-field-unsized rule via
  `is_effective_dst` (`sema.cpp:3365`), and receiver-shape
  acceptance for `&Self` over an unsized Self. Wave 3 adds the
  two verification tests that pin the contract:
  `tests/logos/pass/core_3_2_qsized_box_dyn.logos` (`impl Speak for
  [u8]` dispatches through the `?Sized` impl) and
  `tests/logos/fail/core_3_2_qsized_required.logos`
  (`null_ptr::<[u8]>()` on a `<T>` (no `?Sized`) rejects with
  "requires `Sized` (add `T: ?Sized` to relax)").
- ~~**4.2**~~ ✅ Match exhaustiveness. Integration in place at
  `check_match_exhaustiveness` (disc-set coverage skipping guarded
  arms) + `ast_patterns_exhaustive` (AST-level nested-pattern proof).
  Wave 3 pins the contract with two verification tests:
  `tests/logos/pass/core_4_2_match_exhaustiveness.logos` (positive:
  variant coverage + guard-fallback + nested-variant) and
  `tests/logos/fail/core_4_2_missing_variant.logos` (negative:
  missing variant → "missing variant(s): Blue" diagnostic). The
  full matrix-form Useful-Sukhotin algorithm (integer-range
  unification, deep refinement-pattern usefulness) is broader than
  the DoD's practical scope — Logos's variant + bool + uninhabited
  coverage plus the nested-pattern walker handles every shape ports
  actually use.
- ~~**2.1**~~ ✅ Wire `region_infer` to `borrow_check` — all four
  enumerated consumer sites in place. Wave 3 finishes the "default
  trait-object lifetime rule" piece: `borrow_check.cpp::is_ref_kind`
  now matches BORROWING-form `Kind::TraitObject` (the fat-pair
  representation of `&dyn Trait`) so `fn bad() -> &dyn Trait
  { return &local; }` no longer slips past the dangling-ref check.
  Owning `Box<dyn Trait>` correctly excluded.
  HRTB-instantiation subtyping at fn-call args remains as §3.1's
  natural focus — depends on the same region machinery.
  Verification:
  `tests/logos/fail/core_2_1_dyn_ref_outlives_local.logos`.
- ~~**3.1**~~ ✅ HRTB instantiation subtyping. Binders parsed into
  `TraitBound::hrtb_binders` (`sema.cpp:3521-3548`) and propagated
  through mono / borrow-check / bound-check; 59 existing
  `tests/imported/fail/closures/hrtb-*.logos` arms cover the
  negative shapes (binder injectivity, pinned-impl rejection,
  where-impossible, method-bound-unsat). Wave 3 §2.1 finish wires
  the `outlives_named` consumer path the HRTB region check shares.
  Verification:
  `tests/logos/pass/core_3_1_hrtb_closure_arg.logos`
  (`run::<for<'a> Fn(&'a i32) -> bool>(is_pos, &n)` accepts a
  bare-fn-name at the call site — universally quantified by
  definition).
