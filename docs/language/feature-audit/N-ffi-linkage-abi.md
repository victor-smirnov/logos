# Category N — FFI linkage ABI (audit)

v2 — re-audited 2026-06-12 (v1: 2026-05-30); spec: rust-lang/reference (local checkout). Verdicts re-verified against code + compile probes (`/tmp/n*.logos`).

Summary: 3 features audited (v2 adds crate/linkage model missed in v1) — 0 OK, 2 WARN, 1 GAP. Headline change since v1: **`extern "ABI" { … }` blocks + ABI-string surface landed (2806e349, logos-core §6.7)** — block form, per-item form, ABI validation against `{"C","C-unwind","system","Rust"}`, and `extern static` declarations all parse; probe-verified callable. Still hard-wired to one calling convention (no per-ABI codegen), no ABI tag on fn-ptr types, and the linkage-attribute family (`#[link]`, `#[link_name]`, `#[link_section]`, `#[export_name]`, `#[used]`, `#[link_ordinal]`) is still absent — only `#[no_mangle]`. Inline assembly remains absent end-to-end.

## Feature 1 — `extern "ABI" fn` / blocks

**Rust nomenclature.** External blocks (`items/external-blocks.md`): `unsafe extern "ABI" { … }` with fn + static items, per-item `safe`/`unsafe` (edition 2024), ABI strings (`abi.md` — `"C"`, `"Rust"`, `"system"`, `-unwind` variants, platform ABIs), variadics on a fixed ABI subset, attributes `#[link(name=…, kind=…, modifiers=…)]`, `#[link_name]`, `#[link_ordinal]`; symbol attrs `#[no_mangle]`, `#[link_section]`, `#[export_name]`, `#[used]` (`abi.md`).

