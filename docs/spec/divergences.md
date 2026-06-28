# Divergences register (compiler-derived)

This is the compiler-derived divergence register: every rule artifact under `tools/spec-extract/rules/**` whose `divergence` field is non-empty, grouped by divergence tag. It is the *mechanical* companion to the hand-curated [`docs/DIVERGENCES.md`](../DIVERGENCES.md) (the single source of truth for blessed §A divergences and §B catch-up TODOs): this file enumerates the concrete rules each tag touches, cross-references whether the tag is registered there, and flags any tag that is not.

Generated from 516 divergence-bearing rules across 34 tag families. Each `### ` entry is one rule, preserving its exact id; source evidence is `file#line`.

## Unregistered tags (needs triage)

Tags appearing in compiler rule artifacts but NOT in `docs/DIVERGENCES.md`:

- `B-ca-02` (1 rule)
- `B-ex-08` (1 rule)
- `B-fn-06` (1 rule)
- `B-ty-07` (1 rule)
- `B-ty-09` (1 rule)
- `B72` (1 rule)
- `B78` (1 rule)
- `B81` (1 rule)
- `B82` (1 rule)
- `B84` (1 rule)
- `B93` (1 rule)
- `B97` (1 rule)
- `E0503` (1 rule)
- `G154-5` (1 rule)
- `G156-1` (3 rules)
- `G160-3` (1 rule)
- `P4` (1 rule)
- `T2-15` (1 rule)

## §A1 — const-eval → metacall

Cross-ref: matches blessed §A1 in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — registered.

### `item.enum.discriminant-const-expr` — enum discriminant from const expression

- Divergence: A1: const-eval at discriminant position is via metacall/CTFE rather than miri.
- Statement: An enum discriminant may be given by a general const expression (e.g. `1 << 1`), evaluated via CTFE; a `metacall { <expr> }` discriminant must contain a single integer tail expression, evaluated via CTFE to the discriminant value.
- Source: src/compiler/sema_collect.cpp#L1985-L2026
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_module.json`

### `metaprog.metacall.args-ctfe-constant` — Every argument of a metacall call form must be a compile-time constant

- Divergence: A1/A6: CTFE of metacall args; replaces Rust const-eval.
- Statement: For the call form, each argument expression must be CTFE-evaluable to a constant literal; an argument that cannot be folded is a compile error. CALL stores arguments as a flat array, while GENERIC_CALL/STATIC_CALL wrap them as `{ ITEMS: [...] }`.
- Source: src/compiler/sema_expr.cpp#L17334-L17359
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_blob.json`

### `metaprog.metacall.block-tail-required` — metacall block must end in a tail expression

- Divergence: A1/A6.
- Statement: A `metacall { ... }` block must terminate with a tail expression (no trailing semicolon) so the metacall yields a value; a block lacking a tail expression is a compile error. The block's value type is the type of that tail expression.
- Source: src/compiler/sema_expr.cpp#L17366-L17389
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_blob.json`

### `metaprog.metacall.const-resolver` — metacall argument CTFE resolves bare module-const idents

- Divergence: A1/A6: metacall const folding.
- Statement: CTFE of metacall arguments and operands resolves a bare identifier naming a module-level const (collected into the module const-value map, including cross-package consts) to that const's value, so expressions like `metacall { THRESHOLD + 1 }` fold.
- Source: src/compiler/sema_expr.cpp#L17311-L17332
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_blob.json`

### `metaprog.metacall.forms` — metacall accepts call, parenthesized-expr, and block forms

- Divergence: A1/A6: metacall is the Logos replacement for const-eval.
- Statement: `metacall` accepts exactly three operand shapes: a call expression (`metacall foo(...)`, including generic `foo::<T>(...)` and static `Type::m(...)`), a parenthesized expression (`metacall (e)`), or a block (`metacall { ... }`).
- Source: src/compiler/sema_expr.cpp#L17084-L17088
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_blob.json`

### `metaprog.metacall.no-nested-metacall` — metacall may not be nested inside another metacall's operand

- Divergence: A1/A6: metacall replaces Rust const-eval; rule has no Rust analogue.
- Statement: A `metacall` operand (call args, or the inner subtree for the block/expr forms) must not contain another `metacall` node; metacall is a one-shot lift to compile time whose result is a runtime value and therefore cannot serve as a compile-time argument to an enclosing metacall. Violation is a compile error.
- Source: src/compiler/sema_expr.cpp#L17090-L17178
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_blob.json`

### `metaprog.metacall.no-runtime-capture` — metacall block/expr form cannot capture enclosing runtime locals

- Divergence: A1/A6: compile-time evaluation model specific to metacall.
- Statement: In the block and parenthesized-expr forms, every VAR_REF must resolve to a name introduced inside the operand (LET/FOR/FOR_EACH binding, or a match-arm pattern binding), a module-level const, or a known function (concrete or generic). A reference to an enclosing-scope runtime local is a compile error, since the metacall is evaluated at compile time with no access to surrounding locals.
- Source: src/compiler/sema_expr.cpp#L17196-L17302
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_blob.json`

### `metaprog.metacall.return-type` — metacall result type must be primitive scalar, WritStatic, Writ, or ExprBlob

- Divergence: A1/A6: WritStatic/Writ/ExprBlob returns are Logos additions.
- Statement: The type produced by a metacall operand must be a primitive scalar (bool; integer kinds i8/i16/i24/i32/i56/i64 and u8/u16/u24/u32/u56/u64; f32/f64; integer/float literal types), a &str / Slice<u8>, WritStatic, Writ (incl. Rc<Writ>), or ExprBlob. Any other result type is a compile error.
- Source: src/compiler/sema_expr.cpp#L17408-L17424
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_blob.json`

### `metaprog.metacall.runtime-passthrough` — metacall lowers as a runtime pass-through until driver-side splice

- Divergence: A1/A6: compile-time splice model.
- Statement: During sema iterations a metacall lowers to its operand's lowered value (a pass-through), keeping the in-progress IR valid for borrow/type checks. The driver replaces the metacall AST node with the evaluated literal before the final non-metaprog sema pass, so this pass-through lowering never reaches code generation.
- Source: src/compiler/sema_expr.cpp#L17606-L17610
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_blob.json`

### `mono.subst.const-generic-value-use` — Const-generic params used in value position substitute their concrete value

- Divergence: A1/A2 related: const-generics are real, distinct from const-eval
- Statement: A const-generic parameter `<const N: T>` referenced in expression position is monomorphized by splicing its concrete value: a scalar/IntLit binding lowers to an integer literal of the substituted value; a WritStatic-literal binding splices the registered WritStatic literal at the use site.
- Source: src/compiler/mono_clone.cpp#L509-L538
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part1.json`

### `trait.method.multi-trait-ambiguity` — Method provided by multiple traits is ambiguous

- Divergence: A1: collision removes the plain base from the registry; Rust resolves by receiver/inference where unambiguous
- Statement: If a method name `m` on type `S` is provided by more than one trait, the plain unqualified call `s.m(...)` is an error; the call must be disambiguated via a trait-bounded generic context or an explicit trait-qualified call.
- Source: src/compiler/sema_expr.cpp#L8683-L8700
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_method_call.part4.json`

## §A2 — `const fn` → plain fn + metacall

Cross-ref: matches blessed §A2 in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — registered.

### `const.def.initializer-const-evaluable` — Const/static initializer must be const-evaluable

- Divergence: A2 — const-evaluable bare fn calls are not supported; the escape hatch is explicit `metacall fn(...)` (Rust would allow `const fn`).
- Statement: A const/static initializer must be one of: a literal (int/bool/str/float/char/bytes/wstatic); a WritStatic literal (writ map/array/str/int/float/bool/null); a `metacall fn(...)`; a CAST/PAREN/UNARY of a const-evaluable operand; a BINOP whose both operands are const-evaluable; an array/tuple literal (deferred to a later, more specific check); a struct literal all of whose field-init values are const-evaluable (field-shorthand rejected); a VAR_REF to an already-collected module const/static or to a known free fn (fn-pointer constant); or `&X` where X is a VAR_REF to a module const or otherwise const-evaluable. Any other form (notably a bare fn call) is rejected, because it would silently inline at every read site rather than produce a compile-time constant.
- Source: src/compiler/sema_collect.cpp#L2218-L2327
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_type_alias.part1.json`

## §A3 — macros/derive → metaprog + quote

Cross-ref: matches blessed §A3 in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — registered.

### `coerce.return.ref-by-descriptor` — Reference-kind return ABI determined by RefRepr descriptor

- Divergence: A3/A4 fat-pointer return representation
- Statement: When the return type is a reference kind, its by-value return representation is the reference's RefRepr: dyn-trait and slice references return their 16-byte fat (pointer,metadata) pair by value; closure / custom-DST / thin references return their 8-byte value pointer.
- Source: src/compiler/mlir_gen_fn.cpp#L83-L89, src/compiler/mlir_gen_fn.cpp#L138-L143
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_fn/logos.json`

### `metaprog.metacall.exprblob-deferred-typing` — ExprBlob-returning metacall defers result typing to the post-splice pass

- Divergence: A3/A6: ExprBlob is the Logos metaprog AST-fragment return.
- Statement: When a metacall returns an ExprBlob (an AST-expression fragment marker), pass-1 typing is deferred: `let X: T = metacall foo()` accepts any annotated T over an ExprBlob RHS; the actual expression type is recovered after the driver splices the blob and pass-2 sema re-lowers it.
- Source: src/compiler/sema_expr.cpp#L17400-L17407
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_blob.json`

### `metaprog.quote-expr.antiquot-carrier-positions` — Antiquots are recognized only in defined AST carrier positions

- Divergence: A3/A6
- Statement: Antiquots and repetition groups are recognized only within the supported carrier set: VAR_REF, BINOP (lhs/rhs), PAREN/UNARY/CAST/DEREF (value), FIELD_READ (selector + receiver), CALL/METHOD_CALL/STATIC_CALL (callee-name-var, receiver, args), STRUCT_LIT/FIELD_INIT/FIELD_SHORTHAND, ARR_LIT/TUPLE_LIT/BLOCK items, statement carriers (LET, LET_DESTRUCT, EXPR_STMT, TAIL_EXPR, RETURN), and control flow (IF, WHILE, FOR, LOOP, ASSIGN, COMPOUND_ASSIGN). Antiquots in unsupported shapes are not substituted.
- Source: src/compiler/sema_expr.cpp#L16586-L16747
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `metaprog.quote-expr.antiquot-must-be-in-scope` — Antiquot variable in quote_expr! must be a bound local

- Divergence: A3/A6
- Statement: A `#name` antiquot inside `quote_expr!` is an error unless `name` is a variable in scope at the quote site ("`#name` — variable not in scope").
- Source: src/compiler/sema_expr.cpp#L16510-L16514
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `metaprog.quote-expr.no-nested-repeat` — Nested repetition groups are not allowed

- Divergence: A3/A6
- Statement: A `#(...)` repetition group may not be nested inside another `#(...)` group ("nested `#(...)` repetition not supported").
- Source: src/compiler/sema_expr.cpp#L16589-L16597
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `metaprog.quote-expr.reify-ast-to-exprblob` — quote_expr! reifies an expression AST into an ExprBlob

- Divergence: A3/A6 (replaces Rust macro/quote layer)
- Statement: `quote_expr! { e }` evaluates to a value of struct type `ExprBlob` carrying the serialized AST of `e`. With no antiquots, the AST is emitted as a static rodata blob and wrapped directly as `ExprBlob { ptr }`.
- Source: src/compiler/sema_expr.cpp#L16386-L16423, src/compiler/sema_expr.cpp#L16806-L16813
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `metaprog.quote-expr.repeat-cursor-length-agree` — Fixed-length cursors in one repetition group must agree on length

- Divergence: A3/A6
- Statement: Within a single `#(...)*` group, all fixed-size `[Ident; N]` cursors must share the same length N; a sibling cursor with a different N is rejected ("cursor length mismatches sibling cursor in same #(...)*"). A `Vec`-backed (dynamic) cursor makes the group dynamic and waives the fixed-length agreement check.
- Source: src/compiler/sema_expr.cpp#L16539-L16548
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `metaprog.quote-expr.repeat-cursor-type` — Repetition cursor must be [Ident;N], Vec<Ident>, or Vec<ExprBlob>

- Divergence: A3/A6
- Statement: A `#name` antiquot inside a `#(...)*` repetition group (a cursor) must bind a value of type `[Ident; N]` (fixed count N), `Vec<Ident>`, or `Vec<ExprBlob>` (dynamic count); any other type is rejected ("expected [Ident; N], Vec<Ident>, or Vec<ExprBlob>").
- Source: src/compiler/sema_expr.cpp#L16523-L16538, src/compiler/sema_expr.cpp#L16469-L16492
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `metaprog.quote-expr.repeat-needs-cursor` — A repetition group must contain at least one cursor antiquot

- Divergence: A3/A6
- Statement: A `#(...)*` repetition group body must contain at least one cursor antiquot `#x` of a cursor type; an empty-cursor body is rejected ("`#(...)*` body has no cursor `#x`").
- Source: src/compiler/sema_expr.cpp#L16600-L16605
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `metaprog.quote-expr.scalar-antiquot-type` — Scalar antiquot must be Ident, or Ident/ExprBlob outside ident-only positions

- Divergence: A3/A6
- Statement: A `#name` antiquot outside a repetition group, in a general expression position, must bind a value of type `Ident` or `ExprBlob`; in ident-only positions (field names, struct type name, field-read selector) it must bind an `Ident`. Otherwise it is rejected ("expected Ident" / "expected Ident or ExprBlob").
- Source: src/compiler/sema_expr.cpp#L16549-L16560, src/compiler/sema_expr.cpp#L16618-L16621, src/compiler/sema_expr.cpp#L16661-L16685
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `metaprog.quote-expr.subst-runtime` — quote_expr! with antiquots substitutes at runtime via logos_quote_expr_subst

- Divergence: A3/A6
- Statement: `quote_expr!` containing N>0 antiquots lowers to a block that binds the static template blob and one `IdentSpan { ptr, count, kind }` per placeholder, then calls `logos_quote_expr_subst(template_ptr, size, &spans[0], N) -> *const u8` and wraps the result as `ExprBlob { ptr }`. Span kind is 0 for Ident slots, 1 for ExprBlob slots, 2 for Vec<ExprBlob> cursors.
- Source: src/compiler/sema_expr.cpp#L16815-L16981, src/compiler/sema_expr.cpp#L16866-L16943
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `type.identity.dstref` — Custom-DST reference identity = (package, name, mutability, owning kind, type-args)

- Divergence: A3 (custom-DST)
- Statement: A custom-DST reference type's identity = (package, struct name, mutability, owning kind {Borrow/Box}, type-args); an owning `Box<Foo>` custom-DST is distinct from a borrowed `&Foo`.
- Source: src/compiler/sema.cpp#L855-L863, src/compiler/sema.cpp#L1009-L1014
- Rule file: `tools/spec-extract/rules/sema/sema/install_snapshot.json`

### `type.identity.slice-mut-owning` — Slice identity = (mutability, owning kind, element)

- Divergence: A3 (custom-DST / Box<[T]> as owning slice kind)
- Statement: Slice types are distinguished by element T, mutability, and owning kind (const_val): `&[T]`, `&mut [T]`, and owning `Box<[T]>` are mutually distinct types.
- Source: src/compiler/sema.cpp#L841-L847, src/compiler/sema.cpp#L997-L1003
- Rule file: `tools/spec-extract/rules/sema/sema/install_snapshot.json`

## §A6 — Logos additions (variadics, Writ, metaprog, fibres)

Cross-ref: matches blessed §A6 in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — registered.

### `const.wstatic.content-identity` — Writ static literal type-arg identity is content-only

- Divergence: A6 — Writ is a Logos-only feature; no Rust analogue.
- Statement: A Writ static literal `@{...}` used at type-argument position is reduced to a `WStaticLit` type whose identity is a position-free content hash of the literal AST (schema-aware FNV-1a over node CODE plus value bytes/string children). Two structurally identical `@{...}` literals at different source positions yield the SAME type; differing content yields distinct types. First-write-wins: the first lowering of a given hash registers the materialising LExpr that mono later substitutes for `__const_param:CFG` references.
- Source: src/compiler/sema.cpp#L6392-L6499, src/compiler/sema.cpp#L6486-L6498
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_wstatic_value.json`

### `const.wstatic.dup-key-error` — Duplicate keys in a Writ map literal are rejected

- Divergence: A6 — Writ-specific.
- Statement: Within a Writ map literal (`WRIT_MAP`), two entries with the same key (after stripping surrounding quotes) are an error: "duplicate key '<k>' in Writ map literal". Empty keys are ignored. This applies to map literals at type-argument position, not only `pub const … = @{...}`.
- Source: src/compiler/sema.cpp#L6425-L6440
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_wstatic_value.json`

### `const.wstatic.type-lit-resolves-scope` — Writ type literals resolve type params in current scope

- Divergence: A6 — Writ-specific.
- Statement: A `@type(T)` (`WRIT_TYPE_LIT`) child resolves its TYPE node with the in-scope type parameters and contributes its canonical `type_str` to the literal's content identity; thus the same syntactic literal under different type-param bindings produces distinct WStaticLit types. A legacy NAME-only shape substitutes the bound type param when present, else uses the bare name.
- Source: src/compiler/sema.cpp#L6462-L6482
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_wstatic_value.json`

### `expr.struct-lit.union-single-field` — Union literals initialize exactly one field; missing-field check skipped

- Divergence: A6
- Statement: For a union struct, the all-fields-initialized check is suppressed: a union literal initializes only one (active) field by design.
- Source: src/compiler/sema_expr.cpp#L10015-L10021, src/compiler/sema_expr.cpp#L10215-L10221
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_field_read.part2.json`

### `expr.struct-lit.variadic-field-expansion` — Variadic struct field accepts expansion names `name_*`

- Divergence: A6
- Statement: A variadic struct field named `name` accepts literal field names of the form `name_<suffix>`; each such expansion value is type-checked against the variadic field's type and the variadic field is marked initialized.
- Source: src/compiler/sema_expr.cpp#L9882-L9897, src/compiler/sema_expr.cpp#L10052-L10074
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_field_read.part2.json`

### `generic.bounds.variadic-tail-param` — Variadic tail parameter absorbs extra type args

- Divergence: A6
- Statement: If the last type parameter is variadic, type args beyond the non-variadic count are all checked against that final (variadic) parameter; otherwise excess args are ignored once parameters are exhausted.
- Source: src/compiler/sema_collect.cpp#L812-L833
- Rule file: `tools/spec-extract/rules/sema/sema_collect/simplify_all_types.json`

### `generic.field.variadic-expansion` — Variadic field `name_N` selects the Nth element of the variadic type-arg pack

- Divergence: A6 — variadic type/field packs are Logos-only.
- Statement: A variadic struct field declared `name: A...` expands to fields `name_0, name_1, …`; field `name_<idx>` whose declared type is the variadic type parameter resolves to the type-arg at (start-of-pack + idx), where start-of-pack is the count of preceding non-variadic type parameters. Out-of-range or non-TypeVar variadic field types fall back to the raw declared type.
- Source: src/compiler/sema.cpp#L6578-L6582, src/compiler/sema.cpp#L6606-L6631
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_wstatic_value.json`

### `generic.spec.partial-pattern-typevars` — Partial specialization keeps unbound params as type variables

- Divergence: A6: partial specialization of user structs is a Logos addition.
- Statement: In a struct specialization's pattern list, each slot that does not resolve to a known type stays a free type variable (e.g. `Map<Bitmap, V>` keeps `V`). The concrete spec name derives from `concrete_struct_name(make_generic_struct(name, patterns))`, so both full and partial specs are registered and later matched by best-fit at lookup. Pattern type variables are scoped only during collection and removed afterward.
- Source: src/compiler/sema_collect.cpp#L3810-L3833, src/compiler/sema_collect.cpp#L3858-L3860
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_struct_spec.json`

### `generic.spec.struct-pattern-classification` — Struct specialization detection

- Divergence: A6: Logos supports user struct specialization (`struct Map<Bitmap, V> {...}`), which Rust lacks for structs.
- Statement: A `struct Name<...>` decl is a specialization (not a fresh generic base) iff (a) some type-param slot is a structured pattern (`*T`, `[T;N]`), OR (b) some slot is a concrete/known type name (primitive, alias, struct, datatype, or enum). A bare type-param name is treated as a concrete user-type spec only when a base of the same name is already registered in the current package AND the name is in the pre-scanned decl-name set; otherwise the decl is registered as a generic base. Specializations are not added to `structs_`; they are lowered directly.
- Source: src/compiler/sema_collect.cpp#L3974-L3976, src/compiler/sema_collect.cpp#L4258-L4304
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_struct_spec.json`

### `intrinsic.reflect.apply-generic` — apply_generic(g: Type, args) instantiates a generic constructor

- Divergence: A6 (Logos-only type-level composition intrinsic)
- Statement: `apply_generic(g, args)` (callee __apply_generic__) instantiates the generic constructor described by Type value `g` (produced by generic_of) with `args`, routing through the same struct allocation as type_apply. The template name is recovered from g's `Type` struct-literal `name` field (a string literal); both operands are chased through VarRef let-bindings (max 8 hops).
- Source: src/compiler/mono_clone.cpp#L2085-L2119
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part4.json`

### `intrinsic.reflect.has-trait-of` — has_trait_of::<Trait>(t: Type) -> bool folds at monomorphization

- Divergence: A6 (Logos-only metaprog/reflection intrinsic; no Rust equivalent)
- Statement: `has_trait_of::<Trait>(t)` (callee __has_trait_of__) folds to a `bool` literal during monomorphization. The concrete type T is recovered from t's `Type` struct-literal `uid` field, which must be a `__type_uid_of__::<T>()` call (after chasing VarRef through let-bindings, max 8 hops); T is substituted with the active type substitution. The result is `true` iff T (named by its concrete struct name, enum name, or type_str, truncated at any `$G` generic-suffix) has an impl of Trait, computed recursively over concrete and blanket impls.
- Source: src/compiler/mono_clone.cpp#L1588-L1652
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part4.json`

### `intrinsic.reflect.reify-type` — reify_type(t: Type) -> Type recovers a source TypeRef and re-emits Type

- Divergence: A6 (Logos-only reflection intrinsic)
- Statement: `reify_type(t)` (callee __reify_type__) recovers a concrete TypeRef from a direct Type-producer argument and re-emits a fresh `Type` struct literal. Supported argument shapes (after chasing VarRef through let-bindings, max 8 hops): (1) a `Type` struct literal whose `uid` field is `__type_uid_of__::<T>()` → T substituted; (2) a `__typelist_head__`/`__typelist_nth__` call → the indexed pack element. A missing argument is fatal; any other (unsupported) shape is a fatal compile-time error.
- Source: src/compiler/mono_clone.cpp#L1741-L1834
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part4.json`

### `intrinsic.reflect.type-apply` — type_apply(name, args: [Type;N]) -> Type instantiates a struct template

- Divergence: A6 (Logos-only type-level composition intrinsic)
- Statement: `type_apply(name, args)` (callee __type_apply__) instantiates the struct template named `name` (a string literal; surrounding quote chars stripped) with the TypeRefs recovered from `args` and folds to a `Type` value for the instantiation. `name` must be a string literal (else fatal). The instantiated type's `pkg_name` is taken from the matching template in the program's struct table. Each element TypeRef is recovered from the same producer shapes reify_type accepts (Type struct-lit uid call, or typelist head/nth); a non-recognized producer element is a fatal compile-time error.
- Source: src/compiler/mono_clone.cpp#L1841-L2083
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part4.json`

### `intrinsic.reflect.type-apply-pack-splice` — type_apply pack-splice fast path over Type-array intrinsics

- Divergence: A6 (Logos-only variadic type-pack splice)
- Statement: When the `args` operand of type_apply is itself a Type-array producer intrinsic, its element TypeRefs are spliced directly into the template instantiation instead of requiring an array-literal shape: `type_refs_of` contributes its (substituted) type-args as the pack; `args_of::<T>` contributes T's type-args; `typelist_tail::<T>` contributes T's pack minus its first element; `tuple_elems_of::<T>` contributes T's tuple element types (only when T is a Tuple). Otherwise `args` must be an array literal (else fatal).
- Source: src/compiler/mono_clone.cpp#L1878-L1972
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part4.json`

### `intrinsic.reflect.type-struct-shape` — Reflected Type value layout {kind,name,size,align,uid}

- Divergence: A6 (Logos-only reflection value)
- Statement: A reflected `Type` value materialized by a folding reflection intrinsic is the struct `Type` with fields `kind: u32` = the type's Kind tag, `name: &[u8]` = type_str(T), `size: i64` = size_of(T), `align: i64` = align_of(T), and `uid: u64` = a canonical 64-bit type hash (type_hash_64bit ∘ type_hash_23 ∘ type_id_canon). The compiler records uid→T so the value can be reified back to T.
- Source: src/compiler/mono_clone.cpp#L1716-L1730, src/compiler/mono_clone.cpp#L1810-L1833, src/compiler/mono_clone.cpp#L2074-L2082
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part4.json`

### `intrinsic.reflect.typelist-head-nth` — typelist_head/nth::<L>(i) -> Type folds to a Type struct literal

- Divergence: A6 (Logos-only type-level pack intrinsic)
- Statement: `typelist_head::<L>()` and `typelist_nth::<L>(i)` (callees __typelist_head__/__typelist_nth__) fold to a single `Type { kind, name, size, align, uid }` struct literal describing element idx of L's type-arg pack: head uses idx=0; nth requires `i` to be a literal int. A missing type argument, a non-literal nth index, or an index outside [0, pack.size()) is a fatal compile-time error.
- Source: src/compiler/mono_clone.cpp#L1672-L1731
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part4.json`

### `intrinsic.reflect.typelist-len` — typelist_len::<L>() -> i64 folds to the pack arity

- Divergence: A6 (Logos-only type-level pack intrinsic)
- Statement: `typelist_len::<L>()` (callee __typelist_len__) folds to an `i64` literal equal to the number of type arguments in L's type-argument pack (0 when L is absent). O(1) compile-time probe; the canonical L is `TypeList<T...>`.
- Source: src/compiler/mono_clone.cpp#L1657-L1668
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part4.json`

### `item.datatype.def` — Writ datatype definition

- Divergence: A6
- Statement: A datatype item is `[pub[(vis)]] eidos NAME [<type-params>] { field_def_or_doc* }`. It declares a Writ-fabric datatype with named/repeat-group fields; the optional generic parameter list and visibility marker are accepted.
- Source: tools/peg_gen/grammars/logos.peg#L1096-L1100
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

### `item.datatype.explicit-inst` — Explicit datatype instantiation declaration

- Divergence: A6
- Statement: `[pub[(vis)]] eidos TYPE_REF ;` (no body) is an explicit-instantiation declaration that binds metadata annotations (e.g. `#[type_code=N]`) to a concrete generic instantiation, e.g. `#[type_code=42] datatype Array<i32>;`.
- Source: tools/peg_gen/grammars/logos.peg#L1102-L1109
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

### `item.field.repeat-group` — Repeat-group field (quote)

- Divergence: A6
- Statement: `#( field_def ),*` and `#( field_def )*` denote a repeat-group of field definitions (REPEAT_GROUP, OP=1 comma-separated / OP=0 plain), for use in quoted item bodies.
- Source: tools/peg_gen/grammars/logos.peg#L1183-L1186
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

### `item.field.variadic` — Variadic field

- Divergence: A6
- Statement: A field of form `IDENT ... : TYPE_REF` marks a variadic field (IS_VARIADIC).
- Source: tools/peg_gen/grammars/logos.peg#L1203-L1204
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

### `item.fn.antiquot-name` — Function with antiquoted name

- Divergence: A6
- Statement: `[pub] [unsafe] fn #(expr) [<type-params>] ( [params] ) [-> T] block` carries an expr-TOM name (NAME_VAR), valid only inside a quote body; these alts omit the where-clause because NAME_VAR and WHERE share a slot.
- Source: tools/peg_gen/grammars/logos.peg#L1286-L1293, tools/peg_gen/grammars/logos.peg#L1312-L1319
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

### `item.fn.param-variadic` — Variadic parameter

- Divergence: A6
- Statement: `IDENT : T ...` marks a variadic parameter (IS_VARIADIC); plain `IDENT : T` is the ordinary typed parameter.
- Source: tools/peg_gen/grammars/logos.peg#L1379-L1382
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

### `item.struct.explicit-inst` — Explicit struct instantiation declaration

- Divergence: A6: see B-item-92 — bare `struct Foo;` is the unit struct, generic form kept for the unbound-typevar diagnostic
- Statement: `[pub[(vis)]] struct TYPE_REF ;` where TYPE_REF carries type arguments (e.g. `struct Foo<i64>;`) is an explicit-instantiation declaration binding annotations to a generic struct instantiation. The dedicated `instantiate Foo<T>;` form is preferred.
- Source: tools/peg_gen/grammars/logos.peg#L1133-L1138
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

### `item.trait.explicit-inst` — Explicit genos/trait specialization declaration

- Divergence: A6
- Statement: `[pub[(vis)]] <trait-kw> TYPE_REF ;` (no body) binds annotations to a logical-family (genos) specialization of a concrete trait instantiation; implementing eidos inherit the metadata via impl.
- Source: tools/peg_gen/grammars/logos.peg#L1111-L1118
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

### `metaprog.enum-intrinsic.variant-count-of` — __variant_count_of__::<E>() yields variant count

- Divergence: A6
- Statement: __variant_count_of__::<E>() evaluates to an i64: the number of variants of E when E is an enum with a known definition, else 0.
- Source: src/compiler/mono_clone.cpp#L2395-L2418
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part5.json`

### `metaprog.enum-intrinsic.variant-names-of` — __variant_names_of__::<E>() yields variant names

- Divergence: A6
- Statement: __variant_names_of__::<E>() evaluates to an array of the variant names (string literals) of E in declaration order; empty if E is not a known enum.
- Source: src/compiler/mono_clone.cpp#L2395-L2399, src/compiler/mono_clone.cpp#L2421-L2425, src/compiler/mono_clone.cpp#L2470-L2477
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part5.json`

### `metaprog.enum-intrinsic.variant-payload-counts-of` — __variant_payload_counts_of__::<E>() yields per-variant payload arities

- Divergence: A6
- Statement: __variant_payload_counts_of__::<E>() evaluates to an i64 array giving, per variant in declaration order, the number of payload types it carries; empty if E is not a known enum.
- Source: src/compiler/mono_clone.cpp#L2426-L2431, src/compiler/mono_clone.cpp#L2470-L2477
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part5.json`

### `metaprog.enum-intrinsic.variant-payload-types-flat-of` — __variant_payload_types_flat_of__::<E>() yields flattened payload-type reflections

- Divergence: A6
- Statement: __variant_payload_types_flat_of__::<E>() evaluates to an array of `Type` reflection values: every payload type of every variant of E, in declaration order, flattened across variants and substituted to concrete types; empty if E is not a known enum.
- Source: src/compiler/mono_clone.cpp#L2432-L2477
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part5.json`

### `metaprog.metacall.writ-autofreeze` — Writ-returning metacall auto-freezes to WritStatic and is call-form only

- Divergence: A6: Writ/WritStatic is a Logos addition.
- Statement: A metacall whose operand returns a (mutable) Writ / Rc<Writ> is auto-frozen: user code observes the spliced value as WritStatic (the lowered expression is retyped to WritStatic). The Writ return type is supported only on the call form (`metacall foo()`); using it with the block or expr form is a compile error.
- Source: src/compiler/sema_expr.cpp#L17537-L17568, src/compiler/sema_expr.cpp#L17597-L17603
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_blob.json`

### `metaprog.quote-ty.antiquot-type-var` — $ident antiquot inside quote_ty! refers to a bound Type value

- Divergence: A6 (Logos addition)
- Statement: An ANTIQUOT_TYPE `$x` inside `quote_ty!` lowers to a variable reference of type `Type` (the in-scope binding named `x`), instead of being reified from a static type.
- Source: src/compiler/sema_expr.cpp#L16182-L16184, src/compiler/sema_expr.cpp#L16314-L16316
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `metaprog.quote-ty.array-antiquot-literal-size` — quote_ty! array with antiquot element requires literal integer size

- Divergence: A6 (Logos addition)
- Statement: `quote_ty! { [$t; N] }` lowers to `__array_type_apply__(elem_producer, N)`; the size N MUST be a literal integer (a non-numeric/symbolic size is rejected with "array antiquot requires literal integer size").
- Source: src/compiler/sema_expr.cpp#L16238-L16263
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `metaprog.quote-ty.generic-inst-antiquot` — quote_ty! generic instantiation with antiquot args lowers to __type_apply__

- Divergence: A6 (Logos addition)
- Statement: `quote_ty! { Foo<args...> }` with at least one `$ident` antiquot among the args lowers to `__type_apply__("Foo", [elems])`, where each elem is a var-ref (for `$x`) or a reified `Type` struct literal (for a concrete type arg). Lifetime args and pack-expand args in this position are rejected ("lifetime / pack args not yet supported").
- Source: src/compiler/sema_expr.cpp#L16299-L16355
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `metaprog.quote-ty.pack-splice` — quote_ty! generic pack-splice lowers to __type_apply__ with runtime array

- Divergence: A6 (Logos addition)
- Statement: `quote_ty! { Foo<$ts...> }`, where the sole generic argument is an ANTIQUOT_PACK `$ts...`, lowers to `__type_apply__("Foo", ts)` where ts is a var-ref to a runtime `Array<Type>`. A pack-splice mixed with any other generic argument (`Foo<$t, $ts...>`) is rejected ("mixed pack-splice with other args not yet supported").
- Source: src/compiler/sema_expr.cpp#L16271-L16294
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `metaprog.quote-ty.reify-type-to-struct` — quote_ty! reifies a type into a runtime Type value

- Divergence: A6 (Logos addition; metaprog reflection intrinsic)
- Statement: `quote_ty! { T }` evaluates to a value of struct type `Type` whose fields are { kind: u32 = __type_kind_of__::<T>(), name: &[u8] = __type_name_of__::<T>(), size: i64 = size_of::<T>(), align: i64 = align_of::<T>(), uid: u64 = __type_uid_of__::<T>() }.
- Source: src/compiler/sema_expr.cpp#L16357-L16383, src/compiler/sema_expr.cpp#L16179
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `metaprog.quote-ty.tuple-antiquot` — quote_ty! tuple with antiquot lowers to __tuple_type_apply__

- Divergence: A6 (Logos addition)
- Statement: `quote_ty! { ($t1, $t2, ...) }` where at least one element is an antiquot lowers to `__tuple_type_apply__([p1, p2, ...])` where each pi is the per-element Type producer (var-ref for `$x`, reified `Type` literal otherwise); mixed literal/antiquot elements are permitted.
- Source: src/compiler/sema_expr.cpp#L16209-L16234
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_quote_ty.json`

### `metaprog.type-intrinsic.apply-generic` — __apply_generic__(g, args) instantiates a struct template

