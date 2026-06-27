# Category J — Macros and metaprogramming (audit)

v2 — re-audited 2026-06-12 (v1: 2026-05-30); spec: rust-lang/reference (local checkout `/home/victor/cxx/reference`).

Summary: 2 features audited; 0 OK, 2 WARN (both blessed §A3 replacements — `macro_rules!` / proc-macros → metaprog handlers + `quote_*!` + fn-macros over metacall; `docs/DIVERGENCES.md` §A3 authoritative). WARN = "deliberately different, more general mechanism; check edge cases", not "missing". Since v1: §6.10 all-8 derive handlers ✅ (`654816d1`), §6.11 `unreachable!`/`todo!`/`unimplemented!` ✅ (`b0aa262d`), `offset_of!` ✅ (`43c13796`); the whole metaprog substrate migrated writ1→writ (`24a0c362`/`45f3a229`; fn_macro + `template_of` fixes `0c4caa0e`, capture-`@{}` `57314e7f`, `@`-pattern matching `55285561`, metacall Writ-result serialization `746a42e0`). v1 error corrected: `stringify!`/`env!`/`concat!`/`include_*!`/`compile_error!` were *already present* pre-v1 (`8772f045`/`9c3ff413`/`7ea8af61`, 2026-05-14/21) — they were uncatalogued, not absent. `macro-ambiguity.md` (follow-set spec) — N/A by construction: Logos has no TT matchers.

---

## 1. Macros by example (`macro_rules!`)

**Rust nomenclature.** `macros-by-example.md`: matcher/transcriber rules, fragment specifiers (`:expr`, `:ident`, `:tt`, …), repetitions `$(...)*`/`+`/`?` with separator, follow-set rules, mixed-site hygiene, `#[macro_use]`/`#[macro_export]`/`$crate`.

**Logos nomenclature.** No `macro_rules!`. Replacement: `#[fn_macro]` / `#[token_macro]` fns invoked `name!(...)`/`name![...]`/`name!{...}`; bodies use `quote_expr!`/`quote_item!`/`quote_ty!` + `#( ... )sep*` repetition over `Vec<ExprBlob>`. Surface:
- grammar `FN_MACRO_CALL`/`FN_MACRO_CALL_ITEM`/`FN_MACRO_CALL_ITEM_DONE` (`tools/peg_gen/grammars/logos.peg:284-286,552`); `QUOTE_ITEM`/`QUOTE_EXPR`/`QUOTE_TY`/`REPEAT_GROUP`/`ANTIQUOT_TYPE`/`ANTIQUOT_PACK` (`:259-265`); `TEMPLATE_DECL` (`:258,582`).
- sema: `lower_fn_macro_call` `src/compiler/sema_expr.cpp:17710`, item splice `lower_fn_macro_call_item` `:18395`; `lower_quote_item` `:14741`, `lower_quote_expr` `:15638`, `lower_quote_ty` `:15424`. Callee flags `SemaFuncInfo::is_fn_macro`/`is_token_macro` (`src/compiler/sema_impl.hpp:2246`).
- quote blobs ride the **writ** AST API since the writ cut-over (`0c4caa0e` fixed the splice-time `WritStatic` materialization + DAG-safe `collect_param_slots`); user doc `docs/language/reference/macros.md`.

**Match verdict: WARN — blessed §A3 divergence (rename + model shift).** Strictly more general (Turing-complete codegen vs declarative TT matching). Unchanged since v1 in classification; substrate fully re-platformed onto writ with no surface change.

