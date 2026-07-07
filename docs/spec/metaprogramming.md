# Metaprogramming

Scope: compile-time metaprogramming surface — function-like macros (`#[fn_macro]`, `#[token_macro]`), `quote!`/`quote_expr!`/`quote_item!`/`quote_ty!` AST quoting and antiquotation, explicit `comptime` metacalls, annotations (`#[...]`) and their `cfg!`/derive/trigger machinery, the `format!` family, and the `AnyVal`/`WAny` inline value-form + type/enum reflection intrinsics that back them. Sources: `tools/peg_gen_cpp/grammars/logos.peg` (grammar layer) and `src/compiler/mlir_gen_expr.cpp` / related codegen (lowering layer); extracted into `tools/spec-extract/rules/**/*.json`.

> **Note:** `expr.macro.fn-style-call` and `metaprog.macro.fn-style-call` both describe function-style macro-call grammar (general form vs. operand/no-struct-lit position) and are extracted from adjacent grammar productions; the first carries domain `metaprog` despite an `expr.*`-shaped id. Both are kept verbatim below (grouped under **Macro**) rather than merged, per extraction-fidelity policy.

## Function-style macro call syntax

### `metaprog.macro.raw-text-brace-escape` — Raw-text macro-call capture honours backslash and backtick escapes

RAW_TEXT captured from a balanced brace-delimited group on a fn-macro call (RAW_GROUP_*) honours two escapes so literal braces don't break brace balancing: (a) a backslash immediately preceding `{`, `}`, backtick, or `\` passes that character through without counting it toward the balance (the backslash itself survives into the captured text, for the handler to de-escape); (b) a backtick-delimited region is skipped as a whole, with no brace counting performed inside it. Both are additive: a well-formed body without these constructs is captured byte-identically.

**Divergence:** A3

**Testability:** behavioral

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L80`

### `expr.macro.fn-style-call` — Function-style macro invocation

Function-style macros invoke as `name!(…)`, `name![…]`, or `name!{…}`; the contents between balanced delimiters are captured as raw source text and re-interpreted at sema time per the callee's macro kind (#[fn_macro] re-parses as an expression list; #[token_macro] lexes as a TokenStream). In no-struct-lit (condition) position the brace form `name!{…}` is excluded.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L2743-L2754`, `tools/peg_gen_cpp/grammars/logos.peg#L2550-L2559`

### `metaprog.macro.fn-style-call` — Function-style macro invocation in operand position

Function-style macros invoke as `name!(…)` or `name![…]` in operand/expression position (`if cfg!(unix) && … {}`, `while m!(x) {}`); the ordinary (non-no-struct-lit) primary additionally admits the brace form `name!{…}`, which is excluded from the no-struct-lit grammar since it would swallow the enclosing if/while body block. Contents between the balanced delimiters are captured as raw source text and re-interpreted at sema time per the callee's macro kind.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L2616-L2619`, `tools/peg_gen_cpp/grammars/logos.peg#L2809-L2814`

## `#[fn_macro]` — expression-position function-like macros

### `metaprog.fn-macro.expr-and-item-forms` — Function-style macros exist in expression and item forms

`name!(args)` / `name![args]` (FN_MACRO_CALL, expression position) resolves CALLEE against `#[fn_macro]`-annotated functions, passing each argument AST as an ExprBlob. `name!{...}` at module-item position (FN_MACRO_CALL_ITEM) has the same callee/args shape but routes through the metacall_item-style splice path (callee returns an ItemList / QuoteItemBlob); after splicing the node becomes FN_MACRO_CALL_ITEM_DONE, which sema skips.

**Testability:** behavioral

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L294-L296`

### `metaprog.fn-macro.item-position` — Item-position fn-macro invocation

`IDENT ! { ... }` at item position invokes a function-like macro whose body is captured as raw text (RAW_GROUP_BRACE); brace delimiter only at item position (parens/brackets stay reserved for expression position). The callee resolves through the item-splice pipeline (returns an item list).

```logos
my_macro! { struct A; }
```

**Related:** `metaprog.fn-macro.expr-and-item-forms`

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L579-L580`

### `metaprog.fn-macro.callee-must-be-marked` — name!(...) callee must be a #[fn_macro] or #[token_macro] fn

A `name!(...)` invocation resolves `name` against the function overload set; the callee selected must be a fn annotated `#[fn_macro]` or `#[token_macro]`. If `name` is unknown, or no overload bears such an annotation, the call is rejected. Only macro-annotated fns are callable via `name!(...)` syntax.

**Uncertainty:** Resolution is restricted to non-generic funcs_ (generic fn_macro out of scope for this slice).

**Source:** `src/compiler/sema_expr.cpp#L18497-L18517`

### `metaprog.fn-macro.signature-shapes` — Accepted fn-macro/token-macro signatures

A `#[fn_macro]` callee must have exactly one parameter and signature `(ExprBlob) -> ExprBlob` (single-arg form) or `(Vec<ExprBlob>) -> ExprBlob` (N-arg packed form). A `#[token_macro]` callee must have signature `(str) -> ExprBlob`. Any other signature is rejected.

**Source:** `src/compiler/sema_expr.cpp#L18525-L18555`

### `metaprog.fn-macro.single-arg-arity` — Single-arg fn-macro takes exactly one argument

For a callee with signature `(ExprBlob) -> ExprBlob`, the `name!(...)` invocation must supply exactly one argument; supplying any other count is an error.

**Source:** `src/compiler/sema_expr.cpp#L18536-L18539`, `src/compiler/sema_expr.cpp#L19017-L19022`

### `metaprog.fn-macro.args-are-expr-list` — name!(...) arguments parse as a comma-separated expression list

The raw text between the parentheses of `name!(...)` is parsed as a comma-separated list of expressions (each becoming one macro ARG). If it does not parse as such, the invocation is rejected.

**Source:** `src/compiler/sema_expr.cpp#L18566-L18681`

### `metaprog.fn-macro.arg-passed-as-ast-blob` — Each fn-macro argument is passed as its serialized AST subtree (ExprBlob)

Each argument expression of `name!(...)` is passed to the callee unevaluated as an `ExprBlob` referencing the serialized AST subtree of that argument; the callee receives the syntax tree, not a runtime value. For the Vec form, all argument ExprBlobs are packed into a `Vec<ExprBlob>` in source order.

**Source:** `src/compiler/sema_expr.cpp#L19028-L19116`

### `metaprog.fn-macro.result-is-exprblob-spliced` — fn-macro call expands to the ExprBlob it returns

A `name!(...)` expression has the callee's return type `ExprBlob`; at expansion the AST produced by the macro (the returned ExprBlob) is spliced in place of the call site before final sema.

**Source:** `src/compiler/sema_expr.cpp#L19118-L19132`

### `metaprog.fn-macro.cfg-builtin` — cfg!(...) is a compile-time built-in predicate

The built-in macro `cfg!(...)` is evaluated at compile (sema) time to a bool literal, before user fn-macro resolution. It accepts built-in target keys, the boolean combinators `all`/`any`/`not`, and user feature flags supplied via `--cfg`.

**Source:** `src/compiler/sema_expr.cpp#L18489-L18493`

### `metaprog.fn-macro.callee-marker-required` — name!(...) resolves only #[fn_macro]/#[token_macro] callees

A function-style macro call `name!(...)` resolves only callees marked `#[fn_macro]`; a `#[token_macro]` callee additionally receives its raw source text directly as a `str` argument.

**Testability:** transitive

**Source:** `src/compiler/sema_impl.hpp#L2553-L2554`

### `metaprog.fn-macro.builtin-macro-list` — Fixed set of compiler-built-in function-style macros checked before user #[fn_macro]s

Function-style macro calls (`name!(args)` / `name![args]`) are first checked against a fixed compiler-built-in set — `cfg!`, `line!`, `column!`, `file!`, `include!`, `include_str!`, `include_bytes!`, `env!`, `concat!`, `concat_bytes!`, `stringify!`, `compile_error!` — handled by lower_builtin_macro; a callee name not in this set falls through to resolution against user `#[fn_macro]`-tagged functions.

**Testability:** behavioral

**Source:** `src/compiler/sema_impl.hpp#L4018-L4029`

### `metaprog.fn-macro.item-callee-resolution` — Callee resolution and lowering of item-position fn-macro calls

A function-style macro invoked at module item position as `name!{...}` resolves its callee against `#[fn_macro]`-tagged functions (mirroring expression-position fn-macro calls) and routes its arguments through the per-site raw-capture arg-blob shim, mirroring metacall's item-position lowering.

**Related:** `metaprog.fn-macro.item-position`, `metaprog.metacall.item-position`, `metaprog.fn-macro.builtin-macro-list`

**Source:** `src/compiler/sema_impl.hpp#L4050-L4055`

## `#[fn_macro]` at item position

### `metaprog.fn-macro-item.callee-must-be-fn-macro` — Item-position macro call callee must be #[fn_macro] or #[token_macro]

`callee!{ ... }` / `callee!(...){...}` at ITEM position resolves `callee` in the function-overload set; if no overload is found, or none of the overloads carries `#[fn_macro]` or `#[token_macro]`, the item is rejected.

**Testability:** diagnostic

**Source:** `src/compiler/sema_expr.cpp#L19483-L19508`

### `metaprog.fn-macro-item.return-type` — Item-macro callee must return ItemList or QuoteItemBlob

An item-position `#[fn_macro]`/`#[token_macro]` callee's declared return type must be `ItemList` or `QuoteItemBlob`; any other return type is rejected.

**Testability:** diagnostic

**Source:** `src/compiler/sema_expr.cpp#L19510-L19519`

### `metaprog.fn-macro-item.param-signature` — Item-macro callee parameter shape is fixed per attribute

An item-position `#[fn_macro]` callee must have parameter list `(Vec<ExprBlob>)` or `()`. An item-position `#[token_macro]` callee must have parameter list `(str)`, `(name: str, body: str)`, or `(name: str, params: str, body: str)`. Any other arity/type combination for the given attribute is rejected.

**Testability:** diagnostic

**Source:** `src/compiler/sema_expr.cpp#L19521-L19575`

