# Reparse census: render-and-reparse and text-built expansion

Date: 2026-09-26. Input to ADR 0030 Q4 (`docs/adr/0030-hir-core-layer.md`).
Tree: `3e5cc879e`. Probes ran on `build/bin/logosc 0.47.0-preview+main-g3df07251`; `git diff 3df07251 HEAD -- src stdlib tools` is empty, so the binary matches the tree's compiled sources. No source edited, nothing built.

## 1. Method

- Every construction of the generated parser outside the module loader: `grep LogosParser src/compiler` gives 11 sites (1 loader, 5 `main.cpp`, 5 `sema_expr.cpp`). The public entries are `parse_module`, `parse_type_param_list`, `parse_type_ref`, `parse_expr`, `parse_wstatic_lit_type`, `parse_block`, `parse_param_list` (`build/src/compiler/logos_parser.hpp`).
- Every producer of text for those sites: `render_*_src` callers, `render_ctfe_lit`, `type_str` on text paths, `std::format("package …")`, `thunk_source`, `logos_emit_source` callers, `split_top_level_commas`, and `RAW_TEXT` readers.
- Logos side: `logos_parse_as` (host entry reached through `parse_type/_expr/_block/_params/_type_params/_wstatic` in `stdlib/mem/compiler/metaprog/emitter.logos`) and `Emitter::push_text` + `commit` → `logos_emit_source`.
- Excluded, because the text is a different language by design: `WqlParser` (deem/mapping bodies), `CfgLexer` (`cfg!`), `concat!`/`concat_bytes!`/`stringify!`/`include_str!`/`env!` (they read `RAW_TEXT` as data and never parse Logos), and the wql/trama/el DSL parsers in `stdlib/mem/wql/*_parser.logos`.
- Display-only renders (no reparse): `--expand` and `--dump-metaprog` (`main.cpp::main`, `render_module_source_for_dump`) and plain `--gen-dir` without `-g`. Listed in §2.4 for completeness.

Two classes:

- **R (render-and-reparse):** an AST (or a typed value) goes through `sema_render.cpp` / `type_str` / `render_ctfe_lit` to text and is parsed again.
- **T (text-built):** the user's raw token text (`RAW_TEXT`) or a fixed template is spliced into a string and parsed. No renderer is involved, but positions are lost the same way.

## 2. Census

Columns: **Rebase** = are lines/spans of the reparsed nodes mapped back to the site. **Unhandled** = what happens when the renderer meets a node code it does not handle (for T sites: the failure mode of the text surgery). **Repl.** = structured replacement: is it obvious, and its size (S < 100 lines, M 100–400, L > 400 or cross-module).

### 2.1 User-code desugarings (they feed sema/LIR/BIR/DWARF of user code)

