# Category I — Modules names visibility (audit)

Generated: 2026-05-30; spec: rust-lang/reference (local checkout at `/home/victor/cxx/reference`)

3 features audited: 0 OK, 3 WARN. Logos uses a Rust-shaped vocabulary for items but ships a deliberately *flat* package model (no inline `mod`, no `crate`/`super`/`self` qualifiers, no fine-grained visibility scopes). Every grammar slot for a Rust *path* uses `.` between dotted package segments and `::` only between item segments — this is a blessed model-level divergence (`ref_logos_path_model.md`). The visibility lattice is binary (`pub` / nothing), so the entire `pub(crate)` / `pub(super)` / `pub(in path)` family is genuinely missing. Name resolution exists (the wildcard-import scope + per-kind `find_*_by_name` lookups), but as a single-tier algorithm: there is no expansion/primary/type-relative split and no formal type/value/macro/lifetime/label namespace abstraction — namespacing is implicit in the separate per-kind C++ maps.

---

## 1. Paths (`a::b::c`, `<T as Trait>::item`)

**Rust nomenclature:** `Path` (`paths.md`). Four kinds: `SimplePath` (used in `use`/`vis`/attributes/macros), `PathInExpression`, `QualifiedPathInExpression`, `TypePath`. Segment qualifiers: `self`, `Self`, `super`, `crate`, `$crate`, leading `::` (global). Turbofish `::<>`.

