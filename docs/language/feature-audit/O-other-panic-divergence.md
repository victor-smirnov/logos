# Category O — Other Panic Divergence (audit)

Generated: 2026-05-30; spec: rust-lang/reference (local checkout)

Summary: 2 features audited — 0 OK, 2 WARN, 0 GAP. Both features have working core implementations but diverge from Rust's spec in concrete and tracked ways: Logos panic is `abort`-by-default with a `setjmp/longjmp`-backed recovery path (no stack-unwinding, no Drop-on-unwind), and the never type `!` is the spec-correct kind but several supporting Rust-spec surfaces (`-> !` only in return position by spec, `!`-fallback, `Never`/`Infallible` interplay, `#[panic_handler]`/`-C panic`/`#[track_caller]` attribute family) are absent or carried as cosmetic placeholders.

---

## Feature 1: Panic

**Rust nomenclature.** "Panic" / "panicking" (`reference/src/panic.md`). Core terms: *panic handler* (`#[panic_handler]` attribute on `fn(&PanicInfo) -> !`), *panic strategy* (`-C panic=unwind`/`abort` CLI flag), *unwinding* (frame-by-frame `drop` execution), `std::panic::catch_unwind`, `UnwindSafe`/`RefUnwindSafe`/`AssertUnwindSafe` marker traits, `PanicInfo`, `Location` (via `#[track_caller]`), `panic!`/`unreachable!`/`todo!`/`unimplemented!` macros (all `-> !` and diverging — see `reference/src/divergence.md:32-34`), and the `C-unwind` ABI for unwinding across FFI.

**Logos nomenclature.**
- Runtime entry point: `pub fn panic(msg: str) -> !` at `stdlib/lang/panic/panic.logos:29`. Backed by `extern fn abort() -> !` at `stdlib/lang/panic/panic.logos:14` plus a setjmp-friendly hook `logos_panic_maybe_longjmp` at `stdlib/lang/panic/panic.logos:20`.
- Surface macro: `#[fn_macro] pub fn panic(args: Vec<ExprBlob>) -> ExprBlob` at `stdlib/std/fmt/fmt.logos:226` (rewrites to `format!` + `__fmt_panic`). Assert family `assert!`/`assert_eq!`/`assert_ne!` at `stdlib/std/fmt/fmt.logos:239-308`, all going through `__fmt_panic` at `stdlib/std/fmt/fmt.logos:217`.
- `catch_unwind`: `pub fn catch_unwind(f: fn()) -> Result<(), str>` at `stdlib/lang/panic/panic.logos:88` — fn-ptr only (no closure), error payload `str` not `Box<dyn Any + Send>`.
- `Location` / `PanicInfo` structs at `stdlib/lang/panic/panic.logos:55-75`.
- Marker traits `UnwindSafe` / `RefUnwindSafe` / `AssertUnwindSafe<T>` at `stdlib/lang/panic/panic.logos:116-124` — declared but no auto-trait derivation.
- Recovery runtime (C): `stdlib/rt/test_recovery.c:19-58` — TLS `jmp_buf`, captures last-panic msg buffer.
- Compiler hand-coding: divergence detection in `src/compiler/sema_stmt.cpp:34-53` (`is_divergent_call` originally name-recognised `panic` then generalised to any `-> !`), divergent-block tail-shape carve-out in `src/compiler/sema_expr.cpp:11899-11905` and `src/compiler/sema_expr.cpp:12057-12095`.
- Test harness: `#[should_panic(expected = "msg")]` attribute parsed in `src/compiler/sema_collect.cpp:1335-1355`, threaded into `LFunction::should_panic` in `src/compiler/sema_decl.cpp:241-247`, used in test-main synthesis at `src/compiler/main.cpp:3052/3781-3846`.