| # | Site (file::function) | Trigger | Text built | Parser entry | Class | Rebase | Unhandled | Repl. |
|---|---|---|---|---|---|---|---|---|
| U1 | sema_expr.cpp::lower_fn_macro_call (format block, ~350 lines) | `format!` `print!` `println!` `eprint!` `eprintln!` `panic!` `format_args_str!` `write!` `writeln!` with a literal format string (every `println!`) | stage 1: `package __fn_macro_args; fn __f() { __c(<RAW_TEXT>); }`; stage 2: `{ let mut __buf…; let mut __f: Formatter…; __f.reset_spec(); …; let _ = fmt_display(&(<render_expr_src(arg)>), &mut __f); … }` wrapped in `package __fmt_inline; fn __f() { let _: () = <blk>; }` | parse_module ×2 (T, then R) | T+R | no. Diags and DWARF land on line 2 (probe P3, P13) | expr marker: arg lowered into a statement temp when `cur_stmt_temp_hoist_` is set (ca3127252), otherwise a refusal naming the AST code. pat/stmt markers are not checked: range patterns and `v @ p` in an argument give "internal — synthesised block failed to parse" (probe P14). §3.5 wrong renders (WHILE guard, FOR `mut`, PAT_RANGE `..=`, labels `''a`, PAT_FIELD `ref`/`mut` …) pass as silent wrong text. | yes. The arguments already exist as ASTs. Bind each to `&(arg)` as an LIR statement temp (the ca3127252 mechanism, applied to all args) or copy the arg subtrees into a synthesized block doc; the glue never needs text. M |
| U2 | sema_expr.cpp::lower_expr_inner case TRY_EXPR (Try dispatch) → lower_reparsed_tail_expr | `e?` where `e` is not `Result`/`Option` | `match (<render_expr_src(e)>).branch() { ControlFlow::Continue(__try_c) => __try_c, ControlFlow::Break(__try_r) => return <type_str(ret)>::from_residual(__try_r), }` | parse_module | R | no (line 2, probe P9) | the marker is a `/*…*/` comment, so `(marker).branch()` reads `().branch()`: "receiver is not a struct (got void)" at line 2 (probe P9). `e` is lowered twice (once for the type, once through the reparse), so its diagnostics come out twice (P9: line 47 and line 2). `rt_src` uses the NAME form of `type_str` (enum args dropped), which works only through inference. | yes. Lower `e` once into a temp and build the SMatch/Try LIR directly (the common path already has a Try node). S. Retired again by ADR S10 (HIR `?` via lang items). |
| U3 | same, Box\<dyn\> conversion arm | `e?` with outer error `Box<dyn Tr>` | `match (<render(e)>) { Result::Ok(v) => v, Result::Err(e) => return Result::Err(Box::new(e)), }` | parse_module | R | no | as U2 (double lowering, marker → `()`) | as U2. S |
| U4 | sema_expr.cpp::lower_builtin_macro `vec` | `vec![…]` / `vec!(…)` with no user `vec` macro | with a renderable `let v: Vec<E>` hint: `let mut __vecm: Vec<E> = vec_new::<E>(); __vecm.push(<piece>); … __vecm`. Otherwise `vec_from_arr([<RAW_TEXT>])` or `vec_new::<_>()` | parse_module (lower_reparsed_tail_expr) | T (+R for `E` via `type_str(…, source_form)`) | no (line 2, probe P2) | `split_top_level_commas` tracks `()[]{}` and quotes only: a turbofish `<A, B>` and a closure `\|a, b\|` are split at their comma, and `'a` lifetimes toggle char mode. A `type_str` form that does not reparse falls back to vec_from_arr. | yes, once the arguments are parsed as AST (G1 below). Build the push block or a `Vec::from` lang-item call as nodes. S after G1 |
| U5 | lower_builtin_macro `matches` | `matches!(e, pat [if g])` | `match (<piece0>) { <rest> => true, _ => false }` | parse_module | T | no (P12) | the same comma splitter | yes after G1: `pat [if g]` needs grammar support for a pattern argument. S |
| U6 | lower_builtin_macro `dbg` | `dbg!(e)` / `dbg!()` | `let __dbg_N = <raw>; eprintln!("[<file_>:<line>] <esc raw> = {:?}", __dbg_N); __dbg_N`, which then goes through U1 again | parse_module | T | no (P12) | `file_` is spliced into a string literal unescaped | yes after G1. S |
| U7 | lower_builtin_macro `unreachable`/`todo`/`unimplemented` | those macros | `panic!("<prefix>: {}", format!(<raw>))`, re-entering U1 twice | parse_module | T | no | as U1 | yes after G1. S |
| U8 | sema_expr.cpp::lower_macro_include | `include!("f")` in expression position | `package __include_expr; fn __f() -> i32 { <file contents> }` | parse_module | T | no. Nodes carry the included file's lines +1 under the includer's file name | n/a (raw file). Refused if the file is not one expression. | yes. Parse the file as its own source unit (a SourceMap file) with parse_block/parse_expr. S |
| U9 | lower_fn_macro_call (user `#[fn_macro]` path) | any user or stdlib `#[fn_macro]` call: `assert!`, `assert_eq!`, … | `package __fn_macro_args; fn __f() { __c(<RAW_TEXT>); }`, then each ARG subtree is serialized to an ExprBlob and spliced into user code by the metacall driver | parse_module | T | no. The spliced blobs keep SRC_LINE 2 (probe P11) | a parse failure refuses | yes, same as G1: the grammar parses the arguments where they stand. M (shared with G1) |
| U10 | sema_expr.cpp::lower_fn_macro_call_item | `name!{…}` at item position (non-token macro) | `package __fn_macro_item_args; fn __f() { __c(<RAW_TEXT>); }` | parse_module | T | no | refusal | as U9 |
| G1 | (grammar) `tools/peg_gen_cpp/grammars/logos.peg` FN_MACRO_CALL / FN_MACRO_CALL_ITEM | root of U4–U7, U9, U10, U1 stage 1 | arguments are stored only as `RAW_TEXT`. The node table says "ARGS = expr-list", but no alternative writes ARGS. | — | — | — | — | Try an `expr_list` alternative first and keep `RAW_TEXT` for `#[token_macro]` / unparsable bodies. One grammar change plus about 7 consumers. M |

