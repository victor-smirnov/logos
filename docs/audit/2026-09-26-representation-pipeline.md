# Representation audit: AST -> sema -> LIR -> mono -> BIR -> MLIR/DWARF

Date: 2026-09-26. Tree: `main` @ `3df07251e`. This is a read-only code audit. It merges 12 inventory passes (10 grouped by AST code, plus one on positions/provenance and one on LIR-key survival). Nothing was built for this report. One pass ran the prebuilt `build/bin/logosc 0.47.0-preview+main-g8c1f094c-dirty` on small probes, and those rows say "observed". Every other defect claim comes from reading the code. Rows marked *inferred* were not traced end to end. Code is cited by FILE::SYMBOL.

Legend: status **CP** carried_processed, **CR** carried_raw, **P** partial, **CD** consumed_dropped, **NR** never_read. "—" means absent. "blob" means the quote/metaprog Writ template blob only.

Stage facts that frame every row:

- **Locations.** SRC_LINE is the only position field in the AST. It records the first line only, with no column and no end. In LIR, only statements have a location (`lir_schema::stmt_common::LINE`). Expressions, patterns, arms, params and decls have none. `Func` has `SOURCE_FILE` but no decl line.
- **Mono re-emits everything.** Every fn goes through `mono_clone.cpp::Mono::clone_fn` and every struct through `Mono::clone_struct_def`, including non-generic ones (empty SubstMap). A key those builders do not copy is gone from the whole post-mono program. Non-generic enums, consts, aliases, traits, impls, dispatch entries and inst annotations are moved unchanged. The metaprog records and `module_inner_docs` are not moved.
- **BIR is a dead end for data.** `borrow_bir.inc` builds a per-function IR inside borrow_check (pre-mono on templates, then post-mono), emits diagnostics and throws it away. `mlir_gen` reads post-mono LIR. "Reaches BIR" never means "reaches codegen".
- **DWARF is thin.** It is emitted only with `-g`, as `FileLineColLoc(line, col 0)` per statement.
  - Only `let` locals and params get DILocalVariables. There are no lexical blocks, namespaces, global variables or template parameters.
  - Closures and drop glue run under `DebugScopeSuspend`.
  - Tuple, array, enum and dyn types are opaque blobs.

## 1. Executive summary

Counts are over the rows of the matrix in section 2 (one row per node/field or LIR-key family; trivially identical rows are merged):

| Section | CP | CR | P | CD | NR | Rows |
|---|---|---|---|---|---|---|
| 2.1 Module, imports, attributes, docs | 2 | 0 | 14 | 8 | 1 | 25 |
| 2.2 Functions, params, closures | 19 | 0 | 11 | 11 | 1 | 42 |
| 2.3 Type declarations | 33 | 8 | 33 | 25 | 6 | 105 |
| 2.4 Types | 23 | 2 | 22 | 4 | 3 | 54 |
| 2.5 Generics, bounds, lifetimes | 5 | 2 | 12 | 2 | 0 | 21 |
| 2.6 Statements, control flow | 44 | 4 | 13 | 3 | 2 | 66 |
| 2.7 Expressions | 43 | 1 | 21 | 10 | 2 | 77 |
| 2.8 Literals | 6 | 1 | 2 | 0 | 0 | 9 |
| 2.9 Patterns | 19 | 2 | 15 | 4 | 3 | 43 |
| 2.10 Writ | 17 | 2 | 13 | 4 | 0 | 36 |
| 2.11 Metaprog, macros | 4 | 4 | 2 | 11 | 3 | 24 |
| 2.12 Relational items | 4 | 9 | 7 | 8 | 1 | 29 |
| 2.13 Dead codes | 0 | 0 | 0 | 0 | 13 | 13 |
| 2.14 Positions, diagnostics | 1 | 1 | 11 | 3 | 1 | 17 |
| **Total** | **220** | **36** | **176** | **93** | **36** | **561** |

In short: 39% of rows are carried processed and 6% carried raw. 31% are partial, 17% are consumed and dropped, and 6% are never read. Even a CP row usually loses its span and its written spelling. CP means the semantic fact survives, not that the row is lossless.

The most important losses, ranked by how many future consumers they block:

1. **Positions.**
   - No column or range exists anywhere. Nodes have no end positions.
   - LIR expressions, patterns, arms and every declaration have no line.
   - DWARF takes a fn's decl line from its first body statement, and struct/enum/const decls have no line or file at all.
   - BIR errors enter `Diag` with `line = 0` and an empty file (`borrow_check.cpp`).
   - Reparsed wrapper code (vec!, the `?` dispatch paths, fn_macro args, include!) is not rebased to the macro site, so its lines are probably wrapper-relative (*inferred*).
2. **Desugaring provenance.** It is kept only where the borrow checker needed it: `BorrowOrigin`, `CallMode`, `TRANSPARENT`, `COMPILER_GLUE`, `DESTRUCTURE_TMP`. The following are indistinguishable from hand-written equivalents:
   - `if let`, `while let`, let-chains, iterator `for`, and `?` (the reparse routes)
   - compound assign, overloaded operators, the `unsafe` region
   - `[v; N]`, comprehensions, struct-literal `..base`, field shorthand
   - or-pattern fan-out, and Writ/str/char patterns
   - metacall and macro expansions
3. **Mono strips declaration metadata.**
   - Fns lose DOC, the test flags, TYPE_PARAMS and bounds, and WHERE_TYPE_BOUNDS.
   - Structs lose DOC, F_DOC, ANNOTATIONS, TYPE_HASH and TYPE_CODE.
   - Generic enum instances lose DOC, LIFETIME_PARAMS and BORROW_CARRYING.
   - Docs are read in exactly one place, `emit_module.cpp::emit_docs_facts`, which runs pre-mono. `module_inner_docs` has no reader at all.
4. **Sema-only facts that never reach LIR.**
   - Visibility: `pub(module)` everywhere, field/const/alias/trait visibility, static IS_PUB, assoc-const IS_PUB.
   - `unsafe` on fn, trait, impl, fn-ptr and block.
   - The extern ABI string, so "C-unwind" is treated the same as "C".
   - The import graph and re-exports.
   - Trait method params, assoc consts, GAT params, assoc-type defaults, enum/trait param bounds and defaults, and where-clause origin.
   - Schema declarations (SemaStructInfo only).
5. **LIR bound maps are lossy.** `lir_mirror.cpp::tbound_av` drops `assoc_eqs`, `lifetime_args`, `fn_params`/`fn_ret`/`is_fn_family`, `is_relaxed` (`?Sized`), `on_ref_subject` and `is_ref_mut`. So `where &T: Tr` reads in LIR as `T: Tr`, which is wrong.
6. **Identity carried as spelling.**
   - Callees are mangled strings, fields are name strings, loop labels are raw strings (mlir_gen silently falls back to the innermost loop), and `SAssign` has no VAR_SLOT.
   - Consumers re-resolve names, and the codebase has already had defects from keying on the `__iter`, `__dst_`, `__refut_` and `__impl_` prefixes.
7. **Defects found in passing** (details in section 3.6):
   - REPEAT_GROUP outside a quote is silently swallowed (observed).
   - `ref x: T` parameters are typed `&Self`.
   - Qualified type and enum paths drop their package.
   - Negative impls are exported as positive.
   - `&mut dyn Tr` interns the same as `&dyn Tr`.
   - `unsafe fn()` has the same type as `fn()`.
   - Union DWARF is wrong.
   - TYPE_CODE is lost after mono.
   - Docs leak to the next item after macro, metacall, instantiate, mapping, container and deem items.
   - `pub(module) deem` is emitted fully pub.
   - Render bugs break metaprog round-trip.
8. **Dead AST codes.** 17 node codes are declared in `%nodes`/`ast.hpp` but no grammar action produces them (section 2.13). TEMPLATE_DECL is produced but never read.

## 2. Matrix

Columns: Node | Field | Sema reader | LIR | BIR | MLIR / DWARF | St | Loss.

### 2.1 Module, imports, attributes, docs

| Node | Field | Sema | LIR | BIR | MLIR/DWARF | St | Loss |
|---|---|---|---|---|---|---|---|
| MODULE | NAME+PATH_PARTS | sema_collect read_package_name | dk::PKG per decl; pkg_module_ids | in mangles | in mangles; no DINamespace | CP | no package node; no DWARF namespace |
| MODULE | USES | collect build_import_scope | — | — | — | CD | import graph, aliases, wildcards lost |
| MODULE | ITEMS | collect; lower_module_items | LProgram vectors by kind | per fn | per decl | CP | source item order lost |
| MODULE | file identity | file_ | Func SOURCE_FILE; ModuleInnerDoc | — | DIFile per fn | P | no file on struct/enum/const/trait |
| USE | NAME+PATH_PARTS | build_import_scope; module_loader | — | — | — | CD | which import resolved a name: not recorded |
| USE | IS_PUB | pkg_reexports_ | — | — | — | CD | re-export graph sema-only |
| USE | FROM_KW/FROM_MODULE | collect | — | — | — | CD | module disambiguation lost |
| USE_VARIANTS | NAME, PATH_PARTS, TYPE_NAME, VARIANTS | module_loader extract_uses + 2 sema copies | — (ImportScope) | — | — | CD | pkg qualifier dropped inside sema; case heuristic; render_module_src omits |
| INNER_ANNOTATION | NAME | module_loader / inject_implicit_prelude_ (no_implicit_prelude only) | — | — | — | P | other names silently ignored |
| INNER_ANNOTATION | ARGS, VALUE | never | — | — | — | NR | parsed then discarded |
| ANNOTATION | (node) | collect check_annotations; lower_module_items | spread into flags | flags | flags; reflection blob | P | only at item level; unknown names ignored at lowering |
| ANNOTATION | NAME | per-consumer dispatch | known -> flags; user -> struct ANNOTATIONS | flags | blob (zoned structs only) | P | user annotations only on `#[annotation]` datatypes; dropped by mono |
| ANNOTATION | ARGS | build_annotation_instance; cfg | A_KV; SHOULD_PANIC_MSG | — | blob | P | cfg consumed; trigger args not forwarded to handler |
| ANNOTATION | VALUE | read_annotation_u64 | TYPE_CODE | — | const | P | only type_code uses `#[k=v]` |
| ANNOT_KV | NAME, VALUE | evaluate_cfg_arg; build_annotation_instance; parse_annot_literal | kv on struct ANNOTATIONS | — | blob | P | no type check despite comment; enum value unresolved; spelling normalised |
| ANNOT_POS | VALUE | build_annotation_instance | kv keyed by resolved field | — | blob | P | positional vs named lost |
| ANNOT_ARR | ITEMS | parse_annot_literal | AV_ARR | — | blob | P | no cfg case (silently false); nested render broken |
| ANNOT_CALL | NAME, ARGS | evaluate_cfg_arg | — | — | — | P | dropped outside cfg; renders `#[cfg(all)]` |
| builtin fn attrs | no_mangle, fn_macro, metaprog_handler, cfg | collect_fn; lower_fn | folded into NAME / IS_MACRO_HOOK / handler decls | — | symbol only | CD | attribute itself not kept |
| non-builtin attrs | #[inline], #[cold], #[must_use], #[deprecated] | ignored | — | — | no LLVM fn attrs | CD | no trace, no diagnostic |
| DOC_LINE_LIT | VALUE | append_doc_line; try_append_doc | DOC / F_DOC / TM_DOC / AT_DOC | — | — (emit_docs_facts pre-mono) | P | lost for variants, impls, rel, clauses, cfg-dropped; mono drops fn/struct/field docs |
| DOC_BLOCK_LIT | VALUE | append_doc_block | merged into DOC | — | — | P | block form lost; no render case |
| INNER_DOC_LIT | VALUE | module_inner_doc_ | ModuleInnerDoc | — | — | P | no reader anywhere |
| INNER_DOC_BLOCK_LIT | VALUE | append_doc_block | ModuleInnerDoc | — | — | P | no reader |
| (doc before item) | pending_doc_ | lower_module_items: INSTANTIATE_DECL, METACALL_ITEM, FN_MACRO_CALL_ITEM(_DONE), MAPPING_DEF(_DONE), CONTAINER_DEF(_DONE), DEEM_DEF branches `continue` without take_pending_doc | leaks to next item | — | — | CD | wrong doc attribution (*inferred*) |

### 2.2 Functions, params, closures

