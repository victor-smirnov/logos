# Divergences — compiler-derived register

Cross-cutting view over the extracted rule corpus (`tools/spec-extract/rules/**`): every spec rule whose `divergence` field is non-empty, grouped by divergence tag/kind. This register is *derived from compiler evidence*; the policy source of truth is [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — a tag here that matches a §A/§B row there is registered; anything without a matching row is flagged **unregistered — needs triage** per the "no silent divergence" rule.

**491 divergence-carrying rules** — §A blessed: 158 · §B catch-up refs: 18 · baghunt G156-1: 3 · Logos-specific additions: 202 · unregistered (triage): 71 · conformance notes: 39.

---

## A1 — const-eval → explicit `metacall`

Registered: `docs/DIVERGENCES.md` §A row **A1** (replaced). 15 rule(s).

### `expr.arr-fill.size-metacall` — Array fill length via metacall splice
- **Divergence**: Logos explicit-metacall model replaces Rust const-expression array lengths. *(untagged; matches A1/A2 explicit-metacall model)*
- **Rule**: `[v; metacall { <expr> }]` evaluates the block's tail expression by compile-time evaluation (CTFE), and the integer result becomes the array length. The metacall block must contain an integer tail expression. This is Logos's replacement for Rust const-eval at the array-length position.
- **Source**: `src/compiler/sema_expr.cpp#L11486-L11516`

### `item.enum.discriminant-const-expr` — enum discriminant from const expression
- **Divergence**: A1: const-eval at discriminant position runs through metacall/CTFE splicing rather than miri-style const folding.
- **Rule**: An enum discriminant may be a general const expression (e.g. `1 << 1`, a bare non-BLOCK node), evaluated via the CTFE channel; or a `metacall { <expr> }` block whose single required tail expression is likewise evaluated via CTFE to produce the discriminant. A metacall discriminant block with no resolvable tail expression is a compile error.
- **Source**: `src/compiler/sema_collect.cpp#L2024-L2065`

### `metaprog.metacall.args-ctfe-constant` — Every argument of a metacall call form must be a compile-time constant
- **Divergence**: A1/A6: CTFE of metacall args; replaces Rust const-eval.
- **Rule**: For the call form, each argument expression must be CTFE-evaluable to a constant literal; an argument that cannot be folded is a compile error. CALL stores arguments as a flat array, while GENERIC_CALL/STATIC_CALL wrap them as `{ ITEMS: [...] }`.
- **Source**: `src/compiler/sema_expr.cpp#L17334-L17359`

### `metaprog.metacall.block-tail-required` — metacall block must end in a tail expression
- **Divergence**: A1/A6.
- **Rule**: A `metacall { ... }` block must terminate with a tail expression (no trailing semicolon) so the metacall yields a value; a block lacking a tail expression is a compile error. The block's value type is the type of that tail expression.
- **Source**: `src/compiler/sema_expr.cpp#L17366-L17389`

### `metaprog.metacall.const-resolver` — metacall argument CTFE resolves bare module-const idents
- **Divergence**: A1/A6: metacall const folding.
- **Rule**: CTFE of metacall arguments and operands resolves a bare identifier naming a module-level const (collected into the module const-value map, including cross-package consts) to that const's value, so expressions like `metacall { THRESHOLD + 1 }` fold.
- **Source**: `src/compiler/sema_expr.cpp#L17311-L17332`

### `metaprog.metacall.forms` — metacall expression forms
- **Divergence**: A1/A6 — Logos addition: metacall is the explicit compile-time evaluation operator, the Logos replacement for const-eval (no implicit const-eval).
- **Rule**: `metacall` accepts exactly three operand shapes — a block (`metacall { … }`), a parenthesized expression (`metacall (e)`), or a call expression (`metacall f(…)`, including generic `f::<T>(…)` and static `Type::m(…)`) — and evaluates its argument at compile time.
- **Source**: `tools/peg_gen/grammars/logos.peg#L2731-L2736`, `src/compiler/sema_expr.cpp#L17084-L17088`

### `metaprog.metacall.no-nested-metacall` — metacall may not be nested inside another metacall's operand
- **Divergence**: A1/A6: metacall replaces Rust const-eval; rule has no Rust analogue.
- **Rule**: A `metacall` operand (call args, or the inner subtree for the block/expr forms) must not contain another `metacall` node; metacall is a one-shot lift to compile time whose result is a runtime value and therefore cannot serve as a compile-time argument to an enclosing metacall. Violation is a compile error.
- **Source**: `src/compiler/sema_expr.cpp#L17090-L17178`

### `metaprog.metacall.no-runtime-capture` — metacall block/expr form cannot capture enclosing runtime locals
- **Divergence**: A1/A6: compile-time evaluation model specific to metacall.
- **Rule**: In the block and parenthesized-expr forms, every VAR_REF must resolve to a name introduced inside the operand (LET/FOR/FOR_EACH binding, or a match-arm pattern binding), a module-level const, or a known function (concrete or generic). A reference to an enclosing-scope runtime local is a compile error, since the metacall is evaluated at compile time with no access to surrounding locals.
- **Source**: `src/compiler/sema_expr.cpp#L17196-L17302`

### `metaprog.metacall.return-type` — metacall result type must be primitive scalar, WritStatic, Writ, or ExprBlob
- **Divergence**: A1/A6: WritStatic/Writ/ExprBlob returns are Logos additions.
- **Rule**: The type produced by a metacall operand must be a primitive scalar (bool; integer kinds i8/i16/i24/i32/i56/i64 and u8/u16/u24/u32/u56/u64; f32/f64; integer/float literal types), a &str / Slice&lt;u8&gt;, WritStatic, Writ (incl. Rc&lt;Writ&gt;), or ExprBlob. Any other result type is a compile error.
- **Source**: `src/compiler/sema_expr.cpp#L17408-L17424`

### `metaprog.metacall.runtime-passthrough` — metacall lowers as a runtime pass-through until driver-side splice
- **Divergence**: A1/A6: compile-time splice model.
- **Rule**: During sema iterations a metacall lowers to its operand's lowered value (a pass-through), keeping the in-progress IR valid for borrow/type checks. The driver replaces the metacall AST node with the evaluated literal before the final non-metaprog sema pass, so this pass-through lowering never reaches code generation.
- **Source**: `src/compiler/sema_expr.cpp#L17606-L17610`

### `mono.const.const-arg-specialization` — Compile-time-constant call arguments specialize the callee
- **Divergence**: Logos const-generic-like specialization driven by const-eval reachability; see explicit-metacall comptime model. *(untagged; matches A1/A2 explicit-metacall model)*
- **Rule**: When a call-site argument forwarding (directly or transitively) to a const-evaluating intrinsic position (e.g. an atomic `Ordering`) is a compile-time literal, the callee is specialized with that constant baked in: each use of the parameter is replaced by the literal (an IntLit for integers, or an EnumLit `(enum_name, variant, discriminant)` for enums).
- **Source**: `src/compiler/mono_impl.hpp#L368-L401`

### `mono.subst.const-generic-value-use` — Const-generic params used in value position substitute their concrete value
- **Divergence**: A1/A2 related: const-generics are real, distinct from const-eval
- **Rule**: A const-generic parameter `<const N: T>` referenced in expression position is monomorphized by splicing its concrete value: a scalar/IntLit binding lowers to an integer literal of the substituted value; a WritStatic-literal binding splices the registered WritStatic literal at the use site.
- **Source**: `src/compiler/mono_clone.cpp#L509-L546`

### `trait.method.multi-trait-ambiguity` — Method provided by multiple traits is ambiguous
- **Divergence**: A1: collision removes the plain base from the registry; Rust resolves by receiver/inference where unambiguous
- **Rule**: If a method name `m` on type `S` is provided by more than one trait, the plain unqualified call `s.m(...)` is an error; the call must be disambiguated via a trait-bounded generic context or an explicit trait-qualified call.
- **Source**: `src/compiler/sema_expr.cpp#L8683-L8700`

### `type.array.length-forms` — Array type length forms
- **Divergence**: Array length via `metacall {..}` replaces Rust const-eval at this position (MP-mc-01). *(untagged; matches A1/A2 explicit-metacall model)*
- **Rule**: `[T; N]` length is determined by: a `metacall { expr }` block whose tail integer is CTFE-evaluated; `sizeof...(P)` over an in-scope type-param pack (symbolic `__sizeof_pack:P`); a literal integer; or a symbolic const parameter name. A missing/empty metacall tail or an unknown pack/op is a hard error.
- **Source**: `src/compiler/sema.cpp#L6140-L6226`

### `type.array.size-from-metacall` — Array size from metacall
- **Divergence**: Logos: comptime sizing via explicit metacall (see explicit-metacall design). *(untagged; matches A1/A2 explicit-metacall model)*
- **Rule**: `[T; metacall { ... }]` permits a compile-time metacall block as the array size expression.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1769-L1770`

## A2 — `const fn` → plain `fn` + `metacall` at const call sites

Registered: `docs/DIVERGENCES.md` §A row **A2** (replaced). 1 rule(s).

### `const.def.initializer-const-evaluable` — Const/static initializer must be const-evaluable
- **Divergence**: A2 — const-evaluable bare fn calls are not supported; the escape hatch is explicit `metacall fn(...)` (Rust would allow `const fn`).
- **Rule**: A const/static initializer must be one of: a literal (int/bool/str/float/char/bytes/wstatic); a WritStatic literal (writ map/array/str/int/float/bool/null); a `metacall fn(...)`; a CAST/PAREN/UNARY of a const-evaluable operand; a BINOP whose both operands are const-evaluable; an array/tuple literal (deferred to a later, more specific check); a struct literal all of whose field-init values are const-evaluable (field-shorthand rejected); a VAR_REF to an already-collected module const/static or to a known free fn (fn-pointer constant); or `&X` where X is a VAR_REF to a module const or otherwise const-evaluable. Any other form (notably a bare fn call) is rejected, because it would silently inline at every read site rather than produce a compile-time constant.
- **Source**: `src/compiler/sema_collect.cpp#L2218-L2327`

## A3 — `macro_rules!`/proc-macros/`#[derive]` → `metaprog` handlers + `quote_*!`

Registered: `docs/DIVERGENCES.md` §A row **A3** (replaced). 13 rule(s).

### `metaprog.derive.no-rust-derive-syntax` — `#[derive(...)]` is rejected; use per-trait triggers
- **Divergence**: Logos replaces Rust `#[derive(...)]` with `#[derive_<trait>]` + `#[metaprog_handler]`. *(untagged; matches A3 (derive via metaprog))*
- **Rule**: The Rust-style `#[derive(Trait, ...)]` attribute (a `derive` annotation carrying args) is not Logos surface syntax and is an error. Logos uses one trigger annotation per derive, `#[derive_<trait>]`, paired with an in-scope `#[metaprog_handler("derive_<trait>")]` function.
- **Source**: `src/compiler/sema_impl.hpp#L1762-L1774`

### `metaprog.metacall.exprblob-deferred-typing` — ExprBlob-returning metacall defers result typing to the post-splice pass
- **Divergence**: A3/A6: ExprBlob is the Logos metaprog AST-fragment return.
- **Rule**: When a metacall returns an ExprBlob (an AST-expression fragment marker), pass-1 typing is deferred: `let X: T = metacall foo()` accepts any annotated T over an ExprBlob RHS; the actual expression type is recovered after the driver splices the blob and pass-2 sema re-lowers it.
- **Source**: `src/compiler/sema_expr.cpp#L17400-L17407`

### `metaprog.quote-expr.antiquot-carrier-positions` — Antiquots are recognized only in defined AST carrier positions
- **Divergence**: A3/A6
- **Rule**: Antiquots and repetition groups are recognized only within the supported carrier set: VAR_REF, BINOP (lhs/rhs), PAREN/UNARY/CAST/DEREF (value), FIELD_READ (selector + receiver), CALL/METHOD_CALL/STATIC_CALL (callee-name-var, receiver, args), STRUCT_LIT/FIELD_INIT/FIELD_SHORTHAND, ARR_LIT/TUPLE_LIT/BLOCK items, statement carriers (LET, LET_DESTRUCT, EXPR_STMT, TAIL_EXPR, RETURN), and control flow (IF, WHILE, FOR, LOOP, ASSIGN, COMPOUND_ASSIGN). Antiquots in unsupported shapes are not substituted.
- **Source**: `src/compiler/sema_expr.cpp#L16586-L16747`

### `metaprog.quote-expr.antiquot-must-be-in-scope` — Antiquot variable in quote_expr! must be a bound local
- **Divergence**: A3/A6
- **Rule**: A `#name` antiquot inside `quote_expr!` is an error unless `name` is a variable in scope at the quote site ("`#name` — variable not in scope").
- **Source**: `src/compiler/sema_expr.cpp#L16510-L16514`

### `metaprog.quote-expr.no-nested-repeat` — Nested repetition groups are not allowed
- **Divergence**: A3/A6
- **Rule**: A `#(...)` repetition group may not be nested inside another `#(...)` group ("nested `#(...)` repetition not supported").
- **Source**: `src/compiler/sema_expr.cpp#L16589-L16597`

### `metaprog.quote-expr.reify-ast-to-exprblob` — quote_expr! reifies an expression AST into an ExprBlob
- **Divergence**: A3/A6 (replaces Rust macro/quote layer)
- **Rule**: `quote_expr! { e }` evaluates to a value of struct type `ExprBlob` carrying the serialized AST of `e`. With no antiquots, the AST is emitted as a static rodata blob and wrapped directly as `ExprBlob { ptr }`.
- **Source**: `src/compiler/sema_expr.cpp#L16386-L16423`, `src/compiler/sema_expr.cpp#L16806-L16813`

### `metaprog.quote-expr.repeat-cursor-length-agree` — Fixed-length cursors in one repetition group must agree on length
- **Divergence**: A3/A6
- **Rule**: Within a single `#(...)*` group, all fixed-size `[Ident; N]` cursors must share the same length N; a sibling cursor with a different N is rejected ("cursor length mismatches sibling cursor in same #(...)*"). A `Vec`-backed (dynamic) cursor makes the group dynamic and waives the fixed-length agreement check.
- **Source**: `src/compiler/sema_expr.cpp#L16539-L16548`

### `metaprog.quote-expr.repeat-cursor-type` — Repetition cursor must be [Ident;N], Vec&lt;Ident&gt;, or Vec&lt;ExprBlob&gt;
- **Divergence**: A3/A6
- **Rule**: A `#name` antiquot inside a `#(...)*` repetition group (a cursor) must bind a value of type `[Ident; N]` (fixed count N), `Vec<Ident>`, or `Vec<ExprBlob>` (dynamic count); any other type is rejected ("expected [Ident; N], Vec&lt;Ident&gt;, or Vec&lt;ExprBlob&gt;").
- **Source**: `src/compiler/sema_expr.cpp#L16523-L16538`, `src/compiler/sema_expr.cpp#L16469-L16492`

### `metaprog.quote-expr.repeat-needs-cursor` — A repetition group must contain at least one cursor antiquot
- **Divergence**: A3/A6
- **Rule**: A `#(...)*` repetition group body must contain at least one cursor antiquot `#x` of a cursor type; an empty-cursor body is rejected ("`#(...)*` body has no cursor `#x`").
- **Source**: `src/compiler/sema_expr.cpp#L16600-L16605`

### `metaprog.quote-expr.scalar-antiquot-type` — Scalar antiquot must be Ident, or Ident/ExprBlob outside ident-only positions
- **Divergence**: A3/A6
- **Rule**: A `#name` antiquot outside a repetition group, in a general expression position, must bind a value of type `Ident` or `ExprBlob`; in ident-only positions (field names, struct type name, field-read selector) it must bind an `Ident`. Otherwise it is rejected ("expected Ident" / "expected Ident or ExprBlob").
- **Source**: `src/compiler/sema_expr.cpp#L16549-L16560`, `src/compiler/sema_expr.cpp#L16618-L16621`, `src/compiler/sema_expr.cpp#L16661-L16685`

### `metaprog.quote-expr.subst-runtime` — quote_expr! with antiquots substitutes at runtime via logos_quote_expr_subst
- **Divergence**: A3/A6
- **Rule**: `quote_expr!` containing N&gt;0 antiquots lowers to a block that binds the static template blob and one `IdentSpan { ptr, count, kind }` per placeholder, then calls `logos_quote_expr_subst(template_ptr, size, &spans[0], N) -> *const u8` and wraps the result as `ExprBlob { ptr }`. Span kind is 0 for Ident slots, 1 for ExprBlob slots, 2 for Vec&lt;ExprBlob&gt; cursors.
- **Source**: `src/compiler/sema_expr.cpp#L16815-L16981`, `src/compiler/sema_expr.cpp#L16866-L16943`

### `type.identity.dstref` — Custom-DST reference identity = (package, name, mutability, owning kind, type-args)
- **Divergence**: A3 (custom-DST)
- **Rule**: A custom-DST reference type's identity = (package, struct name, mutability, owning kind {Borrow/Box}, type-args); an owning `Box<Foo>` custom-DST is distinct from a borrowed `&Foo`.
- **Source**: `src/compiler/sema.cpp#L855-L863`, `src/compiler/sema.cpp#L1009-L1014`

### `type.identity.slice-mut-owning` — Slice identity = (mutability, owning kind, element)
- **Divergence**: A3 (custom-DST / Box&lt;[T]&gt; as owning slice kind)
- **Rule**: Slice types are distinguished by element T, mutability, and owning kind (const_val): `&[T]`, `&mut [T]`, and owning `Box<[T]>` are mutually distinct types.
- **Source**: `src/compiler/sema.cpp#L841-L847`, `src/compiler/sema.cpp#L997-L1003`

## A5 — rustc-internal attributes distilled or stripped

Registered: `docs/DIVERGENCES.md` §A row **A5** (replaced/N-A). 1 rule(s).

### `type.anyval.lowered-as-i32` — AnyVal always lowers to a scalar i32
- **Divergence**: A5
- **Rule**: The `AnyVal` type lowers UNIFORMLY to a scalar i32 — as a standalone value ({4,4} layout) and as a struct field — never wrapped in an aggregate (e.g. never `!llvm.struct<"AnyVal",(i32)>`), so that field loads/stores and argument-passing treat it as a plain i32 tag word rather than a 1-field struct value.
- **Source**: `src/compiler/mlir_gen_types.cpp#L32`, `src/compiler/mlir_gen_types.cpp#L201-L212`, `src/compiler/mlir_gen_types.cpp#L447`

## A6 — Logos-only additions (variadics, Writ fabric, metaprog/metacall, fibres)

Registered: `docs/DIVERGENCES.md` §A row **A6** (addition). 83 rule(s).

### `coerce.writ.mapslice-to-typed-map` — MapSlice as &lt;K,AnyVal&gt;{} builds a typed Writ map
- **Divergence**: A6
- **Rule**: `src as <K,V>{}` (target struct WritMap) is permitted only for V = AnyVal and K in {I32,U32,I64,U64}, with source the matching MapSlice&lt;K&gt; struct; it lowers to a stdlib writ_build_map_&lt;k&gt;_anyval call returning Rc&lt;Writ&gt;. Any other key/value combination, a mismatched source, or a missing builder is an error.
- **Source**: `src/compiler/sema_expr.cpp#L774-L838`

### `coerce.writ.slice-to-typed-array` — &[T] as &lt;T&gt;[] builds a typed Writ array
- **Divergence**: A6
- **Rule**: `src as <T>[]` (target struct WritArr) requires `src: &[T]` (a Slice) whose element kind equals the target element kind; element T must be one of i8/u8/i16/u16/i32/u32/i64/u64/f32/f64. It lowers to a stdlib writ_build_array_&lt;T&gt; call returning the builder's Rc&lt;Writ&gt; type; missing builder (no `use logos.lang.writ.typed_arr`) or non-slice source or element mismatch or unsupported element is an error.
- **Source**: `src/compiler/sema_expr.cpp#L716-L773`

### `const.wstatic.content-identity` — Writ static literal type-arg identity is content-only
- **Divergence**: A6 — Writ is a Logos-only feature; no Rust analogue.
- **Rule**: A Writ static literal `@{...}` used at type-argument position is reduced to a `WStaticLit` type whose identity is a position-free content hash of the literal AST (schema-aware FNV-1a over node CODE plus value bytes/string children). Two structurally identical `@{...}` literals at different source positions yield the SAME type; differing content yields distinct types. First-write-wins: the first lowering of a given hash registers the materialising LExpr that mono later substitutes for `__const_param:CFG` references.
- **Source**: `src/compiler/sema.cpp#L6392-L6499`, `src/compiler/sema.cpp#L6486-L6498`

### `const.wstatic.dup-key-error` — Duplicate keys in a Writ map literal are rejected
- **Divergence**: A6 — Writ-specific.
- **Rule**: Within a Writ map literal (`WRIT_MAP`), two entries with the same key (after stripping surrounding quotes) are an error: "duplicate key '&lt;k&gt;' in Writ map literal". Empty keys are ignored. This applies to map literals at type-argument position, not only `pub const … = @{...}`.
- **Source**: `src/compiler/sema.cpp#L6425-L6440`

### `const.wstatic.type-lit-resolves-scope` — Writ type literals resolve type params in current scope
- **Divergence**: A6 — Writ-specific.
- **Rule**: A `@type(T)` (`WRIT_TYPE_LIT`) child resolves its TYPE node with the in-scope type parameters and contributes its canonical `type_str` to the literal's content identity; thus the same syntactic literal under different type-param bindings produces distinct WStaticLit types. A legacy NAME-only shape substitutes the bound type param when present, else uses the bare name.
- **Source**: `src/compiler/sema.cpp#L6462-L6482`

### `expr.struct-lit.union-single-field` — Union literals initialize exactly one field; missing-field check skipped
- **Divergence**: A6
- **Rule**: For a union struct, the all-fields-initialized check is suppressed: a union literal initializes only one (active) field by design.
- **Source**: `src/compiler/sema_expr.cpp#L10015-L10021`, `src/compiler/sema_expr.cpp#L10215-L10221`

### `expr.struct-lit.variadic-field-expansion` — Variadic struct field accepts expansion names `name_*`
- **Divergence**: A6
- **Rule**: A variadic struct field named `name` accepts literal field names of the form `name_<suffix>`; each such expansion value is type-checked against the variadic field's type and the variadic field is marked initialized.
- **Source**: `src/compiler/sema_expr.cpp#L9882-L9897`, `src/compiler/sema_expr.cpp#L10052-L10074`

### `expr.struct-lit.variadic-field-name-convention` — variadic field accepts `name_suffix` field-init keys
- **Divergence**: A6
- **Rule**: A declared variadic struct field named `f` matches struct-literal field-init keys of the form `f_<suffix>` (any key that starts with `f_` and is longer than `f_`); each matching field-init is type-checked against the variadic field's declared element type.
- **Source**: `src/compiler/sema_expr.cpp#L10104-L10106`, `src/compiler/sema_expr.cpp#L10216-L10230`, `src/compiler/sema_expr.cpp#L10388-L10407`

### `generic.bounds.variadic-tail-param` — Variadic tail parameter absorbs extra type args
- **Divergence**: A6
- **Rule**: If the last type parameter is variadic, type args beyond the non-variadic count are all checked against that final (variadic) parameter; otherwise excess args are ignored once parameters are exhausted.
- **Source**: `src/compiler/sema_collect.cpp#L812-L833`

### `generic.field.variadic-expansion` — Variadic field `name_N` selects the Nth element of the variadic type-arg pack
- **Divergence**: A6 — variadic type/field packs are Logos-only.
- **Rule**: A variadic struct field declared `name: A...` expands to fields `name_0, name_1, …`; field `name_<idx>` whose declared type is the variadic type parameter resolves to the type-arg at (start-of-pack + idx), where start-of-pack is the count of preceding non-variadic type parameters. Out-of-range or non-TypeVar variadic field types fall back to the raw declared type.
- **Source**: `src/compiler/sema.cpp#L6578-L6582`, `src/compiler/sema.cpp#L6606-L6631`

### `generic.spec.classify-fn` — Specialization-vs-generic classification for fn type-param lists
- **Divergence**: A6 — generic specialization has no stable-Rust analogue (Rust `min_specialization` is nightly-only); Logos-only addition.
- **Rule**: A fn's type-parameter list classifies the fn as a SPECIALIZATION (rather than a plain generic fn) iff at least one entry is either a structured type pattern (pointer type `*T` or array type `[T; N]`), or a bare identifier naming an already-known concrete type (resolved via the known-type table, or — if not yet resolvable — found in the pre-scanned pass-0 set of all declared names). Otherwise the fn is an ordinary generic fn.
- **Source**: `src/compiler/sema_collect.cpp#L4434-L4457`

### `generic.spec.classify-struct` — Specialization-vs-generic classification for struct/datatype type-param lists
- **Divergence**: A6 — generic specialization has no stable-Rust analogue; Logos-only addition.
- **Rule**: A struct/datatype decl's type-parameter list classifies it as a SPECIALIZATION iff: (a) an entry is a structured pattern (ptr/array type) — always; or (b) a bare-IDENT entry names a known concrete/primitive type — always; or (c) a bare-IDENT entry names a user-declared type from the pass-0 name set AND a struct/datatype of the SAME name is already registered in the current package (the specialization's "base"). Condition (c)'s base-existence gate prevents a fresh generic struct's own type-param name from being misclassified as a specialization merely because an unrelated type of the same name exists elsewhere (e.g. `struct ChainIter<A, B, T>` colliding with an unrelated `struct A`/`struct B` in another module).
- **Source**: `src/compiler/sema_collect.cpp#L4461-L4507`, `src/compiler/sema_collect.cpp#L4484-L4491`, `src/compiler/sema_collect.cpp#L4501-L4503`

### `generic.spec.partial-pattern-typevars` — Partial specialization keeps unbound params as scope-local type variables
- **Divergence**: A6: partial specialization of user structs is a Logos addition.
- **Rule**: A struct-spec pattern parameter that cannot resolve to a known concrete type (partial specialization, e.g. `Map<Bitmap, V>` keeps `V` free) is registered as a TypeVar in current_type_params_ only for the duration of that spec item's field collection; the concrete spec name derives from concrete_struct_name(make_generic_struct(name, patterns)), so both full and partial specs register under a name usable for later best-fit matching. All pattern typevars are erased from scope immediately after the spec's fields are recorded.
- **Source**: `src/compiler/sema_collect.cpp#L3857-L3862`, `src/compiler/sema_collect.cpp#L3869-L3878`, `src/compiler/sema_collect.cpp#L3897-L3899`

### `generic.spec.struct-pattern-classification` — Struct-spec pattern node classification
- **Divergence**: A6: Logos supports user struct specialization (`struct Map<Bitmap, V> {...}`), which Rust lacks for structs.
- **Rule**: In a struct specialization item `struct Name<pat, …> {…}`, each type-param-list entry is classified by node kind: PTR_TYPE/ARR_TYPE nodes are concrete spec patterns (embedded typevars extracted, then the node resolved to a TypeRef); TYPE_PARAM (bare-name) nodes first try to resolve as an already-known concrete type — if resolvable, that concrete type is the pattern, else the name is a partial-spec parameter and is bound as a fresh TypeVar for the duration of collection.
- **Source**: `src/compiler/sema_collect.cpp#L3846-L3864`

### `generic.struct-lit.variadic-typearg-collection` — trailing variadic type-param collects remaining hint args
- **Divergence**: A6
- **Rule**: A struct's trailing variadic type-param (is_variadic, necessarily last) collects all remaining type-args from the expected-type hint positionally if a matching hint is present; otherwise it takes the single inferred value for that name, or the error type if neither is available.
- **Source**: `src/compiler/sema_expr.cpp#L10167-L10177`

### `intrinsic.reflect.apply-generic` — apply_generic(g: Type, args) instantiates a generic constructor
- **Divergence**: A6
- **Rule**: `__apply_generic__(g, args)` instantiates the generic constructor described by Type value `g` (produced by `generic_of`) applying `args`, routed through the same struct-allocation path as intrinsic.reflect.type-apply. The template name is recovered from g's `Type` struct-literal `name` field, which must be a (possibly-quoted) string literal; both `g` and `args` are first chased through VarRef let-bindings (mono.reflect.varref-let-chase). `g` not resolving to such a StructLit, or `args` not being an ArrLit, is a fatal compile-time error.
- **Source**: `src/compiler/mono_clone.cpp#L2090-L2151`

### `intrinsic.reflect.has-trait-of` — has_trait_of::&lt;Trait&gt;(t: Type) -&gt; bool folds at monomorphization
- **Divergence**: A6
- **Rule**: `has_trait_of::<Trait>(t)` (callee __has_trait_of__) folds to a `bool` literal during monomorphization. The concrete type T is recovered from t's `Type` struct-literal `uid` field, which must be a `__type_uid_of__::<T>()` call; T is substituted with the active type substitution. The result is `true` iff T (named by its concrete struct name, enum name, or type_str, truncated at any `$G` generic-instantiation suffix) has an impl of Trait, computed recursively over concrete and blanket impls (mono_has_impl_recursive); absent T or an empty trait name yields `false`.
- **Source**: `src/compiler/mono_clone.cpp#L1617-L1652`

### `intrinsic.reflect.reify-type` — reify_type(t: Type) -&gt; Type recovers a source TypeRef and re-emits Type
- **Divergence**: A6
- **Rule**: `reify_type(t)` (callee __reify_type__) recovers a concrete TypeRef from a direct Type-producer argument and re-emits a fresh `Type` struct literal for it. Supported argument shapes, after chasing a VarRef argument through recorded let-initializers (mono.reflect.varref-let-chase): (1) a `Call` to `__typelist_head__`/`__typelist_nth__` — the indexed pack element becomes T (same index rules as intrinsic.reflect.typelist-head-nth); (2) a `StructLit` whose `uid` field is a call to `__type_uid_of__::<T>()` — T is substituted directly. A missing argument, or any other (unsupported) shape, is a fatal compile-time error naming the accepted producer forms.
- **Source**: `src/compiler/mono_clone.cpp#L1741-L1835`

### `intrinsic.reflect.type-apply` — type_apply(name, args: [Type;N]) -&gt; Type instantiates a struct template
- **Divergence**: A6
- **Rule**: `type_apply(name, args)` (callee __type_apply__) instantiates the struct template named `name` (must be a string literal; a surrounding `"..."` quoting is stripped) applying the TypeRefs recovered from `args` as its type-argument pack, and folds to a `Type` value describing the instantiation. `args` is chased through let-bindings; absent the pack-splice fast path (intrinsic.reflect.type-apply-pack-splice) it must be an ArrLit whose elements each resolve via the same direct-producer shapes intrinsic.reflect.reify-type accepts. The instantiated Struct TypeRef's `pkg_name` is copied from the first existing struct definition matching `name`, so the instance shares registry/UID identity with ordinarily-declared instantiations. A non-literal `name`, a non-ArrLit `args`, or any unrecognized `args` element is a fatal compile-time error.
- **Source**: `src/compiler/mono_clone.cpp#L1841-L1877`, `src/compiler/mono_clone.cpp#L1968-L2083`

### `intrinsic.reflect.type-apply-pack-splice` — type_apply pack-splice fast path over Type-array intrinsics
- **Divergence**: A6
- **Rule**: When type_apply's `args` operand is (after let-chase) itself a call to a Type-array-producing intrinsic, its element TypeRefs are spliced directly into the template instantiation instead of requiring an ArrLit shape: `__type_refs_of__` contributes its full (substituted) type-argument list, one per struct member; `__args_of__::<T>()` contributes T's own type_args; `__typelist_tail__::<T>()` contributes T's pack excluding index 0; `__tuple_elems_of__::<T>()` contributes T's tuple element types when T is a Tuple (otherwise contributes none). This splice runs before, and independent of, the mono ArrLit-folding pass.
- **Source**: `src/compiler/mono_clone.cpp#L1878-L1967`

### `intrinsic.reflect.type-struct-shape` — Reflected Type value layout {kind,name,size,align,uid}
- **Divergence**: A6
- **Rule**: A reflected `Type` value materialized by a folding reflection intrinsic is the struct literal `Type { kind: u32, name: &[u8], size: i64, align: i64, uid: u64 }`: `kind` = the TypeRef's `LogosType::Kind` discriminant, `name` = its canonical type string, `size`/`align` = its target layout (`size_of`/`align_of`), and `uid` = `type_hash_64bit(type_hash_23(type_id_canon(T)))`. Producing `uid` also registers `uid -> T` in a mono-wide table so a later `__type_uid_of__`-keyed lookup can recover T from the uid.
- **Source**: `src/compiler/mono_clone.cpp#L1716-L1730`, `src/compiler/mono_clone.cpp#L1810-L1833`, `src/compiler/mono_clone.cpp#L2074-L2083`

### `intrinsic.reflect.typelist-head-nth` — typelist_head/nth::&lt;L&gt;(i) -&gt; Type folds to a Type struct literal
- **Divergence**: A6
- **Rule**: `typelist_head::<L>()` and `typelist_nth::<L>(i)` (callees __typelist_head__/__typelist_nth__) fold to a single `Type { kind, name, size, align, uid }` struct literal (intrinsic.reflect.type-struct-shape) describing element idx of L's type-arg pack: head uses idx=0; nth requires its argument to be a literal int. A missing type argument, a non-literal nth index, or an index outside `[0, pack.size())` is a fatal compile-time error (abort with diagnostic).
- **Source**: `src/compiler/mono_clone.cpp#L1672-L1731`

### `intrinsic.reflect.typelist-len` — typelist_len::&lt;L&gt;() -&gt; i64 folds to the pack arity
- **Divergence**: A6
- **Rule**: `typelist_len::<L>()` (callee __typelist_len__) folds to an `i64` literal equal to the number of type arguments in L's type-argument pack (0 when L carries no type-argument list). O(1) compile-time probe; the canonical L is `TypeList<T...>`.
- **Source**: `src/compiler/mono_clone.cpp#L1657-L1668`

### `item.datatype.def` — Writ datatype definition
- **Divergence**: A6
- **Rule**: A datatype item is `[pub[(vis)]] eidos NAME [<type-params>] { field_def_or_doc* }`. It declares a Writ-fabric datatype with named/repeat-group fields; the optional generic parameter list and visibility marker are accepted.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1096-L1100`

### `item.datatype.explicit-inst` — Explicit datatype instantiation declaration
- **Divergence**: A6
- **Rule**: `[pub[(vis)]] eidos TYPE_REF ;` (no body) is an explicit-instantiation declaration that binds metadata annotations (e.g. `#[type_code=N]`) to a concrete generic instantiation, e.g. `#[type_code=42] datatype Array<i32>;`.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1102-L1109`

