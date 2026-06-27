# Category C — Items (audit)

v2 — re-audited 2026-06-12 (v1: 2026-05-30); spec: rust-lang/reference (local checkout at `/home/victor/cxx/reference`)

12 features: 8 OK, 4 WARN, 0 GAP (v1: 6/4/2). Closed since v1: **union** (full item, §6.1 Wave 9 conformance), **`static mut`** (parse + unsafe gate), **extern "ABI" blocks** (+ extern statics), **object-safety enforcement**, **`#[repr(uN)]`/`#[repr(transparent)]`**, **enum niche layout**, **fn-param patterns**, **assoc-const `= expr` grammar**. Open hot spots: **S25 — cross-fn `static mut` access MISCOMPILES (segfault; re-confirmed by probe 2026-06-12)**; trait-side assoc-const defaults parse but are not inherited; `use … as` aliasing absent; module model still uncatalogued in DIVERGENCES §A.

---

## 1. Function (`fn`)

**Rust:** `fn` item (`items/functions.md`). Qualifiers: `const` / `async` / `unsafe` / `extern "ABI"` / `safe` (2024, extern-only).

**Logos:** AST `FN`; grammar `fn_def` / `pub_fn_def` (`grammars/logos.peg:1266,1240`), `method_def` (`:1182`), `static_fn_def` / `pub_static_fn_def` (`:1057,1042`), `trait_method` (`:863`), foreign `extern_fn_def` (`:1210`). LIR `LFunction` (`lir.hpp:700`, `is_extern` `:722`). `collect_fn` (`sema_collect.cpp:4348`); `lower_fn` (`sema_decl.cpp:128`); codegen `mlir_gen_fn.cpp`.

**Verdict: OK.** `const fn`→metacall (§A2), `async fn`→fibres (§A4) blessed. ✅ closed since v1: `extern "C" fn name(…);` now takes the ABI string (2806e349); fn-param patterns — struct shapes (59b5d3cc) and tuple destructure `fn first((a,b):(i32,i32))` probe-verified green.

**Interactions:** generics/lifetimes/where-clauses/`unsafe fn`/return-type/diverging — OK (unchanged from v1). Fn-item type distinct from FnPtr — OK (`Kind::FnItem`, 0f1fa0c2). Attributes (`#[inline]` etc.) — partial (Cat L).

**Gaps / debt:**
- `safe fn` qualifier (Rust 2024 extern-block surface) absent — no `KW_SAFE`.
- `KW_NEW`/`KW_NULL` keyword-as-fn-name carve-outs (`:1046-1067`) — Logos extension, not in §A.

---

## 2. Struct (named / tuple / unit)

**Rust:** `StructStruct` / `TupleStruct` / unit-like (`items/structs.md`).

**Logos:** AST `STRUCT`; `struct_def` / `pub_struct_def` (`grammars/logos.peg:1129,1120`, tuple alt inside), `struct_unit` / `pub_struct_unit`, `struct_inst` forms. LIR `LStructDef` (`lir.hpp:835`). `collect_struct` (`sema_collect.cpp:3851`); `lower_struct_def` (`sema_decl.cpp:1002`).

**Verdict: OK.** All three Rust shapes present.

**Interactions:** generics, per-field `pub`, Drop, Copy, partial moves, DST tail (B2 done), construction/patterns/field-access, lifetimes, variance — OK (unchanged). ✅ closed since v1: `#[repr(transparent)]` first-class — parsed in `collect_struct` (`sema_collect.cpp:1557-1590`), `LStructDef.repr_transparent` (`lir.hpp:889`) consumed by `layout_of` (00a96805). Other repr modes (`C`, `packed`, `align`) reject with explicit "not yet supported" diagnostic — honest gap, not silent.

**Gaps / debt:**
- `#[repr(C)]` / `packed` / `align(n)` — diagnostic-rejected, layout driver absent (Cat L row).
- Inline methods inside `struct { … }` body — Logos extension, still not in DIVERGENCES §A. Bless or remove.

---

## 3. Enum

**Rust:** `Enumeration` (`items/enumerations.md`): unit/tuple/struct-like variants, `= expr` discriminants, `#[repr(uN)]`.

