# Category K — Unsafe (audit)

v2 — re-audited 2026-06-12 (v1: 2026-05-30); spec: rust-lang/reference (local checkout).

Summary: 2 features audited — 0 OK, 2 WARN (both materially improved since v1). Closed since v1: **`union` item with full soundness** (44e05308 + e989d16a — field-read unsafe gate, safe writes, Copy field restriction, whole-union borrow coercion, max-of-fields layout); **`static mut`** (18003dc5 Wave 8 — declaration, read+write unsafe gates) but the storage model is BROKEN (see S25 below); **`extern "ABI" { … }` blocks** (§6.7 — ABI strings validated: "C", "C-unwind", "system", "Rust"); **UB register doc** `docs/language/undefined-behavior.md` (127122bf — exists, but 3 entries are stale/wrong, see Feature 2); **`UnsafeCell` lang-item** (79b55734); **integer-overflow runtime trap** (b0bc3eb5, 2026-05-05 — v1 misread this as absent; probe: `120i8+120i8` → SIGILL rc=132). Still missing: `unsafe extern { … }` 2024 keyword form (probe: parse error), `#[unsafe(attr)]` wrapper, `unsafe_op_in_unsafe_fn` lint, `#[target_feature]` call rule, `transmute` intrinsic (`stdlib/lang/num/float.logos:21` routes bit-reinterpret through ptr casts instead), Miri-analog for raw-ptr aliasing.

## Feature 1 — `unsafe fn` / `unsafe` block (+ `unsafe trait`, `unsafe impl`, `unsafe extern`, mutable statics, unions, `#[unsafe(attr)]`)

**Rust nomenclature.** `unsafe fn`, `unsafe {}`, `unsafe trait`, `unsafe impl`, `unsafe extern {…}` (edition 2024), mutable/extern statics, `#[unsafe(attr)]`. Spec: `unsafe-keyword.md` / `unsafety.md`. Unsafe-context-requiring ops (`safety.unsafe-ops`): raw-ptr deref, mutable-or-extern-static access, union field read, unsafe-fn call, unsafe-trait impl, extern declaration, target_feature call, unsafe-attribute application.

**Logos nomenclature.**
- Grammar: `KW_UNSAFE` (`logos.peg:361`); `unsafe_block <- KW_UNSAFE block` (`:1759`, stmt `:1793`/`:2425`, expr `:2601`); `unsafe fn` alts in trait_method (`:863-885`) and fn defs; `unsafe trait` (`:814-845`); `unsafe impl` (`:950-966`); `union_def` (`:1144-1146`); `static mut NAME: T = expr;` → `STATIC_DEF` code 254 (`:313, :683-685`); `extern "ABI" { … }` → `EXTERN_BLOCK` code 249 (`:308, :1192-1208`, incl. `static`/`static mut` items inside).
- Sema state: `bool inside_unsafe_` (`sema_impl.hpp:3292`). Push sites: stmt `sema_stmt.cpp:617`, expr `sema_expr.cpp:1397`, `unsafe fn` body implicit `sema_decl.cpp:747`.
- Call gate `fi.is_unsafe && !inside_unsafe_`: **still 11 replicated sites** in `sema_expr.cpp` (`:2845, 3037, 3617, 6172, 6328, 6929, 7467, 7701, 8176, 12712, 12835`) — v1 refactor recommendation not taken.
- Raw-ptr deref gate: `sema_expr.cpp:2415` (+ method/field/index/write projections, same pattern as v1).
- Union field gates: read requires unsafe `sema_expr.cpp:8917-8933` ("field read of `U.a` requires `unsafe` block"); writes safe `sema_stmt.cpp:6858` (`items.union.fields.write`); field-type Copy restriction `sema_collect.cpp:1465-1510`; fieldless rejected `:1454-1461`; struct/union shared namespace `:359-376`; borrow of one field borrows the whole union `borrow_check.cpp:678-680`.
- Static-mut gates: collection `sema_collect.cpp:1842-1848` (`module_static_muts_`); read gate `sema_expr.cpp:542-546`, write gate `sema_stmt.cpp:2409-2419`, both shadow-aware ("requires `unsafe` block (Rust `items.static.mut.safety`)"). Probes: write-outside-unsafe rejected ✓; `unsafe { A=A+1; }` round-trip ✓.
- Extern: `extern "ABI" {}` blocks flattened with ABI inheritance `sema_collect.cpp:1268-1321`; ABI validated against {"C","C-unwind","system","Rust"} (`:1275-1283`); every `EXTERN_FN` auto-marked `is_unsafe = true` (`:4485`) so calls hit the same gate.
- Unsafe-trait/impl parity: `sema_collect.cpp:3546-3589` (4 diagnostics, unchanged semantics from v1).

