# Category B — Type system primitives (audit)

v2 — re-audited 2026-06-12 (v1: 2026-05-30); spec: rust-lang/reference (local `/home/victor/cxx/reference`)

17 features audited: **14 OK**, **3 WARN**, **0 GAP**. Since v1: both GAPs closed — `Kind::FnItem` (0f1fa0c2) and `Kind::InferredType` (52f72e34 + a960bd89); slice mut bit (c971c97f), `#[repr(transparent)]`/`#[repr(uN)]` (52f72e34 + 00a96805), `dyn + Send/Sync` enforcement (fdae52fb), Never one-directionality (eb894e80 + 7f789b9f), `union` (44e05308), Send/Sync/Unpin auto-trait engine (`sema_auto_trait.cpp`). New findings: `_` in fn-param signature **segfaults** the compiler; `-> _` accepted and leaks (Rust E0121 rejects both); array move-out diagnostic still absent (Vec::get analog landed, 697ca7d2).

---

## 1. Primitive types (`bool`, integer, float, `char`, unit)

**Logos:** `LogosType::Kind::{Bool, U8..I128, Usize, Isize, F32, F64, Char, Void}` + Hermes widths `I24/U24/I56/U56` (`include/logos/compiler/sema.hpp:46-138`); unit = empty-`Tuple` (`logos.peg` `unit_type`). Name lookup `lookup_type_by_name` (`src/compiler/sema.cpp:2350-2383`); literal sentinels `Kind::IntLit/FloatLit`.

**Verdict: OK** — names match Rust; `usize/isize` distinct kinds; `g_target_pointer_bits = 64`.

Interactions: Copy bucket ✓; int widening `types_compatible` (`sema.cpp:1673+`); literal patterns ✓; built-in ops bypass ✓; const eval = metacall (A1).

Debt:
- `"void"` still surface-exposed (`sema.cpp:2371`) — internal kind leaking to the name layer.
- `I24/U24/I56/U56` blessed Hermes widths — still no explicit A6 row (A6 lists variadics/Hermes fabric generally).
- `#[repr(align(N))]` absent (see §14) — `i128` align fixed by platform.

---

## 2. Never type `!`

**Logos:** `Kind::Never` (`sema.hpp:99`); lookup `"!"` (`sema.cpp:2372`); display `sema.cpp:2046`.

**Verdict: OK** — ✅ v1 gaps closed (eb894e80 Phase-1 tighten + 7f789b9f Wave-1, logos-core §1.1):
- Coercion now ONE direction, `Never → T` only (`sema.cpp:1705-1711`); probe: `fn d() -> ! { return 5; }` rejected "return type mismatch — expected !, got {integer}".
- `loop {/*no break*/}` types `!` via `last_loop_diverged_`; if/match/let-else join sees diverging arms (`cur_diverged_`).
- `is_divergent_call_node` (`sema.cpp:1578`) single predicate replaces `callee == "panic"` carve-outs.
- Rust-2024 `!`-fallback for unbound T when callee body always diverges (`SemaFuncInfo::body_always_diverges`).
- Uninhabited exhaustiveness: `match x {}` on `!`/empty-enum scrutinee accepted (`check_match_exhaustiveness`, `sema_stmt.cpp:6996-7006`, logos-core §4.2).
- `!`-typed parameter rejected as uncallable (`sema_decl.cpp:553-559`).

---

## 3. Tuple

**Logos:** `Kind::Tuple` (`sema.hpp:59`); grammar `tuple_type` incl. 1-ary `(T,)` and unit `()`; elementwise compat `sema.cpp:1780`.

**Verdict: OK.** Probe: `let t = (5,); t.0` compiles+runs. Variadic tuple `(A...)` now documented as DIVERGENCES **A6** addition (✅ v1 doc-debt closed). Construction/field-access/drop/generics ✓ as v1.

---

## 4. Array `[T; N]`

