# Category C — Items (audit)

Generated: 2026-05-30; spec: rust-lang/reference (local checkout at `/home/victor/cxx/reference`)

12 features audited: 6 OK, 4 WARN, 2 GAP. Highlights: Logos has solid coverage of `fn`, `struct`, `enum`, `trait`, `impl`, `const`, `type alias`, and `use` with Rust-aligned naming for most. Genuine gaps: **Union** is entirely absent (no `KW_UNION` in grammar; no `Kind::Union` in LIR); **`static mut`** is not a distinct mechanism — only immutable module-level `static` is parsed, and it lowers to the same `LConst` node as `const`. Naming warns: `MODULE` does not exist as an inline item form (Logos uses package-per-file), and the `pub_datatype_def` (`eidos`) / inherent vs trait impl machinery have Logos-specific extensions.

---

## 1. Function (`fn`)

**Rust nomenclature:** `fn` item (`items/functions.md`, AST `Function`). Qualifiers: `const fn`, `async fn`, `unsafe fn`, `extern "ABI" fn`, `safe fn` (extern-only).

**Logos nomenclature:** AST node `FN` (CODE alias `la::FN`), grammar productions `fn_def` / `pub_fn_def` (`tools/peg_gen/grammars/logos.peg:1146,1172`), method shorthand `method_def` / `pub_static_fn_def` / `static_fn_def` (`grammars/logos.peg:1119,989,1005`), trait-method shape `trait_method` (`grammars/logos.peg:833`), foreign `extern_fn_def` (`grammars/logos.peg:1122`). LIR struct `LFunction` (`include/logos/compiler/lir.hpp:700`) with `is_extern` flag (`lir.hpp:721`). Collector `SemaChecker::collect_fn` (`src/compiler/sema_collect.cpp:3727`); lowerer `lower_fn` (`src/compiler/sema_decl.cpp:128`); codegen `mlir_gen_fn.cpp`.

**Match verdict:** OK on the core name (`fn` keyword, `FN` node, `LFunction`); WARN on qualifier set — `const fn` is intentionally replaced (DIVERGENCES §A1/A2 via metacall), `async fn` is intentionally absent (§A4 fibres); `extern "ABI" fn` is a single token (`KW_EXTERN KW_FN`) with no ABI string — Rust spelling `extern "C" fn` not accepted (see `grammars/logos.peg:1122`). `safe fn` (Rust 2024) absent.

**Implementation pointer:** lowering `src/compiler/sema_decl.cpp:128-833`; codegen `src/compiler/mlir_gen_fn.cpp:1-414`.

**Interactions check:**
- Generics — OK. `TYPE_PARAMS` slot on every `FN` alt; `lower_fn` substitutes via `current_type_params_`.
- Lifetimes — OK. `LIFETIME_PARAM` items inside `type_param_list`; outlives computed at `sema_decl.cpp:21` (`compute_fn_lifetime_outlives`).
- Where-clauses — OK. `where_clause` admitted on every FN alt (`grammars/logos.peg:1154-1190`); `collect_fn` parses subject + bound list.
- `unsafe fn` — OK. `KW_UNSAFE` prefix → `IS_UNSAFE` flag; carried into `LFunction.is_unsafe` and checked at call sites.
- `async fn` — N/A — blessed §A4 divergence.
- `const fn` — N/A — blessed §A1/A2; `const` not a fn qualifier in Logos grammar.
- `extern "ABI" fn` — WARN. `extern_fn_def` exists but takes no ABI string; only one implicit ABI ("C-like varargs") is captured (`grammars/logos.peg:1122-1127`). Rust `extern "system" fn`, `extern "Rust" fn` not representable.
- Function-item type — OK in concept (zero-sized fn-item per item); Logos treats `FnPtr` as the realizable form (see Category B audit).
- Patterns in params — partial. The grammar's `param` (not shown above) accepts simple `ident: type`; nested patterns in fn-params (`fn first((a,b): (i32,i32))`) not fully supported. Tracked in §B4 cluster.
- Return type — OK. `RET_TYPE` slot; default unit when absent.
- Diverging (`-> !`) — OK at type-system level (Never type, Cat B).
- Attributes (`#[inline]`, etc.) — partial. `inline` recognised under metaprog handlers, but Rust-level `#[inline(always)]` etc. only partially present.

