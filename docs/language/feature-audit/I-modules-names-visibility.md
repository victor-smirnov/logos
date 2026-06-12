# Category I — Modules names visibility (audit)

v2 — re-audited 2026-06-12 (v1: 2026-05-30); spec: rust-lang/reference (local checkout `/home/victor/cxx/reference`)

3 features audited: 0 OK, 3 WARN. Logos keeps a deliberately *flat* package model (no inline `mod`, no `crate`/`super`/`self` qualifiers, binary `pub`/private lattice). Dotted-`.` package segments + `::` into items = the blessed Logos path model (`ref_logos_path_model.md`). Since v1: §6.6 bare-key pub-bypass closed (`7418de9f`); v1 *missed* the already-landed UFCS expr-position call (`2e57b1e5`, 2026-05-22); NEW gap found by probe — enum/trait visibility entirely unenforced. Cross-package consumption is archive-only (`-l file.a` / `-L dir`); multi-source CLI compile emits only the first module's code (`-I` allowed only with `--emit-module`).

---

## 1. Paths (`a::b::c`, `<T as Trait>::item`)

**Rust nomenclature:** `Path` (`paths.md`): `SimplePath`, `PathInExpression`, `QualifiedPathInExpression`, `TypePath`. Qualifiers `self`/`Self`/`super`/`crate`/`$crate`, leading `::`, turbofish.

**Logos nomenclature:**
- *Package path*: `path_dot_ident` — `DOT IDENT !(DOT LBRACE)` (`tools/peg_gen/grammars/logos.peg:504`). Used by `module` `:480`, `pub_use_decl` `:485`, `use_variants_decl` `:491`, `use_decl` `:498`. AST key `PATH_PARTS`.
- *Qualified path in type*: `qualified_assoc_type` `:1433` — `LT type_ref KW_AS simple_type GT_TYPE COLONCOLON IDENT`. Rust-shaped.
- *Qualified path in expression* (UFCS): `call_expr` arm `:3087` — `<T as Trait>::method(args)` (commit `2e57b1e5`, B146-G2; **v1 wrongly reported this absent**). Caveats: receiver limited to bare `IDENT`; the trait qualifier is parsed then *dropped* — dispatch is on the concrete type, so two same-named methods from different traits cannot be disambiguated. `<T as Trait>::Variant` enum literal `:2679`.
- Turbofish at expr position: `GENERIC_REF` `:273` (`IDENT::<TARGS>`), static-method turbofish `Class::<T>::method(...)` `:3072-3077`.
- C++ key fabric: `sema_key(pkg, name)` → `"pkg::name"` (`src/compiler/sema_impl.hpp:934`).

**Match verdict:** WARN — blessed path-model divergence (dotted `.` package segments; reduced qualifier set) plus parity gaps inside the model. Probed 2026-06-12: `std.io.f()` at expr position → sema `undefined variable 'std'` (parses as field chain, never reaches package resolution) — fully-qualified path in expr/type position is still the headline conformance item.

**Implementation pointer:** Grammar `logos.peg:504,1433,2679,3072-3089,273`. Key construction `sema_impl.hpp:934`. `Self` handling `src/compiler/sema_decl.cpp:204,225-229` (`current_type_params_["Self"]`).

**Interactions check:**
- Modules — WARN. No inline `mod`; one package per file / per `logos.module` manifest (`src/compiler/module_manifest.cpp`).
- Use declarations — WARN-divergent shape: `use pkg.sub;` (package wildcard only). Probed: `use std.fmt as f;` → syntax error; `use std.{fmt, io};` → syntax error. `use pkg.Enum.{V1, V2}` is the only brace form (`use_variants_decl` `:491`).
- `crate`/`super`/`self`/`$crate` qualifiers — GAP. Not in the keyword table (grep confirms, 2026-06-12). `Self` OK (type-param entry); `self` param OK.
- Turbofish — OK (`:273`, `:3072`).
- Associated items (`Self::Item`, `Trait::method`) — OK (`assoc_type_ref`, `qualified_assoc_type`; trait-qualified UFCS `Trait::method(recv,…)` dispatches on first arg, DIVERGENCES "recently caught up" 2026-05-22).
- Qualified paths — OK at type position; expr position works for `::method(...)` / `::CONST` / `::Variant` with the trait-dropped caveat above.

**Gaps / debt:**
- Fully-qualified `pkg.sub.Item` in expr/type position does not resolve (probe: sema error; `docs/language/reference/modules.md:127`).
- `use ... as Alias;` not parsed (probe 2026-06-12: syntax error; `modules.md:143`).
- `use pkg.{a, b};` non-enum brace-list not parsed (probe: syntax error); glob `use foo::*;` N/A (imports are implicitly package-wildcard).
- `crate`/`super`/`self`/`$crate` path qualifiers absent.
- UFCS expr form drops the trait qualifier (no trait-based disambiguation) and only takes `IDENT` receivers.

---