| Node | Field | Sema | LIR | BIR | MLIR/DWARF | St | Loss |
|---|---|---|---|---|---|---|---|
| FN | NAME | collect_fn; lower_fn | dk::NAME (mangled) + METHOD_BASE | Body.name | symbol; DISubprogram name = METHOD_BASE, no linkageName | CP | debuggers cannot match by mangled name |
| FN | IS_PUB | SemaFuncInfo.is_pub | dk::IS_PUB | — | only --emit-abi | CP | no linkage/DW_AT_accessibility effect |
| FN | VIS | read_module_vis -> is_module_only | — | — | — | CD | pub(module) sema-only |
| FN | IS_UNSAFE | SemaFuncInfo.is_unsafe | — | — | — | CD | `unsafe fn` lost |
| FN | TYPE_PARAMS | lower_fn; read_type_params | dk::TYPE_PARAMS (template); empty on instances | tv_bounds (pre-mono) | — | P | `impl Trait` synth params unmarked except `__impl_N` name |
| FN | PARAMS | collect_fn; lower_fn | dk::PARAMS | Param locals | args; DILocalVariable (arg) | CP | see PARAM |
| FN | PARAMS (trait sig) | trait collect | TM_NAME/TM_RET_TYPE/TM_DOC only | callee_fn (*inferred*) | — | P | trait method params not lowered |
| FN | RET_TYPE | collect_fn; lower_fn | RET_TYPE; DECL_RET_TYPE on instances | Return local | result; DISubroutineType | CP | written spelling, aliases, elided lifetimes, RPIT |
| FN | WHERE | lower_fn fold | folded into FTP_BOUNDS / LIFETIME_OUTLIVES | tv_bounds | — | P | where vs inline origin lost |
| FN | BODY | lower_block | BODY + LOCAL_COUNT | CFG | body | CP | inserted drops/temps only partly marked |
| FN | SRC_LINE (decl) | node_line_ | — | — | DISubprogram line = first body stmt, or 1 | CD | header line lost |
| Func | DOC | lower_fn | DOC | — | — | CD | not copied by clone_fn; emit_docs pre-mono only |
| Func | IS_TEST, SHOULD_PANIC, IGNORED, SHOULD_PANIC_MSG | lower_fn | flags | — | — | NR | not copied by clone_fn; no LIR reader found |
| Func | IMPL_TYPE_PARAMS, IMPL_TARGET_PATTERN | lower_impl_block | processed | — | — | CD | consumed by mono |
| Func | WHERE_TYPE_BOUNDS | trait-default synth only | (type, trait name) | read once | — | CD | trait args not stored; consumed by mono |
| Func | IS_METAPROG_STUB, IS_SPECIALIZATION, SPEC_PATTERNS | lower_fn | flags | stub read | — | CD | not copied by clone_fn |
| Func | IS_MACRO_HOOK, UNIT_KEY, IS_EXTERN, IS_VARARG, LOCAL_COUNT | lower_fn | flags | local_count | extern/vararg | CP | — |
| Func | LIFETIME_PARAMS/OUTLIVES, DECL_RET_TYPE, P_DECL_TYPE | lower_fn; clone_fn | kept | yes | — | CP | analysis only |
| EXTERN_FN | NAME | collect | NAME + IS_EXTERN | callee | private external FuncOp | CP | — |
| EXTERN_FN | PARAMS/RET_TYPE | as FN | as FN | signature | func type | CP | written spelling |
| EXTERN_FN | IS_VARARG | lower_fn | IS_VARARG | — | variadic type | CP | — |
| EXTERN_FN | VALUE (ABI) | validate_abi | — | — | default cconv | CD | C-unwind/system/Rust compile the same as C; unwind lost |
| EXTERN_BLOCK | VALUE (ABI) | validate_abi | — | — | — | CD | ABI discarded; `unsafe extern` not in the AST |
| EXTERN_BLOCK | ITEMS | flatten | per-child decls | — | external decls | P | grouping lost; outer attr lands on first child (*inferred*) |
| STATIC_FN | (node) | = FN | = FN, no STATIC flag | Body | not nested in type scope | P | `static` keyword lost |
| STATIC_FN | NAME / IS_PUB / sig | collect_fn | as FN | as FN | as FN | CP | — |
| STATIC_FN | VIS / IS_UNSAFE | read_module_vis; is_unsafe | — | — | — | CD | sema-only |
| NESTED_FN | (node), NAME | lower_nested_fn -> closure | SLet NAME = closure | local | gensym closure fn; no DWARF | P | fn-item nature lost; captures probably accepted (E0434 missing), no self-recursion (*inferred*) |
| NESTED_FN | PARAMS, RET_TYPE, BODY | lower_closure_expr | closure mirror | own Body | yes | CP | no render case |
| PARAM | NAME | lower_fn | P_NAME; `__mutparam_*` / `__pat_param_*` renames | Param local | DWARF arg = synth name | CP | synth-to-prologue-let link lost |
| PARAM | TYPE | collect_fn read_param_types | P_TYPE; P_DECL_TYPE | type | arg type | P | DEFECT: `ref x: T` checks IS_REF first and types it `&Self` (gap N3) |
| PARAM | IS_MUT | lower_fn | becomes prologue SLet IS_MUT / MutRef | prologue local | alloca | CP | param itself recorded immutable |
| PARAM | IS_REF | collect_fn | folded into P_TYPE | type | type | P | `&self` shorthand vs written not recorded |
| PARAM | IS_VARIADIC | lower_fn | P_IS_VARIADIC -> expanded by mono | expanded | expanded | CP | pack origin lost |
| PARAM | PAT | bind_param_pattern | synth param + `let PAT` | locals | locals | CP | pattern not on the param |
| PARAM | IMPLIED_SELF | refuse_misplaced_implied_self_ | — | — | — | CD | validation only |
| CLOSURE_EXPR | (node) | lower_closure_expr | EClosure (CL_NAME `__closure_N`, captures, modes) | own Body | own fn; no DWARF (DebugScopeSuspend) | P | no debug info; counter identity, no location |
| CLOSURE_EXPR | IS_MOVE | lower_closure_expr | CL_IS_MOVE | capture_mode | env by value | CP | — |
| CLOSURE_EXPR | PARAMS | lower_closure_expr | PARAM_NAMES/TYPES; pattern/ref/mut desugared | params | args | P | written vs hint-inferred types lost |
| CLOSURE_EXPR | RET_TYPE | lower_closure_expr | CL_RET_TYPE | return | return | P | annotated vs inferred lost |
| CLOSURE_EXPR | BODY/VALUE | lower_block | CL_BLOCK (expr body -> return) | yes | yes | CP | block vs expr body lost |
| ClosureBox | CAPTURE_MODES, CAPTURE_WIDENED, RET_TIED | capture analysis | kept | read | never | CP | analysis only |

### 2.3 Type declarations

| Node | Field | Sema | LIR | BIR | MLIR/DWARF | St | Loss |
|---|---|---|---|---|---|---|---|
| STRUCT | NAME | collect; lower_struct_def | NAME + PKG | lookup by name | LLVM struct; DICompositeType | CP | — |
| STRUCT | IS_PUB | SemaStructInfo | IS_PUB | — | — | CP | no linkage/DWARF effect |
| STRUCT | VIS | read_module_vis | — | — | — | CD | pub(module) sema-only |
| STRUCT | TYPE_PARAMS | lower_struct_def | TYPE_PARAMS + LIFETIME_* (template) | lifetimes | mangled | P | templates only |
| STRUCT | WHERE | lower_struct_def | folded into outlives/bounds | outlives | — | P | where form lost |
| STRUCT | FIELDS | collect; lower_struct_def | FIELDS | projections | body; DWARF members | CP | see FIELD_DEF |
| STRUCT | ITEMS | collect_fn | METHODS | per body | fns | CP | — |
| STRUCT | TYPE (explicit inst) | lower_module_items | inst_annotations | — | instance | CP | — |
| STRUCT | SRC_LINE | diagnostics | — | — | DICompositeType line 0 | CD | decl location lost |
| Struct | DOC, F_DOC | lower_module_items; push_field | kept pre-mono | — | — | CD | clone_struct_def rebuilds fields `{name,type,false,{}}` |
| Struct | ANNOTATIONS, IS_ANNOTATION_TYPE | apply_annots_to_struct | typed instances | — | reflection_emit blob (zoned, non-generic, pre-mono) | P | dropped by mono; plain structs get nothing |
| Struct | TYPE_CODE | apply_annots_to_struct | i64 | — | dispatch uses DispatchEntry | P | SUSPECTED: dropped by clone_struct_def, so `type_code_of::<T>()` in generic code returns a hash, not N |
| Struct | TYPE_HASH | sema | raw bytes | — | recomputed | CD | read pre-mono only |
| Struct | layout flags (IS_ZONED ... NO_AUTO_DROP) | apply_struct_flags | sparse bools | some | read | CP | attribute that produced the flag is gone; IS_DATA_PLAIN unread |
| Struct | #[pinned] | SemaStructInfo.pinned | — | — | — | CD | no representation |
| FIELD_DEF | NAME | collect | F_NAME | Proj names | GEP; DW_TAG_member | CP | — |
| FIELD_DEF | TYPE | resolve_type | F_TYPE | types | member type | CP | spelling |
| FIELD_DEF | IS_PUB | SemaFieldInfo.is_pub | — | — | — | CD | emit_docs stamps struct is_pub on fields |
| FIELD_DEF | IS_VARIADIC | collect | F_IS_VARIADIC -> expanded | expanded | expanded | CP | pack origin |
| UNION_DEF | (node) | collect_struct is_union | Struct + IS_UNION | is_union_type | max-of-fields body; DWARF structure tag, sequential offsets | CP | DEFECT: union DWARF wrong; no render case |
| UNION_DEF | IS_PUB, NAME, TYPE_PARAMS | collect_struct | as STRUCT | — | — | CP | — |
| UNION_DEF | VIS | read_module_vis | — | — | — | CD | — |
| UNION_DEF | WHERE | collect_struct | lifetimes only | — | — | P | trait bounds not in struct_keys (*inferred*) |
| UNION_DEF | FIELDS | collect_struct | FIELDS | yes | yes | P | per-field pub lost |
| DATATYPE | (node), NAME, TYPE_PARAMS, IS_PUB | collect_datatype | Struct + IS_ZONED/IS_DATA_PLAIN/TYPE_HASH/TYPE_CODE | struct | struct; reflection | CP | — |
| DATATYPE | FIELDS | collect_datatype | FIELDS | yes | yes | P | field pub lost |
| DATATYPE | VIS | read_module_vis | — | — | — | CD | — |
| DATATYPE | TYPE (instantiation) | lower_module_items | InstAnnot | — | instance | CP | annotations other than type_code dropped |
| ENUM | NAME | collect_enum | NAME + PKG | types | layout; DWARF opaque + `__logos_debug_meta` JSON | CP | no DW_TAG_variant_part |
| ENUM | IS_PUB | collect_enum | IS_PUB | — | — | CP | — |
| ENUM | VIS | read_module_vis | — | — | — | CD | — |
| ENUM | TYPE (backing) | collect_enum | BACKING_TYPE | — | disc width | CP | debug-meta hard-codes disc_size 4 (*inferred*) |
| ENUM | TYPE_PARAMS | collect_enum; lower_enum_def | TP_NAME, TP_IS_VARIADIC only | lifetimes | mono | P | bounds, defaults, const-ness dropped |
| ENUM | WHERE | read_lifetime_outlives_from | — (LIFETIME_OUTLIVES never written) | — | — | CD | trait bounds unconsumed (*inferred*) |
| ENUM | ITEMS | collect_enum | VARIANTS | view | TaggedEnumInfo | P | see VARIANT_DEF |
| ENUM | doc | collect_enum | DOC | — | — | CR | only emit_docs pre-mono |
| Enum (generic inst) | DOC, BORROW_CARRYING, LIFETIME_PARAMS, TYPE_PARAMS | — | dropped by clone_enum_def | — | — | P | non-generic enums keep them |
| VARIANT_DEF | NAME | collect_enum | V_NAME | `Variant#i` | JSON only; mlir never reads VARIANT | CR | — |
| VARIANT_DEF | ITEMS / IS_VARIADIC | collect_enum | V_PAYLOAD_TYPES | yes | layout | CP | spelling |
| VARIANT_DEF | IS_STRUCT_SHAPE + field names | payload_field_names (sema) | positional only | positional | positional | P | struct-variant field names and shape lost; docs render `Foo(T,U)` |
| VARIANT_DEF | VALUE (+LO_NEG) | collect_enum | V_DISC | — | const | CP | spelling |
| VARIANT_DEF | BODY (metacall / xref) | ctfe::eval_expr | folded V_DISC | — | const | P | expression, xref, metacall provenance lost |
| VARIANT_DEF | doc | variant_sweep_doc | — (no key) | — | — | CD | emit_docs passes "" |
| VARIANT_DEF | schema_variant NAME/TYPE | schema collect | schema struct info (*inferred*) | ? | if-chain | P | not traced |
| TYPE_ALIAS | NAME | collect_type_alias | NAME, no PKG | — | type_aliases_ by bare name; no DW_TAG_typedef | CR | pkg dropped |
| TYPE_ALIAS | TYPE | collect_type_alias | TYPE_REF; generic -> error_t | — | resolved | P | generic RHS only in sema AST |
| TYPE_ALIAS | TYPE_PARAMS | read_type_params | — | — | — | CD | — |
| TYPE_ALIAS | IS_PUB / VIS | never | — | — | — | NR | — |
| TYPE_ALIAS | doc | take_pending_doc | DOC | — | — | CR | no reader |
| CONST_DEF | NAME | collect_const | NAME + PKG | frame_consts | inlined per use; no global, no DWARF | CP | invisible in debugger |
| CONST_DEF | TYPE | collect_const | TYPE_REF | — | — | CP | spelling |
| CONST_DEF | VALUE | collect_const; lower_const_def | VALUE expr | — | re-emitted per use | CP | no folded value stored |
| CONST_DEF | TYPE_PARAMS | generic_consts_ | — | — | — | CD | bounds not even read |
| CONST_DEF | IS_PUB / VIS | never | — (key exists, unset) | — | — | NR | visibility unenforced |
| CONST_DEF | doc | take_pending_doc | DOC | — | — | CR | emit_docs skips consts |
| CONST/ALIAS/ENUM | SRC_LINE + file | diagnostics | — | — | — | NR | decl position lost |
| STATIC_DEF | (node) | collect_const; module_statics_ | Const + IS_STATIC, SYM | initializer not borrow-checked (*inferred*) | llvm global + `__logos_static_init`; no DIGlobalVariable | CP | no line, no DWARF |
| STATIC_DEF | IS_PUB / VIS | never (render only) | — | — | External linkage always | NR | visibility ignored |
| STATIC_DEF | IS_MUT | module_static_muts_ | IS_MUT | — | no effect (never constant) | CP | no rodata placement |
| STATIC_DEF | NAME / TYPE / VALUE | collect_const | NAME/SYM, TYPE_REF, VALUE | — | global, runtime init | CP | spelling; never a static initializer |
| TRAIT_DEF | NAME | collect_trait | bare NAME + PKG | via bounds | vtable names | CP | readers recombine pkg::Name |
| TRAIT_DEF | IS_PUB / VIS | collect_trait | — | — | — | CD | emit_docs hardcodes true |
| TRAIT_DEF | IS_AUTO | collect_trait | IS_AUTO | — | — | CP | — |
| TRAIT_DEF | IS_UNSAFE | collect_trait | — | — | — | CD | — |
| TRAIT_DEF | TYPE_PARAMS | collect_trait | plain names | — | — | P | bounds, defaults, const, variadic, lifetime params lost |
| TRAIT_DEF | SUPERS | collect_trait | SUPERTRAITS (bare, Copy filtered) | — | vtable | P | args, identities, assoc eqs lost; no reader |
| TRAIT_DEF | ITEMS | collect_trait | METHODS{name, ret, doc}; ASSOC_TYPES | — | vtable | P | method params/generics/unsafe, assoc consts, trait DOC (key never written) lost |
| IMPL_BLOCK | NAME (trait) | collect_impl | TRAIT_NAME + CANONICAL/IDENTITY + IMPL_PKG | — | dispatch keys | CP | — |
| IMPL_BLOCK | TYPE_PARAMS | lower_impl_block | TRAIT_TYPE_ARGS / IMPL_TYPE_PARAMS | — | mangling | P | trait lifetime args not mirrored |
| IMPL_BLOCK | IMPL_TYPE_PARAMS | lower_impl_block | IMPL_TYPE_PARAMS + lifetimes | — | — | CP | bound losses (2.5) |
| IMPL_BLOCK | TYPE (target) | collect_impl | TARGET_TYPE string; TARGET_TYPEREF null if concrete | — | symbol prefix | P | concrete target only a string |
| IMPL_BLOCK | WHERE | lower_impl_block | merged into bounds | — | — | P | origin lost; projection/concrete subjects skipped |
| IMPL_BLOCK | IS_UNSAFE | collect_impl | — | — | — | CD | — |
| IMPL_BLOCK | IS_NEGATIVE | collect_impl | key read, never written | — | — | CD | DEFECT: `impl !Tr for X` exported as a positive concrete impl |
| IMPL_BLOCK | ITEMS | collect_impl; lower_impl_block | methods as Funcs; ASSOC_TYPES; ASSOC_CONSTS (i64) | Funcs | fns | P | impl-method membership, impl doc, non-int assoc consts lost |
| ASSOC_TYPE_DEF | (node) | collect_trait | ASSOC_TYPES {AT_NAME, AT_BOUNDS, AT_DOC} | — | — | P | AT_DOC unread |
| ASSOC_TYPE_DEF | NAME | collect_trait | AT_NAME | — | — | CR | — |
| ASSOC_TYPE_DEF | TYPE_PARAMS (GAT) | read_type_params | — | — | — | CD | GAT params lost |
| ASSOC_TYPE_DEF | ITEMS (bounds) | resolve_bound_trait_ | AT_BOUNDS via tbound | — | — | P | eqs, lifetimes, ?Sized, `: 'a` dropped |
| ASSOC_TYPE_DEF | TYPE (default) | resolve_type | — | — | — | CD | sema-only |
| ASSOC_TYPE_IMPL | (node) | collect_impl | Impl ASSOC_TYPES {AE_NAME, AE_TYPE} | projections | — | P | doc lost |
| ASSOC_TYPE_IMPL | NAME | collect_impl | AE_NAME | — | — | CR | — |
| ASSOC_TYPE_IMPL | TYPE | resolve_type | AE_TYPE | normalised | concrete | CP | spelling |
| ASSOC_TYPE_IMPL | TYPE_PARAMS (GAT) | read_type_params | — | — | — | CD | AE_TYPE's TypeVar binders missing |
| ASSOC_CONST_DEF | (node), NAME, TYPE | collect_trait | — (no trait key) | — | — | CD | trait const decls sema-only |
| ASSOC_CONST_DEF | VALUE (default) | default_value_ast | copied into each impl | inlined | inlined | P | default vs impl-written origin lost |
| ASSOC_CONST_IMPL | (node) | collect_impl; lower_impl_block | `T__kassoc_<n>` accessor (concrete trait impls) + ASSOC_CONSTS i64; inherent: none | accessor body | accessor; no DWARF | P | no first-class decl; doc sema-only |
| ASSOC_CONST_IMPL | NAME | collect_impl | AC_NAME; accessor name | — | symbol | CP | mangled only |
| ASSOC_CONST_IMPL | TYPE | collect_impl | accessor RET_TYPE only | — | — | P | ASSOC_CONSTS untyped |
| ASSOC_CONST_IMPL | VALUE | collect_impl; ctfe | accessor BODY; AC_VALUE `.i` | yes | yes | P | non-int values misrepresented (*inferred*); lowered repeatedly |
| ASSOC_CONST_IMPL | IS_PUB | never | accessor IS_PUB=true always | — | — | NR | privacy unenforced |
| INSTANTIATE_DECL | TYPE | lower_module_items | InstAnnot (CANONICAL_NAME, MANGLED_NAME, STRUCT_TYPE, IS_ROOT_PIN) | via mono roots | effect only | CP | CANONICAL_NAME unread |
| INSTANTIATE_DECL | IS_PUB | lower_module_items | IS_PUB_REEXPORT | — | — | CR | no reader |
| INSTANTIATE_DECL | VIS | never | — | — | — | NR | pub(crate) collapses to pub |
| INSTANTIATE_DECL | annotations / doc | pending_annots.clear() | — | — | — | CD | non-cfg attrs dropped; doc leaks |
| SCHEMA_DEF | (node) | collect_schema | plain Struct {m, z} | struct | 16-byte struct | P | is_schema / fields / keys / code live only in SemaStructInfo |
| SCHEMA_DEF | IS_PUB, NAME, TYPE_PARAMS | collect_schema | struct keys | — | — | CP | — |
| SCHEMA_DEF | VIS | read_module_vis | — | — | — | CD | — |
| SCHEMA_DEF | CODE_EXPR | ctfe | literals at use sites only | literals | literals | P | not on decl |
| SCHEMA_DEF | FIELDS | collect_schema | — | — | — | P | declared fields never reach LIR |
| SCHEMA_FIELD_DEF | NAME / VALUE (key) / IS_PUB | collect_schema | get/set calls with literal keys | — | — | CD | name/key map sema-only |
| SCHEMA_FIELD_DEF | TYPE | resolve_type | result type of get/set only | — | — | P | no decl type |
| SCHEMA_ENUM_DEF | (node) | collect_schema_enum | plain Struct | struct | struct | P | variants sema-only |
| SCHEMA_ENUM_DEF | IS_PUB, NAME, TYPE_PARAMS | collect_schema_enum | struct keys | — | — | CP | — |
| SCHEMA_ENUM_DEF | VIS / CODE_EXPR / FIELDS | collect_schema_enum | — (code literals in match desugar) | — | — | CD | names, types, category sema-only |