### `item.datatype.type-code-register` — #[type_code=N] registers explicit type code
- **Divergence**: A6: Writ datatype-family mechanism, Logos-only.
- **Rule**: `#[type_code=N]` on a datatype registers N under the datatype's fully-qualified name (`pkg::Name`, or bare `Name` with no current package) in the explicit-type-code table, made visible to `collect_impl` within the same collection pass; `#[annotation]` separately flags the datatype's SemaStructInfo as a user-annotation type.
- **Source**: `src/compiler/sema_collect.cpp#L1675-L1687`

### `item.datatype.type-code-unique` — exclusive datatype annotations are unique
- **Divergence**: A6: part of the Writ datatype/type-code fabric, Logos-only.
- **Rule**: On a datatype item, the exclusive annotation names `type_code` and `annotation` may each appear at most once; a duplicate occurrence of either on the same item is a compile error.
- **Source**: `src/compiler/sema_collect.cpp#L1662-L1674`

### `item.enum.zoned-attr` — #[zoned]/#[borrow_carrying] on enum
- **Divergence**: A6: Logos-only zone/niche-enum representation attribute.
- **Rule**: `#[zoned]` on an enum sets its `zoned2` flag (the niche enum's Ref arm is stored self-relative at rest, absolute as a computed value); `#[borrow_carrying]` sets the `borrow_carrying` flag. Both mirror the equivalent struct-level attributes.
- **Source**: `src/compiler/sema_collect.cpp#L1698-L1713`

### `item.field.repeat-group` — Repeat-group field (quote)
- **Divergence**: A6
- **Rule**: `#( field_def ),*` and `#( field_def )*` denote a repeat-group of field definitions (REPEAT_GROUP, OP=1 comma-separated / OP=0 plain), for use in quoted item bodies.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1183-L1186`

### `item.field.variadic` — Variadic field
- **Divergence**: A6
- **Rule**: A field of form `IDENT ... : TYPE_REF` marks a variadic field (IS_VARIADIC).
- **Source**: `tools/peg_gen/grammars/logos.peg#L1203-L1204`

### `item.fn.antiquot-name` — Function with antiquoted name
- **Divergence**: A6
- **Rule**: `[pub] [unsafe] fn #(expr) [<type-params>] ( [params] ) [-> T] block` carries an expr-TOM name (NAME_VAR), valid only inside a quote body; these alts omit the where-clause because NAME_VAR and WHERE share a slot.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1286-L1293`, `tools/peg_gen/grammars/logos.peg#L1312-L1319`

### `item.fn.param-variadic` — Variadic parameter
- **Divergence**: A6
- **Rule**: `IDENT : T ...` marks a variadic parameter (IS_VARIADIC); plain `IDENT : T` is the ordinary typed parameter.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1379-L1382`

### `item.struct.attr-flags` — structural struct attribute flags
- **Divergence**: A6: these are Logos-only zone/memory-model struct attributes with no Rust counterpart.
- **Rule**: Recognised structural struct attributes set (OR-accumulate) per-struct SemaStructInfo bit flags: no_auto_drop, self_describing, rel_ptr, pinned, zone_mut, zoned (zoned2), borrow_carrying, non_null.
- **Source**: `src/compiler/sema_collect.cpp#L1578-L1594`

### `item.struct.explicit-inst` — Explicit struct instantiation declaration
- **Divergence**: A6: see B-item-92 — bare `struct Foo;` is the unit struct, generic form kept for the unbound-typevar diagnostic
- **Rule**: `[pub[(vis)]] struct TYPE_REF ;` where TYPE_REF carries type arguments (e.g. `struct Foo<i64>;`) is an explicit-instantiation declaration binding annotations to a generic struct instantiation. The dedicated `instantiate Foo<T>;` form is preferred.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1133-L1138`

### `item.trait.explicit-inst` — Explicit genos/trait specialization declaration
- **Divergence**: A6
- **Rule**: `[pub[(vis)]] <trait-kw> TYPE_REF ;` (no body) binds annotations to a logical-family (genos) specialization of a concrete trait instantiation; implementing eidos inherit the metadata via impl.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1111-L1118`

### `layout.enum.low-bit-niche` — Pointer-plus-small-integer two-arm enum packs into one word via a low-bit discriminant
- **Divergence**: A6
- **Rule**: A two-variant enum where every variant has exactly one payload field, one variant's field is a reference (`&T`/`&mut T`) whose pointee has alignment &gt;= 2 (guaranteeing its low bit is always 0), and the other variant's field is an integer of &lt;= 56 bits, packs into a SINGLE machine word: the pointer arm is stored raw; the integer arm is stored shifted as `(v << 1) | 1`. The discriminant is the value's low bit (0 = pointer arm, 1 = integer arm) -- no separate discriminant word.
- **Source**: `src/compiler/mlir_gen_types.cpp#L869-L925`, `src/compiler/mlir_gen_types.cpp#L872-L890`

### `layout.enum.low-bit-niche-zoned2-raw-arms` — `#[zoned2]` enums admit a raw untagged pointer arm and a raw 64-bit integer arm in the low-bit niche
- **Divergence**: A6
- **Rule**: For an enum flagged `zoned2`, the low-bit-niche pointer arm additionally accepts any raw `*T`/`&T`/`&mut T` regardless of the pointee's declared alignment (the zoned2 allocator's invariant that all Writ zone objects are &gt;= 2-aligned is trusted directly), and the integer arm additionally accepts a full 64-bit `i64`/`u64` stored RAW (no `<<1` shift) -- because the producer of such a `zoned2` value has already baked a low-bit-1 tag into the raw word itself.
- **Source**: `src/compiler/mlir_gen_types.cpp#L899-L924`

### `layout.enum.null-pointer-niche-nonnull-wrapper` — A `#[non_null]` single-8-byte-pointer wrapper struct also qualifies for the null-pointer niche
- **Divergence**: A6
- **Rule**: In the two-variant fieldless+single-field niche shape, the single payload field also qualifies for the null-pointer niche when its type is a struct/zoned-struct flagged `non_null` whose total ABI byte size is exactly 8 (a Box/Rc/Arc-style single-pointer wrapper) -- its type invariant guarantees the pointer at offset 0 is never zero, so the same null-vs-non-null discriminant encoding applies.
- **Source**: `src/compiler/mlir_gen_types.cpp#L844-L868`

### `layout.enum.zoned-niche-self-relative` — #[zoned2] niche enum: self-relative at rest, absolute in compute
- **Divergence**: A6
- **Rule**: A `#[zoned2]` niche enum's at-rest storage word `r` uses self-relative addressing for its reference arm (anchor = the slot's own address): r==0 → null; r&1==1 → Pod arm (position-independent, copied raw, identity on materialize/lower); otherwise → Ref arm, whose absolute address is `slot + r` on materialize and whose stored delta is `val − slot` on lower. The compute-side value is a fresh alloca holding the word with the Ref arm as an ABSOLUTE address, bridging storage (self-relative) and compute (absolute) representations.
- **Source**: `src/compiler/mlir_gen_expr.cpp#L5095-L5135`

### `layout.refrepr.rel-ptr-struct` — `#[rel_ptr]` struct is a self-relative offset, not a reference
- **Divergence**: A6
- **Rule**: A struct/zoned-struct flagged `rel_ptr` classifies as RelOffset: it is stored as an 8-byte `i64` self-relative offset (absolute address computed on access), not as a thin/fat pointer. A struct/zoned-struct without the flag is NotARef (an ordinary by-value aggregate).
- **Source**: `src/compiler/mlir_gen_types.cpp#L655-L662`

### `layout.refrepr.zoned2-field-self-relative` — Thin-pointer field of a `#[zoned2]` struct is stored self-relative
- **Divergence**: A6
- **Rule**: When computing a FIELD's reference representation (as opposed to a bare type's), a field whose representation would otherwise be ThinPtr is instead stored as RelOffset if the owning struct is flagged `zoned2`. Other representation kinds are unaffected by the owner's `zoned2` flag at this step.
- **Source**: `src/compiler/mlir_gen_types.cpp#L669-L678`

### `metaprog.enum-intrinsic.variant-count-of` — __variant_count_of__::&lt;E&gt;() yields variant count
- **Divergence**: A6
- **Rule**: __variant_count_of__ with type-arg E evaluates to a literal i64 equal to the variant count of E's enum definition when E resolves to a known LogosType::Enum with a matching def in in_.enums, else 0.
- **Source**: `src/compiler/mono_clone.cpp#L2395-L2417`

### `metaprog.enum-intrinsic.variant-names-of` — __variant_names_of__::&lt;E&gt;() yields variant names
- **Divergence**: A6
- **Rule**: __variant_names_of__ with type-arg E evaluates to a fixed-size array literal of each variant's name (string literal), in declaration order; empty array if E does not resolve to a known enum def.
- **Source**: `src/compiler/mono_clone.cpp#L2395-L2407`, `src/compiler/mono_clone.cpp#L2421-L2425`, `src/compiler/mono_clone.cpp#L2470-L2477`

### `metaprog.enum-intrinsic.variant-payload-counts-of` — __variant_payload_counts_of__::&lt;E&gt;() yields per-variant payload arities
- **Divergence**: A6
- **Rule**: __variant_payload_counts_of__ with type-arg E evaluates to a fixed-size array literal of i64, one per variant in declaration order, each equal to that variant's payload-field count; empty array if E does not resolve to a known enum def.
- **Source**: `src/compiler/mono_clone.cpp#L2426-L2431`, `src/compiler/mono_clone.cpp#L2470-L2477`

### `metaprog.enum-intrinsic.variant-payload-types-flat-of` — __variant_payload_types_flat_of__::&lt;E&gt;() yields flattened payload-type reflections
- **Divergence**: A6
- **Rule**: __variant_payload_types_flat_of__ with type-arg E evaluates to a flat fixed-size array literal of `Type` reflection values (per metaprog.type-intrinsic.type-reflection-value), one per payload type across all variants in declaration order (variant-major, field-minor), each payload type substituted through the current instantiation's subst map; empty array if E does not resolve to a known enum def.
- **Source**: `src/compiler/mono_clone.cpp#L2432-L2469`

### `metaprog.handler.register` — #[metaprog_handler("trigger")] registers a hook
- **Divergence**: A6: metaprog handler registration, Logos-only mechanism.
- **Rule**: `#[metaprog_handler("trigger")]` on a function registers `(trigger, fn_name)`; the trigger is the annotation's first positional string-literal argument. The host driver later scans user items for a matching `#[trigger]` annotation and invokes the registered fn on each. A missing/unresolvable trigger string is recorded as the sentinel `<missing>` so a later validation pass can surface the misuse as a diagnostic.
- **Source**: `src/compiler/sema_collect.cpp#L1837-L1873`

### `metaprog.metacall.writ-autofreeze` — Writ-returning metacall auto-freezes to WritStatic and is call-form only
- **Divergence**: A6: Writ/WritStatic is a Logos addition.
- **Rule**: A metacall whose operand returns a (mutable) Writ / Rc&lt;Writ&gt; is auto-frozen: user code observes the spliced value as WritStatic (the lowered expression is retyped to WritStatic). The Writ return type is supported only on the call form (`metacall foo()`); using it with the block or expr form is a compile error.
- **Source**: `src/compiler/sema_expr.cpp#L17537-L17568`, `src/compiler/sema_expr.cpp#L17597-L17603`

### `metaprog.quote-ty.antiquot-type-var` — $ident antiquot inside quote_ty! refers to a bound Type value
- **Divergence**: A6 (Logos addition)
- **Rule**: An ANTIQUOT_TYPE `$x` inside `quote_ty!` lowers to a variable reference of type `Type` (the in-scope binding named `x`), instead of being reified from a static type.
- **Source**: `src/compiler/sema_expr.cpp#L16182-L16184`, `src/compiler/sema_expr.cpp#L16314-L16316`

### `metaprog.quote-ty.array-antiquot-literal-size` — quote_ty! array with antiquot element requires literal integer size
- **Divergence**: A6 (Logos addition)
- **Rule**: `quote_ty! { [$t; N] }` lowers to `__array_type_apply__(elem_producer, N)`; the size N MUST be a literal integer (a non-numeric/symbolic size is rejected with "array antiquot requires literal integer size").
- **Source**: `src/compiler/sema_expr.cpp#L16238-L16263`

### `metaprog.quote-ty.generic-inst-antiquot` — quote_ty! generic instantiation with antiquot args lowers to __type_apply__
- **Divergence**: A6 (Logos addition)
- **Rule**: `quote_ty! { Foo<args...> }` with at least one `$ident` antiquot among the args lowers to `__type_apply__("Foo", [elems])`, where each elem is a var-ref (for `$x`) or a reified `Type` struct literal (for a concrete type arg). Lifetime args and pack-expand args in this position are rejected ("lifetime / pack args not yet supported").
- **Source**: `src/compiler/sema_expr.cpp#L16299-L16355`

### `metaprog.quote-ty.pack-splice` — quote_ty! generic pack-splice lowers to __type_apply__ with runtime array
- **Divergence**: A6 (Logos addition)
- **Rule**: `quote_ty! { Foo<$ts...> }`, where the sole generic argument is an ANTIQUOT_PACK `$ts...`, lowers to `__type_apply__("Foo", ts)` where ts is a var-ref to a runtime `Array<Type>`. A pack-splice mixed with any other generic argument (`Foo<$t, $ts...>`) is rejected ("mixed pack-splice with other args not yet supported").
- **Source**: `src/compiler/sema_expr.cpp#L16271-L16294`

### `metaprog.quote-ty.reify-type-to-struct` — quote_ty! reifies a type into a runtime Type value
- **Divergence**: A6 (Logos addition; metaprog reflection intrinsic)
- **Rule**: `quote_ty! { T }` evaluates to a value of struct type `Type` whose fields are { kind: u32 = __type_kind_of__::&lt;T&gt;(), name: &[u8] = __type_name_of__::&lt;T&gt;(), size: i64 = size_of::&lt;T&gt;(), align: i64 = align_of::&lt;T&gt;(), uid: u64 = __type_uid_of__::&lt;T&gt;() }.
- **Source**: `src/compiler/sema_expr.cpp#L16357-L16383`, `src/compiler/sema_expr.cpp#L16179`

### `metaprog.quote-ty.tuple-antiquot` — quote_ty! tuple with antiquot lowers to __tuple_type_apply__
- **Divergence**: A6 (Logos addition)
- **Rule**: `quote_ty! { ($t1, $t2, ...) }` where at least one element is an antiquot lowers to `__tuple_type_apply__([p1, p2, ...])` where each pi is the per-element Type producer (var-ref for `$x`, reified `Type` literal otherwise); mixed literal/antiquot elements are permitted.
- **Source**: `src/compiler/sema_expr.cpp#L16209-L16234`

### `metaprog.type-intrinsic.apply-generic` — __apply_generic__ instantiates a template struct type
- **Divergence**: A6
- **Rule**: For __apply_generic__(tmpl_name, arg0..argN), each arg expr is resolved to a compile-time TypeRef via the type-producer-recover protocol (unresolvable arg aborts compilation with a diagnostic); the intrinsic builds LogosType::Struct{name=tmpl_name, type_args=[recovered...]} (pkg inherited from the matching struct def in out_.structs), interns the type, and replaces the call with that type's `Type` reflection value.
- **Source**: `src/compiler/mono_clone.cpp#L2191-L2239`

### `metaprog.type-intrinsic.array-type-apply` — __array_type_apply__(Type, N) builds an array type
- **Divergence**: A6
- **Rule**: __array_type_apply__(elem, size) requires elem to resolve via type-producer-recover (unresolvable is a compile-time abort) and size (after chase) to be a LitInt literal (a non-literal is a compile-time abort); the intrinsic yields LogosType::Array{elem=recovered, arr_size=size}'s `Type` reflection value.
- **Source**: `src/compiler/mono_clone.cpp#L2244-L2246`, `src/compiler/mono_clone.cpp#L2342-L2361`

### `metaprog.type-intrinsic.tuple-type-apply` — __tuple_type_apply__([Type;N]) builds a tuple type
- **Divergence**: A6
- **Rule**: __tuple_type_apply__(arr) requires arg0 to chase to an ArrLit (a non-ArrLit is a compile-time abort); each element is resolved via type-producer-recover (unresolvable element aborts); the intrinsic yields LogosType::Tuple{tuple_elems=[recovered...]}'s `Type` reflection value.
- **Source**: `src/compiler/mono_clone.cpp#L2244-L2246`, `src/compiler/mono_clone.cpp#L2316-L2341`

### `metaprog.type-intrinsic.type-producer-recover` — Type-producer recovery protocol for type-apply intrinsics
- **Divergence**: A6
- **Rule**: To resolve a LIR sub-expr to a compile-time TypeRef (shared by __apply_generic__/__tuple_type_apply__/__array_type_apply__): (1) chase through VarRef by looking up type_let_inits_, up to 8 hops; (2) if the result is a Call to __typelist_nth__ or __typelist_head__, substitute its type-arg[0] (a type-list) through the current subst map and index into its type_args pack at the literal-int index given as __typelist_nth__'s value arg (__typelist_head__ implicitly index 0); out-of-range index yields no type; (3) if the result is a StructLit with a field named "uid" whose initializer is a Call to __type_uid_of__, recover that call's type-arg[0] substituted through the subst map; (4) otherwise recovery fails (empty TypeRef).
- **Source**: `src/compiler/mono_clone.cpp#L2152-L2189`, `src/compiler/mono_clone.cpp#L2255-L2315`

### `metaprog.type-intrinsic.type-reflection-value` — Type reflection value shape and uid
- **Divergence**: A6
- **Rule**: A reflected `Type` value (yielded by __apply_generic__/__tuple_type_apply__/__array_type_apply__) is the struct literal `Type{kind:u32=T.kind(), name:&[u8]=type_str(T), size:i64=size_of(T), align:i64=align_of(T), uid:u64}` where `uid = type_hash_64bit(type_hash_23(type_id_canon(T)))`; T is registered into out_.type_pool and uid_to_type_[uid]=T so a later __type_uid_of__ lookup on that uid recovers T.
- **Source**: `src/compiler/mono_clone.cpp#L2213-L2238`, `src/compiler/mono_clone.cpp#L2364-L2387`

### `metaprog.variadic.tuple-all-eq` — __tuple_all_eq__::&lt;T&gt;(a, b) expands to an &&-chain of elementwise eq
- **Divergence**: A6
- **Rule**: __tuple_all_eq__ with type-arg T substituted to a concrete Tuple and exactly 2 args (a, b) expands to the left-associated `&&`-chain of per-element equality a.i.eq(&b.i): a Tuple-kind element inlines a recursive chain over the nested field refs (never re-emits __tuple_all_eq__, since mono does not re-process synthesized intrinsics); a Slice-kind element (canonicalized to element name "str" when it is Slice&lt;u8&gt;) is compared via a direct 2-arg by-value free-function call; any other element resolves a callee symbol by scanning out_.functions then in_.functions for a name containing `<elem-type-str>__eq__f__` at a `.`-delimited-or-leading boundary (falling back to `<elem-type-str>__eq`) and calls it as `a.eq(&b)` via method_call. When T is missing/non-Tuple or fewer than 2 args are present, the intrinsic degenerates to the boolean literal `true` (also the result for an empty tuple).
- **Source**: `src/compiler/mono_clone.cpp#L2488-L2584`

### `metaprog.variadic.tuple-each-field-debug` — __tuple_each_field_debug__::&lt;T&gt;(self, f) expands variadic tuple Debug
- **Divergence**: A6
- **Rule**: __tuple_each_field_debug__ with type-arg T substituted to a concrete Tuple and exactly 2 args (self, f: &mut Formatter) expands to a `fmt_seq`-chained Result: fmt_tuple_open(f), then per element i in order: fmt_tuple_sep(f) folded in when i&gt;0, then either (Tuple-kind element) an inline recursive build over the nested field ref, or (otherwise) a Debug-fmt call resolved as a symbol containing `<elem-type-str>__Debug__fmt__f__` else `<elem-type-str>__fmt__f__` at a `.`-delimited-or-leading boundary, else literally `<elem-type-str>__fmt` (elem-type-str = type_str(elem), with Slice&lt;u8&gt; canonicalized to "str"), invoked as `field.fmt(f)`; finally fmt_tuple_close1(f) if the tuple arity is 1, else fmt_tuple_close(f); each step is folded into the running chain via fmt_seq. Every reuse of the shared `f` argument after its first use goes through a reborrow wrapper (AddrOfTemp(Deref(f))), since `&mut Formatter` is a move-type under borrow-check and a bare reuse would consume it on the first call. When T is missing/non-Tuple or fewer than 2 args are present, the intrinsic degenerates to a single close-call.
- **Source**: `src/compiler/mono_clone.cpp#L2591-L2683`

### `metaprog.writ-blob.ast-fragment-recurse` — WRIT_BLOB carrying an AST-category root lowers as that expression
- **Divergence**: A6: Writ/metaprog is a Logos addition (ExprBlob AST fragments spliced from metafunctions).
- **Rule**: A WRIT_BLOB whose serialized root TinyMap has schema category CAT_AST and whose variant code is a supported expression node (BINOP, LIT_INT, LIT_BOOL, LIT_STR, VAR_REF, CALL, PAREN_EXPR, UNARY, FIELD_READ, METHOD_CALL, CAST, INDEX_READ, STRUCT_LIT, ARR_LIT, TUPLE_LIT, BLOCK, BLOCK_STMT, IF) is lowered by recursively type-checking that root node as an ordinary expression, yielding its recovered expression type. The blob's arena is retained for the lifetime of sema.
- **Source**: `src/compiler/sema_expr.cpp#L17019-L17050`

### `metaprog.writ-blob.opaque-static-fallback` — Non-AST WRIT_BLOB lowers to an opaque WritStatic literal
- **Divergence**: A6: WritStatic is a Logos addition.
- **Rule**: A WRIT_BLOB whose root is null, non-TinyMap, or not of an AST expression category is lowered to an opaque data literal of type WritStatic carrying the raw blob bytes verbatim.
- **Source**: `src/compiler/sema_expr.cpp#L17056-L17060`

### `mono.subst.tuple-receiver-elem-args` — Tuple receiver supplies its element types as impl type-args
- **Divergence**: A6: Logos-only variadic tuple-type impls — `impl Trait for (A,B,...)` specialized by tuple element types.
- **Rule**: For a method call whose receiver is a tuple type, the call's impl-level type-arguments are set to the tuple's element types, enabling specialization of `impl<A,B,...> Trait for (A,B,...)`; for nested tuple recursion the inner receiver's own element types override stale outer-spec args, and any method-level (non-impl) type-args stashed at the tail of the original list are preserved after the tuple elements.
- **Source**: `src/compiler/mono_clone.cpp#L3952-L3953`, `src/compiler/mono_clone.cpp#L4010-L4024`

### `mono.subst.variadic-param-expand` — A variadic param expands to one concrete param per pack element
- **Divergence**: A6 (Logos addition — variadics)
- **Rule**: A variadic parameter `p: A...` whose type is a TypeVar bound to a type pack of length N expands into N non-variadic params named via the per-index pack-arg naming scheme, each typed by the corresponding pack element; non-variadic params are type-substituted unchanged with name/slot/owning-box-dyn flags preserved.
- **Source**: `src/compiler/mono_clone.cpp#L4868-L4896`

### `trait.writ.type-code-marks-writ-family` — #[type_code] on trait marks Writ-datatype family
- **Divergence**: A6: Writ reflection fabric, Logos-only.
- **Rule**: A `#[type_code]` annotation pending on a trait item flags the trait `is_writ` at collection, identifying it as part of the Writ-datatype family that `reflect::<T>()` and reflection emission consult.
- **Source**: `src/compiler/sema_collect.cpp#L1882-L1892`

### `type.antiquot.quote-ty-only` — Type antiquotation
- **Divergence**: A6
- **Rule**: `$ident` in type position is a type antiquotation valid only inside `quote_ty! { ... }`; resolving it elsewhere is an error.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1456-L1459`

### `type.cfg-slot.const-generic-defer` — Deferred cfg-slot when base is a const type-param
- **Divergence**: A6
- **Rule**: When `CFG` names a const-generic type-parameter of the enclosing item, `<type:CFG.path>` is NOT resolved eagerly; it yields a deferred CfgSlotType carrying the CFG ident and an encoded path, which monomorphization resolves once the parameter is bound to a concrete WritStatic value.
- **Source**: `src/compiler/sema.cpp#L4972-L4981`, `src/compiler/sema.cpp#L4982-L4983`, `src/compiler/sema.cpp#L5055`, `src/compiler/sema.cpp#L5101-L5105`

### `type.cfg-slot.const-param-must-be-writstatic` — cfg-slot base type-param must be const WritStatic
- **Divergence**: A6
- **Rule**: If `CFG` in `<type:CFG.path>` names a type-parameter, that parameter must be declared `const CFG: WritStatic`; otherwise a diagnostic is raised (the param must be a const-generic whose type is the WritStatic struct).
- **Source**: `src/compiler/sema.cpp#L4985-L5004`

### `type.cfg-slot.eager-alias-resolution` — Eager cfg-slot resolution against a WStaticLit alias
- **Divergence**: A6
- **Rule**: When `CFG` is not a type-param but resolves to a type alias bound to a WStaticLit (`pub type Cfg = @{...};`), the path is walked eagerly through that literal's registered Writ value at resolution time, producing the concrete projected type directly.
- **Source**: `src/compiler/sema.cpp#L4974-L4976`, `src/compiler/sema.cpp#L5055-L5099`

### `type.cfg-slot.path-extraction` — Config-slot type projection
- **Divergence**: A6
- **Rule**: `<type:CFG.path>` extracts a type from a WritStatic-typed binding `CFG` by walking a path of steps; each step is a struct-field access by name (on a string-keyed Writ map), an integer-field access by index (on an int-keyed Writ map), or an array index (on a Writ array). The path must be non-empty. The final reached Writ value must be a Type value; its named type is then resolved as the result.
- **Source**: `src/compiler/sema.cpp#L4969-L4981`, `src/compiler/sema.cpp#L5038-L5041`, `src/compiler/sema.cpp#L5067-L5096`

### `type.cfg-slot.projection` — Type-level cfg-slot projection
- **Divergence**: A6
- **Rule**: `<type:CFG.path>` projects, at mono-time, the type stored at a path within a WritStatic-typed type-level binding. Path steps are `.IDENT` (string key), `.INTEGER` (int key) and `.[INTEGER]` (array index). At least one path step is required. `<type:CFG.SLOT>::Assoc` projects an associated type on the slot base.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1428-L1449`

### `type.closure.type` — Closure type
- **Divergence**: A6: Rust spells closures via Fn-family bounds; Logos has a dedicated `|..|->R` closure type syntax.
- **Rule**: `|T1, T2| -> R` is a closure type used in parameter annotations; the zero-arg form `|| -> R` is accepted (the `||` token is split).
- **Source**: `tools/peg_gen/grammars/logos.peg#L1657-L1664`

### `type.datatype.data-plain-inference` — DataPlain flag propagates through nested datatype fields
- **Divergence**: A6: Writ datatype DataPlain/DataNode classification is Logos-only.
- **Rule**: A datatype is DataPlain (info.is_data_plain) unless disproved by a field: for a (possibly array-wrapped) ZonedStruct field, if its type is generic (non-empty type_args) or its base name is not yet found in datatypes_ (forward reference / cross-package), the outer type is conservatively marked non-DataPlain (DataNode); if the nested type IS found and itself is_data_plain, embedding it by value does not clear the outer type's DataPlain flag. Array wrapping is stripped before the check, so a DataNode array element also demotes the owner.
- **Source**: `src/compiler/sema_collect.cpp#L3974-L4003`

### `type.datatype.pod-field-restriction` — Writ datatype fields must be POD-compatible
- **Divergence**: A6/A11: Writ datatype fabric is a Logos-only feature; uses extra packed int widths.
- **Rule**: A non-annotation `datatype` field type must be one of: an integer/float/bool/int-or-float-literal primitive kind (incl. packed i24/u24/i56/u56); an Array whose element type is itself datatype-safe (recursively); a ZonedStruct (nested datatype, always OK); a Struct only if it is a `#[rel_ptr]` self-relative pointer type (RelAny/RelPtr&lt;T&gt;, an 8-byte offset — plain structs that may carry heap/absolute pointers are rejected); or a TypeVar (deferred, resolved later by monomorphization). Any other field type raises a diagnostic error. Annotation types (is_annotation_type) are exempt (compile-time only, may hold e.g. str fields).
- **Source**: `src/compiler/sema_collect.cpp#L3931-L3973`

### `type.generic-inst.generic-const` — Generic compile-time const instantiation
- **Divergence**: A6
- **Rule**: Applying type-args to a generic const `pub const X<T1,T2>: WritStatic = @{...}` re-evaluates the const's value AST under the supplied type-arg bindings, yielding a fresh per-instantiation WStaticLit identity. The argument count must equal the const's type-param count.
- **Source**: `src/compiler/sema.cpp#L5345-L5392`

### `type.generic-inst.schema-unsized-arg-canonicalization` — Generic schema struct canonicalizes unsized type-args to sized fat form
- **Divergence**: A6
- **Rule**: When instantiating a generic `schema` struct, an unsized type-argument (`UnsizedSlice<T>`, e.g. produced for `Wrap<str>` under `?Sized`/turbofish) is canonicalized to its sized fat-slice form `Slice<T>`, matching the schema's WAny-handle field storage and `impl WritField for str` (= `Slice<u8>`). Non-schema generics (e.g. `Box<str>`) are unaffected.
- **Source**: `src/compiler/sema.cpp#L5588-L5601`

### `type.tagged.thin-pointer` — tagged thin pointer type
- **Divergence**: A6
- **Rule**: `&tagged<T> Name` is a thin tag-dispatched pointer: a type_code tag is stored in memory before the object, and call sites read the tag, look up the dispatch table, and call indirectly.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1490-L1494`

### `type.typeof.expr` — typeof type
- **Divergence**: A6
- **Rule**: `typeof(expr)` is the compile-time type of expr; the expression is not evaluated.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1461-L1463`

### `type.writ.lit-and-array-map` — Writ literal / typed array / typed map types
- **Divergence**: A6
- **Rule**: `@{...}` at type position is a WritStatic value literal type (LIT_WSTATIC). `<Elem>[]` is a Writ typed-array type and `<K[,V]>{}` is a Writ typed-map type (used in `as` casts).
- **Source**: `tools/peg_gen/grammars/logos.peg#L1451-L1473`

### `writ.schema-enum.arm-shape` — schema-enum match arm restricted to `E::Variant(b)` or `_`
- **Divergence**: A6
- **Rule**: In a match over a schema-enum scrutinee, each non-wildcard arm's top-level pattern must be a variant pattern (`E::Variant(b)` or bare `E::Variant`); any other pattern shape is rejected (`arm pattern must be 'E::Variant(b)' or '_'`). An or-pattern arm (`A | B =>`) over a schema enum is rejected as unsupported.
- **Source**: `src/compiler/sema_stmt.cpp#L8384-L8397`

### `writ.schema-enum.generic-variant-substitution` — generic schema-enum variant type instantiated by scrutinee type-args
- **Divergence**: A6
- **Rule**: For a generic schema enum `E<T...>`, an arm's variant type is instantiated by substituting the enum's type parameters with the scrutinee's concrete type-args (`E<i64>::A(Wrap<T>)` binds the payload as `Wrap<i64>`) before that variant's per-instance schema code is computed and before the arm binding is typed.
- **Source**: `src/compiler/sema_stmt.cpp#L8416-L8434`

### `writ.schema-enum.match-desugar` — match over a schema enum desugars to a schema_type_code if-chain
- **Divergence**: A6
- **Rule**: A `match` whose scrutinee's base type is a schema enum (a struct flagged `is_schema_enum`) desugars, ahead of the ordinary enum-match path, to: `let __sm = <scrut>.m; let __code = (&*__sm).schema_type_code();`, followed by an if/else-if chain comparing `__code` against each present variant's per-instance schema code; a matching arm binds its payload name to a struct-literal view `{ m: __sm }` typed as the concrete variant struct (a trusted view — no further runtime validation is performed beyond the code comparison). The variant discriminant is never a stored field; it is derived from the matched node itself via `schema_type_code()` at match time.
- **Source**: `src/compiler/sema_stmt.cpp#L8335-L8339`, `src/compiler/sema_stmt.cpp#L8493-L8501`, `src/compiler/sema_stmt.cpp#L8359-L8372`, `src/compiler/sema_stmt.cpp#L8452-L8478`, `src/compiler/sema_stmt.cpp#L8456-L8464`

### `writ.schema-enum.unknown-variant-error` — schema-enum arm naming a nonexistent variant is an error
- **Divergence**: A6
- **Rule**: A schema-enum match arm naming a variant absent from the enum's variant list is rejected: `schema enum '<E>' has no variant '<name>'`.
- **Source**: `src/compiler/sema_stmt.cpp#L8416-L8441`

## A7 — panic strategy = abort, only (no unwinding)

Registered: `docs/DIVERGENCES.md` §A row **A7** (design model). 2 rule(s).

### `generic.infer.never-fallback` — Unbound type-param falls back to ! for diverging callees
- **Divergence**: A7 — abort-only panic; `!`-fallback for diverging bodies follows Rust-2024 inference.
- **Rule**: If a non-variadic type-parameter remains unbound after inference, it is an error (ambiguous) UNLESS the callee's body is statically known to always diverge (panic/loop/never-returning tail), in which case the parameter falls back to the never type `!`. The discriminator is the callee body, not the surrounding callsite divergence: `fn f<T>()->T { return 0; }` errors as ambiguous while `fn f<T>()->T { panic(); }` resolves T = `!`.
- **Source**: `src/compiler/sema_expr.cpp#L3946-L3966`

### `item.extern.abi-whitelist` — extern ABI string whitelist
- **Divergence**: A7: "C-unwind" is accepted as a whitelisted ABI string at parse time even though unwinding-across-FFI is moot (panic strategy is abort-only).
- **Rule**: The ABI string of an `extern "ABI" { … }` block or an `extern "ABI" fn …` item must be one of "C", "C-unwind", "system", or "Rust" (enclosing quotes optional-stripped); any other string is rejected.
- **Source**: `src/compiler/sema_collect.cpp#L1355-L1366`, `src/compiler/sema_collect.cpp#L1400-L1401`

## A8 — `#[pinned]` type-level pinning coexisting with the `Pin<P>` API

Registered: `docs/DIVERGENCES.md` §A row **A8** (design model). 3 rule(s).