### 2.2 Metaprog thunk sources (text → JIT, run at compile time)

| # | Site | Trigger | Text built | Parser entry | Class | Rebase | Unhandled | Repl. |
|---|---|---|---|---|---|---|---|---|
| M1 | sema_expr.cpp::lower_metacall (call form) → main.cpp::emit_source_tagged("<metaprog-thunk>") | `metacall f(args)` / `f::<T>(…)` / `T::m(…)` | `package <pkg>; [uses] fn __metacall_thunk_N() -> <ret> { return <callee><turbofish>(<render_ctfe_lit(arg)>…); }` | parse_module | R (values, types) | n/a (JIT code) | `render_ctfe_lit`: every NEGATIVE suffixed int renders `(-7)i64`, which does not parse. The INT64_MIN guard `s.back()++` also turns -10 into `(-:`. Result: "parse failed near line 2" then "thunk lookup … Symbols not found" (probes P1, P6). The turbofish uses `type_str(resolve_type(…))`, the NAME form that drops enum args: `metacall sz::<Option<i64>>()` becomes `sz::<Option>()`, compiles clean, and returns a different value from the direct call (probe P15, exit 1, **silent wrong code**). A char CTFE value falls into the integer branch (*inferred*). | small fixes, text can stay: suffix inside the parens, fix the magnitude, `type_str(…, source_form=true)` or reuse `render_type_src`. S. Structured: pass args as ExprBlobs (M9 path). M |
| M2 | lower_metacall (block / expr form) | `metacall { … }` / `metacall <expr>` | `fn __metacall_thunk_N() -> <ret> <render_block_src(block)>` or `{ return <render_expr_src(e)>; }` | parse_module | R | no | stmt marker `/* … */;` (LET_PAT, NESTED_FN, DESTRUCTURE_ASSIGN …) refuses only because an empty statement does not parse (probe P7: `let (a, b) = …`). §3.5 wrong renders reach the JIT silently (e.g. WHILE guard dropped), so compile-time values can be wrong. | yes. Build the thunk FN as a blob (the `logos_emit_item_blob` path) with the user's block subtree copied in. M |
| M3 | lower_metacall (Writ ret) | `metacall f()` returning `Rc<Writ>` | fixed template around `call_text` | parse_module | R (as M1) | n/a | as M1 | as M1 |
| M4 | sema_expr.cpp::lower_metacall_item | item-position `metacall f(args);` | fixed template (ItemList / QuoteItemBlob drain) around `call_text`. Turbofish: `type_str(resolve_type)`, or a local `render_wstatic` lambda for `@{…}` args | parse_module | R | n/a | as M1 (negative literals, NAME-form turbofish) | as M1. S |
| M5 | lower_fn_macro_call (token / single / Vec forms), emit_token_macro_item_site, lower_fn_macro_call_item | every fn/token macro | fixed templates: `logos_macro_arg(<id>u64, i)`, the callee name, `str_from_raw` | parse_module | T (template only) | n/a | none: no user text | keep. A template with ids only is harmless. |
| M6 | sema.cpp::resolve_wstatic_value → `wstatic_sources` → main.cpp::render_factory_chunks, the inline copy in run_metaprog_dispatch, and the post-mono drain in main | a `@{…}` CFG type argument that demands a container factory | `render_expr_src(@{…})` escaped into `metacall __container_factory("<hash>", "<cfg src>");`, which the factory re-parses with `parse_wstatic` (rule 3) | parse_module + logos_parse_as | R | n/a | no marker check. `render_writ_val_inner_` falls back to `render_expr_src`, so an unsupported node becomes a `/*…*/` comment, and a comment is lexer trivia: the value would be silently absent from the text, while the hash was computed from the AST (*inferred*). §3.5 lists WRIT_ENTRY LO_NEG dropped. Three hand copies of the chunk builder, one with its own escape table. | yes. Hand the factory the registered Writ blob (`wstatic_registry_`) as a WritStatic argument instead of text. M (touches the metaclass-factory ABI) |
| M7 | main.cpp::render_deem_plan_chunks | a `deem` over a container CLASS meeting a generated family | `metacall __deem_bind("<name>", "<params re-spelled by string surgery>", "<query>", "<family>", "<class spec>", "<cfg>")`. The params split on top-level commas with depth over `([<` / `)]>`, so the `>` of `->` unbalances the depth (*inferred*) | parse_module | T | n/a | wrong split, then a refusal downstream | L (DSL text by design; restructure with the deem pipeline) |
| M8 | stdlib/lcm/wql/deem_bind.logos::__deem_bind → Emitter.commit → logos_emit_source("<metaprog>") | M7 continued | `deem <name>(<params>) { <query> }` as raw push_text | parse_module | T | no. The generated deem's diagnostics point into a `<metaprog>` pseudo-file (*inferred*) | refusal | L |