## 2. Visibility and privacy

**Rust nomenclature:** `Visibility` (`visibility-and-privacy.md`): `pub` / `pub(crate)` / `pub(super)` / `pub(self)` / `pub(in SimplePath)` / private. Default-public exceptions: `pub` trait assoc items, `pub` enum variants.

**Logos nomenclature:**
- Grammar: `KW_PUB` (`logos.peg:354`) prefixes every publishable production (`pub_use_decl :485`, `pub_instantiate_decl :569`, enum/trait/struct/datatype/fn/const/field arms). AST key `IS_PUB`. No `KW_PUB LPAREN` arm anywhere — `pub(crate)` family unparseable (probe: syntax error; pinned by `tests/logos/fail/core_6_adv_pub_crate.logos`).
- Sema: `is_pub` on `SemaFuncInfo`/`SemaStructInfo` (default `false`, `sema_impl.hpp:2149,2243`); collection reads `IS_PUB` (`sema_collect.cpp:3754,3868,4489`; `EXTERN_FN` forced pub `:4484`). Trait/impl methods inherit trait accessibility — forced `is_pub=true` post-collection (`sema_collect.cpp:3067-3100,3495-3497`) — matches the Rust default-public rule.
- Enforcement: `check_pub_access` (`sema_collect.cpp:698-703`, "'{name}' is private to package '{pkg}'"). Call sites: fn calls (`sema_expr.cpp:2844,3036,5437,5569,8175,12834`), field access (`:8942,8953`), type lookups via `lookup_qualified_<true>` (`sema_impl.hpp:2776-2812`).
- ~~Bare-key fallback bypass~~ ✅ closed (commit `7418de9f`, logos-core §6.6): the final `m.find(name)` tier now pub-checks any hit whose `package` is non-empty and ≠ `cur_package_` (`sema_impl.hpp:2793-2810`).

**Match verdict:** WARN — `pub`/private split Rust-aligned and enforced for fn/struct/field (probed via archive: private fn → error, private struct → error + private-field error). **NEW GAP (probe 2026-06-12): enum and trait visibility are entirely unenforced** — `lookup_qualified_<false>` for `enums_`/`traits_` (`sema_impl.hpp:2836,2839`); `SemaTraitInfo` carries no `is_pub` at all (code comment: "B-mv-02 family"). A non-`pub` enum from another package constructs and matches without diagnostic. Granular `pub(...)` sublattice uniformly absent.

**Implementation pointer:** `sema_impl.hpp:2776-2840` (lookup + PubCheck), `sema_collect.cpp:698-703` (check), `:212-221` (`pkg_reexports_`), `sema_impl.hpp:2754` (`effective_import_pkgs`).

**Interactions check:**
- Modules — visibility enforced at package boundary only; `pub(self)`/`pub(super)` inexpressible (no `super`).
- Items — OK for fn/struct/datatype/const/type-alias; **GAP for enum/trait** (above). Enum-variant per-variant visibility N/A (matches Rust).
- Struct fields — OK, probe-verified ("field 'v' is private").
- `pub use` re-export — OK. `pkg_reexports_` (`sema_collect.cpp:218`) + transitive `collect_reexports`; exercised by `tests/logos/pub_lib/` (mybridge re-export fixture).
- Crate boundary — WARN-divergent: Logos crate == package; enforcement identical for source-tree siblings and `-l` archives.

**Gaps / debt:**
- **Enum + trait pub enforcement** — add `is_pub` to `SemaTraitInfo`, flip enums to `lookup_qualified_<true>` (enum variant lookup paths too). The B-mv-02 arc.
- `pub(crate)` / `pub(super)` / `pub(in path)` family — still unparsed (`modules.md:144`).
- `pub use ... as Alias;` — not representable (see Paths).

---

## 3. Name resolution / preludes / namespaces / scopes

**Rust nomenclature:** Resolution stages (expansion/primary/type-relative — `names/name-resolution.md`); namespaces Type/Value/Macro/Lifetime/Label (`names/namespaces.md`); five preludes + `#![no_implicit_prelude]` (`names/preludes.md`); scopes + shadowing (`names/scopes.md` — *missed in v1, swept now*).

