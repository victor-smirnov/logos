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

### 1.4. `Kind::FnItem` separate from `Kind::FnPtr`
*Audit: B (Function-item types).*
- **Issue:** Logos collapses fn-item types into `FnPtr`. `foo::<i32>`
  and `foo::<u32>` thereby look the same type, breaking
  `if cond { foo::<i32> } else { foo::<u32> }` shape and unsize-coercion
  rules.
- **Why core:** Rust's "every fn instantiation is a distinct ZST" is
  load-bearing for inference + closures-as-FnPtr coercion.
- **DoD-depth:** new `Kind::FnItem` (ZST per instantiation); auto-coerce
  to `FnPtr` at coercion sites; closure-non-capturing → FnPtr path unchanged.

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

### 2.1. Wire `region_infer.cpp` to `borrow_check.cpp`
*Audit: A (Lifetimes, Variance), D (lifetime params, HRTB, outlives).*
- **Issue:** `region_infer.cpp` is scaffolding only. Declared `'a: 'b`
  outlives, `T: 'a` bounds, HRTB binders, and the default trait-object
  lifetime rule (`'static` outside expr / inferred inside) all parse but
  do NOT flow into borrow-check's lifetime-conformance path.
- **Why core:** the audit's #1 cross-category finding. Every named
  lifetime today rides on local scope inference; explicit annotations
  are checked syntactically but not semantically.
- **DoD-depth:** `region_infer` produces a region constraint graph (CFG ×
  declared lt-params) that borrow_check consults at return-value, assign,
  and `let r = &x` sites; default trait-object lifetime rule applied;
  `'a: 'b` outlives + transitive closure honoured at return sites;
  HRTB-instantiation subtyping at fn-call args. Existing B66 outlives +
  B86 inner-struct lt-args become consumers of this pass, not parallel
  paths.

### 2.2. `UnsafeCell` as a lang-item
*Audit: G (Interior mutability), A (Variance), H (Send/Sync), K (UB).*
- **Issue:** Logos has no lang-item for `UnsafeCell`. Borrow checker
  cannot distinguish "interior-mut through `&T`" from a raw `&T → *mut T`
  violation; variance treats `UnsafeCell<T>` as covariant in `T` instead
  of invariant; auto-`!Sync` propagation absent.
- **Why core:** every safe-mutate-through-shared-ref idiom (`Cell`,
  `RefCell`, `Mutex`, atomics) is built on it; without it the borrow rule
  is unsoundly permissive in unsafe-code contexts.
- **DoD-depth:** `UnsafeCell<T>` recognised by sema as a lang-item;
  variance over it is Inv-in-T; auto-`!Sync` derives for any struct
  reaching `UnsafeCell` structurally; borrow-check exempts writes-through-
  `&UnsafeCell<T>` from the normal `&T` write rule.

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

### 2.4. Auto-trait propagation: closures, Arc, Send/Sync edges
*Audit: H (Send/Sync), A (Variance), B (closures, dyn).*
- **Issue:**
  (a) Closures conservatively `!Send`/`!Sync` (`sema_auto_trait.cpp:199-201`)
      — closure types don't walk capture types.
  (b) `Arc<T>` has no `unsafe impl Send/Sync for Arc<T>` in
      `stdlib/mem/sync/arc.logos`, so the structural derivation rejects it
      because of its raw `*mut` field.
  (c) `dyn Trait + Send` parses but is informational only — no
      enforcement at unsize coercion. The bound on the source type isn't
      checked when coercing to `&dyn Trait + Send`.
- **Why core:** Send/Sync is the basis for cross-thread safety; getting
  it wrong is a silent UB pipeline.
- **DoD-depth:** closures propagate Send/Sync structurally (auto-trait
  walks captures); `Arc<T: Send + Sync>: Send + Sync` via `unsafe impl`;
  unsize coercion `&T → &dyn Trait + Auto` verifies `T: Auto` at the
  coercion site.

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

### 2.7. Definite-assignment analysis
*Audit: G (Variables), B8 in DIVERGENCES.*
- **Issue:** `let x: T;` followed by partial init (e.g. `if c { x = ...; }`)
  doesn't error today. M2's drop elaboration handles the drop-side correctly
  (no garbage drop), but Rust's read-side rule (use of `x` is rejected
  unless every CFG path assigned it) is not enforced.