### 2.3 Logos-side emitters (generated code, spliced through `logos_parse_as`)

| # | Site | Text built | Entry | Rebase | Unhandled | Repl. |
|---|---|---|---|---|---|---|
| L1 | stdlib/mem/wql/rexpr_walk.logos (258 `parse_*` calls: parse_type 121, parse_expr 120, parse_block 13, parse_params 3, parse_type_params 1) | the deem/wql codegen assembles Logos types, expressions and loop nests (depth = the join chain) as strings, including renderings of the USER's query expressions | logos_parse_as rules 0–5 | fragment lines are 1-relative. wql `soff` exists on the DSL side (ADR 0024 S0), but no Logos span | parse failure → null blob → loud splice failure | L. Keep; register the fragments as virtual files at H0. |
| L2 | stdlib/mem/wql/trama_render.logos (58 push_text into a body buffer, then parse_block / parse_params once) | Trama template render bodies | rules 4, 5 | as L1 | as L1 | L. Keep. |
| L3 | stdlib/lcm/canon/container_item.logos (parse_type 10, parse_expr 14, parse_type_params 3, parse_wstatic 5), alg_kernel.logos (parse_expr 5), mem/wql/mapping_item.logos (2), catalog_macro.logos (1) | hashes, literals, family types | rules 0–3 | n/a (no user text, apart from the CFG doc of M6) | loud | keep |
| L4 | derives (`stdlib/mem/compiler/metaprog/derive_*.logos`) | none. All eleven use `quote_item!`/`quote_expr!`; push_str only builds identifiers | — | — | — | already structured. Row `derive_clone_nonprimitive_field_refused` is a quote REPEAT_GROUP defect, not a reparse. |

### 2.4 Driver and tooling

| # | Site | Trigger | Text | Entry | Rebase | Unhandled | Keep? |
|---|---|---|---|---|---|---|---|
| D1 | main.cpp::try_gen_dump (called from logos_emit_item_blob_subst) | `--gen-dir` **with `-g`** | the whole synth module via `render_module_source_for_dump` written to `<pkg>.N.gen.logos`; the reparse REPLACES the in-memory doc | parse_module | by construction: the file IS the source | parse failure or top-level item census mismatch → loud warning, in-memory doc kept. The census counts top-level item kinds only, so a wrong render INSIDE a body (§3.5: WHILE guard, FOR `mut`, labels) passes and is compiled (*inferred*: silent wrong code under `-g` only) | keep (it is the debug-info mechanism). Add a structural body check or a full-AST equality gate. |
| D2 | main.cpp::main (`--test`) | `--test` | synthesized runner `main` calling each `#[test]` fn | parse_module | n/a | refusal | keep (no user code; could become an AST build, S) |
| D3 | main.cpp::emit_source_tagged("<metaprog>") via `logos_emit_source` | legacy Emitter channel | handler text | parse_module | no | refusal, then the handler may retry | keep; legacy (quotes replace it) |
| D4 | main.cpp::logos_metaprog_test_module_blob | Slice-3 test fixture | inline source | parse_module | n/a | refusal | keep or delete with the fixture |
| D5 | main.cpp::main `--expand`, `--dump-metaprog`; `--gen-dir` without `-g` | display | render only, no reparse | — | — | `/* unsupported */` in the output | out of scope |

## 3. Known defects tied to these sites