- Divergence: A6 (metaprog / variadics — Logos addition, no Rust equivalent)
- Statement: During monomorphization, __apply_generic__(g, [a0..aN]) requires g to resolve to a generic_of value carrying a non-empty template name and args to be an array literal; it constructs the Struct type tmpl_name<recover(a0)..recover(aN)>, threading the template definition's pkg, and yields a `Type` reflection value {kind, name=type_str(t), size=size_of(t), align=align_of(t), uid}. A missing template name or non-array args is a compile-time abort.
- Source: src/compiler/mono_clone.cpp#L2130-L2140, src/compiler/mono_clone.cpp#L2191-L2239
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part5.json`

### `metaprog.type-intrinsic.array-type-apply` — __array_type_apply__(Type, N) builds an array type

- Divergence: A6
- Statement: __array_type_apply__(elem, n) requires elem to recover to a type producer and n (after var-chase) to be an integer literal; it constructs the Array type [recover(elem); n] and yields its `Type` reflection value. A non-producer elem or non-literal size is a compile-time abort.
- Source: src/compiler/mono_clone.cpp#L2244-L2247, src/compiler/mono_clone.cpp#L2342-L2362
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part5.json`

### `metaprog.type-intrinsic.tuple-type-apply` — __tuple_type_apply__([Type;N]) builds a tuple type

- Divergence: A6
- Statement: __tuple_type_apply__(arr) requires arr (after var-chase) to be an array literal of type producers; it constructs the Tuple type (recover(e0),..,recover(eN-1)) and yields its `Type` reflection value. Non-array argument is a compile-time abort.
- Source: src/compiler/mono_clone.cpp#L2244-L2247, src/compiler/mono_clone.cpp#L2317-L2341
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part5.json`

### `metaprog.type-intrinsic.type-producer-recover` — Type-producer recovery for type-apply intrinsics

- Divergence: A6
- Statement: An argument to a type-apply intrinsic denotes a type via one of: a __typelist_nth__(L,i)/__typelist_head__(L) call (resolving to element i (default 0) of the substituted type-list L's type_args, out-of-range yields no type), or a `Type` struct literal whose `uid` field is __type_uid_of__::<T>() (resolving to subst(T)). VarRef arguments are chased through type-let bindings (≤8 hops) before recovery. An argument that yields no type is a compile-time abort.
- Source: src/compiler/mono_clone.cpp#L2141-L2190, src/compiler/mono_clone.cpp#L2255-L2315
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part5.json`

### `metaprog.type-intrinsic.type-reflection-value` — Type reflection value shape and uid

- Divergence: A6
- Statement: A reflected `Type` value is a struct literal with fields kind:u32 = t.kind, name:&[u8] = type_str(t), size:i64 = size_of(t), align:i64 = align_of(t), uid:u64 = type_hash_64bit(type_hash_23(type_id_canon(t))); the uid→type mapping is registered for the constructed type.
- Source: src/compiler/mono_clone.cpp#L2214-L2238, src/compiler/mono_clone.cpp#L2363-L2387
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part5.json`

### `metaprog.variadic.tuple-all-eq` — __tuple_all_eq__::<T>(a, b) expands to an &&-chain of elementwise eq

- Divergence: A6
- Statement: __tuple_all_eq__::<T>(a, b) with T = (t0,..,tn) expands to the left-associated && of per-element comparisons a.i.eq(&b.i): nested-tuple elements inline a recursive chain; slice elements (str renders as element name "str") are called by-value as a 2-arg free function; other elements use a method call resolving the symbol whose name contains "<elem>__eq__f__" at a `.`-boundary or start. When T is not a tuple or fewer than 2 args, it evaluates to bool literal `true`. An empty tuple yields `true`.
- Source: src/compiler/mono_clone.cpp#L2488-L2585
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part5.json`

### `metaprog.variadic.tuple-each-field-debug` — __tuple_each_field_debug__::<T>(self, f) expands variadic tuple Debug

- Divergence: A6
- Statement: __tuple_each_field_debug__::<T>(self, f) with T = (t0,..,tn) expands the variadic tuple Debug impl into a fmt_seq-combined Result chain interleaving fmt_tuple_open/sep/close helpers with each field's Debug::fmt, resolving each element formatter to "<elem>__Debug__fmt" (trait-qualified) or "<elem>__fmt"; the reused `&mut Formatter` is reborrowed per call. When T is not a tuple or fewer than 2 args, it evaluates to fmt_tuple_close(f).
- Source: src/compiler/mono_clone.cpp#L2591-L2634
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part5.json`

### `metaprog.writ-blob.ast-fragment-recurse` — WRIT_BLOB carrying an AST-category root lowers as that expression

- Divergence: A6: Writ/metaprog is a Logos addition (ExprBlob AST fragments spliced from metafunctions).
- Statement: A WRIT_BLOB whose serialized root TinyMap has schema category CAT_AST and whose variant code is a supported expression node (BINOP, LIT_INT, LIT_BOOL, LIT_STR, VAR_REF, CALL, PAREN_EXPR, UNARY, FIELD_READ, METHOD_CALL, CAST, INDEX_READ, STRUCT_LIT, ARR_LIT, TUPLE_LIT, BLOCK, BLOCK_STMT, IF) is lowered by recursively type-checking that root node as an ordinary expression, yielding its recovered expression type. The blob's arena is retained for the lifetime of sema.
- Source: src/compiler/sema_expr.cpp#L17019-L17050
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_blob.json`

### `metaprog.writ-blob.opaque-static-fallback` — Non-AST WRIT_BLOB lowers to an opaque WritStatic literal

- Divergence: A6: WritStatic is a Logos addition.
- Statement: A WRIT_BLOB whose root is null, non-TinyMap, or not of an AST expression category is lowered to an opaque data literal of type WritStatic carrying the raw blob bytes verbatim.
- Source: src/compiler/sema_expr.cpp#L17056-L17060
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_blob.json`

### `module.prelude.implicit-injection` — Implicit prelude is wildcard-imported into source modules

- Divergence: A6/note — prelude package is Logos's package-model analogue of Rust's std prelude.
- Statement: Each source-side module (not binary-archive ASTs) implicitly gains a wildcard import of the configured prelude package, unless the module is the prelude itself or already imports it. A module opts out with the inner annotation `#![no_implicit_prelude]`.
- Source: src/compiler/sema.cpp#L6911-L6936
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_wstatic_value.json`

### `mono.reflect.varref-let-chase` — Reflection-intrinsic operands chase VarRef through let-inits, bounded 8 hops

- Divergence: A6 (mechanism specific to Logos compile-time reflection)
- Statement: When folding a reflection intrinsic, an operand that is a VarRef is resolved by following the variable's recorded let-initializer expression, repeated up to 8 times, to reach the underlying producer expression; this enables `let x = <producer>; intrinsic(x)` to fold identically to `intrinsic(<producer>)`. The hop cap guards against self-referential bindings.
- Source: src/compiler/mono_clone.cpp#L1599-L1610, src/compiler/mono_clone.cpp#L1755-L1762, src/compiler/mono_clone.cpp#L1870-L1877, src/compiler/mono_clone.cpp#L2099-L2110
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part4.json`

### `mono.subst.variadic-param-expand` — A variadic param expands to one concrete param per pack element

- Divergence: A6 (Logos addition — variadics)
- Statement: A variadic parameter `p: A...` whose type is a TypeVar bound to a type pack of length N expands into N non-variadic params named via the per-index pack-arg naming scheme, each typed by the corresponding pack element; non-variadic params are type-substituted unchanged with name/slot/owning-box-dyn flags preserved.
- Source: src/compiler/mono_clone.cpp#L4791-L4818
- Rule file: `tools/spec-extract/rules/mono/mono_clone/walk.json`

### `type.antiquot.quote-ty-only` — Type antiquotation

- Divergence: A6
- Statement: `$ident` in type position is a type antiquotation valid only inside `quote_ty! { ... }`; resolving it elsewhere is an error.
- Source: tools/peg_gen/grammars/logos.peg#L1456-L1459
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

### `type.cfg-slot.projection` — Type-level cfg-slot projection

- Divergence: A6
- Statement: `<type:CFG.path>` projects, at mono-time, the type stored at a path within a WritStatic-typed type-level binding. Path steps are `.IDENT` (string key), `.INTEGER` (int key) and `.[INTEGER]` (array index). At least one path step is required. `<type:CFG.SLOT>::Assoc` projects an associated type on the slot base.
- Source: tools/peg_gen/grammars/logos.peg#L1428-L1449
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

### `type.closure.type` — Closure type

- Divergence: A6: Rust spells closures via Fn-family bounds; Logos has a dedicated `|..|->R` closure type syntax.
- Statement: `|T1, T2| -> R` is a closure type used in parameter annotations; the zero-arg form `|| -> R` is accepted (the `||` token is split).
- Source: tools/peg_gen/grammars/logos.peg#L1657-L1664
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

### `type.datatype.data-plain-inference` — DataPlain vs DataNode inference for datatypes

- Divergence: A6: Writ datatype DataPlain/DataNode classification is Logos-only.
- Statement: A datatype is DataPlain unless it (transitively, through array element types) embeds a datatype field that is not itself DataPlain, or a generic/unknown datatype field; such fields demote the enclosing datatype to DataNode. A by-value concrete DataPlain nested datatype does NOT demote the outer type; generic datatype fields (non-empty type args, e.g. `RelPtr<Node>`) and forward-/cross-package-referenced datatypes are treated conservatively as DataNode.
- Source: src/compiler/sema_collect.cpp#L3945-L3964
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_struct_spec.json`

### `type.datatype.pod-field-restriction` — Writ datatype fields must be POD-compatible

- Divergence: A6/A11: Writ datatype fabric is a Logos-only feature; uses extra packed int widths.
- Statement: A field of a `datatype` (Writ fabric type) must be one of: a primitive scalar (i8..i128/u8..u128 incl. packed i24/u24/i56/u56, f32/f64, bool, integer/float literal types), an array whose element is datatype-safe, another datatype (ZonedStruct), a plain struct that is a `#[rel_ptr]` self-relative pointer, or an unresolved type variable (checked later by mono). Any other field type is rejected. Annotation types (compile-time only) are exempt and may hold non-POD fields such as `str`.
- Source: src/compiler/sema_collect.cpp#L3892-L3933
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_struct_spec.json`

### `type.tagged.thin-pointer` — tagged thin pointer type

- Divergence: A6
- Statement: `&tagged<T> Name` is a thin tag-dispatched pointer: a type_code tag is stored in memory before the object, and call sites read the tag, look up the dispatch table, and call indirectly.
- Source: tools/peg_gen/grammars/logos.peg#L1490-L1494
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

### `type.typeof.expr` — typeof type

- Divergence: A6
- Statement: `typeof(expr)` is the compile-time type of expr; the expression is not evaluated.
- Source: tools/peg_gen/grammars/logos.peg#L1461-L1463
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

### `type.writ.lit-and-array-map` — Writ literal / typed array / typed map types

- Divergence: A6
- Statement: `@{...}` at type position is a WritStatic value literal type (LIT_WSTATIC). `<Elem>[]` is a Writ typed-array type and `<K[,V]>{}` is a Writ typed-map type (used in `as` casts).
- Source: tools/peg_gen/grammars/logos.peg#L1451-L1473
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

## §A7 — panic strategy = abort-only

Cross-ref: matches blessed §A7 in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — registered.

### `generic.infer.never-fallback` — Unbound type-param falls back to ! for diverging callees

- Divergence: A7 — abort-only panic; `!`-fallback for diverging bodies follows Rust-2024 inference.
- Statement: If a non-variadic type-parameter remains unbound after inference, it is an error (ambiguous) UNLESS the callee's body is statically known to always diverge (panic/loop/never-returning tail), in which case the parameter falls back to the never type `!`. The discriminator is the callee body, not the surrounding callsite divergence: `fn f<T>()->T { return 0; }` errors as ambiguous while `fn f<T>()->T { panic(); }` resolves T = `!`.
- Source: src/compiler/sema_expr.cpp#L3946-L3966
- Rule file: `tools/spec-extract/rules/sema/sema_expr/unify_types.json`

### `item.extern.abi-whitelist` — extern ABI string whitelist

- Divergence: A7: "C-unwind" is accepted at parse but unwinding-across-FFI is moot (panic=abort).
- Statement: The ABI string of an `extern "ABI" { … }` block or an `extern "ABI" fn …` item must be one of "C", "C-unwind", "system", or "Rust" (enclosing quotes optional); any other string is rejected.
- Source: src/compiler/sema_collect.cpp#L1334-L1344, src/compiler/sema_collect.cpp#L1379-L1381
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_module.json`

## §A8 — Pin/Unpin → `#[pinned]` type-property (+ coexisting Rust API)

Cross-ref: matches blessed §A8 in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — registered.

### `borrow.pin.non-movable-no-by-value-slot` — Location-anchored types may not occupy a by-value slot

- Divergence: A8: `#[pinned]` is non-movability as a property of the TYPE (no value-form), distinct from Rust's pointer-level Pin<P>.
- Statement: A non-movable (location-anchored) type — one with a self-relative `#[rel_ptr]`/`#[zoned2]` field, or a `#[pinned]` type — may not be bound to any by-value slot (let local, parameter, match/for/closure/destructure binding); it must live behind a pointer, in place (e.g. an arena or `[u8;N]` buffer), and be built through a `*mut T`.
- Source: src/compiler/sema_impl.hpp#L2327-L2346, src/compiler/sema_impl.hpp#L2440-L2464
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/logos.part5.json`

### `layout.pinned.non-movable-type` — #[pinned] type is location-anchored and non-movable

- Divergence: A8
- Statement: A `#[pinned]` type's bits are anchored to its storage slot: it must not be moved by value, is accessed in place, and is materialized to a movable value form only explicitly. It is non-movable itself (unlike `#[rel_ptr]`, whose value form is the resolved absolute pointer).
- Source: src/compiler/sema_impl.hpp#L2446-L2453
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/logos.part5.json`

### `type.return.non-movable-by-value-forbidden` — Location-anchored types cannot be returned by value

- Divergence: A8
- Statement: A type that is non-movable — containing a self-relative `#[rel_ptr]` field, or being `#[pinned]` — may not be returned by value; return a pointer (`*mut T` / `&T`) into its zone segment instead. (Crossing a function boundary by value would invalidate the self-relative anchor.)
- Source: src/compiler/sema_decl.cpp#L501-L513
- Rule file: `tools/spec-extract/rules/sema/sema_decl/logos.part1.json`

## §A9 — module-path syntax (`.`-package + `::`-item)

Cross-ref: matches blessed §A9 in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — registered.

### `module.path.package-name` — Package name is dot-joined module path

- Divergence: A9
- Statement: A package's fully-qualified name is its module NAME with each PATH_PART name appended joined by `.` (e.g. `my.cool.pkg`).
- Source: src/compiler/sema_collect.cpp#L731-L744
- Rule file: `tools/spec-extract/rules/sema/sema_collect/simplify_all_types.json`

### `module.path.qualified-call` — Package-qualified call constrains free-fn resolution to that package

- Divergence: A9: packages are `.`-separated, items reached via `::`.
- Statement: A call `pkg.path::fn(args)` carries a dotted package qualifier (RECEIVER + QUAL_PARTS joined by `.`); free-function resolution for that call is restricted to the named package.
- Source: src/compiler/sema_expr.cpp#L2727-L2757
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_unary.part1.json`

### `mono.uid.module-fingerprint-tags` — Runtime type UID includes per-module fingerprint tags

- Divergence: A9 — Logos coexistence of same-named types across modules; no Rust crate-disjointness analog.
- Statement: The canonical type-identity string for runtime UID hashing (type_id::<T>(), Any/downcast/quote_ty) is the type-string PLUS a '|<name>$M<module_id>' tag for EVERY non-stdlib nominal node anywhere in the type tree (recursing through pointee/elem/type-args/tuple-elems/closure params+ret), so two modules' same-named pkg::Type (incl. nested, e.g. Box<pkg::Widget>) hash to DISTINCT UIDs. stdlib (logos.*) and no-module compiles contribute no tags, yielding a string byte-identical to the plain type-string (UIDs unchanged).
- Source: src/compiler/mono_impl.hpp#L772-L808
- Rule file: `tools/spec-extract/rules/mono/mono_impl_hpp/logos.part2.json`

## §A10 — `dyn Fn*` → bare Closure pair

Cross-ref: matches blessed §A10 in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — registered.

### `expr.call.callable-resolution` — Callee resolution to closure or fn-pointer

- Divergence: A10: dyn Fn* collapses to the bare Closure type.
- Statement: A call `x(args)` treats `x` as callable when its type is a Closure or fn-value kind; `Box<dyn Fn*>` (Box<Closure>) is unwrapped to its inner Closure, and an Fn-bounded generic type-param `F` is treated as a closure with the bound's `fn_params`/`fn_ret` signature.
- Source: src/compiler/sema_expr.cpp#L2845-L2926
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_unary.part1.json`

### `layout.closure.fn-env-pair` — Closure value is a {fn_ptr, env_ptr} pair

- Divergence: A10 — dyn Fn/FnMut/FnOnce collapse to this Closure pair; no separate Fn-trait vtable.
- Statement: A closure value is represented as a two-field aggregate: field 0 = function pointer, field 1 = environment pointer. Calling a closure invokes the function pointer with env_ptr prepended as the implicit first argument, followed by the user arguments.
- Source: src/compiler/mlir_gen_expr.cpp#L4759-L4799
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_expr/gen_expr_kind-4.json`

### `trait.bound.fn-family-intrinsic` — Fn-family bound satisfied by callable shapes intrinsically

- Divergence: A10
- Statement: A parenthesized Fn/FnMut/FnOnce bound is satisfied intrinsically (no registered impl needed) by any fn-value kind, Closure, TypeVar (deferred to outer mono pass), or Struct/ZonedStruct (struct-with-Fn-impl bridge); any other kind fails the bound.
- Source: src/compiler/mono_clone.cpp#L5072-L5081, src/compiler/mono_clone.cpp#L5154-L5178
- Rule file: `tools/spec-extract/rules/mono/mono_clone/clone_fn_signature.json`

### `trait.closure.fn-family-auto-impl` — Closure types automatically satisfy Fn/FnMut/FnOnce

- Divergence: A10
- Statement: Every closure type (canonical name beginning with `|`, i.e. `|T1,...| -> R`) is treated as implementing Fn, FnMut, and FnOnce without any explicit `impl`; the trait engine answers satisfies(Fn-family, closure) = true.
- Source: src/compiler/mono_clone.cpp#L4943-L4948
- Rule file: `tools/spec-extract/rules/mono/mono_clone/clone_fn_signature.json`

### `type.dyn.fn-family-is-closure` — dyn Fn/FnMut/FnOnce resolves to Closure

- Divergence: A10
- Statement: `dyn Fn(P...) -> R`, `dyn FnMut(...)`, `dyn FnOnce(...)` resolve directly to the Closure type {fn_ptr, env_ptr}; there is no distinct Fn-trait-object vtable layer.
- Source: src/compiler/sema.cpp#L5928-L5952
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_type.json`

## §A11 — integer widths (+ Writ I24/U24/I56/U56)

Cross-ref: matches blessed §A11 in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — registered.

### `layout.abi.scalar-sizes` — Scalar ABI byte sizes

- Divergence: A11 (I24/U24/I56/U56 are Logos-only widths)
- Statement: ABI size: void/never = 0; bool/u8/i8 = 1; i16/u16 = 2; i24/u24 = 3; i32/u32/f32/char = 4; i56/u56 = 7; i64/u64/f64/usize/isize/pointer/&/&mut/fnptr/fn-item/tagged-ptr = 8; i128/u128 = 16. The Writ-fabric widths I24/U24/I56/U56 occupy their narrow byte sizes (3 and 7).
- Source: src/compiler/mono_clone.cpp#L348-L361
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part1.json`

### `lex.literal.integer` — Integer literal syntax and width suffixes

- Divergence: A11: width set includes Writ-fabric widths i24/u24/i56/u56 beyond Rust's {8,16,32,64,128}+size. Also: a leading `-` is part of the integer token itself (Rust treats `-` as a separate unary operator).
- Statement: An integer literal matches an optional leading `-`, then a decimal (`[0-9][0-9_]*`), hex (`0x[0-9a-fA-F_]+`), binary (`0b[01_]+`), or octal (`0o[0-7_]+`) magnitude, with `_` digit separators, optionally suffixed by a width tag drawn from {i8,i16,i24,i32,i56,i64,i128,u8,u16,u24,u32,u56,u64,u128,usize,isize}.
- Source: tools/peg_gen/grammars/logos.peg#L457
- Rule file: `tools/spec-extract/rules/grammar/logos/tokens.json`

## B-ca-02

Cross-ref: `B-ca-02` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `const.def.type-checked-initializer` — Const initializer must be compatible with the declared const type

- Divergence: B-ca-02: diagnostic surfaced at sema rather than at MLIR-verifier time.
- Statement: For `const NAME: T = expr`, the type of `expr` must be compatible with the declared type `T`; an incompatible initializer is a compile error reported at sema. Error types on either side suppress the check.
- Source: src/compiler/sema_decl.cpp#L1541-L1560
- Rule file: `tools/spec-extract/rules/sema/sema_decl/lower_struct_def.part1.json`

## B-ex-08

Cross-ref: `B-ex-08` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `expr.cmp.no-chained-comparisons` — Chained comparisons rejected

- Divergence: B-ex-08
- Statement: A comparison chain with 2+ comparators in a row (`a < b < c`) is rejected at sema with the diagnostic "chained comparisons not supported; use `a < b && b < c`". It parses (CHAINED_CMP) but is not a valid program.
- Source: tools/peg_gen/grammars/logos.peg#L289
- Rule file: `tools/spec-extract/rules/grammar/logos/nodes.json`

## B-fn-06

Cross-ref: `B-fn-06` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `expr.return.implicit-tail` — Tail expression is implicit return

- Divergence: B-fn-06
- Statement: A trailing expression with no terminating `;` at statement position synthesizes an implicit `return expr` for a non-void function.
- Source: tools/peg_gen/grammars/logos.peg#L291
- Rule file: `tools/spec-extract/rules/grammar/logos/nodes.json`

## B-ty-07

Cross-ref: `B-ty-07` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `type.ref.double-ref-nesting` — Double reference types desugar to nested references

- Divergence: B-ty-07
- Statement: `&&T` resolves to a nested reference `&(&T)`; `&&mut T` resolves to `&(&mut T)`.
- Source: tools/peg_gen/grammars/logos.peg#L286-L287
- Rule file: `tools/spec-extract/rules/grammar/logos/nodes.json`

## B-ty-09

Cross-ref: `B-ty-09` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `type.paren.unwrap-to-inner` — Parenthesized type is structurally its inner type

- Divergence: B-ty-09
- Statement: A parenthesized type `(T)` is unwrapped to its inner type `T`; `(T)` and `T` are structurally identical.
- Source: tools/peg_gen/grammars/logos.peg#L290
- Rule file: `tools/spec-extract/rules/grammar/logos/nodes.json`

## B1

Cross-ref: `B1` appears in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — registered.

### `borrow.classify.type-param-move-unless-copy` — Bare type parameter moves unless Copy-bounded

- Divergence: B1
- Statement: Inside a generic body a bare type-parameter T is a Move type unless it carries an explicit `T: Copy` bound; partial moves of fields typed T are tracked accordingly.
- Source: src/compiler/borrow_check.cpp#L284-L297
- Rule file: `tools/spec-extract/rules/sema/borrow_check/logos.json`

### `borrow.generic.copy-bound-is-copy-type` — A type parameter is move unless it carries a Copy bound

- Divergence: B1
- Statement: In a generic body a bare type parameter `T` is move-classified for use-after-move tracking unless `T` is declared with a `Copy` bound, in which case its values are Copy and not consumed on use.
- Source: src/compiler/borrow_check.cpp#L3216-L3226
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part5.json`

## B2

Cross-ref: `B2` appears in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — registered.

### `layout.dyn.fat-pointer-data-vtable-pair` — dyn trait object is a 16-byte {data, vtable} fat pair by value

- Divergence: B2/B3: fat-pointer model for owned dyn; Box<dyn> is the owning trait object.
- Statement: `&dyn Trait`, `*dyn Trait`, and `Box<dyn Trait>` share a uniform 16-byte fat representation: a `{data_ptr, vtable_ptr}` pair stored inline. `data_ptr` is the concrete value's address (heap concrete for an owning `Box<dyn>`). The pair travels by value; escape consumers copy the 16 bytes into their own inline storage rather than holding a heap handle.
- Source: src/compiler/mlir_gen_dyn.cpp#L1204-L1234, src/compiler/mlir_gen_dyn.cpp#L1264-L1270
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_dyn/emit_static_globals.json`

### `type.struct.dst-tail-slice-last-field` — Custom-DST slice tail only at last field

- Divergence: B2: custom-DST tail-slice (DONE) — Logos supports `struct Foo { hdr: H, tail: [T] }`.
- Statement: A struct field may have an unsized slice type `[T]` only at the last field position; such a struct is marked dynamically-sized (DST). Unsized field types are otherwise rejected.
- Source: src/compiler/sema_collect.cpp#L4023-L4029, src/compiler/sema_collect.cpp#L4055-L4069
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_struct_spec.json`

## B6

Cross-ref: `B6` appears in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — registered.

### `borrow.nll.capture-flow-store` — Storing a borrowing argument into a receiver taints the receiver's provenance