**Match verdict: WARN — semantics divergent, naming OK.** Logos names (`panic`, `catch_unwind`, `UnwindSafe`/`RefUnwindSafe`/`AssertUnwindSafe`, `PanicInfo`, `Location`) match Rust. The implementation, however, has two deliberate divergences flagged in source comments but **not present in `docs/DIVERGENCES.md`**: (a) no real stack-unwinding — `panic()` calls `logos_panic_maybe_longjmp(...)` then `abort()` (`stdlib/lang/panic/panic.logos:36-37`), so drop-on-unwind doesn't run (`catch_unwind` skips all destructors between the call site and the jmp_buf); (b) `catch_unwind` is fn-ptr-only and returns `Result<(), str>` not `Result<R, Box<dyn Any + Send>>`. Both should be filed as blessed §A/§B divergences or as concrete debt items.

**Implementation pointer.** Runtime at `stdlib/lang/panic/panic.logos:29-39`; `__fmt_panic` thunk at `stdlib/std/fmt/fmt.logos:217`; setjmp recovery at `stdlib/rt/test_recovery.c:19-58`; sema divergence-flow in `src/compiler/sema_stmt.cpp:27-99` and `src/compiler/sema_expr.cpp:11892-11933`.

**Interactions check** (table edge: Panic ↔ Drop · `?` · Send/Sync (UnwindSafe) · catch_unwind · Result · abort panic-strategy · ABI):
- Drop (unwinding runs destructors). **WARN — divergent.** `panic()` calls `abort()` after the optional `longjmp` hook (`panic.logos:37`); the longjmp-recovery path used by `catch_unwind` and `#[should_panic]` skips all stack frames — no destructors run between the panic site and the recovery jmp_buf. Comment at `stdlib/std/fmt/fmt.logos:235-237` openly acknowledges the panic branch is "unreachable" w.r.t. drop. The Drop-audit at `docs/language/feature-audit/A-ownership.md:102` calls this out as "partial — Drop-on-unwind invariants vs panic strategy aren't explicitly tested." Not in `docs/DIVERGENCES.md`.
- `?` (Try). **OK (indirectly).** `?` desugars via `Try::branch`; the diverging `Err`/`None` arm threads a `return …` whose type is `!` — handled correctly via the never-coercion in `src/compiler/sema.cpp:1614-1619`. No interaction with `panic()` per se.
- Send/Sync (UnwindSafe family). **WARN.** `UnwindSafe`/`RefUnwindSafe` defined as empty traits at `stdlib/lang/panic/panic.logos:116-117` and impl'd unconditionally for `AssertUnwindSafe<T>` at `:123-124`. No auto-trait propagation, no per-type impls, no opt-out for `&mut T`/`UnsafeCell` (which in Rust have `impl !UnwindSafe`). Since Logos `catch_unwind` takes `fn()` (no closure captures), the bound is unenforced anyway. Documented as "markers exist so port targets parse" at `panic.logos:108-110`.
- `catch_unwind`. **WARN — divergent surface.** `fn catch_unwind(f: fn()) -> Result<(), str>` at `panic.logos:88` — closure-only API in Rust (`F: FnOnce() -> R`, returns `Result<R, Box<dyn Any + Send>>`); Logos takes fn-ptr only and discards the closure return. The divergence is acknowledged in-source at `panic.logos:83-87`. Should be promoted to `docs/DIVERGENCES.md`.
- `Result` (alternative to panic). **OK.** `Result<T, E>` exists in `stdlib/lang/result/`; `Option::unwrap`/`expect` at `stdlib/lang/option/option.logos:48-61` use `panic()` directly. Same shape as `core::option`.
- `abort` panic-strategy. **WARN — missing surface.** No `-C panic=abort`/`unwind` CLI flag; Logos always behaves as `abort` (plus an optional setjmp hook). `#[cfg(panic = "abort")]` is also not recognised (`docs/language/feature-audit/L-attributes.md:95` flags `panic` as one of the missing `cfg` keys). Not a bug per se — fixed strategy is a legitimate language choice — but not a blessed §A divergence either.
- ABI (panic across FFI = UB). **GAP.** No `extern "C-unwind"` ABI variant (see `docs/language/feature-audit/N-ffi-linkage-abi.md:33` — already filed). No compile-time check that `panic` cannot cross a non-unwind extern boundary. With `abort` as the strategy, the question is moot for FFI safety (no unwind ever leaves Rust frames), but the rule is not enforced anywhere.
- `#[panic_handler]` attribute. **GAP.** Grep `panic_handler` in `src/compiler/` returns 0 matches outside the test-harness `#[should_panic]` text-search at `main.cpp:3052`. There is no way to override the panic behaviour at the language surface (Rust requires this for `no_std`). With std-only builds today this is currently unused but missing.
- `#[track_caller]` / `Location::caller()`. **GAP.** Acknowledged at `stdlib/lang/panic/panic.logos:50-53` ("isn't yet wired"); `Location` is constructable but never auto-populated. No compiler injection of caller frames.
- `panic!`/`assert!`/`assert_eq!`/`assert_ne!` macros. **OK** — present at `stdlib/std/fmt/fmt.logos:226-308`. `unreachable!`/`todo!`/`unimplemented!` macros: **GAP** — grep shows zero `unreachable!`/`todo!`/`unimplemented!` macro definitions in `stdlib/`; only `stdlib/lang/hint/hint.logos:36 unreachable_unchecked()` (unsafe fn that calls `abort()`) is present. Imported test ports work around this by replacing `unreachable!()` with `panic(...)` (e.g. `tests/imported/pass/array-slice-vec/vec-matching-fixed.logos:2`, `tests/imported/pass/binding/pat-tuple-4.logos:6`).
- Integer-overflow panic (`debug_assert!`-on-overflow). **GAP** — already filed at `docs/language/feature-audit/K-unsafe.md:81-89`.

