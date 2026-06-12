# Category L — Attributes (audit)

v2 — re-audited 2026-06-12 (v1: 2026-05-30); spec: rust-lang/reference (local checkout `/home/victor/cxx/reference`).

Summary: 2 features audited; 0 OK, 2 WARN (both materially improved since v1). Closed since v1: **`#[repr(transparent)]` + `#[repr(uN)]` minimal** (00a96805 — layout consumer; other modes parse-then-reject); **cfg combinators in attribute position + `cfg_attr` activation** (1da75445 — ANNOT_CALL schema, unified `evaluate_cfg_arg`); **derive handler coverage 2→10** (cf4d25e2 → 2018bc3b → 83975755 → 654816d1 — Copy/Clone/Debug/Default/Eq/PartialEq/Hash/Ord/PartialOrd + branch_node, logos-core §6.10 ✅ 8/8); **unified struct/enum attr-flag parsing** (5782dd4b — one `parse_struct_attr_flags`, zero drifting string compares); **`#[zoned]`/`#[datatype]` split** (f52a8f22); new Logos attrs `#[pinned]` (6dabfe99), `#[borrow_carrying]` (d63bbb31), `#[rel_ptr]`/`#[self_describing]`/`#[zone_mut]` (RefRepr/zone work). Still open: attribute positions are **item-only** (no fields/variants/trait-items/params/arms — probe: parse error), no tool/path attrs, no `unsafe(...)` wrapper, no lint family, codegen hints warn-as-unknown, `cfg(true/false)` literals missing, `cfg_select!` absent. **NEW v2 bug:** a `#[cfg]`-false fn is dropped from collection but its body is still lowered — the canonical Rust same-name-per-platform pair dies with "duplicate function body for symbol" at mlir-gen (probe).

The audit reads `docs/DIVERGENCES.md` §A3 (derive→metaprog) and §A5 (rustc-internal attrs) as authoritative for blessed deltas.

---

## 1. Built-in attributes

**Rust nomenclature.** "Built-in attributes" — `attributes.md:207-289`; outer/inner syntax, 4 meta-item shapes, `unsafe(...)` wrapping, tool attributes, active vs inert; index spans conditional-compilation, testing, derive, macros, diagnostics, ABI/linking, codegen, doc, preludes, modules, limits, runtime, type-system, debugger families.

**Logos nomenclature.** "Annotations". Grammar: outer `annotation` (`logos.peg:597-606`), inner `inner_annotation` (`:609`), args incl. nested combinator calls `ANNOT_CALL` code 248 (`:307, :627`). AST: `ANNOTATION` 150, `INNER_ANNOTATION` 242, `ANNOT_KV/POS/ARR` 192-194. Registry: `attr_builtin_targets` (`sema_impl.hpp:1254-1300`); per-target validation `check_annotations` (`:1544`); unified flag parser `parse_struct_attr_flags` (`:1223-1252`, StructAttrFlags — single point of truth per 5782dd4b). Recognised set:
- Layout/marker: `#[type_code]`, `#[zoned]`, `#[datatype]`, `#[self_describing]`, `#[rel_ptr]`, `#[pinned]`, `#[zone_mut]`, `#[borrow_carrying]`, `#[no_auto_drop]`, `#[annotation]`, `#[tag_dispatch]`, **`#[repr]`** (struct: `transparent`; enum: `uN/iN`; other modes parse-then-reject — probe: `#[repr(C)]` → "not yet supported (only `transparent` …)").
- Metaprog: `#[metaprog_handler]`, `#[fn_macro]`, `#[token_macro]`.
- ABI: `#[no_mangle]`. Testing: `#[test]`, `#[should_panic]`, `#[ignore]`.
- Conditional compilation: `#[cfg]`, `#[cfg_attr]` (all 6 AttrTargets).
- Inner: `#![no_implicit_prelude]` (`sema_collect.cpp:228-241`).

**Match verdict: WARN — coverage grew (repr, derive handlers, Logos marker set) but positions, lint family, and codegen hints unchanged.**

