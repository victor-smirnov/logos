# Category A — Ownership (audit)

v2 — re-audited 2026-06-12 (v1: 2026-05-30); spec: rust-lang/reference (local checkout at `/home/victor/cxx/reference`)

8 features audited: 5 OK, 2 WARN, 1 GAP. All five v1 soundness items closed (`&mut`-field auto-Copy eb894e80, TraitObject variance eb894e80, UnsafeCell Inv 5bccc7fc, region_infer wiring 4c38aed4/239bd7b7, slice mutability c971c97f). Pin/Unpin landed (6dabfe99, DIVERGENCES §A8). New v2 findings: nested partial-move (depth ≥2) is unchecked AND leaks the sibling field; `impl Copy` + `impl Drop` accepted (Rust E0184); temporary-lifetime-extension `let r = &String::from(..)` ICEs in mlir-gen; constant promotion to `'static` rejected.

---

## 1. Move

**Rust nomenclature:** *move semantics* / E0382 "use of moved value" (spec: `destructors.md`, `expressions.md` §value vs place).

**Logos nomenclature:** `is_move_type(t)`; `VarState::moved`/`moved_line`; partial-move map `moved_fields`; `mark_moved_expr`/`track_args_moved` (`src/compiler/sema_expr.cpp:855,2958`); shared aggregate skeleton `moveclass::is_move_type` (`include/logos/compiler/move_classify.hpp:30-47`); diagnostics `"use of moved variable '{}'"` (`sema_expr.cpp:529`), `"use of moved value '{}' (moved on line N)"` (`borrow_check.cpp:942-967`), `"use of moved field '{}'"` / `"use of partially moved value"` (`borrow_check.cpp:931-934`).

**Match verdict:** WARN — core move tracking conformant (probes: one-level field move-out correctly rejects re-use); demoted from v1 OK because the depth-≥2 partial-move hole is now probe-confirmed real, with a leak.

**Implementation pointer:**
- Sema: `sema_expr.cpp:2958,3212,3984` (`track_args_moved` call sites), `:529` (use-after-move report).
- Borrow check (post-mono): `borrow_check.cpp:237-267` (`is_move_type` leaf: `&mut T` → move; structs `needs_drop && !Copy`; enums Drop-impl or move payload).
- Drop suppression of moved vars: `sema.cpp:2979-3020` (dotted-path `moved_vars_`, whole-field suppression at the FIRST level — `sema.cpp:2992`).

**Interactions check:**
- Copy / Drop / Borrow / let-replace / return — OK (unchanged from v1; B8 drop elaboration, `cannot move while borrowed` at `borrow_check.cpp:949`).
- Closures — capture modes handled (RFC-2229 field-precise, 70526c97/20c817d5); **but a non-Copy closure value itself is not move-tracked** — probe: `let c = move ||…String…; let d = c; c()` compiles (Rust E0382). Memory-sound (valgrind-clean; drop of `c` suppressed), diagnosis missing. See Copy §2.
- Match — OK (`mark_match_scrutinee_moved`, `sema_stmt.cpp:7089`; PLACE scrutinees since 51d2e29e).
- Generic-struct moves / iterator receiver borrows — fixed classes, adversarial sweeps #1/#2 (955cebbf, 00355c52).

**Gaps / debt (probe-confirmed 2026-06-12):**
- **Depth-≥2 partial move (B78).** `moved_fields` keys on the outermost field only (`borrow_check.cpp:1568-1574`). Probe `o.i.s` moved, then:
  - read `o.i.s` again → ACCEPTED (Rust: E0382) — conformance hole;
  - read sibling `o.i.t` → accepted (Rust-legal) but `o.i`'s WHOLE drop is suppressed (`sema.cpp:2992`) → **`o.i.t` leaks** (valgrind: 1 block definitely lost). Fix = dotted-path granularity in both `moved_fields` checking and drop suppression.