**Interactions check:**
- **Fragment specifiers:** WARN. Antiquot kinds are host-typed (`Ident`, `ExprBlob`, `QuoteItemBlob`, `Vec<ExprBlob>`, `Type`) — no `:tt`/`:pat`/`:meta`/`:vis`/`:lifetime` vocabulary. Arbitrary-token-tree input only via `#[token_macro]` raw `str` (RAW_TEXT, `logos.peg:78`).
- **Hygiene:** WARN. Call-site hygiene + `gensym` (host shim `src/compiler/main.cpp:1266-1272`); Rust mixed-site hygiene unmodeled. "Hybrid hygiene" still roadmap (`docs/internals/component-based-metaprog.md:208`). Doc `macros.md:177-185` unchanged.
- **Declaration scope:** WARN-by-design. `#[fn_macro]` fns obey ordinary package visibility + `use`; no `#[macro_use]`/`#[macro_export]`/`$crate` (grep 2026-06-12: zero matches) — manifest `prelude` (`module_manifest.cpp:56-61`) covers the macros-everywhere case at package granularity.
- **Repetition:** OK with two narrow gaps. `#( ... )sep*` separators = none/`,`/`&&` (OP 0/1/2, multiple grammar sites `:1159,2464-2468,2665-2669`) — narrower than Rust's any-token. Nested `#( #( … )* )*` rejected at both lowering sites (`sema_expr.cpp:14822` quote_item, `:15860` quote_expr) — unchanged since v1.
- **Builtin macro catalog** (sema-resident, `lower_builtin_macro` + splits): `cfg!` `:17421`, `vec!` `:17437` (user-overridable), `line!`/`column!`/`file!`/`module_path!` `:17518-17531` (**`column!()` still constant 0** — AST tracks no columns), `include!`/`include_str!`/`include_bytes!` `:17549,17621`-adjacent, `env!`/`option_env!` `:17586`, `concat!`/`concat_bytes!` `:17621`, `stringify!` `:17633`, `compile_error!`, `unreachable!`/`todo!`/`unimplemented!` `:17651-17655` (✅ closed `b0aa262d`, §6.11 — wrap `panic!`, type as `!`), `offset_of!` `:16910` (✅ closed `43c13796`, Rust-faithful ABI-layout walk), format family `format!`/`print!`/`println!`/`eprint!`/`eprintln!`/`panic!` `:17927-17929` + `write!`/`writeln!` `:17937` (`a2f41877`, streams to dyn-Write Formatter). **Still missing as macros: `assert!`/`assert_eq!`/`assert_ne!`/`debug_assert*!`, `matches!`, `dbg!`** — only `fn assert(cond, msg)` (`stdlib/lang/panic/panic.logos:41`) and `TestCtx::assert_*` methods (`stdlib/std/testing/testing.logos:81-110`) exist.