**Gaps / debt.**
1. **`unreachable!()`/`todo!()`/`unimplemented!()` panic macros absent** — every imported test needing them rewrites to `panic(str)`. Adding the three as `#[fn_macro]` wrappers in `stdlib/std/fmt/fmt.logos` is a ~20-line patch.
2. **Drop-on-unwind not implemented** — `catch_unwind` skips destructors of intervening frames. Either land real personality-function unwinding (large) OR document as a §A blessed divergence (small) and gate any imported test that depends on Drop-on-unwind.
3. **`catch_unwind` signature divergence** (fn-ptr / `Result<(), str>`) — promote `panic.logos:83-87` comment to `docs/DIVERGENCES.md` §A or §B entry.
4. **`#[panic_handler]` / `-C panic=abort|unwind` / `#[track_caller]` attribute family unwired** — none of these are recognised. File as §A (deliberate replacement: panic strategy is a runtime choice not a CLI flag) or §B (catch-up). `cfg(panic = "abort")` predicate key already flagged in L-audit.
5. **UnwindSafe/RefUnwindSafe auto-trait derivation absent** — `panic.logos:108-110` notes this. Low-priority (no enforcement site).
6. **`PanicInfo::location` always `Option::None`** — wait on `#[track_caller]`. Note in `panic.logos:69-71`.
7. **No mechanism for `panic_str` / `panic_fmt` lang items** — the `__fmt_panic` shim is the de-facto lang item but is plain `pub fn`, not marked as a compiler-recognised lang item. If we later add `#[lang = "..."]` it should cover this.

---

## Feature 2: Divergence `!`

**Rust nomenclature.** *Divergence* (`reference/src/divergence.md`) is the property of an expression that never completes; the never type `!` (`reference/src/types/never.md`) is the type carrier. Spec rules: `!` only appears in **function return position** (`types/never.md:r[type.never.constraint]`); `!` is coerce-everything (`r[type.never.coercion]`); fallback to `!` in Rust 2024 / `()` pre-2024 (`reference/src/divergence.md:r[divergence.fallback]`); the family of diverging expressions enumerated in `divergence.md:18-31` covers `return`, `break` (with value), `continue`, infinite `loop {}`, empty `match`, `!`-returning calls, `panic!`-family.