**Logos nomenclature.**
- Grammar: `extern_block` at `tools/peg_gen/grammars/logos.peg:1192-1208` — `extern "ABI"? { extern_block_item* }`; children use bare Rust-style `fn name(…) -> T;` (vararg alt incl.) **plus `static IDENT: T;` (→CONST_DEF) and `static mut IDENT: T;` (→STATIC_DEF)**. Single-decl `extern_fn_def` at `:1210+` gains `extern "ABI" fn …;` alts; bare `extern fn …;` retained. Schema `EXTERN_BLOCK = 249` (`:308`), ABI string in VALUE slot.
- Sema: `validate_abi` at `src/compiler/sema_collect.cpp:1275-1283` — allow-list `{"C","C-unwind","system","Rust"}`, unknown → "unsupported ABI string" (fail test `core_6_7_extern_unknown_abi`). Block flattened into the module item stream with block-ABI inherited by children lacking their own (`:1290-1321`). Extern items auto-flag `is_pub = is_unsafe = is_extern = true` (`:4483-4486`); raw symbol name preserved (mangling skip via `is_runtime_abi` at `:4658-4664`).
- mlir-gen: `forward_declare` at `src/compiler/mlir_gen_fn.cpp:151-178` — `llvm.func` variadic for `is_vararg`, else `func::FuncOp`; `setPrivate` for extern (`:178`). **No calling-convention attribute is emitted — all four admitted ABI strings lower identically to the platform C convention** (logos-core §6.7 explicitly defers cconv threading to Wave 6; grep `CConv|CallingConv` in `mlir_gen*` = 0 hits).
- Probes: `extern "C" { fn abs(i32) -> i32; }` + `extern "C" fn labs(i64) -> i64;` compile, link, run (n1 ✅). `extern "C" { static mut environ: *mut *mut u8; }` compiles (n2 — **parse/collect only; load/store semantics untested and, given S25's const-inlining storage model, presumed broken**). `let f: extern "C" fn(i64) -> i64` → parse error (n3 ❌).

**Match verdict.** WARN — surface parity reached for the block/item/ABI-string/extern-static *grammar*; semantic parity not: one hard-wired calling convention, no ABI on `Kind::FnPtr`, no `unsafe extern` wrapper / `safe`-`unsafe` per-item qualifiers, no linkage attributes beyond `#[no_mangle]`. §B catch-up, not a §A divergence (no §A row covers ABI taxonomy). Note: `"C-unwind"` is *accepted* but per §A7 (abort-only panic) it is semantically identical to `"C"` here — fine, since no unwind ever crosses a Logos frame.

**Implementation pointer.** Grammar `logos.peg:1192-1230`; sema `sema_collect.cpp:1275-1321, 4483-4486, 4658-4664`; mlir-gen `mlir_gen_fn.cpp:151-178`; vararg call `mlir_gen_expr.cpp:1775+`. Tests: `tests/logos/pass/core_6_7_extern_abi_block.logos`, `tests/logos/fail/core_6_7_extern_unknown_abi.logos`.

**Interactions check** (delta from v1):
- **Function pointers (ABI-tagged):** GAP unchanged — `fn_ptr_type` (`logos.peg:1631`) has `unsafe`/HRTB alts but no `extern "ABI"` alt (probe n3 parse error). `Kind::FnPtr` carries no ABI field.
- **`unsafe`:** OK in effect — extern fns auto-`is_unsafe`; call sites require `unsafe` (probe n1 needs the block). Edition-2024 `unsafe extern { … }` spelling still absent (implicit-unsafe is the only mode).
- **Linkage attributes:** GAP unchanged — attr table (`sema_impl.hpp:1275`) admits only `no_mangle`. No `#[link]`/`#[link_name]`/`#[link_ordinal]`/`#[link_section]`/`#[export_name]`/`#[used]`.
- **Calling conventions:** WARN — ABI strings now parse+validate; codegen ignores them (all → C convention). Becomes a real bug the day a `"system"`≠`"C"` target (win32 stdcall) is supported.
- **`#[repr(C)]`:** improved elsewhere — logos-core §1.5 landed minimal `#[repr(transparent)]`/`#[repr(uN)]`; `repr(C)` struct-layout pinning for FFI remains a §B item (Category B/L).
- **Raw pointers:** OK unchanged.
- **Variadics:** OK + §A6 addition unchanged (also on non-extern fns; Rust's ABI-subset restriction vacuous).
- **`extern static`:** NEW since v1 — parses in block form (both mut and immutable). Semantics (real external symbol binding, addressof reads) unverified/likely missing — shares M's S25 global-storage gap. WARN.
- **Panic across FFI:** moot by §A7 (abort-only; no unwind exists to cross). `"C-unwind"` accepted-as-alias is consistent with that.

**Gaps / debt.**
- Calling-convention threading: ABI string → MLIR/LLVM cconv attribute (the Wave-6 deferral). Includes ABI field on `SemaFnInfo`→`LFunction`.
- ABI tag on `Kind::FnPtr` + `extern "ABI" fn(…)` type grammar (n3).
- `extern static` load/store semantics (true external global, not const-inline) — joint root with M's S25 (`llvm.mlir.global`/`addressof`).
- Linkage-attribute family parsers + link-step threading (`#[link]`, `#[link_name]`, …; `#[export_name]`/`#[link_section]`/`#[used]` for export side).
- `unsafe extern { … }` / per-item `safe`/`unsafe` qualifiers (edition-2024 surface).
- `pub` on extern items is forced-on (`:4483`) — visibility non-configurable.

## Feature 2 — Inline assembly

**Rust nomenclature.** `asm!` / `global_asm!` / `naked_asm!` (`inline-assembly.md`): operands (`in/out/inout/lateout/inlateout/sym/const/label`), options, `clobber_abi`, register classes, `#[naked]`.

**Logos nomenclature.** Still absent end-to-end (re-grepped 2026-06-12: `KW_ASM|asm|naked` → 0 grammar hits, 0 sema handlers, no `llvm.inline_asm` emission). Hand-written assembly lives only *outside* Logos source calling in via `#[no_mangle]` (fibre runtime).

**Match verdict.** GAP — unchanged from v1.

**Interactions check.** All n/a (feature absent). Note one adjacent improvement: the atomics intrinsics that motivated "asm-ish" needs are now properly handled — `logos_atomic_*` extern fns are intercepted at codegen (`mlir_gen_expr.cpp:1775-1892`) and emitted as native LLVM atomic ops with const-eval'd per-variant `Ordering` (logos-core §6.14, + swap/fetch_or/and/xor/sub + weak CAS in 1b8ff07e). The `extern fn` *declarations* remain in `stdlib/lang/atomic/atomic.logos:50-58` as the intrinsic registration surface — they are no longer runtime calls. This "extern-decl-as-intrinsic-key" pattern is the in-tree precedent for an `asm!` v0.

**Gaps / debt.** Unchanged: no `asm!`/`global_asm!`/`naked_asm!`, no `#[naked]`, no register classes/`clobber_abi`. Convergence path unchanged: metaprog fn-macro → `INLINE_ASM` LIR node → `llvm.inline_asm`.

## Feature 3 — Crate linkage model (`linkage.md`) — NEW in v2 (missed by v1)

**Rust nomenclature.** `linkage.md`: crate types (`bin`/`lib`/`rlib`/`dylib`/`cdylib`/`staticlib`/`proc-macro`), `--crate-type`/`crate_type` attribute, static vs dynamic C runtime (`crt-static`), mixed-codebase linking, prohibited-linkage rules.

**Logos nomenclature.** Logos has its own package/compilation model (dotted packages, `logosc` driver emitting `.o` + static `lib/logos/*.a` archives; `.pkgi` embedded indexes per `feedback_archive_embed_indexes`). No `--crate-type` analogue, no dylib/cdylib emission, no crt-static toggle.

**Match verdict.** WARN — design-model divergence in substance (the Logos package model replaces Rust's crate model), **but `docs/DIVERGENCES.md` §A has no explicit row for it** (A6 covers additions, not the pkg/linkage model; memory `feedback_think_in_rust` names "pkg-model" in the blessed set). Action: add an §A row (pkg/compilation model) so the classification is on the register, or file the missing emission kinds (cdylib at minimum, for FFI-export use-cases) as §B.

## Cross-category gaps

- **Linkage attributes ↔ Category L** — ownership with L's attribute plumbing; N consumes. Unchanged.
- **`#[repr(C)]` ↔ Category B/L** — §1.5 gives `transparent`/`uN`; `repr(C)` still open.
- **`unsafe extern` ↔ Category K** — edition-2024 surface.
- **`extern static` storage ↔ Category M (S25)** — same `llvm.mlir.global` root; fix once.
- **ABI-tagged FnPtr ↔ Category B** — type-side field; N consumes at call codegen.
- **DIVERGENCES register** — add §A row for the pkg/linkage model (Feature 3); A7 already covers unwind-ABI moot-ness.

## Recommended next moves

1. **Thread the ABI string to codegen** — `SemaFnInfo.abi` → `LFunction` → MLIR cconv attr; map `"C"`/`"C-unwind"`/`"system"`(non-win)→ccc, keep `"Rust"`=default. Mechanical now that parse+validate exist.
2. **One `llvm.mlir.global` root fix** for `static mut` (M-S25) + `extern static` binding — two audited gaps, one mechanism.
3. **ABI tag on `Kind::FnPtr`** + `extern "ABI" fn(…)` grammar alt (closes probe n3).
4. **`#[link(name=…)]` + `#[link_name]` parsers** + driver link-arg threading.
5. **Inline-asm v0** via the extern-intrinsic precedent: `asm!` fn-macro → `INLINE_ASM` LIR → `llvm.inline_asm` (`in`/`out`/`options(nomem,nostack)` first).
6. **DIVERGENCES §A row for the package/linkage model** (Feature 3 classification debt).