### 2.4 Types

| Node | Field | Sema | LIR | BIR | MLIR/DWARF | St | Loss |
|---|---|---|---|---|---|---|---|
| TYPE_REF | NAME | resolve_type | TypeRef | types | MLIR type; DWARF name = type_str | CP | aliases expanded, Self replaced; LogosType has no alias field |
| TYPE_REF | QUAL_PARTS | resolve_type: last segment only | — | — | — | NR | DEFECT: package ignored, so homonym types can mis-resolve (*inferred*) |
| PTR_TYPE | POINTEE | resolve_type | Ptr pointee | type | opaque ptr; DWARF pointee kept | CP | — |
| PTR_TYPE | MUTPTR | resolve_type | Ptr mut | type | lost | P | *const vs *mut lost in DWARF |
| PTR_TYPE | NAME (`*zoned`) | resolve_type | zoned bit | type | bridge codegen; plain DWARF ptr | CP | — |
| REF_TYPE | (node), POINTEE | resolve_type | Ref / Slice / TraitObject / DstRef | Ref kind | ptr; unnamed DW_TAG_pointer_type | CP | `&str` = `&[u8]`; & vs &mut vs * identical in DWARF |
| REF_TYPE | LIFETIME | resolve_type; ast_elided_ref_ | lifetime slot; minted `'%N` for elided | regions | erased | CP | TypeUID ignores lifetimes (*inferred* merge); written vs elided only via spelling |
| MUT_REF_TYPE | (node), POINTEE, LIFETIME | resolve_type | MutRef etc. | yes | ptr | CP | `&mut dyn Tr` interns as `&dyn Tr` (see DYN_TYPE) |
| DOUBLE_REF_TYPE / DOUBLE_REF_MUT_TYPE | POINTEE, LIFETIME | resolve_type (synth inner ref) | Ref(Ref/MutRef) | yes | ptr-ptr | CP | only `&&` token |
| PAREN_TYPE | TYPE | resolve_type | inner | inner | inner | CP | not in syntactic dump renderer |
| ARR_TYPE | (node), TYPE | resolve_type | Array{elem, size, symbolic} | type | LLVM array; DWARF opaque blob | CP | no DW_TAG_array_type |
| ARR_TYPE | SIZE | resolve_array_len | folded size + symbolic name | — | length | P | expression folded; failure falls back to length 1 |
| ARR_LEN | SIZE / NAME / OP (sizeof...) / const-expr | resolve_array_len; build_const_expr_postfix | size or arr_size_var postfix | concrete | concrete | CP | `[T; MAX]` = `[T; 16]`; renders `[T; ]` for arithmetic |
| ARR_LEN | VALUE (`Q::N`) | resolve_array_len | size or `%Q.N` postfix | concrete | concrete | CP | RENDER BUG: `[T; Self::N]` renders as `[T; N]` |
| ARR_LEN | BODY (metacall) | ctfe_eval_const | size | — | — | CD | earlier statements ignored; no round-trip |
| SLICE_TYPE | TYPE | resolve_type | Slice elem | proj | fat ptr; DWARF ptr/len | CP | `&[u8]` = `&str` |
| SLICE_TYPE | MUTPTR, RAW_PTR | resolve_type | flags | yes | fat ptr | CP | — |
| SLICE_TYPE | LIFETIME | resolve_type | lifetime string | regions | erased | CR | — |
| UNSIZED_SLICE_TYPE | TYPE | resolve_type | UnsizedSlice / Slice behind & | yes | fat ptr | CP | two spellings merge |
| TUPLE_TYPE | ITEMS | resolve_type | Tuple | Field 'i' | struct; DWARF opaque | CP | no members in DWARF |
| TUPLE_TYPE | (unit `()`) | resolve_type | Void | — | — | CD | `-> ()` = no return type |
| TUPLE_TYPE | IS_VARIADIC + NAME | resolve_type | Tuple(TypeVar) | — | expanded | P | `(A...)` = `(A,)` |
| GENERIC_INST | NAME | resolve_type_generic_inst | TypeRef (Box<dyn> -> TraitObject etc.) | types | struct; DWARF composite | CP | alias identity, smart-pointer spelling |
| GENERIC_INST | ITEMS | resolve_type_generic_inst | type_args / lifetime_args | regions | mangled | CP | defaulted = written |
| IMPL_TYPE | (node) | resolve_type; lower_fn | param: synth `__impl_N` TypeParam; ret: replaced by concrete type | concrete | concrete | P | opaque identity lost; param form only via name prefix |
| IMPL_TYPE | NAME | resolve_type | param: TB_TRAIT_NAME; ret: erased | param bound | — | P | RPIT trait lost |
| IMPL_TYPE | TYPE_PARAMS | read_trait_bound_args | param: TB_TYPE_ARGS; no assoc_eqs | — | — | P | `impl Iterator<Item=T>` eqs dropped |
| IMPL_TYPE | PARAMS/RET_TYPE (Fn sugar) | read_trait_bound_args | — | — | — | CD | Fn signature lost |
| DYN_TYPE | (node) | resolve_type | Closure (Fn family) / TraitObject / UnsizedDyn | yes | fat pair; DWARF opaque | P | impl target `$dyn$Tr` drops args (*inferred*) |
| DYN_TYPE | NAME | resolve_type | trait_name + pkg | yes | vtable key | CP | bare-name lookup: local homonym -> prelude trait |
| DYN_TYPE | LIFETIME | resolve_type | lifetime slot | yes | — | CP | `&'a` vs `+ 'b` share one slot |
| DYN_TYPE | IS_MUT | Fn-family arm only | const_val bit (Fn only) | Fn only | — | P | DEFECT: non-Fn `&mut dyn Tr` = `&dyn Tr` |
| DYN_TYPE | IS_REF | Fn-family arm only | owning kind | yes | layout | P | non-Fn decided by context, not syntax |
| DYN_TYPE | HRTB_BINDERS | never | — | — | — | NR | `for<'a>` dropped; `'a` looks free |
| DYN_TYPE | ITEMS (args, auto bounds, lifetimes) | resolve_type | type_args; Send/Sync bits; lifetime | yes | — | P | other auto traits, all auto bounds on dyn Fn, `dyn Tr<'a>` args dropped |
| DYN_TYPE | PARAMS/RET_TYPE | Fn arm | closure_params/ret | yes | sig | P | ignored silently on non-Fn names |
| AUTO_TRAIT_BOUND | NAME | resolve_type DYN arm | Send/Sync bits only | via type | — | P | other names discarded, no E0225; ignored on `dyn Fn()+Send` |
| AUTO_LIFE_BOUND | NAME | resolve_type DYN arm | lifetime slot | region | — | P | `&'a (dyn Tr + 'b)` loses 'b |
| CLOSURE_TYPE | (node) | resolve_type | Closure (FnFamily Unstated, no region) | type | fat pair; DWARF void* | P | family/owning/region absent |
| CLOSURE_TYPE | PARAMS / RET_TYPE | resolve_type | closure_params/ret | yes | sig | CP | no E0106 check here |
| FN_PTR_TYPE | (node) | resolve_type | FnPtr | regions | opaque ptr; DWARF void* | P | impl target keyed by arity only |
| FN_PTR_TYPE | VALUE (ABI) | resolve_type | struct_name (identity) | identity | no cconv | P | render drops it |
| FN_PTR_TYPE | PARAMS / RET_TYPE | resolve_type | closure_params/ret | yes | erased | CP | absent ret = `-> ()` |
| FN_PTR_TYPE | IS_UNSAFE | never | — | — | — | NR | DEFECT: `unsafe fn()` = `fn()` |
| FN_PTR_TYPE | HRTB_BINDERS | mint_fnptr_binder | minted `'%hN` in slots | tokens | — | P | written names lost; render drops `for<>` |
| TAGGED_TYPE | (node) | resolve_type | TaggedPtr | — | ptr; no DI case (*inferred*) | P | 0 corpus uses |
| TAGGED_TYPE | TYPE | resolve_type | struct_name only | — | — | P | args, pkg, TypeRef dropped |
| TAGGED_TYPE | NAME | resolve_trait | trait_name spelling | — | lookup by name (*inferred*) | CR | homonym traits ambiguous |
| ASSOC_TYPE_REF | (node) | resolve_type_assoc_ref | concrete, or AssocType{base, trait$args, name} | pre-mono | concrete | P | `<T as Tr>::X` = `T::X`; trait args packed in string |
| ASSOC_TYPE_REF | RECEIVER / FIELD / NAME / TYPE_PARAMS | resolve_type_assoc_ref | assoc_base / name / trait_name / gat_args | pre-mono | — | CP | GAT lifetime args overload lifetime_args slot |
| TYPEOF_TYPE | VALUE | resolve_type (lower_expr, result discarded) | resolved TypeRef; cfg_classes entry for containers | type | type | CD | `typeof` origin and operand lost |
| CFG_SLOT_TYPE | NAME | resolve_type_cfg_slot | CfgSlotType or eager type | type | type | CP | eager path loses config provenance |
| CFG_SLOT_TYPE | ITEMS (path) | resolve_type_cfg_slot | ad hoc `F..\x1F` string in assoc_type_name | — | — | P | path gone after mono |
| PACK_EXPAND | NAME (type position) | resolve_type | bare TypeVar | — | — | P | `T...` = `T` |

### 2.5 Generics, bounds, lifetimes

