# Category J — Macros and metaprogramming (audit)

Generated: 2026-05-30; spec: rust-lang/reference (local checkout `/home/victor/cxx/reference`).

Summary: 2 features audited; 0 OK, 2 WARN (both are blessed §A3 replacements — Rust `macro_rules!` / proc-macros are replaced by the Logos metacall + `#[fn_macro]` / `#[token_macro]` / `#[metaprog_handler]` layer; not gaps but naming and edge-case parity items).

The audit reads §A3 of `docs/DIVERGENCES.md:41` as authoritative: the Rust macro layer is *replaced*, not absent. So "WARN" here means "Logos has a deliberately differently-named mechanism that covers the capability and is more general; check edge cases", not "missing".

---

## 1. Macros by example (`macro_rules!`)

**Rust nomenclature.** `macro_rules!` — declarative "macros by example" defined in `reference/src/macros-by-example.md`. Matcher / transcriber rules, fragment specifiers (`block`, `expr`, `expr_2021`, `ident`, `item`, `lifetime`, `literal`, `meta`, `pat`, `pat_param`, `path`, `stmt`, `tt`, `ty`, `vis`), repetitions (`$(...)*` / `+` / `?` with optional separator), follow-set ambiguity rules, mixed-site hygiene, textual + path-based scope, `#[macro_use]` / `#[macro_export]` / `$crate`.

**Logos nomenclature.** No `macro_rules!`. The replacement is `#[fn_macro]` (and `#[token_macro]`) — ordinary Logos `fn`s with a marker attribute, invoked with `name!(...)` / `name![...]` / `name!{...}`. Pattern-substitution happens inside the body via `quote_expr! { ... }` / `quote_item! { ... }` / `quote_ty! { ... }` and `#( ... )*` repetition over `Vec<ExprBlob>`. See:
- grammar productions `FN_MACRO_CALL` / `FN_MACRO_CALL_ITEM` at `tools/peg_gen/grammars/logos.peg:284-286,539,2477-2481`
- grammar `QUOTE_ITEM` / `QUOTE_EXPR` / `QUOTE_TY` / `REPEAT_GROUP` / `ANTIQUOT_TYPE` / `ANTIQUOT_PACK` at `tools/peg_gen/grammars/logos.peg:259-265`
- sema entry `SemaChecker::lower_fn_macro_call` at `src/compiler/sema_expr.cpp:16326`
- the kind attached to a callee fn: `SemaFuncInfo::is_fn_macro` / `is_token_macro` at `src/compiler/sema_impl.hpp:1934-1935`, recorded by `sema_collect.cpp:3867-3870`.
- user-facing doc: `docs/language/reference/macros.md` (the "three macro kinds" table).