**Logos:** AST `ENUM`; `enum_def` / `pub_enum_def` (`grammars/logos.peg:722,713`). LIR `LEnumDef` (`lir.hpp:914`, `backing_type` `:922`). `collect_enum` (`sema_collect.cpp:1858`); `lower_enum_def` (`sema_decl.cpp:1140`).

**Verdict: OK** (was WARN). ✅ closed since v1: `#[repr(uN)]` Rust spelling accepted — sets discriminant width, conflicts with a declared `: T` backing type diagnosed (`sema_collect.cpp:1676-1722`); probe `#[repr(u8)] enum` green. ✅ niche optimization landed — F2 LowBit (`&T`+int packs to one word, sizeof 16→8) + NullPtr, single `enum_payload_ptr` chokepoint (9e132ea0); raw-u64 Pod niche (56501214); `#[zoned2]` niche enums ride on it (9383d687). ✅ empty enum compiles as uninhabited (`sema_collect.cpp:1861` "B-it-06 — empty enum bodies intentionally legal"); probe green.

**Interactions:** variants (3 shapes), discriminants, match exhaustiveness, patterns, recursive drop (B7 value-repr), generics, methods, Option/Result — OK.

**Gaps / debt:**
- Backing-int syntax `enum E: u64 {…}` retained alongside `#[repr(u64)]` — Logos surface extension, still not catalogued (§A6 row or retire).
- `match e {}` over an uninhabited enum (exhaustive-by-emptiness) untested.

---

## 4. Union

**Rust:** `Union` item (`items/unions.md`) — `union` keyword; field access requires `unsafe`; one-field initialization; field-type restrictions.

**Logos:** ✅ **closed since v1 — full item landed** (44e05308; Wave 9 conformance sweep e989d16a fixed 11 spec gaps incl. generic unions, where-clauses, nested patterns, pattern-read unsafe gate, union-in-union fields, const/static init, NLL root borrows). `KW_UNION` (`grammars/logos.peg:335`); `union_def` / `pub_union_def` (`:1146,1144` — `type_param_list? where_clause?`); routed through `collect_struct` with `SemaStructInfo::is_union` / `LStructDef.is_union` (`lir.hpp:882`); layout = max-size/max-align blob; field read/write outside `unsafe` rejected; multi-field init rejected (`items.union.init.one-field`).

**Verdict: OK** (was GAP). Probe: declare + one-field init + unsafe read → exit 42.

**Interactions:** unsafe field access — OK (gated). Pattern reads — OK (gated, one-field constraint). Generics / where — OK. Const/static init — OK. Type alias to union — OK. FFI — OK (C-union ABI = blob of max size/align).

**Gaps / debt:**
- Field-type restriction (`Copy` / `&T` / `ManuallyDrop<T>` only, `items.union.fields`) — not verified as enforced for Drop-bearing field types; probe and add the diagnostic if absent.

---

## 5. Const / Static

**Rust:** `ConstantItem` (inlined-at-use) vs `StaticItem` (single allocation, `Sync`, `static mut` unsafe) — two distinct items.

**Logos:** `const_def` (`grammars/logos.peg:666-693`): `const`/`let` + immutable `static` → `CONST_DEF`/`LConst` (`lir.hpp:1032` — still no `is_static`/`is_mut` field); `static mut` → own `STATIC_DEF` code 254 (matched before the immutable alt). `collect_const` (`sema_collect.cpp:2089`); STATIC_DEF registers in `module_static_muts_` (`sema_collect.cpp:1842-1848`); read gate `sema_expr.cpp:546`, write gate `sema_stmt.cpp:2412-2419` (shadowing-aware), both demand `unsafe` per `items.static.mut.safety`. `lower_const_def` (`sema_decl.cpp:1230`).

**Verdict: WARN** (was WARN+GAP). ✅ closed since v1: immutable `static` (0508922a) typing `&STATIC: &'static T` (927461fc); `static mut` parse + unsafe gating (18003dc5); static-refs-static init S3/S6/S11 (7d7f2ee9); `static S: &str = "lit"` S20 + extern statics (4300f0dd); `const _` parses (single instance). **NOT closed — S25 (critical): cross-fn `static mut` access segfaults** — probe 2026-06-12 (`bump(); bump(); get()` → SIGSEGV): STATIC_DEF reuses const storage, mlir-gen materializes a fresh alloca per use instead of one `llvm.mlir.global` + `addressof`. Same root keeps immutable statics without a single allocation/address identity (const-inlining convention).