| Evidence | Site | Kind |
|---|---|---|
| probe P15: `metacall sz::<Option<i64>>()` ≠ `sz::<Option<i64>>()` (exit 1) | M1/M4 turbofish NAME form | **silent wrong code** (new, unrowed) |
| probes P1, P6: `metacall id(-7i64)` / `id(-10i64)` refused with "thunk lookup … Symbols not found" | M1/M4 `render_ctfe_lit` | refusal, misleading (new, unrowed) |
| probe P14: `format!("{}", match x { 1..=5 => 1, _ => 0 })` and `v @ 1` → "internal — synthesised block failed to parse" | U1, pat marker unchecked | legal program refused (new, unrowed) |
| probe P9: `(loop { break mk(k); })?` → "receiver is not a struct (got void)" at line 2; duplicated diagnostic | U2 | refusal, misleading; double lowering (new, unrowed) |
| probe P7: `metacall { let (a, b) = …; a + b }` refused | M2 stmt marker | refusal (new, unrowed) |
| probes P2, P3, P11, P12: diagnostics for `vec!`, `println!`, `assert!`, `assert_eq!`, `matches!`, `dbg!` arguments all say `:2:` | U1, U4–U7, U9 | wrong position, every macro in the language |
| probe P13: `-g` line table of `main` contains `line: 2` for the `println!` at line 9 | U1 | wrong DWARF |
| squeue `vec_literal_elem_type_ignores_call_site_wrong` (tier 1) | U4 fallback `vec_from_arr([...])` has no expected-type channel | wrong code. Structured expansion is necessary but not sufficient (needs expected-type flow). |
| squeue `format_named_and_captured_args_refused` (tier 2) | U1 (named args need the arg map; `{name}` captures need a VAR_REF synthesized at the site, which is natural in a structured expansion) | refusal |
| squeue `assert_with_message_crash` (tier 1) | U9/G1 (*inferred*): `format!(#(#tail),*)` inside `quote_expr!` is a FN_MACRO_CALL whose args are only `RAW_TEXT`, so the repeat splice has no ARGS slot to fill ("TinyObjectMap::put — map is full (capacity 0)") | crash-refusal |
| history e07fcde46: `sizeof::<Option<Arc<i32>>>()` inside `println!` printed 4 for 8 | U1 (`type_str` NAME form in rendered text) | wrong code, fixed for that path; M1 is the same defect still open |
| history 4e7ec7e8a: `*expr = value` rendered as `* = value` | renderer | wrong text |
| history 60d6cc398 / 7cbd0687b: `render_pat_src` dropped `mut` in the if-let reparse door; the door was DELETED by delegation to `lower_match` over a synthesized AST node (−395 lines) | former if-let door | precedent: structured replacement closed two tier-1 rows |
| repr audit §3.5: about 30 node codes without a render case, 14 wrong renders, and only the expr marker is checked | U1, U2, M2, M6, D1 | silent wrong text wherever those nodes reach a render |
| repr audit §3.1: "reparsed macro lines: no rebase (*inferred*)" | U* | now measured (P2–P13) |

tests/soundness/open has no other row naming these sites.

## 4. Recommendation for ADR 0030 Q4

**(a) Do it FIRST (a step R0 before H0) for user-code desugarings only (U1–U10 + G1). Everything else can follow.**

Reasoning:

1. H0 puts `SRC_SPAN` on every node, and D5 says a synthesized node takes the span of its construct. A reparsed node has a span into a synthetic buffer. Landing H0 first means one of two things:
   - teach each of the 10 user-code sites to register a virtual `SourceMap` file with an `expanded_from` span. That is work on code R0 deletes. For rendered text (U1 stage 2, U2, U3) no byte mapping back to the user's source exists at all.
   - exempt them from the H0 "every node has a span" gate. The gate is then born with holes on the most-used construct (`println!`).
2. "Desugarings must not go through text" is a D-rule of the ADR. The HIR pass would be the new home for `?`, `vec!`, the format family and `matches!`. Porting a text path into the HIR violates the rule, and porting it later means porting it twice.
3. R0 does not depend on H0. Its key piece, G1 (parse macro arguments as AST where they stand), fixes positions immediately through the existing `SRC_LINE`, and it gives R4a's `SRC_SPAN` real nodes to stamp.
4. The renderer's defect surface (§3.5) shrinks to the sites that legitimately stay textual: generated code (M*, L*, D*). Those produce code, not user desugarings. H0 covers them with virtual files, which is the ADR's "minimum" option, applied where it is correct.
5. Cost is bounded. G1 is M; U1 is M; U2/U3/U4–U8 are S each, and U2/U3 are also retired by S10 later. The precedent 7cbd0687b removed one such door at −395 lines with two rows closed.