- Nomenclature drift (annotation vs attribute) — unchanged from v1; diagnostics say "attribute", grammar/AST say `annot*`.
- Grammar surface:
  - Names still single-`IDENT` (`logos.peg:597`) — no tool/path attrs (`rustfmt::skip`, `diagnostic::*`), no `unsafe(...)` wrapper (grep: 0).
  - Args now admit nested `IDENT(args)` meta-items via ANNOT_CALL (✅ closed for the cfg family; generic `MetaListNameValueStr` consumers like `#[link(name=..., kind=...)]` still have no consumer).
  - **Positions: item-only**, unchanged (`logos.peg:507`; `field_def_or_doc`/`trait_method_or_doc`/`impl_item_or_doc` `:542-545` and `variant_def` `:755` have no annotation alt). Probe: `#[cfg(unix)]` on a struct field → syntax error. Note annotations ARE items in the stream attached to the *next* item, so impl blocks/unions/statics take them (probe: `#[cfg(windows)] impl S {…}` correctly drops the impl), but fields/variants/params/arms/statements cannot.
  - Inner attrs still module-top only.
- Recognised-set deltas vs the Rust index:
  - Repr: ✅ minimal closed (00a96805) — `#[repr(transparent)]` single-field struct collapses to field layout (`mlir_gen_types.cpp:473`, `lir.hpp:885-889`, parse `sema_collect.cpp:1559-1586`); `#[repr(uN)]` sets enum discriminant width; probe runs (rc=42). `repr(C)`/`packed`/`align` parse-then-reject — FFI struct layout remains open breadth work.
  - Derive: Rust shape still rejected with redirect (`check_annotations`, `sema_impl.hpp:1556-1564`; blessed §A3). ✅ stdlib handlers now 10: `stdlib/std/compiler/metaprog/derive_{clone,copy,debug,default,eq,partial_eq,hash,ord,partial_ord,branch_node}.logos` (logos-core §6.10 ✅, 8 `core_6_10_derive_*` tests; probe `#[derive_partial_eq]` + `==` compiles).
  - Diagnostics: `#[allow]/#[warn]/#[deny]/#[forbid]/#[expect]`, `#[deprecated]`, `#[must_use]`, `#[diagnostic::*]` — still absent (probe: `#[must_use]` → unknown-attr warning). No lint-level infra.
  - Codegen: `#[inline]`, `#[cold]`, `#[naked]`, `#[no_builtins]`, `#[target_feature]`, `#[track_caller]`, `#[instruction_set]` — still unknown-attr warnings (probe; `sema_collect.cpp:610`). Not accept-and-ignore.
  - Type system: `#[non_exhaustive]` — absent.
  - ABI/linking beyond `no_mangle` (`export_name`, `link_section`, `used`, `link*`, `crate_*`, `no_main`) — absent.
  - Doc: `#[doc = "..."]` attr-shape absent; `///`/`/** */` dedicated productions (`logos.peg:502-519` area) — parallel surface, unchanged.
  - Preludes/modules/limits/runtime/debugger families — absent, unchanged (`#![no_std]` n/a, `#[path]` n/a to package-rooted module model).
- Active vs inert: `#[cfg]` active ✓; `#[cfg_attr]` now FULLY active (✅ wrapped-attr activation, `sema_collect.cpp:1373-1417` — pushed into `pending_annots` before the cfg-drop loop, so `#[cfg_attr(unix, cfg(windows))]` drops the item same-iteration); `#[metaprog_handler]` triggers active; rest inert.
- Unknown-attr handling: builtin/trigger/`#[annotation]`-datatype names pass; everything else warns post-collection (`sema_collect.cpp:599-615`).

**Implementation pointer.** Grammar `logos.peg:593-636` (+ ANNOT_CALL `:627`); registry `sema_impl.hpp:1254-1300`; flags parser `:1223-1252`; validation `:1544`; unknown-attr warn `sema_collect.cpp:610`; cfg_attr/cfg item gating `sema_collect.cpp:1373-1431`; repr parse `:1559-1586` (struct), `:1684` (enum); repr layout `mlir_gen_types.cpp:473`.

