# Category O — Other Panic Divergence (audit)

v2 — re-audited 2026-06-12 (v1: 2026-05-30); spec: rust-lang/reference (local checkout). Verdicts re-verified against code + compile probes (`/tmp/o*.logos`).

Summary: 2 features audited — 0 OK, 2 WARN. Since v1: the panic-strategy divergence is now **formally blessed as `docs/DIVERGENCES.md` §A7** (abort-only, no Drop-on-unwind, `catch_unwind` = test-harness setjmp shortcut, UnwindSafe markers parse-only, supervisor-process recovery model) — v1's "not in DIVERGENCES" WARN items are resolved by that row. `unreachable!()`/`todo!()`/`unimplemented!()` landed as compiler builtins (b0aa262d, §6.11). Never/divergence closed end-to-end (logos-core §1.1): one-directional coercion, `loop {}` → `!`, generalized divergent-call predicate (eb894e80), Rust-2024 `!`-fallback (7f789b9f). **New critical finding: the §6.11 macros ICE (compiler segfault) in match-arm position, and `panic!()` in match-arm types as `void` — the Never-typing of the format-family builtins only works in if-arm/tail positions.**

---

## Feature 1: Panic

**Rust nomenclature.** `reference/src/panic.md`: `#[panic_handler]`, panic strategy (`-C panic=unwind|abort`), unwinding (+ across FFI), `catch_unwind`, `UnwindSafe`/`RefUnwindSafe`/`AssertUnwindSafe`, `PanicInfo`, `Location`/`#[track_caller]`, `panic!`/`unreachable!`/`todo!`/`unimplemented!`. `runtime.md`: `#[global_allocator]`, `#[windows_subsystem]` (v1 missed `runtime.md` — added below).

**Logos nomenclature.** (file refs re-verified 2026-06-12)
- Runtime: `pub fn panic(msg: str) -> !` at `stdlib/lang/panic/panic.logos:29` → stderr write + `logos_panic_maybe_longjmp` + `abort()`. Recovery runtime `stdlib/rt/test_recovery.c`.
- `catch_unwind(f: fn()) -> Result<(), str>` at `panic.logos:88`; `Location`/`PanicInfo` `:55-75`; `UnwindSafe`/`RefUnwindSafe`/`AssertUnwindSafe` `:116-124` (markers only).
- `panic!`/`assert!` family: sema builtin format-family fast-path (`sema_expr.cpp:17929`, `:18170`) → `__fmt_panic` (`stdlib/std/fmt/fmt.logos`).
- **`unreachable!`/`todo!`/`unimplemented!`** — compiler builtins in `lower_builtin_macro` (`sema_expr.cpp:17415`, branch `:17645-17656`), each expanding to `panic!(<prefix>: {}, format!(args))`; landed b0aa262d (§6.11). Test `core_6_11_never_macros` (if-arm forms).
- `#[should_panic(expected=…)]` harness wiring unchanged.

**Match verdict: WARN — semantics divergent BY BLESSED DESIGN (§A7) + two real typing/ICE gaps in the macro family.**

§A7 (docs/DIVERGENCES.md) now governs: abort-only strategy, no unwinding, Drop does NOT run on panic, `catch_unwind` is a harness shortcut (skips destructors, fn-ptr-only, `Result<(), str>`), markers parse-only, `extern "C-unwind"` moot, `cfg(panic="abort")` cosmetic, supervisor-process recovery for production. Imported tests relying on unwind semantics → permanently skipped per A7(f). v1 gap items 2/3/5/6 and the "not in DIVERGENCES" flags are closed by this row; remaining non-blessed items below.

Probe matrix (2026-06-12):
- ✅ `unreachable!()` / `todo!()` / formatted forms in **if-arm** position (o3b; test `core_6_11_never_macros`).
- ❌ **ICE:** `unreachable!()` or `todo!()` as a **match-arm** expression (`_ => unreachable!()`) **segfaults the compiler** (o3/o3d, exit 139, no diagnostic). Critical — silent crash on a canonical Rust idiom.
- ❌ `panic!("…")` as a match-arm expression → "match expression: arm type 'void' is incompatible with '{integer}'" (o3c). The format-family sema-inline doesn't type as Never in the match-arm unifier (if-arm path does).
- ✅ user `fn bail() -> !` as a match-arm expression merges correctly (o1, runs 0) — the general `-> !` path is fine; only the builtin-macro inline path is broken in match arms.

