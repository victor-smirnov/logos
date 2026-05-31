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

### 1.2. Coercion pipeline canonical order
*Audit: B (Coercions), partially landed via M2's `coerce_arg_to_param`.*
- **Issue:** three sites still run a non-canonical order
  (`sema_expr.cpp:7692` widen-first, `:5849`/`:6401` `arg_to_dyn`-after-widen).
  Local `retype_bare_enum_arg` lambda at `sema_expr.cpp:3417` duplicates
  the member fn.
- **Why core:** coercion is the chokepoint at every arg/return/let; an
  ordering quirk reproduces silently in every downstream check.
- **DoD-depth:** all coercion sites route through `coerce_arg_to_param`
  with explicit flags; outliers either dissolve into the canonical
  order (suite-gated) or are documented as load-bearing with a test that
  pins the order. Lambda duplicate eliminated.

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

### 2.3. Variance over trait objects
*Audit: A (Variance), B (TraitObject).*
- **Issue:** `variance_in_type` has no `TraitObject` arm; `dyn Trait<T>`
  falls through to BiVar (bivariant — accepts both directions). Should be
  Co-variant in `'a`, Invariant in each type-arg.
- **Why core:** variance correctness is a soundness property; BiVar is
  pretty much always wrong.
- **DoD-depth:** explicit `TraitObject` arm; type-args invariant; bound
  lifetime covariant; auto-trait bounds Co (they're set-membership, not
  type identity).

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