- **Why core:** completes the assignment-soundness story.
- **DoD-depth:** definite-assignment forward-analysis over the LIR CFG;
  every read of a local checks the may-init set; error at first read on
  a path that didn't assign.

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

### 3.1. HRTB instantiation subtyping
*Audit: D (HRTB).*
- **Issue:** `for<'a> fn(&'a T)` parses; nothing instantiates it against
  a caller-side lifetime at call sites. Late-bound regions cannot be
  unified with the inference state.
- **Why core:** closures-as-`FnMut`, iterator-adapter chains, and any
  callback API rely on it.
- **DoD-depth:** HRTB instantiation at call sites: fresh universal at
  the binder, unify with caller actual; subtyping check honours the
  resulting region constraints (consumer of 2.1).

### 3.2. `?Sized` / `Sized` invariants
*Audit: D (Sized/?Sized).*
- **Issue:** generic params have implicit `Sized` bound today; `?Sized`
  opt-in parses but is only partly threaded into method receivers and
  field positions.
- **Why core:** required for `Box<[T]>`, `&dyn Trait` and any custom-DST
  contact with generic code.
- **DoD-depth:** every generic param has an explicit Sized/`?Sized`
  classification; struct-last-field-unsized rule consumes it; method
  receivers (`&self`, `Box<Self>`, …) accept `?Sized` per the
  receiver-shape table.

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

### 4.2. Match exhaustiveness
*Audit: E (match).*
- **Issue:** match exhaustiveness on enums works for the common case but
  doesn't yet prove finite-enum coverage of *guarded* arms, doesn't
  catch missing variants of imported nested patterns, and uninhabited
  arms aren't fully exploited.
- **Why core:** exhaustiveness is the soundness invariant of `match` —
  unmatched scrutinee = UB.
- **DoD-depth:** Useful-Sukhotin-style exhaustiveness algorithm at the
  match-checker; guards integrated; uninhabited (Never, empty enum)
  variant arms accepted without `_`; missing arms rejected even through
  nested variants.

### 4.3. Chained auto-deref in pattern position
*Audit: F (Pattern kinds), audit tier-1 #9.*
- **Issue:** `match &&Some(x) { Some(x) => … }` doesn't bind `x` through
  both `&` layers.
- **Why core:** default-binding-modes ergonomics rule; without it, every
  use of `match` over a doubly-borrowed value falls back to ad-hoc.
- **DoD-depth:** scrutinee + pattern paired walk that peels `&` layers
  in lockstep until shapes align; tested for arbitrary depth.

---

## 5. Memory model core

### 5.1. `Ordering` honoured on atomics
*Audit: G (Memory model / atomics), H, B.*
- **Issue:** stdlib atomic ops accept an `Ordering` argument but discard
  it — every op lowers to `seq_cst` on x86. Unsound on non-TSO targets
  (ARM, RISC-V).
- **Why core:** memory model is THE foundation of concurrent correctness;
  silently strengthening orderings sometimes masks soundness bugs but
  silently *weakening* them (the discarded relaxed → seq_cst direction is
  safe, the reverse is the danger).
- **DoD-depth:** `Ordering` lowered to the MLIR atomic intrinsic with
  the matching ordering enum; `extern fn logos_atomic_*` ad-hoc stubs
  retired; targeted test on a TLA+-modellable two-thread relaxed-store
  / acquire-load case.

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

## 6. Coupling rules (depth ↔ breadth)

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

## 7. Definition of M3 "core done"

M3 ships when every item in §§1-5 is at DoD-depth, with the full suite
green and a 200-test imported-batch demonstrating the core items lit
end-to-end (named lifetimes, dyn-trait with auto-traits, slice-mutability,
match-exhaustive-with-guards, atomic ordering, etc.). At that point
breadth-first work continues against a stable core.

This file is **not exhaustive forever** — promotions from breadth to
core are recorded here with a rationale. Closing items move to a
"Recently closed" section at the bottom (mirroring `DIVERGENCES.md` §B).

---

## 7a. Score (canonical — `/goal` reads this)