### `metaprog.fn-macro-item.raw-text-as-expr-list` — fn_macro `Vec<ExprBlob>` item form re-parses RAW_TEXT as a comma-separated expr list

For an item-position `#[fn_macro]` callee of shape `(Vec<ExprBlob>)`, the macro's RAW_TEXT is wrapped as `fn __f() { __c(<RAW_TEXT>); }`, parsed as Logos source, and the resulting call's ARGS elements are lowered one-for-one into `ExprBlob` values (each argument's AST subtree is copied into its own document, tagged with its original AST node CODE) and passed to the callee as `Vec<ExprBlob>`.

**Testability:** behavioral

**Source:** `src/compiler/sema_expr.cpp#L19769-L19825`, `src/compiler/sema_expr.cpp#L19838-L19876`, `src/compiler/sema_expr.cpp#L19887-L19901`

### `metaprog.fn-macro-item.zero-arg-arity` — Zero-arg fn_macro item form rejects nonzero arg count

An item-position `#[fn_macro]` callee declared with zero parameters is invoked with the RAW_TEXT re-parsed as a (possibly empty) comma-separated arg list; if that re-parse yields one or more arguments, the item is rejected.

**Testability:** diagnostic

**Source:** `src/compiler/sema_expr.cpp#L19773-L19825`, `src/compiler/sema_expr.cpp#L19827-L19832`

### `metaprog.fn-macro-item.arg-must-be-ast-node` — Each re-parsed fn_macro item arg must be a valid AST node

Each element of the re-parsed RAW_TEXT arg list (for the `Vec<ExprBlob>` item form) must be a non-null pointer value whose root carries a nonzero AST CODE; otherwise the item is rejected.

**Testability:** diagnostic

**Source:** `src/compiler/sema_expr.cpp#L19841-L19855`

## `#[token_macro]` — token-stream macros

### `metaprog.token-macro.raw-text-as-str` — token-macro receives unparsed raw text as str

For a `#[token_macro]` callee, the raw bytes between the delimiters of `name!(...)` are forwarded verbatim as a single `str` argument, bypassing expression-list parsing and per-argument AST serialization.

**Source:** `src/compiler/sema_expr.cpp#L18576-L18619`

## `#[token_macro]` at item position

### `metaprog.token-macro-item.name-slot-scope` — Resource NAME slot required for 2/3-arg token_macro, optional+dropped for 1-arg

`resource <name> = h!{...}` / `resource <name> = h!(<params>){...}` supplies the LHS binding as a NAME field on the item node. Targeting the 2-arg `(name: str, body: str)` or 3-arg `(name: str, params: str, body: str)` `#[token_macro]` form without a NAME is rejected. Targeting the 1-arg `(body: str)` form, a supplied NAME is accepted but the value is discarded — never delivered to the callee.

**Testability:** diagnostic

**Source:** `src/compiler/sema_expr.cpp#L19577-L19592`, `src/compiler/sema_expr.cpp#L19658-L19663`

### `metaprog.token-macro-item.params-slot-scope` — Resource PARAMS slot only valid for the 3-arg token_macro form

`h!(<params>){...}` supplies a PARAMS field carrying the raw params source text. Supplying PARAMS against any callee signature other than the 3-arg `(name: str, params: str, body: str)` `#[token_macro]` is rejected; conversely, targeting the 3-arg form without a supplied PARAMS is also rejected.

**Testability:** diagnostic

**Source:** `src/compiler/sema_expr.cpp#L19600-L19620`

### `metaprog.token-macro-item.raw-text-verbatim` — token_macro item form receives raw block text unparsed

For an item-position `#[token_macro]` callee of shape `(str)`, `(name: str, body: str)`, or `(name: str, params: str, body: str)`, the full RAW_TEXT bytes of the macro's block (and, where present, the NAME and PARAMS text) are passed to the callee as opaque `str` values byte-for-byte — the content is NOT re-parsed as Logos source before the call.

**Testability:** behavioral

**Source:** `src/compiler/sema_expr.cpp#L19630-L19664`, `src/compiler/sema_expr.cpp#L19670-L19707`

## Antiquotation (`#ident` capture into quoted AST)

### `metaprog.antiquot.capture-forms` — Writ antiquotation capture syntax

Within a quoted/Writ literal, an antiquotation captures a value either by identifier `$name` or by expression block `${expr}`.

**Divergence:** Logos metaprogramming antiquotation; no Rust equivalent.

**Testability:** transitive

**Source:** `src/compiler/sema_render.cpp#L532-L537`

### `metaprog.antiquot.callee-skips-payload-arg` — Antiquoted callee drops synthetic first arg

When a call's callee is produced by antiquotation substitution, the grammar's bulk `$...` capture inserts the antiquot payload as the first argument; semantically that first argument is not a real call argument and is skipped.

**Uncertainty:** Inferred from renderer mirroring lower_call; exact substitution mechanism defined elsewhere.

**Testability:** transitive

**Source:** `src/compiler/sema_render.cpp#L141-L170`

## `quote!`/`quote_item!`/`quote_ty!` — overview

### `metaprog.quote.antiquote-placeholder` — `#ident` antiquotation placeholder inside `quote_*!` bodies

Inside the body of a `quote_*!` macro, `#ident` is an antiquotation placeholder splicing an external value; its AST node carries a NAME_VAR field holding the placeholder's identifier text. NAME_VAR exists only inside quote_*! bodies (where a where-clause never co-occurs).

**Divergence:** A3

**Testability:** behavioral

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L78`

### `metaprog.quote.typed-ast-literals` — quote_item!/quote_expr!/quote_ty! are typed AST literals

`quote_item! { item* }` (QUOTE_ITEM), `quote_expr! { expr }` (QUOTE_EXPR), and `quote_ty! { type }` (QUOTE_TY) are typed AST-literal forms yielding, respectively, an item list, a single expression AST (root carries a schema_type_code), and a Type AST. Inside a quote body, `#(body)sep*` (REPEAT_GROUP) repeats `body` with separator `sep` in {none, comma, `&&`}. `$ident` in type position (ANTIQUOT_TYPE) is legal only inside `quote_ty!` and refers to a Type-valued binding; `$ident...` in a type-argument list (ANTIQUOT_PACK) is legal only inside `quote_ty!` and refers to an `Array<Type>`-valued binding.

**Testability:** behavioral

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L269-L275`

### `metaprog.quote.quote-item` — quote_item! typed-AST item literal

`quote_item! { item* }` parses zero or more item declarations as its body and produces a QUOTE_ITEM node; sema deep-clones the parsed items into a fresh Writ module, serializes it, and emits a byte-slice (WritStatic) literal value.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L3105-L3112`

### `metaprog.quote.quote-expr` — quote_expr! typed-AST expression literal

`quote_expr! { expr }` parses a single expression as its body and produces a QUOTE_EXPR node; sema deep-clones the expr's AnyVal as the root TOM of a fresh Writ doc (root schema_type_code set from CODE) and emits an ExprBlob value, spliced via metacall.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L3114-L3121`

### `metaprog.quote.quote-ty` — quote_ty! typed Type literal

`quote_ty! { type_ref }` parses a single type expression as its body and produces a QUOTE_TY node; sema resolves it to a TypeRef and emits the same `Type{kind, name, size}` struct literal that `type_of::<T>()` produces.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L3123-L3129`

## `quote_expr!` — expression-AST quoting

### `metaprog.quote-expr.reify-ast-to-exprblob` — quote_expr! reifies an expression AST into an ExprBlob

`quote_expr! { e }` evaluates to a value of struct type `ExprBlob` carrying the serialized AST of `e`. With no antiquots, the AST is emitted as a static rodata blob and wrapped directly as `ExprBlob { ptr }`.

**Divergence:** A3/A6 (replaces Rust macro/quote layer)

**Source:** `src/compiler/sema_expr.cpp#L16386-L16423`, `src/compiler/sema_expr.cpp#L16806-L16813`

### `metaprog.quote-expr.antiquot-must-be-in-scope` — Antiquot variable in quote_expr! must be a bound local

A `#name` antiquot inside `quote_expr!` is an error unless `name` is a variable in scope at the quote site ("`#name` — variable not in scope").

**Divergence:** A3/A6

**Source:** `src/compiler/sema_expr.cpp#L16510-L16514`

### `metaprog.quote-expr.scalar-antiquot-type` — Scalar antiquot must be Ident, or Ident/ExprBlob outside ident-only positions

A `#name` antiquot outside a repetition group, in a general expression position, must bind a value of type `Ident` or `ExprBlob`; in ident-only positions (field names, struct type name, field-read selector) it must bind an `Ident`. Otherwise it is rejected ("expected Ident" / "expected Ident or ExprBlob").

**Divergence:** A3/A6

**Source:** `src/compiler/sema_expr.cpp#L16549-L16560`, `src/compiler/sema_expr.cpp#L16618-L16621`, `src/compiler/sema_expr.cpp#L16661-L16685`

### `metaprog.quote-expr.repeat-cursor-type` — Repetition cursor must be [Ident;N], `Vec<Ident>`, or `Vec<ExprBlob>`

A `#name` antiquot inside a `#(...)*` repetition group (a cursor) must bind a value of type `[Ident; N]` (fixed count N), `Vec<Ident>`, or `Vec<ExprBlob>` (dynamic count); any other type is rejected ("expected [Ident; N], `Vec<Ident>`, or `Vec<ExprBlob>`").

**Divergence:** A3/A6

**Source:** `src/compiler/sema_expr.cpp#L16523-L16538`, `src/compiler/sema_expr.cpp#L16469-L16492`

### `metaprog.quote-expr.repeat-cursor-length-agree` — Fixed-length cursors in one repetition group must agree on length

Within a single `#(...)*` group, all fixed-size `[Ident; N]` cursors must share the same length N; a sibling cursor with a different N is rejected ("cursor length mismatches sibling cursor in same #(...)*"). A `Vec`-backed (dynamic) cursor makes the group dynamic and waives the fixed-length agreement check.

**Divergence:** A3/A6

**Source:** `src/compiler/sema_expr.cpp#L16539-L16548`

### `metaprog.quote-expr.repeat-needs-cursor` — A repetition group must contain at least one cursor antiquot

A `#(...)*` repetition group body must contain at least one cursor antiquot `#x` of a cursor type; an empty-cursor body is rejected ("`#(...)*` body has no cursor `#x`").

**Divergence:** A3/A6

**Source:** `src/compiler/sema_expr.cpp#L16600-L16605`

### `metaprog.quote-expr.no-nested-repeat` — Nested repetition groups are not allowed

A `#(...)` repetition group may not be nested inside another `#(...)` group ("nested `#(...)` repetition not supported").

**Divergence:** A3/A6

**Testability:** untestable

**Source:** `src/compiler/sema_expr.cpp#L16589-L16597`

### `metaprog.quote-expr.antiquot-carrier-positions` — Antiquots are recognized only in defined AST carrier positions

Antiquots and repetition groups are recognized only within the supported carrier set: VAR_REF, BINOP (lhs/rhs), PAREN/UNARY/CAST/DEREF (value), FIELD_READ (selector + receiver), CALL/METHOD_CALL/STATIC_CALL (callee-name-var, receiver, args), STRUCT_LIT/FIELD_INIT/FIELD_SHORTHAND, ARR_LIT/TUPLE_LIT/BLOCK items, statement carriers (LET, LET_DESTRUCT, EXPR_STMT, TAIL_EXPR, RETURN), and control flow (IF, WHILE, FOR, LOOP, ASSIGN, COMPOUND_ASSIGN). Antiquots in unsupported shapes are not substituted.

**Divergence:** A3/A6

**Uncertainty:** The exact carrier set is an evolving implementation surface (slices noted in comments), not a frozen spec.

**Source:** `src/compiler/sema_expr.cpp#L16586-L16747`

### `metaprog.quote-expr.subst-runtime` — quote_expr! with antiquots substitutes at runtime via logos_quote_expr_subst

`quote_expr!` containing N>0 antiquots lowers to a block that binds the static template blob and one `IdentSpan { ptr, count, kind }` per placeholder, then calls `logos_quote_expr_subst(template_ptr, size, &spans[0], N) -> *const u8` and wraps the result as `ExprBlob { ptr }`. Span kind is 0 for Ident slots, 1 for ExprBlob slots, 2 for `Vec<ExprBlob>` cursors.

**Divergence:** A3/A6

**Source:** `src/compiler/sema_expr.cpp#L16815-L16981`, `src/compiler/sema_expr.cpp#L16866-L16943`

## `quote_item!` — item-AST quoting

### `metaprog.quote-item.name-antiquot-forms` — quote_item! accepts #name and #(expr) name antiquotations

Within `quote_item! { ... }`, a NAME_VAR placeholder accepts two forms: `#name` (shortcut) looks the variable up in the metafn scope and requires type Ident; `#(expr)` lowers the inner expression in the metafn scope and requires type Ident or ExprBlob. Any other pointee kind is an error.

**Divergence:** Logos metaprogramming construct; no Rust equivalent.

**Source:** `src/compiler/sema_expr.cpp#L15569-L15625`

### `metaprog.quote-item.repeat-cursor-depth` — #(...)* repetition binds cursor placeholders by Vec nesting depth

Inside a `#(...)*` repetition group, a `#name` placeholder becomes a Cursor whose depth equals its Vec nesting (1 for `Vec<Ident>`, 2 for `Vec<Vec<Ident>>`); the variable's cursor depth d must be non-zero and ≤ the current repeat depth, else an error. Outside any repetition group, `#name` must be a scalar Ident.

**Source:** `src/compiler/sema_expr.cpp#L15582-L15606`, `src/compiler/sema_expr.cpp#L15523-L15533`

### `metaprog.quote-item.repeat-nesting-limit` — #(...) repetition nesting limited to 2 levels

`#(...)` repetition groups in `quote_item!` may nest at most 2 levels deep; deeper nesting is an error.

**Source:** `src/compiler/sema_expr.cpp#L15554-L15568`

### `metaprog.quote-item.placeholder-walk-order` — Placeholder index is fixed by source-tree DFS order

Placeholder indices are assigned by a deterministic depth-first walk of the quoted item subtrees (recursing into all pointer-valued TOM keys and array elements except NAME_VAR), and the destination rewrite mirrors the same recursion so producer indices align with placeholder slots.

**Testability:** transitive

**Source:** `src/compiler/sema_expr.cpp#L15541-L15661`

### `metaprog.quote-item.synthetic-main-module` — quote_item! produces a synthetic `package main` module

`quote_item! { item* }` constructs a synthetic AST module whose root is MODULE with NAME="main", empty PATH_PARTS, ITEMS = the deep-cloned quoted items, and SRC_LINE=1. The result is emitted as a serialized WritStatic blob carried by a `QuoteItemBlob` value.

**Divergence:** Logos metaprogramming addition (no Rust equivalent).

**Testability:** untestable

**Source:** `src/compiler/sema_expr.cpp#L15859-L15894`, `src/compiler/sema_expr.cpp#L15805-L15819`

### `metaprog.quote-item.inherit-import-scope` — quote_item! inherits the metafn's import scope

The synthetic module inherits the enclosing metafn's wildcard `use` packages, plus a self-use of the metafn's own package (if non-empty), so that unqualified names inside the quoted items resolve through the metafn's `use`-list. Each inherited package becomes one USE node carrying the full dotted package name in NAME.

**Divergence:** Logos metaprogramming addition; controls hygiene/name resolution of quoted items.

**Testability:** transitive

**Source:** `src/compiler/sema_expr.cpp#L15821-L15857`

### `metaprog.quote-item.placeholder-walk-balance` — Source and destination placeholder counts must match

The number of antiquot placeholders discovered while scanning the source items must equal the number of placeholder slots rewritten in the cloned destination tree; a mismatch is a compile error.

**Divergence:** Logos metaprogramming addition.

**Uncertainty:** This is an internal consistency invariant; user-observable only as a diagnostic.

**Testability:** untestable

**Source:** `src/compiler/sema_expr.cpp#L15797-L15802`

### `metaprog.quote-item.blob-result-type` — quote_item! evaluates to a QuoteItemBlob value

`quote_item!` evaluates to a `QuoteItemBlob` struct value with fields { template_ptr, template_size, idents_blob, blobs_blob, cursors_blob }, where template_ptr/template_size address the serialized synthetic-module blob and the *_blob fields carry the packed antiquot substitution data (null when the corresponding placeholder kind has zero occurrences).

**Divergence:** Logos metaprogramming addition.

**Testability:** untestable

**Source:** `src/compiler/sema_expr.cpp#L16133-L16144`, `src/compiler/sema_expr.cpp#L15907-L15910`

### `metaprog.quote-item.ident-antiquot-packing` — `#name`/`#(expr)` Ident antiquots are packed as Ident pointers

Each scalar Ident antiquot site (`#name` shortcut or `#(expr)` yielding Ident) contributes one `*const Ident` to the idents blob, in DFS placeholder order; a `#(expr)` form binds the lowered expression to a fresh local whose address is taken. When there are no Ident sites, idents_blob is null.

**Divergence:** Logos metaprogramming addition.

**Testability:** untestable

**Related:** `metaprog.quote-item.blob-result-type`

**Source:** `src/compiler/sema_expr.cpp#L15953-L16003`

### `metaprog.quote-item.exprblob-antiquot-packing` — ExprBlob antiquots are packed by their .ptr field

Each `#(expr)` antiquot whose lowered expression has type ExprBlob contributes one `*const u8` (the ExprBlob's `ptr` field) to the blobs blob, in DFS placeholder order; the lowered ExprBlob is bound to a local that outlives the array. When there are no ExprBlob sites, blobs_blob is null.

**Divergence:** Logos metaprogramming addition.

**Testability:** untestable

**Related:** `metaprog.quote-item.blob-result-type`

**Source:** `src/compiler/sema_expr.cpp#L16005-L16054`

### `metaprog.quote-item.cursor-repetition-packing` — Cursor (`#(...)*`) antiquots carry a per-site nesting depth

Each repetition-cursor antiquot site contributes a `*const u8` (the address of a Vec cursor variable) plus a parallel per-site depth byte: depth 1 = `Vec<Ident>`, depth 2 = `Vec<Vec<Ident>>` (nested `#(...)*`). The element type is the neutral `*const u8`; pack reads each cursor according to its depth. When there are no cursor sites, cursors_blob is null.

**Divergence:** Logos metaprogramming addition.

**Testability:** untestable

**Related:** `metaprog.quote-item.blob-result-type`

**Source:** `src/compiler/sema_expr.cpp#L16056-L16127`, `src/compiler/sema_expr.cpp#L15939-L15944`

## `quote_ty!` — type-AST quoting

### `metaprog.quote-ty.reify-type-to-struct` — quote_ty! reifies a type into a runtime Type value

`quote_ty! { T }` evaluates to a value of struct type `Type` whose fields are { kind: u32 = `__type_kind_of__::<T>()`, name: &[u8] = `__type_name_of__::<T>()`, size: i64 = `size_of::<T>()`, align: i64 = `align_of::<T>()`, uid: u64 = `__type_uid_of__::<T>()` }.

**Divergence:** A6 (Logos addition; metaprog reflection intrinsic)

**Related:** `intrinsic.type-reflect.kind`, `intrinsic.type-reflect.name`, `intrinsic.type-reflect.uid`

**Source:** `src/compiler/sema_expr.cpp#L16357-L16383`, `src/compiler/sema_expr.cpp#L16179`

### `metaprog.quote-ty.antiquot-type-var` — $ident antiquot inside quote_ty! refers to a bound Type value

An ANTIQUOT_TYPE `$x` inside `quote_ty!` lowers to a variable reference of type `Type` (the in-scope binding named `x`), instead of being reified from a static type.

**Divergence:** A6 (Logos addition)

**Source:** `src/compiler/sema_expr.cpp#L16182-L16184`, `src/compiler/sema_expr.cpp#L16314-L16316`

### `metaprog.quote-ty.tuple-antiquot` — quote_ty! tuple with antiquot lowers to __tuple_type_apply__

`quote_ty! { ($t1, $t2, ...) }` where at least one element is an antiquot lowers to `__tuple_type_apply__([p1, p2, ...])` where each pi is the per-element Type producer (var-ref for `$x`, reified `Type` literal otherwise); mixed literal/antiquot elements are permitted.

**Divergence:** A6 (Logos addition)

**Source:** `src/compiler/sema_expr.cpp#L16209-L16234`

### `metaprog.quote-ty.array-antiquot-literal-size` — quote_ty! array with antiquot element requires literal integer size

`quote_ty! { [$t; N] }` lowers to `__array_type_apply__(elem_producer, N)`; the size N MUST be a literal integer (a non-numeric/symbolic size is rejected with "array antiquot requires literal integer size").

**Divergence:** A6 (Logos addition)

**Source:** `src/compiler/sema_expr.cpp#L16238-L16263`

### `metaprog.quote-ty.pack-splice` — quote_ty! generic pack-splice lowers to __type_apply__ with runtime array

`quote_ty! { Foo<$ts...> }`, where the sole generic argument is an ANTIQUOT_PACK `$ts...`, lowers to `__type_apply__("Foo", ts)` where ts is a var-ref to a runtime `Array<Type>`. A pack-splice mixed with any other generic argument (`Foo<$t, $ts...>`) is rejected ("mixed pack-splice with other args not yet supported").

**Divergence:** A6 (Logos addition)

**Uncertainty:** Mixed-pack rejection is a current implementation limit, not a permanent language rule.

**Source:** `src/compiler/sema_expr.cpp#L16271-L16294`

### `metaprog.quote-ty.generic-inst-antiquot` — quote_ty! generic instantiation with antiquot args lowers to __type_apply__

`quote_ty! { Foo<args...> }` with at least one `$ident` antiquot among the args lowers to `__type_apply__("Foo", [elems])`, where each elem is a var-ref (for `$x`) or a reified `Type` struct literal (for a concrete type arg). Lifetime args and pack-expand args in this position are rejected ("lifetime / pack args not yet supported").

**Divergence:** A6 (Logos addition)

**Source:** `src/compiler/sema_expr.cpp#L16299-L16355`

## Metacall — explicit `comptime` invocation

### `metaprog.metacall.explicit-comptime-call` — metacall runs a compile-time callee and splices its result

`metacall <call_expr>` (METACALL, expression position) is an explicit compile-time invocation. At item position, `metacall <call_expr>;` (METACALL_ITEM) is JIT-executed by the driver; the callee must return a QuoteItemBlob, which is spliced into the surrounding item stream via `logos_emit_item_blob_subst`. After splicing, the node is rewritten to METACALL_ITEM_DONE, which sema's item-dispatch silently skips (and which is never produced by parsing).

**Testability:** behavioral

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L267`, `tools/peg_gen_cpp/grammars/logos.peg#L278-L279`

### `metaprog.metacall.forms` — metacall expression forms

`metacall` accepts exactly three operand shapes — a block (`metacall { … }`), a parenthesized expression (`metacall (e)`), or a call expression (`metacall f(…)`, including generic `f::<T>(…)` and static `Type::m(…)`) — and evaluates its argument at compile time.

**Divergence:** A1/A6 — Logos addition: metacall is the explicit compile-time evaluation operator, the Logos replacement for const-eval (no implicit const-eval).

**Related:** `metaprog.metacall.block-tail-required`

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L2599-L2604`, `tools/peg_gen_cpp/grammars/logos.peg#L2731-L2736`, `tools/peg_gen_cpp/grammars/logos.peg#L2791-L2796`, `src/compiler/sema_expr.cpp#L17084-L17088`

### `metaprog.metacall.item-decl-syntax` — Item-position metacall statement

`metacall <call_expr> ;` at item (module top-level) position invokes a metacall whose callee returns a `QuoteItemBlob`; the driver runs the JIT'd thunk, which splices the returned items into the enclosing program.

**Related:** `metaprog.metacall.explicit-comptime-call`

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L604-L610`

### `metaprog.metacall.item-position-splice` — Item-position metacall splices synthesized items

A `metacall foo();` in item position is evaluated during discovery and its result is spliced as synthesized top-level item(s) into the program.

**Source:** `src/compiler/metaprog_dispatch.hpp#L1-L3`, `src/compiler/metaprog_dispatch.hpp#L97-L98`

### `metaprog.metacall.no-nested-metacall` — metacall may not be nested inside another metacall's operand

A `metacall` operand (call args, or the inner subtree for the block/expr forms) must not contain another `metacall` node; metacall is a one-shot lift to compile time whose result is a runtime value and therefore cannot serve as a compile-time argument to an enclosing metacall. Violation is a compile error.

**Divergence:** A1/A6: metacall replaces Rust const-eval; rule has no Rust analogue.

**Related:** `metaprog.metacall.forms`

**Source:** `src/compiler/sema_expr.cpp#L17090-L17178`

### `metaprog.metacall.no-runtime-capture` — metacall block/expr form cannot capture enclosing runtime locals

In the block and parenthesized-expr forms, every VAR_REF must resolve to a name introduced inside the operand (LET/FOR/FOR_EACH binding, or a match-arm pattern binding), a module-level const, or a known function (concrete or generic). A reference to an enclosing-scope runtime local is a compile error, since the metacall is evaluated at compile time with no access to surrounding locals.

**Divergence:** A1/A6: compile-time evaluation model specific to metacall.

**Related:** `metaprog.metacall.const-resolver`

**Source:** `src/compiler/sema_expr.cpp#L17196-L17302`

### `metaprog.metacall.args-ctfe-constant` — Every argument of a metacall call form must be a compile-time constant

For the call form, each argument expression must be CTFE-evaluable to a constant literal; an argument that cannot be folded is a compile error. CALL stores arguments as a flat array, while GENERIC_CALL/STATIC_CALL wrap them as `{ ITEMS: [...] }`.

**Divergence:** A1/A6: CTFE of metacall args; replaces Rust const-eval.

**Related:** `metaprog.metacall.const-resolver`

**Source:** `src/compiler/sema_expr.cpp#L17334-L17359`

### `metaprog.metacall.const-resolver` — metacall argument CTFE resolves bare module-const idents

CTFE of metacall arguments and operands resolves a bare identifier naming a module-level const (collected into the module const-value map, including cross-package consts) to that const's value, so expressions like `metacall { THRESHOLD + 1 }` fold.

**Divergence:** A1/A6: metacall const folding.

**Related:** `metaprog.metacall.args-ctfe-constant`, `metaprog.metacall.no-runtime-capture`

**Source:** `src/compiler/sema_expr.cpp#L17311-L17332`

### `metaprog.metacall.block-tail-required` — metacall block must end in a tail expression

A `metacall { ... }` block must terminate with a tail expression (no trailing semicolon) so the metacall yields a value; a block lacking a tail expression is a compile error. The block's value type is the type of that tail expression.

**Divergence:** A1/A6.

**Related:** `metaprog.metacall.return-type`

**Source:** `src/compiler/sema_expr.cpp#L17366-L17389`

### `metaprog.metacall.return-type` — metacall result type must be primitive scalar, WritStatic, Writ, or ExprBlob

The type produced by a metacall operand must be a primitive scalar (bool; integer kinds i8/i16/i24/i32/i56/i64 and u8/u16/u24/u32/u56/u64; f32/f64; integer/float literal types), a &str / `Slice<u8>`, WritStatic, Writ (incl. `Rc<Writ>`), or ExprBlob. Any other result type is a compile error.

**Divergence:** A1/A6: WritStatic/Writ/ExprBlob returns are Logos additions.

**Related:** `metaprog.metacall.exprblob-deferred-typing`, `metaprog.metacall.writ-autofreeze`

**Source:** `src/compiler/sema_expr.cpp#L17408-L17424`

### `metaprog.metacall.exprblob-deferred-typing` — ExprBlob-returning metacall defers result typing to the post-splice pass

When a metacall returns an ExprBlob (an AST-expression fragment marker), pass-1 typing is deferred: `let X: T = metacall foo()` accepts any annotated T over an ExprBlob RHS; the actual expression type is recovered after the driver splices the blob and pass-2 sema re-lowers it.

**Divergence:** A3/A6: ExprBlob is the Logos metaprog AST-fragment return.

**Testability:** untestable

**Related:** `metaprog.writ-blob.ast-fragment-recurse`

**Source:** `src/compiler/sema_expr.cpp#L17400-L17407`

### `metaprog.metacall.writ-autofreeze` — Writ-returning metacall auto-freezes to WritStatic and is call-form only

A metacall whose operand returns a (mutable) Writ / `Rc<Writ>` is auto-frozen: user code observes the spliced value as WritStatic (the lowered expression is retyped to WritStatic). The Writ return type is supported only on the call form (`metacall foo()`); using it with the block or expr form is a compile error.

**Divergence:** A6: Writ/WritStatic is a Logos addition.

**Related:** `metaprog.metacall.return-type`

**Source:** `src/compiler/sema_expr.cpp#L17537-L17568`, `src/compiler/sema_expr.cpp#L17597-L17603`

### `metaprog.metacall.runtime-passthrough` — metacall lowers as a runtime pass-through until driver-side splice

During sema iterations a metacall lowers to its operand's lowered value (a pass-through), keeping the in-progress IR valid for borrow/type checks. The driver replaces the metacall AST node with the evaluated literal before the final non-metaprog sema pass, so this pass-through lowering never reaches code generation.

**Divergence:** A1/A6: compile-time splice model.

**Testability:** untestable

**Related:** `metaprog.metacall.return-type`

**Source:** `src/compiler/sema_expr.cpp#L17606-L17610`

### `metaprog.metacall.item-position` — Item-position metacall

`metacall <call-expr> ;` is legal at module item position (top level). The named metafunction is invoked at compile time and must return an item blob; the items it produces are spliced into the program at the metacall's position, replacing the `metacall` item itself.

```logos
metacall gen_items::<Foo>();
```

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L581-L582`, `src/compiler/sema_impl.hpp#L4044-L4049`

## Metacall at item position

### `metaprog.metacall-item.callee-form` — Item-position metacall callee must be a call expression

`metacall <expr>;` at item position requires `<expr>` to be a free-function call, a generic (turbofish) call, or a static-method call; any other expression form is rejected.

**Testability:** diagnostic

**Source:** `src/compiler/sema_expr.cpp#L19980-L19989`

### `metaprog.metacall-item.args-const-eval` — Item-position metacall arguments must be compile-time constants

Every argument in an item-position `metacall <callee>(args...)` is evaluated via CTFE (with module-level named consts resolvable through `lookup_const`); an argument that fails to fold to a constant is rejected.

**Testability:** diagnostic

**Source:** `src/compiler/sema_expr.cpp#L19991-L20034`

### `metaprog.metacall-item.return-type` — Item-position metacall callee must return QuoteItemBlob or ItemList

The callee of an item-position `metacall` (after ordinary lowering/type-checking of the inner call) must have return type `QuoteItemBlob` or `ItemList`; any other return type is rejected.

**Testability:** diagnostic

**Source:** `src/compiler/sema_expr.cpp#L20038-L20047`

### `metaprog.metacall-item.turbofish-wstatic-source` — WritStatic turbofish type-args re-rendered as source for the metacall thunk

When building the literal call text for an item-position `metacall`'s generic/static call, a turbofish type-argument that is a `WritStatic` literal (`Foo::<@{...}>`) is re-rendered from its parsed writ-literal AST back into `@{...}` source syntax, rather than through the ordinary type-to-string path (which would emit the non-reparseable internal form `@hs_<hex>`).

**Uncertainty:** Internal `@hs_<hex>` rendering form is inferred from the comment; the type_str() implementation itself is outside this slice.

**Testability:** behavioral

**Source:** `src/compiler/sema_expr.cpp#L20050-L20126`, `src/compiler/sema_expr.cpp#L20140-L20151`

## Item emission from macro/metacall results

### `metaprog.item-emit.itemlist-iteration` — ItemList-returning item macro/metacall emits each contained item

When an item-position fn_macro/token_macro call or item-position `metacall` resolves to a callee returning `ItemList`, the compiler synthesises a void thunk that iterates `ItemList.blobs` and, for each `QuoteItemBlob` element, emits it into the surrounding module (substituting captured identifiers) then releases its idents/blobs/cursors buffers.

**Testability:** behavioral

**Source:** `src/compiler/sema_expr.cpp#L19709-L19736`, `src/compiler/sema_expr.cpp#L19904-L19930`, `src/compiler/sema_expr.cpp#L20196-L20222`

### `metaprog.item-emit.quoteitemblob-single` — QuoteItemBlob-returning item macro/metacall emits a single item

When an item-position fn_macro/token_macro call or item-position `metacall` resolves to a callee returning `QuoteItemBlob` (not `ItemList`), the compiler synthesises a void thunk that emits that single blob into the surrounding module (substituting captured identifiers) then releases its idents/blobs/cursors buffers.

**Testability:** behavioral

**Source:** `src/compiler/sema_expr.cpp#L19737-L19757`, `src/compiler/sema_expr.cpp#L19931-L19950`, `src/compiler/sema_expr.cpp#L20223-L20241`

## Macro/handler discovery

### `metaprog.discovery.entry-body-skipped` — Entry-file function bodies are skipped during discovery

During the metaprog discovery pass, function bodies of the entry file are not lowered (metaprog_mode); only signatures/items needed for trigger discovery are processed. Name mangling during discovery must stay consistent with the final pass.

**Uncertainty:** Exact set of skipped work is described only via comment.

**Testability:** untestable

**Source:** `src/compiler/metaprog_dispatch.hpp#L94-L95`, `src/compiler/metaprog_dispatch.hpp#L82-L87`

## Macro expansion dispatch

### `metaprog.dispatch.fixpoint-iteration` — Metaprog discovery iterates to fixpoint, capped at 16

Metaprogram item-generation runs as a discovery loop: each iteration re-lowers and fires triggers/metacalls that may emit new items; the loop repeats until an iteration emits nothing new (fixpoint), bounded by a hard cap of 16 iterations.

**Uncertainty:** Header doc comment; the loop body lives in the .cpp. Behavior of exceeding the cap (error vs silent stop) is not specified here.

**Testability:** transitive

**Source:** `src/compiler/metaprog_dispatch.hpp#L94-L100`

## Macro handler registration

### `metaprog.handler.register` — #[metaprog_handler("trigger")] registers a hook

`#[metaprog_handler("trigger")]` on a function registers `(trigger, fn_name)`; the trigger is the annotation's first positional string-literal argument. The host driver later scans user items for a matching `#[trigger]` annotation and invokes the registered fn on each. A missing/unresolvable trigger string is recorded as the sentinel `<missing>` so a later validation pass can surface the misuse as a diagnostic.

**Divergence:** A6: metaprog handler registration, Logos-only mechanism.

**Source:** `src/compiler/sema_collect.cpp#L1837-L1873`

## Annotation-triggered expansion

### `metaprog.trigger.annotation-scan` — top-level annotations matching a registered handler trigger are recorded as metaprog targets

After all modules complete field/variant/signature collection (so the metaprog-handler trigger registry is final), the compiler scans top-level ANNOTATION items in user (non-binary) ASTs; one or more consecutive annotations immediately preceding a non-annotation item, whose name matches a registered handler trigger, are each recorded as an (ast-index, item-byte-offset, trigger-name) metaprog target for the driver to dispatch against that following item.

**Testability:** transitive

**Source:** `src/compiler/sema_collect.cpp#L577-L626`

## Derive-style annotation triggers

### `metaprog.derive.trigger-may-emit-items` — Derive/handler hooks emit sibling items into the program

An annotated item bearing a `#[derive_*]` / metaprog-handler attribute invokes the corresponding handler during discovery; the handler may synthesize and splice additional top-level items into the program (alongside the annotated item).

**Source:** `src/compiler/metaprog_dispatch.hpp#L94-L98`, `src/compiler/metaprog_dispatch.hpp#L1-L2`

### `metaprog.derive.no-rust-derive-syntax` — `#[derive(...)]` is rejected; use per-trait triggers

The Rust-style `#[derive(Trait, ...)]` attribute (a `derive` annotation carrying args) is not Logos surface syntax and is an error. Logos uses one trigger annotation per derive, `#[derive_<trait>]`, paired with an in-scope `#[metaprog_handler("derive_<trait>")]` function.

**Divergence:** Logos replaces Rust `#[derive(...)]` with `#[derive_<trait>]` + `#[metaprog_handler]`.

**Source:** `src/compiler/sema_impl.hpp#L1762-L1774`

## Annotations (`#[...]`) — literal/argument forms and targets

### `metaprog.annot.literal-kinds` — Annotation argument literal kinds

An annotation argument literal is one of: integer, float, bool, string, enum-variant (Name::Variant), or array of literals. Arrays are parsed recursively element-by-element.

```logos
#[A(42)]
```
```logos
#[A("s")]
```
```logos
#[A(Color::Red)]
```
```logos
#[A([1, 2, 3])]
```

**Source:** `src/compiler/sema.cpp#L7165-L7229`

### `metaprog.annot.float-suffix-strip` — Float annotation literal: strip underscores and f32/f64 suffix

A float annotation literal has all '_' separators removed and an optional trailing 'f32'/'f64' suffix stripped before parsing to a double.

```logos
#[A(3.14f64)]
```
```logos
#[A(1_000.5)]
```

**Source:** `src/compiler/sema.cpp#L7173-L7180`

### `metaprog.annot.string-escapes` — String annotation literal escape decoding

A quoted string annotation literal has surrounding quotes stripped and escapes \n \t \r \\ \" \0 decoded; an unrecognised escape is preserved verbatim (backslash + char). A raw string r"..." strips the r" prefix and trailing quote with no escape processing.

```logos
#[A("line\n")]
```
```logos
#[A(r"raw\n")]
```

**Source:** `src/compiler/sema.cpp#L7186-L7217`

### `metaprog.annot.no-args-bare` — Bare annotation #[A] takes no arguments

An annotation written #[A] (no ARGS, no VALUE) produces an instance with an empty argument list.

```logos
#[A]
```

**Source:** `src/compiler/sema.cpp#L7249-L7250`

### `metaprog.annot.eq-value-first-field` — #[A = lit] binds first field

An annotation #[A = lit] maps the single value to the annotation datatype's first declared field; if the datatype has no fields it is an error ('annotation takes no arguments').

```logos
#[A = 5]
```

**Source:** `src/compiler/sema.cpp#L7252-L7261`

### `metaprog.annot.positional-by-order` — Positional annotation args bind by declaration order

Positional arguments in #[A(arg, ...)] bind to the annotation datatype's fields in declaration order; supplying more positional args than fields is an error ('takes at most N positional args').

```logos
#[A(1, 2)]
```

**Source:** `src/compiler/sema.cpp#L7282-L7292`

### `metaprog.annot.named-by-field-name` — Named annotation args bind by field name

A named argument key=value in #[A(...)] binds to the field whose name equals the key; an unknown field name is an error ('has no field') but does not abort parsing of remaining args.

```logos
#[A(name = 3)]
```

**Source:** `src/compiler/sema.cpp#L7271-L7281`

### `metaprog.annot.user-annotation-requires-marker` — User annotation NAME must be an #[annotation]-marked datatype

An annotation whose name is not a compiler-internal key is treated as a user annotation only if NAME resolves to a registered datatype carrying the IS_ANNOTATION_TYPE marker; otherwise it is silently ignored.

**Related:** `metaprog.annot.struct-flags-set-only`

**Source:** `src/compiler/sema.cpp#L7327-L7336`

### `metaprog.annot.accumulate-until-item` — Annotations and doc-comments accumulate until next item

Annotation nodes accumulate in a pending buffer and are consumed by the next non-annotation item; doc-comment lines/blocks (///, /**, //!, /*!) similarly accumulate. Both buffers are cleared after each item.

**Testability:** transitive

**Source:** `src/compiler/sema.cpp#L7304-L7305`, `src/compiler/sema.cpp#L7424-L7458`, `src/compiler/sema.cpp#L8055-L8058`

### `metaprog.annot.type-code-on-struct` — #[type_code=N] on struct/datatype sets type code

#[type_code=N] on a struct or datatype sets the type's TYPE_CODE to N and registers N under the fully-qualified name (pkg::Name) in the explicit-type-code table so `type_code_of::<T>()` resolves cross-package.

```logos
#[type_code=42] datatype Foo { }
```

**Source:** `src/compiler/sema.cpp#L7313-L7323`

### `metaprog.annot.tag-dispatch-on-trait` — #[tag_dispatch(system)] on trait sets dispatch system name

#[tag_dispatch(system_name)] on a trait records system_name as the trait's TAG_DISPATCH_SYSTEM, identifying the tagged-dispatch system the trait participates in.

**Source:** `src/compiler/sema.cpp#L7367-L7376`

### `metaprog.annot.type-code-on-trait-genos` — #[type_code=N] on trait marks it a genos

#[type_code=N] on a trait sets the trait's TYPE_CODE: the code names the logical datatype family, and each `impl Trait for Eidos` propagates it to the target struct during lowering.

**Source:** `src/compiler/sema.cpp#L7377-L7383`

### `metaprog.annot.type-code-on-template-genos-forbidden` — #[type_code] forbidden on parametric (template) genos

#[type_code=N] on a parametric genos (trait with non-empty type params) is an error; the code must be attached to a concrete specialization, else every specialization would collide in the same tag-system slot.

**Source:** `src/compiler/sema.cpp#L8040-L8048`

### `metaprog.annot.struct-flags-set-only` — Structural struct flags are set-only (monotonic)

Structural attributes #[zone_mut], #[zoned2], #[rel_ptr], #[self_describing], #[borrow_carrying] each set the corresponding boolean struct flag when present; a present flag can only set, never clear, the base value.

**Testability:** untestable

**Source:** `src/compiler/sema.cpp#L7344-L7360`

### `metaprog.annot.value-int-or-enum-variant` — Attribute `= value` must be an integer or enum variant

The right-hand value of an attribute `#[name = V]` read as an integer must be either an integer literal (`#[name = 123]`) or an enum variant path (`#[name = Enum::Variant]`, resolving to that variant's discriminant). Any other value form is an error; an unknown enum or unknown variant is also an error.

```logos
#[type_code = 42]
```
```logos
#[type_code = Tag::Foo]
```

**Source:** `src/compiler/sema_impl.hpp#L1372-L1398`

### `metaprog.annot.builtin-target-validity` — Builtin attribute must match its allowed targets

A builtin attribute is valid only on item kinds in its allowed target set; applying a builtin attribute to a target outside that set is an error. Non-builtin attribute names are not diagnosed at this phase (may be user annotations, cross-module metaprog triggers, or deferred typo checks).

**Source:** `src/compiler/sema_impl.hpp#L1775-L1790`

### `metaprog.annot.type-code-not-on-generic` — `#[type_code]` forbidden on generic items

`#[type_code]` may not be applied to an item that has type parameters; a type code must be assigned per concrete instantiation, not on the generic definition.

**Source:** `src/compiler/sema_impl.hpp#L1792-L1797`

### `metaprog.annot.type-code-reserved-range` — type_code values 1..128 reserved for stdlib

Explicit `#[type_code=N]` values with 1 <= N <= 128 are reserved for stdlib primitive tags (TypeTagSystem); user code outside package `std`/`std.*` using a reserved value is warned and should use codes >= 129.

**Testability:** untestable

**Source:** `src/compiler/sema_impl.hpp#L1798-L1811`

## `cfg!`/`#[cfg(...)]` — configuration predicates

### `metaprog.cfg.key-value-predicates` — cfg key=value predicate resolution

A cfg key=value predicate matches against compile-target metadata: target_arch, target_os, target_endian, target_family, target_pointer_width resolve to the host/target platform values; `feature = "name"` matches iff name is in the active feature set. Any unknown key evaluates to false.

**Divergence:** Unknown-key-false matches Rust per inline comment.

**Source:** `src/compiler/sema.cpp#L3507-L3517`, `src/compiler/sema.cpp#L3572-L3575`

### `metaprog.cfg.bare-flag-predicates` — cfg bare-identifier flag resolution

A cfg bare identifier matches as: `unix`/`windows` against target_family; `test`/`debug_assertions` against the feature set; otherwise as a feature-like flag checked against the active feature set.

**Source:** `src/compiler/sema.cpp#L3519-L3528`, `src/compiler/sema.cpp#L3582`

### `metaprog.cfg.combinators` — cfg all/any/not combinators and boolean literals

cfg predicates compose: `all(p...)` is the AND of its children, `any(p...)` the OR, `not(p)` requires exactly one child and negates it (else error/false). The literals `cfg(true)`/`cfg(false)` evaluate to true/false directly. Unknown combinators evaluate to false / raise an error in attribute position.

**Divergence:** cfg(true)/cfg(false) per Rust 1.80 RFC 3695 (noted inline).

**Source:** `src/compiler/sema.cpp#L3553-L3582`, `src/compiler/sema.cpp#L3692-L3708`, `src/compiler/sema.cpp#L3721-L3737`

### `metaprog.cfg.attr-multi-arg-implicit-and` — cfg attribute multi-arg implicit AND

In `#[cfg(...)]` attribute position, a top-level multi-argument list is an implicit AND of its arguments; `#[cfg]` with no args matches (true).

**Divergence:** Multi-arg implicit AND matches Rust (noted inline).

**Source:** `src/compiler/sema.cpp#L3654-L3673`

### `metaprog.cfg.cfg-attr-splice` — cfg_attr predicate-gated attribute splicing and item drop

`#[cfg_attr(pred, attrs...)]` activates first: when pred is true its wrapped attrs are spliced into the item's annotation list, when false the entry is dropped (wrapped attrs are NOT re-fed to cfg_attr). After activation, every plain `#[cfg(...)]` is evaluated; an item is dropped from compilation iff any of its cfg predicates is false. The drop gate applies uniformly to both the collection and lowering walks.

**Source:** `src/compiler/sema.cpp#L3606-L3652`

## Built-in macros

### `metaprog.builtin-macro.vec-desugar` — `vec![...]` desugars via a synthesized `vec_from_arr` call

The `vec![...]` built-in macro's synthesized body is re-parsed as the tail expression of an ephemeral `fn __f() -> i32 { <wrap_body> }` and lowered in the current context, routing `vec![e1, e2, ..]` through a synthesized `vec_from_arr([e1, e2, ..])` call; a re-parse failure produces `error_expr()` after a diagnostic.

**Testability:** behavioral

**Source:** `src/compiler/sema_impl.hpp#L4032-L4039`

## Template declarations

### `metaprog.template.decl` — Template declaration

`template <item>` wraps a struct/enum/datatype/trait/impl/fn declaration as inert data (an AST blob) rather than a real binding; the inner names are never registered by sema, so referencing the template as a type yields the ordinary unknown-type diagnostic. Templates are consumed later by metafunctions via apply/metacall.

```logos
template struct Pair<A,B> { a: A, b: B }
```

**Divergence:** No Rust equivalent.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L625-L640`

## Variadic tuple intrinsics

### `metaprog.variadic.tuple-all-eq` — `__tuple_all_eq__::<T>(a, b)` expands to an &&-chain of elementwise eq

__tuple_all_eq__ with type-arg T substituted to a concrete Tuple and exactly 2 args (a, b) expands to the left-associated `&&`-chain of per-element equality a.i.eq(&b.i): a Tuple-kind element inlines a recursive chain over the nested field refs (never re-emits __tuple_all_eq__, since mono does not re-process synthesized intrinsics); a Slice-kind element (canonicalized to element name "str" when it is `Slice<u8>`) is compared via a direct 2-arg by-value free-function call; any other element resolves a callee symbol by scanning out_.functions then in_.functions for a name containing `<elem-type-str>__eq__f__` at a `.`-delimited-or-leading boundary (falling back to `<elem-type-str>__eq`) and calls it as `a.eq(&b)` via method_call. When T is missing/non-Tuple or fewer than 2 args are present, the intrinsic degenerates to the boolean literal `true` (also the result for an empty tuple).

**Divergence:** A6

**Uncertainty:** Symbol resolution by substring scan is an implementation detail; the observable language rule is the &&-chain elementwise-Eq semantics.

**Testability:** behavioral

**Source:** `src/compiler/mono_clone.cpp#L2488-L2584`

### `metaprog.variadic.tuple-each-field-debug` — `__tuple_each_field_debug__::<T>(self, f)` expands variadic tuple Debug

__tuple_each_field_debug__ with type-arg T substituted to a concrete Tuple and exactly 2 args (self, f: &mut Formatter) expands to a `fmt_seq`-chained Result: fmt_tuple_open(f), then per element i in order: fmt_tuple_sep(f) folded in when i>0, then either (Tuple-kind element) an inline recursive build over the nested field ref, or (otherwise) a Debug-fmt call resolved as a symbol containing `<elem-type-str>__Debug__fmt__f__` else `<elem-type-str>__fmt__f__` at a `.`-delimited-or-leading boundary, else literally `<elem-type-str>__fmt` (elem-type-str = type_str(elem), with `Slice<u8>` canonicalized to "str"), invoked as `field.fmt(f)`; finally fmt_tuple_close1(f) if the tuple arity is 1, else fmt_tuple_close(f); each step is folded into the running chain via fmt_seq. Every reuse of the shared `f` argument after its first use goes through a reborrow wrapper (AddrOfTemp(Deref(f))), since `&mut Formatter` is a move-type under borrow-check and a bare reuse would consume it on the first call. When T is missing/non-Tuple or fewer than 2 args are present, the intrinsic degenerates to a single close-call.

**Divergence:** A6

**Testability:** behavioral

**Source:** `src/compiler/mono_clone.cpp#L2591-L2683`

## `format!`/`println!` family — macro expansion shape

### `metaprog.format.literal-string-checked` — Format-family macros validate a literal format string at compile time

For the format family (`format`, `print`, `println`, `eprint`, `eprintln`, `panic`, `format_args_str`) and the write family (`write`, `writeln`), when the format-string argument is a string literal it is parsed and validated at sema time (brace balance, placeholder structure). A non-literal format-string argument skips the check (mirroring Rust `format_args!`). The format string is arg[0] for the format family and arg[1] for the write family.

**Source:** `src/compiler/sema_expr.cpp#L18683-L18728`, `src/compiler/sema_expr.cpp#L18701-L18722`

### `metaprog.format.arity-check` — Format placeholder count must match the argument count

When no explicit `{N}` index appears, the number of value placeholders in the format string must equal the number of value arguments provided (args after the format string, and after the sink for write/writeln). When explicit indices are used, the number of value arguments must be at least max(explicit-index)+1. Otherwise the macro is rejected.

**Source:** `src/compiler/sema_expr.cpp#L18754-L18792`

### `metaprog.format.placeholder-trait-dispatch` — Each format placeholder dispatches to its format-trait method

Each format placeholder lowers to a call dispatching to the format trait selected by the placeholder's trait kind (e.g. `{}` -> Display, `{:?}` -> Debug, plus hex/oct/bin/exp variants), invoked on the argument value with a `&mut Formatter`. Per-placeholder format spec fields (fill, alignment, sign, alternate, zero-pad, width, precision) are applied to the Formatter before each dispatch and reset between placeholders.

**Source:** `src/compiler/sema_expr.cpp#L18843-L18918`

### `metaprog.format.expansion-shape` — Format-family expansion builds a String/Formatter block

format!/format_args_str! expand to a block yielding a `String` built via a `Formatter` over a `__buf`. The print family (`println`/`print`/`eprintln`/`eprint`) appends a drain of the buffer to stdout/stderr; `panic!` drains to a panic. write!/writeln! build a `Formatter` directly over the sink (via `(sink).as_formatter()`), stream placeholders into it, and yield `Result<(),Error>` (always Ok — per-placeholder errors are discarded); writeln! additionally writes a trailing newline.

**Source:** `src/compiler/sema_expr.cpp#L18828-L18941`

### `metaprog.format.requires-format-arg` — Format/write macros require a format-string argument

A format-family or write-family invocation must supply at least the format-string argument; an empty argument list is an error.

**Testability:** untestable

**Source:** `src/compiler/sema_expr.cpp#L19011-L19014`

## Format-spec mini-language

### `metaprog.fmt.trait-spec-types` — Format-spec type characters select a formatting trait

In a `format!`-family format string, a placeholder's trailing spec type char selects a formatting trait: no type / absent = Display (`{}`), `?` = Debug (`{:?}`), `x` = LowerHex, `X` = UpperHex, `o` = Octal, `b` = Binary, `e` = LowerExp, `E` = UpperExp. Each placeholder lowers to a call of that trait's method on the argument rather than a variadic runtime path.

**Uncertainty:** LowerExp/UpperExp marked '(future)' in source; presence in enum implies recognized but possibly not yet lowered.

**Source:** `src/compiler/sema_fmt.hpp#L20-L29`, `src/compiler/sema_fmt.hpp#L52`

### `metaprog.fmt.spec-alignment` — Format-spec alignment characters

A format placeholder spec may carry an alignment: `<` = Left, `>` = Right, `^` = Center; when unspecified the default alignment is chosen per formatting trait.

**Source:** `src/compiler/sema_fmt.hpp#L31-L36`, `src/compiler/sema_fmt.hpp#L46`

### `metaprog.fmt.spec-fill-and-flags` — Format-spec fill, sign, alternate, and zero flags

A format placeholder spec supports: a fill char (default space) used with alignment; a sign flag (`+` = always show sign, default none); an alternate flag `#` (emit base prefix `0x`/`0o`/`0b`); and a zero flag `0` (pad with zeros, overridden by an explicit fill char).

**Source:** `src/compiler/sema_fmt.hpp#L38-L42`, `src/compiler/sema_fmt.hpp#L45-L49`

### `metaprog.fmt.spec-width-precision` — Format-spec width and precision

A format placeholder spec may set width and precision; both are unset by default (sentinel -1), and when set precision >= 0 denotes an exact precision.

**Source:** `src/compiler/sema_fmt.hpp#L50-L51`

### `metaprog.fmt.placeholder-arg-selection` — Placeholder argument selection: positional, explicit index, or named

A format placeholder binds to an argument in one of three modes: positional auto-counter (`{}` consuming 0,1,2,… in order), explicit index (`{N}`), or named (`{name}`). Arity validation against positional args applies only when no placeholder uses the named or explicit-index form.

**Source:** `src/compiler/sema_fmt.hpp#L55-L66`, `src/compiler/sema_fmt.hpp#L71-L75`

### `metaprog.fmt.string-segmentation` — Format string parses into literal text + typed placeholder segments

A format string body (without surrounding quotes) parses into an ordered sequence of segments, each either a literal-text run or a placeholder (carrying its argument selector and spec). Parsing is best-effort: a soft issue emits a diagnostic and continues, while a hard parse error halts at the broken placeholder and sets a failure flag.

**Source:** `src/compiler/sema_fmt.hpp#L68-L89`

## Writ literal capture (rodata embedding)

### `metaprog.writ-lit.size-prefixed-rodata` — Capture-free @-literal lowered to size-prefixed rodata

An @-literal (Writ literal) without runtime captures is serialized to a constant blob laid out as `[u64 little-endian size][bytes]`; the materialized handle points to the payload (8 bytes past the size prefix), so `size()` reads `*(ptr - 8)`.

**Source:** `src/compiler/mlir_gen_expr.cpp#L6151-L6196`, `src/compiler/mlir_gen_expr.cpp#L6234-L6267`

### `metaprog.writ-lit.content-keyed-dedup` — Identical @-literal blobs share one global (one address)

Two capture-free @-literals with byte-identical serialized (size-prefixed) content share a single rodata global, and therefore compare equal by address; capture-bearing @-literals are never deduplicated.

**Source:** `src/compiler/mlir_gen_expr.cpp#L6199-L6202`, `src/compiler/mlir_gen_expr.cpp#L6247-L6249`, `src/compiler/mlir_gen_expr.cpp#L6270-L6271`

### `metaprog.writ-lit.capture-zone-alloc-kinds` — Capture kinds requiring zone allocation

An @-literal capture requires zone allocation (rather than an inline WAny word) iff its type is F64/F32/FloatLit, a raw pointer (treated as null-terminated C-string), a slice `&[u8]`/str (ptr+len), or struct `StringView`; all other (scalar/AnyVal) captures are stored as inline WAny value words.

**Testability:** untestable

**Source:** `src/compiler/mlir_gen_expr.cpp#L6311-L6324`, `src/compiler/mlir_gen_expr.cpp#L6404-L6459`

### `metaprog.writ-lit.float-capture-widens-to-f64` — Float captures widen to f64

A zone-allocated float capture is stored as f64: an f32 value is widened via float-extension, and a FloatLit (untyped float literal) defaults to f64.

**Testability:** untestable

**Source:** `src/compiler/mlir_gen_expr.cpp#L6407-L6422`

## Writ AST-blob fragments

### `metaprog.writ-blob.ast-fragment-recurse` — WRIT_BLOB carrying an AST-category root lowers as that expression

A WRIT_BLOB whose serialized root TinyMap has schema category CAT_AST and whose variant code is a supported expression node (BINOP, LIT_INT, LIT_BOOL, LIT_STR, VAR_REF, CALL, PAREN_EXPR, UNARY, FIELD_READ, METHOD_CALL, CAST, INDEX_READ, STRUCT_LIT, ARR_LIT, TUPLE_LIT, BLOCK, BLOCK_STMT, IF) is lowered by recursively type-checking that root node as an ordinary expression, yielding its recovered expression type. The blob's arena is retained for the lifetime of sema.

**Divergence:** A6: Writ/metaprog is a Logos addition (ExprBlob AST fragments spliced from metafunctions).

**Related:** `metaprog.writ-blob.opaque-static-fallback`

**Source:** `src/compiler/sema_expr.cpp#L17019-L17050`

### `metaprog.writ-blob.opaque-static-fallback` — Non-AST WRIT_BLOB lowers to an opaque WritStatic literal

A WRIT_BLOB whose root is null, non-TinyMap, or not of an AST expression category is lowered to an opaque data literal of type WritStatic carrying the raw blob bytes verbatim.

**Divergence:** A6: WritStatic is a Logos addition.

**Testability:** untestable

**Related:** `metaprog.writ-blob.ast-fragment-recurse`

**Source:** `src/compiler/sema_expr.cpp#L17056-L17060`

## Writ type embedding

### `metaprog.writ-type.embed-as-first-class` — `<type:T>` embeds a Type as a first-class Writ value

`<type:T>` inside a Writ `@`-literal (WRIT_TYPE_LIT, NAME = bare type identifier) is sema-embedded as a first-class Writ Type value, i.e. a type usable as ordinary literal data rather than only in type position.

**Uncertainty:** Comment marks this "slice 1: IDENT only" — the grammar accepted here may not cover the full type-expression surface for this position.

**Testability:** behavioral

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L280`

## `AnyVal` inline value-form encoding

### `metaprog.anyval.bool-value-form` — AnyVal bool value-form encoding

A `bool` coerced to an inline AnyVal word encodes as `(b << 8) | 5` where b in {0,1} (low byte = (WA_BOOL=2)<<1 | 1 = 5).

**Uncertainty:** Bit-layout is a data-substrate (Writ/Memoria) encoding, not a Rust-language construct.

**Source:** `src/compiler/mlir_gen_expr.cpp#L6002-L6010`

### `metaprog.anyval.int-i24-value-form` — AnyVal small-integer value-form (i24 niche)

Any integer type of width <=32 bits (i8/i16/i32/u8/u16/u32/i24/u24) coerced to an inline AnyVal word encodes as `((v & 0xFFFFFF) << 8) | 0x2F` (24-bit payload, type tag 0x2F).

**Source:** `src/compiler/mlir_gen_expr.cpp#L6011-L6022`

### `metaprog.anyval.int64-truncates-to-i24` — AnyVal 64-bit integer truncates to 24 bits

An i64/u64 coerced to an inline AnyVal word is truncated to its low 24 bits and embedded as the i24 niche `((v & 0xFFFFFF) << 8) | 0x2F`; values outside [-2^23, 2^23) lose their high bits in the inline form.

**Uncertainty:** Lossy truncation is the inline-AnyVal fallback; full-range values require the zone-alloc (C5) path elsewhere.

**Source:** `src/compiler/mlir_gen_expr.cpp#L6023-L6032`

### `metaprog.anyval.float-ptr-not-inline-capturable` — Floats and pointers are not inline-capturable as AnyVal

F32/F64 and pointer/reference types (Ptr/Ref/MutRef) have no inline AnyVal value-form; coercing one yields a null (zero) AnyVal word, and such types must instead use zone-allocated capture encoding.

**Testability:** untestable

**Source:** `src/compiler/mlir_gen_expr.cpp#L6033-L6040`

### `metaprog.anyval.identity-passthrough` — AnyVal value passes through as its first word

Coercing a value already of struct type `AnyVal` to an AnyVal word extracts its leading word (field 0) unchanged.

**Testability:** untestable

**Source:** `src/compiler/mlir_gen_expr.cpp#L6041-L6048`

## `WAny` inline value-form encoding

### `metaprog.wany.bool-value-form` — WAny bool value-form encoding

A `bool` coerced to an 8-byte WAny value word encodes as `(b << 8) | 5` (low byte = (WA_BOOL=2)<<1 | 1).

**Source:** `src/compiler/mlir_gen_expr.cpp#L6055-L6073`

### `metaprog.wany.int-i56-value-form` — WAny integer value-form (i56 niche)

Any integer type up to 64 bits (i8..i64/u8..u64/i24/u24) coerced to an 8-byte WAny word encodes as `(v << 8) | 3` (payload in the high 56 bits, tag = (WA_I56=1)<<1 | 1 = 3).

**Source:** `src/compiler/mlir_gen_expr.cpp#L6074-L6082`

### `metaprog.wany.identity-passthrough` — WAny passthrough and AnyVal zero-extension into WAny

A value already of enum type `WAny` passes its niche word through unchanged (extracted if struct-typed). A legacy struct `AnyVal` coerced to WAny is its leading 4-byte word zero-extended to 64 bits (the i24/bool Pod encodings coincide in the low 32 bits).

**Testability:** transitive

**Source:** `src/compiler/mlir_gen_expr.cpp#L6083-L6104`

## Type-reflection intrinsics

### `metaprog.type-intrinsic.apply-generic` — __apply_generic__ instantiates a template struct type

For __apply_generic__(tmpl_name, arg0..argN), each arg expr is resolved to a compile-time TypeRef via the type-producer-recover protocol (unresolvable arg aborts compilation with a diagnostic); the intrinsic builds LogosType::Struct{name=tmpl_name, type_args=[recovered...]} (pkg inherited from the matching struct def in out_.structs), interns the type, and replaces the call with that type's `Type` reflection value.

**Divergence:** A6

**Testability:** behavioral

**Source:** `src/compiler/mono_clone.cpp#L2191-L2239`

### `metaprog.type-intrinsic.type-producer-recover` — Type-producer recovery protocol for type-apply intrinsics

To resolve a LIR sub-expr to a compile-time TypeRef (shared by __apply_generic__/__tuple_type_apply__/__array_type_apply__): (1) chase through VarRef by looking up type_let_inits_, up to 8 hops; (2) if the result is a Call to __typelist_nth__ or __typelist_head__, substitute its type-arg[0] (a type-list) through the current subst map and index into its type_args pack at the literal-int index given as __typelist_nth__'s value arg (__typelist_head__ implicitly index 0); out-of-range index yields no type; (3) if the result is a StructLit with a field named "uid" whose initializer is a Call to __type_uid_of__, recover that call's type-arg[0] substituted through the subst map; (4) otherwise recovery fails (empty TypeRef).

**Divergence:** A6

**Testability:** transitive

**Source:** `src/compiler/mono_clone.cpp#L2152-L2189`, `src/compiler/mono_clone.cpp#L2255-L2315`

### `metaprog.type-intrinsic.tuple-type-apply` — __tuple_type_apply__([Type;N]) builds a tuple type

__tuple_type_apply__(arr) requires arg0 to chase to an ArrLit (a non-ArrLit is a compile-time abort); each element is resolved via type-producer-recover (unresolvable element aborts); the intrinsic yields LogosType::Tuple{tuple_elems=[recovered...]}'s `Type` reflection value.

**Divergence:** A6

**Testability:** behavioral

**Source:** `src/compiler/mono_clone.cpp#L2244-L2246`, `src/compiler/mono_clone.cpp#L2316-L2341`

### `metaprog.type-intrinsic.array-type-apply` — __array_type_apply__(Type, N) builds an array type

__array_type_apply__(elem, size) requires elem to resolve via type-producer-recover (unresolvable is a compile-time abort) and size (after chase) to be a LitInt literal (a non-literal is a compile-time abort); the intrinsic yields LogosType::Array{elem=recovered, arr_size=size}'s `Type` reflection value.

**Divergence:** A6

**Testability:** behavioral

**Source:** `src/compiler/mono_clone.cpp#L2244-L2246`, `src/compiler/mono_clone.cpp#L2342-L2361`

### `metaprog.type-intrinsic.type-reflection-value` — Type reflection value shape and uid

A reflected `Type` value (yielded by __apply_generic__/__tuple_type_apply__/__array_type_apply__) is the struct literal `Type{kind:u32=T.kind(), name:&[u8]=type_str(T), size:i64=size_of(T), align:i64=align_of(T), uid:u64}` where `uid = type_hash_64bit(type_hash_23(type_id_canon(T)))`; T is registered into out_.type_pool and uid_to_type_[uid]=T so a later __type_uid_of__ lookup on that uid recovers T.

**Divergence:** A6

**Testability:** transitive

**Source:** `src/compiler/mono_clone.cpp#L2213-L2238`, `src/compiler/mono_clone.cpp#L2364-L2387`

## Enum-reflection intrinsics

### `metaprog.enum-intrinsic.variant-count-of` — `__variant_count_of__::<E>()` yields variant count

__variant_count_of__ with type-arg E evaluates to a literal i64 equal to the variant count of E's enum definition when E resolves to a known LogosType::Enum with a matching def in in_.enums, else 0.

**Divergence:** A6

**Testability:** behavioral

**Source:** `src/compiler/mono_clone.cpp#L2395-L2417`

### `metaprog.enum-intrinsic.variant-names-of` — `__variant_names_of__::<E>()` yields variant names

__variant_names_of__ with type-arg E evaluates to a fixed-size array literal of each variant's name (string literal), in declaration order; empty array if E does not resolve to a known enum def.

**Divergence:** A6

**Testability:** behavioral

**Source:** `src/compiler/mono_clone.cpp#L2395-L2407`, `src/compiler/mono_clone.cpp#L2421-L2425`, `src/compiler/mono_clone.cpp#L2470-L2477`

### `metaprog.enum-intrinsic.variant-payload-counts-of` — `__variant_payload_counts_of__::<E>()` yields per-variant payload arities

__variant_payload_counts_of__ with type-arg E evaluates to a fixed-size array literal of i64, one per variant in declaration order, each equal to that variant's payload-field count; empty array if E does not resolve to a known enum def.

**Divergence:** A6

**Testability:** behavioral

**Source:** `src/compiler/mono_clone.cpp#L2426-L2431`, `src/compiler/mono_clone.cpp#L2470-L2477`

### `metaprog.enum-intrinsic.variant-payload-types-flat-of` — `__variant_payload_types_flat_of__::<E>()` yields flattened payload-type reflections

__variant_payload_types_flat_of__ with type-arg E evaluates to a flat fixed-size array literal of `Type` reflection values (per metaprog.type-intrinsic.type-reflection-value), one per payload type across all variants in declaration order (variant-major, field-minor), each payload type substituted through the current instantiation's subst map; empty array if E does not resolve to a known enum def.

**Divergence:** A6

**Testability:** behavioral

**Source:** `src/compiler/mono_clone.cpp#L2432-L2469`

## Named resource item declarations

### `metaprog.resource.named-item-decl` — Named resource item declaration

`resource NAME = CALLEE!{ body };` desugars to the same FN_MACRO_CALL_ITEM node as an item-position fn-macro, but the left-hand NAME rides in the (otherwise-unused) NAME slot: sema passes it to the `#[token_macro]` callee as a second `str` arg so the emitted item is named by NAME, not by a `fn <name>` header inside body. A parameterized form `resource NAME = CALLEE!(PARAMS){ body };` additionally captures the parenthesized list as balanced raw source text (PARAMS), passed as a third arg; the callee re-emits it verbatim as the generated fn's real parameter list, so PARAMS is parsed and type-checked when that fn is compiled. The parameterized alternative is tried first (ordered choice; the extra paren group makes it strictly more specific, so plain `resource N = h!{ ... }` is unaffected).

```logos
resource conn = open_conn!{ ... };
```
```logos
resource h = handler!(req: Request){ ... };
```

**Divergence:** no Rust equivalent (ADR 0012 §6 named resource-macro item).

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L582-L602`

## Statement-level repeat groups

### `metaprog.stmt.repeat-group` — Statement repeat-group `#(stmt)*`

`#(stmt)*` at statement position is a metaprogram repeat-group over a statement, expanding to a compile-time-repeated sequence of statements (mirrors the analogous repeat-group forms in type-argument and pattern-group positions).

**Divergence:** A3

**Testability:** transitive

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1900-L1901`

## ABI interaction

### `metaprog.abi.macro-hook-excluded` — fn_macro/token_macro hooks excluded from ABI surface

A function attributed `#[fn_macro]` or `#[token_macro]` is a compiler-invoked metaprogramming hook (discovered by attribute and invoked by the metaprog driver), not a linkable consumer API entry point; it is flagged as a macro hook so ABI-surface extraction (`--emit-abi`) excludes it from the public pub-allowlist, preventing its churning signature from tripping the ABI-compat gate.

**Testability:** transitive

**Source:** `src/compiler/sema_decl.cpp#L1138-L1146`