**Logos nomenclature:**
- *Package path* (Logos' analogue of `SimplePath`): grammar rule `path_dot_ident` — `DOT IDENT !(DOT LBRACE)` (`tools/peg_gen/grammars/logos.peg:492`). Dotted, not `::`-separated. Used by `module`, `pub_use_decl`, `use_decl`, `use_variants_decl` (`grammars/logos.peg:468,473,479,486`). Stored under the AST key `PATH_PARTS` (`la::mod::PATH_PARTS`).
- *Qualified path in type* (UFCS `<T as Trait>::Item`): grammar `qualified_assoc_type` (`grammars/logos.peg:1315`) — `LT type_ref KW_AS simple_type GT_TYPE COLONCOLON IDENT`. This IS Rust-shaped.
- *Path-in-expression / path-in-type root segment*: just `IDENT` followed by `LT_TYPE ... GT_TYPE` (turbofish-less) in `simple_type` (`grammars/logos.peg:1616,1620`). At call position the grammar admits dotted `pkg.sub.fn(...)` (module-as-namespace), see `path_step` in `cfg_slot_type` (`grammars/logos.peg:1275`). At expression position `IDENT COLONCOLON IDENT` is used for enum variants (`enum_xref` `grammars/logos.peg:717`, `enum_lit` `grammars/logos.peg:2517`) and annotation values (`annot_val` `grammars/logos.peg:604`).
- Turbofish-shaped genericised callee at expression position: `GENERIC_REF` (`tools/peg_gen/grammars/logos.peg:273`) — `IDENT::<TARGS>`.
- C++ key fabric: `sema_key(pkg, name)` returning `"pkg::name"` (`src/compiler/sema_impl.hpp:882`). Internal lookups always use `::` between the package qualifier and item name.

**Match verdict:** WARN — Logos diverges deliberately on the segment separator (`.` between package segments vs Rust's `::`) and on the qualifier set. This is the blessed Logos path model (`ref_logos_path_model.md`). Fully-qualified `pkg.sub.Item` *in expression/type position* is **not parsed** (`docs/language/reference/modules.md:127`, `:142`) — only the `use ...;` import-then-bare-name form works. That's a parity gap inside the divergent model itself.

**Implementation pointer:** Grammar `tools/peg_gen/grammars/logos.peg:492,1275,1315,1616-1621,717,604`. Path-qualified key construction `src/compiler/sema_impl.hpp:882`. Path qualifier scan / dotted-call recognition is inside `sema_expr.cpp` (`grep find_fn_by_name`).

**Interactions check:**
- Modules — WARN. There is no inline `mod x { ... }` form; one package per file (or per directory under a `logos.module` manifest, `src/compiler/module_manifest.cpp:33`). Documented divergence (`docs/language/reference/modules.md:146`).
- Use declarations — WARN-divergent shape: `use pkg.sub.X;` (not `use pkg::sub::X;`), see `use_decl` (`grammars/logos.peg:486`). `use a::b::{c, d};` does not parse; the variant-list shorthand `use pkg.Enum.{V1, V2}` is a Logos-specific bare-variant-import (`use_variants_decl`, `grammars/logos.peg:479`). No `as`-aliasing (`KW_AS` is used for casts and qualified-assoc-type only; not on `use_decl` arms).
- Crate roots / `super`/`self`/`crate`/`$crate` — GAP. None of these qualifier keywords are reserved in `tools/peg_gen/grammars/logos.peg` (grep `"crate"|"super"|"\\$crate"` returns nothing) and `sema_*.cpp` has no special-case handling. `Self` IS handled — special-cased as a type-param entry in `current_type_params_["Self"]` (`src/compiler/sema_decl.cpp:204,225,1574`, `sema_collect.cpp:1870`) and refers to the current impl/trait subject. `self` parameter is handled at fn-signature level (`sema_decl.cpp:576` `pname == "self"`).
- Generics (turbofish `::<>`) — OK. `GENERIC_REF` node at expression position (`grammars/logos.peg:273`); type-position turbofish uses `IDENT LT_TYPE` without the leading `::` (since `<` is unambiguous in types — same simplification as Rust's `TypePath`).
- Associated items (`Self::Item`, `Trait::method`) — OK. `assoc_type_ref` `grammars/logos.peg:1319`, `qualified_assoc_type` `grammars/logos.peg:1315`. `Self::C` resolves via `current_type_params_["Self"]` substitution.
- Qualified paths (UFCS `<T as Trait>::item`) — OK at type position (`qualified_assoc_type` `grammars/logos.peg:1315`). At expression position UFCS is partial (used inside `enum_lit` `<T as Trait>::Variant`, `grammars/logos.peg:2517`); a general `<T as Trait>::method(arg)` expression form was not found in the grammar.

**Gaps / debt:**
- Fully-qualified path *in expression / type position* (`pkg.sub.Item` or `pkg::sub::Item`) does not parse — must go via `use` (documented `modules.md:127,142`).
- `use ... as Alias;` not supported (`KW_AS` absent from `pub_use_decl` / `use_decl` arms `grammars/logos.peg:473,486`).
- `use a.b.{c, d};` (sub-package brace-list import) parses only as `USE_VARIANTS` and is desugared to per-variant wildcards (`sema_collect.cpp:139-162`); brace-list of items (without enum prefix) does not parse.
- Glob `use foo::*;` not parsed (always implicit wildcard — `modules.md:145`).
- Path qualifier keywords `crate`/`super`/`self` (in paths) and `$crate` (in macros) are entirely absent — Logos has only `package <dotted>;` at file head.
- No general UFCS `<T as Trait>::method(arg)` at expression position.

---

## 2. Visibility and privacy

**Rust nomenclature:** `Visibility` (`visibility-and-privacy.md`). Lattice: `pub` / `pub(crate)` / `pub(super)` / `pub(self)` / `pub(in SimplePath)` / default-private. Default-public exceptions: assoc items in `pub` trait, variants in `pub` enum.

**Logos nomenclature:**
- Grammar: `KW_PUB = "pub"` (`tools/peg_gen/grammars/logos.peg:343`). Used as a prefix on every "publishable" production: `pub_use_decl` `:473`, `pub_instantiate_decl` `:557`, `const_def` `:652`, `pub_enum_def` `:690`, `variant_struct_field` `:724`, `pub_trait_def` `:783-805`, `pub_static_fn_def` `:990`, `pub_datatype_def` `:1019`, `pub_datatype_inst` `:1028`, `pub_trait_inst` `:1037`, `pub_struct_unit` `:1050`, `pub_struct_inst` `:1057`, `pub_struct_def` `:1068`, `tuple_field` `:1089`, `field_def` `:1099`, `pub_fn_def` `:1146`. The AST key is `IS_PUB` (e.g. `pub_use_decl` `=> { IS_PUB: 1, ... }`).
- Sema: `bool is_pub` field on `SemaFuncInfo` / `SemaStructInfo` etc. (`src/compiler/sema_decl.cpp:1642,1705,1772`; `sema_collect.cpp:3187,3291,3856,3861`). Re-export tracking `pkg_reexports_` (`sema_collect.cpp:218`). Enforcement entry point `SemaChecker::check_pub_access` (`sema_collect.cpp:673-678`) emitting `"'{name}' is private to package '{def_package}'"`.
- The `pub` parenthesised forms (`pub(crate)`, `pub(super)`, `pub(in path)`, `pub(self)`) — grep `"pub("` returns no matches anywhere in `src/compiler/` or `tools/peg_gen/grammars/`. The grammar has no `KW_LPAREN` arm after `KW_PUB`. Documented gap: `docs/language/reference/modules.md:51` "`pub(crate)` / `pub(super)` — Not implemented".

**Match verdict:** WARN — `pub` keyword and the public/private split are Rust-aligned. The granular sublattice (`pub(crate)`, `pub(super)`, `pub(in SimplePath)`, `pub(self)`) is uniformly absent. Errors talk about "package" not "module" (`sema_collect.cpp:677`), which matches Logos' model (one package == one Rust crate-ish unit) but warns against Rust spec terminology if the long-term goal is parity prose.

**Implementation pointer:** Grammar `tools/peg_gen/grammars/logos.peg:343` and every `pub_*` arm; enforcement `src/compiler/sema_collect.cpp:673-678`; re-export map `src/compiler/sema_collect.cpp:212-221` and `src/compiler/sema_impl.hpp:2393-2403` (`effective_import_pkgs`).

**Interactions check:**
- Modules — partial. Visibility is enforced at the package boundary (`check_pub_access` `sema_collect.cpp:675`), but there's no intra-package scope so `pub(self)` / `pub(super)` are inexpressible (no `super`).
- Items (per-item visibility) — OK for the kinds wired up (fn, struct, datatype, trait, const, type alias, enum). Enum-variant per-variant visibility — N/A (Rust: variants of a `pub enum` are auto-public, no per-variant `pub`). Logos enum-variants don't carry IS_PUB — matches Rust default-public rule.
- Struct fields (per-field) — OK. `field_def` `grammars/logos.peg:1099`, `tuple_field` `:1089`, `variant_struct_field` `:724` all admit `KW_PUB`. Field-level `is_pub` enforcement at field-access site exists but the audit didn't probe its strictness (Category C had this OK).
- Use declarations (`pub use` re-export) — OK. `pub_use_decl` `grammars/logos.peg:473`, sema records into `pkg_reexports_` (`sema_collect.cpp:218`); `effective_import_pkgs()` (`sema_impl.hpp:2393`) walks the transitive closure during lookup.
- Trait items (no per-method modifiers in Rust) — WARN-divergent: Logos `trait_method` and `impl`-body methods carry `IS_PUB` but the collector force-sets `is_pub = true` for trait/impl-block fns (`sema_decl.cpp:1642`, `sema_collect.cpp:2595,2972`) — so the field exists but the effective rule matches Rust. Cosmetic divergence.
- `pub(crate)` / `pub(super)` / `pub(in path)` — GAP. Not parsed, not represented in any sema struct.
- Crate boundary — WARN-divergent. The "crate" boundary in Logos == package boundary; `check_pub_access` compares `def_package` vs `cur_package_`. The lib boundary (a binary archive) is also a package boundary in Logos. There is no separate "this crate vs other crates" distinction matching Rust's `pub(crate)`.

**Gaps / debt:**
- Whole `pub(in path)` family missing — listed in `docs/language/reference/modules.md:144` and in `docs/DIVERGENCES.md` reach (not a blessed divergence). RFC-style follow-up needed.
- No `pub` form for enum variants (matches Rust — N/A); enum-variant visibility on `non_exhaustive`-style modifiers absent.
- No `pub use ... as Alias;` — see Paths gap.
- Re-export check leaks bare-name fallback: `lookup_qualified_` (`src/compiler/sema_impl.hpp:2414`) falls through to `m.find(std::string(name))` after pkg+imports, with NO pub check on that bare-key tier (`PubCheck` runs only on the imported-pkg hit `sema_impl.hpp:2425-2428`). Comment at `modules.md:154` admits "host-injected items only" — but should be tightened or asserted.

---

## 3. Name resolution / preludes / namespaces

**Rust nomenclature:** Three resolution stages (expansion-time, primary, type-relative — `names/name-resolution.md`). Namespaces: Type, Value, Macro (with bang/attr sub-namespaces), Lifetime, Label (`names/namespaces.md`). Preludes: Standard library, Extern, Language, `macro_use`, Tool (`names/preludes.md`); `#![no_implicit_prelude]` opt-out; `#![no_std]` swaps `std` → `core`.

**Logos nomenclature:**
- *Resolution machinery*: `ImportScope` struct holding `wildcard_packages` + `variant_aliases` (`src/compiler/sema_impl.hpp:871-878`); current scope `cur_imports_` (`sema_impl.hpp:879`); current package `cur_package_` (`sema_impl.hpp:864`). Per-file imports are built once by `build_import_scope` (`sema_collect.cpp:102-224`). Implicit prelude injection by `maybe_inject_implicit_prelude` (`sema_collect.cpp:231-252`).
- *Lookup template*: `SemaChecker::lookup_qualified_<PubCheck>` (`sema_impl.hpp:2414-2435`) — three-tier search: (1) `cur_package_::name`, (2) each pkg in `effective_import_pkgs()`, (3) bare `name`. Wrappers: `find_struct_by_name` (`:2436`), `find_datatype_by_name` (`:2439`), `find_enum_by_name` (`:2442`), `find_trait_by_name` (`:2445`). Function lookup walks `funcs_` / `generic_funcs_` directly (`sema.cpp:1441-1513`).
- *Namespaces*: there is NO `enum class Namespace` or namespace tag. Type/value/macro are partitioned implicitly by the per-kind maps (`structs_` / `datatypes_` / `enums_` / `traits_` typed maps in `sema_impl.hpp:3949-3962`; functions in `funcs_` / `generic_funcs_`). Lifetimes have their own grammar token slot `LIFETIME = 40` (`tools/peg_gen/grammars/logos.peg:60`) and AST `LIFETIME_PARAM = 131` (`:190`). Loop labels are `LABEL = 47` (`grammars/logos.peg:67`) and `LABELED_LOOP = 142` (`:201`). Macros are dispatched through metaprog (`ref_subsystem_metaprog.md`) and live in their own scope under `#[metaprog_handler]` registration.
- *Preludes*: `implicit_prelude_` member (`sema_impl.hpp:692`), setter `set_implicit_prelude` (`:690`); manifest directive `prelude <pkg>` (`src/compiler/module_manifest.cpp:56-62`). Opt-out via `#![no_implicit_prelude]` inner-attribute scan (`sema_collect.cpp:234-243`). Tied to the manifest tier system (`tier: lang|mem|std`, `module_manifest.cpp:47-55`).
- *No staged resolution* — Logos is single-pass: macros are pre-expanded by metaprog before sema runs; imports and primary resolution happen together in `collect()` (`sema_collect.cpp:100`). No "type-relative" deferred phase.

**Match verdict:** WARN — name resolution conceptually matches the primary-resolution layer but is uniformly single-stage. Preludes match (manifest-driven implicit prelude == Rust's std prelude + `no_implicit_prelude` opt-out wired). Namespaces are de-facto correct (separate maps per kind, separate token slots for lifetime/label) but the implementation has no first-class `Namespace` concept; if you want spec-traceable diagnostics you'd want one. Macro sub-namespaces (bang vs attribute, `cfg`/`cfg_attr` carve-outs) — absent (metaprog handler keys are global).

**Implementation pointer:** Resolution core `src/compiler/sema_impl.hpp:2393-2472` (`effective_import_pkgs`, `lookup_qualified_`, `find_trait_iter_scoped`). Scope builder `src/compiler/sema_collect.cpp:100-224`. Prelude injection `src/compiler/sema_collect.cpp:231-252` and `src/compiler/sema.cpp:5840-5859`. Manifest prelude `src/compiler/module_manifest.cpp:56-62`. Access check `src/compiler/sema_collect.cpp:673-678`.

**Interactions check:**
- Paths — WARN. Resolution operates on the dotted-package form, not on a general `SimplePath`/`PathInExpression` AST. Full path-in-expression with package qualifier doesn't reach `lookup_qualified_` (see Paths §1 gap).
- Use declarations — OK. Each `USE` node feeds `wildcard_packages`; `pub use` enrols into `pkg_reexports_`; transitive walk via `collect_reexports`.
- Modules — WARN. No inline `mod`, so the module hierarchy is flat (one file == one package; manifest aggregates many files into one package). Resolution doesn't have a "look in parent module first" step — instead it's strict `cur_package_ → imports → bare`.
- Macros (separate namespace) — WARN. Metaprog handlers form an effective macro namespace via the `#[metaprog_handler]` registry, but there's no formal Type vs Macro disambiguation. Bang-form vs attribute-form macros: the metaprog dispatcher routes attribute vs bang-call distinct entry points (`metaprog_dispatch.hpp`), so the sub-namespace concept exists pragmatically but without a Rust-parity sub-namespace label.
- Type vs value namespace — partial. The audit confirms separate maps per kind (`structs_/funcs_/...` in `sema_impl.hpp:3949-3962`). A struct name and a function name with the same identifier won't collide because they live in different maps. Same-named constants and structs is the riskier interaction — `funcs_`/`generic_funcs_` (where constants get lifted to) vs `structs_` is well-separated; `consts_` map not located in this audit.
- Lifetime / label namespaces — OK. Distinct lexer tokens (`LIFETIME = 40`, `LABEL = 47`, `grammars/logos.peg:60,67`), distinct AST node codes (`LIFETIME_PARAM = 131`, `LABELED_LOOP = 142`, `:190,201`).
- Hygiene (macros) — see Category J. Metaprog uses gensym (`ref_subsystem_metaprog.md`) — hygiene-by-fresh-name, not Rust's hygiene-by-syntax-context.
- Preludes — WARN. Logos has a manifest-driven *single* prelude package (`module_manifest.cpp:56-62`) — no separate Standard / Extern / Language / `macro_use` / Tool tiers. The language-level prelude (built-in primitives `bool`, `i32`, `&T`, `dyn`) lives implicitly in the type system, not in a "language prelude" injection. `#![no_implicit_prelude]` is recognised (`sema_collect.cpp:234`).
- Crate / `extern crate` — GAP. No `extern crate` directive; cross-package dependency is via manifest `depends` (`module_manifest.cpp:36`). No `--extern` flag analogue (build-driven via manifest).
- `no_std` — N/A — Logos has no std/core split; the tier system (`lang/mem/std`, `module_manifest.cpp:47`) is the closest analogue.

**Gaps / debt:**
- No first-class `Namespace` enum in sema; namespacing is implicit-by-map. A formal `enum class Namespace { Type, Value, Macro, Lifetime, Label }` with a single resolver entry point would tighten diagnostics.
- No staged resolution (expansion / primary / type-relative). Single-pass collect+lower has worked so far but precludes some Rust-spec-mandated ambiguity errors (e.g. glob-vs-outer ambiguity at use time `names/name-resolution.md` §names.resolution.expansion.imports.ambiguity).
- Macro sub-namespaces (bang vs attr; `cfg`/`cfg_attr` carve-outs) not modeled.
- Bare-key lookup tier in `lookup_qualified_` (`sema_impl.hpp:2432`) bypasses `check_pub_access` — documented as host-injected-only assumption (`modules.md:154`) but not enforced.
- Only one implicit prelude; Rust has five overlapping preludes. The language prelude (primitives) is implicit-by-type-system rather than implicit-by-prelude.
- Re-export resolution is package-wildcard transitive (`effective_import_pkgs`); item-level re-exports under a renamed alias (`pub use foo::Bar as Baz;`) not representable.
- No `extern crate` directive (manifest depends covers it, but a Rust-style `extern crate alloc;` form does not parse).

---

## Cross-category gaps

- **Path qualifier keywords (`crate`/`super`/`self`/`$crate`)** are absent from the lexer keyword table (`tools/peg_gen/grammars/logos.peg:330-410`). Adding them is grammar-tier work that touches every production using `IDENT` as the leading path segment. Cross-cuts Category C (items: `extern crate` form, inline `mod`) and Category J (`$crate` for macro hygiene).
- **UFCS at expression position** (`<T as Trait>::method(arg)`) is partial — currently only `<T as Trait>::Variant` for enum literals (`grammars/logos.peg:2517`). Cross-cuts Category E (method call / field expression) and Category D (trait methods, generic dispatch).
- **`#[non_exhaustive]`** — a visibility-adjacent attribute (Category L) that interacts with match exhaustiveness (Category E `match`) and `pub` enum/struct construction. Not located in this audit.
- **Field visibility enforcement at field-expression sites** — flagged OK by Category C; cross-check that `lookup_qualified_` shape applies to field access too (currently field access is a different code path).
- **Per-package binary archives ↔ Rust crate boundary** — `pkg_reexports_` walks across manifests, but archive vs source consumers may resolve differently. Cross-cuts the `[Auxiliary loader-hot indexes embed inside the .a]` memo (memory index).

---

## Recommended next moves

1. **Spec a `Namespace` enum + single-pass resolver entry point.** Today `find_struct_by_name` / `find_trait_by_name` / `funcs_` / `generic_funcs_` lookups all duplicate the three-tier search via the `lookup_qualified_` template + ad-hoc fn-lookup in `sema.cpp:1441-1513`. A unified `resolve(name, ns, scope)` API would let later staged-resolution work attach without rewriting call sites. *Single-session size.*
2. **Tighten the bare-key fallback tier in `lookup_qualified_`** (`sema_impl.hpp:2432`): either remove the unprotected bare-name lookup or assert `def_package.empty()` (i.e. only host-injected items). Today a non-`pub` user item placed at an unqualified key (none exist deliberately, but rebases could introduce one) would leak. *Single-session, audit-cleanup-sized.*
3. **Parse `use pkg.sub.Item as Alias;`.** Tiny grammar extension (`pub_use_decl` / `use_decl` arms gain optional `KW_AS IDENT` tail); sema records an alias map alongside `wildcard_packages`. Enables the `Item` → `Alias` rename Rust users expect. *Single-session, grammar-and-sema-only.*
4. **Parse `use pkg.{a, b, c};` (non-enum brace-list).** Currently rides on `use_variants_decl` for the enum-variant case (`grammars/logos.peg:479`); generalising to plain item lists is a small grammar arm + sema-collect fan-out. *Single-session.*
5. **`pub(crate)` first**, then `pub(super)`. `pub(crate)` is the most-requested Rust form. Since Logos already discriminates `cur_package_ == def_package`, the "crate" scope is exactly the current package — a no-op renaming + grammar arm. `pub(super)` requires inline-`mod` first, so defer. *`pub(crate)` is single-session.*
6. **Document the "package is Logos' crate" mapping** in `docs/DIVERGENCES.md` (blessed: dotted-`.` package path; binary visibility; no inline `mod`). Currently only in `docs/language/reference/modules.md`. Brings the divergence register into line with the audit verdicts. *15-minute doc edit.*