- Divergence: B6: NLL E0597 via capture-flow on container-element stores.
- Statement: When a `&mut self` method is called on a tracked local receiver and a by-value borrow-carrying argument (or an argument whose ref-type equals the receiver container's element type, e.g. `Vec<&T>::push(&x)`) is stored into the receiver, the receiver transitively acquires the argument's borrow of the source local. A later use of the receiver after that source local dies is then E0597. `&self` reads and `&x` ref-args do not taint (so `v.contains(&x)`/`v.len()` stay clean).
- Source: src/compiler/borrow_check.cpp#L3563-L3604
- Rule file: `tools/spec-extract/rules/sema/borrow_check/visit.json`

### `borrow.nll.dangling-ref-first-use-error` — NLL E0597: a borrow outliving its referent errors at first later use

- Divergence: B6
- Statement: A reference/borrow-carrying binding that outlives a local it borrows becomes dangling when that local goes out of scope; this is not an error in itself — only the FIRST subsequent USE of the dangling binding is rejected (NLL: a stored borrow never used after its referent dies is accepted). A binding dying in the same scope as its source is always fine.
- Source: src/compiler/borrow_check.cpp#L765-L775, src/compiler/borrow_check.cpp#L878-L893
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part1.json`

## B8

Cross-ref: `B8` appears in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — registered.

### `mono.subst.assign-drop-old-preserved` — Drop-before-replace flag survives monomorphization

- Divergence: B8 (resolved — Rust-conformant)
- Statement: For an assignment `x = v` and for deref-write `*p = v`, the drop-before-replace flag (drop old contents iff the place is initialized) is carried verbatim through substitution.
- Source: src/compiler/mono_clone.cpp#L4406-L4412, src/compiler/mono_clone.cpp#L4536-L4542
- Rule file: `tools/spec-extract/rules/mono/mono_clone/walk.json`

## B72

Cross-ref: `B72` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `borrow.pass.region-conflict-diag` — Region inference reports overlapping-borrow conflicts

- Divergence: B72/B73: NLL region-based conflict diagnostics.
- Statement: Region inference runs before the lexical borrow check and shares the same declared `'a: 'b` outlives source; it reports a conflict when two borrows of the same target have overlapping live regions where at least one is mutable. The later borrow (by source line) is the offending one and the earlier is reported as the still-live borrow.
- Source: src/compiler/borrow_check.cpp#L3805-L3846
- Rule file: `tools/spec-extract/rules/sema/borrow_check/visit.json`

## B78

Cross-ref: `B78` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `borrow.move.partial-field-path` — Partial move tracks full dotted field paths

- Divergence: B78/T1-10: full dotted-path granularity (Rust-conformant partial moves).
- Statement: Moving a place `root.a.b...` of move type marks that exact path on `root` as moved. A subsequent use is an error if it overlaps the moved path: reading the same path, anything inside it, or any containing parent (including the whole value `root`). Disjoint sibling paths (e.g. `root.a.t` vs moved `root.a.s`) stay usable. A strict-parent read (`root.a` while `root.a.s` moved) errors only for a genuine whole-value read, not when it is merely an intermediate projection toward a disjoint deeper leaf (place-base position).
- Source: src/compiler/borrow_check.cpp#L3442-L3520
- Rule file: `tools/spec-extract/rules/sema/borrow_check/visit.json`

## B81

Cross-ref: `B81` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `borrow.conflict.mut-while-borrowed` — Cannot take &mut a place that is already borrowed

- Divergence: B81/B93.2: method-receiver/auto-borrow sites get the same path-aware conflict checks as explicit &mut.
- Statement: A new borrow of `root.path` conflicts when `root` (or an overlapping path) is already borrowed: a `&mut` borrow conflicts with any existing mutable borrow, with any existing shared borrow, and with any existing shared-field or mut-field borrow whose path overlaps `path`. Path overlap is prefix-or-equal in either direction.
- Source: src/compiler/borrow_check.cpp#L3393-L3424
- Rule file: `tools/spec-extract/rules/sema/borrow_check/visit.json`

## B82

Cross-ref: `B82` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `borrow.conflict.tpb-reservation-shared-read` — A mut reservation passed as a call argument tolerates concurrent shared reads of the same target

- Divergence: B82 (two-phase-borrow reservation compatible with shared reads)
- Statement: A two-phase-borrow (TPB) mut-reservation does not conflict with a concurrent shared (&) borrow of the same target: when one of a conflicting pair is a TPB reservation and the other is non-mut, no conflict is reported. A reservation still conflicts with any other mut borrow or reservation of the same target.
- Source: src/compiler/region_infer.cpp#L849-L854
- Rule file: `tools/spec-extract/rules/sema/region_infer/compute_liveness.json`

## B84

Cross-ref: `B84` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `type.subtype.rawptr-variance` — *const covariant, *mut invariant; mut/const mismatch is shape diff

- Divergence: B84 (compiler tag); Rust-conformant for *const/*mut variance
- Statement: Raw pointers carry no lifetime. *const T is covariant in pointee (*const T <: *const U iff T <: U); *mut T is invariant in pointee (*mut T <: *mut U iff T == U with lifetimes). A const-vs-mut pointer-kind mismatch is a shape difference, deferred to the compatibility check (subtype returns true).
- Source: include/logos/compiler/subtype.hpp#L226-L235
- Rule file: `tools/spec-extract/rules/sema/subtype_hpp/logos.json`

## B87

Cross-ref: `B87` appears in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — registered.

### `borrow.dropck.drop-binding-must-outlive-borrowed-local` — A Drop-having binding may not borrow a local that dies first

- Divergence: B87
- Statement: If a binding's type has a Drop impl and its declared type carries a lifetime parameter (dropck-relevant), then every local it borrows at construction must outlive the binding; a local going out of scope while such a binding still lives is rejected (the binding's Drop would run after the local dies).
- Source: src/compiler/borrow_check.cpp#L856-L877, src/compiler/borrow_check.cpp#L918-L933
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part1.json`

## B93

Cross-ref: `B93` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `borrow.autoborrow.method-receiver-transient` — Method-call receiver borrow is scoped to the call

- Divergence: B93.2/B94: auto-borrows are check-only and NLL-released, not recorded.
- Statement: A `&self`/`&mut self` method borrows its receiver for the duration of the call only; the implicit receiver borrow is released at the enclosing scope-pop (NLL), so consecutive calls `b.foo(); b.bar();` do not conflict. A bare-place receiver (VarRef/FieldRead, not an explicit AddrOfTemp) still incurs the whole-root conflict check: `&mut self` (kind 2) vs an outstanding borrow of the receiver root errors (iterator-invalidation, e.g. `let r=&v[i]; v.push(..)`).
- Source: src/compiler/borrow_check.cpp#L3344-L3433, src/compiler/borrow_check.cpp#L3543-L3562
- Rule file: `tools/spec-extract/rules/sema/borrow_check/visit.json`

## B97

Cross-ref: `B97` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `trait.assoc-const.inherent-allowed` — Inherent associated constants permitted

- Divergence: B97 (Logos extension over baseline; Rust-conformant feature)
- Statement: An inherent impl (no trait) may declare associated constants `const C: T = ...;`, registered under the target type.
- Source: src/compiler/sema_collect.cpp#L3296-L3308
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_type_alias.part3.json`

## E0503

Cross-ref: `E0503` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `borrow.path.access-vs-field-borrow` — Accessing a place conflicts with overlapping field borrows

- Divergence: E0503/E0505
- Statement: Accessing target.path is rejected if it overlaps a tracked mutable field-borrow of the same root; an exclusive access (whole move or partial move) additionally conflicts with any overlapping shared field-borrow, whereas a plain read conflicts only with a mutable field-borrow.
- Source: src/compiler/borrow_check.cpp#L1131-L1165
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part1.json`

## G154-5

Cross-ref: `G154-5` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `stmt.let-else.literal-pattern-refutable` — Literal/bool/range pattern in let-else is refutable

- Divergence: G154-5: comment notes non-matching literal `let 4 = x else {…}` was previously silently accepted; now correctly refutable.
- Statement: For a non-enum integer scrutinee, an `Int` or `Bool` pattern matches iff the scrutinee equals the literal; a `Range` pattern (`lo..=hi`) matches iff `lo <= scrut <= hi` (signed). On mismatch the else block runs.
- Source: src/compiler/mlir_gen_stmt.cpp#L4921-L4950
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_stmt/gen_stmt_kind.json`

## G156-1

Cross-ref: `G156-1` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `mono.assoc.suffixed-projection-resolution` — Associated types resolve per trait type-args when a type has multiple impls of one trait

- Divergence: G156-1: addresses two same-trait impls at distinct type-args; tracked as a known narrow area.
- Statement: When a type has multiple impls of one parameterized trait at distinct type-args (e.g. two `Trait<T>` impls), an associated-type projection `<P as Trait<i64>>::A` resolves to the impl matching the trait's concrete type-args (via a type-args suffix key); a bare projection resolves first-wins.
- Source: src/compiler/mono.cpp#L251-L277
- Rule file: `tools/spec-extract/rules/mono/mono/logos.json`

### `trait.assoc-type.dual-impl-ambiguous-projection` — Ambiguous bare associated-type projection across generic-trait impls

- Divergence: G156-1: Rust requires fully-qualified `<X as Trait<T>>::Assoc` for ambiguous projections; Logos matches by erasing the ambiguous bare key.
- Statement: When two impls of a generic trait Trait<T> for one target at distinct T each declare the same associated type, the bare projection `X::Assoc` becomes ambiguous and must be written `<X as Trait<T>>::Assoc`; the unsuffixed projection key is first-impl-wins and is erased once a second distinct-args impl appears so a bare lookup fails.
- Source: src/compiler/sema_collect.cpp#L3235-L3248, src/compiler/sema_collect.cpp#L3281-L3295
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_type_alias.part3.json`

### `type.assoc-ref.eager-concrete-projection` — Eager projection for concrete base with generic trait

- Divergence: G156-1 disambiguation of multiple Trait<T> impls.
- Statement: When the base is a concrete type and the resolved trait is generic (has type-args), the projection is resolved immediately by looking up the trait+args-suffixed assoc-type impl and substituting the base's type-args; this disambiguates two `Trait<T>` impls on one type that would otherwise intern to a single trait-arg-less deferred node and collapse.
- Source: src/compiler/sema.cpp#L5275-L5307
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_type_cfg_slot.json`

## G160-3

Cross-ref: `G160-3` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `trait.impl.default-on-scalar-self` — Self bound to scalar primitive for default-method bodies

- Divergence: G160-3 (Logos implementation note; Rust-conformant)
- Statement: For `impl Trait for <scalar primitive>` (i8..i128/u8..u128/usize/isize/f32/f64/bool/char), Self resolves to the primitive so inherited default method bodies using `&Self` typecheck; this binding is restricted to scalar kinds and excludes `str` and enum targets, whose defaults keep Self as a type variable.
- Source: src/compiler/sema_collect.cpp#L3545-L3567
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_type_alias.part3.json`

## P4

Cross-ref: `P4` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `stmt.let.ref-binding-sugar` — `let ref y = x` is sugar for `let y = &x`

- Divergence: P4-pm-14
- Statement: A `ref` binding `let ref y = x` (or `let ref y: T = x`) lowers to taking the address of the RHS, giving `y` type `&T`.
- Source: src/compiler/sema_stmt.cpp#L1897-L1931
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/lower_let_else.json`

## T2-15

Cross-ref: `T2-15` does NOT appear in [`docs/DIVERGENCES.md`](../DIVERGENCES.md) — **unregistered — needs triage**.

### `mono.operator.unary-overload` — Unary operator on struct dispatches to overload method

- Divergence: T2-15: the logical-not overload method is named `not` (not Rust `Not::not` symbol but conformant in spirit); note earlier name was `not_`.
- Statement: After substitution, a unary operator whose operand is a struct is rewritten to a call to the struct's operator method: `-` -> `neg`, `!` -> `not`; resolved on the concrete struct name within the struct's package.
- Source: src/compiler/mono_clone.cpp#L1002-L1024
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part2.json`

## Logos-specific additions

Logos-only features / behaviors with no direct Rust equivalent. Most fall under blessed §A6 (additions) in `docs/DIVERGENCES.md`; listed here at rule granularity.

### `borrow.scoped.rc-arc-root-exempt` — Self-borrowing method results on Rc/Arc roots do not hold a receiver borrow

- Divergence: Logos-specific exemption for Rc/Arc receivers (residency-escape / interior-mutability pattern).
- Statement: When a self-borrowing method's bare-VarRef receiver roots at an Rc or Arc value, no scoped receiver borrow is recorded: shared-ownership handles are the blessed interior-mutability domain, so `h.array()` followed by `hold(&mut h, root)` is permitted.
- Source: src/compiler/borrow_check.cpp#L2160-L2199
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part3.json`

### `coerce.cast.int-null-to-trait-object` — Integer (null) cast to trait object yields zeroed fat pair

- Divergence: Logos uniform-fat model: `*mut dyn`/`&dyn` are both 16-byte {data,vtable}; integer-to-dyn null cast is a Logos extension for null sentinels (no Rust analog).
- Statement: `E as T` where T is a trait object (`*mut dyn`/`&dyn`) and E has an integer type (IntLit/i32/u32/i64/u64/isize/usize) produces a 16-byte {data,vtable} fat pair with both halves null. This makes null-handle sentinels (`0 as *mut dyn`) and `… as *mut u64 == 0` null checks behave under the uniform-fat dyn model.
- Source: src/compiler/mlir_gen_expr.cpp#L3170-L3193
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_expr/gen_expr_kind-2.json`

### `coerce.ref.widen-int-literal-temp-pointee` — &<int-literal> sizes its temporary to the expected pointee width

- Divergence: Logos-specific miscompile fix (G149-2); Rust never has this hazard since &literal infers the pointee type.
- Statement: When `&L` (an address-of an integer literal materialized as a temporary) is passed where `&T`/`&mut T` is expected and T is a wider integer type that L can widen to or fits, the inner literal is cast to T before being addressed, so the temporary's storage slot is sized to T (preventing a narrow temp read as the wider pointee).
- Source: src/compiler/sema_impl.hpp#L4365-L4400
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/logos.part8.json`

### `coerce.writ-anyval.scalar-helpers` — Implicit coercion of comprehension element to AnyVal

- Divergence: Logos-specific Writ value model.
- Statement: Inside a Writ comprehension element/value, the value is coerced to AnyVal: WAny and legacy AnyVal struct values pass through unchanged; bool/i8/i16/i32/IntLit/u8/u16/u32 are wrapped via the matching `writ_coerce_<ty>` helper; `str` (`&[u8]`) is wrapped via `writ_coerce_str` (taking `&ctr` first). Any other type is rejected with a message to cast explicitly or wrap with AnyVal::embed_*.
- Source: src/compiler/sema_expr.cpp#L11382-L11458
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_list_comp.json`

### `coerce.writ-anyval.wide-int-no-implicit` — Wide integers not implicitly coerced to AnyVal

- Divergence: Logos-specific anti-truncation rule.
- Statement: i64/u64/i24/u24/i56/u56/i128/u128 are intentionally NOT auto-coerced to AnyVal (implicit i32 embedding would silently truncate); the user must cast explicitly (`x as i32`) or wrap with WAny::from.
- Source: src/compiler/sema_expr.cpp#L11418-L11427, src/compiler/sema_expr.cpp#L11430-L11436
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_list_comp.json`

### `const.literal.integer-suffix-by-kind` — integer constant suffix by type kind

- Divergence: Logos has additional integer widths I24/U24/I56/U56 beyond Rust's fixed set.
- Statement: An integer constant carries a type suffix matching its kind (i8/i16/i32/i64/u8/u16/u32/u64); IntLit and the non-power-of-two-byte kinds I24/U24/I56/U56 are emitted unsuffixed. Signedness is determined by the kind (signed: i8/i16/i24/i32/i56/i64/i128/IntLit).
- Source: src/compiler/sema_render.cpp#L992-L1016
- Rule file: `tools/spec-extract/rules/sema/sema_render/render_stmt_src.json`

### `divergence.heap.no-class-new-delete` — No C++-style class/new/delete

- Divergence: Logos addition/removal vs C++; Rust-conformant (Rust also has no class/new/delete).
- Statement: The language has no `class` declaration, `new` expression, or `delete` statement. Heap allocation is expressed via `Box` (and other library owning types), not a built-in `new`/`delete` pair.
- Source: tools/peg_gen/grammars/logos.peg#L165-L166
- Rule file: `tools/spec-extract/rules/grammar/logos/nodes.json`

### `expr.arr-fill.size-metacall` — Array fill length via metacall splice

- Divergence: Logos explicit-metacall model replaces Rust const-expression array lengths.
- Statement: `[v; metacall { <expr> }]` evaluates the block's tail expression by compile-time evaluation (CTFE), and the integer result becomes the array length. The metacall block must contain an integer tail expression. This is Logos's replacement for Rust const-eval at the array-length position.
- Source: src/compiler/sema_expr.cpp#L11486-L11516
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_list_comp.json`

### `expr.arr-fill.size-sizeof-pack` — Array fill length via sizeof...(P)

- Divergence: Logos variadic-pack feature.
- Statement: `[v; sizeof...(P)]` where P is an in-scope type parameter yields a single-element array literal whose length is symbolic (`__sizeof_pack:P`); monomorphization repeats the element to the variadic pack's expanded length. Any spread operator other than `sizeof` is rejected; an undefined P is an error.
- Source: src/compiler/sema_expr.cpp#L11468-L11485
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_list_comp.json`

### `expr.assign.deref-write` — Dereference write statement

- Divergence: Logos addition: distinct DEREF_WRITE/DEREF_COMPOUND statement forms; semantics match Rust place-expression assignment.
- Statement: `* p = v ;` writes value `v` through dereferenced place `p` (a `unary_expr`). `* p OP v ;` performs compound assignment through a bare dereference and is defined to lower to `*p = *p OP v`.
- Source: tools/peg_gen/grammars/logos.peg#L2335-L2340
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-compound_assign_op.json`

### `expr.closure.ref-bind-param` — `|ref x: T|` binds x as &T

- Divergence: Logos closure ref-binding param syntax; no direct Rust equivalent.
- Statement: A closure parameter written `ref x: T` (IS_REF with an explicit TYPE) takes its argument by value of type T under a synthetic name and binds the user-visible `x` to `&T` aliasing the synthetic param. IS_REF without a TYPE is the `&self`/`&mut self` shorthand, not a ref-bind.
- Source: src/compiler/sema_expr.cpp#L14191-L14206, src/compiler/sema_expr.cpp#L14257-L14259, src/compiler/sema_expr.cpp#L14304-L14311
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_block_expr.part2.json`

### `expr.comprehension.list-and-map` — List and map comprehensions

- Divergence: Logos addition: Python-style comprehensions; not present in Rust.
- Statement: List comprehension `[expr for x in iter (if pred)?]` and map comprehension `{kexpr: vexpr for x in iter (if pred)?}` produce a collection by iterating `iter`, binding `x`, optionally filtering by `pred`.
- Source: tools/peg_gen/grammars/logos.peg#L2875-L2885
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-compound_assign_op.json`

### `expr.ctor.variant-alias-shorthand` — Bare enum-variant constructor via use-alias

- Divergence: Logos `use Type.{V}` variant-import surface (pkg `.` / item `::` path model)
- Statement: A `use Enum.{V, …};` import registers variant aliases; a bare call `V(payload)` whose name is an imported variant alias constructs that enum's variant `V` (typed via enum-literal lowering with payload typing), when no function of that name resolved.
- Source: src/compiler/sema_expr.cpp#L5943-L5953
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_generic_call.json`

### `expr.drop.flag-uninit-conditional` — Conditionally/late-initialized variables drop only when live

- Divergence: Logos drop flags / static drop tracking (B8). Models Rust's conditional drop flags.
- Statement: A variable that may be uninitialized at a drop point runs its destructor only if it currently holds a live value. With dynamic tracking a per-variable drop flag (0/1) is consulted at runtime (flag==1 → drop, else no-op). With static tracking the destructor is emitted only when the variable is statically known to be assigned at that point; an early return before first assignment, the !c arm of a conditional init, or a never-assigned variable drops nothing.
- Source: src/compiler/mlir_gen_stmt.cpp#L1184-L1214
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_stmt/gen_drop_owning_dst.json`

### `expr.list-comp.desugar-vec` — List comprehension desugars to Vec build loop

- Divergence: Logos-specific surface syntax (Python-style comprehension); not present in Rust.
- Statement: A list comprehension `[value for x in iter (if guard)?]` desugars to a block that binds `let mut v: Vec<T> = vec_new::<T>()`, iterates `x` over `iter`, (optionally gated by `guard`) calls `Vec::push(&mut v, value)`, and evaluates to `v`. T is the iterator element type; the block's type is `Vec<T>`.
- Source: src/compiler/sema_expr.cpp#L10885-L10986
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_list_comp.json`

### `expr.list-comp.requires-vec-import` — List comprehension requires Vec in scope

- Divergence: Logos-specific: surface sugar depends on a stdlib import being present.
- Statement: A list comprehension is ill-formed unless the `Vec` struct and the generic `vec_new` function are visible (via `use logos.mem.collections.vec;`).
- Source: src/compiler/sema_expr.cpp#L10909-L10921
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_list_comp.json`

### `expr.map-comp.desugar-hashmap` — Map comprehension desugars to HashMap build loop

- Divergence: Logos-specific surface syntax; not present in Rust.
- Statement: A map comprehension `{key: value for x in iter (if guard)?}` desugars to a block that binds `let mut m: HashMap<K,V> = hashmap_new::<K,V>()`, iterates `x` over `iter`, (optionally gated by `guard`) calls `HashMap::insert(&mut m, key, value)`, and evaluates to `m`. K = type of `key`, V = type of `value`; block type is `HashMap<K,V>`.
- Source: src/compiler/sema_expr.cpp#L10992-L11090
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_list_comp.json`

### `expr.map-comp.requires-hashmap-import` — Map comprehension requires HashMap in scope

- Divergence: Logos-specific.
- Statement: A map comprehension is ill-formed unless the `HashMap` struct and the generic `hashmap_new` function are visible (via `use logos.mem.collections.hashmap;`).
- Source: src/compiler/sema_expr.cpp#L11015-L11026
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_list_comp.json`

### `expr.match.writ-pattern-needs-view` — Writ patterns require a view scrutinee

- Divergence: Logos extension: Writ structured-data pattern matching (not in Rust).
- Statement: A match arm containing a Writ scalar pattern (PAT_WRIT_NULL/BOOL/INT/STR/MAP/ARR/TYPED_ARR/TYPED_MAP, including inside an or-pattern) requires the scrutinee to be a Writ view (Writ, WritView, or WritStatic; use `&` to borrow); otherwise a diagnostic is emitted.
- Source: src/compiler/sema_stmt.cpp#L8961-L9003
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/lower_match_expr.json`

### `expr.raw-ptr.is-null-safe` — Raw-pointer is_null is safe; user impl wins

- Divergence: Logos lets an inherent fn on the pointee shadow the built-in pointer is_null.
- Statement: On a raw-pointer receiver, `p.is_null()` is safe (no dereference), takes zero arguments, and lowers to `(p as i64) == 0 : bool` — UNLESS the pointee type declares an inherent `is_null` method, in which case that user-defined method is dispatched instead.
- Source: src/compiler/sema_expr.cpp#L6661-L6692
- Rule file: `tools/spec-extract/rules/sema/sema_expr/track_args_moved.json`

### `expr.str.as-bytes-identity` — &str.as_bytes() is the identity

- Divergence: Logos models &str as Slice<u8> (writ/string-repr); identity conversion.
- Statement: Because `&str` is represented as `Slice<u8>` (same fat-pointer ABI as `&[u8]`), `s.as_bytes()` on a `Slice<u8>` receiver returns the receiver verbatim with no conversion.
- Source: src/compiler/sema_expr.cpp#L6472-L6481
- Rule file: `tools/spec-extract/rules/sema/sema_expr/track_args_moved.json`

### `expr.struct-lit.anyval-raw-constructor` — AnyVal struct-literal constructor

- Divergence: Logos built-in; struct-literal syntax over a scalar type
- Statement: `AnyVal { raw: e }` is a valid constructor expression that yields the scalar value of `e`. The literal must contain exactly one field named `raw`; any other field set is rejected.
- Source: src/compiler/mlir_gen.cpp#L885-L903
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen/get_struct_ptr.json`

### `expr.writ-capture.capturable-types` — Set of types capturable in an @-literal

- Divergence: Logos addition: @-literal (Writ) capture has no Rust analogue.
- Statement: A value may be captured into an @-literal iff its type is one of: integer scalars i8/i16/i32/i64/u8/u16/u32/u64, bool (→ inline AnyVal); f64/f32/FloatLit (→ zone-allocated F64, type_code 31); AnyVal (passthrough) or StringView (→ varchar) struct types; `*const u8`/`*mut u8` (→ C-string varchar); or `str`/`&[u8]` slice of u8 (→ length-bearing varchar). All other types are rejected.
- Source: src/compiler/sema_expr.cpp#L15325-L15350, src/compiler/sema_expr.cpp#L15360-L15367, src/compiler/sema_expr.cpp#L15387-L15394
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part2.json`

### `expr.writ-list-comp.desugar` — Writ list comprehension desugars to a Writ array builder loop

- Divergence: Logos-specific Writ data-substrate sugar; no Rust equivalent.
- Statement: A writ list comprehension `@[value for x in iter (if guard)?]` desugars to a block that binds `let mut c = writ_list_comp_new(cap_hint)` (yielding the builder's return type, e.g. Rc<Writ>), iterates `x` over `iter`, coerces `value` to AnyVal, (optionally gated by `guard`) calls `writ_list_comp_push(&c, value)`, and evaluates to `c`. cap_hint = arr_size*8+128 for arrays of known size, else 128.
- Source: src/compiler/sema_expr.cpp#L11098-L11226
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_list_comp.json`

### `expr.writ-list-comp.requires-builder-import` — Writ list comprehension requires comp_builder import

- Divergence: Logos-specific.
- Statement: A writ list comprehension is ill-formed unless arity-1 `writ_list_comp_new` and arity-2 `writ_list_comp_push` are visible (via `use logos.lang.writ.comp_builder;`).
- Source: src/compiler/sema_expr.cpp#L11125-L11135
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_list_comp.json`

### `expr.writ-lit.value-kinds` — Writ literal value kinds and their encodings

- Divergence: Logos addition (Writ SDN literals); no Rust equivalent.
- Statement: A Writ SDN literal value is one of: null; bool (0/1); int (see int encoding); float (boxed f64); string; array (homogeneous scalar arrays I8..F64 use a typed array, otherwise an object array); map (integer-keyed I32/U32/I64/U64 use a typed map, otherwise an object map keyed by string); type (a tiny map carrying kind/uid/name); or capture/PARAM (an inline placeholder bound to a value index, substituted at runtime).
- Source: src/compiler/mlir_gen_expr.cpp#L5759-L5882, src/compiler/mlir_gen_expr.cpp#L5820-L5882
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_expr/gen_expr_kind-5.json`

### `expr.writ-map-comp.desugar` — Writ map comprehension desugars to a Writ object-map builder loop

- Divergence: Logos-specific Writ sugar; no Rust equivalent.
- Statement: A writ map comprehension `@{key: value for x in iter (if guard)?}` desugars to a block that binds `let mut c = writ_map_comp_new(cap_hint, slot_hint)`, iterates `x` over `iter`, coerces `value` to AnyVal, (optionally gated by `guard`) calls `writ_map_comp_put(&c, key, value)`, and evaluates to `c`. slot_hint = arr_size (else 64); cap_hint = arr_size*48+256 (else 4096).
- Source: src/compiler/sema_expr.cpp#L11231-L11375
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_list_comp.json`

### `expr.writ-map-comp.key-must-be-str` — Writ map comprehension key must be str

- Divergence: Logos-specific (v1 limitation: string keys only).
- Statement: In a writ map comprehension v1 the `key` expression must have type `str` (a `&[u8]` slice with u8 element); any other key type is rejected.
- Source: src/compiler/sema_expr.cpp#L11285-L11296
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_list_comp.json`

### `expr.writ-map-comp.requires-builder-import` — Writ map comprehension requires comp_builder import

- Divergence: Logos-specific.
- Statement: A writ map comprehension is ill-formed unless arity-2 `writ_map_comp_new` and arity-3 `writ_map_comp_put` are visible (via `use logos.lang.writ.comp_builder;`).
- Source: src/compiler/sema_expr.cpp#L11258-L11268
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_list_comp.json`

### `expr.writ.array` — Writ untyped array literal

- Divergence: Logos addition (Writ literals).
- Statement: An untyped Writ array `@[...]` lowers each element as a recursive Writ value in order.
- Source: src/compiler/sema_expr.cpp#L15131-L15143
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.bool` — Writ bool literal

- Divergence: Logos addition (Writ literals).
- Statement: A Writ bool node yields a boolean Writ value; the value is true iff its byte payload is present and nonzero.
- Source: src/compiler/sema_expr.cpp#L15021-L15025
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.capturable-types` — Types capturable by $-capture into a Writ value

- Divergence: Logos addition (Writ captures).
- Statement: A captured Logos expression is admissible into a Writ @-literal iff its type is: a scalar integer (i8/i16/i32/i64/u8/u16/u32/u64) or bool (coerced to inline AnyVal); F32/F64/float-literal (zone-allocated F64); AnyVal or a string-view struct; a pointer to u8 (*const u8 / *mut u8, captured as C-string varchar); or a u8 slice (str/&[u8], captured as varchar with length). Other types are not capturable.
- Source: src/compiler/sema_expr.cpp#L15325-L15350
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.capture-outside-context` — $-capture only inside capturable @-literal

- Divergence: Logos addition (Writ captures).
- Statement: A $-capture ($ident or $expr) in a Writ value is a compile error unless it occurs inside a capturable @-literal context.
- Source: src/compiler/sema_expr.cpp#L15319-L15323
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.cfg-slot-type` — WritStatic const-generic slot type

- Divergence: Logos-specific const-generic/Writ syntax.
- Statement: A slot of a WritStatic-typed const-generic is referenced as `<type:CFG.slot.path>` with dot-separated step names.
- Source: src/compiler/sema_render.cpp#L517-L531
- Rule file: `tools/spec-extract/rules/sema/sema_render/logos.json`

### `expr.writ.cfg-slot-type-literal` — <type:CFG.path> at writ-value position

- Divergence: Logos addition (Writ/CFG type literals).
- Statement: `<type:CFG.path>` resolves the config path eagerly and must denote a concrete top-level alias; if it resolves to a const-generic config-slot parameter (kind CfgSlotType) it is rejected with a compile error (parametric Writ literals are not supported).
- Source: src/compiler/sema_expr.cpp#L14982-L15009
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.embedded-type-lit` — Embedded type in Writ literal

- Divergence: Logos-specific Writ syntax.
- Statement: A Logos type can be embedded inside a Writ literal as `<type:T>`.
- Source: src/compiler/sema_render.cpp#L510-L516
- Rule file: `tools/spec-extract/rules/sema/sema_render/logos.json`

### `expr.writ.float-suffix` — Writ float literal: suffix stripping

- Divergence: Logos addition (Writ literals).
- Statement: A Writ float literal accepts an optional `f32` or `f64` suffix which is stripped before parsing the value as a double-precision float.
- Source: src/compiler/sema_expr.cpp#L15052-L15060
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.int-suffix-and-radix` — Writ integer literal: suffix stripping and radix

- Divergence: Logos addition (Writ literals); note i24/i56/u24/u56 width suffixes.
- Statement: A Writ integer literal accepts an optional numeric-type suffix (i8/i16/i24/i32/i56/i64/i128, u8/u16/u24/u32/u56/u64/u128, usize, isize) which is stripped before parsing, an optional leading '-', and a radix prefix: `0x` = hexadecimal, `0b` = binary, otherwise decimal. The resulting magnitude is negated if the sign was present.
- Source: src/compiler/sema_expr.cpp#L15027-L15050
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.map-entry-colon` — Writ map entry syntax

- Divergence: Logos-specific Writ syntax.
- Statement: A Writ map literal `@{ ... }` contains comma-separated entries `key: value`; nested scalar values omit the `@` prefix in inner position.
- Source: src/compiler/sema_render.cpp#L479-L497
- Rule file: `tools/spec-extract/rules/sema/sema_render/logos.json`

### `expr.writ.map-keys` — Writ map literal keys (string or integer)

- Divergence: Logos addition (Writ literals).
- Statement: An untyped Writ map `@{...}` has entries whose key is either a quoted string (quote-stripped and escape-processed like a Writ string) or an integer; an integer key is negated when the entry carries the negative-key marker. Values are recursively lowered Writ values.
- Source: src/compiler/sema_expr.cpp#L15088-L15129
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.neg-int` — Writ negative integer literal

- Divergence: Logos addition (Writ literals).
- Statement: A Writ negative-integer node yields an integer Writ value equal to the negation of the parsed decimal magnitude.
- Source: src/compiler/sema_expr.cpp#L15012-L15016
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.null` — Writ null literal

- Divergence: Logos addition (Writ literals).
- Statement: A Writ null node yields the null Writ value.
- Source: src/compiler/sema_expr.cpp#L15018-L15019
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.outer-at-prefix` — Writ literal outer `@` prefix

- Divergence: Logos-specific Writ data-literal syntax; no Rust equivalent.
- Statement: Writ (data) literals in expression position are introduced with a leading `@`: `@null`, `@true`/`@false`, `@INT`, `@-INT`, `@FLOAT`, `@"str"`, `@{ ... }` (map), `@[ ... ]` (array).
- Source: src/compiler/sema_render.cpp#L463-L509
- Rule file: `tools/spec-extract/rules/sema/sema_render/logos.json`

### `expr.writ.sdn-literal` — Writ SDN literals

- Divergence: Logos addition: Writ self-describing data-notation literals.
- Statement: Writ structured-data literals use the `@` sigil: `@{k:v,…}` map, `@[v,…]` array, `@"s"` string, `@42`/`@-1` int, `@<float>` float, `@true`/`@false` bool, `@null`. Typed forms `@<Elem>[…]` (dense array) and `@<K,V>{…}` / `@<K>{…}` (typed map). Comprehension forms `@[expr for x in iter (if p)?]` and `@{k:v for …}`. Only the outermost literal needs the `@` sigil; inner values are plain.
- Source: tools/peg_gen/grammars/logos.peg#L2887-L2923
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-compound_assign_op.json`

### `expr.writ.string-escapes` — Writ string literal: quote stripping and escapes

- Divergence: Logos addition (Writ literals); escape set is a fixed subset.
- Statement: A Writ string literal has surrounding double-quotes stripped and recognizes escape sequences \n, \t, \r, \\, \", \0; an unrecognized escape `\x` is kept literally as backslash followed by x.
- Source: src/compiler/sema_expr.cpp#L15062-L15086
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.type-literal` — Writ type-literal <type:T>

- Divergence: Logos addition: Writ first-class type values have no Rust equivalent.
- Statement: A Writ value `<type:T>` embeds a Logos type T as a first-class value. T is resolved as a type (primitives, structs, in-scope type-params, and generic instantiations like Vec<u8> all permitted). The value carries (kind, type-uid, canonical-name) where the name is the canonical printed form (e.g. "Vec<u8>") and serves as the value's identity label.
- Source: src/compiler/sema_expr.cpp#L14937-L14979
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.type-literal-unknown-bare` — Bare type-name in <type:T> must be a known type or in-scope type-param

- Divergence: Logos addition (Writ type literals).
- Statement: When `<type:T>` names a bare type identifier that is neither a resolvable known type nor an in-scope type-param, it is a compile error; the diagnostic directs the user to declare T as a type-param of the enclosing const (`pub const X<T>: WritStatic = ...`) or use a concrete type.
- Source: src/compiler/sema_expr.cpp#L14954-L14966
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.typed-array-elem-types` — Typed Writ array element types

- Divergence: Logos addition (Writ literals).
- Statement: A typed Writ array `@<E>[...]` requires E to be one of I8, U8, I16, U16, I32, U32, I64, U64, F32, F64; any other element type is a compile error.
- Source: src/compiler/sema_expr.cpp#L15145-L15168
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.typed-array-i32-bounds` — @<I32> array element range check

- Divergence: Logos addition (Writ literals).
- Statement: Each integer element of an `@<I32>[...]` typed array is bounds-checked at compile time to the i32 range [-2147483648, 2147483647]; out-of-range values are a compile error.
- Source: src/compiler/sema_expr.cpp#L15190-L15203
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.typed-array-no-captures` — Typed Writ arrays reject $-captures

- Divergence: Logos addition (Writ literals/captures).
- Statement: Within a typed Writ array `@<E>[...]`, a $-capture element ($ident or $expr) is a compile error because typed arrays store raw element values rather than AnyVal; an untyped `@[...]` literal must be used instead.
- Source: src/compiler/sema_expr.cpp#L15174-L15187
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.typed-map-key-discipline` — Typed integer-map key discipline

- Divergence: Logos addition (Writ literals).
- Statement: In a typed integer-keyed Writ map, a string key is a compile error (integer maps require integer keys); integer keys are negated when marked negative, and are bounds/sign-checked per key type: I32 to [-2^31, 2^31-1], U32 to [0, 2^32-1], U64 to non-negative.
- Source: src/compiler/sema_expr.cpp#L15255-L15311
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `expr.writ.typed-map-types` — Typed Writ map key/value types

- Divergence: Logos addition (Writ literals).
- Statement: A typed Writ map `@<K>{...}` or `@<K,V>{...}` requires K ∈ {I32, U32, I64, U64, Varchar} and, if V is given, V == AnyVal; any other key or value type is a compile error. Varchar keys produce the same representation as the untyped object map.
- Source: src/compiler/sema_expr.cpp#L15209-L15252
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part1.json`

### `generic.bound.lifetime-arg-not-structural` — Lifetime args in trait bounds are recorded but not dispatched on

- Divergence: Logos does not track regions structurally for bound dispatch; lifetime bound-args carry no dispatch significance.
- Statement: A lifetime argument at a trait bound's type-argument position (e.g. `Foo<'a>`) is captured for record only; regions are not tracked structurally for bound dispatch.
- Source: src/compiler/sema.cpp#L4034-L4041
- Rule file: `tools/spec-extract/rules/sema/sema/finalize_relaxed_bounds.json`

### `generic.call.antiquot-pack-type-arg` — Type-arg antiquote pack splices a reflected type list

- Divergence: Logos metaprog reflection extension (no Rust analogue)
- Statement: An antiquote pack `$v...` in a generic call's type arguments splices a runtime-produced list of types (e.g. a struct's field types) into the callee's type args; it is carried as a marker TypeVar `__splicepack$v` that flows like a variadic pack and is expanded during monomorphization by chasing the variable to its type-list producer.
- Source: src/compiler/sema_expr.cpp#L5985-L6000
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_generic_call.json`

### `grammar.expr.call-package-qualified` — Package-qualified free-function call

- Divergence: Logos path model: '.'-separated package path + '::'-item (vs Rust all-'::').
- Statement: A call 'IDENT path_dot_ident+ '::' IDENT ('::' '<' type_arg_list '>')? '(' call_arg_list? ')'' resolves a free fn by its dotted package path (RECEIVER = first segment, QUAL_PARTS = rest); this disambiguates same-named free fns across packages (e.g. logos.lang.mem::replace vs logos.lang.ptr::replace).
- Source: tools/peg_gen/grammars/logos.peg#L3191-L3203
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `grammar.generic.type-param-forms` — Type parameter forms

- Divergence: Logos additions: variadic type/const params ('...'), metavar params ('#'), repeat-group expansion (no Rust equivalent).
- Statement: type_param admits: lifetime_param; 'IDENT: lifetime_param (+ lifetime_param)*' (type-outlives); ptr/arr specialisation patterns; const params 'const IDENT: T', 'const IDENT...: T' (variadic), 'const #IDENT: T'; variadic type param 'IDENT... (: bounds)?'; metavar '#IDENT (: bounds)?'; 'IDENT: bounds (= default)?'; 'IDENT = default'; or bare 'IDENT'. A repeat-group '#(type_param), *' expands variadically.
- Source: tools/peg_gen/grammars/logos.peg#L3147-L3181
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `intrinsic.args-count-of.arg-count` — args_count_of yields generic-arg count

- Divergence: Logos addition.
- Statement: `args_count_of::<T>()` requires one type argument and yields `i64` = number of T's generic type arguments (0 for primitive or non-generic struct).
- Source: src/compiler/sema_expr.cpp#L5213-L5233
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.args-of.type-arg-array` — args_of yields generic type arguments

- Divergence: Logos addition.
- Statement: `args_of::<T>()` requires one type argument and yields `[Type; N]` listing T's generic type arguments; for non-generic T the result is `[Type; 0]`. The array length is fixed at mono once T is concrete.
- Source: src/compiler/sema_expr.cpp#L5185-L5211
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.bits.u64-bit-ops` — u64 bitwise intrinsics

- Divergence: Logos addition: explicit free-function bit-op intrinsics.
- Statement: `popcount_u64`, `leading_zeros_u64`, `trailing_zeros_u64` each take 1 u64 argument and return u32; `bswap_u64`, `bitreverse_u64` each take 1 u64 argument and return u64. Wrong arity is an error. (Lower to the corresponding LLVM intrinsics; ctlz/cttz are non-poison at zero.)
- Source: src/compiler/sema_expr.cpp#L3186-L3204
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_unary.part2.json`

### `intrinsic.dst-from-raw-parts.unsafe` — dst_from_raw_parts requires unsafe and a custom-DST struct

- Divergence: Logos addition (custom-DST construction intrinsic).
- Statement: `dst_from_raw_parts::<S>(ptr, len)` (and `_mut`) requires unsafe context, exactly one type argument S that is a (Zoned)Struct whose last field resolves to `[T]` or `dyn Trait` (directly is_dst or via type-parameter substitution), and exactly two value arguments.
- Source: src/compiler/sema_expr.cpp#L4740-L4802
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_intrinsic_has_trait_of.json`

### `intrinsic.dyn-from-parts.fat-trait-ptr` — dyn_from_parts builds a trait object from raw halves

- Divergence: Logos addition.
- Statement: `dyn_from_parts::<Trait>(data: *mut u8, vtable: *const u8) -> *mut dyn Trait` forms a fat {data, vtable} trait-object pointer. Exactly one trait type argument (its own type-args, if any, are carried so the produced object matches a parameterized `dyn Trait<...>` annotation, skipping lifetime/auto-trait bound sub-nodes) and exactly two value arguments are required. Trait must be a known, object-safe trait. The result is the bare canonical TraitObject (matching `*mut dyn`/`&dyn`), not a thin pointer.
- Source: src/compiler/sema_expr.cpp#L5314-L5391
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.field-count-of.struct-field-count` — field_count_of yields struct field count

- Divergence: Logos addition.
- Statement: `field_count_of::<T>()` requires one type argument and yields `i64` = number of declared fields of struct T (0 for non-struct or unknown-struct T).
- Source: src/compiler/sema_expr.cpp#L5562-L5582
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.field-reflect.types-and-names` — field_types_of / field_names_of reflect struct fields

- Divergence: Logos addition.
- Statement: `field_types_of::<T>()` yields `[Type; N]` of T's field types and `field_names_of::<T>()` yields `[&[u8]; N]` of T's field names; each requires one type argument; non-struct T yields empty arrays. At mono field types are substituted via the SubstMap built from the struct template's type_params -> T.type_args().
- Source: src/compiler/sema_expr.cpp#L5584-L5613
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.generic-of.signature` — generic_of requires a bare struct/enum name

- Divergence: Logos addition (compile-time reflection intrinsic).
- Statement: `generic_of::<X>()` requires its single type-argument to be a bare named struct or enum (a TYPE_REF or GENERIC_INST with a NAME); the name must resolve to a declared struct or enum in the current program, otherwise a compile error.
- Source: src/compiler/sema_expr.cpp#L4517-L4551
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_intrinsic_has_trait_of.json`

### `intrinsic.generic-of.unapplied-ctor` — generic_of yields a handle for an unapplied generic constructor

- Divergence: Logos addition.
- Statement: `generic_of::<X>()` yields a Type-shaped value-handle for the unapplied generic constructor X (struct or enum) with kind=Generic, name=X, size=arity, and UID = FNV-1a of "generic:X".
- Source: src/compiler/sema_expr.cpp#L5615-L5619
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.get-annotation.option-result` — get_annotation yields the annotation instance as Option<A>

- Divergence: Logos addition.
- Statement: `get_annotation::<T, A>() -> Option<A>` const-folds to `Some(A{...})` if datatype T carries annotation A, else `None`.
- Source: src/compiler/sema_expr.cpp#L5825-L5827
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.get-annotation.signature` — get_annotation arity and annotation-type constraint

- Divergence: Logos addition (compile-time annotation reflection intrinsic).
- Statement: `get_annotation::<T, A>()` requires exactly two type arguments; A must be a ZonedStruct that is an annotation type. `Option` must be in scope. Result type is `Option<A>`.
- Source: src/compiler/sema_expr.cpp#L4901-L4938
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_intrinsic_has_trait_of.json`

### `intrinsic.has-annotation.const-fold` — has_annotation is a compile-time annotation check