| Node | Field | Sema | LIR | BIR | MLIR/DWARF | St | Loss |
|---|---|---|---|---|---|---|---|
| TYPE_PARAM | NAME | read_type_params_from | FTP_NAME / TP_NAME / trait names | tv_bounds | mangled only | CP | no DWARF template params |
| TYPE_PARAM | IS_VARIADIC | read_type_params_from | FTP/TP_IS_VARIADIC | — | expanded | CP | lost on traits |
| TYPE_PARAM | ITEMS (bounds) | read_type_params_from; fold_where_bounds | FTP_BOUNDS (fn/struct/impl); none for enum/trait | tv_bounds | — | P | inline/where merged; projection and concrete where-subjects skipped (permissive) |
| TYPE_PARAM | TYPE (default) | read_type_params_from | FTP_DEFAULT_TYPE (fn/struct/impl) | — | — | P | trait/enum defaults lost (`Rhs = Self`); defaulted = written |
| TYPE_PARAM | TYPE (where subject `&T:` / `C::Item:`) | fold_where_bounds | on_ref_subject not serialised | — | — | P | `where &T: Tr` reads `T: Tr` (wrong) |
| TRAIT_BOUND | NAME | read_trait_bound_args; resolve_bound_trait_ | TB_TRAIT_NAME + TB_IDENTITY | tv_bounds | — | CP | canonical key not emitted; Copy filtered from SUPERTRAITS |
| TRAIT_BOUND | RELAXED | finalize_relaxed_bounds | — (no IMPLICIT_SIZED) | — | — | CD | `?Sized` lost for archive consumers (*inferred*) |
| TRAIT_BOUND | TYPE_PARAMS (incl. ASSOC_EQ_BIND, lifetimes) | read_trait_bound_args | TB_TYPE_ARGS only | — | — | P | assoc_eqs, lifetime_args dropped (impl-level eqs kept) |
| TRAIT_BOUND | PARAMS / RET_TYPE (Fn sugar) | read_trait_bound_args | — | — (CALL_MODE instead) | — | CD | bound is just `Fn` |
| TRAIT_BOUND | HRTB_BINDERS | read_trait_bound_args | TB_HRTB_BINDERS | read | — | CR | — |
| ASSOC_EQ_BIND | NAME, TYPE | read_trait_bound_args | only blanket impl PRIMARY/EXTRA_ASSOC_EQS | — | — | P | fn/method bounds, where, RPIT lose it |
| WHERE_CLAUSE | (node) | parents' WHERE slot | folded into params | FTP_BOUNDS | — | P | clause structure lost |
| WHERE_CLAUSE | ITEMS | fold_where_bounds; read_lifetime_outlives_from | FTP_BOUNDS / FTP_LIFETIME_OUTLIVES / LIFETIME_OUTLIVES | partly | — | P | skipped subjects; ref qualifier; eqs/fn/relaxed/lifetimes dropped |
| LIFETIME_PARAM | (generic param) | read_lifetime_params | LIFETIME_PARAMS (fn/struct/enum/impl) | body_.lifetime_params | — | P | trait lifetime params never arrive |
| LIFETIME_PARAM | NAME | read_lifetime_params; read_trait_bound_args | names; type lifetime_args | where present | — | P | bound and dyn lifetime args dropped |
| LIFETIME_PARAM | ITEMS (outlives) | read_lifetime_outlives_from | LIFETIME_OUTLIVES / FTP_LIFETIME_OUTLIVES | read | — | CP | enum outlives not emitted |
| LIFETIME_PARAM | (HRTB binder) | read_trait_bound_args; fn-ptr mint | TB_HRTB_BINDERS; minted names | read | — | P | dyn form dropped |
| CONST_PARAM | (node) | read_type_params | FTP_IS_CONST/FTP_CONST_TYPE (fn/struct) | ConstVar pre-mono | folded | P | lost on trait/enum |
| CONST_PARAM | NAME | read_type_params | FTP_NAME | pre-mono | — | CR | — |
| CONST_PARAM | TYPE | resolve_type | FTP_CONST_TYPE | — | — | P | absent for enum/trait |
| CONST_PARAM | IS_VARIADIC | read_type_params | FTP/TP_IS_VARIADIC | — | — | CP | trait form loses it |

### 2.6 Statements and control flow

| Node | Field | Sema | LIR | BIR | MLIR/DWARF | St | Loss |
|---|---|---|---|---|---|---|---|
| BLOCK | ITEMS | lower_block | block mirror + inserted SDrop/temps | stmts + StorageDead | inline | CP | inserted stmts only partly marked |
| BLOCK | extent | node_line_ only | — | scope = StorageDead | no DILexicalBlock | CD | shadowed locals collide in DWARF |
| BLOCK_STMT | BODY | lower_stmt -> lower_block | SBlock | scoped block | inline | CP | user vs synthesized blocks identical |
| UNSAFE_BLOCK | (node) | inside_unsafe_ toggle | plain SBlock / EBlockExpr | — | — | CD | unsafe region invisible after sema |
| UNSAFE_BLOCK | BODY | lower_block | block | yes | yes | CP | — |
| LET | NAME | lower_let | NAME + VAR_SLOT | User local | alloca + DILocalVariable | CP | — |
| LET | TYPE | lower_let | TYPE (annotation or inferred); ANNOT_LIFETIME bit | type | type | P | written vs inferred, `_` holes lost |
| LET | VALUE | lower_let | VALUE (+ `__lit_temp` split, TRANSPARENT) | assign | store | CP | — |
| LET | IS_MUT | lower_let | IS_MUT | E0384 | not in DWARF | CP | — |
| LET | IS_REF | lower_let | AddrOf value, &T type | Ref | addr | CP | = `let x = &e` |
| Let (LIR) | COMPILER_GLUE, DESTRUCTURE_TMP, ANNOT_LIFETIME | sema | sparse bools | read | never | CP | not used for DIFlagArtificial |
| LET_PAT | PAT | lower_let_pat* | TRANSPARENT SBlock: `__let_tmp_N` + per-binding SLets / SLetElse | lets | allocas; synth temps in DWARF (*inferred*) | CP | pattern unit lost; no render case |
| LET_PAT | TYPE | lower_let_pat | retyped rhs only | types | types | CD | never sets ANNOT_LIFETIME (BIR rule blind, *inferred*) |
| LET_PAT | VALUE | lower_expr | rhs | yes | yes | CP | — |
| LET_ELSE | (node) | lower_let_else_core | SLetElse {PAT, SCRUT, ELSE_DIVERGE, GUARDS} | LetElse | gen; no DWARF for bindings | CP | irrefutable `let P = e` uses same form with synth `loop {}` else |
| LET_ELSE | PAT | build_pattern | PAT + synth guards | binds | tests | CP | inner literals moved into guards |
| LET_ELSE | VALUE / BODY | lower_let_else | SCRUT / ELSE_DIVERGE | yes | yes | CP | — |
| RETURN | VALUE | lower_return | SReturn (or SBlock with `__rv`) | Return local | return | CP | = tail expr |
| TAIL_EXPR | VALUE | lower_stmt_inner | SReturn / block result / SExpr | yes | yes | CP | implicit vs explicit return lost |
| EXPR_STMT | VALUE | lower_stmt_inner | SExprStmt or TRANSPARENT temp+drop | read | discard | CP | — |
| IF | COND / THEN / ELSE | lower_if; lower_if_expr | SIf / EIfExpr | branch | cond br | CP | `else if` nests |
| IF | PAT / VALUE / GUARD (if-let) | synth_match -> lower_match | SMatch + synth `_` arm | match | switch | CP | if-let form lost |
| IF_LET_CHAIN | (node), ITEMS, THEN | synth_let_chain | nested SMatch/SIf | yes | yes | CP | chain form lost |
| IF_LET_CHAIN | ELSE | synth_let_chain | lowered once per segment | N copies | N copies | CP | N-fold duplication; possible duplicate diags (*inferred*) |
| LET_CHAIN_LET | PAT / VALUE | synth_match | arm pattern / scrut | yes | yes | CP | — |
| LET_CHAIN_LET / LET_CHAIN_COND | (node) | synth_let_chain | SMatch / SIf level | yes | yes | P | segment identity lost |
| LET_CHAIN_COND | VALUE | lower_if | SIf cond | yes | yes | CP | — |
| WHILE | (node) / COND / BODY | lower_while | SWhile + LABEL | loop blocks | cf blocks | CP | no named blocks, no DILexicalBlock |
| WHILE | PAT / VALUE / GUARD (while-let) | lower_while synth_match -> lower_loop | SLoop + SMatch + synth SBreak | yes | yes | P | while-let form lost; render drops GUARD |
| WHILE | ITEMS (let-chain) | synth_let_chain | SLoop + nested | yes | yes | P | render drops ITEMS |
| LOOP | (node) / BODY | lower_loop | SLoop (expr: EBlockExpr + break_slot read) | yes | gen_loop | CP | expr nature only by pattern |
| LABELED_LOOP | (node), LABEL | lower_stmt (pending_loop_label_) | LABEL string on inner loop | find_loop by string | loop_stack_ by string; no DILabel | CR | render doubles `'` and drops loop header (*inferred*) |
| LABELED_LOOP | BODY | lower_stmt | inner loop | yes | yes | CP | — |
| FOR | NAME | lower_for | SFor.var + VAR_SLOT | fresh local | alloca, no DbgDeclare | P | loop var invisible in debugger |
| FOR | LHS / RHS / BODY | lower_for | lo / hi / body | yes | yes | CP | var type inferred from bounds |
| FOR | INCLUSIVE | lower_for | INCLUSIVE | IGNORED (always `<`) | honoured | CP | BIR CFG does not model `..=` |
| FOR | IS_MUT | lower_for | IS_MUT | Local.is_mut | never read | CP | render drops `mut` |
| FOR_EACH | (node) | lower_for_each | ForEach (arrays/slices) or `__iter` loop+match | ForEach loop; array/slice LABEL dropped | loop | P | iterator for-provenance lost; labeled break over arrays makes BIR bail |
| FOR_EACH | NAME | lower_for_each | VAR + VAR_SLOT | local | no DWARF | CP | — |
| FOR_EACH | PAT | emit_for_pattern_destructure | `__fe_pat_N` + SLets | lets | lets | P | synth var unflagged |
| FOR_EACH | ITER / BODY | lower_for_each | ITER (coerced) / BODY | yes | yes | CP | coercion unmarked |
| BREAK | (node) / VALUE | lower_stmt_inner | SBreak{value, label, LINE} | break slot | gen_break | CP | — |
| BREAK / CONTINUE | LABEL | lower_stmt_inner | raw label string | find_loop | unknown label -> innermost loop silently | CR | render `''a` (*inferred*) |
| BREAK_EXPR / CONTINUE_EXPR / RETURN_EXPR | VALUE, LABEL | lower_expr | EBlockExpr{SBreak/SContinue/SReturn} + never-typed dummy | yes | cf.br / return | CP | expr vs stmt form lost; no render case |
| MATCH | (node) | lower_match / lower_match_expr | SMatch / EMatchExpr; schema-enum -> if-chain; Writ/str -> hoisted let + guards | lower_match | gen_match | P | synth guards indistinguishable |
| MATCH | VALUE | lower_match | SCRUT | place | scrut | CP | — |
| MATCH | ITEMS | lower_match | ARMS; or-pattern fan-out 1 -> N | arms | arms | P | arm link to AST lost; bodies duplicated |
| MATCH_ARM | LHS / GUARD / BODY | lower_match | ARM_PAT / ARM_GUARD / ARM_BODY | yes | yes | CP | user vs synth guard boundary (*inferred*) |
| MATCH_ARM | EXPR | lower_match | ARM_VALUE or 1-stmt block with LINE = stale node_line_ | yes | yes | P | likely wrong line (*inferred*) |
| MATCH_ARM | SRC_LINE | never | — | — | — | NR | root of stale arm lines |
| ASSIGN | (node) | lower_assign | SAssign{NAME, VALUE, DROP_OLD}; static -> SDerefWrite(`__static_addr`) | write_place | store | CP | static form rewritten |
| ASSIGN | NAME | lower_assign | raw name, no VAR_SLOT | named_place by name | scope by name | CR | resolved binding dropped |
| ASSIGN | VALUE | lower_assign_to | VALUE | rvalue | store | CP | — |
| PLACE_ASSIGN | RECEIVER | lower_place_assign | SDerefWrite(AddrOfTemp(place, Desugar)) / IndexMut call / schema write | synth &mut + write | GEP+store | CP | "place = rvalue" identity gone; spurious loans possible (*inferred*) |
| PLACE_ASSIGN | VALUE | lower_place_assign | value | yes | yes | CP | — |
| *Write stmts (LIR) | RECEIVER string, FIELD, MID_FIELD, EXTRA_MIDS | sema split by shape | raw names | rebuilt Place | read | CR | LHS not an expr tree |
| DEREF_WRITE | (node) / NAME (ptr expr) / VALUE | lower_stmt_inner | SDerefWrite{PTR, VALUE, DROP_OLD} | write_place | store | CP | unsafe/raw context not recorded; key misnamed |
| COMPOUND_ASSIGN | (node) | lower_compound_assign | `x = x op y` / op_assign call / double-evaluated place | desugared | load/op/store | P | op= provenance lost; place side effects run twice |
| COMPOUND_ASSIGN | RECEIVER / VALUE | lower_compound_assign | NAME or place (twice) / rhs | yes | yes | CP | — |
| COMPOUND_ASSIGN | OP | lower_compound_assign | base op or method symbol | yes | yes | P | `+=` token lost |
| DEREF_COMPOUND | NAME / OP / VALUE | lower_stmt_inner | SDerefWrite(ptr, BinOp(Deref ptr, rhs)) | read+write | load/op/store | CP | place lowered twice unless call |
| DESTRUCTURE_ASSIGN | OP, NAMES, FIELDS, VALUE | lower_destructure_assign | TRANSPARENT SBlock{`__da_N` (DESTRUCTURE_TMP), spills unflagged, SAssign} | yes | stores | P | literals silently `_`; nested field sub-pattern writes var named after field (*inferred*); no render |
| DESTRUCTURE_ASSIGN | NAME (struct path) | never | — | — | — | NR | wrong struct name accepted (*inferred*) |
| Drop (LIR) | NAME, DROP_FN, DROP_FIELDS, MOVED_FIELDS | sema synth | kept | read | read | CP | no compiler-inserted flag |
| Block (LIR) | TRANSPARENT | sema | flag | read | scope restore | CP | — |
| Block (LIR) | EXPORT_ID | emit_module stamp | u24 | — | — | CP | infra only |

### 2.7 Expressions