**Gaps / debt:**
- ABI string on `extern fn` is single-token; multi-ABI matrix absent.
- No `safe fn` qualifier (Rust 2024 edition surface).
- Fn-param patterns beyond `ident:type` (tuple/struct destructure) — partial; see §B4.
- `KW_NEW` / `KW_NULL` carve out keywords-as-fn-names (`grammars/logos.peg:865-883`) — a Logos extension; no Rust analogue.

---

## 2. Struct (named / tuple / unit)

**Rust nomenclature:** `StructStruct` / `TupleStruct` / unit-like (`items/structs.md`).

**Logos nomenclature:** AST `STRUCT` node; productions `struct_def` / `pub_struct_def` (named, `grammars/logos.peg:1077,1068`), tuple-struct alt inside the same productions (`grammars/logos.peg:1081`), `struct_unit` / `pub_struct_unit` (`grammars/logos.peg:1053,1050`), explicit-instantiation form `struct_inst` / `pub_struct_inst` (`grammars/logos.peg:1057,1060`). LIR `LStructDef` (`include/logos/compiler/lir.hpp:825`). Collector `collect_struct` (`src/compiler/sema_collect.cpp:3274`); `is_tuple_struct` flag computed `sema_collect.cpp:3311`. Lowerer `lower_struct_def` (`src/compiler/sema_decl.cpp:834`).

**Match verdict:** OK. Three Rust shapes (named/tuple/unit) all exist; unit struct caught up 2026-05-25 per DIVERGENCES (G172-14, line 78).

**Implementation pointer:** decl `sema_decl.cpp:834-952`; mono `mono_clone.cpp` clones via struct_templates_; mlir-gen via struct registry (qualified keys + bare alias).

**Interactions check:**
- Generics — OK. `TYPE_PARAMS` slot on all four shapes.
- Visibility (per-field) — OK. `field_def` admits `KW_PUB` per field (`grammars/logos.peg:1099`); IS_PUB carried through.
- Drop — OK. `impl Drop for S` recognised; recursive drop in `collect_drops`.
- Copy — OK (see Category A). Auto-Copy + manual impl coexist.
- Move (field-wise) — OK (partial moves; see Category A audit).
- Repr — partial. `#[repr(C)]` and equivalents are handled by metaprog handlers, but a first-class `#[repr(...)]` parser+layout-driver is absent; LStructDef has no `repr` field exposed in `lir.hpp:825`.
- DST (last-field unsized) — OK. Custom-DST tail-slice DONE 2026-05-29 (B2 in DIVERGENCES).
- Construction expr — OK (`StructLit`).
- Pattern destructure — OK.
- Field access — OK; `.0`/`.1` for tuple-struct synthesised names.
- Lifetime params — OK.
- Methods (via impl) — OK; also inline `method_def` inside the struct body (`grammars/logos.peg:1068,1077`) is a Logos sugar — Rust requires a separate `impl` block. Blessed-style ergonomic, not in DIVERGENCES.
- Variance (per-field) — OK; see Cat A Variance.

**Gaps / debt:**
- Inline methods inside `struct { ... }` are a Logos extension not in DIVERGENCES §A. Either bless or remove.
- `#[repr(...)]` is not a first-class attr; layout-driving annotations route through metaprog. Worth a §B-style row.
- The HASH-NAME_VAR (antiquot `#name`) form is a metaprog quoting concession; works fine but distinct from Rust spec.

---

## 3. Enum

**Rust nomenclature:** `Enumeration` with `EnumVariant`s (`items/enumerations.md`); variants may be unit, tuple-like, struct-like; explicit `EnumVariantDiscriminant = expr`; `#[repr(uN)]` backing.