**Interactions:** const eval via metacall — OK (§A1). Visibility — OK. Generic module consts — OK (Logos extension). `Sync` bound on statics — GAP (no Sync auto-trait, Cat H). Atomics in static init — OK (7f82669e).

**Gaps / debt:**
- **S25**: emit one `llvm.mlir.global` per static (mut or not), route reads/writes via `addressof`. Unblocks address identity for both halves.
- S17: local `static` inside fn bodies — parse error (probed).
- S12: `static F: fn() -> i32 = answer;` — fn name not visible at phase-2 const-init.
- S15: `static mut ARR[i] = …` indexed write bypasses the unsafe gate.
- S2: `static X` + `fn X` collision accepted (value-namespace clash, `items.static.namespace`).
- Second `const _` rejected "duplicate const '_'" — Rust allows N anonymous consts; `_` parses as an ordinary name.

---

## 6. Type alias

**Rust:** `TypeAlias` (`items/type-aliases.md`); `= Type` at item level; where-clauses both sides of `=`.

**Logos:** AST `TYPE_ALIAS`; `type_alias` (`grammars/logos.peg:698-705`, pub × generic alts). LIR `LTypeAlias` (`lir.hpp:1040`). `lower_type_alias_def` (`sema_decl.cpp:1284`). Trait-position form = separate `ASSOC_TYPE_DEF` (`:931-935`).

**Verdict: OK.** Generic alias probe (`type Pair<T> = (T, T)`) green; union-target alias fixed in Wave 9 (e989d16a P31).

**Interactions:** generics, visibility, modules, assoc-type parallel, alias-is-not-a-newtype flattening — OK.

**Gaps / debt:**
- No `where_clause` on item-level alias (grammar has none at `:698`).

---

## 7. Trait

**Rust:** `Trait` item (`items/traits.md`); `unsafe trait`, supertraits, assoc items, dyn-compatibility rules.

**Logos:** AST `TRAIT_DEF`; `trait_def` / `pub_trait_def` (`grammars/logos.peg:831,806`, pub × auto × unsafe × params × supers alts). LIR `LTraitDef` (`lir.hpp:956`). `collect_trait` (`sema_collect.cpp:2353`); `trait_vtable_layout` (`sema_collect.cpp:4701`); `lower_trait_def` (`sema_decl.cpp:1295`).

**Verdict: OK.** ✅ closed since v1: **object-safety enforcement complete** — `check_trait_object_safe` (`sema.cpp:2837`) covers generic methods, no-self, `Self` in return / by-value param, GAT items, `where Self: Sized` opt-out, and opaque `impl Trait` in return/param via `mentions_impl_trait` (b8ad5ef8); trait-method where-clause capture (f3f163f6); `fn new() -> Self where Self: Sized;` decl (3978bd47); where-clause on trait defaults + forward refs (e62b2fa0). `Unpin` is now a real auto trait (structural, negative impls honored — 6dabfe99); dyn+auto-trait enforcement at unsize site (fdae52fb).

**Interactions:** assoc items, supertraits (diamond vtables, `ref_dyn_supertrait_vtable`), `Self`, generics, where, default methods, static+dynamic dispatch, `impl Trait for &T` (208ee9d3) — OK. Send/Sync auto-derivation — GAP (Cat H). `?Trait` — only `?Sized` honoured.

**Gaps / debt:**
- **Trait-side assoc-const default not inherited** (new finding): `trait Tr { const C: i32 = 7; }` + non-overriding impl → `S::C` / `Tr::C` / `Self::C` all fail to resolve ("unknown enum"); impl-side `const C = 9` + `S::C` works (probe exit 9). Grammar carries the default (`ASSOC_CONST_DEF` VALUE, `:939`); lookup lacks the trait-default fallback.
- Coherence/overlap detection still light (multi-impl selection 8c10eb4e made first-wins sites shape-aware but is not an overlap checker).

---

## 8. Impl block (inherent / trait)

**Rust:** `InherentImpl` / `TraitImpl` (`items/implementations.md`).

**Logos:** AST `IMPL_BLOCK`; `impl_block` (`grammars/logos.peg:950+`, unsafe × negative × generic alts). LIR `LImplBlock` (`lir.hpp:991`). `collect_impl` (`sema_collect.cpp:2584`); `lower_impl_block` (`sema_decl.cpp:1332`).