**Logos:** `Kind::Array` (`sema.hpp:55`); `make_array(elem, n, symbolic)`; `&[T;N] → &[T]` unsize coercion ✓ (structural inside tuples too, DIVERGENCES "recently caught up").

**Verdict: OK.**

Debt:
- "Cannot move out of `arr[i]` of non-Copy T" diagnostic still absent. The Vec analog landed (697ca7d2: `Vec::get` on move-type element rejected with fix-it, `sema_expr.cpp:6579-6597`) — its comment says "mirrors the rule for fixed arrays would-be"; extend the same rule to `Kind::Array` indexing.
- `[T; 0]` ZST untested (unchanged).

---

## 5. Slice `[T]`

**Logos:** `Kind::Slice` = fat `&[T]` form; bare `[T]` = `Kind::UnsizedSlice`, canonicalises under `&`/`*`; `Box<[T]>` = `OwningKind::Box` in const_val (B3).

**Verdict: OK** (was WARN) — ✅ **B6 mut-bit closed** (c971c97f, logos-core §2.6): `Kind::Slice` carries `mut_ptr`; `make_slice_type(elem, is_mut, owning)` (`sema_impl.hpp:622`); writes through shared `&[T]` rejected — "cannot write through a shared `&[T]` slice (need `&mut [T]`)" (`sema_stmt.cpp:6634`); `&mut [T] → &[T]` downgrade works. Pool interning serializes the mut bit (`sema.cpp:202-204`), so the two forms intern distinctly — DIVERGENCES B6's "residual pool-level UID split" note looks stale.

Debt:
- `&'a [T]` lifetime still parsed-and-dropped (grammar comment at `slice_type`).
- Internal naming (`Slice` ≡ ref-to-slice) unchanged; cosmetic.

---

## 6. `str`

**Logos:** no `Kind::Str` — `"str"` resolves to `Slice<u8>` / `UnsizedSlice<u8>` (`sema.cpp:2373-2382`); `&str` parses as REF over `str` and collapses to the same.

**Verdict: WARN** (unchanged) — `str` ≡ `[u8]` by type identity; `&str` and `&[u8]` indistinguishable (Rust rejects the cross-assignment); UTF-8 invariant not enforced. `str = Slice<u8>` is shared-only by construction (mut bit never set), consistent with §2.6. `Box<str>` resolution unverified.

Fix: distinct `Kind::Str` (or a tag bit on Slice) to separate dispatch + track UTF-8.

---

## 7. Raw pointer `*const T` / `*mut T`

**Logos:** `Kind::Ptr` (`sema.hpp:52`); deref requires `unsafe` ✓; `&T → *const T`, `*mut → *const` coercions ✓; Ptr trivially-Copy ✓.

**Verdict: OK.** A6 additions since v1: `*zoned T` / `*zoned mut T` zoned raw pointer (39ac9d16; zoned bit in Ptr const_val bit 0, interned distinctly after 3d40b2e8) and `&tagged<TS> Trait` thin tag-dispatched ptr (`Kind::TaggedPtr`, `sema.hpp:70`) — both Hermes-model additions, not parity items.

Debt (unchanged):
- `&raw const` / `&raw mut` (Rust 2024) — no grammar entry; `&x as *const T` detour remains.
- `*const [T]` / `*mut [T]` parse to `SLICE_TYPE` → `Kind::Slice` (`logos.peg` ptr_type alts) — raw fat ptr conflated with the safe slice type.

---

## 8. Function-item types

**Verdict: OK** (was GAP) — ✅ closed 0f1fa0c2 (logos-core §1.4). `Kind::FnItem` (`sema.hpp:114-138`): per-instantiation ZST carrying signature + symbol name (`struct_name`) + `type_args`; TypeUID hashes all three. Bare fn name lowers to FnItem (`lower_var_ref`); FnItem → FnPtr auto-coerces at value-use sites; FnItem → FnItem NOT compatible (`[add1, sub1]` rejected — `tests/logos/fail/core_1_4_fnitem_distinct_arms.logos`). 39 downstream sites accept both via `LogosType::is_fn_value_kind` (`sema.hpp:137`). `marker::<i32>` vs `marker::<u32>` (signature-invariant type-args) intern distinctly via hashed type_args. Note: the logos-core plan-table row 1629 still carries the stale "DEFERRED" investigation text; the §1.4 body + scoreboard are authoritative (closed).