**Match verdict.** WARN — the unsafe-op set now covers raw-ptr deref, unsafe-fn call, unsafe-trait impl, **union field read** (✅ closed 44e05308), **mutable-static access** (✅ gate closed 18003dc5), extern-fn calls. Remaining surface gaps:
- `unsafe extern { … }` keyword form (`unsafe.extern.edition2024`) — **GAP**. `extern "C" { fn …; }` parses and runs (probe rc=42); prefixing `unsafe` is a syntax error (probe). One-token grammar fix.
- Extern **immutable** static (`static NAME: T;` inside extern block) folds to `CONST_DEF` (`logos.peg:1207-1208`) — reads are NOT unsafe-gated. Rust `safety.unsafe-static` covers *extern* statics too. **GAP** (small).
- `#[unsafe(attr)]` wrapper (`safety.unsafe-attribute`) — **GAP** (grep `unsafe(` in grammar: 0).
- `unsafe_op_in_unsafe_fn` opt-in lint — **GAP** by design: `sema_decl.cpp:747` makes an `unsafe fn` body one giant unsafe block.
- `#[target_feature]` call rule — **GAP** (attribute absent, L-side).
- **`static mut` storage model broken (S25-class, CRITICAL):** `STATIC_DEF` reuses const storage; mlir-gen materialises a fresh alloca per access instead of one `llvm.mlir.global`. Documented residual (logos-core §6.2 S25: cross-fn read segfaults). **NEW v2 finding:** even single-fn read-before-first-write returns garbage — probe `static mut A: i32 = 7; return unsafe { A };` → rc=48 (≠7). Write-then-read in `main` works (probe init 5,+1,+1→7 ✓), which is why the suite test stays green. The gates are right; the codegen is wrong.

**Implementation pointer.** `logos.peg:361, 1759, 1144, 683-685, 1192`; `sema_impl.hpp:3292`; push `sema_stmt.cpp:617` / `sema_expr.cpp:1397` / `sema_decl.cpp:747`; union gates `sema_expr.cpp:8917` / `sema_collect.cpp:1432-1510`; static-mut gates `sema_expr.cpp:542` / `sema_stmt.cpp:2409`; extern `sema_collect.cpp:1268-1321, 4485`; trait/impl parity `sema_collect.cpp:3546-3589`.

**Interactions check.**
- **Raw pointers (deref)** — OK (`sema_expr.cpp:2415` + projections). Unchanged.
- **`transmute`** — n/a / GAP — still absent (grep `transmute` in `src/compiler/`: 0; `stdlib/lang/num/float.logos:21` comment: "Logos doesn't expose a transmute intrinsic", uses `&x as *const f32 as *const u32` under unsafe). NOTE: `undefined-behavior.md:49-50` claims transmute size-mismatch is "REJECTED at compile" — **doc wrong**, no such intrinsic exists.
- **Union field access** — ✅ closed (44e05308 item + e989d16a 11 conformance fixes). Probes: read outside unsafe rejected ✓; write-safe + unsafe-read runs (rc=35) ✓. 20 `core_6_1_union_*` tests.
- **Calling unsafe fns** — OK (11 gate sites; refactor debt stands).
- **Implementing unsafe traits** — OK (`sema_collect.cpp:3546-3589`; stdlib `unsafe impl Send/Sync` in `stdlib/std/sync/`).
- **FFI (`extern`)** — WARN: block form + ABI validation ✅ closed (§6.7); `unsafe extern` keyword + non-default calling-convention codegen ("Wave 6 follow-up", logos-core:1580) remain.
- **Inline asm** — n/a / GAP — unchanged (no `asm!`).
- **`#[unsafe(attr)]`** — GAP, unchanged.

**Gaps / debt.**
1. **`static mut` codegen** — real `llvm.mlir.global` + `addressof` routing (S25 + v2 read-before-write garbage). Highest-priority K item: silent wrong values inside `unsafe`.
2. `unsafe extern { … }` keyword form — grammar-mechanical.
3. Extern immutable-static reads not unsafe-gated (`safety.unsafe-static` partial).
4. `#[unsafe(attr)]` wrapper — pending L-side attribute machinery.
5. `unsafe_op_in_unsafe_fn` — needs a lint stack (L-side pair).
6. 11 duplicated call-gate sites → one `require_unsafe_for(fi, name)` helper ([[feedback_derive_from_foundation]]; carried from v1 unaddressed).
7. `transmute` — when added, sema-time size/layout check (`undefined.invalid`).

## Feature 2 — Behavior considered undefined (UB list)

**Rust nomenclature.** `behavior-considered-undefined.md` anchors `undefined.race / .pointer-access / .place-projection / .alias / .immutable / .intrinsic / .target-feature / .call / .invalid / .asm / .runtime` + misaligned/dangling/validity subsections. Companion `behavior-not-considered-unsafe.md` (deadlocks, leaks, overflow, logic errors).

