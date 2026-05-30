# Category K — Unsafe (audit)

Generated: 2026-05-30; spec: rust-lang/reference (local checkout)

Summary: 2 features audited — 0 fully OK, 2 WARN. Headline gaps: (a) **most of the `unsafe`-keyword surface beyond `unsafe fn`/`unsafe {}`/`unsafe trait`/`unsafe impl` is missing**: no `unsafe extern { … }` blocks (Logos uses bare `extern fn` items — auto-marked unsafe but the *block* form doesn't exist); no `unsafe static …`; no `#[unsafe(attr)]` attribute wrapper; no `unsafe_op_in_unsafe_fn` lint (Logos always treats an `unsafe fn` body as one giant unsafe block, with no opt-in to require nested `unsafe {}`); (b) **the Rust UB list has zero language-level enforcement** in Logos — `inside_unsafe_` simply gates raw-ptr deref/method/field/index ops and unsafe-fn calls, but there is no compiler awareness of misalignment, dangling, aliasing, invalid values (bool ≠ 0/1, `char` surrogates, niche ranges, `&` non-null), `transmute` size match, data races, mutating immutable bytes, or unwinding-across-ABI; (c) `static mut` is not in the grammar at all (G-audit notes `static NAME: T = expr;` collapses to `CONST_DEF`); (d) no `union` item (no UB rules for union read could exist); (e) no `transmute` intrinsic in stdlib (grep returns 0). Practical impact: any imported Rust test that relies on the compiler refusing UB-prone misuses passes by accident or via the BC's exclusivity check, not via an unsafety/UB rule.

## Feature 1 — `unsafe fn` / `unsafe` block (+ `unsafe trait`, `unsafe impl`, `unsafe extern`, `unsafe static`, `#[unsafe(attr)]`)

**Rust nomenclature.** `unsafe fn`, `unsafe {}` block, `unsafe trait`, `unsafe impl`, `unsafe extern {…}` block (edition 2024), `unsafe static`, `#[unsafe(attr)]` attribute wrapper. Spec: `unsafe-keyword.md` / `unsafety.md`. Operations that REQUIRE an unsafe context (`unsafety.md` §unsafe-ops): raw-pointer deref, mutable-or-extern-static read/write, union field read, unsafe-fn call, unsafe-trait impl, extern block declaration, target_feature call, unsafe-attribute application.

**Logos nomenclature.**
- Grammar keyword: `KW_UNSAFE = "unsafe"` at `tools/peg_gen/grammars/logos.peg:350`.
- Grammar productions: `unsafe_block <- KW_UNSAFE block` at `tools/peg_gen/grammars/logos.peg:1628` (used in stmt position `:1662`, expr position `:2441`, and a third site `:2267`); `unsafe fn` lifted via `KW_UNSAFE KW_FN …` alts at `:833-847, 990-1007, 1146-1182`; `unsafe trait` at `:791-822`; `unsafe impl` at `:898-916`; `unsafe fn(…)` POINTER TYPE at `:1510-1516`.
- AST flag: `IS_UNSAFE = 42` (`tools/peg_gen/grammars/logos.peg:62`); node code `UNSAFE_BLOCK = 132` (`:191`).
- Sema state: `bool inside_unsafe_ = false;` at `src/compiler/sema_impl.hpp:2899`. Storage of `is_unsafe` on `SemaFnInfo` (`sema_impl.hpp:1983`), `SemaTraitInfo` (`:2010`), `SemaImplInfo` (`:2027`), per-method (`:1932`).
- Unsafe-context push: stmt-form `src/compiler/sema_stmt.cpp:533-538`; expr-form `src/compiler/sema_expr.cpp:1194-1220`; **`unsafe fn` body implicitly opens an unsafe context** at `src/compiler/sema_decl.cpp:661-663, 828`.
- Call-site enforcement: `if (fi.is_unsafe && !inside_unsafe_) error("call to unsafe function …")` at `src/compiler/sema_expr.cpp:3213` (and replicated at `:2587, :2769, :5659, :5815, :6348, :6859, :7090, :7558, :11609, :11742` for the various method/UFCS paths).
- Raw-ptr deref gate: `src/compiler/sema_expr.cpp:2166-2167` ("dereference of raw pointer requires unsafe context"); pointer-method gating `:5601, :5619`; method-via-ptr `:6168, :6185, :7568`; field-read-via-ptr `:7968-7977, :8080`; index-via-ptr `:9032-9033`; chain-field/index/deref WRITE `src/compiler/sema_stmt.cpp:448-449, 516-517, 6210, 6238, 6269, 6497`.
- Unsafe-fn implicitly unsafe body (no `unsafe_op_in_unsafe_fn` lint): `sema_decl.cpp:661-663`.
- Unsafe-trait/impl parity gating: `src/compiler/sema_collect.cpp:3007-3024` (errors: "implementing unsafe trait requires `unsafe impl`", "`unsafe impl` for a safe trait", "`unsafe impl` for a safe built-in trait Copy", "standalone impl cannot be unsafe"); per-method parity at `:2880-2884`.
- Extern fn auto-marking: `src/compiler/sema_collect.cpp:3855-3858` — `EXTERN_FN` items get `is_pub = is_unsafe = is_extern = true`, so calls to them go through the same `fi.is_unsafe` gate at `sema_expr.cpp:3213`. This substitutes for the missing `unsafe extern {…}` block form.

**Match verdict.** WARN — core naming matches Rust (`unsafe fn`, `unsafe {}`, `unsafe trait`, `unsafe impl`, `unsafe fn(…)` ptr) but four Rust-spec surface forms are MISSING and one is partially present:
- `unsafe extern { … }` block — **GAP** (no production; only bare `extern fn IDENT(…)` items exist at `logos.peg:1122-1127`, no `extern { … }` syntactic block). Logos compensates by auto-flagging every `extern fn` as unsafe.
- `unsafe static …` (mutable / external statics) — **GAP** (no `static mut`; `logos.peg:664-671` accepts only immutable `static NAME: T = expr;` and folds it to `CONST_DEF`).
- `#[unsafe(attr)]` attribute wrapper — **GAP** (grep `unsafe(` in grammar/sema: 0 matches).
- `unsafe_op_in_unsafe_fn` opt-in lint — **GAP** by design today: `sema_decl.cpp:661-663` unconditionally treats an `unsafe fn` body as one giant unsafe block. No lint, no opt-in.
- `target_feature`-safety call rule (`safety.unsafe-target-feature-call`) — **GAP** (no `#[target_feature]` attribute in Logos).

**Implementation pointer.** `tools/peg_gen/grammars/logos.peg:350, 1628`; `src/compiler/sema_impl.hpp:2899` (`inside_unsafe_` flag); `src/compiler/sema_expr.cpp:1191-1224` + `src/compiler/sema_stmt.cpp:532-539` (push/pop); `src/compiler/sema_decl.cpp:661-663` (unsafe-fn body implicit); `src/compiler/sema_collect.cpp:3007-3024` (unsafe trait/impl parity); call gate `src/compiler/sema_expr.cpp:3213`; raw-ptr deref gate `:2166-2167`.

**Interactions check** (vs the feature-interactions table edge list for "`unsafe fn` / `unsafe` block"):
- **Raw pointers (deref)** — OK. `sema_expr.cpp:2166-2167` rejects `*p` for `p: *const/*mut T` outside an unsafe context. Field-read via raw ptr (`sema_expr.cpp:7968-7977`), method via raw ptr (`:6168-6186, 7568`), index via raw ptr (`:9032-9033`), and the WRITE-side gates (`sema_stmt.cpp:448-517, 6210-6497`) cover the place projections. Aligned with Rust's `safety.unsafe-deref`.
- **`transmute`** — n/a / GAP — feature absent. Grep `transmute` across `src/` and `src/stdlib/` returns 0 matches. Logos has no `mem::transmute` intrinsic and no `as`-cast with size/init check beyond standard `expressions.md` cast rules. Cannot be "unsafe-gated" since it doesn't exist.
- **Union field access** — n/a / GAP — feature absent. Grep `KW_UNION` / `^union` in `logos.peg` returns 0 matches. `union { f: T, g: U }` is not parseable; the spec's `safety.unsafe-union-access` rule cannot apply.
- **Calling unsafe fns** — OK. `sema_expr.cpp:3213` enforces `fi.is_unsafe && !inside_unsafe_` → "call to unsafe function …"; mirrors `safety.unsafe-call`. Replicated for method/UFCS/generic call paths.
- **Implementing unsafe traits (`Send`/`Sync` manually)** — OK. `sema_collect.cpp:3007-3024` enforces `unsafe impl` parity; trait-method `unsafe`-parity check at `:2880-2884`. Used in stdlib at `stdlib/std/sync/sync.logos:85-86, 152-153`. Mirrors `safety.unsafe-impl`.
- **FFI (`extern fn`)** — WARN divergent surface (likely catch-up TODO per [[ref_divergences_register]] §B rule). Logos accepts bare `extern fn name(…) -> R;` items (`logos.peg:1122-1127`) and auto-marks them unsafe at `sema_collect.cpp:3855-3858`. Rust 2024 requires an `unsafe extern "ABI" { fn …; }` BLOCK; the block form is missing and `"ABI"` strings (`"C"`, `"system"`, `"C-unwind"`, …) aren't parsed. So `extern fn` calls *are* unsafe-gated (good), but the surface diverges (bug, not blessed in `docs/DIVERGENCES.md`). Mirrors partial coverage of `safety.unsafe-extern`.
- **Inline asm** — n/a / GAP — feature absent. No `asm!` macro / `INLINE_ASM` node (grep returns 0 outside `mlir_gen` LLVM-side comments). `safety` does not list asm directly but `behavior-considered-undefined.md` r[undefined.asm] does.
- **`#[unsafe(attr)]` attribute wrapper** — GAP. No `unsafe(...)` attribute syntax (grep returns 0 in grammar). Required by `safety.unsafe-attribute` for attrs with safety obligations (`#[unsafe(no_mangle)]`, `#[unsafe(export_name = "…")]`, `#[unsafe(link_section = "…")]`).

**Gaps / debt.**
1. **`unsafe extern { … }` block form missing** — bare `extern fn` items work but the spec-form block is unparseable. Catch-up TODO; not in `docs/DIVERGENCES.md`.
2. **`static mut` missing** — even with `unsafe { static_mut_var = … }`, the declaration form isn't accepted. Concrete grammar gap (`logos.peg:664-671`). Per G-audit headline.
3. **`#[unsafe(attr)]` wrapper missing** — once attribute machinery lands more attrs, the safety-obligation wrapper has to exist.
4. **`unsafe_op_in_unsafe_fn` opt-in not implementable** — Logos treats every `unsafe fn` body as one giant `unsafe` block (`sema_decl.cpp:663`). Rust's lint is allow-by-default but warn-by-default in edition 2024; align eventually.
5. **`target_feature`-call safety rule** missing entirely — but `#[target_feature]` itself is missing, so this is downstream of the attribute system.
6. **Duplicated call-gate logic** — `fi.is_unsafe && !inside_unsafe_` is replicated at 11 sites in `sema_expr.cpp` (`:2587, 2769, 3213, 5659, 5815, 6348, 6859, 7090, 7558, 11609, 11742`). Refactor to one helper `require_unsafe_for(fi, callee_name)` — derive-from-foundation per [[feedback_derive_from_foundation]].

## Feature 2 — Behavior considered undefined (UB list)

**Rust nomenclature.** "Behavior considered undefined" (`behavior-considered-undefined.md`); spec list with anchors `undefined.race / .pointer-access / .place-projection / .alias / .immutable / .intrinsic / .target-feature / .call / .invalid / .asm / .runtime` plus the structured "Pointed-to bytes", "Places based on misaligned pointers", "Dangling pointers", "Invalid values" subsections (bool/char/never/scalar/str/enum/struct/union/reference-box/wide/valid-range/const-provenance). Companion: `behavior-not-considered-unsafe.md` (deadlocks, leaks, overflow-wraps, logic errors).

**Logos nomenclature.** No equivalent of a "UB list" or compiler-enforced invalid-value check exists. Grep `behavior considered undefined`, `undefined behavior`, `behavior_undefined`, `invalid value` in `src/compiler/` returns **0 matches**. The closest mechanism is the `inside_unsafe_` gate (Feature 1), which is a *syntactic* opt-in to "you take the obligation", not a *semantic* UB-list. Wrapping-arithmetic intrinsics exist (`wrapping_add` / `wrapping_sub` / `wrapping_mul` at `src/compiler/mlir_gen_expr.cpp:1666-1701`); two's-complement on overflow is the implicit default for non-wrapping ops (no `debug_assert!`-on-overflow panic codegen) — diverges from `behavior-not-considered-unsafe.md` §Integer overflow (which requires a debug-mode panic).

**Match verdict.** WARN — the spec-rule "Rust programs must never cause undefined behavior, regardless of `unsafe`" has no compiler enforcement. Logos treats `inside_unsafe_ = true` as "the programmer is responsible" and emits LLVM IR without invariant assumptions or layout checks beyond what the type-checker already infers. Many spec UB cases (misalignment, aliasing, mutating immutable bytes, invalid bool/char/enum-disc, dangling, ABI mismatch) are not even attempted, and a few are silently exposed (raw `*mut` arithmetic, transmute-via-`as`-cast through pointer types is not size-checked).

**Implementation pointer.** No dedicated source. Indirect coverage:
- BC exclusivity (`src/compiler/borrow_check.cpp` — Cat-A audit) blocks some aliasing UB *in safe code*; `unsafe { … }` removes that check (`feedback_no_defer_fix_now_generalize` notwithstanding, G-audit notes "BC trusts whatever lives inside `unsafe { … }`").
- Drop elaboration (`docs/DIVERGENCES.md` §B7 area) plus Drop-before-replace handles part of `undefined.runtime` (Rust-runtime assumption: destructors run). No `longjmp` / asm `noreturn` story.
- Atomic ops lower to `seq_cst` only (G-audit §Memory model) — UB rules for misordered atomics (`undefined.race`) aren't even modelled because `Ordering` doesn't propagate.

**Interactions check** (vs the feature-interactions table edge list for "UB list"):
- **Memory model** — GAP. No formal memory model (G-audit §Memory model: `lower atomics to seq_cst on x86; no codegen pathway for weaker targets`). `undefined.race` unenforced.
- **References (aliasing)** — PARTIAL via BC (Cat-A). Inside `unsafe { … }` aliasing checks are off; spec `undefined.alias` (the "`&T` immutability while live" + "`&mut T` exclusivity while live" rules) is not enforced at all through raw pointers. No Stacked-Borrows / Tree-Borrows analog.
- **`transmute` (size/init)** — n/a — `transmute` absent. Spec rule `undefined.invalid` (re-interpreting a pointer as int loses provenance under const) has no surface.
- **Unaligned access** — GAP. No alignment-check IR emission, no rejection of `ptr::read_unaligned`-style ops (which don't exist as intrinsics). `undefined.misaligned` is unenforced.
- **Mutability violations** — PARTIAL via BC. Immutable-byte UB (`undefined.immutable` — mutating bytes reachable through `&T`/`&[]`/`static`) is not enforced for raw-pointer writes; G-audit confirms `&T → *mut T → write` compiles inside `unsafe { }`.
- **Atomics misuse** — GAP (see Memory model above).
- **Raw pointer rules** — PARTIAL via the unsafe gate. Raw-ptr arithmetic (`add`/`sub`/`byte_add`/`offset_from`) requires unsafe (`sema_expr.cpp:5601, 5619`) but bounds (`pointer#method.offset` ⇒ `undefined.place-projection`) are not checked.
- **Invalid values** — GAP for every sub-rule:
  - `undefined.validity.bool` (bool ∈ {0,1}): no IR-side `assume`, no codegen check. Logos `bool` is `i1` so most paths can't form an invalid bool, but `transmute` is absent so the surface is limited.
  - `undefined.validity.char`: no surrogate check. A `char` is `u32` internally (sema), no UTF-32-validity assumed.
  - `undefined.validity.fn-pointer` (non-null): no check.
  - `undefined.validity.never`: never-type inhabitation is treated as unreachable in match exhaustiveness but no UB-class wrong-value protection beyond that.
  - `undefined.validity.enum`: enum discriminant validity is *implicitly* trusted at lowering (gen_match scrutinee) — niche optimization not in use (G + B audits), so range-of-disc is wide.
  - `undefined.validity.reference-box` (aligned, non-null, pointing to valid): no alignment IR emission; references can be constructed from `unsafe { &*ptr }` with no check.
  - `undefined.validity.wide`: slice fat-pointer length and `dyn` vtable validity are trusted.
  - `undefined.validity.valid-range`: no `NonZero`/`NonNull`-style niche; spec rule moot.
- **Behavior NOT considered unsafe** (`behavior-not-considered-unsafe.md`):
  - Integer overflow: Logos lacks `debug_assert!`-on-overflow panics; `wrapping_*` intrinsics work; Rust requires panic in debug builds. WARN divergent (`mlir_gen_expr.cpp:685` comment acknowledges); not in `docs/DIVERGENCES.md` §A as blessed.
  - Leaks / deadlocks / exiting without destructors: same as Rust (not unsafe).

**Gaps / debt.**
1. **No UB list anywhere in the compiler or docs** — there is no `docs/UB.md` / `docs/language/undefined-behavior.md`. Even a stub mapping each Rust UB anchor to "enforced / partial / N/A in Logos" would be load-bearing for unsafe-code authors.
2. **No misalignment check** — neither at IR-emit time nor at runtime (debug-mode `debug_assert_aligned`-equivalent).
3. **No `transmute`** — when added, must size+layout-check at sema time (`undefined.invalid` consequences).
4. **No invalid-value sanitization on `transmute` / `as`-cast through ptr** — must reject UB-prone reinterpretations.
5. **Integer overflow debug-panic missing** — diverges from `behavior-not-considered-unsafe.md` §Integer overflow. Either ship checked-arith debug intrinsics or file as blessed divergence in `docs/DIVERGENCES.md` §A.
6. **Stacked/Tree-borrows analog** — for raw-pointer aliasing inside `unsafe { … }`, Logos's BC turns off. Rust's MIR layer doesn't enforce either, but Miri does. Logos has no Miri-equivalent.
7. **`#[link_section]`/`#[link_name]`/`#[no_mangle]`** unsafe attributes — see Feature 1 gap #3.

## Cross-category gaps

- **G (Memory / safety) ↔ K (Unsafe):** `UnsafeCell` lang-item carve-out (G-audit headline) is the foundation that makes `&T → &mut interior` legal — Logos elides this in favor of "`unsafe { … }` trusts the author". This means K's spec rule `safety.unsafe-deref`/`safety.unsafe-union-access` IS enforced, but the G-side "shared-ref freeze" rule is not, so unsound `&T → *mut T` writes pass.
- **H (Concurrency) ↔ K:** Auto-trait `unsafe impl` for `Send`/`Sync` is OK (Cat-H §`unsafe impl — OK`). Rust requires `unsafe impl Send for Arc<T>` (H-audit headline #1); the stdlib gap is independent of K but unblocks K-style "implementer takes the obligation" patterns.
- **N (FFI / linkage / ABI) ↔ K:** `unsafe extern { … }` (K Feature 1 gap #1) is a Cat-N item; `extern fn` items work but the block form + `"ABI"` string is N-side surface.
- **B (Type system) ↔ K:** `union` (B-audit doesn't cover it — confirmed missing) is the host for `safety.unsafe-union-access`. Adding `union` adds a K-rule.
- **L (Attributes) ↔ K:** `#[unsafe(attr)]` wrapper (Feature 1 gap #3) is an L-side syntactic feature with K-side semantics — needs both.
- **M (Const eval) ↔ K:** `undefined.validity.const-provenance` cites const-context provenance restrictions; Logos has no const-eval beyond simple constant-folding (per `project_no_const_eval.md`), so this UB rule is moot until that lands.

## Recommended next moves

1. **Single-session: write `docs/language/undefined-behavior.md`** — mirror `behavior-considered-undefined.md` section-by-section with one line each: "enforced via X / partial via Y / unenforced (gap N)". Cite file:line. Cost: 1-2h. Pays off every future unsafe-related test review.
2. **Single-session: harmonise the 11 `fi.is_unsafe && !inside_unsafe_` sites in `sema_expr.cpp` (`:2587, 2769, 3213, 5659, 5815, 6348, 6859, 7090, 7558, 11609, 11742`) into a single helper** — `require_unsafe_for_call(fi, callee_name, line)`. Per [[feedback_derive_from_foundation]] (derive from one foundation). Pure refactor; one fix-site if we ever change the rule (e.g. add a "warn-by-default" mode).
3. **Single-session: file three DIVERGENCES.md §B catch-up entries** — (i) `static mut` missing, (ii) `unsafe extern { … }` block form missing, (iii) integer-overflow debug-panic missing. Each is a known gap not currently in the register, blocking later "did we converge with Rust?" audits.
4. **Single-session: add `#[unsafe(no_mangle)]` parse + sema** as a thin first instance of the `#[unsafe(attr)]` wrapper — `no_mangle` is the most common unsafe attribute and unblocks FFI ergonomics.
5. **Multi-session (do NOT defer the design, but defer the impl in one block per [[feedback_draw_the_boundary_not_thrash]]):** decide whether to grow `union` and `transmute` together as a Cat-B/K pair, OR ship `transmute<T,U>` first (gated `where sizeof(T) == sizeof(U) && align_of(T) >= align_of(U)`) since stdlib already wants it for `Box<[T]>` ↔ `Box<dyn>` casts (B3). This is the highest-leverage K-side feature add — it would expand the UB list's surface significantly and force the `undefined.invalid` rules to gain teeth.