**Logos nomenclature.**
- Type kind: `LogosType::Kind::Never`. Defined alongside the other primitive kinds; constructor `prim(LogosType::Kind::Never)` at `src/compiler/sema.cpp:2199/2224`; accessor `never_t()` at `src/compiler/sema_impl.hpp:189`; print form `"!"` at `src/compiler/sema.cpp:1900`.
- Subtype-of-everything rule: `src/compiler/sema.cpp:1614-1619` (`types_compatible` returns true if either side is `Never`).
- LIR / MLIR codegen: `logos_to_mlir(Never) = nullptr` at `src/compiler/mlir_gen_types.cpp:35-37`; size/align `{0,1}` at `mlir_gen_types.cpp:372`; struct-field-of-Never collapses to zero-size array at `mlir_gen_types.cpp:297-303`; field-load skipped at `src/compiler/mlir_gen.cpp:1000`.
- Empty enum surrogate: `pub enum Never {}` at `stdlib/lang/marker/marker.logos:45` — explicitly described as "Logos's replacement for Rust's `!` (never) type. ... Mirrors the role of `core::convert::Infallible`" (`marker.logos:40-44`).
- Parameter-position rejection: `src/compiler/sema_decl.cpp:524-535` ("the never type `!` is uninhabited and cannot be a parameter type").
- Return-position `-> !`: supported. `pub fn panic(msg: str) -> !` at `stdlib/lang/panic/panic.logos:29` and `extern fn abort() -> !` at `:14`. Codegen elides the operand for void-`!` returns at `src/compiler/mlir_gen_stmt.cpp:1726-1746`.
- Divergence-of-`break`/`continue`/`return` expression typing at `src/compiler/sema_expr.cpp:1045-1113` (lowered to `EBlockExpr` with `dummy = lit_int(0, never_t())` and block type `never_t()`).
- Block tail divergence handling at `src/compiler/sema_expr.cpp:11892-11933` (block adopts `Error` type instead of `Never` when tail is a `panic` call — special-case carve-out).
- `if` / `match` join: diverging branch's type is ignored, surviving arm wins at `src/compiler/sema_expr.cpp:12080-12127`. Scrutinee-`Never` carve-outs in `src/compiler/sema_stmt.cpp:3741/4104/5320`.
- Borrow-check divergence flag `cur_diverged_` at `src/compiler/borrow_check.cpp:344-348`, with per-branch merge at `:1755-1781`.
- Infinite-`loop {}` divergence: `src/compiler/sema_stmt.cpp:96-99`.

**Match verdict: WARN — `!` type is implemented and conformant; the `Never`-enum sibling at `stdlib/lang/marker/marker.logos:45` doubles up the nomenclature and the spec-rule "appears only in return position" is enforced loosely.**

The `!` type kind exists and is correctly bottom-typed. Two naming/surface issues:
- `pub enum Never {}` at `marker.logos:45` is an additional uninhabited type that conceptually maps to **`core::convert::Infallible`** in Rust, not the never type. The naming `Never` collides with the colloquial term for `!`. Recommend rename to `Infallible` (with a `type Never = Infallible;` alias if back-compat is desired), or fold into a `#[lang = "infallible"]` distinguishable surface. The marker comment at `marker.logos:40-44` already acknowledges the Infallible role.
- Spec rule `r[type.never.constraint]` says `!` may only appear in return types. Logos rejects it as a parameter type (`sema_decl.cpp:529`) which is conformant, but does NOT prevent `!` in other type positions (let-binding, struct-field, generic arg). Quick grep shows no broader rejection.

**Implementation pointer.** Type kind at `src/compiler/sema.cpp:2199/2224`, accessor at `src/compiler/sema_impl.hpp:189`, subtype rule at `src/compiler/sema.cpp:1614-1619`, MLIR lowering at `src/compiler/mlir_gen_types.cpp:35-37`, divergent-expr lowering at `src/compiler/sema_expr.cpp:1045-1113`.

