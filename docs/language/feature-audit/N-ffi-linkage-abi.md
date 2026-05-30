# Category N — FFI linkage ABI (audit)

Generated: 2026-05-30; spec: rust-lang/reference (local checkout)

Summary: 2 features audited — 0 fully OK, 1 WARN (extern fn/blocks: partial — works as Rust `extern "C"` but no ABI-string, no `extern { … }` block surface, no `unsafe extern` wrapper, no link/export/section attributes, no `safe`/`unsafe` per-item qualifier), 1 GAP (inline assembly absent end-to-end). Headline finding: Logos implements only the **`extern "C"` calling convention as a single hard-wired default** plumbed through a sui-generis bare-item production `extern_fn_def` (one fn per item, no block), with one optional attribute (`#[no_mangle]`) and no other linkage/ABI attribute surface. The combination "Logos has FFI" is true but the Rust ABI taxonomy (string, unwind variants, variadic restrictions, link attributes, static externs) is essentially absent — a parity gap to plan, not a blessed divergence.

## Feature 1 — `extern "ABI" fn` / blocks

**Rust nomenclature.** `extern` keyword introduces an external block (`items/external-blocks.md`, `r[items.extern]`) or qualifies a function item (`r[items.fn.extern]`). Optional ABI string literal (`"C"`, `"Rust"`, `"system"`, `"C-unwind"`, `"cdecl"`, `"stdcall"`, `"sysv64"`, `"win64"`, `"aapcs"`, `"fastcall"`, `"thiscall"`, `"efiapi"`, and their `-unwind` variants — `abi.md`). Edition 2024 requires `unsafe extern { … }`; per-item `safe`/`unsafe` function qualifiers inside the block; variadic `...` last param only on a fixed ABI subset (`r[items.extern.variadic.conventions]`). Companion attributes: `#[link(name=…, kind=…, modifiers=…, wasm_import_module=…)]`, `#[link_name="…"]`, `#[link_ordinal(N)]`, `#[no_mangle]`, `#[link_section=".name"]`, `#[export_name="…"]`, `#[used]` (`abi.md` `r[abi.used|no_mangle|link_section|export_name]`). Spec sections: `items/external-blocks.md`, `abi.md`, `linkage.md`.

**Logos nomenclature.**
- Grammar: ONE production `extern_fn_def` at `tools/peg_gen/grammars/logos.peg:1122-1127`, emitting node code `EXTERN_FN` — three alts: `extern fn IDENT(params, ...) -> T;` (vararg), `extern fn IDENT(params?) -> T;`, `extern fn IDENT(params?);` (unit return). No `extern { … }` block, no ABI string literal, no `unsafe extern`, no `safe`/`unsafe` per-item qualifier.
- Item-list integration: `item <- … / extern_fn_def / pub_fn_def / fn_def` at `tools/peg_gen/grammars/logos.peg:495`. No `pub_extern_fn_def`.
- AST keys: `IS_VARIADIC` on a variadic field-def `:1115-1116`; `IS_VARARG` flag at extern_fn level emitted via `tools/peg_gen/grammars/logos.peg:1123`.
- Sema collect: `EXTERN_FN` branch at `src/compiler/sema_collect.cpp:1313`; auto-flag `is_pub = is_unsafe = is_extern = true` for extern items at `:3855-3858`; preserve raw libc symbol name (skip mangling) at `:3885-3898` ("Extern declarations are ABI symbols, not overloadable implementation names. Keep the raw callee name stable…").
- Sema lower: extern detection `fn.is_extern = (node_code == EXTERN_FN)` at `src/compiler/sema_decl.cpp:249`; vararg flag at `:252-255`; body skipped because `extern_fn_def` only matches `…SEMI`.
- mlir-gen: `forward_declare` switches on `fn.is_vararg` to use `llvm.func` (variadic FunctionType) at `src/compiler/mlir_gen_fn.cpp:163-178`; else `func::FuncOp` at `:180`; `if (fn.is_extern || is_binary_skip) f.setPrivate()` at `:183`. Vararg-fn call special-case at `src/compiler/mlir_gen_expr.cpp:1776`.
- Linkage attribute: only `#[no_mangle]` recognised. Pending flag `pending_no_mangle_` at `src/compiler/sema_impl.hpp:~2950` (also `pending_no_mangle_` accessor near :2860), set at `src/compiler/sema_collect.cpp:1321, 1330`; consumed at `:4031-4037` (`is_runtime_abi = (base_name=="main") || pending_no_mangle_ || …` skips pkg+sig mangling). One stdlib user: `stdlib/std/rt/fiber/fiber.logos`.
- Allowed-attribute table: `if (name == "no_mangle") return bit(AttrTarget::Fn);` at `src/compiler/sema_impl.hpp` (search hit in the audit).
- Usage corpus: ~30+ test files (`tests/logos/pass/*.logos`, `tests/logos/fail/*.logos`) declare `extern fn printf(…) -> i32;` / `extern fn malloc(…) -> *mut u8;`. Compiler-injected prelude lines at `src/compiler/main.cpp:3794-3803` — declare libc + runtime helpers as bare `extern fn`. Metaprog also routes through `extern fn` (`src/compiler/sema_expr.cpp:17228-17230`).