Keep out of R0, to follow at or after H0: M1/M4 (fix now as S0 one-point fixes, see below), M2, M6, M7/M8, L1–L3, D1–D4.

**(b) Order of replacement by harm**

| Order | Item | Harm | Size | Note |
|---|---|---|---|---|
| 0 | M1/M4 turbofish `type_str` NAME form → source form; `render_ctfe_lit` negative literals and the digit bump | silent wrong code (P15), refusals (P1, P6) | S | one-point fixes (ADR step S0); no dependency. Row P15 and P1/P6 first. |
| 1 | G1 + U9/U10: macro arguments parsed as AST in the grammar | wrong line on every macro argument, diag and DWARF; `assert!` message crash (*inferred*); comma-splitter fragility | M | unlocks 2–5 |
| 2 | U1 format family: glue built as nodes/LIR over the already-parsed args; pat/stmt markers become unreachable | the highest-traffic render path; §3.5 wrong renders can be silent wrong code; P14 refusals; named/captured args (`format_named_and_captured_args_refused`) become natural | M | after 1, stage 1 disappears |
| 3 | U2/U3 `?` dispatch: lower once, build SMatch directly | misleading refusal, double lowering, line 2 | S | interim; S10 replaces it with the lang-item HIR desugaring |
| 4 | U4 `vec!` as nodes, with an expected-type channel | tier-1 wrong code (`vec_literal_elem_type_ignores_call_site_wrong`) needs this plus inference | S (+ inference work) | after 1 |
| 5 | U5–U7 `matches!`, `dbg!`, `unreachable!`/`todo!`/`unimplemented!` | wrong line; `dbg!` path escaping | S each | after 1, 2 |
| 6 | U8 `include!` as a SourceMap unit | wrong file and line | S | independent |
| 7 | M2 metacall block/expr thunk as a blob | silent compile-time miscompute possible through wrong renders; P7 refusal | M | after H0 is fine |
| 8 | M6 CFG doc to the factory as a Writ blob, not text; fold the three chunk-builder copies | silent config divergence possible (*inferred*) | M | factory ABI |
| 9 | D1 body-level fidelity gate for `--gen-dir -g` | silent wrong code under `-g` only (*inferred*) | S | keep the mechanism |
| 10 | M7/M8, L1–L3, D2–D4 | generated code; loud failures | L / keep | register as virtual files at H0 |

## Appendix: probes

Compile-only unless noted (`logosc p.logos -o p.o`). The line under test is not line 2 in every program.

| Probe | Program core | Observed |
|---|---|---|
| P1 | `let n: i64 = metacall id(-10i64);` | `logos_emit_source: parse failed near line 2`; thunk symbol not found; rc 1 |
| P2 | `vec![a, nope_undefined, 3]` at line 8 | `p2.logos:2: undefined variable` |
| P3 | `println!("{}", undefined_in_fmt)` at line 7 | `:2:` twice |
| P6 | `metacall id(-7i64)` | thunk text `return id((-7)i64);`, parse failed |
| P7 | `metacall { let (a, b) = (40i64, 2i64); a + b }` | thunk text contains `/* render_stmt: unsupported AST code 217 */;` |
| P9 | `(loop { break mk(k); })?` with a user `Try` type; `mk(undefined_zz)?` at line 47 | `:2: receiver is not a struct (got void)`; `:47:` and `:2:` for the same undefined variable |
| P11 | `assert!(undefined_q > 1)` at line 7, `assert_eq!(a, nope_w)` at line 9 | both `:2:` |
| P12 | `matches!(undefined_m, 1)`, `dbg!(undefined_d)` | both `:2:` |
| P13 | `println!("{}", x)` at line 9, `-g --emit-llvm` | `main`'s `!dbg` locations: lines 5, 5, 2, 10. Line 9 is absent. |
| P14 | `format!("{}", match x { 1..=5 => 1, _ => 0 })`, and the same with `1..5` and `v @ 1` | `format!: internal — synthesised block failed to parse`. The `1 => 1` control compiles. |
| P15 | `fn sz<T>() -> i64 { sizeof::<T>() as i64 }`; `metacall sz::<Option<i64>>()` vs `sz::<Option<i64>>()`; linked and run | thunk text `sz::<Option>()`; compiles clean; exit 1 (the values differ) |