**Interactions check** (table edge: Never type · `return` · `break` (with value) · `continue` · `panic!` · `loop {}` · Match arms (uninhabited)):
- Never type. **OK.** `Never` kind, `never_t()` accessor, prints as `"!"`. Spec-correct subtype-of-everything coercion at `sema.cpp:1618-1619`.
- `return`. **OK.** `RETURN_EXPR` lowering at `sema_expr.cpp:1059-1084` types it as `never_t()`. Tail-return reachability at `sema_stmt.cpp:27-99`. Void-`!` return collapsed in codegen at `mlir_gen_stmt.cpp:1726-1746`.
- `break` (with value). **OK.** `BREAK_EXPR` at `sema_expr.cpp:1090-1110` types as `never_t()`; the carried value is recorded in `LoopBreakFrame` for the loop's expression-form value type.
- `continue`. **OK.** `CONTINUE_EXPR` at `sema_expr.cpp:1085-1089` types as `never_t()`.
- `panic!`. **OK structurally — special cases.** Both the macro form and bare `panic(...)` are recognised as divergent via `is_divergent_call` at `sema_stmt.cpp:34-53` (name-check + ret-type `Never` check). However, the block-tail handling at `sema_expr.cpp:11892-11905` is a NAME-keyed carve-out (`str_of(val_node.get(la::CALLEE.code)) == "panic"`) that should fall out of the general `-> !` rule — DEBT. Same pattern recurs at `sema_expr.cpp:12057-12095`.
- `loop {}` (infinite). **OK.** `sema_stmt.cpp:96-99` marks `LOOP` always-returns; an infinite loop diverges. Codegen marks the loop exit unreachable at `mlir_gen_stmt.cpp:2102-2104` when no `break` exists.
- Match arms (uninhabited). **WARN.** Empty-enum `match e {}` and uninhabited scrutinee handling: scrutinee-`Never` carve-outs at `sema_stmt.cpp:3741/4104` skip exhaustiveness for `Never`-typed scrutinees, but I found no specific support for matching `Never`-payload enum variants as "automatically unreachable" (Rust does this for `enum Foo { A, B(!) }` — the `B` arm is trivially unreachable and can be omitted). No tests for this shape.
- `!`-fallback (Rust 2024). **GAP.** Rust 2024 says an inferred type unifying only with diverging expressions falls back to `!` (`divergence.md:r[divergence.fallback]`). Logos has no fallback step; an inference variable that only saw `Never` would likely stick as `TypeVar` or error. No code in `src/compiler/sema*.cpp` mentions `divergence.fallback` or a similar rule. **Not a blessed divergence.** Concrete test gap.
- `Some(loop {})` / `Option<!>`. **PARTIAL.** Rust says `Some(loop {})` has type `Option<!>`. Logos can express `Option<!>` (Never is a valid type arg) but I found no test exercising the structural-propagation case from `reference/src/divergence.md:80-91`. Likely works because `never_t` is treated like any primitive, but untested.

**Gaps / debt.**
1. **Generalise the name-keyed `panic` carve-outs at `sema_expr.cpp:11899-11905` and `sema_expr.cpp:12057-12095`** — they should key off `is_divergent_call` (already exists at `sema_stmt.cpp:34`) instead of `callee == "panic"`. Today a user `-> !` function in tail position would not get the same special-case carve-out.
2. **`!`-fallback rule (`divergence.fallback`) not implemented.** Add a step in type-inference closure that turns "only-Never-unified" inference vars into `Never`. Edition gating not needed since Logos has no edition mechanism (`-> !` fallback is the only edition wart in this area).
3. **`!`-in-non-return-position rejection inconsistent.** Parameter position rejected (`sema_decl.cpp:529`) but let-binding, struct-field, generic arg are not. Either widen the check OR document as a deliberate divergence (`Vec<!>`/`Option<!>` are technically useful per spec).
4. **`Never` vs `Infallible` nomenclature collision** — rename `pub enum Never {}` at `marker.logos:45` to `Infallible` (canonical Rust name for that role); keep `Never` as a `type Never = Infallible;` alias or drop entirely. The Logos `!` type IS Rust's `Never`; the marker-enum is Rust's `Infallible`. Today the docstring already says so.
5. **Uninhabited-variant arm-elision** — `match e { Foo::A => …, /* Foo::B(!) omitted */ }` should be accepted as exhaustive. No code path I found does this; `check_match_exhaustiveness` at `sema_stmt.cpp:6608-6691` only knows about variant enumeration, not uninhabited-payload pruning.
6. **`Some(loop {})` structural-fallback** — write a test (`tests/logos/pass/never_structural_fallback.logos`).