**Match verdict.** WARN — Logos has *one* extern surface (`extern fn IDENT(…);` item), no ABI string, no block form, no per-item `safe`/`unsafe` qualifier, no `unsafe extern { … }` wrapper, no `pub` qualifier. The capability "declare a C-callable symbol with C calling convention" is preserved (because the *default* and *only* ABI is what Rust spells `extern "C"`, which is also Rust's default when the ABI string is omitted — `r[items.extern.abi.default]`). Naming drift: Rust calls the item form `extern "ABI" fn` (with an ABI string) and the block form an *external block*; Logos calls its single form `extern fn` (no string). This is a §B catch-up TODO, not a blessed §A divergence — none of the §A categories (replaced / design-model / Logos-addition) apply.

**Implementation pointer.** Grammar: `tools/peg_gen/grammars/logos.peg:1122-1127`. Sema collect: `src/compiler/sema_collect.cpp:1313, 3855-3898, 4031-4037`. Sema lower: `src/compiler/sema_decl.cpp:249-255`. mlir-gen: `src/compiler/mlir_gen_fn.cpp:163-184`. Vararg call: `src/compiler/mlir_gen_expr.cpp:1776`.

**Interactions check** (edges from the table, `extern "ABI" fn` neighbours):
- **Function pointers (ABI-tagged):** WARN — Logos has `fn_ptr_type` at `tools/peg_gen/grammars/logos.peg:1499-1528` with `unsafe fn(…)` and HRTB-binder alts but **NO `extern "ABI" fn(…)` ABI-tagged variant**. So a Rust `extern "C" fn()` function-pointer type has no Logos surface; a `fn`-item declared `extern fn` decays to the same `Kind::FnPtr` as a non-extern fn (no ABI tag carried on the type). GAP — divergent: extern-fn-item type does not differ from rust-fn-item type at value-level.
- **`unsafe`:** OK in effect — extern fns are auto-marked `is_unsafe` (`sema_collect.cpp:3855-3858`), so call sites require unsafe context exactly like Rust extern calls (`sema_expr.cpp:3213`). But the *spelling* differs: Rust requires `unsafe extern { … }` (edition 2024) or `extern fn IDENT` (item form is implicitly unsafe to call); Logos has no `unsafe extern` keyword sequence — implicitly-unsafe is the only mode. Acceptable equivalence; rename surface is the §K gap not this one.
- **Linkage attributes (`#[link]`, `#[link_name]`, `#[link_ordinal]`):** GAP — only `#[no_mangle]` is recognised (`sema_collect.cpp:1330`). No `#[link]` (native library specification), no `#[link_name="…"]` (rename imported symbol), no `#[link_ordinal(N)]`. Tests sidestep this by relying on the host's libc being implicitly linked into the binary by the JIT/compiler-driver wiring.
- **Calling conventions:** GAP — there is no ABI string parser; `"system"`, `"stdcall"`, `"sysv64"`, `"win64"`, `"C-unwind"`, etc. have no surface. The single hard-wired ABI is whatever LLVM `func.func` / `llvm.func` defaults to on the target (i.e., effectively `"C"` for the vararg path and the default Rust-ABI-or-C on the non-vararg `func.func` path — Logos does not distinguish).
- **Repr (`repr(C)`):** GAP — audit category L notes no `#[repr(...)]` surface is honoured. So passing a Logos struct through `extern fn` does not get layout-pinned to C ABI — works today only because Logos struct layout happens to match what LLVM gives field-order structs at this commit. A §B catch-up TODO.
- **Raw pointers (FFI types):** OK — `*const T` / `*mut T` are first-class (grammar `ptr_type`; canonical FFI param type in the test corpus, e.g. `printf(fmt: *const u8, ...)`).
- **`Drop` (panics across FFI = UB):** GAP — no `panic` propagation contract across `extern fn` boundaries is enforced; no `-unwind` ABI variant means there is also no way to *opt in* to unwind. With fibres-as-design-model (§A4) this corner is less salient, but the soundness rule "Rust panics may not unwind across non-`-unwind` extern" has no analogue.
- **Variadic functions:** OK + Logos addition — Logos supports `...` last-param via the dedicated `extern_fn_def` alt at `:1122-1123`, AND **also extends variadic to non-extern Logos fns** (audit category C5 notes the `DOTDOTDOT` alt in `pub_fn_def`/`fn_def` too). Rust restricts `...` to a fixed ABI subset (`r[items.extern.variadic.conventions]`); Logos has no ABI taxonomy so the restriction is vacuous. Per §A6 the variadic-on-native-fn surface is a Logos addition; per-Rust-variadic-restrictions are a non-applicable parity item.

**Gaps / debt.**
- No `extern { … }` block surface — every extern decl is a standalone item. Multi-decl ergonomics + per-block attribute targeting are lost.
- No ABI-string literal (`"C"`, `"system"`, `"C-unwind"`, …) on item OR on `fn_ptr_type`. Single hard-wired default ABI.
- No `unsafe extern` / `safe` / `unsafe` per-item qualifier sequences (edition-2024 alignment).
- No `pub extern fn` — extern items are forced `is_pub = is_unsafe = is_extern` together (`sema_collect.cpp:3855-3858`). Visibility is non-configurable.
- No `#[link]`, `#[link_name]`, `#[link_ordinal]`, `#[link_section]`, `#[export_name]`, `#[used]` attribute parsers. Only `#[no_mangle]`.
- No `extern static FOO: T;` (audit category K notes `static` is folded to `CONST_DEF`).
- No ABI tag on `Kind::FnPtr` — `extern fn` items coerce to plain `fn(…)` ptr. A `fn_ptr_type` with `extern "C"` cannot be expressed.
- No documented Rust-vs-Logos-default-ABI test (relies on host linker tolerance).
- No untested intersection: `extern fn` + generic param (`extern fn foo<T>(…)`) — Rust forbids; Logos's grammar admits the alt only via the non-vararg path with no type-param slot, so it's effectively forbidden by surface (worth a fail test).

## Feature 2 — Inline assembly

**Rust nomenclature.** `asm!`, `global_asm!`, `naked_asm!` macros in `core::arch` (`inline-assembly.md` `r[asm.intro]`); operand grammar `in(reg)`, `out(reg)`, `inout(reg)`, `lateout(reg)`, `inlateout(reg)`, `sym path`, `const expr`, `label { … }`, options (`pure`, `nomem`, `readonly`, `preserves_flags`, `noreturn`, `nostack`, `att_syntax`, `raw`), `clobber_abi(…)`, register classes (`reg`, `freg`, …), explicit registers (`"rax"`, …). Stable architectures: x86, x86-64, ARM, AArch64, RISC-V, LoongArch, s390x, PowerPC.

**Logos nomenclature.**
- Grammar: no production. `grep -nE "(KW_ASM|\bASM\b)" tools/peg_gen/grammars/logos.peg` returns zero hits; no `asm`/`naked_asm`/`global_asm` keyword token. Keyword `naked` is also absent.
- AST: no node code; no schema field.
- Sema: no handler. The only matches of `asm` in the compiler are two passing comments at `src/compiler/sema_collect.cpp:1320, 4031` ("inline-asm callees" / "inline asm / extern \"C\" callers") — referencing the *use-case* (a hand-written assembly stub elsewhere calling a Logos `#[no_mangle]` symbol), not an inline-asm feature in the language.
- mlir-gen: no `llvm.inline_asm` op emission.
- Stdlib: `grep -rE '\basm\b' stdlib/` returns no inline-asm usage; `stdlib/std/rt/fiber/fiber.logos` uses `#[no_mangle]` so a hand-written context-switch can call back in, but Logos does not contain that hand-written stub.

**Match verdict.** GAP — feature absent. Not surfaced in grammar, AST, sema, or mlir-gen. No `tests/imported/asm/*` directory exists.

**Implementation pointer.** n/a — feature absent.

**Interactions check** (edges from the table, Inline-assembly neighbours):
- **`unsafe`:** n/a — feature absent.
- **Raw pointers:** n/a — feature absent.
- **ABI:** n/a — feature absent.
- **Registers:** n/a — feature absent.
- **`extern "C"` calling:** partial relevance — Logos *does* support hand-written assembly *outside* the Logos source calling back in via `#[no_mangle] extern fn` (the fibre runtime is the in-tree example, `stdlib/std/rt/fiber/fiber.logos`). This is hand-written assembly in a *separate* compilation unit; inline asm in Logos source is still GAP.

**Gaps / debt.**
- No `asm!` / `global_asm!` / `naked_asm!` builtin macro. Would require: a metaprog handler over a templated format-string + operand list, lowering to `llvm.inline_asm`.
- No `naked` function attribute (`#[naked]`) — orthogonal but conventionally bundled with the inline-asm surface, since the only sound `#[naked]` body is `naked_asm!`.
- No `clobber_abi(…)` surface; no register-class names.
- No untested intersections — everything is GAP.
- Convergence path: build the macro side via `#[fn_macro]` registry (existing metaprog channel) emitting an `INLINE_ASM` LIR node that mlir-gen lowers to `llvm.inline_asm`. Operand format-string parsing is the bulk; the LLVM op exists. Reasonable in a single session for a v0 that handles `out(reg) x` / `in(reg) x` / `inout(reg) x` and a register-class allowlist.

## Cross-category gaps

- **Linkage attributes overlap with Category L (Attributes).** Only `#[no_mangle]` is recognised; `#[link]`, `#[link_name]`, `#[link_ordinal]`, `#[link_section]`, `#[export_name]`, `#[used]` are GAP. These belong half here and half in L; ownership should sit with L's attribute parser plumbing with Category N as a consumer.
- **`#[repr(C)]` for FFI struct layout** overlaps with Category B (Type system primitives → Type layout / `#[repr(...)]`) and Category L (Attributes). Required for sound cross-ABI struct passing. Tracked as a §B catch-up.
- **`unsafe extern { … }` block + edition-2024 `unsafe` semantics** overlap with Category K (Unsafe). K's audit already lists this as a missing surface; N inherits.
- **`extern static FOO: T` / `extern static mut FOO: T`** overlap with Category C (Items → Const item / Static item). Logos has no `static mut` and no `extern static` — both gaps belong to C's static-item plan.
- **`extern "ABI" fn(…)` ABI-tagged function-pointer TYPE** overlaps with Category B (Function pointers). Adding an ABI tag on `Kind::FnPtr` is a B item; the *user* of that tag (FFI codegen) lives here.
- **Panic-across-FFI / unwind-ABI variants** overlap with Category O (Panic) and §A4 (async/fibres design model). Logos's fibre model arguably moots Rust's `-unwind` ABI variants for the cross-thread case; for the cross-language case it does not, but no `catch_unwind` / `UnwindSafe` exists yet so the soundness condition is unobservable.

## Recommended next moves

Sized for single-session work items, in priority order:

1. **ABI string on `extern_fn_def`** — admit `KW_EXTERN STRING_LIT? KW_FN …` in the grammar; parse the literal; validate against an allowlist (`"C"`, `"Rust"`, `"system"`, optionally `"C-unwind"`); store on `SemaFnInfo`; thread to mlir-gen as the function attribute. v0 may treat all admitted ABIs as `"C"`-equivalent (single LLVM lowering) while accepting the surface so imported tests parse. Unblocks ~all imported FFI tests that name an ABI.
2. **`extern { … }` block surface** — add `extern_block <- KW_EXTERN STRING_LIT? LBRACE extern_item* RBRACE` with `extern_item` = current `extern_fn_def` (without `KW_EXTERN`) plus a future `extern static`. Keep the bare-item form as a shorthand alias for backward compat. Plumbs through as N×(SemaFnInfo with is_extern). Removes the per-decl-keyword stutter in stdlib/tests.
3. **`#[link(name=…)]` parser + linker arg threading** — add the `link` attribute to `is_attr_allowed`; record `name`/`kind`/`modifiers` on the containing block; pass through to the link step of the binary backend (`src/compiler/main.cpp` link driver). Companion: `#[link_name="…"]` to override a per-item imported symbol name. Unblocks any test that imports from a non-libc library.
4. **`extern "ABI" fn(…)` ABI tag on `Kind::FnPtr`** — add an ABI-string field to the FnPtr type representation; admit `KW_EXTERN STRING_LIT? KW_FN LPAREN … RPAREN …` alts in `fn_ptr_type`; record the ABI; teach call codegen to honour it. Required for `Box<dyn Fn>` over FFI / function-pointer fields in `repr(C)` structs.
5. **Inline-asm v0 via `#[fn_macro]`** — register `asm!` as a metaprog fn-macro that parses a format-string + operand list and emits an `INLINE_ASM` LIR node mlir-gen lowers to `llvm.inline_asm`. Start with `in(reg) x` / `out(reg) x` and `options(nomem, nostack)`. Implements §N's second feature end-to-end and unlocks `naked_asm!`/`global_asm!` as siblings.
6. **`#[no_mangle]` parity polish** — rename the internal helper `is_runtime_abi` (`sema_collect.cpp:4032`) to something less load-bearing-sounding (`skip_pkg_mangle`) and add `#[export_name="…"]` as the configurable variant of `no_mangle`. Cheap follow-on once attribute scaffolding from item 3 lands.