- `mem::forget` absent from `stdlib/lang/mem/mem.logos` (has unsafe `swap`/`replace`/`take` :55-71 + safe `swap_ref`/`replace_ref`/`take_ref` :88-109 — naming diverges from Rust's ref-taking `mem::swap`; ManuallyDrop covers the suppress-drop use).

---

## 2. Copy

**Rust nomenclature:** `Copy` marker trait (`special-types-and-traits.md` §Copy); mutually exclusive with `Drop` (E0184).

**Logos nomenclature:** built-in trait name `"Copy"`; `copy_types_` + post-mono `TypeSets::copy_types`; structural auto-Copy `compute_auto_copy_types` (`src/compiler/sema.cpp:2702-2800`); `T: Copy` bound honored (`sema_collect.cpp:825-831`, B1 done); **conditional Copy** `impl<P: Copy> Copy for Pin<P>` → `conditional_copy_` positions (`sema_collect.cpp:3558-3585`, landed with Pin 6dabfe99/00355c52).

**Match verdict:** WARN — spelling and auto-Copy conformant; `&mut`-field bug fixed; demoted from v1 OK because `impl Copy` for a `Drop` type is silently accepted (probe) — double-drop hazard.

**Implementation pointer:**
- Registration + `unsafe impl Copy` rejection: `sema_collect.cpp:3553-3585`.
- Structural auto-Copy fixpoint (recursive `is_copy_field` through Struct/Tuple/payload-less Enum): `sema.cpp:2702-2800`.
- ✅ closed (eb894e80): `K::MutRef` removed from `field_kind_is_trivially_copy` — `struct S { r: &mut T }` no longer auto-Copy; fail-test `tests/logos/fail/struct_with_mut_ref_not_auto_copy.logos` verified rejecting.

**Interactions check:**
- Move / Drop / primitives / `&T`-Copy-`&mut T`-move / fn pointers / `T: Copy` / unions (fields restricted to Copy, `sema_collect.cpp:1466-1509`) — OK.
- `#[derive(Copy)]` — blessed divergence A3 (metaprog `#[derive_copy]`); structural auto-Copy covers the same surface.
- Conditional Copy impls — OK (new since v1; the earlier blanket `impl<P> Copy for Pin<P>` made `Pin<Box<T>>` Copy → double free, fixed in adversarial t03).

**Gaps / debt:**
- **E0184 missing** — probe: `impl Drop for R {} impl Copy for R {}` compiles. `compute_auto_copy_types` skips Drop structs structurally, but the MANUAL `impl Copy` path (`sema_collect.cpp:3554`) registers without a Drop cross-check. Copies of a Drop type each drop → double-drop for resource-holding types. One-line check + diagnostic.
- Closure Copy semantics untracked either way: closures with all-Copy captures are usable after re-binding (right result, wrong mechanism), and non-Copy-capture closures are too (wrong — see Move §1). Root: closure values bypass `is_move_type` (`Kind::Closure` falls to skeleton default `false`, `move_classify.hpp:44`).

---

## 3. Drop / RAII

**Rust nomenclature:** Destructor / `core::ops::Drop::drop` / drop glue (spec: `destructors.md`).

**Logos nomenclature:** SDrop statement; `make_drop_stmt` (`src/compiler/sema.cpp:2929`); scope-end `collect_drops`/`collect_all_drops` (`sema.cpp:3045,3052`); mlir-gen `gen_stmt_kind(SDropView)` (`src/compiler/mlir_gen_stmt.cpp:974`); `"__box_dyn__drop"` vtable-drop sentinel (`sema.cpp:2937-2949`); `impls_["Drop::" + target]` lookup.

**Match verdict:** OK — upgraded from v1 OK/WARN: drop elaboration (B8), fn-param drop, and Pin all landed; coverage debt now isolated to the partial-move granularity item (§1).

**Implementation pointer / closed since v1:**
- ✅ fn-param drop Rust-conformant (23f5b86b, 2026-06-01): params drop at fn epilogue with ever-moved gate; `closure_owned_drop_` cleanup for body-moved captures.
- ✅ `Pin<P>` + auto-trait `Unpin` (6dabfe99): `stdlib/lang/pin/pin.logos` — full Rust API (`new`/`into_inner`/`get_ref`/`get_mut`/`as_ref`/`as_mut`/`new_unchecked`/`get_unchecked_mut`/`box_pin`, `Deref`, cond-`DerefMut where T: Unpin`); `Unpin` structural auto-trait, `PhantomPinned`/`#[pinned]` → `!Unpin`; registered DIVERGENCES §A8 (51a8019a) — coexists with native `#[pinned]`. Tests: pin_basic, pin_box, pin_unpin_auto, fail/pin_unpin_phantom, fail/pin_get_mut_not_unpin.
- Drop elaboration (B8, 3e7bb5df): static placement + runtime drop flags only for conditionally-assigned vars — Rust MIR model.
- ManuallyDrop / MaybeDangling / DropGuard: `stdlib/mem/manually_drop/manually_drop.logos`.

**Interactions check:**
- Move-suppresses-Drop / scopes (reverse decl order) / struct-enum field drops / `Box<dyn>` vtable drop / assignment drop-before-replace — OK. Drop-order tests exist (`tests/logos/pass/drop_order_with_non_droppable`, `adv1_capture_drop_order`).
- Copy×Drop — the E0184 gap (§2) sits on this boundary.
- Panic — N/A-by-design (A7 panic=abort; no unwind drops to verify).
- Vec element drop — resolved (52c24b28, per DIVERGENCES B7 residual).

**Gaps / debt:**
- `impl Copy` + `impl Drop` diagnostic (§2).
- Partial-move drop suppression too coarse → sibling leak (§1).
- Match-arm-guard drop-order test (spec §destructors.scope.bindings.match-arm) still absent — minor.

---

## 4. Borrow `&T` / `&mut T`

**Rust nomenclature:** Shared / mutable reference (`types/pointer.md` §References, `expressions/operator-expr.md` §Borrow).

**Logos nomenclature:** `Kind::Ref`/`Kind::MutRef` (`include/logos/compiler/sema.hpp:53-54`); grammar `ref_type` family (`tools/peg_gen/grammars/logos.peg` ~1447-1500, LIFETIME token :457); `EAddrOf`/`EAddrOfTemp`; borrow checker `src/compiler/borrow_check.cpp` (3148 LOC, grown ~760 since v1).

**Match verdict:** OK — type kinds, grammar, exclusivity, and (new) slice mutability all Rust-shaped.

**Implementation pointer / closed since v1:**
- ✅ slice mutability (B6, c971c97f + Wave-9 finish): `Kind::Slice` carries `mut_ptr` (`sema.hpp:95,249`); write through `&[T]` rejects (`"cannot write through a shared `&[T]` slice"` — fail-test verified); `&mut [T] → &[T]` downgrade works. DIVERGENCES B6 marked done — consistent.
- ✅ `&T → *const T` coercion — probe-verified working (v1 had it unverified).
- ✅ escape analysis battery (new since v1): method-result ref holds receiver (2716030f), return-of-local field/index ref (af33a8d2), conflicts through reference roots (f9e4efa3), iterator invalidation `&v[i]` across `v.push` (849b1a91) + through-`&mut` (f14ec365), E0716 temp-dropped-while-borrowed (91e6c945), `#[borrow_carrying]` aggregate/container propagation (0000284a, 47610a77).
- Two-phase borrows (B82): state `borrow_check.cpp:279`, `take_borrow` :818,1509-1595. Field-path borrows (B83): `FieldBorrow` dotted paths :396-403, single place-extraction foundation :410-435. Dropck (B87): :537-546,618-640.
- NLL: `BorrowRecord::holder` released after holder's last use (:383-391) + region_infer now feeding outlives (§5) — still approximation, not full region-graph NLL.

**Interactions check:**
- Reborrow / method receivers / closures-by-ref / DST fat refs (`Slice`/`TraitObject`/`DstRef`) / two-phase / field-path — OK.
- Deref — user-`Deref` auto-invoke `&*x` resolves through Deref impls (dcc2f4e8); impl selection by self-type shape (8c10eb4e).
- Variance — OK (§7).

**Gaps / debt:**
- `&raw const` / `&raw mut` syntax absent (spec `type.pointer.raw.constructor`) — grammar has no production; low impact (`&x as *const T` path works).
- `&[T]` vs `&mut [T]` share a TypeUID pool entry (mut bit at LIR level only) — DIVERGENCES B6 residual hygiene note, not soundness.

---

## 5. Lifetimes

**Rust nomenclature:** `'a` / `'static` / elision / outlives `'a: 'b` / HRTB `for<'a>` (`lifetime-elision.md`, `subtyping.md`, `trait-bounds.md`).

**Logos nomenclature:** LIFETIME token (`logos.peg:457`); `lifetime_params` on fn/struct/impl (`sema_decl.cpp:439`); outlives capture (`sema_collect.cpp:2625-2640`); HRTB binders `trait_bound.hrtb_binders` (`sema.cpp:3729-3733`, consumer :4943); region engine `src/compiler/region_infer.cpp` (931 LOC).

**Match verdict:** OK — upgraded from v1 WARN: the three structural gaps (region wiring, default trait-object lifetime, HRTB instantiation) all closed with fail/pass tests; residuals are representation hygiene, not behavior.

**Implementation pointer / closed since v1:**
- ✅ region_infer wired into borrow_check (4c38aed4): `RegionInferer::outlives_named` (`region_infer.cpp:102`) is the canonical consumer API; borrow_check holds `const RegionInferer* ri_` (`borrow_check.cpp:560-566`), prefers it over the string `outlives_adj_` graph at the return-lifetime check (:1425-1433), runs it first per fn (:3078-3085). Fail-test `core_2_1_dyn_ref_outlives_local` ✓.
- ✅ default trait-object lifetime rule (239bd7b7): borrowing `&dyn Trait` treated as ref-kind so `fn bad() -> &dyn T { &local }` rejects (`borrow_check.cpp:365-377`); owning `Box<dyn>` exempt.
- ✅ HRTB instantiation subtyping (ff12df64, logos-core §3.1): fresh-universal instantiation at binder + region-constraint consumption; `core_3_1_hrtb_closure_arg` + 59 hrtb-* tests.
- ✅ `'_` placeholder — handled as elided at return-lifetime check (`borrow_check.cpp:1400,1458`); probe `fn pick(v: &'_ i32)` compiles+runs.
- Return-elision rules: `check_return_value` (`borrow_check.cpp:1368-1460`); receiver elision `&self → &T` provenance (:1063,1285).

**Interactions check:**
- Borrow / generics / where-outlives / struct-impl lifetime params / trait objects `+ 'a` — OK.
- Subtyping — `subtype(sub, sup, outlives_adj, def_variances)` (`include/logos/compiler/subtype.hpp:37`) consumed at let-coercion/return (`sema_stmt.cpp`, `sema.cpp`, `sema_expr.cpp`); probe: `fn sf() -> &'static i32 { &42 }` rejects with a lifetime-structure diagnostic — the check is live (the rejection itself is the §8 promotion gap).
- Dropck — B87 conservative check; full Polonius-style still out of scope.
- Async — N/A (A4 fibres).

**Gaps / debt:**
- `'static` still string-matched (`"'static"||"static"`, `sema_decl.cpp:26,108,1088,1197,1549`) — no structural kind; foot-gun, minor.
- Region engine covers declared lt-params + CFG within a fn; cross-fn inference remains elision-shaped (Rust-equivalent for the supported surface, but no inferred-region diagnostics quality).

---

## 6. Reborrow

**Rust nomenclature:** implicit reborrow of `&mut T` (or downgrade to `&T`) at coercion sites (`type-coercions.md`).

**Logos nomenclature:** `try_implicit_reborrow_mut` (`src/compiler/sema_expr.cpp:12226`); wrap shape `AddrOfTemp(Deref(arg))`; `lir_view::is_reborrow_shape` peephole (`mlir_gen_dyn.cpp:1351-1356`).

**Match verdict:** OK — unchanged; line refs refreshed.

**Implementation pointer:**
- Call-arg coercion: `CFLAG_IMPLICIT_REBORROW` dispatch (`sema_expr.cpp:12154`); direct call sites :3848,3863,3894.
- Method receiver: `sema_expr.cpp:12140` (`allow_downgrade=false` — by design, matches Rust receiver rules).
- Let-binding coercion site: `sema_stmt.cpp` (Rust auto-reborrow at `let _: T = rhs`).
- Mono passthrough: `mono_clone.cpp` reborrow-vs-rebind distinction.

**Interactions check:** Borrow / receivers / call args / let ascription / two-phase / NLL release / `&mut → &` downgrade — all OK. New since v1: receiver reborrow composes with multi-impl Deref selection (8c10eb4e) and user-Deref `&*x` auto-invoke (dcc2f4e8); borrow_check routes reborrow shapes through reference-root conflict tracking (f9e4efa3) so aliased `&mut`+`&` through a reborrow is caught.

**Gaps / debt:**
- Reborrow wrap still fires only for `VarRef`/`FieldRead`/`IndexRead` roots; method-call results returning `&mut T` rely on existing AddrOfTemp shape — unchanged, no observed failure class.

---

## 7. Variance & subtyping

**Rust nomenclature:** Variance, subtyping (`subtyping.md`); {covariant, contravariant, invariant} (+ internal bivariant).

**Logos nomenclature:** `enum class Variance { BiVar, Co, Contra, Inv }` + `variance_meet`/`variance_compose`/`DefVarianceTable` (`include/logos/compiler/variance.hpp:31-63`); `variance_in_type` (`src/compiler/sema.cpp:7365-7488`); `compute_variances` (:7495); `subtype()` (`include/logos/compiler/subtype.hpp:37`).

**Match verdict:** OK — full lattice, per-kind rules match the `subtyping.md` table; both v1 soundness bugs fixed.

**Implementation pointer / closed since v1:**
- ✅ `TraitObject` arm (eb894e80): `dyn Trait<T…> + 'a` Co in `'a`, Inv in each type arg, auto-trait bounds variance-inert (`sema.cpp:7468-7487`); fail-test `core_2_3_traitobj_variance_typearg` ✓. (v1: fell to BiVar — accepted wrong directions.)
- ✅ `UnsafeCell<T>` Inv-in-T (5bccc7fc): lang-item recognized by qualified name `logos.lang.cell.UnsafeCell` (`sema.cpp:7414-7427`); pass-test `core_2_2_unsafecell_write` ✓.
- Per-kind rules: `&'a T` Co/Co, `&'a mut T` Co-in-lt/Inv-in-pointee (:7377-7395), `*const` Co / `*mut` Inv (:7396-7400), fn types Contra-args/Co-ret (:7461-7467), Struct/Enum per-param table with Co default (:7428-7460).

**Interactions check:**
- Lifetimes / references / generics / fn pointers / `*const`/`*mut` / trait objects — OK.
- HRTB — closed via §5 (ff12df64).
- DST coercions — unsize site checks auto-trait bounds separately (logos-core §2.4c); variance composition through `DstRef` unexercised by tests — minor.
- PhantomData — `stdlib/lang/marker/marker.logos:38` (`pub struct PhantomData<T> {}`, landed 9e70c6c1). **Variance-inert**: no compiler special-case, the unused param infers BiVar, so `PhantomData<T>` does NOT force Co-in-T as in Rust (`subtyping.md` table row: PhantomData<T> = covariant). WARN-level residual.

**Gaps / debt:**
- `PhantomData<T>` should contribute variance as-if a `T` field (and dropck may-drop, when dropck deepens) — small special-case next to the UnsafeCell one.

---

## 8. Temporary scopes, lifetime extension, constant promotion — NEW in v2

**Rust nomenclature:** temporary scopes / temporary lifetime extension / constant promotion (`destructors.md` §§temporary-scopes, lifetime-extension, constant-promotion) — missed entirely by v1.

**Logos status (probed 2026-06-12):**
- Temporary scopes + E0716 — ✅ closed (91e6c945): statement-temporaries get `RefProv::is_temp` provenance; `let v = make().view(); use(v)` rejects "temporary dropped while borrowed". Rust-conformant for the rvalue-receiver class.
- **Temporary lifetime extension — GAP (ICE).** `let r = &String::from("abc");` (Rust: temp extended to `r`'s scope) fails in mlir-gen with `'func.call' op operand type mismatch … module verification failed` — not even a clean diagnostic. Scalar temps work (`let r: &i32 = &42;` runs); the extension rule for droppable temps is unimplemented and the fallback path miscompiles.
- **Constant promotion — GAP.** `fn sf() -> &'static i32 { return &42; }` rejected ("variance mismatch — expected &'static i32, got &{integer}"). Rust promotes the literal to a static. Needs a promotion pass (const-eligible rvalue in `&` position → static alloc) feeding the region check.

**Match verdict:** GAP — one of three spec rules implemented.

---

## Cross-category gaps

- **B (Type system) — `&[T]`/`&mut [T]` TypeUID split** — residual hygiene (B6 done otherwise).
- **F (Patterns) — `ref`/`ref mut`** — under category F; exclusivity interaction unchanged.
- **G (Memory/safety) — UnsafeCell** write-exemption surface lives in G; variance side closed here.
- **H (Concurrency) — auto-traits** closed at the propagation level (logos-core §2.4); `'static`-string weakness (§5) still shows up in dyn+Send bounds.
- **O — Pin** — no longer a gap (A8 coexistence model).

## Scoreboard cross-check (logos-core.md §2/§3 vs reality)

All claimed rows verified real at code+test level: 2.1 (ri_ consumer + default dyn-lt), 2.2 (lang-item + Inv + `!Sync`), 2.3, 2.5, 2.6, 2.7, 3.1 — no scoreboard-vs-reality inconsistency found in category A. Note 2.6's first landing predates the Wave-9 close date (c971c97f, 2026-05-29). The v2-new gaps (§1 nested partial move, §2 E0184, §8 extension/promotion) are NOT covered by any logos-core row — catalog candidates.

## Recommended next moves

1. **Fix temporary-lifetime-extension ICE** (§8): `let r = &<droppable rvalue>;` must either extend (Rust rule) or cleanly reject; today it miscompiles in mlir-gen. Soundness + UX.
2. **Nested partial-move granularity** (§1, B78): dotted-path `moved_fields` end-to-end (check at `borrow_check.cpp:1568`, suppression at `sema.cpp:2992`); fixes both the missed E0382 and the sibling-field leak.
3. **E0184 diagnostic** (§2): reject `impl Copy` when the target has `impl Drop` at `sema_collect.cpp:3554` — one-line check, closes a double-drop hazard.
4. **Constant promotion** (§8): promote const-eligible `&rvalue` to static storage; unblocks `&'static` literal returns.
5. **Closure move/Copy classification** (§§1-2): give `Kind::Closure` a real leaf in `move_classify` (move iff any capture is move-type); makes non-Copy closures E0382-tracked and Copy closures principled.
6. **PhantomData<T> variance** (§7): treat as a `T` field in `variance_in_type`, next to the UnsafeCell special-case.