**Logos nomenclature:** AST `ENUM` node; productions `enum_def` / `pub_enum_def` (`grammars/logos.peg:699,690`); variant productions `variant_def` (with `IS_VARIADIC`, `IS_STRUCT_SHAPE` flags) (`grammars/logos.peg:732`). LIR `LEnumDef` (`lir.hpp:868`). Collector `collect_enum` (`sema_collect.cpp:1433`); lowerer `lower_enum_def` (`sema_decl.cpp:953`). Backing-integer type via `COLON type_ref` (`grammars/logos.peg:703`) — Rust spells this `#[repr(uN)]` (Logos's spelling is a divergence not in §A).

**Match verdict:** WARN — backing-integer syntax `enum E: u64 {...}` is non-Rust (Rust uses `#[repr(u64)]`). All three variant shapes (unit/tuple/struct-like) present. Discriminant `= expr` covered.

**Implementation pointer:** `sema_decl.cpp:953-1040`; mlir-gen uses value-repr (B7 done, MEMORY.md).

**Interactions check:**
- Variants — OK (all three shapes, `variant_def`).
- Discriminant (`= expr`) — OK (`EnumVariantDiscriminant` in `variant_def`).
- Match exhaustiveness — OK in match-codegen path; payload-binding inners still partial per DIVERGENCES line 71.
- Patterns (variant pat) — OK.
- Drop (payload-recursive) — OK after B7 (value-repr).
- Niche optimization — GAP. No niche layout in `LEnumDef`; tag is always `i32`. `Option<&T>` / `Option<Box<T>>` not niche-encoded.
- `#[repr(uN)]` — WARN. Logos accepts a `COLON type_ref` syntactic form instead; Rust spelling `#[repr(u64)]` would route through metaprog only. Likely worth a §B row.
- Generics — OK.
- Methods — OK (via impl).
- Never (empty enum) — partial. `LEnumDef` accepts zero variants but exhaustiveness logic for the empty enum (uninhabited) is not specially exploited (Cat B Never type).
- Option/Result — OK, in `stdlib/lang/option`, `stdlib/lang/result`.

**Gaps / debt:**
- Backing-int syntax `enum E: u64` is a Logos divergence not catalogued in §A — add or replace with `#[repr]`.
- Niche optimization absent (every enum is `{i32 disc, [N x i8]}` per B7 design); `Option<&T>` will be 16 bytes instead of 8.
- Uninhabited-enum exhaustiveness (`match e {}` for `enum Void {}`) untested.

---

## 4. Union

**Rust nomenclature:** `Union` item (`items/unions.md`) — declared with `union` keyword; field-access requires `unsafe`; field-type restrictions (`Copy` / `&T` / `ManuallyDrop`).

**Logos nomenclature:** None. `grep KW_UNION` returns no matches in `grammars/logos.peg`; no `union_def` production; no `LUnion` / `Kind::Union` in `include/logos/compiler/lir.hpp`; no `collect_union` in `sema_collect.cpp`.

**Match verdict:** GAP — feature absent entirely. The string "tagged union" appears once in a comment (`sema_collect.cpp:1561`) referring to *enum* payloads, not Rust unions.

**Implementation pointer:** n/a.

**Interactions check:**
- Field access (`unsafe` read) — n/a — feature absent.
- Drop (manual) — n/a.
- Repr — n/a.
- FFI — n/a (limits FFI parity for libs that surface C unions).
- `Copy` (all fields Copy) — n/a.
- Type layout — n/a.
- Generics — n/a.

**Gaps / debt:**
- Add a §B row in DIVERGENCES (no §A blessing; FFI parity wants it).
- For Hermes purposes the closest analogue is the `eidos` POD `datatype` (`grammars/logos.peg:1019`) but that's struct-shaped, not union.
- Imports that mention `union` will be hard-skipped.

---

## 5. Const / Static

**Rust nomenclature:** Two distinct items — `ConstantItem` (`items/constant-items.md`; inlined-at-use, no allocation) and `StaticItem` (`items/static-items.md`; has a single allocation, requires `Sync`, `static mut` exists).

**Logos nomenclature:** Both fold into a single `CONST_DEF` node and `LConst` struct. `const_def` grammar production (`grammars/logos.peg:652-671`) admits seven alternative left-hand sides — `KW_CONST`, `KW_LET` (module level), and `KW_STATIC` — all building the same `CODE: CONST_DEF`. The `static` alt comment at line 664-666 explicitly says "Immutable module-level `static NAME: T = expr;` — same immutable storage model as `const`; `static mut` is intentionally NOT matched here." Collector `collect_const` (`sema_collect.cpp:1664`); lowerer `lower_const_def` (`sema_decl.cpp:1041`). `LConst` (`lir.hpp:977`) has no `is_static` / `is_mut` discriminator.

**Match verdict:** WARN on `const` (works but is grammar-collapsed with `static`); GAP on `static mut` (explicitly absent per grammar comment). The single-allocation property of Rust `static` (vs. const's inline-at-use) is also collapsed — Logos consts/statics share the same lowering with a per-use re-eval (see `sema_decl.cpp:1047-1053` comment "re-evaluates the const's value at each use-site").

**Implementation pointer:** `sema_decl.cpp:1041-1094`.

**Interactions check:**
- Const eval — OK via metacall (§A1).
- Type inference (no bare `_`) — partial; `const _: T = ...` syntactic form not present (no `IDENT | "_"` alt in `const_def`).
- Mutability (`static mut` unsafe) — GAP — `static mut` not parsed; no `unsafe` read/write through static.
- `Sync` bound (statics) — GAP — Logos has no Sync auto-trait (Cat H).
- Visibility — OK (`pub const` and `pub static`).
- Modules — OK (consts/statics are module-level items).
- Generics (const generics distinct) — partial; module-level generic `const` admitted (`grammars/logos.peg:652,656`) but ‹const generics on types› work separately (Cat D).
- Linkage (`#[no_mangle]`) — partial. Detected at `sema_collect.cpp:1319` for fns; not for consts/statics.

**Gaps / debt:**
- Restore `static` as a distinct kind once `static mut` lands; gate Sync.
- Unnamed const `const _: T = ...` is not parsed.
- Per-use re-eval comment (`sema_decl.cpp:1049`) is a soundness smell when a const-with-Drop is used N times — Rust says one drop per use scope, Logos may drop differently.

---

## 6. Type alias

**Rust nomenclature:** `TypeAlias` (`items/type-aliases.md`). Optional `: Bounds` only in assoc context; `= Type` mandatory at item level.

**Logos nomenclature:** AST `TYPE_ALIAS` node; grammar `type_alias` (`grammars/logos.peg:675-682`) — four alts (pub/no-pub × with/without type_param_list). LIR `LTypeAlias` (`lir.hpp:985`). Lowerer `lower_type_alias_def` (`sema_decl.cpp:1095`). Associated-type variant uses a separate AST node `ASSOC_TYPE_DEF` (`grammars/logos.peg:884-887`).

**Match verdict:** OK on basic shape; the explicit split between item-level `TYPE_ALIAS` and trait-position `ASSOC_TYPE_DEF` matches Rust spec subdivision (`items.type.associated-type`).

**Implementation pointer:** `sema_decl.cpp:1095-1105`; collected in `sema_collect.cpp:1630-1663`.

**Interactions check:**
- Generics (lifetime+type) — OK (`type_param_list?` slot).
- Visibility — OK (`pub_type_alias`).
- Modules — OK.
- Trait associated types (parallel) — OK (`ASSOC_TYPE_DEF`; assoc-type entries collected `sema_collect.cpp:1923-1944`).
- Inference (type aliases are NOT new types) — OK; alias resolution flattens.

**Gaps / debt:**
- No `where_clause` on item-level type alias (Rust accepts it before `=`).
- Bounds-only form `type Foo: Bound;` (trait position) is handled by `ASSOC_TYPE_DEF`, not `type_alias` — naming a touch confusing but functionally correct.

---

## 7. Trait

**Rust nomenclature:** `Trait` item (`items/traits.md`); admits `unsafe trait`, supertraits `: Bounds`, generic params, associated items (fn / type / const).

**Logos nomenclature:** AST `TRAIT_DEF` node; grammar `trait_def` / `pub_trait_def` (`grammars/logos.peg:783-831`) with 24 alternatives covering pub × auto × unsafe × type-params × supers. LIR `LTraitDef` (`lir.hpp:901`). Collector `collect_trait` (`sema_collect.cpp:1866`); vtable layout `trait_vtable_layout` (`sema_collect.cpp:4073`). Associated items inside the trait body: `trait_method` for fns and `ASSOC_TYPE_DEF` / `ASSOC_CONST_DEF` (`grammars/logos.peg:884-889`).

**Match verdict:** OK on naming (`trait`, supertraits as `:` list); auto traits (`KW_AUTO`) admitted at item position. Supertrait vtable layout matches Rust (see [`ref_dyn_supertrait_vtable`] in MEMORY).

**Implementation pointer:** `sema_collect.cpp:1866-2078` (`collect_trait`); `sema_decl.cpp:1106-1142` (`lower_trait_def`); `sema_collect.cpp:4073-4092` (`trait_vtable_layout`).

**Interactions check:**
- Associated items (fn/type/const) — OK.
- Supertraits — OK (`SUPERS` slot; collected; vtable handles diamonds).
- `Self` — OK (recognised in receiver / return positions).
- Generics — OK.
- Where-clauses — OK on trait header *and* on associated items.
- Trait objects (object-safety) — OK; dyn-Trait vtable layout supports supertrait dispatch.
- Default methods — OK (`trait_method` admits a `block` body).
- Marker traits — partial. `Copy` is recognised by name only (sema_collect.cpp:3016). `Sized` is recognised. No general "marker trait" abstraction.
- Auto-traits (`Send`/`Sync`) — partial. `KW_AUTO` parses but `Send`/`Sync` aren't auto-derived (Cat H).
- `impl Trait for Type` — OK.
- Coherence/orphan rules — partial. Some orphan-rule checks live in `collect_impl`, but full coherence (overlap detection across crates) is light.
- Method dispatch — OK (static + dynamic).
- Bounds — OK.

**Gaps / debt:**
- Send/Sync auto-derivation absent (also Cat H).
- `?Trait` relaxed bound: `RELAXED` slot exists (`grammars/logos.peg:%fields RELAXED=39`) but only `?Sized` is honoured.
- `genos` keyword referenced as a comment at `grammars/logos.peg:780` ("the upcoming spec form") — dead reference, can be removed.

---

## 8. Impl block (inherent / trait)

**Rust nomenclature:** `Implementation` with `InherentImpl` (no `for Trait`) and `TraitImpl` (`items/implementations.md`).

**Logos nomenclature:** AST `IMPL_BLOCK` node; grammar `impl_block` (`grammars/logos.peg:898+`) — many alts covering unsafe × negative-impl × generic shapes. LIR `LImplBlock` (`lir.hpp:936`). Collector `collect_impl` (`sema_collect.cpp:2079`). Inherent-impl marker: empty trait_name, with "inherent::" key prefix for assoc-consts (`sema_collect.cpp:2685-2690`).

**Match verdict:** OK; inherent vs trait impl correctly distinguished (`trait_name.empty()` test at `sema_collect.cpp:2121`). Negative impl `impl !Trait for X {}` parses (via `IS_NEGATIVE` slot).

**Implementation pointer:** `sema_collect.cpp:2079-3105`; `sema_decl.cpp:1143` (`lower_impl_block`).

**Interactions check:**
- Trait — OK.
- Struct/Enum/Union — OK for Struct/Enum; Union n/a (feature absent).
- Generics — OK (`IMPL_TYPE_PARAMS` slot).
- Lifetimes — OK (`impl_lt_params`, `impl_lt_outlives`).
- Coherence rules — partial (see Trait).
- `where` clauses — OK (`grammars/logos.peg:898`).
- Associated items — OK (assoc fn / type / const; `sema_collect.cpp:2614-2706`).
- Method receivers — OK for `self`/`&self`/`&mut self`/`self: Box<Self>`; arbitrary receiver types (`self: Pin<&mut Self>`) — partial.
- Orphan rule — partial.
- Blanket impls — OK (separate code path via `$blanket$<trait>` key, `sema_collect.cpp:3987`).

**Gaps / debt:**
- Multi-receiver kinds (`self: Rc<Self>`, `self: Pin<&mut Self>`) — partial.
- Coherence overlap checks: known light, see Trait.

---

## 9. Module

**Rust nomenclature:** `Module` item — `mod x;` (file-loaded) or `mod x { ... }` (inline), nested arbitrarily; defines a name in the type namespace (`items/modules.md`).

**Logos nomenclature:** **No `mod` item.** The file-level production is `module` (`grammars/logos.peg:468`) which is fixed as: optional inner-attrs/docs, then `KW_PACKAGE IDENT path_dot_ident* SEMI`, then `use` declarations, then `item*`. There is no `KW_MOD` keyword; there is `KW_PACKAGE` (`grammars/logos.peg:315`). Each source file IS one package; inline-mod (`mod x { ... }`) does not exist. Path model: dotted package + `::` for items (per `ref_logos_path_model`).

**Match verdict:** WARN — naming divergence (`package` not `mod`); inline-mod GAP. This is closer to a §A-blessed divergence (Logos package model is the design) than a bug, but it is not currently catalogued in DIVERGENCES §A — add a row.

**Implementation pointer:** `sema_collect.cpp:1095` (`collect_module`); module file is one compilation unit.

**Interactions check:**
- Visibility — OK at item level (`IS_PUB`); no `pub(crate)` / `pub(super)` / `pub(in path)` (greppable absence).
- Paths — OK; dotted `a.b.c::Item`.
- Use declarations — OK.
- Name resolution — OK (`ImportScope`, `cur_imports_`).
- Item nesting — GAP (no inline `mod`).
- `extern crate` — absent (Logos has no crate concept; package == crate).
- Inline `mod x { ... }` vs file `mod x;` — GAP.
- Preludes — OK (`#![no_implicit_prelude]` via INNER_ANNOTATION, `sema_collect.cpp:231-251`).
- Crate root — N/A (Logos package model has no crate root).

**Gaps / debt:**
- Add `pub(crate)` / `pub(super)` if Rust-imports require it (visibility ladder).
- Catalog `package` ↔ `mod` in DIVERGENCES §A as a blessed naming divergence; explicit Rust spec citation.
- Inline `mod x { ... }` — required for ports of multi-mod single-file Rust tests; tracked nowhere today.

---

## 10. Use declaration

**Rust nomenclature:** `UseDeclaration` with `UseTree` — supports `*`, `{...}`, `as`, `self`, nested groups (`items/use-declarations.md`).

**Logos nomenclature:** AST `USE` and `USE_VARIANTS` nodes (`grammars/logos.peg:300`); productions `use_decl` / `pub_use_decl` (`grammars/logos.peg:486,473`) and `use_variants_decl` (group form). Path uses dotted (`.`) not `::`. `ImportScope` (`sema_collect.cpp:100-224`) handles wildcard packages + variant aliases + re-exports.

**Match verdict:** WARN — Logos uses `.` separator (dotted) and a curly group with `Type.{V1, V2, ...}` (`grammars/logos.peg:300`). Rust uses `::` and `pkg::{a, b}`. Functionally equivalent; the syntax differs. Catalogued in `ref_logos_path_model` but not in DIVERGENCES §A.

**Implementation pointer:** `sema_collect.cpp:100-224` (`build_import_scope`).

**Interactions check:**
- Paths — OK.
- Visibility (`pub use` re-export) — OK (`sema_collect.cpp:212-221`).
- Name resolution — OK.
- Glob (`*`) — partial. Whole-package use IS implicit glob (`use pkg;` brings all into scope). Explicit `*` doesn't exist syntactically.
- Aliasing (`as`) — GAP (no `KW_AS` in `use_decl`); imports always use the source name.
- Modules — OK.
- Preludes — OK.

**Gaps / debt:**
- `use foo as bar;` (aliasing) not parsed.
- `use self::` / `use super::` / `use crate::` path roots — absent (Logos has no `self`/`super`/`crate` at use position).
- Catalog `.` vs `::` separator + group-curly shape in DIVERGENCES §A or §B.

---

## 11. External block (`extern`)

**Rust nomenclature:** `ExternBlock` — `unsafe? extern Abi? { ExternalItem* }` with statics/fns (`items/external-blocks.md`); fn declarations are body-less.

**Logos nomenclature:** No `extern { ... }` block production. Only `extern_fn_def` (`grammars/logos.peg:1122`) — a single body-less fn declaration. AST `EXTERN_FN`; LIR `LFunction.is_extern = true` (`lir.hpp:721`).

**Match verdict:** WARN — Logos surfaces individual `extern fn name(...) -> T;` declarations at item position; no grouping block. ABI not parameterised. `extern { static FOO: T; }` (extern static) absent.

**Implementation pointer:** `sema_collect.cpp:3855-4043` for `EXTERN_FN` processing.

**Interactions check:**
- FFI — partial. Direct calls work (varargs `...` accepted).
- ABI — GAP (no Abi string).
- `unsafe` (extern fn calls) — partial. Extern fns are implicitly unsafe to call in Rust; Logos doesn't enforce a calling-context check for `extern fn` calls.
- Static (`static FOO: T;` extern) — GAP.
- Linkage attributes (`#[link]`) — GAP.
- Function-pointer types (matched signature) — OK.

**Gaps / debt:**
- Extern static declarations (`extern { static ERRNO: c_int; }`) absent.
- ABI grouping / per-block ABI tag absent.
- `#[link(name = "...")]` absent.

---

## 12. Associated items

**Rust nomenclature:** Items inside `trait` / `impl` blocks — assoc fn (incl. methods), assoc type, assoc const (`items/associated-items.md`); may carry GATs (generic associated types).

**Logos nomenclature:** AST nodes `FN` (assoc fn / method, identified by being inside `TRAIT_DEF` / `IMPL_BLOCK`), `ASSOC_TYPE_DEF` (`grammars/logos.peg:884`), `ASSOC_CONST_DEF` (`grammars/logos.peg:888`). Storage `SemaAssocTypeInfo` / `SemaAssocConstInfo` (`sema_collect.cpp:1923-1947`); resolver references `LogosType::Kind::AssocType` (`sema_collect.cpp:729`). Per-impl assoc-type table: `AssocTypeEntry` (`sema_collect.cpp:2661`).

**Match verdict:** OK — three Rust assoc-item kinds all present, with declaration vs definition distinction (trait declares, impl defines, default in trait body allowed).

**Implementation pointer:** `sema_collect.cpp:1923-1947` (collect declarations); `sema_collect.cpp:2614-2706` (impl-side definitions); `sema_collect.cpp:2983-3007` (completeness check).

**Interactions check:**
- Trait — OK.
- Impl — OK.
- Generics — OK; type-param list on `ASSOC_TYPE_DEF` admits GAT params (`grammars/logos.peg:884`).
- `Self::Item` paths — OK; resolved through AssocType type-kind.
- GATs — partial. GAT parameters parse (line 884: `type_param_list`); resolution at use-sites only partially exercised. Tracked under Cat D.
- Default values — OK for assoc const (`grammars/logos.peg:888` only accepts decl form `KW_CONST IDENT COLON type_ref SEMI` — no `= expr` default; collector accepts both forms via `ASSOC_CONST_DEF`).
- `where` bounds on assoc types — partial; subject in trait body works, impl side via the assoc-type-equality clauses (`sema_collect.cpp:2505`).
- Trait object compatibility (object-safety bars certain shapes) — partial; some object-safety checks (no Sized supertrait, no generic methods) are not all enforced.

**Gaps / debt:**
- Assoc const default-value form `const C: i32 = 99;` in a trait body — grammar `ASSOC_CONST_DEF` doesn't admit `= expr` (`grammars/logos.peg:888`). Workaround: declare in trait + override in impl; default-with-value blocks port.
- Object-safety enforcement (Rust's "dyn-compatibility" rules `items.traits.dyn-compatible.*`) is incomplete.
- Inherent impls cannot contain assoc type aliases (Rust spec `items.impl.inherent.type-alias`); Logos's `collect_impl` doesn't explicitly forbid them — verify and add a diagnostic.

---

## Cross-category gaps

- **`#[repr(...)]` as a first-class attribute** — affects Struct, Enum, Union (Category L Attributes). Today layout-tuning routes through metaprog; Rust spec ties layout to `#[repr]`. (Cat L)
- **`Send` / `Sync` auto-traits** — surfaces in Trait + Static items (`Sync` bound on statics). Tracked in Cat H. (Cat H)
- **`#[derive(Copy)]`** — Copy is auto-promoted structurally, but the surface `#[derive(...)]` is per-trait `#[derive_<trait>]` (DIVERGENCES §A3). Not strictly missing, but surface-naming divergent. (Cat L)
- **Object-safety enforcement** — Trait + Trait objects (Cat B `dyn Trait`). Partial here, partial in Cat B.
- **Module visibility ladder (`pub(crate)`/`pub(super)`/`pub(in path)`)** — Module + Visibility (Cat I).
- **`use ... as ...`** — Use decl + Paths (Cat I).
- **Const-generics inference and complex const-expressions** — Const items + Cat D (const params).

## Recommended next moves

(Each sized for a single working session.)

1. **Audit + bless Logos's module model.** Add a §A row in `docs/DIVERGENCES.md` for `package` vs `mod` and the dotted-path separator. State precisely what the Rust spec calls a module and what Logos calls a package, and whether inline `mod x { ... }` will be added (likely B-row) or stays absent (A-row). One session.

2. **`static mut` minimum-viable.** Today the `static` alt collapses to `LConst`. Add an `is_mut` bit on `LConst` (or split into `LStaticItem`), make `static mut` parse, route reads/writes through an `unsafe` requirement. The Sync bound for non-mut statics can come later. One session.

3. **Union item stub.** Add `KW_UNION`, a `union_def` production mirroring `struct_def`, an `LUnionDef` LIR node, and a Sema collector that *errors* on field access outside `unsafe`. Even a parse-only landing unblocks Rust imports that mention `union` and stops them being silently skipped. One session.

4. **`#[repr(...)]` first-class.** A repr enum (`Rust` / `C` / `Transparent` / `Int(uN)`) stored on `LStructDef` / `LEnumDef`; replace the `enum E: u64` syntax with `#[repr(u64)] enum E`. Wire to layout. One+ session.

5. **`use ... as <ident>;` aliasing.** Smallest-surface item — extend `use_decl` to accept `KW_AS IDENT`, store as alias in `ImportScope.alias_map`, consult during name resolution. One session.

6. **Object-safety enforcement.** Walk the `items.traits.dyn-compatible.*` spec list, identify which checks Logos already does (`Sized` exclusion, generic-method exclusion) and add the missing ones (`Self`-in-non-receiver, opaque return, etc.). One session, mostly diagnostic plumbing.