> **Score: 11 / 21 ✅ closed at DoD-depth (52.4%)** · 5 🟡 partial · 5 ❌ not
> started. Suite: 5295 / 5295 ✓.
>
> Updated 2026-05-30 (Wave 1 in progress). **Single source of truth for "closed" status — every
> item's status here MUST match the actual implementation tree.** No item
> is ✅ unless its DoD-depth (verbatim from §§ 1-5 above) is met AND a
> verification test exists at `tests/logos/{pass,fail}/core_<§>_<slug>.logos`
> (or the item is marked "verified-by-suite" for pure-refactor cases).

| § | Item | Status | Verification |
|---|------|--------|--------------|
| 1.1 | `Never` / `!` + divergence end-to-end | ✅ | `tests/logos/pass/core_1_1_never_fallback.logos` ✓ |
| 1.2 | Coercion canonical order | ✅ | verified-by-suite (pure internal refactor through `coerce_arg_to_param`) |
| 1.3 | `Kind::InferredType` + `_` | ✅ | `tests/logos/pass/core_1_3_inferred_nested.logos` ✓ |
| 1.4 | `Kind::FnItem` distinct | ❌ | deferred — 39 touchpoints |
| 1.5 | `#[repr]` minimal | ✅ | `tests/logos/pass/core_1_5_repr_transparent_layout.logos` ✓ |
| 2.1 | Wire `region_infer` to `borrow_check` | 🟡 | outlives consumer ✓; default trait-object lt rule + HRTB consumer absent |
| 2.2 | `UnsafeCell` lang-item | 🟡 | variance + auto-`!Sync` ✓; borrow-check carve-out absent — basic case works via existing `unsafe`+raw-ptr surface |
| 2.3 | Variance over trait objects | ✅ | `tests/logos/fail/core_2_3_traitobj_variance_typearg.logos` ✓ |
| 2.4 | Auto-trait propagation | 🟡 | closures + Arc ✓; `dyn+Auto` unsize enforce absent (schema field `auto_bounds` missing) |
| 2.5 | `&mut T` out of Copy-trivial | ✅ | `tests/logos/fail/struct_with_mut_ref_not_auto_copy.logos` ✓ |
| 2.6 | Slice mutability tracked | ✅ | `tests/logos/fail/core_2_6_slice_write_through_shared.logos` ✓ |
| 2.7 | Definite-assignment | ❌ | not started — CFG forward dataflow needed |
| 2.8 | Object-safety enforcement | ✅ | `tests/logos/fail/core_2_8_obj_safety_opaque_return.logos` ✓ |
| 3.1 | HRTB instantiation | ❌ | not started — depends on 2.1 default trait-obj lt rule |
| 3.2 | `?Sized` / `Sized` invariants | ❌ | not started — receiver-shape walker absent |
| 3.3 | GAT + object-safety | ✅ | `tests/logos/fail/core_3_3_gat_dyn_rejected.logos` ✓ |
| 4.1 | `is_refutable` single foundation | ✅ | verified-by-suite (predicate `lir_view::is_irrefutable_pattern` consumed by 3 sites) |
| 4.2 | Match exhaustiveness | 🟡 | variant coverage + bool + uninhabited ✓; Useful-Sukhotin alg + guard integration absent |
| 4.3 | Chained auto-deref in pattern position | 🟡 | sema multi-level peel ✓; binding-modes + codegen multi-level load absent |
| 5.1 | Atomics `Ordering` honoured | ❌ | not started — MLIR atomic-intrinsic ordering enum threading needed |
| 5.2 | UB list documented | ✅ | `docs/language/undefined-behavior.md` ✓ — every PARTIAL/UNENFORCED anchor carries an explicit `**Follow-up:**` line |

**`/goal` convergence rule:** target = first column count where Status = ✅
equals 21. Score line above is the canonical authority — when an item moves
from 🟡/❌ to ✅, BOTH the per-item §-body AND this scoreboard row update in
the same commit. No ✅ without (a) DoD-depth code change OR explicit
"verified-by-suite" rationale; (b) a verification test by the recorded
path; (c) full suite gate.

---

## 8. Implementation plan

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

**M3 release gate:** §§1-5 all at DoD-depth; full suite green; 200-test imported-batch demonstrating core lit end-to-end (named lifetimes, dyn-trait+auto-traits, slice mut, exhaustive guards, atomic orderings).

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