**Logos nomenclature:**
- *Resolution*: `ImportScope { wildcard_packages, variant_aliases }` (`sema_impl.hpp:923-931`), `cur_package_` `:916`, built per-file by `build_import_scope` (`sema_collect.cpp:102-224`). Three-tier lookup `lookup_qualified_` (`sema_impl.hpp:2776`): `cur_package_::name` → `effective_import_pkgs()` (pub-checked) → bare key (pub-checked since `7418de9f`). Wrappers `find_struct_by_name :2813` / `find_datatype_by_name :2832` / `find_enum_by_name :2835` / `find_trait_by_name :2838`; fn lookup walks `funcs_`/`generic_funcs_` separately.
- *Namespaces*: no `enum class Namespace`; partition is implicit per-kind maps. Probe 2026-06-12: `struct Foo` + `fn Foo()` coexist; `Foo { v: 2 }` and `Foo()` both resolve (type vs value ns de-facto correct). Lifetime/label have distinct tokens (`LIFETIME=40 logos.peg:60`, `LABEL=47 :67`) and AST nodes (`LIFETIME_PARAM=131 :190`, `LABELED_LOOP=142 :201`).
- *Scopes / shadowing* (new row vs v1): probe — `let x = 1; let x = x + 1;` and a local `let f` shadowing fn `f` both work (Rust-conformant binding shadowing). Item scopes flat per package; generic-param and label scopes handled by their own machinery.
- *Preludes*: manifest `prelude <pkg>` directive (`module_manifest.cpp:56-61`) + `implicit_prelude_` (`sema_impl.hpp:722-725`); opt-out `#![no_implicit_prelude]` scan (`sema_collect.cpp:231-252`; skipped for binary-loaded modules). Tier system `lang|mem|std` (`module_manifest.cpp:47-55`) now load-bearing post three-layer stdlib split (`1095bf41` placeholders dropped, `baa09e64` layer cycle broken).
- *Single-stage*: metaprog pre-expands before sema; imports + primary resolution in one `collect()` pass; no type-relative deferral.

**Match verdict:** WARN — primary-resolution layer matches; preludes match (single manifest-driven prelude vs Rust's five); namespaces de-facto correct but not first-class; shadowing OK; no staged resolution (precludes Rust's glob-ambiguity diagnostics — moot while glob-import of items doesn't exist).

**Implementation pointer:** `sema_impl.hpp:2754-2840`, `sema_collect.cpp:102-252,698-703`, `module_manifest.cpp:36-61`. Loader package walk `src/compiler/module_loader.cpp:1469-1500` (`visit_package`: text index → binary index → error).

**Interactions check:**
- Paths — WARN: resolution operates on bare names within the import scope, not on general path ASTs (Paths §1 gap).
- Use declarations — OK (`USE` → `wildcard_packages`; `pub use` → `pkg_reexports_`).
- Macros — WARN: metaprog handler keys are a global registry, not a macro namespace; bang vs attribute routed by distinct entry points (pragmatic sub-namespace, no formal label).
- Preludes — WARN: one prelude tier; language prelude (primitives) implicit-by-type-system. `no_implicit_prelude` wired.
- `extern crate` — GAP-shaped but covered: manifest `depends` (`module_manifest.cpp:36`) + `-l`/`-L`; the Rust directive form does not parse.
- `no_std` — N/A; `lang/mem/std` tiers are the analogue.

**Gaps / debt:**
- No first-class `Namespace` enum / unified `resolve(name, ns, scope)` entry point — duplication across `lookup_qualified_` + ad-hoc fn lookup remains.
- Single-stage resolution; macro sub-namespaces unmodeled.
- Item-level renamed re-exports (`pub use foo::Bar as Baz;`) not representable.
- Only one implicit prelude tier.

---

## Cross-category gaps

- **Path qualifier keywords (`crate`/`super`/`self`/`$crate`)** absent from the lexer keyword table — grammar-tier work; cross-cuts C (inline `mod`) and J (`$crate`).
- **Enum/trait visibility enforcement** — cross-cuts E (match on cross-pkg enum), D (trait bounds on private traits).
- **UFCS trait-qualifier drop** — cross-cuts D (multi-trait same-name method disambiguation).
- **`#[non_exhaustive]`** — visibility-adjacent (L), interacts with match exhaustiveness (E). Still not located.
- **Per-package archives ↔ crate boundary** — `.a` is now the only cross-pkg consumption path in compile mode; visibility behaves identically (probe-verified).

---

## Recommended next moves

1. **Enforce enum + trait visibility** (`sema_impl.hpp:2835-2839`): add `is_pub` to `SemaTraitInfo`, flip both lookups to `PubCheck=true`, sweep variant-lookup side paths. Fail tests mirror `pub_call_private`. *Single-session; closes the only soundness-grade hole left in this category.*
2. **Parse `use pkg.sub.Item as Alias;` + `use pkg.{a, b};`.** Grammar arms on `use_decl`/`pub_use_decl` + alias map alongside `wildcard_packages`. *Single-session.*
3. **`pub(crate)` first** (== current package, near-no-op semantics), `pub(super)` deferred on inline-`mod`. *Single-session.*
4. **Fully-qualified dotted path in expr/type position** — the known Logos-model conformance item (`ref_logos_path_model.md`); needs a grammar path production + `lookup_qualified_` entry from expr lowering. *Session-sized.*
5. **Spec a `Namespace` enum + single resolver entry point** — prerequisite for staged-resolution work and spec-traceable diagnostics. *Single-session.*
6. **UFCS: honor the trait qualifier** in `<T as Trait>::method` dispatch instead of dropping it. *Small, pairs with multi-impl selection work (`8c10eb4e`).*