**Verdict: OK.** Inherent vs trait distinguished; negative impls parse. ✅ since v1: **multi-impl selection by self-type shape** (8c10eb4e) — generic overloads, Deref `impls_all_`, mono `__g__` keys, ConstVar/AssocType defer all pick by shape instead of first-wins; `impl Trait for &T` / `&mut T` reference-Self (208ee9d3).

**Interactions:** trait/struct/enum/union targets, generics, lifetimes, where, assoc items, blanket impls (`$blanket$` keys), receivers `self`/`&self`/`&mut self`/`self: Box<Self>` — OK.

**Gaps / debt:**
- Arbitrary self types: `fn get(self: Pin<&mut S>)` parses but method dispatch on a `Pin<&mut S>` receiver fails ("has no method") — probe 2026-06-12. `self: Rc<Self>` likewise unproven.
- Orphan/coherence — partial (see Trait).

---

## 9. Module

**Rust:** `Module` — `mod x;` / `mod x { … }`, nested, `#[path]` attr (`items/modules.md`).

**Logos:** **No `mod` item** (unchanged). File-level `module` production (`grammars/logos.peg:480`): `KW_PACKAGE IDENT path_dot_ident* SEMI` + uses + items; one file = one package; dotted package + `::` into items (`ref_logos_path_model`). No `KW_MOD`.

**Verdict: WARN** (unchanged). The package model is the intended design, but **DIVERGENCES §A still has no row for it** (v1 flagged this; not done — `grep package docs/DIVERGENCES.md` → no §A match). Scoreboard inconsistency: blessed-in-memory, uncatalogued in the register.

**Interactions:** item visibility (`IS_PUB`), dotted paths, use decls, name resolution (`ImportScope`), preludes — OK. Inline `mod x { … }` — GAP. `pub(crate)` / `pub(super)` / `pub(in path)` — absent (no `KW_PUB LPAREN` in grammar). `extern crate` — N/A (package == crate). `#[path]` attr — absent (moot under package-per-file, but unlisted).

**Gaps / debt:**
- Add the §A row: `package` vs `mod`, dotted separator, no-inline-mod. One sitting.
- Visibility ladder (`pub(crate)`/`pub(super)`) if Rust imports require it.
- Inline `mod x { … }` for multi-mod single-file test ports — still tracked nowhere.

---

## 10. Use declaration

**Rust:** `UseDeclaration` / `UseTree` — `*`, `{…}`, `as`, `self` (`items/use-declarations.md`).

**Logos:** `use_decl` / `pub_use_decl` / `use_variants_decl` (`grammars/logos.peg:498,485,491`). Dotted paths; variant-group form `use pkg.Type.{V1, V2};`. `ImportScope` handles wildcard packages, variant aliases, re-exports.

**Verdict: WARN** (unchanged). `use … as bar;` still rejected (probe: syntax error). Dotted separator + group shape still uncatalogued in DIVERGENCES.

**Interactions:** paths, `pub use` re-export, resolution, preludes — OK. Glob — implicit (`use pkg;` = whole package); explicit `*` absent. Aliasing (`as`) — GAP. `use … as _;` — GAP. `self::`/`super::`/`crate::` roots — absent.

**Gaps / debt:**
- `use foo as bar;` — smallest-surface open item; extend `use_decl` + alias map in `ImportScope`.
- Catalog `.` separator + `Type.{…}` group in DIVERGENCES.

---

## 11. External block (`extern`)

**Rust:** `unsafe? extern Abi? { fns + statics }` (`items/external-blocks.md`); 2024 adds `safe`/`unsafe` item qualifiers.

**Logos:** ✅ **block form landed** (2806e349): `extern_block` (`grammars/logos.peg:1192-1194`) = `KW_EXTERN STRING? LBRACE extern_block_item* RBRACE`; children use bare `fn …;` and — since 4300f0dd — `static [mut] NAME: T;` (`:1206-1208`). Single-decl `extern "C" fn …;` also takes the ABI (`:1210`). Sema flattens blocks into the module item stream and validates ABI ∈ {"C","C-unwind","system","Rust"} (`sema_collect.cpp:1275-1321`); EXTERN_FN carries the ABI in VALUE. **Extern fns are implicitly unsafe to call** — probe: call outside `unsafe` → "call to unsafe function 'abs' requires unsafe context".