**Logos nomenclature.** ✅ `docs/language/undefined-behavior.md` exists (127122bf closes v1 gap #1): per-anchor enforced/PARTIAL/UNENFORCED register, every non-enforced anchor carries a `**Follow-up:**` line; panic strategy abort-only (§A7) noted up front.

**Match verdict.** WARN — the register is the right artifact, and enforcement improved since v1, but the doc has drifted from the code in three places and its anchor IDs don't match the spec's:
- **Integer overflow — ✅ closed (b0bc3eb5, pre-v1; v1 verdict was wrong).** `+`/`-`/`*` lower to LLVM with-overflow intrinsics + `llvm.intr.trap` (`mlir_gen_expr.cpp:706-726`); `wrapping_*` intrinsics opt out. Probe: SIGILL rc=132. Stronger than Rust's debug-panic/release-wrap (always-trap; consistent with §A7 abort). **Doc stale:** `undefined-behavior.md:133-141` still says "runtime overflow wraps … no debug-mode panic".
- **Atomics `Ordering` — ✅ closed (16ed6296 + 2d145bf4 §6.14).** Per-variant Ordering threads to MLIR atomic ops (`mlir_gen_expr.cpp:1790-1798`, disc→AtomicOrdering map matching Rust layout). **Doc stale:** `undefined-behavior.md:18-22` still says "accepts an Ordering parameter but currently discards it".
- **`transmute` size check — doc WRONG:** `undefined-behavior.md:49-50` documents a compile-time size-mismatch rejection for an intrinsic that does not exist (see Feature 1).
- **Anchor fidelity:** doc uses `undefined.deref/.mut_immutable/.aliasing/.dangling`; spec anchors are `undefined.pointer-access/.immutable/.alias/.dangling.*`. Cosmetic but breaks grep-ability against the spec.

**Implementation pointer.** Register: `docs/language/undefined-behavior.md`. Enforcement: BC exclusivity (`borrow_check.cpp`, union coercion `:678`); overflow trap `mlir_gen_expr.cpp:706`; atomic ordering `mlir_gen_expr.cpp:1790`; `UnsafeCell` lang-item `sema_auto_trait.cpp:145-153` + variance `sema.cpp:7414-7420` (interior-mutability carve-out; BC write-path carve-out still follow-up, logos-core §2.2 partial); drop elaboration (B8).

**Interactions check** (delta from v1; unchanged rows elided).
- **Memory model** — PARTIAL (was GAP): Ordering threads per-variant to MLIR; formal model + weak-target codegen still open.
- **References (aliasing)** — PARTIAL via BC, unchanged; no Stacked/Tree-Borrows analog; raw-ptr aliasing inside `unsafe {}` unchecked.
- **Mutability violations** — PARTIAL: `&T`/`*const T` writes rejected; `&T → *mut T → write` still compiles inside `unsafe {}` (UnsafeCell carve-out recognised for auto-traits/variance, not yet in BC write path).
- **Unaligned access / invalid values** — GAP for every validity sub-rule, unchanged (no `assume`s, no niche-validity checks; `transmute` absent limits the attack surface).
- **Behavior NOT considered unsafe** — overflow now traps (✅, stronger than spec's debug-panic floor); leaks/deadlocks same as Rust.

**Gaps / debt.**
1. **Sync `undefined-behavior.md` with reality** — 3 stale/wrong entries (overflow, atomics, transmute) + spec-anchor IDs. One-hour doc fix; the register's value is exactness.
2. `static mut` codegen (Feature 1 #1) is also the `undefined.runtime`-adjacent hole: wrong values without any UB-rule being violated by the user.
3. No misalignment checks (compile or debug-runtime).
4. No Miri-analog for raw-ptr aliasing inside `unsafe {}`.
5. `transmute` absent → `undefined.invalid` rules moot; design sema-checked `transmute<T,U>` (with B2/B3 custom-DST casts as the forcing client).

## Cross-category gaps

- **G ↔ K:** `UnsafeCell` lang-item landed (79b55734) for auto-traits + variance; the BC write-path carve-out (`&UnsafeCell<T>.get()` vs generic `&T → *mut T`) is the remaining §2.2 half.
- **N ↔ K:** `unsafe extern` keyword + calling-convention threading (logos-core §6.7 "Wave 6 follow-up"); extern immutable-static gating.
- **B ↔ K:** `union` landed — `safety.unsafe-union-access` now enforced; `transmute` remains the B/K joint item.
- **L ↔ K:** `#[unsafe(attr)]` wrapper needs L-side syntax; `unsafe_op_in_unsafe_fn` needs L-side lint stack.
- **M ↔ K:** `undefined.validity.const-provenance` still moot (no const-eval; §A1 metacall).

## Recommended next moves

1. **`static mut` real-global codegen** (S25 + read-before-write garbage) — `llvm.mlir.global` once, `addressof` at each access. Critical correctness; everything else in §6.2 is cosmetic next to it.
2. **Sync `undefined-behavior.md`** — fix overflow/atomics/transmute entries, align anchor IDs to spec.
3. **`unsafe extern { … }` + extern-static gating** — small grammar + collect changes; closes `safety.unsafe-extern`/`safety.unsafe-static` fully.
4. **Harmonise the 11 call-gate sites** into one helper (carried from v1).
5. **`transmute<T,U>` design** (with size/layout sema check) — unblocks B3-style casts and gives `undefined.invalid` teeth (carried from v1).