---

## 9. Function pointers `fn(T) -> U`

**Logos:** `Kind::FnPtr` (`sema.hpp:69`); grammar covers `fn(T)->R`, `unsafe fn` (IS_UNSAFE captured, call-site enforced), `for<'a> fn(...)` (HRTB_BINDERS captured).

**Verdict: OK.**

Debt (unchanged):
- No ABI tag at the type level (`extern "C" fn(...)` — fn_ptr_type grammar has no extern alt). `extern "ABI"` block/item forms landed (§6.7) but don't surface on the pointer type.
- HRTB binders on fn-ptr parsed, not skolemized per-type (grammar comment) — though §3.1 HRTB instantiation landed for trait bounds.

---

## 10. Closure types

**Logos:** `Kind::Closure` (`sema.hpp:61`); RFC-2229 capture ✓; `dyn Fn*(...)` collapses to `Kind::Closure`.

**Verdict: OK.**

Closed/confirmed since v1:
- Capturing-closure → FnPtr coercion correctly REJECTED: `try_coerce_closure_to_fnptr` (`sema_impl.hpp:606-621`) requires `capture_count() == 0` (v1 unverified, now confirmed).
- Send/Sync now structural over captures (`sema_auto_trait.cpp`, logos-core §2.4(a)) — v1's "Send/Sync absent globally" is obsolete.
- Unpin auto trait: closures/types default-Unpin, `PhantomPinned`/`#[pinned]` → `!Unpin` (6dabfe99, A8).

Debt:
- `dyn Fn*(...) → Kind::Closure` collapse still has no DIVERGENCES entry (blessed simplification, undocumented).
- Closure auto-Copy/Clone when all captures Copy — still open (category A).

---

## 11. Trait objects `dyn Trait`

**Logos:** `Kind::TraitObject` (fat) / `Kind::UnsizedDyn` (bare DST); owning forms Box/Rc/Arc in const_val (`OwningKind`, `sema.hpp:268`); object safety `check_trait_object_safe` (`sema.cpp:2837`).