### `borrow.pin.non-movable-no-by-value-slot` — Location-anchored types may not occupy a by-value slot
- **Divergence**: A8: `#[pinned]` is non-movability as a property of the TYPE (no value-form), distinct from Rust's pointer-level Pin&lt;P&gt;.
- **Rule**: A non-movable (location-anchored) type — one with a self-relative `#[rel_ptr]`/`#[zoned2]` field, or a `#[pinned]` type — may not be bound to any by-value slot (let local, parameter, match/for/closure/destructure binding); it must live behind a pointer, in place (e.g. an arena or `[u8;N]` buffer), and be built through a `*mut T`.
- **Source**: `src/compiler/sema_impl.hpp#L2337-L2354`, `src/compiler/sema_impl.hpp#L2454-L2461`

### `layout.pinned.non-movable-type` — #[pinned] type is location-anchored and non-movable
- **Divergence**: A8
- **Rule**: A `#[pinned]` type's bits are anchored to its storage slot: it must not be moved by value, is accessed in place, and is materialized to a movable value form only explicitly. It is non-movable itself (unlike `#[rel_ptr]`, whose value form is the resolved absolute pointer).
- **Source**: `src/compiler/sema_impl.hpp#L2454-L2461`

### `type.return.non-movable-by-value-forbidden` — Location-anchored types cannot be returned by value
- **Divergence**: A8
- **Rule**: A type that is non-movable — containing a self-relative `#[rel_ptr]` field, or being `#[pinned]` — may not be returned by value; return a pointer (`*mut T` / `&T`) into its zone segment instead. (Crossing a function boundary by value would invalidate the self-relative anchor.)
- **Source**: `src/compiler/sema_decl.cpp#L501-L513`

## A9 — `.`-separated package path + `::` into the item

Registered: `docs/DIVERGENCES.md` §A row **A9** (design model). 11 rule(s).

### `expr.ctor.variant-alias-shorthand` — Bare enum-variant constructor via use-alias
- **Divergence**: Logos `use Type.{V}` variant-import surface (pkg `.` / item `::` path model) *(untagged; matches the A9 path model)*
- **Rule**: A `use Enum.{V, …};` import registers variant aliases; a bare call `V(payload)` whose name is an imported variant alias constructs that enum's variant `V` (typed via enum-literal lowering with payload typing), when no function of that name resolved.
- **Source**: `src/compiler/sema_expr.cpp#L5943-L5953`

### `grammar.expr.call-package-qualified` — Package-qualified free-function call
- **Divergence**: Logos path model: '.'-separated package path + '::'-item (vs Rust all-'::'). *(untagged; matches the A9 path model)*
- **Rule**: A call 'IDENT path_dot_ident+ '::' IDENT ('::' '&lt;' type_arg_list '&gt;')? '(' call_arg_list? ')'' resolves a free fn by its dotted package path (RECEIVER = first segment, QUAL_PARTS = rest); this disambiguates same-named free fns across packages (e.g. logos.lang.mem::replace vs logos.lang.ptr::replace).
- **Source**: `tools/peg_gen/grammars/logos.peg#L3191-L3203`

### `item.use.path-form` — use declaration path form
- **Divergence**: Logos paths use `.` for package/module segments rather than Rust's `::`. *(untagged; matches the A9 path model)*
- **Rule**: A use declaration is `[pub] use NAME(.part)* ;`, where path segments after the head are dot-separated.
- **Source**: `src/compiler/sema_render.cpp#L1036-L1050`, `src/compiler/sema_render.cpp#L1182-L1190`

### `module.package.decl` — Package declaration header
- **Divergence**: Rust uses no `package` header; module name is path-derived. Logos requires an explicit `package` line with a dotted package path. *(untagged; matches the A9 path model)*
- **Rule**: A compilation unit begins with `package NAME ('.' IDENT)* ';'`, optionally preceded by inner doc-comments (`//!`, `/*! */`) and inner attributes (`#![...]`). The dotted path gives the package's full name to arbitrary depth (first component = NAME, remaining components = PATH_PARTS). After the package line come zero-or-more use-declarations, then zero-or-more items.
- **Source**: `tools/peg_gen/grammars/logos.peg#L489-L490`

### `module.path.package-name` — Package name is dot-joined module path
- **Divergence**: A9
- **Rule**: A package's fully-qualified name is its module NAME with each PATH_PART name appended joined by `.` (e.g. `my.cool.pkg`).
- **Source**: `src/compiler/sema_collect.cpp#L731-L744`

### `module.path.qualified-call` — Package-qualified call scopes free-fn resolution to that package
- **Divergence**: A9
- **Rule**: A package-qualified call `pkg.path::fn(args)` carries RECEIVER + QUAL_PARTS (the dotted package segments); for the duration of lowering this call, free-function-name resolution is constrained to that package via `call_pkg_qualifier_` (RAII-restored to the prior value on return, supporting nesting). The qualified form's ARGS may be wrapped in a `call_arg_list` node (`{ITEMS:[...]}`) instead of a raw array; both shapes are accepted, gated on QUAL_PARTS so the (overwhelmingly common) unqualified raw-array shape is never reinterpreted as a map.
- **Source**: `src/compiler/sema_expr.cpp#L2760-L2803`

### `module.path.qualified-member-fallback` — Qualified call with no matching free fn falls back to a type-member static call
- **Divergence**: A9
- **Rule**: A qualified call `pkg.path::name(args)` whose package has no matching free function and no matching generic free function is instead treated as a type-member static call `pkg.path.Type::method(...)` (the trailing dotted segment names the type) and delegated to `lower_static_call`, which re-derives the class from QUAL_PARTS. A free function of that name in the qualified package always takes precedence over this fallback.
- **Source**: `src/compiler/sema_expr.cpp#L2805-L2814`

### `module.use.brace-group-desugar` — `use pkg.{a, b, c}` with a lowercase head desugars to N wildcard imports
- **Divergence**: note — Logos path model uses `.` for packages, `::` for items. *(untagged; matches the A9 path model)*
- **Rule**: A grouped use whose head segment begins with a lowercase letter is treated as a package path: `use pkg.{a, b, c}` desugars to wildcard imports `pkg.a.*`, `pkg.b.*`, `pkg.c.*`. A head segment beginning uppercase is instead the enum-variant import form.
- **Source**: `src/compiler/sema.cpp#L6835-L6861`

### `module.use.variant-shorthand` — Enum-variant bare-name import
- **Divergence**: Uses `.`-separated path with `.{}` variant group; Rust spells this `use core::Option::{Some, None};` (A: `::`-item / `.`-pkg path model). *(untagged; matches the A9 path model)*
- **Rule**: `use pkg.Path.Type.{V1, V2, ...} ;` brings the named variants of enum `Type` into bare (unqualified) scope. The last dotted component before `.{...}` is the enum type name; the brace-list (trailing comma allowed) names the variants.
- **Source**: `tools/peg_gen/grammars/logos.peg#L506-L511`, `tools/peg_gen/grammars/logos.peg#L523-L527`

### `mono.uid.module-fingerprint-tags` — Runtime type UID includes per-module fingerprint tags
- **Divergence**: A9 — Logos coexistence of same-named types across modules; no Rust crate-disjointness analog.
- **Rule**: The canonical type-identity string for runtime UID hashing (type_id::&lt;T&gt;(), Any/downcast/quote_ty) is the type-string PLUS a '|&lt;name&gt;$M&lt;module_id&gt;' tag for EVERY non-stdlib nominal node anywhere in the type tree (recursing through pointee/elem/type-args/tuple-elems/closure params+ret), so two modules' same-named pkg::Type (incl. nested, e.g. Box&lt;pkg::Widget&gt;) hash to DISTINCT UIDs. stdlib (logos.*) and no-module compiles contribute no tags, yielding a string byte-identical to the plain type-string (UIDs unchanged).
- **Source**: `src/compiler/mono_impl.hpp#L772-L808`

### `type.ref.dotted-path` — Fully-qualified non-generic type path
- **Divergence**: Logos path model: `.` for package/module path, `::` for items. *(untagged; matches the A9 path model)*
- **Rule**: A fully-qualified non-generic type in type position is written `pkg.path.Type` (dotted); the last path segment is the type. Matched before bare-IDENT alternatives so the whole dotted form is claimed. The generic dotted form `pkg.path.Type<A>` is not supported (use a `use` import + short name).
- **Source**: `tools/peg_gen/grammars/logos.peg#L1805-L1813`

## A10 — `dyn Fn*` collapses to the bare Closure pair

Registered: `docs/DIVERGENCES.md` §A row **A10** (replaced). 3 rule(s).

### `layout.closure.fn-env-pair` — Closure value is a {fn_ptr, env_ptr} pair
- **Divergence**: A10
- **Rule**: A closure value is represented as a struct with field 0 = function pointer and field 1 = environment pointer. Calling a closure loads both fields and invokes the function indirectly with env_ptr prepended as the first argument, ahead of the user-supplied arguments.
- **Source**: `src/compiler/mlir_gen_expr.cpp#L4819-L4845`

### `type.closure.fat-fn-env-repr` — Closures represent uniformly as a 16-byte {fn,env} fat pair
- **Divergence**: A10
- **Rule**: Every closure value (the `Closure` kind, which also covers `dyn Fn`/`FnMut`/`FnOnce`) has a FIXED 16-byte {fn-ptr, env-ptr} storage representation — not a per-closure anonymous capture struct sized by its captures. Stored inline in aggregates/arrays exactly like a Slice; a plain closure value elsewhere is a pointer to this 16-byte storage.
- **Source**: `src/compiler/mlir_gen_types.cpp#L111-L112`, `src/compiler/mlir_gen_types.cpp#L130`, `src/compiler/mlir_gen_types.cpp#L314-L321`, `src/compiler/mlir_gen_types.cpp#L468-L469`

### `type.dyn.fn-family-is-closure` — dyn Fn/FnMut/FnOnce resolves to Closure
- **Divergence**: A10
- **Rule**: `dyn Fn(P...) -> R`, `dyn FnMut(...)`, `dyn FnOnce(...)` resolve directly to the Closure type {fn_ptr, env_ptr}; there is no distinct Fn-trait-object vtable layer.
- **Source**: `src/compiler/sema.cpp#L5928-L5952`

## A11 — extra integer widths `I24`/`U24`/`I56`/`U56`

Registered: `docs/DIVERGENCES.md` §A row **A11** (addition). 10 rule(s).

### `const.literal.integer-suffix-by-kind` — integer constant suffix by type kind
- **Divergence**: Logos has additional integer widths I24/U24/I56/U56 beyond Rust's fixed set. *(untagged; matches A11)*
- **Rule**: An integer constant carries a type suffix matching its kind (i8/i16/i32/i64/u8/u16/u32/u64); IntLit and the non-power-of-two-byte kinds I24/U24/I56/U56 are emitted unsuffixed. Signedness is determined by the kind (signed: i8/i16/i24/i32/i56/i64/i128/IntLit).
- **Source**: `src/compiler/sema_render.cpp#L992-L1016`

### `expr.litint.width-by-type` — Integer literal bit-width from its inferred type
- **Divergence**: A: i24/u24/i56/u56 are Logos-only integer widths (no Rust equivalent). *(untagged; matches A11)*
- **Rule**: An integer literal is encoded at the bit-width of its inferred type: i8/u8=8, i16/u16=16, i24/u24=24, i32/u32=32, i56/u56=56, i64/u64=64, i128/u128=128, bool=1. usize/isize use the target pointer bit-width. An untyped integer literal (IntLit) defaults to 32 bits, widening to 64 bits when its value falls outside [INT32_MIN, INT32_MAX].
- **Source**: `src/compiler/mlir_gen_expr.cpp#L253-L298`

### `expr.writ.int-suffix-and-radix` — Writ integer literal: suffix stripping and radix
- **Divergence**: Logos addition (Writ literals); note i24/i56/u24/u56 width suffixes. *(untagged; matches A11)*
- **Rule**: A Writ integer literal accepts an optional numeric-type suffix (i8/i16/i24/i32/i56/i64/i128, u8/u16/u24/u32/u56/u64/u128, usize, isize) which is stripped before parsing, an optional leading '-', and a radix prefix: `0x` = hexadecimal, `0b` = binary, otherwise decimal. The resulting magnitude is negated if the sign was present.
- **Source**: `src/compiler/sema_expr.cpp#L15027-L15050`

### `layout.abi.scalar-sizes` — Scalar ABI byte sizes
- **Divergence**: A11 (I24/U24/I56/U56 are Logos-only widths)
- **Rule**: ABI size: void/never = 0; bool/u8/i8 = 1; i16/u16 = 2; i24/u24 = 3; i32/u32/f32/char = 4; i56/u56 = 7; i64/u64/f64/usize/isize/pointer/&/&mut/fnptr/fn-item/tagged-ptr = 8; i128/u128 = 16. The Writ-fabric widths I24/U24/I56/U56 occupy their narrow byte sizes (3 and 7).
- **Source**: `src/compiler/mono_clone.cpp#L348-L361`

### `layout.int.fixed-widths` — Fixed-width scalar sizes/alignments
- **Divergence**: A11
- **Rule**: Scalar type layout is fixed and self-aligned: bool/i8/u8={1,1}; i16/u16={2,2}; i24/u24={3,1}; i32/u32/f32/char={4,4}; i56/u56={7,1}; i64/u64/f64={8,8}; i128/u128={16,16}; usize/isize={ptr-width,ptr-width}. The odd widths i24/u24/i56/u56 have byte size = ceil(bits/8) but align 1 (packed, not natively aligned).
- **Source**: `src/compiler/mlir_gen_types.cpp#L44-L63`, `src/compiler/mlir_gen_types.cpp#L456-L466`

### `lex.literal.int-suffix` — Integer literal type suffix
- **Divergence**: Includes Logos-specific suffixes i24/u24/i56/u56 absent in Rust. *(untagged; matches A11)*
- **Rule**: An integer literal may carry an explicit type suffix selecting its kind: i8/i16/i24/i32/i56/i64/i128, u8/u16/u24/u32/u56/u64/u128, usize, isize; the suffix follows the (optionally radix-prefixed) digit body. Absence of a recognised suffix yields the unsuffixed literal type.
- **Source**: `src/compiler/sema_impl.hpp#L4812-L4841`

### `lex.literal.integer` — Integer literal syntax and width suffixes
- **Divergence**: A11: width set includes Writ-fabric widths i24/u24/i56/u56 beyond Rust's {8,16,32,64,128}+size. Also: a leading `-` is part of the integer token itself (Rust treats `-` as a separate unary operator).
- **Rule**: An integer literal matches an optional leading `-`, then a decimal (`[0-9][0-9_]*`), hex (`0x[0-9a-fA-F_]+`), binary (`0b[01_]+`), or octal (`0o[0-7_]+`) magnitude, with `_` digit separators, optionally suffixed by a width tag drawn from {i8,i16,i24,i32,i56,i64,i128,u8,u16,u24,u32,u56,u64,u128,usize,isize}.
- **Source**: `tools/peg_gen/grammars/logos.peg#L457`

### `pat.writ.int-i24-range` — Writ integer pattern fits i24
- **Divergence**: Logos addition; i24 bound is Writ-specific. *(untagged; matches A11)*
- **Rule**: A Writ integer pattern `@<int>` value v must satisfy -2^23 &lt;= v &lt; 2^23 (i24 range); otherwise it is a compile error. The literal may carry a negation flag that negates the parsed magnitude.
- **Source**: `src/compiler/sema_stmt.cpp#L5312-L5327`

### `type.integer.kind-set` — Integer-class type kinds
- **Divergence**: Logos adds non-power-of-two integer widths i24/u24/i56/u56 (not in Rust); also classifies Enum as an integer kind. *(untagged; matches A11)*
- **Rule**: The integer type class comprises the fixed-width signed/unsigned kinds {i8,u8,i16,u16,i24,u24,i32,u32,i56,u56,i64,u64,i128,u128}, the pointer-sized {usize,isize}, the unsuffixed-literal type IntLit, and Enum. An enum type is treated as an integer kind for these classifications.
- **Source**: `src/compiler/sema_impl.hpp#L4439-L4449`

### `type.primitive.set` — Built-in primitive scalar types
- **Divergence**: A: extra fixed-width widths i24/u24/i56/u56 and 128-bit i128/u128 beyond Rust's standard set. *(untagged; matches A11)*
- **Rule**: The language has primitive scalar types: void, bool, char, the floats f32/f64, and the integers i8/u8, i16/u16, i24/u24, i32/u32, i56/u56, i64/u64, i128/u128, isize/usize. Each is a distinct type identified by its keyword name.
- **Source**: `src/compiler/sema.cpp#L2077-L2097`, `src/compiler/sema.cpp#L2530-L2551`

## A12 — unit `()` vs internal `Kind::Void` no-return split

Registered: `docs/DIVERGENCES.md` §A row **A12** (addition). 1 rule(s).

### `layout.zero-size.void-never` — Void/Never/unit-field are zero-sized, no SSA value
- **Divergence**: A12
- **Rule**: `Void` (absence of a return value) and `Never` (`!`, an uninhabited/diverging type) both have layout {0,1} and lower to no SSA value at all (a diverging expression emits its own terminator instead of a value). When either occurs as a concrete struct FIELD's type (e.g. a `!`-typed Err payload, or a unit `()` field), it is materialized as a genuine zero-size `[i8; 0]` storage slot so the aggregate's other field offsets stay correct, even though it is never read.
- **Source**: `src/compiler/mlir_gen_types.cpp#L40-L43`, `src/compiler/mlir_gen_types.cpp#L322-L343`, `src/compiler/mlir_gen_types.cpp#L455`

## A13 — integer overflow always traps (aborts) in every profile

Registered: `docs/DIVERGENCES.md` §A row **A13** (design model). 1 rule(s).