**Interactions check.**
- **Items:** WARN — top-level items + impl/union/static take attrs; fields/variants/trait-items-in-body/params/arms still reject at parse (probe). Fix remains grammar + `AttrTarget::{Field,Variant,…}`.
- **Derive ↔ traits:** WARN→nearly-OK — §A3 blessed redirect + 10 handlers. Residual: handler must be imported (`use logos.std.compiler.metaprog;`) — no implicit availability; enum-derive coverage partial (see logos-core §6.10 note on derive_debug sidestep).
- **`#[repr]` ↔ layout:** WARN (was GAP) — transparent + uN landed with layout consumers; `repr(C)`/`packed`/`align(N)` rejected loudly; FFI-struct layout still open.
- **`#[inline]` ↔ codegen:** GAP unchanged (warn-as-unknown).
- **`#[deprecated]` / `#[must_use]` / `#[non_exhaustive]` / lint family:** GAP unchanged.

**Gaps / debt.**
- Attribute positions beyond items (fields/variants/trait items in body/params/arms/stmts/exprs) — grammar + AttrTarget work; unlocks field-`#[cfg]`, variant-`#[non_exhaustive]`, serde-style metadata.
- Codegen-hint family: accept-and-ignore extension of `attr_builtin_targets` is still the cheapest import-friction fix.
- Lint-level family + lint stack; `#[must_use]`/`#[deprecated]` ride on it.
- `#[non_exhaustive]` + exhaustiveness-checker flag bit.
- Tool/path attr names; `unsafe(...)` wrapper (K-side pair).
- `repr(C)`/`packed`/`align` — breadth tier, FFI blocker.
- `#[doc]` attr-shape (port-transfer nicety).
- Naming drift annotation↔attribute (document or rename; carried from v1).

---

## 2. Conditional compilation `#[cfg]` / `cfg!()`

**Rust nomenclature.** `conditional-compilation.md`: `ConfigurationPredicate` = option | `all(...)` | `any(...)` | `not(...)` | `true` | `false`; compiler-set keys (target_*, `unix`/`windows`, `test`, `debug_assertions`, `panic`, `proc_macro`, …); surfaces `#[cfg]`, `#[cfg_attr(pred, attr, …)]`, `cfg!()`, `cfg_select!`.

**Logos nomenclature.** `#[cfg]`/`#[cfg_attr]` registered for all 6 AttrTargets (`sema_impl.hpp:1290-1296`). Shared recursive evaluator `evaluate_cfg_arg` (`sema.cpp:3427` — handles ANNOT_CALL all/any/not, ANNOT_KV, bare-NAME) feeds both the attribute path (`evaluate_cfg_annotation` `sema.cpp:3403`) and cfg_attr; `cfg!()` macro lowers via `lower_builtin_macro` (`sema_expr.cpp:17415-17421`) through the `CfgLexer` raw-text path (`parse_and_eval_cfg`). Keys: `match_cfg_key_value` (`sema.cpp:3309`) — `target_arch/os/endian/family/pointer_width` + `feature`; `match_cfg_flag` (`:3321`) — `unix`/`windows` (host-derived), `test`/`debug_assertions` (feature-set driven), unknown bare names fall back to the feature set. Item gating `sema_collect.cpp:1418-1431`.

**Match verdict: WARN — combinator asymmetry closed (✅ 1da75445); `cfg_attr` activation closed (✅ same); NEW lowering bug on same-name pairs; literals/key-coverage/`cfg_select!` still open.**