**Verdict: OK** (was WARN — v1's naming quibble dropped).

Closed since v1:
- ✅ `dyn Trait + Send/Sync` ENFORCED at unsize-coercion site (fdae52fb, logos-core §2.4(c)): bits 8/9 of TraitObject const_val carry the bounds (`trait_requires_send/sync`, `sema.hpp:277-292`), folded into TypeUID; coercion verifies `T: Auto` via `sema_auto_trait.cpp`.
- ✅ Object safety extended: `impl Trait` in return/param position rejected (E0038 analog, `mentions_impl_trait` walker, logos-core §2.8); GAT items rejected (§3.3).
- ✅ Variance over trait objects checked (logos-core §2.3).
- Supertrait vtable slots + upcasting (527182b9); `Rc/Arc<dyn Tr>` real-struct repr + implicit CoerceUnsized (B3 stage-2b).

Debt: `+ 'a` lifetime bound parsed-and-ignored (grammar comment at dyn_type Fn-family alts); spec's trait-object lifetime defaults not modeled.

---

## 12. `impl Trait`

**Logos:** `Kind::ImplTrait` (`sema.hpp:67`); arg-position desugars to synthetic generic; return-position opaque.

**Verdict: OK.** New coverage verified: **RPITIT** (return-position `impl Trait` in trait + impl methods) compiles and dispatches statically (probe: trait method `fn make(&self) -> impl Counter`, runs exit 0). Dyn-side correctly rejected per §2.8.

Debt (unchanged): no `use<...>` precise-capture (no grammar hit); edition-2024 automatic capture rules untested.

---

## 13. Inferred type `_`

**Verdict: WARN** (was GAP) — core ✅ closed (52f72e34 Phase-2 + a960bd89 Wave-1, logos-core §1.3): `Kind::InferredType` (`sema.hpp:107`); `_` is a `type_ref` alternative; `let x: _ = rhs` ✓; nested `let v: Vec<_> = vec_new::<i32>()` ✓ (probe compiles, runs exit 0) via Struct-vs-Struct elementwise rule in `types_compatible` + `InferredType`-as-wildcard in `subtype.hpp::types_equal_with_lifetimes`; turbofish `_` ✓.

**New gaps (this re-audit)** — Rust E0121 ("`_` not allowed in item signatures") has no analog:
- `fn f(x: _)` **segfaults logosc** (exit 139, no diagnostic) — crash-on-invalid-input; needs a collect-time reject.
- `fn g() -> _ { return 1; }` accepted; the `_` leaks to call sites ("operator '-': left must be numeric, got _") instead of an item-signature error.

---

## 14. Type layout / `#[repr]`

**Verdict: WARN** (was GAP) — minimal repr ✅ landed (52f72e34 surface + 00a96805 layout consumer, logos-core §1.5):
- `#[repr(transparent)]`: `SemaStructInfo::repr_transparent` → `LStructDef` → `layout_of` returns the single field's layout (`mlir_gen_types.cpp:473`); single-non-ZST-field invariant enforced at collect (`sema_collect.cpp:1584`).
- `#[repr(uN)]` enum: `SemaEnumInfo::backing_type` sets discriminant width; conflict with declared backing type errors (`sema_collect.cpp:1676-1722`).
- `#[repr(C/packed/align)]`: parse-then-reject with explicit "not yet supported" (`sema_collect.cpp:1590`) — honest diagnostic, still a parity gap (FFI layout).
- `union` layout landed (44e05308, logos-core §6.1): Struct-shaped `is_union=true`, max-of-fields size @ max alignment, unsafe field access.
- Niche optimizations (repr(Rust) freedom, exercised): NullPtr niche for `Option<&T>` (f0266cbd), LowBit one-word packing (9e132ea0), `#[zoned2]` zoned RefRepr niche (9383d687). Enum value-repr inline (51d2e29e).
- `layout_of` is the single size/align foundation (refrepr Phase-1 routed reference cases through `repr_storage_layout`, 20e108da).

Remaining: `#[repr(C)]`, `#[repr(packed)]`, `#[repr(align(N))]` — parse-rejected, not implemented.

---

## 15. Type coercions

**Logos:** `types_compatible(from, to)` central driver (`sema.cpp:1673+`); canonical arg entry `coerce_arg_to_param` (logos-core §1.2: 8+ sites routed; 5 `widen_int_expr` pre-widen sites documented as the load-bearing exception set).

**Verdict: OK** (was OK/WARN — both WARN causes closed):
- ✅ Never one-way (§2 above). ✅ Slice mut conflation closed; `&mut [T] → &[T]` downgrade is now a real coercion (§5).
- Unsize: `&[T;N] → &[T]` ✓ (incl. structural in tuple/struct fields); `&T → &dyn Trait (+ Auto verified)` ✓; implicit `CoerceUnsized` for `Rc<A> → Rc<dyn>` ✓ (B3 stage-2b).
- Raw-ptr coercions ✓; auto-ref/auto-deref receivers ✓ (multi-impl Deref selection by self-shape, 8c10eb4e; DerefMut autoderef §6.13).
- LUB-style if/match arm unification present (Never-aware join); not a named LUB pass.
- `InferredType` permissive both directions — intentional (`_` wildcard), not a soundness hole (resolved before codegen).

Debt:
- Coercion-site `Deref` still hard-coded to `&Vec<T> → &[T]` (`sema.cpp:1841-1857`, comment: "full `Deref` trait surface is the longer path"); arbitrary `impl Deref` types don't coerce at let/arg/return — only method autoderef is trait-driven.

---

## 16. Dynamically sized types (DST)

**Logos:** `is_dst()`; `UnsizedSlice`/`UnsizedDyn`/`DstRef` kinds; custom-DST tail-slice + owning `Box<Foo>` (B2 ✓), `Box<[T]>` (B3 ✓).

**Verdict: OK** — ✅ §3.2 invariants pinned end-to-end (f895f1f3):
- Classification: `TypeParam::implicit_sized` default-true; only `?Sized` relaxes (`finalize_relaxed_bounds`); other `?Trait` errors.
- Enforcement at every type-arg substitution: unsized arg into `Sized`-required slot → "requires `Sized` (add `T: ?Sized` to relax)"; outer-`?Sized` can't leak into inner `Sized` slot (`current_type_relaxed_sized_`).
- Struct-last-field-unsized → effective custom-DST (`is_effective_dst`).
- Receiver shapes over unsized Self ✓ (`impl Speak for [u8]`, `&[u8]` dispatch).
- DST writes through `&mut DstStruct` (71a65859); RefRepr phases 0-3.5 centralize fat-ptr storage/compute (fceba1d3 → 26dff9ce).

Debt: trait-definition default for `Self: Sized` vs Rust's implicit `?Sized` on traits — unverified (unchanged); `&'a (T + 'b)` untested.

---

## 17. Parenthesized types `(T)` — added in v2 (missed in v1)

Spec `types.md` §Parenthesized types. **OK** — `paren_type <- LPAREN type_ref RPAREN` (`logos.peg:1379`, landed bde67186 "B-ty-09"), in the `type_ref` alternation after tuple_type so `(T,)` wins as tuple, `(T)` as grouping. Needed for `&(dyn Trait + Send)`-style precedence.

---

## Cross-category notes

- **Send/Sync/Unpin auto traits now exist** (`sema_auto_trait.cpp`: structural, `&T`/`&mut T` rules per spec, scalars/fn-ptrs always Send+Sync) — v1's headline cross-gap is closed; remaining depth (e.g. `unsafe impl !Send`) is category H.
- **Union landed** (§6.1) — v1's "Kind::Union entirely missing" obsolete (Struct-shaped, `is_union` flag).
- **`#[repr(C/packed/align)]`** remains the FFI-relevant layout gap (§14).
- **Lifetimes parsed-and-dropped on Slice / dyn `+ 'a`** — narrower than v1 (region_infer wired §2.1, HRTB §3.1) but these two surfaces still discard.
- **`&raw const` / `&raw mut`** still absent (B7 raw ptr).
- Scoreboard-vs-reality: logos-core §1.4 plan-table row (line ~1629) retains stale "DEFERRED" text vs the closed §-body; DIVERGENCES B6 "residual pool-UID split" contradicts logos-core §2.6 "pool no longer aliases" — code supports §2.6 (mut bit serialized at `sema.cpp:202`).

## Recommended next moves

1. **`_` in item signatures**: fix the `fn f(x: _)` segfault + reject `_` in param/return of items (E0121 analog). Crash-on-input is the only P0 in this category. Half-session.
2. **Array move-out diagnostic** — extend the Vec::get rule (697ca7d2) to `Kind::Array`/slice indexing of non-Copy elements. Half-session.
3. **Trait-driven `Deref` coercion at coercion sites** — replace the Vec hard-code (`sema.cpp:1841`) with an `impl Deref` probe; reuses the multi-impl selection machinery (8c10eb4e). One session.
4. **`Kind::Str`** distinct from `Slice<u8>` — `&str` vs `&[u8]` identity + UTF-8 invariant. One session, stdlib churn.
5. **`#[repr(C)]`** implement (parse already lands; plumb into `layout_of`). One session.
6. **DIVERGENCES doc pass** — register `dyn Fn* → Closure` collapse, `I24/U24/I56/U56` widths, `Void` surface name; refresh B6 residual note.