### `expr.binop.integer-overflow-trap` — Checked +/-/* trap on overflow
- **Divergence**: A13: always traps on integer +/-/* overflow regardless of build profile (Rust wraps in release, panics in debug); explicit wrapping_* for wraparound.
- **Rule**: Integer `+`, `-`, `*` are checked: on overflow execution aborts (trap). Signed/unsigned overflow detection selects checked signed vs unsigned arithmetic by the LHS type's signedness. Intentional wrapping must use the `wrapping_add`/`wrapping_sub`/`wrapping_mul` intrinsics, which emit the unchecked operation.
- **Source**: `src/compiler/mlir_gen_expr.cpp#L835-L884`

## §A-claimed, no row number

These divergence notes claim blessed (§A) status with a bare `A:` prefix but cite no row. Most are Writ/zone/layout additions that belong under the blanket **A6** addition row; each needs either a concrete row assignment or an explicit new §A row. 14 rule(s). **Partially registered — row assignment needed.**

### `borrow.escape.borrow-carrying-struct` — borrow_carrying struct/enum values are escape-tracked like references
- **Divergence**: A: #[borrow_carrying] Logos addition for opaque borrow-holding types (WAny).
- **Rule**: Values of a struct or enum annotated `#[borrow_carrying]` are tracked by the borrow checker for escape/lifetime like ordinary references.
- **Source**: `src/compiler/sema_decl.cpp#L1226-L1228`, `src/compiler/sema_decl.cpp#L1444`

### `borrow.union.field-borrow-borrows-all` — Borrowing one union field borrows the whole union
- **Divergence**: A-union-field-borrow-whole (Rust has no native untagged unions with this exact borrow-widening rule; treat as Logos-specific until reconciled against a §A tag)
- **Rule**: Because union fields share storage, a borrow of any one field of a union implicitly borrows ALL fields; field-path borrows of a union root are coerced to whole-root borrows so any other field-path of the same union overlaps and conflicts.
- **Source**: `src/compiler/borrow_check.cpp#L934-L949`

### `const.def.writ-static-literal-compat` — WStaticLit initializer is compatible with a WritStatic const
- **Divergence**: A: Writ-static literal coercion is a Logos addition.
- **Rule**: A const whose declared type is a WritStatic struct accepts an initializer whose type is a Writ-static literal (WStaticLit); this combination is treated as type-compatible.
- **Source**: `src/compiler/sema_decl.cpp#L1581-L1585`

### `layout.enum.niche-lowbit-encoding` — LowBit niche enum payload encoding
- **Divergence**: A: niche-packing layout is Logos-defined; not a Rust-guaranteed representation.
- **Rule**: For an enum with a LowBit niche packed into a single word: the pointer arm stores the pointer's raw integer value (low bit 0, guaranteed by &gt;=2 alignment); the value arm stores (v&lt;&lt;1)|1 after sign/zero extension to the word width. In RAW mode the producer-supplied value (low-bit already set) is stored verbatim without shifting. An empty payload stores 0.
- **Source**: `src/compiler/mlir_gen_expr.cpp#L570-L602`

### `lex.ident.ascii-only` — Identifiers are ASCII-only
- **Divergence**: A: diverges from Rust, which accepts Unicode (XID) identifiers.
- **Rule**: Identifiers consist of ASCII bytes only; a source line containing a non-ASCII (high-bit, &gt;= 0x80) byte at the point of a syntax error is diagnosed as an identifier encoding error, since non-ASCII bytes cannot form a valid identifier token.
- **Source**: `src/compiler/module_loader.cpp#L1361-L1377`

### `pat.refutable.nested-variant-guard` — Nested variant inner pattern lowers to a synthesized guard
- **Divergence**: A — guarded nested-variant arms need a catch-all for exhaustiveness (DIVERGENCES.md: finite-enum coverage of guarded arms not yet proven)
- **Rule**: A nested variant inner pattern (e.g. `Some(Color::Red)`, `Some(Some(v))`) binds the outer payload to a synthetic name and gates the arm with a synthesized `match synth { <inner> => <check>, _ => false }`; binding-carrying inners additionally re-extract their bindings in the arm body via a let-else, composing to arbitrary depth.
- **Source**: `src/compiler/sema_stmt.cpp#L3284-L3453`

### `stmt.diverge.never-returning-call` — call to a Never-returning fn diverges
- **Divergence**: A: `panic` recognized as divergent by hardcoded callee name (Logos historically lacked the `!` type); now generalized to any `-> !` callee.
- **Rule**: A call expression `f(...)` (including the macro form `panic!(...)` which parses as FN_MACRO_CALL) in expression-statement, tail-expression, or let-initializer position is divergent — control never falls through — iff the callee is named `panic` OR any candidate function with that name has return type `!` (Never). `panic` is recognized by name even without a `!` annotation.
- **Source**: `src/compiler/sema_stmt.cpp#L34-L53`, `src/compiler/sema_stmt.cpp#L208-L218`

### `stmt.fallback.never-only-on-provable-divergence` — Never-fallback gated on provably non-returning body
- **Divergence**: A: implements a Rust-2024-style `!`-fallback but with a stricter, narrower divergence predicate than full `block_always_returns`.
- **Rule**: A generic return type-param may fall back to `!` only when the callee body provably never returns normally — i.e. the body's last statement is a divergent call (`panic`/`-> !`), a `loop`, or an expression-statement/tail wrapping a `loop`. A body ending in `return 0;` does NOT qualify (that is a normal return, leaving the type-param ambiguous).
- **Source**: `src/compiler/sema_stmt.cpp#L194-L226`

### `trait.def.copy-not-a-supertrait` — Copy is excluded from a trait's supertrait list
- **Divergence**: A: Copy treated specially, excluded from supertrait closure.
- **Rule**: When recording a trait's declared supertraits, `Copy` is omitted from the supertrait set (it is an auto/marker bound, not a vtable-bearing supertrait).
- **Source**: `src/compiler/sema_decl.cpp#L1665-L1669`

### `type.struct.non-null-niche` — non_null single-pointer wrapper yields Option niche
- **Divergence**: A: #[non_null] attribute is a Logos addition mirroring Rust NonNull niche.
- **Rule**: A struct annotated `#[non_null]` wrapping a single non-null pointer makes `Option<T>` use the null-pointer value as the None niche (no discriminant overhead).
- **Source**: `src/compiler/sema_decl.cpp#L1229-L1230`

### `type.struct.rel-ptr-offset-storage` — rel_ptr struct is a self-relative pointer
- **Divergence**: A: RefRepr RelOffset Logos addition, no Rust analog.
- **Rule**: A struct annotated `#[rel_ptr]` is classified as a self-relative pointer using 8-byte offset storage.
- **Source**: `src/compiler/sema_decl.cpp#L1223-L1225`

### `type.struct.self-describing-thin-ptr` — self_describing keeps *Self thin
- **Divergence**: A: Writ/RefRepr Logos addition, no Rust analog.
- **Rule**: A struct annotated `#[self_describing]` keeps `*Self` a thin pointer (no DstRef fattening) under Ptr→DstRef canonicalization.
- **Source**: `src/compiler/sema_decl.cpp#L1216-L1218`

### `type.struct.zone-mut-fat-ref` — zone_mut makes &mut T fat carrying its allocator
- **Divergence**: A: Writ zone model Logos addition; Rust &mut is thin.
- **Rule**: For a struct annotated `#[zone_mut]`, a `&mut T` reference is a fat `{data, zone}` pair carrying the value's allocator/zone.
- **Source**: `src/compiler/sema_decl.cpp#L1219-L1221`

### `type.struct.zoned2-relative-fields` — zoned2 struct fields use relative pointers
- **Divergence**: A: Writ zoned2 Logos addition, no Rust analog.
- **Rule**: A struct annotated `#[zoned2]` stores its pointer fields as self-relative offsets (RelOffset) rather than absolute addresses.
- **Source**: `src/compiler/sema_decl.cpp#L1222`

## B1 — §B catch-up reference

Registered: `docs/DIVERGENCES.md` §B row **B1** — generic `T: Copy` treated as move — DONE 2026-05-22 (bound now makes `T` Copy). 1 rule(s).

### `borrow.classify.type-param-move-unless-copy` — Bare type parameter moves unless Copy-bounded
- **Divergence**: B1
- **Rule**: Inside a generic body a bare type-parameter T is a Move type unless it carries an explicit `T: Copy` bound; partial moves of fields typed T are tracked accordingly.
- **Source**: `src/compiler/borrow_check.cpp#L284-L297`

## B2 — §B catch-up reference

Registered: `docs/DIVERGENCES.md` §B row **B2** — custom-DST tail-slice + owning `Box<Foo>` — DONE 2026-05-29. 8 rule(s).

### `expr.field.dst-prefix-offset` — DST non-tail field addressed positionally
- **Divergence**: B2
- **Rule**: A non-tail (prefix) field of a custom-DST struct accessed through a DstRef fat pointer is addressed positionally: its byte offset is the sum of the ABI sizes (each padded to its natural alignment, capped at 8) of all preceding declared fields, with the DstRef's carried type-arguments substituted into generic field types; the field is read by dereferencing `data+offset` typed as the field's (substituted) type.
- **Source**: `src/compiler/sema_expr.cpp#L9682-L9717`

### `expr.field.dst-prefix-positional` — Prefix (non-tail) field access on a DstRef is positional
- **Divergence**: Custom-DST model — see DIVERGENCES B2.
- **Rule**: For a fat-pointer receiver to a custom-DST struct, a non-tail prefix field is addressed positionally: its byte offset is computed by walking the sized prefix fields (with the DstRef's type-args substituted), and the field is read by dereferencing `data_ptr + offset` typed as the field type. This works uniformly for generic and non-generic DST instances, including those with no registered monomorphized layout.
- **Source**: `src/compiler/sema_expr.cpp#L9394-L9429`

### `expr.field.dst-ref-unsafe` — Field read through a custom-DST fat-pointer reference requires unsafe unless self-describing
- **Divergence**: B2 — custom-DST raw-pointer-shaped field access (see DIVERGENCES.md).
- **Rule**: Reading any field through a fat-pointer (DstRef) receiver `&CustomDstStruct` requires an enclosing `unsafe` context, UNLESS the struct is declared `#[self_describing]` — its tail length is recovered in-band, making the borrow a complete, safe reference. Otherwise the program is rejected with: "field read through `&DstStruct` requires unsafe context (custom-DST field access is raw-pointer-shaped)".
- **Source**: `src/compiler/sema_expr.cpp#L9275-L9281`, `src/compiler/sema_expr.cpp#L9564-L9569`

### `expr.field.dst-tail-dyn` — DST dyn-tail field projection shares the DstRef's carried vtable
- **Divergence**: Custom-DST dyn-tail model — see DIVERGENCES B2/B3.
- **Rule**: For a custom-DST struct whose tail field's (generic-substituted) type is `dyn Trait`, projecting the tail field from a `&Struct` DstRef fat pointer `{data, vtable}` yields a `&dyn Trait` fat pair `{ data = base + prefix_byte_size, vtable = the receiver's OWN carried vtable }`, reusing the wide pointer's metadata verbatim — no static/independent vtable lookup for the tail. The dyn-tail prefix offset is aligned to pointer width (8 bytes) since the concrete payload alignment is not known statically.
- **Source**: `src/compiler/sema_expr.cpp#L9330-L9335`, `src/compiler/sema_expr.cpp#L9346-L9368`, `src/compiler/sema_expr.cpp#L9634-L9656`

### `expr.field.dst-tail-slice` — Slice-tail projection on a DstRef
- **Divergence**: Custom-DST model — see DIVERGENCES B2.
- **Rule**: For a fat-pointer receiver to a custom-DST struct whose last field `tail` has unsized-slice type `[T]`, `r.tail` yields a slice `{ data_ptr + prefix_byte_size, len }` reusing the fat pointer's len half; prefix_byte_size is the offset after all sized prefix fields, aligned to size_of(T) (capped at 8). Slice mutability follows the receiver: `(&mut Foo).tail: &mut [T]`, `(&Foo).tail: &[T]`.
- **Source**: `src/compiler/sema_expr.cpp#L9296-L9345`, `src/compiler/sema_expr.cpp#L9369-L9393`, `src/compiler/sema_expr.cpp#L9657-L9681`

### `expr.field.self-describing-thin-tail` — Self-describing DST tail through a thin raw pointer
- **Divergence**: Custom-DST / self-describing model — see DIVERGENCES B2.
- **Rule**: For a thin raw pointer `p: *const/*mut Self` to a `#[self_describing]` struct whose last field is the unsized-slice tail, `p.tail` yields a slice `{ (p as *u8)+prefix_offset, dst_len(p) }`, where prefix_offset is the natural-aligned byte offset after all sized prefix fields and the tail length is recovered by calling the struct's `SelfDescribing::dst_len` method. Slice mutability follows the pointer's mutability.
- **Source**: `src/compiler/sema_expr.cpp#L9185-L9248`

### `layout.dyn.fat-pointer-data-vtable-pair` — dyn trait object is a 16-byte {data, vtable} fat pair by value
- **Divergence**: B2/B3: fat-pointer model for owned dyn; Box&lt;dyn&gt; is the owning trait object.
- **Rule**: `&dyn Trait`, `*dyn Trait`, and `Box<dyn Trait>` share a uniform 16-byte fat representation: a `{data_ptr, vtable_ptr}` pair stored inline. `data_ptr` is the concrete value's address (heap concrete for an owning `Box<dyn>`). The pair travels by value; escape consumers copy the 16 bytes into their own inline storage rather than holding a heap handle.
- **Source**: `src/compiler/mlir_gen_dyn.cpp#L1204-L1234`, `src/compiler/mlir_gen_dyn.cpp#L1264-L1270`

### `type.struct.dst-tail-slice-last-field` — Custom-DST slice tail only at last field
- **Divergence**: B2: custom-DST tail-slice (DONE) — Logos supports `struct Foo { hdr: H, tail: [T] }`.
- **Rule**: An unsized slice type (`[T]`, UNSIZED_SLICE_TYPE node) is permitted as a struct field's type only when that field is the last FIELD_DEF in the struct; the unsized-allowed flag is set only for resolving that one field's type node and restored immediately after. When used there, the struct is marked is_dst.
- **Source**: `src/compiler/sema_collect.cpp#L4226-L4272`

## B4 — §B catch-up reference

Registered: `docs/DIVERGENCES.md` §B row **B4** — accumulated unsupported-syntax gaps surfaced by imports (rolling row). 4 rule(s).

### `pat.for-loop.ref-element-deref` — by-ref for-loop element is dereferenced before destructure
- **Divergence**: B4
- **Rule**: When the iterated element type is `&T`/`&mut T`, the loop binding is dereferenced to a value temporary of type `T` and the tuple pattern destructures that value (by-ref default binding modes are not applied).
- **Source**: `src/compiler/sema_stmt.cpp#L8277-L8291`

### `pat.for-loop.tuple-only` — for-loop pattern restricted to tuple of names/nested-tuples
- **Divergence**: B4
- **Rule**: A `for <pat> in <iter>` loop pattern that is destructured in place must be a tuple pattern `(p0, ..., pn)` over a tuple-typed element; each element pattern must be a name, `_`, or a nested tuple pattern (recursed). Any other element sub-pattern (literal, struct, variant, range, etc.) is rejected; a non-tuple top-level pattern over a non-tuple element is rejected (`bind a name and destructure in the body`).
- **Source**: `src/compiler/sema_stmt.cpp#L8292-L8297`, `src/compiler/sema_stmt.cpp#L8311-L8330`

### `stmt.let-pat.array-fixed-no-rest` — let [p0,p1,...] = arr requires exact fixed-length match, no rest
- **Divergence**: B4
- **Rule**: `let [p0, p1, ...] = arr;` is treated as irrefutable, and thus legal at `let`, only when arr's static type is a fixed-size Array and the pattern's element count equals the array length exactly; a `..` rest in this position is a compile error (refutable-shape restriction), and an element-count mismatch is a compile error. Each element position must bind a plain identifier or `_` to skip — any other element-pattern shape is a compile error.
- **Source**: `src/compiler/sema_stmt.cpp#L1120-L1136`, `src/compiler/sema_stmt.cpp#L1238-L1269`, `src/compiler/sema_stmt.cpp#L1286-L1317`

### `stmt.let-pat.struct-shapes-only` — let &lt;pattern&gt; = expr accepts only shapes provable irrefutable here
- **Divergence**: B4
- **Rule**: `let <pattern> = expr;` accepts only pattern shapes this lowering can prove irrefutable: plain struct patterns, tuple-struct patterns (rewritten via synthesized "0","1",... field names), fixed-size array patterns whose element count matches the array length exactly (no rest), and struct-shaped single-variant-enum patterns; any other pattern shape at this position is rejected with a diagnostic directing the user to `match`/`let-else`.
- **Source**: `src/compiler/sema_stmt.cpp#L1079-L1136`

## B6 — §B catch-up reference

Registered: `docs/DIVERGENCES.md` §B row **B6** — NLL scope-lifetime (E0597) — CLOSED 2026-06-19. 1 rule(s).

### `borrow.nll.capture-flow-store` — Storing a borrowing argument into a receiver taints the receiver's provenance
- **Divergence**: B6: NLL E0597 via capture-flow on container-element stores.
- **Rule**: When a `&mut self` method is called on a tracked local receiver and a by-value borrow-carrying argument (or an argument whose ref-type equals the receiver container's element type, e.g. `Vec<&T>::push(&x)`) is stored into the receiver, the receiver transitively acquires the argument's borrow of the source local. A later use of the receiver after that source local dies is then E0597. `&self` reads and `&x` ref-args do not taint (so `v.contains(&x)`/`v.len()` stay clean).
- **Source**: `src/compiler/borrow_check.cpp#L3563-L3604`

## B8 — §B catch-up reference

Registered: `docs/DIVERGENCES.md` §B row **B8** — assignment drop-before-replace — RESOLVED 2026-05-28 (full drop elaboration). 2 rule(s).

### `expr.drop.flag-uninit-conditional` — Conditionally/late-initialized variables drop only when live
- **Divergence**: Logos drop flags / static drop tracking (B8). Models Rust's conditional drop flags.
- **Rule**: A variable that may be uninitialized at a drop point runs its destructor only if it currently holds a live value. With dynamic tracking a per-variable drop flag (0/1) is consulted at runtime (flag==1 → drop, else no-op). With static tracking the destructor is emitted only when the variable is statically known to be assigned at that point; an early return before first assignment, the !c arm of a conditional init, or a never-assigned variable drops nothing.
- **Source**: `src/compiler/mlir_gen_stmt.cpp#L1184-L1214`

### `mono.subst.assign-drop-old-preserved` — Drop-before-replace flag survives monomorphization
- **Divergence**: B8 (resolved — Rust-conformant)
- **Rule**: For an assignment `x = v` and for deref-write `*p = v`, the drop-before-replace flag (drop old contents iff the place is initialized) is carried verbatim through substitution.
- **Source**: `src/compiler/mono_clone.cpp#L4470-L4476`, `src/compiler/mono_clone.cpp#L4600-L4606`

## B-assoc — §B catch-up reference

Registered: `docs/DIVERGENCES.md` §B row **B-assoc** — ambiguous associated-type projection across two `Trait<T>` impls at distinct `T` (G156-1) — OPEN, priority low. 2 rule(s).

### `type.assoc-ref.deferred-node` — Deferred associated-type node carries trait args
- **Divergence**: B-assoc
- **Rule**: An unresolved projection yields a deferred AssocType node {base, trait, name, gat_args}; the trait name is suffixed with the concrete trait type-args so distinct `Trait<T>` instantiations produce distinct nodes (empty suffix for non-generic traits preserves the bare name). Bounds declared on the assoc type are propagated into the projection's bound context.
- **Source**: `src/compiler/sema.cpp#L5308-L5337`

### `type.assoc-ref.eager-concrete-projection` — Eager projection for concrete base with generic trait
- **Divergence**: B-assoc
- **Rule**: When the base is a concrete type and the resolved trait is generic (has type-args), the projection is resolved immediately by looking up the trait+args-suffixed assoc-type impl and substituting the base's type-args; this disambiguates two `Trait<T>` impls on one type that would otherwise intern to a single trait-arg-less deferred node and collapse.
- **Source**: `src/compiler/sema.cpp#L5275-L5307`

## G156-1 — trait type-arg method mangling (baghunt)

Registered: `docs/DIVERGENCES.md` §B row **B-assoc** and `docs/baghunt/README.md` (G156-1). STILL OPEN — two same-trait impls at distinct type-args can collide in mangling, and a bare `X::Assoc` projection erases the ambiguous key. 3 rule(s).

### `mono.assoc.suffixed-projection-resolution` — Associated types resolve per trait type-args when a type has multiple impls of one trait
- **Divergence**: G156-1: addresses two same-trait impls at distinct type-args; tracked as a known narrow area.
- **Rule**: When a type has multiple impls of one parameterized trait at distinct type-args (e.g. two `Trait<T>` impls), an associated-type projection `<P as Trait<i64>>::A` resolves to the impl matching the trait's concrete type-args (via a type-args suffix key); a bare projection resolves first-wins.
- **Source**: `src/compiler/mono.cpp#L251-L277`

### `mono.dispatch.trait-qualified-mangling` — Ambiguous-by-name dispatch resolves to trait-qualified symbol
- **Divergence**: Logos-specific name-mangling scheme; see G156-1 baghunt for two-impl collision
- **Rule**: When a method call on a type-variable receiver is dispatch-ambiguous by name and a trait was selected (trait T), monomorphization resolves the callee base to `<recv-type>__<T>__<method>` if such a symbol exists; otherwise it falls back to the plain `<recv-type>__<method>`.
- **Source**: `src/compiler/mono_clone.cpp#L3758-L3774`

### `trait.assoc-type.dual-impl-ambiguous-projection` — Ambiguous bare associated-type projection across generic-trait impls
- **Divergence**: G156-1: Rust requires fully-qualified `<X as Trait<T>>::Assoc` for ambiguous projections; Logos matches by erasing the ambiguous bare key.
- **Rule**: When two impls of a generic trait Trait&lt;T&gt; for one target at distinct T each declare the same associated type, the bare projection `X::Assoc` becomes ambiguous and must be written `<X as Trait<T>>::Assoc`; the unsuffixed projection key is first-impl-wins and is erased once a second distinct-args impl appears so a bare lookup fails.
- **Source**: `src/compiler/sema_collect.cpp#L3235-L3248`, `src/compiler/sema_collect.cpp#L3281-L3295`

## Logos-specific additions

Untagged divergence notes whose text marks a Logos-only capability (Writ fabric, zones, metaprog/reflection, tag-dispatch, variadics, self-relative pointers, …). Registered *by kind* under the blanket **A6** addition row of `docs/DIVERGENCES.md` (kind (c): "Logos has it, Rust doesn't"); no per-item row exists or is required. 202 rule(s).

### `borrow.conflict.tpb-reservation-shared-read` — A mut reservation passed as a call argument tolerates concurrent shared reads of the same target
- **Divergence**: Logos-specific carve-out with no Rust equivalent: Rust's borrow checker rejects a mutable borrow of a place that overlaps a concurrent shared borrow of the same place (E0502-style); Logos permits this specific pattern for TPB call-argument reservations.
- **Rule**: A two-phase-borrow (TPB) mut-reservation does not conflict with a concurrent shared (&) borrow/read of the same target: when one of a conflicting pair is a TPB reservation and the other is non-mut, no conflict is reported. A reservation still conflicts with any other mut borrow or reservation of the same target.
- **Source**: `src/compiler/region_infer.cpp#L914-L919`

### `borrow.scoped.rc-arc-root-exempt` — Self-borrowing method results on Rc/Arc roots do not hold a receiver borrow
- **Divergence**: Logos-specific exemption for Rc/Arc receivers (residency-escape / interior-mutability pattern).
- **Rule**: When a self-borrowing method's bare-VarRef receiver roots at an Rc or Arc value, no scoped receiver borrow is recorded: shared-ownership handles are the blessed interior-mutability domain, so `h.array()` followed by `hold(&mut h, root)` is permitted.
- **Source**: `src/compiler/borrow_check.cpp#L2168-L2207`

### `coerce.anyval.let-binds-i32` — AnyVal-typed let binds an i32
- **Divergence**: AnyVal itself is a Logos addition with no Rust equivalent (not a tracked DIVERGENCES.md tag).
- **Rule**: `let name: AnyVal = expr;` numerically coerces expr's value to a 32-bit integer before storing; the binding's storage is a single i32-sized scalar slot.
- **Source**: `src/compiler/mlir_gen_stmt.cpp#L1465-L1474`

### `coerce.cast.int-null-to-trait-object` — Integer (null) cast to trait object yields zeroed fat pair
- **Divergence**: Logos uniform-fat model: `*mut dyn`/`&dyn` are both 16-byte {data,vtable}; integer-to-dyn null cast is a Logos extension for null sentinels (no Rust analog).
- **Rule**: `E as T` where T is a trait object (`*mut dyn`/`&dyn`) and E has an integer type (IntLit/i32/u32/i64/u64/isize/usize) produces a 16-byte {data,vtable} fat pair with both halves null. This makes null-handle sentinels (`0 as *mut dyn`) and `… as *mut u64 == 0` null checks behave under the uniform-fat dyn model.
- **Source**: `src/compiler/mlir_gen_expr.cpp#L3236-L3253`

### `coerce.writ-anyval.scalar-helpers` — Implicit coercion of comprehension element to AnyVal
- **Divergence**: Logos-specific Writ value model.
- **Rule**: Inside a Writ comprehension element/value, the value is coerced to AnyVal: WAny and legacy AnyVal struct values pass through unchanged; bool/i8/i16/i32/IntLit/u8/u16/u32 are wrapped via the matching `writ_coerce_<ty>` helper; `str` (`&[u8]`) is wrapped via `writ_coerce_str` (taking `&ctr` first). Any other type is rejected with a message to cast explicitly or wrap with AnyVal::embed_*.
- **Source**: `src/compiler/sema_expr.cpp#L11382-L11458`

### `coerce.writ-anyval.wide-int-no-implicit` — Wide integers not implicitly coerced to AnyVal
- **Divergence**: Logos-specific anti-truncation rule.
- **Rule**: i64/u64/i24/u24/i56/u56/i128/u128 are intentionally NOT auto-coerced to AnyVal (implicit i32 embedding would silently truncate); the user must cast explicitly (`x as i32`) or wrap with WAny::from.
- **Source**: `src/compiler/sema_expr.cpp#L11418-L11427`, `src/compiler/sema_expr.cpp#L11430-L11436`

### `const.enum.discriminant` — Enum discriminant value forms
- **Divergence**: Cross-enum discriminant reference `OtherEnum::Variant` as a discriminant value has no Rust analog.
- **Rule**: A variant discriminant `Name = D` may be: a bare (optionally negated) integer literal that is the complete value (no trailing binary operator); `metacall <block>`; a cross-enum reference `OtherEnum::Variant` (with optional `as T` cast whose type is dropped, width governed by the enclosing enum's backing/repr); or a general constant expression evaluated via CTFE. A bare literal alt only matches when no binary operator follows; otherwise the value falls through to the const-expr alternative.
- **Source**: `tools/peg_gen/grammars/logos.peg#L788-L812`, `tools/peg_gen/grammars/logos.peg#L760-L763`

### `expr.arr-fill.size-sizeof-pack` — Array fill length via sizeof...(P)
- **Divergence**: Logos variadic-pack feature.
- **Rule**: `[v; sizeof...(P)]` where P is an in-scope type parameter yields a single-element array literal whose length is symbolic (`__sizeof_pack:P`); monomorphization repeats the element to the variadic pack's expanded length. Any spread operator other than `sizeof` is rejected; an undefined P is an error.
- **Source**: `src/compiler/sema_expr.cpp#L11468-L11485`

### `expr.assign.dataref-field-unsafe` — DataRef&lt;ZonedStruct&gt; field write desugars via mut_ptr and needs unsafe
- **Divergence**: Logos-specific: DataRef&lt;T&gt; is a zoned-memory smart pointer with no direct Rust counterpart; unlike Rust's DerefMut (auto-deref without an unsafe requirement), this ergonomic field-write path mandates an enclosing `unsafe` block.
- **Rule**: `p.field = v` where `p: DataRef<Z>` with `Z` a zoned struct desugars to `{ let t = p.mut_ptr(); (*t).field = v; }` (the DerefMut analog); it requires an `unsafe` context, `p` must be a mutable binding, and `v` must be type-compatible with the field type.
- **Source**: `src/compiler/sema_stmt.cpp#L7194-L7235`

### `expr.closure.ref-bind-param` — `|ref x: T|` binds x as &T
- **Divergence**: Logos closure ref-binding param syntax; no direct Rust equivalent.
- **Rule**: A closure parameter written `ref x: T` (IS_REF with an explicit TYPE) takes its argument by value of type T under a synthetic name and binds the user-visible `x` to `&T` aliasing the synthetic param. IS_REF without a TYPE is the `&self`/`&mut self` shorthand, not a ref-bind.
- **Source**: `src/compiler/sema_expr.cpp#L14191-L14206`, `src/compiler/sema_expr.cpp#L14257-L14259`, `src/compiler/sema_expr.cpp#L14304-L14311`

### `expr.comprehension.list-and-map` — List and map comprehensions
- **Divergence**: Logos addition: Python-style comprehensions; not present in Rust.
- **Rule**: List comprehension `[expr for x in iter (if pred)?]` and map comprehension `{kexpr: vexpr for x in iter (if pred)?}` produce a collection by iterating `iter`, binding `x`, optionally filtering by `pred`.
- **Source**: `tools/peg_gen/grammars/logos.peg#L2875-L2885`

### `expr.list-comp.desugar-vec` — List comprehension desugars to Vec build loop
- **Divergence**: Logos-specific surface syntax (Python-style comprehension); not present in Rust.
- **Rule**: A list comprehension `[value for x in iter (if guard)?]` desugars to a block that binds `let mut v: Vec<T> = vec_new::<T>()`, iterates `x` over `iter`, (optionally gated by `guard`) calls `Vec::push(&mut v, value)`, and evaluates to `v`. T is the iterator element type; the block's type is `Vec<T>`.
- **Source**: `src/compiler/sema_expr.cpp#L10885-L10986`

### `expr.list-comp.requires-vec-import` — List comprehension requires Vec in scope
- **Divergence**: Logos-specific: surface sugar depends on a stdlib import being present.
- **Rule**: A list comprehension is ill-formed unless the `Vec` struct and the generic `vec_new` function are visible (via `use logos.mem.collections.vec;`).
- **Source**: `src/compiler/sema_expr.cpp#L10909-L10921`

### `expr.map-comp.desugar-hashmap` — Map comprehension desugars to HashMap build loop
- **Divergence**: Logos-specific surface syntax; not present in Rust.
- **Rule**: A map comprehension `{key: value for x in iter (if guard)?}` desugars to a block that binds `let mut m: HashMap<K,V> = hashmap_new::<K,V>()`, iterates `x` over `iter`, (optionally gated by `guard`) calls `HashMap::insert(&mut m, key, value)`, and evaluates to `m`. K = type of `key`, V = type of `value`; block type is `HashMap<K,V>`.
- **Source**: `src/compiler/sema_expr.cpp#L10992-L11090`

### `expr.map-comp.requires-hashmap-import` — Map comprehension requires HashMap in scope
- **Divergence**: Logos-specific.
- **Rule**: A map comprehension is ill-formed unless the `HashMap` struct and the generic `hashmap_new` function are visible (via `use logos.mem.collections.hashmap;`).
- **Source**: `src/compiler/sema_expr.cpp#L11015-L11026`

### `expr.match.writ-pattern-needs-view` — Writ patterns require a view scrutinee
- **Divergence**: Logos extension: Writ structured-data pattern matching (not in Rust).
- **Rule**: A match arm containing a Writ scalar pattern (PAT_WRIT_NULL/BOOL/INT/STR/MAP/ARR/TYPED_ARR/TYPED_MAP, including inside an or-pattern) requires the scrutinee to be a Writ view (Writ, WritView, or WritStatic; use `&` to borrow); otherwise a diagnostic is emitted.
- **Source**: `src/compiler/sema_stmt.cpp#L8961-L9003`

### `expr.writ-capture.capturable-types` — Set of types capturable in an @-literal
- **Divergence**: Logos addition: @-literal (Writ) capture has no Rust analogue.
- **Rule**: A value may be captured into an @-literal iff its type is one of: integer scalars i8/i16/i32/i64/u8/u16/u32/u64, bool (→ inline AnyVal); f64/f32/FloatLit (→ zone-allocated F64, type_code 31); AnyVal (passthrough) or StringView (→ varchar) struct types; `*const u8`/`*mut u8` (→ C-string varchar); or `str`/`&[u8]` slice of u8 (→ length-bearing varchar). All other types are rejected.
- **Source**: `src/compiler/sema_expr.cpp#L15325-L15350`, `src/compiler/sema_expr.cpp#L15360-L15367`, `src/compiler/sema_expr.cpp#L15387-L15394`

### `expr.writ-list-comp.desugar` — Writ list comprehension desugars to a Writ array builder loop
- **Divergence**: Logos-specific Writ data-substrate sugar; no Rust equivalent.
- **Rule**: A writ list comprehension `@[value for x in iter (if guard)?]` desugars to a block that binds `let mut c = writ_list_comp_new(cap_hint)` (yielding the builder's return type, e.g. Rc&lt;Writ&gt;), iterates `x` over `iter`, coerces `value` to AnyVal, (optionally gated by `guard`) calls `writ_list_comp_push(&c, value)`, and evaluates to `c`. cap_hint = arr_size*8+128 for arrays of known size, else 128.
- **Source**: `src/compiler/sema_expr.cpp#L11098-L11226`

### `expr.writ-list-comp.requires-builder-import` — Writ list comprehension requires comp_builder import
- **Divergence**: Logos-specific.
- **Rule**: A writ list comprehension is ill-formed unless arity-1 `writ_list_comp_new` and arity-2 `writ_list_comp_push` are visible (via `use logos.lang.writ.comp_builder;`).
- **Source**: `src/compiler/sema_expr.cpp#L11125-L11135`

### `expr.writ-lit.value-kinds` — Writ literal value kinds and their encodings
- **Divergence**: Logos addition (Writ SDN literals); no Rust equivalent.
- **Rule**: A Writ SDN literal value is one of: null; bool (0/1); int (see int encoding); float (boxed f64); string; array (homogeneous scalar arrays I8..F64 use a typed array, otherwise an object array); map (integer-keyed I32/U32/I64/U64 use a typed map, otherwise an object map keyed by string); type (a tiny map carrying kind/uid/name); or capture/PARAM (an inline placeholder bound to a value index, substituted at runtime).
- **Source**: `src/compiler/mlir_gen_expr.cpp#L5759-L5882`, `src/compiler/mlir_gen_expr.cpp#L5820-L5882`

### `expr.writ-map-comp.desugar` — Writ map comprehension desugars to a Writ object-map builder loop
- **Divergence**: Logos-specific Writ sugar; no Rust equivalent.
- **Rule**: A writ map comprehension `@{key: value for x in iter (if guard)?}` desugars to a block that binds `let mut c = writ_map_comp_new(cap_hint, slot_hint)`, iterates `x` over `iter`, coerces `value` to AnyVal, (optionally gated by `guard`) calls `writ_map_comp_put(&c, key, value)`, and evaluates to `c`. slot_hint = arr_size (else 64); cap_hint = arr_size*48+256 (else 4096).
- **Source**: `src/compiler/sema_expr.cpp#L11231-L11375`

### `expr.writ-map-comp.key-must-be-str` — Writ map comprehension key must be str
- **Divergence**: Logos-specific (v1 limitation: string keys only).
- **Rule**: In a writ map comprehension v1 the `key` expression must have type `str` (a `&[u8]` slice with u8 element); any other key type is rejected.
- **Source**: `src/compiler/sema_expr.cpp#L11285-L11296`

### `expr.writ-map-comp.requires-builder-import` — Writ map comprehension requires comp_builder import
- **Divergence**: Logos-specific.
- **Rule**: A writ map comprehension is ill-formed unless arity-2 `writ_map_comp_new` and arity-3 `writ_map_comp_put` are visible (via `use logos.lang.writ.comp_builder;`).
- **Source**: `src/compiler/sema_expr.cpp#L11258-L11268`

### `expr.writ.array` — Writ untyped array literal
- **Divergence**: Logos addition (Writ literals).
- **Rule**: An untyped Writ array `@[...]` lowers each element as a recursive Writ value in order.
- **Source**: `src/compiler/sema_expr.cpp#L15131-L15143`

### `expr.writ.bool` — Writ bool literal
- **Divergence**: Logos addition (Writ literals).
- **Rule**: A Writ bool node yields a boolean Writ value; the value is true iff its byte payload is present and nonzero.
- **Source**: `src/compiler/sema_expr.cpp#L15021-L15025`

### `expr.writ.capturable-types` — Types capturable by $-capture into a Writ value
- **Divergence**: Logos addition (Writ captures).
- **Rule**: A captured Logos expression is admissible into a Writ @-literal iff its type is: a scalar integer (i8/i16/i32/i64/u8/u16/u32/u64) or bool (coerced to inline AnyVal); F32/F64/float-literal (zone-allocated F64); AnyVal or a string-view struct; a pointer to u8 (*const u8 / *mut u8, captured as C-string varchar); or a u8 slice (str/&[u8], captured as varchar with length). Other types are not capturable.
- **Source**: `src/compiler/sema_expr.cpp#L15325-L15350`

### `expr.writ.capture-outside-context` — $-capture only inside capturable @-literal
- **Divergence**: Logos addition (Writ captures).
- **Rule**: A $-capture ($ident or $expr) in a Writ value is a compile error unless it occurs inside a capturable @-literal context.
- **Source**: `src/compiler/sema_expr.cpp#L15319-L15323`

### `expr.writ.cfg-slot-type` — WritStatic const-generic slot type
- **Divergence**: Logos-specific const-generic/Writ syntax.
- **Rule**: A slot of a WritStatic-typed const-generic is referenced as `<type:CFG.slot.path>` with dot-separated step names.
- **Source**: `src/compiler/sema_render.cpp#L517-L531`

### `expr.writ.cfg-slot-type-literal` — &lt;type:CFG.path&gt; at writ-value position
- **Divergence**: Logos addition (Writ/CFG type literals).
- **Rule**: `<type:CFG.path>` resolves the config path eagerly and must denote a concrete top-level alias; if it resolves to a const-generic config-slot parameter (kind CfgSlotType) it is rejected with a compile error (parametric Writ literals are not supported).
- **Source**: `src/compiler/sema_expr.cpp#L14982-L15009`

### `expr.writ.embedded-type-lit` — Embedded type in Writ literal
- **Divergence**: Logos-specific Writ syntax.
- **Rule**: A Logos type can be embedded inside a Writ literal as `<type:T>`.
- **Source**: `src/compiler/sema_render.cpp#L510-L516`

### `expr.writ.float-suffix` — Writ float literal: suffix stripping
- **Divergence**: Logos addition (Writ literals).
- **Rule**: A Writ float literal accepts an optional `f32` or `f64` suffix which is stripped before parsing the value as a double-precision float.
- **Source**: `src/compiler/sema_expr.cpp#L15052-L15060`

### `expr.writ.map-entry-colon` — Writ map entry syntax
- **Divergence**: Logos-specific Writ syntax.
- **Rule**: A Writ map literal `@{ ... }` contains comma-separated entries `key: value`; nested scalar values omit the `@` prefix in inner position.
- **Source**: `src/compiler/sema_render.cpp#L479-L497`

### `expr.writ.map-keys` — Writ map literal keys (string or integer)
- **Divergence**: Logos addition (Writ literals).
- **Rule**: An untyped Writ map `@{...}` has entries whose key is either a quoted string (quote-stripped and escape-processed like a Writ string) or an integer; an integer key is negated when the entry carries the negative-key marker. Values are recursively lowered Writ values.
- **Source**: `src/compiler/sema_expr.cpp#L15088-L15129`

### `expr.writ.neg-int` — Writ negative integer literal
- **Divergence**: Logos addition (Writ literals).
- **Rule**: A Writ negative-integer node yields an integer Writ value equal to the negation of the parsed decimal magnitude.
- **Source**: `src/compiler/sema_expr.cpp#L15012-L15016`

### `expr.writ.null` — Writ null literal
- **Divergence**: Logos addition (Writ literals).
- **Rule**: A Writ null node yields the null Writ value.
- **Source**: `src/compiler/sema_expr.cpp#L15018-L15019`

### `expr.writ.outer-at-prefix` — Writ literal outer `@` prefix
- **Divergence**: Logos-specific Writ data-literal syntax; no Rust equivalent.
- **Rule**: Writ (data) literals in expression position are introduced with a leading `@`: `@null`, `@true`/`@false`, `@INT`, `@-INT`, `@FLOAT`, `@"str"`, `@{ ... }` (map), `@[ ... ]` (array).
- **Source**: `src/compiler/sema_render.cpp#L463-L509`

### `expr.writ.sdn-literal` — Writ SDN literals
- **Divergence**: Logos addition: Writ self-describing data-notation literals.
- **Rule**: Writ structured-data literals use the `@` sigil: `@{k:v,…}` map, `@[v,…]` array, `@"s"` string, `@42`/`@-1` int, `@<float>` float, `@true`/`@false` bool, `@null`. Typed forms `@<Elem>[…]` (dense array) and `@<K,V>{…}` / `@<K>{…}` (typed map). Comprehension forms `@[expr for x in iter (if p)?]` and `@{k:v for …}`. Only the outermost literal needs the `@` sigil; inner values are plain.
- **Source**: `tools/peg_gen/grammars/logos.peg#L2887-L2923`

### `expr.writ.string-escapes` — Writ string literal: quote stripping and escapes
- **Divergence**: Logos addition (Writ literals); escape set is a fixed subset.
- **Rule**: A Writ string literal has surrounding double-quotes stripped and recognizes escape sequences \n, \t, \r, \\, \", \0; an unrecognized escape `\x` is kept literally as backslash followed by x.
- **Source**: `src/compiler/sema_expr.cpp#L15062-L15086`

### `expr.writ.type-literal` — Writ type-literal &lt;type:T&gt;
- **Divergence**: Logos addition: Writ first-class type values have no Rust equivalent.
- **Rule**: A Writ value `<type:T>` embeds a Logos type T as a first-class value. T is resolved as a type (primitives, structs, in-scope type-params, and generic instantiations like Vec&lt;u8&gt; all permitted). The value carries (kind, type-uid, canonical-name) where the name is the canonical printed form (e.g. "Vec&lt;u8&gt;") and serves as the value's identity label.
- **Source**: `src/compiler/sema_expr.cpp#L14937-L14979`

### `expr.writ.type-literal-unknown-bare` — Bare type-name in &lt;type:T&gt; must be a known type or in-scope type-param
- **Divergence**: Logos addition (Writ type literals).
- **Rule**: When `<type:T>` names a bare type identifier that is neither a resolvable known type nor an in-scope type-param, it is a compile error; the diagnostic directs the user to declare T as a type-param of the enclosing const (`pub const X<T>: WritStatic = ...`) or use a concrete type.
- **Source**: `src/compiler/sema_expr.cpp#L14954-L14966`

### `expr.writ.typed-array-elem-types` — Typed Writ array element types
- **Divergence**: Logos addition (Writ literals).
- **Rule**: A typed Writ array `@<E>[...]` requires E to be one of I8, U8, I16, U16, I32, U32, I64, U64, F32, F64; any other element type is a compile error.
- **Source**: `src/compiler/sema_expr.cpp#L15145-L15168`

### `expr.writ.typed-array-i32-bounds` — @&lt;I32&gt; array element range check
- **Divergence**: Logos addition (Writ literals).
- **Rule**: Each integer element of an `@<I32>[...]` typed array is bounds-checked at compile time to the i32 range [-2147483648, 2147483647]; out-of-range values are a compile error.
- **Source**: `src/compiler/sema_expr.cpp#L15190-L15203`

### `expr.writ.typed-array-no-captures` — Typed Writ arrays reject $-captures
- **Divergence**: Logos addition (Writ literals/captures).
- **Rule**: Within a typed Writ array `@<E>[...]`, a $-capture element ($ident or $expr) is a compile error because typed arrays store raw element values rather than AnyVal; an untyped `@[...]` literal must be used instead.
- **Source**: `src/compiler/sema_expr.cpp#L15174-L15187`

### `expr.writ.typed-map-key-discipline` — Typed integer-map key discipline
- **Divergence**: Logos addition (Writ literals).
- **Rule**: In a typed integer-keyed Writ map, a string key is a compile error (integer maps require integer keys); integer keys are negated when marked negative, and are bounds/sign-checked per key type: I32 to [-2^31, 2^31-1], U32 to [0, 2^32-1], U64 to non-negative.
- **Source**: `src/compiler/sema_expr.cpp#L15255-L15311`

### `expr.writ.typed-map-types` — Typed Writ map key/value types
- **Divergence**: Logos addition (Writ literals).
- **Rule**: A typed Writ map `@<K>{...}` or `@<K,V>{...}` requires K ∈ {I32, U32, I64, U64, Varchar} and, if V is given, V == AnyVal; any other key or value type is a compile error. Varchar keys produce the same representation as the untyped object map.
- **Source**: `src/compiler/sema_expr.cpp#L15209-L15252`

### `generic.call.antiquot-pack-type-arg` — Type-arg antiquote pack splices a reflected type list
- **Divergence**: Logos metaprog reflection extension (no Rust analogue)
- **Rule**: An antiquote pack `$v...` in a generic call's type arguments splices a runtime-produced list of types (e.g. a struct's field types) into the callee's type args; it is carried as a marker TypeVar `__splicepack$v` that flows like a variadic pack and is expanded during monomorphization by chasing the variable to its type-list producer.
- **Source**: `src/compiler/sema_expr.cpp#L5985-L6000`

### `generic.param.bounds-and-const` — type-parameter and const-parameter forms
- **Divergence**: Variadic type/const parameters (`...`) are a Logos extension.
- **Rule**: A type parameter is `NAME [: bound + bound + ...]`; a const generic parameter is `const NAME : TYPE`. Either may be marked variadic with `...`. Bounds are joined with `+`.
- **Source**: `src/compiler/sema_render.cpp#L1052-L1099`

### `generic.param.variadic-last` — Variadic type parameter must be last
- **Divergence**: Variadic type/const parameters are a Logos addition not present in Rust.
- **Rule**: A variadic type parameter `T...` must be the final entry in the type-parameter list; a non-final variadic param is an error "variadic type parameter must be last".
- **Source**: `src/compiler/sema.cpp#L4188-L4190`

### `generic.spec.method-shadows-impl-param-warn` — Method type-param shadowing an impl-block param becomes an implicit specialization (warned)
- **Divergence**: No Rust analogue: Rust treats the method's type-param as a plain shadowing generic, never as an implicit specialization leg; Logos reinterprets it as a specialization on the impl's param (warned).
- **Rule**: Inside a generic impl block, if a method declares a bare, unbounded type-param whose name is identical to one of the enclosing impl block's type-params, the method is silently classified as a specialization on the impl's param (not a fresh method-level generic); the compiler emits a warning naming the shadowed parameter and the method and recommending a rename if a fresh generic was intended.
- **Source**: `src/compiler/sema_collect.cpp#L4763-L4797`, `src/compiler/sema_collect.cpp#L4551-L4586`

### `grammar.expr.call-metavar` — Metavariable call
- **Divergence**: No Rust analogue; metaprogramming callee splice.
- **Rule**: '#IDENT(args)' and '#(expr)(args)' invoke a callee named by a metavariable (NAME_VAR) or by an evaluated expression, used in metaprogramming-expanded call sites.
- **Source**: `tools/peg_gen/grammars/logos.peg#L3230-L3237`

### `grammar.generic.type-param-forms` — Type parameter forms
- **Divergence**: Logos additions: variadic type/const params ('...'), metavar params ('#'), repeat-group expansion (no Rust equivalent).
- **Rule**: type_param admits: lifetime_param; 'IDENT: lifetime_param (+ lifetime_param)*' (type-outlives); ptr/arr specialisation patterns; const params 'const IDENT: T', 'const IDENT...: T' (variadic), 'const #IDENT: T'; variadic type param 'IDENT... (: bounds)?'; metavar '#IDENT (: bounds)?'; 'IDENT: bounds (= default)?'; 'IDENT = default'; or bare 'IDENT'. A repeat-group '#(type_param), *' expands variadically.
- **Source**: `tools/peg_gen/grammars/logos.peg#L3147-L3181`

### `grammar.metaprog.quote-expr` — quote_expr! macro
- **Divergence**: No Rust analogue.
- **Rule**: quote_expr_expr ::= 'quote_expr' '!' '{' expr '}' ; body is a single expression producing a typed AST (expr-blob) literal.
- **Source**: `tools/peg_gen/grammars/logos.peg#L3060-L3061`

### `grammar.metaprog.quote-item` — quote_item! macro
- **Divergence**: No Rust analogue (Rust uses macro_rules!/proc-macro quote).
- **Rule**: quote_item_expr ::= 'quote_item' '!' '{' item* '}' ; body is zero or more item declarations producing a typed AST (item-blob) literal.
- **Source**: `tools/peg_gen/grammars/logos.peg#L3051-L3052`

### `grammar.metaprog.quote-ty` — quote_ty! macro
- **Divergence**: No Rust analogue.
- **Rule**: quote_ty_expr ::= 'quote_ty' '!' '{' type_ref '}' ; body is a single type expression producing a first-class Type literal (same Type{kind,name,size} shape as type_of::&lt;T&gt;()).
- **Source**: `tools/peg_gen/grammars/logos.peg#L3068-L3069`

### `grammar.writ.capture-placeholders` — Writ runtime capture placeholders
- **Divergence**: No Rust analogue; Writ interpolation.
- **Rule**: Inside a Writ literal, '${' expr '}' captures an arbitrary expression (WRIT_CAP_EXPR) and '$' IDENT captures a named binding (WRIT_CAP_IDENT) as a runtime value.
- **Source**: `tools/peg_gen/grammars/logos.peg#L2949-L2950`

### `grammar.writ.entry-key-kinds` — Writ entry keys
- **Divergence**: No Rust analogue; Writ data-literal grammar.
- **Rule**: writ_entry ::= (STRING | '-' INTEGER | INTEGER) ':' writ_val ; a map key is a quoted string, a negative integer, or a non-negative integer. A '-' INTEGER key carries LO_NEG.
- **Source**: `tools/peg_gen/grammars/logos.peg#L2931-L2936`

### `grammar.writ.nested-at-optional` — Optional @ on nested Writ aggregates
- **Divergence**: No Rust analogue; Writ literal nesting.
- **Rule**: A nested writ_map / writ_array inside a writ_val may optionally be prefixed by '@'; '@'-prefixed and bare forms are equivalent.
- **Source**: `tools/peg_gen/grammars/logos.peg#L2951-L2955`

### `grammar.writ.scalar-values` — Writ scalar values
- **Divergence**: No Rust analogue; Writ scalar literals.
- **Rule**: writ_val scalars: RAW_STRING/STRING -&gt; WRIT_STR; FLOAT -&gt; WRIT_FLOAT; '-' INTEGER -&gt; WRIT_NEG_INT; INTEGER -&gt; WRIT_INT; 'true'/'false' -&gt; WRIT_BOOL; 'null' -&gt; WRIT_NULL.
- **Source**: `tools/peg_gen/grammars/logos.peg#L2956-L2963`

### `grammar.writ.type-literal` — Writ embedded type literal `<type: T>`
- **Divergence**: No Rust analogue; type-as-value embedding.
- **Rule**: A Writ value may embed a Logos Type as a first-class value via `'<' 'type' ':' simple_type '>'`, producing a WRIT_TYPE_LIT node carrying the rendered type T. Any simple_type is accepted, including generic instantiations (e.g. Vec&lt;u8&gt;, Result&lt;T,E&gt;); it renders back as `<type: T>`.
- **Source**: `tools/peg_gen/grammars/logos.peg#L2941-L2948`, `src/compiler/sema_render.cpp#L1526-L1531`

### `grammar.writ.type-slot-path` — Writ CFG type-slot
- **Divergence**: No Rust analogue; Writ embedded-type slot.
- **Rule**: writ_val may be '&lt;' 'type' ':' IDENT path_step+ '&gt;' producing a CFG_SLOT_TYPE (slot extraction keeping an IDENT-only head followed by path steps).
- **Source**: `tools/peg_gen/grammars/logos.peg#L2945-L2946`

### `intrinsic.args-count-of.arg-count` — args_count_of yields generic-arg count
- **Divergence**: Logos addition.
- **Rule**: `args_count_of::<T>()` requires one type argument and yields `i64` = number of T's generic type arguments (0 for primitive or non-generic struct).
- **Source**: `src/compiler/sema_expr.cpp#L5213-L5233`

### `intrinsic.args-of.type-arg-array` — args_of yields generic type arguments
- **Divergence**: Logos addition.
- **Rule**: `args_of::<T>()` requires one type argument and yields `[Type; N]` listing T's generic type arguments; for non-generic T the result is `[Type; 0]`. The array length is fixed at mono once T is concrete.
- **Source**: `src/compiler/sema_expr.cpp#L5185-L5211`

### `intrinsic.bits.u64-bit-ops` — u64 bitwise intrinsics
- **Divergence**: Logos addition: explicit free-function bit-op intrinsics.
- **Rule**: `popcount_u64`, `leading_zeros_u64`, `trailing_zeros_u64` each take 1 u64 argument and return u32; `bswap_u64`, `bitreverse_u64` each take 1 u64 argument and return u64. Wrong arity is an error. (Lower to the corresponding LLVM intrinsics; ctlz/cttz are non-poison at zero.)
- **Source**: `src/compiler/sema_expr.cpp#L3186-L3204`

### `intrinsic.dst-from-raw-parts.unsafe` — dst_from_raw_parts requires unsafe and a custom-DST struct
- **Divergence**: Logos addition (custom-DST construction intrinsic).
- **Rule**: `dst_from_raw_parts::<S>(ptr, len)` (and `_mut`) requires unsafe context, exactly one type argument S that is a (Zoned)Struct whose last field resolves to `[T]` or `dyn Trait` (directly is_dst or via type-parameter substitution), and exactly two value arguments.
- **Source**: `src/compiler/sema_expr.cpp#L4740-L4802`

### `intrinsic.dyn-from-parts.fat-trait-ptr` — dyn_from_parts builds a trait object from raw halves
- **Divergence**: Logos addition.
- **Rule**: `dyn_from_parts::<Trait>(data: *mut u8, vtable: *const u8) -> *mut dyn Trait` forms a fat {data, vtable} trait-object pointer. Exactly one trait type argument (its own type-args, if any, are carried so the produced object matches a parameterized `dyn Trait<...>` annotation, skipping lifetime/auto-trait bound sub-nodes) and exactly two value arguments are required. Trait must be a known, object-safe trait. The result is the bare canonical TraitObject (matching `*mut dyn`/`&dyn`), not a thin pointer.
- **Source**: `src/compiler/sema_expr.cpp#L5314-L5391`

### `intrinsic.field-count-of.struct-field-count` — field_count_of yields struct field count
- **Divergence**: Logos addition.
- **Rule**: `field_count_of::<T>()` requires one type argument and yields `i64` = number of declared fields of struct T (0 for non-struct or unknown-struct T).
- **Source**: `src/compiler/sema_expr.cpp#L5562-L5582`

### `intrinsic.field-reflect.types-and-names` — field_types_of / field_names_of reflect struct fields
- **Divergence**: Logos addition.
- **Rule**: `field_types_of::<T>()` yields `[Type; N]` of T's field types and `field_names_of::<T>()` yields `[&[u8]; N]` of T's field names; each requires one type argument; non-struct T yields empty arrays. At mono field types are substituted via the SubstMap built from the struct template's type_params -&gt; T.type_args().
- **Source**: `src/compiler/sema_expr.cpp#L5584-L5613`

### `intrinsic.generic-of.signature` — generic_of requires a bare struct/enum name
- **Divergence**: Logos addition (compile-time reflection intrinsic).
- **Rule**: `generic_of::<X>()` requires its single type-argument to be a bare named struct or enum (a TYPE_REF or GENERIC_INST with a NAME); the name must resolve to a declared struct or enum in the current program, otherwise a compile error.
- **Source**: `src/compiler/sema_expr.cpp#L4517-L4551`

### `intrinsic.generic-of.unapplied-ctor` — generic_of yields a handle for an unapplied generic constructor
- **Divergence**: Logos addition.
- **Rule**: `generic_of::<X>()` yields a Type-shaped value-handle for the unapplied generic constructor X (struct or enum) with kind=Generic, name=X, size=arity, and UID = FNV-1a of "generic:X".
- **Source**: `src/compiler/sema_expr.cpp#L5615-L5619`

### `intrinsic.get-annotation.option-result` — get_annotation yields the annotation instance as Option&lt;A&gt;
- **Divergence**: Logos addition.
- **Rule**: `get_annotation::<T, A>() -> Option<A>` const-folds to `Some(A{...})` if datatype T carries annotation A, else `None`.
- **Source**: `src/compiler/sema_expr.cpp#L5825-L5827`

### `intrinsic.get-annotation.signature` — get_annotation arity and annotation-type constraint
- **Divergence**: Logos addition (compile-time annotation reflection intrinsic).
- **Rule**: `get_annotation::<T, A>()` requires exactly two type arguments; A must be a ZonedStruct that is an annotation type. `Option` must be in scope. Result type is `Option<A>`.
- **Source**: `src/compiler/sema_expr.cpp#L4901-L4938`

### `intrinsic.has-annotation.const-fold` — has_annotation is a compile-time annotation check
- **Divergence**: Logos addition (annotation metaprogramming).
- **Rule**: `has_annotation::<T, A>()` requires exactly two type arguments and const-folds to `bool`: true iff datatype T carries a user annotation of annotation-type A. A must be a known annotation datatype (else compile error); the check matches against T's declared annotation instances by fully-qualified or simple name.
- **Source**: `src/compiler/sema_expr.cpp#L5786-L5823`

### `intrinsic.has-trait-of.signature` — has_trait_of arity and shape
- **Divergence**: Logos addition (reflection intrinsic); no Rust equivalent.
- **Rule**: `has_trait_of::<Trait>(t)` requires exactly one trait type-argument (a single named type in the turbofish) and exactly one value argument; violating either is a compile error. It evaluates to `bool`.
- **Source**: `src/compiler/sema_expr.cpp#L4367-L4410`

### `intrinsic.has-trait-of.type-method` — has_trait_of is the Type-method form of has_trait
- **Divergence**: Logos addition.
- **Rule**: `has_trait_of::<Trait>(t: Type) -> bool` recovers concrete T from the value t's Type.uid field and runs the same impl-table recursion as has_trait.
- **Source**: `src/compiler/sema_expr.cpp#L5272-L5276`

### `intrinsic.has-trait.t-trait-bool` — has_trait queries impl tables
- **Divergence**: Logos addition.
- **Rule**: `has_trait::<T, Trait>()` requires two type arguments and yields `bool`: whether concrete T implements Trait, resolved at mono against the same impl tables (concrete + recursive blanket lookup) that drive method dispatch. The second argument is read by its identifier name only (passed as a string literal arg), not resolved as a type. Missing T or empty Trait name is a compile error.
- **Source**: `src/compiler/sema_expr.cpp#L5235-L5270`

### `intrinsic.is-data-plain-of.copyable-predicate` — is_data_plain_of predicates DataPlain layout
- **Divergence**: Logos addition (zoned/Writ datatypes).
- **Rule**: `is_data_plain_of::<T>()` yields `bool`: true iff T is a DataPlain datatype (no relative-pointer fields). Array wrappers are stripped ([D; N] checks D). Non-datatype types (scalars, ordinary structs) always yield true; a generic (type-arg-bearing) zoned datatype yields false (conservative); an unknown datatype defaults to true.
- **Source**: `src/compiler/sema_expr.cpp#L5739-L5779`

### `intrinsic.is-kind.predicate-family` — Type-kind predicate family
- **Divergence**: Logos addition.
- **Rule**: The predicates is_ptr / is_ref / is_mut_ref / is_struct / is_zoned / is_enum / is_tuple / is_slice / is_array / is_integer / is_signed / is_unsigned / is_float / is_bool / is_primitive each take exactly one type argument and yield `bool`, resolved against the substituted T at mono. Wrong arity is a compile error.
- **Source**: `src/compiler/sema_expr.cpp#L5127-L5140`

### `intrinsic.is-same.two-type-args` — is_same arity and result
- **Divergence**: Logos addition.
- **Rule**: `is_same::<T1, T2>()` requires exactly two type arguments and yields `bool`; structural/identity equality of T1 and T2 is resolved post-substitution at mono. Wrong arity is a compile error.
- **Source**: `src/compiler/sema_expr.cpp#L5018-L5026`

### `intrinsic.metaprog.reify-type` — reify_type round-trips a Type value at mono time
- **Divergence**: Logos addition: type-reflection metaprogramming intrinsic.
- **Rule**: `reify_type(t: Type) -> Type` takes exactly 1 argument and lowers to the `__reify_type__` mono intercept, which substitutes the argument and re-emits a fresh `Type` struct literal from its uid. Wrong arity is an error.
- **Source**: `src/compiler/sema_expr.cpp#L3139-L3154`

### `intrinsic.metaprog.type-apply` — type_apply / apply_generic instantiate a type-level template
- **Divergence**: Logos addition: type-level composition metaprogramming intrinsics.
- **Rule**: `type_apply(name: &[u8], args: [Type; N]) -> Type` and `apply_generic(g: Type, args: [Type; N]) -> Type` each take exactly 2 arguments and lower to the `__type_apply__` / `__apply_generic__` mono intercepts, which recover concrete TypeRefs from each element and emit a fresh `Type` struct literal for `Name<T0,...>`. Wrong arity is an error.
- **Source**: `src/compiler/sema_expr.cpp#L3156-L3184`

### `intrinsic.reflect.deferred-fold-after-subst` — Type-introspection intrinsics fold after substitution at mono
- **Divergence**: Logos addition: compile-time type reflection intrinsics (no Rust equivalent).
- **Rule**: Type-trait/type-introspection intrinsics taking type-args are not evaluated at sema; each lowers to a magic `__<name>__` call carrying its type-args, and is folded to a concrete value only after monomorphization substitutes those type-args. Inside a generic body where T is still a type variable the call is preserved (never frozen to 'TypeVar' semantics).
- **Source**: `src/compiler/sema_expr.cpp#L5014-L5017`, `src/compiler/sema_expr.cpp#L5079-L5087`, `src/compiler/sema_expr.cpp#L5142-L5146`

### `intrinsic.reflect.typeinfo-rodata` — reflect requests TypeInfo rodata
- **Divergence**: Logos addition.
- **Rule**: `reflect::<T>() -> WritStatic` is a compile-time request that registers T for reflection so a TypeInfo global is emitted; the expression resolves to the address of that emitted TypeInfo rodata.
- **Source**: `src/compiler/sema_expr.cpp#L5781-L5784`

### `intrinsic.reflect.writ-trait` — reflect on a writ trait registers a reflect request
- **Divergence**: Logos addition (Writ reflection intrinsic).
- **Rule**: `reflect::<Tr>()` where Tr names a writ trait (is_writ) registers a reflect request for `pkg::Tr` and evaluates to a `WritStatic` reflection of that trait/datatype.
- **Source**: `src/compiler/sema_expr.cpp#L4851-L4876`

### `intrinsic.slice-from-raw.ptr-len` — slice_from_raw builds a slice fat pointer
- **Divergence**: Logos addition (unsafe raw-parts constructor).
- **Rule**: `slice_from_raw::<T>(ptr: *const T, len: i64) -> &[T]` requires exactly one type argument and exactly two value arguments; it materialises a slice fat-pointer of element type T (uniform fat-pointer layout shared with str_from_raw). Wrong type-arg count or value-arg count is a compile error.
- **Source**: `src/compiler/sema_expr.cpp#L5032-L5057`

### `intrinsic.str.str-from-raw` — str_from_raw constructs a str fat pointer
- **Divergence**: Logos addition: no Rust equivalent free function.
- **Rule**: `str_from_raw(ptr: *const u8, len: i64) -> str` is a compiler intrinsic taking exactly 2 arguments; it yields a value of type `&[u8]`/str fat-pointer. Wrong arity is an error.
- **Source**: `src/compiler/sema_expr.cpp#L3117-L3127`

### `intrinsic.template-of.decl-handle` — template_of yields a Template handle to a declaration
- **Divergence**: Logos addition.
- **Rule**: `template_of::<X>()` resolves X at sema, locates the declaration item named X in the current AST root, and yields a `Template { raw: AnyVal { raw: <offset> } }` baking that declaration's arena offset as a u32 literal (same-AST scope).
- **Source**: `src/compiler/sema_expr.cpp#L5621-L5627`

### `intrinsic.template-of.signature` — template_of requires a top-level item name in the current file
- **Divergence**: Logos addition (metaprogramming intrinsic).
- **Rule**: `template_of::<X>()` requires its single type-argument to be a bare named item; X must name a top-level declaration in the current source file, otherwise a compile error. It also requires `use logos.std.compiler.metaprog` (the `template_of_at` shim) to be in scope.
- **Source**: `src/compiler/sema_expr.cpp#L4576-L4632`

### `intrinsic.tuple-all-eq.chain-expand` — tuple_all_eq expands an element-wise eq chain
- **Divergence**: Logos addition (variadic-tuple support).
- **Rule**: `tuple_all_eq::<T>(a, b)` expands to the conjunction `a.0.eq(&b.0) && ... && a.{N-1}.eq(&b.{N-1})`. If T is a concrete tuple the chain is expanded at sema; if any element is a type variable a `__tuple_all_eq__` placeholder is emitted and expanded at mono once T's arity is concrete.
- **Source**: `src/compiler/sema_expr.cpp#L5459-L5471`

### `intrinsic.tuple-all-eq.signature` — tuple_all_eq arity and tuple constraint
- **Divergence**: Logos addition (variadic-tuple support intrinsic).
- **Rule**: `tuple_all_eq::<T>(a, b)` requires exactly one type argument T which must be a tuple type, and exactly two value arguments; otherwise a compile error. Result type is `bool`. An empty tuple yields the constant `true`.
- **Source**: `src/compiler/sema_expr.cpp#L4413-L4451`

### `intrinsic.tuple-count-of.elem-count` — tuple_count_of yields tuple element count
- **Divergence**: Logos addition.
- **Rule**: `tuple_count_of::<T>()` requires one type argument and yields `i64` = number of elements in tuple T (0 for non-tuple T).
- **Source**: `src/compiler/sema_expr.cpp#L5516-L5534`

### `intrinsic.tuple-each-field-debug.requires-tuple` — tuple_each_field_debug formats every tuple field
- **Divergence**: Logos addition.
- **Rule**: `tuple_each_field_debug::<T>(self, f)` requires one type argument that MUST be a tuple type (else compile error) and exactly two value arguments; result type is the enclosing function's return type. It Debug-formats every field of T into Formatter f, deferring to a `__tuple_each_field_debug__` placeholder expanded at mono.
- **Source**: `src/compiler/sema_expr.cpp#L5473-L5514`

### `intrinsic.tuple-elems-of.elem-types` — tuple_elems_of yields tuple element types
- **Divergence**: Logos addition.
- **Rule**: `tuple_elems_of::<T>()` requires one type argument and yields `[Type; N]` of T's element types; empty array for non-tuple T.
- **Source**: `src/compiler/sema_expr.cpp#L5536-L5560`

### `intrinsic.type-code-of.signature` — type_code_of arity and result type
- **Divergence**: Logos addition (Writ/zoned reflection intrinsic).
- **Rule**: `type_code_of::<T>()` requires exactly one type argument and evaluates to a `u64` type code.
- **Source**: `src/compiler/sema_expr.cpp#L4634-L4647`, `src/compiler/sema_expr.cpp#L4712`

### `intrinsic.type-code-of.writ-code` — type_code_of yields the Writ type code
- **Divergence**: Logos addition (Writ substrate).
- **Rule**: `type_code_of::<T>()` yields `u64`, the Writ type_code of a concrete datatype = SHA-256 of "package::Name" truncated to 56 bits, shifted to &gt;= 128 if needed (codes 1-127 reserved for inline AnyVal). For non-datatype T it yields 0.
- **Source**: `src/compiler/sema_expr.cpp#L5733-L5737`

### `intrinsic.type-hash.structural-u64` — type_hash is layout-structural
- **Divergence**: Logos addition.
- **Rule**: `type_hash::<T>()` requires one type argument and yields `u64`: a structural FNV-1a-64 hash of T's layout — primitives map to fixed codes; struct/tuple/array/ptr hash a tag plus the recursive hashes of constituents, with NO struct/field names. Two structurally identical layouts hash equal; generic instances hash through their substituted args (Foo&lt;i32&gt; != Foo&lt;u32&gt;).
- **Source**: `src/compiler/sema_expr.cpp#L5073-L5087`

### `intrinsic.type-of.type-struct` — type_of constructs a Type reflection struct
- **Divergence**: Logos addition (type reflection).
- **Rule**: `type_of::<T>()` requires exactly one type argument and yields a `Type` struct literal with fields {kind: u32 (from __type_kind_of__), name: &[u8] (from __type_name_of__), size: i64 (size_of T), align: i64 (align_of T), uid: u64 (type_uid of T)}. Each component is concretized at mono.
- **Source**: `src/compiler/sema_expr.cpp#L5142-L5183`

### `intrinsic.type-refs-of.pack-array` — type_refs_of reflects a type pack
- **Divergence**: Logos addition.
- **Rule**: `type_refs_of::<T...>()` yields `[Type; N]` with one Type value per pack member, substituted after pack expansion at mono. When the pack reduces to a single type-variable pack, the placeholder array carries a pack-size marker so let-bound/return types lift to the concrete `[Type; N]` automatically.
- **Source**: `src/compiler/sema_expr.cpp#L5670-L5701`

### `intrinsic.type-uid-hi.high-half` — type_uid_hi is the high half of the 128-bit UID
- **Divergence**: Logos addition.
- **Rule**: `type_uid_hi::<T>()` requires one type argument and yields `u64`, the HIGH 64 bits of the 128-bit nominal type UID; together with type_uid (low half) they form a 128-bit TypeId.
- **Source**: `src/compiler/sema_expr.cpp#L5103-L5115`

### `intrinsic.type-uid.nominal-u64` — type_uid is nominal identity
- **Divergence**: Logos addition.
- **Rule**: `type_uid::<T>()` requires one type argument and yields `u64`: a NOMINAL 64-bit type identity (hash of the canonical named type string), so distinct nominal types differ even at identical layout (unlike type_hash). It is the low 64 bits of the 128-bit type UID and equals the `.uid` field exposed by type_of.
- **Source**: `src/compiler/sema_expr.cpp#L5088-L5102`, `src/compiler/sema_expr.cpp#L5172-L5174`

### `intrinsic.typelist.probe-family` — typelist O(1) probes over a type pack
- **Divergence**: Logos addition.
- **Rule**: Over L's type-pack (L.type_args()), one type argument required: `typelist_len::<L>() -> i64`; `typelist_head::<L>() -> Type` (error if pack empty); `typelist_nth::<L>(i) -> Type` requiring exactly one i64 index arg (out-of-range = error); `typelist_tail::<L>() -> [Type; N-1]`. Substituted after L is concrete.
- **Source**: `src/compiler/sema_expr.cpp#L5393-L5457`

### `intrinsic.variant-reflect.enum-family` — Enum-variant decompose intrinsics
- **Divergence**: Logos addition.
- **Rule**: Each requires one type argument E: `variant_count_of::<E>() -> i64`; `variant_names_of::<E>() -> [&[u8]; N]`; `variant_payload_counts_of::<E>() -> [i64; N]`; `variant_payload_types_flat_of::<E>() -> [Type; M]`. For non-enum or unknown E all yield 0 / empty arrays.
- **Source**: `src/compiler/sema_expr.cpp#L5629-L5668`

### `intrinsic.vtable-of.static-vtable-addr` — vtable_of yields a static vtable address
- **Divergence**: Logos addition.
- **Rule**: `vtable_of::<Trait, T>() -> *const u8` yields the address of the static vtable for `impl Trait for T`. Trait is read by NAME (must be a known trait, else error); T is resolved as a type and substituted at mono. Missing trait name or type is a compile error; an unknown trait name is a compile error.
- **Source**: `src/compiler/sema_expr.cpp#L5278-L5312`

### `intrinsic.wstatic-hash-of.u64` — wstatic_hash_of identity hash
- **Divergence**: Logos addition.
- **Rule**: `wstatic_hash_of::<CFG>()` requires exactly one type argument and yields `u64`, the byte-hash identity of a WritStatic value; folded at mono once CFG is a concrete WStaticLit.
- **Source**: `src/compiler/sema_expr.cpp#L5064-L5072`

### `intrinsic.zone-mut-ref.unsafe` — zone_mut_ref signature and unsafe requirement
- **Divergence**: Logos addition (zoned-reference construction intrinsic).
- **Rule**: `zone_mut_ref::<T>(ptr, zone)` requires unsafe context, exactly one type argument T, and exactly two value arguments.
- **Source**: `src/compiler/sema_expr.cpp#L4820-L4843`

### `intrinsic.zone.zone-of` — zone_of recovers the Writ zone pointer of a fat &mut T
- **Divergence**: Logos addition: Writ/zone memory model intrinsic.
- **Rule**: `zone_of(r: &mut T) -> *mut u8` takes exactly 1 argument and yields the metadata half of the fat reference reinterpreted as a `*mut u8` (dual of zone_mut_ref). Wrong arity is an error.
- **Source**: `src/compiler/sema_expr.cpp#L3129-L3137`

### `item.attr.datatype-promotion` — #[datatype]/#[annotation] promote a struct into the datatype pipeline
- **Divergence**: Logos addition: datatype/annotation/zoned attributes (no Rust equivalent).
- **Rule**: A struct-syntax item annotated `#[datatype]` or `#[annotation]` is treated as a datatype declaration; `#[zoned]` marks self-relative fields and does NOT promote a struct to a datatype.
- **Source**: `src/compiler/sema_collect.cpp#L367-L373`, `src/compiler/sema_collect.cpp#L374`, `src/compiler/sema_collect.cpp#L435`

### `item.attr.struct-enum-flag-set` — Struct/enum attribute flag vocabulary
- **Divergence**: Logos-specific memory/zone attribute set; no Rust analogue.
- **Rule**: The recognised struct/enum modifier attributes are exactly: `datatype`, `annotation`, `zoned`, `zone_mut`, `rel_ptr`, `self_describing`, `pinned`, `borrow_carrying`, `no_auto_drop`, `non_null`. A struct bearing `#[datatype]` or `#[annotation]` is promoted to the datatype pipeline.
- **Source**: `src/compiler/sema_impl.hpp#L1430-L1460`

### `item.const.def` — Module-level constant definition
- **Divergence**: `let` accepted as a const keyword at module level; generic `const NAME<...>` factory has no direct Rust analog.
- **Rule**: A module constant is `[pub] (const|let) NAME [<params>] : T = expr ;`. The `const` keyword admits an optional type-parameter list, making the RHS a generic compile-time factory substituted at each use site; `let` stays non-generic. Both forms require an explicit type annotation and an initializer.
- **Source**: `tools/peg_gen/grammars/logos.peg#L688-L699`

### `item.const.generic-and-typed` — const item with optional generics and type
- **Divergence**: Generic const items (const with type parameters) are a Logos extension.
- **Rule**: A const item is `[pub] const NAME [<type-params>] [: TYPE] = VALUE ;`; const items may be generic.
- **Source**: `src/compiler/sema_render.cpp#L1192-L1211`

### `item.dup.odr-dedup` — structurally identical duplicate items dedup; differing ones error
- **Divergence**: Logos addition: ODR dedup of metacall-emitted items (Rust has no metacall splice model).
- **Rule**: Two item definitions (struct/union/schema/datatype/enum) sharing the same name in the same package are an error UNLESS their AST sub-trees are structurally equal, in which case the duplicate is silently dropped (ODR-style dedup). Structural equality recurses through TinyObjectMap-by-bitmap-key, Array-by-index and WritString-by-content, ignores SRC_LINE metadata (so identical items emitted by metaprogramming at different source positions still dedup), and treats any other value-kind pair as conservatively unequal.
- **Source**: `src/compiler/sema_collect.cpp#L25-L76`, `src/compiler/sema_collect.cpp#L267-L282`, `src/compiler/sema_collect.cpp#L374-L467`

### `item.enum.variant-shapes` — Enum variant shapes
- **Divergence**: Variadic-tuple variant `Name(...T)` has no Rust analog.
- **Rule**: A variant is one of: unit `Name`; tuple `Name(T, ...)`; variadic-tuple `Name(...T)`; struct-shape `Name { f: T, ... }` (fields may be `pub`); empty struct-shape `Name {}`; or a discriminant-bearing `Name = <disc>`. Variant lists allow leading doc-comments per variant and a trailing comma.
- **Source**: `tools/peg_gen/grammars/logos.peg#L753-L786`, `tools/peg_gen/grammars/logos.peg#L757-L775`

### `item.fn-param.datanode-by-value` — DataNode eidos cannot be passed by value
- **Divergence**: Logos addition (zoned/DataNode model); no Rust analog
- **Rule**: A parameter whose type is (or is an array of) a DataNode datatype (one holding relative-pointer fields) is rejected by value at signature-collection time; the relative pointers require a zone base pointer unavailable in that position — use `DataRef<T>` instead.
- **Source**: `src/compiler/sema_decl.cpp#L700-L713`

### `layout.anyval.scalar-i32` — AnyVal is a scalar i32, never an aggregate
- **Divergence**: Logos-specific built-in type; no Rust equivalent (addition).
- **Rule**: The built-in type `AnyVal` is represented as a bare i32 scalar value at every place a value of that type occurs (local bindings, receivers, struct fields) — never as an LLVM aggregate/struct value or a pointer-to-aggregate, and never spilled to a by-value aggregate slot the way a struct receiver would be.
- **Source**: `src/compiler/mlir_gen.cpp#L744-L757`, `src/compiler/mlir_gen.cpp#L866-L868`, `src/compiler/mlir_gen.cpp#L889-L904`

### `layout.dst.self-describing-ref-is-thin` — Ref to a #[self_describing] DST is a thin 8-byte pointer
- **Divergence**: Logos custom-DST extension (#[self_describing]); no Rust equivalent.
- **Rule**: A reference to a `#[self_describing]` DST is physically thin (8-byte pointer straight to the header); the tail length is recovered in-band from the pointee header rather than carried alongside the pointer.
- **Source**: `src/compiler/mlir_gen_impl.hpp#L976-L980`

### `layout.enum.niche-lowbit-ptr-int` — Low-bit niche packs pointer + small-int arms
- **Divergence**: Logos low-bit pointer-tagging niche; no direct Rust analog.
- **Rule**: A two single-field-arm enum where one arm is a pointer to an align&gt;=2 pointee (low bit always 0) and the other arm is a &lt;=56-bit integer stored shifted `(v<<1)|1` packs into one word; the discriminant is the low bit (0=ptr arm, 1=int arm).
- **Source**: `src/compiler/mlir_gen_types.cpp#L796-L853`

### `layout.enum.niche-zoned-raw-word` — Zoned (#[zoned2]) raw 64-bit low-bit niche
- **Divergence**: Logos zoned (Writ) niche; no Rust equivalent.
- **Rule**: In a `#[zoned2]` enum, the low-bit niche additionally accepts a raw `*T` pointer arm (trusting the zone allocator's &gt;=2 alignment even for `*u8`) and a raw 64-bit `u64`/`i64` value arm stored without a `<<1` shift (the producer bakes the low-bit-1 tag into the word).
- **Source**: `src/compiler/mlir_gen_types.cpp#L811-L851`

### `layout.field.rel-ptr-self-relative-offset` — #[rel_ptr] field stores a self-relative i64 offset
- **Divergence**: Logos addition: self-relative pointer field representation (no Rust analogue).
- **Rule**: A struct field marked #[rel_ptr] (RefRepr RelOffset) does not store an absolute pointer; on assignment the destination pointer value is lowered to a signed i64 offset relative to the field slot's own address (the slot is the anchor) and that offset is stored in the slot.
- **Source**: `src/compiler/mlir_gen_stmt.cpp#L2748-L2758`, `src/compiler/mlir_gen_stmt.cpp#L2828-L2838`

### `layout.ref.rel-offset-eight-bytes` — Relative-offset reference layout
- **Divergence**: Logos self-relative pointers (zoned/Writ); no Rust equivalent.
- **Rule**: A relative-offset (self-relative) reference is stored as a single i64 offset word: {size=8, align=8}.
- **Source**: `src/compiler/mlir_gen_types.cpp#L622`, `src/compiler/mlir_gen_types.cpp#L637`

### `layout.ref.self-relative-offset` — Self-relative (writ / rel_ptr) pointers store a byte offset
- **Divergence**: Logos addition: self-relative zoned pointers, no Rust analogue.
- **Rule**: A self-relative pointer (the writ / `#[rel_ptr]` zoned pointer) is stored as an i64 byte offset from its own storage slot's address; materialization = slot_address + load_i64(slot); lowering a target pointer stores (target_address − slot_address). A plain thin-pointer struct field is upgraded to this self-relative storage, even without an explicit `#[rel_ptr]` tag, when its owning struct is `#[zoned2]` (the untagged zoned-reference case).
- **Source**: `src/compiler/mlir_gen_impl.hpp#L884-L887`, `src/compiler/mlir_gen_impl.hpp#L890-L895`

### `layout.ref.zone-mut-fat-pair` — &mut T to a zone_mut type carries its allocator as a fat reference
- **Divergence**: Logos addition: zone/allocator-carrying mutable reference, no Rust analogue.
- **Rule**: A &mut T where T is a `#[zone_mut]` type has a 16-byte {data, zone=*mut Allocator} fat representation, returned by value like a slice fat pair; the allocator rides the &mut so grow-style methods reach it from &mut self.
- **Source**: `src/compiler/mlir_gen_impl.hpp#L881-L883`

### `layout.zone-mut-ref.fat-data-zone` — &mut T of a #[zone_mut] type is a fat {data, zone} pair
- **Divergence**: Logos-specific zone/Writ memory-model addition; no Rust equivalent.
- **Rule**: A `&mut T` reference to a `#[zone_mut]` (FatZoneMut) type is represented as a two-word fat pointer pair `{data, zone}`. Field/method access on the referent resolves through the `data` half of the pair (peeled off before descent); every other (thin) reference kind is unaffected (identity).
- **Source**: `src/compiler/mlir_gen.cpp#L668-L681`

### `lex.keyword.reserved-set` — Reserved keyword set
- **Divergence**: Adds Logos-specific keywords absent in Rust: quote_item/quote_expr/quote_ty/template/package/instantiate/eidos/genos/auto/metacall/tagged/new/typeof/offset_of/null; lacks Rust keywords (mod, pub(crate), crate, self, Self, fn-async forms, etc.) handled elsewhere.
- **Rule**: The following are reserved keywords matched as distinct tokens and unavailable as ordinary identifiers: continue, quote_item, quote_expr, quote_ty, template, package, instantiate, eidos, genos, auto, metacall, static, return, extern, struct, union, match, while, break, false, trait, const, type, impl, enum, loop, else, true, for, use, mut, let, dyn, tagged, pub, new, fn, if, in, as, where, unsafe, move, typeof, offset_of, ref, null, async, await.
- **Source**: `tools/peg_gen/grammars/logos.peg#L328-L380`

### `lex.writ.integer-literal` — Writ integer literal with radix and suffix
- **Divergence**: Data-language lexer (Writ), not Logos source; C-style suffixes ull/ul/ll/u and '_s32'-style signed suffix differ from Rust integer-literal suffixes.
- **Rule**: A Writ INTEGER is an optional leading '-' followed by a hex (0x/0X), binary (0b/0B), octal (0o/0O), or decimal magnitude, with an optional suffix: '_(u|s)(8|16|32|64)' (sized) or C-style 'ull'|'ul'|'ll'|'u'. Regex: /[-]?(0[xX][0-9a-fA-F]+|0[bB][01]+|0[oO][0-7]+|[0-9]+)(_(u|s)(8|16|32|64)|ull|ul|ll|u)?/.
- **Source**: `tools/peg_gen/grammars/writ.peg#L66`

### `metaprog.antiquot.capture-forms` — Writ antiquotation capture syntax
- **Divergence**: Logos metaprogramming antiquotation; no Rust equivalent.
- **Rule**: Within a quoted/Writ literal, an antiquotation captures a value either by identifier `$name` or by expression block `${expr}`.
- **Source**: `src/compiler/sema_render.cpp#L532-L537`

### `metaprog.quote-item.blob-result-type` — quote_item! evaluates to a QuoteItemBlob value
- **Divergence**: Logos metaprogramming addition.
- **Rule**: `quote_item!` evaluates to a `QuoteItemBlob` struct value with fields { template_ptr, template_size, idents_blob, blobs_blob, cursors_blob }, where template_ptr/template_size address the serialized synthetic-module blob and the *_blob fields carry the packed antiquot substitution data (null when the corresponding placeholder kind has zero occurrences).
- **Source**: `src/compiler/sema_expr.cpp#L16133-L16144`, `src/compiler/sema_expr.cpp#L15907-L15910`

### `metaprog.quote-item.cursor-repetition-packing` — Cursor (`#(...)*`) antiquots carry a per-site nesting depth
- **Divergence**: Logos metaprogramming addition.
- **Rule**: Each repetition-cursor antiquot site contributes a `*const u8` (the address of a Vec cursor variable) plus a parallel per-site depth byte: depth 1 = Vec&lt;Ident&gt;, depth 2 = Vec&lt;Vec&lt;Ident&gt;&gt; (nested `#(...)*`). The element type is the neutral `*const u8`; pack reads each cursor according to its depth. When there are no cursor sites, cursors_blob is null.
- **Source**: `src/compiler/sema_expr.cpp#L16056-L16127`, `src/compiler/sema_expr.cpp#L15939-L15944`

### `metaprog.quote-item.exprblob-antiquot-packing` — ExprBlob antiquots are packed by their .ptr field
- **Divergence**: Logos metaprogramming addition.
- **Rule**: Each `#(expr)` antiquot whose lowered expression has type ExprBlob contributes one `*const u8` (the ExprBlob's `ptr` field) to the blobs blob, in DFS placeholder order; the lowered ExprBlob is bound to a local that outlives the array. When there are no ExprBlob sites, blobs_blob is null.
- **Source**: `src/compiler/sema_expr.cpp#L16005-L16054`

### `metaprog.quote-item.ident-antiquot-packing` — `#name`/`#(expr)` Ident antiquots are packed as Ident pointers
- **Divergence**: Logos metaprogramming addition.
- **Rule**: Each scalar Ident antiquot site (`#name` shortcut or `#(expr)` yielding Ident) contributes one `*const Ident` to the idents blob, in DFS placeholder order; a `#(expr)` form binds the lowered expression to a fresh local whose address is taken. When there are no Ident sites, idents_blob is null.
- **Source**: `src/compiler/sema_expr.cpp#L15953-L16003`

### `metaprog.quote-item.inherit-import-scope` — quote_item! inherits the metafn's import scope
- **Divergence**: Logos metaprogramming addition; controls hygiene/name resolution of quoted items.
- **Rule**: The synthetic module inherits the enclosing metafn's wildcard `use` packages, plus a self-use of the metafn's own package (if non-empty), so that unqualified names inside the quoted items resolve through the metafn's `use`-list. Each inherited package becomes one USE node carrying the full dotted package name in NAME.
- **Source**: `src/compiler/sema_expr.cpp#L15821-L15857`

### `metaprog.quote-item.name-antiquot-forms` — quote_item! accepts #name and #(expr) name antiquotations
- **Divergence**: Logos metaprogramming construct; no Rust equivalent.
- **Rule**: Within `quote_item! { ... }`, a NAME_VAR placeholder accepts two forms: `#name` (shortcut) looks the variable up in the metafn scope and requires type Ident; `#(expr)` lowers the inner expression in the metafn scope and requires type Ident or ExprBlob. Any other pointee kind is an error.
- **Source**: `src/compiler/sema_expr.cpp#L15569-L15625`

### `metaprog.quote-item.placeholder-walk-balance` — Source and destination placeholder counts must match
- **Divergence**: Logos metaprogramming addition.
- **Rule**: The number of antiquot placeholders discovered while scanning the source items must equal the number of placeholder slots rewritten in the cloned destination tree; a mismatch is a compile error.
- **Source**: `src/compiler/sema_expr.cpp#L15797-L15802`

### `metaprog.quote-item.synthetic-main-module` — quote_item! produces a synthetic `package main` module
- **Divergence**: Logos metaprogramming addition (no Rust equivalent).
- **Rule**: `quote_item! { item* }` constructs a synthetic AST module whose root is MODULE with NAME="main", empty PATH_PARTS, ITEMS = the deep-cloned quoted items, and SRC_LINE=1. The result is emitted as a serialized WritStatic blob carried by a `QuoteItemBlob` value.
- **Source**: `src/compiler/sema_expr.cpp#L15859-L15894`, `src/compiler/sema_expr.cpp#L15805-L15819`

### `metaprog.template.decl` — Template declaration
- **Divergence**: No Rust equivalent.
- **Rule**: `template <item>` wraps a struct/enum/datatype/trait/impl/fn declaration as inert data (an AST blob) rather than a real binding; the inner names are never registered, so referencing the template as a type yields an unknown-type diagnostic. Templates are consumed by metafunctions via apply/metacall.
- **Source**: `tools/peg_gen/grammars/logos.peg#L604-L612`

### `module.abi.one-directional-minor-compat` — Binary archive ABI compatibility is one-directional within a major version
- **Divergence**: Logos addition: semantic-version ABI gate on binary modules (Rust has no stable cross-version library ABI).
- **Rule**: A compiler may consume a binary library iff (a) the library's language major version equals the compiler's, and (b) for stable releases the library's minor version is &lt;= the compiler's minor. A differing major is incompatible; a library built by a newer minor is rejected. An ABI-incompatible archive is not indexed (its packages become unavailable). Identical version strings are always compatible; legacy archives without a version stamp are not enforced.
- **Source**: `src/compiler/module_loader.cpp#L1100-L1144`, `src/compiler/module_loader.cpp#L1182-L1184`

### `module.abi.prerelease-no-guarantee` — Pre-release / snapshot builds require exact version match
- **Divergence**: Logos addition.
- **Rule**: If either the library or the compiler is a pre-release (`-pre`) or snapshot (`+meta`) build, no ABI guarantee holds: only an exact version-string match is silently accepted; any mismatch is permitted but warned. The check is disabled entirely by environment override.
- **Source**: `src/compiler/module_loader.cpp#L1100-L1113`, `src/compiler/module_loader.cpp#L1130-L1142`, `src/compiler/module_loader.cpp#L1110-L1110`

### `module.import.from-pins-module` — `use pkg from <M>` pins resolution to module M's archive
- **Divergence**: Logos addition (`from <module>` import selector); no Rust equivalent.
- **Rule**: An import `use pkg from <M>;` resolves `pkg` from the archive whose embedded module canonical-name is `M`, independent of which other archive(s) also provide a package named `pkg`. This lets two distinct modules supplying a same-named package coexist; a bare `use pkg;` and `use pkg from M;` are keyed independently and both load.
- **Source**: `src/compiler/module_loader.cpp#L1594-L1611`, `src/compiler/module_loader.cpp#L1280-L1282`

### `module.prelude.cross-cutting-auto-load` — Cross-cutting foundation packages auto-load without explicit `use`
- **Divergence**: Logos addition: implicit prelude is prefix-scoped to the lang tier (transitional; manifest-tier system intended).
- **Rule**: Foundation packages under prefixes `std.lang`, `std.writ`, or `logos.lang` (excluding the `logos.lang.writ` substrate) are implicitly available to every compilation: when an archive is loaded for a requested package, sibling packages with these prefixes are also loaded so cross-cutting traits and types (Default, Ord, Send, Clone, etc.) resolve without an explicit import edge.
- **Source**: `src/compiler/module_loader.cpp#L1397-L1432`, `src/compiler/module_loader.cpp#L1566-L1571`

### `module.tagdispatch.binary-archive-provides-tables` — Fully-binary tag systems are provided by the archive
- **Divergence**: Module/separate-compilation model; no direct Rust analogue.
- **Rule**: A tag system whose every registered callee is already present in a linked binary archive is not re-defined; the consuming unit emits only external references to that system's tables, lookup function, and initializer. Tables also present in an archive use weak (deduplicating) linkage rather than triggering a duplicate-definition error.
- **Source**: `src/compiler/mlir_gen_dyn.cpp#L219-L292`, `src/compiler/mlir_gen_dyn.cpp#L358-L368`, `src/compiler/mlir_gen_dyn.cpp#L394-L396`

### `module.use.from-clause-syntax` — `use pkg from <module>` clause: operand syntax and extraction
- **Divergence**: Logos-specific: type/package coexistence across modules sharing a package name (no Rust analog).
- **Rule**: The optional `from <module>` clause of a `use` names the providing module as its operand. The operand may be a bare identifier or a double-quoted string literal; surrounding quotes are stripped to yield the module name. Absence of the `from` clause records an empty module name, selecting default resolution. The clause is recorded as a (package dotted-path, from-module) pair.
- **Source**: `src/compiler/module_loader.cpp#L115-L133`, `src/compiler/module_loader.cpp#L206-L207`

### `module.use.from-module` — use with explicit source module
- **Divergence**: `use ... from <module>` clause has no Rust analog.
- **Rule**: `[pub] use pkg('.'IDENT)* IDENT use_module ';'` imports `pkg.path` from a named module; the trailing bare IDENT is the contextual `from` keyword and `use_module` is the source (a bare name or a quoted string for hyphenated ids, with quotes stripped). The from-bearing alternative is tried before the plain form.
- **Source**: `tools/peg_gen/grammars/logos.peg#L498-L521`

### `module.use.from-module-disambiguation` — `use pkg from <module>` restricts candidate visibility to the named module's id
- **Divergence**: Logos-specific: type/package coexistence across modules sharing a package name (no Rust analog).
- **Rule**: A `use pkg from <module>;` import makes a candidate for that package visible only if the candidate's owning-module id equals the named module's id; a plain `use pkg;` accepts a candidate from any module. The `<module>` name is resolved via the canonical module-name→id table; if that table is absent/empty or the name is unknown, the `from` clause is unresolvable.
- **Source**: `src/compiler/sema_impl.hpp#L1093-L1110`

### `module.use.from-module-restriction` — `use pkg from "module"` restricts the import to a specific module id
- **Divergence**: note — part of Logos's C++-style module-linkage system; no direct Rust equivalent.
- **Rule**: A use of the form `use pkg from "module";` resolves the quoted module name to a module id and restricts the imported package's symbol resolution to that module's exports; the restriction is in force during lowering, not only collection.
- **Source**: `src/compiler/sema.cpp#L6882-L6905`

### `module.use.from-module-restricts-candidates` — use pkg from module restricts candidates
- **Divergence**: Logos addition: per-import module qualification (no Rust equivalent).
- **Rule**: `use pkg from <module>;` restricts the candidates of `pkg` to the named module. The `from` keyword is contextual (matched as a bare identifier, so `From::from` stays valid); a missing/incorrect `from` keyword, a missing module name, or a module name matching no loaded module is an error — unless the module-name-to-id map isn't primed yet (e.g. an early metaprog discovery pass), in which case the restriction is silently skipped for that pass.
- **Source**: `src/compiler/sema_collect.cpp#L192-L225`

### `mono.dispatch.self-generic-template-mangle` — Self-typed method-generic call mangles resolved template directly
- **Divergence**: Logos-specific monomorphization fixup
- **Rule**: A trait-default body call to another method-generic method on a `Self`-typed generic-struct receiver, where the resolved symbol is a method-generic template (contains `__g__`) with method-level type-params and all call type-args are concrete, is resolved by mangling the resolved template with the call's type-args and enqueuing it; impl-only-generic methods (no method-level type-params) are excluded from this path.
- **Source**: `src/compiler/mono_clone.cpp#L4147-L4203`

### `mono.instantiate.decl` — Explicit instantiation root-pin
- **Divergence**: No Rust equivalent; analog of C++ `template class Foo<int>;`.
- **Rule**: `[pub] instantiate <type_ref> ;` materializes the named generic instance as a monomorphization root: all its inherent and trait methods become roots, transitively pulling everything they call. `pub instantiate` additionally marks the instance as part of the package's public API surface.
- **Source**: `tools/peg_gen/grammars/logos.peg#L591-L595`

### `mono.intrinsic.args-count-of` — args-count-of yields the number of generic type arguments of T
- **Divergence**: Logos reflection extension.
- **Rule**: `__args_count_of__` yields an i64 lit_int equal to the count of the concrete substituted first type-arg's own type_args (0 for a non-generic/primitive T, or when type_args is empty).
- **Source**: `src/compiler/mono_clone.cpp#L1530-L1543`

### `mono.intrinsic.field-types-of-nonstruct-empty` — field-types-of on a non-struct yields an empty type pack
- **Divergence**: Logos reflection extension.
- **Rule**: `__field_types_of__` applied to a substituted T that is not Struct/ZonedStruct yields an empty pack (matching the declared [Type;0] result for non-struct T) rather than aborting, so that mono monomorphizing both arms of a runtime `is_struct()`-guarded branch still succeeds on the non-struct arm (an empty pack resolves variadic instantiation to the 0-arg base overload). For a struct T, fields are located by name (preferring a pkg-matching struct template, else any struct of that name) and their types substituted via the template's type-params mapped onto T's type-args.
- **Source**: `src/compiler/mono_clone.cpp#L1342-L1380`

### `mono.intrinsic.has-trait` — has-trait resolves a trait implementation at monomorphization time
- **Divergence**: Logos reflection extension (compile-time trait-satisfaction predicate).
- **Rule**: `__has_trait__` yields lit_bool: it reads the trait name from the call's original lit_str argument and the concrete type from type_args[0], reduces T to a concrete name (concrete_struct_name for Struct/ZonedStruct, enum_name for Enum, else type_str(T), with any trailing `$G...` instantiation-marker suffix stripped), and recursively tests trait satisfaction against concrete_impls_ + blanket_impls_ (mono_has_impl_recursive), which the trait engine populates lazily.
- **Source**: `src/compiler/mono_clone.cpp#L1544-L1584`

### `mono.intrinsic.has-trait-of` — has-trait-of recovers T from a reflected Type value then resolves the trait
- **Divergence**: Logos reflection extension (Type-method form).
- **Rule**: `__has_trait_of__(trait, t: Type)` recovers the concrete T from t's StructLit `uid` field (itself produced by a `__type_uid_of__` call), chasing VarRef aliases up to 8 hops via type_let_inits_ to locate the StructLit, and then performs the same impl-table recursion as __has_trait__ to yield lit_bool.
- **Source**: `src/compiler/mono_clone.cpp#L1585-L1616`

### `mono.intrinsic.is-same` — is-same compares two substituted types for equality
- **Divergence**: Logos reflection extension.
- **Rule**: `__is_same__` yields lit_bool(true) iff exactly two type-args are given and they are equal (TypeRef ==) after substitution; otherwise false.
- **Source**: `src/compiler/mono_clone.cpp#L1490-L1494`

### `mono.intrinsic.type-hash-of` — type-hash-of yields a structural layout-stable hash
- **Divergence**: Logos reflection extension.
- **Rule**: `__type_hash_of__` is replaced by lit_int equal to compute_type_hash(T), a structural FNV-1a-64 hash of the substituted T that bears no struct/field names and recurses into field types (layout-stable identity).
- **Source**: `src/compiler/mono_clone.cpp#L1438-L1449`

### `mono.intrinsic.type-kind-of` — type-kind-of yields the kind discriminant of substituted T
- **Divergence**: Logos reflection extension.
- **Rule**: `__type_kind_of__` is replaced by lit_int equal to the LogosType::Kind discriminant of the concrete substituted first type-arg (0 if type_args is empty).
- **Source**: `src/compiler/mono_clone.cpp#L1419-L1427`

### `mono.intrinsic.type-kind-predicates` — Type-trait predicates evaluate on the substituted type's kind
- **Divergence**: Logos reflection extension.
- **Rule**: Each predicate yields lit_bool computed from the Kind of the concrete substituted first type-arg (Kind::Error if type_args is empty): __is_ptr__/__is_ref__/__is_mut_ref__/__is_struct__/__is_zoned__/__is_enum__/__is_tuple__/__is_slice__/__is_array__/__is_bool__ test an exact Kind match; __is_integer__ is true for {I8,I16,I24,I32,I56,I64,I128,U8,U16,U24,U32,U56,U64,U128}; __is_float__ for {F32,F64}; __is_signed__ for the I* subset; __is_unsigned__ for the U* subset; __is_primitive__ for Bool | floating | integer.
- **Source**: `src/compiler/mono_clone.cpp#L1482-L1529`

### `mono.intrinsic.type-name-of` — type-name-of yields the canonical type string of T
- **Divergence**: Logos reflection extension.
- **Rule**: `__type_name_of__` is replaced by lit_str equal to the canonical `type_str(T)` of the concrete substituted first type-arg (empty string if type_args is empty).
- **Source**: `src/compiler/mono_clone.cpp#L1450-L1457`

### `mono.intrinsic.type-uid-of` — type-uid-of yields the two 64-bit halves of the nominal type UID
- **Divergence**: Logos reflection extension.
- **Rule**: `__type_uid_of__` is replaced by lit_int equal to the low 64 bits of the nominal TypeUID = type_hash_64bit(type_hash_23(type_id_canon(T))) for concrete substituted T (0 if type_args is empty or T is null); the uid -&gt; T mapping is recorded (uid_to_type_) for later reification (e.g. quote_ty!). `__type_uid_hi_of__` yields the high 64 bits, type_hash_hi64(type_hash_23(type_id_canon(T))), computed from the same canonical-name hash input so the two halves agree.
- **Source**: `src/compiler/mono_clone.cpp#L1458-L1481`

### `mono.intrinsic.wstatic-hash-of` — wstatic-hash-of yields the byte-hash of CFG
- **Divergence**: Logos compile-time-static (Writ) extension.
- **Rule**: `__wstatic_hash_of__` is replaced by lit_int equal to the const_val (u64 byte-hash) carried by the substituted first type-arg (a WStaticLit kind); 0 if type_args is empty or const_val is absent.
- **Source**: `src/compiler/mono_clone.cpp#L1428-L1437`

### `mono.mangle.owning-vs-borrowed-dyn` — Owning Box&lt;dyn T&gt; mangles distinctly from borrowed &dyn T
- **Divergence**: Internal mangling distinction with no Rust analog; reflects owning-dyn vs borrowed-dyn repr split.
- **Rule**: An OWNING trait object (Box&lt;dyn T&gt;) mangles to 'owndyn_&lt;trait-name&gt;' followed by a per-type-arg suffix (one suffix per type argument), while a borrowed &dyn T keeps the plain type-string mangling. This keeps generic specs such as Vec&lt;Box&lt;dyn T&gt;&gt; and Vec&lt;&dyn T&gt; DISTINCT, so the owning bit is not collapsed onto the borrow form (which would skip element drop and leak).
- **Source**: `src/compiler/mono_impl.hpp#L740-L751`, `src/compiler/sema.cpp#L1500-L1512`

### `mono.subst.cfg-slot-type-projection` — CFG-slot type projection from Writ config
- **Divergence**: Logos-specific compile-time Writ-config-driven type projection; no Rust analogue.
- **Rule**: A CfgSlotType `<type:CFG.path>` resolves CFG (a const-generic param or inlined Writ static literal) to a WStaticLit, walks the encoded path (string-keyed 'F', int-keyed 'I' map fields and 'A' array indices joined by 0x1F) through the Writ value, and yields the type named at the terminal Type node (primitive, struct, or enum); if CFG is not yet concrete or any step misses, the projection stays deferred (unchanged).
- **Source**: `src/compiler/mono_subst.cpp#L432-L535`

### `mono.subst.drop-args-non-generic-impl` — Type-args dropped/truncated to template type-param count
- **Divergence**: Logos-specific (T9-tr-02) plus variadic-pack handling
- **Rule**: A method call's type-arguments are kept only up to the resolved template's declared type-parameter count: if the template has zero type-params (concrete impl) all type-args are cleared; otherwise they are truncated to the param count, EXCEPT a variadic template type-param consumes all trailing type-args (no truncation).
- **Source**: `src/compiler/mono_clone.cpp#L3962-L4001`, `src/compiler/mono_clone.cpp#L4025-L4027`

### `mono.subst.self-describing-dst-thin-ptr` — Raw pointer to self-describing DST stays thin
- **Divergence**: Logos-specific Writ/RefRepr self-describing-DST contract; no Rust analogue.
- **Rule**: When the pointee struct is `#[self_describing]` (recovers its tail length from an in-band prefix field), a raw `*const Self`/`*mut Self` (kind Ptr) stays a thin 8-byte pointer and is NOT canonicalized to fat DstRef; `&Self`/`&mut Self` still take the fat representation.
- **Source**: `src/compiler/mono_subst.cpp#L164-L174`

### `mono.subst.splicepack-producer-fold` — `$fs...` splice-pack folds reflected [Type] producers into call type-args
- **Divergence**: Logos reflection/metaprogramming extension; no Rust equivalent.
- **Rule**: A call type-arg encoded as a marker TypeVar named `__splicepack$<v>` is resolved by chasing `v` (up to 8 VarRef alias hops via type_let_inits_) to a producer Call, then folding that producer's element types directly into the enclosing call's type_args in place of the marker: `__type_refs_of__` -&gt; all its (substituted) type-args as-is; `__args_of__` -&gt; the type-args of its first type-arg's type; `__typelist_tail__` -&gt; that type's type-args minus the first; `__tuple_elems_of__` -&gt; that type's tuple elements (type must be Tuple); `__field_types_of__` -&gt; the field types of that (struct) type, substituted through a SubstMap built from the struct template's type-params against the concrete type's type-args. An unrecognized producer callee aborts compilation.
- **Source**: `src/compiler/mono_clone.cpp#L1287-L1393`

### `mono.subst.variadic-tuple-splice` — Variadic-tuple pack expansion
- **Divergence**: Variadic tuples are a Logos addition not present in Rust.
- **Rule**: A single-element tuple whose sole element is a pack type-var `(A...)` splices in the elements of the concrete tuple A maps to during substitution, yielding the full concrete tuple.
- **Source**: `src/compiler/sema.cpp#L4639-L4659`

### `pat.tuple.str-element-via-guard` — String-literal tuple element lowered to str_eq guard
- **Divergence**: Logos addition: tuple-arm codegen lacks a native str_eq dispatch, so string elements are desugared to guards.
- **Rule**: A string-literal element of a tuple pattern binds the element to a synthesized name and adds a refutable `str_eq(synth, lit)` guard, rather than a value-equality test (a raw `==` would pointer-compare). Requires the refutable-guard context to be active.
- **Source**: `src/compiler/sema_stmt.cpp#L4552-L4567`, `src/compiler/sema_stmt.cpp#L4600-L4617`

### `pat.writ.array-len-and-rest` — Writ array pattern length and rest
- **Divergence**: Logos addition.
- **Rule**: A Writ array pattern `@[p0, p1, ...]` matches iff the scrutinee is an array of exactly the listed element count and each element matches its sub-pattern. A trailing `..` rest changes the length check to &gt;= (count of non-rest elements) and binds no further elements. `..` is permitted only as the LAST element; otherwise a compile error.
- **Source**: `src/compiler/sema_stmt.cpp#L5525-L5562`

### `pat.writ.container` — Writ map/array patterns
- **Divergence**: Logos addition: Writ container patterns.
- **Rule**: `@{ key: pat, ... }` / `@{}` match writ maps; `@[ elem, ... ]` / `@[]` match writ arrays. Array elements admit a trailing `..` to match length ≥ n; map keys are string literals.
- **Source**: `tools/peg_gen/grammars/logos.peg#L2028-L2041`, `tools/peg_gen/grammars/logos.peg#L2113-L2120`

### `pat.writ.map-shape` — Writ map pattern
- **Divergence**: Logos addition.
- **Rule**: A Writ map pattern `@{k: p, ...}` matches iff the scrutinee is a map AND, for each listed entry key k, the key is present and its slot value matches sub-pattern p (conjunction over all entries). An entry without a value sub-pattern requires only presence of the key. Map patterns are non-exhaustive: keys not listed are ignored.
- **Source**: `src/compiler/sema_stmt.cpp#L5495-L5524`

### `pat.writ.match-only` — Writ scalar patterns only in match arms
- **Divergence**: Logos extension (Writ value patterns); no Rust equivalent.
- **Rule**: Writ scalar patterns (`@null`, `@true`, `@false`, `@<int>`, `@"str"`, `@{...}`, `@[...]`, and typed array/map forms) are permitted only in `match` arms, not in if-let / while-let / let-bindings / nested pattern positions; elsewhere is an error. In a match arm they lower to a wildcard plus a synthesized guard.
- **Source**: `src/compiler/sema_stmt.cpp#L5086-L5104`

### `pat.writ.or-no-mixing` — Or-patterns may not mix Writ and non-Writ alternatives
- **Divergence**: Logos addition.
- **Rule**: In an or-pattern, if any alternative is a Writ pattern then all alternatives must be Writ patterns; mixing Writ patterns with non-Writ patterns is a compile error. An all-Writ or-pattern matches iff any alternative matches (disjunction).
- **Source**: `src/compiler/sema_stmt.cpp#L5641-L5664`

### `pat.writ.scalar` — Writ scalar patterns
- **Divergence**: Logos addition: Writ data-substrate patterns.
- **Rule**: `@null`, `@true`/`@false`, `@N`/`@-N`, and `@"str"` are writ scalar patterns matching writ null, bool, integer, and string values respectively.
- **Source**: `tools/peg_gen/grammars/logos.peg#L2092-L2106`

### `pat.writ.scalar-leaves` — Writ scalar leaf patterns
- **Divergence**: Writ pattern matching is a Logos addition (no Rust equivalent).
- **Rule**: Within a Writ value pattern (@{...}/@[...]), the scalar leaves are: null (`@null`), bool (`@true`/`@false`), integer (`@<int>`), and string (`@"..."`). Each tests the corresponding AnyVal scrutinee: null-ness, boolean equality, integer equality, and string equality respectively.
- **Source**: `src/compiler/sema_stmt.cpp#L5293-L5334`, `src/compiler/sema_stmt.cpp#L5484-L5486`

### `pat.writ.typed-array-element-types` — Typed Writ array pattern element types
- **Divergence**: Logos addition.
- **Rule**: A typed Writ array pattern `@<T>[..]` matches iff the scrutinee has the array type-code for element type T. T must be one of {I8,U8,I16,U16,I32,U32,I64,U64,F32,F64,AnyVal}; any other element type is a compile error.
- **Source**: `src/compiler/sema_stmt.cpp#L5563-L5588`

### `pat.writ.typed-container` — Writ typed map/array patterns
- **Divergence**: Logos addition: Writ typed-container patterns.
- **Rule**: `@<T>{..}`, `@<T,R>{..}`, and `@<T>[..]` are typed writ map and array patterns annotating the matched container's element type(s).
- **Source**: `tools/peg_gen/grammars/logos.peg#L2107-L2112`

### `pat.writ.typed-map-key-value-types` — Typed Writ map pattern key/value types
- **Divergence**: Logos addition.
- **Rule**: A typed Writ map pattern `@<K[,V]>{..}` matches iff the scrutinee has the map type-code for key type K. K must be one of {Varchar,I32,U32,I64,U64}; the value type V, if given, must be AnyVal. Any other key or value type is a compile error.
- **Source**: `src/compiler/sema_stmt.cpp#L5589-L5616`

### `pat.writ.wildcard-binding` — Named wildcard inside Writ pattern binds the AnyVal
- **Divergence**: Logos addition.
- **Rule**: A wildcard with a non-`_` name inside a Writ pattern binds that name to the current AnyVal sub-value and always matches; a `_` (or empty) name binds nothing.
- **Source**: `src/compiler/sema_stmt.cpp#L5487-L5494`

### `region.borrow-carrying.escape-tracked` — #[borrow_carrying] values are escape-tracked like references
- **Divergence**: Logos addition (no Rust equivalent)
- **Rule**: A value of a `#[borrow_carrying]` struct or enum holds a borrow into an arena and is escape-tracked like a reference; returning it escapes the borrow as if returning the bare reference. Borrow-carrying-ness propagates transitively: a struct with an inline field, or an enum with a variant payload, of a (transitively) borrow-carrying type is itself borrow-carrying, as is a container whose generic type-argument is borrow-carrying (e.g. Vec&lt;WAny&gt;).
- **Source**: `src/compiler/borrow_check.cpp#L52-L54`, `src/compiler/borrow_check.cpp#L137-L164`, `src/compiler/borrow_check.cpp#L204-L227`

### `region.borrow-carrying.residency-holder-exempt` — Residency-holder packages are exempt from borrow-carrying
- **Divergence**: Logos addition (no Rust equivalent)
- **Rule**: A struct with an Rc/Arc field (a residency-holder / laundered-escape package such as Held&lt;T&gt;/HeldAny) ref-counts the arena alive independent of any local, so it is NOT borrow-carrying and may safely escape — even via its type-arguments. An explicit `#[borrow_carrying]` annotation overrides this auto-exemption.
- **Source**: `src/compiler/borrow_check.cpp#L55-L60`, `src/compiler/borrow_check.cpp#L165-L203`, `src/compiler/borrow_check.cpp#L207-L209`

### `region.escape.borrow-carrying-type` — Borrow-carrying types and transitive containers are escape-tracked
- **Divergence**: Logos-only extension: #[borrow_carrying] generalizes escape/lifetime tracking to non-reference (arena-view) types; Rust has no analogous whole-value lifetime annotation.
- **Rule**: is_borrow_carrying_type(t): a named struct/zoned-struct/niche-enum type is borrow-carrying iff registered in ts_.borrow_carrying, UNLESS its name is registered in ts_.residency_exempt (laundered escape wrappers Held&lt;T&gt;/HeldAny, holding an Rc/Arc that keeps the arena alive, are NEVER borrow-carrying — even through their type-args, e.g. Held&lt;WArray&lt;WAny&gt;&gt;). A generic container whose element type-arg is itself borrow-carrying (Vec&lt;WAny&gt;, Option&lt;WAny&gt;, Box&lt;WAny&gt;) is transitively borrow-carrying. A raw pointer to such a type (*mut WAny) has no type-args on the pointer itself and is NOT checked.
- **Source**: `src/compiler/borrow_check.cpp#L1634-L1659`

### `trait.impl.target-fnptr-erased` — impl for fn-pointer covers all fn-ptrs of an arity
- **Divergence**: Logos additive behavior: fn-ptr impls are arity-keyed and non-generic due to fn-ptr type erasure (no per-signature monomorphization).
- **Rule**: `impl<A,B,C> Trait for fn(A,B)->C` is permitted; because fn-pointers are type-erased to a uniform pointer at the Logos ABI, the impl covers every fn-pointer of the given arity and its methods are collected non-generically (one shared codegen, keyed by arity).
- **Source**: `src/compiler/sema_collect.cpp#L2928-L2934`, `src/compiler/sema_collect.cpp#L2963-L2967`

### `trait.impl.variadic-pack-param` — Variadic trait type-param pack absorbs trailing impl params
- **Divergence**: Fn-family variadic type packs are a Logos extension (no stable Rust equivalent).
- **Rule**: If a trait declares a variadic type parameter `A...` used at a method parameter position, an impl may expose any number of concrete parameters from that position onward; each post-pack impl parameter type must equal the corresponding trait-instantiation type-arg (trait_type_args[k - pack_pos]), and the count of post-pack impl params must equal the number of pack instantiation args.
- **Source**: `src/compiler/sema_collect.cpp#L3408-L3492`

### `trait.tagdispatch.dispatch-table-layout` — Tag-dispatch table maps per-type type_code to fn_ptr per (tag_system, trait, method)
- **Divergence**: Logos addition: tag-dispatch dispatch model has no Rust analogue (Rust uses fat-pointer vtables only).
- **Rule**: Tag-dispatch (an alternative to vtable-based dyn dispatch) is backed by a per-(tag_system, trait, method) dispatch table mapping a per-concrete-type integer `type_code` (logically u64) to the implementing method `fn_ptr`. A `type_code == 0` entry is the unset sentinel meaning 'no impl registered' and is skipped at table build / treated as no-impl at lookup.
- **Source**: `src/compiler/mlir_gen_dyn.cpp#L181-L186`, `src/compiler/mlir_gen_dyn.cpp#L302-L321`

### `trait.tagdispatch.registration-uniqueness` — At most one impl per (tag_system, trait, type_code)
- **Divergence**: Logos addition (tag-dispatch); analogous to Rust's coherence/orphan-style uniqueness but enforced at link time.
- **Rule**: Each (tag_system, trait, type_code) registration is unique program-wide: registering the same triple from two separately-compiled units is a hard error (detected as a multiply-defined link symbol). Multiple methods of one trait for one type share a single registration (deduplicated per triple, not per method).
- **Source**: `src/compiler/mlir_gen_dyn.cpp#L188-L214`, `src/compiler/mlir_gen_dyn.cpp#L324-L355`

### `trait.tagdispatch.registry-lookup-api` — Per-triple public dispatch-lookup function
- **Divergence**: Logos addition: runtime trait-method registry by type_code has no Rust analogue.
- **Rule**: For each (tag_system, trait, method) triple with at least one tier, a public lookup function `type_code -> fn_ptr` is exposed, checking tier-1 (with an in-range guard against the 256 bound) and falling back to tier-2, returning null when no table has the entry. This enables reflective / deferred invocation of trait methods by type_code.
- **Source**: `src/compiler/mlir_gen_dyn.cpp#L538-L645`

### `trait.tagdispatch.startup-table-init` — Dispatch tables are populated at program startup
- **Divergence**: Logos addition (tag-dispatch).
- **Rule**: Dispatch tables are zero-initialized statically and filled at program startup before user code runs (one initializer per tag system, invoked from main's prologue). Method bodies observe fully-populated tables; the dispatch tables are not const-folded per call site.
- **Source**: `src/compiler/mlir_gen_dyn.cpp#L184-L186`, `src/compiler/mlir_gen_dyn.cpp#L434-L530`, `src/compiler/mlir_gen_dyn.cpp#L532-L536`

### `trait.tagdispatch.tier-boundary-256` — Tag dispatch is two-tier with a type-code boundary of 256
- **Divergence**: Logos addition: tiered type-code dispatch table; no Rust analogue.
- **Rule**: Tag dispatch tables are split into a dense tier-1 array of 256 slots indexed directly by type_code, and a tier-2 sparse lookup function. When both exist, dispatch selects tier-1 iff type_code &lt; 256 (unsigned), else calls the tier-2 lookup(type_code); a missing tier resolves to a null function pointer. At least one tier must exist for the call to be emitted.
- **Source**: `src/compiler/mlir_gen_dyn.cpp#L1287`, `src/compiler/mlir_gen_dyn.cpp#L1366-L1370`, `src/compiler/mlir_gen_dyn.cpp#L1375-L1442`

### `trait.tagdispatch.tier2-binary-search-sorted` — Tier-2 dispatch requires sorted, gap-free codes
- **Divergence**: Logos addition (tag-dispatch).
- **Rule**: Tier-2 dispatch tables list only registrations whose callee is defined; the (type_code, fn) entries are sorted ascending by type_code with no zero/placeholder gaps, and resolution performs an unsigned binary search over type_code returning the paired fn or null on miss.
- **Source**: `src/compiler/mlir_gen_dyn.cpp#L82-L128`, `src/compiler/mlir_gen_dyn.cpp#L378-L391`

### `trait.tagdispatch.two-tier-codespace` — type_code space is split into two dispatch tiers
- **Divergence**: Logos addition (tag-dispatch).
- **Rule**: The type_code key space is partitioned at 256: codes in [1,255] dispatch via a dense direct-indexed table of fixed size 256 (tier-1, O(1) index); codes &gt;= 256 dispatch via a sorted (type_code, fn) pair table searched by binary search (tier-2, O(log n)). A lookup that hits neither tier yields null (no matching impl).
- **Source**: `src/compiler/mlir_gen_dyn.cpp#L181-L186`, `src/compiler/mlir_gen_dyn.cpp#L239-L240`, `src/compiler/mlir_gen_dyn.cpp#L316-L320`, `src/compiler/mlir_gen_dyn.cpp#L604-L645`

### `trait.tagdispatch.type-code-keyed` — Tag-based dispatch keys on a runtime type-code read from the receiver
- **Divergence**: Logos addition: runtime type-code/TagSystem dispatch has no direct Rust analogue (Rust uses vtables only).
- **Rule**: A `#[tag_dispatch]`-style trait call resolves the target method at runtime by (1) reading an integer `type_code` from the receiver value via the trait's TagSystem `read_tag(self=null, obj_ptr) -> i64`, then (2) indexing a per-(tag_system, trait, method) dispatch structure by that type_code to obtain the method function pointer, then (3) calling it indirectly with the receiver pointer as `self: *const u8` followed by the user args. The TagSystem is a stateless unit struct (self passed as null).
- **Source**: `src/compiler/mlir_gen_dyn.cpp#L1308-L1356`, `src/compiler/mlir_gen_dyn.cpp#L1360-L1364`, `src/compiler/mlir_gen_dyn.cpp#L1444-L1474`

### `type.array.size-from-pack` — Array size from variadic pack length
- **Divergence**: Logos addition: pack-length array sizing.
- **Rule**: `[T; P...(P)]` sizes the array from a variadic pack length; lowered to symbolic array-size-var `__sizeof_pack:P` and resolved at monomorphization.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1762-L1768`

### `type.identity.cfg-slot` — Config-slot type identity = (cfg-typevar name, slot key)
- **Divergence**: Logos addition (zone/config slots)
- **Rule**: A config-slot type is identified by the pair (config type-variable name, slot key); distinct slots intern to distinct types.
- **Source**: `src/compiler/sema.cpp#L923-L929`, `src/compiler/sema.cpp#L1050-L1052`

### `type.identity.wstatic-config` — WritStatic-literal type identity = its byte-hash
- **Divergence**: Logos addition (WritStatic const-config type parameters)
- **Rule**: A type parameterized by a WritStatic literal config (`Foo::<@{...}>`) is identified by the byte-hash of that literal; distinct configurations instantiate to distinct types and do not dedupe.
- **Source**: `src/compiler/sema.cpp#L917-L922`

### `type.integer.bit-width` — Integer bit-width and signedness
- **Divergence**: usize/isize width is target-dependent (pointer bits) as in Rust; the exotic 24/56-bit widths are a Logos addition.
- **Rule**: Each concrete integer kind has a fixed bit width and signedness: i8/u8=8, i16/u16=16, i24/u24=24, i32/u32=32, i56/u56=56, i64/u64=64, i128/u128=128; signed forms are signed, unsigned forms unsigned. usize/isize have width equal to the target pointer width (isize signed, usize unsigned). IntLit, Enum, and non-integers have no defined rank (width 0).
- **Source**: `src/compiler/sema_impl.hpp#L4453-L4474`

### `type.pin.non-movable-classification` — Non-movable (location-anchored) type classification
- **Divergence**: Logos addition (zones/pin): `#[pinned]`/`#[zoned2]`/`#[rel_ptr]` anchoring has no Rust analog.
- **Rule**: A type is non-movable iff: it is a `#[pinned]` struct; or a `#[zoned2]` struct (self-relative pointer fields anchored to their own slot); or it inlines (transitively through struct/tuple/array by-value fields, not through pointers/references) a `#[rel_ptr]` or `#[pinned]` field. A `#[rel_ptr]` type itself is movable (its value-form is the resolved absolute pointer); it counts as non-movable only when embedded as an inline field.
- **Source**: `src/compiler/sema_impl.hpp#L2104-L2154`

### `type.ptr.modifier-set` — Raw-pointer modifiers
- **Divergence**: `*zoned` is a Logos-only zoned-pointer modifier (F3).
- **Rule**: A raw pointer type is written `*const T`, `*mut T`, or `*zoned T`/`*zoned mut T`; any other word after `*` is a hard error (`unknown raw-pointer modifier`).
- **Source**: `src/compiler/sema.cpp#L5685-L5699`, `src/compiler/sema.cpp#L5741`

### `type.ptr.zoned` — Zoned raw pointer `*zoned [mut] T`
- **Divergence**: Logos addition (F3 ref-repr design): zoned pointers, no Rust equivalent.
- **Rule**: `*zoned T` / `*zoned mut T` is a zoned raw pointer (Ref-arm self-relative at rest; deref/assign runs the storage↔compute bridge). `zoned` is a contextual keyword recognized only in pointer position (a bare IDENT after `*`), validated as NAME=="zoned" by sema; it is not globally reserved.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1750-L1759`

### `type.ptr.zoned-pointer-distinct` — *zoned T is a distinct pointer type
- **Divergence**: Logos addition (F3 ref-repr/zoned types); no Rust equivalent.
- **Rule**: A zoned raw pointer `*zoned T` is a type distinct from `*T`; the zoned bit participates in type identity (interning, serialization, equality). Deref/assignment through a `*zoned T` runs the zoned storage↔compute bridge rather than a plain load/store.
- **Source**: `src/compiler/sema_impl.hpp#L222-L231`

### `type.ref.metavar` — Metavariable type reference
- **Divergence**: Logos metaprogramming addition.
- **Rule**: `#Ident` and `#(expr)` are type references whose name is supplied by a metaprogram variable/expression rather than a literal identifier.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1801-L1804`

### `type.tagged.thin-ptr-dispatch` — &tagged&lt;TS&gt; Trait
- **Divergence**: Logos-only tagged-dispatch pointer.
- **Rule**: `&tagged<TS> Trait` resolves to a thin TaggedPtr with tag-based dispatch; Trait must be a registered trait and TS must resolve to a concrete struct type, else hard error.
- **Source**: `src/compiler/sema.cpp#L6021-L6039`

### `type.tuple.variadic-arity` — Variadic-arity tuple target `(A...)`
- **Divergence**: Logos addition: variadic tuple impls (no direct Rust equivalent).
- **Rule**: `(A...)` is a variadic-arity tuple type naming pack-typevar A; used as an impl target `impl<A...> Trait for (A...)`. Resolves to a Tuple type with one variadic element naming A.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1726-L1731`

### `type.typeof.expr-type-no-eval` — typeof(expr) yields the sema type without evaluation
- **Divergence**: Logos addition: Rust has no `typeof` operator.
- **Rule**: `typeof(expr)` resolves to the sema-computed type of `expr`; the expression is type-checked but never evaluated at runtime.
- **Source**: `src/compiler/sema.cpp#L5673-L5681`

### `type.writ-arr.elem-set` — Writ typed array type &lt;Elem&gt;[]
- **Divergence**: Logos-only Writ container type-expression.
- **Rule**: `<Elem>[]` resolves to a generic struct `WritArr<elem>`; Elem must be one of I8/U8/I16/U16/I32/U32/I64/U64/F32/F64 (mapped to the Logos primitive), else hard error.
- **Source**: `src/compiler/sema.cpp#L6234-L6266`

### `type.writ-map.key-val-set` — Writ typed map type &lt;K,V&gt;{}
- **Divergence**: Logos-only Writ container type-expression.
- **Rule**: `<K,V>{}` resolves to `WritMap<key,val>`; key must be I32/U32/I64/U64 and value must be `AnyVal` (default), else hard error.
- **Source**: `src/compiler/sema.cpp#L6267-L6297`

### `type.wstatic.literal-arg` — WritStatic literal in type-arg position
- **Divergence**: Logos-only WritStatic value-as-type-arg.
- **Rule**: A WritStatic literal `Foo::<@{...}>` (or a bare writ-lit value-AST in const recognition) resolves to the value's WritStatic type; a missing payload is a hard error.
- **Source**: `src/compiler/sema.cpp#L6370-L6386`

## Unregistered — needs triage

Untagged behavioral differences from Rust that are neither marked as additions nor tied to any `docs/DIVERGENCES.md` row. Per the register's rule, each must be triaged into a §A blessed row (with a tag) or a §B catch-up TODO — none may remain a silent divergence. Notable clusters: implicit integer widening, structural auto-`Copy`, free-fn signature overloading, `str` = `Slice<u8>`, `i64` where Rust uses `usize`, lifetimes not structurally tracked. 71 rule(s).

### `borrow.closure.capture-by-ref-loan` — Non-move closure captures register field-path (RFC-2229) borrows
- **Divergence**: RFC-2229 disjoint closure capture: field-path precision, but a whole-var SHARED capture is a liveness check only (not a recorded shared borrow) to avoid blocking sibling mutation
- **Rule**: A non-`move` closure capturing place `p` by reference registers a borrow of `p` held for the closure holder's lifetime: a mutated/`&mut` capture registers a `&mut` (exclusive) loan, a shared capture registers a `&` (shared) loan/liveness check. A capture of a strict sub-field `p.x` registers a precise FIELD-PATH borrow (so disjoint sibling access `&mut p.y` beside `|| p.x` is allowed; a conflicting `&mut p.x` is rejected); a whole-root capture instead registers a whole-value borrow (mut) or a bare liveness check (shared), since Logos captures a whole variable and a whole-var shared borrow would otherwise block disjoint sibling mutation. Loans release at the closure holder's last use (NLL).
- **Source**: `src/compiler/borrow_check.cpp#L2241-L2276`

### `borrow.move.no-move-out-of-borrowed-place` — Cannot move a move-typed value out of a borrowed place (E0507)
- **Divergence**: Box move-out (`let s = *b`) is rejected because Logos does not implement Rust's built-in Box DerefMove; in Rust it is allowed.
- **Rule**: Moving a move-typed value by value out of a non-owning place is rejected (E0507): deref of a `&`/`&mut` reference variable (`*r`); index `v[i]`/slice-index `s[i]` of a non-raw container (including user `Index`, lowered to `*v.index(i)`); deref of a user `Deref` (`*x.deref()`) including `Box` (`*b`, since DerefMove is unimplemented); and reading a move-typed field out of a `&`/`&mut` receiver (`r.field`). Exempt: any place whose access chain passes through a raw-pointer (`*const`/`*mut`) hop, and partial moves out of owned receivers.
- **Source**: `src/compiler/sema_impl.hpp#L876-L968`

### `coerce.int.implicit-widening` — Safe implicit integer widening
- **Divergence**: Rust performs NO implicit integer widening at all (requires explicit `as`). Logos permits value-preserving implicit widening here.
- **Rule**: An implicit integer widening from `from` to `to` is permitted iff every value of `from` is representable in `to`: signed-&gt;signed and unsigned-&gt;unsigned require to_width &gt;= from_width; unsigned-&gt;signed requires to_width &gt; from_width; signed-&gt;unsigned is never permitted. usize/isize are distinct types: no implicit conversion between a pointer-sized integer and any fixed-width integer (only psize&lt;-&gt;psize among themselves). Either operand having undefined rank (IntLit/Enum/non-integer) blocks widening.
- **Source**: `src/compiler/sema_impl.hpp#L4482-L4495`

### `coerce.let.implicit-int-widening` — Implicit safe integer widening at let-init
- **Divergence**: Rust requires an explicit `as` cast for any integer width change; Logos performs implicit safe widening.
- **Rule**: At a let-init coercion site, a concrete (non-IntLit, non-enum) integer RHS whose type can safely widen to the annotated integer type is implicitly widened (e.g. u32→i64, i32→i64, u8→u32) without an explicit `as`.
- **Source**: `src/compiler/sema_stmt.cpp#L2054-L2061`

### `coerce.unsize.ref-concrete-to-trait-object` — Reference/pointer to concrete unsizes to bare trait object
- **Divergence**: Uniform-fat model: `&dyn` and `*mut dyn` are both 16-byte fat pairs (Logos), unlike Rust where only references unsize.
- **Rule**: `&T`/`&mut T`/`*const T`/`*mut T` (T a concrete struct or primitive) cast to a bare trait object synthesizes a {data,vtable} fat pair; the vtable keys on T's concrete struct name (or the primitive's bare type name for a blanket-impl `&i64 as &dyn`). Only fires when the source pointee is concrete; a `&dyn`→`dyn` reinterpret (pointee already a trait object) is a no-op.
- **Source**: `src/compiler/mlir_gen_expr.cpp#L3470-L3493`

### `const.binop.intlit-fold-overflow` — Integer-literal arithmetic is folded; i64 overflow is rejected
- **Divergence**: Rust folds in the inferred type; Logos folds in i64 and errors on i64 overflow, deferring per-type fit to the coercion site.
- **Rule**: When both arithmetic operands are integer literals with recoverable values, +,-,*,/,% are constant-folded to a single integer literal (of untyped IntLit type); if the fold overflows i64 the expression is rejected rather than silently wrapped.
- **Source**: `src/compiler/sema_expr.cpp#L2319-L2355`

### `const.binop.shift-count-overflow-width` — Literal shift count &gt;= LHS bit-width rejected
- **Divergence**: usize/isize fixed at 64-bit (target-specific).
- **Rule**: &lt;&lt; or &gt;&gt; whose shift count is a literal value &gt;= the bit-width of the left operand's type is a compile-time error (shifting by &gt;= width is undefined); widths: i8/u8=8, i16/u16=16, i24/u24=24, i32/u32=32, i56/u56=56, i64/u64=64, i128/u128=128, usize/isize=64.
- **Source**: `src/compiler/sema_expr.cpp#L2424-L2453`

### `expr.assign.place-nesting-bound` — Deeply-nested assignment targets rejected
- **Divergence**: Compiler-side lowering limitation: Rust places arbitrary-depth field/index/tuple-index nesting; this compiler's general place-write path currently accepts only the bounded shapes above, erroring (with a workaround) on deeper nestings rather than treating the program as ill-formed.
- **Rule**: A place-write target is accepted only for shapes the address-of machinery can lower: a bare variable or `*p` bottoming out a recursion, INDEX_READ recursing to arbitrary depth over its receiver, and FIELD_READ/TUPLE_INDEX bounded to a receiver that is itself var/deref, a field chain over one, or an index into a supported place. Deeper/other nestings are rejected with 'assignment target too deeply nested to assign in place yet' (suggesting an intermediate `&mut` binding) rather than mis-lowered.
- **Source**: `src/compiler/sema_stmt.cpp#L6927-L6964`, `src/compiler/sema_stmt.cpp#L7455-L7463`

### `expr.block.tail-return-adopts-value-type` — Block ending in `return e` adopts e's type
- **Divergence**: No real `!`/never subtyping for tail-return; the return-value's type is adopted as a block-type proxy instead of `!`.
- **Rule**: A block whose final statement is `return e` is non-diverging in the value system: the block's result type is taken as `typeof(e)` even though no value is produced, so the divergent block is usable at a non-void expected type (e.g. inside a tuple/struct literal). The `return` is still lowered and executed.
- **Source**: `src/compiler/sema_expr.cpp#L13664-L13672`, `src/compiler/sema_expr.cpp#L13706-L13720`

### `expr.call.callable-field` — Call of a callable struct field
- **Divergence**: Rust requires explicit `(s.m)(args)` to call a callable field; bare `s.m(args)` is method-only
- **Rule**: If `s.m(args)` finds no method `m` but struct `s` has a field named `m` whose type is a fn-pointer/fn-value or closure, the expression is lowered as a field read followed by a fn-ptr call (fn-value kind) or closure call (closure kind), returning that callable's return type.
- **Source**: `src/compiler/sema_expr.cpp#L8701-L8728`

### `expr.call.static-turbofish-before-method` — Static-call turbofish precedes method name
- **Divergence**: Rust places the turbofish after the method for trait/inherent fns (e.g. T::method::&lt;U&gt;); Logos surface form puts it before the method name on the type path.
- **Rule**: In an associated/static call, turbofish type arguments attach to the receiver type and precede the `::method` segment: `Recv::<T>::method(args)`.
- **Source**: `src/compiler/sema_render.cpp#L203-L241`

### `expr.compound-assign.int-widen` — Implicit integer widening in the compound-assign fallback
- **Divergence**: Rust has no implicit integer widening on assignment.
- **Rule**: In the general (non-`*Assign`-impl) place-compound-assign path, the rhs is implicitly widened to the place's integer type before combining with the base operator.
- **Source**: `src/compiler/sema_stmt.cpp#L2528`

### `expr.deref.non-pointer-identity` — `*x` on a non-pointer, non-Deref type is the identity
- **Divergence**: Not in docs/DIVERGENCES.md as a blessed item; Rust rejects unary `*` on a type without Deref/a pointer kind. This is a permissive relaxation admitting faithfully-ported Rust source that spells an already-loaded read as `*i` (e.g. `for i in &v` sites); soundness is preserved since it only relaxes the diagnostic, never changes which value is produced.
- **Rule**: `*x` where x's type is none of Ptr/Ref/MutRef and has no generic Deref impl returns x unchanged (identity) rather than a diagnostic error.
- **Source**: `src/compiler/sema_expr.cpp#L2702-L2713`

### `expr.fmt.precision-requires-number` — Precision dot requires a number
- **Divergence**: Rust additionally permits `.*` and `.N$` precision forms; Logos here requires a literal number after `.`.
- **Rule**: A `.` in the format spec must be followed by an unsigned-integer precision; a `.` not followed by a digit is a compile error.
- **Source**: `src/compiler/sema_fmt.cpp#L224-L235`

### `expr.index.range-slice` — Range indexing produces a sub-slice
- **Divergence**: Range-slicing relies on stdlib `slice_get_range`; open/inclusive ends are clamped to length rather than panicking on out-of-range as Rust does.
- **Rule**: A range index `recv[lo..hi]`, `recv[lo..]`, `recv[..hi]`, `recv[..]`, or inclusive `recv[lo..=hi]` produces a sub-slice `&[T]` via `slice_get_range(recv, lo, hi)`. The receiver must be a slice, array (decayed to `&[T]` via addr-of + slice-coercion), or reference-to-slice; otherwise an error is reported. Missing `lo` defaults to 0; missing `hi` defaults to INT64_MAX (clamped to len); an inclusive upper bound is lowered as `hi+1`. Bounds are widened to i64. `slice_get_range` must be in scope (`use logos.lang.slice`).
- **Source**: `src/compiler/sema_expr.cpp#L10328-L10389`

### `expr.list-comp.iter-array-or-slice-only` — Comprehension iterables restricted to array/slice
- **Divergence**: Narrower than Rust: only concrete array/slice, no IntoIterator/Iterator protocol.
- **Rule**: The iterable of any comprehension form must have type `[T; N]` (array) or `[T]` (slice); any other iterator type is rejected. Element type defaults to i32 when the array/slice element type is absent.
- **Source**: `src/compiler/sema_expr.cpp#L10896-L10907`, `src/compiler/sema_expr.cpp#L11002-L11013`, `src/compiler/sema_expr.cpp#L11112-L11123`, `src/compiler/sema_expr.cpp#L11245-L11256`

### `expr.method-dispatch.callable-field-call` — Call syntax on a callable struct field with no matching method
- **Divergence**: Rust method-call syntax `recv.f(args)` never falls back to a callable field of the same name (E0599 even when a field `f: fn(..)`/`impl Fn` exists; caller must write `(recv.f)(args)`). Logos accepts the field-call form directly.
- **Rule**: If `recv.method_name(args)` matches no method (including blanket impls) but the receiver's struct type has a field named `method_name` whose type is a fn-pointer-kind or `Closure`, the call is lowered as a field-read of that field followed by an `fn_ptr_call` (fn-pointer field) or `closure_call` (closure field) with the field's closure return type, rather than reporting a missing-method error.
- **Source**: `src/compiler/sema_expr.cpp#L8748-L8775`

### `expr.method.array-len-builtin` — Fixed-array `.len()` is a compile-time built-in
- **Divergence**: Return type is `i64` (Logos stdlib uses i64 for lengths throughout), not `usize` as in Rust's `[T; N]::len() -> usize`.
- **Rule**: `a.len()` where `a` has raw fixed-size array type `[T; N]` is a built-in: it lowers directly to the compile-time constant `N` as an `i64` literal; no runtime call is emitted.
- **Source**: `src/compiler/sema_expr.cpp#L7323-L7331`, `src/compiler/sema_expr.cpp#L7280-L7284`

### `expr.raw-ptr.is-null-safe` — Pointer .is_null() is safe unless shadowed by a user-defined inherent method
- **Divergence**: Logos lets a user-defined inherent is_null on the pointee shadow the built-in raw-pointer null check.
- **Rule**: On a `Ptr` receiver, `.is_null()` does not require unsafe context: it lowers to `(recv as i64) == 0` and takes 0 arguments. If the pointee is a `Struct`/`ZonedStruct`/`Enum` that declares an inherent `<Pointee>__is_null` function, that user-defined method is dispatched instead (resolution falls through, `nullopt`) rather than the built-in null check.
- **Source**: `src/compiler/sema_expr.cpp#L6694-L6726`

### `expr.str.as-bytes-identity` — &str.as_bytes() is a representation identity
- **Divergence**: Logos models &str as Slice&lt;u8&gt;; .as_bytes() is a no-op identity conversion by construction.
- **Rule**: `&str` is modeled as `Slice<u8>` — the same fat-pointer ABI as `&[u8]`. Calling `.as_bytes()` on a receiver whose slice element kind is `U8` lowers to the receiver expression unchanged (no conversion emitted).
- **Source**: `src/compiler/sema_expr.cpp#L6505-L6514`

### `generic.bound.lifetime-arg-not-structural` — Lifetime args in trait bounds are recorded but not dispatched on
- **Divergence**: Logos does not track regions structurally for bound dispatch; lifetime bound-args carry no dispatch significance.
- **Rule**: A lifetime argument at a trait bound's type-argument position (e.g. `Foo<'a>`) is captured for record only; regions are not tracked structurally for bound dispatch.
- **Source**: `src/compiler/sema.cpp#L4034-L4041`

### `generic.mangling.duplicate-fn-error` — Duplicate function detection is full-signature + package qualified
- **Divergence**: Rust does not permit free-function overloading by parameter-type signature under one name in one scope; Logos's signature-keyed registration allows same-named fns with differing signatures to coexist.
- **Rule**: A fn's registration key (symbol_name) is derived from its package, base name, and parameter-type/vararg signature. Two fn declarations (generic or non-generic) whose fully computed symbol_name collide within the same base-name overload bucket are rejected with error "duplicate function '&lt;base&gt;'" — EXCEPT when both are `extern` declarations of the same signature (accepted silently, see item.extern-fn.dedup-signature). Because the key includes package and full parameter signature, the SAME base name with a DIFFERING parameter signature, or the SAME base+signature in a DIFFERENT package, does not collide and both coexist.
- **Source**: `src/compiler/sema_collect.cpp#L4924-L4926`, `src/compiler/sema_collect.cpp#L5049-L5067`, `src/compiler/sema_collect.cpp#L5077-L5093`

### `grammar.expr.call-ufcs-qualified` — UFCS qualified-path call
- **Divergence**: Trait qualifier in &lt;T as Tr&gt;::m is dropped (Rust uses it for disambiguation).
- **Rule**: '&lt;Type as Trait&gt;::method(args)' dispatches on the concrete Type; the trait qualifier is consumed and dropped because the type-dispatch already resolves the method.
- **Source**: `tools/peg_gen/grammars/logos.peg#L3214-L3219`

### `grammar.generic.hrtb-binder` — HRTB for&lt;...&gt; binder parsed then dropped
- **Divergence**: Lifetimes not structurally tracked: HRTB binder is accepted but discarded (Rust enforces it).
- **Rule**: hrtb_binder ::= 'for' '&lt;' LIFETIME (',' LIFETIME)* ','? '&gt;' may prefix any trait_bound. Lifetimes are not tracked structurally, so for&lt;'a&gt; Trait&lt;...&gt; is semantically equivalent to Trait&lt;...&gt; (binder parsed into a disposable head).
- **Source**: `tools/peg_gen/grammars/logos.peg#L3077-L3108`

### `intrinsic.align-of.alignment` — align_of yields alignment
- **Divergence**: Result is i64 (Rust mem::align_of -&gt; usize).
- **Rule**: `align_of::<T>()` requires exactly one type argument and yields `i64` = alignment of T.
- **Source**: `src/compiler/sema_expr.cpp#L5718-L5731`

### `intrinsic.concat.macro` — concat! string-literal concatenation
- **Divergence**: Floats and char literals are not supported (Rust supports them).
- **Rule**: `concat!(a, b, …)` concatenates string, integer (decimal, suffix-stripped), and bool (`true`/`false`) literals at compile time into a single `&str` (`Slice<u8>`) literal. Non-literal args are a compile error. String escapes \n \t \r \\ \" \0 are decoded.
- **Source**: `src/compiler/sema_expr.cpp#L18318-L18324`, `src/compiler/sema_expr.cpp#L17836-L17920`

### `intrinsic.env.macro` — env! / option_env! read environment at compile time
- **Divergence**: option_env! returns an empty &str tombstone rather than Option&lt;&str&gt;.
- **Rule**: `env!("VAR")` yields the value of environment variable VAR as a `&str` literal and is a compile error if unset; `option_env!("VAR")` yields the value or an empty `&str` if unset.
- **Source**: `src/compiler/sema_expr.cpp#L18289-L18316`

### `intrinsic.include-str.macro` — include_str! / include_bytes! embed file contents
- **Divergence**: Rust's include_bytes! has type &[u8;N] distinct from &str; in Logos both are Slice&lt;u8&gt;.
- **Rule**: `include_str!("path")` and `include_bytes!("path")` read the file at compile time (path relative to the including file) and yield its contents as a `&str` (`Slice<u8>`) literal; both forms collapse to the same representation since `str` is `Slice<u8>`. Unreadable files are a compile error.
- **Source**: `src/compiler/sema_expr.cpp#L18252-L18282`

### `intrinsic.include.expr-only` — include! splices a file as an expression
- **Divergence**: Rust supports item-position include!; Logos supports only expression position.
- **Rule**: `include!("path")` reads the file at compile time and re-parses its contents as an expression spliced at the call site; only expression-position include! is supported (item-position is a compile error). Paths are resolved relative to the including file.
- **Source**: `src/compiler/sema_expr.cpp#L18238-L18244`, `src/compiler/sema_expr.cpp#L17686-L17784`

### `intrinsic.line.macro` — line! / column! positional macros
- **Divergence**: column!() always returns 0 rather than the true column.
- **Rule**: `line!()` yields the current source line as `u32`; `column!()` yields `u32` 0 (columns are not tracked).
- **Source**: `src/compiler/sema_expr.cpp#L18221-L18227`

### `intrinsic.offset-of.value` — offset_of! yields a compile-time i64 byte offset
- **Divergence**: Rust's offset_of! yields usize; Logos yields i64.
- **Rule**: `offset_of!(T, f)` evaluates to an `i64` constant equal to the byte offset of field `f` within `T`'s layout, computed by sequentially laying out fields: each field is placed at the next position aligned up to its alignment, then advanced by its byte size. Result type is `i64`.
- **Source**: `src/compiler/sema_expr.cpp#L17657-L17681`

### `intrinsic.sizeof.byte-size` — sizeof yields byte size
- **Divergence**: Logos spelling of size_of; result is i64 (Rust mem::size_of -&gt; usize).
- **Rule**: `sizeof::<T>()` requires exactly one type argument and yields `i64` = byte size of T.
- **Source**: `src/compiler/sema_expr.cpp#L5703-L5716`

### `item.fn.signature-overloading` — Functions overloadable by signature
- **Divergence**: Rust does not permit free-function overloading by signature.
- **Rule**: Functions are keyed by a signature derived from base name, parameter types, and vararg-ness, allowing multiple same-named functions to coexist; only an exact symbol-name collision (same package, base, signature) is a "duplicate function" error.
- **Source**: `src/compiler/sema_collect.cpp#L4712-L4713`, `src/compiler/sema_collect.cpp#L4837-L4881`

### `item.repr.recognized-modes` — `#[repr(...)]` minimal recognised modes
- **Divergence**: Only `transparent` (struct) and integer-width (enum) repr supported; Rust's `C`/`packed`/`align`/etc. not yet.
- **Rule**: `#[repr(...)]` is recognised only on structs (`transparent`) and enums (integer-discriminant width). Other repr modes are parsed and then rejected (no silent acceptance).
- **Source**: `src/compiler/sema_impl.hpp#L1501-L1505`

### `item.static-fn.def` — Static (associated) function definition
- **Divergence**: `static fn` spelling for associated (no-self) functions; Rust uses an `fn` without a `self` parameter inside an impl.
- **Rule**: `[pub] static [unsafe] fn NAME [<params>] (params) [-> T] { ... }` defines an associated/free function with no `self` receiver; its own optional type-parameter list follows the name, matching instance/free fn generics. The name may be the `new` keyword.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1067-L1093`

### `item.static.runtime-initialized-storage` — static items get zero-init storage filled at program startup
- **Divergence**: Rust requires `static` initializers to be const-evaluable; Logos evaluates them at runtime startup instead.
- **Rule**: A non-extern `static` has global storage that is zero-initialized at link time and assigned its declared initializer value at program startup (before `main`), via a synthesized startup initializer running every static's init expression in declaration order. A `static`'s initializer is thus an ordinary runtime-evaluated expression, not a compile-time constant.
- **Source**: `src/compiler/mlir_gen_dyn.cpp#L702-L714`, `src/compiler/mlir_gen_dyn.cpp#L716-L758`

### `item.struct.fields-and-inherent-methods` — struct item form with optional inherent methods
- **Divergence**: Legacy `struct Foo { fields, fn ... }` form (methods inside the struct body) is accepted; not a Rust form.
- **Rule**: A struct is `[pub] struct NAME [<type-params>] { fields... }`, or `[pub] struct NAME [<type-params>] ;` when field-less; each field is `[pub] NAME : TYPE [...]`. Inherent methods may be declared in the struct body, which is equivalent to a separate `impl NAME { ... }` block.
- **Source**: `src/compiler/sema_render.cpp#L1140-L1150`, `src/compiler/sema_render.cpp#L1251-L1308`

### `item.visibility.pub-module` — Visibility marker pub / pub(module)
- **Divergence**: Logos uses `pub(module)` for module-linkage; Rust uses `pub(crate)`/path-restricted visibilities.
- **Rule**: Item visibility is `pub` (fully exported) or `pub(IDENT)` where IDENT is a contextual keyword validated == "module" in sema, meaning module-linkage: visible to other packages of the SAME module but not exported to consumers.
- **Source**: `tools/peg_gen/grammars/logos.peg#L1273-L1284`

### `layout.dstref.fat-only-with-slice-tail` — Custom-DST reference is a 16-byte fat slot only with a literal slice tail
- **Divergence**: Logos custom-DST representation split (slice-tail fat vs dyn-tail/self-describing thin).
- **Rule**: A custom-DST reference (&Foo/&mut Foo where Foo has a tail) is a 16-byte {data,len} fat pointer ONLY when the pointee has a literal `[T]` slice tail (len carried inline) and is not #[self_describing]. A `dyn`-tail DST ref or a #[self_describing] DST is physically THIN (8-byte pointer; tail length recovered in-band, e.g. sizeof(Rc&lt;dyn&gt;)==8) and is not copied as a 16-byte fat slot.
- **Source**: `src/compiler/mlir_gen_stmt.cpp#L1330-L1351`

### `layout.enum.niche-nullptr-nonnull-wrapper` — Null-pointer niche for #[non_null] 8-byte wrapper
- **Divergence**: Logos `#[non_null]` attribute exposes Rust's NonNull niche to user wrapper types.
- **Rule**: The null-pointer niche also applies when the single-field variant's field is a `#[non_null]` struct that is exactly an 8-byte pointer wrapper (Box/Rc/Arc-shape), whose invariant guarantees offset-0 is non-zero.
- **Source**: `src/compiler/mlir_gen_types.cpp#L769-L795`

### `layout.union.max-of-fields` — Union layout is max-size at max-alignment
- **Divergence**: Logos union via #[repr]/union attribute; layout semantics match C/Rust unions.
- **Rule**: A struct marked as a union (`#[repr(...)]` union) is laid out as the maximum field size aligned to the maximum field alignment; all fields overlap at offset 0.
- **Source**: `src/compiler/sema_decl.cpp#L1231-L1233`

### `lex.literal.float` — Float literal syntax
- **Divergence**: A leading `-` is part of the float token (Rust parses `-` as separate unary minus). A fractional part is mandatory (no `1.` form); float-width suffix set is {f32,f64}.
- **Rule**: A float literal matches an optional leading `-`, an integer part, a mandatory `.` with a fractional part (both `[0-9][0-9_]*`), an optional exponent `([eE][+-]?[0-9][0-9_]*)`, and an optional suffix `f32` or `f64`. `_` digit separators are permitted.
- **Source**: `tools/peg_gen/grammars/logos.peg#L456`

### `lex.literal.int-overflow-i64` — Unsuffixed integer literal must fit i64/u64 (64-bit) magnitude
- **Divergence**: Rust default integer literal type is i32; here the raw overflow bound is 64-bit (i64/u64), with per-suffix bounds layered at the call site.
- **Rule**: An integer literal's magnitude is rejected if it exceeds 64-bit representable range: an unsigned magnitude must fit u64; a negated literal's magnitude must not exceed 2^63 (INT64_MIN is representable, anything past overflows). Literals are parsed in base 10, or 0x/0X hex, 0b/0B binary, 0o/0O octal, with `_` digit separators ignored; parsing stops at the first character that is not a valid digit for the base (the type suffix).
- **Source**: `src/compiler/sema_impl.hpp#L4577-L4602`

### `lex.literal.int128-magnitude` — 128-bit integer literal magnitude
- **Divergence**: Logos provides i128/u128 literals; magnitude bound is 128 bits rather than 64.
- **Rule**: An integer literal targeting i128/u128 is accumulated as a 128-bit unsigned magnitude (sign applied by the caller) and is rejected only if its magnitude exceeds 128 bits; 64-bit-overflowing values round-trip intact.
- **Source**: `src/compiler/sema_impl.hpp#L4659-L4703`

### `lex.token.ident` — Identifier token
- **Divergence**: Identifiers are ASCII-only; Rust permits Unicode (XID) identifiers and raw identifiers `r#name`.
- **Rule**: IDENT = `[a-zA-Z_][a-zA-Z0-9_]*` — ASCII letter/underscore followed by ASCII alphanumerics/underscores.
- **Source**: `tools/peg_gen/grammars/logos.peg#L467`

### `lex.token.lifetime` — Lifetime token
- **Divergence**: Lifetime names must start with a lowercase letter or `_`; uppercase-initial lifetimes (allowed in Rust) are not recognized.
- **Rule**: LIFETIME = `'[a-z_][a-z0-9_]*` — an apostrophe followed by a lowercase-initiated identifier (no closing apostrophe).
- **Source**: `tools/peg_gen/grammars/logos.peg#L466`

### `lex.writ.float-literal` — Writ float literal
- **Divergence**: Requires a fractional digit after '.'; bare-integer floats and leading-dot are governed by this regex (no trailing-dot form); 'f'/'d' suffixes.
- **Rule**: A Writ FLOAT is an optional '-', optional integer part, a mandatory '.' with a fractional part, optional exponent ([eE][+-]?digits), and an optional 'f'|'d' type suffix. Regex: /[-]?[0-9]*\.[0-9]+([eE][+-]?[0-9]+)?[fd]?/. The fractional part is required (a '.' must be followed by &gt;=1 digit).
- **Source**: `tools/peg_gen/grammars/writ.peg#L67`

### `module.prelude.implicit-auto-import` — Implicit prelude auto-imported per file
- **Divergence**: Logos uses a named prelude *package*; the model parallels Rust's std prelude but is package-granular.
- **Rule**: Every source file implicitly imports the prelude package in addition to its explicit `use` declarations, unless the file opts out. The implicit prelude is deduplicated against explicit uses (no duplicate import if already named).
- **Source**: `src/compiler/module_loader.cpp#L95-L103`

### `module.use.variant-vs-subpackage-by-case` — Group target classified by first-character case
- **Divergence**: Disambiguation by identifier capitalization is a Logos convention, not a Rust rule.
- **Rule**: In a USE_VARIANTS group `use pkg.X.{...};`, the bracketed target `X` is classified by its first character: lowercase-leading `X` is treated as a grouped sub-package import (each member becomes `pkg.X.<member>`); uppercase-leading `X` is treated as an enum-variant import, importing the enclosing package `pkg` as a wildcard so the type is in scope. This relies on the convention that enum/type names are capitalized.
- **Source**: `src/compiler/module_loader.cpp#L167-L204`

### `module.visibility.pub-marker-only-module` — Restricted-visibility marker accepts only pub(module)
- **Divergence**: Logos has only `pub` and `pub(module)`; Rust's `pub(crate)`/`pub(super)`/`pub(in path)` are not recognised.
- **Rule**: An item's restricted-visibility marker `pub(W)` is accepted only when W is the contextual word `module` (module-linkage). Plain `pub` and no marker are non-module. Any other word (e.g. `pub(crate)`, `pub(super)`, `pub(in path)`) is rejected with the diagnostic "unsupported visibility `pub(W)` — only `pub(module)` is recognised".
- **Source**: `src/compiler/sema_impl.hpp#L1176-L1191`

### `pat.bytes.scrutinee-must-be-u8-array` — Byte-string pattern requires `[u8; N]` scrutinee
- **Divergence**: Rust permits byte-string patterns against `&[u8]`/`&[u8; N]`; Logos requires fixed `[u8; N]` and rejects dynamic slices.
- **Rule**: A byte-string pattern requires the scrutinee (after peeling a single `&`/`&mut` reference) to be a fixed-size array `[u8; N]`; otherwise it is an error. Dynamic `&[u8]` slice scrutinees are not supported.
- **Source**: `src/compiler/sema_stmt.cpp#L4025-L4050`

### `pat.float.literal-rejected` — Float-literal patterns are rejected
- **Divergence**: Rust deprecated-but-still-accepts float patterns; Logos hard-rejects them.
- **Rule**: A float-literal pattern is parsed but rejected as unsupported (IEEE-equality pattern semantics undecided).
- **Source**: `src/compiler/sema_stmt.cpp#L4286-L4294`

### `pat.float.rejected-at-sema` — Float-literal patterns rejected
- **Divergence**: Rust also forbids float patterns (deprecated/removed).
- **Rule**: A float-literal pattern parses but is rejected at sema (not a valid match pattern).
- **Source**: `tools/peg_gen/grammars/logos.peg#L283`

### `pat.lit.float-rejected` — Float-literal pattern rejected
- **Divergence**: Rust deprecated float patterns; Logos rejects them outright.
- **Rule**: A float-literal pattern parses but is rejected by sema with a diagnostic: float equality matching in patterns is deliberately not supported (IEEE equality semantics undefined).
- **Source**: `tools/peg_gen/grammars/logos.peg#L2195-L2199`

### `pat.range.scrutinee-integer` — Range pattern requires integer scrutinee
- **Divergence**: Logos char ranges are handled separately (PAT_CHAR_RANGE); PAT_RANGE is integer-only.
- **Rule**: A range pattern requires an integer scrutinee type; a non-integer, non-error scrutinee is an error. A `never` scrutinee is exempted from this check.
- **Source**: `src/compiler/sema_stmt.cpp#L4685-L4689`

### `pat.str.position-restricted` — String-literal patterns allowed only in specific positions
- **Divergence**: Rust permits string patterns in all pattern positions; Logos restricts them.
- **Rule**: String-literal patterns are supported only as a whole match arm (`match s { "foo" => .. }`), inside an enum-variant payload (`Some("foo")`), or as a tuple element (`("foo", _)`). In any other position (e.g. inside an array/slice pattern) a string-literal pattern is an error.
- **Source**: `src/compiler/sema_stmt.cpp#L4296-L4312`

### `region.impl.trait-arg-lifetime-erased` — Lifetime arguments at trait-argument position are not tracked for trait dispatch
- **Divergence**: Logos does not use regions in trait selection/coherence; Rust's HRTB/lifetime args participate in trait-ref identity even though they are erased at codegen.
- **Rule**: A LIFETIME_PARAM occurring among an impl's trait type-arguments (`impl SomeTrait<'a, T> for X`) is skipped when positionally resolving trait type arguments: regions are not tracked structurally for trait selection/dispatch.
- **Source**: `src/compiler/sema_decl.cpp#L2064-L2066`

### `region.outlives.permissive-unmentioned-pair` — Two unmentioned named regions assumed compatible in permissive mode
- **Divergence**: Permissive default; Rust requires the outlives relation to be explicitly established (would reject).
- **Rule**: In permissive mode, if two named non-static regions L and S neither equal nor reach one another and NEITHER appears anywhere in the explicit outlives graph, L: S is assumed to hold (region inference is expected to unify them). If either L or S appears in the graph (but no path connects them), L: S is rejected. In strict mode, an unestablished named constraint is always rejected.
- **Source**: `include/logos/compiler/outlives.hpp#L86-L102`

### `stmt.assign.int-widen` — Implicit integer widening on assignment
- **Divergence**: Rust has no implicit integer widening on assignment.
- **Rule**: On assignment to an integer variable, a non-literal non-enum integer RHS of a narrower integer kind that can widen safely to the LHS kind is implicitly widened.
- **Source**: `src/compiler/sema_stmt.cpp#L2647-L2653`

### `trait.bounds.partialeq-via-eq` — PartialEq/PartialOrd satisfied by Eq/Ord impls
- **Divergence**: Logos Eq/Ord carry the methods Rust puts on PartialEq/PartialOrd; full split pending.
- **Rule**: A `T: PartialEq` bound is satisfied by an existing Eq impl, and `T: PartialOrd` by an Ord impl (alias resolution over concrete and unwrapped names).
- **Source**: `src/compiler/sema_collect.cpp#L1110-L1131`

### `trait.impl.foreign-private-trait-error` — impl of a foreign private trait is an error
- **Divergence**: Note: §4 module/package visibility model; trait must be pub-accessible to be implemented across package boundaries.
- **Rule**: In `impl Trait for T`, if `Trait` resolves to a trait that is not accessible from the impl site (not pub, or module-only and outside its module), the impl is rejected (privacy error). The check fires at the impl site that introduces the foreign trait name.
- **Source**: `src/compiler/sema_collect.cpp#L2685-L2699`

### `trait.impl.target-ref` — impl for reference types
- **Divergence**: Note: receiver-shape mangling is a Logos dispatch-implementation detail; observable rule is which reference forms are valid impl targets and that &[T] ≡ [T] for dispatch.
- **Rule**: `impl Trait for &T` / `&mut T` is permitted; `&[T]`/`&mut [T]` canonicalize to the fat-pointer slice form and register under the same `$slice$<elem>` key as `impl Trait for [T]` (binding Self to the unsized-slice type); a generic ref-blanket `impl<T> Trait for &T`/`&mut T` keys under a fixed `$ref_$T`/`$mut_ref_$T` sentinel, restricted by coherence to one such impl per trait/ref-shape.
- **Source**: `src/compiler/sema_collect.cpp#L2818-L2863`

### `trait.impl.trait-type-args-bind` — trait type args bind the trait's parameters
- **Divergence**: Note: Logos does not track regions structurally for trait dispatch; trait-position lifetime args are skipped from type-arg resolution.
- **Rule**: For `impl Trait<X> for U`, the trait's positional type arguments are resolved and bound to the trait's declared type parameters (e.g. `impl Into<i32> for C` binds the `Into` parameter to `i32`), making them available in method signatures. Lifetime arguments at trait position (`impl Trait<'a>`) are collected separately and not treated as type args.
- **Source**: `src/compiler/sema_collect.cpp#L3076-L3110`

### `trait.impl.unknown-trait-error` — impl of an undeclared trait is an error
- **Divergence**: Copy and Drop are treated as compiler built-in marker traits resolvable by name alone (not requiring import/dependency-graph visibility).
- **Rule**: `impl Trait for T` requires `Trait` to be a declared trait, except for the built-in marker traits `Copy` and `Drop`, which are always implementable by name without a visible trait declaration; any other unknown trait name is an error.
- **Source**: `src/compiler/sema_collect.cpp#L3064-L3072`

### `type.copy.struct-structural-auto` — Structural auto-Copy for plain-data structs
- **Divergence**: Logos auto-derives Copy structurally; Rust requires explicit `#[derive(Copy)]`. Capability-equivalent (a Copy type stays usable after by-value use).
- **Rule**: A plain-data `struct` with no `impl Drop` and at least one field, whose every field type is Copy, is itself Copy — no `#[derive(Copy)]` opt-in is required. Determined by fixpoint over the struct dependency graph (a struct may become Copy once all its struct-typed fields are known Copy). Zero-field structs are not auto-promoted.
- **Source**: `src/compiler/sema.cpp#L2867-L2880`, `src/compiler/sema.cpp#L2955-L2981`

### `type.copy.structural-auto` — non-Drop struct of all-Copy fields is automatically Copy
- **Divergence**: Rust requires an explicit `#[derive(Copy)]`/`impl Copy`; Logos structurally auto-derives Copy for non-Drop, all-Copy-field structs.
- **Rule**: A struct that does not implement Drop and whose every field type is Copy is automatically Copy, without an explicit `impl Copy`; this runs after manually-written `impl Copy` entries are collected, so it only fills gaps rather than overriding explicit impls.
- **Source**: `src/compiler/sema_collect.cpp#L695-L699`

### `type.impl-trait.param-position-forbidden` — `impl Trait` not allowed at parameter position
- **Divergence**: Logos restriction: Rust supports argument-position impl Trait (APIT).
- **Rule**: `impl Trait` is not supported in parameter position; use an explicit generic `fn f<T: Trait>(x: T)` or `&dyn Trait` instead.
- **Source**: `src/compiler/sema_decl.cpp#L309-L318`

### `type.inhabited.ref-conservative` — References to uninhabited types are treated as inhabited
- **Divergence**: Rust treats `&!` as uninhabited; Logos stays conservative and treats `&Never` as inhabited.
- **Rule**: A reference or pointer to an uninhabited type is conservatively treated as inhabited (only value-carrying composites are marked uninhabited).
- **Source**: `src/compiler/sema.cpp#L4359-L4362`

### `type.let.intlit-default-i32` — Unannotated integer literal binding defaults to i32 (i64 on overflow)
- **Divergence**: Rust defaults unconstrained integer literals to i32 but never silently widens to i64 on overflow (it is a compile error); Logos auto-upgrades to i64.
- **Rule**: An unannotated let whose RHS is an integer literal binds at type i32, upgraded to i64 when the literal value falls outside the i32 range.
- **Source**: `src/compiler/sema_stmt.cpp#L2191-L2202`

### `type.param.unit-type-forbidden` — Unit-typed parameters forbidden
- **Divergence**: Logos restriction: Rust permits `()`-typed parameters.
- **Rule**: A function parameter may not have the unit type `()`; a unit-typed parameter carries no information and is ill-formed.
- **Source**: `src/compiler/sema_decl.cpp#L303-L308`

### `type.str.slice-alias` — str is an alias for Slice&lt;u8&gt;; impls aliased to &[u8]
- **Divergence**: Logos models `str` as Slice&lt;u8&gt;; Rust `str` is a distinct DST.
- **Rule**: `str` is a built-in that resolves to Slice&lt;u8&gt; (printed `&[u8]`); a trait impl whose target is `str` is also registered under target `&[u8]` so trait-satisfaction checks keyed on the printed slice type find the impl.
- **Source**: `src/compiler/sema_collect.cpp#L3777-L3787`

## Conformance notes (no divergence)

Rules whose `divergence` field carries a Rust-conformance or spec-citation note ("Rust-conformant", RFC/`logos-core` references, resolved §B rows) rather than an actual divergence. Listed for audit completeness; nothing to register. 39 rule(s).

### `borrow.assign.static-mut-unsafe` — static mut write requires unsafe; immutable static not writable
- **Divergence**: Rust-conformant (items.static.mut.safety)
- **Rule**: A write to a place rooted at a `static mut` is permitted (storage is mutable) but requires an `unsafe` block; a write to a plain immutable `static` is rejected.
- **Source**: `src/compiler/sema_stmt.cpp#L7005-L7018`

### `borrow.let.ref-from-temp-dangles` — A let-bound reference borrowing into a per-statement temporary is rejected (E0716)
- **Divergence**: Rust E0716 analog (temporary value dropped while borrowed)
- **Rule**: Binding a reference (or borrow-carrying value) whose provenance is a temporary value dropped at the end of the binding statement is an error: the reference would outlive its own statement, but the temporary it borrows into is dropped when the statement ends. The owning value must first be bound to a variable so it outlives the borrow.
- **Source**: `src/compiler/borrow_check.cpp#L2709-L2721`

### `borrow.pass.generic-template-checked` — Generic fn bodies are borrow-checked even when never instantiated
- **Divergence**: Rust-conformant (uninstantiated generics are still checked).
- **Rule**: A dedicated pre-monomorphization pass borrow-checks generic function bodies directly (exclusivity-only mode, no region inference, imprecise move tracking on TypeVars), so an uninstantiated generic is still checked. The post-mono pass checks concrete functions and specializations with full region inference. Functions loaded from a precompiled binary module and extern functions are skipped (already checked when their layer was built).
- **Source**: `src/compiler/borrow_check.cpp#L3788-L3818`, `src/compiler/borrow_check.cpp#L3849-L3852`

### `borrow.scope.stored-borrow-outlives-referent` — Every binding records its borrow sources for end-of-scope outlives checking (E0597)
- **Divergence**: Rust E0597 analog (borrowed value does not live long enough)
- **Rule**: Every `let` binding (not only dropck-relevant ones) records the local borrow sources of its RHS value, so that at scope-pop a stored borrow whose holder outlives the referent it borrows from can be detected and rejected.
- **Source**: `src/compiler/borrow_check.cpp#L2736-L2738`

### `coerce.binop.autoderef-numeric-ref` — Auto-deref reference operand to primitive in scalar binops
- **Divergence**: Models Rust's `impl Add<i32> for &i32` family via auto-deref rather than blanket ref impls.
- **Rule**: For binary operators in {+,-,*,/,%,&lt;,&lt;=,&gt;,&gt;=,==,!=,&,|,^,&lt;&lt;,&gt;&gt;}, an operand of type &T or &mut T whose pointee T is an integer, f32, f64, bool, or char is implicitly dereferenced to T before operator resolution; struct pointees are not peeled.
- **Source**: `src/compiler/sema_expr.cpp#L1718-L1742`

### `coerce.cast.supertrait-upcast` — Supertrait upcast preserves data, swaps to super vtable
- **Divergence**: Rust-conformant (trait upcasting); vtable layout {drop,size,align, methods…, super-vtables…} is Logos-specific.
- **Rule**: `&dyn Sub`/`dyn Sub` cast to `&dyn Super` (Sub ≠ Super, Super a supertrait of Sub) keeps the SAME data pointer and replaces the vtable with Super's vtable, recovered from a stored super-vtable-pointer slot in Sub's vtable at index `3 + |methods(Sub)| + idx(Super)`. Identity dyn casts (Sub == Super) fall through to the no-op reinterpret.
- **Source**: `src/compiler/mlir_gen_expr.cpp#L3321-L3364`

### `coerce.fn.fnitem-to-fnptr` — FnItem coerces to a matching FnPtr; not the reverse, not FnItem to FnItem
- **Divergence**: logos-core 1.4: FnItem (ZST per-fn identity) auto-coerces to FnPtr; Rust models the analogous fn-item to fn-pointer coercion.
- **Rule**: A FnItem value coerces to an FnPtr at every value-use site iff arity matches and each param and the return type are pairwise compatible. FnPtr to FnItem is rejected, and two distinct FnItems with identical signatures are not mutually compatible (distinct fn identity).
- **Source**: `src/compiler/sema.cpp#L1816-L1826`

### `coerce.infer.placeholder-unifies` — Inference placeholder _ unifies in either direction
- **Divergence**: logos-core 1.3
- **Rule**: If either side is the InferredType placeholder (_), the pair is compatible; actual resolution is deferred to the surrounding annotation/RHS unifier.
- **Source**: `src/compiler/sema.cpp#L1836-L1840`

### `coerce.never.subtype-of-all` — Never (!) is a subtype of every type; T to ! rejected
- **Divergence**: logos-core 1.1: T to ! previously accepted, now rejected to match Rust.
- **Rule**: Never coerces to any type T (Never to T accepted unconditionally). The reverse T to Never is rejected.
- **Source**: `src/compiler/sema.cpp#L1827-L1835`

### `coerce.struct.elementwise-typeargs` — Same-named structs compatible iff type-args pairwise compatible
- **Divergence**: logos-core 1.3 (nested)
- **Rule**: Two Struct types with equal struct_name and pkg_name and equal type-arg arity are compatible iff every type-arg pair is compatible (allowing inference holes like Vec&lt;_&gt; vs Vec&lt;i32&gt;).
- **Source**: `src/compiler/sema.cpp#L1846-L1857`

### `divergence.heap.no-class-new-delete` — No C++-style class/new/delete
- **Divergence**: Logos addition/removal vs C++; Rust-conformant (Rust also has no class/new/delete).
- **Rule**: The language has no `class` declaration, `new` expression, or `delete` statement. Heap allocation is expressed via `Box` (and other library owning types), not a built-in `new`/`delete` pair.
- **Source**: `tools/peg_gen/grammars/logos.peg#L165-L166`

### `expr.assign.deref-write` — Dereference write statement
- **Divergence**: Logos addition: distinct DEREF_WRITE/DEREF_COMPOUND statement forms; semantics match Rust place-expression assignment.
- **Rule**: `* p = v ;` writes value `v` through dereferenced place `p` (a `unary_expr`). `* p OP v ;` performs compound assignment through a bare dereference and is defined to lower to `*p = *p OP v`.
- **Source**: `tools/peg_gen/grammars/logos.peg#L2335-L2340`

### `expr.assign.drop-before-replace` — Field assignment drops old value first
- **Divergence**: Rust-conformant (expr.assign.drop-target / B8)
- **Rule**: Assigning to a field place over an owned local root drops the place's prior value before the store, provided the value is live (root owned, definitely-initialized, no overlapping moved-out path) and droppable; assigning to a path also lifts drop-suppression for the covered (equal-or-deeper) moved paths so the scope-end drop releases the new value.
- **Source**: `src/compiler/sema_stmt.cpp#L7386-L7436`, `src/compiler/sema_stmt.cpp#L7592-L7604`

### `expr.assign.union-field-safe` — Writing a union field is safe
- **Divergence**: Rust-conformant (items.union.fields.write-safety)
- **Rule**: Writing to a union field is safe (no `unsafe` required for the write): the place-write LHS sets `in_place_write_lhs_`, suppressing the union unsafe gate that otherwise applies when reading a union field.
- **Source**: `src/compiler/sema_stmt.cpp#L7467-L7473`

### `expr.binop.comparison-signedness` — Ordering comparisons select signed/unsigned by type
- **Divergence**: bool ordering forced unsigned to preserve Rust's `false < true` despite i1 signed representation; documented inline as Rust-conformant intent.
- **Rule**: `<`/`>`/`<=`/`>=` use unsigned comparison when the LHS type is unsigned (u8..u128) or bool, signed comparison otherwise. bool is treated as unsigned so that `false < true` holds (i1 false=0 &lt; true=1).
- **Source**: `src/compiler/mlir_gen_expr.cpp#L1144-L1166`

### `expr.binop.string-vs-str-eq` — String == str views String as str
- **Divergence**: Mirrors Rust `impl PartialEq<str> for String`.
- **Rule**: For == and !=, when one operand is the struct String and the other is str (Slice&lt;u8&gt;), the String operand is viewed as str via .as_str() so the comparison proceeds through the str equality path.
- **Source**: `src/compiler/sema_expr.cpp#L1782-L1808`

### `expr.closure.mutated-capture-by-reference` — Mutated captures are captured by reference
- **Divergence**: Capture mode is inferred per-variable from usage (read-only vs mutated), conceptually aligned with Rust closure capture-mode inference.
- **Rule**: A captured variable that is the target of a mutation in the body (assignment / field write / index write / deref write) is captured by reference so the mutation propagates to the outer binding rather than to a local env copy. A write-only target (no prior read of its base) is still added to the capture set as a whole-variable capture.
- **Source**: `src/compiler/sema_expr.cpp#L14395-L14420`

### `expr.cmp.non-chainable` — Comparison operators are non-chainable
- **Divergence**: Rust-conformant outcome (chained comparison is an error); Logos detects it grammatically for a better diagnostic.
- **Rule**: Comparison operators are non-chainable: at most one comparison per level is well-formed. A chain of 2+ comparators (e.g. `a < b < c`) is parsed as a distinct CHAINED_CMP node so sema can reject it with a dedicated diagnostic rather than a generic syntax error.
- **Source**: `tools/peg_gen/grammars/logos.peg#L2589-L2600`, `tools/peg_gen/grammars/logos.peg#L2424-L2431`

### `expr.compound-assign.opassign-dispatch` — Compound-assign dispatches via *Assign impl when present
- **Divergence**: Rust-conformant operator-overload semantics; Logos struct-name-keyed impl lookup.
- **Rule**: For a place of struct type S, if an impl of the operator's *Assign trait exists for S (matched by concrete or base struct name), `place op= rhs` lowers to the in-place call `op_assign(&mut place, rhs)` (void result, no assign-back). The trait method's Rhs parameter need not equal Self: the impl is selected by the actual rhs operand type, falling back to the Self-Rhs signature if the rhs-typed one does not resolve.
- **Source**: `src/compiler/sema_stmt.cpp#L2318-L2360`, `src/compiler/sema_stmt.cpp#L2493-L2518`

### `expr.drop.tuple-array-reverse` — Tuple and array element drop in reverse order
- **Divergence**: Rust drops array elements in forward (index-ascending) order; tuple reverse-order is conformant. Array order here is N forward but element-by-element; flagged as possibly observable only via Drop side effects.
- **Rule**: Dropping a tuple drops its droppable elements in reverse index order; dropping a fixed array [T;N] drops each of the N elements when T is droppable. Ref/ptr elements and non-droppable elements are skipped, and statically moved-out tuple element positions are suppressed.
- **Source**: `src/compiler/mlir_gen_stmt.cpp#L922-L938`, `src/compiler/mlir_gen_stmt.cpp#L985-L995`

### `expr.match.fnitem-arms-lub-fnptr` — distinct fn-item arms LUB to the common fn-pointer type
- **Divergence**: Rust-conformant: matches Rust LUB for fn-item match arms.
- **Rule**: When two arms produce distinct FnItem values with the same signature (e.g. `=> a_f` and `=> b_f`), the match result type is the corresponding `fn(...)->R` pointer type, since FnItem→FnItem coercion is rejected; both arms coerce to that FnPtr.
- **Source**: `src/compiler/sema_stmt.cpp#L9502-L9523`

### `expr.method.autoderef-lowest-priority` — By-value-self via auto-deref is lowest dispatch priority
- **Divergence**: Mirrors Rust autoderef order: try T/&T/&mut T at a deref level before stepping deeper.
- **Rule**: A method whose `self` is by value, reachable only by auto-dereferencing a `&T`/`&mut T`/`*T` receiver, is selected only if no exact or auto-ref candidate at the current deref level matches. When chosen, the receiver is auto-dereferenced (copying/moving the pointee out, subject to downstream Copy/move borrow checks).
- **Source**: `src/compiler/sema_expr.cpp#L8484-L8491`, `src/compiler/sema_expr.cpp#L8524-L8557`, `src/compiler/sema_expr.cpp#L8563-L8580`

### `expr.static-call.trait-qualified-ufcs` — Trait-qualified UFCS `Trait::method(recv, ...)`
- **Divergence**: Rust-conformant (DIVERGENCES.md: trait-qualified UFCS supported)
- **Rule**: When the class names a TRAIT (not a struct/enum/datatype/type-param) and args are non-empty, `Trait::method(recv, ...)` dispatches on the first argument's concrete receiver type (auto-derefed through refs/ptrs): struct/zoned-struct by name, enum by name, or primitive by type_str. The rewrite to `<recv-type>__<method>` commits only if that concrete symbol actually resolves; otherwise normal resolution and error reporting proceed.
- **Source**: `src/compiler/sema_expr.cpp#L13198-L13248`

### `grammar.expr.closure-param-untyped` — Closure parameter type may be omitted
- **Divergence**: Conformant with Rust closure type-inference.
- **Rule**: closure_param allows the type annotation to be omitted: '|x|' is accepted as well as '|x: T|'. Forms: '&mut IDENT', '&IDENT', 'ref IDENT: T', 'mut IDENT: T', 'mut IDENT', '(pat_binding_list): T', 'IDENT: T', 'IDENT'. The omitted type is inferred from the surrounding fn(T)-&gt;R formal at the call site.
- **Source**: `tools/peg_gen/grammars/logos.peg#L2979-L3000`

### `metaprog.cfg.attr-multi-arg-implicit-and` — cfg attribute multi-arg implicit AND
- **Divergence**: Multi-arg implicit AND matches Rust (noted inline).
- **Rule**: In `#[cfg(...)]` attribute position, a top-level multi-argument list is an implicit AND of its arguments; `#[cfg]` with no args matches (true).
- **Source**: `src/compiler/sema.cpp#L3654-L3673`

### `metaprog.cfg.combinators` — cfg all/any/not combinators and boolean literals
- **Divergence**: cfg(true)/cfg(false) per Rust 1.80 RFC 3695 (noted inline).
- **Rule**: cfg predicates compose: `all(p...)` is the AND of its children, `any(p...)` the OR, `not(p)` requires exactly one child and negates it (else error/false). The literals `cfg(true)`/`cfg(false)` evaluate to true/false directly. Unknown combinators evaluate to false / raise an error in attribute position.
- **Source**: `src/compiler/sema.cpp#L3553-L3582`, `src/compiler/sema.cpp#L3692-L3708`, `src/compiler/sema.cpp#L3721-L3737`

### `metaprog.cfg.key-value-predicates` — cfg key=value predicate resolution
- **Divergence**: Unknown-key-false matches Rust per inline comment.
- **Rule**: A cfg key=value predicate matches against compile-target metadata: target_arch, target_os, target_endian, target_family, target_pointer_width resolve to the host/target platform values; `feature = "name"` matches iff name is in the active feature set. Any unknown key evaluates to false.
- **Source**: `src/compiler/sema.cpp#L3507-L3517`, `src/compiler/sema.cpp#L3572-L3575`

### `mono.subst.closure-substitution` — Closure substitution rewrites param/return/capture types
- **Divergence**: RFC-2229 disjoint-closure-capture metadata preserved through mono
- **Rule**: Substituting a closure applies the type substitution to each parameter type, the return type, and each capture type, while preserving the closure id, move-ness, fn-pointer-ness, escape (heap-env) flag, capture names, per-capture mutability flags, per-capture field paths (RFC-2229), and per-capture narrow field types (also substituted).
- **Source**: `src/compiler/mono_clone.cpp#L4236-L4269`

### `pat.bind.default-binding-mode-struct` — Default binding modes for struct shorthand fields under a reference scrutinee
- **Divergence**: RFC 2005 default binding modes (Rust-conformant intent).
- **Rule**: Under a `&`/`&mut` struct scrutinee, a shorthand field binding of a move-only field type T binds by reference (`&T` / `&mut T` matching the scrutinee's mutability) rather than moving the field out; Copy field types bind by value. Error and bare-TypeVar field types are excluded from the reference promotion.
- **Source**: `src/compiler/sema_stmt.cpp#L5792-L5816`

### `pat.binding.default-by-ref-mode` — Default binding modes wrap payload bindings by reference
- **Divergence**: Rust-conformant (RFC 2005); historical move-only-type restriction now lifted
- **Rule**: Under a `&`/`&mut` scrutinee, every plain named payload binding binds by-reference: the binding type is wrapped in `&`/`&mut` once per scrutinee ref-layer, with the outermost layer carrying mut iff any peeled layer was `&mut`. Bindings to `_` and synthesized slots are exempt.
- **Source**: `src/compiler/sema_stmt.cpp#L3252-L3265`, `src/compiler/sema_stmt.cpp#L3915-L3949`

### `pat.ergonomics.deref-scrutinee` — Match ergonomics peel all &/&mut/* layers
- **Divergence**: Rust-conformant (RFC 2005 default binding modes)
- **Rule**: Pattern matching peels all `&`, `&mut`, and `*` layers of the scrutinee type to obtain the concrete payload shape, so a pattern over `&&Enum<T>` (arbitrary depth) unifies against the inner `Enum<T>`.
- **Source**: `src/compiler/sema_stmt.cpp#L3220-L3243`, `src/compiler/sema_stmt.cpp#L3828-L3851`

### `pat.variant.unit-payload-binding` — Named binding against unit-typed payload is a zero-sized local
- **Divergence**: Rust-conformant (rustc issue-41888 `Err(err)` over `Result<(),()>`)
- **Rule**: When a variant's payload types are all `()`, a `_` binding is dropped and a named binding is kept with a `()` binding type (a zero-sized local in scope), since unit fields are elided from the enum layout. The unit payload position itself is omitted from binding types.
- **Source**: `src/compiler/sema_stmt.cpp#L3852-L3886`

### `region.dangling.dyn-trait-ref` — &dyn Trait data half is a borrowed reference
- **Divergence**: logos-core 2.1 default trait-object lifetime rule
- **Rule**: A borrowing trait object (&dyn Trait, non-owning Kind::TraitObject) is treated as a reference kind for dangling-return detection: returning &dyn Trait to a local is rejected; an owning Box&lt;dyn Trait&gt; does not qualify.
- **Source**: `src/compiler/borrow_check.cpp#L488-L501`

### `stmt.assign.destructuring-into-places` — Destructuring assignment into existing places
- **Divergence**: RFC 2909 (Rust-conformant).
- **Rule**: Destructuring assignment `(a,b)=e` / `[a,b]=e` / `S{a,b}=e` writes into EXISTING places (not new bindings), desugared to `let tmp = rhs;` followed by per-place assignments.
- **Source**: `tools/peg_gen/grammars/logos.peg#L311`

### `trait.binop.partial-ord-derive` — Relational ops derive from partial_cmp when direct method absent
- **Divergence**: Mirrors Rust's default PartialOrd lt/le/gt/ge bodies.
- **Rule**: For a struct LHS with relational op {&lt;,&lt;=,&gt;,&gt;=}, if the direct lt/le/gt/ge method is not implemented but partial_cmp is, the comparison derives as a.partial_cmp(&b) followed by is_lt/is_le/is_gt/is_ge; when partial_cmp returns Option&lt;Ordering&gt; it routes through cmp_opt_is_&lt;op&gt; (None =&gt; false), and when it returns Ordering directly it calls Ordering::is_&lt;op&gt;.
- **Source**: `src/compiler/sema_expr.cpp#L1990-L2055`

### `trait.binop.tuple-eq-impl` — Tuple == / != routes to Eq impl only for non-primitive tuples
- **Divergence**: Primitive-tuple fast path avoids requiring f64:Eq (f64 is PartialEq-only, Rust parity).
- **Rule**: == / != between two tuples of equal arity routes to the tuple's Eq eq/ne impl (keyed concrete `$tuple$N$...`, then arity `$tuple$N`, then variadic `$tuple$variadic`) ONLY when at least one field is non-primitive; an all-primitive tuple falls through to per-field value comparison and never requires the Eq trait. Operands are auto-borrowed to &Tuple.
- **Source**: `src/compiler/sema_expr.cpp#L1812-L1928`

### `type.binop.bitwise-integer-or-bool` — Bitwise/shift operands must be integer (or bool for bitwise-only)
- **Divergence**: Matches Rust `impl BitAnd/BitOr/BitXor for bool`.
- **Rule**: Bitwise operators {&,|,^} require integer or bool operands; shift operators {&lt;&lt;,&gt;&gt;} require integer operands only. The result type is the unified integer type of the operands.
- **Source**: `src/compiler/sema_expr.cpp#L2384-L2416`, `src/compiler/sema_expr.cpp#L2454-L2454`

### `type.identity.lifetime-ignored` — Lifetimes excluded from type identity for & / &mut
- **Divergence**: Rust treats lifetimes as part of the type but as a separate region-check phase; identity-collapse of lifetimes here matches Rust's type-equality-modulo-regions.
- **Rule**: Reference types `&'a T` and `&mut 'a T` have identity determined solely by mutability and pointee `T`; the lifetime `'a` is NOT part of type identity (matches types_equal). Lifetime args on struct/enum/assoc types likewise do not affect type equality.
- **Source**: `src/compiler/sema.cpp#L817-L821`, `src/compiler/sema.cpp#L954-L959`

### `type.infer.never-fallback-on-divergent-body` — ! fallback for unbound type-param of always-diverging callee
- **Divergence**: Rust-2024 `!`-fallback semantics (logos-core 1.1).
- **Rule**: If a callee's body always diverges (panic-tail or `loop {}`-tail) and a type-parameter is otherwise unbound at the call site, the inference variable falls back to `!` (Never). A non-diverging body leaves an unbound type-param as an ambiguity error: `fn f<T>()->T{panic();}` infers T=! while `fn f<T>()->T{return 0;}` is ambiguous.
- **Source**: `src/compiler/sema_impl.hpp#L2574-L2584`