Conformance hits (probed 2026-06-12):
- `#[cfg(all(unix, target_pointer_width = "64"))]` in attribute position works (probe rc=42); nested `all(unix, not(windows))` covered by `core_6_8_cfg_combinators.logos`.
- `#[cfg_attr(P, attr…)]` activates wrapped attrs when P true, incl. wrapped `cfg(...)` joining the same iteration's drop set (`sema_collect.cpp:1373-1416`).
- `cfg!(all(unix, not(windows)))` → bool literal (probe rc=42). Note `cfg!` parses only in expression positions the grammar feeds to macros — `let c = cfg!(…)` works; `if cfg!(…)` directly is a parse error (grammar nit, macro-call-in-if-condition).
- `#[cfg]` on impl blocks drops the impl (probe).
- Item gating drops form from collection — fail test `core_6_8_cfg_combinator_drops` green.

Divergences / gaps:
- **NEW (v2): cfg-dropped fn bodies still lowered.** Two same-named fns gated `#[cfg(unix)]` / `#[cfg(windows)]` — the canonical Rust platform-switch idiom — fail at mlir-gen: "duplicate function body for symbol '…$pick__f__void'" (probe). `sema_collect` drops the item (lookup-level), but the body-lowering walk doesn't consult the cfg result. Fix: gate the lowering walk on the same drop decision (single-source it).
- `true`/`false` literal predicates — still missing; `#[cfg(true)]` treats `true` as a feature flag → false → item dropped (probe: "call to undefined function").
- Key coverage unchanged: 5 target keys + `feature` + 4 bare flags. Missing `target_env/abi/vendor/has_atomic`, `panic`, `proc_macro`, `overflow_checks`, etc. (one-line adds; unknown keys correctly silently-false, `sema.cpp:3316-3317`).
- `cfg_select!` — absent (grep 0).
- Target table host-derived (`sema.cpp:3267-3306`); `--target` cross-compile would invalidate — acknowledged comment, roadmap.
- `#[cfg]` on fields/variants — blocked by Feature-1 positions gap.

**Implementation pointer.** Evaluators `sema.cpp:3224-3480`; gating `sema_collect.cpp:1373-1431`; macro `sema_expr.cpp:17415`; registry `sema_impl.hpp:1290-1296`.

**Gaps / debt.**
- **cfg-drop must reach the lowering walk** (NEW, the only soundness-grade item here — breaks the most common Rust cfg idiom).
- `true`/`false` literal predicates (small).
- Key-coverage one-liners (`target_env`, `panic = "abort"` — the latter cosmetic per §A7d).
- `cfg_select!` (low priority).
- `if cfg!(…)` parse position (grammar nit).

---

## Cross-category gaps

- **L ↔ C (items):** attr positions item-only — fields/variants/trait-body items/params/arms (carried from v1; probe-confirmed still open).
- **L ↔ D (generics):** no attrs on generic/lifetime params.
- **L ↔ E (expressions):** no attrs on arms/closures/blocks/statements; `if cfg!(…)` parse nit.
- **L ↔ G/K:** `unsafe(...)` wrapper absent; `#[no_mangle]` recognised unsafely-unwrapped.
- **L ↔ J (macros):** §A3 derive model now has 10 stdlib handlers; lint-control attrs await a lint stack.
- **`#[repr]` ↔ B (layout):** transparent/uN closed; `repr(C)`/`packed`/`align` = FFI debt.
- **`#[non_exhaustive]` ↔ F:** paired gap unchanged.

## Recommended next moves

1. **Fix cfg-drop at the lowering walk** (NEW bug) — single-source the drop decision so a cfg-false item neither collects NOR lowers. Unblocks the same-name platform-pair idiom; add `cfg_platform_pair.logos` test.
2. **`cfg(true)`/`cfg(false)` literals** — two lines in `evaluate_cfg_arg` + `match_cfg_flag` guard.
3. **Codegen-hint accept-and-ignore** (`inline`, `cold`, `track_caller`, …) — registry extension, kills import friction (carried from v1).
4. **Attributes on fields + variants** — grammar + `AttrTarget::{Field,Variant}`; unlocks `#[cfg]`-on-field and future `#[non_exhaustive]` (carried from v1).
5. **Lint-family syntactic stub** (`allow/warn/deny/forbid/expect`) — accept-and-ignore until a lint stack exists (carried from v1).