| Node | Field | Sema | LIR | BIR | MLIR/DWARF | St | Loss |
|---|---|---|---|---|---|---|---|
| (all LExpr) | TYPE | type check | expr_common::TYPE | Local.type | logos_to_mlir | CP | written spelling; annotated vs inferred |
| CALL | CALLEE | lower_call | ECall CALLEE = mangled symbol, or MethodCall/EnumLitData/ClosureCall/StructLit | Term.callee string, re-looked-up | direct call | CP | identity = spelling; ctor/variant/intrinsic origin lost |
| CALL | ARGS | lower_call | ARGS + AddrOf/Cast/ClosureBox wrappers | operands | operands | CP | only BorrowOrigin marks implicit coercion |
| CALL | RECEIVER + QUAL_PARTS | extract_pkg_qualifier | consumed into symbol | — | — | CD | written path lost |
| GENERIC_CALL | CALLEE / ARGS | lower_generic_call | ECall symbol (mangled after mono) | call | call | CP | prelude/tuple-struct ctor origin lost |
| GENERIC_CALL | TYPE_PARAMS | lower_generic_call | TYPE_ARGS | — | mangled | CP | explicit = inferred |
| GENERIC_CALL | RECEIVER + QUAL_PARTS | extract_pkg_qualifier | — | — | — | CD | — |
| GENERIC_REF | CALLEE / TYPE_PARAMS | lower_generic_ref | EGenericRef -> VarRef(mangled) by mono | ignored | fn address | CP | base name/args only in mangle; no render case |
| STATIC_CALL | RECEIVER / NAME / ARGS | lower_static_call | mangled CALLEE or MethodCall/EnumLitData | callee | call | CP | type path and `Self` lost |
| STATIC_CALL | TYPE_PARAMS | lower_static_call | TYPE_ARGS / mangled class | — | mangled | P | `T::<X>::m` = `T::m::<X>` already in AST |
| STATIC_CALL | TYPE (`<T as Tr>::m`) | lower_static_call (tq.NAME only) | chosen symbol | — | — | P | trait args ignored; silent fallback to plain resolution |
| METHOD_CALL | (node) | lower_method_call + try_* | EMethodCall / ECall / intrinsic | Term::Call | direct or vtable call | P | method syntax lost on ECall/intrinsic paths |
| METHOD_CALL | RECEIVER | lower_method_call | receiver; autoref AddrOf(Autoref) | two-phase | yes | CP | autoderef unmarked |
| METHOD_CALL | NAME | lower_method_call | METHOD + RESOLVED_SYMBOL | callee | in mangle | P | spelling only with EMethodCall |
| METHOD_CALL | TYPE_PARAMS | lower_method_call | merged into TYPE_ARGS | — | — | P | turbofish vs inferred lost |
| METHOD_CALL | ARGS | finish_generic_call | ARGS | args | operands | CP | — |
| MethodCall (LIR) | VTABLE_INDEX, TAG_SYSTEM, TAG_TRAIT, RESOLVED_TYPE | sema | kept | partial | read | CP | UFCS vs dot lost |
| INVOKE_EXPR | RECEIVER / ARGS | lower_invoke_on | EClosureCall(CallMode) / EFnPtrCall | call | indirect call | CP | Box deref step unmarked |
| ClosureCall (LIR) | CALL_MODE | sema | u8 | read | never | CP | analysis only |
| BINOP | OP | lower_binop | EBinOp raw op; overloaded -> ECall; `!=` -> `!eq`; ord -> partial_cmp+is_lt; mono rewrites struct operands | Rvalue::Op | arith or call | P | operator form lost when overloaded |
| BINOP | LHS / RHS | lower_binop | operands | operands | operands | CP | — |
| UNARY | OP | lower_unary | EUnary / ECall(neg,not) / AddrOf(Explicit) / nested AddrOfTemp for `&&` | Op / Ref | arith / addr | P | overloaded form lost; `&&` split |
| UNARY | VALUE | lower_unary | operand | yes | yes | CP | — |
| CHAINED_CMP | ITEMS | never (diagnostic only) | error_expr | — | — | NR | fix-it cannot name operators |
| VAR_REF | NAME | lower_var_ref | EVarRef NAME + VAR_SLOT / const-param / fn symbol / EnumLit | Place or Const | load | CP | fn-item/variant name replaced by symbol |
| FIELD_READ | (node) | lower_field_read_impl | EFieldRead; EBlockExpr `__rtmp_N`; autoderef EDeref | Proj::Field | GEP; name only in DWARF members | CP | autoderef unmarked |
| FIELD_READ | RECEIVER | lower_field_read_impl | RECEIVER | base | base | CP | parens dropped |
| FIELD_READ | FIELD | lower_field_read_impl | field name string | Proj name | GEP index | CR | no field index/decl handle |
| TUPLE_INDEX | RECEIVER | lower_expr | TupleIndex + EDeref | place | GEP | CP | autoderef unmarked |
| TUPLE_INDEX | FIELD | parse_int_literal | TUPLE_INDEX_VAL; FieldRead '0' on tuple struct | Field 'N' | GEP | CP | tuple-index form lost on tuple structs |
| INDEX_READ | (node) | lower_index_read | EIndexRead / ESliceIndex / Deref(index call) / slice_get_range | Proj::Index + fake borrow | GEP+check / call | P | Index-trait and range forms become calls; synth bounds look like literals |
| INDEX_READ | RECEIVER / VALUE | lower_index_read | operands | yes | yes | CP | — |
| DEREF | (node) / VALUE | lower_deref | EDeref / deref() call / `__static_addr` | Proj::Deref | load | CP | user `*` = autoderef |
| ADDR_OF_MUT | (node) | lower_expr | EAddrOf/EAddrOfTemp(Explicit); static -> VarRef; `&mut *Box<dyn>` -> retype; DerefMut/IndexMut calls | borrow_origin | ptr; DWARF = & | P | explicit-borrow fact lost on 4 shapes |
| ADDR_OF_MUT | VALUE | lower_mut_place | borrowed place | Place | addr | CP | auto-deref paths unmarked |
| AddrOf (LIR) | BORROW_ORIGIN | sema | u8 enum | read | never | CP | only expr provenance marker |
| PAREN_EXPR | (node) | lower_expr passthrough | — | — | — | CD | grouping gone (render keeps it) |
| PAREN_EXPR | VALUE | lower_expr | inner | yes | yes | CP | — |
| CAST | (node) / VALUE | lower_cast | ECast; identity/unsize returns inner | Op cast | cast / writ builder | P | written `as T` vanishes when identity/unsize |
| CAST | TYPE | resolve_type | ECast TYPE | dest local | target | CP | spelling |
| TRY_EXPR | (node) | lower_expr | always desugared: match + early return; Box<dyn>/other: render + reparse | match | match; ETry codegen dead | CD | `?` provenance lost; dead Code::Try still walked by BIR, mono, region_infer, mlir_gen |
| TRY_EXPR | VALUE | lower_expr / render_expr_src | scrutinee (fresh reparsed AST on 2 routes) | yes | yes | P | source node identity lost on reparse |
| RANGE_EXPR | (node) | lower_expr; lower_index | ECall range_i32/range_i64/range_incl_of; index: slice_get_range | call | call | P | no range node; element type forced i32/i64 (*inferred*) |
| RANGE_EXPR | LHS / RHS | lower_expr | args; missing -> error_expr lit 0 / INT64_MAX | args | args | P | half-open value-position ranges accepted silently (*inferred*) |
| RANGE_EXPR | INCLUSIVE | lower_expr | ctor choice or `hi+1` | — | — | CP | never a flag |
| STRUCT_LIT | NAME | lower_struct_lit | STRUCT_NAME + TYPE | Aggregate | struct | CP | `Self` = concrete name |
| STRUCT_LIT | TYPE_PARAMS | lower_struct_lit | folded into TYPE | type | type | CP | written vs inferred |
| STRUCT_LIT | ITEMS | lower_struct_lit | FIELD_NAMES/VALUES (source order) | Aggregate | insertvalue | CP | — |
| STRUCT_LIT | BASE | lower_struct_lit | one FieldRead of base per missing field | reads | loads | CP | `..base` provenance lost |
| FIELD_INIT | NAME / VALUE | lower_struct_lit | FIELD_NAMES[i] / FIELD_VALUES[i] | Aggregate | values | CP | = shorthand |
| FIELD_SHORTHAND | (node) | lower_struct_lit; lower_enum_lit_data | EVarRef(name) with NO slot | operand | load | P | shorthand origin lost; no VAR_SLOT (shadowing risk, *inferred*) |
| FIELD_SHORTHAND | NAME | lookup | field + VarRef NAME | yes | yes | CP | — |
| TUPLE_LIT | ITEMS | lower_expr | TupleLit | Aggregate | struct | CP | silent i32 -> i64 upgrade |
| TUPLE_LIT | (unit) | lower_expr | TupleLit{} Void | — | — | CP | — |
| ARR_LIT | ITEMS | lower_arr_lit | EArrLit | Aggregate | alloca+stores | CP | — |
| ARR_FILL_LIT | (node) | lower_arr_fill_lit | unrolled N-element EArrLit (or 1 elem + arr_size_var) | N operands | splat re-guessed (arr_lit_const_splat) | CD | `[v;N]` lost; LIR O(N); N==0 emits 1 element (*inferred* bug) |
| ARR_FILL_LIT | VALUE / SIZE | lower_arr_fill_lit | N copies / folded size | yes | yes | CP | single-eval semantics path-dependent |
| ENUM_LIT | (node) | lower_enum_lit | EnumLit / assoc-const value (shared cached LExpr) / FnPtr VarRef / synth closure | Const | disc | P | `Path::Name` origin lost; one LExpr aliased across uses |
| ENUM_LIT | NAME / FIELD | lower_enum_lit | ENUM_NAME / VARIANT + DISC | — | disc | CP | alias/Self spelling; pkg not carried |
| ENUM_LIT | TYPE_PARAMS | lower_enum_lit | folded into TYPE | — | — | P | ignored when hint present (`None::<i32>` retyped, *inferred*) |
| ENUM_LIT | QUAL_PARTS | lower_enum_lit (last segment) | — | — | — | CD | DEFECT: pkg ignored in resolution |
| ENUM_LIT | trait in `<T as Tr>::X` | parser drops $4 | — | — | — | CD | qualified path read as `T::X` |
| EnumLit (LIR) | VARIANT | — | kept | read | never read (disc only) | P | variant name ends at BIR |
| ENUM_LIT_DATA | (node) / NAME / FIELD / ARGS | lower_enum_lit_data | EnumLitData{PAYLOAD} | Aggregate | tagged construction | CP | tuple forms unreachable from source (*inferred*) |
| ENUM_LIT_DATA | TYPE_PARAMS | never (real enum path) | — | — | — | NR | STATIC_CALL route reads it: routes disagree |
| ENUM_LIT_DATA | IS_STRUCT_SHAPE / ITEMS | payload_field_names | positional PAYLOAD, declaration order | no field_names | positional | P | side-effect order may be declaration order (*inferred*) |
| OFFSET_OF | (node), TYPE, NAME | lower_offset_of | ELitInt | const | const | CD | folded pre-mono from unsubstituted layout (*inferred* risk) |
| SIZEOF_PACK | OP | lower_expr | — | — | — | CD | keyword gate |
| SIZEOF_PACK | NAME | lower_expr | `__sizeof_pack__::<T>()` -> lit by mono | const | const | CP | any type param accepted (*inferred*) |
| PACK_EXPAND | (expr) NAME | lower_expr | EPackExpand -> expanded by mono | pre-mono unsupported | bug if reached | P | expansion provenance lost |
| LIST_COMP | (node) | lower_list_comp | block{SLet `__lc_v_N`; ForEach; SIf; Vec__push(Desugar)} | lets/loop | loop; `__lc_v_N` in DWARF | P | no comprehension marker; array/slice only |
| LIST_COMP | VALUE / NAME / ITER | lower_list_comp | push arg / ForEach var / iter | yes | var no DWARF | CP | literal not coerced (*inferred*) |
| LIST_COMP | GUARD | lower_list_comp (no bool check) | SIf cond | yes | yes | CP | non-bool guard may reach cond_br (*inferred*) |
| MAP_COMP | (node) | lower_map_comp | block{`__mc_m_N`; ForEach; HashMap__insert typed void} | yes | `__mc_m_N` in DWARF | P | insert result typed void; no IntLit defaulting |
| MAP_COMP | KEY / VALUE / NAME / ITER / GUARD | lower_map_comp | args / var / iter / cond | yes | yes | CP | no bool check on guard |
| FormatCall / PtrArith / SizeOf / LitX (LIR) | all keys | sema | kept | read | read | CP | fmt spec is an expr, not structured |
| GenericRef / PackExpand (LIR) | CALLEE, NAME | sema | pre-mono forms | — | resolved | CD | provenance lost at mono |

### 2.8 Literals

| Node | Field | Sema | LIR | BIR | MLIR/DWARF | St | Loss |
|---|---|---|---|---|---|---|---|
| LIT_INT | VALUE | lower_int_lit | LIT_I64 (+HI); suffix in TYPE | Const, no value | IntegerAttr | CP | radix, `_`, suffix-vs-inferred; unary minus folded |
| LIT_INT | LO_NEG | type-arg resolve | folded into type const | — | — | CP | sign folded |
| LIT_FLOAT | (node) | lower_expr | ELitFloat LIT_F64 | Const | float const | CP | — |
| LIT_FLOAT | VALUE | parse_float_literal | double | value | value | P | spelling; f32 double rounding (recorded, not fixed) |
| LIT_BOOL | VALUE | lower_expr | LIT_BOOL | Const, no value | i1 | CP | — |
| LIT_STR | VALUE | lower_expr; lower_cstr_lit | raw token text; c"" -> `__cstr_from_lit` call | Const | decoded in decode_str_lit_ | CR | best-preserved literal; c-string provenance lost |
| LIT_CHAR | VALUE | lower_char_lit | ELitInt(Char) | Const | i32; DW_ATE_UTF | CP | escape spelling |
| LIT_BYTES | VALUE | lower_bytes_lit | EArrLit of N u8 | Aggregate | element-wise (*inferred*) | P | typed `[u8;N]` value, not `&'static [u8;N]`; hand-copied decoder |
| LIT_WSTATIC | VALUE | resolve_wstatic_value; lower_writ_lit | WStaticLit hash; wstatic_registry_ + wstatic_sources | type id | `@hs_<hex>`; rodata | CP | first-write-wins site |

### 2.9 Patterns

