# Category G — Memory and safety (audit)

v2 — re-audited 2026-06-12. v1 generated 2026-05-30. Spec: rust-lang/reference (local `/home/victor/cxx/reference`).

Summary: 3 features — 1 OK (interior mutability), 2 WARN (atomics, variables). Since v1: UnsafeCell lang-item + Inv variance + auto-`!Sync` landed (5bccc7fc); atomics route through MLIR atomic intrinsics with per-variant Ordering (16ed6296, 2d145bf4); definite-assignment analysis landed (5cdf376d); `static`/`static mut` distinct items landed (0508922a, 18003dc5). v2 headline residuals, all probe-confirmed 2026-06-12: (a) **statics have no address identity** — every `&STATIC` materialises a fresh alloca (const-inline convention), so cross-fn `static mut` writes segfault (S25) and atomic-in-static mutation silently hits a copy; (b) **deferred init of a non-`mut` local is rejected** ("assignment to immutable variable") — the spec's own `variable.init` example does not compile; (c) safe `*_ordered` atomic methods over-synchronize to seq_cst (Ordering const-eval only fires at literal intrinsic call sites).

---

## G.1 Interior mutability

### Spec anchor
`/home/victor/cxx/reference/src/interior-mutability.md`: `interior-mut.unsafe-cell` (only legal mutate-through-`&T` channel), `.mut-unsafe-cell`, `.abstraction`, `.ref-cell`, `.atomic`; `special-types-and-traits.md` `lang-types.unsafe-cell.{interior-mut,read-only-alloc}`.

### Match verdict
**OK — lang-item landed, all four DoD pieces verified (logos-core §2.2, commit 5bccc7fc).**

- **Lang-item recognition** — qualified-name match `struct_name()=="UnsafeCell" && pkg_name()=="logos.lang.cell"` at `src/compiler/sema_auto_trait.cpp:153` and `src/compiler/sema.cpp:7420`. No collision with user `UnsafeCell`.
- **Variance Inv-in-T** — `sema.cpp:7414-7420` composes `Variance::Inv` over every UnsafeCell type-arg; `Cell`/`RefCell` inherit through the field.
- **Auto-`!Sync`** — `sema_auto_trait.cpp:153-159`: `Sync`→false, `Send`→defer to wrapped `T`. Structural derivation propagates. Probe 2026-06-12: `need_sync::<Cell<i32>>(&c)` rejects with field-precise diagnostic "`Cell<i32>` does not satisfy auto trait 'Sync' (field 'inner' of type 'UnsafeCell<T>' is not Sync)". v1's "Cell is incidentally Sync" hole is closed.
- **Write exemption** — by-construction via `UnsafeCell::get(&self) -> *mut T` + `unsafe` block (raw-ptr write governed by `*mut` rules, not the `&T` write rule). Same shape Rust documents; `tests/logos/pass/core_2_2_unsafecell_write.logos` pins multi-shared-borrow writes.

Stdlib surface unchanged from v1 except `Cell<T>` now stores `inner: UnsafeCell<T>` (`stdlib/lang/cell/cell.logos:102-103`); `UnsafeCell` at `:36`, `RefCell` `:202`-region, `OnceCell`, `LazyCell` follow.

### Residual debt
- **`lang-types.unsafe-cell.read-only-alloc` — broken by the static-storage gap (G.3).** Interior-mutable statics aren't placed read-only — but they aren't placed *at all*: each use re-materialises the const initializer. Probe: `static G: AtomicI32` + `fetch_add` in fn A, `load` in `main` → reads 0 (mutated a copy). Root = S25 storage class, not an UnsafeCell rule.
- `RefCell::replace`/`swap` (`cell.logos:291-308`) still do raw-ptr moves of `T` without drop bookkeeping for non-Copy `T`; unverified by valgrind. Unchanged WARN from v1.
- Mutex/RwLock still expose `unsafe fn lock(self: *mut …) -> *mut T` (`stdlib/std/sync/sync.logos:62`), not RAII guards — tracked under Fn-family/lifetime-guards catch-up.
- Doc rot: `cell.logos:14-21` header still says "no compiler magic, no `#[lang_item]` registration" — stale since 5bccc7fc; fix comment.
- Imported `tests/imported/fail/variance/variance-cell-is-invariant.logos` substitutes a `*mut T` stand-in for Cell (header comment says so) — it exercises the raw-ptr Inv rule, NOT the UnsafeCell carve-out. A real-`Cell<&'a T>` invariance fail-test is still missing.

---

## G.2 Memory model / atomics

### Spec anchor
`memory-model.md` (stub: bytes init/uninit + optional provenance); `core::sync::atomic` types + 5-variant `Ordering`; `interior-mut.atomic`.

### Match verdict
**WARN — intrinsics + per-variant Ordering landed (was GAP); method-path over-synchronizes; statics break atomic identity.**