- Divergence: Logos addition (annotation metaprogramming).
- Statement: `has_annotation::<T, A>()` requires exactly two type arguments and const-folds to `bool`: true iff datatype T carries a user annotation of annotation-type A. A must be a known annotation datatype (else compile error); the check matches against T's declared annotation instances by fully-qualified or simple name.
- Source: src/compiler/sema_expr.cpp#L5786-L5823
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.has-trait-of.signature` — has_trait_of arity and shape

- Divergence: Logos addition (reflection intrinsic); no Rust equivalent.
- Statement: `has_trait_of::<Trait>(t)` requires exactly one trait type-argument (a single named type in the turbofish) and exactly one value argument; violating either is a compile error. It evaluates to `bool`.
- Source: src/compiler/sema_expr.cpp#L4367-L4410
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_intrinsic_has_trait_of.json`

### `intrinsic.has-trait-of.type-method` — has_trait_of is the Type-method form of has_trait

- Divergence: Logos addition.
- Statement: `has_trait_of::<Trait>(t: Type) -> bool` recovers concrete T from the value t's Type.uid field and runs the same impl-table recursion as has_trait.
- Source: src/compiler/sema_expr.cpp#L5272-L5276
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.has-trait.t-trait-bool` — has_trait queries impl tables

- Divergence: Logos addition.
- Statement: `has_trait::<T, Trait>()` requires two type arguments and yields `bool`: whether concrete T implements Trait, resolved at mono against the same impl tables (concrete + recursive blanket lookup) that drive method dispatch. The second argument is read by its identifier name only (passed as a string literal arg), not resolved as a type. Missing T or empty Trait name is a compile error.
- Source: src/compiler/sema_expr.cpp#L5235-L5270
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.is-data-plain-of.copyable-predicate` — is_data_plain_of predicates DataPlain layout

- Divergence: Logos addition (zoned/Writ datatypes).
- Statement: `is_data_plain_of::<T>()` yields `bool`: true iff T is a DataPlain datatype (no relative-pointer fields). Array wrappers are stripped ([D; N] checks D). Non-datatype types (scalars, ordinary structs) always yield true; a generic (type-arg-bearing) zoned datatype yields false (conservative); an unknown datatype defaults to true.
- Source: src/compiler/sema_expr.cpp#L5739-L5779
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.is-kind.predicate-family` — Type-kind predicate family

- Divergence: Logos addition.
- Statement: The predicates is_ptr / is_ref / is_mut_ref / is_struct / is_zoned / is_enum / is_tuple / is_slice / is_array / is_integer / is_signed / is_unsigned / is_float / is_bool / is_primitive each take exactly one type argument and yield `bool`, resolved against the substituted T at mono. Wrong arity is a compile error.
- Source: src/compiler/sema_expr.cpp#L5127-L5140
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.is-same.two-type-args` — is_same arity and result

- Divergence: Logos addition.
- Statement: `is_same::<T1, T2>()` requires exactly two type arguments and yields `bool`; structural/identity equality of T1 and T2 is resolved post-substitution at mono. Wrong arity is a compile error.
- Source: src/compiler/sema_expr.cpp#L5018-L5026
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.metaprog.reify-type` — reify_type round-trips a Type value at mono time

- Divergence: Logos addition: type-reflection metaprogramming intrinsic.
- Statement: `reify_type(t: Type) -> Type` takes exactly 1 argument and lowers to the `__reify_type__` mono intercept, which substitutes the argument and re-emits a fresh `Type` struct literal from its uid. Wrong arity is an error.
- Source: src/compiler/sema_expr.cpp#L3139-L3154
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_unary.part2.json`

### `intrinsic.metaprog.type-apply` — type_apply / apply_generic instantiate a type-level template

- Divergence: Logos addition: type-level composition metaprogramming intrinsics.
- Statement: `type_apply(name: &[u8], args: [Type; N]) -> Type` and `apply_generic(g: Type, args: [Type; N]) -> Type` each take exactly 2 arguments and lower to the `__type_apply__` / `__apply_generic__` mono intercepts, which recover concrete TypeRefs from each element and emit a fresh `Type` struct literal for `Name<T0,...>`. Wrong arity is an error.
- Source: src/compiler/sema_expr.cpp#L3156-L3184
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_unary.part2.json`

### `intrinsic.reflect.deferred-fold-after-subst` — Type-introspection intrinsics fold after substitution at mono

- Divergence: Logos addition: compile-time type reflection intrinsics (no Rust equivalent).
- Statement: Type-trait/type-introspection intrinsics taking type-args are not evaluated at sema; each lowers to a magic `__<name>__` call carrying its type-args, and is folded to a concrete value only after monomorphization substitutes those type-args. Inside a generic body where T is still a type variable the call is preserved (never frozen to 'TypeVar' semantics).
- Source: src/compiler/sema_expr.cpp#L5014-L5017, src/compiler/sema_expr.cpp#L5079-L5087, src/compiler/sema_expr.cpp#L5142-L5146
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.reflect.typeinfo-rodata` — reflect requests TypeInfo rodata

- Divergence: Logos addition.
- Statement: `reflect::<T>() -> WritStatic` is a compile-time request that registers T for reflection so a TypeInfo global is emitted; the expression resolves to the address of that emitted TypeInfo rodata.
- Source: src/compiler/sema_expr.cpp#L5781-L5784
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.reflect.writ-trait` — reflect on a writ trait registers a reflect request

- Divergence: Logos addition (Writ reflection intrinsic).
- Statement: `reflect::<Tr>()` where Tr names a writ trait (is_writ) registers a reflect request for `pkg::Tr` and evaluates to a `WritStatic` reflection of that trait/datatype.
- Source: src/compiler/sema_expr.cpp#L4851-L4876
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_intrinsic_has_trait_of.json`

### `intrinsic.sizeof.byte-size` — sizeof yields byte size

- Divergence: Logos spelling of size_of; result is i64 (Rust mem::size_of -> usize).
- Statement: `sizeof::<T>()` requires exactly one type argument and yields `i64` = byte size of T.
- Source: src/compiler/sema_expr.cpp#L5703-L5716
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.slice-from-raw.ptr-len` — slice_from_raw builds a slice fat pointer

- Divergence: Logos addition (unsafe raw-parts constructor).
- Statement: `slice_from_raw::<T>(ptr: *const T, len: i64) -> &[T]` requires exactly one type argument and exactly two value arguments; it materialises a slice fat-pointer of element type T (uniform fat-pointer layout shared with str_from_raw). Wrong type-arg count or value-arg count is a compile error.
- Source: src/compiler/sema_expr.cpp#L5032-L5057
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.str.str-from-raw` — str_from_raw constructs a str fat pointer

- Divergence: Logos addition: no Rust equivalent free function.
- Statement: `str_from_raw(ptr: *const u8, len: i64) -> str` is a compiler intrinsic taking exactly 2 arguments; it yields a value of type `&[u8]`/str fat-pointer. Wrong arity is an error.
- Source: src/compiler/sema_expr.cpp#L3117-L3127
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_unary.part2.json`

### `intrinsic.template-of.decl-handle` — template_of yields a Template handle to a declaration

- Divergence: Logos addition.
- Statement: `template_of::<X>()` resolves X at sema, locates the declaration item named X in the current AST root, and yields a `Template { raw: AnyVal { raw: <offset> } }` baking that declaration's arena offset as a u32 literal (same-AST scope).
- Source: src/compiler/sema_expr.cpp#L5621-L5627
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.template-of.signature` — template_of requires a top-level item name in the current file

- Divergence: Logos addition (metaprogramming intrinsic).
- Statement: `template_of::<X>()` requires its single type-argument to be a bare named item; X must name a top-level declaration in the current source file, otherwise a compile error. It also requires `use logos.std.compiler.metaprog` (the `template_of_at` shim) to be in scope.
- Source: src/compiler/sema_expr.cpp#L4576-L4632
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_intrinsic_has_trait_of.json`

### `intrinsic.tuple-all-eq.chain-expand` — tuple_all_eq expands an element-wise eq chain

- Divergence: Logos addition (variadic-tuple support).
- Statement: `tuple_all_eq::<T>(a, b)` expands to the conjunction `a.0.eq(&b.0) && ... && a.{N-1}.eq(&b.{N-1})`. If T is a concrete tuple the chain is expanded at sema; if any element is a type variable a `__tuple_all_eq__` placeholder is emitted and expanded at mono once T's arity is concrete.
- Source: src/compiler/sema_expr.cpp#L5459-L5471
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.tuple-all-eq.signature` — tuple_all_eq arity and tuple constraint

- Divergence: Logos addition (variadic-tuple support intrinsic).
- Statement: `tuple_all_eq::<T>(a, b)` requires exactly one type argument T which must be a tuple type, and exactly two value arguments; otherwise a compile error. Result type is `bool`. An empty tuple yields the constant `true`.
- Source: src/compiler/sema_expr.cpp#L4413-L4451
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_intrinsic_has_trait_of.json`

### `intrinsic.tuple-count-of.elem-count` — tuple_count_of yields tuple element count

- Divergence: Logos addition.
- Statement: `tuple_count_of::<T>()` requires one type argument and yields `i64` = number of elements in tuple T (0 for non-tuple T).
- Source: src/compiler/sema_expr.cpp#L5516-L5534
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.tuple-each-field-debug.requires-tuple` — tuple_each_field_debug formats every tuple field

- Divergence: Logos addition.
- Statement: `tuple_each_field_debug::<T>(self, f)` requires one type argument that MUST be a tuple type (else compile error) and exactly two value arguments; result type is the enclosing function's return type. It Debug-formats every field of T into Formatter f, deferring to a `__tuple_each_field_debug__` placeholder expanded at mono.
- Source: src/compiler/sema_expr.cpp#L5473-L5514
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.tuple-elems-of.elem-types` — tuple_elems_of yields tuple element types

- Divergence: Logos addition.
- Statement: `tuple_elems_of::<T>()` requires one type argument and yields `[Type; N]` of T's element types; empty array for non-tuple T.
- Source: src/compiler/sema_expr.cpp#L5536-L5560
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.type-code-of.signature` — type_code_of arity and result type

- Divergence: Logos addition (Writ/zoned reflection intrinsic).
- Statement: `type_code_of::<T>()` requires exactly one type argument and evaluates to a `u64` type code.
- Source: src/compiler/sema_expr.cpp#L4634-L4647, src/compiler/sema_expr.cpp#L4712
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_intrinsic_has_trait_of.json`

### `intrinsic.type-code-of.writ-code` — type_code_of yields the Writ type code

- Divergence: Logos addition (Writ substrate).
- Statement: `type_code_of::<T>()` yields `u64`, the Writ type_code of a concrete datatype = SHA-256 of "package::Name" truncated to 56 bits, shifted to >= 128 if needed (codes 1-127 reserved for inline AnyVal). For non-datatype T it yields 0.
- Source: src/compiler/sema_expr.cpp#L5733-L5737
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.type-hash.structural-u64` — type_hash is layout-structural

- Divergence: Logos addition.
- Statement: `type_hash::<T>()` requires one type argument and yields `u64`: a structural FNV-1a-64 hash of T's layout — primitives map to fixed codes; struct/tuple/array/ptr hash a tag plus the recursive hashes of constituents, with NO struct/field names. Two structurally identical layouts hash equal; generic instances hash through their substituted args (Foo<i32> != Foo<u32>).
- Source: src/compiler/sema_expr.cpp#L5073-L5087
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.type-of.type-struct` — type_of constructs a Type reflection struct

- Divergence: Logos addition (type reflection).
- Statement: `type_of::<T>()` requires exactly one type argument and yields a `Type` struct literal with fields {kind: u32 (from __type_kind_of__), name: &[u8] (from __type_name_of__), size: i64 (size_of T), align: i64 (align_of T), uid: u64 (type_uid of T)}. Each component is concretized at mono.
- Source: src/compiler/sema_expr.cpp#L5142-L5183
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.type-refs-of.pack-array` — type_refs_of reflects a type pack

- Divergence: Logos addition.
- Statement: `type_refs_of::<T...>()` yields `[Type; N]` with one Type value per pack member, substituted after pack expansion at mono. When the pack reduces to a single type-variable pack, the placeholder array carries a pack-size marker so let-bound/return types lift to the concrete `[Type; N]` automatically.
- Source: src/compiler/sema_expr.cpp#L5670-L5701
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.type-uid-hi.high-half` — type_uid_hi is the high half of the 128-bit UID

- Divergence: Logos addition.
- Statement: `type_uid_hi::<T>()` requires one type argument and yields `u64`, the HIGH 64 bits of the 128-bit nominal type UID; together with type_uid (low half) they form a 128-bit TypeId.
- Source: src/compiler/sema_expr.cpp#L5103-L5115
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.type-uid.nominal-u64` — type_uid is nominal identity

- Divergence: Logos addition.
- Statement: `type_uid::<T>()` requires one type argument and yields `u64`: a NOMINAL 64-bit type identity (hash of the canonical named type string), so distinct nominal types differ even at identical layout (unlike type_hash). It is the low 64 bits of the 128-bit type UID and equals the `.uid` field exposed by type_of.
- Source: src/compiler/sema_expr.cpp#L5088-L5102, src/compiler/sema_expr.cpp#L5172-L5174
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.typelist.probe-family` — typelist O(1) probes over a type pack

- Divergence: Logos addition.
- Statement: Over L's type-pack (L.type_args()), one type argument required: `typelist_len::<L>() -> i64`; `typelist_head::<L>() -> Type` (error if pack empty); `typelist_nth::<L>(i) -> Type` requiring exactly one i64 index arg (out-of-range = error); `typelist_tail::<L>() -> [Type; N-1]`. Substituted after L is concrete.
- Source: src/compiler/sema_expr.cpp#L5393-L5457
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.variant-reflect.enum-family` — Enum-variant decompose intrinsics

- Divergence: Logos addition.
- Statement: Each requires one type argument E: `variant_count_of::<E>() -> i64`; `variant_names_of::<E>() -> [&[u8]; N]`; `variant_payload_counts_of::<E>() -> [i64; N]`; `variant_payload_types_flat_of::<E>() -> [Type; M]`. For non-enum or unknown E all yield 0 / empty arrays.
- Source: src/compiler/sema_expr.cpp#L5629-L5668
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.vtable-of.static-vtable-addr` — vtable_of yields a static vtable address

- Divergence: Logos addition.
- Statement: `vtable_of::<Trait, T>() -> *const u8` yields the address of the static vtable for `impl Trait for T`. Trait is read by NAME (must be a known trait, else error); T is resolved as a type and substituted at mono. Missing trait name or type is a compile error; an unknown trait name is a compile error.
- Source: src/compiler/sema_expr.cpp#L5278-L5312
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.wstatic-hash-of.u64` — wstatic_hash_of identity hash

- Divergence: Logos addition.
- Statement: `wstatic_hash_of::<CFG>()` requires exactly one type argument and yields `u64`, the byte-hash identity of a WritStatic value; folded at mono once CFG is a concrete WStaticLit.
- Source: src/compiler/sema_expr.cpp#L5064-L5072
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.zone-mut-ref.unsafe` — zone_mut_ref signature and unsafe requirement

- Divergence: Logos addition (zoned-reference construction intrinsic).
- Statement: `zone_mut_ref::<T>(ptr, zone)` requires unsafe context, exactly one type argument T, and exactly two value arguments.
- Source: src/compiler/sema_expr.cpp#L4820-L4843
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_intrinsic_has_trait_of.json`

### `intrinsic.zone.zone-of` — zone_of recovers the Writ zone pointer of a fat &mut T

- Divergence: Logos addition: Writ/zone memory model intrinsic.
- Statement: `zone_of(r: &mut T) -> *mut u8` takes exactly 1 argument and yields the metadata half of the fat reference reinterpreted as a `*mut u8` (dual of zone_mut_ref). Wrong arity is an error.
- Source: src/compiler/sema_expr.cpp#L3129-L3137
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_unary.part2.json`

### `item.attr.datatype-promotion` — #[datatype]/#[annotation] promote a struct into the datatype pipeline

- Divergence: Logos addition: datatype/annotation/zoned attributes (no Rust equivalent).
- Statement: A struct-syntax item annotated `#[datatype]` or `#[annotation]` is treated as a datatype declaration; `#[zoned]` marks self-relative fields and does NOT promote a struct to a datatype.
- Source: src/compiler/sema_collect.cpp#L366-L431
- Rule file: `tools/spec-extract/rules/sema/sema_collect/logos.json`

### `item.attr.struct-enum-flag-set` — Struct/enum attribute flag vocabulary

- Divergence: Logos-specific memory/zone attribute set; no Rust analogue.
- Statement: The recognised struct/enum modifier attributes are exactly: `datatype`, `annotation`, `zoned`, `zone_mut`, `rel_ptr`, `self_describing`, `pinned`, `borrow_carrying`, `no_auto_drop`, `non_null`. A struct bearing `#[datatype]` or `#[annotation]` is promoted to the datatype pipeline.
- Source: src/compiler/sema_impl.hpp#L1430-L1460
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/logos.part3.json`

### `item.datatype.type-code-register` — #[type_code=N] registers explicit type code

- Divergence: Logos addition (Writ datatype family).
- Statement: `#[type_code=N]` on a datatype registers N as the explicit type code for the datatype's fully-qualified name, making it resolvable by impl-collection in the same pass; `#[annotation]` flags the datatype as a user-annotation type.
- Source: src/compiler/sema_collect.cpp#L1654-L1667
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_module.json`

### `item.datatype.type-code-unique` — exclusive datatype annotations are unique

- Divergence: Logos addition.
- Statement: On a datatype, the exclusive annotations `#[type_code]` and `#[annotation]` may each appear at most once; a duplicate occurrence is rejected.
- Source: src/compiler/sema_collect.cpp#L1641-L1652
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_module.json`

### `item.dup.odr-dedup` — structurally identical duplicate items dedup; differing ones error

- Divergence: Logos addition: ODR dedup of metacall-emitted items (Rust has no metacall splice model).
- Statement: Two item definitions (struct/union/datatype/enum) sharing the same name in the same package are an error UNLESS their AST sub-trees are structurally equal, in which case the duplicate is silently dropped (ODR-style dedup). Structural equality ignores source-line metadata, so identical items emitted by metaprogramming at different source positions still dedup.
- Source: src/compiler/sema_collect.cpp#L25-L75, src/compiler/sema_collect.cpp#L267-L282, src/compiler/sema_collect.cpp#L378-L446
- Rule file: `tools/spec-extract/rules/sema/sema_collect/logos.json`

### `item.enum.zoned-attr` — #[zoned]/#[borrow_carrying] on enum

- Divergence: Logos addition.
- Statement: `#[zoned]` on an enum sets its zoned2 flag (niche-enum Ref arm stored self-relative at rest, absolute as value); `#[borrow_carrying]` sets the borrow_carrying flag.
- Source: src/compiler/sema_collect.cpp#L1681-L1692
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_module.json`

### `item.fn-param.datanode-by-value` — DataNode eidos cannot be passed by value

- Divergence: Logos addition (zoned/DataNode model); no Rust analog
- Statement: A parameter whose type is (or contains) a DataNode datatype (one holding relative-pointer fields) is rejected by value; it must be passed as `DataRef<T>` because the relative pointers require a zone base pointer.
- Source: src/compiler/sema_decl.cpp#L700-L713
- Rule file: `tools/spec-extract/rules/sema/sema_decl/logos.part2.json`

### `item.struct.attr-flags` — structural struct attribute flags

- Divergence: Logos addition (zone/memory-model attributes).
- Statement: Recognised structural struct attributes set per-struct flags: no_auto_drop, self_describing, rel_ptr, pinned, zone_mut, zoned (zoned2), borrow_carrying, non_null.
- Source: src/compiler/sema_collect.cpp#L1557-L1573
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_module.json`

### `item.use.path-form` — use declaration path form

- Divergence: Logos paths use `.` for package/module segments rather than Rust's `::`.
- Statement: A use declaration is `[pub] use NAME(.part)* ;`, where path segments after the head are dot-separated.
- Source: src/compiler/sema_render.cpp#L1036-L1050, src/compiler/sema_render.cpp#L1182-L1190
- Rule file: `tools/spec-extract/rules/sema/sema_render/render_stmt_src.json`

### `item.visibility.pub-module` — Visibility marker pub / pub(module)

- Divergence: Logos uses `pub(module)` for module-linkage; Rust uses `pub(crate)`/path-restricted visibilities.
- Statement: Item visibility is `pub` (fully exported) or `pub(IDENT)` where IDENT is a contextual keyword validated == "module" in sema, meaning module-linkage: visible to other packages of the SAME module but not exported to consumers.
- Source: tools/peg_gen/grammars/logos.peg#L1273-L1284
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-pub_datatype_def.json`

### `layout.anyval.scalar-i32` — AnyVal is a scalar value type

- Divergence: Logos built-in type with no Rust analogue
- Statement: The built-in type `AnyVal` has scalar value representation (a single machine word, narrowed to 32-bit), not an aggregate. It is never spilled to a by-value aggregate slot like a struct receiver.
- Source: src/compiler/mlir_gen.cpp#L743-L746, src/compiler/mlir_gen.cpp#L888-L903, src/compiler/mlir_gen.cpp#L865-L867
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen/get_struct_ptr.json`

### `layout.dst.self-describing-ref-is-thin` — Ref to a #[self_describing] DST is a thin 8-byte pointer

