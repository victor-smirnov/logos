# Bug catalog: Modules & Visibility

**Group**: 1 — Module & Visibility
**Grammar rules covered**: `module`, `path_dot_ident`, `any_use_decl`, `pub_use_decl`, `use_decl`
**Reference doc**: [docs/language/reference/modules.md](../language/reference/modules.md)
**Implementation entry points**:
- [src/compiler/sema_collect.cpp](../../src/compiler/sema_collect.cpp) (`cur_imports_`, `cur_package_`, `pkg_reexports_`, `check_pub_access`)
- [src/compiler/sema_impl.hpp](../../src/compiler/sema_impl.hpp) (`find_*_by_name`, `effective_import_pkgs`, `sema_key`)
- [src/compiler/module_manifest.cpp](../../src/compiler/module_manifest.cpp), [src/compiler/module_loader.cpp](../../src/compiler/module_loader.cpp)

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/module-visibility/`

## Bugs

### B-mv-01: Cross-pkg same-name fn → "duplicate function" instead of first-import-wins

**Severity**: P2 design (inconsistency)
**Status**: fixed — pkg-aware diagnostic ("function defined in both packages X and Y with the same signature; rename one") replaces bare "duplicate function". This satisfies the catalog's "OR explicit ambiguous reference diagnostic" branch. Coexistence + qualified-call syntax (`pkg::shared()`) is a separate future language feature, not a bug.
**Repro**: `B01/` — pkg_a defines `pub fn shared() -> i32`, pkg_b defines `pub fn shared() -> i32`. `main` does `use pkg_a; use pkg_b; let v = shared();`.
**Observed**: `error [fn shared]: duplicate function 'shared'` and compilation aborts.
**Expected**: For consistency with structs (first-import-wins, see B-mv-08 / `feat_type_uid_pkg_skip_bug`), should resolve to pkg_a's `shared()` silently. OR produce an explicit "ambiguous reference" diagnostic — but NOT a "duplicate" error attached to the definition site. The function definitions are NOT duplicates; they're in different packages.
**Suspected root**: function name registration in [src/compiler/sema_collect.cpp](../../src/compiler/sema_collect.cpp) keys by bare `fn name` rather than `pkg::name` for the duplicate-detection check.
**Tags**: `tech-debt:bare-name-lookup`, `design:incomplete`

### B-mv-02: Cross-pkg same-name trait → wrong-trait resolution + confusing diagnostic

**Severity**: P2 design + P1 diagnostic
**Status**: fixed — pkg-aware diagnostic ("trait defined in both packages X and Y; rename one") replaces bare "duplicate trait"; SemaTraitInfo carries `package`. This satisfies the catalog's "explicit ambiguous trait diagnostic with both pkg paths shown" branch and what strategy.md asked for. Coexistence + qualified `pkg::Trait` syntax is a separate future language feature, not a bug.
**Repro**: `B02/` — pkg_a defines `pub trait Greet { fn hello(...); }`, pkg_b defines `pub trait Greet { fn bye(...); }`. `main` does `use pkg_a; use pkg_b; impl Greet for Foo { fn hello(...) {...} }`.
**Observed**: `error: impl Greet for Foo: missing method 'bye'` — i.e. `find_trait_by_name` returns pkg_b's `Greet` (despite pkg_a being imported first), and the impl-validation expects pkg_b's method set.
**Expected**: First-import-wins (pkg_a's `Greet`) OR explicit "ambiguous trait" diagnostic with both pkg paths shown.
**Suspected root**: `find_trait_by_name` iteration order differs from `find_struct_by_name`, OR the impl-resolution path uses a different lookup that picks the last-registered.
**Tags**: `tech-debt:bare-name-lookup`, `design:incomplete`, `tech-debt:diagnostic-no-pkg`

### B-mv-03: `use <missing-pkg>;` produces diagnostic but compilation continues

**Severity**: P1 diagnostic
**Status**: fixed (load_modules now returns `out_had_error`; main treats missing pkg as fatal; lock-in tests `use_missing_pkg` + `pub_use_missing_pkg`. Two source-only tests `hermes_type_lit_parse`/`hermes_lit_parse` were silently relying on the lenient loader and got `-I ${STDLIB_BIN_DIR}` added to LOCAL_HERMES_USERS to keep them passing.)
**Repro**: `B07/` — `package main; use does_not_exist; fn main() -> i32 { return 0; }`
**Observed (was)**: `module_loader: cannot find package 'does_not_exist'` printed to stderr, but `logosc: wrote /tmp/B07.o` follows. Exit code is **0** despite the error.
**Expected**: Hard error, non-zero exit, no `.o` written.
**Suspected root**: [src/compiler/module_loader.cpp](../../src/compiler/module_loader.cpp) emits the "cannot find package" message as a warning rather than promoting it to a sema error.
**Tags**: `oversight:simple`, `tech-debt:diagnostic-not-fatal`

### B-mv-04: `pub use <missing-pkg>;` same as B-mv-03

**Severity**: P1 diagnostic
**Status**: fixed (same patch as B-mv-03; same lock-in test set)
**Repro**: `B17/` — `pub use does_not_exist;` at item position.
**Observed**: Same as B-mv-03 — message + clean exit + `.o` written.
**Expected**: Hard error.
**Suspected root**: same code path as B-mv-03.
**Tags**: `oversight:simple`, `tech-debt:diagnostic-not-fatal`

### B-mv-05: Keyword as package name → ASSERTION CRASH

**Severity**: P0 hard (compiler crash)
**Status**: fixed-in-M0.2 (parser error recovery; clean "parse error in module" + exit 1)
**Repro**: `B11/` — `package fn; fn main() -> i32 { return 0; }`
**Observed**: `[LOGOS ASSERTION FAILURE] Requirement: LOGOS-PARSE-001`. Compiler aborts. No clean syntax error.
**Expected**: Clean syntax error: "package name cannot be a reserved keyword" or just "expected IDENT, got KW_FN".
**Suspected root**: Parser path for `module` rule expects `IDENT` but receives a keyword token; the `LOGOS_ASSERT(LOGOS-PARSE-001)` fires instead of recovering with a diagnostic.
**Tags**: `tech-debt:assertion-as-diagnostic`, `oversight:simple`

### B-mv-06: Missing `package` decl → ASSERTION CRASH

**Severity**: P0 hard (compiler crash)
**Status**: fixed-in-M0.2 (parser error recovery; clean "parse error in module" + exit 1)
**Repro**: `B13/` — file with just `fn main() -> i32 { return 0; }` (no `package` line).
**Observed**: `[LOGOS ASSERTION FAILURE] Requirement: LOGOS-PARSE-001`.
**Expected**: Clean syntax error: "expected `package` declaration".
**Suspected root**: Same parser path as B-mv-05 — `module` rule asserts on missing `KW_PACKAGE` rather than producing an error node.
**Tags**: `tech-debt:assertion-as-diagnostic`, `oversight:simple`

### B-mv-07: Trailing dot in package path → ASSERTION CRASH

**Severity**: P0 hard (compiler crash)
**Status**: fixed-in-M0.2 (parser error recovery; clean "parse error in module" + exit 1)
**Repro**: `B15/` — `package main.; fn main() -> i32 { return 0; }`
**Observed**: `[LOGOS ASSERTION FAILURE] Requirement: LOGOS-PARSE-001`.
**Expected**: Clean syntax error: "expected IDENT after '.'".
**Suspected root**: `path_dot_ident` rule asserts on missing trailing IDENT after `DOT`. Probably the same assertion category as B-mv-05/06 — parser uses LOGOS_ASSERT instead of error recovery.
**Tags**: `tech-debt:assertion-as-diagnostic`, `oversight:simple`

### B-mv-08: Empty package path → ASSERTION CRASH

**Severity**: P0 hard (compiler crash)
**Status**: fixed-in-M0.2 (parser error recovery; clean "parse error in module" + exit 1)
**Repro**: `B20/` — `package ; fn main() -> i32 { return 0; }`
**Observed**: `[LOGOS ASSERTION FAILURE] Requirement: LOGOS-PARSE-001`.
**Expected**: Clean syntax error: "expected package name after `package`".
**Suspected root**: Same family as B-mv-05/06/07 — parser asserts when expected IDENT is absent.
**Tags**: `tech-debt:assertion-as-diagnostic`, `oversight:simple`

### B-mv-09: Ambiguous type annotation across packages → confusing diagnostic

**Severity**: P1 diagnostic
**Status**: improved-in-Sprint6.3 (type_str_pair auto-qualifies same-bare-name types in let / return diagnostics; other mismatch sites still bare — bundle remaining call sites in a follow-up)
**Repro**: `B18/` — pkg_a and pkg_b both define `pub struct Pt`. `main` imports both, then `let pb: Pt = make_b();`. The annotation `Pt` resolves to pkg_a's (first-import-wins), but `make_b()` returns pkg_b::Pt.
**Observed**: `error [fn main]: let 'pb': type mismatch — expected Pt, got Pt`. Both sides display as bare `Pt` — useless for the user trying to debug.
**Expected**: Diagnostic should show the package qualification: `expected pkg_a.Pt, got pkg_b.Pt` (or similar).
**Suspected root**: [src/compiler/sema.cpp](../../src/compiler/sema.cpp) `type_str(t)` returns bare struct name without pkg. Diagnostic-formatting paths should use a qualified form when both sides have the same bare name. Same systemic issue as B-mv-02 (diagnostic-no-pkg).
**Tags**: `tech-debt:diagnostic-no-pkg`, `design:incomplete`

### B-mv-10: Repeated `use <pkg>;` silently accepted (no warning)

**Severity**: P2 design
**Status**: fixed (warn in build_import_scope on duplicate dotted-path within a module)
**Repro**: `B09/` — `use lib; use lib; use lib;` (three times) followed by usage.
**Observed**: Compiles cleanly without warnings.
**Expected**: At minimum a warning: "duplicate `use lib;`". This is dead code that hints at copy-paste error or unclear import discipline.
**Suspected root**: `cur_imports_.wildcard_packages` is a `std::vector` that doesn't dedupe on insertion (only on read via `effective_import_pkgs()`'s visited-set).
**Tags**: `oversight:simple`, `design:incomplete`

### B-mv-11: Self-import (`use my_own_package;`) silently accepted

**Severity**: P2 design
**Status**: fixed (warn in build_import_scope when dotted path matches cur_package_)
**Repro**: `B14/` — `package main; use main; fn main() -> i32 { return 0; }`
**Observed**: Compiles cleanly. Probably harmless because `cur_package_` is always tried first in `find_*_by_name`, but the redundancy is confusing.
**Expected**: Warning: "self-import of own package has no effect".
**Suspected root**: No check in `use_decl` lowering against `cur_package_`.
**Tags**: `oversight:simple`

## Tag summary

| Tag | Open | Fixed | Total | Bugs |
|---|---|---|---|---|
| `oversight:simple` | 0 | 8 | 8 | B-mv-03, B-mv-04, B-mv-05, B-mv-06, B-mv-07, B-mv-08, B-mv-10, B-mv-11 |
| `design:incomplete` | 0 | 4 | 4 | B-mv-01, B-mv-02, B-mv-09, B-mv-10 |
| `tech-debt:assertion-as-diagnostic` | 0 | 4 | 4 | B-mv-05, B-mv-06, B-mv-07, B-mv-08 |
| `tech-debt:bare-name-lookup` | 0 | 2 | 2 | B-mv-01, B-mv-02 |
| `tech-debt:diagnostic-no-pkg` | 0 | 2 | 2 | B-mv-02, B-mv-09 |
| `tech-debt:diagnostic-not-fatal` | 0 | 2 | 2 | B-mv-03, B-mv-04 |

**Cluster preview**:
- **Parser-asserts-on-missing-token** is a tight cluster (B-mv-05/06/07/08) — root is one or two `LOGOS_ASSERT(LOGOS-PARSE-001)` sites that should become error productions with recovery. Single architectural fix removes 4 P0 crashes.
- **Diagnostic-not-fatal** for module loader (B-mv-03/04) — a one-line fix to promote module-loader warnings to errors.
- **Diagnostic-no-pkg** (B-mv-02, B-mv-09) — `type_str` should qualify when ambiguous. Cuts across all 14 feature groups; will surface in many catalogs.
- **First-match resolution inconsistency across kinds** (B-mv-01, B-mv-02) — fn registration uses different duplicate-detection from struct/enum/trait; needs unification.

## Regression-confirmed (NOT bugs — sanity baseline)

These verify recent fixes still hold; no entries needed but worth recording:

- **B16**: cross-pkg same-name struct with distinct layouts works end-to-end (exit 0, fields read correctly). Validates `feat_type_uid_pkg_skip_bug` fix.
- **B04**: `pub use` re-export propagation works (consumer can call deep_fn through re-export chain).
- **B05**: re-export cycle handled (visited-set guards `effective_import_pkgs`).
- **B06**: local var `let foo: i32 = 7;` correctly shadows imported fn `foo()`.
- **B08**: private fn rejected with clear "private to package" message.
- **B10**: `pub use` does not lift private-to-source items (privacy preserved through re-export).
- **B12**: `use` after `fn` items rejected by parser (good).
- **B21**: `Use` (capitalized) rejected (good — case-sensitive keywords).

## Notes for Phase 3

The `tech-debt:assertion-as-diagnostic` cluster is likely much wider than this group. Phase 2 sweeps of other groups will probably find more `LOGOS_ASSERT(LOGOS-PARSE-001)`-class failures wherever the parser expects a token and doesn't get it. Worth grep'ing all of `src/compiler/` for `LOGOS_ASSERT.*PARSE` to size the cluster before Phase 4 strategy.

The `diagnostic-no-pkg` issue is a sub-symptom of the broader pkg-threading work that the recent UID fix only partially addressed — `type_str()` is not pkg-aware, so any diagnostic that prints types via `type_str()` loses the disambiguation. Fix is in [src/compiler/sema.cpp:933](../../src/compiler/sema.cpp).

## Future feature (not a bug): qualified cross-pkg coexistence

B-mv-01 and B-mv-02 are closed at the bug-catalog bar (collisions produce a clear, pkg-aware diagnostic instructing rename). The original "first-import-wins" branch of the Expected line is **not a bug fix** — it requires:

- Trait registry keyed by `(pkg, name)`; ~32 direct `traits_.find/count` sites migrate through a `find_trait_by_name(name, pkg_hint)` helper.
- Function symbol mangling pkg-qualified end-to-end: single source of truth in `function_symbol_name`; mlir_gen / mono / metacall / generic_ref switch from independently-built bare names to that helper.
- Surface syntax for disambiguation (`pkg::Trait`, `pkg::fn()` or similar) so users can pick when both are visible.
- Lookup rule for bare name in scope of multiple imports: ambiguity-error vs first-import-wins decision.

Tracked as language feature, not a baghunt deferral.