**Match verdict: WARN — blessed §A3 divergence (rename + model shift).** Logos chose to replace declarative pattern-substitution with "ordinary fn that returns an AST blob, plus a quote-template surface inside the body". Documented as A3 (`docs/DIVERGENCES.md:41`). The mechanism is strictly more general (Turing-complete codegen vs declarative TT pattern matching). Recommended nomenclature alignment: keep `#[fn_macro]` as the canonical name (it's already documented at `docs/language/reference/macros.md`), but explicitly state in the audit'd spec docs that this is the Logos answer to `macro_rules!`. Already done in the divergence row but not in the per-feature reference pages.

**Implementation pointer.** `src/compiler/sema_expr.cpp:16326-16470` (dispatch), `:13500` (`lower_quote_item`), `:14471-14572` (antiquot binding kinds incl. `Vec<ExprBlob>` cursor — slice 1.6), `:14838` (quote_expr! lowering — emits `ExprBlob` via static rodata + placeholder substitution), `:17008-17260` (item-position fn_macro splice through `logos_emit_item_blob_subst`). JIT thunk + host shim: `src/compiler/main.cpp:1079-1112` (`logos_macro_arg`).

**Interactions check** (vs the feature-interactions edges at `docs/language/feature-interactions.md:411-414`):

- **Tokens (fragment specifiers `$x:ident`, `$x:expr`, …):** WARN. Logos antiquot kinds are typed via the host language type system — `Ident`, `ExprBlob`, `QuoteItemBlob`, `Vec<ExprBlob>`, `Type` — not via a `:ident` / `:expr` / `:tt` fragment vocabulary. There is no `:tt`, no `:pat_param`, no `:meta`, no `:vis`, no `:lifetime`. This is the A3 row's "`:tt` `any token tree' fragment → Logos requires an explicit fragment choice" note. Concrete gap: there is no way to write a Logos macro that accepts an arbitrary token tree and re-emits it verbatim — every antiquot must declare a host type. The `#[token_macro]` callee receives raw `str` bytes, which is the workaround, but it has no structured parser handoff.
- **Hygiene:** WARN — partially divergent. Logos has *call-site* hygiene for free references (like `macro_rules!` non-`$crate` references) and uses `gensym` (`std.compiler.metaprog.ast` + host shim `logos_metaprog_gensym` at `src/compiler/main.cpp:1273`) for opaque-name freshness. The `docs/internals/component-based-metaprog.md:193, 208` calls "Hybrid hygiene (full split of literal-internal vs antiquoted name scopes)" still on the roadmap. Rust's mixed-site hygiene (labels + locals resolve at definition site, items at use site) is NOT modelled — Logos has only call-site hygiene + `gensym`. Note in `docs/language/reference/macros.md:177-185` candidly says "call-site hygiene, like Rust's `macro_rules!` non-hygienic references" — which is actually a UNDER-statement of Rust's hygiene, since Rust does separate label/local hygiene from item hygiene.
- **Modules (declaration scope):** WARN. `#[fn_macro]` callees are ordinary `fn`s — they obey ordinary module visibility and `use` semantics. Rust's textual-vs-path-based macro scope, `#[macro_use]` on modules, and `#[macro_use] extern crate` macro-prelude all have no Logos analogue (grepped `src/compiler/` + grammar — `macro_use`, `macro_export`, `$crate` returned zero matches). This is consistent with the A3 replacement model (macros are fns → fn scoping applies), but it means Rust code that uses `#[macro_use]` / `#[macro_export]` to control macro visibility doesn't port directly. The Logos prelude mechanism (`module_manifest.cpp:56-61`, implicit `use <pkg>;` via manifest) covers the "make macros available everywhere" case at a different granularity (package, not item).
- **`use` (macro-use):** OK as ordinary `use`. WARN as `#[macro_use]`. Logos `use` brings the macro fn into scope just like any other fn (`use std.fmt;` + `println!(...)` is the working pattern). No `#[macro_use]`-on-mod equivalent.
- **Tokens (output):** OK at the structural level — output goes through `ExprBlob` / `QuoteItemBlob` (typed AST blobs), so output is *more* structured than Rust's `TokenStream`. The token-stream-equivalent escape hatch is "encode in the blob via `quote_*!`".
- **Repetition (`$(...)*`):** OK — Logos `#( ... )sep*` in `quote_expr!` / `quote_item!` bodies (grammar `REPEAT_GROUP` at `logos.peg:261`, sema in `sema_expr.cpp:13578` and the `Vec<ExprBlob>` cursor binding at `:14544-14552`). Separators supported per `OP` code (0=none, 1=comma, 2=`&&`). Nested repetition is rejected at BOTH lowering sites (`sema_expr.cpp:13578` for `quote_item!`, `:14615` for `quote_expr!`, both error `"nested \`#(...)\` repetition not supported"`) — Rust permits nested repetitions, so this is a genuine narrow gap inside an otherwise-blessed divergence. Sep set is small (`,` / `&&` / none) — Rust allows any non-delim non-rep token.

**Gaps / debt.**
- No `:tt` / arbitrary-token-tree fragment — every antiquot has a host type. Workaround: `#[token_macro]` (raw bytes).
- Nested `#( #( … )* )*` repetition rejected at both `sema_expr.cpp:13578` (quote_item) and `:14615` (quote_expr).
- Repetition separator vocabulary is `,` / `&&` / none — narrower than Rust's "any non-delim non-rep-op token".
- No mixed-site hygiene (labels/locals def-site vs items use-site); only call-site + `gensym`. Listed in `component-based-metaprog.md:208` as roadmap.
- No `#[macro_use]` / `#[macro_export]` / `$crate` — replaced by ordinary fn scoping + package prelude in `module_manifest.cpp:56-61`. Document this mapping explicitly in `docs/language/reference/macros.md` (the current page focuses on the new surface, not the migration path from `macro_rules!`).
- The `vec!` builtin (`sema_expr.cpp:16097`), the `cfg!` builtin (`:16081`), and the `format!`/`println!`/`print!`/`eprint!`/`eprintln!` family (`stdlib/std/fmt/fmt.logos:136-292`, sema-resident lowering at `sema_expr.cpp:11900-12069`) are documented in `docs/language/reference/macros.md:118-176` but the audit didn't find a per-feature compat-table cross-referencing what these subsume from Rust's `std::macros` prelude (`assert_eq!`, `dbg!`, `matches!`, `todo!`, `unimplemented!`, `unreachable!`, `include_str!`, `include_bytes!`, `concat!`, `stringify!`, `env!`, `option_env!`, `file!`/`line!`/`module_path!`/`column!`). `file!`/`line!`/`module_path!`/`column!` are present per `sema_expr.cpp:16172-16178`; the others are uncatalogued.

---

## 2. Procedural macros

**Rust nomenclature.** `proc-macro` crate type. Three flavors: function-like (`#[proc_macro]`), derive (`#[proc_macro_derive(Name, attributes(helper))]`), and attribute (`#[proc_macro_attribute]`). Operate on `proc_macro::TokenStream` (`Vec<TokenTree>`-ish, cheap-clone) returning `TokenStream`. Unhygienic by spec — output behaves as if written inline. `Span` for diagnostics. Reference: `reference/src/procedural-macros.md`.

**Logos nomenclature.** No `proc-macro` crate kind; no `TokenStream` type. The replacement layer:
- Function-like proc-macro analog: `#[fn_macro]` returning `ExprBlob` (expr position) or `ItemList` / `QuoteItemBlob` (item position). See `docs/language/reference/macros.md:54-58`.
- Derive proc-macro analog: per-trait `#[derive_<trait>]` triggers, each backed by an `#[metaprog_handler("derive_<trait>")]` fn. Registry in `sema_impl.hpp:1580-1582` (`metaprog_handlers_`), collected at `sema_collect.cpp:1364-1395`, dispatched at `sema_collect.cpp:485-542`. Reject of the Rust `#[derive(Trait, ...)]` shape with a guidance message: `sema_impl.hpp:1447-1457`.
- Attribute proc-macro analog: also a `#[metaprog_handler("<attr_name>")]` fn — the handler runs over the annotated item's AST view. Examples: `stdlib/std/compiler/metaprog/derive_clone.logos:25` (`#[metaprog_handler("derive_clone")]`), `derive_branch_node.logos:165`.
- Token-stream analog: `#[token_macro]` callee receives the raw bytes as `str` (`sema_expr.cpp:16394-16397`). No structured `TokenTree` API; ExprBlob is structured-AST.
- Output AST blobs: `ExprBlob`, `QuoteItemBlob`, `ItemList` (`stdlib/std/compiler/metaprog/itemlist.logos:17-22`). Splice host shim: `logos_emit_item_blob_subst` (`stdlib/std/compiler/metaprog/emitter.logos:37`).

**Match verdict: WARN — blessed §A3 divergence (full model swap).** Same row as #1. Logos doesn't isolate proc-macros into a separate crate kind — every `#[fn_macro]` / `#[metaprog_handler]` callee is part of the surrounding crate and runs in the same metacall JIT (`src/compiler/main.cpp:3527-3541`). Nomenclature alignment: there is no canonical "proc macro" term to map; consumers should be steered to the three Logos kinds:
- `#[proc_macro]` (function-like) → `#[fn_macro]`
- `#[proc_macro_derive(X, attributes(h))]` → `#[derive_<X>]` trigger annotation + `#[metaprog_handler("derive_<X>")]` impl
- `#[proc_macro_attribute]` → `#[<attr_name>]` trigger + `#[metaprog_handler("<attr_name>")]` impl

**Implementation pointer.** Function-like: `sema_expr.cpp:16326` (dispatch). Derive/attribute: `sema_collect.cpp:485-542` (handler invocation pass) + `:1364-1395` (registration). Driver/JIT wiring + symbol binding: `src/compiler/main.cpp:2446` (host shims) and `:3527-3541` (metacall JIT bindings). Sample handlers in stdlib: `stdlib/std/compiler/metaprog/derive_clone.logos:26`, `derive_branch_node.logos:166`.

**Interactions check** (vs `docs/language/feature-interactions.md:417-419`):

- **Tokens (input):** WARN. Three input shapes (Logos chooses one per macro flavor):
  - `Vec<ExprBlob>` / `ExprBlob` — already-parsed AST blobs (`sema_expr.cpp:16386-16397`).
  - `str` — raw bytes for `#[token_macro]`.
  - `target_offset: u32` — host-side AST handle for `#[metaprog_handler]` callees; the handler walks the AST via `meta_*` reflection (`AnyVal` / `OView` / `meta_field_iter` etc., enumerated in `component-based-metaprog.md:184-191`).
  None of these are Rust's `TokenStream`. Concrete consequence: a Rust proc-macro that consumes raw lexical token streams (e.g. a DSL like `quote!` or `syn`) doesn't port verbatim — it has to either re-target the Logos AST or accept `str` via `#[token_macro]`.
- **Items (output):** OK. Item-position fn-macros return `ItemList` / `QuoteItemBlob`; the splice point inserts items into the enclosing module exactly as Rust derive/attribute proc-macros append items after the annotated item. Verified path: `sema_expr.cpp:17198-17260` builds a synthetic thunk that walks the returned `ItemList` and emits each `QuoteItemBlob` via `logos_emit_item_blob_subst`.
- **Attributes (`#[derive(X)]`, `#[attr]`):** WARN. The `#[derive(X, Y)]` Rust shape is *explicitly rejected* with a redirect message — `sema_impl.hpp:1447-1457`. Users write one `#[derive_<trait>]` per derive instead. This is a real ergonomic divergence: a Rust port has to mechanically rewrite each `#[derive(Clone, Debug, PartialEq)]` into three lines, and each derive trait needs a stdlib handler. Current stdlib has only `derive_clone` and `derive_branch_node` (`ls stdlib/std/compiler/metaprog/`); no `derive_debug`, `derive_eq`/`derive_partial_eq`, `derive_ord`/`derive_partial_ord`, `derive_default`, `derive_hash`, `derive_copy`. That's a meaningful catch-up backlog inside an otherwise-blessed divergence — every Track-3 import that hits `#[derive(Debug)]` needs a manual workaround until those handlers exist.
- **Crate type (`proc-macro` crate kind):** WARN. There is no separate crate kind — handlers compile into the same module as their users. The metacall JIT pipeline (`main.cpp:3527-3541`) is module-internal. This means handler authors can't be sandboxed the way Rust proc-macro crates are, and there's no link-time isolation. Logos's bet: metaprog is a JIT pass over the same compilation, which simplifies the build graph (no extra crate, no two-stage compilation) but loses the partial-isolation property.
- **Diagnostics:** WARN. There's a single host shim path for handler-emitted diagnostics (the `#[metaprog_handler]` hook emitter, `stdlib/std/compiler/metaprog/emitter.logos:114`). No `Span`-equivalent API for attaching diagnostics to arbitrary tokens in the *input* — diagnostics anchor at the trigger annotation site. Acceptable for derives (the trigger location is the right anchor); narrower for general attribute macros.
- **Source spans:** WARN. Logos `line!()` / `file!()` / `module_path!()` / `column!()` exist (`sema_expr.cpp:16172-16178`) but `column!()` returns 0 ("the AST doesn't track columns yet"). The `meta_*` reflection layer exposes type/field info; it doesn't expose token spans (no analog to `proc_macro::Span::call_site()` / `Span::def_site()`). For derive handlers that need to position diagnostics on a specific field, the field offset (`target_offset: u32`) is the anchor — coarser than Rust's per-token span.

**Gaps / debt.**
- Sparse derive coverage: only `derive_clone` + `derive_branch_node` in stdlib. Missing handlers for `Debug`, `PartialEq`/`Eq`, `PartialOrd`/`Ord`, `Default`, `Hash`, `Copy` — each one is one stdlib hook + tests. Hot-list priority: `Debug` (used everywhere in coretests), `PartialEq`/`Eq`, `Default`.
- The Rust `#[derive(X, Y, Z)]` rejection message at `sema_impl.hpp:1453-1456` is good triage, but the doc page `docs/language/reference/macros.md` doesn't mention `#[derive_<trait>]` at all — readers landing there from an audit don't get the migration path. Add a "derives" section.
- No `Span`-equivalent / column tracking → `column!()` always 0 (`sema_expr.cpp:16172`); derive handler diagnostics anchor on the field offset, not per-token spans. Listed in `component-based-metaprog.md:208` as roadmap.
- No `quote_stmt!` / `quote_pat!` / `quote_ident!` (`component-based-metaprog.md:205` — roadmap). For derives that need to emit a stmt or a pattern, the current pattern is "emit a block-expr containing the stmts, splice through a metacall driver" which works but is convoluted.
- No `proc-macro` crate isolation; handlers live in the same module graph. Documented design choice but a real divergence vs Rust's stable build-time isolation.

---

## Cross-category gaps

- **L (Attributes) intersection.** The whole Logos macro layer is attribute-driven (`#[fn_macro]` / `#[token_macro]` / `#[metaprog_handler("...")]` / `#[derive_<trait>]`). The attributes-category audit should pick up: each marker's allowed positions are partially enforced by `AttrTarget::Fn` bits at `sema_impl.hpp:1174-1177`; doc surface lives in `docs/language/reference/macros.md` and `docs/language/reference/attributes.md` but the linkage between them isn't audited here.
- **I (Modules, visibility) intersection.** `#[macro_use]` / `#[macro_export]` / `$crate` have no Logos analogue. Replaced by ordinary fn scoping + the manifest `prelude` field (`module_manifest.cpp:56-61`). Belongs in category I's audit row on "macros in name resolution".
- **C (Items) intersection — `vec!` builtin.** `vec!` is sema-resident (`sema_expr.cpp:16097-16170`) and overridable by a user `#[fn_macro] vec` in scope. The "prelude builtin macros" surface (cfg!, vec!, format-family, file!/line!/module_path!/column!) is undocumented as a closed list; should appear in either the macros reference or a prelude reference (category I).
- **K (Unsafe) intersection.** Handler emission goes through `unsafe { logos_emit_item_blob_subst(&blob); }` (e.g. `stdlib/std/compiler/metaprog/derive_clone.logos:72`). Whether this `unsafe` is a wart or load-bearing isn't decided in the docs — it surfaces because the shim takes a raw `*const QuoteItemBlob`. Could be wrapped to be safe.
- **M (Const eval) intersection.** `metacall` is the const-eval channel (A1/A2). Macros and `metacall` share the JIT driver — same bind sites (`main.cpp:3527-3541`) — so any improvement to JIT compile speed lands once across both surfaces.

## Recommended next moves

Each item is sized for a single-session work slice.

1. **Land `derive_debug` + `derive_partial_eq` + `derive_eq` stdlib handlers.** Pattern: copy `stdlib/std/compiler/metaprog/derive_clone.logos:25-100`, swap the emitted `impl` body. Highest ergonomic impact for Track-3 imports — every imported Rust file with `#[derive(Debug)]` currently needs hand-replacement. Tests: per-handler `tests/logos/pass/derive_*.logos`.
2. **Allow nested `#( #( … )* )*` repetition in `quote_*!`** (`sema_expr.cpp:13578` rejects it). Rust's macro repetitions can nest; Memoria-class assembly will need it. Slice: re-walk the placeholder tree depth-first, tracking the cursor stack per repetition level.
3. **Document the `macro_rules!` → `#[fn_macro]` migration path in `docs/language/reference/macros.md`.** Add a "Migrating from `macro_rules!`" section: matcher → ARGs vec; transcriber → `quote_expr!`; repetition → `#(…)sep*`; `:tt` → `#[token_macro]` (str); `$crate` → fully-qualified paths; `#[macro_export]` → `pub`. ~50 lines.
4. **Catalog the prelude macro builtins.** A short table at the top of `docs/language/reference/macros.md` listing `cfg!`, `vec!`, `format!`/`print!`/`println!`/`eprint!`/`eprintln!`, `file!`/`line!`/`module_path!`/`column!` with their Rust equivalents and exact lowering. Map to `sema_expr.cpp:16081, 16097, 16178`.
5. **Decide on `column!()` source positions.** Either wire column tracking through the AST (small Hermes-schema change in token records) or stabilise `column!() == 0` as a documented quirk. Today it's an undocumented zero (`sema_expr.cpp:16172-16178` comment, but no user doc).
6. **Per-derive stdlib batch follow-on.** After `Debug`/`PartialEq`/`Eq`: `derive_default`, `derive_hash`, `derive_partial_ord`/`derive_ord`, `derive_copy`. Order chosen by frequency in `tests/imported/`.