| Node | Field | Sema | LIR | BIR | MLIR/DWARF | St | Loss |
|---|---|---|---|---|---|---|---|
| (all patterns) | SRC_LINE | never | — | — | — | NR | patterns have no location in LIR |
| PAT_WILD | (node) | build_pattern_impl fallback | Wild / Variant / Int/Bool (const) / synth guard / RefBind | bind_to | scope; no DILocalVariable | P | no BIND_TYPE; const identity lost; synth binders unflagged |
| PAT_WILD | NAME | build_pattern_impl | NAME | Local.name | no DWARF | CP | replaced if resolves to variant/const |
| PAT_WILD | IS_MUT | pat_byval_mut | IS_MUT | is_mut | slot | CP | — |
| PAT_WILD | IS_REF | build_pattern_impl | RefBind | ref mode | addr | CP | written vs default-mode ref lost outside VariantData |
| PAT_VARIANT | (node), NAME, FIELD | build_pattern_variant | Variant{ENUM_NAME, VARIANT, DISC} | examined place; no switch constants | disc compare | CP | pkg identity, alias/prelude spelling lost |
| PAT_VARIANT_DATA | (node) | build_pattern_variant_data | VariantData{BINDINGS, TYPES, SLOTS, REF_MODES}, no sub-patterns | Field 'Variant#i' | disc + loads | P | nested tree flattened into synth binders + guards/lets |
| PAT_VARIANT_DATA | NAME / FIELD | build_pattern_variant_data | ENUM_NAME / VARIANT / DISC | proj | disc | CP | spelling, pkg |
| PAT_VARIANT_DATA | ARGS | build_pattern_variant_data | leaf binders + ref modes | locals | no DWARF | P | non-leaf desugared |
| PAT_VARIANT_DATA | IS_STRUCT_SHAPE / ITEMS | payload_field_names | positional | positional | positional | P | field names, `..` lost |
| VariantData (LIR) | BINDING_REF_MODES | match ergonomics | u32 per binding | read | never (re-derived) | P | — |
| PAT_INT | VALUE | parse_int_literal | Int i64 (top); nested -> `__refut_*` guard | examined; no value | icmp | P | suffix ignored; i128/u128 wrap; u64 > i64::MAX negative (*inferred*) |
| PAT_NEG_INT | (node), VALUE | build_pattern_impl | Int (negated) / guard | examined | icmp | P | spelling; >i64 truncated; no render case |
| PAT_BOOL | VALUE | build_pattern_impl (u8) / synth (i32) | Bool / guard | examined | i1 | CP | same key read as u8 and i32 |
| PAT_CHAR | VALUE | decode_char_lit_ | PatInt | int switch | icmp | P | char-ness lost; accepted on integer scrutinee (E0308 missing) |
| PAT_CHAR_RANGE | LHS, RHS | decode_char_lit_ | PatRange | range | icmp | P | as PAT_CHAR |
| PAT_STR | VALUE | lower_match is_str_arm | `__smatch_N` + Wild + str_eq guard (no 'static) | guard call | call | P | exhaustiveness sees a guarded wildcard |
| PAT_FLOAT | VALUE | never (error only) | Wild placeholder | — | — | NR | diagnostic only |
| PAT_BYTES | VALUE | build_pattern_bytes | PatSlice of PatInt | tests | byte chain | CP | literal form lost; `&[u8]` scrutinee unsupported |
| PAT_OR | (node) | build_pattern_or | collapse if 1 alt; PatOr SUBS; payload/at fan-out into arms | Or with bindings -> unsupported (fn skipped) | OR chain | CP | parens and leading `|` unrecoverable; BIR coverage hole |
| PAT_OR | ITEMS | build_pattern_or | SUBS | tested places | tests | CP | — |
| PAT_TUPLE | (node), ITEMS | build_pattern_impl | PatTuple; `..` -> `_` skips; str element -> `__tstr_` guard | Field(i) | tests; no DWARF | CP | literal and rest position lost |
| PAT_TUPLE | NAMES | lower_let / bind_list | temp + TupleIndex + SLets | lets | lets with DWARF | P | shape lost; renders `()` |
| PAT_UNIT | (node) | fallback wildcard | Wild | — | — | CD | no `()` type check (*inferred*) |
| PAT_STRUCT | (node) | build_pattern_impl | PatStruct{STRUCT_NAME, FIELDS, HAS_REST} | Field(name) | tests | CP | render iterates wrapper wrongly (*inferred*) |
| PAT_STRUCT | NAME | find_struct_by_name; alias_find | resolved name string | — | layout | P | alias spelling; bare name, no DefId |
| PAT_STRUCT | ITEMS | build_pattern_impl | FIELDS (+HAS_REST) | proj | binds | CP | — |
| PAT_FIELD | (node) | build_pattern_impl; variant; let; destructure | PatFieldBinding / positional / SLets / SAssigns | Field proj | GEP by name; match binds no DWARF, `__dst_N` has DWARF | P | syntax lost outside match |
| PAT_FIELD | NAME | build_pattern_impl | field_name string | Proj name | GEP | CR | no field index; variant form -> position |
| PAT_FIELD | VALUE | build_pattern_impl | SUB[0]; literal -> synth + guard | yes | yes | P | literal sub-pattern loses pattern-ness |
| PAT_FIELD | IS_REF / IS_MUT | build_pattern_impl | synth PatRefBind / IS_MUT | yes | yes | CP | render drops both: `S{ref x}` -> `S{x}` |
| PAT_REST | struct | build_pattern_impl | HAS_REST | never | never | CR | no reader; possible missing E0027 in let path (*inferred*) |
| PAT_REST | tuple / variant args / let-tuple | build_pattern_impl | N synth `_` skips | as wildcards | as wildcards | CD | `(a,..,c)` = `(a,_,_,c)` |
| PAT_REST | slice | build_pattern_impl | PatSlice REST | walked | sge + subslice | CP | writ-array `..` -> guard |
| PAT_REST | NAME | build_pattern_impl | Wild NAME in REST | binding | no DWARF | CP | render drops `xs @` |
| PAT_REST | IS_REF | Rust 2024 check only | — | — | — | CD | written `ref` lost |
| PAT_REST | IS_MUT | never | — | — | — | NR | `mut xs @ ..` emitted immutable |
| PAT_SLICE | (node), ITEMS | build_pattern_impl | PatSlice{PREFIX, REST, SUFFIX} | slice rest/suffix -> unsupported | tests | CP | no render case; BIR coverage hole |
| PAT_RANGE | (node) | build_pattern_impl | PatRange LO/HI (+HI halves) closed | examined | two icmps | P | written form unrecoverable |
| PAT_RANGE | LHS / RHS / LO_NEG / HI_NEG | read_bound | LO / HI (folded, clamped) | — | consts | CP | render passes token through map_of (*inferred* garbage) |
| PAT_RANGE | INCLUSIVE | build_pattern_impl | — (hi-1) | — | — | CD | `a..b` = `a..=b-1`; render always `..=` |
| PAT_AT | NAME / VALUE / IS_REF / IS_MUT | build_pattern_impl | PatAt{NAME, SLOT, TYPE, SUB, AT_REF_MODE} | bind_to | bind; no DWARF | CP | or-sub fanned out; no render case |
| PAT_REF | VALUE / IS_MUT | build_pattern_impl | PatRefPat{INNER, IS_MUT} | Deref proj | deref | CP | `&&p` token fusion |

### 2.10 Writ literals, types, patterns, comprehensions

| Node | Field | Sema | LIR | BIR | MLIR/DWARF | St | Loss |
|---|---|---|---|---|---|---|---|
| WRIT_MAP | ITEMS | lower_writ_val; resolve_wstatic_value | WVMap entries | Use / Aggregate | `__writ_lit_N` rodata | CP | no dup-key check in expression position; hash over raw spellings |
| WRIT_ARRAY | ITEMS | lower_writ_val | WVArray | Use / Aggregate | ObjectArray | CP | — |
| WRIT_STR | VALUE | lower_writ_val | WVStr cooked | const | varchar | P | `\x`/`\u` not decoded; `@r"..."` stores `r"..."` (*inferred*) |
| WRIT_INT | VALUE | lower_writ_val (stoull) | WVInt i64 | const | blob int | P | suffix dropped; `_`, `0o` misparsed; >u64 throws (*inferred*) |
| WRIT_NEG_INT | VALUE | lower_writ_val (-stoll) | WVInt | const | blob | P | `@-0x10` -> 0; i64::MIN throws (*inferred*) |
| WRIT_FLOAT | VALUE | parse_float_literal | WVFloat double | const | F64 | P | f32 intent lost |
| WRIT_BOOL / WRIT_NULL | VALUE | lower_writ_val | WVBool / WVNull | const | blob | CP | — |
| WRIT_ENTRY | KEY | lower_writ_val | WVMapEntry key (str / i64) | const | map key | P | int key stoll issues; dup check on raw text |
| WRIT_ENTRY | VALUE | lower_writ_val | val | const | blob | CP | — |
| WRIT_ENTRY | LO_NEG | lower_writ_val | folded negative | const | blob | CP | render drops it: `@{-1:x}` -> `@{1:x}` |
| WRIT_TYPED_ARRAY | TYPE | lower_writ_val table | elem_type string | const | typed writer | CR | not a TypeRef; no render case |
| WRIT_TYPED_ARRAY | ITEMS | lower_writ_val | elements | const | raw values | P | only I32 range-checked |
| WRIT_TYPED_MAP | TYPE | lower_writ_val table | key_type; Varchar -> "" | const | map blob | P | written annotation lost; no render |
| WRIT_TYPED_MAP | RET_TYPE | validated only | — | — | — | CD | — |
| WRIT_TYPED_MAP | ITEMS | lower_writ_val | entries | const | blob | CP | no dup check |
| WRIT_CAP_IDENT | NAME | lower_writ_val | WVCapture + capture var_ref | Aggregate("writ"); builder callee unmodelled | patched | CP | dedup merges occurrences; hash ignores name |
| WRIT_CAP_EXPR | VALUE | lower_writ_val | WVCapture + expr | operand | patched | CP | — |
| WRIT_ARR_TYPE | TYPE | resolve_type | WritArr<prim>, consumed by cast into builder call | Rc<Writ> | builder call | CD | other type positions unverified |
| WRIT_MAP_TYPE | TYPE / RET_TYPE | resolve_type | WritMap, consumed by cast | — | builder call | CD | Varchar refused here but accepted in literal |
| WRIT_TYPE_LIT | TYPE | lower_writ_val; resolve_wstatic_value | WVType{kind, uid64, name} | blob | blob | CP | type reduced to (kind, uid, name) |
| WRIT_BLOB | (node) | lower_writ_blob (overwritten METACALL node) | EWritLit static_blob or re-lowered fragment | Use / code | rodata | CP | originating metacall erased |
| WRIT_BLOB | VALUE | lower_writ_blob | raw bytes | const | rodata | CR | only expr fragment roots |
| WRIT_LIST_COMP | (node) | lower_writ_list_comp | `__hlc_c_N` + ForEach + push(coerce) | calls | `__hlc_c_N` in DWARF | P | no marker |
| WRIT_LIST_COMP | VALUE / NAME / ITER / GUARD | lower_writ_list_comp | push arg / var / iter / cond (bool checked) | yes | yes | CP | — |
| WRIT_MAP_COMP | (node) | lower_writ_map_comp | `__hmc_c_N` + put | calls | calls | P | entries beyond slot hint silently dropped (documented v1) |
| WRIT_MAP_COMP | KEY / VALUE / NAME / ITER / GUARD | lower_writ_map_comp | args / var / iter / cond | yes | yes | CP | str keys only |
| PAT_WRIT_NULL | (node) | build_writ_pat_guard | Wild + `writ_pat_is_null` guard | guard call | call | P | pattern form lost; no render case |
| PAT_WRIT_BOOL / STR | VALUE | build_leaf | guard call arg | const | const | CP | — |
| PAT_WRIT_INT | VALUE / LO_NEG | build_leaf | i32 arg to eq_i24 | const | const | CP | spelling; i24 range |
| PAT_WRIT_MAP | ITEMS | build_rec | is_map + hoisted `__hp_N` slots + guards | lets+calls | calls; `__hp_N` DWARF (*inferred*) | P | lookups hoisted for every arm (*inferred*) |
| PAT_WRIT_MAP | (empty) | build_rec | is_map guard | call | call | CP | — |
| PAT_WRIT_MAP_ENTRY | KEY | build_rec | lit_str arg | const | string | CP | — |
| PAT_WRIT_MAP_ENTRY | VALUE | build_rec | sub-guard | calls | calls | P | — |
| PAT_WRIT_ARR | ITEMS | build_rec | len_eq/len_ge + slots + guards | lets | calls | P | rest position only as eq/ge |
| PAT_WRIT_TYPED_ARR / TYPED_MAP | TYPE | build_rec table | type_hash const | const | const | CP | never resolved as a type |
| PAT_WRIT_TYPED_MAP | RET_TYPE | validation | — | — | — | CD | — |

### 2.11 Metaprogramming and macros

| Node | Field | Sema | LIR | BIR | MLIR/DWARF | St | Loss |
|---|---|---|---|---|---|---|---|
| (all quotable nodes) | NAME_VAR | quote walk; main.cpp splice rewrites to NAME/FIELD/RECEIVER/VALUE before sema | blob only | — | rodata blob | CD | antiquote provenance of every generated name lost (USE, FN, PARAM, LET, CALL, VAR_REF, LIT_STR, TYPE_REF, STRUCT, FIELD_DEF, FIELD_INIT, STRUCT_LIT, FIELD_READ, ASSIGN, CONST_DEF, PAT_WILD, PAT_FIELD, TYPE_PARAM, GENERIC_INST, STATIC_CALL, TRAIT_DEF, REL_OP, REL_BIND, ENUM) |
| USE | IS_OPTIONAL | logos_emit_item_blob_subst | blob | — | — | CD | dropped-optional fact lost |
| QUOTE_ITEM | ITEMS | lower_quote_item (deep clone, NAME_VAR -> index) | writ_lit_v blob + QuoteItemBlob | const | rodata | CR | placeholder names -> indices; source file replaced at splice |
| QUOTE_ITEM | USES | lower_quote_item | merged into blob | const | rodata | CR | explicit vs inherited uses (*inferred*) |
| QUOTE_EXPR | VALUE | lower_quote_expr | template + logos_quote_expr_subst call | const+call | rodata+call | CR | quote_expr! = quote_block!; no render |
| QUOTE_TY | (node) | lower_quote_ty | StructLit Type{kind,name,size,align,uid} via intrinsics / `__type_apply__` | folded consts | consts | CP | TypeRef only in mono uid_to_type_ |
| QUOTE_TY | TYPE | lower_quote_ty | type_args of intrinsics | folded | consts | CP | spelling; composite antiquot shapes rejected; no render |
| ANTIQUOT_TYPE | NAME | lower_quote_ty | EVarRef(Type) | var read / folded | load | CP | splice marker lost |
| ANTIQUOT_PACK | NAME | lower_quote_ty; lower_generic_call | `__splicepack$<n>` TypeVar | expanded by mono | instantiation | CP | pack encoded as name prefix |
| REPEAT_GROUP | VALUE | quote walkers; outside quote: lower_expr default / lower_stmt dummy / collectors skip | inside: blob; outside: lit 0 or dropped | — | — | P | HOLE (observed): `let y = #(x)* + 7` compiles to `ret i32 7`; stmt/field/use silently dropped |
| REPEAT_GROUP | OP | splice shims | blob | — | — | CR | render: OP=2 prints `*` |
| METACALL | VALUE | lower_metacall; main.cpp overwrites CODE/VALUE in place | final: literal only | const | const | CD | callee/args/compile-time origin destroyed |
| METACALL_ITEM | VALUE / (node) | lower_metacall_item (call re-rendered as text) | MetacallSite (transient) | — | JIT only | CD | args as CTFE literals; flipped to DONE; spliced items unlinked |
| METACALL_ITEM_DONE | (node) | skipped | — | — | — | CD | tombstone |
| FN_MACRO_CALL | CALLEE | lower_fn_macro_call; lower_builtin_macro | builtins inline / reparsed; user: MetacallSite + placeholder, node overwritten | expansion | expansion | CD | no expansion backtrace; column!() = 0 |
| FN_MACRO_CALL | RAW_TEXT | lower_fn_macro_call | macro_arg_blobs (metaprog phase) | — | — | CD | delimiter kind never stored; `vec![]` renders `vec!()` (*inferred*) |
| FN_MACRO_CALL_ITEM | CALLEE / RAW_TEXT / NAME / PARAMS | lower_fn_macro_call_item | MetacallSite + arg blobs | — | — | CD | resource binding not a declaration; no render case |
| FN_MACRO_CALL_ITEM | annotations / doc | pending_annots.clear() | — | — | — | CD | attrs not forwarded; doc leaks |
| FN_MACRO_CALL_ITEM_DONE | (node) | skipped | — | — | — | CD | DEEM_DEF also flipped here, so the archive loses deem identity |
| FN_MACRO_CALL_ITEM_DONE | CALLEE, RAW_TEXT, NAME, PARAMS (+ deem fields) | never after flip | — | — | — | NR | survive only raw in archived AST |
| TEMPLATE_DECL | VALUE | never | — | — | — | NR | invisible to template_of (matches NAME; node has only VALUE) |
| MetaprogHandler / Target / MetacallSite (LIR) | TRIGGER, HOOK_FN, DEF_AST_IDX, THUNK_* | sema collect | driver records | — | — | CD | not moved by mono; DEF_AST_IDX unread |
| EmitProvenance | src_file, src_line, callee, trigger, target | main.cpp record_emit_provenance (dump mode only) | — | — | --gen-dir header comment only | P | per document, not per item; null in normal builds |
| sema_render.cpp | positions | never | — | — | — | NR | every render/reparse gets fresh positions, no source map |

### 2.12 Relational / container / deem items

| Node | Field | Sema | LIR | BIR | MLIR/DWARF | St | Loss |
|---|---|---|---|---|---|---|---|
| MAPPING_DEF | (node) | reconstruct_mapping_def; lower_mapping_def | transient MetacallSite + arg blobs + rule IR | — | `<M>__rules()` / `<M>__src()` strings | P | no item decl; no render; doc leak |
| MAPPING_DEF | IS_PUB / VIS | reconstruct_mapping_def | — | — | — | CD | registry only |
| MAPPING_DEF | NAME | reconstruct_mapping_def | blob | — | generated fn prefix | CP | — |
| MAPPING_DEF | TYPE_PARAMS | reconstruct_mapping_def | — (generic emits nothing) | — | — | P | one param + bare bound name only |
| MAPPING_DEF | PARAMS / FIELDS | reconstruct_mapping_def | text blobs | — | text in `__rules` | CR | syntactic only; rel docs dropped |
| REL_DEF | (node), NAME, PARAMS, RAW_TEXT | reconstruct_mapping_def | body_text + rule IR | — | text in `__rules` | CR | per-rel line absent (errors offset from item head) |
| REL_DEF | IS_PUB | pub_mask | blob | — | not enforced by `__mapping_item` | CD | — |
| REL_DEF / CONTAINER_DEF / REL_SIG / REL_OP / REL_BIND / DEEM_DEF | REL_KW | keyword check | — | — | — | CD | fixed keyword |
| MAPPING_DEF_DONE | (node) | pre-scan for fusion; lower skip | — | — | — | P | identity kept; doc leak |
| CONTAINER_DEF | (node) | reconstruct_container_def; lower_container_def | transient spec line + DeemPlan.class_spec | — | generated CFG const / CtrFamily / family code | P | RENDER BUG: always prints `for <TYPE>`, reparse rejects |
| CONTAINER_DEF | IS_PUB | reconstruct_container_def | spec `pub=` | — | handler-dependent | CR | — |
| CONTAINER_DEF | VIS | read_module_vis | — | — | — | CD | pub(module) lost at handler boundary |
| CONTAINER_DEF | NAME | reconstruct_container_def | blob; LProgram::cfg_classes | — | family types hash-named | P | name <-> family only in C++ side table |
| CONTAINER_DEF | TYPE_PARAMS | render_type_param_list_ | spec text | — | generated | CR | — |
| CONTAINER_DEF | TYPE | presence is an error | — | — | — | CD | retired |
| CONTAINER_DEF | FIELDS | reconstruct_container_clauses | spec segments | — | generated | CR | clause order, docs lost |
| CONTAINER_CLAUSE | REL_KW | dispatch | spec section | — | generated | CP | — |
| CONTAINER_CLAUSE | NAME / VALUE / PARAMS / ITEMS / FIELDS | reconstruct_container_clauses | spec text | — | generated | CR | column lines lost; nesting flattened |
| CONTAINER_OP | NAME / VALUE / OP_ARG | reconstruct_container_clauses | spec `ops=` | — | via Canon | CR | positions lost |
| CONTAINER_DEF_DONE | (node) | pre-scans; lower skip | — | — | — | P | render bug and doc leak |
| REL_SIG | (node), NAME, PARAMS | collect_trait -> trait_rels_ | — | — | natspec text | CD | line = trait's line; rel docs cleared |
| REL_OP | (node), TYPE_NAME, FIELD, OP, NAME, RET_TYPE (flag) | collect_impl -> source_impls_ | natspec text | — | generated calls | CD | fn name not resolved; RET_TYPE slot is a flag |
| REL_BIND | (node), NAME, VALUE | collect_impl | natspec text | — | generated | CD | bare fn names |
| DEEM_DEF | (node) | lower_deem_def | MetacallSite + rule IR (file/line/raw/segs) or DeemPlan (side table) | generated | generated | P | flipped to FN_MACRO_CALL_ITEM_DONE; no render; doc leak |
| DEEM_DEF | IS_PUB / NAME | lower_deem_def | generated fn IS_PUB / name | — | symbol | CP | — |
| DEEM_DEF | VIS | never | — | — | — | NR | DEFECT: `pub(module) deem` emitted fully pub |
| DEEM_DEF | TYPE_PARAMS | render_type_param_list_ | text on generated heads | generic | mono | CR | bounds unchecked at item |
| DEEM_DEF | PARAMS | lower_deem_def | params / natspec / DeemPlan | params | params | CP | class binder respelled to hash handle |
| DEEM_DEF | RAW_TEXT | enrich_deem_params; parse_program | rule IR (soff per SExpr) | generated | generated | CR | back-mapping from generated code unverified |

### 2.13 Dead node codes

| Node | Field | Sema | LIR | BIR | MLIR/DWARF | St | Loss |
|---|---|---|---|---|---|---|---|
| PACKAGE | (node) | never | — | — | — | NR | no producer |
| FIELD_WRITE | (node) | is_stmt_only_code; dead render case | — | — | — | NR | no producer |
| INDEX_WRITE | (node) | is_stmt_only_code | — | — | — | NR | no producer |
| FIELD_INDEX_WRITE | (node) | is_stmt_only_code | — | — | — | NR | no producer |
| LET_DESTRUCT | (node) | comments; spec-extract rule | — | — | — | NR | no producer; stale references |
| DEREF_FIELD_WRITE | (node) | is_stmt_only_code | — | — | — | NR | no producer |
| TUPLE_FIELD_WRITE | (node) | is_stmt_only_code | — | — | — | NR | no producer |
| FIELD_/INDEX_/DEREF_FIELD_/TUPLE_FIELD_/FIELD_INDEX_COMPOUND_ASSIGN | (node) | is_stmt_only_code | — | — | — | NR | 5 codes; COMPOUND_ASSIGN covers all shapes |
| CHAIN_FIELD_WRITE | (node) | is_stmt_only_code; render case | — | — | — | NR | no producer |
| CHAIN_FIELD_COMPOUND_ASSIGN | (node) | is_stmt_only_code | — | — | — | NR | no producer |
| ABSTRACT_FN | (node) | render only | — | — | — | NR | no producer |
| META_BLOCK | (node) | never | — | — | — | NR | no producer |
| GENOS_DEF | (node) | never | — | — | — | NR | spec-extract nodes.json claim is false |

### 2.14 Positions and diagnostics (cross-cutting)

| Node | Field | Sema | LIR | BIR | MLIR/DWARF | St | Loss |
|---|---|---|---|---|---|---|---|
| all AST nodes | SRC_LINE | get_line -> node_line_ | stmt_common::LINE only | sticky line_; point_lines_ | `-g`: FileLineColLoc(line, 0); loc_ not restored after nested stmt | P | expression/decl/pattern lines lost; node without SRC_LINE resets node_line_ to 0 |
| all AST nodes | column / byte offset | never (parser has Token text + source_ base; `soff` unused by logos.peg) | — | — | col 0 | CD | column!() = 0; no carets |
| all AST nodes | end position | never computed | — | — | — | NR | no folding, extents, lexical blocks |
| per-AST document | filename | file_ | Func SOURCE_FILE only | — | DIFile per fn | P | inlined/reparsed code keeps the wrong file |
| el.peg schema nodes | soff, file/line/raw root keys, segs | token-macro path; srcloc.logos | raw in handler blob | — | — | CR | the only real spans; private to Deem |
| synth_doc_ nodes | SRC_LINE | synth_node (only if line != 0) | construct line | line | line | P | line 0 synth nodes lose location (*inferred*) |
| reparsed wrappers | SRC_LINE | lower_reparsed_tail_expr; fn_macro wraps; include! | wrapper-relative line (no rebase) | wrong line | wrong DWARF line | P | points at line 2+k of user file (*inferred*) |
| quote-emitted docs | SRC_LINE + filename | `<metaprog-blob-subst>` etc. | handler-quote lines + synthetic file | same | bogus DIFile unless --gen-dir | P | the (file, line) pair points nowhere |
| LIR decls | decl line | node_line_ (diags) | — | — | fn line = first stmt; types line 0, no file | CD | break-on-header, ptype, reflection location |
| params / pattern binds | binding line | — | no param line | line_ at creation | params get scope line | P | declaration position lost |
| closures / drop glue | stmt LINE | recorded | kept | yes | UnknownLoc (DebugScopeSuspend) | P | invisible to debugger |
| sema Diag | {file, line} | error()/warning() | LProgram.diags | — | — | P | no column, range, secondary spans; JSON same |
| BIR verdicts | point_line | — | — | line formatted into message text | Diag line = 0, file "" | P | structured position dropped; mono diags also ""/0 |
| parse errors | furthest_line/column | module_loader | — | — | — | CP | only place a column is produced |
| line!/column!/file! | builtin | node_line_ / 0 / file_ | literal | const | const | P | no #[track_caller]; runtime panics location-less |
| metaprog error_at | SRC_LINE at offset | logos_metaprog_error_at | — | — | — | P | handlers get line only; no span API in reflection |
| BIR (stage) | places, scope ends, two-phase points | — | input | discarded after check | — | CD | none reaches codegen/DWARF/IDE |

## 3. Cross-cutting losses

### 3.1 Positions

| What | Where it dies | Evidence |
|---|---|---|
| column, byte offset, length | never stored. Parser generator computes `first_start_` but stamps it only into `soff` nodes, and logos.peg declares none. | tools/peg_gen_cpp/src/codegen.cpp; sema_expr.cpp column!() |
| end position | never computed | generated parser tracks `first_line_` only |
| expression line | sema -> LIR boundary (expr_common has TYPE only) | lir_schema.hpp expr_common |
| pattern / arm / param line | same | pat_keys, arm_keys, param_keys |
| decl line (fn, struct, enum, const, alias, trait, impl) | same | mlir_gen_debug.cpp::begin_fn_debug comment; di_struct_type line 0 |
| per-decl file | only Func has SOURCE_FILE | decl_keys |
| BIR/mono diag positions | Diag boundary: line 0, empty file | borrow_check.cpp; mono_scan.cpp |
| reparsed macro lines | no rebase | sema_expr.cpp::lower_reparsed_tail_expr (*inferred*) |
| closure / drop-glue lines | codegen (DebugScopeSuspend) | mlir_gen_dyn.cpp |
| lexical scopes | codegen (no DILexicalBlock) | mlir_gen_debug.cpp |

### 3.2 Desugaring provenance

| Construct | LIR shape | Marker left |
|---|---|---|
| `if let`, `while let`, let-chains | SMatch/SLoop with synth `_` / `break` arm; chain else duplicated N times | none (AST MATCH.PAT is sema-only) |
| iterator `for` | `let mut __iter; loop { match next() }` | name prefix `__iter` |
| `?` | Try node (common path); reparsed match (dispatch paths) | Try node / none |
| compound assign, `*p op= v` | `x = x op y`; place evaluated twice | BorrowOrigin::CompoundAssign on the overloaded path only |
| overloaded binop/unary, Index, Deref-impl `*` | ECall to trait method | OperatorAutoref on operand borrows only |
| autoderef | EDeref | none (autoref has BorrowOrigin::Autoref) |
| `unsafe {}` | SBlock | none |
| `[v; N]` | unrolled N-element ArrLit | none (mlir re-guesses splat) |
| comprehensions | let + ForEach + push/insert | BorrowOrigin::Desugar on the addr_of |
| `..base`, field shorthand | per-field FieldRead / slotless VarRef | none |
| or-pattern fan-out, nested variant patterns, str/char/Writ patterns | N arms / synth binders + guards | name prefixes `__refut_`, `__pat_pld_`, `__tstr_`, `__smatch_`, `__hp_` |
| param `mut`/pattern, closure param patterns | `__mutparam_`, `__pat_param_`, `__tup_param_` + prologue lets | name prefix |
| destructuring let / assign | `__dst_N`, `__let_tmp_N`, `__da_N` | DESTRUCTURE_TMP (outer temp only) |
| tail expr / explicit return | SReturn | none |
| identity/unsize cast, parens | removed | none |
| RPIT, `impl Trait` param | concrete type / `__impl_N` | name prefix |
| metacall, fn/item macros | node overwritten in place, `*_DONE` tombstones | doc-level EmitProvenance in dump mode only |
| statement temps, lit temps, glue lets | SBlock/SLet | TRANSPARENT, COMPILER_GLUE |

Conclusion: every existing marker (BorrowOrigin, CallMode, TRANSPARENT, COMPILER_GLUE, DESTRUCTURE_TMP) is BIR-driven and says only "compiler made this". None says which source construct it came from. Everything else relies on name spelling.

### 3.3 Attributes, docs, visibility, lifetimes, bounds

| Item | Status after sema |
|---|---|
| unknown / non-builtin attributes | silently ignored. No LLVM fn attributes are emitted. |
| user `#[annotation]` instances | structs only (zoned for runtime). Dropped by mono. No fn/enum/trait/field/param carrier. |
| cfg / cfg_attr / test / should_panic / deem_source | consumed into sema state. Test flags on Func are copied nowhere post-mono. |
| outer docs | fn, struct, field, enum, trait: pre-mono only. Variant, impl, rel, clause, assoc-const: lost. Trait DOC key never written. |
| module inner docs | in LIR, no reader |
| `pub(module)` | sema-only for all item kinds. DEEM_DEF widens it to pub. Containers drop it. |
| field / const / alias / trait / static / assoc-const visibility | no LIR key (static and assoc-const: never even read) |
| `unsafe` (fn, trait, impl, fn-ptr type, block, extern block) | none survives |
| lifetimes | LogosType lifetime slot + LIFETIME_PARAMS/OUTLIVES reach BIR. Nothing reaches MLIR/DWARF. Trait lifetime params and enum outlives are missing. `&'a (dyn Tr + 'b)` is collapsed. Bound lifetime args are dropped. TypeUID ignores regions (*inferred* merge). |
| bounds | `tbound_av` drops assoc_eqs, lifetime_args, Fn sugar, `?Sized`, ref-subject. Where vs inline is merged. Projection and concrete where-subjects are skipped (permissive). Enum/trait param bounds and defaults are lost. |

### 3.4 Original spellings

| Kind | Kept? |
|---|---|
| type names (aliases, `Self`, paths, `_`, elided lifetimes, `()`) | no. LogosType has no alias field. `&str` = `&[u8]`. |
| integer / float literals | value only (radix, `_`, suffix, f32 intent lost) |
| string literals | LIT_STR raw token survives to mlir_gen. Writ strings are cooked. |
| callee / method / variant names | mangled symbols. EMethodCall keeps METHOD. Variant name ends at BIR. |
| field names | yes, but as strings, never as indices or decl handles |
| labels | raw strings, matched by name |
| turbofish vs inferred type args | merged everywhere |
| written `ref` / `mut` vs default binding mode | only on VariantData (BINDING_REF_MODES 1/2 vs 3/4) |
| macro delimiter | never stored |
| mapping/container/deem bodies | deliberately syntactic text (the reverse problem: no resolved form in LIR) |

### 3.5 Round-trip renderer (`sema_render.cpp`)

These cases are missing or wrong, and they matter for quotes, `--gen-dir` and the format-args reparse:

- **No render case:**
  - Patterns: PAT_NEG_INT, PAT_SLICE, PAT_AT, PAT_CHAR(_RANGE), PAT_STR, PAT_BYTES, PAT_FLOAT, all PAT_WRIT_*.
  - Statements and expressions: LET_PAT, NESTED_FN, DESTRUCTURE_ASSIGN, BREAK/CONTINUE/RETURN_EXPR, QUOTE_TY/QUOTE_*, GENERIC_REF, METACALL, SIZEOF_PACK, OFFSET_OF, WRIT_TYPED_*, the comprehensions, IF_LET_CHAIN.
  - Items and other: UNION_DEF, SCHEMA_*, MAPPING_DEF, DEEM_DEF, EXTERN_BLOCK, USE_VARIANTS, INNER_ANNOTATION, DOC_BLOCK_LIT, FN_MACRO_CALL_ITEM(_DONE), INSTANTIATE_DECL, TEMPLATE_DECL, WRIT_BLOB.
- **Wrong render:**

| Node | Rendered as |
|---|---|
| PAT_RANGE | token passed through map_of; always prints `..=` |
| PAT_STRUCT | wrapper map iterated as an array |
| PAT_FIELD | `ref`/`mut` dropped |
| named PAT_REST | `xs @` dropped |
| PAT_TUPLE NAMES form | `()` |
| WRIT_ENTRY | LO_NEG dropped |
| ARR_LEN | `Self::N` -> `N`; arithmetic -> `[T; ]`; `metacall {...}` rendered literally |
| REPEAT_GROUP | OP=2 -> `*` |
| ANNOT_CALL | `cfg(all)` |
| FN_PTR_TYPE | ABI, `unsafe`, `for<>` dropped |
| WHILE | GUARD and ITEMS dropped |
| FOR | `mut` dropped |
| LABELED_LOOP / BREAK / CONTINUE | `''a`; loop header dropped (*inferred*) |
| CONTAINER_DEF(_DONE) | `for _` |

- **Unchecked markers:** only the `render_expr` unsupported marker is checked (by format-args). The pat, stmt and item markers are never checked.

### 3.6 Defects found in passing (triage list)

| # | Defect | Site | Basis |
|---|---|---|---|
| 1 | REPEAT_GROUP outside a quote is silently swallowed (expr -> 0, stmt/field/use dropped) | sema_expr.cpp::lower_expr default (error_expr without diag); sema_stmt.cpp lower_stmt "Unknown stmt"; field/use collectors | observed |
| 2 | `ref x: T` param typed `&Self` | sema_collect.cpp::collect_fn read_param_types | read; known gap N3 |
| 3 | `pkg.path.Type` and `pkg.path.Enum::V` ignore the package | sema.cpp::resolve_type TYPE_REF; sema_expr.cpp::lower_enum_lit | read |
| 4 | negative impl exported as positive (IS_NEGATIVE never written) | sema_decl.cpp::lower_impl_block; emit_module.cpp | read |
| 5 | non-Fn `&mut dyn Tr` = `&dyn Tr` | sema.cpp::resolve_type DYN_TYPE | read |
| 6 | `dyn for<'a>` binders dropped; `unsafe fn()` = `fn()` | resolve_type DYN_TYPE / FN_PTR_TYPE | read |
| 7 | `where &T: Tr` serialised as `T: Tr` | lir_mirror.cpp::tbound_av | read |
| 8 | TYPE_CODE dropped for non-generic structs, so `type_code_of::<T>()` in generic code returns a hash | mono_clone.cpp::clone_struct_def, subst_expr TypeCodeOf | read, suspected |
| 9 | union DWARF is a structure with sequential offsets | mlir_gen_debug.cpp::di_struct_type | read |
| 10 | doc leak after INSTANTIATE / METACALL_ITEM / FN_MACRO_CALL_ITEM / MAPPING / CONTAINER / DEEM | sema.cpp::lower_module_items | read, *inferred* effect |
| 11 | `pub(module) deem` -> pub; container pub(module) dropped | lower_deem_def; container_spec_line | read |
| 12 | static and assoc-const IS_PUB never read | collect_const; lower_impl_block | read |
| 13 | turbofish ignored on `E::V{..}` and under a hint | lower_enum_lit_data; lower_enum_lit | read |
| 14 | STATIC_CALL `<T as Trait>::m` falls back silently | lower_static_call | read |
| 15 | integer pattern literals i64 only (suffix ignored, 128-bit wrap) | build_pattern_impl | read |
| 16 | Writ int parsing (`_`, `0o`, hex neg, i64::MIN) and `@r"..."` | lower_writ_val | read |
| 17 | range exprs forced to i32/i64; half-open value ranges accepted silently | sema_expr.cpp RANGE_EXPR | read |
| 18 | `[v; 0]` emits 1 element | lower_arr_fill_lit | read, plausible |
| 19 | compound-assign place and `*p op= v` evaluated twice | lower_compound_assign; DEREF_COMPOUND | read, documented intent |
| 20 | char patterns accepted on integer scrutinee; PAT_UNIT has no type check | build_pattern | read |
| 21 | array/slice ForEach label dropped in BIR; for INCLUSIVE ignored in BIR | borrow_bir.inc | read |
| 22 | match-arm `=> expr` wrapper line uses stale node_line_ | lower_match | read, *inferred* |
| 23 | LET_PAT never sets ANNOT_LIFETIME | lower_let_pat | read |
| 24 | FIELD_SHORTHAND VarRef has no VAR_SLOT | lower_struct_lit; lower_enum_lit_data | read |
| 25 | NESTED_FN is a closure (captures likely allowed, no recursion) | lower_nested_fn | read, *inferred* |
| 26 | DESTRUCTURE_ASSIGN struct NAME unchecked; nested field pattern writes the wrong variable | lower_destructure_assign | read, *inferred* |
| 27 | LIST_COMP/MAP_COMP guard not bool-checked; HashMap__insert typed void | lower_list_comp; lower_map_comp | read |
| 28 | BIR skips fns with binding or-patterns or slice rest/suffix | borrow_bir.inc bind_pattern | read (coverage hole) |

## 4. Requirements for the core / desugaring layer

### 4.1 Principles

- **R1. Lossless lowering.** Every AST fact is either (a) carried into the core form, (b) recorded as a provenance tag, or (c) consumed with an explicit "consumed: reason" entry in the schema. Silent drops are defects. Today at least 5 fields per inventory are read by nobody: the NR rows.
- **R2. Processed over raw.** When a fact has been resolved, the core form carries the resolved identity: DefId for callee, field, variant, trait and loop label, a slot for bindings, a TypeRef for types. The raw spelling is kept only as a secondary `spelling` attribute, never as the key. The target is a codebase where no consumer re-resolves by name and no logic keys on a `__prefix`. Exception: the mapping/container/deem bodies are syntactic by design. For those, the core layer should add the resolved form next to the text, not replace it.
- **R3. Provenance tags.** Add one general key on LExpr, LStmt and Pattern: `origin = {construct: enum, source_span, parent_origin?}`. Minimal enum values:
  - `user`
  - `if_let`, `while_let`, `let_chain`, `for_iter`, `try`
  - `compound_assign`, `op_overload`, `autoderef`, `autoref`
  - `arr_repeat`, `comprehension`, `struct_base`, `field_shorthand`
  - `or_fanout`, `pat_literal_guard`, `writ_pattern`
  - `param_mut`, `param_pattern`, `destructure`, `tail_return`
  - `drop_glue`, `stmt_temp`
  - `macro_expansion(site)`, `metacall(site)`, `quote(handler)`

  It subsumes TRANSPARENT, COMPILER_GLUE, DESTRUCTURE_TMP and BorrowOrigin::Desugar (keep BorrowOrigin for the two-phase semantics).
- **R4. Spans are data.**
  - The parser stores `{file_id, byte_start, byte_end}` on every node. Line and column are derived on demand, as Deem's srcloc.logos already does. SRC_LINE becomes derived.
  - Every core node carries a span, or inherits it through `origin.parent`.
  - Reparse paths (render -> reparse) must re-stamp spans from the site, or carry a source map.
- **R4a. Span encoding (planned, not scheduled; decided 2026-09-26).** Key budget: `TinyObjectMap` has 52 key codes and every one is used by SOME node, but codes are per-node (the `group X { … }` blocks and the slot aliases in `%fields`, e.g. QUAL_PARTS=17 over USES, DOC=50 over HI_NEG). Measured from the grammar actions (sema's post-parse puts not counted): a node carries at most 12 keys (FN, TRAIT_DEF), 69 of 203 node codes carry 3. So a code is freed by aliasing, not by widening the bitmap. Cheapest: KEY (51) is used only by WRIT_ENTRY and PAT_WRIT_MAP_ENTRY, read only as `la::KEY` (21 sites), so alias it onto LHS (12), which neither carries; code 51 becomes the global SRC_SPAN. Shape (rustc `Span` over a global `SourceMap`):
  - a global SourceMap assigns each loaded file a range in ONE byte-offset space; reparse buffers (`vec!`, `?`, fn_macro, `include!`, metaprog output) register as virtual files with an `expanded_from: span` record;
  - SRC_SPAN = one inline i56 = global start (32 bits) + length (23 bits, saturating); SRC_LINE (24) stays until its readers move to `line_of`/`span_of`, then retires;
  - file, line, column and end are derived (line table, binary search); macro provenance is the `expanded_from` chain;
  - cost: both parser generators (peg_gen_cpp, peg_gen_logos) stamp SRC_SPAN; position-free equality (`sema_collect.cpp::ast_tom_equal`) must skip key 51 too; renumbering KEY changes the AST key ABI seen by metaprog handlers (minor version bump); `Diag` gains a span (R6).
- **R5. Mono is a copy, not a filter.** clone_fn, clone_struct_def and clone_enum_def copy every metadata key they do not substitute: docs, annotations, test flags, TYPE_CODE, TYPE_HASH, provenance, spans. An instance also records `template = DefId` and `subst = [TypeRef]`.
- **R6. Diagnostics carry structured spans.** `Diag{primary: span, labels: [(span, text)], notes}`. BIR and mono must pass `point_span`, not text.

### 4.2 Where each lost item should live

| Lost item | Core / LIR | BIR | MLIR / DWARF | Consumer that needs it |
|---|---|---|---|---|
| byte span per node | `span` on every LExpr/LStmt/Pattern/decl/field/variant/param/arm | Stmt/Term/Local span | FileLineColLoc with column; DW_AT_decl_line/column | IDE, diagnostics, DWARF, column!(), coverage |
| decl line + file for all items | decl `span` + `file_id` | — | DISubprogram line = header; DICompositeType/DIGlobalVariable decl_line | debugger, reflection, go-to-def |
| desugaring origin | `origin` key (R3) | Local/Stmt origin (hide artificial temps) | DIFlagArtificial on synth locals; skip in line table | diagnostics wording, debugger, IDE, lints |
| macro / metacall / quote expansion | `origin = macro_expansion(site span, callee)` per item | — | DWARF inlined-at style chain or DW_AT_artificial | diagnostics backtrace, IDE go-to-generator, incremental rebuild |
| lexical scope extent | block span | StorageDead points | DILexicalBlock | debugger (shadowing), IDE |
| loop labels | resolved loop id + spelling | loop id | DILabel | IDE, debugger, removes string fallback |
| field / variant / callee identity | DefId + spelling | DefId | — | IDE, reflection, cheaper consumers |
| binding identity on SAssign, shorthand | VAR_SLOT everywhere | slot | — | correctness under shadowing |
| visibility (incl. `pub(module)`, fields, consts, aliases, traits, statics, assoc consts) | `vis = {private, module, pub}` on every decl and field | — | linkage (internal for non-pub), DW_AT_accessibility | ABI surface, reflection, docs, IDE |
| `unsafe` (fn, trait, impl, fn-ptr, block) | `is_unsafe` on decls/types; `unsafe_region` origin on blocks | region marker (optional) | — | lints, reflection, IDE, safety audit |
| extern ABI string | `abi` on Func and FnPtr | — | calling convention; nounwind unless C-unwind | codegen correctness, ABI spec |
| docs (all item kinds, variants, impls, module) | DOC on every decl/variant/impl/assoc item; copied by mono | — | optional custom section | doc tooling, IDE hover, reflection |
| attributes (known and unknown) | `attrs = [{path, args(typed), span}]` on every item/field/variant/param | — | LLVM fn attrs for inline/cold; must_use/deprecated -> lints | reflection, metaprog handlers, lints, optimizer |
| generic params, bounds, defaults, const-ness (all item kinds incl. trait/enum/GAT) | full `TypeParam` schema everywhere; tbound adds assoc_eqs, lifetime_args, fn_sig, relaxed, ref_subject, origin (inline/where) | tv_bounds full | DW_TAG_template_*_parameter on instances | reflection, cross-archive bound checking, IDE |
| trait method signatures, assoc consts, assoc-type defaults | TraitView: full method sig; assoc const decls; AT default + GAT params | — | — | reflection, IDE signature help, cross-module checks |
| RPIT / `impl Trait` identity | opaque-type decl referenced by ret type; synth param flagged | — | — | IDE hover, diagnostics |
| written type spelling / alias | `spelling_type` side attribute on annotation sites; alias kind in LogosType | — | DW_TAG_typedef | IDE hover, diagnostics, DWARF |
| written vs inferred (let type, turbofish, closure param/ret) | `written` bit per type slot / type arg | — | — | inlay hints, diagnostics |
| literal spelling | `spelling` on literal nodes | — | — | render/reparse, lints |
| pattern tree (nested variant, rest position, inclusive range, char/str/Writ literal) | full Pattern tree; PatInt 128-bit; range inclusive + open flags; char kind; str pattern node | BIR lowers the tree | — | exhaustiveness diagnostics, IDE, render |
| struct-variant field names | variant_keys field names + shape | named fields | DW_TAG_variant_part members | debugger, docs, reflection |
| schema decls | schema decl (fields, keys, code, variants) in LIR | — | DWARF shows declared fields | reflection, debugger |
| relational items (mapping, rel, container, deem) | decl records with resolved types alongside the syntactic text | — | — | reflection (query catalog), IDE |
| const values | folded value + expr on Const; DIGlobalVariable for statics | — | DIGlobalVariable | debugger, reflection |
| import graph / re-exports | module decl with imports and re-exports | — | DINamespace for package | IDE (unused imports), ABI spec |
| item source order | order index on decls | — | — | docs, reflection |

### 4.3 Rules for the layer

1. **Enumerate by property.** The schema gains a per-key disposition table: carried, provenance, consumed(reason). A CI check verifies that every grammar field key appears in it. This turns the NR/CD census into a gate.
2. **Invariant: no identity by spelling.** No consumer may match `__`-prefixed names. Grepping `starts_with("__` in consumers should reach zero once `origin` lands.
3. **The core form is the single source for BIR and MLIR.** BIR keeps its per-function IR, but it reads origin and span from core, never from name heuristics.
4. **The renderer is tested by the gate.** Render(AST) followed by reparse must equal the AST modulo spans, for every node code. The unsupported markers for pat, stmt and item become hard errors.
5. **Dead codes are deleted.** The 17 unproduced node codes are removed from `%nodes`, `ast.hpp`, `is_stmt_only_code`, sema_render and the stale spec-extract rules. TEMPLATE_DECL is either wired up or deleted.
6. **Unknown node codes refuse.** `lower_expr` default, `lower_stmt` "Unknown stmt" and the item/field/use collectors must emit a diagnostic. `error_expr()` without a diagnostic is itself a permissive exit (see defect 1).