Closed since v1:
- ✅ **MLIR atomic intrinsics (commit 16ed6296, §5.1).** `mlir_gen_expr.cpp:1775-1930` short-circuits the eight `logos_atomic_{load,store,fetch_add,cas}{32,64}` callees by bare name into `llvm.load/store atomic`, `AtomicRMWOp`, `AtomicCmpXchgOp`. The `stdlib/rt/` asm stubs are dead code on every call path.
- ✅ **Per-variant Ordering (commit 2d145bf4, §6.14).** `read_ordering_at` (`mlir_gen_expr.cpp:1798-1814`) const-evals a literal `Ordering` EnumLit at the call site: disc 0→`monotonic`, 1→`acquire`, 2→`release`, 3→`acq_rel`, 4→`seq_cst`; non-literal → seq_cst (sound, over-synchronized). `_ord` intrinsic family extended with swap/or/and/xor/cas_weak (`stdlib/lang/atomic/atomic.logos:67-110`). Probe 2026-06-12: direct `logos_atomic_store32_ord(p, v, Ordering::Relaxed)` emits `llvm.store … atomic monotonic`; `…load32_ord(…, Acquire)` emits `atomic acquire`.

Remaining gaps:
1. **Safe `*_ordered` methods always emit seq_cst.** `AtomicI32::store_ordered(self, v, ord)` (`atomic.logos:151`) forwards its runtime `ord` parameter to the intrinsic; inside the precompiled stdlib body the arg is a VarRef, not an EnumLit → const-eval fallback. Per-variant ordering reaches machine code ONLY at direct intrinsic call sites with a literal. §6.14's claim "passing the user-supplied Ordering through so the const-eval sees the literal EnumLit" is wrong for the method path (methods are not inlined; AtomicI32 is non-generic so mono never clones them). Sound but defeats the API's purpose. Fix: inline `_ordered` bodies at sema, or const-prop Ordering args into known-intrinsic wrappers.
2. **No `AtomicUsize`/`AtomicIsize`.** Still absent (grep stdlib = 0); logos-core §6.14 note defers a thin alias to "Wave 6 ergonomics". usize/isize canonicalize to i64 so `AtomicI64` covers semantics — but ports must rename.
3. **Atomic storage is a bare — now `pub` — field.** `AtomicI32 { pub val: i32 }` (`atomic.logos:120`); 2f5ad014 made `val` pub for struct-lit static init (no const fn ⇒ A1/A2-adjacent workaround). Consequences: safe code can read/write `a.val` non-atomically (data-race surface Rust's private field forecloses), and storage is not `UnsafeCell`-wrapped, so mutation goes through `&self → *mut` casts. Replace with `UnsafeCell<i32>` + keep struct-lit const path.
4. **Atomic-in-static is value-broken cross-fn.** `static G: AtomicI32 = AtomicI32 { val: 0 }` is accepted (2f5ad014) but every use re-materialises the initializer (S25 class, see G.3): probe `bump(); bump(); G.load()` → 0, not 2. The §5 scoreboard row "atomic-in-static Rust-conformant" is **init-syntax-only**; semantics await true static storage.
5. **Provenance** — unchanged: BC "provenance" (`borrow_check.cpp`) is region tracking, not byte-level pointer provenance. Matches Rust's own stub status; no work item.
6. **Doc rot:** `docs/language/undefined-behavior.md` "Data races" section still says "Ordering parameter … currently discards it (every atomic op lowers to seq_cst)" — stale since 16ed6296/2d145bf4; update to the method-vs-intrinsic split above.

### Pointers
Surface `stdlib/lang/atomic/atomic.logos` (10 atomic structs, lines 120-515; widths 8/16/32/64 + Bool + Ptr<T>); lowering `src/compiler/mlir_gen_expr.cpp:1775-1930`; tests `tests/logos/pass/{atomic_basic,atomic_narrow,atomic_ordering,core_5_1_atomic_release_acquire,core_6_14_atomics_per_variant_ordering,core_5_adv_atomic_in_static}.logos`. Note: core_6_14 verifies *values* through the method path — it cannot observe the seq_cst over-synchronization (gap 1); only the raw-intrinsic MLIR smoke pins orderings.

---

## G.3 Variables (mutability, scope, statics)

### Spec anchor
`variables.md` `variable.{local,local-mut,param-mut,init}`; `memory-allocation-and-lifetime.md` `alloc.{static,dynamic}`; `items/static-items.md`.

### Match verdict
**WARN — definite-assignment + static/static mut landed; deferred-init regression + static storage identity remain.**

Closed since v1:
- ✅ **Definite-assignment analysis (commit 5cdf376d, §2.7).** `currently_uninit_vars_` forward pass (`sema_decl.cpp:508` reset; `sema_stmt.cpp:1810/1835/2611` insert/erase; `:5610-5621` if/match branch snapshot + post-state union). Probe: branch-partial init rejected "use of possibly uninitialised binding" (E0381 equivalent). `tests/logos/fail/core_2_7_use_before_init.logos`.
- ✅ **Immutable `static` distinct from `const` (0508922a, 927461fc, §6.2-half-1).** `&STATIC: &'static T` types across fn boundaries; static-init may reference other statics/`&statics` (7d7f2ee9).
- ✅ **`static mut` (18003dc5, §6.2-half-2).** `STATIC_DEF` schema code 254 (`logos.peg:313, 683-686`); `module_static_muts_` set; reads (`lower_var_ref`) and writes (`lower_assign`) require `unsafe` per `items.static.mut.safety`; name-shadow pollution fixed (7d7f2ee9 S18). Tests `core_6_2_static_mut{,_read,_write,_name_no_pollution}`.

Open gaps:
1. **NEW (v2 probe): deferred init of non-`mut` local rejected.** `let x: i32; x = 5;` errors "assignment to immutable variable 'x'". Rust allows exactly one delayed initialization of a non-`mut` binding (E0384 fires only on the *second* assign); the `variable.init` spec example itself uses non-`mut` locals initialized in branches → does not compile in Logos. The §2.7 pass landed the *read*-side check but the *write*-side gate conflates "assign to immutable" with "first assign = init". Fix: `lower_assign` must treat an assignment to a name in `currently_uninit_vars_` as initialization, not mutation. Today users must write `let mut`, diverging from Rust.
2. **CRITICAL (S25, probe-confirmed): no static storage class.** Statics reuse `collect_const` storage; mlir-gen materialises a FRESH alloca per var-ref/addr-of. Cross-fn `static mut` write→read **segfaults** (probe `bump()×3` in fn, read in another fn). Same root silently breaks interior-mutable statics (G.2 gap 4) and `&STATIC` address identity (`alloc.static`: "stored uniquely in the memory image"). Fix: emit one `llvm.mlir.GlobalOp` per static + route uses through `llvm.mlir.addressof` (machinery exists — `mlir_gen.cpp:476` vtables do exactly this).
3. **§6.2 deferred residue** (documented in logos-core, still open): S2 `static X` + `fn X` namespace collision accepted; S12 `static F: fn()->i32 = answer;` rejected (fn names invisible at phase-2 const-init); S15 `static mut ARR[i] = …` write doesn't consult `module_static_muts_`; S17 fn-local `static` not in grammar; S20 `static MSG: &str = "…"` double-`&` type error.
4. **Static-item `Sync` requirement unenforced** (probe: `static G: NotSync` with `*mut u8` field accepted) — shared row with H.1.

`let mut`/`mut` params/closure `|mut x|`/shadowing: unchanged-OK from v1 (grammar `logos.peg` `let_stmt`/PARAM `IS_MUT`; `lookup_is_mut`/`is_mut_binding` gates). Shadow-drop timing (drop at scope end, not shadow point) still untested by a targeted case.

`alloc.dynamic` (v1 missed): heap allocations never relocate — Logos Box/Vec conform (malloc-backed; Writ2 never-move segments reinforce). No gap.

---

## Cross-category gaps

- **Static storage class (S25)** — G.3 owns; breaks G.2 atomic-in-static and H.1 static-Sync enforcement point. Single root, three symptoms: fix once in mlir-gen.
- **Deferred-init write gate** — G.3 ↔ Category E (assignment expressions): the immutability check needs init-state awareness.
- **Ordering const-prop through stdlib wrappers** — G.2 ↔ Category M (metacall is the blessed const-eval channel; an Ordering-specializing inline is the non-divergent fix).
- **Real-Cell variance fail-test** — G.1 ↔ Category A: imported variance-cell tests use a `*mut` stand-in; add a `Cell<&'a i64>` test exercising the UnsafeCell Inv rule itself.
- **`global_allocator` attribute** (`runtime.md`, v1 missed) — absent; Logos has no allocator-selection attr. Low priority; flag as catch-up, not blessed.

## Recommended next moves

1. **(Soundness+conformance, ~1 session) True static storage (S25).** One `llvm.mlir.GlobalOp` per static, `addressof` at uses. Closes: static-mut cross-fn segfault, atomic-in-static copy bug, `&STATIC` identity, unsafe-cell.read-only-alloc. Then enforce static-Sync (H) at the same collection point.
2. **(Conformance, ~½ session) Deferred-init assignment gate.** First assign to a declared-uninit non-`mut` binding = initialization. Unlocks the spec's `variable.init` example; add it verbatim as a pass test.
3. **(Perf-conformance, ~1 session) Ordering const-prop into `*_ordered` methods** (sema-inline or wrapper-specialization), then re-pin MLIR orderings through the METHOD path in core_6_14.
4. **(Hygiene, ~½ session)** `AtomicUsize`/`AtomicIsize` aliases; wrap atomic `val` in `UnsafeCell` and re-privatize (keep struct-lit const path via UnsafeCell struct-lit); fix stale comments (`cell.logos:14-21`, `undefined-behavior.md` data-races section).