**Interactions check** (delta from v1; unchanged rows compressed):
- Drop / catch_unwind / UnwindSafe family / abort strategy / ABI-unwind: **blessed §A7** (was WARN-undocumented). No enforcement work pending by design.
- `?` (Try): OK — via Never coercion (`sema.cpp:1711`); §6.5 Try/FromResidual landed separately.
- Result: OK unchanged.
- `#[panic_handler]`: GAP (low) — still unrecognized; not covered by A7's text. With a fixed abort runtime its only use is no_std-style behavior override; file as §B-low or extend A7 to bless its absence.
- `#[track_caller]` / `Location::caller()`: GAP unchanged (`panic.logos:50-53` "isn't yet wired"); orthogonal to unwinding — A7 does not bless it. `PanicInfo.location` always None pends on it.
- `panic!`/`assert!`/`assert_eq!`/`assert_ne!`: OK. `unreachable!`/`todo!`/`unimplemented!`: **landed (b0aa262d) with the match-arm ICE + void-typing residual above.**
- Integer-overflow panic: GAP unchanged (Category K row).
- `#[global_allocator]` / `#[windows_subsystem]` (`runtime.md`, missed in v1): GAP — unrecognized; no allocator-override hook (allocation goes through the runtime's malloc path). Low priority; file under L-attributes ownership.

**Gaps / debt.**
1. **Match-arm ICE for §6.11 builtins** (o3/o3d segfault) — the synthesized `__fmt_panic` block from `lower_builtin_macro` crashes the match-arm lowering. Fix the class: the format-family inline must carry `never_t()` through the match-arm unifier (same root as 2).
2. **`panic!` match-arm types void** (o3c) — same root; the if-arm carve-out generalization (eb894e80) covers CALL/FN_MACRO_CALL nodes but the match-arm expression unifier doesn't see the inlined block's Never tail.
3. `#[track_caller]`/`Location::caller()` — unwired (GAP, not blessed).
4. `#[panic_handler]`, `#[global_allocator]`, `#[windows_subsystem]` — unrecognized attrs (GAP-low; L-attributes ownership).
5. `__fmt_panic` is a plain `pub fn` acting as a de-facto lang item — fold into a `#[lang]`-style registration if/when that mechanism lands (unchanged from v1).

---

## Feature 2: Divergence `!`

**Rust nomenclature.** `divergence.md` (diverging expression forms `:18-31`, fallback `:64+`), `types/never.md` (`!` return-position constraint, coerce-to-all).

**Logos nomenclature.** (refs re-verified)
- `LogosType::Kind::Never`; `never_t()` accessor (`sema_impl.hpp:189`); prints `"!"` (`sema.cpp:2046`); parses from `!` (`:2372`).
- **One-directional coercion** at `sema.cpp:1711` (`from.kind()==Never → true`; Never-FROM removed) — §1.1 piece 1.
- **`is_divergent_call_node`** at `sema.cpp:1578-1599` — single predicate (CALL/FN_MACRO_CALL; `panic` name-anchor for the pre-expansion macro shape + generic ret-type-Never check over `find_func_candidates`); replaced the scattered `callee == "panic"` carve-outs (eb894e80; generalization note at `sema_expr.cpp:12992`). v1 gap #1 closed.
- `body_always_diverges_simple` at `sema_stmt.cpp:202` + `SemaFuncInfo::body_always_diverges` (`sema_impl.hpp:2275`) — collect-time precompute.
- **Rust-2024 `!`-fallback** (7f789b9f): unbound type-param falls back to `never_t()` iff the callee's body always diverges — `fn f<T>() -> T { panic() }` resolves `T = !`; `fn f<T>() -> T { return 0; }` still ambiguous-errors (matches rustc). Test `core_1_1_never_fallback`. v1 gap #2 closed.
- `loop {}` → `Never` via `last_loop_diverged_` channel (§1.1 piece 2). Probe o2: `let x: i32 = loop {};` compiles ✅.
- Param-position `!` rejection at `sema_decl.cpp:552-560`.
- Uninhabited scrutinee: `match x {}` over `Never` or empty enum trivially exhaustive (`sema_stmt.cpp:6996-7005`).
- `pub enum Never {}` still at `stdlib/lang/marker/marker.logos:45` (Infallible-role; rename NOT done).

**Match verdict: WARN — core divergence semantics now Rust-conformant end-to-end (§1.1 closed); residuals: uninhabited-variant arm elision, Never/Infallible naming, `!` in non-return type positions unchecked.**

Probe matrix (2026-06-12):
- ✅ user `-> !` fn in match-arm merge (o1, runs 0).
- ✅ `let x: i32 = loop {};` (o2).
- ✅ `Some(return 0)` structural divergence — `Option<i32>` accepts a Never-typed element (o4, runs 0). v1 "untested PARTIAL" → confirmed.
- ❌ uninhabited-variant arm elision: `enum Foo { A, B(Never) }; match f { Foo::A => … }` → "match is not exhaustive — missing variant(s): B" (o5). v1 gap #5 still open (clean diagnostic, not a miscompile).

**Interactions check** (delta from v1): Never type / `return` / `break`-with-value / `continue` / `loop {}` — OK unchanged. `panic!` — OK structurally; the name-keyed carve-outs are retired into `is_divergent_call_node` (the remaining `callee == "panic"` at `sema.cpp:1592` is a justified pre-expansion macro-shape anchor, documented in-source). Match arms (uninhabited): scrutinee-level OK, variant-payload-level GAP (o5). `!`-fallback: **closed** (narrow Rust-2024-correct discriminator). `Option<!>`/structural: confirmed working (o4).

**Gaps / debt.**
1. **Uninhabited-variant arm-elision** (o5) — `check_match_exhaustiveness` (`sema_stmt.cpp:6991`) enumerates variants without uninhabited-payload pruning. Skip variants whose payload type is uninhabited (Never, empty enum, recursively).
2. **`Never` vs `Infallible` naming** — `marker.logos:45` unchanged; rename to `Infallible` + `type Never = Infallible;` alias (docstring already concedes the mapping).
3. **`!` accepted in non-return type positions** (let/field/generic-arg) — param-position rejected only. Note: `Option<!>`-style generic args are *useful* per spec (o4 relies on it); the spec constraint `r[type.never.constraint]` would require rejecting only the *syntactic* non-return type positions. Low.

---

## Cross-category gaps

- `#[track_caller]`, `#[panic_handler]`, `#[global_allocator]`, `#[windows_subsystem]`, `cfg(panic="abort")` → Category L attribute plumbing (cfg(panic) cosmetic per A7(d)).
- C-unwind ABI → Category N; accepted-as-alias there, moot per §A7.
- Integer overflow debug-panic → Category K (open).
- Drop-on-unwind → blessed §A7; A-ownership audit row should now cite A7 instead of "partial".
- `assert!` at const-context → Category M (still no CTFE panic channel).

## Recommended next moves

1. **(critical, ~½ session) Fix the match-arm Never-typing class for format-family builtins** — `panic!`/`unreachable!`/`todo!`/`unimplemented!` as match-arm expressions: ICE (o3/o3d) + void-typing (o3c). One root: the sema-inlined `__fmt_panic` block must present `never_t()` to the match-arm unifier (and not crash the lowerer). Add match-arm forms to `core_6_11_never_macros`.
2. **(~½ session) Uninhabited-variant arm-elision** in `check_match_exhaustiveness` — unlocks `match e { Foo::A => … }` over `Foo { A, B(Never) }` rustc ports.
3. **(~½ session) Rename `enum Never` → `Infallible`** in `marker.logos` (+ alias).
4. **(~1 session) `#[track_caller]` + `Location::caller()`** — compiler-injected caller location; populates `PanicInfo.location`.
5. **(doc-only) Update A-ownership Drop row** to cite §A7 for panic-path Drop semantics.