---

## Cross-category gaps

- **`#[track_caller]` & `Location::caller()`** intersect with **Category L (Attributes)** — already flagged at `docs/language/feature-audit/L-attributes.md` as an unrecognised attribute.
- **`#[panic_handler]` & `#[global_allocator]`** also Category L — same audit notes them missing.
- **`#[cfg(panic = "abort")]`** intersects Category L (cfg keys), flagged at `L-attributes.md:95`.
- **C-unwind ABI** intersects Category N (FFI/ABI) — `docs/language/feature-audit/N-ffi-linkage-abi.md:33` already filed.
- **Integer overflow debug-panic** intersects Category K (unsafe / UB) — `docs/language/feature-audit/K-unsafe.md:81-89` filed.
- **Drop-on-unwind** intersects Category A (ownership / Drop) — `docs/language/feature-audit/A-ownership.md:102` filed as partial.
- **`assert!` at const-context** intersects Category M (const eval) — `docs/language/feature-audit/M-const-evaluation.md:74-88` filed.

## Recommended next moves

Sized for single sessions, ordered by impact / ease:

1. **(low-risk, ~1 session) Land `unreachable!()`, `todo!()`, `unimplemented!()` `#[fn_macro]` wrappers** in `stdlib/std/fmt/fmt.logos` — each lowers to `__fmt_panic("internal error: entered unreachable code")` / similar. Eliminates the per-test rewrite seen across `tests/imported/pass/**` and brings the divergence-canon macro family complete. (Spec: `divergence.md:32-34`.)
2. **(low-risk, ~½ session) Generalise the name-keyed `panic` divergence carve-outs at `src/compiler/sema_expr.cpp:11899-11905` and `:12057-12095`** to route through `is_divergent_call` so any `-> !` fn (incl. `unreachable!` / `todo!` once landed, or a user diverging fn) gets the same divergent-block treatment. Single root, removes name-based special-case smell. (Derives from `feedback_derive_from_foundation`.)
3. **(low-risk, ~½ session) Rename `pub enum Never {}` → `pub enum Infallible {}` in `stdlib/lang/marker/marker.logos:45`, with `pub type Never = Infallible;` for back-compat.** Aligns Logos nomenclature with `core::convert::Infallible` and removes the conceptual collision with the `!` (never) type. Comment block at `marker.logos:40-44` already concedes the Infallible mapping.
4. **(~1 session) Implement the `!`-fallback inference rule.** Add a post-pass on each inference-var: if all unifications were against `Never`, resolve to `Never`. Write a port of `reference/src/divergence.md:80-91`'s `Some(return)` example as `tests/logos/pass/never_structural_fallback.logos`.
5. **(~1 session) File three `docs/DIVERGENCES.md` entries** — (a) `panic` is abort-only (no real unwinding, no Drop-on-unwind), (b) `catch_unwind` takes `fn()` not `FnOnce`, returns `Result<(), str>` not `Result<R, Box<dyn Any + Send>>`, (c) `#[panic_handler]` / `-C panic` / `#[track_caller]` unwired. Whether §A (blessed) or §B (catch-up) per-item per Victor's call.
6. **(~½ session) Uninhabited-variant arm-elision** in `check_match_exhaustiveness` (`src/compiler/sema_stmt.cpp:6608`). Skip variants whose payload is `Never` (or whose payload contains `Never` recursively, by uninhabitedness). Cheap and unlocks porting `match e { Foo::A => …, /* B(!) */ }` shapes from rustc.