- Divergence: Logos custom-DST extension (#[self_describing]); no Rust equivalent.
- Statement: A reference to a `#[self_describing]` DST is physically thin (8-byte pointer straight to the header); the tail length is recovered in-band from the pointee header rather than carried alongside the pointer.
- Source: src/compiler/mlir_gen_impl.hpp#L884-L888
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_impl_hpp/logos.part3.json`

### `layout.dst.self-describing-thin` — self_describing DST reference is thin

- Divergence: Logos-only self_describing DST model (Rust's DST metadata is always out-of-band).
- Statement: A DstRef whose pointee struct is #[self_describing] is physically THIN (8B pointer straight to the header); its tail length is not carried out-of-band but recovered in-band by calling SelfDescribing::dst_len(header_ptr). This contrasts with a plain [T]-tail DstRef which is an 8B pointer to a 16-byte {data,len} pair.
- Source: src/compiler/mlir_gen_expr.cpp#L5007-L5032, src/compiler/mlir_gen_expr.cpp#L5124-L5157
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_expr/gen_expr_kind-4.json`

### `layout.dstref.fat-only-with-slice-tail` — Custom-DST reference is a 16-byte fat slot only with a literal slice tail

- Divergence: Logos custom-DST representation split (slice-tail fat vs dyn-tail/self-describing thin).
- Statement: A custom-DST reference (&Foo/&mut Foo where Foo has a tail) is a 16-byte {data,len} fat pointer ONLY when the pointee has a literal `[T]` slice tail (len carried inline) and is not #[self_describing]. A `dyn`-tail DST ref or a #[self_describing] DST is physically THIN (8-byte pointer; tail length recovered in-band, e.g. sizeof(Rc<dyn>)==8) and is not copied as a 16-byte fat slot.
- Source: src/compiler/mlir_gen_stmt.cpp#L1330-L1351
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_stmt/gen_drop_owning_dst.json`

### `layout.enum.niche-lowbit-ptr-int` — Low-bit niche packs pointer + small-int arms

- Divergence: Logos low-bit pointer-tagging niche; no direct Rust analog.
- Statement: A two single-field-arm enum where one arm is a pointer to an align>=2 pointee (low bit always 0) and the other arm is a <=56-bit integer stored shifted `(v<<1)|1` packs into one word; the discriminant is the low bit (0=ptr arm, 1=int arm).
- Source: src/compiler/mlir_gen_types.cpp#L796-L853
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_types/repr_storage_type.json`

### `layout.enum.niche-nullptr-nonnull-wrapper` — Null-pointer niche for #[non_null] 8-byte wrapper

- Divergence: Logos `#[non_null]` attribute exposes Rust's NonNull niche to user wrapper types.
- Statement: The null-pointer niche also applies when the single-field variant's field is a `#[non_null]` struct that is exactly an 8-byte pointer wrapper (Box/Rc/Arc-shape), whose invariant guarantees offset-0 is non-zero.
- Source: src/compiler/mlir_gen_types.cpp#L769-L795
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_types/repr_storage_type.json`

### `layout.enum.niche-zoned-raw-word` — Zoned (#[zoned2]) raw 64-bit low-bit niche

- Divergence: Logos zoned (Writ) niche; no Rust equivalent.
- Statement: In a `#[zoned2]` enum, the low-bit niche additionally accepts a raw `*T` pointer arm (trusting the zone allocator's >=2 alignment even for `*u8`) and a raw 64-bit `u64`/`i64` value arm stored without a `<<1` shift (the producer bakes the low-bit-1 tag into the word).
- Source: src/compiler/mlir_gen_types.cpp#L811-L851
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_types/repr_storage_type.json`

### `layout.enum.zoned-niche-self-relative` — Zoned niche enum stores Ref arm self-relative

- Divergence: Logos-only zoned representation; no Rust analogue.
- Statement: A #[zoned2] niche enum's at-rest 8-byte word encodes: r==0 → null; r&1==1 → Pod (position-independent, copied raw); else Ref → self-relative offset (anchor = slot address). Materialize: Ref → absolute = slot + r (null/Pod identity). Lower: Ref → delta = val − slot (null/Pod identity).
- Source: src/compiler/mlir_gen_expr.cpp#L5034-L5074, src/compiler/mlir_gen_expr.cpp#L5076-L5081
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_expr/gen_expr_kind-4.json`

### `layout.field.rel-ptr-self-relative-offset` — #[rel_ptr] field stores a self-relative i64 offset

- Divergence: Logos addition: self-relative pointer field representation (no Rust analogue).
- Statement: A struct field marked #[rel_ptr] (RefRepr RelOffset) does not store an absolute pointer; on assignment the destination pointer value is lowered to a signed i64 offset relative to the field slot's own address (the slot is the anchor) and that offset is stored in the slot.
- Source: src/compiler/mlir_gen_stmt.cpp#L2748-L2758, src/compiler/mlir_gen_stmt.cpp#L2828-L2838
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_stmt/gen_field_write.json`

### `layout.ref.rel-offset-eight-bytes` — Relative-offset reference layout

- Divergence: Logos self-relative pointers (zoned/Writ); no Rust equivalent.
- Statement: A relative-offset (self-relative) reference is stored as a single i64 offset word: {size=8, align=8}.
- Source: src/compiler/mlir_gen_types.cpp#L622, src/compiler/mlir_gen_types.cpp#L637
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_types/repr_storage_type.json`

### `layout.ref.self-relative-offset` — Self-relative (writ / rel_ptr) pointers store a byte offset

- Divergence: Logos addition: self-relative zoned pointers, no Rust analogue.
- Statement: A self-relative pointer (the writ / #[rel_ptr] zoned pointer) is stored as an i64 byte offset from the slot's own address; its compute/absolute form is slot_address + load_i64(slot), and lowering stores (target_address - slot_address). A thin-pointer field inside a #[zoned2] struct stores self-relative.
- Source: src/compiler/mlir_gen_impl.hpp#L792-L795, src/compiler/mlir_gen_impl.hpp#L799-L803
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_impl_hpp/logos.part2.json`

### `layout.ref.zone-mut-fat-pair` — &mut T to a zone_mut type carries its allocator as a fat reference

- Divergence: Logos addition: zone/allocator-carrying mutable reference, no Rust analogue.
- Statement: A &mut T where T is a #[zone_mut] type has a 16-byte {data, zone=*mut Allocator} fat representation, returned by value; the allocator rides the &mut so grow-style methods reach it from &mut self.
- Source: src/compiler/mlir_gen_impl.hpp#L789-L791, src/compiler/mlir_gen_impl.hpp#L507-L508
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_impl_hpp/logos.part2.json`

### `layout.union.max-of-fields` — Union layout is max-size at max-alignment

- Divergence: Logos union via #[repr]/union attribute; layout semantics match C/Rust unions.
- Statement: A struct marked as a union (`#[repr(...)]` union) is laid out as the maximum field size aligned to the maximum field alignment; all fields overlap at offset 0.
- Source: src/compiler/sema_decl.cpp#L1202-L1204
- Rule file: `tools/spec-extract/rules/sema/sema_decl/lower_struct_def.part1.json`

### `layout.zone-mut-ref.fat-data-zone` — &mut T of a zone_mut type is a fat {data, zone} pair

- Divergence: Logos zone model; no Rust analogue
- Statement: A mutable reference `&mut T` where `T` is a `#[zone_mut]` (FatZoneMut) type is a two-word fat pointer carrying {data, zone}; the address of the referent object is the data half. Field and method access on such a receiver descend through the data pointer, not the fat-pair storage.
- Source: src/compiler/mlir_gen.cpp#L667-L680
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen/get_struct_ptr.json`

### `lex.literal.int128-magnitude` — 128-bit integer literal magnitude

- Divergence: Logos provides i128/u128 literals; magnitude bound is 128 bits rather than 64.
- Statement: An integer literal targeting i128/u128 is accumulated as a 128-bit unsigned magnitude (sign applied by the caller) and is rejected only if its magnitude exceeds 128 bits; 64-bit-overflowing values round-trip intact.
- Source: src/compiler/sema_impl.hpp#L4659-L4703
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/is_integer_kind.json`

### `metaprog.antiquot.capture-forms` — Writ antiquotation capture syntax

- Divergence: Logos metaprogramming antiquotation; no Rust equivalent.
- Statement: Within a quoted/Writ literal, an antiquotation captures a value either by identifier `$name` or by expression block `${expr}`.
- Source: src/compiler/sema_render.cpp#L532-L537
- Rule file: `tools/spec-extract/rules/sema/sema_render/logos.json`

### `metaprog.derive.no-rust-derive-syntax` — `#[derive(...)]` is rejected; use per-trait triggers

- Divergence: Logos replaces Rust `#[derive(...)]` with `#[derive_<trait>]` + `#[metaprog_handler]`.
- Statement: The Rust-style `#[derive(Trait, ...)]` attribute (a `derive` annotation carrying args) is not Logos surface syntax and is an error. Logos uses one trigger annotation per derive, `#[derive_<trait>]`, paired with an in-scope `#[metaprog_handler("derive_<trait>")]` function.
- Source: src/compiler/sema_impl.hpp#L1762-L1774
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/logos.part4.json`

### `metaprog.handler.register` — #[metaprog_handler("trigger")] registers a hook

- Divergence: Logos addition (three-layer metaprog).
- Statement: `#[metaprog_handler("trigger")]` on a function registers (trigger, fn-name); the trigger is the first positional string-literal argument and the host driver invokes the hook on each user item carrying a matching `#[trigger]`.
- Source: src/compiler/sema_collect.cpp#L1798-L1834
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_module.json`

### `metaprog.metacall.forms` — metacall expression forms

- Divergence: Logos addition: explicit compile-time evaluation operator (no implicit const-eval).
- Statement: `metacall` accepts a block (`metacall { … }`), a parenthesized expression (`metacall (e)`), or a call expression (`metacall f(…)`), and evaluates its argument at compile time.
- Source: tools/peg_gen/grammars/logos.peg#L2731-L2736
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-compound_assign_op.json`

### `metaprog.quote-item.blob-result-type` — quote_item! evaluates to a QuoteItemBlob value

- Divergence: Logos metaprogramming addition.
- Statement: `quote_item!` evaluates to a `QuoteItemBlob` struct value with fields { template_ptr, template_size, idents_blob, blobs_blob, cursors_blob }, where template_ptr/template_size address the serialized synthetic-module blob and the *_blob fields carry the packed antiquot substitution data (null when the corresponding placeholder kind has zero occurrences).
- Source: src/compiler/sema_expr.cpp#L16133-L16144, src/compiler/sema_expr.cpp#L15907-L15910
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part3.json`

### `metaprog.quote-item.cursor-repetition-packing` — Cursor (`#(...)*`) antiquots carry a per-site nesting depth

- Divergence: Logos metaprogramming addition.
- Statement: Each repetition-cursor antiquot site contributes a `*const u8` (the address of a Vec cursor variable) plus a parallel per-site depth byte: depth 1 = Vec<Ident>, depth 2 = Vec<Vec<Ident>> (nested `#(...)*`). The element type is the neutral `*const u8`; pack reads each cursor according to its depth. When there are no cursor sites, cursors_blob is null.
- Source: src/compiler/sema_expr.cpp#L16056-L16127, src/compiler/sema_expr.cpp#L15939-L15944
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part3.json`

### `metaprog.quote-item.exprblob-antiquot-packing` — ExprBlob antiquots are packed by their .ptr field

- Divergence: Logos metaprogramming addition.
- Statement: Each `#(expr)` antiquot whose lowered expression has type ExprBlob contributes one `*const u8` (the ExprBlob's `ptr` field) to the blobs blob, in DFS placeholder order; the lowered ExprBlob is bound to a local that outlives the array. When there are no ExprBlob sites, blobs_blob is null.
- Source: src/compiler/sema_expr.cpp#L16005-L16054
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part3.json`

### `metaprog.quote-item.ident-antiquot-packing` — `#name`/`#(expr)` Ident antiquots are packed as Ident pointers

- Divergence: Logos metaprogramming addition.
- Statement: Each scalar Ident antiquot site (`#name` shortcut or `#(expr)` yielding Ident) contributes one `*const Ident` to the idents blob, in DFS placeholder order; a `#(expr)` form binds the lowered expression to a fresh local whose address is taken. When there are no Ident sites, idents_blob is null.
- Source: src/compiler/sema_expr.cpp#L15953-L16003
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part3.json`

### `metaprog.quote-item.inherit-import-scope` — quote_item! inherits the metafn's import scope

- Divergence: Logos metaprogramming addition; controls hygiene/name resolution of quoted items.
- Statement: The synthetic module inherits the enclosing metafn's wildcard `use` packages, plus a self-use of the metafn's own package (if non-empty), so that unqualified names inside the quoted items resolve through the metafn's `use`-list. Each inherited package becomes one USE node carrying the full dotted package name in NAME.
- Source: src/compiler/sema_expr.cpp#L15821-L15857
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part3.json`

### `metaprog.quote-item.name-antiquot-forms` — quote_item! accepts #name and #(expr) name antiquotations

- Divergence: Logos metaprogramming construct; no Rust equivalent.
- Statement: Within `quote_item! { ... }`, a NAME_VAR placeholder accepts two forms: `#name` (shortcut) looks the variable up in the metafn scope and requires type Ident; `#(expr)` lowers the inner expression in the metafn scope and requires type Ident or ExprBlob. Any other pointee kind is an error.
- Source: src/compiler/sema_expr.cpp#L15569-L15625
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part2.json`

### `metaprog.quote-item.placeholder-walk-balance` — Source and destination placeholder counts must match

- Divergence: Logos metaprogramming addition.
- Statement: The number of antiquot placeholders discovered while scanning the source items must equal the number of placeholder slots rewritten in the cloned destination tree; a mismatch is a compile error.
- Source: src/compiler/sema_expr.cpp#L15797-L15802
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part3.json`

### `metaprog.quote-item.synthetic-main-module` — quote_item! produces a synthetic `package main` module

- Divergence: Logos metaprogramming addition (no Rust equivalent).
- Statement: `quote_item! { item* }` constructs a synthetic AST module whose root is MODULE with NAME="main", empty PATH_PARTS, ITEMS = the deep-cloned quoted items, and SRC_LINE=1. The result is emitted as a serialized WritStatic blob carried by a `QuoteItemBlob` value.
- Source: src/compiler/sema_expr.cpp#L15859-L15894, src/compiler/sema_expr.cpp#L15805-L15819
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_writ_val.part3.json`

### `module.abi.one-directional-minor-compat` — Binary archive ABI compatibility is one-directional within a major version

- Divergence: Logos addition: semantic-version ABI gate on binary modules (Rust has no stable cross-version library ABI).
- Statement: A compiler may consume a binary library iff (a) the library's language major version equals the compiler's, and (b) for stable releases the library's minor version is <= the compiler's minor. A differing major is incompatible; a library built by a newer minor is rejected. An ABI-incompatible archive is not indexed (its packages become unavailable). Identical version strings are always compatible; legacy archives without a version stamp are not enforced.
- Source: src/compiler/module_loader.cpp#L1100-L1144, src/compiler/module_loader.cpp#L1182-L1184
- Rule file: `tools/spec-extract/rules/sema/module_loader/extract_writ0_exports.json`

### `module.abi.prerelease-no-guarantee` — Pre-release / snapshot builds require exact version match

- Divergence: Logos addition.
- Statement: If either the library or the compiler is a pre-release (`-pre`) or snapshot (`+meta`) build, no ABI guarantee holds: only an exact version-string match is silently accepted; any mismatch is permitted but warned. The check is disabled entirely by environment override.
- Source: src/compiler/module_loader.cpp#L1100-L1113, src/compiler/module_loader.cpp#L1130-L1142, src/compiler/module_loader.cpp#L1110-L1110
- Rule file: `tools/spec-extract/rules/sema/module_loader/extract_writ0_exports.json`

### `module.import.from-pins-module` — `use pkg from <M>` pins resolution to module M's archive

- Divergence: Logos addition (`from <module>` import selector); no Rust equivalent.
- Statement: An import `use pkg from <M>;` resolves `pkg` from the archive whose embedded module canonical-name is `M`, independent of which other archive(s) also provide a package named `pkg`. This lets two distinct modules supplying a same-named package coexist; a bare `use pkg;` and `use pkg from M;` are keyed independently and both load.
- Source: src/compiler/module_loader.cpp#L1594-L1611, src/compiler/module_loader.cpp#L1280-L1282
- Rule file: `tools/spec-extract/rules/sema/module_loader/extract_writ0_exports.json`

### `module.prelude.cross-cutting-auto-load` — Cross-cutting foundation packages auto-load without explicit `use`

- Divergence: Logos addition: implicit prelude is prefix-scoped to the lang tier (transitional; manifest-tier system intended).
- Statement: Foundation packages under prefixes `std.lang`, `std.writ`, or `logos.lang` (excluding the `logos.lang.writ` substrate) are implicitly available to every compilation: when an archive is loaded for a requested package, sibling packages with these prefixes are also loaded so cross-cutting traits and types (Default, Ord, Send, Clone, etc.) resolve without an explicit import edge.
- Source: src/compiler/module_loader.cpp#L1397-L1432, src/compiler/module_loader.cpp#L1566-L1571
- Rule file: `tools/spec-extract/rules/sema/module_loader/extract_writ0_exports.json`

### `module.prelude.implicit-auto-import` — Implicit prelude auto-imported per file

- Divergence: Logos uses a named prelude *package*; the model parallels Rust's std prelude but is package-granular.
- Statement: Every source file implicitly imports the prelude package in addition to its explicit `use` declarations, unless the file opts out. The implicit prelude is deduplicated against explicit uses (no duplicate import if already named).
- Source: src/compiler/module_loader.cpp#L95-L103
- Rule file: `tools/spec-extract/rules/sema/module_loader/logos.json`

### `module.prelude.implicit-injection` — Implicit prelude injected into source files

- Divergence: Logos uses a named injectable prelude package + `#![no_implicit_prelude]` opt-out (Rust-analogous but explicitly package-configured).
- Statement: A configured implicit-prelude package is injected as an implicit `use` into every source file loaded for the current compilation, except files carrying the inner attribute `#![no_implicit_prelude]`. An empty implicit-prelude setting injects nothing.
- Source: src/compiler/module_loader.hpp#L130-L134
- Rule file: `tools/spec-extract/rules/sema/module_loader_hpp/logos.json`

### `module.use.from-module` — use pkg from module restricts candidates

- Divergence: Logos addition: per-import module qualification (no Rust equivalent).
- Statement: `use pkg from <module>;` restricts the candidates of `pkg` to the named module. The `from` keyword is contextual (matched as a bare identifier); a missing/incorrect `from` keyword or a module name matching no loaded module is an error.
- Source: src/compiler/sema_collect.cpp#L192-L225
- Rule file: `tools/spec-extract/rules/sema/sema_collect/logos.json`

### `module.use.from-module-disambiguation` — use ... from <module> disambiguates provider

- Divergence: Logos-specific: type/package coexistence across modules sharing a name (no Rust analog).
- Statement: `use pkg from <module>;` binds the import to the named module, disambiguating which archive provides `pkg` when two modules share a package name. The `<module>` operand may be a bare identifier or a double-quoted string literal (surrounding quotes are stripped). Absence of `from` means default resolution.
- Source: src/compiler/module_loader.cpp#L115-L133, src/compiler/module_loader.cpp#L206-L207
- Rule file: `tools/spec-extract/rules/sema/module_loader/logos.json`

### `module.visibility.pub-module-only` — Restricted visibility only `pub(module)`

- Divergence: Logos has only `pub` and `pub(module)`; Rust's `pub(crate)`/`pub(super)`/`pub(in path)` are not recognised.
- Statement: An item's restricted-visibility marker `pub(W)` is accepted only when W is the contextual word `module` (module-linkage). Plain `pub` and no marker are non-module. Any other word (e.g. `pub(crate)`, `pub(super)`) is rejected: `unsupported visibility `pub(W)` — only `pub(module)` is recognised`.
- Source: src/compiler/sema_impl.hpp#L1176-L1191
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/logos.part3.json`

### `mono.const.const-arg-specialization` — Compile-time-constant call arguments specialize the callee

- Divergence: Logos const-generic-like specialization driven by const-eval reachability; see explicit-metacall comptime model.
- Statement: When a call-site argument forwarding (directly or transitively) to a const-evaluating intrinsic position (e.g. an atomic `Ordering`) is a compile-time literal, the callee is specialized with that constant baked in: each use of the parameter is replaced by the literal (an IntLit for integers, or an EnumLit `(enum_name, variant, discriminant)` for enums).
- Source: src/compiler/mono_impl.hpp#L368-L401
- Rule file: `tools/spec-extract/rules/mono/mono_impl_hpp/logos.part1.json`

### `mono.dispatch.self-generic-template-mangle` — Self-typed method-generic call mangles resolved template directly

- Divergence: Logos-specific monomorphization fixup
- Statement: A trait-default body call to another method-generic method on a `Self`-typed generic-struct receiver, where the resolved symbol is a method-generic template (contains `__g__`) with method-level type-params and all call type-args are concrete, is resolved by mangling the resolved template with the call's type-args and enqueuing it; impl-only-generic methods (no method-level type-params) are excluded from this path.
- Source: src/compiler/mono_clone.cpp#L4083-L4139
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part8.json`

### `mono.dispatch.trait-qualified-mangling` — Ambiguous-by-name dispatch resolves to trait-qualified symbol

- Divergence: Logos-specific name-mangling scheme; see G156-1 baghunt for two-impl collision
- Statement: When a method call on a type-variable receiver is dispatch-ambiguous by name and a trait was selected (trait T), monomorphization resolves the callee base to `<recv-type>__<T>__<method>` if such a symbol exists; otherwise it falls back to the plain `<recv-type>__<method>`.
- Source: src/compiler/mono_clone.cpp#L3716-L3737
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part8.json`

### `mono.intrinsic.args-count-of` — args-count-of yields the number of generic type arguments of T

- Divergence: Logos reflection extension.
- Statement: `__args_count_of__` yields an i64 literal equal to the count of T's generic type-args (0 for non-generic/primitive T).
- Source: src/compiler/mono_clone.cpp#L1530-L1543
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part3.json`

### `mono.intrinsic.field-types-of-nonstruct-empty` — field-types-of on a non-struct yields an empty type pack

- Divergence: Logos reflection extension.
- Statement: `__field_types_of__` applied to a substituted T that is not a Struct/ZonedStruct yields an empty pack (the [Type;0] result), which resolves variadic instantiation to the 0-arg base overload rather than aborting. Field types of a generic struct are computed by substituting the struct template's type-params with T's type-args.
- Source: src/compiler/mono_clone.cpp#L1342-L1380
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part3.json`

### `mono.intrinsic.has-trait` — has-trait resolves a trait implementation at monomorphization time

- Divergence: Logos reflection extension (compile-time trait-satisfaction predicate).
- Statement: `__has_trait__` yields a bool literal: it reads the trait name from the call's first string-literal argument and the concrete type from type_args[0], reduces T to a concrete name (concrete_struct_name for Struct/ZonedStruct, enum_name for Enum, else type_str, with any `$G...` generic suffix stripped), and recursively tests trait satisfaction against concrete + blanket impl tables.
- Source: src/compiler/mono_clone.cpp#L1544-L1584
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part3.json`

### `mono.intrinsic.has-trait-of` — has-trait-of recovers T from a reflected Type value then resolves the trait

- Divergence: Logos reflection extension (Type-method form).
- Statement: `__has_trait_of__(trait, t: Type)` recovers the concrete T from t's StructLit `uid` field (a `__type_uid_of__` call, chasing ≤8 VarRef hops) and then performs the same impl-table recursion as __has_trait__ to yield a bool literal.
- Source: src/compiler/mono_clone.cpp#L1585-L1592
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part3.json`

### `mono.intrinsic.is-same` — is-same compares two substituted types for equality

- Divergence: Logos reflection extension.
- Statement: `__is_same__` yields a bool literal true iff exactly two type-args are given and they are equal after substitution.
- Source: src/compiler/mono_clone.cpp#L1490-L1494
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part3.json`

### `mono.intrinsic.type-hash-of` — type-hash-of yields a structural layout-stable hash

- Divergence: Logos reflection extension.
- Statement: `__type_hash_of__` is replaced by an integer literal equal to a structural FNV-1a-64 hash of the substituted T (no struct/field names; recurses into field types), i.e. layout-stable identity.
- Source: src/compiler/mono_clone.cpp#L1438-L1449
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part3.json`

### `mono.intrinsic.type-kind-of` — type-kind-of yields the kind discriminant of substituted T

- Divergence: Logos reflection extension.
- Statement: `__type_kind_of__` is replaced by an integer literal equal to the LogosType kind discriminant of the concrete substituted first type-arg (0 if none).
- Source: src/compiler/mono_clone.cpp#L1419-L1427
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part3.json`

### `mono.intrinsic.type-kind-predicates` — Type-trait predicates evaluate on the substituted type's kind

- Divergence: Logos reflection extension.
- Statement: Each predicate yields a bool literal computed from the kind of the concrete substituted first type-arg: __is_ptr__/ref/mut_ref/struct/zoned/enum/tuple/slice/array/bool test the exact kind; __is_integer__ is true for I8..I128/U8..U128; __is_float__ for F32/F64; __is_signed__ for I8..I128; __is_unsigned__ for U8..U128; __is_primitive__ for bool|float|integer.
- Source: src/compiler/mono_clone.cpp#L1495-L1528
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part3.json`

### `mono.intrinsic.type-name-of` — type-name-of yields the canonical type string of T

- Divergence: Logos reflection extension.
- Statement: `__type_name_of__` is replaced by a string literal equal to the canonical `type_str(T)` of the concrete substituted T (empty if none).
- Source: src/compiler/mono_clone.cpp#L1450-L1457
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part3.json`

### `mono.intrinsic.type-uid-of` — type-uid-of yields the low 64 bits of the nominal type UID

- Divergence: Logos reflection extension.
- Statement: `__type_uid_of__` is replaced by an integer literal equal to the low 64 bits of the nominal UID (derived from a hash of the canonical type id of substituted T); the mapping uid→type is recorded for later reification. `__type_uid_hi_of__` yields the high 64 bits over the same canonical input, so the halves agree.
- Source: src/compiler/mono_clone.cpp#L1458-L1481
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part3.json`

### `mono.intrinsic.wstatic-hash-of` — wstatic-hash-of yields the byte-hash of CFG

- Divergence: Logos compile-time-static (Writ) extension.
- Statement: `__wstatic_hash_of__` is replaced by an integer literal equal to the const-value (u64 byte-hash) carried by the substituted first type-arg (a WStaticLit kind); 0 if absent.
- Source: src/compiler/mono_clone.cpp#L1428-L1437
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part3.json`

### `mono.subst.cfg-slot-type-projection` — CFG-slot type projection from Writ config

- Divergence: Logos-specific compile-time Writ-config-driven type projection; no Rust analogue.
- Statement: A CfgSlotType `<type:CFG.path>` resolves CFG (a const-generic param or inlined Writ static literal) to a WStaticLit, walks the encoded path (string-keyed 'F', int-keyed 'I' map fields and 'A' array indices joined by 0x1F) through the Writ value, and yields the type named at the terminal Type node (primitive, struct, or enum); if CFG is not yet concrete or any step misses, the projection stays deferred (unchanged).
- Source: src/compiler/mono_subst.cpp#L432-L535
- Rule file: `tools/spec-extract/rules/mono/mono_subst/logos.json`

### `mono.subst.drop-args-non-generic-impl` — Type-args dropped/truncated to template type-param count

- Divergence: Logos-specific (T9-tr-02) plus variadic-pack handling
- Statement: A method call's type-arguments are kept only up to the resolved template's declared type-parameter count: if the template has zero type-params (concrete impl) all type-args are cleared; otherwise they are truncated to the param count, EXCEPT a variadic template type-param consumes all trailing type-args (no truncation).
- Source: src/compiler/mono_clone.cpp#L3913-L3963
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part8.json`

### `mono.subst.lazy-blanket-method-instantiation` — Lazy blanket-impl method instantiation for generic receiver

- Divergence: Logos-specific blanket-impl monomorphization (G159-1)
- Statement: A method call on a generic struct/enum receiver (after stripping ptr/ref/mut-ref) dispatched through a blanket `impl<T: Bound> Trait for T` is instantiated lazily: the blanket method template is cloned with {T -> receiver type} and enqueued, provided the receiver concrete type satisfies the blanket's bound trait and all extra bounds; the resulting spec is `<recv>__<method>`.
- Source: src/compiler/mono_clone.cpp#L3804-L3878
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part8.json`

### `mono.subst.self-describing-dst-thin-ptr` — Raw pointer to self-describing DST stays thin

- Divergence: Logos-specific Writ/RefRepr self-describing-DST contract; no Rust analogue.
- Statement: When the pointee struct is `#[self_describing]` (recovers its tail length from an in-band prefix field), a raw `*const Self`/`*mut Self` (kind Ptr) stays a thin 8-byte pointer and is NOT canonicalized to fat DstRef; `&Self`/`&mut Self` still take the fat representation.
- Source: src/compiler/mono_subst.cpp#L164-L174
- Rule file: `tools/spec-extract/rules/mono/mono_subst/logos.json`

### `mono.subst.splicepack-producer-fold` — $fs... splice-pack folds reflected [Type] producers into call type-args

- Divergence: Logos reflection/metaprogramming extension; no Rust equivalent.
- Statement: A call type-arg encoded as TypeVar named `__splicepack$<v>` is resolved by chasing `v` (≤8 VarRef hops) to a producer call and folding its element types into the callee's type-args: `__type_refs_of__` → all (substituted) type-args; `__args_of__` → type-args of the first type-arg T; `__typelist_tail__` → T.type_args[1..]; `__tuple_elems_of__` → T's tuple elements (T must be Tuple); `__field_types_of__` → the (substituted) field types of struct T. An unrecognized producer is a hard error.
- Source: src/compiler/mono_clone.cpp#L1287-L1392
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part3.json`

### `mono.subst.tuple-receiver-elem-args` — Tuple receiver supplies its element types as impl type-args

- Divergence: Logos-specific (SL-sl-08 / CP-cm-08b)
- Statement: For a method call whose receiver is a tuple type, the call's impl-level type-arguments are set to the tuple's element types, enabling specialization of `impl<A,B,...> Trait for (A,B,...)`; for nested tuple recursion the inner receiver's own element types override stale outer-spec args.
- Source: src/compiler/mono_clone.cpp#L3894-L3912
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part8.json`

### `pat.range.scrutinee-integer` — Range pattern requires integer scrutinee

- Divergence: Logos char ranges are handled separately (PAT_CHAR_RANGE); PAT_RANGE is integer-only.
- Statement: A range pattern requires an integer scrutinee type; a non-integer, non-error scrutinee is an error. A `never` scrutinee is exempted from this check.
- Source: src/compiler/sema_stmt.cpp#L4685-L4689
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_pattern_bytes.part2.json`

### `pat.tuple.str-element-via-guard` — String-literal tuple element lowered to str_eq guard

- Divergence: Logos addition: tuple-arm codegen lacks a native str_eq dispatch, so string elements are desugared to guards.
- Statement: A string-literal element of a tuple pattern binds the element to a synthesized name and adds a refutable `str_eq(synth, lit)` guard, rather than a value-equality test (a raw `==` would pointer-compare). Requires the refutable-guard context to be active.
- Source: src/compiler/sema_stmt.cpp#L4552-L4567, src/compiler/sema_stmt.cpp#L4600-L4617
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_pattern_bytes.part2.json`

### `pat.writ.array-len-and-rest` — Writ array pattern length and rest

- Divergence: Logos addition.
- Statement: A Writ array pattern `@[p0, p1, ...]` matches iff the scrutinee is an array of exactly the listed element count and each element matches its sub-pattern. A trailing `..` rest changes the length check to >= (count of non-rest elements) and binds no further elements. `..` is permitted only as the LAST element; otherwise a compile error.
- Source: src/compiler/sema_stmt.cpp#L5525-L5562
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_writ_pat_guard.json`

### `pat.writ.container` — Writ map/array patterns

- Divergence: Logos addition: Writ container patterns.
- Statement: `@{ key: pat, ... }` / `@{}` match writ maps; `@[ elem, ... ]` / `@[]` match writ arrays. Array elements admit a trailing `..` to match length ≥ n; map keys are string literals.
- Source: tools/peg_gen/grammars/logos.peg#L2028-L2041, tools/peg_gen/grammars/logos.peg#L2113-L2120
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-never_type.json`

### `pat.writ.int-i24-range` — Writ integer pattern fits i24

- Divergence: Logos addition; i24 bound is Writ-specific.
- Statement: A Writ integer pattern `@<int>` value v must satisfy -2^23 <= v < 2^23 (i24 range); otherwise it is a compile error. The literal may carry a negation flag that negates the parsed magnitude.
- Source: src/compiler/sema_stmt.cpp#L5312-L5327
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_writ_pat_guard.json`

### `pat.writ.map-shape` — Writ map pattern

- Divergence: Logos addition.
- Statement: A Writ map pattern `@{k: p, ...}` matches iff the scrutinee is a map AND, for each listed entry key k, the key is present and its slot value matches sub-pattern p (conjunction over all entries). An entry without a value sub-pattern requires only presence of the key. Map patterns are non-exhaustive: keys not listed are ignored.
- Source: src/compiler/sema_stmt.cpp#L5495-L5524
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_writ_pat_guard.json`

### `pat.writ.match-only` — Writ scalar patterns only in match arms

- Divergence: Logos extension (Writ value patterns); no Rust equivalent.
- Statement: Writ scalar patterns (`@null`, `@true`, `@false`, `@<int>`, `@"str"`, `@{...}`, `@[...]`, and typed array/map forms) are permitted only in `match` arms, not in if-let / while-let / let-bindings / nested pattern positions; elsewhere is an error. In a match arm they lower to a wildcard plus a synthesized guard.
- Source: src/compiler/sema_stmt.cpp#L5086-L5104
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_pattern_bytes.part3.json`

### `pat.writ.or-no-mixing` — Or-patterns may not mix Writ and non-Writ alternatives

- Divergence: Logos addition.
- Statement: In an or-pattern, if any alternative is a Writ pattern then all alternatives must be Writ patterns; mixing Writ patterns with non-Writ patterns is a compile error. An all-Writ or-pattern matches iff any alternative matches (disjunction).
- Source: src/compiler/sema_stmt.cpp#L5641-L5664
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_writ_pat_guard.json`

### `pat.writ.scalar` — Writ scalar patterns

- Divergence: Logos addition: Writ data-substrate patterns.
- Statement: `@null`, `@true`/`@false`, `@N`/`@-N`, and `@"str"` are writ scalar patterns matching writ null, bool, integer, and string values respectively.
- Source: tools/peg_gen/grammars/logos.peg#L2092-L2106
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-never_type.json`

### `pat.writ.typed-array-element-types` — Typed Writ array pattern element types

- Divergence: Logos addition.
- Statement: A typed Writ array pattern `@<T>[..]` matches iff the scrutinee has the array type-code for element type T. T must be one of {I8,U8,I16,U16,I32,U32,I64,U64,F32,F64,AnyVal}; any other element type is a compile error.
- Source: src/compiler/sema_stmt.cpp#L5563-L5588
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_writ_pat_guard.json`

### `pat.writ.typed-container` — Writ typed map/array patterns

- Divergence: Logos addition: Writ typed-container patterns.
- Statement: `@<T>{..}`, `@<T,R>{..}`, and `@<T>[..]` are typed writ map and array patterns annotating the matched container's element type(s).
- Source: tools/peg_gen/grammars/logos.peg#L2107-L2112
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-never_type.json`

### `pat.writ.typed-map-key-value-types` — Typed Writ map pattern key/value types

- Divergence: Logos addition.
- Statement: A typed Writ map pattern `@<K[,V]>{..}` matches iff the scrutinee has the map type-code for key type K. K must be one of {Varchar,I32,U32,I64,U64}; the value type V, if given, must be AnyVal. Any other key or value type is a compile error.
- Source: src/compiler/sema_stmt.cpp#L5589-L5616
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_writ_pat_guard.json`

### `pat.writ.wildcard-binding` — Named wildcard inside Writ pattern binds the AnyVal

- Divergence: Logos addition.
- Statement: A wildcard with a non-`_` name inside a Writ pattern binds that name to the current AnyVal sub-value and always matches; a `_` (or empty) name binds nothing.
- Source: src/compiler/sema_stmt.cpp#L5487-L5494
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_writ_pat_guard.json`

### `region.borrow-carrying.escape-tracked` — #[borrow_carrying] values are escape-tracked like references

- Divergence: Logos addition (no Rust equivalent)
- Statement: A value of a `#[borrow_carrying]` struct or enum holds a borrow into an arena and is escape-tracked like a reference; returning it escapes the borrow as if returning the bare reference. Borrow-carrying-ness propagates transitively: a struct with an inline field, or an enum with a variant payload, of a (transitively) borrow-carrying type is itself borrow-carrying, as is a container whose generic type-argument is borrow-carrying (e.g. Vec<WAny>).
- Source: src/compiler/borrow_check.cpp#L52-L54, src/compiler/borrow_check.cpp#L137-L164, src/compiler/borrow_check.cpp#L204-L227
- Rule file: `tools/spec-extract/rules/sema/borrow_check/logos.json`

### `region.borrow-carrying.residency-holder-exempt` — Residency-holder packages are exempt from borrow-carrying

- Divergence: Logos addition (no Rust equivalent)
- Statement: A struct with an Rc/Arc field (a residency-holder / laundered-escape package such as Held<T>/HeldAny) ref-counts the arena alive independent of any local, so it is NOT borrow-carrying and may safely escape — even via its type-arguments. An explicit `#[borrow_carrying]` annotation overrides this auto-exemption.
- Source: src/compiler/borrow_check.cpp#L55-L60, src/compiler/borrow_check.cpp#L165-L203, src/compiler/borrow_check.cpp#L207-L209
- Rule file: `tools/spec-extract/rules/sema/borrow_check/logos.json`

### `region.escape.borrow-carrying-type` — Borrow-carrying types and transitive containers are escape-tracked

- Divergence: Logos addition (#[borrow_carrying] arena escape model)
- Statement: A `#[borrow_carrying]` type (e.g. WAny) is escape-tracked like a reference, as is any generic container whose type-argument is transitively borrow-carrying (`Vec<WAny>`, `Option<WAny>`, `Box<WAny>`). A residency-exempt laundered-escape type (`Held<T>`/`HeldAny`) is never borrow-carrying, including via its type-arguments. A raw pointer (no type-args) is not borrow-carrying.
- Source: src/compiler/borrow_check.cpp#L1626-L1651
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part2.json`

### `region.impl.trait-arg-lifetime-erased` — Lifetime arguments at trait-argument position are not tracked for trait dispatch

- Divergence: Logos does not use regions in trait selection; Rust late-bound/early-bound lifetimes participate in coherence.
- Statement: Lifetime parameters appearing among impl trait type-arguments are skipped: regions are not tracked structurally for trait selection/dispatch.
- Source: src/compiler/sema_decl.cpp#L2020-L2022
- Rule file: `tools/spec-extract/rules/sema/sema_decl/lower_struct_def.part2.json`

### `trait.bounds.partialeq-via-eq` — PartialEq/PartialOrd satisfied by Eq/Ord impls

- Divergence: Logos Eq/Ord carry the methods Rust puts on PartialEq/PartialOrd; full split pending.
- Statement: A `T: PartialEq` bound is satisfied by an existing Eq impl, and `T: PartialOrd` by an Ord impl (alias resolution over concrete and unwrapped names).
- Source: src/compiler/sema_collect.cpp#L1110-L1131
- Rule file: `tools/spec-extract/rules/sema/sema_collect/simplify_all_types.json`

### `trait.impl.target-fnptr-erased` — impl for fn-pointer covers all fn-ptrs of an arity

- Divergence: Logos additive behavior: fn-ptr impls are arity-keyed and non-generic due to fn-ptr type erasure (no per-signature monomorphization).
- Statement: `impl<A,B,C> Trait for fn(A,B)->C` is permitted; because fn-pointers are type-erased to a uniform pointer at the Logos ABI, the impl covers every fn-pointer of the given arity and its methods are collected non-generically (one shared codegen, keyed by arity).
- Source: src/compiler/sema_collect.cpp#L2928-L2934, src/compiler/sema_collect.cpp#L2963-L2967
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_type_alias.part2.json`

### `trait.tag-dispatch.entry-emission` — Tag-dispatch entries emitted for concrete impls of tag-dispatched traits

- Divergence: Logos addition: trait tag-dispatch table for zoned/data types has no Rust analogue.
- Statement: When a trait carries `#[tag_dispatch(TS)]`, the impl is non-generic, and the concrete target datatype has a known nonzero type_code, one dispatch entry (tag-system, trait, method, fn-symbol, type-name, type-code) is emitted for each trait method that is either explicitly overridden or has a default body. The type_code is taken from the struct's annotation-applied code, else an explicit `#[type_code=N]`, else computed from a 56-bit hash of the type's canonical name with values below 128 biased into [128, ...).
- Source: src/compiler/sema_decl.cpp#L2590-L2679
- Rule file: `tools/spec-extract/rules/sema/sema_decl/lower_struct_def.part3.json`

### `trait.tagdispatch.registration-uniqueness` — At most one impl per (tag_system, trait, type_code)

- Divergence: Logos addition (tag-dispatch); analogous to Rust's coherence/orphan-style uniqueness but enforced at link time.
- Statement: Each (tag_system, trait, type_code) registration is unique program-wide: registering the same triple from two separately-compiled units is a hard error (detected as a multiply-defined link symbol). Multiple methods of one trait for one type share a single registration (deduplicated per triple, not per method).
- Source: src/compiler/mlir_gen_dyn.cpp#L188-L214, src/compiler/mlir_gen_dyn.cpp#L324-L355
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_dyn/logos.json`

### `trait.tagdispatch.registry-lookup-api` — Per-triple public dispatch-lookup function

- Divergence: Logos addition: runtime trait-method registry by type_code has no Rust analogue.
- Statement: For each (tag_system, trait, method) triple with at least one tier, a public lookup function `type_code -> fn_ptr` is exposed, checking tier-1 (with an in-range guard against the 256 bound) and falling back to tier-2, returning null when no table has the entry. This enables reflective / deferred invocation of trait methods by type_code.
- Source: src/compiler/mlir_gen_dyn.cpp#L538-L645
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_dyn/logos.json`

### `trait.tagdispatch.startup-table-init` — Dispatch tables are populated at program startup

- Divergence: Logos addition (tag-dispatch).
- Statement: Dispatch tables are zero-initialized statically and filled at program startup before user code runs (one initializer per tag system, invoked from main's prologue). Method bodies observe fully-populated tables; the dispatch tables are not const-folded per call site.
- Source: src/compiler/mlir_gen_dyn.cpp#L184-L186, src/compiler/mlir_gen_dyn.cpp#L434-L530, src/compiler/mlir_gen_dyn.cpp#L532-L536
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_dyn/logos.json`

### `trait.tagdispatch.tier-boundary-256` — Tag dispatch is two-tier with a type-code boundary of 256

- Divergence: Logos addition: tiered type-code dispatch table; no Rust analogue.
- Statement: Tag dispatch tables are split into a dense tier-1 array of 256 slots indexed directly by type_code, and a tier-2 sparse lookup function. When both exist, dispatch selects tier-1 iff type_code < 256 (unsigned), else calls the tier-2 lookup(type_code); a missing tier resolves to a null function pointer. At least one tier must exist for the call to be emitted.
- Source: src/compiler/mlir_gen_dyn.cpp#L1287, src/compiler/mlir_gen_dyn.cpp#L1366-L1370, src/compiler/mlir_gen_dyn.cpp#L1375-L1442
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_dyn/gen_tagged_dispatch.json`

### `trait.tagdispatch.tier2-binary-search-sorted` — Tier-2 dispatch requires sorted, gap-free codes

- Divergence: Logos addition (tag-dispatch).
- Statement: Tier-2 dispatch tables list only registrations whose callee is defined; the (type_code, fn) entries are sorted ascending by type_code with no zero/placeholder gaps, and resolution performs an unsigned binary search over type_code returning the paired fn or null on miss.
- Source: src/compiler/mlir_gen_dyn.cpp#L82-L128, src/compiler/mlir_gen_dyn.cpp#L378-L391
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_dyn/logos.json`

### `trait.tagdispatch.two-tier-codespace` — type_code space is split into two dispatch tiers

- Divergence: Logos addition (tag-dispatch).
- Statement: The type_code key space is partitioned at 256: codes in [1,255] dispatch via a dense direct-indexed table of fixed size 256 (tier-1, O(1) index); codes >= 256 dispatch via a sorted (type_code, fn) pair table searched by binary search (tier-2, O(log n)). A lookup that hits neither tier yields null (no matching impl).
- Source: src/compiler/mlir_gen_dyn.cpp#L181-L186, src/compiler/mlir_gen_dyn.cpp#L239-L240, src/compiler/mlir_gen_dyn.cpp#L316-L320, src/compiler/mlir_gen_dyn.cpp#L604-L645
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_dyn/logos.json`

### `trait.tagdispatch.type-code-keyed` — Tag-based dispatch keys on a runtime type-code read from the receiver

- Divergence: Logos addition: runtime type-code/TagSystem dispatch has no direct Rust analogue (Rust uses vtables only).
- Statement: A `#[tag_dispatch]`-style trait call resolves the target method at runtime by (1) reading an integer `type_code` from the receiver value via the trait's TagSystem `read_tag(self=null, obj_ptr) -> i64`, then (2) indexing a per-(tag_system, trait, method) dispatch structure by that type_code to obtain the method function pointer, then (3) calling it indirectly with the receiver pointer as `self: *const u8` followed by the user args. The TagSystem is a stateless unit struct (self passed as null).
- Source: src/compiler/mlir_gen_dyn.cpp#L1308-L1356, src/compiler/mlir_gen_dyn.cpp#L1360-L1364, src/compiler/mlir_gen_dyn.cpp#L1444-L1474
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_dyn/gen_tagged_dispatch.json`

### `trait.tagdispatch.type-code-keyed` — Tagged dynamic dispatch is keyed by per-type type_code

- Divergence: Logos addition: tag-dispatch dispatch model has no Rust analogue (Rust uses fat-pointer vtables only).
- Statement: Tag-dispatch (an alternative to vtable-based dyn dispatch) selects a method implementation at runtime by a per-concrete-type integer `type_code` (logically u64). For each (tag_system, trait, method) triple a dispatch table maps `type_code -> fn_ptr`; a runtime lookup with `type_code == 0` is treated as 'no impl registered'.
- Source: src/compiler/mlir_gen_dyn.cpp#L181-L186, src/compiler/mlir_gen_dyn.cpp#L302-L321
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_dyn/logos.json`

### `type.array.size-from-metacall` — Array size from metacall

- Divergence: Logos: comptime sizing via explicit metacall (see explicit-metacall design).
- Statement: `[T; metacall { ... }]` permits a compile-time metacall block as the array size expression.
- Source: tools/peg_gen/grammars/logos.peg#L1769-L1770
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-never_type.json`

### `type.array.size-from-pack` — Array size from variadic pack length

- Divergence: Logos addition: pack-length array sizing.
- Statement: `[T; P...(P)]` sizes the array from a variadic pack length; lowered to symbolic array-size-var `__sizeof_pack:P` and resolved at monomorphization.
- Source: tools/peg_gen/grammars/logos.peg#L1762-L1768
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-never_type.json`

### `type.copy.struct-structural-auto` — Structural auto-Copy for plain-data structs

- Divergence: Logos auto-derives Copy structurally; Rust requires explicit `#[derive(Copy)]`. Capability-equivalent (a Copy type stays usable after by-value use).
- Statement: A plain-data `struct` with no `impl Drop` and at least one field, whose every field type is Copy, is itself Copy — no `#[derive(Copy)]` opt-in is required. Determined by fixpoint over the struct dependency graph (a struct may become Copy once all its struct-typed fields are known Copy). Zero-field structs are not auto-promoted.
- Source: src/compiler/sema.cpp#L2867-L2880, src/compiler/sema.cpp#L2955-L2981
- Rule file: `tools/spec-extract/rules/sema/sema/normalize_assoc_eq.json`

### `type.identity.cfg-slot` — Config-slot type identity = (cfg-typevar name, slot key)

- Divergence: Logos addition (zone/config slots)
- Statement: A config-slot type is identified by the pair (config type-variable name, slot key); distinct slots intern to distinct types.
- Source: src/compiler/sema.cpp#L923-L929, src/compiler/sema.cpp#L1050-L1052
- Rule file: `tools/spec-extract/rules/sema/sema/install_snapshot.json`

### `type.identity.wstatic-config` — WritStatic-literal type identity = its byte-hash

- Divergence: Logos addition (WritStatic const-config type parameters)
- Statement: A type parameterized by a WritStatic literal config (`Foo::<@{...}>`) is identified by the byte-hash of that literal; distinct configurations instantiate to distinct types and do not dedupe.
- Source: src/compiler/sema.cpp#L917-L922
- Rule file: `tools/spec-extract/rules/sema/sema/install_snapshot.json`

### `type.impl-trait.param-position-forbidden` — `impl Trait` not allowed at parameter position

- Divergence: Logos restriction: Rust supports argument-position impl Trait (APIT).
- Statement: `impl Trait` is not supported in parameter position; use an explicit generic `fn f<T: Trait>(x: T)` or `&dyn Trait` instead.
- Source: src/compiler/sema_decl.cpp#L309-L318
- Rule file: `tools/spec-extract/rules/sema/sema_decl/logos.part1.json`

### `type.integer.kind-set` — Integer-class type kinds

- Divergence: Logos adds non-power-of-two integer widths i24/u24/i56/u56 (not in Rust); also classifies Enum as an integer kind.
- Statement: The integer type class comprises the fixed-width signed/unsigned kinds {i8,u8,i16,u16,i24,u24,i32,u32,i56,u56,i64,u64,i128,u128}, the pointer-sized {usize,isize}, the unsuffixed-literal type IntLit, and Enum. An enum type is treated as an integer kind for these classifications.
- Source: src/compiler/sema_impl.hpp#L4439-L4449
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/is_integer_kind.json`

### `type.param.unit-type-forbidden` — Unit-typed parameters forbidden

- Divergence: Logos restriction: Rust permits `()`-typed parameters.
- Statement: A function parameter may not have the unit type `()`; a unit-typed parameter carries no information and is ill-formed.
- Source: src/compiler/sema_decl.cpp#L303-L308
- Rule file: `tools/spec-extract/rules/sema/sema_decl/logos.part1.json`

### `type.pin.non-movable-classification` — Non-movable (location-anchored) type classification

- Divergence: Logos addition (zones/pin): `#[pinned]`/`#[zoned2]`/`#[rel_ptr]` anchoring has no Rust analog.
- Statement: A type is non-movable iff: it is a `#[pinned]` struct; or a `#[zoned2]` struct (self-relative pointer fields anchored to their own slot); or it inlines (transitively through struct/tuple/array by-value fields, not through pointers/references) a `#[rel_ptr]` or `#[pinned]` field. A `#[rel_ptr]` type itself is movable (its value-form is the resolved absolute pointer); it counts as non-movable only when embedded as an inline field.
- Source: src/compiler/sema_impl.hpp#L2096-L2146, src/compiler/sema_impl.hpp#L2126-L2142
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/logos.part4.json`

### `type.ptr.zoned` — Zoned raw pointer `*zoned [mut] T`

- Divergence: Logos addition (F3 ref-repr design): zoned pointers, no Rust equivalent.
- Statement: `*zoned T` / `*zoned mut T` is a zoned raw pointer (Ref-arm self-relative at rest; deref/assign runs the storage↔compute bridge). `zoned` is a contextual keyword recognized only in pointer position (a bare IDENT after `*`), validated as NAME=="zoned" by sema; it is not globally reserved.
- Source: tools/peg_gen/grammars/logos.peg#L1750-L1759
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-never_type.json`

### `type.ptr.zoned-pointer-distinct` — *zoned T is a distinct pointer type

- Divergence: Logos addition (F3 ref-repr/zoned types); no Rust equivalent.
- Statement: A zoned raw pointer `*zoned T` is a type distinct from `*T`; the zoned bit participates in type identity (interning, serialization, equality). Deref/assignment through a `*zoned T` runs the zoned storage↔compute bridge rather than a plain load/store.
- Source: src/compiler/sema_impl.hpp#L222-L231
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/logos.part1.json`

### `type.ref-repr.rel-ptr-self-relative` — #[rel_ptr] and #[zoned2] pointers are stored self-relative

- Divergence: Logos addition: self-relative pointer storage (persistent/Writ model), no Rust equivalent.
- Statement: A #[rel_ptr] struct is represented as a self-relative pointer: 8-byte i64 offset storage, resolved to an absolute thin pointer on access. Additionally, a thin-pointer field of a #[zoned2] struct is stored self-relative (offset) rather than absolute.
- Source: src/compiler/mlir_gen_types.cpp#L579-L586, src/compiler/mlir_gen_types.cpp#L594-L603
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_types/logos.json`

### `type.ref-repr.zone-mut-fat` — &mut to a #[zone_mut] type is a fat zone-carrying ref

- Divergence: Logos addition: zone-carrying mutable references (Writ/zone model), no Rust equivalent.
- Statement: A &mut T where T is a #[zone_mut] struct is a fat reference {data, zone} carrying its allocator/zone pointer, so growth methods reach the allocator through &mut self. A shared &T or *T to the same type stays thin (reads never grow).
- Source: src/compiler/mlir_gen_types.cpp#L552-L565
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_types/logos.json`

### `type.ref.dotted-path` — Fully-qualified non-generic type path

- Divergence: Logos path model: `.` for package/module path, `::` for items.
- Statement: A fully-qualified non-generic type in type position is written `pkg.path.Type` (dotted); the last path segment is the type. Matched before bare-IDENT alternatives so the whole dotted form is claimed. The generic dotted form `pkg.path.Type<A>` is not supported (use a `use` import + short name).
- Source: tools/peg_gen/grammars/logos.peg#L1805-L1813
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-never_type.json`

### `type.ref.metavar` — Metavariable type reference

- Divergence: Logos metaprogramming addition.
- Statement: `#Ident` and `#(expr)` are type references whose name is supplied by a metaprogram variable/expression rather than a literal identifier.
- Source: tools/peg_gen/grammars/logos.peg#L1801-L1804
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-never_type.json`

### `type.str.slice-alias` — str is an alias for Slice<u8>; impls aliased to &[u8]

- Divergence: Logos models `str` as Slice<u8>; Rust `str` is a distinct DST.
- Statement: `str` is a built-in that resolves to Slice<u8> (printed `&[u8]`); a trait impl whose target is `str` is also registered under target `&[u8]` so trait-satisfaction checks keyed on the printed slice type find the impl.
- Source: src/compiler/sema_collect.cpp#L3777-L3787
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_type_alias.part3.json`

### `type.tagged.thin-ptr-dispatch` — &tagged<TS> Trait

- Divergence: Logos-only tagged-dispatch pointer.
- Statement: `&tagged<TS> Trait` resolves to a thin TaggedPtr with tag-based dispatch; Trait must be a registered trait and TS must resolve to a concrete struct type, else hard error.
- Source: src/compiler/sema.cpp#L6021-L6039
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_type.json`

### `type.tuple.variadic-arity` — Variadic-arity tuple target `(A...)`

- Divergence: Logos addition: variadic tuple impls (no direct Rust equivalent).
- Statement: `(A...)` is a variadic-arity tuple type naming pack-typevar A; used as an impl target `impl<A...> Trait for (A...)`. Resolves to a Tuple type with one variadic element naming A.
- Source: tools/peg_gen/grammars/logos.peg#L1726-L1731
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-never_type.json`

### `type.typeof.expr-type-no-eval` — typeof(expr) yields the sema type without evaluation

- Divergence: Logos addition: Rust has no `typeof` operator.
- Statement: `typeof(expr)` resolves to the sema-computed type of `expr`; the expression is type-checked but never evaluated at runtime.
- Source: src/compiler/sema.cpp#L5673-L5681
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_type.json`

### `type.writ-arr.elem-set` — Writ typed array type <Elem>[]

- Divergence: Logos-only Writ container type-expression.
- Statement: `<Elem>[]` resolves to a generic struct `WritArr<elem>`; Elem must be one of I8/U8/I16/U16/I32/U32/I64/U64/F32/F64 (mapped to the Logos primitive), else hard error.
- Source: src/compiler/sema.cpp#L6234-L6266
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_type.json`

### `type.writ-map.key-val-set` — Writ typed map type <K,V>{}

- Divergence: Logos-only Writ container type-expression.
- Statement: `<K,V>{}` resolves to `WritMap<key,val>`; key must be I32/U32/I64/U64 and value must be `AnyVal` (default), else hard error.
- Source: src/compiler/sema.cpp#L6267-L6297
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_type.json`

### `type.wstatic.literal-arg` — WritStatic literal in type-arg position

- Divergence: Logos-only WritStatic value-as-type-arg.
- Statement: A WritStatic literal `Foo::<@{...}>` (or a bare writ-lit value-AST in const recognition) resolves to the value's WritStatic type; a missing payload is a hard error.
- Source: src/compiler/sema.cpp#L6370-L6386
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_type.json`

## Descriptive / Rust-relation notes (untagged)

Divergence notes without a leading tag — prose comparisons to Rust, implementation notes, and conformance affirmations. Triage individually; many are Rust-conformant clarifications rather than true divergences.

### `borrow.assign.static-mut-unsafe` — static mut write requires unsafe; immutable static not writable

- Divergence: Rust-conformant (items.static.mut.safety)
- Statement: A write to a place rooted at a `static mut` is permitted (storage is mutable) but requires an `unsafe` block; a write to a plain immutable `static` is rejected.
- Source: src/compiler/sema_stmt.cpp#L6981-L6994
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/lower_loop.json`

### `borrow.closure.capture-by-ref-loan` — Non-move closure captures register field-path borrows of captured places

- Divergence: RFC-2229 disjoint closure capture: field-path precision, but a whole-var SHARED capture is treated as a liveness check only (not a recorded shared borrow) to avoid blocking sibling mutation
- Statement: A non-`move` closure capturing place `p` by reference registers a borrow of `p` held for the closure value's lifetime: a mutated/`&mut` capture registers a `&mut` (exclusive) loan, a shared capture registers a `&` (shared) loan. Captures of a strict sub-field `p.x` register a precise FIELD-PATH borrow (so disjoint sibling access `&mut p.y` beside `|| p.x` is allowed, conflicting `&mut p.x` is rejected); a whole-root capture registers a whole-value borrow for `&mut` (or a liveness check for shared). The loan is released at the closure holder's last use (NLL).
- Source: src/compiler/borrow_check.cpp#L2233-L2267
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part4.json`

### `borrow.escape.borrow-carrying-struct` — borrow_carrying struct values are escape-tracked like references

- Divergence: A: #[borrow_carrying] Logos addition for opaque borrow-holding types (WAny).
- Statement: Values of a struct annotated `#[borrow_carrying]` are tracked by the borrow checker for escape/lifetime like ordinary references.
- Source: src/compiler/sema_decl.cpp#L1197-L1199, src/compiler/sema_decl.cpp#L1412
- Rule file: `tools/spec-extract/rules/sema/sema_decl/lower_struct_def.part1.json`

### `borrow.field.mutability-through-reference-root` — Field mutation legality from reference-typed root

- Divergence: Rust E0596 conformant
- Statement: When the root `x` of a field place `x.p` has reference type, mutation legality is determined by the reference TYPE not the binding's `mut`: a `&mut`-typed root permits `&mut x.p`; a `&`-typed (shared) root rejects `&mut x.p` (E0596); the mut-binding declaration check is skipped for reference-typed roots.
- Source: src/compiler/borrow_check.cpp#L1192-L1209
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part2.json`

### `borrow.generic.exclusivity-only-pre-mono` — Generic templates borrow-check exclusivity only, deferring move checks

- Divergence: Implementation strategy, not a user-visible language divergence; final move-checking is Rust-conformant on concrete instantiations.
- Statement: When borrow-checking a generic function template before monomorphization, only borrow-exclusivity conflicts are reported; move/use-after-move diagnostics are suppressed (imprecise over TypeVar values) and are fully checked on each monomorphized specialization.
- Source: src/compiler/borrow_check.cpp#L3180-L3186
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part5.json`

### `borrow.let.ref-from-temp-dangles` — A let-bound reference borrowing into a per-statement temporary is rejected (E0716)

- Divergence: Rust E0716 (temporary dropped while borrowed)
- Statement: Binding a reference (or borrow-carrying value) whose provenance is a temporary value dropped at the end of the binding statement is an error: the reference would outlive the temporary it borrows into. The owning value must first be bound to a variable so it outlives the borrow.
- Source: src/compiler/borrow_check.cpp#L2701-L2714
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part4.json`

### `borrow.move.borrowed-cannot-move` — Cannot move a borrowed value

- Divergence: Rust E0505 conformant
- Statement: Moving `x` is rejected while `x` is mutably borrowed, shared-borrowed, has a mut reservation in flight, or while any field of `x` is borrowed (E0505); a successful move resets `x`'s state and records it as moved.
- Source: src/compiler/borrow_check.cpp#L1402-L1415
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part2.json`

### `borrow.move.no-move-out-of-array-index` — Cannot move a Drop-bearing element out of a fixed-size array by index

- Divergence: Rust E0508 analog.
- Statement: Moving by value out of a fixed-size array element via index (`let s = arr[i]`) is rejected when the element type is concrete and needs Drop, because a single array slot cannot be marked moved (the array would still drop it → double free). Generic element types (TypeVar/AssocType/ImplTrait) are exempted, as are borrows/autoref which do not move.
- Source: src/compiler/sema_impl.hpp#L2209-L2248
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/logos.part4.json`

### `borrow.move.no-move-out-of-borrowed-place` — Cannot move a move-typed value out of a borrowed place (E0507)

- Divergence: Box move-out (`let s = *b`) is rejected because Logos does not implement Rust's built-in Box DerefMove; in Rust it is allowed.
- Statement: Moving a move-typed value by value out of a non-owning place is rejected (E0507): deref of a `&`/`&mut` reference variable (`*r`); index `v[i]`/slice-index `s[i]` of a non-raw container (including user `Index`, lowered to `*v.index(i)`); deref of a user `Deref` (`*x.deref()`) including `Box` (`*b`, since DerefMove is unimplemented); and reading a move-typed field out of a `&`/`&mut` receiver (`r.field`). Exempt: any place whose access chain passes through a raw-pointer (`*const`/`*mut`) hop, and partial moves out of owned receivers.
- Source: src/compiler/sema_impl.hpp#L877-L968
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/logos.part2.json`

### `borrow.move.tuple-element-moved` — Concrete move-type tuple elements are moved into the tuple

- Divergence: TypeVar tuple elements are leniently exempt from move-tracking (note G154-4)
- Statement: A concrete (non-TypeVar) move-type value placed into a tuple element is moved into the tuple (its source binding is marked consumed); TypeVar elements are exempt (their drop is routed through the mono mechanism).
- Source: src/compiler/sema_expr.cpp#L1621-L1630
- Rule file: `tools/spec-extract/rules/sema/sema_expr/try_struct_unsize_coerce.json`

### `borrow.pass.generic-template-checked` — Generic fn bodies are borrow-checked even when never instantiated

- Divergence: Rust-conformant (uninstantiated generics are still checked).
- Statement: A dedicated pre-monomorphization pass borrow-checks generic function bodies directly (exclusivity-only mode, no region inference, imprecise move tracking on TypeVars), so an uninstantiated generic is still checked. The post-mono pass checks concrete functions and specializations with full region inference. Functions loaded from a precompiled binary module and extern functions are skipped (already checked when their layer was built).
- Source: src/compiler/borrow_check.cpp#L3788-L3818, src/compiler/borrow_check.cpp#L3849-L3852
- Rule file: `tools/spec-extract/rules/sema/borrow_check/visit.json`

### `borrow.region.dangling-after-scope-exit` — Use of reference after referent leaves scope (E0597)

- Divergence: Rust NLL E0597 conformant; line-granular release (DIVERGENCES B6 closed)
- Statement: A reference/borrow-carrying binding whose referent local has gone out of scope is flagged dangling; the first subsequent use reports E0597 ('does not live long enough'), once per binding. A stored borrow never used after its referent dies is sound (NLL).
- Source: src/compiler/borrow_check.cpp#L1433-L1456
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part2.json`

### `borrow.scope.stored-borrow-outlives-referent` — Every binding records its borrow sources for end-of-scope outlives checking (E0597)

- Divergence: Rust E0597 (borrowed value does not live long enough)
- Statement: Every `let` binding records the local borrow sources of its value, so that at scope exit a stored borrow that outlives its referent can be detected and rejected.
- Source: src/compiler/borrow_check.cpp#L2728-L2730
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part4.json`

### `borrow.scoped.index-reborrow-borrows-receiver` — &v[i] borrows the whole indexed container (its receiver)

- Divergence: Element borrow is whole-container coarse (rustc E0499 parity for aliasing two `&mut v[i]`).
- Statement: An indexing reference `&v[i]` / `&mut v[i]` desugars to `&*(Vec::index(&v,i))`; the borrow is recorded on the index method's receiver (the whole container v), so a `v.push()` while the element ref is live is rejected (iterator/element invalidation). `&mut v[i]` forces the receiver borrow to be mutable even when the desugared index_mut self-kind is unresolved.
- Source: src/compiler/borrow_check.cpp#L2026-L2047
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part3.json`

### `borrow.take.call-arg-mut-reservation` — Two-phase borrow reservation during call-argument evaluation

- Divergence: Rust two-phase borrow conformant
- Statement: Inside function-call argument evaluation, a `&mut x` is taken as a reservation that is compatible with shared borrows of `x` created during the same argument evaluation, but is rejected if any shared borrow of `x` pre-exists from an outer scope. Two `&mut x` in the same call (overlapping reservations) still conflict.
- Source: src/compiler/borrow_check.cpp#L1278-L1321
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part2.json`

### `coerce.anyval.let-binds-i32` — AnyVal-typed let binds an i32

- Divergence: No Rust equivalent (AnyVal is a Logos addition).
- Statement: A binding declared with type `AnyVal` coerces its RHS to a 32-bit integer and stores it as a scalar binding.
- Source: src/compiler/mlir_gen_stmt.cpp#L1444-L1454
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_stmt/gen_let.json`

### `coerce.binop.autoderef-numeric-ref` — Auto-deref reference operand to primitive in scalar binops

- Divergence: Models Rust's `impl Add<i32> for &i32` family via auto-deref rather than blanket ref impls.
- Statement: For binary operators in {+,-,*,/,%,<,<=,>,>=,==,!=,&,|,^,<<,>>}, an operand of type &T or &mut T whose pointee T is an integer, f32, f64, bool, or char is implicitly dereferenced to T before operator resolution; struct pointees are not peeled.
- Source: src/compiler/sema_expr.cpp#L1718-L1742
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_binop.json`

### `coerce.cast.ref-to-scalar-autoderef` — &T as scalar auto-derefs the reference

- Divergence: RFC 2005 match-ergonomics interaction; behaviorally Rust-conformant (note T2-26)
- Statement: When casting a value of type `&T`/`&mut T` (with scalar pointee T) to a scalar target (any integer/usize/isize/f32/f64/char/bool), the operand is auto-dereferenced before the cast, so the pointee value is converted, not the pointer bits. Pointer→pointer casts and `&T as *T`/`as usize` reinterpretations are unaffected.
- Source: src/compiler/sema_expr.cpp#L841-L875
- Rule file: `tools/spec-extract/rules/sema/sema_expr/try_struct_unsize_coerce.json`

### `coerce.cast.supertrait-upcast` — Supertrait upcast preserves data, swaps to super vtable

- Divergence: Rust-conformant (trait upcasting); vtable layout {drop,size,align, methods…, super-vtables…} is Logos-specific.
- Statement: `&dyn Sub`/`dyn Sub` cast to `&dyn Super` (Sub ≠ Super, Super a supertrait of Sub) keeps the SAME data pointer and replaces the vtable with Super's vtable, recovered from a stored super-vtable-pointer slot in Sub's vtable at index `3 + |methods(Sub)| + idx(Super)`. Identity dyn casts (Sub == Super) fall through to the no-op reinterpret.
- Source: src/compiler/mlir_gen_expr.cpp#L3254-L3303
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_expr/gen_expr_kind-2.json`

### `coerce.fn.fnitem-to-fnptr` — FnItem coerces to a matching FnPtr; not the reverse, not FnItem to FnItem

- Divergence: logos-core 1.4: FnItem (ZST per-fn identity) auto-coerces to FnPtr; Rust models the analogous fn-item to fn-pointer coercion.
- Statement: A FnItem value coerces to an FnPtr at every value-use site iff arity matches and each param and the return type are pairwise compatible. FnPtr to FnItem is rejected, and two distinct FnItems with identical signatures are not mutually compatible (distinct fn identity).
- Source: src/compiler/sema.cpp#L1816-L1826
- Rule file: `tools/spec-extract/rules/sema/sema/types_equal.json`

### `coerce.infer.placeholder-unifies` — Inference placeholder _ unifies in either direction

- Divergence: logos-core 1.3
- Statement: If either side is the InferredType placeholder (_), the pair is compatible; actual resolution is deferred to the surrounding annotation/RHS unifier.
- Source: src/compiler/sema.cpp#L1836-L1840
- Rule file: `tools/spec-extract/rules/sema/sema/types_equal.json`

### `coerce.int.implicit-widening` — Safe implicit integer widening

- Divergence: Rust performs NO implicit integer widening at all (requires explicit `as`). Logos permits value-preserving implicit widening here.
- Statement: An implicit integer widening from `from` to `to` is permitted iff every value of `from` is representable in `to`: signed->signed and unsigned->unsigned require to_width >= from_width; unsigned->signed requires to_width > from_width; signed->unsigned is never permitted. usize/isize are distinct types: no implicit conversion between a pointer-sized integer and any fixed-width integer (only psize<->psize among themselves). Either operand having undefined rank (IntLit/Enum/non-integer) blocks widening.
- Source: src/compiler/sema_impl.hpp#L4482-L4495
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/is_integer_kind.json`

### `coerce.let.implicit-int-widening` — Implicit safe integer widening at let-init

- Divergence: Rust requires an explicit `as` cast for any integer width change; Logos performs implicit safe widening.
- Statement: At a let-init coercion site, a concrete (non-IntLit, non-enum) integer RHS whose type can safely widen to the annotated integer type is implicitly widened (e.g. u32→i64, i32→i64, u8→u32) without an explicit `as`.
- Source: src/compiler/sema_stmt.cpp#L2054-L2061
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/lower_let_else.json`

### `coerce.never.subtype-of-all` — Never (!) is a subtype of every type; T to ! rejected

- Divergence: logos-core 1.1: T to ! previously accepted, now rejected to match Rust.
- Statement: Never coerces to any type T (Never to T accepted unconditionally). The reverse T to Never is rejected.
- Source: src/compiler/sema.cpp#L1827-L1835
- Rule file: `tools/spec-extract/rules/sema/sema/types_equal.json`

### `coerce.struct.elementwise-typeargs` — Same-named structs compatible iff type-args pairwise compatible

- Divergence: logos-core 1.3 (nested)
- Statement: Two Struct types with equal struct_name and pkg_name and equal type-arg arity are compatible iff every type-arg pair is compatible (allowing inference holes like Vec<_> vs Vec<i32>).
- Source: src/compiler/sema.cpp#L1846-L1857
- Rule file: `tools/spec-extract/rules/sema/sema/types_equal.json`

### `coerce.unsize.ref-concrete-to-trait-object` — Reference/pointer to concrete unsizes to bare trait object

- Divergence: Uniform-fat model: `&dyn` and `*mut dyn` are both 16-byte fat pairs (Logos), unlike Rust where only references unsize.
- Statement: `&T`/`&mut T`/`*const T`/`*mut T` (T a concrete struct or primitive) cast to a bare trait object synthesizes a {data,vtable} fat pair; the vtable keys on T's concrete struct name (or the primitive's bare type name for a blanket-impl `&i64 as &dyn`). Only fires when the source pointee is concrete; a `&dyn`→`dyn` reinterpret (pointee already a trait object) is a no-op.
- Source: src/compiler/mlir_gen_expr.cpp#L3406-L3432
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_expr/gen_expr_kind-2.json`

### `const.binop.intlit-fold-overflow` — Integer-literal arithmetic is folded; i64 overflow is rejected

- Divergence: Rust folds in the inferred type; Logos folds in i64 and errors on i64 overflow, deferring per-type fit to the coercion site.
- Statement: When both arithmetic operands are integer literals with recoverable values, +,-,*,/,% are constant-folded to a single integer literal (of untyped IntLit type); if the fold overflows i64 the expression is rejected rather than silently wrapped.
- Source: src/compiler/sema_expr.cpp#L2319-L2355
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_binop.json`

### `const.binop.shift-count-overflow-width` — Literal shift count >= LHS bit-width rejected

- Divergence: usize/isize fixed at 64-bit (target-specific).
- Statement: << or >> whose shift count is a literal value >= the bit-width of the left operand's type is a compile-time error (shifting by >= width is undefined); widths: i8/u8=8, i16/u16=16, i24/u24=24, i32/u32=32, i56/u56=56, i64/u64=64, i128/u128=128, usize/isize=64.
- Source: src/compiler/sema_expr.cpp#L2424-L2453
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_binop.json`

### `const.def.writ-static-literal-compat` — WStaticLit initializer is compatible with a WritStatic const

- Divergence: A: Writ-static literal coercion is a Logos addition.
- Statement: A const whose declared type is a WritStatic struct accepts an initializer whose type is a Writ-static literal (WStaticLit); this combination is treated as type-compatible.
- Source: src/compiler/sema_decl.cpp#L1549-L1554
- Rule file: `tools/spec-extract/rules/sema/sema_decl/lower_struct_def.part1.json`

### `const.enum.discriminant` — Enum discriminant value forms

- Divergence: Cross-enum discriminant reference `OtherEnum::Variant` as a discriminant value has no Rust analog.
- Statement: A variant discriminant `Name = D` may be: a bare (optionally negated) integer literal that is the complete value (no trailing binary operator); `metacall <block>`; a cross-enum reference `OtherEnum::Variant` (with optional `as T` cast whose type is dropped, width governed by the enclosing enum's backing/repr); or a general constant expression evaluated via CTFE. A bare literal alt only matches when no binary operator follows; otherwise the value falls through to the const-expr alternative.
- Source: tools/peg_gen/grammars/logos.peg#L788-L812, tools/peg_gen/grammars/logos.peg#L760-L763
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-module.json`

### `expr.assign.drop-before-replace` — Field assignment drops old value first

- Divergence: Rust-conformant (expr.assign.drop-target / B8)
- Statement: Assigning to a field place over an owned local root drops the place's prior value before the store, provided the value is live (root owned, definitely-initialized, no overlapping moved-out path) and droppable; assigning to a path also lifts drop-suppression for the covered (equal-or-deeper) moved paths so the scope-end drop releases the new value.
- Source: src/compiler/sema_stmt.cpp#L7237-L7299, src/compiler/sema_stmt.cpp#L7450-L7455, src/compiler/sema_stmt.cpp#L7461-L7462
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/lower_loop.json`

### `expr.assign.union-field-safe` — Writing a union field is safe

- Divergence: Rust-conformant (items.union.fields.write-safety)
- Statement: Writing to a union field is safe (no `unsafe` required for the write): the place-write LHS suppresses the union unsafe gate that otherwise applies when reading a union field.
- Source: src/compiler/sema_stmt.cpp#L7325-L7331
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/lower_loop.json`

### `expr.binop.comparison-signedness` — Ordering comparisons select signed/unsigned by type

- Divergence: bool ordering forced unsigned to preserve Rust's `false < true` despite i1 signed representation; documented inline as Rust-conformant intent.
- Statement: `<`/`>`/`<=`/`>=` use unsigned comparison when the LHS type is unsigned (u8..u128) or bool, signed comparison otherwise. bool is treated as unsigned so that `false < true` holds (i1 false=0 < true=1).
- Source: src/compiler/mlir_gen_expr.cpp#L1134-L1156
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_expr/gen_expr_kind.json`

### `expr.binop.integer-overflow-trap` — Checked +/-/* trap on overflow

- Divergence: Always traps on overflow (B-ex-01) regardless of build profile, unlike Rust where overflow wraps in release and panics in debug.
- Statement: Integer `+`, `-`, `*` are checked: on overflow execution aborts (trap). Signed/unsigned overflow detection selects checked signed vs unsigned arithmetic by the LHS type's signedness. Intentional wrapping must use the `wrapping_add`/`wrapping_sub`/`wrapping_mul` intrinsics, which emit the unchecked operation.
- Source: src/compiler/mlir_gen_expr.cpp#L835-L884
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_expr/gen_expr_kind.json`

### `expr.binop.string-vs-str-eq` — String == str views String as str

- Divergence: Mirrors Rust `impl PartialEq<str> for String`.
- Statement: For == and !=, when one operand is the struct String and the other is str (Slice<u8>), the String operand is viewed as str via .as_str() so the comparison proceeds through the str equality path.
- Source: src/compiler/sema_expr.cpp#L1782-L1808
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_binop.json`

### `expr.block.tail-return-adopts-value-type` — Block ending in `return e` adopts e's type

- Divergence: No real `!`/never subtyping for tail-return; the return-value's type is adopted as a block-type proxy instead of `!`.
- Statement: A block whose final statement is `return e` is non-diverging in the value system: the block's result type is taken as `typeof(e)` even though no value is produced, so the divergent block is usable at a non-void expected type (e.g. inside a tuple/struct literal). The `return` is still lowered and executed.
- Source: src/compiler/sema_expr.cpp#L13664-L13672, src/compiler/sema_expr.cpp#L13706-L13720
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_block_expr.part1.json`

### `expr.call.callable-field` — Call of a callable struct field

- Divergence: Rust requires explicit `(s.m)(args)` to call a callable field; bare `s.m(args)` is method-only
- Statement: If `s.m(args)` finds no method `m` but struct `s` has a field named `m` whose type is a fn-pointer/fn-value or closure, the expression is lowered as a field read followed by a fn-ptr call (fn-value kind) or closure call (closure kind), returning that callable's return type.
- Source: src/compiler/sema_expr.cpp#L8701-L8728
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_method_call.part4.json`

### `expr.call.static-turbofish-before-method` — Static-call turbofish precedes method name

- Divergence: Rust places the turbofish after the method for trait/inherent fns (e.g. T::method::<U>); Logos surface form puts it before the method name on the type path.
- Statement: In an associated/static call, turbofish type arguments attach to the receiver type and precede the `::method` segment: `Recv::<T>::method(args)`.
- Source: src/compiler/sema_render.cpp#L203-L241
- Rule file: `tools/spec-extract/rules/sema/sema_render/logos.json`

### `expr.closure.mutated-capture-by-reference` — Mutated captures are captured by reference

- Divergence: Capture mode is inferred per-variable from usage (read-only vs mutated), conceptually aligned with Rust closure capture-mode inference.
- Statement: A captured variable that is the target of a mutation in the body (assignment / field write / index write / deref write) is captured by reference so the mutation propagates to the outer binding rather than to a local env copy. A write-only target (no prior read of its base) is still added to the capture set as a whole-variable capture.
- Source: src/compiler/sema_expr.cpp#L14395-L14420
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_block_expr.part2.json`

### `expr.cmp.non-chainable` — Comparison operators are non-chainable

- Divergence: Rust-conformant outcome (chained comparison is an error); Logos detects it grammatically for a better diagnostic.
- Statement: Comparison operators are non-chainable: at most one comparison per level is well-formed. A chain of 2+ comparators (e.g. `a < b < c`) is parsed as a distinct CHAINED_CMP node so sema can reject it with a dedicated diagnostic rather than a generic syntax error.
- Source: tools/peg_gen/grammars/logos.peg#L2589-L2600, tools/peg_gen/grammars/logos.peg#L2424-L2431
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-compound_assign_op.json`

### `expr.compound-assign.opassign-dispatch` — Compound-assign dispatches via *Assign impl when present

- Divergence: Rust-conformant operator-overload semantics; Logos struct-name-keyed impl lookup.
- Statement: For a place of struct type S, if an impl of the operator's *Assign trait exists for S (matched by concrete or base struct name), `place op= rhs` lowers to the in-place call `op_assign(&mut place, rhs)` (void result, no assign-back). The trait method's Rhs parameter need not equal Self: the impl is selected by the actual rhs operand type, falling back to the Self-Rhs signature if the rhs-typed one does not resolve.
- Source: src/compiler/sema_stmt.cpp#L2315-L2351, src/compiler/sema_stmt.cpp#L2484-L2509
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/op_assign_trait_method.json`

### `expr.deref.non-pointer-identity` — Dereference of a non-pointer value is identity

- Divergence: Rust rejects `*x` on a non-pointer; Logos relaxes it to identity to accept faithful Rust loop imports (B3-bg-07), since `for i in &v` already yields T not &T.
- Statement: `*x` where `x` is neither a raw pointer nor a reference (and has no Deref impl) yields `x` unchanged rather than an error.
- Source: src/compiler/sema_expr.cpp#L2669-L2680
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_unary.part1.json`

### `expr.drop.tuple-array-reverse` — Tuple and array element drop in reverse order

- Divergence: Rust drops array elements in forward (index-ascending) order; tuple reverse-order is conformant. Array order here is N forward but element-by-element; flagged as possibly observable only via Drop side effects.
- Statement: Dropping a tuple drops its droppable elements in reverse index order; dropping a fixed array [T;N] drops each of the N elements when T is droppable. Ref/ptr elements and non-droppable elements are skipped, and statically moved-out tuple element positions are suppressed.
- Source: src/compiler/mlir_gen_stmt.cpp#L922-L938, src/compiler/mlir_gen_stmt.cpp#L985-L995
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_stmt/gen_drop_owning_dst.json`

### `expr.field.dst-prefix-positional` — Prefix (non-tail) field access on a DstRef is positional

- Divergence: Custom-DST model — see DIVERGENCES B2.
- Statement: For a fat-pointer receiver to a custom-DST struct, a non-tail prefix field is addressed positionally: its byte offset is computed by walking the sized prefix fields (with the DstRef's type-args substituted), and the field is read by dereferencing `data_ptr + offset` typed as the field type. This works uniformly for generic and non-generic DST instances, including those with no registered monomorphized layout.
- Source: src/compiler/sema_expr.cpp#L9394-L9429
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_field_read.part1.json`

### `expr.field.dst-ref-unsafe` — Field read through a non-self-describing &DstStruct requires unsafe

- Divergence: Custom-DST raw-pointer-shaped field access — see DIVERGENCES B2.
- Statement: Field access on a fat-pointer (DstRef) receiver `&DstStruct` requires an `unsafe` context, EXCEPT when the struct is `#[self_describing]` (its tail length is recovered in-band, so the borrow is a complete safe reference).
- Source: src/compiler/sema_expr.cpp#L9275-L9281
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_field_read.part1.json`

### `expr.field.dst-tail-dyn` — dyn-tail projection on a DstRef shares the carried vtable

- Divergence: Custom-DST dyn-tail model — see DIVERGENCES B2/B3.
- Statement: For a fat-pointer receiver to a custom-DST struct whose last field has unsized-dyn type `dyn Tr`, `r.tail` yields a `&dyn Tr` fat pair `{ data = base + prefix_byte_size, vtable = the receiver's OWN carried vtable }`. The tail's metadata is the wide pointer's metadata (no static vtable lookup). The dyn prefix offset is aligned to pointer width (8) since the concrete payload alignment is not known statically.
- Source: src/compiler/sema_expr.cpp#L9330-L9335, src/compiler/sema_expr.cpp#L9346-L9368
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_field_read.part1.json`

### `expr.field.dst-tail-slice` — Slice-tail projection on a DstRef

- Divergence: Custom-DST model — see DIVERGENCES B2.
- Statement: For a fat-pointer receiver to a custom-DST struct whose last field `tail` has unsized-slice type `[T]`, `r.tail` yields a slice `{ data_ptr + prefix_byte_size, len }` reusing the fat pointer's len half; prefix_byte_size is the offset after all sized prefix fields, aligned to size_of(T) (capped at 8). Slice mutability follows the receiver: `(&mut Foo).tail: &mut [T]`, `(&Foo).tail: &[T]`.
- Source: src/compiler/sema_expr.cpp#L9296-L9345, src/compiler/sema_expr.cpp#L9369-L9393
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_field_read.part1.json`

### `expr.field.self-describing-thin-tail` — Self-describing DST tail through a thin raw pointer

- Divergence: Custom-DST / self-describing model — see DIVERGENCES B2.
- Statement: For a thin raw pointer `p: *const/*mut Self` to a `#[self_describing]` struct whose last field is the unsized-slice tail, `p.tail` yields a slice `{ (p as *u8)+prefix_offset, dst_len(p) }`, where prefix_offset is the natural-aligned byte offset after all sized prefix fields and the tail length is recovered by calling the struct's `SelfDescribing::dst_len` method. Slice mutability follows the pointer's mutability.
- Source: src/compiler/sema_expr.cpp#L9185-L9248
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_field_read.part1.json`

### `expr.fmt.precision-requires-number` — Precision dot requires a number

- Divergence: Rust additionally permits `.*` and `.N$` precision forms; Logos here requires a literal number after `.`.
- Statement: A `.` in the format spec must be followed by an unsigned-integer precision; a `.` not followed by a digit is a compile error.
- Source: src/compiler/sema_fmt.cpp#L224-L235
- Rule file: `tools/spec-extract/rules/sema/sema_fmt/logos.json`

### `expr.index.range-slice` — Range indexing produces a sub-slice

- Divergence: Range-slicing relies on stdlib `slice_get_range`; open/inclusive ends are clamped to length rather than panicking on out-of-range as Rust does.
- Statement: A range index `recv[lo..hi]`, `recv[lo..]`, `recv[..hi]`, `recv[..]`, or inclusive `recv[lo..=hi]` produces a sub-slice `&[T]` via `slice_get_range(recv, lo, hi)`. The receiver must be a slice, array (decayed to `&[T]` via addr-of + slice-coercion), or reference-to-slice; otherwise an error is reported. Missing `lo` defaults to 0; missing `hi` defaults to INT64_MAX (clamped to len); an inclusive upper bound is lowered as `hi+1`. Bounds are widened to i64. `slice_get_range` must be in scope (`use logos.lang.slice`).
- Source: src/compiler/sema_expr.cpp#L10328-L10389
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_index_place.json`

### `expr.list-comp.iter-array-or-slice-only` — Comprehension iterables restricted to array/slice

- Divergence: Narrower than Rust: only concrete array/slice, no IntoIterator/Iterator protocol.
- Statement: The iterable of any comprehension form must have type `[T; N]` (array) or `[T]` (slice); any other iterator type is rejected. Element type defaults to i32 when the array/slice element type is absent.
- Source: src/compiler/sema_expr.cpp#L10896-L10907, src/compiler/sema_expr.cpp#L11002-L11013, src/compiler/sema_expr.cpp#L11112-L11123, src/compiler/sema_expr.cpp#L11245-L11256
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_list_comp.json`

### `expr.litint.width-by-type` — Integer literal bit-width from its inferred type

- Divergence: A: i24/u24/i56/u56 are Logos-only integer widths (no Rust equivalent).
- Statement: An integer literal is encoded at the bit-width of its inferred type: i8/u8=8, i16/u16=16, i24/u24=24, i32/u32=32, i56/u56=56, i64/u64=64, i128/u128=128, bool=1. usize/isize use the target pointer bit-width. An untyped integer literal (IntLit) defaults to 32 bits, widening to 64 bits when its value falls outside [INT32_MIN, INT32_MAX].
- Source: src/compiler/mlir_gen_expr.cpp#L253-L298
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_expr/logos.json`

### `expr.match.fnitem-arms-lub-fnptr` — distinct fn-item arms LUB to the common fn-pointer type

- Divergence: Rust-conformant: matches Rust LUB for fn-item match arms.
- Statement: When two arms produce distinct FnItem values with the same signature (e.g. `=> a_f` and `=> b_f`), the match result type is the corresponding `fn(...)->R` pointer type, since FnItem→FnItem coercion is rejected; both arms coerce to that FnPtr.
- Source: src/compiler/sema_stmt.cpp#L9502-L9523
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/lower_match_expr.json`

### `expr.method.array-len-builtin` — len() on a fixed array is a compile-time constant

- Divergence: Result type is i64 (Logos stdlib uses i64 for lengths), not usize as in Rust.
- Statement: `a.len()` where `a` has fixed-array type `[T; N]` evaluates to the compile-time size `N` as an `i64` literal; no runtime call is emitted.
- Source: src/compiler/sema_expr.cpp#L7280-L7284
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_method_call.part1.json`

### `expr.method.autoderef-lowest-priority` — By-value-self via auto-deref is lowest dispatch priority

- Divergence: Mirrors Rust autoderef order: try T/&T/&mut T at a deref level before stepping deeper.
- Statement: A method whose `self` is by value, reachable only by auto-dereferencing a `&T`/`&mut T`/`*T` receiver, is selected only if no exact or auto-ref candidate at the current deref level matches. When chosen, the receiver is auto-dereferenced (copying/moving the pointee out, subject to downstream Copy/move borrow checks).
- Source: src/compiler/sema_expr.cpp#L8484-L8491, src/compiler/sema_expr.cpp#L8524-L8557, src/compiler/sema_expr.cpp#L8563-L8580
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_method_call.part3.json`

### `expr.method.vec-get-move-out-rejected` — Vec::get of a non-Copy element is rejected

- Divergence: Mirrors Rust's E0507 'cannot move out of index'; scoped here to Vec::get.
- Statement: `v.get(i)` on a receiver resolving (through one reference layer) to `Vec<E>` where `E` is a non-Copy (move) type is an error: it would move an element out of borrowed Vec storage, aliasing and double-freeing on drop. The fix is `.borrow(i)` for `&E`, or `.remove(..)`/`.pop()` to take ownership. Copy elements are permitted.
- Source: src/compiler/sema_expr.cpp#L7139-L7160
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_method_call.part1.json`

### `expr.range.desugar-range-struct` — lo..hi / lo..=hi desugar to stdlib Range constructors

- Divergence: Ranges are nominal stdlib structs (RangeI32/RangeI64/RangeOfIncl), not language built-ins
- Statement: A range expression requires integer bounds. Exclusive `lo..hi` lowers to `range_i32`/`range_i64`; inclusive `lo..=hi` lowers to the generic `range_incl_of` (RangeOfIncl<T>). The bound width is i64 if either bound is wider than 32 bits or an integer literal overflows i32, else i32; both bounds are widened to that bound type. Missing stdlib constructors are an error.
- Source: src/compiler/sema_expr.cpp#L1310-L1387
- Rule file: `tools/spec-extract/rules/sema/sema_expr/try_struct_unsize_coerce.json`

### `expr.static-call.trait-qualified-ufcs` — Trait-qualified UFCS `Trait::method(recv, ...)`

- Divergence: Rust-conformant (DIVERGENCES.md: trait-qualified UFCS supported)
- Statement: When the class names a TRAIT (not a struct/enum/datatype/type-param) and args are non-empty, `Trait::method(recv, ...)` dispatches on the first argument's concrete receiver type (auto-derefed through refs/ptrs): struct/zoned-struct by name, enum by name, or primitive by type_str. The rewrite to `<recv-type>__<method>` commits only if that concrete symbol actually resolves; otherwise normal resolution and error reporting proceed.
- Source: src/compiler/sema_expr.cpp#L13198-L13248
- Rule file: `tools/spec-extract/rules/sema/sema_expr/try_implicit_reborrow_mut.json`

### `expr.try.trait-dispatch-from-residual` — ? on non-Result/Option dispatches via Try/FromResidual

- Divergence: Receiver for from_residual is explicit from fn ret type (no contextual Self inference)
- Statement: `e?` where e is neither stdlib Result nor Option desugars through the Try/FromResidual surface: `match e.branch() { Continue(c) => c, Break(r) => return RetType::from_residual(r) }`. The receiver RetType is taken from the enclosing function's declared return type (Logos does not infer trait Self from context); an undeterminable return type is an error.
- Source: src/compiler/sema_expr.cpp#L1167-L1195
- Rule file: `tools/spec-extract/rules/sema/sema_expr/try_struct_unsize_coerce.json`

### `expr.unary.neg-unsigned-rejected` — Unary minus on an unsigned type is rejected

- Divergence: Rust also rejects unary `-` on unsigned types (no `Neg` impl); Logos diagnostic is bespoke (B-ex-04). Extra widths u24/u56 are Logos-only (A11).
- Statement: `-x` where `x` has any unsigned integer type (u8/u16/u24/u32/u56/u64/u128) is a compile error; an explicit cast to a signed type is required (e.g. `-(x as i64)`).
- Source: src/compiler/sema_expr.cpp#L2628-L2639
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_unary.part1.json`

### `generic.param.bounds-and-const` — type-parameter and const-parameter forms

- Divergence: Variadic type/const parameters (`...`) are a Logos extension.
- Statement: A type parameter is `NAME [: bound + bound + ...]`; a const generic parameter is `const NAME : TYPE`. Either may be marked variadic with `...`. Bounds are joined with `+`.
- Source: src/compiler/sema_render.cpp#L1052-L1099
- Rule file: `tools/spec-extract/rules/sema/sema_render/render_stmt_src.json`

### `generic.param.variadic-last` — Variadic type parameter must be last

- Divergence: Variadic type/const parameters are a Logos addition not present in Rust.
- Statement: A variadic type parameter `T...` must be the final entry in the type-parameter list; a non-final variadic param is an error "variadic type parameter must be last".
- Source: src/compiler/sema.cpp#L4188-L4190
- Rule file: `tools/spec-extract/rules/sema/sema/finalize_relaxed_bounds.json`

### `generic.spec.method-shadows-impl-param-warn` — Method type-param shadowing impl param is a silent specialization

- Divergence: Rust treats the method param as a fresh shadowing generic; Logos reinterprets it as a specialization leg (warned).
- Statement: When a method's bare-IDENT type-param has the same name as an enclosing impl-block type-param, the method is silently treated as a specialization on the impl's param; the compiler emits a warning advising a rename.
- Source: src/compiler/sema_collect.cpp#L4551-L4586
- Rule file: `tools/spec-extract/rules/sema/sema_collect/lower_spec_fn.json`

### `grammar.expr.call-metavar` — Metavariable call

- Divergence: No Rust analogue; metaprogramming callee splice.
- Statement: '#IDENT(args)' and '#(expr)(args)' invoke a callee named by a metavariable (NAME_VAR) or by an evaluated expression, used in metaprogramming-expanded call sites.
- Source: tools/peg_gen/grammars/logos.peg#L3230-L3237
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `grammar.expr.call-ufcs-qualified` — UFCS qualified-path call

- Divergence: Trait qualifier in <T as Tr>::m is dropped (Rust uses it for disambiguation).
- Statement: '<Type as Trait>::method(args)' dispatches on the concrete Type; the trait qualifier is consumed and dropped because the type-dispatch already resolves the method.
- Source: tools/peg_gen/grammars/logos.peg#L3214-L3219
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `grammar.expr.closure-param-untyped` — Closure parameter type may be omitted

- Divergence: Conformant with Rust closure type-inference.
- Statement: closure_param allows the type annotation to be omitted: '|x|' is accepted as well as '|x: T|'. Forms: '&mut IDENT', '&IDENT', 'ref IDENT: T', 'mut IDENT: T', 'mut IDENT', '(pat_binding_list): T', 'IDENT: T', 'IDENT'. The omitted type is inferred from the surrounding fn(T)->R formal at the call site.
- Source: tools/peg_gen/grammars/logos.peg#L2979-L3000
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `grammar.generic.hrtb-binder` — HRTB for<...> binder parsed then dropped

- Divergence: Lifetimes not structurally tracked: HRTB binder is accepted but discarded (Rust enforces it).
- Statement: hrtb_binder ::= 'for' '<' LIFETIME (',' LIFETIME)* ','? '>' may prefix any trait_bound. Lifetimes are not tracked structurally, so for<'a> Trait<...> is semantically equivalent to Trait<...> (binder parsed into a disposable head).
- Source: tools/peg_gen/grammars/logos.peg#L3077-L3108
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `grammar.metaprog.quote-expr` — quote_expr! macro

- Divergence: No Rust analogue.
- Statement: quote_expr_expr ::= 'quote_expr' '!' '{' expr '}' ; body is a single expression producing a typed AST (expr-blob) literal.
- Source: tools/peg_gen/grammars/logos.peg#L3060-L3061
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `grammar.metaprog.quote-item` — quote_item! macro

- Divergence: No Rust analogue (Rust uses macro_rules!/proc-macro quote).
- Statement: quote_item_expr ::= 'quote_item' '!' '{' item* '}' ; body is zero or more item declarations producing a typed AST (item-blob) literal.
- Source: tools/peg_gen/grammars/logos.peg#L3051-L3052
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `grammar.metaprog.quote-ty` — quote_ty! macro

- Divergence: No Rust analogue.
- Statement: quote_ty_expr ::= 'quote_ty' '!' '{' type_ref '}' ; body is a single type expression producing a first-class Type literal (same Type{kind,name,size} shape as type_of::<T>()).
- Source: tools/peg_gen/grammars/logos.peg#L3068-L3069
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `grammar.writ.capture-placeholders` — Writ runtime capture placeholders

- Divergence: No Rust analogue; Writ interpolation.
- Statement: Inside a Writ literal, '${' expr '}' captures an arbitrary expression (WRIT_CAP_EXPR) and '$' IDENT captures a named binding (WRIT_CAP_IDENT) as a runtime value.
- Source: tools/peg_gen/grammars/logos.peg#L2949-L2950
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `grammar.writ.entry-key-kinds` — Writ entry keys

- Divergence: No Rust analogue; Writ data-literal grammar.
- Statement: writ_entry ::= (STRING | '-' INTEGER | INTEGER) ':' writ_val ; a map key is a quoted string, a negative integer, or a non-negative integer. A '-' INTEGER key carries LO_NEG.
- Source: tools/peg_gen/grammars/logos.peg#L2931-L2936
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `grammar.writ.nested-at-optional` — Optional @ on nested Writ aggregates

- Divergence: No Rust analogue; Writ literal nesting.
- Statement: A nested writ_map / writ_array inside a writ_val may optionally be prefixed by '@'; '@'-prefixed and bare forms are equivalent.
- Source: tools/peg_gen/grammars/logos.peg#L2951-L2955
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `grammar.writ.scalar-values` — Writ scalar values

- Divergence: No Rust analogue; Writ scalar literals.
- Statement: writ_val scalars: RAW_STRING/STRING -> WRIT_STR; FLOAT -> WRIT_FLOAT; '-' INTEGER -> WRIT_NEG_INT; INTEGER -> WRIT_INT; 'true'/'false' -> WRIT_BOOL; 'null' -> WRIT_NULL.
- Source: tools/peg_gen/grammars/logos.peg#L2956-L2963
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `grammar.writ.type-literal` — Writ embedded type value

- Divergence: No Rust analogue; type-as-value embedding.
- Statement: writ_val may be '<' 'type' ':' simple_type '>' embedding a Logos Type as a first-class Writ value (WRIT_TYPE_LIT); any simple_type (e.g. generic instantiations Vec<u8>, Result<T,E>) is accepted.
- Source: tools/peg_gen/grammars/logos.peg#L2941-L2948
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `grammar.writ.type-slot-path` — Writ CFG type-slot

- Divergence: No Rust analogue; Writ embedded-type slot.
- Statement: writ_val may be '<' 'type' ':' IDENT path_step+ '>' producing a CFG_SLOT_TYPE (slot extraction keeping an IDENT-only head followed by path steps).
- Source: tools/peg_gen/grammars/logos.peg#L2945-L2946
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-writ_map.json`

### `intrinsic.align-of.alignment` — align_of yields alignment

- Divergence: Result is i64 (Rust mem::align_of -> usize).
- Statement: `align_of::<T>()` requires exactly one type argument and yields `i64` = alignment of T.
- Source: src/compiler/sema_expr.cpp#L5718-L5731
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_type_intrinsic.json`

### `intrinsic.concat.macro` — concat! string-literal concatenation

- Divergence: Floats and char literals are not supported (Rust supports them).
- Statement: `concat!(a, b, …)` concatenates string, integer (decimal, suffix-stripped), and bool (`true`/`false`) literals at compile time into a single `&str` (`Slice<u8>`) literal. Non-literal args are a compile error. String escapes \n \t \r \\ \" \0 are decoded.
- Source: src/compiler/sema_expr.cpp#L18318-L18324, src/compiler/sema_expr.cpp#L17836-L17920
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_offset_of.json`

### `intrinsic.env.macro` — env! / option_env! read environment at compile time

- Divergence: option_env! returns an empty &str tombstone rather than Option<&str>.
- Statement: `env!("VAR")` yields the value of environment variable VAR as a `&str` literal and is a compile error if unset; `option_env!("VAR")` yields the value or an empty `&str` if unset.
- Source: src/compiler/sema_expr.cpp#L18289-L18316
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_offset_of.json`

### `intrinsic.include-str.macro` — include_str! / include_bytes! embed file contents

- Divergence: Rust's include_bytes! has type &[u8;N] distinct from &str; in Logos both are Slice<u8>.
- Statement: `include_str!("path")` and `include_bytes!("path")` read the file at compile time (path relative to the including file) and yield its contents as a `&str` (`Slice<u8>`) literal; both forms collapse to the same representation since `str` is `Slice<u8>`. Unreadable files are a compile error.
- Source: src/compiler/sema_expr.cpp#L18252-L18282
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_offset_of.json`

### `intrinsic.include.expr-only` — include! splices a file as an expression

- Divergence: Rust supports item-position include!; Logos supports only expression position.
- Statement: `include!("path")` reads the file at compile time and re-parses its contents as an expression spliced at the call site; only expression-position include! is supported (item-position is a compile error). Paths are resolved relative to the including file.
- Source: src/compiler/sema_expr.cpp#L18238-L18244, src/compiler/sema_expr.cpp#L17686-L17784
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_offset_of.json`

### `intrinsic.line.macro` — line! / column! positional macros

- Divergence: column!() always returns 0 rather than the true column.
- Statement: `line!()` yields the current source line as `u32`; `column!()` yields `u32` 0 (columns are not tracked).
- Source: src/compiler/sema_expr.cpp#L18221-L18227
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_offset_of.json`

### `intrinsic.offset-of.value` — offset_of! yields a compile-time i64 byte offset

- Divergence: Rust's offset_of! yields usize; Logos yields i64.
- Statement: `offset_of!(T, f)` evaluates to an `i64` constant equal to the byte offset of field `f` within `T`'s layout, computed by sequentially laying out fields: each field is placed at the next position aligned up to its alignment, then advanced by its byte size. Result type is `i64`.
- Source: src/compiler/sema_expr.cpp#L17657-L17681
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_offset_of.json`

### `item.const.def` — Module-level constant definition

- Divergence: `let` accepted as a const keyword at module level; generic `const NAME<...>` factory has no direct Rust analog.
- Statement: A module constant is `[pub] (const|let) NAME [<params>] : T = expr ;`. The `const` keyword admits an optional type-parameter list, making the RHS a generic compile-time factory substituted at each use site; `let` stays non-generic. Both forms require an explicit type annotation and an initializer.
- Source: tools/peg_gen/grammars/logos.peg#L688-L699
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-module.json`

### `item.const.generic-and-typed` — const item with optional generics and type

- Divergence: Generic const items (const with type parameters) are a Logos extension.
- Statement: A const item is `[pub] const NAME [<type-params>] [: TYPE] = VALUE ;`; const items may be generic.
- Source: src/compiler/sema_render.cpp#L1192-L1211
- Rule file: `tools/spec-extract/rules/sema/sema_render/render_stmt_src.json`

### `item.enum.variant-shapes` — Enum variant shapes

- Divergence: Variadic-tuple variant `Name(...T)` has no Rust analog.
- Statement: A variant is one of: unit `Name`; tuple `Name(T, ...)`; variadic-tuple `Name(...T)`; struct-shape `Name { f: T, ... }` (fields may be `pub`); empty struct-shape `Name {}`; or a discriminant-bearing `Name = <disc>`. Variant lists allow leading doc-comments per variant and a trailing comma.
- Source: tools/peg_gen/grammars/logos.peg#L753-L786, tools/peg_gen/grammars/logos.peg#L757-L775
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-module.json`

### `item.fn.signature-overloading` — Functions overloadable by signature

- Divergence: Rust does not permit free-function overloading by signature.
- Statement: Functions are keyed by a signature derived from base name, parameter types, and vararg-ness, allowing multiple same-named functions to coexist; only an exact symbol-name collision (same package, base, signature) is a "duplicate function" error.
- Source: src/compiler/sema_collect.cpp#L4712-L4713, src/compiler/sema_collect.cpp#L4837-L4881
- Rule file: `tools/spec-extract/rules/sema/sema_collect/lower_spec_fn.json`

### `item.repr.recognized-modes` — `#[repr(...)]` minimal recognised modes

- Divergence: Only `transparent` (struct) and integer-width (enum) repr supported; Rust's `C`/`packed`/`align`/etc. not yet.
- Statement: `#[repr(...)]` is recognised only on structs (`transparent`) and enums (integer-discriminant width). Other repr modes are parsed and then rejected (no silent acceptance).
- Source: src/compiler/sema_impl.hpp#L1501-L1505
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/logos.part3.json`

### `item.static-fn.def` — Static (associated) function definition

- Divergence: `static fn` spelling for associated (no-self) functions; Rust uses an `fn` without a `self` parameter inside an impl.
- Statement: `[pub] static [unsafe] fn NAME [<params>] (params) [-> T] { ... }` defines an associated/free function with no `self` receiver; its own optional type-parameter list follows the name, matching instance/free fn generics. The name may be the `new` keyword.
- Source: tools/peg_gen/grammars/logos.peg#L1067-L1093
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-module.json`

### `item.static.runtime-initialized-storage` — static items get zero-init storage filled at program startup

- Divergence: Rust requires `static` initializers to be const-evaluable; Logos evaluates them at runtime startup instead.
- Statement: A non-extern `static` has global storage that is zero-initialized at link time and assigned its declared initializer value at program startup (before `main`), via a synthesized startup initializer running every static's init expression in declaration order. A `static`'s initializer is thus an ordinary runtime-evaluated expression, not a compile-time constant.
- Source: src/compiler/mlir_gen_dyn.cpp#L702-L714, src/compiler/mlir_gen_dyn.cpp#L716-L758
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_dyn/emit_static_globals.json`

### `item.struct.fields-and-inherent-methods` — struct item form with optional inherent methods

- Divergence: Legacy `struct Foo { fields, fn ... }` form (methods inside the struct body) is accepted; not a Rust form.
- Statement: A struct is `[pub] struct NAME [<type-params>] { fields... }`, or `[pub] struct NAME [<type-params>] ;` when field-less; each field is `[pub] NAME : TYPE [...]`. Inherent methods may be declared in the struct body, which is equivalent to a separate `impl NAME { ... }` block.
- Source: src/compiler/sema_render.cpp#L1140-L1150, src/compiler/sema_render.cpp#L1251-L1308
- Rule file: `tools/spec-extract/rules/sema/sema_render/render_stmt_src.json`

### `item.union.field-copy-restriction` — union field types restricted to non-move types

- Divergence: B: generic-union Copy check is deferred to mono rather than enforced at use site as in Rust.
- Statement: Each non-generic union field type must not be a move type (Vec/Box/String/owning trait object); allowed are Copy types, references, ManuallyDrop<T>, or aggregates thereof. A field whose type is a bare type-parameter is exempt at collection (checked at monomorphization); a field that is itself a union is allowed.
- Source: src/compiler/sema_collect.cpp#L1502-L1530
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_module.json`

### `layout.enum.niche-low-bit` — Low-bit niche enum packs tag into the payload word's low bit

- Divergence: Niche layout is a Logos-defined packing not specified by Rust.
- Statement: A LowBit-niche enum packs a 64-bit word where the low bit distinguishes arms: low bit 0 → pointer arm (the aligned word IS the pointer, ptr_disc), low bit 1 → value arm. The value-arm payload is encoded as (v<<1)|1 and decoded as word>>1 (arithmetic shift if signed, logical otherwise), yielding val_disc. In raw mode (WAny Pod(u64)) both arms read the word verbatim with no decode.
- Source: src/compiler/mlir_gen_expr.cpp#L4897-L4919, src/compiler/mlir_gen_expr.cpp#L4926-L4931, src/compiler/mlir_gen_expr.cpp#L4963-L4976
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_expr/gen_expr_kind-4.json`

### `layout.enum.niche-lowbit-encoding` — LowBit niche enum payload encoding

- Divergence: A: niche-packing layout is Logos-defined; not a Rust-guaranteed representation.
- Statement: For an enum with a LowBit niche packed into a single word: the pointer arm stores the pointer's raw integer value (low bit 0, guaranteed by >=2 alignment); the value arm stores (v<<1)|1 after sign/zero extension to the word width. In RAW mode the producer-supplied value (low-bit already set) is stored verbatim without shifting. An empty payload stores 0.
- Source: src/compiler/mlir_gen_expr.cpp#L570-L602
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_expr/logos.json`

### `layout.enum.niche-null-pointer` — Null-pointer niche enum has no discriminant word

- Divergence: Niche layout is an unspecified Rust optimization; here it is observable/normative for Option<&T>-shaped enums.
- Statement: A null-pointer-niche enum (Option<&T> shape) has no separate discriminant word: the payload (a non-null pointer) occupies offset 0; the `none` variant is encoded as a null pointer at offset 0, and the `some` variant's non-null payload pointer is itself the discriminant. Decoding: null → none_disc, non-null → some_disc.
- Source: src/compiler/mlir_gen_expr.cpp#L4920-L4921, src/compiler/mlir_gen_expr.cpp#L4932-L4941, src/compiler/mlir_gen_expr.cpp#L4977-L4988
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_expr/gen_expr_kind-4.json`

### `layout.enum.niche-packed-no-disc` — Niche-packed enum drops the discriminant word

- Divergence: Rust-conformant in intent (niche optimization); see ref_enum_niche.
- Statement: A niche-optimized enum is represented by its payload alone, with layout {payload_bytes, payload_align} and no separate discriminant word; the variant is distinguished by an in-payload niche value.
- Source: src/compiler/mlir_gen_types.cpp#L522-L524
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_types/logos.json`

### `layout.int.fixed-widths` — Primitive integer/float sizes and alignments

- Divergence: A: Logos adds non-power-of-two integer widths i24/u24 (align 1) and i56/u56 (align 1) not present in Rust.
- Statement: Scalar layout {size,align} in bytes: bool/i8/u8 = {1,1}; i16/u16 = {2,2}; i24/u24 = {3,1}; i32/u32/f32/char/{integer literal} = {4,4}; i56/u56 = {7,1}; i64/u64/f64/{float literal} = {8,8}; i128/u128 = {16,16}. usize/isize and all pointers are target-pointer-width-sized (8 on a 64-bit target).
- Source: src/compiler/mlir_gen_types.cpp#L453-L464, src/compiler/mlir_gen_types.cpp#L61-L62
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_types/logos.json`

### `lex.ident.ascii-only` — Identifiers are ASCII-only

- Divergence: A: diverges from Rust, which accepts Unicode (XID) identifiers.
- Statement: Identifiers consist of ASCII bytes only; a source line containing a non-ASCII (high-bit, >= 0x80) byte at the point of a syntax error is diagnosed as an identifier encoding error, since non-ASCII bytes cannot form a valid identifier token.
- Source: src/compiler/module_loader.cpp#L1361-L1377
- Rule file: `tools/spec-extract/rules/sema/module_loader/extract_writ0_exports.json`

### `lex.keyword.reserved-set` — Reserved keyword set

- Divergence: Adds Logos-specific keywords absent in Rust: quote_item/quote_expr/quote_ty/template/package/instantiate/eidos/genos/auto/metacall/tagged/new/typeof/offset_of/null; lacks Rust keywords (mod, pub(crate), crate, self, Self, fn-async forms, etc.) handled elsewhere.
- Statement: The following are reserved keywords matched as distinct tokens and unavailable as ordinary identifiers: continue, quote_item, quote_expr, quote_ty, template, package, instantiate, eidos, genos, auto, metacall, static, return, extern, struct, union, match, while, break, false, trait, const, type, impl, enum, loop, else, true, for, use, mut, let, dyn, tagged, pub, new, fn, if, in, as, where, unsafe, move, typeof, offset_of, ref, null, async, await.
- Source: tools/peg_gen/grammars/logos.peg#L328-L380
- Rule file: `tools/spec-extract/rules/grammar/logos/tokens.json`

### `lex.literal.float` — Float literal syntax

- Divergence: A leading `-` is part of the float token (Rust parses `-` as separate unary minus). A fractional part is mandatory (no `1.` form); float-width suffix set is {f32,f64}.
- Statement: A float literal matches an optional leading `-`, an integer part, a mandatory `.` with a fractional part (both `[0-9][0-9_]*`), an optional exponent `([eE][+-]?[0-9][0-9_]*)`, and an optional suffix `f32` or `f64`. `_` digit separators are permitted.
- Source: tools/peg_gen/grammars/logos.peg#L456
- Rule file: `tools/spec-extract/rules/grammar/logos/tokens.json`

### `lex.literal.int-overflow-i64` — Unsuffixed integer literal must fit i64/u64 (64-bit) magnitude

- Divergence: Rust default integer literal type is i32; here the raw overflow bound is 64-bit (i64/u64), with per-suffix bounds layered at the call site.
- Statement: An integer literal's magnitude is rejected if it exceeds 64-bit representable range: an unsigned magnitude must fit u64; a negated literal's magnitude must not exceed 2^63 (INT64_MIN is representable, anything past overflows). Literals are parsed in base 10, or 0x/0X hex, 0b/0B binary, 0o/0O octal, with `_` digit separators ignored; parsing stops at the first character that is not a valid digit for the base (the type suffix).
- Source: src/compiler/sema_impl.hpp#L4577-L4602
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/is_integer_kind.json`

### `lex.literal.int-suffix` — Integer literal type suffix

- Divergence: Includes Logos-specific suffixes i24/u24/i56/u56 absent in Rust.
- Statement: An integer literal may carry an explicit type suffix selecting its kind: i8/i16/i24/i32/i56/i64/i128, u8/u16/u24/u32/u56/u64/u128, usize, isize; the suffix follows the (optionally radix-prefixed) digit body. Absence of a recognised suffix yields the unsuffixed literal type.
- Source: src/compiler/sema_impl.hpp#L4812-L4841
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/is_integer_kind.json`

### `lex.token.ident` — Identifier token

- Divergence: Identifiers are ASCII-only; Rust permits Unicode (XID) identifiers and raw identifiers `r#name`.
- Statement: IDENT = `[a-zA-Z_][a-zA-Z0-9_]*` — ASCII letter/underscore followed by ASCII alphanumerics/underscores.
- Source: tools/peg_gen/grammars/logos.peg#L467
- Rule file: `tools/spec-extract/rules/grammar/logos/tokens.json`

### `lex.token.lifetime` — Lifetime token

- Divergence: Lifetime names must start with a lowercase letter or `_`; uppercase-initial lifetimes (allowed in Rust) are not recognized.
- Statement: LIFETIME = `'[a-z_][a-z0-9_]*` — an apostrophe followed by a lowercase-initiated identifier (no closing apostrophe).
- Source: tools/peg_gen/grammars/logos.peg#L466
- Rule file: `tools/spec-extract/rules/grammar/logos/tokens.json`

### `lex.writ.float-literal` — Writ float literal

- Divergence: Requires a fractional digit after '.'; bare-integer floats and leading-dot are governed by this regex (no trailing-dot form); 'f'/'d' suffixes.
- Statement: A Writ FLOAT is an optional '-', optional integer part, a mandatory '.' with a fractional part, optional exponent ([eE][+-]?digits), and an optional 'f'|'d' type suffix. Regex: /[-]?[0-9]*\.[0-9]+([eE][+-]?[0-9]+)?[fd]?/. The fractional part is required (a '.' must be followed by >=1 digit).
- Source: tools/peg_gen/grammars/writ.peg#L67
- Rule file: `tools/spec-extract/rules/grammar/writ/tokens.json`

### `lex.writ.integer-literal` — Writ integer literal with radix and suffix

- Divergence: Data-language lexer (Writ), not Logos source; C-style suffixes ull/ul/ll/u and '_s32'-style signed suffix differ from Rust integer-literal suffixes.
- Statement: A Writ INTEGER is an optional leading '-' followed by a hex (0x/0X), binary (0b/0B), octal (0o/0O), or decimal magnitude, with an optional suffix: '_(u|s)(8|16|32|64)' (sized) or C-style 'ull'|'ul'|'ll'|'u'. Regex: /[-]?(0[xX][0-9a-fA-F]+|0[bB][01]+|0[oO][0-7]+|[0-9]+)(_(u|s)(8|16|32|64)|ull|ul|ll|u)?/.
- Source: tools/peg_gen/grammars/writ.peg#L66
- Rule file: `tools/spec-extract/rules/grammar/writ/tokens.json`

### `metaprog.cfg.attr-multi-arg-implicit-and` — cfg attribute multi-arg implicit AND

- Divergence: Multi-arg implicit AND matches Rust (noted inline).
- Statement: In `#[cfg(...)]` attribute position, a top-level multi-argument list is an implicit AND of its arguments; `#[cfg]` with no args matches (true).
- Source: src/compiler/sema.cpp#L3654-L3673
- Rule file: `tools/spec-extract/rules/sema/sema/struct_name_from_type.json`

### `metaprog.cfg.combinators` — cfg all/any/not combinators and boolean literals

- Divergence: cfg(true)/cfg(false) per Rust 1.80 RFC 3695 (noted inline).
- Statement: cfg predicates compose: `all(p...)` is the AND of its children, `any(p...)` the OR, `not(p)` requires exactly one child and negates it (else error/false). The literals `cfg(true)`/`cfg(false)` evaluate to true/false directly. Unknown combinators evaluate to false / raise an error in attribute position.
- Source: src/compiler/sema.cpp#L3553-L3582, src/compiler/sema.cpp#L3692-L3708, src/compiler/sema.cpp#L3721-L3737
- Rule file: `tools/spec-extract/rules/sema/sema/struct_name_from_type.json`

### `metaprog.cfg.key-value-predicates` — cfg key=value predicate resolution

- Divergence: Unknown-key-false matches Rust per inline comment.
- Statement: A cfg key=value predicate matches against compile-target metadata: target_arch, target_os, target_endian, target_family, target_pointer_width resolve to the host/target platform values; `feature = "name"` matches iff name is in the active feature set. Any unknown key evaluates to false.
- Source: src/compiler/sema.cpp#L3507-L3517, src/compiler/sema.cpp#L3572-L3575
- Rule file: `tools/spec-extract/rules/sema/sema/struct_name_from_type.json`

### `metaprog.template.decl` — Template declaration

- Divergence: No Rust equivalent.
- Statement: `template <item>` wraps a struct/enum/datatype/trait/impl/fn declaration as inert data (an AST blob) rather than a real binding; the inner names are never registered, so referencing the template as a type yields an unknown-type diagnostic. Templates are consumed by metafunctions via apply/metacall.
- Source: tools/peg_gen/grammars/logos.peg#L604-L612
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-module.json`

### `module.package.decl` — Package declaration header

- Divergence: Rust uses no `package` header; module name is path-derived. Logos requires an explicit `package` line with a dotted package path.
- Statement: A compilation unit begins with `package NAME ('.' IDENT)* ';'`, optionally preceded by inner doc-comments (`//!`, `/*! */`) and inner attributes (`#![...]`). The dotted path gives the package's full name to arbitrary depth (first component = NAME, remaining components = PATH_PARTS). After the package line come zero-or-more use-declarations, then zero-or-more items.
- Source: tools/peg_gen/grammars/logos.peg#L489-L490
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-module.json`

### `module.tagdispatch.binary-archive-provides-tables` — Fully-binary tag systems are provided by the archive

- Divergence: Module/separate-compilation model; no direct Rust analogue.
- Statement: A tag system whose every registered callee is already present in a linked binary archive is not re-defined; the consuming unit emits only external references to that system's tables, lookup function, and initializer. Tables also present in an archive use weak (deduplicating) linkage rather than triggering a duplicate-definition error.
- Source: src/compiler/mlir_gen_dyn.cpp#L219-L292, src/compiler/mlir_gen_dyn.cpp#L358-L368, src/compiler/mlir_gen_dyn.cpp#L394-L396
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_dyn/logos.json`

### `module.use.brace-group-desugar` — `use pkg.{a, b, c}` with a lowercase head desugars to N wildcard imports

- Divergence: note — Logos path model uses `.` for packages, `::` for items.
- Statement: A grouped use whose head segment begins with a lowercase letter is treated as a package path: `use pkg.{a, b, c}` desugars to wildcard imports `pkg.a.*`, `pkg.b.*`, `pkg.c.*`. A head segment beginning uppercase is instead the enum-variant import form.
- Source: src/compiler/sema.cpp#L6835-L6861
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_wstatic_value.json`

### `module.use.from-module` — use with explicit source module

- Divergence: `use ... from <module>` clause has no Rust analog.
- Statement: `[pub] use pkg('.'IDENT)* IDENT use_module ';'` imports `pkg.path` from a named module; the trailing bare IDENT is the contextual `from` keyword and `use_module` is the source (a bare name or a quoted string for hyphenated ids, with quotes stripped). The from-bearing alternative is tried before the plain form.
- Source: tools/peg_gen/grammars/logos.peg#L498-L521
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-module.json`

### `module.use.from-module-restriction` — `use pkg from "module"` restricts the import to a specific module id

- Divergence: note — part of Logos's C++-style module-linkage system; no direct Rust equivalent.
- Statement: A use of the form `use pkg from "module";` resolves the quoted module name to a module id and restricts the imported package's symbol resolution to that module's exports; the restriction is in force during lowering, not only collection.
- Source: src/compiler/sema.cpp#L6882-L6905
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_wstatic_value.json`

### `module.use.variant-shorthand` — Enum-variant bare-name import

- Divergence: Uses `.`-separated path with `.{}` variant group; Rust spells this `use core::Option::{Some, None};` (A: `::`-item / `.`-pkg path model).
- Statement: `use pkg.Path.Type.{V1, V2, ...} ;` brings the named variants of enum `Type` into bare (unqualified) scope. The last dotted component before `.{...}` is the enum type name; the brace-list (trailing comma allowed) names the variants.
- Source: tools/peg_gen/grammars/logos.peg#L506-L511, tools/peg_gen/grammars/logos.peg#L523-L527
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-module.json`

### `module.use.variant-vs-subpackage-by-case` — Group target classified by first-character case

- Divergence: Disambiguation by identifier capitalization is a Logos convention, not a Rust rule.
- Statement: In a USE_VARIANTS group `use pkg.X.{...};`, the bracketed target `X` is classified by its first character: lowercase-leading `X` is treated as a grouped sub-package import (each member becomes `pkg.X.<member>`); uppercase-leading `X` is treated as an enum-variant import, importing the enclosing package `pkg` as a wildcard so the type is in scope. This relies on the convention that enum/type names are capitalized.
- Source: src/compiler/module_loader.cpp#L167-L204
- Rule file: `tools/spec-extract/rules/sema/module_loader/logos.json`

### `mono.instantiate.decl` — Explicit instantiation root-pin

- Divergence: No Rust equivalent; analog of C++ `template class Foo<int>;`.
- Statement: `[pub] instantiate <type_ref> ;` materializes the named generic instance as a monomorphization root: all its inherent and trait methods become roots, transitively pulling everything they call. `pub instantiate` additionally marks the instance as part of the package's public API surface.
- Source: tools/peg_gen/grammars/logos.peg#L591-L595
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-module.json`

### `mono.mangle.owning-vs-borrowed-dyn` — Owning Box<dyn T> mangles distinctly from borrowed &dyn T

- Divergence: Internal mangling distinction with no Rust analog; reflects owning-dyn vs borrowed-dyn repr split.
- Statement: A trait object that is OWNING (Box<dyn T>) mangles as 'owndyn_<trait-name>' plus '__<arg>' per type-arg, while a borrowed &dyn T keeps the plain type-string mangling. This keeps Vec<Box<dyn T>> and Vec<&dyn T> as DISTINCT specs so the owning bit is not dropped (which would skip element drop and leak).
- Source: src/compiler/mono_impl.hpp#L740-L751
- Rule file: `tools/spec-extract/rules/mono/mono_impl_hpp/logos.part2.json`

### `mono.subst.closure-substitution` — Closure substitution rewrites param/return/capture types

- Divergence: RFC-2229 disjoint-closure-capture metadata preserved through mono
- Statement: Substituting a closure applies the type substitution to each parameter type, the return type, and each capture type, while preserving the closure id, move-ness, fn-pointer-ness, escape (heap-env) flag, capture names, per-capture mutability flags, per-capture field paths (RFC-2229), and per-capture narrow field types (also substituted).
- Source: src/compiler/mono_clone.cpp#L4164-L4208
- Rule file: `tools/spec-extract/rules/mono/mono_clone/logos.part8.json`

### `mono.subst.variadic-tuple-splice` — Variadic-tuple pack expansion

- Divergence: Variadic tuples are a Logos addition not present in Rust.
- Statement: A single-element tuple whose sole element is a pack type-var `(A...)` splices in the elements of the concrete tuple A maps to during substitution, yielding the full concrete tuple.
- Source: src/compiler/sema.cpp#L4639-L4659
- Rule file: `tools/spec-extract/rules/sema/sema/finalize_relaxed_bounds.json`

### `pat.bind.default-binding-mode-struct` — Default binding modes for struct shorthand fields under a reference scrutinee

- Divergence: RFC 2005 default binding modes (Rust-conformant intent).
- Statement: Under a `&`/`&mut` struct scrutinee, a shorthand field binding of a move-only field type T binds by reference (`&T` / `&mut T` matching the scrutinee's mutability) rather than moving the field out; Copy field types bind by value. Error and bare-TypeVar field types are excluded from the reference promotion.
- Source: src/compiler/sema_stmt.cpp#L5792-L5816
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_writ_pat_guard.json`

### `pat.binding.default-by-ref-mode` — Default binding modes wrap payload bindings by reference

- Divergence: Rust-conformant (RFC 2005); historical move-only-type restriction now lifted
- Statement: Under a `&`/`&mut` scrutinee, every plain named payload binding binds by-reference: the binding type is wrapped in `&`/`&mut` once per scrutinee ref-layer, with the outermost layer carrying mut iff any peeled layer was `&mut`. Bindings to `_` and synthesized slots are exempt.
- Source: src/compiler/sema_stmt.cpp#L3252-L3265, src/compiler/sema_stmt.cpp#L3915-L3949
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/make_pat_wild.json`

### `pat.bytes.scrutinee-must-be-u8-array` — Byte-string pattern requires `[u8; N]` scrutinee

- Divergence: Rust permits byte-string patterns against `&[u8]`/`&[u8; N]`; Logos requires fixed `[u8; N]` and rejects dynamic slices.
- Statement: A byte-string pattern requires the scrutinee (after peeling a single `&`/`&mut` reference) to be a fixed-size array `[u8; N]`; otherwise it is an error. Dynamic `&[u8]` slice scrutinees are not supported.
- Source: src/compiler/sema_stmt.cpp#L4025-L4050
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_pattern_bytes.part1.json`

### `pat.ergonomics.deref-scrutinee` — Match ergonomics peel all &/&mut/* layers

- Divergence: Rust-conformant (RFC 2005 default binding modes)
- Statement: Pattern matching peels all `&`, `&mut`, and `*` layers of the scrutinee type to obtain the concrete payload shape, so a pattern over `&&Enum<T>` (arbitrary depth) unifies against the inner `Enum<T>`.
- Source: src/compiler/sema_stmt.cpp#L3220-L3243, src/compiler/sema_stmt.cpp#L3828-L3851
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/make_pat_wild.json`

### `pat.float.literal-rejected` — Float-literal patterns are rejected

- Divergence: Rust deprecated-but-still-accepts float patterns; Logos hard-rejects them.
- Statement: A float-literal pattern is parsed but rejected as unsupported (IEEE-equality pattern semantics undecided).
- Source: src/compiler/sema_stmt.cpp#L4286-L4294
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_pattern_bytes.part1.json`

### `pat.float.rejected-at-sema` — Float-literal patterns rejected

- Divergence: Rust also forbids float patterns (deprecated/removed).
- Statement: A float-literal pattern parses but is rejected at sema (not a valid match pattern).
- Source: tools/peg_gen/grammars/logos.peg#L283
- Rule file: `tools/spec-extract/rules/grammar/logos/nodes.json`

### `pat.for-loop.ref-element-deref` — by-ref for-loop element is dereferenced before destructure

- Divergence: A: no default-binding-mode by-ref propagation for refs in for-loop patterns.
- Statement: When the iterated element type is `&T`/`&mut T`, the loop binding is dereferenced to a value temporary of type `T` and the tuple pattern destructures that value (by-ref default binding modes are not applied).
- Source: src/compiler/sema_stmt.cpp#L8135-L8149
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/emit_for_pattern_destructure.json`

### `pat.for-loop.tuple-only` — for-loop pattern restricted to tuple of names/nested-tuples

- Divergence: Subset of Rust for-loop pattern support (richer sub-patterns are a follow-up).
- Statement: A `for <pat> in <iter>` loop pattern that is destructured in place must be a tuple pattern `(p0, ..., pn)` over a tuple-typed element; each element pattern must be a name, `_`, or a nested tuple pattern. Any other element sub-pattern (literal, struct, variant, range, etc.) is rejected; a non-tuple top-level pattern over a non-tuple element is rejected (`bind a name and destructure in the body`).
- Source: src/compiler/sema_stmt.cpp#L8150-L8155, src/compiler/sema_stmt.cpp#L8184-L8188
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/emit_for_pattern_destructure.json`

### `pat.lit.float-rejected` — Float-literal pattern rejected

- Divergence: Rust deprecated float patterns; Logos rejects them outright.
- Statement: A float-literal pattern parses but is rejected by sema with a diagnostic: float equality matching in patterns is deliberately not supported (IEEE equality semantics undefined).
- Source: tools/peg_gen/grammars/logos.peg#L2195-L2199
- Rule file: `tools/spec-extract/rules/grammar/logos/rule-never_type.json`

### `pat.refutable.nested-variant-guard` — Nested variant inner pattern lowers to a synthesized guard

- Divergence: A — guarded nested-variant arms need a catch-all for exhaustiveness (DIVERGENCES.md: finite-enum coverage of guarded arms not yet proven)
- Statement: A nested variant inner pattern (e.g. `Some(Color::Red)`, `Some(Some(v))`) binds the outer payload to a synthetic name and gates the arm with a synthesized `match synth { <inner> => <check>, _ => false }`; binding-carrying inners additionally re-extract their bindings in the arm body via a let-else, composing to arbitrary depth.
- Source: src/compiler/sema_stmt.cpp#L3284-L3453
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/make_pat_wild.json`

### `pat.str.position-restricted` — String-literal patterns allowed only in specific positions

- Divergence: Rust permits string patterns in all pattern positions; Logos restricts them.
- Statement: String-literal patterns are supported only as a whole match arm (`match s { "foo" => .. }`), inside an enum-variant payload (`Some("foo")`), or as a tuple element (`("foo", _)`). In any other position (e.g. inside an array/slice pattern) a string-literal pattern is an error.
- Source: src/compiler/sema_stmt.cpp#L4296-L4312
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_pattern_bytes.part1.json`

### `pat.variant.unit-payload-binding` — Named binding against unit-typed payload is a zero-sized local

- Divergence: Rust-conformant (rustc issue-41888 `Err(err)` over `Result<(),()>`)
- Statement: When a variant's payload types are all `()`, a `_` binding is dropped and a named binding is kept with a `()` binding type (a zero-sized local in scope), since unit fields are elided from the enum layout. The unit payload position itself is omitted from binding types.
- Source: src/compiler/sema_stmt.cpp#L3852-L3886
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/make_pat_wild.json`

### `pat.writ.scalar-leaves` — Writ scalar leaf patterns

- Divergence: Writ pattern matching is a Logos addition (no Rust equivalent).
- Statement: Within a Writ value pattern (@{...}/@[...]), the scalar leaves are: null (`@null`), bool (`@true`/`@false`), integer (`@<int>`), and string (`@"..."`). Each tests the corresponding AnyVal scrutinee: null-ness, boolean equality, integer equality, and string equality respectively.
- Source: src/compiler/sema_stmt.cpp#L5293-L5334, src/compiler/sema_stmt.cpp#L5484-L5486
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_writ_pat_guard.json`

### `region.dangling.dyn-trait-ref` — &dyn Trait data half is a borrowed reference

- Divergence: logos-core 2.1 default trait-object lifetime rule
- Statement: A borrowing trait object (&dyn Trait, non-owning Kind::TraitObject) is treated as a reference kind for dangling-return detection: returning &dyn Trait to a local is rejected; an owning Box<dyn Trait> does not qualify.
- Source: src/compiler/borrow_check.cpp#L488-L501
- Rule file: `tools/spec-extract/rules/sema/borrow_check/logos.json`

### `region.elision.method-result-borrows-self` — Elided &self -> &T (or borrow-carrying) ties result to receiver

- Divergence: Rust lifetime-elision conformant
- Statement: A method with a reference first parameter, a reference or borrow-carrying return type, and no explicit lifetime parameters has its result lifetime tied to the receiver (self-borrowing). A method with explicit lifetime parameters does NOT count as self-borrowing.
- Source: src/compiler/borrow_check.cpp#L1531-L1567
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part2.json`

### `region.escape.value-local-root-walk` — Value-local root of a borrow place

- Divergence: Rust parity (raw-pointer deref breaks provenance, cf. box_leak)
- Statement: The dangling-root of a borrow/receiver place is found by walking one optional leading address-of-temp, then a field/tuple-index/index/deref projection chain to a terminal variable, where a raw-pointer deref stops the walk (pointee not tied to pointer's stack lifetime). The terminal must be a value local (not a parameter, not a tracked reference binding) for the reference to be considered dangling-on-escape.
- Source: src/compiler/borrow_check.cpp#L1653-L1689
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part2.json`

### `region.outlives.permissive-unmentioned-pair` — Two unmentioned named regions assumed compatible in permissive mode

- Divergence: Permissive default; Rust requires the outlives relation to be explicitly established (would reject).
- Statement: In permissive mode, if two named non-static regions L and S neither equal nor reach one another and NEITHER appears anywhere in the explicit outlives graph, L: S is assumed to hold (region inference is expected to unify them). If either L or S appears in the graph (but no path connects them), L: S is rejected. In strict mode, an unestablished named constraint is always rejected.
- Source: include/logos/compiler/outlives.hpp#L86-L102
- Rule file: `tools/spec-extract/rules/sema/outlives_hpp/logos.json`

### `region.pat.by-ref-binding-inherits-borrows` — By-reference match binding inherits scrutinee borrows

- Divergence: Rust NLL conformant (DIVERGENCES B6)
- Statement: When a `match`-arm variant-data binding has reference type or borrow-carrying type, it inherits the scrutinee's borrow sources, so the borrow cannot be smuggled past the referent's scope via the binding. By-value bindings copy out and carry no borrow.
- Source: src/compiler/borrow_check.cpp#L1506-L1528
- Rule file: `tools/spec-extract/rules/sema/borrow_check/merge_moves.part2.json`

### `region.temp.ref-into-temporary-dangles-on-escape` — Reference into a statement-scoped temporary dangles on escape

- Divergence: Rust E0716 (temporary dropped while borrowed)
- Statement: A reference borrowing into a statement-scoped temporary (a fresh value with no named storage: a literal, struct/tuple/array/enum literal, or call/method/closure-call result, including compiler-materialized `__rtmp_N` receivers) dangles the moment it escapes its enclosing statement (e.g. `let v = make().view();`).
- Source: src/compiler/borrow_check.cpp#L436-L443, src/compiler/borrow_check.cpp#L457-L481
- Rule file: `tools/spec-extract/rules/sema/borrow_check/logos.json`

### `stmt.assign.destructuring-into-places` — Destructuring assignment into existing places

- Divergence: RFC 2909 (Rust-conformant).
- Statement: Destructuring assignment `(a,b)=e` / `[a,b]=e` / `S{a,b}=e` writes into EXISTING places (not new bindings), desugared to `let tmp = rhs;` followed by per-place assignments.
- Source: tools/peg_gen/grammars/logos.peg#L311
- Rule file: `tools/spec-extract/rules/grammar/logos/nodes.json`

### `stmt.assign.int-widen` — Implicit integer widening on assignment

- Divergence: Rust has no implicit integer widening on assignment.
- Statement: On assignment to an integer variable, a non-literal non-enum integer RHS of a narrower integer kind that can widen safely to the LHS kind is implicitly widened.
- Source: src/compiler/sema_stmt.cpp#L2638-L2644
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/op_assign_trait_method.json`

### `stmt.block.dead-code-after-terminator` — Unreachable code after a terminator is warned

- Divergence: Rust-conformant (unreachable_code lint, here always a warning not deny-by-default)
- Statement: Within a block, a statement S that follows a hard terminator statement (return / break / continue) is unreachable; the compiler emits a `unreachable code after terminator` warning. Annotation statements following a terminator do not trigger the warning, and only the first such occurrence per block is reported.
- Source: src/compiler/sema_stmt.cpp#L671-L697
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/lower_block.json`

### `stmt.diverge.never-returning-call` — call to a Never-returning fn diverges

- Divergence: A: `panic` recognized as divergent by hardcoded callee name (Logos historically lacked the `!` type); now generalized to any `-> !` callee.
- Statement: A call expression `f(...)` (including the macro form `panic!(...)` which parses as FN_MACRO_CALL) in expression-statement, tail-expression, or let-initializer position is divergent — control never falls through — iff the callee is named `panic` OR any candidate function with that name has return type `!` (Never). `panic` is recognized by name even without a `!` annotation.
- Source: src/compiler/sema_stmt.cpp#L34-L53, src/compiler/sema_stmt.cpp#L208-L218
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/logos.json`

### `stmt.fallback.never-only-on-provable-divergence` — Never-fallback gated on provably non-returning body

- Divergence: A: implements a Rust-2024-style `!`-fallback but with a stricter, narrower divergence predicate than full `block_always_returns`.
- Statement: A generic return type-param may fall back to `!` only when the callee body provably never returns normally — i.e. the body's last statement is a divergent call (`panic`/`-> !`), a `loop`, or an expression-statement/tail wrapping a `loop`. A body ending in `return 0;` does NOT qualify (that is a normal return, leaving the type-param ambiguous).
- Source: src/compiler/sema_stmt.cpp#L194-L226
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/logos.json`

### `stmt.if.cond-must-be-bool` — if condition must be bool

- Divergence: Accepting Never as an `if` condition (e.g. `if (return x){}`) is a Logos accommodation, tag G160-10.
- Statement: A non-pattern `if` condition must have type bool; Error and Never types are accepted (Never permits `if return x {}`), any other type is a compile error.
- Source: src/compiler/sema_stmt.cpp#L5955-L5961
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/build_writ_pat_guard.json`

### `stmt.let-chain.nested-if-let-desugar` — let-chain desugars to nested if-let with duplicated else

- Divergence: Accepted limitation: ELSE side effects duplicated per fall-through site.
- Statement: A let-chain (`if a && let P = e && b { .. } else { .. }`) desugars to nested `if let`/`if` over the segment list, with the ELSE branch textually duplicated at every fall-through site (matching Rust's classic desugar); user-visible side effects in ELSE are duplicated.
- Source: src/compiler/sema_impl.hpp#L4078-L4084
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/logos.part8.json`

### `stmt.let-pat.union-requires-unsafe` — let pattern on a union requires unsafe and exactly one field

- Divergence: Rust-conformant (items.union.pattern.safety / one-field)
- Statement: An irrefutable `let U { f } = u;` over a union type requires an enclosing `unsafe` block (it reads the named field's memory) and must name exactly one field with no `..` rest.
- Source: src/compiler/sema_stmt.cpp#L1410-L1457
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/lower_block.json`

### `stmt.let.uninit-drop-flag-when-conditional-init` — Uninitialized `let mut` needs a runtime drop flag only if assigned conditionally

- Divergence: Implementation strategy of Rust drop-flag elaboration; observable equivalence to Rust drop semantics.
- Statement: A `let mut x: T;` declared without initializer requires a runtime drop flag iff it is later assigned at a control-flow nesting depth strictly greater than its declaration depth (inside a conditional/loop/match/let-else arm). A plain `{ }` block does not increase nesting depth; variables assigned only at declaration depth use static drop placement.
- Source: src/compiler/mlir_gen_stmt.cpp#L309-L352
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_stmt/logos.json`

### `trait.binop.partial-ord-derive` — Relational ops derive from partial_cmp when direct method absent

- Divergence: Mirrors Rust's default PartialOrd lt/le/gt/ge bodies.
- Statement: For a struct LHS with relational op {<,<=,>,>=}, if the direct lt/le/gt/ge method is not implemented but partial_cmp is, the comparison derives as a.partial_cmp(&b) followed by is_lt/is_le/is_gt/is_ge; when partial_cmp returns Option<Ordering> it routes through cmp_opt_is_<op> (None => false), and when it returns Ordering directly it calls Ordering::is_<op>.
- Source: src/compiler/sema_expr.cpp#L1990-L2055
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_binop.json`

### `trait.binop.tuple-eq-impl` — Tuple == / != routes to Eq impl only for non-primitive tuples

- Divergence: Primitive-tuple fast path avoids requiring f64:Eq (f64 is PartialEq-only, Rust parity).
- Statement: == / != between two tuples of equal arity routes to the tuple's Eq eq/ne impl (keyed concrete `$tuple$N$...`, then arity `$tuple$N`, then variadic `$tuple$variadic`) ONLY when at least one field is non-primitive; an all-primitive tuple falls through to per-field value comparison and never requires the Eq trait. Operands are auto-borrowed to &Tuple.
- Source: src/compiler/sema_expr.cpp#L1812-L1928
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_binop.json`

### `trait.def.copy-not-a-supertrait` — Copy is excluded from a trait's supertrait list

- Divergence: A: Copy treated specially, excluded from supertrait closure.
- Statement: When recording a trait's declared supertraits, `Copy` is omitted from the supertrait set (it is an auto/marker bound, not a vtable-bearing supertrait).
- Source: src/compiler/sema_decl.cpp#L1621-L1625
- Rule file: `tools/spec-extract/rules/sema/sema_decl/lower_struct_def.part1.json`

### `trait.impl.foreign-private-trait-error` — impl of a foreign private trait is an error

- Divergence: Note: §4 module/package visibility model; trait must be pub-accessible to be implemented across package boundaries.
- Statement: In `impl Trait for T`, if `Trait` resolves to a trait that is not accessible from the impl site (not pub, or module-only and outside its module), the impl is rejected (privacy error). The check fires at the impl site that introduces the foreign trait name.
- Source: src/compiler/sema_collect.cpp#L2685-L2699
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_type_alias.part2.json`

### `trait.impl.target-ref` — impl for reference types

- Divergence: Note: receiver-shape mangling is a Logos dispatch-implementation detail; observable rule is which reference forms are valid impl targets and that &[T] ≡ [T] for dispatch.
- Statement: `impl Trait for &T` / `&mut T` is permitted; `&[T]`/`&mut [T]` canonicalize to the fat-pointer slice form and register under the same `$slice$<elem>` key as `impl Trait for [T]` (binding Self to the unsized-slice type); a generic ref-blanket `impl<T> Trait for &T`/`&mut T` keys under a fixed `$ref_$T`/`$mut_ref_$T` sentinel, restricted by coherence to one such impl per trait/ref-shape.
- Source: src/compiler/sema_collect.cpp#L2818-L2863
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_type_alias.part2.json`

### `trait.impl.trait-type-args-bind` — trait type args bind the trait's parameters

- Divergence: Note: Logos does not track regions structurally for trait dispatch; trait-position lifetime args are skipped from type-arg resolution.
- Statement: For `impl Trait<X> for U`, the trait's positional type arguments are resolved and bound to the trait's declared type parameters (e.g. `impl Into<i32> for C` binds the `Into` parameter to `i32`), making them available in method signatures. Lifetime arguments at trait position (`impl Trait<'a>`) are collected separately and not treated as type args.
- Source: src/compiler/sema_collect.cpp#L3076-L3110
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_type_alias.part2.json`

### `trait.impl.unknown-trait-error` — impl of an undeclared trait is an error

- Divergence: Copy and Drop are treated as compiler built-in marker traits resolvable by name alone (not requiring import/dependency-graph visibility).
- Statement: `impl Trait for T` requires `Trait` to be a declared trait, except for the built-in marker traits `Copy` and `Drop`, which are always implementable by name without a visible trait declaration; any other unknown trait name is an error.
- Source: src/compiler/sema_collect.cpp#L3064-L3072
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_type_alias.part2.json`

### `trait.impl.variadic-pack-param` — Variadic trait type-param pack absorbs trailing impl params

- Divergence: Fn-family variadic type packs are a Logos extension (no stable Rust equivalent).
- Statement: If a trait declares a variadic type parameter `A...` used at a method parameter position, an impl may expose any number of concrete parameters from that position onward; each post-pack impl parameter type must equal the corresponding trait-instantiation type-arg (trait_type_args[k - pack_pos]), and the count of post-pack impl params must equal the number of pack instantiation args.
- Source: src/compiler/sema_collect.cpp#L3408-L3492
- Rule file: `tools/spec-extract/rules/sema/sema_collect/collect_type_alias.part3.json`

### `type.array.length-forms` — Array type length forms

- Divergence: Array length via `metacall {..}` replaces Rust const-eval at this position (MP-mc-01).
- Statement: `[T; N]` length is determined by: a `metacall { expr }` block whose tail integer is CTFE-evaluated; `sizeof...(P)` over an in-scope type-param pack (symbolic `__sizeof_pack:P`); a literal integer; or a symbolic const parameter name. A missing/empty metacall tail or an unknown pack/op is a hard error.
- Source: src/compiler/sema.cpp#L6140-L6226
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_type.json`

### `type.binop.bitwise-integer-or-bool` — Bitwise/shift operands must be integer (or bool for bitwise-only)

- Divergence: Matches Rust `impl BitAnd/BitOr/BitXor for bool`.
- Statement: Bitwise operators {&,|,^} require integer or bool operands; shift operators {<<,>>} require integer operands only. The result type is the unified integer type of the operands.
- Source: src/compiler/sema_expr.cpp#L2384-L2416, src/compiler/sema_expr.cpp#L2454-L2454
- Rule file: `tools/spec-extract/rules/sema/sema_expr/lower_binop.json`

### `type.copy.structural-auto` — non-Drop struct of all-Copy fields is automatically Copy

- Divergence: A: diverges from Rust, which requires an explicit `#[derive(Copy)]`/`impl Copy`.
- Statement: A struct that does not implement Drop and whose every field type is Copy is automatically Copy, without an explicit `impl Copy`.
- Source: src/compiler/sema_collect.cpp#L674-L678
- Rule file: `tools/spec-extract/rules/sema/sema_collect/logos.json`

### `type.drop.closure-value-not-auto-dropped` — A closure value is not auto-reported as needs-drop

- Divergence: Narrows automatic Drop coverage relative to Rust for indirectly-stored closures; intentional to avoid misreading a pointer slot as a {fn,env} pair.
- Statement: A closure value (e.g. stored in a struct field or iterator adapter, held by pointer) is not classified as needs-drop by recursive aggregate scanning; closure drop is driven narrowly only via the owning `Box<Closure>` path.
- Source: src/compiler/mlir_gen_stmt.cpp#L503-L511
- Rule file: `tools/spec-extract/rules/codegen/mlir_gen_stmt/logos.json`

### `type.identity.lifetime-ignored` — Lifetimes excluded from type identity for & / &mut

- Divergence: Rust treats lifetimes as part of the type but as a separate region-check phase; identity-collapse of lifetimes here matches Rust's type-equality-modulo-regions.
- Statement: Reference types `&'a T` and `&mut 'a T` have identity determined solely by mutability and pointee `T`; the lifetime `'a` is NOT part of type identity (matches types_equal). Lifetime args on struct/enum/assoc types likewise do not affect type equality.
- Source: src/compiler/sema.cpp#L817-L821, src/compiler/sema.cpp#L954-L959
- Rule file: `tools/spec-extract/rules/sema/sema/install_snapshot.json`

### `type.infer.never-fallback-on-divergent-body` — ! fallback for unbound type-param of always-diverging callee

- Divergence: Rust-2024 `!`-fallback semantics (logos-core 1.1).
- Statement: If a callee's body always diverges (panic-tail or `loop {}`-tail) and a type-parameter is otherwise unbound at the call site, the inference variable falls back to `!` (Never). A non-diverging body leaves an unbound type-param as an ambiguity error: `fn f<T>()->T{panic();}` infers T=! while `fn f<T>()->T{return 0;}` is ambiguous.
- Source: src/compiler/sema_impl.hpp#L2550-L2560
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/logos.part5.json`

### `type.infer.no-underscore-in-item-signature` — `_` placeholder type forbidden in item signatures

- Divergence: Rust E0121 analog.
- Statement: The inferred-type placeholder `_` is not permitted within types in item signature positions (function parameter types, return type, const item type), including nested occurrences (`Vec<_>`, `&_`, `[_; N]`); such occurrences are an error rather than an inference hole.
- Source: src/compiler/sema_impl.hpp#L1957-L1968
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/logos.part4.json`

### `type.inhabited.ref-conservative` — References to uninhabited types are treated as inhabited

- Divergence: Rust treats `&!` as uninhabited; Logos stays conservative and treats `&Never` as inhabited.
- Statement: A reference or pointer to an uninhabited type is conservatively treated as inhabited (only value-carrying composites are marked uninhabited).
- Source: src/compiler/sema.cpp#L4359-L4362
- Rule file: `tools/spec-extract/rules/sema/sema/finalize_relaxed_bounds.json`

### `type.integer.bit-width` — Integer bit-width and signedness

- Divergence: usize/isize width is target-dependent (pointer bits) as in Rust; the exotic 24/56-bit widths are a Logos addition.
- Statement: Each concrete integer kind has a fixed bit width and signedness: i8/u8=8, i16/u16=16, i24/u24=24, i32/u32=32, i56/u56=56, i64/u64=64, i128/u128=128; signed forms are signed, unsigned forms unsigned. usize/isize have width equal to the target pointer width (isize signed, usize unsigned). IntLit, Enum, and non-integers have no defined rank (width 0).
- Source: src/compiler/sema_impl.hpp#L4453-L4474
- Rule file: `tools/spec-extract/rules/sema/sema_impl_hpp/is_integer_kind.json`

### `type.let.intlit-default-i32` — Unannotated integer literal binding defaults to i32 (i64 on overflow)

- Divergence: Rust defaults unconstrained integer literals to i32 but never silently widens to i64 on overflow (it is a compile error); Logos auto-upgrades to i64.
- Statement: An unannotated let whose RHS is an integer literal binds at type i32, upgraded to i64 when the literal value falls outside the i32 range.
- Source: src/compiler/sema_stmt.cpp#L2191-L2202
- Rule file: `tools/spec-extract/rules/sema/sema_stmt/lower_let_else.json`

### `type.primitive.set` — Built-in primitive scalar types

- Divergence: A: extra fixed-width widths i24/u24/i56/u56 and 128-bit i128/u128 beyond Rust's standard set.
- Statement: The language has primitive scalar types: void, bool, char, the floats f32/f64, and the integers i8/u8, i16/u16, i24/u24, i32/u32, i56/u56, i64/u64, i128/u128, isize/usize. Each is a distinct type identified by its keyword name.
- Source: src/compiler/sema.cpp#L2077-L2097, src/compiler/sema.cpp#L2530-L2551
- Rule file: `tools/spec-extract/rules/sema/sema/type_str.json`

### `type.ptr.modifier-set` — Raw-pointer modifiers

- Divergence: `*zoned` is a Logos-only zoned-pointer modifier (F3).
- Statement: A raw pointer type is written `*const T`, `*mut T`, or `*zoned T`/`*zoned mut T`; any other word after `*` is a hard error (`unknown raw-pointer modifier`).
- Source: src/compiler/sema.cpp#L5685-L5699, src/compiler/sema.cpp#L5741
- Rule file: `tools/spec-extract/rules/sema/sema/resolve_type.json`

### `type.struct.non-null-niche` — non_null single-pointer wrapper yields Option niche

- Divergence: A: #[non_null] attribute is a Logos addition mirroring Rust NonNull niche.
- Statement: A struct annotated `#[non_null]` wrapping a single non-null pointer makes `Option<T>` use the null-pointer value as the None niche (no discriminant overhead).
- Source: src/compiler/sema_decl.cpp#L1200-L1201
- Rule file: `tools/spec-extract/rules/sema/sema_decl/lower_struct_def.part1.json`

### `type.struct.rel-ptr-offset-storage` — rel_ptr struct is a self-relative pointer

- Divergence: A: RefRepr RelOffset Logos addition, no Rust analog.
- Statement: A struct annotated `#[rel_ptr]` is classified as a self-relative pointer using 8-byte offset storage.
- Source: src/compiler/sema_decl.cpp#L1194-L1196
- Rule file: `tools/spec-extract/rules/sema/sema_decl/lower_struct_def.part1.json`

### `type.struct.self-describing-thin-ptr` — self_describing keeps *Self thin

- Divergence: A: Writ/RefRepr Logos addition, no Rust analog.
- Statement: A struct annotated `#[self_describing]` keeps `*Self` a thin pointer (no DstRef fattening) under Ptr→DstRef canonicalization.
- Source: src/compiler/sema_decl.cpp#L1186-L1189
- Rule file: `tools/spec-extract/rules/sema/sema_decl/lower_struct_def.part1.json`

### `type.struct.zone-mut-fat-ref` — zone_mut makes &mut T fat carrying its allocator

- Divergence: A: Writ zone model Logos addition; Rust &mut is thin.
- Statement: For a struct annotated `#[zone_mut]`, a `&mut T` reference is a fat `{data, zone}` pair carrying the value's allocator/zone.
- Source: src/compiler/sema_decl.cpp#L1190-L1192
- Rule file: `tools/spec-extract/rules/sema/sema_decl/lower_struct_def.part1.json`

### `type.struct.zoned2-relative-fields` — zoned2 struct fields use relative pointers

- Divergence: A: Writ zoned2 Logos addition, no Rust analog.
- Statement: A struct annotated `#[zoned2]` stores its pointer fields as self-relative offsets (RelOffset) rather than absolute addresses.
- Source: src/compiler/sema_decl.cpp#L1193
- Rule file: `tools/spec-extract/rules/sema/sema_decl/lower_struct_def.part1.json`