**Verdict: WARN** (surface OK; codegen threading open). Calling convention is NOT threaded: no `abi` field on `Kind::FnPtr`/`LFunction` — all four ABI strings map to the platform C convention (benign on linux-x86-64; wrong the day a stdcall target appears; logos-core §6.7 lists it as the follow-up).

**Interactions:** FFI calls + varargs `...` — OK. unsafe gate — OK. Extern statics — parse OK (runtime linkage shares the S25 global-storage gap). Fn-ptr types — OK.

**Gaps / debt:**
- Thread ABI → `Kind::FnPtr` + per-call convention in mlir-gen.
- `#[link(name = "…")]` — absent (no handler in `sema_collect.cpp`).
- 2024 `safe fn` / `unsafe static` item qualifiers inside extern blocks — absent.

---

## 12. Associated items

**Rust:** assoc fn / type / const in `trait` / `impl` (`items/associated-items.md`); GATs; arbitrary receivers.

**Logos:** `FN` in trait/impl context; `ASSOC_TYPE_DEF` (`grammars/logos.peg:931-935`, GAT `type_param_list`, bounds-only + `= default` alts); `ASSOC_CONST_DEF` (`:939-941` — **now admits `= expr` default**) / `ASSOC_CONST_IMPL` (`:1038`). Qualified projection `<T as Tr>::Item` (`:1433`).

**Verdict: OK.** ✅ since v1: GATs confirmed practically Rust-conformant — decl/impl/projection-with-args/lifetime-args/bounds/mono multi-step all exercised (`ref_gat_rust_conformant`; used in Writ fabric `Datatype::View<S>`); object-safety interaction closed (see §7).

**Interactions:** trait/impl sides, generics, `Self::Item` paths, GATs, impl-side where/assoc-type-equality — OK. Impl-side assoc-const definition + `S::C` access — OK (probe exit 9).

**Gaps / debt:**
- Trait-default assoc const not inherited (see §7 — the resolution fallback, not the grammar).
- Inherent impls containing assoc type aliases — Rust forbids (`items.impl.inherent.type-alias`); Logos diagnostic still unverified.
- GAT-instantiation equality inside generic bodies — rough edge (fabric.logos:84 workaround).

---

## Cross-category gaps

- **S25 static global storage** — Const/Static + Cat G/N; the one C-category miscompile.
- **`#[repr(C)]`/`packed`/`align`** — Struct/Union layout driver (Cat L); `transparent`+`uN` done.
- **`Send`/`Sync` auto-traits** — Trait + Static `Sync` bound (Cat H).
- **ABI threading to codegen** — Extern block + Cat N.
- **Visibility ladder `pub(crate)`/`pub(super)`** — Module + Cat I.
- **`use … as …`** — Use decl + Cat I.
- **Arbitrary self types (`Pin<&mut Self>`, `Rc<Self>`) dispatch** — Impl + Cat B (Pin API itself landed, 6dabfe99).
- **Derives** — all 8 trait-family handlers landed Wave 8 (logos-core §6.10); surface remains `#[derive_<trait>]`-shaped metaprog (§A3) — Cat J/L.

## Recommended next moves

1. **S25 — real global storage for statics.** One `llvm.mlir.global` per static (mut and immutable), reads/writes via `addressof`. Kills the cross-fn segfault, gives Rust address identity, and unblocks extern-static linkage. Highest-value C item; one session.
2. **Trait-default assoc-const inheritance.** Resolution fallback trait→impl when the impl doesn't override; grammar already carries the default. Small; also fix `S::C`-style diagnostics ("unknown enum" for a const path).
3. **Catalog the module/path model in DIVERGENCES §A.** `package` vs `mod`, dotted separator, `Type.{…}` use-groups, no inline mod — flagged in v1, still missing. Doc-only session.
4. **`use foo as bar;` aliasing.** Grammar + `ImportScope` alias map. One session.
5. **ABI → FnPtr threading + `#[link]`.** Completes §6.7 beyond parse/validate. One session.
6. **Union field-type restriction check** (Drop-bearing field types must be `ManuallyDrop`); plus S2/S12/S15/S17 statics follow-ups as a sweep.