**Gaps / debt.**
- No `:tt`-equivalent structured handoff (workaround `#[token_macro]` raw bytes).
- Nested repetition rejected (`sema_expr.cpp:14822,15860`).
- Separator set `,`/`&&`/none.
- No mixed-site hygiene (roadmap `component-based-metaprog.md:208`).
- `assert!` family / `matches!` / `dbg!` builtins absent — highest-frequency remaining Rust-prelude macros for imports.
- `docs/language/reference/macros.md` still lacks the `macro_rules!`-migration section (v1 move #3 — not done).

---

## 2. Procedural macros

**Rust nomenclature.** `procedural-macros.md`: `proc-macro` crate type; function-like / derive / attribute flavors over `TokenStream`; `Span` diagnostics.

**Logos nomenclature.** No proc-macro crate kind, no `TokenStream`. Mapping:
- `#[proc_macro]` → `#[fn_macro]` returning `ExprBlob` (expr) / `ItemList`/`QuoteItemBlob` (item).
- `#[proc_macro_derive(X)]` → `#[derive_<x>]` trigger + `#[metaprog_handler("derive_<x>")]` hook fn.
- `#[proc_macro_attribute]` → `#[<attr>]` trigger + `#[metaprog_handler("<attr>")]`.
- Registry `metaprog_handlers_` (`sema_impl.hpp:1725`), collected `sema_collect.cpp:1781-1812`, dispatched `:512-567`; Rust `#[derive(X, …)]` shape rejected with redirect message (`sema_impl.hpp:1552-1561` — B-at-06, unchanged design).
- Splice shims: `logos_emit_item_blob_subst` (`src/compiler/main.cpp:327`), `logos_macro_arg` (`:119`); emitters `stdlib/std/compiler/metaprog/emitter.logos`, `itemlist.logos`. Handlers walk the target item via `meta_*` reflection over the writ doc (read layer re-ported `996ca163`).

**Match verdict: WARN — blessed §A3 divergence (full model swap).** ✅ The v1 headline backlog is CLOSED: **all 8 derive trait families landed** (`654816d1` Wave 8 final; partials `cf4d25e2` Copy, `2018bc3b` PartialEq/Eq/Hash) — stdlib now ships `derive_{clone,copy,debug,default,eq,hash,ord,partial_eq,partial_ord,branch_node}.logos`; tests `tests/logos/pass/core_6_10_derive_*.logos` (16 files). Probe 2026-06-12: `core_6_10_derive_debug` compiles, links, runs, exit 0. Quality caveats inside the closed item (logos-core §6.10 notes): `derive_debug` emits tuple-shape `(5, 7, )` not field-named; `derive_default` = `MaybeUninit::zeroed()` (correct for POD fields only, diverges for Vec/Box non-zero defaults); `derive_partial_ord` = marker impl (Ord→PartialOrd fallback does the work). All three blocked on the same `quote_expr!` antiquot limit (no Ident cursor at type/string-literal position).

**Interactions check:**
- **Input:** WARN. Three shapes — `Vec<ExprBlob>`/`ExprBlob` (parsed AST), `str` (token_macro raw), `target_offset: u32` (handler AST handle). No `TokenStream`; `syn`-style consumers re-target the Logos AST.
- **Items (output):** OK. `ItemList`/`QuoteItemBlob` splice via `logos_emit_item_blob_subst` thunks; `template`/`template_of` declaration-as-data (`logos.peg:582`; `template_of` lowering fixed to the writ offset model in `0c4caa0e`).
- **Attributes:** WARN. One `#[derive_<trait>]` per derive (mechanical rewrite of Rust `#[derive(A, B)]` lists); no helper-attribute (`attributes(...)`) concept.
- **Crate isolation:** WARN-by-design. Handlers JIT in the same compilation (metacall driver); no proc-macro sandbox. Shared bind sites with metacall (`main.cpp` host-shim table).
- **Diagnostics/spans:** WARN. Handler diagnostics anchor at the trigger annotation; no `Span` API; `column!()` = 0. Unchanged.

**Gaps / debt.**
- Derive *quality* parity: field-named `Debug`, per-field `Default::default()`, real `PartialOrd` — all gated on extending `quote_expr!` antiquots (Ident cursor at type/str-literal position). One mechanism unblocks all three (`feedback_derive_from_foundation`).
- No `quote_stmt!`/`quote_pat!`/`quote_ident!` (roadmap `component-based-metaprog.md:205`).
- No `Span`/column tracking; `column!()` stable-0 undocumented in user docs.
- Handler emission still `unsafe { logos_emit_item_blob_subst(&blob) }` (`derive_clone.logos:38,50,69`) — wrappable.
- `macros.md` has no derives section — readers don't find `#[derive_<trait>]` from the reference (v1 move, still open).

---

## Cross-category gaps

- **L (Attributes):** marker positions enforced via `AttrTarget::Fn` bits (`sema_impl.hpp:1274-1277` — `metaprog_handler`, `token_macro`); linkage macros.md ↔ attributes.md still unaudited.
- **I (Modules/visibility):** no `#[macro_use]`/`#[macro_export]`/`$crate` — replaced by fn scoping + manifest prelude. Covered in I §3.
- **K (Unsafe):** the splice-shim `unsafe` (above).
- **M (Const eval):** metacall shares the JIT driver with all macro flavors; serialization now writ-native (`746a42e0`).

## Recommended next moves

1. **`assert!`/`assert_eq!`/`assert_ne!` (+`debug_assert*!`), `matches!`, `dbg!` builtins** — same C++ layer as `unreachable!` (`sema_expr.cpp:17651` pattern); highest remaining import-ergonomics win. *Single-session.*
2. **`quote_expr!` antiquot Ident-at-type/str-position** — the single mechanism gating Debug/Default/PartialOrd derive parity (three Wave-9 prototypes already hit it). *Session-sized, foundational.*
3. **Nested `#( #( … )* )*` repetition** (`sema_expr.cpp:14822,15860`): depth-first placeholder walk with a cursor stack. *Single-session.*
4. **Docs: derives + `macro_rules!` migration section in `macros.md`** (matcher→args, transcriber→`quote_expr!`, `:tt`→token_macro, `$crate`→use, `#[macro_export]`→`pub`; plus the builtin-macro table with Rust equivalents). *~60 lines.*
5. **Decide `column!()`** — wire columns through token records or document stable-0. *Small.*