### 2.5. `&mut T` no longer auto-promotes Copy structs
*Audit: A (Copy).*
- **Issue:** `field_kind_is_trivially_copy` admits `K::MutRef` (the
  Copy-auto check thinks `&mut` is Copy because it's a pointer). A struct
  holding `&mut T` thereby auto-promotes to `Copy`. After M2's
  `is_move_type` MutRef→true, this is the last drift point.
- **Why core:** Copy ⊥ Drop and Copy is a soundness gate.
- **DoD-depth:** `K::MutRef` removed from the Copy-trivial set; targeted
  test confirms `struct S { r: &mut T }` no longer auto-Copy.

### 2.6. Slice mutability tracked at the type level
*Audit: A (Borrow), B (Slice), audit recorded as B6 in DIVERGENCES (§B).*
- **Issue:** `&[T]` and `&mut [T]` canonicalise to a single `Kind::Slice`
  with no mut bit, so indexed write through `&[T]` is NOT rejected.
- **Why core:** soundness — write through a shared slice is the classic
  `&T → write` UB.
- **DoD-depth:** mut bit threaded through `Kind::Slice` (the schema's
  `MUT_PTR` field flips it); `lower_index_write` rejects writes through
  non-mut slice; `&mut [T] → &[T]` coerces (downgrade allowed); pool no
  longer aliases the two as one TypeRef. `str = Slice<u8>` keeps the
  shared-only invariant.

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

### 3.3. GAT compatibility with object-safety
*Audit: D (GATs).*
- **Issue:** GATs parse; object-safety check doesn't reject GAT-carrying
  traits from `dyn` coercion (would be unsound — GAT instantiation
  requires a concrete impl).
- **Why core:** soundness gate for trait objects.
- **DoD-depth:** GAT items mark their declaring trait `!dyn-compatible`;
  unsize coercion rejects.

---

## 4. Pattern matching soundness core

### 4.1. Single canonical refutability predicate
*Audit: F (Refutability), audit top-finding #6.*
- **Issue:** at least three sites encode their own irrefutability check
  (`mlir_gen_stmt.cpp:3521`, `mlir_gen_expr.cpp:3877`,
  `sema_stmt.cpp:990-1003`). `let`-destruct accepts a narrow hand-listed
  shape set; fn-params accept only `IDENT`/`mut IDENT`/`(pat, …)`.
- **Why core:** refutability is the soundness gate that distinguishes
  `let`-bind from `if let` / `match`. Drift here lets refutable patterns
  bind in irrefutable positions (reading uninit memory) or rejects
  irrefutable patterns in irrefutable positions (false negatives).
- **DoD-depth:** one `is_refutable(Pat, ScrutTy) -> bool` predicate
  consulted by every site (let, fn-params, if-let/while-let warning,
  match-shortcut). Hand-rolled checks deleted.

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

### 4.4. `PAT_PATH` — constants-as-patterns
*Audit: F (Patterns), audit Tier-3 #25.*
- **Issue:** `match x { CONST_NAME => ... }` over a const-bound
  identifier doesn't parse as a pattern-path today — the grammar
  only accepts `PAT_INT` / `PAT_BOOL` / `PAT_STR` literal forms and
  variant paths. Ported tests using `match status { OK => ..., FAIL
  => ... }` reroute to or-patterns of literals.
- **Why core:** structural-equality patterns on constants are a
  load-bearing match shape; pattern soundness requires that the
  pattern-equality machinery be the SAME path the rest of sema uses
  for `==` (Rust's `StructuralPartialEq` contract).
- **DoD-depth:** new `PAT_PATH` AST node (path → const lookup); sema
  emits a structural-equality guard via the const's typeck'd value;
  rejects non-`StructuralPartialEq`-shaped consts with a specific
  diagnostic ("only structural-equality types may appear in
  patterns"). Targeted pass + fail tests.

### 4.5. Fn-params accept arbitrary irrefutable patterns
*Audit: F (Patterns), C (Items), audit Tier-3 #23.*
- **Issue:** today only `IDENT` / `mut IDENT` / `(pat, …)` parse in
  fn-param position. Rust accepts any irrefutable pattern
  (`fn foo(Point { x, y }: Point)`, `fn bar([head, .., tail]: [i32; 4])`).
- **Why core:** pattern uniformity invariant — `let` and fn-param
  bindings should share the same accept-set (`is_refutable` ✓ today
  but the fn-param grammar restricts the surface).
- **DoD-depth:** `param` grammar rule references the full irrefutable-
  pattern non-terminal; `lower_fn` synth-binds via the canonical
  pattern-destructure machinery; refutable-pattern rejection at the
  fn boundary cites the `is_refutable` predicate. Pass test for
  struct + tuple-struct + slice patterns; fail test for the
  refutable `fn(Some(x): Option<i32>)` shape.

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

### 6.1. `union` item — parse + layout
*Audit: C (Items), B (Type system), K (Unsafe), Tier-3 #28.*
- **Issue:** `union { f1: T1, f2: T2 }` doesn't parse at all today.
  Ported Rust code that mentions `union` (even just under
  `#[cfg(...)]`-guarded blocks) fails at the grammar.
- **Why core:** Rust's items grammar core surface. Union layout
  + access rules are also a soundness gate (every field-read of a
  union is unsafe).
- **DoD-depth:** `KW_UNION` + `union_def` grammar; `LUnionDef` LIR
  node; layout = max-of-fields aligned to max-alignment; field
  access requires `unsafe` block; targeted pass test (parse +
  unsafe-field-read works) + fail test (safe-field-read rejects).

### 6.2. `static` / `static mut` distinct from `const`
*Audit: C (Items), G (Memory and safety), M (Const-eval), Tier-3 #24.*
- **Issue:** `static NAME: T = expr;` parses as `CONST_DEF` (inline
  substitution at every use); no stable address. `static mut` not in
  the grammar. `&STATIC` lifetime accounting wrong: today its slot
  has the lifetime of the const literal, not `'static`.
- **Why core:** soundness — `&STATIC` must be `'static`. Cross-module
  storage anchors flow into ownership / lifetime invariants.
- **DoD-depth:** distinct AST node (`STATIC_DEF`); stable-address
  storage at link time; `&STATIC` references type as `&'static T`
  (consumer of §2.1 region machinery); `static mut` lands as a
  separate kind with `unsafe`-block requirement on read/write.

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

### 6.4. let-chain in if/while/match guards
*Audit: E (Expressions), Tier-3 #18.*
- **Issue:** `if let A && cond && let B { ... }` parses as a single
  `if let A` followed by stray tokens (the multi-`&&` chain isn't
  supported). Same for `while let`, `match ... if cond_with_let`.
- **Why core:** Rust stable feature (2024 edition); ports of stdlib
  control-flow lean on it heavily.
- **DoD-depth:** shared `let_chain` non-terminal in the PEG grammar
  consumed by `if_expr`, `while_stmt`, and `match_arm.GUARD`. Sema
  desugars to nested `if let` / `match` arms with the conditions
  sequenced. Pass test for 3-way chain; pass test for let-in-guard.

### 6.5. `?` operator on `Try` / `FromResidual`
*Audit: E (Expressions), C (Trait), Tier-2 #15.*
- **Issue:** today `?` is hardcoded to match `Ok`/`Err`/`Some`/`None`
  by callee-name. User types can't implement `?`; Rust's `Try` /
  `FromResidual` trait surface is absent.
- **Why core:** `?` is the canonical error-propagation contract.
  Hardcoding by name is a long-standing parity divergence.
- **DoD-depth:** stdlib lang-item traits `Try` + `FromResidual`;
  sema lowers `?` to a call through the resolved `Try::branch` +
  early-return through `FromResidual::from_residual`; ported
  Result/Option still pass; `impl Try for MyType` pass test.

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

### 6.7. `extern "ABI" { … }` blocks + ABI tag on `Kind::FnPtr`
*Audit: N (FFI/linkage/ABI), B (Type system), Tier-3 #29.*
- **Issue:** `extern fn` declarations are flat — no block form, no
  ABI string. `Kind::FnPtr` doesn't carry an ABI tag, so all
  `extern fn` calls go through the default Logos-internal
  convention. Mismatched-ABI calls are silent UB.
- **Why core:** FFI safety — the ABI string is part of the fn-ptr
  type identity in Rust.
- **DoD-depth:** grammar `extern_block <- KW_EXTERN STRING_LIT
  LBRACE extern_item* RBRACE`; ABI string parsed (`"C"`, `"system"`,
  `"C-unwind"`); `Kind::FnPtr` extended with `abi` field (stored in
  `const_val` / new slot); calls through a non-default-ABI fn-ptr
  emit the matching `llvm.func` calling convention.

### 6.8. `#[cfg(all/any/not)]` combinators + `cfg_attr` activation
*Audit: L (Attributes), Tier-4 #37.*
- **Issue:** the structured `#[cfg(...)]` attribute path accepts only
  single-arg predicates (`#[cfg(unix)]`); the `all/any/not`
  combinators work in `cfg!()` macro context but not in attribute
  position. `#[cfg_attr(pred, attr)]` wrapped-attribute activation
  is a stub.
- **Why core:** conditional-compilation parity; ported Rust code
  uses `#[cfg(all(unix, target_arch = "x86_64"))]` extensively.
- **DoD-depth:** unify the cfg predicate evaluator across attribute
  and macro contexts (single `evaluate_cfg_predicate` shared);
  add `all/any/not` parsing in the attribute path; activate
  `cfg_attr` by recursively re-applying the wrapped attribute.
  Pass tests for the 3 combinators + a `cfg_attr` wrap-and-activate.

### 6.9. `ConstResolver` seam through `metacall { N }`
*Audit: M (Const-eval), Tier-4 #38 + #39.*
- **Issue:** path-to-const references inside `metacall { … }` don't
  fold today — `ctfe::do_eval` is name-blind and can't resolve
  `MY_CONST` to its value when the value lives in another module.
  K10-co-06 tracks this.
- **Why core:** const-eval through the metacall channel is the
  primary "compile-time computation" surface in Logos (replacing
  Rust's `const fn`). Without path-to-const folding, const args
  flow as opaque tokens.
- **DoD-depth:** `ConstResolver` interface threaded into
  `ctfe::do_eval(node, ConstResolver*)`; mono passes the
  `current_consts_` map as the resolver; one-source-of-truth
  shared with `is_const_evaluable` (Tier-4 #39). Pass test:
  `metacall { THRESHOLD + 1 }` where `THRESHOLD` is a stdlib
  const folds correctly.

### 6.10. Derive handlers — `Debug`/`PartialEq`/`Eq`/`Default`/`Hash`/`PartialOrd`/`Ord`/`Copy`
*Audit: J (Macros), L (Attributes), Tier-2 #11.*
- **Issue:** `#[derive(Debug)]` etc. parses as a no-op annotation;
  the derive-handler infrastructure exists (a `#[metaprog_handler]`
  channel) but no stdlib handlers are registered for the standard
  derives. Ported tests using `assert_eq!(...)` on user types
  require manual `impl PartialEq` boilerplate.
- **Why core:** these derives are the standard surface every
  ported Rust struct/enum touches.
- **DoD-depth:** one `#[metaprog_handler]` per trait family in
  stdlib (Debug formatter, PartialEq/Eq via field-by-field eq,
  Default via const-zero / explicit-default chain, Hash via
  field-by-field hasher, PartialOrd/Ord via lexicographic field
  cmp, Copy via field-all-Copy check); each ships with its
  per-derive pass test.

### 6.11. `unreachable!()` / `todo!()` / `unimplemented!()` macros
*Audit: O (Other / Panic), J (Macros), Tier-2 #12.*
- **Issue:** these three are absent from stdlib; ports rewrite to
  `panic!("unreachable")` / `panic!("todo")` etc. by hand.
- **Why core:** standard library control-flow surface; pairs with
  §1.1 Never machinery (their return type is `!`).
- **DoD-depth:** three `#[fn_macro]` wrappers in
  `stdlib/std/fmt/fmt.logos`; each returns `!`; targeted pass test
  exercising each in a dead-branch.

### 6.12. `Range` family — `Range`/`RangeFrom`/`RangeTo`/`RangeFull`/`RangeInclusive`/`RangeToInclusive` as generics
*Audit: E (Expressions / Range), Tier-2 #14.*
- **Issue:** today only `RangeI32` and `RangeI64` exist (concrete
  types per integer width). Rust's `Range<T>` is generic over the
  element type.
- **Why core:** ports of iterator chains (`0i32..count`, `'a'..='z'`)
  rely on the generic surface.
- **DoD-depth:** generic `Range<T>` + 5 siblings; `IntoIterator`-
  style trait surface (or `Iterator` directly per Logos's model);
  the `..` / `..=` operators desugar to the appropriate generic;
  `RangeI32`/`RangeI64` become aliases or `Range<i32>`/`Range<i64>`
  instantiations. Pass tests across the 6 forms.

### 6.13. `DerefMut`-driven autoderef for `&mut self` methods
*Audit: E (Expressions / method dispatch), B (Type system), Tier-2 #16.*
- **Issue:** `box_ref.method()` auto-derefs through `Deref` for
  `&self` methods but not through `DerefMut` for `&mut self`
  methods. `let mut b = Box::new(Vec::new()); b.push(1);` fails
  unless explicit `(*b).push(1)`.
- **Why core:** standard ergonomics for smart-pointer receivers
  (Box/Rc/Arc). Autoderef chain symmetry — `Deref` for shared,
  `DerefMut` for mut, parallel.
- **DoD-depth:** `lookup_method_with_autoderef` chains through
  `DerefMut` when the receiver is mut-borrowable and the candidate
  method takes `&mut self`. Targeted pass test:
  `Box<Vec<i32>>::push` resolves automatically.

### 6.14. Atomics per-variant `Ordering` lowered to MLIR (finish §5.1)
*Audit: G (Memory model), N (FFI), Tier-2 #17 + §5.1 follow-up.*
- **Issue:** §5.1's Wave-2 closure lands MLIR atomic intrinsics
  with conservative `seq_cst` for every Ordering variant. The
  ordering enum value at the call site flows in but is ignored —
  always-sound on every target, but over-synchronizes
  Relaxed/Acquire/Release on weak-memory backends.
- **Why core:** completes the §5.1 contract — the Ordering enum
  value must thread to the MLIR op's ordering attribute for ARM/
  RISC-V codegen to emit the right barriers.
- **DoD-depth:** const-eval the `Ordering` arg at the call site;
  thread the resolved enum-disc to the MLIR atomic op's `ordering`
  attribute (mapping Relaxed → monotonic, Acquire → acquire, etc.).
  Add `AtomicUsize` / `AtomicIsize` to round out the integer
  width matrix. Targeted multi-thread test that observes the
  Acquire/Release ordering via the existing `thread_spawn` path.

---

## 7. Coupling rules (depth ↔ breadth)

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

## 8. Definition of M3 "core done"

M3 ships when every item in §§1-6 is at DoD-depth, with the full suite
green and a 200-test imported-batch demonstrating the core items lit
end-to-end (named lifetimes, dyn-trait with auto-traits, slice-mutability,
match-exhaustive-with-guards, atomic ordering, etc.). At that point
breadth-first work continues against a stable core.

This file is **not exhaustive forever** — promotions from breadth to
core are recorded here with a rationale. Closing items move to a
"Recently closed" section at the bottom (mirroring `DIVERGENCES.md` §B).

---

## 8a. Score (canonical — `/goal` reads this)

> **Score: 23 / 37 ✅ closed at DoD-depth (62.2%)** · 0 🟡 partial · 14 ❌ not
> started. Suite: 5309 / 5309 ✓.
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
| 4.4 | `PAT_PATH` constants-as-patterns | ❌ | not started — Tier-3 #25 |
| 4.5 | fn-params irrefutable patterns | ❌ | not started — Tier-3 #23 |
| 6.1 | `union` item — parse + layout | ❌ | not started — Tier-3 #28 |
| 6.2 | `static`/`static mut` vs `const` split | ❌ | not started — Tier-3 #24 |
| 6.3 | `let-else` divergence assertion | ✅ | `tests/logos/pass/core_6_3_let_else_diverges.logos` ✓ + `tests/logos/fail/core_6_3_let_else_fallthrough.logos` ✓ |
| 6.4 | let-chain in if/while/match | ❌ | not started — Tier-3 #18 |
| 6.5 | `?` on `Try` / `FromResidual` | ❌ | not started — Tier-2 #15 |
| 6.6 | `lookup_qualified_` pub-bypass tightening | ✅ | verified-by-suite (defense-in-depth pub-check on bare-key fallback) |
| 6.7 | `extern "ABI" { … }` blocks + ABI tag on FnPtr | ❌ | not started — Tier-3 #29 |
| 6.8 | `#[cfg(all/any/not)]` combinators + `cfg_attr` activate | ❌ | not started — Tier-4 #37 |
| 6.9 | `ConstResolver` seam through `metacall` | ❌ | not started — Tier-4 #38/#39 |
| 6.10 | Derive handlers (Debug/PartialEq/Eq/Default/Hash/Ord/Copy) | ❌ | not started — Tier-2 #11 (one-per-session sub-deliverables) |
| 6.11 | `unreachable!()` / `todo!()` / `unimplemented!()` | ❌ | not started — Tier-2 #12 |
| 6.12 | `Range`/`RangeFrom`/`RangeTo`/`RangeFull`/`RangeInclusive`/`RangeToInclusive` generics | ❌ | not started — Tier-2 #14 |
| 6.13 | `DerefMut` autoderef for `&mut self` methods | ❌ | not started — Tier-2 #16 |
| 6.14 | Atomics per-variant Ordering threaded to MLIR | ❌ | not started — Tier-2 #17 + §5.1 follow-up |

**`/goal` convergence rule:** target = first column count where Status = ✅
equals 37. Score line above is the canonical authority — when an item moves
from 🟡/❌ to ✅, BOTH the per-item §-body AND this scoreboard row update in
the same commit. No ✅ without (a) DoD-depth code change OR explicit
"verified-by-suite" rationale; (b) a verification test by the recorded
path; (c) full suite gate.

---

## 9. Implementation plan

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
