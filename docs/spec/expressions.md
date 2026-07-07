# Expressions and Intrinsics

> Scope: expression-form (`expr`) and compiler-intrinsic (`intrinsic`) semantic rules of Logos. Rules are extracted from the compiler source layers — PEG grammar, semantic analysis (`sema_*`), monomorphization (`mono_*`), and MLIR code generation (`mlir_gen_*`) — grouped by their id middle-segment. Every `id` is preserved verbatim as a permanent linkable address; type/generic notation is rendered literally.

<a id="expr-domain"></a>
# Expressions (`expr`)

## Literals (`expr.lit`)

### `expr.lit.char-is-unicode-scalar` — Char literal denotes a Unicode scalar value

`'X'` (LIT_CHAR) carries the original char-literal text including quotes; sema decodes it to a Unicode scalar value (not a raw byte).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L293`

### `expr.lit.array` — Array literal

`[e1, e2, ...]` is an array literal (ARR_LIT) with an ITEMS list of expr; trailing comma allowed; may be empty (`[]`).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3026-L3027`

### `expr.lit.tuple` — Tuple literal requires a comma

`(e1, e2, ...)` with 2+ comma-separated exprs, or `(e,)` with exactly one expr followed by a mandatory trailing comma, is a tuple literal (TUPLE_LIT); the trailing comma in the single-element form disambiguates the 1-tuple from a parenthesized expression.

```logos
(1, 2)
(1,)
```

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3030-L3033`

### `expr.lit.paren` — Parenthesized expression

`(expr)` is a parenthesized expression (PAREN_EXPR) wrapping a single expr; distinct from tuple_lit, which requires a comma.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3101-L3103`

## Literals (`expr.literal`)

### `expr.literal.kinds` — Primary literal forms

The primary/atom literal forms, identical in both `primary_expr` (normal expression position) and `primary_expr_ns` (struct-lit-suppressed position used in `if`/`while` conditions etc.): `RAW_STRING`/`STRING` -&gt; `LIT_STR`, `BYTE_STRING` -&gt; `LIT_BYTES`, `CHAR_LIT` -&gt; `LIT_CHAR`, `KW_TRUE`/`KW_FALSE` -&gt; `LIT_BOOL`, `FLOAT` -&gt; `LIT_FLOAT`, `INTEGER` -&gt; `LIT_INT`. A `BYTE_STRING` literal lowers at sema to a `[u8; N]` array literal of its decoded bytes (escapes `\n \t \r \0 \\ \" \x..` supported, matching the `PAT_BYTES` decoder).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2822-L2834`, `tools/peg_gen_cpp/grammars/logos.peg#L2624-L2632`

### `expr.literal.float-format-and-suffix` — Float literal format, underscores, and suffix typing

A float literal must be well-formed; underscores are stripped from the digits; a recognized 3-char float suffix sets the literal's concrete type (e.g. f32/f64) while a suffix-less float literal has the inference type FloatLit.

*Source:* `src/compiler/sema_expr.cpp#L1016-L1031`

## Boolean literals (`expr.litbool`)

### `expr.litbool.zero-one` — Boolean literal encoding

A boolean literal is a 1-bit integer: true=1, false=0.

*Source:* `src/compiler/mlir_gen_expr.cpp#L313-L315`

## Integer literals (`expr.litint`)

### `expr.litint.width-by-type` — Integer literal bit-width from its inferred type

An integer literal is encoded at the bit-width of its inferred type: i8/u8=8, i16/u16=16, i24/u24=24, i32/u32=32, i56/u56=56, i64/u64=64, i128/u128=128, bool=1. usize/isize use the target pointer bit-width. An untyped integer literal (IntLit) defaults to 32 bits, widening to 64 bits when its value falls outside [INT32_MIN, INT32_MAX].

*Divergence:* A: i24/u24/i56/u56 are Logos-only integer widths (no Rust equivalent).

*Source:* `src/compiler/mlir_gen_expr.cpp#L253-L298`

### `expr.litint.usize-pointer-sized` — usize/isize literals are pointer-sized

A usize- or isize-typed integer literal is encoded at the target's pointer bit-width (e.g. 64 on a 64-bit target), not the default 32, so high bits are well-defined.

*Source:* `src/compiler/mlir_gen_expr.cpp#L270-L279`

### `expr.litint.i128-two-halves` — 128-bit integer literal assembled from low and high words

A 128-bit integer literal's value is the 128-bit integer whose low 64 bits are value and high 64 bits are value_hi; neither half is discarded.

*Source:* `src/compiler/mlir_gen_expr.cpp#L291-L297`

## Integer literals (`expr.int-lit`)

### `expr.int-lit.overflow-reject` — Integer literals that exceed their type range are rejected

An integer literal whose value cannot be represented (would silently saturate/truncate) is a compile error: 'integer literal out of range'. ≤64-bit literals are bound-checked against i64/destination range; literals with u128/i128 suffix are bound-checked against the 128-bit range.

*Source:* `src/compiler/sema_expr.cpp#L233-L235`, `src/compiler/sema_expr.cpp#L249-L252`

### `expr.int-lit.suffix-range` — Suffixed integer literal bound-checked against suffix type

A suffixed integer literal `Nsuf` is given type `suf` and its magnitude is bound-checked against that type's range: signed types permit |min| (e.g. i8 down to -128, up to 127), unsigned types permit 0..2^N-1. Exceeding the bound is 'integer literal out of range for its suffix type'.

*Source:* `src/compiler/sema_expr.cpp#L255-L293`

### `expr.int-lit.negate-fold` — Leading unary minus folds into integer literal for range check

A leading unary minus is folded into the integer literal before range checking, so the magnitude is bounded by |min| rather than max (e.g. `-128i8` is valid, equal to i8::MIN).

*Source:* `src/compiler/sema_expr.cpp#L219-L221`, `src/compiler/sema_expr.cpp#L262-L271`

### `expr.int-lit.unsigned-negative` — Negative value with unsigned suffix is rejected

A negative integer literal with an unsigned suffix (u8/u16/u32/u64/u128) is a compile error: 'negative value with unsigned suffix'.

*Source:* `src/compiler/sema_expr.cpp#L238-L241`, `src/compiler/sema_expr.cpp#L283-L286`

### `expr.int-lit.unsuffixed-type` — Unsuffixed integer literal has inferred-integer type

An integer literal without a suffix is given a polymorphic integer-literal type whose concrete type is resolved later by destination-type coercion; only suffixed literals get a fixed primitive type at lowering.

*Source:* `src/compiler/sema_expr.cpp#L256-L258`, `src/compiler/sema_expr.cpp#L292-L293`

### `expr.int-lit.malformed` — Malformed integer literal is rejected

An integer literal whose textual form is not a valid integer literal is a compile error: 'malformed integer literal'.

*Source:* `src/compiler/sema_expr.cpp#L223-L226`

## Float literals (`expr.litfloat`)

### `expr.litfloat.f32-vs-f64` — Float literal precision from type, default f64

A float literal typed f32 is encoded as a 32-bit float; otherwise it is encoded as a 64-bit float (f64 is the default).

*Source:* `src/compiler/mlir_gen_expr.cpp#L301-L311`

## Character literals (`expr.char-lit`)

### `expr.char-lit.escapes` — Character literal escape sequences

A char literal `'c'` accepts the escapes \n \t \r \0 \\ \' \" ; \xNN (exactly 2 hex digits, byte 0..255); and \u{H..} (1..6 hex digits in braces). Any other escape is a compile error.

*Source:* `src/compiler/sema_expr.cpp#L314-L374`

### `expr.char-lit.unicode-scalar` — char value must be a valid Unicode scalar

A \u{H..} char value must be a Unicode scalar value: ≤ U+10FFFF and not in the surrogate range U+D800..U+DFFF; otherwise it is a compile error. A char literal lowers to a value of type `char`.

*Source:* `src/compiler/sema_expr.cpp#L364-L368`, `src/compiler/sema_expr.cpp#L402`

### `expr.char-lit.utf8-body` — Multibyte char literal body decoded as one UTF-8 codepoint

A char literal whose body is a single multibyte character is decoded as exactly one UTF-8 codepoint; a malformed or length-mismatched UTF-8 body is a compile error.

*Source:* `src/compiler/sema_expr.cpp#L376-L401`

## Strings / str (`expr.str`)

### `expr.str.as-bytes-identity` — &str.as_bytes() is a representation identity

`&str` is modeled as `Slice<u8>` — the same fat-pointer ABI as `&[u8]`. Calling `.as_bytes()` on a receiver whose slice element kind is `U8` lowers to the receiver expression unchanged (no conversion emitted).

*Divergence:* Logos models &str as Slice&lt;u8&gt;; .as_bytes() is a no-op identity conversion by construction.

*Source:* `src/compiler/sema_expr.cpp#L6505-L6514`

### `expr.str.method-forwarding` — &str method-call syntax forwards to stdlib free functions

On a `Slice<u8>` (`&str`) receiver, the method names `starts_with, ends_with, contains, eq_str, cmp, index_of, find, trim, trim_start, trim_end, split` resolve, if no more specific match applies, by forwarding to the stdlib free functions `str_starts_with, str_ends_with, str_contains, str_eq, str_cmp, str_index_of` (for both `index_of` and `find`), `str_trim, str_trim_start, str_trim_end, split` respectively, called as `fn(receiver, ...explicit_args)`.

*Source:* `src/compiler/sema_expr.cpp#L6515-L6550`

## Byte-string literals (`expr.bytes-lit`)

### `expr.bytes-lit.type` — Byte-string literal has type [u8; N]

A byte-string literal `b"…"` lowers to an array literal of type `[u8; N]` where N is the decoded byte count; it accepts the escapes \n \t \r \0 \\ \' \" and \xNN (2 hex digits). Unknown or malformed escapes are compile errors.

*Source:* `src/compiler/sema_expr.cpp#L405-L471`

## Array literals (`expr.array-lit`)

### `expr.array-lit.dyn-elem-unsize` — Array literal elements coerce &Concrete to &dyn Trait

In an array literal typed `[&dyn Trait; N]`, an element expression of type `&Concrete` (or `&mut Concrete`/`*Concrete`) is unsize-coerced to the trait object representation before being stored: the concrete struct behind the source reference is resolved and a `{data, vtable}` fat pointer is synthesized for that (struct, trait) pair — the same coercion `gen_struct_lit` applies to a `&dyn` struct field.

*Source:* `src/compiler/mlir_gen.cpp#L1250-L1254`, `src/compiler/mlir_gen.cpp#L1255-L1262`, `src/compiler/mlir_gen.cpp#L1270-L1292`

### `expr.array-lit.dyn-elem-fat-repr` — &dyn Trait array elements are stored as inline fat pointer pairs

Each element slot of a `[&dyn Trait; N]` array is an inline `{data-ptr, vtable-ptr}` pair (uniform fat-pointer model, matching the type's own layout as two pointer-sized words); the coerced fat value is written as a single unit into the slot, never split into an 8-byte partial store, so both the data and vtable halves are always initialized together.

*Source:* `src/compiler/mlir_gen.cpp#L1293-L1300`

### `expr.array-lit.struct-elem-by-value` — Struct-typed array literal elements copy by value

When an array literal's element type is an aggregate (struct) type, each element expression is evaluated and its full byte representation is copied into the array slot by value (a full-size copy, not a pointer/reference store) — regardless of whether the element expression yields a pointer to the aggregate (e.g. a nested struct literal or local) or the aggregate value directly (e.g. a function-call return). This gives `[Struct; N]` value semantics: each slot holds an independent copy.

*Source:* `src/compiler/mlir_gen.cpp#L1303-L1322`

### `expr.array-lit.nested-array-elem-by-value` — Nested-array-typed elements copy element-wise by value

When an array literal's element type is itself an array type (`[[T; M]; N]`), each outer element is materialized by copying every inner element individually (load from source, store to destination slot) rather than aliasing or bulk-memcpy-ing the source, giving the nested array value copy semantics.

*Source:* `src/compiler/mlir_gen.cpp#L1324-L1337`

### `expr.array-lit.scalar-elem-numeric-coerce` — Scalar array literal elements undergo numeric coercion to the element type

When an array literal's element type is a plain scalar (not a trait object, struct, or nested array), each element expression's value is numerically coerced to the array's element type before being stored into the slot (e.g. an untyped/differently-typed integer literal is widened/converted to match).

*Source:* `src/compiler/mlir_gen.cpp#L1338-L1343`

### `expr.array-lit.bracket-comma` — Array literal

An array literal is a comma-separated element list in brackets: `[e0, e1, ...]`.

*Source:* `src/compiler/sema_render.cpp#L333-L344`

## Array literals (`expr.arr-lit`)

### `expr.arr-lit.empty-needs-hint` — Empty array literal element type comes from an annotation hint

An empty array literal `[]` takes its element type from an enclosing `[T;N]`/`[T]`/`&[T]` annotation or return-type hint, building `[T;0]` (which borrows to an empty `&[T]`). Without such a hint the element type is unknown and a warning is emitted.

*Source:* `src/compiler/sema_expr.cpp#L10529-L10548`

### `expr.arr-lit.scalar-hint-adopt` — Concrete scalar element hint retypes literal elements up front

When an array literal has a concrete scalar integer/float element hint and every element is either already of the hint type or an in-range integer/float literal, all literal elements are retyped to the hint and the hint becomes the element type. An integer literal that does not fit the hinted width is an error (not a silent fall-back to the default int type).

*Source:* `src/compiler/sema_expr.cpp#L10554-L10602`

### `expr.arr-lit.fnptr-hint` — FnPtr element hint unifies distinct FnItems

Under a `[fn(...) -> R; N]` annotation, a heterogeneous array of distinct function items coerces to a common function-pointer element type when every element is compatible with the hint; each non-matching element is cast to the hint and the hint becomes the element type.

*Source:* `src/compiler/sema_expr.cpp#L10603-L10628`

### `expr.arr-lit.dyn-hint-unsize` — &dyn Trait element hint unifies concrete refs via unsize coercion

Under a `[&dyn Trait; N]` annotation, a heterogeneous array of distinct `&Concrete` refs unifies to `&dyn Trait` when every element is compatible with, or unsize-coercible to, the dyn element type. Each not-already-`&dyn` element is wrapped in an explicit dyn-coercion cast (building the fat pointer / vtable per element); the homogeneity check is then skipped.

*Source:* `src/compiler/sema_expr.cpp#L10629-L10677`

### `expr.arr-lit.homogeneous` — Array literal elements must be mutually compatible and range-checked

Absent a unifying hint, all array-literal elements must be pairwise compatible; the element type is the numeric unification of the elements. Integer-literal elements (including those nested in array/tuple literal elements, and element 0 retroactively against a later concrete anchor) are range-checked against the inferred concrete element type, reporting an out-of-range error per offending element/sub-element.

*Source:* `src/compiler/sema_expr.cpp#L10678-L10843`

### `expr.arr-lit.intlit-i64-widen` — IntLit element type widens to i64 on overflow of i32

When the inferred element type is the untyped integer-literal type, it is upgraded to i64 if any element value overflows the i32 range; otherwise it stays IntLit so annotation-based coercion (e.g. `[i64;N] = [1,2,3]`) remains applicable.

*Source:* `src/compiler/sema_expr.cpp#L10844-L10856`

### `expr.arr-lit.const-pack-expand` — Const-pack array expansion builds a symbolic-length array

An array literal `[N...]` over a `<const N...: T>` pack with a single pack-expand element of const-var element type builds a `[T; sizeof...(N)]` symbolic-length array; monomorphization later replaces the single pack-expand element with one integer literal per pack member.

*Source:* `src/compiler/sema_expr.cpp#L10858-L10873`

## Arrays (`expr.array`)

### `expr.array.struct-element-by-value` — Struct array elements: addressed on read, value-copied on construction

Indexing a struct-typed array/tuple element (`a[i]`, `r.cells[i]`, nested `g.rows[i].cells[j]`) — including through an implicit `&`/`&mut` auto-ref wrapper on the receiver — resolves to the element's real address computed with the element's stride, not a loaded copy, so a `&mut self` call through it mutates the original element. Conversely, when a struct/array field is initialized from an array-literal whose elements are themselves aggregates (inline struct or nested array), each element is written into its destination slot by copying the element's byte value, never by storing a pointer to it.

*Source:* `src/compiler/mlir_gen.cpp#L838-L861`, `src/compiler/mlir_gen.cpp#L944-L976`

### `expr.array.literal-forms` — Array literal and fill forms

An array literal `[ ... ]` has two forms, tried in this order (fill before list, to resolve the shared `[ expr` prefix ambiguity): (1) fill form `[value; N]`, repeating `value` N times, where `N` is one of: an integer literal, a named constant/identifier, a variadic pack length `sizeof...(P)` written `IDENT...(IDENT)`, or a `metacall { ... }` block evaluated to produce the size; (2) element-list form `[e1, e2, ...]` - zero or more comma-separated exprs with an optional trailing comma.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2863-L2873`, `tools/peg_gen_cpp/grammars/logos.peg#L2703-L2704`, `tools/peg_gen_cpp/grammars/logos.peg#L2926-L2933`

## Array-fill literals (`expr.arr-fill`)

### `expr.arr-fill.repeat-literal` — Array fill literal repeats the element to length N

`[v; N]` produces an array literal of element type T (= type of v) with N copies; the element is re-lowered for each slot. N must be a positive integer; the element IntLit is left unresolved so struct-literal type inference can widen it.

*Source:* `src/compiler/sema_expr.cpp#L11461-L11529`, `src/compiler/sema_expr.cpp#L11517-L11528`

### `expr.arr-fill.size-sizeof-pack` — Array fill length via sizeof...(P)

`[v; sizeof...(P)]` where P is an in-scope type parameter yields a single-element array literal whose length is symbolic (`__sizeof_pack:P`); monomorphization repeats the element to the variadic pack's expanded length. Any spread operator other than `sizeof` is rejected; an undefined P is an error.

*Divergence:* Logos variadic-pack feature.

*Source:* `src/compiler/sema_expr.cpp#L11468-L11485`

### `expr.arr-fill.size-metacall` — Array fill length via metacall splice

`[v; metacall { <expr> }]` evaluates the block's tail expression by compile-time evaluation (CTFE), and the integer result becomes the array length. The metacall block must contain an integer tail expression. This is Logos's replacement for Rust const-eval at the array-length position.

*Divergence:* Logos explicit-metacall model replaces Rust const-expression array lengths.

*Source:* `src/compiler/sema_expr.cpp#L11486-L11516`

## Tuple literals (`expr.tuple-lit`)

### `expr.tuple-lit.one-elem-trailing-comma` — One-element tuple requires trailing comma

A tuple literal is `(e0, e1, ...)`; a single-element tuple is distinguished from a parenthesized expression by a mandatory trailing comma: `(e,)`.

*Source:* `src/compiler/sema_render.cpp#L318-L331`

## Tuples (`expr.tuple`)

### `expr.tuple.unit-and-element-typing` — Tuple literal: unit, expected-type widening, overflow upgrade

`()` is the unit value of type `()`. Each tuple element widens toward its expected positional element type from an enclosing tuple-type hint (propagated into nested-tuple elements only); an int-literal element that overflows i32 is upgraded to i64; the literal's type is the tuple of the (possibly widened) element types.

*Source:* `src/compiler/sema_expr.cpp#L1602-L1651`

## Struct literals (`expr.struct-lit`)

### `expr.struct-lit.anyval-raw-constructor` — `AnyVal { raw: expr }` literal is a scalar constructor

A struct-literal expression naming `AnyVal` is not a normal struct literal but a constructor for the scalar AnyVal value: it must supply exactly one field named `raw`; its value is evaluated and numerically coerced to i32. Any other field count, or a field name other than `raw`, is rejected.

```logos
AnyVal { raw: 42 }
```

*Related:* `layout.anyval.scalar-i32`

*Source:* `src/compiler/mlir_gen.cpp#L888-L905`

### `expr.struct-lit.array-field-value-copy` — Array-typed struct field is copied by value from a non-literal source

An array-typed struct field (`[T; N]`) is stored in-place as an inline aggregate. When such a field is initialized from a source expression that is not itself an array literal (e.g. a local array-typed variable), the source array's element data is copied byte-for-byte into the field's slot rather than storing a reference to the source.

*Source:* `src/compiler/mlir_gen.cpp#L1047-L1060`

### `expr.struct-lit.forms` — Struct literal forms

Struct literals: `T { f: e, … }`, generic `T::<A,…> { f: e, … }`, and functional-update `T { f: e, .. base }` / `T { .. base, f: e }` / `T { .. base }` (explicit fields always override `base` regardless of source field order); plus the antiquote forms `#(expr) { … }` / `#IDENT { … }` (struct name supplied by a bound variable — valid only inside `quote_expr!`).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2818-L2838`, `tools/peg_gen_cpp/grammars/logos.peg#L2823-L2831`, `tools/peg_gen_cpp/grammars/logos.peg#L2880-L2898`

### `expr.struct-lit.field-init` — Struct field initializers and shorthand

A field initializer is `name: expr`, or shorthand `name` (`FIELD_SHORTHAND`, binds the in-scope variable of that name). Tuple-struct fields may be initialized by numeric name, `S { 0: a, 1: b }`, since fields of `struct S(T0, T1)` are named "0"/"1" (matching `.0`/`.1` access). The reserved words `new`/`null` are valid field names (`new: e`, `null: e`). Antiquote alts `#(field_init),*` (cursor-expanded field-init list) and `#(expr): expr` / `#IDENT: expr` (placeholder field name) are valid only inside a `quote_expr!` body.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2900-L2921`

### `expr.struct-lit.explicit-type-args-seed-inference` — Explicit type args seed struct-lit inference

In a struct literal `S::<A1,...,Ak> { ... }` for generic `S`, supplied type args are bound positionally to S's type-params (up to the number of params) and used to seed the inferred-arg map; each supplied arg is resolved and ignored if it resolves to an error type.

*Source:* `src/compiler/sema_expr.cpp#L9696-L9713`

### `expr.struct-lit.full-explicit-args-select-spec` — Fully-supplied type args select a matching specialization

If all type args of a generic struct are explicitly supplied and a matching (full or partial) specialization exists, the literal's field set and field types are taken from that specialization rather than the primary template.

*Source:* `src/compiler/sema_expr.cpp#L9715-L9719`, `src/compiler/sema_expr.cpp#L9766-L9777`

### `expr.struct-lit.infer-typevar-from-field` — Infer struct type-param from a directly-typed field value

A struct type-param `T` used directly as a field's declared type is inferred from that field's value type; an uninferred-T field value of IntLit type defaults to T's hint (else i32), and of FloatLit type defaults to T's hint (else f64).

*Source:* `src/compiler/sema_expr.cpp#L9779-L9791`

### `expr.struct-lit.infer-typevar-from-array-field` — Infer T from `[T; N]` field via element type

For a field declared `[T; N]` with type-param element T, T is inferred from the element type of an array-typed field value; an IntLit element defaults to T's hint (else i32).

*Source:* `src/compiler/sema_expr.cpp#L9792-L9805`

### `expr.struct-lit.infer-typevar-from-ptr-field` — Infer T from `*T`/`&T`/`&mut T` field via pointee

For a field declared as a pointer/reference to type-param T (`*T`, `&T`, `&mut T`), T is inferred from the pointee of a ref-like field value type, provided that pointee is not an error type.

*Source:* `src/compiler/sema_expr.cpp#L9806-L9818`

### `expr.struct-lit.infer-nested-typevar` — Recursive inference of nested struct type-params

A struct type-param appearing nested inside a compound field type (generic struct/enum type-args, array/pointer element, tuple element, or fn-ptr/closure parameter and return types) is inferred by parallel structural walk of the declared field type and the field value type; only the struct's own as-yet-uninferred type-params are bound, and binding to an Error/IntLit/FloatLit value type is skipped.

*Source:* `src/compiler/sema_expr.cpp#L9730-L9764`, `src/compiler/sema_expr.cpp#L9819-L9823`

### `expr.struct-lit.uninferred-typevar-fallback-hint` — Fallback type-param resolution from hint then error

Any struct type-param not inferred from fields is resolved from the expected-type hint if available; a param still unresolved after the hint becomes an error type (poisoning the instantiation). The hint struct type also supplies type-args positionally and variadic params consume the hint's trailing type-args.

*Source:* `src/compiler/sema_expr.cpp#L9825-L9856`

### `expr.struct-lit.unknown-field-error` — Struct-lit may not name a field absent from the definition

A field name in a struct literal that is neither a field of the effective struct definition nor a variadic-field expansion is an 'unknown field' error.

*Source:* `src/compiler/sema_expr.cpp#L9878-L9899`, `src/compiler/sema_expr.cpp#L10049-L10076`

### `expr.struct-lit.duplicate-field-error` — Struct-lit may not initialize a field twice

Initializing the same field more than once in a struct literal is a 'duplicate field' error.

*Source:* `src/compiler/sema_expr.cpp#L9900-L9905`, `src/compiler/sema_expr.cpp#L10077-L10082`

### `expr.struct-lit.intlit-fits-field` — IntLit field value must fit the declared field type

An integer-literal field value must fit within the declared field type's range; otherwise a 'value V does not fit in T' error. The same fit-check applies element-wise to array-literal, tuple-literal, and nested array/tuple-literal field values against the corresponding narrow element types.

*Source:* `src/compiler/sema_expr.cpp#L9962-L9967`, `src/compiler/sema_expr.cpp#L10102-L10168`

### `expr.struct-lit.variadic-field-expansion` — Variadic struct field accepts expansion names `name_*`

A variadic struct field named `name` accepts literal field names of the form `name_<suffix>`; each such expansion value is type-checked against the variadic field's type and the variadic field is marked initialized.

*Divergence:* A6

*Source:* `src/compiler/sema_expr.cpp#L9882-L9897`, `src/compiler/sema_expr.cpp#L10052-L10074`

### `expr.struct-lit.union-single-field` — Union literals initialize exactly one field; missing-field check skipped

For a union struct, the all-fields-initialized check is suppressed: a union literal initializes only one (active) field by design.

*Divergence:* A6

*Source:* `src/compiler/sema_expr.cpp#L10015-L10021`, `src/compiler/sema_expr.cpp#L10215-L10221`

### `expr.struct-lit.field-value-moved` — Move-typed field values are consumed by the literal

When constructing a struct literal, each field value whose type is a move type is marked moved (consumed) in the surrounding scope, preventing later use and double-drop.

*Source:* `src/compiler/sema_expr.cpp#L10023-L10033`, `src/compiler/sema_expr.cpp#L10223-L10227`

### `expr.struct-lit.outlives-check` — Struct `where 'a: 'b` outlives constraints enforced at literal

A struct literal must satisfy the struct's declared lifetime outlives constraints (`where 'a: 'b`), checked against the literal's lifetime args, the struct's field types, and the supplied field values.

*Source:* `src/compiler/sema_expr.cpp#L10035-L10041`, `src/compiler/sema_expr.cpp#L10232-L10238`

### `expr.struct-lit.dyn-auto-bounds-at-field-init` — Auto-trait bounds checked at dyn field-init coercion

When a field value is coerced to a field type that is a dyn-trait with auto-trait bounds (e.g. `&dyn Trait + Send`), the value's type must satisfy those auto-trait bounds.

*Source:* `src/compiler/sema_expr.cpp#L10098-L10101`

### `expr.struct-lit.functional-update` — Functional struct update `..base` fills unset fields

A struct literal may end with a functional-update base `S { ..., ..base }`. The `base` expression must have struct type S (same struct name); every field not explicitly initialized is read from base via field-read, with the struct's type-params substituted into each carried field's declared type (generic path). A base of differing struct type is an error.

*Source:* `src/compiler/sema_expr.cpp#L9970-L10013`, `src/compiler/sema_expr.cpp#L10171-L10213`, `src/compiler/sema_render.cpp#L386-L391`

### `expr.struct-lit.self-resolves-to-impl-target` — `Self { .. }` resolves to impl target struct

A struct-literal named `Self` resolves to the struct/zoned-datatype bound to the literal key "Self" in the enclosing impl's type-param scope (current_type_params_), provided that binding's kind is Struct or ZonedStruct; the resolved name replaces "Self" for the remainder of lowering.

*Source:* `src/compiler/sema_expr.cpp#L9874-L9882`

### `expr.struct-lit.name-lookup-struct-or-datatype` — struct-literal name resolves to struct or zoned datatype

A struct-literal's name is looked up first as a plain struct (find_struct_by_name), then, if not found, as a zoned datatype (find_datatype_by_name); resolution via the latter marks the literal as a zoned-datatype literal for the rest of lowering.

*Source:* `src/compiler/sema_expr.cpp#L9885-L9891`

### `expr.struct-lit.alias-resolution` — struct-literal name resolves through non-generic type alias

If a struct-literal's name does not directly name a struct/datatype, it is looked up as a type alias with no type-params and no lifetime-params (`type Alias = Struct;`) in the current package, then in each wildcard-imported package; a generic alias is not resolved this way.

*Source:* `src/compiler/sema_expr.cpp#L9894-L9918`

### `expr.struct-lit.unknown-struct-error` — unresolved struct-literal name is an error

A struct-literal whose name resolves to neither a struct/datatype nor a non-generic alias is a compile error ("unknown struct '&lt;name&gt;'").

*Source:* `src/compiler/sema_expr.cpp#L9921-L9924`

### `expr.struct-lit.union-single-active-field` — union literal initializes exactly one field

A union-typed literal (`U { .. }`) must supply exactly one field-init (FIELD_INIT or FIELD_SHORTHAND); zero or more-than-one inits is a compile error naming the observed count.

*Source:* `src/compiler/sema_expr.cpp#L9931-L9945`

### `expr.struct-lit.private-field-cross-package-forbidden` — cross-package construction requires all fields pub

Constructing a struct literal from a package other than the struct's declaring package is a compile error if the struct has any non-pub field.

*Source:* `src/compiler/sema_expr.cpp#L9948-L9956`

### `expr.struct-lit.field-shorthand` — field-init shorthand

`Struct { x }` (a FIELD_SHORTHAND field-init) is equivalent to `Struct { x: x }`: it looks up `x` as an in-scope variable; an undefined `x` is a compile error.

*Source:* `src/compiler/sema_expr.cpp#L9981-L9989`

### `expr.struct-lit.enum-field-hint-pins-typeargs` — concrete-enum field type hints payload-less enum literal value

When a struct field's declared type is a concrete (type-args resolved) generic Enum, that type is set as the expected-type hint while lowering the field's initializer, and after lowering, a bare/payload-less enum-literal value for that field is retyped against the field's declared enum type (so it takes the heap-allocated representation matching the field's slot instead of an inline-discriminant representation).

*Source:* `src/compiler/sema_expr.cpp#L9968-L9979`, `src/compiler/sema_expr.cpp#L9990-L10005`

### `expr.struct-lit.closure-field-hint-from-fn-bound` — Fn-bound field type infers closure param types

When a struct field's declared type is a type-param bounded by an `Fn`-like trait, an untyped closure-literal value supplied for that field infers its parameter types from the bound during lowering of the field's initializer.

*Source:* `src/compiler/sema_expr.cpp#L9993-L10001`

### `expr.struct-lit.variadic-field-name-convention` — variadic field accepts `name_suffix` field-init keys

A declared variadic struct field named `f` matches struct-literal field-init keys of the form `f_<suffix>` (any key that starts with `f_` and is longer than `f_`); each matching field-init is type-checked against the variadic field's declared element type.

*Divergence:* A6

*Source:* `src/compiler/sema_expr.cpp#L10104-L10106`, `src/compiler/sema_expr.cpp#L10216-L10230`, `src/compiler/sema_expr.cpp#L10388-L10407`

### `expr.struct-lit.unknown-or-duplicate-field-error` — unknown/duplicate field-init is an error

A struct-literal field-init key that matches no declared field (and no variadic-prefix match) is an "unknown field" compile error; a field-init key that repeats an already-initialized field is a "duplicate field" compile error.

*Source:* `src/compiler/sema_expr.cpp#L10211-L10238`, `src/compiler/sema_expr.cpp#L10382-L10415`

### `expr.struct-lit.intlit-field-fits` — integer-literal field value must fit declared field width

An integer-literal field-init value (directly, or as an element of an array/tuple field value, recursively through nested arrays/tuples) must fit within the declared field's integer type; a value that does not fit is a compile error naming the field/element path and the offending type.

*Source:* `src/compiler/sema_expr.cpp#L10295-L10300`, `src/compiler/sema_expr.cpp#L10441-L10501`

### `expr.struct-lit.functional-update-generic` — `..base` on a generic struct-literal fills unset fields from base

For a generic struct-literal, `Struct { .., ..base }` fills every field not explicitly field-init'd by a field_read off `base`, with the carried field's declared type substituted using the literal's resolved struct type-args.

*Source:* `src/compiler/sema_expr.cpp#L10308-L10346`

### `expr.struct-lit.functional-update-base-type-check` — `..base` must have the literal's own struct type

`..base` in a struct-literal requires `base`'s type be the same struct (Struct or ZonedStruct kind, matching struct_name) as the struct being constructed; a mismatch is a compile error naming both the expected and actual type.

*Source:* `src/compiler/sema_expr.cpp#L10308-L10322`, `src/compiler/sema_expr.cpp#L10513-L10523`

### `expr.struct-lit.functional-update-nongeneric` — `..base` on a non-generic struct-literal fills unset fields

For a non-generic struct-literal, `..base` fills every field not explicitly initialized by a field_read off `base`, reusing `base`'s VarRef when it is a simple variable reference, otherwise re-lowering the base expression for each remaining field (which may evaluate a non-trivial `base` expression more than once).

*Note:* Re-lowering a complex (non-VarRef) base expression once per remaining field means a side-effecting base expression executes multiple times; comment at L10539 flags this as accepted/rare rather than fixed.

*Source:* `src/compiler/sema_expr.cpp#L10524-L10546`

### `expr.struct-lit.dyn-auto-bound-field-coercion` — dyn-trait field coercion checks auto-trait bounds

Coercing a field-init value into a declared `&dyn Trait + AutoBound` field type checks that the value satisfies the required auto-trait bound (check_dyn_auto_bounds_at_coercion) at the coercion site.

*Source:* `src/compiler/sema_expr.cpp#L10433-L10434`

### `expr.struct-lit.result-type-package-qualified` — non-generic struct-literal result type carries resolving package

The result type of a non-generic struct-literal carries the package name that resolved the struct (resolve_struct_pkg_) alongside the struct name.

*Source:* `src/compiler/sema_expr.cpp#L10576`

### `expr.struct-lit.field-type-mismatch-error` — Struct-lit field value must be compatible with declared field type

Each struct-literal field-init value's type must be compatible (`types_compatible`) with the field's declared type after substituting the struct's type-params into it; otherwise it is a compile error reporting expected vs. got types. A closure value coercible to a declared fn-ptr field type is accepted. When the substituted field type still contains a TypeVar/ConstVar/CfgSlotType/AssocType, the comparison is deferred to mono-time substitution rather than reported at sema.

*Source:* `src/compiler/sema_expr.cpp#L10278-L10286`, `src/compiler/sema_expr.cpp#L10417-L10424`, `src/compiler/sema_expr.cpp#L9906-L9953`, `src/compiler/sema_expr.cpp#L9916-L9921`, `src/compiler/sema_expr.cpp#L9926-L9944`

### `expr.struct-lit.field-variance-check` — Struct-literal field-init coercion is variance-checked, permissively

Each field-init's value type is checked against the declared field type under variance rules (check_variance) in permissive mode: the struct's lifetime-args are bound at the construction (struct-literal) site rather than at function scope, so the caller's region inference fills elided source regions. The check is skipped when the declared field type still contains an unresolved type-param (unification handles those).

*Source:* `src/compiler/sema_expr.cpp#L9954-L9961`, `src/compiler/sema_expr.cpp#L10092-L10097`, `src/compiler/sema_expr.cpp#L10291-L10294`, `src/compiler/sema_expr.cpp#L10427-L10430`

### `expr.struct-lit.missing-field-error` — Non-union struct literal must initialize every field

Unless the struct-literal targets a union (union literals initialize exactly one field), every declared field must be initialized — by an explicit field initializer, by variadic-field expansion (`name_i` entries filling a variadic field `name`), or from `..base` — otherwise it is a compile error: "struct literal '&lt;struct&gt;': field '&lt;name&gt;' not initialized".

*Source:* `src/compiler/sema_expr.cpp#L10015-L10021`, `src/compiler/sema_expr.cpp#L10215-L10221`, `src/compiler/sema_expr.cpp#L10350-L10354`, `src/compiler/sema_expr.cpp#L10548-L10554`

### `expr.struct-lit.field-init-and-shorthand` — Struct literal field forms

A struct literal is `Name { f: v, ... }`; fields are either `name: value` (FIELD_INIT) or shorthand `name` (FIELD_SHORTHAND). The name may carry turbofish type args `Name::<T> { ... }`.

*Source:* `src/compiler/sema_render.cpp#L346-L385`

## Constructors (`expr.ctor`)

### `expr.ctor.prelude-option-result-shorthand` — Bare Some/Ok/Err prelude variant constructor

If no function named `Some`/`Ok`/`Err` resolves, a bare call `Some(x)`/`Ok(x)`/`Err(x)` constructs the corresponding `Option`/`Result` variant, provided that enum (with that variant) is in scope; a user-defined function of the same name shadows this (function lookup runs first).

*Source:* `src/compiler/sema_expr.cpp#L5921-L5942`

### `expr.ctor.variant-alias-shorthand` — Bare enum-variant constructor via use-alias

A `use Enum.{V, …};` import registers variant aliases; a bare call `V(payload)` whose name is an imported variant alias constructs that enum's variant `V` (typed via enum-literal lowering with payload typing), when no function of that name resolved.

*Divergence:* Logos `use Type.{V}` variant-import surface (pkg `.` / item `::` path model)

*Source:* `src/compiler/sema_expr.cpp#L5943-L5953`

## Enum literals (`expr.enum-lit`)

### `expr.enum-lit.forms` — Enum variant literal forms

Enum variants are written `E::V` (unit), `E::V(args)` (tuple payload; optional turbofish `E::V::<T,…>(args)`), `E::V { f: e, … }` (struct-shape payload; field names resolved to canonical positional indices via the variant's payload field names), or bare turbofish `E::V::<T,…>` (generic variant reference without a call). The qualified-as form `<T as Trait>::V` and the dotted-package-prefix form `pkg.path.E::V` (unit variant; the last dotted segment is the enum type) are also accepted.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2787-L2816`, `tools/peg_gen_cpp/grammars/logos.peg#L2847-L2876`

### `expr.enum-lit.unknown-variant` — Enum literal references an existing variant

In an enum literal `E::V(args)`, `V` must be a declared variant of enum `E`; otherwise the program is ill-formed (diagnostic "enum 'E' has no variant 'V'").

*Source:* `src/compiler/sema_expr.cpp#L12287-L12293`

### `expr.enum-lit.args-shape` — Enum-literal argument list shape

The payload argument list of an enum literal is accepted either as a direct sequence of argument expressions or as a map containing an ITEMS sequence; both forms denote the same ordered payload list.

*Source:* `src/compiler/sema_expr.cpp#L12321-L12348`

### `expr.enum-lit.unit-payload-kept` — Unit payload retained, not elided

A unit-typed payload argument (e.g. `()` in `Result::Ok(())`) is retained as a real payload entry; void/unit payloads are not filtered out.

*Source:* `src/compiler/sema_expr.cpp#L12299-L12300`, `src/compiler/sema_expr.cpp#L12321-L12348`

### `expr.enum-lit.nested-hint-projection` — Per-payload type hint via outer-hint projection

When the surrounding expected type is `E<A1..An>` for the same enum `E`, each payload slot whose formal type is a TypeVar receives a per-argument expected-type hint computed by substituting `E`'s type parameters with the outer hint's type-args; this lets a nested enum literal (e.g. inner `Result::Ok` inside `Option::Some(Result::Ok(42))`) lower with its own concrete enum hint.

*Source:* `src/compiler/sema_expr.cpp#L12301-L12320`, `src/compiler/sema_expr.cpp#L12327-L12338`

### `expr.enum-lit.arity` — Non-variadic variant arity

For a non-variadic variant, the number of payload arguments must equal the number of declared payload types; otherwise the program is ill-formed ("expects N args, got M").

*Source:* `src/compiler/sema_expr.cpp#L12524-L12527`

### `expr.enum-lit.arg-type-compat` — Payload argument type compatibility

Each non-variadic payload argument's type must be compatible with its resolved formal payload type; an incompatibility is ill-formed ("arg i: expected X, got Y").

*Source:* `src/compiler/sema_expr.cpp#L12528-L12535`

### `expr.enum-lit.intlit-fit` — Integer-literal payload range check

An integer-literal payload argument whose constant value does not fit in the target integer type's range is ill-formed; this check recurses into array-literal elements and tuple-literal elements (and their nested array/tuple sub-elements) of the payload type.

*Source:* `src/compiler/sema_expr.cpp#L12536-L12542`, `src/compiler/sema_expr.cpp#L12543-L12608`

### `expr.enum-lit.variadic` — Variadic variant payload checking

For a variadic variant, every payload argument is checked for compatibility against (and integer-literal fit within) the single pack element type (the first declared payload type), with no arity constraint.

*Source:* `src/compiler/sema_expr.cpp#L12524-L12527`, `src/compiler/sema_expr.cpp#L12610-L12628`

### `expr.enum-lit.self-resolves-to-enclosing-enum` — `Self::Variant` resolves to the enclosing enum

Inside an `impl Enum` body, the path head `Self` in a unit-variant or struct/tuple-shaped variant literal resolves to the enclosing enum's name, provided `Self` is bound to a type of enum kind.

*Source:* `src/compiler/sema_expr.cpp#L11585-L11590`, `src/compiler/sema_expr.cpp#L11732-L11737`

### `expr.enum-lit.type-alias-peel` — Variant path through a non-generic enum type alias

A variant-literal path head that names a non-generic type alias whose aliased type is an enum is rewritten to the underlying enum name before variant lookup; generic aliases are not peeled here.

*Source:* `src/compiler/sema_expr.cpp#L11591-L11598`, `src/compiler/sema_expr.cpp#L11738-L11745`

### `expr.enum-lit.unknown-enum-error` — Unknown enum / unknown variant diagnostics

A variant-literal path whose head names no enum (after Self/alias resolution and all assoc-const/fn-ptr fallbacks) is an error `unknown enum '<name>'`; a known enum with no matching variant is an error `enum '<E>' has no variant '<V>'`.

*Source:* `src/compiler/sema_expr.cpp#L11681-L11682`, `src/compiler/sema_expr.cpp#L11701-L11702`, `src/compiler/sema_expr.cpp#L11838-L11847`

### `expr.enum-lit.unit-variant-hint-type-args` — Payload-less variant on a generic enum infers type args from the surrounding hint

A payload-less variant of a generic enum (e.g. `Option::None`) takes its type arguments from the surrounding type hint when the hint is the same enum with a matching type-arg arity; otherwise the result type is the bare (un-parameterized) enum.

*Source:* `src/compiler/sema_expr.cpp#L11704-L11725`

### `expr.enum-lit.struct-shape-named-fields` — Struct-shaped variant literal `E::V { f: e, .. }`

A struct-shaped variant literal binds named field initializers (and shorthands `name` ⇒ `name` var-ref) to the variant's declared payload fields by name, producing positional payload in declaration order. Errors: unknown field name, field specified more than once, missing field(s) (all reported together), and using `{}` form on a non-struct-shape variant. An empty struct-shape variant `E::Empty {}` is accepted with empty payload.

*Source:* `src/compiler/sema_expr.cpp#L11853-L11966`

### `expr.enum-lit.payload-arity-check` — Non-variadic variant payload arity must match

For a non-variadic variant, the number of supplied payload arguments must equal the declared payload arity; mismatch is an error `<E>::<V> expects N args, got M`. Each payload argument's type must be compatible with the declared (substituted) payload type.

*Source:* `src/compiler/sema_expr.cpp#L12168-L12180`

### `expr.enum-lit.intlit-payload-fits` — Integer-literal payload must fit the declared payload type

An integer-literal payload argument (directly, or as an element of an array/tuple payload, recursively) must fit within the declared narrow integer payload type; an out-of-range value is an error.

*Source:* `src/compiler/sema_expr.cpp#L12180-L12251`

### `expr.enum-lit.payload-type-inference` — Generic enum type-arg inference from payload and hint

For a generic enum, each type parameter is inferred from the corresponding payload: a bare-TypeVar payload binds the param to the argument's type; a structural payload type is unified against the argument to extract nested bindings. Unresolved integer/float literal payloads default to i32/f64 unless the surrounding hint pins the param to a concrete type, in which case the hint wins and the literal is widened to it. Params still unresolved after payload inference are filled from a matching enum hint.

*Source:* `src/compiler/sema_expr.cpp#L12059-L12138`, `src/compiler/sema_expr.cpp#L12082-L12127`

### `expr.enum-lit.dyn-payload-arg` — Concrete payload into a dyn-typed enum slot widens the type arg

When the hint pins a type parameter to a trait-object-wrapping type (e.g. `Box<dyn Tr>`) but the payload argument is a concrete coercible value (e.g. `Box<Sq>`), the constructed enum's type argument records the dyn type while the payload expression stays concrete; the store later unsize-fattens it into the dyn slot.

*Source:* `src/compiler/sema_expr.cpp#L12097-L12119`

## Writ literals (`expr.writ-lit`)

### `expr.writ-lit.int-small-inline-else-boxed` — Writ literal integer encoding: i24-inline vs boxed i64

In a Writ SDN literal, an integer in [-2^23, 2^23-1] is encoded inline as a 24-bit value; any integer outside that range is boxed as a 64-bit value.

*Source:* `src/compiler/mlir_gen_expr.cpp#L5831-L5836`

### `expr.writ-lit.value-kinds` — Writ literal value kinds and their encodings

A Writ SDN literal value is one of: null; bool (0/1); int (see int encoding); float (boxed f64); string; array (homogeneous scalar arrays I8..F64 use a typed array, otherwise an object array); map (integer-keyed I32/U32/I64/U64 use a typed map, otherwise an object map keyed by string); type (a tiny map carrying kind/uid/name); or capture/PARAM (an inline placeholder bound to a value index, substituted at runtime).

*Divergence:* Logos addition (Writ SDN literals); no Rust equivalent.

*Note:* Writ is a Logos-specific data substrate (zoned SDN); these encodings are language-level data-literal semantics, not a Rust feature.

*Source:* `src/compiler/mlir_gen_expr.cpp#L5759-L5882`, `src/compiler/mlir_gen_expr.cpp#L5820-L5882`

### `expr.writ-lit.capture-context-save-restore` — Nested @-literals do not clobber the outer capture context

Lowering an @-literal establishes a fresh capture context for the duration of the literal and restores the prior context afterward, so a static @-literal nested inside a `${expr}` capture does not disturb outer `$`-captures.

*Source:* `src/compiler/sema_expr.cpp#L15407-L15421`

### `expr.writ-lit.result-type` — @-literal result type depends on presence of captures

An @-literal with no captures has type `WritStatic`; an @-literal with one or more `$`-captures has the return type of `writ_build_from_template` (an Rc&lt;Writ&gt;), which requires `use logos.lang.writ.tmpl;` to be in scope.

*Source:* `src/compiler/sema_expr.cpp#L15422-L15444`

## Name references (`expr.name`)

### `expr.name.innermost-scope-wins` — Name resolution: innermost binding wins, then module consts

A name resolves to its innermost in-scope local binding (shadowing-correct); if no local binding exists it falls back to a module-level const; otherwise it is unresolved. Slot lookup (for the Phase-1 dense-slot scheme) follows the identical innermost-wins order; names with no local binding carry no slot and fall back to name-keying downstream.

*Source:* `src/compiler/sema_impl.hpp#L2366-L2374`, `src/compiler/sema_impl.hpp#L2376-L2387`

## Paths (`expr.path`)

### `expr.path.assoc-const-disambiguation` — `Type::member` not naming an enum variant is tried as an associated const

When a `Name::member` path parses as an enum literal but `Name` is not a known enum, it is resolved as an associated const access in order: (1) inherent assoc const `impl Name { const member }`; (2) trait assoc const `<Tr>::member` for any trait `Tr` impl'd for `Name`; (3) generic assoc-const projection when `Name` is a bound type parameter. The const's value AST is lowered once and cached.

*Source:* `src/compiler/sema_expr.cpp#L11604-L11638`, `src/compiler/sema_expr.cpp#L11691-L11700`

### `expr.path.method-as-fn-pointer` — Path to a non-generic method in value position becomes a fn pointer

A path `Type::method` (or `Trait::method`) used in value position, not naming a variant or const, denotes a function-pointer value when it resolves to a single non-generic method: its type is `FnPtr(param_types) -> ret`. For a trait-qualified head, resolution succeeds only when exactly one impl of the trait is in scope; otherwise it is ambiguous.

*Source:* `src/compiler/sema_expr.cpp#L11639-L11680`, `src/compiler/sema_expr.cpp#L11773-L11814`

### `expr.path.typaram-static-method-call` — `Z::method::<..>(args)` on a bound type parameter

A call `Z::method::<TArgs>(args)` where `Z` is a type parameter bound by a trait declaring a static `method` dispatches to the bound's static method, disambiguated from generic enum-variant construction by `Z` being a bound type parameter.

*Source:* `src/compiler/sema_expr.cpp#L11815-L11837`

## Variable references (`expr.var-ref`)

### `expr.var-ref.undefined` — Reference to an undefined name is an error

A variable reference whose name resolves to no local binding, const-generic parameter, function, enum variant, or unit struct is a compile error: 'undefined variable'.

*Source:* `src/compiler/sema_expr.cpp#L583-L584`

### `expr.var-ref.const-param-value-use` — Const-generic parameter usable in value position

A const-generic parameter `<const N: T>` referenced in expression position evaluates to a value of its underlying numeric type T (default i64); monomorphization substitutes the concrete constant.

*Source:* `src/compiler/sema_expr.cpp#L481-L490`

### `expr.var-ref.fn-item-type` — Bare function name has a distinct per-function fn-item type

A function name used as a value has a zero-sized fn-item type unique to that function (distinct type per function/instantiation), which auto-coerces to the corresponding `fn(T)->R` pointer type at value-use sites.

*Source:* `src/compiler/sema_expr.cpp#L491-L510`

### `expr.var-ref.unit-struct-value` — Unit struct name in value position constructs it

A bare name of a known zero-field, non-generic struct in value position constructs that struct (unit-struct construction); a fielded struct still requires `S { … }` form.

*Related:* `expr.var-ref.undefined`

*Source:* `src/compiler/sema_expr.cpp#L573-L582`

### `expr.var-ref.bare-variant-alias` — Imported no-payload enum variant usable as a bareword

A no-payload enum variant brought into scope via `use Type.{V, …};` (or the prelude bareword `None`) can be referenced as a bare identifier, constructing that variant; payload-carrying variants require call syntax.

*Source:* `src/compiler/sema_expr.cpp#L511-L571`

## Data references (`expr.dataref`)

### `expr.dataref.field-ergonomic` — DataRef&lt;T&gt; ergonomic field read

For `p: DataRef<T>` where T is a zoned-struct type declaring field `f`, `p.f` desugars to `p.ptr().f` (bypassing an explicit `let pw = p.ptr()` intermediate); this access requires an enclosing `unsafe` context: "DataRef&lt;T&gt;.&lt;f&gt;: field access requires unsafe context".

*Source:* `src/compiler/sema_expr.cpp#L9773-L9791`

## Generic references (`expr.generic-ref`)

### `expr.generic-ref.turbofish-callee` — IDENT::&lt;TARGS&gt; at expression position

`IDENT::<TARGS>` at expression position (GENERIC_REF, CALLEE=ident, TYPE_PARAMS=type_arg_list) names an explicitly-instantiated generic item (function or associated item) as a first-class expression-position reference, independent of any surrounding call.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L283`

## Turbofish (`expr.turbofish`)

### `expr.turbofish.generic-ref` — Turbofish generic reference and static call

`IDENT::<T,…>` is a generic reference (`GENERIC_REF`, explicit type arguments applied to a function/item), available in both the ordinary and no-struct-lit expression grammars. `IDENT::<T,…>::METHOD` is a static call (`STATIC_CALL`) on the type-applied receiver.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2621-L2622`, `tools/peg_gen_cpp/grammars/logos.peg#L2756-L2760`, `tools/peg_gen_cpp/grammars/logos.peg#L2817-L2820`

## Static references (`expr.static`)

### `expr.static.mut-read-unsafe` — Reading a mutable static requires unsafe

Reading a `static mut` outside an `unsafe` block is a compile error (Rust items.static.mut.safety); the gate is suppressed when the name is shadowed by a local binding or a const-generic parameter.

*Source:* `src/compiler/sema_expr.cpp#L595-L628`

### `expr.static.extern-access-unsafe` — Accessing an extern static requires unsafe

Any access to an extern static outside an `unsafe` block is a compile error (Rust items.extern.static), with the same local/const-param shadowing suppression as mutable statics.

*Related:* `expr.static.mut-read-unsafe`

*Source:* `src/compiler/sema_expr.cpp#L604-L607`, `src/compiler/sema_expr.cpp#L620-L623`

## Associated constants (`expr.assoc-const`)

### `expr.assoc-const.generic-typeparam-projection` — `T::CONST` projection through a bound type-parameter

`T::CONST`, where T is an abstract type-parameter whose bound trait declares `const CONST`, lowers to a zero-arg accessor call `T__kassoc_CONST()`; monomorphization rewrites the `T__` prefix to the concrete instantiating type, and lower_impl_block emits the per-impl accessor. Not treated as such a projection (returns null) unless `cname` names a bound-trait-declared const of the abstract type-param.

*Source:* `src/compiler/sema_impl.hpp#L3984-L3989`

## Static / free calls (`expr.static-call`)

### `expr.static-call.qualified-path-drops-package-prefix` — `pkg.path.Type::method()` resolves on the last segment as the type

In a qualified static call `pkg.path.Type::member(args)`, the LAST dotted segment names the type/class; the package prefix is dropped (type/method resolution and arg lowering are not package-filtered, only free-fn lookups are).

*Source:* `src/compiler/sema_expr.cpp#L13087-L13094`

### `expr.static-call.self-resolves-to-impl-type` — `Self::method()` resolves Self to the impl's concrete type

Inside an impl body, `Self::method()` resolves `Self` to the impl's concrete type name (struct/zoned-struct via concrete name, enum via enum name) before static-method resolution, equivalent to writing the type name.

*Source:* `src/compiler/sema_expr.cpp#L13099-L13111`

### `expr.static-call.enum-variant-vs-static-method` — `Enum::Name(...)` constructs a variant only when Name is a variant

When the class is an enum (directly or via a non-generic type-alias to an enum), `Enum::Name(args)` lowers as a variant construction iff Name matches a declared variant; otherwise it falls through to ordinary static-method resolution (trait-impl-on-enum).

*Source:* `src/compiler/sema_expr.cpp#L13113-L13146`

### `expr.static-call.type-alias-resolution` — Static calls resolve non-generic type aliases to the target type

A non-generic type alias used as a static-call class resolves to its target struct/zoned-struct (using the concrete name when type-args are present) before mangling the method symbol.

*Source:* `src/compiler/sema_expr.cpp#L13149-L13164`

### `expr.static-call.array-default` — `<[E; N]>::default()` synthesizes elementwise default

`default()` with no args on an array type (named via a non-generic alias `type M = [E; N]`) synthesizes `[E::default(); N]`; if the element type has no Default impl it is an error. Arrays carry no `__default` symbol.

```logos
type M = [i32; 4]; let a = M::default();
```

*Source:* `src/compiler/sema_expr.cpp#L13183-L13196`

### `expr.static-call.trait-qualified-ufcs` — Trait-qualified UFCS `Trait::method(recv, ...)`

When the class names a TRAIT (not a struct/enum/datatype/type-param) and args are non-empty, `Trait::method(recv, ...)` dispatches on the first argument's concrete receiver type (auto-derefed through refs/ptrs): struct/zoned-struct by name, enum by name, or primitive by type_str. The rewrite to `<recv-type>__<method>` commits only if that concrete symbol actually resolves; otherwise normal resolution and error reporting proceed.

*Divergence:* Rust-conformant (DIVERGENCES.md: trait-qualified UFCS supported)

*Source:* `src/compiler/sema_expr.cpp#L13198-L13248`

### `expr.static-call.type-param-shadows-struct` — In-scope abstract type-param shadows a same-name concrete type

A bounded type-param used as the static-call class (`S::method` with `S: Bound`) dispatches through the trait bound and NOT through a same-name concrete struct in scope; an active abstract type-param (resolves to a TypeVar) suppresses concrete-symbol lookup so resolution falls to generic-static dispatch.

*Source:* `src/compiler/sema_expr.cpp#L13250-L13263`, `src/compiler/sema_expr.cpp#L13268-L13269`

### `expr.static-call.turbofish-concrete-partial-spec` — Turbofish on a partial-spec static call builds the concrete mangled name

For `Type::<A, B>::method(...)` where a concrete partial-spec impl registers methods under the concrete mangled name, if base lookup misses and all turbofish args are concrete (non-TypeVar), the concrete instantiation name (datatype vs struct) is built and the symbol re-resolved.

*Source:* `src/compiler/sema_expr.cpp#L13296-L13328`

### `expr.static-call.unsafe-requires-unsafe-context` — Calling an unsafe static method requires an unsafe context

A call to an unsafe static method outside an unsafe context is an error.

*Source:* `src/compiler/sema_expr.cpp#L13532-L13533`, `src/compiler/sema_expr.cpp#L13409-L13410`

### `expr.static-call.generic-method-infers-type-args` — Generic static method infers concrete type-args outside generic context

A generic static method (type-params from the enclosing impl) called outside a generic context (no TypeVar/AssocType in value or explicit type-args) is resolved by turbofish args if present, else by argument inference, then routed through the generic-call finisher to trigger the concrete instantiation. Inside a generic body, it is emitted with TypeVar type-args (or turbofish) and the return type substituted, for mono to rename to the concrete struct method.

*Source:* `src/compiler/sema_expr.cpp#L13538-L13618`

### `expr.static-call.arg-count-and-type-check` — Static call arity and per-argument type checking

A non-generic static call checks argument count against the parameter list (error on mismatch) and coerces then type-checks each argument against its parameter (error on incompatibility). By-value move-typed args (and owning Box&lt;dyn&gt;) are marked moved so scope-end drops do not fire on transferred locals.

*Source:* `src/compiler/sema_expr.cpp#L13621-L13643`

## Unary operators (`expr.unary`)

### `expr.unary.negation` — Unary minus

`-x` negates: floating-point negation for floats, `0 - x` for integers.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1176-L1181`

### `expr.unary.not` — Unary not is logical on bool, bitwise on integers

`!x` is logical NOT (XOR with 1) when `x` is bool (i1) and bitwise complement (XOR with all-ones) when `x` is a wider integer. Applying `!` to a non-integer type is an error.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1183-L1198`

### `expr.unary.operator-set` — Unary / prefix operators

Prefix unary operators bind tighter than `as`-cast (and thus tighter than every binary operator): `*` deref, `&` borrow, `&mut` mutable borrow, `-` negate, `!` not. `&&v` lexes as the single AND token but denotes a double reference, lowering to nested address-of (mirrors DOUBLE_REF_TYPE at the type level).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2531-L2537`, `tools/peg_gen_cpp/grammars/logos.peg#L2708-L2716`

### `expr.unary.double-ref` — `&&e` desugars to `&(&e)`

Unary `&&e` (lexed as a single AND token) desugars to `&(&e)`: lower e, build ADDR_OF(ADDR_OF(e)) — inner ref type `&T`, outer `&&T`. If e's type is Error, propagate Error (return error-expr) instead.

*Source:* `src/compiler/sema_expr.cpp#L2495-L2503`

### `expr.unary.neg-literal-fold` — `-LIT_INT` folds the sign before range-checking

`-LIT_INT` folds the negative sign into the integer-literal lowering (negate=true) rather than lowering the bare literal and negating the result, so a suffix-edge value like `-128i8` (the i8 minimum) is accepted — lowering the bare `128i8` first would reject it as out-of-range for i8.

*Source:* `src/compiler/sema_expr.cpp#L2627-L2631`

### `expr.unary.operator-overload` — Unary `-`/`!` on a struct dispatch to Neg/Not

Unary `-x` / `!x` where x has Struct kind dispatch to the operator-overload trait method: `-x` resolves `<Type>__neg` (trait Neg, method `neg`), `!x` resolves `<Type>__not` (trait Not, method `not`), looked up via `find_func_by_base_and_signature` against the concrete struct name and invoked as a static call with `x` as sole argument. If no matching impl exists, lowering falls through to the built-in numeric/bool unary rules (which then reject the struct operand).

*Source:* `src/compiler/sema_expr.cpp#L2639-L2655`

### `expr.unary.neg-numeric` — Unary `-` requires a numeric operand

Unary `-x` requires `x` numeric (`is_numeric`); the result type equals the operand's type exactly (no widening/promotion). Non-numeric operands are a diagnostic error.

*Source:* `src/compiler/sema_expr.cpp#L2658-L2660`, `src/compiler/sema_expr.cpp#L2673`

### `expr.unary.neg-unsigned-rejected` — Unary `-` on an unsigned integer type is rejected

Unary `-x` is rejected for every unsigned integer kind (u8/u16/u24/u32/u56/u64/u128) with a diagnostic instructing the user to cast to a signed type first (e.g. `-(x as i64)`); negation on an unsigned operand would otherwise wrap silently.

*Source:* `src/compiler/sema_expr.cpp#L2661-L2672`

### `expr.unary.not-bool-or-integer` — Unary `!` is bool-not or bitwise-not

Unary `!x`: for `x: bool` yields bool (logical NOT). For x of any integer kind or the untyped IntLit, yields bitwise NOT with result type = operand's type, except an untyped IntLit operand defaults its result type to i32. Any other operand type is a diagnostic error (result type defaults to bool so lowering continues).

*Source:* `src/compiler/sema_expr.cpp#L2674-L2683`

### `expr.unary.prefix-no-space` — Unary operators are prefix

Unary operators (`&`, `!`, `-`, etc.) are prefix and bind directly to their operand with no intervening space: `OP operand`.

*Source:* `src/compiler/sema_render.cpp#L128-L133`

## Binary operators (`expr.binop`)

### `expr.binop.short-circuit-logical` — Logical && / || short-circuit

For `a && b`: if `a` is false the result is false and `b` is not evaluated; otherwise the result is `b`. For `a || b`: if `a` is true the result is true and `b` is not evaluated; otherwise the result is `b`. Both produce a bool (i1).

*Source:* `src/compiler/mlir_gen_expr.cpp#L688-L728`

### `expr.binop.divergent-rhs-no-merge` — Diverging RHS of short-circuit yields no result

If the RHS of `&&`/`||` diverges (e.g. `c || return false`), the expression has no value and control does not reach the merge point; the result is taken solely from the short-circuit branch.

*Note:* Inferred from terminator check around the RHS store; the language-visible effect is that divergence propagates.

*Source:* `src/compiler/mlir_gen_expr.cpp#L714-L724`

### `expr.binop.integer-operand-widening` — Mixed integer-width binop widens narrower operand

When the two operands of a binary operator are integers of unequal width, the narrower is widened to the wider operand's width before the operation: zero-extension if the narrow operand's type is unsigned (u8/u16/u24/u32/u56/u64/u128) or bool, sign-extension otherwise.

*Source:* `src/compiler/mlir_gen_expr.cpp#L732-L765`

### `expr.binop.int-to-float-promotion` — Mixed int/float binop promotes integer to float

When one operand is a float and the other an integer, the integer is converted to the float operand's type: unsigned-to-float if the integer type is unsigned (u8..u128), signed-to-float otherwise.

*Source:* `src/compiler/mlir_gen_expr.cpp#L766-L797`

### `expr.binop.float-width-unification` — Mixed float-width binop unification

When operands are floats of different widths: an untyped float literal operand is coerced to the typed operand's float type; if both are typed, the narrower is widened to the wider.

*Related:* `coerce.intlit.to-integer-typevar-float`

*Source:* `src/compiler/mlir_gen_expr.cpp#L798-L820`

### `expr.binop.integer-overflow-trap` — Checked +/-/* trap on overflow

Integer `+`, `-`, `*` are checked: on overflow execution aborts (trap). Signed/unsigned overflow detection selects checked signed vs unsigned arithmetic by the LHS type's signedness. Intentional wrapping must use the `wrapping_add`/`wrapping_sub`/`wrapping_mul` intrinsics, which emit the unchecked operation.

*Divergence:* A13: always traps on integer +/-/* overflow regardless of build profile (Rust wraps in release, panics in debug); explicit wrapping_* for wraparound.

*Source:* `src/compiler/mlir_gen_expr.cpp#L835-L884`

### `expr.binop.div-rem-signedness` — Division and remainder select signed/unsigned by type

`/` and `%` lower to unsigned division/remainder when the LHS type is unsigned (u8..u128), signed division/remainder otherwise.

*Source:* `src/compiler/mlir_gen_expr.cpp#L885-L902`

### `expr.binop.shift-right-signedness` — Right shift is arithmetic or logical by signedness

`>>` performs a logical (zero-filling) shift when the LHS integer type is unsigned (u8..u128), and an arithmetic (sign-filling) shift otherwise.

*Source:* `src/compiler/mlir_gen_expr.cpp#L909-L922`

### `expr.binop.bitwise-and-shift-set` — Integer bitwise and shift operators

`&`,`|`,`^` are bitwise and/or/xor; `<<` is logical left shift. `&&`/`||` applied to already-i1 values reduce to bitwise and/or.

*Source:* `src/compiler/mlir_gen_expr.cpp#L903-L908`

### `expr.binop.tuple-structural-eq` — Tuple == / != is structural

For two tuples of equal arity with all-primitive element types, `==` is the conjunction of element-wise `==` and `!=` is its negation; comparison is performed per element (float elements compared with float equality), regardless of whether an operand is a named place or an SSA call-result value. Tuples containing non-primitive elements (str, nested tuple, struct) are not structurally compared by this rule.

*Note:* Restriction to all-primitive fields is an implementation limitation noted as a follow-up, not a language design intent.

*Source:* `src/compiler/mlir_gen_expr.cpp#L923-L1010`

### `expr.binop.tuple-lexicographic-order` — Tuple ordering is lexicographic

For two tuples of equal arity with all-primitive element types, `<`/`<=`/`>`/`>=` compare lexicographically (left-to-right element priority), folding right-to-left as `lt_i || (eq_i && rest)`; the all-equal result is false for strict (`<`,`>`) and true for non-strict (`<=`,`>=`). `>`/`>=` are the operand-swapped forms of `<`/`<=`. Per-element comparison uses unsigned ordering for unsigned/bool/char element types and signed otherwise.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1012-L1088`

### `expr.binop.ref-prim-autoderef-eq` — == / != on references to primitives dereferences

For `==`/`!=` where both operands are references (`&T`/`&mut T`) to the same primitive scalar type, the operands are dereferenced and the underlying values compared (value equality), rather than comparing the reference addresses. Matches the PartialEq-for-&T blanket impl.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1090-L1131`

### `expr.binop.pointer-equality` — Pointer == / != compares addresses

When operands are pointers (and not the deref-eligible reference-to-primitive case), `==`/`!=` compare pointer addresses.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1091-L1143`

### `expr.binop.comparison-signedness` — Ordering comparisons select signed/unsigned by type

`<`/`>`/`<=`/`>=` use unsigned comparison when the LHS type is unsigned (u8..u128) or bool, signed comparison otherwise. bool is treated as unsigned so that `false < true` holds (i1 false=0 &lt; true=1).

*Divergence:* bool ordering forced unsigned to preserve Rust's `false < true` despite i1 signed representation; documented inline as Rust-conformant intent.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1144-L1166`

### `expr.binop.precedence-cascade` — Binary operator precedence

Binary precedence, lowest→highest: logical (`&&`/`||`) &lt; comparison (`==` `!=` `<=` `>=` `<` `>`) &lt; bitor `|` &lt; bitxor `^` &lt; bitand `&` &lt; shift (`<<` `>>`) &lt; additive (`+` `-`) &lt; multiplicative (`*` `/` `%`) &lt; `as`-cast &lt; unary. All binary levels are left-associative. The same cascade exists in two parallel forms — the ordinary one (`log_expr` … `cast_expr`) and the no-struct-lit one (`log_expr_ns` … `cast_expr_ns`) used in if/while/for condition position, where a trailing `IDENT { ... }` would otherwise be greedily parsed as a struct literal.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2585-L2636`, `tools/peg_gen_cpp/grammars/logos.peg#L2602-L2606`, `tools/peg_gen_cpp/grammars/logos.peg#L2479-L2529`, `tools/peg_gen_cpp/grammars/logos.peg#L2645-L2706`

### `expr.binop.string-vs-str-eq` — String == str views String as str

For == and !=, when one operand is the struct String and the other is str (Slice&lt;u8&gt;), the String operand is viewed as str via .as_str() so the comparison proceeds through the str equality path.

```logos
s == "lit"
```

*Divergence:* Mirrors Rust `impl PartialEq<str> for String`.

*Source:* `src/compiler/sema_expr.cpp#L1782-L1808`

### `expr.binop.str-eq-by-content` — str equality compares contents via str_eq

== / != between two str operands (both Slice&lt;u8&gt; with u8 element) desugar to a call to stdlib `str_eq` (content comparison); != negates the result. With no `str_eq` in scope, falls back to (incorrect) pointer comparison.

*Source:* `src/compiler/sema_expr.cpp#L2194-L2221`

### `expr.binop.str-relational-cmp` — str ordering via str_cmp compared to 0

Relational operators {&lt;,&lt;=,&gt;,&gt;=} between two str operands desugar to `str_cmp(lhs, rhs) OP 0`, where str_cmp returns lexicographic -1/0/1 (i32).

*Source:* `src/compiler/sema_expr.cpp#L2223-L2250`

### `expr.binop.ptr-null-compare` — Pointer compared only against integer literal 0

A raw pointer may be compared (== / != / relational) with an integer literal, but the literal must be 0; comparing a pointer with any non-zero literal is an error.

```logos
ptr == 0
```

*Source:* `src/compiler/sema_expr.cpp#L2274-L2289`

### `expr.binop.unknown-operator` — Unknown binary operator is an error

A binary operator not in the recognized set is rejected as an unknown binary operator.

*Source:* `src/compiler/sema_expr.cpp#L2466-L2467`

### `expr.binop.parenthesized` — Binary operator is infix

A binary operation is written `lhs OP rhs` with OP an infix operator token.

*Source:* `src/compiler/sema_render.cpp#L121-L126`

## Arithmetic (`expr.arith`)

### `expr.arith.overflow-checks-default` — Integer +/-/* trap on overflow by default; `-C overflow-checks=off` switches to wrapping

Runtime overflow checks (trap) on integer +, -, * are ON by default. With overflow-checks explicitly turned off, +/-/* lower to plain wrapping arithmetic instead (vectorizable, branchless, matching release-mode wrapping semantics). The mode is a whole-codegen-pass setting (fixed before code generation begins), not per-expression.

*Source:* `src/compiler/mlir_gen_impl.hpp#L136-L141`, `src/compiler/mlir_gen_impl.hpp#L156`

### `expr.arith.wrapping-intrinsics-unchecked` — Explicit wrapping_add/sub/mul are always unchecked

The explicit wrapping_add / wrapping_sub / wrapping_mul intrinsic methods are always unchecked (wrapping) regardless of the overflow-checks setting that governs the plain +/-/* operators.

*Related:* `expr.arith.overflow-checks-default`

*Source:* `src/compiler/mlir_gen_impl.hpp#L136-L140`

## Comparisons (`expr.cmp`)

### `expr.cmp.no-chained-comparisons` — Chained comparisons rejected

A run of 2+ comparison operators in one expression (`a < b < c`) parses as CHAINED_CMP; sema rejects it with a diagnostic suggesting `a < b && b < c`.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L290`

### `expr.cmp.non-chainable` — Comparison operators are non-chainable

Comparison operators are non-chainable: at most one comparison per level is well-formed. A chain of 2+ comparators (e.g. `a < b < c`), in both the struct-literal-permitting (`cmp_expr`) and no-struct-literal (`cmp_expr_ns`) expression grammars, is grammatically distinguished as a dedicated `CHAINED_CMP` node — tried before the single-comparison alt — so sema can reject it with a specific diagnostic ("chained comparisons not supported; use `a < b && b < c`", B-ex-08) rather than a generic syntax error.

*Divergence:* Rust-conformant outcome (chained comparison is rejected); Logos additionally detects it grammatically (rather than only in sema) to produce a dedicated diagnostic instead of a generic parse error.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2484-L2491`, `tools/peg_gen_cpp/grammars/logos.peg#L2653-L2660`, `tools/peg_gen_cpp/grammars/logos.peg#L2424-L2431`, `tools/peg_gen_cpp/grammars/logos.peg#L2589-L2600`, `tools/peg_gen_cpp/grammars/logos.peg#L290`

### `expr.cmp.chained-comparison-forbidden` — Chained comparisons are not supported

A chained comparison such as `a < b < c` (captured as a distinct grammar node) is rejected; it must be written `a < b && b < c`.

*Source:* `src/compiler/sema_expr.cpp#L1096-L1103`

## Comparisons (`expr.comp`)

### `expr.comp.map-comprehension` — Map comprehension expression

`{ KEY : VALUE for NAME in ITER [if GUARD] }` is a map-comprehension expression: KEY, VALUE, ITER, GUARD are arbitrary exprs, NAME a single bound identifier; GUARD is optional.

```logos
{ k: v*2 for k in items if v > 0 }
```

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2942-L2945`

## Casts (`as`) (`expr.cast`)

### `expr.cast.byte-string-to-array` — Byte-string literal lowers to [u8; N] array literal

A byte-string literal `b"..."` (LIT_BYTES) carries the raw token text including the `b"…"` envelope; sema decodes escapes and lowers the literal to an `[u8; N]` array literal of the decoded byte length N.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L303`

### `expr.cast.as-chain` — as-cast chaining

`v as T` binds below unary operators and chains left-associatively, so `x as T1 as T2` folds as `(x as T1) as T2`; a bare unary_expr with zero `as`-suffixes passes through unchanged.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2521-L2529`, `tools/peg_gen_cpp/grammars/logos.peg#L2693-L2706`, `tools/peg_gen_cpp/grammars/logos.peg#L2638-L2646`, `tools/peg_gen_cpp/grammars/logos.peg#L2632`

### `expr.cast.as-keyword` — Cast syntax

A cast is written `expr as Type`.

*Source:* `src/compiler/sema_render.cpp#L135-L139`

## Ranges (`expr.range`)

### `expr.range.for-induction-widen` — Range-`for` induction variable is typed as the wider of the range endpoints

For `for i in lo..hi` / `for i in lo..=hi`, the induction variable `i` takes the wider of `lo`'s and `hi`'s integer types (so a narrower bound is not truncated relative to a wider one), and the loop-exit comparison uses an unsigned comparison when the corresponding bound's type is one of `u8/u16/u24/u32/u56/u64/u128`, signed otherwise.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L2405-L2469`

### `expr.range.inclusive-exclusive-bound` — Inclusive vs exclusive range bound in `for`

`for i in lo..hi {}` excludes `hi` (loop condition `i < hi`); `for i in lo..=hi {}` includes `hi` (loop condition `i <= hi`), with signedness of the comparison selected per `expr.range.for-induction-widen`.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L2459-L2469`

### `expr.range.family` — Range expressions

Range value-expressions: `lo..hi` (half-open), `lo..=hi` (inclusive), `lo..` (from), `..hi` (to), `..=hi` (to-inclusive), `..` (full). Grammar: `expr <- range_expr`, i.e. range is the top-level value-expression production — it binds loosest of all operators in the precedence cascade (below it: logical operators). An omitted side leaves the corresponding bound unspecified/unfilled; sema fills the open side. Sema lowers each form to a stdlib `RangeI64`/`RangeI32` struct implementing `Iterator<T>`.

```logos
let r = 0..10;
let r = 0..=10;
s[a..b]
s[..n]
s[a..]
s[..]
```

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2452-L2469`

### `expr.range.desugar-range-struct` — lo..hi / lo..=hi desugar to stdlib Range constructors

A range expression requires integer bounds. Exclusive `lo..hi` lowers to `range_i32`/`range_i64`; inclusive `lo..=hi` lowers to the generic `range_incl_of` (RangeOfIncl&lt;T&gt;), which stores the real end plus a `done` flag (avoiding an overflow-prone `hi+1` encoding at the bound type's maximum value). The bound width is i64 if either bound is wider than 32 bits or an integer literal overflows i32, else i32; both bounds are widened to that bound type. Missing stdlib constructors are an error.

*Source:* `src/compiler/sema_expr.cpp#L1327-L1404`

## Field access (`expr.field`)

### `expr.field.inline-vs-pointer-field-descent` — Chained field access descends by address for inline fields, by load for pointer fields

Descending into a struct field for chained access (`a.b.c`): a field embedded in-place (an inline aggregate, or a scalar-represented named type such as AnyVal or RelPtr) yields its own field-slot address directly, so further chained access — and mutation through `&mut self` methods — operates on the original storage; a field that is a genuine pointer is loaded first, and the loaded value becomes the address used for further descent.

*Source:* `src/compiler/mlir_gen.cpp#L790-L830`

### `expr.field.tuple-index` — Field / tuple-index access

Postfix `.field` (`FIELD_READ`) reads a named field (`new`/`null` also accepted as field names); `.N` (integer) reads the Nth tuple/tuple-struct element (`TUPLE_INDEX`); `.#(expr)` and `.#ident` read a field whose name is computed from a bound expression/antiquote variable.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2554-L2565`, `tools/peg_gen_cpp/grammars/logos.peg#L2738-L2749`, `tools/peg_gen_cpp/grammars/logos.peg#L2684-L2685`, `tools/peg_gen_cpp/grammars/logos.peg#L2678-L2679`

### `expr.field.autoderef-via-deref` — Field access auto-derefs through Deref

For receiver `r` of struct type S that has no field `f` but `S: Deref<Target=U>`, `r.f` is equivalent to `(*r).f`; the deref step repeats (bounded, up to 16 levels) until a type bearing field `f` is reached. Generalizes Box/Rc/Arc and any user Deref uniformly.

*Related:* `expr.field.ref-peel`

*Source:* `src/compiler/sema_expr.cpp#L9166-L9181`

### `expr.field.ref-peel` — Field access peels reference layers

For receiver of reference-like type, `r.f` peels extra reference layers via explicit derefs so a multiply-referenced base (`&&S`) accesses the field of the underlying struct: `r.f` for `r: &&S` ≡ `(*r).f`. One reference layer remains for the single-level field projection.

*Related:* `expr.field.autoderef-via-deref`

*Source:* `src/compiler/sema_expr.cpp#L9252-L9264`

### `expr.field.self-describing-thin-tail` — Self-describing DST tail through a thin raw pointer

For a thin raw pointer `p: *const/*mut Self` to a `#[self_describing]` struct whose last field is the unsized-slice tail, `p.tail` yields a slice `{ (p as *u8)+prefix_offset, dst_len(p) }`, where prefix_offset is the natural-aligned byte offset after all sized prefix fields and the tail length is recovered by calling the struct's `SelfDescribing::dst_len` method. Slice mutability follows the pointer's mutability.

*Divergence:* Custom-DST / self-describing model — see DIVERGENCES B2.

*Related:* `expr.field.dst-tail-slice`

*Source:* `src/compiler/sema_expr.cpp#L9185-L9248`

### `expr.field.dst-prefix-positional` — Prefix (non-tail) field access on a DstRef is positional

For a fat-pointer receiver to a custom-DST struct, a non-tail prefix field is addressed positionally: its byte offset is computed by walking the sized prefix fields (with the DstRef's type-args substituted), and the field is read by dereferencing `data_ptr + offset` typed as the field type. This works uniformly for generic and non-generic DST instances, including those with no registered monomorphized layout.

*Divergence:* Custom-DST model — see DIVERGENCES B2.

*Source:* `src/compiler/sema_expr.cpp#L9394-L9429`

### `expr.field.dataref-ergonomic-read` — DataRef&lt;T&gt; ergonomic field read

For receiver `p: DataRef<T>` where T is a zoned struct having field `f`, `p.f` is equivalent to `p.ptr().f`. The access requires an `unsafe` context.

*Note:* DataRef is a Logos-specific zone/Writ type; no direct Rust analogue.

*Source:* `src/compiler/sema_expr.cpp#L9440-L9458`

### `expr.field.not-a-struct-error` — Field read receiver must be a struct/class

A field read whose receiver does not resolve to a struct or class type is an error ('receiver is not a struct or class'), except during metaprog discovery when the receiver (or its pointee) is already of error type, in which case the error type is propagated silently.

*Source:* `src/compiler/sema_expr.cpp#L9460-L9478`

### `expr.field.union-read-unsafe` — Union field read requires unsafe

Reading a field of a union requires an enclosing `unsafe` block (only one field is active at a time). Writing to a union field is safe; the read-safety check is suppressed when the access is the LHS of an in-place write.

*Source:* `src/compiler/sema_expr.cpp#L9495-L9509`

### `expr.field.pub-access` — Private field access restricted to defining package

A non-`pub` field is accessible only within the package that defines the struct (checked via check_pub_access against the struct's package). Variadic field families (`name_<n>`) are matched by prefix for the access check.

*Related:* `module.vis.pub-field`

*Source:* `src/compiler/sema_expr.cpp#L9486-L9528`

### `expr.field.hoist-droppable-rvalue-temp` — Droppable fresh-rvalue field base is hoisted to a statement temp

When a field is read off a fresh owned rvalue base of a move (droppable) type (`make().x`), the base is hoisted into a named statement-scoped temporary so it lives to end of statement and its Drop runs at scope exit; the field is then read from that local. A place or borrow base is left untouched.

*Source:* `src/compiler/sema_expr.cpp#L9151-L9164`

### `expr.field.name-from-field-or-name-slot` — Field name resolved from FIELD then NAME slot

The accessed field name is taken from the FIELD slot; if empty (e.g. a substituted antiquotation that landed at the field-name position via NAME_VAR→NAME rewrite), it falls back to the NAME slot.

*Note:* Fallback is a metaprog-substitution artifact, not a user-facing surface rule.

*Source:* `src/compiler/sema_expr.cpp#L9147-L9150`

### `expr.field.dst-ref-unsafe` — Field read through a custom-DST fat-pointer reference requires unsafe unless self-describing

Reading any field through a fat-pointer (DstRef) receiver `&CustomDstStruct` requires an enclosing `unsafe` context, UNLESS the struct is declared `#[self_describing]` — its tail length is recovered in-band, making the borrow a complete, safe reference. Otherwise the program is rejected with: "field read through `&DstStruct` requires unsafe context (custom-DST field access is raw-pointer-shaped)".

*Divergence:* B2 — custom-DST raw-pointer-shaped field access (see DIVERGENCES.md).

*Source:* `src/compiler/sema_expr.cpp#L9275-L9281`, `src/compiler/sema_expr.cpp#L9564-L9569`

### `expr.field.dst-tail-dyn` — DST dyn-tail field projection shares the DstRef's carried vtable

For a custom-DST struct whose tail field's (generic-substituted) type is `dyn Trait`, projecting the tail field from a `&Struct` DstRef fat pointer `{data, vtable}` yields a `&dyn Trait` fat pair `{ data = base + prefix_byte_size, vtable = the receiver's OWN carried vtable }`, reusing the wide pointer's metadata verbatim — no static/independent vtable lookup for the tail. The dyn-tail prefix offset is aligned to pointer width (8 bytes) since the concrete payload alignment is not known statically.

*Divergence:* Custom-DST dyn-tail model — see DIVERGENCES B2/B3.

*Note:* Conservative 8-byte alignment for dyn tails noted as over-aligning vs Rust.

*Source:* `src/compiler/sema_expr.cpp#L9330-L9335`, `src/compiler/sema_expr.cpp#L9346-L9368`, `src/compiler/sema_expr.cpp#L9634-L9656`

### `expr.field.dst-tail-slice` — Slice-tail projection on a DstRef

For a fat-pointer receiver to a custom-DST struct whose last field `tail` has unsized-slice type `[T]`, `r.tail` yields a slice `{ data_ptr + prefix_byte_size, len }` reusing the fat pointer's len half; prefix_byte_size is the offset after all sized prefix fields, aligned to size_of(T) (capped at 8). Slice mutability follows the receiver: `(&mut Foo).tail: &mut [T]`, `(&Foo).tail: &[T]`.

*Divergence:* Custom-DST model — see DIVERGENCES B2.

*Source:* `src/compiler/sema_expr.cpp#L9296-L9345`, `src/compiler/sema_expr.cpp#L9369-L9393`, `src/compiler/sema_expr.cpp#L9657-L9681`

### `expr.field.raw-ptr-unsafe` — Field read through raw pointer requires unsafe

Reading a field through a raw-pointer receiver (`p.f` where `p: *const T`/`*mut T`) requires an enclosing `unsafe` context; otherwise it is a compile error: "field read through raw pointer requires unsafe context".

*Source:* `src/compiler/sema_expr.cpp#L9182-L9184`, `src/compiler/sema_expr.cpp#L9251`, `src/compiler/sema_expr.cpp#L9470-L9472`

### `expr.field.unknown-field-error` — Unknown field on a known struct is a compile error

Reading a field name not declared on the receiver's (resolved) struct type is a compile error: "field read: struct '&lt;S&gt;' has no field '&lt;f&gt;'".

*Source:* `src/compiler/sema_expr.cpp#L9481-L9485`, `src/compiler/sema_expr.cpp#L9814-L9818`

### `expr.field.autoderef` — Field read auto-derefs through Deref

`b.v` where b has struct/zoned-struct type lacking a field named `v` but whose type implements `Deref` is resolved by repeatedly applying one Deref step (bounded, &lt;=16 iterations) until a type with field `v` is reached: `b.v` ≡ `(*b).v`, uniformly for Box/Rc/Arc or any user Deref impl.

*Source:* `src/compiler/sema_expr.cpp#L9454-L9469`

### `expr.field.self-describing-dst-tail` — Self-describing DST tail access through a thin raw pointer

For a `#[self_describing]` struct whose last declared field has unsized-slice type `[T]`, accessing the tail field `p.tail` through a THIN `*const Self`/`*mut Self` pointer recovers the runtime element count by calling the struct's generated `<Struct>__dst_len(ptr)` function, and yields a `[T]` slice located at the field's statically-computed prefix-aligned byte offset from `p`.

*Source:* `src/compiler/sema_expr.cpp#L9473-L9538`

### `expr.field.ref-ref-autoderef` — Depth-N reference autoderef for field read

For a receiver with N&gt;1 stacked reference layers (e.g. `&&S`), field read `r.f` peels every extra reference layer via explicit deref down to a single reference before the one-level field projection: `r.f` ≡ `(*r).f` for `r: &&S`.

*Source:* `src/compiler/sema_expr.cpp#L9540-L9552`

### `expr.field.dst-prefix-offset` — DST non-tail field addressed positionally

A non-tail (prefix) field of a custom-DST struct accessed through a DstRef fat pointer is addressed positionally: its byte offset is the sum of the ABI sizes (each padded to its natural alignment, capped at 8) of all preceding declared fields, with the DstRef's carried type-arguments substituted into generic field types; the field is read by dereferencing `data+offset` typed as the field's (substituted) type.

*Divergence:* B2

*Source:* `src/compiler/sema_expr.cpp#L9682-L9717`

### `expr.field.privacy` — Private field access restricted to defining package

A struct or spec field is subject to pub-visibility: a non-`pub` field is only readable from code in the package that defines the struct/spec; access elsewhere is a compile error.

*Source:* `src/compiler/sema_expr.cpp#L9819-L9862`

### `expr.field.union-unsafe` — Union field read requires unsafe; write does not

Reading a field of a struct declared `union` requires an enclosing `unsafe` block: "field read of `<S>.<f>` requires `unsafe` block (`<S>` is a union - only one field is active at a time)". Writing a union field is exempt from this check (unions permit overwriting any field without an activeness precondition).

*Source:* `src/compiler/sema_expr.cpp#L9829-L9842`

### `expr.field.variadic-match` — Variadic field name matching

A struct field declared variadic with base name `f` additionally matches any accessed field name of the shape `f_<suffix>` (an underscore-joined suffix), resolving `x.f_<suffix>` against the single variadic field's declaration for pub-check purposes.

*Note:* This slice only shows the name-matching used for the pub-access check; the underlying variadic-field mechanism (declaration, storage, and full read/write semantics) is defined elsewhere.

*Source:* `src/compiler/sema_expr.cpp#L9843-L9848`, `src/compiler/sema_expr.cpp#L9854-L9856`

### `expr.field.non-struct-receiver-error` — Diagnostic: field read on a non-struct/class receiver

Reading a field on a receiver whose (dereferenced) type is not a struct or class is a compile error: "field read: receiver is not a struct or class (got &lt;T&gt;)"; the error is suppressed (result silently propagated as `<error>`) when metaprog discovery mode is active and the receiver's type is already `<error>` (a not-yet-derived struct in a chain).

*Source:* `src/compiler/sema_expr.cpp#L9793-L9810`

### `expr.field.dot-access` — Field access

Named field access is `receiver.field`.

*Source:* `src/compiler/sema_render.cpp#L282-L295`

## Field writes (`expr.field-write`)

### `expr.field-write.chain-auto-deref` — Chained field assignment auto-dereferences pointer segments

In a chained field assignment a.b.c...x = v, each intermediate path segment whose field type is a pointer-to-struct is dereferenced (one load) before descending, while embedded (non-pointer) struct segments are addressed in place; the final segment is the assignment target.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L2897-L2950`

## Tuple indexing (`expr.tuple-index`)

### `expr.tuple-index.auto-deref-receiver` — Tuple-index auto-derefs a reference receiver

A tuple index `t.k` where `t: &(..)`/`&mut(..)`/`*(..)` (reference/pointer whose pointee is a tuple) operates on the pointee tuple; the receiver pointer is used directly as the tuple address.

*Source:* `src/compiler/mlir_gen_expr.cpp#L3113-L3120`

### `expr.tuple-index.aggregate-element-by-address` — Tuple-index of inline-aggregate element yields its address

A tuple index `t.k` whose element type is a struct, tagged enum, slice, closure, trait object, or nested tuple yields the address of the inline element slot (the value being pointer-represented); scalar elements are loaded by value. A by-value tuple result is first materialized into storage before address computation.

*Source:* `src/compiler/mlir_gen_expr.cpp#L3128-L3160`, `src/compiler/mlir_gen_expr.cpp#L3140-L3159`

### `expr.tuple-index.access` — Tuple/tuple-struct .N indexing with auto-deref

`recv.N` indexes a tuple (auto-deref through `&`/`&mut`) returning the Nth element type, or reads field N of a tuple-struct (auto-deref through `&Foo`/`&mut Foo`) with the struct's type-params substituted by the receiver's concrete type-args. An out-of-range index, or a receiver that is neither, is an error.

*Source:* `src/compiler/sema_expr.cpp#L1653-L1714`

### `expr.tuple-index.dot-number` — Tuple index access

Tuple element access uses a numeric field after a dot: `receiver.N`.

*Source:* `src/compiler/sema_render.cpp#L297-L303`

## Indexing (`expr.index`)

### `expr.index.unsigned-index-zero-extends` — Unsigned index operand zero-extends to 64-bit

When indexing with an unsigned integer index (u8/u16/u24/u32/u56/u64/u128) narrower than 64 bits, the index is zero-extended to 64 bits before address computation.

*Source:* `src/compiler/mlir_gen_expr.cpp#L2970-L2979`

### `expr.index.ref-to-array-decays-to-element-pointer` — Indexing through a reference/pointer to an array strides by element

Indexing a value of type `&[T;N]`, `&mut [T;N]`, or `*[T;N]` uses the SSA pointer directly as the address of element 0 and strides by the element type; the array is not loaded by value.

*Source:* `src/compiler/mlir_gen_expr.cpp#L2842-L2859`

### `expr.index.ptr-to-dyn-loads-handle` — Indexing a *dyn Trait pointer loads an 8-byte dyn handle per slot

Indexing a `*const/*mut dyn Trait` (pointer whose pointee is a trait object) strides by pointer width per slot and loads the dyn handle; `p[0]` is the index form of `*p`.

*Source:* `src/compiler/mlir_gen_expr.cpp#L2872-L2881`

### `expr.index.read-write-same-slot` — Indexed read and write address identical slot

`s[i]` as an lvalue (for `&mut s[i]` or `s[i] = v`) computes the same element address as the by-value read: a slice loads the data pointer from descriptor field 0 then strides by the element slot type; an array strides from its storage; a pointer/ref variable strides from the loaded pointer value (indexing the pointee, or the pointee array's element for `*mut [T;N]`); a pointer field of fat elements loads the buffer base then strides by the 16-byte fat slot. Element stride equals the element type's place-slot type in every case so reads and writes never address different slots.

*Related:* `layout.place.element-slot-by-repr`

*Source:* `src/compiler/mlir_gen_expr.cpp#L1298-L1402`

### `expr.index.unsigned-index-extension` — Unsigned index extended to 64-bit

When an index expression has an unsigned integer type narrower than 64 bits, it is zero-extended to 64 bits before being used as a GEP index.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1315-L1325`, `src/compiler/mlir_gen_expr.cpp#L1393-L1399`

### `expr.index.unsigned-zero-extend` — Unsigned index operand is zero-extended to the index width

An index expression of an unsigned integer type (u8/u16/u24/u32/u56/u64/u128) is zero-extended to the address-index width before address computation, so e.g. u8(200) indexes element 200 rather than being sign-extended to a negative offset.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L3041-L3051`, `src/compiler/mlir_gen_stmt.cpp#L3140-L3159`

### `expr.index.read` — Index expression

`e[i]` (`INDEX_READ`) is a postfix index-read, available identically in the ordinary and no-struct-lit postfix chains (`LBRACKET expr RBRACKET => INDEX_READ`). When the index is a range expression (`s[a..b]`, `s[a..]`, `s[..b]`, `s[..]`) the result is a slice rather than a single element.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2566-L2567`, `tools/peg_gen_cpp/grammars/logos.peg#L2750-L2751`, `tools/peg_gen_cpp/grammars/logos.peg#L2690-L2691`, `tools/peg_gen_cpp/grammars/logos.peg#L2394-L2396`

### `expr.index.indexmut-place` — Mutable index place requires IndexMut, shared requires Index

For an index place `&mut a[i]` the receiver type must impl `IndexMut`; for `&a[i]` an `Index` impl suffices. The place lowers to a call of the impl's `__index_mut` / `__index` method (the unique 2-parameter candidate), returning the reference produced by that method directly (no extra deref). Trait presence is checked against both the concrete struct name and the base (generic) struct name.

*Related:* `expr.index.user-index-read`

*Source:* `src/compiler/sema_expr.cpp#L10258-L10268`, `src/compiler/sema_expr.cpp#L10300-L10305`

### `expr.index.place-real-slot` — Index-place receiver uses the real variable slot

When the index-place receiver is a plain variable, its address is taken from the real variable slot (`&mut v`), not a spilled copy, so the mutation through `IndexMut` persists. A receiver already of reference/pointer kind is passed through unchanged; other receiver shapes materialize a temporary reference.

*Source:* `src/compiler/sema_expr.cpp#L10286-L10296`

### `expr.index.user-index-read` — Index read dispatches to user Index impl as *recv.index(i)

`a[i]` for a struct `a` that impls `Index<Idx, Output>` lowers to `*(a.index(i))`: the impl's `__index` method (unique 2-param candidate) is called with a materialized `&a` receiver and the index, and the result reference is dereferenced to yield the element place. The integer-literal index is widened to the formal index parameter type. User `Index` dispatch is attempted before the built-in integer-index check, so an impl may accept non-integer keys.

*Related:* `expr.index.indexmut-place`, `expr.index.generic-index-via-method`

*Source:* `src/compiler/sema_expr.cpp#L10396-L10453`

### `expr.index.autoderef` — Autoderef at index position through Deref

A struct receiver at index position without its own `Index` impl is dereferenced through its `Deref` impl(s) until an indexable type appears, mirroring method-resolution autoderef. The walk is bounded to 4 steps. If a step yields a Slice or trait-object (fat) value, that value is taken directly as the receiver.

*Source:* `src/compiler/sema_expr.cpp#L10399-L10424`

### `expr.index.generic-index-via-method` — Generic-struct Index impl routed through method-call machinery

When a struct impls `Index` but no concrete `__index` symbol exists yet (a generic impl, e.g. `impl<T> Index for Vec<T>`), `v[i]` lowers to `*v.index(i)` via the method-call path. The element type is the impl's `Index<Idx, Output>` second trait-arg with the struct's type-args substituted for the impl's type params (matched positionally against `TypeVar`s in the impl target pattern); the index is widened to the substituted `Idx` when it is not a type variable.

*Related:* `expr.index.user-index-read`

*Source:* `src/compiler/sema_expr.cpp#L10454-L10485`

### `expr.index.integer-required` — Built-in index requires an integer index

For built-in (non-user-Index) indexing the index expression must have integer type; otherwise an `array index must be integer` error is reported.

*Source:* `src/compiler/sema_expr.cpp#L10489-L10490`

### `expr.index.receiver-kind` — Built-in index receiver must be array, slice, or pointer/reference

A built-in index `a[i]` requires the receiver to be a Slice, Array, raw Ptr, or reference (`Ref`/`MutRef`); any other receiver kind is a type error. Slice indexing lowers to a dedicated slice-index operation; an array/ref/ptr yields the element type, auto-dereferencing a single reference/pointer layer (and through a `[T;N]` array pointee) to the element.

*Source:* `src/compiler/sema_expr.cpp#L10492-L10526`

### `expr.index.raw-ptr-unsafe` — Indexing through a raw pointer requires unsafe

Indexing a value of raw-pointer kind (`*const`/`*mut`) is only permitted inside an `unsafe` context; outside one it is an error.

*Source:* `src/compiler/sema_expr.cpp#L10506-L10508`

### `expr.index.ref-to-slice-retype` — Indexing a reference-to-slice GEPs through the fat-pointer pair

When the receiver type is a reference to a slice (`Ref/MutRef -> Slice`, e.g. `&s` where `s: &[T]`), it is retyped to the pointee Slice rather than loaded, so `(&s)[i]` indexes the underlying `{data,len}` pair and yields element type `T` instead of the whole slice.

*Source:* `src/compiler/sema_expr.cpp#L10311-L10326`

### `expr.index.range-slice` — Range indexing produces a sub-slice

A range index `recv[lo..hi]`, `recv[lo..]`, `recv[..hi]`, `recv[..]`, or inclusive `recv[lo..=hi]` produces a sub-slice `&[T]` via `slice_get_range(recv, lo, hi)`. The receiver must be a slice, array (decayed to `&[T]` via addr-of + slice-coercion), or reference-to-slice; otherwise an error is reported. Missing `lo` defaults to 0; missing `hi` defaults to INT64_MAX (clamped to len); an inclusive upper bound is lowered as `hi+1`. Bounds are widened to i64. `slice_get_range` must be in scope (`use logos.lang.slice`).

*Divergence:* Range-slicing relies on stdlib `slice_get_range`; open/inclusive ends are clamped to length rather than panicking on out-of-range as Rust does.

*Source:* `src/compiler/sema_expr.cpp#L10328-L10389`

### `expr.index.bracket` — Index expression

Indexing is written `receiver[index]`.

*Source:* `src/compiler/sema_render.cpp#L305-L312`

## Slicing (`expr.slice`)

### `expr.slice.len-and-ptr-projection` — Slice length/pointer projection

A slice's length is the metadata half of its reference representation (repr_meta), UNLESS the slice's static type is a thin `#[self_describing]` DstRef, in which case the length is recovered in-band via `dst_len(header_ptr)` instead. A slice's data pointer is always the data half of its reference representation (repr_data).

*Source:* `src/compiler/mlir_gen_expr.cpp#L5303-L5324`

### `expr.slice.len-as-ptr-builtin` — Built-in Slice.len() / .as_ptr()

On a receiver of kind `Slice`, `.len()` lowers to a slice-length read of type `i64`; `.as_ptr()` lowers to a slice-data-pointer read of type `*const u8`. These are checked before any user-defined slice method.

*Source:* `src/compiler/sema_expr.cpp#L6496-L6504`

## Slice indexing (`expr.slice-index`)

### `expr.slice-index.element-projection` — Slice indexing element access/return convention

`s[i]` GEPs into the slice's data pointer by `i` (zero-extended to i64 if `i`'s type is unsigned) using the element's place-slot type, the same slot type the lvalue path (`&s[i]`, `s[i] = v`) strides by, so reads and writes address the identical element. If the element's slot type is an aggregate (LLVM struct — inline struct/tuple/tagged-enum value-repr/fat {data,meta} pair), the expression yields the element's ADDRESS; otherwise it loads and yields the element VALUE.

*Source:* `src/compiler/mlir_gen_expr.cpp#L5236-L5279`

## Dereference (`expr.deref`)

### `expr.deref.aggregate-pointer-identity` — *p on aggregate-typed pointee is a no-op reinterpret

`*p` whose result type is a struct, zoned-struct, tuple, array, or trait-object (dyn handle) yields the same pointer value (no load), since those types are pointer-represented; subsequent field/index access or by-value copy handles the byte-level move.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1794-L1812`

### `expr.deref.scalar-load` — *p default case loads the pointee's representation type

`*p` for any pointee type not matched by a pointer-identity or materialize special case loads exactly one value of the pointee's representation type from the address p.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1846-L1848`

### `expr.deref.tagged-enum-identity` — *p on a tagged enum yields the storage pointer

A tagged (payload-carrying) enum is pointer-to-inline-storage, so `*p` over a `&Enum`/`*Enum` to a tagged enum yields the same pointer (no load); a C-like (fieldless) enum instead follows the generic scalar-load rule.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1821-L1826`

### `expr.deref.fatslice-pointer-identity` — *p on a fat-slice pointee (str / &[T]) is a no-op reinterpret

`*p` where p's reference-representation kind is FatSlice (a `str`/`&[T]` pointee) yields the same pointer (no load): the slice value convention is pointer-to-{data,len}-pair storage, so the pair's address IS the dereferenced value. Restricted to FatSlice among fat kinds: a Closure value is a distinct 8-byte pointer-to-{fn,env} handle and still loads; TraitObject has its own identity rule; other fat kinds (RelOffset, FatCustomDst, FatZoneMut) remain on the load branch as unexercised.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1827-L1845`

### `expr.deref.zoned-enum-materialize` — *p over an at-rest zoned niche-enum slot materializes the value

`*p` where the operand's static type is a zoned pointer to a niche-optimizable enum materializes the by-pointer enum value from its at-rest self-relative encoding (the Ref arm is anchored to the slot address p). Both an at-rest zoned slot and a plain value-form local share the surface type `*Enum`; the operand's zoned-pointer marker is what disambiguates, and a non-zoned `*Enum` falls through to the ordinary tagged-enum/scalar rules.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1813-L1820`

### `expr.deref.user-deref-impl` — `*x` for a Deref-implementing struct calls `.deref()`

`*x` for a struct `x` implementing (possibly generic) `Deref` lowers through the generic-aware method-call machinery as `x.deref()` (`emit_generic_deref_step` in lower_deref; mirrored by `emit_generic_deref_call` in the `&*` reborrow path) — this dispatches through generic impls too, not only concrete symbols.

*Source:* `src/compiler/sema_expr.cpp#L2696-L2701`, `src/compiler/sema_expr.cpp#L2582-L2584`

### `expr.deref.non-pointer-identity` — `*x` on a non-pointer, non-Deref type is the identity

`*x` where x's type is none of Ptr/Ref/MutRef and has no generic Deref impl returns x unchanged (identity) rather than a diagnostic error.

*Divergence:* Not in docs/DIVERGENCES.md as a blessed item; Rust rejects unary `*` on a type without Deref/a pointer kind. This is a permissive relaxation admitting faithfully-ported Rust source that spells an already-loaded read as `*i` (e.g. `for i in &v` sites); soundness is preserved since it only relaxes the diagnostic, never changes which value is produced.

*Note:* The call sites that feed an already-non-pointer value into this deref (and whether other units reject it earlier) are outside this slice.

*Source:* `src/compiler/sema_expr.cpp#L2702-L2713`

### `expr.deref.raw-ptr-unsafe` — Raw-pointer deref requires `unsafe`

`*p` where `p: *T` (raw pointer) requires an enclosing unsafe context; outside unsafe it is a diagnostic error, though lowering still proceeds and returns the pointee-typed deref node.

*Source:* `src/compiler/sema_expr.cpp#L2714-L2716`

### `expr.deref.box-move-out` — `*b` for `b: Box<T>` (T non-Copy) moves T out and frees the box

`*b` where `b` is a bare-VarRef binding of type `Box<T>` and `T` is a move-type (not Copy) lowers to a call to the generic free function `box_take::<T>(b)` (the matching candidate is chosen by 1 param / 1 type-param; the type-arg is inferred from the arg via `infer_type_args` and the call emitted via `finish_generic_call`, exactly as a real `box_take::<T>(b)` call site would be, so it mangles/monomorphizes correctly) — this consumes `b`, moves the T value out of the heap block, and frees the block. A Copy element type, or any operand more complex than a bare variable, is left to the ordinary (copying) deref path instead — the caller re-lowers the whole deref on a null return.

*Source:* `src/compiler/sema_expr.cpp#L2721-L2758`

### `expr.deref.box-move-out-non-copy` — Dereferencing a bare move-typed Box local moves its contents out

`*box_var` where `box_var` is a bare local of type `Box<T>` with non-Copy `T` lowers to a move-out (`box_take`) rather than a borrowing deref. When the operand is not a bare Box-typed variable, or its element type is Copy, this special case does not apply and the caller falls through to normal deref lowering.

*Source:* `src/compiler/sema_impl.hpp#L3836-L3839`

### `expr.deref.generic-autoderef-via-method-call` — Generic-impl auto-deref lowers to a real deref() method call

Auto-deref of a receiver whose type implements Deref/DerefMut — including a generic impl (Box/Rc/Arc) whose `deref` has no concrete symbol at sema time — lowers to an actual `.deref()`/`.deref_mut()` method_call, monomorphized identically to an explicit call, and yields a place of the Deref impl's Target type (computed by substituting the impl's target pattern against the receiver's concrete type). Produces nothing when the receiver's type implements no Deref.

*Source:* `src/compiler/sema_impl.hpp#L4288-L4297`

### `expr.deref.prefix-star` — Dereference operator

Dereference is written with prefix `*`: `*expr`.

*Source:* `src/compiler/sema_render.cpp#L314-L316`

## Dereference writes (`expr.deref-write`)

### `expr.deref-write.drop-before-replace` — Deref/place write drops the old owned value before overwriting

Writing through a pointer/place to an owned droppable location runs the OLD value's destructor before the store, after the RHS has been materialized (so a self-referencing `p = f(&*p)` reads the old buffer before it is freed). Drop-before-store applies only to live owned droppable places.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L1225-L1234`

### `expr.deref-write.aggregate-by-value-copy` — Deref-write of an aggregate/fat value copies the full footprint by value

A deref/place write `*p = v` where v is an aggregate or fat value copies the full value footprint, not an 8-byte pointer: a struct/zoned-struct, tuple, embedded datatype, or fixed-array pointee is memcpy'd by size; a closure or slice value copies its 16-byte fat pair; a bare fat `dyn` (TraitObject) or slice-tailed custom-DST destination copies 16 bytes via the reference repr. An enum pointee copies its inline {disc,payload} footprint (this is how `Option::take`/`*self = None` mutate through inline storage); a C-like (discriminant-only) enum falls to a scalar store.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L1281-L1311`, `src/compiler/mlir_gen_stmt.cpp#L1312-L1366`, `src/compiler/mlir_gen_stmt.cpp#L1367-L1386`

## Address-of (`expr.addr-of`)

### `expr.addr-of.static` — `&STATIC` is the global's stable address

`&STATIC_NAME` for a module-level static (not locally shadowed) with non-Array type yields the STABLE address of the global itself ('static lifetime) — lowered as a distinguished `__static_addr:<sym>` VarRef of ref type `&T` (mut ref if the static is declared mut). This routes before the general addr-of-local path, which would otherwise materialize a fresh stack copy and break address identity. Array statics instead build a slice over that address (see coerce.unsize.array-to-slice); scalars/structs return `&T`.

*Source:* `src/compiler/sema_expr.cpp#L2515-L2526`

### `expr.addr-of.index-place` — `&container[i]` over a user Index type is the index place directly

`&container[i]` where the indexed child is INDEX_READ over a type with a user-defined Index impl lowers to that index method's place reference directly (`lower_index_place`), bypassing the generic deref/temp-materialize path.

*Source:* `src/compiler/sema_expr.cpp#L2587-L2591`

### `expr.addr-of.range-index-identity` — `&a[range]` is the identity, not an extra `&[T]` wrapper

`&a[range]` (indexing by a Range) yields the Slice-typed inner expression itself, unchanged — Logos's Slice kind already IS the borrowed `&[T]` form, so applying `&` to a range-index is identity rather than producing `&&[T]`.

*Source:* `src/compiler/sema_expr.cpp#L2595-L2602`

### `expr.addr-of.temp-materialize` — `&<rvalue>` spills a temporary to the stack

`&e` for any other rvalue `e` spills `e` to a fresh stack slot (AddrOfTemp) and returns its address, typed `&T` where T = typeof(e). If e's type is an array literal `[T; N]`, the array is spilled and the result is instead a slice literal `{addr, len=N}` typed `&[T]` (see coerce.unsize.array-to-slice) rather than `&[T; N]`.

*Source:* `src/compiler/sema_expr.cpp#L2592-L2594`, `src/compiler/sema_expr.cpp#L2619-L2621`

### `expr.addr-of.static-mut` — &mut on a module static yields the global address

`&mut STATIC` for an unshadowed module static (that is not an array) produces a `&mut T` to the global's address rather than materializing a temporary.

*Source:* `src/compiler/sema_expr.cpp#L1117-L1123`

### `expr.addr-of.mut-array-whole` — &mut arr references the whole array

`&mut arr` for `arr: [T; N]` produces `&mut [T; N]` (a reference to the whole array, sharing the array's base address); coercion to a `&mut [T]` slice parameter occurs separately at the call site.

*Source:* `src/compiler/sema_expr.cpp#L1124-L1133`

### `expr.addr-of.mut-deref-reborrow` — &mut *p reborrows through a pointer/reference

`&mut *p` where p is a Ptr/MutRef/Ref preserves an explicit AddrOfTemp(Deref(p)) shape so it is treated as a reborrow (distinct from a rebind), yielding `&mut Pointee`; for a struct with a DerefMut impl it lowers to `p.deref_mut()`.

*Source:* `src/compiler/sema_expr.cpp#L1135-L1157`

## Address-of (`expr.addrof`)

### `expr.addrof.var-place-identity` — &x yields the address of x's own storage

`&x` / `&mut x` over a local or parameter denotes the address of that binding's storage slot. A by-value binding (scalar, by-value-fat, or pointer-family) is first spilled to its own stack slot whose address is the reference; a slot-backed binding (aggregate, address-holding) hands back its existing slot address directly.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1408-L1457`

### `expr.addrof.ref-param-rebind` — &p on a reference parameter rebinds to a single shared slot

When `&p` (or `&mut p`) is taken on a parameter whose SSA arg is a value (not already `ptr`-typed) or a pointer-family parameter, the parameter's value is spilled once to a fresh entry-block alloca whose address is the reference. If `p` is a `Ref`/`MutRef` parameter, the scope binding is REBOUND to that alloca, so subsequent reads and further `&p` share one storage location (write-through for `&&mut T` chains); other by-value parameters get address-of-a-copy with the binding left untouched.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1429-L1450`

### `expr.addrof.module-const-temp` — &CONST materializes a temporary slot

Taking the address of a module-level const that has no local storage evaluates the const's initializer, materializes a fresh stack slot, stores the value, and yields that slot's address as the reference.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1412-L1425`

### `expr.addrof.mut-place-element-address` — &mut over an index/field/tuple place yields the real element address

`&[mut] <place>` over a place expression (`a[i]`, `(*p).0`, `s.f`, nested/chained mixes such as `a[i][j]`, `(*p).0`, `arr[i].field`) yields the actual element/field address computed with the correct per-element stride and layout — never the address of a by-value copy — so writes through the resulting reference reach the original aggregate. The immutable `&x.N` tuple-index path is deliberately left on the value-copy behavior (relied on elsewhere for spilled-copy semantics).

*Source:* `src/compiler/mlir_gen_expr.cpp#L1466-L1487`, `src/compiler/mlir_gen_expr.cpp#L1522-L1539`, `src/compiler/mlir_gen_expr.cpp#L1540-L1569`, `src/compiler/mlir_gen_expr.cpp#L1570-L1663`

### `expr.addrof.reborrow-pointer-identity` — &[mut] *r is identity on r

Reborrowing `&[mut] *r` where r holds a reference or raw pointer (`&T`/`&mut T`/`*T`) is equivalent to the pointer value r itself (no extra indirection): r is loaded and returned unchanged. A fat zone-mut `&mut T` reborrowed to a thin result type is peeled to its data half; reborrowed to another fat `&mut T` it keeps the full pair; reborrowing a thin pointer to a `#[self_describing]` DST yields the thin header pointer.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1494-L1520`

### `expr.addrof.temp-aggregate-spill` — & over a by-value aggregate temporary extends its lifetime via a slot

`&<temp>` where the operand is a by-value aggregate (struct, tuple, array, slice, trait-object) spills the temporary once to a stack slot only if it is not already pointer-represented (e.g. a by-value aggregate returned from a call), and that slot is the reference (temporary lifetime extension to the enclosing statement). Aggregates already held by pointer are returned unchanged.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1714-L1733`

### `expr.addrof.enum-single-level` — & over an enum is one level of indirection

A tagged (payload-carrying) enum is represented as a pointer to its inline {discriminant,payload} storage; `&enum` therefore yields that storage address directly (one indirection level, like `&struct`), never a pointer-to-pointer. A local already bound to its storage address returns it directly; a freshly constructed enum temp returns its own storage alloca; a by-value enum (e.g. a call return) is spilled once into a slot shaped like the enum's layout. A C-like (scalar-discriminant) enum is spilled to a slot whose address is the reference.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1452-L1456`, `src/compiler/mlir_gen_expr.cpp#L1734-L1751`

### `expr.addrof.enum-autoref-slot` — autoref of a slot-backed tagged-enum local returns the real slot

Autoref `(&mut o).method()` of a tagged-enum local that is bound to a genuine storage slot returns that slot's address directly, not a spilled copy of the pointer held in scope, so the callee's `*self = …` rebind (e.g. `Option::take`) is observed through the caller's binding.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1664-L1682`

### `expr.addrof.scalar-autoref-slot` — autoref of a slot-backed scalar local returns the local's own slot

Autoref `(&mut b).method()` of a scalar-primitive `let`-bound local (integer/float/bool/char/usize/isize width) that is backed by a real alloca returns the variable's own storage slot address (not a spilled copy), so a callee's mutation through `*self` reaches the caller's binding; scalar function PARAMETERS (SSA-value args, not slot-backed) keep the copy-and-spill behavior instead.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1683-L1713`

## Raw pointers (`expr.raw-ptr`)

### `expr.raw-ptr.arith-unsafe` — Raw-pointer arithmetic methods require unsafe context

On a raw-pointer (`Ptr`) receiver, the methods `byte_add, byte_sub, add, sub` (single `i64` argument, offset by byte/element) and `byte_offset_from, offset_from` (single pointer argument, yielding `i64` distance) each require the call site to be inside `unsafe`; outside unsafe, a diagnostic is raised. `byte_add/byte_sub/add/sub` require exactly 1 argument of (or widenable to) type `i64`; `byte_offset_from/offset_from` require exactly 1 argument whose type is `Ptr`.

*Source:* `src/compiler/sema_expr.cpp#L6652-L6693`

### `expr.raw-ptr.is-null-safe` — Pointer .is_null() is safe unless shadowed by a user-defined inherent method

On a `Ptr` receiver, `.is_null()` does not require unsafe context: it lowers to `(recv as i64) == 0` and takes 0 arguments. If the pointee is a `Struct`/`ZonedStruct`/`Enum` that declares an inherent `<Pointee>__is_null` function, that user-defined method is dispatched instead (resolution falls through, `nullopt`) rather than the built-in null check.

*Divergence:* Logos lets a user-defined inherent is_null on the pointee shadow the built-in raw-pointer null check.

*Source:* `src/compiler/sema_expr.cpp#L6694-L6726`

## Postfix chains (`expr.postfix`)

### `expr.postfix.chain` — Postfix operator chain

A primary expression may be followed by zero or more left-associative postfix suffixes: method call `.m(args)` (optionally `.m::<T>(args)` with explicit turbofish, or the `.new(args)`/`.null(args)` reserved-name-method spellings), expression-callee invocation `e(args)` (arbitrary receiver, e.g. `arr[i](x)`, `(get_fn())(x)`, `(|| body)(x)`), field read `.field` (also `.new`/`.null` as field names, and dynamic-name forms `.#(expr)` / `.#ident`), tuple index `.N`, indexing `[i]`, and the try operator `?`. Chains parse left-to-right via a fold accumulator (`a.b.c`, `a.f().b`).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2539-L2570`, `tools/peg_gen_cpp/grammars/logos.peg#L2722-L2754`

## Method receivers (`expr.receiver`)

### `expr.receiver.ref-autoderef-to-struct` — Pointer/reference receivers auto-deref to the pointee struct

Resolving a method/field-access receiver whose static type is `&T`, `&mut T`, `*const T`, or `*mut T` with `T` a struct or zoned-struct auto-derefs: the pointer/reference value (loaded from its storage slot first if it is a let-bound mutable-pointer local) is used directly as the struct's address, with no explicit deref required in source syntax.

*Source:* `src/compiler/mlir_gen.cpp#L699-L773`, `src/compiler/mlir_gen.cpp#L850-L861`, `src/compiler/mlir_gen.cpp#L870-L881`

## Calls (`expr.call`)

### `expr.call.qualified-path-segments` — Qualified CALL/GENERIC_CALL carries package-path segments

A qualified CALL or GENERIC_CALL node may carry QUAL_PARTS: package-path segments following RECEIVER, supporting fully-qualified dotted-package call syntax (e.g. `pkg.sub::func(...)`); call nodes never populate a plain module USES field simultaneously, so QUAL_PARTS reuses that slot.

*Divergence:* A9

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L37-L38`

### `expr.call.package-qualified` — Package-qualified free-function call

`IDENT ('.' IDENT)+ '::' IDENT (args)` — a dotted package-path prefix (RECEIVER = first segment, QUAL_PARTS = the remaining `.`-segments) followed by `::IDENT(...)` — calls the free function whose `.package` equals the joined dotted path; this disambiguates same-named free functions across packages (e.g. `logos.lang.mem::replace` vs `logos.lang.ptr::replace`). A turbofish variant `... '::' IDENT '::' <T>(args)` supplies explicit type args (GENERIC_CALL). Tried before the single-IDENT `::` call forms since the mandatory `.`-segment makes it unambiguous.

```logos
logos.lang.mem::replace(a, b)
```

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3253-L3263`

### `expr.call.turbofish-type-vs-method` — Turbofish on type vs. turbofish on method

`Type::<T>::method(args)` (STATIC_CALL, turbofish on the type's own generics) is grammatically distinct from `Type::method::<T>(args)` (STATIC_CALL, turbofish on the method's own generics, TYPE_PARAMS applied to the associated fn); the method-turbofish alternative is tried before the plain `Type::method(args)` form to resolve the ambiguity.

```logos
Vec::<i32>::new()
Vec::new::<i32>()
```

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3264-L3269`, `tools/peg_gen_cpp/grammars/logos.peg#L3280-L3287`

### `expr.call.ufcs-qualified-path` — UFCS qualified-path call resolves by concrete type only

`<Type as Trait>::method(args)` is accepted as a STATIC_CALL keyed on RECEIVER=Type and NAME=method; the `as Trait` qualifier is parsed but consumed/dropped, so dispatch resolves purely from the concrete type — the trait qualifier does not participate in overload disambiguation.

*Divergence:* In Rust, the `<Type as Trait>::method` qualifier disambiguates among multiple trait impls providing a same-named method; here it is grammar-accepted but discarded, relying solely on type-based method resolution (see also the already-blessed trait-qualified-UFCS entry in docs/DIVERGENCES.md "Trait::method(recv, ...)" dispatch-on-first-arg).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3274-L3279`

### `expr.call.name-var-callee` — Call through a name-variable callee

`# IDENT (args)` and `# (expr) (args)` call through a metaprogramming name-variable callee (CALL, NAME_VAR); the second form allows the name-variable itself to be an arbitrary parenthesized expr.

*Note:* The binding/resolution of NAME_VAR callees is implemented outside this slice; only the call-site grammar shape is evidenced here.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3290-L3297`

### `expr.call.static-new` — `::new` static-call sugar with turbofish

`IDENT::<T>::new(args)` is accepted as a STATIC_CALL with NAME bound to the `new` keyword, alongside the general `IDENT::<T>::method(args)` form; `IDENT::new(args)` (no turbofish) is likewise a STATIC_CALL alongside the general `IDENT::IDENT(args)` form.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3266-L3267`, `tools/peg_gen_cpp/grammars/logos.peg#L3288-L3289`

### `expr.call.plain` — Plain call expression

`IDENT(args)`, `new(args)`, and `null(args)` are plain CALL expressions with an optional comma-separated, optionally trailing-comma-terminated call_arg_list.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3298-L3303`

### `expr.call.overload-best-match-scoring` — Overload resolution scores exact(2) over compatible(1); ties broken by local package

Among arity-matching non-generic candidates, each is scored by its worst param match: exact (types_equal) = 2, compatible-only = 1; if any param is incompatible the candidate is rejected. The unique highest-scoring candidate wins. A score tie is ambiguous, broken by preferring the candidate whose package equals the current package (local shadows imported); an unbroken tie is an 'ambiguous call' error.

*Source:* `src/compiler/sema.cpp#L1724-L1793`

### `expr.call.divergent-never-return` — A call to a `-> !` function (or panic) is divergent

A call/macro-call node is divergent if its callee is `panic` or if any resolved candidate's return type is Never; marker-macros (unreachable!/todo!/unimplemented!) divert through panic!.

*Source:* `src/compiler/sema.cpp#L1702-L1722`

### `expr.call.callable-field` — Call of a callable struct field

If `s.m(args)` finds no method `m` but struct `s` has a field named `m` whose type is a fn-pointer/fn-value or closure, the expression is lowered as a field read followed by a fn-ptr call (fn-value kind) or closure call (closure kind), returning that callable's return type.

*Divergence:* Rust requires explicit `(s.m)(args)` to call a callable field; bare `s.m(args)` is method-only

*Source:* `src/compiler/sema_expr.cpp#L8701-L8728`

### `expr.call.tuple-struct-ctor` — Tuple-struct name called as a function builds positional-field literal

Calling a tuple-struct's name as a function, `Foo(a, b, …)`, constructs Foo as a struct literal with positional fields named "0","1",…. Arg count must equal declared field count (diagnostic otherwise). Each arg is widened (`widen_int_expr`) and compat/variance-checked (`types_compatible`, `check_variance`) against the (possibly type-substituted) declared field type. For a generic tuple struct, the struct's type-args are inferred by unifying each field's declared type against the corresponding arg's expression type (`unify_types`); a type-param left unresolved defaults to a fresh TypeVar in the literal's type.

*Source:* `src/compiler/sema_expr.cpp#L2816-L2876`

### `expr.call.callable-resolution` — Which local-name calls are closure/fn-ptr calls (not named-fn calls)

`callee(args)` where `callee` names a local binding is treated as a closure/fn-pointer call (not a resolved named-function call) when the binding's type is: Closure; any fn-value kind; `Box<Closure>` (unwrapped to the inner Closure, flagged callee_is_box_closure — the box's value is the heap pointer to the {fn_ptr,env_ptr} pair); a `Ref`/`MutRef` to a callable (unwrapped, flagged callee_is_ref_fn); or a type-parameter bounded by an Fn/FnMut/FnOnce-family bound (synthesizes a Closure type from the bound's fn_params/fn_ret, flagged is_fn_bound), including through exactly one layer of `&`/`&mut` to that bounded type-param.

*Source:* `src/compiler/sema_expr.cpp#L2878-L2960`

### `expr.call.callable-autoderef-ref` — Calling through a reference to a callable auto-derefs one layer

A callee of type `&fn(…)->R` / `&mut fn(…)` / a reference to a Closure (or `&F`/`&mut F` for an Fn-bounded type-param F) auto-derefs through exactly one reference layer to expose the callable before invoking it: the emitted call wraps the var_ref in a Deref (loading the fn-ptr/closure value out of the reference slot) ahead of the FnPtrCall/ClosureCall.

*Source:* `src/compiler/sema_expr.cpp#L2897-L2913`, `src/compiler/sema_expr.cpp#L2925-L2937`, `src/compiler/sema_expr.cpp#L3003-L3010`

### `expr.call.callable-arity-and-args` — Closure/fn-ptr call arity and per-argument checks

A closure/fn-pointer call's argument count must equal the callable type's parameter count (diagnostic error otherwise, naming the call kind: "closure call" / "fn-ptr call"); when the count matches, each argument is coerced to its parameter type (`coerce_arg_to_param`, CFLAG_MINIMAL) then compat/variance-checked (`types_compatible`, `check_variance`), each producing its own diagnostic on mismatch independent of the arity check.

*Source:* `src/compiler/sema_expr.cpp#L2961-L2988`

### `expr.call.callable-arg-move` — By-value move-type args to a closure/fn-ptr call are marked moved

In a closure/fn-pointer call `f(args)`, each by-value argument whose static type is a concrete move-type (excluding a bare TypeVar arg, whose move-ness is unknown at the generic call site, and excluding args bound to a Ref/MutRef parameter) is marked moved at its source place after lowering (`mark_moved_expr`) — ownership transfers into the callee, suppressing the caller's scope-exit drop for that source (otherwise a moved `String` would be dropped by both callee and caller).

*Source:* `src/compiler/sema_expr.cpp#L3012-L3030`

### `expr.call.arg-formal-hint-propagation` — Formal parameter types hint argument inference

When a free-function call's callee is uniquely resolvable (a generic entry, or exactly one candidate), each argument is lowered with the corresponding formal parameter type as an inference hint: a closure-literal arg adopts the formal's Fn-family signature (TypeVar formal: from its Fn-family bound; FnPtr/Closure formal: used directly), a payload-carrying enum-literal arg adopts a fully-concrete enum formal, a tuple-literal arg adopts a Tuple formal, and an array-literal arg adopts the element type of a Slice/Array formal with non-TypeVar element. Hints from generic (unresolved) formals are NOT applied.

*Note:* Hint applicability conditions inferred from the per-kind lambdas; exact resolution precedence (generic vs single-candidate) is implementation-derived.

*Source:* `src/compiler/sema_expr.cpp#L3026-L3113`

### `expr.call.closure-hint-from-fn-bound` — Closure param/return types inferred from callee Fn-family bound

For a generic free fn `fn f<F>(g: F) where F: FnOnce(A)->R`, an un-annotated closure argument infers its parameter and return types from the bound's Fn-family signature `(A)->R` (missing return → unit).

*Source:* `src/compiler/sema_expr.cpp#L3031-L3051`

### `expr.call.unsafe-context-required` — Calling an unsafe fn requires an unsafe context

A call to a function declared `unsafe` is an error unless it occurs inside an unsafe context.

*Source:* `src/compiler/sema_expr.cpp#L3217-L3218`, `src/compiler/sema_expr.cpp#L3409-L3410`

### `expr.call.pub-access-check` — Free-function call respects visibility

A free-function call checks the callee's pub/package/module-only visibility against the call site; an inaccessible callee is an error.

*Source:* `src/compiler/sema_expr.cpp#L3216`, `src/compiler/sema_expr.cpp#L3406-L3411`

### `expr.call.arity-exact` — Non-vararg call arity must match

For a non-vararg function, the argument count must equal the declared parameter count; otherwise an error 'expected N args, got M'.

*Source:* `src/compiler/sema_expr.cpp#L3242-L3244`, `src/compiler/sema_expr.cpp#L3499-L3501`

### `expr.call.arity-vararg-minimum` — Vararg call requires at least the fixed-parameter count

For a vararg function, the argument count must be &gt;= the number of declared (fixed) parameters; fewer is an error 'expected at least N args, got M'. Only the fixed parameters are type-checked against formals.

*Source:* `src/compiler/sema_expr.cpp#L3219-L3241`, `src/compiler/sema_expr.cpp#L3475-L3498`

### `expr.call.arg-variance-check` — Argument passing enforces variance

Each argument/parameter pair is variance-checked at the call site (lifetime/subtyping soundness).

*Source:* `src/compiler/sema_expr.cpp#L3234`, `src/compiler/sema_expr.cpp#L3257`, `src/compiler/sema_expr.cpp#L3514`

### `expr.call.intlit-fit-scalar` — Integer-literal argument must fit the formal's integer type

An untyped integer-literal argument coerced to an integer parameter type is an error if its value does not fit that type's range ('value V does not fit in T').

*Source:* `src/compiler/sema_expr.cpp#L3235-L3239`, `src/compiler/sema_expr.cpp#L3515-L3519`

### `expr.call.intlit-fit-aggregate` — Integer-literal elements of array/tuple args must fit narrowed element types

When an array-literal or tuple-literal argument is checked against an Array/Tuple formal, each untyped integer-literal element (recursively through nested arrays/tuples) must fit the corresponding narrowed element type; overflow is an error naming the element index.

*Source:* `src/compiler/sema_expr.cpp#L3263-L3322`, `src/compiler/sema_expr.cpp#L3520-L3579`

### `expr.call.macro-overloads-not-callable-as-fn` — fn_macro/token_macro overloads are not callable via plain call syntax

A `#[fn_macro]` or `#[token_macro]` overload of a name is invocable only via `name!(...)` syntax; plain `name(...)` call resolution excludes such overloads.

*Source:* `src/compiler/sema_expr.cpp#L3336-L3344`

### `expr.call.prelude-enum-shorthand` — Some/Ok/Err call shorthand constructs enum literals

When `Some`, `Ok`, or `Err` is not resolvable as a function, the call is treated as the corresponding `Option::Some` / `Result::Ok` / `Result::Err` enum-variant literal (honoring any enum type hint for parameter substitution). `None` is not handled here (it is a bare-ident path).

*Source:* `src/compiler/sema_expr.cpp#L3381-L3403`

### `expr.call.undefined-function-error` — Call to an undefined function is an error

A call whose callee resolves to no function (and is not a prelude enum shorthand) is an error 'call to undefined function', except in metaprog mode where it is permitted to pass through with error type.

*Source:* `src/compiler/sema_expr.cpp#L3377-L3404`

### `expr.call.unsafe-context` — Calling an unsafe function requires unsafe context

A call to a function marked `unsafe` is an error unless it occurs inside an unsafe context; this applies to both inferred and explicit-turbofish call paths.

*Source:* `src/compiler/sema_expr.cpp#L3995-L3997`

### `expr.call.arg-count` — Call argument count must match

A non-variadic call must supply exactly as many value arguments as the function has parameters; a variadic call must supply at least the fixed parameter count. Otherwise it is an error.

*Source:* `src/compiler/sema_expr.cpp#L4235-L4237`, `src/compiler/sema_expr.cpp#L4262-L4265`

### `expr.call.arg-coercions` — Implicit coercions applied per argument at a call

Each value argument is, in order, retyped if a bare payload-less enum literal, coerced closure→fn-ptr, array-ref↔slice coerced, implicitly mut-reborrowed, struct-unsize coerced (e.g. `Rc<A>`→`Rc<dyn Tr>`), and integer-widened toward the (substituted) parameter type before type checking.

*Related:* `coerce.unsize.struct-smart-ptr`

*Source:* `src/compiler/sema_expr.cpp#L4267-L4275`

### `expr.call.intlit-fits` — Integer-literal argument must fit the parameter type

An integer-literal argument (including literal elements nested in array- and tuple-literal arguments, recursively) must fit within the target integer type; a value out of range is an error.

*Source:* `src/compiler/sema_expr.cpp#L4288-L4293`, `src/compiler/sema_expr.cpp#L4294-L4353`

### `expr.call.move-by-value-args` — By-value move-type arguments are marked moved

By-value arguments of move (non-Copy) type at a call are marked moved so their scope-exit Drop does not fire on storage whose ownership transferred to the callee.

*Related:* `borrow.move.by-value-call`

*Source:* `src/compiler/sema_expr.cpp#L4358-L4363`

### `expr.call.arg-type-compatible` — Argument type must be compatible with parameter type

After argument coercions, each argument's type must be compatible with the (substituted) corresponding parameter type, or satisfy a `&T`-&gt;`dyn` reference match; an incompatible argument yields an "expected X, got Y" error. Parameters whose type is Error, TypeVar, or AssocType (and Error-typed arguments) are exempt. For non-Error, non-TypeVar, non-AssocType parameters, variance is additionally checked.

*Source:* `src/compiler/sema_expr.cpp#L3247-L3256`, `src/compiler/sema_expr.cpp#L3503-L3513`, `src/compiler/sema_expr.cpp#L4276-L4287`

### `expr.call.divergent-never-returning` — Direct call to a `-> !` function is a diverging expression

A direct call or macro-call whose resolved callee has return type `!` (Never) — including the builtins `panic`/`abort`/`exit` and any user-declared `fn foo() -> !` — is treated as diverging at that syntactic position, generalizing the historical special-cased callee-name checks.

*Source:* `src/compiler/sema_impl.hpp#L3817-L3822`

### `expr.call.turbofish-free-fn` — Free-function turbofish placement

Explicit type arguments to a free-function call use turbofish after the callee name and before the argument list: `callee::<T1, T2>(args)`.

*Source:* `src/compiler/sema_render.cpp#L172-L201`

### `expr.call.static-turbofish-before-method` — Static-call turbofish precedes method name

In an associated/static call, turbofish type arguments attach to the receiver type and precede the `::method` segment: `Recv::<T>::method(args)`.

*Divergence:* Rust places the turbofish after the method for trait/inherent fns (e.g. T::method::&lt;U&gt;); Logos surface form puts it before the method name on the type path.

*Source:* `src/compiler/sema_render.cpp#L203-L241`

## Invocations (`expr.invoke`)

### `expr.invoke.expression-callee` — IIFE / expression-as-callee call

`(expr)(args)` (INVOKE_EXPR) allows an arbitrary expression, not just a name, in callee position; RECEIVER is the callee expression, ARGS the argument list. Sema routes the call through closure-call or fn-pointer-call resolution depending on RECEIVER's type.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L299`

### `expr.invoke.callable-receiver` — Expression-as-callee (IIFE) must be callable

`(expr)(args)` invokes the receiver expression: a Closure-typed receiver lowers to a closure call, an fn-value-kind (fn-ptr) receiver to a fn-ptr call, and a TypeVar receiver bounded by an Fn/FnMut/FnOnce family bound synthesizes a closure type from that bound for arity/arg checks (without retyping the receiver). A receiver of any other type is a non-callable error.

*Source:* `src/compiler/sema_expr.cpp#L6187-L6294`

### `expr.invoke.arity-and-arg-types` — Closure/fn-ptr call arity and argument typing

A closure or fn-ptr call must supply exactly the parameter count; each argument is coerced to its parameter type and a non-error argument type incompatible with the parameter type is an error; variance is checked per argument.

*Source:* `src/compiler/sema_expr.cpp#L6221-L6243`

## Method calls (`expr.method`)

### `expr.method.dyn-vtable-dispatch` — Method call on a trait-object receiver dispatches via vtable

A method call `recv.m(..)` where `recv: dyn Trait` (or `&dyn Trait`/`&mut dyn Trait`, i.e. a reference whose pointee is a trait object) and the method has a vtable slot is dispatched dynamically through the receiver's vtable at that slot; references to a trait object load the dyn handle once before dispatch.

*Source:* `src/compiler/mlir_gen_expr.cpp#L2554-L2559`

### `expr.method.auto-ref-receiver` — Primitive/value receiver is auto-referenced for &self

When the method's `self` is `&self`/`&mut self` but the receiver is a by-value primitive (i8/i16/i32/i64, u8/u16/u32/u64, f32/f64, bool, char), the receiver value is materialized into storage and a pointer to it is passed as the self argument.

*Source:* `src/compiler/mlir_gen_expr.cpp#L2565-L2599`, `src/compiler/mlir_gen_expr.cpp#L2581-L2588`

### `expr.method.self-is-first-arg` — Receiver passed as method's first argument

A method call lowers to a call whose argument 0 is the receiver (self) and arguments 1..n are the call's explicit arguments; explicit argument i maps to callee parameter i+1.

*Source:* `src/compiler/mlir_gen_expr.cpp#L2683-L2702`

### `expr.method.receiver-multiref-autoderef` — Method receiver peels surplus reference layers

For a method call `r.m(...)`, if the receiver type is a (non-raw) reference-like type whose pointee is itself reference-like (`&&T`, `&&mut T`, etc.), the extra reference layers are removed by explicit derefs until a single reference layer remains: `r.m()` for `r:&&T` ≡ `(*r).m()`. Raw pointers (`*const`/`*mut`) are not peeled here.

*Source:* `src/compiler/sema_expr.cpp#L7124-L7130`

### `expr.method.vec-get-move-out-rejected` — Vec::get of a non-Copy element is rejected

`v.get(i)` on a receiver resolving (through one reference layer) to `Vec<E>` where `E` is a non-Copy (move) type is an error: it would move an element out of borrowed Vec storage, aliasing and double-freeing on drop. The fix is `.borrow(i)` for `&E`, or `.remove(..)`/`.pop()` to take ownership. Copy elements are permitted.

*Source:* `src/compiler/sema_expr.cpp#L7139-L7160`

### `expr.method.deref-autoderef-resolution` — Method resolution autoderefs through Deref/DerefMut

If the receiver is a struct with no direct method named `m` (no candidate keyed by concrete or base struct name), and the struct implements `Deref<Target>`, the receiver is dereferenced to `Target` and resolution retries; iterated up to a fixed bound (16). A method defined on the outer type always wins over a Deref-target method.

*Source:* `src/compiler/sema_expr.cpp#L7203-L7238`

### `expr.method.deref-step-prefers-mut` — Per-step DerefMut chosen when target method needs &mut self

At each autoderef step, if the Deref target has a candidate method `m` whose first parameter is `&mut Self` and the receiver type implements DerefMut, the mutable DerefMut step is taken so the resulting receiver is a mutable place (`&mut Target`) rather than the shared `&Target` an immutable Deref would yield. Falls back to Deref when no DerefMut impl exists.

*Source:* `src/compiler/sema_expr.cpp#L7170-L7202`, `src/compiler/sema_expr.cpp#L7234-L7237`

### `expr.method.turbofish-bypasses-inference` — Method-level turbofish supplies explicit type args

A method call may carry an explicit turbofish `recv.m::<T1,T2>(args)`; the supplied type arguments become the method's type parameters and downstream per-arg type-param inference from argument types is bypassed.

*Source:* `src/compiler/sema_expr.cpp#L7241-L7265`, `src/compiler/sema_expr.cpp#L7504-L7510`

### `expr.method.raw-ptr-call-requires-unsafe` — Method call through a raw pointer requires unsafe

Dispatching a method when the receiver type is a raw pointer (`*const`/`*mut`), including `*mut dyn Trait`/`*const dyn Trait`, requires an `unsafe` context; outside `unsafe` it is an error. The raw pointer is peeled to its pointee for dispatch.

*Source:* `src/compiler/sema_expr.cpp#L7301-L7312`, `src/compiler/sema_expr.cpp#L7324-L7327`

### `expr.method.unsafe-method-requires-unsafe` — Calling an unsafe trait method requires unsafe

Calling a trait method declared `unsafe` outside an `unsafe` context is an error.

*Source:* `src/compiler/sema_expr.cpp#L7487-L7490`

### `expr.method.arity-check` — Method call argument count must match

A method call must supply exactly `param_count - 1` explicit arguments (excluding the implicit `self` receiver); for a zero-parameter signature the expected count is 0. A mismatch between supplied and expected explicit argument counts is an error ('expected N args, got M').

*Source:* `src/compiler/sema_expr.cpp#L7492-L7497`, `src/compiler/sema_expr.cpp#L8867-L8871`

### `expr.method.unsafe-context` — Calling an unsafe method requires an unsafe context

A method-call expression `r.m(..)` whose resolved method is declared `unsafe` is a compile error unless it occurs inside an unsafe context (`unsafe { .. }` block or unsafe fn).

*Source:* `src/compiler/sema_expr.cpp#L8259-L8261`

### `expr.method.autoref-ladder` — Method receiver auto-ref ladder

When resolving `r.m(args)`, candidate receiver types are tried in order: the receiver type T as-is, then `&T`, then `&mut T` (and for primitive/raw receivers also `*const T`, `*mut T`). The first signature-matching method wins; if matched against an autoref'd variant, the receiver is wrapped with the corresponding `&`/`&mut` address-of before the call.

*Related:* `expr.method.autoderef-lowest-priority`, `expr.method.auto-ref-self`

*Source:* `src/compiler/sema_expr.cpp#L8137-L8154`, `src/compiler/sema_expr.cpp#L8386-L8420`, `src/compiler/sema_expr.cpp#L8503-L8520`

### `expr.method.auto-ref-self` — Auto-reference/auto-address receiver for &Self / &mut Self / *Self methods

If the resolved method's first formal parameter is `&Self`/`&mut Self` (or `*const Self`/`*mut Self`) and the receiver is a non-reference, non-pointer value, the receiver is automatically taken by reference (resp. raw address-of) with the matching mutability before the call.

*Related:* `expr.method.autoref-ladder`

*Source:* `src/compiler/sema_expr.cpp#L8283-L8294`, `src/compiler/sema_expr.cpp#L8303-L8324`, `src/compiler/sema_expr.cpp#L8581-L8589`

### `expr.method.autoderef-lowest-priority` — By-value-self via auto-deref is lowest dispatch priority

A method whose `self` is by value, reachable only by auto-dereferencing a `&T`/`&mut T`/`*T` receiver, is selected only if no exact or auto-ref candidate at the current deref level matches. When chosen, the receiver is auto-dereferenced (copying/moving the pointee out, subject to downstream Copy/move borrow checks).

*Divergence:* Mirrors Rust autoderef order: try T/&T/&mut T at a deref level before stepping deeper.

*Related:* `expr.method.autoref-ladder`

*Source:* `src/compiler/sema_expr.cpp#L8484-L8491`, `src/compiler/sema_expr.cpp#L8524-L8557`, `src/compiler/sema_expr.cpp#L8563-L8580`

### `expr.method.mut-ref-to-shared-demotion` — &mut T receiver may call a &self method

A `&mut T` receiver may dispatch to a method declared on `&T` (shared self): for resolution the `&mut T` is coerced to `&T` (same pointee, weaker mutability); the receiver value is reused unchanged since `&mut`/`&` share ABI.

*Source:* `src/compiler/sema_expr.cpp#L8231-L8245`

### `expr.method.ref-impl-target` — Dispatch to impls declared on reference receiver types

An `impl Trait for &T` (or `&mut T`) provides methods reachable by a `&T`/`&mut T` receiver; these are preferred over auto-deref to T. For a struct pointee both the concrete-arg form and the base form are tried; for a non-struct pointee the impl target is keyed by the full receiver type string.

*Source:* `src/compiler/sema_expr.cpp#L8156-L8161`, `src/compiler/sema_expr.cpp#L8358-L8379`, `src/compiler/sema_expr.cpp#L8396-L8409`

### `expr.method.ref-blanket-impl` — Generic reference blanket impl dispatch

An `impl<T> Trait for &T` is reachable from a reference receiver `&U`: T is bound to the pointee U, the receiver is auto-referenced, and the call is monomorphized with T=U.

*Related:* `expr.method.ref-impl-target`

*Source:* `src/compiler/sema_expr.cpp#L8156-L8177`

### `expr.method.str-slice-alias` — str method lookup aliases &[u8]

When a receiver's type renders as `&[u8]` (the representation of `str`) and no method is found under that name, methods registered under `str__<method>` are tried as a fallback.

*Note:* str is modeled as Slice&lt;u8&gt;/&[u8]; alias is a representation detail surfaced as a resolution rule.

*Source:* `src/compiler/sema_expr.cpp#L8186-L8195`

### `expr.method.turbofish-method-args` — Method turbofish supplies type args verbatim, else inferred

For a generic method `r.m::<A,..>(args)`, the explicit turbofish type arguments are used verbatim (positionally); missing trailing args are errors/placeholders. With no turbofish, method-level type args are inferred from arguments with seed `Self = typeof(recv)`; failure to infer is a compile error.

*Source:* `src/compiler/sema_expr.cpp#L8265-L8282`

### `expr.method.generic-struct-base-fallback` — Generic-struct methods resolvable under the base type name

For a receiver of a monomorphized generic struct type (e.g. `Foo$G1$i32`), if no method is found under the concrete name, methods registered under the base struct name (`Foo`) are tried, with the struct's type parameters substituted from the receiver's type arguments.

*Source:* `src/compiler/sema_expr.cpp#L8460-L8478`, `src/compiler/sema_expr.cpp#L8591-L8651`

### `expr.method.ref-impl-typeparam-subst` — Reference-impl method binds pointee type args

When dispatching through a reference impl on a generic struct (`impl<T> Foo for &Pair<T>`), the impl/struct type parameters are bound from the pointee's type arguments; non-generic returns are substituted, generic methods are monomorphized with the derived args.

*Related:* `expr.method.ref-impl-target`

*Source:* `src/compiler/sema_expr.cpp#L8421-L8453`

### `expr.method.blanket-on-primitive` — Value blanket impls dispatch on primitive receivers

A value blanket impl (`impl<T> Trait for T`) is reachable on a primitive receiver (enabling From→Into, TryFrom→TryInto, identity Borrow, etc.) before the not-a-struct error is reported.

*Source:* `src/compiler/sema_expr.cpp#L8330-L8335`

### `expr.method.not-a-struct-error` — Method on non-struct receiver with no resolution is an error

If no method resolves for a primitive/non-struct receiver, it is a compile error 'receiver is not a struct'. Exception: in metaprog mode, an `<error>`-typed receiver (or `&`/`*` to an `<error>` pointee) silently propagates `<error>` without diagnostic.

*Source:* `src/compiler/sema_expr.cpp#L8336-L8349`

### `expr.method.no-method-error` — No method on receiver type

If no method, blanket-impl, multi-trait collision, or callable field matches `s.m`, the call is an error "'S' has no method 'm'".

*Source:* `src/compiler/sema_expr.cpp#L8729-L8730`

### `expr.method.pub-access-check` — Method visibility enforced at call site

A resolved method call is subject to the method's pub/module-only visibility; calling a non-visible method from outside its allowed scope is an error.

*Source:* `src/compiler/sema_expr.cpp#L8734`

### `expr.method.unsafe-required` — Unsafe method requires unsafe context

Calling a method marked `unsafe` outside an `unsafe` context is an error.

*Source:* `src/compiler/sema_expr.cpp#L8735-L8736`

### `expr.method.raw-ptr-recv-unsafe` — Method call through raw pointer requires unsafe

Calling a method on a receiver of raw-pointer type requires an `unsafe` context; otherwise it is an error. The raw pointer is auto-dereferenced to its pointee for method resolution.

*Source:* `src/compiler/sema_expr.cpp#L8743-L8746`

### `expr.method.arg-type-compat` — Method argument type compatibility

After coercion, each method argument type must be compatible with its substituted parameter type; an incompatibility is an error.

*Source:* `src/compiler/sema_expr.cpp#L8888-L8896`

### `expr.method.intlit-fits` — Integer-literal argument range check

An integer-literal argument (including elements of array/tuple literals, recursively) must fit in the target integer parameter type; an out-of-range literal is an error.

*Source:* `src/compiler/sema_expr.cpp#L8897-L8961`

### `expr.method.tuple-sentinel-dispatch` — Tuple receiver method dispatch via sentinel key

A method call `recv.method(args)` whose receiver type is a tuple `(T1,...,Tn)` (or `&`/`&mut` thereof) resolves the callee by probing a synthesized sentinel function name, in order: (1) the concrete-element key `$tuple$<n>$<T1>$<T2>...__<method>` and (2) the arity-only blanket key `$tuple$<n>__<method>`. This enables `impl Trait for (A,B,...)` (concrete) and blanket tuple-trait impls to provide methods on tuple values.

*Source:* `src/compiler/sema_expr.cpp#L7082-L7095`

### `expr.method.tuple-receiver-shape-match` — Tuple method receiver shape trial (Self / &Self / &mut Self)

For each tuple-method sentinel key, resolution tries the receiver in three shapes in order — by-value `Self`, `&Self`, `&mut Self` — matching the first whose full parameter signature (receiver shape + argument types) is registered; the receiver expression is then coerced (materialize-ref or deref, as needed) to the matched formal receiver shape.

*Source:* `src/compiler/sema_expr.cpp#L7100-L7133`

### `expr.method.tuple-generic-fallback` — Generic tuple method fallback + type-param substitution

If no concrete overload is registered under a tuple-method sentinel key, a generic function registered under the same key is used, substituting the generic method's type-params 1..n with the tuple's element types in order (any type-param beyond the tuple's arity substitutes to the error type).

*Source:* `src/compiler/sema_expr.cpp#L7112-L7114`, `src/compiler/sema_expr.cpp#L7138-L7150`

### `expr.method.autoderef-doubleref-peel` — Depth-N `&&T` receiver autoderef before method resolution

Before method resolution, a receiver whose type is reference-like with a reference-like pointee (e.g. `&&T`) has its extra outer reference layers peeled via explicit deref, one layer per iteration, until a single reference (or non-reference) type remains: `r.m()` for `r: &&T` is equivalent to `(*r).m()`. Raw pointers are excluded from this peeling (no binding-mode role).

*Source:* `src/compiler/sema_expr.cpp#L7162-L7173`

### `expr.method.vec-get-move-reject` — Vec::get rejects by-value read of a move element

`v.get(i)` on `Vec<T>` (or `&Vec<T>` / `&mut Vec<T>`) is rejected when `T` is a move (non-Copy) type: returning the element by value out of a shared `&self` read would alias the Vec's still-owned storage, so both the returned binding's drop and the Vec's element drop would free the same buffer (double-free). Copy element types are unaffected. Diagnostic suggests `.borrow(i)` or `.remove(..)`/`.pop()`.

*Source:* `src/compiler/sema_expr.cpp#L7175-L7203`

### `expr.method.deref-chain-autoderef` — Method resolution autoderef through user Deref impl

Method resolution on a struct receiver with no direct method (`<ConcreteName>__<method>` nor `<BaseName>__<method>`) falls back to the receiver type's `Deref` impl: the receiver is stepped through one Deref application and the direct-method probe retried, bounded to at most 16 iterations. A method defined directly on the outer type always wins over a Deref-target method.

*Source:* `src/compiler/sema_expr.cpp#L7205-L7211`, `src/compiler/sema_expr.cpp#L7246-L7281`

### `expr.method.derefmut-step-selection` — DerefMut-aware step selection during deref-chain method resolution

During deref-chain method resolution, before committing a step the resolver peeks the Deref target type: if it exposes a candidate method of the wanted name whose first formal parameter is `&mut Self`, the step is taken via `DerefMut` (not plain `Deref`) so the resulting receiver is a mutable place, preventing a mutation-through-shared-borrow unsoundness; if the receiver type has no `DerefMut` impl the step transparently falls back to `Deref`.

*Source:* `src/compiler/sema_expr.cpp#L7213-L7245`, `src/compiler/sema_expr.cpp#L7277-L7280`

### `expr.method.turbofish-type-args` — Explicit method-level turbofish type arguments

A method call may supply explicit type arguments via turbofish: `recv.method::<T1,T2,...>(args)`. When present (`user_type_args` non-empty), these are used as the method's type-param substitution and downstream type-param inference from argument types is bypassed for that call.

*Source:* `src/compiler/sema_expr.cpp#L7284-L7308`

### `expr.method.dispatch-order` — Method-call resolution stage order

After receiver autoderef and turbofish parsing, `recv.method(args)` resolution is attempted, in order, against: (1) schema construct/bind methods, (2) user tuple-impl methods, (3) slice built-ins, (4) fixed-array `.len()` built-in, (5) DstRef-typed impl methods, (6) raw-pointer arithmetic built-ins, (7) `*mut`/`*const dyn Trait` (peeled to `&dyn Trait` dispatch), (8) `&dyn Trait` vtable dispatch, (9) tagged-union tier-1 dispatch, (10) bounded-TypeVar / AssocType-projection trait-bound dispatch. The first matching stage wins.

*Source:* `src/compiler/sema_expr.cpp#L7310-L7414`

### `expr.method.ptr-to-dyn-deref-dispatch` — Method call through raw pointer to trait object

A method call through a raw pointer to a trait object (`*mut dyn Trait` / `*const dyn Trait`) requires an enclosing `unsafe` context; the pointer is retyped to its pointee `TraitObject` and dispatched through the same vtable-call path used for `&dyn Trait`.

*Source:* `src/compiler/sema_expr.cpp#L7348-L7361`

### `expr.method.array-len-builtin` — Fixed-array `.len()` is a compile-time built-in

`a.len()` where `a` has raw fixed-size array type `[T; N]` is a built-in: it lowers directly to the compile-time constant `N` as an `i64` literal; no runtime call is emitted.

*Divergence:* Return type is `i64` (Logos stdlib uses i64 for lengths throughout), not `usize` as in Rust's `[T; N]::len() -> usize`.

*Source:* `src/compiler/sema_expr.cpp#L7323-L7331`, `src/compiler/sema_expr.cpp#L7280-L7284`

## Method calls (`expr.method-call`)

### `expr.method-call.autoref-receiver` — Implicit receiver auto-ref for by-reference self parameters

If the resolved method's self (first) formal parameter type is Ref or MutRef, and the receiver expression's own type is not already reference-like and not Ptr, the receiver is implicitly wrapped in an address-of expression of matching mutability before the call is constructed.

*Source:* `src/compiler/sema_expr.cpp#L7743-L7758`, `src/compiler/sema_expr.cpp#L8108-L8117`

### `expr.method-call.autoref-receiver-ptr` — Implicit receiver auto-ref to raw pointer for pointer self parameters

If the resolved method's self (first) formal parameter type is Ptr, and the receiver expression's own type is neither reference-like nor already Ptr, the receiver is implicitly wrapped into a pointer (of matching mutability) before the call is constructed.

*Note:* Only observed on the generic-enum method-dispatch path within this slice; scope of self-by-raw-pointer methods (declaration site) is not shown here.

*Source:* `src/compiler/sema_expr.cpp#L8118-L8126`

### `expr.method-call.closure-arg-hint` — Contextual closure-argument typing from resolved method formal

When lowering a method call argument at a position whose resolved (receiver-substituted) formal parameter type is a function/closure kind, that formal type is used as a contextual hint for an untyped closure argument literal at that position; if the formal is instead an unresolved type-variable carrying an Fn-family trait bound, a closure-shape hint is synthesized from the bound's function signature so the closure's parameter types can be inferred.

*Source:* `src/compiler/sema_expr.cpp#L7985-L7996`, `src/compiler/sema_expr.cpp#L8000-L8026`

### `expr.method-call.unsafe-requires-unsafe-context` — Unsafe generic-enum method call requires unsafe context

Calling a method resolved against a generic enum receiver (e.g. `Option<T>`) that is declared unsafe is a compile error ("call to unsafe method '{name}' requires unsafe context") unless the call site is within an unsafe context.

*Source:* `src/compiler/sema_expr.cpp#L8072-L8075`

### `expr.method-call.unsafe-requires-context` — Calling an unsafe method requires an unsafe context

A method-call resolved to a function-info marked `is_unsafe` is rejected unless the call site is lexically inside an `unsafe` block/context.

*Source:* `src/compiler/sema_expr.cpp#L8306-L8308`

### `expr.method-call.autoref-self-param` — Method call auto-refs the receiver when the resolved method expects `&Self`/`&mut Self`

If the resolved method's formal parameter 0 is `&Self`/`&mut Self` and the receiver expression's type is not already ref-like or a raw pointer, the receiver is implicitly wrapped in `&`/`&mut` (materialized as an address-of-temp) to match, matching the formal's mutability.

*Source:* `src/compiler/sema_expr.cpp#L8330-L8342`, `src/compiler/sema_expr.cpp#L8350-L8361`, `src/compiler/sema_expr.cpp#L8628-L8636`

### `expr.method-call.autoref-ptr-self-param` — Method call auto-addresses the receiver when the resolved method expects `*const Self`/`*mut Self`

If the resolved method's formal parameter 0 is a raw pointer type (`*const Self`/`*mut Self`) and the receiver's type is neither ref-like nor already a pointer, the receiver is implicitly wrapped in the matching raw-pointer form.

*Source:* `src/compiler/sema_expr.cpp#L8362-L8370`

### `expr.method-call.non-struct-receiver-diagnostic` — Method call on a non-struct receiver with no resolvable method is a diagnostic

If a method-call receiver is a primitive/ref/pointer type and no method-info is found through any lookup path (direct, ref-mangled, generic, deref, blanket), sema reports "method call: receiver is not a struct (got &lt;type&gt;)" — suppressed only when in metaprog-discovery mode and the receiver (or, for ref/ptr receivers, its pointee) is already the error type, to avoid cascading diagnostics.

*Source:* `src/compiler/sema_expr.cpp#L8383-L8396`

### `expr.method-call.pub-access-check` — Resolved method visibility is checked at the call site

Once a method `fi` is resolved, its `pub`/package/module-only visibility is enforced against the calling context via `check_pub_access`, regardless of which resolution path (direct, base-name fallback, blanket impl) produced `fi`.

*Source:* `src/compiler/sema_expr.cpp#L8780-L8781`

### `expr.method-call.unsafe-method-requires-unsafe` — Calling an unsafe method requires an unsafe context

If the resolved method is `unsafe` and the call site is not inside an `unsafe` block/fn, it is an error: `call to unsafe method '{}' requires unsafe context`.

*Source:* `src/compiler/sema_expr.cpp#L8782-L8783`

### `expr.method-call.raw-ptr-requires-unsafe` — Method call through a raw-pointer receiver requires unsafe

If the receiver's static type is `Ptr`, calling any method through it requires an enclosing `unsafe` context (`method call through raw pointer requires unsafe context`); the pointee type is then used in place of the pointer type for further struct/type-arg resolution.

*Source:* `src/compiler/sema_expr.cpp#L8789-L8796`

### `expr.method-call.arg-count-check` — Explicit method-call argument count must match the method's declared arity

The number of explicit call arguments must equal `fi.param_types.size() - 1` (excluding the implicit `self` slot); a mismatch is an error `method call '{}': expected {} args, got {}` and the per-argument checks below are skipped.

*Source:* `src/compiler/sema_expr.cpp#L8914-L8919`

### `expr.method-call.autoref-value-receiver` — By-value receiver auto-referenced when the method expects &Self/&mut Self

If the resolved method's (substituted) first formal type is ref-like and the actual receiver expression's static type is a by-value, non-ref, non-`Ptr` type, the receiver is auto-referenced (mutability taken from the formal) before the call is built — covering method-chain temporaries such as `iter_over_slice(&v).find(p)`. On the plain (non-`finish_generic_call`) path this auto-ref is applied only when the method has a genuine method-level type param, so struct-only-generic methods with a separate downstream auto-ref path (e.g. `Arc::deref_mut`) are left alone, avoiding a caller-package mono re-emit that would expose the callee's private fields.

*Source:* `src/compiler/sema_expr.cpp#L9068-L9084`, `src/compiler/sema_expr.cpp#L9164-L9178`

### `expr.method-call.lowering-static-dispatch` — Non-generic-route method calls lower to a statically-dispatched EMethodCall

On the plain (non-`finish_generic_call`) path, a resolved method call lowers to an `EMethodCall` node carrying the receiver, method name, `resolved_symbol` (the method's `symbol_name` if set, else the mangled name), inferred `type_args`, coerced args, and the (struct/enum + lifetime substituted) return type; `vtable_index` is set to `-1`, marking it as a statically resolved (non-virtual) call.

*Source:* `src/compiler/sema_expr.cpp#L9179-L9190`

### `expr.method-call.turbofish-after-name` — Method-call turbofish placement

A method call is `receiver.method(args)`; explicit type arguments are turbofish placed after the method name: `receiver.method::<T>(args)`.

*Source:* `src/compiler/sema_render.cpp#L243-L280`

## Method dispatch (`expr.method-dispatch`)

### `expr.method-dispatch.generic-base-name-fallback` — Generic-struct method lookup falls back to base type name

If method resolution on a struct instantiation name `Sname` (e.g. `Foo$G1$i32`) finds no candidate mangled `Sname__method`, and `Sname` contains `$`, the base name `Base` (`Foo`) is derived and `Base__method` candidates are searched: receiver (arg0) compatibility is checked (identical type, or reachable via one auto-ref/auto-ptr-deref step, or matching pointee types for pointer receivers), remaining args checked via `arg_compatible_for_dispatch`; if no exact candidate matches, `find_generic_func_for_args`/`find_generic_func` are tried on `Base__method`.

*Source:* `src/compiler/sema_expr.cpp#L8639-L8710`

### `expr.method-dispatch.receiver-autoref-adapt` — Base-name-fallback receiver adapted by auto-ref/auto-ptr

In the base-name fallback match, a receiver whose formal is `&T`/`&mut T` and whose actual is `T` by value is accepted by auto-referencing the receiver (mutability taken from the formal); a `T` actual against a `*T`/`*mut T` formal is likewise accepted by auto-referencing; a `*T`/`*mut T` actual against a `*T`/`*mut T` formal is accepted directly whenever the pointees are equal, without an added conversion. When a match is selected this way, the receiver expression is materialized into a real reference before the call is built.

*Source:* `src/compiler/sema_expr.cpp#L8654-L8682`, `src/compiler/sema_expr.cpp#L8713-L8721`

### `expr.method-dispatch.callable-field-call` — Call syntax on a callable struct field with no matching method

If `recv.method_name(args)` matches no method (including blanket impls) but the receiver's struct type has a field named `method_name` whose type is a fn-pointer-kind or `Closure`, the call is lowered as a field-read of that field followed by an `fn_ptr_call` (fn-pointer field) or `closure_call` (closure field) with the field's closure return type, rather than reporting a missing-method error.

*Divergence:* Rust method-call syntax `recv.f(args)` never falls back to a callable field of the same name (E0599 even when a field `f: fn(..)`/`impl Fn` exists; caller must write `(recv.f)(args)`). Logos accepts the field-call form directly.

*Source:* `src/compiler/sema_expr.cpp#L8748-L8775`

### `expr.method-dispatch.no-method-diagnostic` — No-method error when method/blanket-impl/callable-field all fail

If direct lookup, generic base-name fallback, blanket-impl dispatch, and the callable-field fallback all fail to resolve `recv.method_name(args)`, sema reports `method call: '{}' has no method '{}'` and synthesizes an error-typed `method_call` node so downstream passes see a well-formed (error) expression.

*Source:* `src/compiler/sema_expr.cpp#L8776-L8777`

## Assignment (`expr.assign`)

### `expr.assign.compound-op-set` — Compound assignment operators

The compound-assignment operators are `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`. A compound-assign statement is `place OP value ;` where `place` is an atom (postfix-chained lvalue) and `value` is a full `expr`.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2324-L2327`

### `expr.assign.deref-write` — Dereference write statement

`* p = v ;` writes value `v` through dereferenced place `p` (a `unary_expr`). `* p OP v ;` performs compound assignment through a bare dereference and is defined to lower to `*p = *p OP v`.

*Divergence:* Logos addition: distinct DEREF_WRITE/DEREF_COMPOUND statement forms; semantics match Rust place-expression assignment.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2335-L2340`

### `expr.assign.place-only` — Assignment LHS must be an assignable place

The left side of a compound place assignment must be a genuine lvalue shape: an index `a[i]`, field access `a.f`, tuple index `a.N`, or dereference `*p`. Any other LHS (call result, literal, arithmetic) is rejected with 'invalid assignment target: left side is not an assignable place'.

*Source:* `src/compiler/sema_stmt.cpp#L7350-L7361`, `src/compiler/sema_stmt.cpp#L6908-L6919`

### `expr.assign.index-mut-desugar` — Indexed assignment uses IndexMut

For a type implementing `IndexMut`, `a[i] = v` desugars to a store through the trait's produced reference (`*<Type>__index_mut(&mut a, i)` or `*a.index_mut(i)`, index widened to the trait's index-type parameter); the receiver `a` must be a mutable binding, else 'index write to immutable struct' is diagnosed.

*Source:* `src/compiler/sema_stmt.cpp#L7130-L7187`, `src/compiler/sema_stmt.cpp#L7140-L7141`, `src/compiler/sema_stmt.cpp#L7364-L7373`

### `expr.assign.place-nesting-bound` — Deeply-nested assignment targets rejected

A place-write target is accepted only for shapes the address-of machinery can lower: a bare variable or `*p` bottoming out a recursion, INDEX_READ recursing to arbitrary depth over its receiver, and FIELD_READ/TUPLE_INDEX bounded to a receiver that is itself var/deref, a field chain over one, or an index into a supported place. Deeper/other nestings are rejected with 'assignment target too deeply nested to assign in place yet' (suggesting an intermediate `&mut` binding) rather than mis-lowered.

*Divergence:* Compiler-side lowering limitation: Rust places arbitrary-depth field/index/tuple-index nesting; this compiler's general place-write path currently accepts only the bounded shapes above, erroring (with a workaround) on deeper nestings rather than treating the program as ill-formed.

*Note:* The exact accepted shape set is defined by place_write_supported/place_field_base_ok recursion; bound is an implementation limitation, not a language-design boundary.

*Source:* `src/compiler/sema_stmt.cpp#L6927-L6964`, `src/compiler/sema_stmt.cpp#L7455-L7463`

### `expr.assign.dataref-field-unsafe` — DataRef&lt;ZonedStruct&gt; field write desugars via mut_ptr and needs unsafe

`p.field = v` where `p: DataRef<Z>` with `Z` a zoned struct desugars to `{ let t = p.mut_ptr(); (*t).field = v; }` (the DerefMut analog); it requires an `unsafe` context, `p` must be a mutable binding, and `v` must be type-compatible with the field type.

*Divergence:* Logos-specific: DataRef&lt;T&gt; is a zoned-memory smart pointer with no direct Rust counterpart; unlike Rust's DerefMut (auto-deref without an unsafe requirement), this ergonomic field-write path mandates an enclosing `unsafe` block.

*Source:* `src/compiler/sema_stmt.cpp#L7194-L7235`

### `expr.assign.drop-before-replace` — Field assignment drops old value first

Assigning to a field place over an owned local root drops the place's prior value before the store, provided the value is live (root owned, definitely-initialized, no overlapping moved-out path) and droppable; assigning to a path also lifts drop-suppression for the covered (equal-or-deeper) moved paths so the scope-end drop releases the new value.

*Divergence:* Rust-conformant (expr.assign.drop-target / B8)

*Source:* `src/compiler/sema_stmt.cpp#L7386-L7436`, `src/compiler/sema_stmt.cpp#L7592-L7604`

### `expr.assign.type-mismatch` — Assignment value must match place type

The assigned value's type must be compatible with the place's type (modulo `#[rel_ptr]`↔`*T` relations); otherwise a type-mismatch error is raised. Before the store the value is integer-widened to the place type, and the place type hints enum/struct literal RHS resolution.

*Source:* `src/compiler/sema_stmt.cpp#L7493-L7515`

### `expr.assign.union-field-safe` — Writing a union field is safe

Writing to a union field is safe (no `unsafe` required for the write): the place-write LHS sets `in_place_write_lhs_`, suppressing the union unsafe gate that otherwise applies when reading a union field.

*Divergence:* Rust-conformant (items.union.fields.write-safety)

*Source:* `src/compiler/sema_stmt.cpp#L7467-L7473`

## Compound assignment (`expr.compound-assign`)

### `expr.compound-assign.op-trait-mapping` — Compound-assign operator → *Assign trait/method

Each compound-assign operator `op=` maps to a trait + method: `+=`→AddAssign::add_assign, `-=`→SubAssign::sub_assign, `*=`→MulAssign::mul_assign, `/=`→DivAssign::div_assign, `%=`→RemAssign::rem_assign, `&=`→BitAndAssign::bitand_assign, `|=`→BitOrAssign::bitor_assign, `^=`→BitXorAssign::bitxor_assign, `<<=`→ShlAssign::shl_assign, `>>=`→ShrAssign::shr_assign. Operators outside this set have no *Assign trait.

*Source:* `src/compiler/sema_stmt.cpp#L2269-L2283`

### `expr.compound-assign.base-op-strip` — Compound-assign base operator

A compound-assign token `op=` denotes the binary operator `op` obtained by stripping the trailing `=`; the place is the receiver and the right side is the value operand. A bare `VAR_REF` place takes the simple-variable path; any other place (field/index/tuple-field/chain/`(*p).f`) routes through the general place-compound path.

*Source:* `src/compiler/sema_stmt.cpp#L2286-L2301`

### `expr.compound-assign.var-undefined` — Compound-assign to undefined variable is an error

`x op= e` where `x` is not a bound variable is rejected: "compound assignment to undefined variable".

*Source:* `src/compiler/sema_stmt.cpp#L2303-L2309`

### `expr.compound-assign.var-immutable` — Compound-assign requires a mutable place

`x op= e` requires `x` to be declared `mut`; an immutable target is rejected: "compound assignment to immutable variable". The struct-array `IndexMut` compound path likewise requires the array/struct variable to be `mut`: "index compound assign to immutable struct".

*Source:* `src/compiler/sema_stmt.cpp#L2310-L2311`, `src/compiler/sema_stmt.cpp#L2425-L2426`

### `expr.compound-assign.opassign-dispatch` — Compound-assign dispatches via *Assign impl when present

For a place of struct type S, if an impl of the operator's *Assign trait exists for S (matched by concrete or base struct name), `place op= rhs` lowers to the in-place call `op_assign(&mut place, rhs)` (void result, no assign-back). The trait method's Rhs parameter need not equal Self: the impl is selected by the actual rhs operand type, falling back to the Self-Rhs signature if the rhs-typed one does not resolve.

*Divergence:* Rust-conformant operator-overload semantics; Logos struct-name-keyed impl lookup.

*Source:* `src/compiler/sema_stmt.cpp#L2318-L2360`, `src/compiler/sema_stmt.cpp#L2493-L2518`

### `expr.compound-assign.opassign-fallback-binop` — Compound-assign without *Assign impl desugars to read-modify-write

Absent a matching *Assign impl, `place op= rhs` desugars to `place = (place) op rhs` (read-twice / double-eval of the place), dispatching `op` through the corresponding binary-operator trait (Add/Sub/…), which constructs a fresh Self.

*Source:* `src/compiler/sema_stmt.cpp#L2313-L2314`, `src/compiler/sema_stmt.cpp#L2370-L2373`, `src/compiler/sema_stmt.cpp#L2520-L2534`

### `expr.compound-assign.type-mismatch` — Compound-assign RHS type-compatibility

In the read-modify-write path, the rhs type must be compatible with the place type; otherwise "compound assignment: type mismatch — expected T, got U".

*Source:* `src/compiler/sema_stmt.cpp#L2362-L2369`, `src/compiler/sema_stmt.cpp#L2521-L2527`

### `expr.compound-assign.index-mut-dispatch` — Compound-assign through IndexMut on a struct

`a[i] op= v` where `a` has struct type with an `IndexMut` impl lowers to `*index_mut(&mut a, i) = (*index(&a, i)) op v`, using the `Index` read accessor for the current value when present (else `index_mut`); the index expression is widened to the accessor's index-parameter integer type, and the rhs must be compatible with the indexed output type.

*Source:* `src/compiler/sema_stmt.cpp#L2413-L2480`

### `expr.compound-assign.place-too-nested` — Compound-assign target nesting limit

A compound-assign target too deeply nested to write in place is rejected with guidance to bind an intermediate `&mut` reference.

*Note:* Implementation-capability limit rather than a designed language restriction.

*Source:* `src/compiler/sema_stmt.cpp#L2481-L2486`

### `expr.compound-assign.int-widen` — Implicit integer widening in the compound-assign fallback

In the general (non-`*Assign`-impl) place-compound-assign path, the rhs is implicitly widened to the place's integer type before combining with the base operator.

*Divergence:* Rust has no implicit integer widening on assignment.

*Source:* `src/compiler/sema_stmt.cpp#L2528`

## Block expressions (`expr.block`)

### `expr.block.value-block-scopes-let` — Value-producing block scopes its own let bindings

A value-producing block expression `{ stmts; result }` introduces a new lexical scope: a `let` at the block's top level that shadows an outer binding of the same name is visible only inside the block, and the outer binding is restored when the block's value is produced; the block does not clobber the outer slot.

```logos
let x = 1; let y = { let x = 100; x + 1 }; // x still == 1, y == 101
```

*Source:* `src/compiler/mlir_gen_expr.cpp#L5494-L5557`

### `expr.block.as-value` — Block / control constructs as expressions

`{ … }` (bare block), `unsafe { … }`, `loop { … }`, `if … {} else {}`, and `match … {}` are all primary expressions producing a value (block/loop yield their tail/break value). In the no-struct-lit primary (`primary_expr_ns`, used e.g. for an `if`/`while`/`match` condition) a *bare* `{ … }` block is NOT admitted as a primary — only the keyword-prefixed forms `unsafe`/`loop`/`if`/`match` are — to avoid ambiguity with the enclosing control construct's own opening brace.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2711-L2715`, `tools/peg_gen_cpp/grammars/logos.peg#L2771-L2775`, `tools/peg_gen_cpp/grammars/logos.peg#L2576-L2588`

### `expr.block.empty-is-void` — Empty block has type ()

A block expression `{}` with no statements evaluates to the unit/void type `()`.

*Source:* `src/compiler/sema_expr.cpp#L13653-L13656`

### `expr.block.tail-expr-value` — Block value is its trailing tail expression

The type and value of a block `{ s1; ...; e }` are those of its final element when that element is a tail/expression statement (or a non-statement expression form); a block whose final element is a `let`, destructuring-let, `return`, or `;`-terminated expr-stmt produces no tail value and types as `()`.

*Source:* `src/compiler/sema_expr.cpp#L13676-L13724`

### `expr.block.tail-divergent-call-never` — Block with diverging tail call types as !

If a block's final tail expression is a call to a `-> !` (diverging) callee, the block types as the never type `!`; the diverging call is still emitted and the block contributes no concrete value type to its context.

*Related:* `expr.if.never-branch-skipped`

*Source:* `src/compiler/sema_expr.cpp#L13692-L13696`

### `expr.block.tail-return-adopts-value-type` — Block ending in `return e` adopts e's type

A block whose final statement is `return e` is non-diverging in the value system: the block's result type is taken as `typeof(e)` even though no value is produced, so the divergent block is usable at a non-void expected type (e.g. inside a tuple/struct literal). The `return` is still lowered and executed.

*Divergence:* No real `!`/never subtyping for tail-return; the return-value's type is adopted as a block-type proxy instead of `!`.

*Note:* Behavior is a stated workaround pending full never-type support.

*Source:* `src/compiler/sema_expr.cpp#L13664-L13672`, `src/compiler/sema_expr.cpp#L13706-L13720`

## Tail expressions (`expr.tail`)

### `expr.tail.implicit-return-in-fn-body` — A function body's tail expression is an implicit return

Inside a function body (governed by a per-lowering flag), a `TAIL_EXPR` statement (the block's final expression with no trailing semicolon) acts as an implicit `return`. The flag is cleared while lowering nested block-as-expression contexts (match-arm body, unsafe-block-as-expr, if-as-expr), where the tail expression is instead the block's VALUE, not a function return.

*Source:* `src/compiler/sema_impl.hpp#L3700-L3704`

## `if` / `if let` (`expr.if`)

### `expr.if.branch-result-coercion` — If-expression coerces both branch values to the result type

An if-expression of type T evaluates the condition then both branches; each non-diverging branch value is numerically coerced to T and stored into a shared result slot, whose value is the if-expression's result. Aggregate branch values are spilled to a stack slot so both branches store a pointer when T is pointer-represented.

*Source:* `src/compiler/mlir_gen_expr.cpp#L3788-L3833`

### `expr.if.divergent-branch-skips-merge` — Diverging if-branch omits its merge edge

If a branch body diverges (e.g. `break`/`return` that already terminates the block), the if-expression omits that branch's result-store and merge branch; the merge point's predecessors simply exclude the diverging edge.

*Source:* `src/compiler/mlir_gen_expr.cpp#L3807-L3820`, `src/compiler/mlir_gen_expr.cpp#L3824-L3834`

### `expr.if.void-branches-still-evaluated` — Void if-expression still evaluates both branches

An if-expression of unit type `()` still emits and evaluates both branch bodies (for their side effects such as panics/writes) and yields a synthetic unit value; the branches are not dropped despite producing no value.

*Source:* `src/compiler/mlir_gen_expr.cpp#L3776-L3785`, `src/compiler/mlir_gen_expr.cpp#L3837-L3840`

### `expr.if.branch-scope-isolation` — Each `if`/`else` branch is an independent lexical scope

Bindings (and their variable-classification state, e.g. dyn/tuple/struct/enum tagging) introduced inside one arm of an `if`/`else` are not visible in the sibling arm or in code after the `if`: the classification state is snapshotted before entering the branches and restored after each branch, isolating `let`s local to a branch.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L2328-L2343`

### `expr.if.both-diverge-no-merge` — An `if`/`else` whose branches both diverge has no fall-through point

If both the `then` and `else` blocks of an `if` terminate control flow (each ends in a diverging op, e.g. `return`), the `if` as a whole does not produce a merge/continuation point — no code after it in the same block is reachable through it.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L2335-L2348`

### `expr.if.let-chain` — if let-chain

An `if` may chain conditions with `&&` where the first segment is a `let` binding: `if let P = e && seg (&& seg)* { THEN } [else …]`. The `let` scrutinee is parsed at `cmp_expr_ns` (one level below `&&`) so the `&&` belongs to the chain rather than the scrutinee expression. Each subsequent seg is either `let P = e` (scrutinee also at `cmp_expr_ns`) or a bare condition (`cmp_expr_ns`). The chain requires the first segment to be a `let` and at least two `&&`-joined segments — enforced so the legacy single-let and bare-cond `if` forms are not shadowed — and is followed by a lookahead on `{` so it does not swallow tokens past the if-block opening. Desugars to nested matching: all let-patterns must match and all bare conditions must hold for THEN to execute (equivalent to `match e { P if <trailing-cond> => THEN, _ => ELSE }` when there is exactly one trailing bare condition).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2402-L2416`, `tools/peg_gen_cpp/grammars/logos.peg#L2430-L2441`

### `expr.if.single-let-guard` — if-let with single guard condition

`if let P = e && cond { THEN } [else ELSE]` (single let plus one trailing condition) desugars to `match e { P if cond => THEN, _ => ELSE }`; the let scrutinee is parsed at `cmp_expr_ns` so the `&&` belongs to the guard, and the trailing `cond` is a full `expr_ns` (itself possibly an `&&` chain).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2417-L2420`

### `expr.if.no-struct-lit-cond` — if/while/for condition restricts struct literals

In `if`/`while`/`for` condition position the scrutinee uses the no-struct-lit expression grammar (`expr_ns`): a top-level `IDENT { … }` is NOT parsed as a struct literal, so the brace opens the control-flow block instead. The restriction applies only to the top-level primary — inside parens/brackets/calls, full `expr` resumes. To use a struct literal in condition position it must be parenthesized: `if (Foo { x: 1 }.ok) { }`.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2471-L2477`, `tools/peg_gen_cpp/grammars/logos.peg#L2572-L2576`

### `expr.if.plain` — if / if-else expression

`if cond { THEN }` and `if cond { THEN } else (if_expr | { ELSE })` are expressions; `cond` is a bare `expr_ns` (no-struct-lit). `else` may chain to another `if_expr`, forming an if/else-if/…/else ladder.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2425-L2428`

### `expr.if.requires-else-in-expr-position` — if/if-let in expression position requires else

An `if` or `if let` used as an expression (yielding a value) must have an `else` branch; an `if` without `else` is only valid in statement position.

*Source:* `src/compiler/sema_expr.cpp#L13820-L13823`, `src/compiler/sema_expr.cpp#L13913-L13916`

### `expr.if.cond-must-be-bool` — if condition must be bool

The condition of a non-`let` `if` must have type `bool`; the error/never types are also accepted (error recovery and diverging conditions).

*Source:* `src/compiler/sema_expr.cpp#L13901-L13906`

### `expr.if.let-desugars-to-match` — if-let expression lowers to a two-arm match

`if let P = e { THEN } else { ELSE }` in expression position is equivalent to `match e { P => THEN, _ => ELSE }`; the pattern's bindings are in scope only within THEN, and the result type is that of the THEN branch.

*Source:* `src/compiler/sema_expr.cpp#L13815-L13897`

### `expr.if.branch-type-compatible` — if-expr branches must have compatible types

In an `if` expression, the THEN and ELSE branch types must be mutually compatible (one assignable to the other); incompatible non-error, non-never branch types are an error. The result type is the unification (LUB) of the two branch types.

*Source:* `src/compiler/sema_expr.cpp#L14000-L14037`

### `expr.if.never-branch-skipped` — Never/error branch yields the other branch's type

A branch typed `!` (never) or error contributes no type to an `if` expression: the expression's type is the other branch's type. `!` behaves as a subtype of every type at the join. A branch whose final statement is `return`/`break`/`continue` (or a diverging tail call) is typed `!`.

*Related:* `expr.block.tail-divergent-call-never`

*Source:* `src/compiler/sema_expr.cpp#L13959-L13970`, `src/compiler/sema_expr.cpp#L13998-L14005`

### `expr.if.let-condition` — if and if-let

`if cond { ... }` takes a boolean condition; `if let PAT = expr { ... }` matches a pattern. An `else` branch is either a block or a chained `else if`.

*Source:* `src/compiler/sema_render.cpp#L395-L420`

## `if let` chains (`expr.if-let-chain`)

### `expr.if-let-chain.fall-to-else-on-failure` — if-let chains desugar to sequential refutable binds

`if let P1 = e1 && let P2 = e2 && cond { THEN } else { ELSE }` (IF_LET_CHAIN, segments LET_CHAIN_LET/LET_CHAIN_COND) desugars to a flat sequence of refutable pattern binds and boolean-condition checks; the chain falls through to ELSE as soon as any segment fails (bind mismatch or false condition), short-circuiting the rest.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L319-L321`

### `expr.if-let-chain.min-two-segments` — if-let chain requires at least two segments

An `if let ... && ...` chain must contain at least two segments (let-bindings and/or conditions); fewer is an error. The chain desugars inside-out into nested `if let`/`if` with the `else` branch duplicated at each fall-through.

*Note:* ELSE duplication at each fall-through is documented as an accepted limitation, not a fundamental rule.

*Source:* `src/compiler/sema_expr.cpp#L13745-L13797`

## `match` (`expr.match`)

### `expr.match.scrutinee-autoderef` — Match auto-derefs reference/pointer scrutinees

When the scrutinee type is a chain of `&` / `&mut` / `*` over an enum (arbitrary depth, e.g. `&&Option<T>`), `match` peels all reference layers and matches against the underlying value: `match &e { ... }` behaves identically to `match e { ... }`.

*Source:* `src/compiler/mlir_gen_expr.cpp#L3823-L3877`

### `expr.match.enum-discriminant-dispatch` — Match on enum dispatches by discriminant

For an enum scrutinee, arm selection compares the scrutinee's discriminant against each arm's variant discriminant. A payload-carrying enum (with TaggedEnumInfo) loads its discriminant from its storage; a fieldless/C-like enum's value IS its i32 discriminant.

*Source:* `src/compiler/mlir_gen_expr.cpp#L3841-L3876`, `src/compiler/mlir_gen_expr.cpp#L4382-L4390`, `src/compiler/mlir_gen_expr.cpp#L4721-L4737`

### `expr.match.arm-first-match-order` — Arms tested top-to-bottom; first match wins

Arms are evaluated in source order; the first arm whose pattern matches (and whose guard, if any, holds) is selected, and remaining arms are not tested.

*Source:* `src/compiler/mlir_gen_expr.cpp#L4294-L4738`, `src/compiler/mlir_gen_expr.cpp#L4337`, `src/compiler/mlir_gen_expr.cpp#L4734-L4736`

### `expr.match.value-result-type` — Match expression yields a single value of the common arm type

A `match` used as an expression evaluates to the value of the selected arm; every arm body's value is coerced to the match's result type. Arms whose body diverges (does not fall through) contribute no value.

*Source:* `src/compiler/mlir_gen_expr.cpp#L3789-L3807`, `src/compiler/mlir_gen_expr.cpp#L4346-L4351`, `src/compiler/mlir_gen_expr.cpp#L4743`

### `expr.match.guard-after-bindings` — Guard evaluated after pattern bindings, fall-through on false

An arm guard `if cond` is evaluated only after the arm's pattern matches and its bindings are in scope; the guard may reference those bindings. If the guard is false, control falls through to the next arm rather than selecting this arm.

*Source:* `src/compiler/mlir_gen_expr.cpp#L4318-L4339`

### `expr.match.exhaustive-no-default-arm` — Exhaustive discrete match needs no fallthrough default

A match over `bool` covering both `true` and `false` (or a wildcard), or over an enum covering every variant (or a wildcard), is exhaustive; no implicit fall-through arm is required and the non-matching path is unreachable.

*Source:* `src/compiler/mlir_gen_expr.cpp#L4229-L4293`

### `expr.match.arm-forms` — match arm syntax

`match scrutinee { PAT [if GUARD] => RHS, ... }`; each arm has an optional `if`-guard and an arm body that is either a block or an expression followed by a comma.

*Source:* `src/compiler/sema_render.cpp#L422-L447`

### `expr.match.result-type-lub` — match-expression result type is the LUB of its arms

The type of a `match` expression is the least-upper-bound of its arms' value types. Arms are unified left-to-right: error-typed and Never-typed arms contribute no type; numeric arms unify via numeric-LUB. If two arms have types that are mutually incompatible (neither `types_compatible` direction holds) the match is a type error.

*Related:* `expr.match.never-arm-ignored`, `expr.match.fnitem-arms-lub-fnptr`, `expr.match.intlit-result-widen`

*Source:* `src/compiler/sema_stmt.cpp#L9497-L9534`

### `expr.match.never-arm-ignored` — Never-typed (diverging) arms do not constrain the result type

An arm whose value type is `!` (Never) contributes no type to the match result; Never is a subtype of every type. If the accumulated result type is still `!` or Error, the next arm's type replaces it.

*Related:* `expr.match.result-type-lub`

*Source:* `src/compiler/sema_stmt.cpp#L9494-L9501`

### `expr.match.fnitem-arms-lub-fnptr` — distinct fn-item arms LUB to the common fn-pointer type

When two arms produce distinct FnItem values with the same signature (e.g. `=> a_f` and `=> b_f`), the match result type is the corresponding `fn(...)->R` pointer type, since FnItem→FnItem coercion is rejected; both arms coerce to that FnPtr.

*Divergence:* Rust-conformant: matches Rust LUB for fn-item match arms.

*Related:* `expr.match.result-type-lub`

*Source:* `src/compiler/sema_stmt.cpp#L9502-L9523`

### `expr.match.intlit-result-widen` — integer-literal match result widens to i64 on i32 overflow

If the inferred match result type is the unconstrained integer-literal type, and any arm's literal value exceeds the i32 range (&gt; INT32_MAX or &lt; INT32_MIN), the result type is fixed to i64.

*Related:* `expr.match.result-type-lub`

*Source:* `src/compiler/sema_stmt.cpp#L9535-L9550`

### `expr.match.guard-bool` — match guard must be bool

An arm guard expression (`pat if <guard> =>`) must have type `bool` (or Error); any other type is a diagnostic.

*Source:* `src/compiler/sema_stmt.cpp#L9343-L9348`

### `expr.match.arm-after-catchall-unreachable` — arm after a catch-all `_` arm is unreachable

A match arm that follows an unguarded catch-all (`_`) arm is unreachable and is diagnosed (closes B-pt-07 expr position).

*Source:* `src/compiler/sema_stmt.cpp#L8946-L8959`

### `expr.match.exhaustive-enum` — match on enum must be exhaustive

A `match` on an enum scrutinee without a wildcard/catch-all arm (and without AST-level proof of exhaustiveness for nested patterns) must cover every constructible variant; uncovered variants are reported as 'missing variant(s)'. A variant all of whose (substituted) payload types are uninhabited is unconstructable and need not be covered.

*Related:* `expr.match.exhaustive-enum-uninhabited`

*Source:* `src/compiler/sema_stmt.cpp#L9603-L9680`

### `expr.match.exhaustive-enum-uninhabited` — uninhabited-payload variants are exempt from exhaustiveness

Exhaustiveness substitutes the scrutinee's type-arguments into each variant's (generic) payload types before the uninhabited check; a variant with any uninhabited payload (e.g. `Result<T, Void>`'s Err) is unconstructable and omitting its arm remains exhaustive (T2-29).

*Related:* `expr.match.exhaustive-enum`

*Source:* `src/compiler/sema_stmt.cpp#L9650-L9675`

### `expr.match.exhaustive-bool` — match on bool must cover true and false

A `match` on a `bool` scrutinee without a wildcard arm must have both a `true` and a `false` unguarded literal arm; a missing case is diagnosed.

*Source:* `src/compiler/sema_stmt.cpp#L9681-L9694`

### `expr.match.guarded-arm-not-exhaustive` — guarded arms do not count toward exhaustiveness

An arm with a guard (`if`) does not contribute to exhaustiveness coverage; only unguarded patterns are counted as covering variants/wildcards.

*Source:* `src/compiler/sema_stmt.cpp#L9612`, `src/compiler/sema_stmt.cpp#L9618-L9623`, `src/compiler/sema_stmt.cpp#L9639-L9640`

### `expr.match.arm-block-tail-is-value` — block arm yields its tail expression, not an implicit return

A block-form arm (`pat => { stmts }`) yields its trailing expression as the arm value (tail-as-return disabled inside match arms). A non-diverging block arm whose last statement is not an expression is a diagnostic ('block arm must end with an expression or always return'). A block arm all of whose paths diverge contributes Error and is skipped in unification.

*Source:* `src/compiler/sema_stmt.cpp#L9414-L9467`

### `expr.match.arm-requires-body` — every arm must have an expr or block body

A match arm must have either an expression body (`=> expr`) or a block body (`=> { ... }`); an arm with neither is a diagnostic.

*Source:* `src/compiler/sema_stmt.cpp#L9412-L9471`

### `expr.match.temp-scrutinee-dropped` — a droppable rvalue scrutinee is dropped after the match value

When the scrutinee of a match-expression is a droppable move-type rvalue (not a place: not a var/field/tuple-index/deref/index read), it is bound to a synthetic local and dropped on every exit path. On fall-through the temporary is dropped after the match result is bound (unless an arm moved its payload); an arm that returns drops it via its own drop set.

*Related:* `borrow.match.scrutinee-moved-by-binding`

*Source:* `src/compiler/sema_stmt.cpp#L8875-L8937`, `src/compiler/sema_stmt.cpp#L8884-L8903`

### `expr.match.str-literal-arm-guard` — string-literal arms lower to wildcard + str-eq guard

A top-level string-literal arm (`match s { "foo" => ... }`) matches via a wildcard pattern plus a synthesized `str_eq(scrutinee, "foo")` guard, AND-ed ahead of any user guard; the scrutinee is hoisted into a synthetic local first (G172-1).

*Source:* `src/compiler/sema_stmt.cpp#L9034-L9067`, `src/compiler/sema_stmt.cpp#L9193-L9211`, `src/compiler/sema_stmt.cpp#L9350-L9359`

### `expr.match.writ-pattern-needs-view` — Writ patterns require a view scrutinee

A match arm containing a Writ scalar pattern (PAT_WRIT_NULL/BOOL/INT/STR/MAP/ARR/TYPED_ARR/TYPED_MAP, including inside an or-pattern) requires the scrutinee to be a Writ view (Writ, WritView, or WritStatic; use `&` to borrow); otherwise a diagnostic is emitted.

*Divergence:* Logos extension: Writ structured-data pattern matching (not in Rust).

*Source:* `src/compiler/sema_stmt.cpp#L8961-L9003`

## Loops (`expr.loop`)

### `expr.loop.break-value-slot` — `loop { ... break v; ... }` evaluates to the broken-out value

A `loop` expression used to produce a value allocates a result slot before entering the loop body; `break v;` targeting that loop stores `v` into the slot before branching to the loop's exit, and the slot is the loop expression's value at the exit block.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L2516-L2526`, `src/compiler/mlir_gen_stmt.cpp#L2561-L2566`

### `expr.loop.as-expr-type` — loop expression type: ! if no break-value, () if value-less break

A `loop {...}` used as an expression has type `!` (never) when no `break v` is reachable and the loop diverges, type `()` when a value-less `break` is reached, and the common break-value type (read back via a synthesized break slot) when `break v` is reached.

*Source:* `src/compiler/sema_expr.cpp#L1521-L1564`

### `expr.loop.empty-loop-diverges` — `loop {}` with no reachable break is a diverging (`!`) expression

A `loop { ... }` expression that contains no `break` reachable to its own frame is diverging: as an expression its type is `!`, not `()`. The per-lowering "no break reached" flag is reset on every `lower_loop` call so one loop's divergence does not leak into a sibling loop's typing.

*Source:* `src/compiler/sema_impl.hpp#L3694-L3699`

## `for` loops (`expr.for-each`)

### `expr.for-each.slice-elem-by-ref` — Iterating a slice binds the loop variable as a reference into the original buffer

`for x in <slice expr> {}` binds `x` to the ADDRESS of each element within the slice's backing storage (a reference `&T` into the original data), not a copy — mutations to `*x` (or through auto-deref) are visible in the original buffer, mirroring Rust's `for x in &[T]` (`IntoIterator for &[T]` yielding `&T`).

*Source:* `src/compiler/mlir_gen_stmt.cpp#L2664-L2684`

### `expr.for-each.array-elem-binding` — Iterating a fixed-size array by value: scalar elements are copied, struct/tuple elements bind the in-place address

`for x in <array expr> {}` (non-slice, fixed-size array): for a scalar element type, `x` is bound to a fresh stack slot holding a COPY of the element (mutating `x` does not affect the source array); for a struct- or tuple-typed element, `x` is bound directly to the element's address inside the array's own backing storage.

*Note:* Whether struct/tuple loop-variable mutation is intended to alias the source array (vs. Rust's uniform move/copy-by-value for `for x in array`) is not resolved within this slice alone; may reflect Logos's general pointer-based struct value representation elsewhere rather than an aliasing divergence.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L2745-L2780`

## `break` (`expr.break`)

### `expr.break.value-loop-typing` — break value selects the loop's value type

A `break value` (optionally labeled) attributes its value type to the target loop frame; the frame's value type is the (numeric) unification of all break values reaching it, making the loop a value-yielding expression.

*Source:* `src/compiler/sema_expr.cpp#L1455-L1476`

### `expr.break.label-must-be-in-scope` — `break`/`continue` with a label must reference an enclosing labelled loop

`break 'label` / `continue 'label` is valid only when `'label` names a currently-active enclosing loop; the active-label stack contains only labelled loops (unlabelled loops push nothing), so referencing an out-of-scope or nonexistent label is a diagnostic.

*Source:* `src/compiler/sema_impl.hpp#L3660-L3664`

### `expr.break.value-attributed-to-labeled-frame` — `break 'label v` attributes its value to the matching labeled loop frame, not an inner loop

Each active loop (for/while/loop), regardless of kind, pushes a break-frame {label, value_type, without_value}. A `break 'label v` attributes v's type to the frame whose label matches; an unlabeled `break v` targets the innermost frame. Only a `loop { ... }` expression reads its OWN frame's value_type to become value-yielding — so a value breaking to an outer labeled loop is not incorrectly captured as the type of an intervening inner `loop`.

*Related:* `expr.break.label-must-be-in-scope`, `expr.loop.empty-loop-diverges`

*Source:* `src/compiler/sema_impl.hpp#L3679-L3692`

## `return` (`expr.return`)

### `expr.return.implicit-tail` — Tail expression synthesizes implicit return

An expression with no trailing `;` at statement position (TAIL_EXPR) is sugar: sema synthesizes an implicit `return expr` for functions with non-void return type.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L292`

## Control flow (`expr.control`)

### `expr.control.break-continue-return-in-value-position` — break/continue/return are value-position expressions

`break`, `continue`, and bare `return` may appear in expression position (BREAK_EXPR, CONTINUE_EXPR, RETURN_EXPR), each carrying Rust's `!`/Never type; sema lowers each to its statement form (SBreak/SContinue/SReturn) plus an Error-typed sentinel expression so the surrounding type-check accepts the position. RETURN_EXPR in this form takes no value.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L300-L302`

### `expr.control.never-position` — Diverging control-flow as expression

In full expression position (`primary_expr`): `return [e]`, `break ['label] [e]`, bare `break`, and `continue ['label]` may appear as an expression producing the never type `!`, enabling forms like `let x = if c { v } else { return e };`, `_ => break`, and `match … { … => continue }`. In the no-struct-lit primary (`primary_expr_ns`, used in if/while/for condition position) only bare `break` is admitted as an expression — labeled/value-carrying `break`, and `continue`/`return` in any form, are excluded there because they would greedily consume following tokens or shadow the statement-level forms (e.g. `return *v;` would otherwise parse as a bare RETURN_EXPR followed by an orphan `*v`).

```logos
let x = if c { v } else { return e };
match m { _ => break }
```

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2716-L2728`, `tools/peg_gen_cpp/grammars/logos.peg#L2781-L2788`, `tools/peg_gen_cpp/grammars/logos.peg#L2589-L2596`

## Control flow (`expr.control-flow`)

### `expr.control-flow.diverging-is-never` — break/continue/return in expression position have type !

`break`, `continue`, and `return` used in expression position have type `!` (never), which unifies with any surrounding expected type. `continue`/`break` outside any loop are errors. `return e` in expression position checks e against the function's return type.

*Source:* `src/compiler/sema_expr.cpp#L1410-L1479`

## Never type (`expr.never`)

### `expr.never.fallback-on-diverging-callee` — Unbound generic type-param falls back to `!` only when the callee body always diverges

An unconstrained generic type-parameter with no other binding information defaults to `!` (Never) exactly when the callee's function body ALWAYS diverges (every control path panics or ends in a diverging tail-loop) — strictly narrower than general "always returns": a body with a normal reachable `return` on any path disqualifies the `!`-fallback.

*Source:* `src/compiler/sema_impl.hpp#L3757-L3762`

## `?` operator (`expr.try`)

### `expr.try.ok-unwrap-err-propagate` — `expr?` unwraps Ok or early-returns Err

`expr?` on a Result-like tagged enum loads the discriminant: on the Ok variant it yields the Ok payload value; on the Err variant it reconstructs an Err value carrying the original error payload and immediately returns it from the enclosing function. The expression's value is the unwrapped Ok payload.

*Source:* `src/compiler/mlir_gen_expr.cpp#L5563-L5681`

### `expr.try.operator` — Try operator

Postfix `e?` (`TRY_EXPR`) is the try operator; it propagates the error/none case of a Result/Option-like receiver and yields the success payload.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2692-L2693`, `tools/peg_gen_cpp/grammars/logos.peg#L2568-L2569`, `tools/peg_gen_cpp/grammars/logos.peg#L2752-L2753`

### `expr.try.result-option-extract` — ? on Result/Option extracts or early-returns

`e?` where `e: Result<T,E>` extracts Ok(v) and early-returns Err(e); where `e: Option<T>` extracts Some(v) and early-returns None. It is valid only inside a function whose declared return type is the same enum (Result resp. Option); otherwise an error.

*Source:* `src/compiler/sema_expr.cpp#L1170-L1236`, `src/compiler/sema_expr.cpp#L1324`

### `expr.try.heterogeneous-error-from` — ? converts inner error via From when error types differ

For `e?` with `e: Result<T,E_inner>` in a function returning `Result<U,E_outer>` where E_inner != E_outer, the Err path returns `Err(E_outer::from(err))`, requiring a resolvable `From<E_inner> for E_outer` (matched as an `<E_outer>__from` candidate with a matching sole parameter type); absence of that impl is an error suggesting `.map_err(...)?`.

*Source:* `src/compiler/sema_expr.cpp#L1237-L1323`

### `expr.try.trait-dispatch-from-residual` — ? on non-Result/Option dispatches via Try/FromResidual

`e?` where e is neither stdlib Result nor Option desugars through the Try/FromResidual surface: `match (e).branch() { ControlFlow::Continue(c) => c, ControlFlow::Break(r) => return RetType::from_residual(r) }`. RetType is rendered from the enclosing function's declared return type; an undeterminable return type is an error.

*Source:* `src/compiler/sema_expr.cpp#L1184-L1212`

## Closures (`expr.closure`)

### `expr.closure.env-capture-binding` — A capturing closure binds captures from an environment record

A capturing closure is a {fn_ptr, env_ptr} value; the body receives env_ptr as a hidden leading parameter and each capture is bound from env field i+1 (env field 0 reserved for drop glue). Aggregate (struct/array/tuple/enum/dyn) captures are stored/bound by pointer; scalar captures are stored by value and re-allocated locally in the body.

*Source:* `src/compiler/mlir_gen_dyn.cpp#L1843-L1849`, `src/compiler/mlir_gen_dyn.cpp#L1957-L2048`, `src/compiler/mlir_gen_dyn.cpp#L2216-L2228`

### `expr.closure.uniform-drop-glue-slot` — Closure env carries a uniform drop-glue slot

Every closure env reserves field 0 for a `drop_glue: ptr` slot for a uniform drop protocol; the slot holds the address of generated drop glue when the closure owns droppable captures or has a heap env (which must be freed), otherwise null (drop is a no-op).

*Source:* `src/compiler/mlir_gen_dyn.cpp#L1843-L1849`, `src/compiler/mlir_gen_dyn.cpp#L2117-L2165`

### `expr.closure.param-type-inference` — Closure parameter type may be omitted

A closure_param's `: Type` annotation is optional (`|x|` vs `|x: T|`); when omitted, the parameter's type is inferred from the surrounding `fn(T) -> R` formal at the call site (sema), not fixed by the grammar.

*Note:* The inference mechanism itself is implemented in sema, outside this slice; only the grammar's relaxed-annotation shape is directly evidenced here.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3039-L3043`

### `expr.closure.param-forms` — Closure parameter binding forms

closure_param accepts: `&mut IDENT` (ref+mut, no type), `&IDENT` (ref, no type), `ref IDENT: Type`, `mut IDENT: Type`, `mut IDENT` (no type), `(pat_binding_list): Type` (tuple-destructuring param, type mandatory), `IDENT: Type`, or bare `IDENT` (no type).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3044-L3062`

### `expr.closure.forms` — Closure expression forms

closure_expr = [`move`] (`||` | `|` closure_param_list? `|`) [`-> RetType`] BODY, where BODY is either a block `{ ... }` or a bare expression; the expression-body alternatives are tried after the block-body alternatives so `|x| { ... }` still parses with a block body rather than a struct-literal/block expression.

```logos
move |x: i32| -> i32 { x + 1 }
|x| x + 1
|| 42
```

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3064-L3099`

### `expr.closure.param-type-inference-from-hint` — Untyped closure params infer types from expected fn signature

For a closure literal `|x, y| …` whose parameters carry no type annotation, each untyped parameter's type is taken from the corresponding formal of the expected callable type at the call site (the closure-formal hint), by positional index. The hint is consulted only for params that lack both a TYPE and a NAMES (tuple-destructure) node.

```logos
let f: fn(i32) -> i32 = |x| x + 1;
```

*Source:* `src/compiler/sema_expr.cpp#L14137-L14158`

### `expr.closure.hint-peels-callable-wrappers` — Closure-formal hint peels through refs/pointers and single-arg wrappers to a callable

When inferring closure param types from an expected type, the expected type is peeled (up to 8 levels) through `&T`/`&mut T`/`*T` (to pointee) and through a Struct/ZonedStruct with exactly one type argument (to that argument) until a Closure or FnPtr type is reached; the resulting callable's parameter list supplies the param-type hints. This lets `Box<dyn Fn(..)>`/`&dyn Fn(..)`-typed contexts still drive inference.

```logos
let b: Box<dyn Fn(i32) -> i32> = box_new(|x| x + 1);
```

*Source:* `src/compiler/sema_expr.cpp#L14082-L14099`, `src/compiler/sema_expr.cpp#L14138-L14148`

### `expr.closure.ref-bind-param` — `|ref x: T|` binds x as &T

A closure parameter written `ref x: T` (IS_REF with an explicit TYPE) takes its argument by value of type T under a synthetic name and binds the user-visible `x` to `&T` aliasing the synthetic param. IS_REF without a TYPE is the `&self`/`&mut self` shorthand, not a ref-bind.

```logos
let f = |ref x: i32| *x + 1;
```

*Divergence:* Logos closure ref-binding param syntax; no direct Rust equivalent.

*Source:* `src/compiler/sema_expr.cpp#L14191-L14206`, `src/compiler/sema_expr.cpp#L14257-L14259`, `src/compiler/sema_expr.cpp#L14304-L14311`

### `expr.closure.mut-bind-param` — `|mut x|` binds a mutable copy of the parameter

A closure parameter written `mut x` (IS_MUT, not a ref-bind) takes its argument under a synthetic name and binds the user-visible `x` as a mutable local initialized from the synthetic param (`let mut x = synth;`). The synthetic name is not entered into the sema scope, so move-typed params do not receive double drop glue.

```logos
let f = |mut x: i32| { x += 1; x };
```

*Source:* `src/compiler/sema_expr.cpp#L14199-L14212`, `src/compiler/sema_expr.cpp#L14248-L14256`, `src/compiler/sema_expr.cpp#L14296-L14303`

### `expr.closure.tuple-destructure-param` — `|(a, b): (T1, T2)|` destructures a tuple parameter

A closure parameter written `(a, b, …): (T1, T2, …)` takes a single synthetic tuple-typed parameter and binds each user name to the corresponding tuple element (`let a = synth.0; let b = synth.1; …`), with `_` sub-patterns skipped. Element bindings are only emitted when the param type is a Tuple type; bindings are positional up to the lesser of name-count and tuple arity.

```logos
let f = |(a, b): (i32, i32)| a + b;
```

*Source:* `src/compiler/sema_expr.cpp#L14159-L14188`, `src/compiler/sema_expr.cpp#L14260-L14268`, `src/compiler/sema_expr.cpp#L14312-L14326`

### `expr.closure.expr-body-yields-value` — Expression-body closure yields its expression

A closure with an expression body `|y| expr` (no braces) is lowered as if its body were `return expr;`; the closure result is the value of `expr`.

```logos
let f = |y| y * 2;
```

*Source:* `src/compiler/sema_expr.cpp#L14284-L14290`

### `expr.closure.return-type-inference` — Closure return type inferred from first non-void return

A closure without an explicit `-> R` annotation infers its return type by scanning the lowered body (recursing into if/while/loop/block) for return statements and adopting the type of the first return value whose type is neither Void nor Error; if none is found the return type is `()` (void). During body lowering of an unannotated closure the expected return type is left unset so `return X;` is not strictly type-checked against it.

```logos
let f = |x: i32| { if x > 0 { return 1; } 2 };
```

*Source:* `src/compiler/sema_expr.cpp#L14229-L14231`, `src/compiler/sema_expr.cpp#L14275-L14277`, `src/compiler/sema_expr.cpp#L14340-L14386`

### `expr.closure.body-is-drop-boundary` — Closure body scope is a drop boundary

A closure body is lowered in its own scope that is a drop boundary: a `return` inside the body drops only the closure's own frames, not the enclosing function's locals captured by the closure (those are owned by their original bindings or borrowed by the env).

*Source:* `src/compiler/sema_expr.cpp#L14243-L14247`, `src/compiler/sema_expr.cpp#L14334-L14338`

### `expr.closure.body-own-unsafe-scope` — Closure body does not inherit enclosing unsafe context

A closure body is lowered as its own scope and does not inherit the enclosing `unsafe` context; the inside-unsafe state is reset to false for the body and restored afterward.

*Source:* `src/compiler/sema_expr.cpp#L14274-L14278`, `src/compiler/sema_expr.cpp#L14332-L14333`

### `expr.closure.capture-by-free-variable` — Closures capture free variables resolving in an enclosing scope

A closure captures exactly those names used in its body that are not its own parameters and that resolve to a binding in an enclosing scope; each captured name's type is the enclosing binding's type.

*Source:* `src/compiler/sema_expr.cpp#L14388-L14432`, `src/compiler/sema_expr.cpp#L14421-L14432`

### `expr.closure.mutated-capture-by-reference` — Mutated captures are captured by reference

A captured variable that is the target of a mutation in the body (assignment / field write / index write / deref write) is captured by reference so the mutation propagates to the outer binding rather than to a local env copy. A write-only target (no prior read of its base) is still added to the capture set as a whole-variable capture.

*Divergence:* Capture mode is inferred per-variable from usage (read-only vs mutated), conceptually aligned with Rust closure capture-mode inference.

*Source:* `src/compiler/sema_expr.cpp#L14395-L14420`

### `expr.closure.disjoint-field-capture` — Closures capture disjoint fields (RFC-2229)

When a closure body reads a precise dotted field path `root.x.y` rooted at a captured variable, the capture is recorded at that path; multiple paths off the same root are widened to their lowest common ancestor segment (`lca("p.x","p.y")="p"`, widening to a larger/less precise borrow which is sound). The capture's slot is sized at the leaf field type when the path walks entirely through plain `Struct` fields; otherwise the whole root is captured. Paths are extracted only when the head is a plain variable reference followed by field reads (indexing or deref-through-box falls back to whole-variable capture).

```logos
let g = |p: &Pt| { use(p.x); use(p.y); };
```

*Source:* `src/compiler/sema_expr.cpp#L14433-L14528`, `src/compiler/sema_expr.cpp#L14455-L14482`, `src/compiler/sema_expr.cpp#L14486-L14506`

### `expr.closure.capture-free-vars` — Closure captures the free variables referenced in its body

A closure literal captures exactly the set of variables from the enclosing scope that its body references (transitively through every expression and statement form), excluding the closure's own parameters and variables bound locally inside the body. A bare variable reference `x` captures the whole root `x`.

*Source:* `src/compiler/sema_expr.cpp#L14539-L14546`, `src/compiler/sema_expr.cpp#L14691-L14773`, `src/compiler/sema_expr.cpp#L14801`

### `expr.closure.capture-borrow-of-var` — Taking the address of a variable in a closure body captures it

`&x` or `&mut x` appearing in a closure body captures the whole root variable `x` from the enclosing scope, just as a plain read would.

*Source:* `src/compiler/sema_expr.cpp#L14584-L14587`

### `expr.closure.capture-by-ref-on-mutation` — Mutating a captured variable forces by-reference capture

A captured variable that the closure body mutates is captured by reference. Mutation includes: assignment to the variable, field writes / multi-level (chained) field writes through it, indexed writes into it, and an auto `&mut` of the variable produced as a method receiver. A by-value capture of a mutated variable would lose the write.

*Source:* `src/compiler/sema_expr.cpp#L14594-L14602`, `src/compiler/sema_expr.cpp#L14699-L14704`, `src/compiler/sema_expr.cpp#L14724-L14746`

### `expr.closure.capture-disjoint-fields` — Disjoint closure capture by precise field path (RFC 2229)

When a closure body accesses fields of a variable through a pure `root.field*` dotted chain, the capture is the precise path rather than the whole root; multiple paths off the same root are widened to their lowest-common-ancestor path. If the access head is not a pure VarRef/FieldRead chain (e.g. `(*box).x`), the whole root is captured instead.

*Related:* `expr.closure.capture-free-vars`

*Source:* `src/compiler/sema_expr.cpp#L14563-L14569`, `src/compiler/sema_expr.cpp#L14805`

### `expr.closure.nested-transitive-capture` — Outer closure transitively captures a nested closure's free vars

A closure literal nested in another closure's body causes the outer closure to capture the nested closure's free variables. If the nested closure captures a variable by reference (mutates it), the outer closure must also capture that variable by reference; otherwise the nested write would target the outer's by-value copy and be lost.

*Related:* `expr.closure.capture-by-ref-on-mutation`

*Source:* `src/compiler/sema_expr.cpp#L14640-L14656`

### `expr.closure.writ-capture-exprs` — Writ literal $-captures count as closure captures

Variables referenced via `$`-capture expressions inside a Writ literal in a closure body are captured by the enclosing closure.

*Related:* `expr.closure.capture-free-vars`

*Source:* `src/compiler/sema_expr.cpp#L14681-L14687`

### `expr.closure.move-marks-moved` — move closure consumes its move-type captures at the capture site

In a `move` closure, each captured variable (or, for an escaping narrow capture, the captured field path) whose type is a move type is marked moved at the closure site, making subsequent use of that variable/path a use-after-move error. Copy-type captures are not consumed.

*Source:* `src/compiler/sema_expr.cpp#L14811-L14848`

### `expr.closure.escaping-env-owns-captures` — Escaping move closure owns droppable captures in its environment

An escaping (heap-environment / boxed) `move` closure that captures a droppable struct/array/tuple/enum moves it into the closure environment by value; the environment's drop glue drops it, so the originating scope does not. A non-escaping (stack-environment) `move` closure borrows the source storage, so the source scope still drops the value unless the body itself already moved the capture onward.

*Related:* `expr.closure.boxing-escapes`

*Source:* `src/compiler/sema_expr.cpp#L14855-L14888`

### `expr.closure.boxing-escapes` — A closure assigned to a Box&lt;...Fn...&gt; escapes

A closure lowered against an expected type that peels (through a Box / struct wrapper) to a callable Fn type is treated as escaping: its captured environment lives on the heap. A bare or reference-wrapped Fn expectation (e.g. an iterator-adapter argument) does not escape and keeps a stack environment.

*Source:* `src/compiler/sema_expr.cpp#L14787-L14793`

### `expr.closure.narrow-move-requires-escape` — Narrow (field) move capture applies only to escaping closures; user Drop on root forces whole-var

RFC-2229 narrow move capture (moving only a field path, leaving sibling fields usable) applies only when the closure escapes; a non-escaping narrow capture moves nothing and the root keeps ownership. However, a `move` closure capturing a path whose root type has a user `impl Drop` captures the whole variable (so the value drops with the closure); mere drop glue from droppable fields keeps disjoint capture.

*Related:* `expr.closure.capture-disjoint-fields`, `expr.closure.escaping-env-owns-captures`

*Source:* `src/compiler/sema_expr.cpp#L14820-L14854`

### `expr.closure.capture-drop-order` — Source-scope-dropped captures drop with the closure in capture order

Captures whose destructor the source scope still runs are dropped at the closure binding's slot in capture order, not at their own variable-order slots, matching Rust's closure capture drop order.

*Source:* `src/compiler/sema_expr.cpp#L14892-L14897`

### `expr.closure.infer-param-types-from-call-site` — Untyped closure parameters infer their types from the call-site's expected formal

A closure literal with untyped parameters (`|x| body`, no `: T` annotation) has its parameter types inferred from the corresponding call-site formal parameter's `fn(T,...) -> R` / Closure type, when the call-site path (`lower_call` / `lower_method_call`) supplies such a hint for that argument position.

*Source:* `src/compiler/sema_impl.hpp#L3720-L3724`

### `expr.closure.infer-params-from-fn-bound` — Untyped closure literal infers parameter types from an Fn-family bound

An untyped closure literal (`|x| ..`) appearing where an Fn-family-bounded type-parameter is expected (a method formal `F: FnMut(..)`, or a generic struct field `f: F`) has its parameter and return types synthesized as a Closure type from that bound's signature, with the ambient substitution (SemaSubst) applied to the bound's param/return types. No inference occurs (null) if the declared TypeVar is not Fn-bounded.

*Source:* `src/compiler/sema_impl.hpp#L3990-L3999`

### `expr.closure.infer-through-wrapped-callable` — Closure-literal Fn-bound inference peels through Ref/MutRef and single-arg generic wrappers

When the expected type for an untyped closure literal is not itself a bare Fn-bounded TypeVar but a Ref/MutRef or a single-type-arg generic wrapper around a callable (e.g. `Box<dyn Fn(..)>`), the compiler first peels the reference and the wrapper to expose the inner Closure/FnPtr signature, then applies Fn-bound parameter inference (rule expr.closure.infer-params-from-fn-bound) against that inner signature — e.g. inferring closure param types for `box_new(|x| ..)` where the enclosing fn's return type is `Box<dyn Fn(..)>`.

*Related:* `expr.closure.infer-params-from-fn-bound`

*Source:* `src/compiler/sema_impl.hpp#L4000-L4005`

## Comprehensions (`expr.comprehension`)

### `expr.comprehension.list-and-map` — List and map comprehension expressions

`[elem for x in iter (if guard)?]` (LIST_COMP: VALUE=elem, NAME=x, ITER=iter, GUARD?=guard) and `{k: v for x in iter (if guard)?}` (MAP_COMP: adds KEY=k, VALUE=v) are expression-position comprehensions that iterate `iter`, bind `x` per element, optionally filter by `guard`, and produce a new list or map respectively from the (filtered) elements.

*Divergence:* Logos addition: Python-style comprehensions; not present in Rust.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L247-L248`, `tools/peg_gen_cpp/grammars/logos.peg#L2875-L2885`

### `expr.comprehension.list` — List comprehension

`[expr for x in iter if pred]` and `[expr for x in iter]` (guard optional) produce a list by iterating `iter`, binding `x`, and — if present — filtering by `pred`.

*Divergence:* A6 — Logos addition: Python-style comprehension syntax, not present in Rust.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2936-L2939`

## List comprehensions (`expr.list-comp`)

### `expr.list-comp.desugar-vec` — List comprehension desugars to Vec build loop

A list comprehension `[value for x in iter (if guard)?]` desugars to a block that binds `let mut v: Vec<T> = vec_new::<T>()`, iterates `x` over `iter`, (optionally gated by `guard`) calls `Vec::push(&mut v, value)`, and evaluates to `v`. T is the iterator element type; the block's type is `Vec<T>`.

*Divergence:* Logos-specific surface syntax (Python-style comprehension); not present in Rust.

*Source:* `src/compiler/sema_expr.cpp#L10885-L10986`

### `expr.list-comp.iter-array-or-slice-only` — Comprehension iterables restricted to array/slice

The iterable of any comprehension form must have type `[T; N]` (array) or `[T]` (slice); any other iterator type is rejected. Element type defaults to i32 when the array/slice element type is absent.

*Divergence:* Narrower than Rust: only concrete array/slice, no IntoIterator/Iterator protocol.

*Note:* i32 default for missing elem type is a fallback; normally elem type is always present.

*Source:* `src/compiler/sema_expr.cpp#L10896-L10907`, `src/compiler/sema_expr.cpp#L11002-L11013`, `src/compiler/sema_expr.cpp#L11112-L11123`, `src/compiler/sema_expr.cpp#L11245-L11256`

### `expr.list-comp.requires-vec-import` — List comprehension requires Vec in scope

A list comprehension is ill-formed unless the `Vec` struct and the generic `vec_new` function are visible (via `use logos.mem.collections.vec;`).

*Divergence:* Logos-specific: surface sugar depends on a stdlib import being present.

*Source:* `src/compiler/sema_expr.cpp#L10909-L10921`

### `expr.list-comp.bind-scope` — Comprehension binds the loop variable in value/guard scope

The loop variable `x` is bound (immutable, element type) in a new scope covering the value/key expressions and the guard; it is not visible outside the comprehension.

*Source:* `src/compiler/sema_expr.cpp#L10939-L10946`, `src/compiler/sema_expr.cpp#L11030-L11037`, `src/compiler/sema_expr.cpp#L11142-L11149`, `src/compiler/sema_expr.cpp#L11275-L11283`

## Map comprehensions (`expr.map-comp`)

### `expr.map-comp.desugar-hashmap` — Map comprehension desugars to HashMap build loop

A map comprehension `{key: value for x in iter (if guard)?}` desugars to a block that binds `let mut m: HashMap<K,V> = hashmap_new::<K,V>()`, iterates `x` over `iter`, (optionally gated by `guard`) calls `HashMap::insert(&mut m, key, value)`, and evaluates to `m`. K = type of `key`, V = type of `value`; block type is `HashMap<K,V>`.

*Divergence:* Logos-specific surface syntax; not present in Rust.

*Source:* `src/compiler/sema_expr.cpp#L10992-L11090`

### `expr.map-comp.requires-hashmap-import` — Map comprehension requires HashMap in scope

A map comprehension is ill-formed unless the `HashMap` struct and the generic `hashmap_new` function are visible (via `use logos.mem.collections.hashmap;`).

*Divergence:* Logos-specific.

*Source:* `src/compiler/sema_expr.cpp#L11015-L11026`

## Formatting (`expr.fmt`)

### `expr.fmt.brace-escape` — Doubled braces escape a literal brace

In a format string, `{{` denotes a literal `{` and `}}` denotes a literal `}`; each doubled brace contributes exactly one brace to the literal output and is not treated as a placeholder delimiter.

*Source:* `src/compiler/sema_fmt.cpp#L121-L134`

### `expr.fmt.unmatched-close-brace` — Unescaped `}` is an error

A `}` that is not part of a `}}` escape and does not close a placeholder is a compile error (`unmatched `}``); use `}}` to emit a literal `}`.

*Source:* `src/compiler/sema_fmt.cpp#L135-L142`

### `expr.fmt.unmatched-open-brace` — Unterminated placeholder is an error

A `{` opening a placeholder must be closed by a matching `}`; if the placeholder body ends without `}`, it is a compile error (`unmatched `{``).

*Source:* `src/compiler/sema_fmt.cpp#L259-L265`

### `expr.fmt.placeholder-syntax` — Placeholder grammar

A placeholder has form `{` arg_id? (`:` format_spec)? `}` where arg_id is either an unsigned integer (explicit positional index) or an identifier (named argument); absence of arg_id means the next implicit positional argument.

*Source:* `src/compiler/sema_fmt.cpp#L148-L170`

### `expr.fmt.implicit-positional-counter` — Implicit positional argument assignment

Placeholders without an explicit arg_id are assigned consecutive positional indices starting at 0, incremented per implicit placeholder; explicit-index and named placeholders do not advance this counter.

*Source:* `src/compiler/sema_fmt.cpp#L166-L169`

### `expr.fmt.arg-id-kind` — Explicit-index vs named argument id

If the first arg_id char is a digit it is parsed as an explicit positional index; if it is an alphabetic char or `_` it is parsed as a named-argument identifier ([A-Za-z_][A-Za-z0-9_]*).

*Source:* `src/compiler/sema_fmt.cpp#L75-L89`, `src/compiler/sema_fmt.cpp#L157-L165`

### `expr.fmt.spec-field-order` — Format spec field ordering

After `:` the format spec fields appear in fixed order: (fill align)? sign? `#`? `0`? width? (`.` precision)? type? where align in {`<`,`>`,`^`}, sign in {`+`,`-`}, width and precision are unsigned integers, and type is a single char.

*Source:* `src/compiler/sema_fmt.cpp#L172-L256`

### `expr.fmt.fill-align` — Fill+align detection

A fill character is recognized only when immediately followed by an alignment marker (`<`,`>`,`^`), forming a 2-char fill+align prefix; a bare alignment marker uses the default fill; `<`=Left, `>`=Right, `^`=Center.

*Source:* `src/compiler/sema_fmt.cpp#L176-L196`

### `expr.fmt.precision-requires-number` — Precision dot requires a number

A `.` in the format spec must be followed by an unsigned-integer precision; a `.` not followed by a digit is a compile error.

*Divergence:* Rust additionally permits `.*` and `.N$` precision forms; Logos here requires a literal number after `.`.

*Source:* `src/compiler/sema_fmt.cpp#L224-L235`

### `expr.fmt.type-char-set` — Format type chars select a formatting trait

The type char selects the formatting trait: `?`=Debug, `x`=LowerHex, `X`=UpperHex, `o`=Octal, `b`=Binary, `e`=LowerExp, `E`=UpperExp; absence means Display; any other char before `}` is a compile error (`unknown type char`).

*Source:* `src/compiler/sema_fmt.cpp#L237-L256`, `src/compiler/sema_fmt.cpp#L43-L55`

## Formatting macros (`expr.format`)

### `expr.format.arg-widen-to-i64` — format() arguments are widened to i64 with a type tag

The `format()` built-in packs each variadic argument into parallel stack arrays: an i32 type-tag array and an i64 data array. Each argument is widened to i64: pointer-typed values via ptrtoint; unsigned integer types narrower than 64 bits via zero-extension; other integer types via the general (sign-preserving) int coercion. The type tag records enough of the original type's class (i32/i64/ptr-or-slice/bool/u8/u32/u64/i8) for runtime formatting dispatch — narrower integer kinds (i16/u16/i24/i56/u24/u56/i128/u128/IntLit) each map onto the tag of their same-signedness ≥32-bit dispatch class.

*Source:* `src/compiler/mlir_gen_expr.cpp#L5330-L5353`, `src/compiler/mlir_gen_expr.cpp#L5373-L5413`

### `expr.format.requires-text-import` — format() requires std.lang.text to be imported

The `format()` built-in lowers to a call to the runtime symbol `__format_impl`; if that symbol is not present in the module (i.e. `use std.lang.text;` was not imported), codegen fails for the expression (diagnostic to stderr, null result).

*Source:* `src/compiler/mlir_gen_expr.cpp#L5416-L5426`

## Drop (`expr.drop`)

### `expr.drop.dynamic-flag` — Dynamic drop flag for conditionally-initialized variables

A `let mut x: T;` declared without an initializer whose initialization is not statically determinable (an assignment nested inside a conditional/loop deeper than its declaration) gets a hidden runtime i8 drop flag (0 = empty, 1 = live). Each assignment drops the old value only if the flag is set then sets it; scope-exit/return drops only if the flag is set. Variables whose every assignment is straight-line (statically dominates its uses, determined by a pre-scan of the fn body) are flag-free: drops are placed statically instead, matching Rust's MIR drop elaboration for the common case.

*Source:* `src/compiler/mlir_gen_impl.hpp#L332-L353`

### `expr.drop.ref-ptr-noop` — References and raw pointers are never dropped

Dropping a value of kind &T, &mut T, or *T (Ref/MutRef/Ptr) is a no-op: a reference/pointer does not own its referent, so dropping it runs no destructor and frees nothing. This also holds for fields/elements of those kinds during recursive drop.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L845`, `src/compiler/mlir_gen_stmt.cpp#L708`, `src/compiler/mlir_gen_stmt.cpp#L908`, `src/compiler/mlir_gen_stmt.cpp#L929`, `src/compiler/mlir_gen_stmt.cpp#L976`

### `expr.drop.owning-box-dst` — Drop of an owning custom-DST box (Box&lt;Foo&gt; with [T] tail)

Dropping an owning custom-DST handle (Box&lt;Foo&gt; where Foo = {prefix fields..., [T] tail}) over a non-null data pointer: (1) drop each droppable prefix field (in declaration order, skipping ref/ptr fields and fields that don't need drop), (2) drop the tail's elements over the runtime length len at element stride layout_of(T).size, then (3) free the whole heap block. A null data pointer (a moved-from handle) drops nothing and frees nothing.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L658-L753`, `src/compiler/mlir_gen_stmt.cpp#L680-L689`, `src/compiler/mlir_gen_stmt.cpp#L704-L743`, `src/compiler/mlir_gen_stmt.cpp#L750`

### `expr.drop.owning-box-slice` — Drop of an owning Box&lt;[T]&gt; fat slice

Dropping an owning Box&lt;[T]&gt; ({data,len} fat slice) over a non-null data pointer: if T is droppable, drop each element i in [0,len) at data + i*stride (stride = layout_of(T).size, min 1), then free the heap buffer; if T is not droppable, only free the buffer. A null data pointer (moved-from) is a no-op.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L755-L817`, `src/compiler/mlir_gen_stmt.cpp#L768-L771`, `src/compiler/mlir_gen_stmt.cpp#L781-L815`

### `expr.drop.owning-box-dyn` — Drop of an owning Box&lt;dyn Trait&gt; fat handle is uniform across storage sites

An owning trait-object handle (inline {data,vtable} fat pair, e.g. Box&lt;dyn&gt;/Rc&lt;dyn&gt;/Arc&lt;dyn&gt;) drops by running vtable[0] (drop_in_place) on data followed by the kind-specific release (Box: free data; Rc/Arc: decrement strong count, free at last reference). This drop is uniform across every storage site — local, struct field, return temp, Vec/tuple/array element — reached via ordinary aggregate field recursion, not only a top-level local.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L846-L855`, `src/compiler/mlir_gen_stmt.cpp#L1049-L1052`

### `expr.drop.struct-user-drop-then-fields` — Struct drop: user Drop runs first, then field recursion governed by ownership

Dropping a struct/zoned-struct value first calls its user `impl Drop` (if one exists) which owns the value. A nested (non-top-level) struct then STOPS — the by-value self of the user drop already consumed the fields, so recursing them would double-drop. A top-level owner, or a struct with NO user Drop, recurses its droppable fields in REVERSE declaration order (skipping ref/ptr/non-droppable fields and statically moved-out field paths).

*Source:* `src/compiler/mlir_gen_stmt.cpp#L880-L920`, `src/compiler/mlir_gen_stmt.cpp#L891-L897`, `src/compiler/mlir_gen_stmt.cpp#L905-L918`

### `expr.drop.enum-user-drop-then-variant` — Enum drop: user Drop runs first, else variant-switched payload recursion

Dropping an enum value first calls its user `impl Drop` if a drop symbol actually exists (a by-value self that consumes the payload; nested enums then stop). Absent a real user Drop, drop switches on the loaded discriminant and, for each variant carrying a droppable payload field, recurses into that field. Variants whose payload needs no drop emit no work; a wholly drop-less enum drops nothing.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L939-L983`, `src/compiler/mlir_gen_stmt.cpp#L946-L950`, `src/compiler/mlir_gen_stmt.cpp#L951-L982`

### `expr.drop.tuple-array-reverse` — Tuple and array element drop in reverse order

Dropping a tuple drops its droppable elements in reverse index order; dropping a fixed array [T;N] drops each of the N elements when T is droppable. Ref/ptr elements and non-droppable elements are skipped, and statically moved-out tuple element positions are suppressed.

*Divergence:* Rust drops array elements in forward (index-ascending) order; tuple reverse-order is conformant. Array order here is N forward but element-by-element; flagged as possibly observable only via Drop side effects.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L922-L938`, `src/compiler/mlir_gen_stmt.cpp#L985-L995`

### `expr.drop.closure-env-glue` — Closure drop runs the captured environment's drop glue

Dropping a closure value ({fn, env} 16-byte handle) loads env = handle[1]; if env != null, loads glue = env[0]; if glue != null, calls glue(env). A non-owning closure has a null env (or null glue) so its drop is a guarded no-op. Closures are not auto-recursed via the needs-drop predicate; their drop is driven explicitly.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L868-L869`, `src/compiler/mlir_gen_stmt.cpp#L996-L1034`

### `expr.drop.skip-moved-paths` — Moved-out fields/elements are suppressed during scope drop

Scope-end drop of an owning aggregate suppresses sub-values that were moved out, identified by dotted field/element paths. An exact path ("f" or "i") skips the whole field/element; a deeper path ("f.g") recurses but suppresses only the moved leaf, so its siblings still drop. This prevents double-free of a value already moved elsewhere.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L823-L838`, `src/compiler/mlir_gen_stmt.cpp#L1107-L1159`, `src/compiler/mlir_gen_stmt.cpp#L910-L918`

### `expr.drop.flag-uninit-conditional` — Conditionally/late-initialized variables drop only when live

A variable that may be uninitialized at a drop point runs its destructor only if it currently holds a live value. With dynamic tracking a per-variable drop flag (0/1) is consulted at runtime (flag==1 → drop, else no-op). With static tracking the destructor is emitted only when the variable is statically known to be assigned at that point; an early return before first assignment, the !c arm of a conditional init, or a never-assigned variable drops nothing.

*Divergence:* Logos drop flags / static drop tracking (B8). Models Rust's conditional drop flags.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L1184-L1214`

### `expr.drop.scope-order-user-then-children` — Scope drop runs the variable's own Drop before recursing its children

At scope end a variable's own user drop function (if any) is invoked first, then its owned sub-values (struct fields, tuple elements, enum payload, array elements, owning slice/DST/closure) are recursively dropped. Children moved out are skipped (see expr.drop.skip-moved-paths). A moved-out unsized `dyn` tail runs only the concrete Drop via vtable[0](data) with NO free (the enclosing block is freed separately).

*Source:* `src/compiler/mlir_gen_stmt.cpp#L1053-L1099`, `src/compiler/mlir_gen_stmt.cpp#L1101-L1181`

## `unsafe` (`expr.unsafe`)

### `expr.unsafe.block` — Unsafe block

`unsafe { ... }` is an unsafe block whose body is an ordinary block.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L1886-L1887`

### `expr.unsafe.block-in-expr-position` — unsafe block as expression

An `unsafe { ... }` block may appear in expression position (e.g. as a let initializer).

*Source:* `src/compiler/sema_render.cpp#L538-L542`

## `unsafe` blocks (`expr.unsafe-block`)

### `expr.unsafe-block.tail-value` — unsafe block in expression position yields its tail value

An `unsafe { ... }` in expression position evaluates its statements with unsafe permitted and yields the trailing expression's value (not an implicit early return); with no trailing expression it has type `()`.

*Source:* `src/compiler/sema_expr.cpp#L1566-L1600`

## Writ expressions (`expr.writ`)

### `expr.writ.sdn-literal` — Writ SDN literals

Writ structured-data literals use the `@` sigil: `@{k:v,…}` map, `@[v,…]` array, `@"s"` string, `@42`/`@-1` int, `@<float>` float, `@true`/`@false` bool, `@null`. Typed forms `@<Elem>[…]` (dense array) and `@<K,V>{…}` / `@<K>{…}` (typed map). Comprehension forms `@[expr for x in iter (if p)?]` and `@{k:v for …}`. Only the outermost literal needs the `@` sigil; inner values are plain.

*Divergence:* Logos addition: Writ self-describing data-notation literals.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2887-L2923`

### `expr.writ.type-literal` — Writ type-literal &lt;type:T&gt;

A Writ value `<type:T>` embeds a Logos type T as a first-class value. T is resolved as a type (primitives, structs, in-scope type-params, and generic instantiations like Vec&lt;u8&gt; all permitted). The value carries (kind, type-uid, canonical-name) where the name is the canonical printed form (e.g. "Vec&lt;u8&gt;") and serves as the value's identity label.

*Divergence:* Logos addition: Writ first-class type values have no Rust equivalent.

*Source:* `src/compiler/sema_expr.cpp#L14937-L14979`

### `expr.writ.type-literal-unknown-bare` — Bare type-name in &lt;type:T&gt; must be a known type or in-scope type-param

When `<type:T>` names a bare type identifier that is neither a resolvable known type nor an in-scope type-param, it is a compile error; the diagnostic directs the user to declare T as a type-param of the enclosing const (`pub const X<T>: WritStatic = ...`) or use a concrete type.

*Divergence:* Logos addition (Writ type literals).

*Source:* `src/compiler/sema_expr.cpp#L14954-L14966`

### `expr.writ.cfg-slot-type-literal` — &lt;type:CFG.path&gt; at writ-value position

`<type:CFG.path>` resolves the config path eagerly and must denote a concrete top-level alias; if it resolves to a const-generic config-slot parameter (kind CfgSlotType) it is rejected with a compile error (parametric Writ literals are not supported).

*Divergence:* Logos addition (Writ/CFG type literals).

*Note:* Restriction is stated as a current limitation in the source.

*Source:* `src/compiler/sema_expr.cpp#L14982-L15009`

### `expr.writ.neg-int` — Writ negative integer literal

A Writ negative-integer node yields an integer Writ value equal to the negation of the parsed decimal magnitude.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15012-L15016`

### `expr.writ.null` — Writ null literal

A Writ null node yields the null Writ value.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15018-L15019`

### `expr.writ.bool` — Writ bool literal

A Writ bool node yields a boolean Writ value; the value is true iff its byte payload is present and nonzero.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15021-L15025`

### `expr.writ.int-suffix-and-radix` — Writ integer literal: suffix stripping and radix

A Writ integer literal accepts an optional numeric-type suffix (i8/i16/i24/i32/i56/i64/i128, u8/u16/u24/u32/u56/u64/u128, usize, isize) which is stripped before parsing, an optional leading '-', and a radix prefix: `0x` = hexadecimal, `0b` = binary, otherwise decimal. The resulting magnitude is negated if the sign was present.

*Divergence:* Logos addition (Writ literals); note i24/i56/u24/u56 width suffixes.

*Source:* `src/compiler/sema_expr.cpp#L15027-L15050`

### `expr.writ.float-suffix` — Writ float literal: suffix stripping

A Writ float literal accepts an optional `f32` or `f64` suffix which is stripped before parsing the value as a double-precision float.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15052-L15060`

### `expr.writ.string-escapes` — Writ string literal: quote stripping and escapes

A Writ string literal has surrounding double-quotes stripped and recognizes escape sequences \n, \t, \r, \\, \", \0; an unrecognized escape `\x` is kept literally as backslash followed by x.

*Divergence:* Logos addition (Writ literals); escape set is a fixed subset.

*Source:* `src/compiler/sema_expr.cpp#L15062-L15086`

### `expr.writ.map-keys` — Writ map literal keys (string or integer)

An untyped Writ map `@{...}` has entries whose key is either a quoted string (quote-stripped and escape-processed like a Writ string) or an integer; an integer key is negated when the entry carries the negative-key marker. Values are recursively lowered Writ values.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15088-L15129`

### `expr.writ.array` — Writ untyped array literal

An untyped Writ array `@[...]` lowers each element as a recursive Writ value in order.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15131-L15143`

### `expr.writ.typed-array-elem-types` — Typed Writ array element types

A typed Writ array `@<E>[...]` requires E to be one of I8, U8, I16, U16, I32, U32, I64, U64, F32, F64; any other element type is a compile error.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15145-L15168`

### `expr.writ.typed-array-no-captures` — Typed Writ arrays reject $-captures

Within a typed Writ array `@<E>[...]`, a $-capture element ($ident or $expr) is a compile error because typed arrays store raw element values rather than AnyVal; an untyped `@[...]` literal must be used instead.

*Divergence:* Logos addition (Writ literals/captures).

*Source:* `src/compiler/sema_expr.cpp#L15174-L15187`

### `expr.writ.typed-array-i32-bounds` — @&lt;I32&gt; array element range check

Each integer element of an `@<I32>[...]` typed array is bounds-checked at compile time to the i32 range [-2147483648, 2147483647]; out-of-range values are a compile error.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15190-L15203`

### `expr.writ.typed-map-types` — Typed Writ map key/value types

A typed Writ map `@<K>{...}` or `@<K,V>{...}` requires K ∈ {I32, U32, I64, U64, Varchar} and, if V is given, V == AnyVal; any other key or value type is a compile error. Varchar keys produce the same representation as the untyped object map.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15209-L15252`

### `expr.writ.typed-map-key-discipline` — Typed integer-map key discipline

In a typed integer-keyed Writ map, a string key is a compile error (integer maps require integer keys); integer keys are negated when marked negative, and are bounds/sign-checked per key type: I32 to [-2^31, 2^31-1], U32 to [0, 2^32-1], U64 to non-negative.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15255-L15311`

### `expr.writ.capture-outside-context` — $-capture only inside capturable @-literal

A $-capture ($ident or $expr) in a Writ value is a compile error unless it occurs inside a capturable @-literal context.

*Divergence:* Logos addition (Writ captures).

*Source:* `src/compiler/sema_expr.cpp#L15319-L15323`

### `expr.writ.capturable-types` — Types capturable by $-capture into a Writ value

A captured Logos expression is admissible into a Writ @-literal iff its type is: a scalar integer (i8/i16/i32/i64/u8/u16/u32/u64) or bool (coerced to inline AnyVal); F32/F64/float-literal (zone-allocated F64); AnyVal or a string-view struct; a pointer to u8 (*const u8 / *mut u8, captured as C-string varchar); or a u8 slice (str/&[u8], captured as varchar with length). Other types are not capturable.

*Divergence:* Logos addition (Writ captures).

*Source:* `src/compiler/sema_expr.cpp#L15325-L15350`

### `expr.writ.capture-not-standalone` — $-capture is not a standalone expression

A `$`-capture node (WRIT_CAP_IDENT / WRIT_CAP_EXPR) is only valid nested inside a writ value literal; appearing as a standalone expression is an error.

*Source:* `src/compiler/sema_expr.cpp#L1506-L1511`

### `expr.writ.outer-at-prefix` — Writ literal outer `@` prefix

Writ (data) literals in expression position are introduced with a leading `@`: `@null`, `@true`/`@false`, `@INT`, `@-INT`, `@FLOAT`, `@"str"`, `@{ ... }` (map), `@[ ... ]` (array).

*Divergence:* Logos-specific Writ data-literal syntax; no Rust equivalent.

*Source:* `src/compiler/sema_render.cpp#L463-L509`

### `expr.writ.map-entry-colon` — Writ map entry syntax

A Writ map literal `@{ ... }` contains comma-separated entries `key: value`; nested scalar values omit the `@` prefix in inner position.

*Divergence:* Logos-specific Writ syntax.

*Source:* `src/compiler/sema_render.cpp#L479-L497`

### `expr.writ.embedded-type-lit` — Embedded type in Writ literal

A Logos type can be embedded inside a Writ literal as `<type:T>`.

*Divergence:* Logos-specific Writ syntax.

*Source:* `src/compiler/sema_render.cpp#L510-L516`

### `expr.writ.cfg-slot-type` — WritStatic const-generic slot type

A slot of a WritStatic-typed const-generic is referenced as `<type:CFG.slot.path>` with dot-separated step names.

*Divergence:* Logos-specific const-generic/Writ syntax.

*Source:* `src/compiler/sema_render.cpp#L517-L531`

## Writ capture (`expr.writ-capture`)

### `expr.writ-capture.context-required` — $-capture requires a capturable @-literal context

A `$ident` or `${expr}` capture node is only valid lexically inside a capturable @-literal (Writ) context; using one elsewhere is an error.

*Source:* `src/compiler/sema_expr.cpp#L15319-L15323`

### `expr.writ-capture.capturable-types` — Set of types capturable in an @-literal

A value may be captured into an @-literal iff its type is one of: integer scalars i8/i16/i32/i64/u8/u16/u32/u64, bool (→ inline AnyVal); f64/f32/FloatLit (→ zone-allocated F64, type_code 31); AnyVal (passthrough) or StringView (→ varchar) struct types; `*const u8`/`*mut u8` (→ C-string varchar); or `str`/`&[u8]` slice of u8 (→ length-bearing varchar). All other types are rejected.

*Divergence:* Logos addition: @-literal (Writ) capture has no Rust analogue.

*Source:* `src/compiler/sema_expr.cpp#L15325-L15350`, `src/compiler/sema_expr.cpp#L15360-L15367`, `src/compiler/sema_expr.cpp#L15387-L15394`

### `expr.writ-capture.ident-lookup` — $ident capture resolves a variable by name

A `$ident` capture (WRIT_CAP_IDENT) resolves `ident` against the enclosing scope; an unknown variable is an error.

*Source:* `src/compiler/sema_expr.cpp#L15352-L15359`

### `expr.writ-capture.ident-dedup` — Identical $ident captures share one value slot

Two `$ident` captures of the same identifier name reuse the same capture value index (deduplicated), while each occurrence consumes a distinct parameter slot.

*Source:* `src/compiler/sema_expr.cpp#L15368-L15380`

### `expr.writ-capture.expr-no-dedup` — ${expr} captures are never deduplicated

A `${expr}` capture (WRIT_CAP_EXPR) lowers its inner expression and always allocates a fresh capture value index (no deduplication, since the expression may have side effects).

*Source:* `src/compiler/sema_expr.cpp#L15381-L15399`

## Writ comprehensions (`expr.writ-comp`)

### `expr.writ-comp.guard-must-be-bool` — Writ comprehension guard must be bool

In a writ list/map comprehension the `guard` expression must have type `bool`; any other type is rejected (errors on Error type are swallowed to avoid cascades).

*Source:* `src/compiler/sema_expr.cpp#L11158-L11172`, `src/compiler/sema_expr.cpp#L11305-L11318`

## Writ list comprehensions (`expr.writ-list-comp`)

### `expr.writ-list-comp.desugar` — Writ list comprehension desugars to a Writ array builder loop

A writ list comprehension `@[value for x in iter (if guard)?]` desugars to a block that binds `let mut c = writ_list_comp_new(cap_hint)` (yielding the builder's return type, e.g. Rc&lt;Writ&gt;), iterates `x` over `iter`, coerces `value` to AnyVal, (optionally gated by `guard`) calls `writ_list_comp_push(&c, value)`, and evaluates to `c`. cap_hint = arr_size*8+128 for arrays of known size, else 128.

*Divergence:* Logos-specific Writ data-substrate sugar; no Rust equivalent.

*Source:* `src/compiler/sema_expr.cpp#L11098-L11226`

### `expr.writ-list-comp.requires-builder-import` — Writ list comprehension requires comp_builder import

A writ list comprehension is ill-formed unless arity-1 `writ_list_comp_new` and arity-2 `writ_list_comp_push` are visible (via `use logos.lang.writ.comp_builder;`).

*Divergence:* Logos-specific.

*Source:* `src/compiler/sema_expr.cpp#L11125-L11135`

## Writ map comprehensions (`expr.writ-map-comp`)

### `expr.writ-map-comp.desugar` — Writ map comprehension desugars to a Writ object-map builder loop

A writ map comprehension `@{key: value for x in iter (if guard)?}` desugars to a block that binds `let mut c = writ_map_comp_new(cap_hint, slot_hint)`, iterates `x` over `iter`, coerces `value` to AnyVal, (optionally gated by `guard`) calls `writ_map_comp_put(&c, key, value)`, and evaluates to `c`. slot_hint = arr_size (else 64); cap_hint = arr_size*48+256 (else 4096).

*Divergence:* Logos-specific Writ sugar; no Rust equivalent.

*Source:* `src/compiler/sema_expr.cpp#L11231-L11375`

### `expr.writ-map-comp.key-must-be-str` — Writ map comprehension key must be str

In a writ map comprehension v1 the `key` expression must have type `str` (a `&[u8]` slice with u8 element); any other key type is rejected.

*Divergence:* Logos-specific (v1 limitation: string keys only).

*Source:* `src/compiler/sema_expr.cpp#L11285-L11296`

### `expr.writ-map-comp.requires-builder-import` — Writ map comprehension requires comp_builder import

A writ map comprehension is ill-formed unless arity-2 `writ_map_comp_new` and arity-3 `writ_map_comp_put` are visible (via `use logos.lang.writ.comp_builder;`).

*Divergence:* Logos-specific.

*Source:* `src/compiler/sema_expr.cpp#L11258-L11268`

## Sizeof pack (`expr.sizeof-pack`)

### `expr.sizeof-pack.spelling` — sizeof...(T) on a type-parameter pack

The pack-size operator must be spelled `sizeof...(T)` where T is an in-scope type parameter; it lowers to the intrinsic `__sizeof_pack__` call and yields a u64. A different operator name or an unknown type parameter is an error.

*Source:* `src/compiler/sema_expr.cpp#L1070-L1086`

<a id="intrinsic-domain"></a>
# Intrinsics (`intrinsic`)

## `size_of` (`intrinsic.sizeof`)

### `intrinsic.sizeof.unified-layout-size` — sizeof::&lt;T&gt;() yields the padded layout size

`sizeof::<T>()` evaluates to a 64-bit compile-time constant equal to the type's full size including inter-field and trailing alignment padding (e.g. `{i32,i64}` =&gt; 16, not 12), drawn from the single unified layout used by all other size queries.

```logos
sizeof::<(i32, i64)>() == 16
```

*Source:* `src/compiler/mlir_gen_expr.cpp#L5398-L5405`

### `intrinsic.sizeof.byte-size` — sizeof yields byte size

`sizeof::<T>()` requires exactly one type argument and yields `i64` = byte size of T.

*Divergence:* Logos spelling of size_of; result is i64 (Rust mem::size_of -&gt; usize).

*Source:* `src/compiler/sema_expr.cpp#L5703-L5716`

## `align_of` (`intrinsic.align-of`)

### `intrinsic.align-of.alignment` — align_of yields alignment

`align_of::<T>()` requires exactly one type argument and yields `i64` = alignment of T.

*Divergence:* Result is i64 (Rust mem::align_of -&gt; usize).

*Source:* `src/compiler/sema_expr.cpp#L5718-L5731`

## `align_of` (`intrinsic.alignof`)

### `intrinsic.alignof.unified-layout-align` — alignof::&lt;T&gt;() yields layout alignment, min 1

`alignof::<T>()` evaluates to a 64-bit compile-time constant equal to the type's alignment from the unified layout; if the layout reports alignment 0 the result is 1.

*Source:* `src/compiler/mlir_gen_expr.cpp#L5408-L5412`

## `offset_of` (`intrinsic.offset-of`)

### `intrinsic.offset-of.compile-time-byte-offset` — offset_of! yields a compile-time i64 byte offset

`offset_of!(Type, field)` (OFFSET_OF) resolves Type, walks its ABI layout to `field`, and emits an `i64` constant equal to the field's byte offset within Type.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L324`

### `intrinsic.offset-of.form` — offset_of! intrinsic

`offset_of!(Type, field)` yields the byte offset of `field` within `Type`.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2597-L2598`, `tools/peg_gen_cpp/grammars/logos.peg#L2729-L2730`, `tools/peg_gen_cpp/grammars/logos.peg#L2789-L2790`

### `intrinsic.offset-of.syntax` — offset_of! signature

`offset_of!(Type, field)` requires both a type argument and a field name; either missing is a compile error.

*Source:* `src/compiler/sema_expr.cpp#L17630-L17634`

### `intrinsic.offset-of.struct-only` — offset_of! requires a struct type

The type argument of `offset_of!` must resolve to a struct or zoned-struct type; otherwise it is a compile error. The named struct must be known.

*Source:* `src/compiler/sema_expr.cpp#L17635-L17648`

### `intrinsic.offset-of.value` — offset_of! yields a compile-time i64 byte offset

`offset_of!(T, f)` evaluates to an `i64` constant equal to the byte offset of field `f` within `T`'s layout, computed by sequentially laying out fields: each field is placed at the next position aligned up to its alignment, then advanced by its byte size. Result type is `i64`.

*Divergence:* Rust's offset_of! yields usize; Logos yields i64.

*Source:* `src/compiler/sema_expr.cpp#L17657-L17681`

### `intrinsic.offset-of.generic-subst` — offset_of! substitutes the type's generic args

When the struct is generic, the concrete type arguments of `T` are substituted into the field types before computing sizes/alignments, so `offset_of!` reflects the layout of the concrete instantiation.

*Source:* `src/compiler/sema_expr.cpp#L17649-L17659`

## Bit intrinsics (`intrinsic.bits`)

### `intrinsic.bits.count-ops-return-u32` — Bit-count intrinsics return u32

`popcount_u64`, `leading_zeros_u64`, `trailing_zeros_u64` take a u64 operand and return u32 (the i64 count result is truncated to 32 bits). `bswap_u64` and `bitreverse_u64` take and return u64 (no truncation).

*Source:* `src/compiler/mlir_gen_expr.cpp#L2264-L2293`

### `intrinsic.bits.ctlz-cttz-zero-defined` — Leading/trailing-zero count is defined at zero

`leading_zeros_u64` and `trailing_zeros_u64` are defined for a zero operand (not poison): a zero input yields the operand's bit width (64, before truncation to u32).

*Source:* `src/compiler/mlir_gen_expr.cpp#L2277-L2282`

### `intrinsic.bits.u64-bit-ops` — u64 bitwise intrinsics

`popcount_u64`, `leading_zeros_u64`, `trailing_zeros_u64` each take 1 u64 argument and return u32; `bswap_u64`, `bitreverse_u64` each take 1 u64 argument and return u64. Wrong arity is an error. (Lower to the corresponding LLVM intrinsics; ctlz/cttz are non-poison at zero.)

*Divergence:* Logos addition: explicit free-function bit-op intrinsics.

*Source:* `src/compiler/sema_expr.cpp#L3186-L3204`

## Reflection (`intrinsic.reflect`)

### `intrinsic.reflect.has-trait-of` — has_trait_of::&lt;Trait&gt;(t: Type) -&gt; bool folds at monomorphization

`has_trait_of::<Trait>(t)` (callee __has_trait_of__) folds to a `bool` literal during monomorphization. The concrete type T is recovered from t's `Type` struct-literal `uid` field, which must be a `__type_uid_of__::<T>()` call; T is substituted with the active type substitution. The result is `true` iff T (named by its concrete struct name, enum name, or type_str, truncated at any `$G` generic-instantiation suffix) has an impl of Trait, computed recursively over concrete and blanket impls (mono_has_impl_recursive); absent T or an empty trait name yields `false`.

*Divergence:* A6

*Source:* `src/compiler/mono_clone.cpp#L1617-L1652`

### `intrinsic.reflect.typelist-len` — typelist_len::&lt;L&gt;() -&gt; i64 folds to the pack arity

`typelist_len::<L>()` (callee __typelist_len__) folds to an `i64` literal equal to the number of type arguments in L's type-argument pack (0 when L carries no type-argument list). O(1) compile-time probe; the canonical L is `TypeList<T...>`.

*Divergence:* A6

*Source:* `src/compiler/mono_clone.cpp#L1657-L1668`

### `intrinsic.reflect.typelist-head-nth` — typelist_head/nth::&lt;L&gt;(i) -&gt; Type folds to a Type struct literal

`typelist_head::<L>()` and `typelist_nth::<L>(i)` (callees __typelist_head__/__typelist_nth__) fold to a single `Type { kind, name, size, align, uid }` struct literal (intrinsic.reflect.type-struct-shape) describing element idx of L's type-arg pack: head uses idx=0; nth requires its argument to be a literal int. A missing type argument, a non-literal nth index, or an index outside `[0, pack.size())` is a fatal compile-time error (abort with diagnostic).

*Divergence:* A6

*Related:* `intrinsic.reflect.type-struct-shape`

*Source:* `src/compiler/mono_clone.cpp#L1672-L1731`

### `intrinsic.reflect.type-struct-shape` — Reflected Type value layout {kind,name,size,align,uid}

A reflected `Type` value materialized by a folding reflection intrinsic is the struct literal `Type { kind: u32, name: &[u8], size: i64, align: i64, uid: u64 }`: `kind` = the TypeRef's `LogosType::Kind` discriminant, `name` = its canonical type string, `size`/`align` = its target layout (`size_of`/`align_of`), and `uid` = `type_hash_64bit(type_hash_23(type_id_canon(T)))`. Producing `uid` also registers `uid -> T` in a mono-wide table so a later `__type_uid_of__`-keyed lookup can recover T from the uid.

*Divergence:* A6

*Related:* `intrinsic.reflect.typelist-head-nth`, `intrinsic.reflect.reify-type`, `intrinsic.reflect.type-apply`

*Source:* `src/compiler/mono_clone.cpp#L1716-L1730`, `src/compiler/mono_clone.cpp#L1810-L1833`, `src/compiler/mono_clone.cpp#L2074-L2083`

### `intrinsic.reflect.reify-type` — reify_type(t: Type) -&gt; Type recovers a source TypeRef and re-emits Type

`reify_type(t)` (callee __reify_type__) recovers a concrete TypeRef from a direct Type-producer argument and re-emits a fresh `Type` struct literal for it. Supported argument shapes, after chasing a VarRef argument through recorded let-initializers (mono.reflect.varref-let-chase): (1) a `Call` to `__typelist_head__`/`__typelist_nth__` — the indexed pack element becomes T (same index rules as intrinsic.reflect.typelist-head-nth); (2) a `StructLit` whose `uid` field is a call to `__type_uid_of__::<T>()` — T is substituted directly. A missing argument, or any other (unsupported) shape, is a fatal compile-time error naming the accepted producer forms.

*Divergence:* A6

*Related:* `intrinsic.reflect.type-struct-shape`

*Source:* `src/compiler/mono_clone.cpp#L1741-L1835`

### `intrinsic.reflect.type-apply` — type_apply(name, args: [Type;N]) -&gt; Type instantiates a struct template

`type_apply(name, args)` (callee __type_apply__) instantiates the struct template named `name` (must be a string literal; a surrounding `"..."` quoting is stripped) applying the TypeRefs recovered from `args` as its type-argument pack, and folds to a `Type` value describing the instantiation. `args` is chased through let-bindings; absent the pack-splice fast path (intrinsic.reflect.type-apply-pack-splice) it must be an ArrLit whose elements each resolve via the same direct-producer shapes intrinsic.reflect.reify-type accepts. The instantiated Struct TypeRef's `pkg_name` is copied from the first existing struct definition matching `name`, so the instance shares registry/UID identity with ordinarily-declared instantiations. A non-literal `name`, a non-ArrLit `args`, or any unrecognized `args` element is a fatal compile-time error.

*Divergence:* A6

*Related:* `intrinsic.reflect.type-struct-shape`, `intrinsic.reflect.reify-type`

*Source:* `src/compiler/mono_clone.cpp#L1841-L1877`, `src/compiler/mono_clone.cpp#L1968-L2083`

### `intrinsic.reflect.type-apply-pack-splice` — type_apply pack-splice fast path over Type-array intrinsics

When type_apply's `args` operand is (after let-chase) itself a call to a Type-array-producing intrinsic, its element TypeRefs are spliced directly into the template instantiation instead of requiring an ArrLit shape: `__type_refs_of__` contributes its full (substituted) type-argument list, one per struct member; `__args_of__::<T>()` contributes T's own type_args; `__typelist_tail__::<T>()` contributes T's pack excluding index 0; `__tuple_elems_of__::<T>()` contributes T's tuple element types when T is a Tuple (otherwise contributes none). This splice runs before, and independent of, the mono ArrLit-folding pass.

*Divergence:* A6

*Related:* `intrinsic.reflect.type-apply`

*Source:* `src/compiler/mono_clone.cpp#L1878-L1967`

### `intrinsic.reflect.apply-generic` — apply_generic(g: Type, args) instantiates a generic constructor

`__apply_generic__(g, args)` instantiates the generic constructor described by Type value `g` (produced by `generic_of`) applying `args`, routed through the same struct-allocation path as intrinsic.reflect.type-apply. The template name is recovered from g's `Type` struct-literal `name` field, which must be a (possibly-quoted) string literal; both `g` and `args` are first chased through VarRef let-bindings (mono.reflect.varref-let-chase). `g` not resolving to such a StructLit, or `args` not being an ArrLit, is a fatal compile-time error.

*Divergence:* A6

*Related:* `intrinsic.reflect.type-apply`

*Note:* Unit ends at L2151 mid-function (inside the args-element `recover` lambda); the element-recovery/instantiation tail is only partially visible in this slice and continues in the following unit.

*Source:* `src/compiler/mono_clone.cpp#L2090-L2151`

### `intrinsic.reflect.tuple-count-of` — tuple_count_of::&lt;T&gt;() yields tuple arity

tuple_count_of::&lt;T&gt;() evaluates at compile time to an i64 literal equal to the number of element types of T when T is a tuple type, and to 0 for any non-tuple T.

*Source:* `src/compiler/mono_clone.cpp#L2686-L2698`

### `intrinsic.reflect.field-count-of` — field_count_of::&lt;T&gt;() yields struct field count

field_count_of::&lt;T&gt;() evaluates at compile time to an i64 literal equal to the number of declared fields of T when T is a struct (or zoned struct) type; for any non-struct or unresolvable T it is 0. The struct template is matched by name, preferring a package-qualified match (T.pkg) and falling back to name-only.

*Source:* `src/compiler/mono_clone.cpp#L2703-L2730`

### `intrinsic.reflect.field-names-of` — field_names_of::&lt;T&gt;() yields array of field-name strings

field_names_of::&lt;T&gt;() evaluates at compile time to an array [&str; N] whose elements are the declared field names of struct T in declaration order; for non-struct or unresolvable T it is the empty array. Struct lookup prefers a package-qualified match and falls back to name-only.

*Source:* `src/compiler/mono_clone.cpp#L2733-L2768`

### `intrinsic.reflect.args-of` — args_of::&lt;T&gt;() yields T's generic type arguments

args_of::&lt;T&gt;() produces a [Type; N] descriptor array of the generic type-arguments of T (in order); for a non-generic T the array is empty.

*Related:* `intrinsic.reflect.type-descriptor-array`

*Source:* `src/compiler/mono_clone.cpp#L2780-L2783`

### `intrinsic.reflect.typelist-tail` — typelist_tail::&lt;T&gt;() drops the first type argument

typelist_tail::&lt;T&gt;() produces a [Type; N] descriptor array of T's generic type-arguments excluding the first (i.e. the tail beginning at index 1); empty when T has fewer than two type arguments.

*Related:* `intrinsic.reflect.type-descriptor-array`

*Source:* `src/compiler/mono_clone.cpp#L2784-L2789`

### `intrinsic.reflect.tuple-elems-of` — tuple_elems_of::&lt;T&gt;() yields tuple element types

tuple_elems_of::&lt;T&gt;() produces a [Type; N] descriptor array of the element types of T when T is a tuple; empty otherwise.

*Related:* `intrinsic.reflect.type-descriptor-array`

*Source:* `src/compiler/mono_clone.cpp#L2790-L2796`

### `intrinsic.reflect.field-types-of` — field_types_of::&lt;T&gt;() yields substituted struct field types

field_types_of::&lt;T&gt;() produces a [Type; N] descriptor array of the field types of struct (or zoned struct) T in declaration order, with the struct template's type parameters substituted by T's actual type arguments (positional binding of template params to T.type_args); empty for non-struct or unresolvable T.

*Related:* `intrinsic.reflect.type-descriptor-array`

*Source:* `src/compiler/mono_clone.cpp#L2797-L2827`

### `intrinsic.reflect.type-descriptor-array` — type-reflection intrinsics produce [Type; N] descriptors

args_of::&lt;T&gt;(), type_refs_of, tuple_elems_of, typelist_tail, and field_types_of each evaluate at compile time to an array [Type; N] of struct literals. Each Type element has fields {kind: u32 = the type's kind tag, name: &str = the type's printed name, size: i64 = size_of, align: i64 = align_of, uid: u64 = a canonical 64-bit type hash recorded into a uid-&gt;type map}. N and the per-element source types are determined per-intrinsic (see related rules); type_refs_of uses its call-site type_args verbatim.

*Source:* `src/compiler/mono_clone.cpp#L2828-L2869`

### `intrinsic.reflect.writ-trait` — reflect on a writ trait registers a reflect request

`reflect::<Tr>()` where Tr names a writ trait (is_writ) registers a reflect request for `pkg::Tr` and evaluates to a `WritStatic` reflection of that trait/datatype.

*Divergence:* Logos addition (Writ reflection intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4851-L4876`

### `intrinsic.reflect.datatype` — reflect on a concrete datatype

`reflect::<T>()` requires exactly one type argument. A bare TypeVar T is deferred to mono. Otherwise T must be a concrete (non-generic, no type-args) ZonedStruct datatype; it registers a reflect request for `pkg::T` and yields a `WritStatic`.

*Source:* `src/compiler/sema_expr.cpp#L4877-L4899`

### `intrinsic.reflect.deferred-fold-after-subst` — Type-introspection intrinsics fold after substitution at mono

Type-trait/type-introspection intrinsics taking type-args are not evaluated at sema; each lowers to a magic `__<name>__` call carrying its type-args, and is folded to a concrete value only after monomorphization substitutes those type-args. Inside a generic body where T is still a type variable the call is preserved (never frozen to 'TypeVar' semantics).

*Divergence:* Logos addition: compile-time type reflection intrinsics (no Rust equivalent).

*Source:* `src/compiler/sema_expr.cpp#L5014-L5017`, `src/compiler/sema_expr.cpp#L5079-L5087`, `src/compiler/sema_expr.cpp#L5142-L5146`

### `intrinsic.reflect.typeinfo-rodata` — reflect requests TypeInfo rodata

`reflect::<T>() -> WritStatic` is a compile-time request that registers T for reflection so a TypeInfo global is emitted; the expression resolves to the address of that emitted TypeInfo rodata.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5781-L5784`

## `type_of` (`intrinsic.type-of`)

### `intrinsic.type-of.type-struct` — type_of constructs a Type reflection struct

`type_of::<T>()` requires exactly one type argument and yields a `Type` struct literal with fields {kind: u32 (from __type_kind_of__), name: &[u8] (from __type_name_of__), size: i64 (size_of T), align: i64 (align_of T), uid: u64 (type_uid of T)}. Each component is concretized at mono.

*Divergence:* Logos addition (type reflection).

*Source:* `src/compiler/sema_expr.cpp#L5142-L5183`

## Type codes (`intrinsic.type-code-of`)

### `intrinsic.type-code-of.signature` — type_code_of arity and result type

`type_code_of::<T>()` requires exactly one type argument and evaluates to a `u64` type code.

*Divergence:* Logos addition (Writ/zoned reflection intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4634-L4647`, `src/compiler/sema_expr.cpp#L4712`

### `intrinsic.type-code-of.compute` — type_code_of derivation for zoned structs

For a concrete ZonedStruct T, type_code_of(T) = an explicit `#[type_code=N]` annotation on T (keyed by `pkg::Name`) if present, else a hash derived as type_hash_56bit(type_hash_23(canonical)) of the package-qualified canonical name, with raw codes &lt; 128 biased up by +128 (reserving 0..127).

*Source:* `src/compiler/sema_expr.cpp#L4649-L4707`

### `intrinsic.type-code-of.typevar-defer` — type_code_of defers on TypeVar-bearing arguments

If T is a bare TypeVar, or a generic ZonedStruct any of whose type-args is a TypeVar, `type_code_of::<T>()` is deferred to monomorphization so each concrete instantiation gets its own type code; non-zoned non-typevar types yield code 0.

*Source:* `src/compiler/sema_expr.cpp#L4677-L4712`

### `intrinsic.type-code-of.writ-code` — type_code_of yields the Writ type code

`type_code_of::<T>()` yields `u64`, the Writ type_code of a concrete datatype = SHA-256 of "package::Name" truncated to 56 bits, shifted to &gt;= 128 if needed (codes 1-127 reserved for inline AnyVal). For non-datatype T it yields 0.

*Divergence:* Logos addition (Writ substrate).

*Source:* `src/compiler/sema_expr.cpp#L5733-L5737`

## Type UID (`intrinsic.type-uid`)

### `intrinsic.type-uid.nominal-u64` — type_uid is nominal identity

`type_uid::<T>()` requires one type argument and yields `u64`: a NOMINAL 64-bit type identity (hash of the canonical named type string), so distinct nominal types differ even at identical layout (unlike type_hash). It is the low 64 bits of the 128-bit type UID and equals the `.uid` field exposed by type_of.

*Divergence:* Logos addition.

*Related:* `intrinsic.type-uid-hi.high-half`, `intrinsic.type-hash.structural-u64`

*Source:* `src/compiler/sema_expr.cpp#L5088-L5102`, `src/compiler/sema_expr.cpp#L5172-L5174`

## Type UID (high half) (`intrinsic.type-uid-hi`)

### `intrinsic.type-uid-hi.high-half` — type_uid_hi is the high half of the 128-bit UID

`type_uid_hi::<T>()` requires one type argument and yields `u64`, the HIGH 64 bits of the 128-bit nominal type UID; together with type_uid (low half) they form a 128-bit TypeId.

*Divergence:* Logos addition.

*Related:* `intrinsic.type-uid.nominal-u64`

*Source:* `src/compiler/sema_expr.cpp#L5103-L5115`

## Type hash (`intrinsic.type-hash`)

### `intrinsic.type-hash.structural-u64` — type_hash is layout-structural

`type_hash::<T>()` requires one type argument and yields `u64`: a structural FNV-1a-64 hash of T's layout — primitives map to fixed codes; struct/tuple/array/ptr hash a tag plus the recursive hashes of constituents, with NO struct/field names. Two structurally identical layouts hash equal; generic instances hash through their substituted args (Foo&lt;i32&gt; != Foo&lt;u32&gt;).

*Divergence:* Logos addition.

*Related:* `intrinsic.type-uid.nominal-u64`

*Source:* `src/compiler/sema_expr.cpp#L5073-L5087`

## Type references (`intrinsic.type-refs-of`)

### `intrinsic.type-refs-of.pack-array` — type_refs_of reflects a type pack

`type_refs_of::<T...>()` yields `[Type; N]` with one Type value per pack member, substituted after pack expansion at mono. When the pack reduces to a single type-variable pack, the placeholder array carries a pack-size marker so let-bound/return types lift to the concrete `[Type; N]` automatically.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5670-L5701`

## `is_kind` (`intrinsic.is-kind`)

### `intrinsic.is-kind.predicate-family` — Type-kind predicate family

The predicates is_ptr / is_ref / is_mut_ref / is_struct / is_zoned / is_enum / is_tuple / is_slice / is_array / is_integer / is_signed / is_unsigned / is_float / is_bool / is_primitive each take exactly one type argument and yield `bool`, resolved against the substituted T at mono. Wrong arity is a compile error.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5127-L5140`

## `is_same` (`intrinsic.is-same`)

### `intrinsic.is-same.two-type-args` — is_same arity and result

`is_same::<T1, T2>()` requires exactly two type arguments and yields `bool`; structural/identity equality of T1 and T2 is resolved post-substitution at mono. Wrong arity is a compile error.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5018-L5026`

## Plain-data test (`intrinsic.is-data-plain-of`)

### `intrinsic.is-data-plain-of.copyable-predicate` — is_data_plain_of predicates DataPlain layout

`is_data_plain_of::<T>()` yields `bool`: true iff T is a DataPlain datatype (no relative-pointer fields). Array wrappers are stripped ([D; N] checks D). Non-datatype types (scalars, ordinary structs) always yield true; a generic (type-arg-bearing) zoned datatype yields false (conservative); an unknown datatype defaults to true.

*Divergence:* Logos addition (zoned/Writ datatypes).

*Source:* `src/compiler/sema_expr.cpp#L5739-L5779`

## Template-of (`intrinsic.template-of`)

### `intrinsic.template-of.signature` — template_of requires a top-level item name in the current file

`template_of::<X>()` requires its single type-argument to be a bare named item; X must name a top-level declaration in the current source file, otherwise a compile error. It also requires `use logos.std.compiler.metaprog` (the `template_of_at` shim) to be in scope.

*Divergence:* Logos addition (metaprogramming intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4576-L4632`

### `intrinsic.template-of.lowering` — template_of lowers to runtime AST-node anchoring

`template_of::<X>()` lowers to `template_of_at(off)` where `off` is the holder-relative AST node offset of the matching top-level item, producing a `Template` whose `raw` is anchored to the module-AST OView base at runtime.

*Source:* `src/compiler/sema_expr.cpp#L4612-L4631`

### `intrinsic.template-of.decl-handle` — template_of yields a Template handle to a declaration

`template_of::<X>()` resolves X at sema, locates the declaration item named X in the current AST root, and yields a `Template { raw: AnyVal { raw: <offset> } }` baking that declaration's arena offset as a u32 literal (same-AST scope).

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5621-L5627`

## Generic-of (`intrinsic.generic-of`)

### `intrinsic.generic-of.signature` — generic_of requires a bare struct/enum name

`generic_of::<X>()` requires its single type-argument to be a bare named struct or enum (a TYPE_REF or GENERIC_INST with a NAME); the name must resolve to a declared struct or enum in the current program, otherwise a compile error.

*Divergence:* Logos addition (compile-time reflection intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4517-L4551`

### `intrinsic.generic-of.value` — generic_of yields a Type descriptor

`generic_of::<X>()` evaluates to a `Type` struct literal with kind = Generic, name = X, size = X's type-parameter arity (count of declared type params), align = 0, and a uid = FNV-1a hash of "generic:" ++ X.

*Source:* `src/compiler/sema_expr.cpp#L4552-L4573`

### `intrinsic.generic-of.unapplied-ctor` — generic_of yields a handle for an unapplied generic constructor

`generic_of::<X>()` yields a Type-shaped value-handle for the unapplied generic constructor X (struct or enum) with kind=Generic, name=X, size=arity, and UID = FNV-1a of "generic:X".

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5615-L5619`

## Type lists (`intrinsic.typelist`)

### `intrinsic.typelist.probe-family` — typelist O(1) probes over a type pack

Over L's type-pack (L.type_args()), one type argument required: `typelist_len::<L>() -> i64`; `typelist_head::<L>() -> Type` (error if pack empty); `typelist_nth::<L>(i) -> Type` requiring exactly one i64 index arg (out-of-range = error); `typelist_tail::<L>() -> [Type; N-1]`. Substituted after L is concrete.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5393-L5457`

## Field count (`intrinsic.field-count-of`)

### `intrinsic.field-count-of.struct-field-count` — field_count_of yields struct field count

`field_count_of::<T>()` requires one type argument and yields `i64` = number of declared fields of struct T (0 for non-struct or unknown-struct T).

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5562-L5582`

## Field reflection (`intrinsic.field-reflect`)

### `intrinsic.field-reflect.types-and-names` — field_types_of / field_names_of reflect struct fields

`field_types_of::<T>()` yields `[Type; N]` of T's field types and `field_names_of::<T>()` yields `[&[u8]; N]` of T's field names; each requires one type argument; non-struct T yields empty arrays. At mono field types are substituted via the SubstMap built from the struct template's type_params -&gt; T.type_args().

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5584-L5613`

## Variant reflection (`intrinsic.variant-reflect`)

### `intrinsic.variant-reflect.enum-family` — Enum-variant decompose intrinsics

Each requires one type argument E: `variant_count_of::<E>() -> i64`; `variant_names_of::<E>() -> [&[u8]; N]`; `variant_payload_counts_of::<E>() -> [i64; N]`; `variant_payload_types_flat_of::<E>() -> [Type; M]`. For non-enum or unknown E all yield 0 / empty arrays.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5629-L5668`

## Tuple count (`intrinsic.tuple-count-of`)

### `intrinsic.tuple-count-of.elem-count` — tuple_count_of yields tuple element count

`tuple_count_of::<T>()` requires one type argument and yields `i64` = number of elements in tuple T (0 for non-tuple T).

*Divergence:* Logos addition.

*Related:* `intrinsic.tuple-elems-of.elem-types`

*Source:* `src/compiler/sema_expr.cpp#L5516-L5534`

## Tuple elements (`intrinsic.tuple-elems-of`)

### `intrinsic.tuple-elems-of.elem-types` — tuple_elems_of yields tuple element types

`tuple_elems_of::<T>()` requires one type argument and yields `[Type; N]` of T's element types; empty array for non-tuple T.

*Divergence:* Logos addition.

*Related:* `intrinsic.tuple-count-of.elem-count`

*Source:* `src/compiler/sema_expr.cpp#L5536-L5560`

## Tuple all-equal (`intrinsic.tuple-all-eq`)

### `intrinsic.tuple-all-eq.signature` — tuple_all_eq arity and tuple constraint

`tuple_all_eq::<T>(a, b)` requires exactly one type argument T which must be a tuple type, and exactly two value arguments; otherwise a compile error. Result type is `bool`. An empty tuple yields the constant `true`.

*Divergence:* Logos addition (variadic-tuple support intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4413-L4451`

### `intrinsic.tuple-all-eq.typevar-defer` — tuple_all_eq defers to mono on unbound tuple elements

If any element type of the tuple T is an unbound TypeVar, `tuple_all_eq::<T>(a,b)` is deferred to monomorphization as a `__tuple_all_eq__` call carrying T; otherwise it is expanded at sema time.

*Related:* `mono.subst.const-arg`

*Source:* `src/compiler/sema_expr.cpp#L4452-L4468`

### `intrinsic.tuple-all-eq.concrete-expansion` — tuple_all_eq concrete expansion via per-element eq

For a fully concrete tuple T = (T0,..,Tn-1), `tuple_all_eq::<T>(a,b)` expands to the `&&`-conjunction over i of `Ti::eq(&a.i, &b.i)`, where each `eq` impl is resolved by candidate lookup on `<Ti>__eq` requiring a 2-parameter signature `(&Ti, &Ti)`. If no `eq` impl exists for some element type, it is a compile error.

*Source:* `src/compiler/sema_expr.cpp#L4469-L4514`

### `intrinsic.tuple-all-eq.chain-expand` — tuple_all_eq expands an element-wise eq chain

`tuple_all_eq::<T>(a, b)` expands to the conjunction `a.0.eq(&b.0) && ... && a.{N-1}.eq(&b.{N-1})`. If T is a concrete tuple the chain is expanded at sema; if any element is a type variable a `__tuple_all_eq__` placeholder is emitted and expanded at mono once T's arity is concrete.

*Divergence:* Logos addition (variadic-tuple support).

*Source:* `src/compiler/sema_expr.cpp#L5459-L5471`

## Tuple field debug (`intrinsic.tuple-each-field-debug`)

### `intrinsic.tuple-each-field-debug.requires-tuple` — tuple_each_field_debug formats every tuple field

`tuple_each_field_debug::<T>(self, f)` requires one type argument that MUST be a tuple type (else compile error) and exactly two value arguments; result type is the enclosing function's return type. It Debug-formats every field of T into Formatter f, deferring to a `__tuple_each_field_debug__` placeholder expanded at mono.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5473-L5514`

## `has_trait` (`intrinsic.has-trait`)

### `intrinsic.has-trait.t-trait-bool` — has_trait queries impl tables

`has_trait::<T, Trait>()` requires two type arguments and yields `bool`: whether concrete T implements Trait, resolved at mono against the same impl tables (concrete + recursive blanket lookup) that drive method dispatch. The second argument is read by its identifier name only (passed as a string literal arg), not resolved as a type. Missing T or empty Trait name is a compile error.

*Divergence:* Logos addition.

*Related:* `intrinsic.has-trait-of.type-method`

*Source:* `src/compiler/sema_expr.cpp#L5235-L5270`

## `has_trait_of` (`intrinsic.has-trait-of`)

### `intrinsic.has-trait-of.signature` — has_trait_of arity and shape

`has_trait_of::<Trait>(t)` requires exactly one trait type-argument (a single named type in the turbofish) and exactly one value argument; violating either is a compile error. It evaluates to `bool`.

```logos
let b: bool = has_trait_of::<Display>(x);
```

*Divergence:* Logos addition (reflection intrinsic); no Rust equivalent.

*Source:* `src/compiler/sema_expr.cpp#L4367-L4410`

### `intrinsic.has-trait-of.lowering` — has_trait_of dispatches to runtime helper with trait name

`has_trait_of::<Trait>(t)` lowers to a call `__has_trait_of__(name, t)` where `name` is the trait's identifier passed as a `[u8]` string literal; the trait is identified by name only.

*Note:* Trait identity is by bare name string; package-qualification semantics not enforced at this site.

*Source:* `src/compiler/sema_expr.cpp#L4400-L4410`

### `intrinsic.has-trait-of.type-method` — has_trait_of is the Type-method form of has_trait

`has_trait_of::<Trait>(t: Type) -> bool` recovers concrete T from the value t's Type.uid field and runs the same impl-table recursion as has_trait.

*Divergence:* Logos addition.

*Related:* `intrinsic.has-trait.t-trait-bool`

*Source:* `src/compiler/sema_expr.cpp#L5272-L5276`

## Marker panics (`intrinsic.marker-panics`)

### `intrinsic.marker-panics.macro` — unreachable! / todo! / unimplemented! marker macros

`unreachable!`, `todo!`, and `unimplemented!` are thin wrappers around `panic!` with default prefix messages ("internal error: entered unreachable code", "not yet implemented", "not implemented"); with args they panic with `"<prefix>: {}"` filled by `format!(args)`. They type as `!` (Never) and are valid in any expression position.

*Source:* `src/compiler/sema_expr.cpp#L18348-L18390`

## Writ-static hash (`intrinsic.wstatic-hash-of`)

### `intrinsic.wstatic-hash-of.u64` — wstatic_hash_of identity hash

`wstatic_hash_of::<CFG>()` requires exactly one type argument and yields `u64`, the byte-hash identity of a WritStatic value; folded at mono once CFG is a concrete WStaticLit.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5064-L5072`

## Pointer arithmetic (`intrinsic.ptr-arith`)

### `intrinsic.ptr-arith.element-vs-byte-scaling` — Pointer arithmetic scales by pointee for Add/Sub, by byte for ByteAdd/ByteSub

Pointer arithmetic offsets the base pointer by `offset` elements (each step = sizeof(pointee)) for Add/Sub; for ByteAdd/ByteSub the offset is in bytes (pointee treated as i8). The offset operand is normalized to a 64-bit integer. Sub and ByteSub negate the offset.

*Source:* `src/compiler/mlir_gen_expr.cpp#L5414-L5453`

## Pointer difference (`intrinsic.ptr-diff`)

### `intrinsic.ptr-diff.byte-and-element` — Pointer difference: raw byte distance or element count

Pointer difference computes `(usize)lhs - (usize)rhs`; when by-byte it is that raw byte distance, otherwise it is the signed quotient `byte_distance / sizeof(pointee)` giving the element count between the two pointers.

*Source:* `src/compiler/mlir_gen_expr.cpp#L5456-L5486`

## Slice from raw parts (`intrinsic.slice-from-raw`)

### `intrinsic.slice-from-raw.ptr-len` — slice_from_raw builds a slice fat pointer

`slice_from_raw::<T>(ptr: *const T, len: i64) -> &[T]` requires exactly one type argument and exactly two value arguments; it materialises a slice fat-pointer of element type T (uniform fat-pointer layout shared with str_from_raw). Wrong type-arg count or value-arg count is a compile error.

*Divergence:* Logos addition (unsafe raw-parts constructor).

*Source:* `src/compiler/sema_expr.cpp#L5032-L5057`

## DST from raw parts (`intrinsic.dst-from-raw-parts`)

### `intrinsic.dst-from-raw-parts.unsafe` — dst_from_raw_parts requires unsafe and a custom-DST struct

`dst_from_raw_parts::<S>(ptr, len)` (and `_mut`) requires unsafe context, exactly one type argument S that is a (Zoned)Struct whose last field resolves to `[T]` or `dyn Trait` (directly is_dst or via type-parameter substitution), and exactly two value arguments.

*Divergence:* Logos addition (custom-DST construction intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4740-L4802`

### `intrinsic.dst-from-raw-parts.value` — dst_from_raw_parts builds a fat DstRef

`dst_from_raw_parts::<S>(ptr, len)` produces a `DstRef` to S ({data, len} fat-pair, same ABI as a slice); the `_mut` callee sets the DstRef mut flag. The length argument is widened to i64. The DstRef carries S's type-args for later tail-element field access.

*Source:* `src/compiler/sema_expr.cpp#L4803-L4812`

## `dyn` construction (`intrinsic.dyn`)

### `intrinsic.dyn.tagged-dispatch-tier-split` — tagged-trait dispatch splits at type_code 223 into table vs lookup

Dispatch through a `&tagged<TS> Trait` reads the object's type_code (i64) at its known offset, then for type_code &lt; 223 (tier-1) indexes a static dispatch table by type_code, and for type_code &gt;= 223 (tier-2) calls a tier-2 lookup function with the type_code, in both cases obtaining a function pointer through which the call is made indirectly with (obj_ptr, args...).

*Note:* The 223 threshold and dispatch sequence are described in a comment heading the next unit's function; the full mechanism is in gen_tagged_dispatch.

*Source:* `src/compiler/mlir_gen_dyn.cpp#L1273-L1283`

### `intrinsic.dyn.vtable-of` — vtable_of::&lt;Trait, T&gt; yields the static vtable address

`vtable_of::<Trait, T>() -> *const u8` returns the address of the compiler-materialized static vtable for `impl Trait for T`, with Trait given as a string-literal value argument (arg 0) and T as the sole type argument.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1935-L1950`

### `intrinsic.dyn.from-parts` — dyn_from_parts assembles a fat dyn pointer

`dyn_from_parts::<Trait>(data: *mut u8, vtable: *const u8) -> *mut dyn Trait` assembles a fat trait-object handle as the 16-byte {data, vtable} pair from the two raw half pointers, storing them into a freshly allocated slot and returning that slot's address; the trait argument is unused at codegen (layout is uniform across traits).

*Source:* `src/compiler/mlir_gen_expr.cpp#L1954-L1970`

### `intrinsic.dyn.vtable-drop-slot` — Every trait-object vtable's slot 0 is a drop_in_place glue function

Every vtable synthesized for a concrete type has slot 0 populated with a `__drop_in_place__<type>` function that runs that concrete type's full (Rust-faithful) drop; the glue is emitted once per concrete type (deduplicated by vtable type-name key), and slot 0 is always non-empty — a non-droppable type gets a glue function with an empty (no-op) body rather than an omitted slot.

*Source:* `src/compiler/mlir_gen_impl.hpp#L485-L492`

### `intrinsic.dyn.vtable-slot0-is-drop` — Trait-object vtable slot 0 is drop_in_place; supertrait vtables nested

A trait object's vtable carries the concrete type's drop_in_place at slot 0 (called for dynamic destruction) and includes super-vtable pointer slots for each supertrait, each pointing at the supertrait's own vtable global (recursively built).

*Related:* `intrinsic.drop.owning-dyn-handle`

*Note:* Slot-0 = drop is stated by the drop-sequence comments; exact remaining vtable slot ordering is not specified in this unit.

*Source:* `src/compiler/mlir_gen_impl.hpp#L1058-L1067`, `src/compiler/mlir_gen_impl.hpp#L1196-L1201`

### `intrinsic.dyn.deref-raw-dyn-ptr-is-identity` — Dereferencing a raw *const/*mut dyn is a no-op unless it points into storage holding the handle

For a `*const dyn`/`*mut dyn` operand, the default convention is that the pointer VALUE already IS the trait-object fat handle, so `*p` is the identity (no load). Only when the pointer is a genuine pointer-into-storage that itself holds a stored dyn handle (e.g. a container-accessor return such as `HashMap::get -> *const Box<dyn>`) does `*p` load the fat {data,vtable} pair from that storage.

*Related:* `layout.dyn.fat-pair-16-byte`

*Source:* `src/compiler/mlir_gen_impl.hpp#L1267-L1271`

## `dyn` from parts (`intrinsic.dyn-from-parts`)

### `intrinsic.dyn-from-parts.fat-trait-ptr` — dyn_from_parts builds a trait object from raw halves

`dyn_from_parts::<Trait>(data: *mut u8, vtable: *const u8) -> *mut dyn Trait` forms a fat {data, vtable} trait-object pointer. Exactly one trait type argument (its own type-args, if any, are carried so the produced object matches a parameterized `dyn Trait<...>` annotation, skipping lifetime/auto-trait bound sub-nodes) and exactly two value arguments are required. Trait must be a known, object-safe trait. The result is the bare canonical TraitObject (matching `*mut dyn`/`&dyn`), not a thin pointer.

*Divergence:* Logos addition.

*Related:* `intrinsic.vtable-of.static-vtable-addr`

*Source:* `src/compiler/sema_expr.cpp#L5314-L5391`

## Vtable-of (`intrinsic.vtable-of`)

### `intrinsic.vtable-of.static-vtable-addr` — vtable_of yields a static vtable address

`vtable_of::<Trait, T>() -> *const u8` yields the address of the static vtable for `impl Trait for T`. Trait is read by NAME (must be a known trait, else error); T is resolved as a type and substituted at mono. Missing trait name or type is a compile error; an unknown trait name is a compile error.

*Divergence:* Logos addition.

*Related:* `intrinsic.dyn-from-parts.fat-trait-ptr`

*Source:* `src/compiler/sema_expr.cpp#L5278-L5312`

## Zone intrinsics (`intrinsic.zone`)

### `intrinsic.zone.zone-of` — zone_of recovers the Writ zone pointer of a fat &mut T

`zone_of(r: &mut T) -> *mut u8` takes exactly 1 argument and yields the metadata half of the fat reference reinterpreted as a `*mut u8` (dual of zone_mut_ref). Wrong arity is an error.

*Divergence:* Logos addition: Writ/zone memory model intrinsic.

*Source:* `src/compiler/sema_expr.cpp#L3129-L3137`

## Zone mut-ref (`intrinsic.zone-mut-ref`)

### `intrinsic.zone-mut-ref.unsafe` — zone_mut_ref signature and unsafe requirement

`zone_mut_ref::<T>(ptr, zone)` requires unsafe context, exactly one type argument T, and exactly two value arguments.

*Divergence:* Logos addition (zoned-reference construction intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4820-L4843`

### `intrinsic.zone-mut-ref.value` — zone_mut_ref builds a fat &mut T carrying the zone

`zone_mut_ref::<T>(ptr, zone)` produces a fat `&mut T` whose data slot = ptr and whose metadata slot = zone pointer cast to i64.

*Source:* `src/compiler/sema_expr.cpp#L4844-L4847`

## Atomics (`intrinsic.atomic`)

### `intrinsic.atomic.primitive-set` — Atomic intrinsic family over 32/64-bit cells

The language exposes atomic primitives over 32- and 64-bit integer cells, each in a bare and an `_ord` form: load{32,64}, store{32,64}, fetch_add{32,64}, cas{32,64}, cas_weak{32,64}, swap{32,64} (exchange), fetch_{or,and,xor,sub}{32,64}. load and every fetch_*/swap RMW op return the cell's value from BEFORE the operation, at the cell's width (i32 or i64); store returns unit (represented as constant 0:i32); cas/cas_weak return a bool success flag (not the observed value).

*Source:* `src/compiler/mlir_gen_expr.cpp#L2030-L2146`, `src/compiler/mlir_gen_expr.cpp#L2147-L2243`

### `intrinsic.atomic.default-ordering-seqcst` — Non-`_ord` atomics are sequentially consistent

An atomic operation invoked through the bare (non-`_ord`) form always has sequentially-consistent ordering; cas/cas_weak use seq-cst for both the success and the failure ordering.

*Source:* `src/compiler/mlir_gen_expr.cpp#L2030-L2146`, `src/compiler/mlir_gen_expr.cpp#L2147-L2162`, `src/compiler/mlir_gen_expr.cpp#L2195-L2218`

### `intrinsic.atomic.ordering-enum-layout` — Ordering enum discriminant layout

The `Ordering` enum has fixed discriminants: Relaxed=0, Acquire=1, Release=2, AcqRel=3, SeqCst=4. `_ord` atomic variants take one trailing `Ordering` argument (two, for cas/cas_weak: success then failure ordering) that selects the operation's memory ordering.

*Source:* `src/compiler/mlir_gen_expr.cpp#L2006-L2029`, `src/compiler/mlir_gen_expr.cpp#L2164-L2183`, `src/compiler/mlir_gen_expr.cpp#L2219-L2243`

### `intrinsic.atomic.nonliteral-ordering-fallback` — Non-literal ordering argument is conservatively over-synchronized

When the `Ordering` argument of an `_ord` atomic is not a compile-time `Ordering` enum literal (e.g. a runtime value threaded through a wrapper call), the operation's observable ordering is never weaker than what the dynamic value requests for every possible `Ordering` value — correctness is preserved by choosing an ordering that is sound (&gt;=) for all cases, even though this may over-synchronize relative to a weaker requested ordering (e.g. Relaxed/Acquire).

*Note:* The precise runtime-selected ordering for a non-literal argument (e.g. release-vs-seqcst branch for stores, unconditional seq_cst for load/RMW/CAS) is an implementation choice; the language-normative guarantee is soundness (never weaker than requested), not the exact chosen ordering.

*Source:* `src/compiler/mlir_gen_expr.cpp#L2012-L2028`, `src/compiler/mlir_gen_expr.cpp#L2054-L2106`

## Wrapping arithmetic (`intrinsic.wrapping`)

### `intrinsic.wrapping.silent-twos-complement` — wrapping_add/sub/mul opt out of overflow trapping

`wrapping_add(a,b)` / `wrapping_sub(a,b)` / `wrapping_mul(a,b)` perform two's-complement add/sub/mul that wraps silently, explicitly opting out of the runtime overflow trap applied to `+`/`-`/`*`. Operands of differing integer width are zero-extended to the wider width before the operation.

*Source:* `src/compiler/mlir_gen_expr.cpp#L1869-L1910`

## Metaprogramming (`intrinsic.metaprog`)

### `intrinsic.metaprog.reify-type` — reify_type round-trips a Type value at mono time

`reify_type(t: Type) -> Type` takes exactly 1 argument and lowers to the `__reify_type__` mono intercept, which substitutes the argument and re-emits a fresh `Type` struct literal from its uid. Wrong arity is an error.

*Divergence:* Logos addition: type-reflection metaprogramming intrinsic.

*Source:* `src/compiler/sema_expr.cpp#L3139-L3154`

### `intrinsic.metaprog.type-apply` — type_apply / apply_generic instantiate a type-level template

`type_apply(name: &[u8], args: [Type; N]) -> Type` and `apply_generic(g: Type, args: [Type; N]) -> Type` each take exactly 2 arguments and lower to the `__type_apply__` / `__apply_generic__` mono intercepts, which recover concrete TypeRefs from each element and emit a fresh `Type` struct literal for `Name<T0,...>`. Wrong arity is an error.

*Divergence:* Logos addition: type-level composition metaprogramming intrinsics.

*Source:* `src/compiler/sema_expr.cpp#L3156-L3184`

## Get annotation (`intrinsic.get-annotation`)

### `intrinsic.get-annotation.signature` — get_annotation arity and annotation-type constraint

`get_annotation::<T, A>()` requires exactly two type arguments; A must be a ZonedStruct that is an annotation type. `Option` must be in scope. Result type is `Option<A>`.

*Divergence:* Logos addition (compile-time annotation reflection intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4901-L4938`

### `intrinsic.get-annotation.value` — get_annotation materializes the annotation instance

`get_annotation::<T, A>()` returns `Option::None` if T carries no annotation of type A; otherwise `Option::Some(A{...})` where the A literal is reconstructed field-by-field from the stored annotation values (int/float/bool/string/enum/array kinds), matched by annotation fqn or bare name.

*Source:* `src/compiler/sema_expr.cpp#L4942-L5010`

### `intrinsic.get-annotation.option-result` — get_annotation yields the annotation instance as Option&lt;A&gt;

`get_annotation::<T, A>() -> Option<A>` const-folds to `Some(A{...})` if datatype T carries annotation A, else `None`.

*Divergence:* Logos addition.

*Related:* `intrinsic.has-annotation.const-fold`

*Source:* `src/compiler/sema_expr.cpp#L5825-L5827`

## Has annotation (`intrinsic.has-annotation`)

### `intrinsic.has-annotation.const-fold` — has_annotation is a compile-time annotation check

`has_annotation::<T, A>()` requires exactly two type arguments and const-folds to `bool`: true iff datatype T carries a user annotation of annotation-type A. A must be a known annotation datatype (else compile error); the check matches against T's declared annotation instances by fully-qualified or simple name.

*Divergence:* Logos addition (annotation metaprogramming).

*Source:* `src/compiler/sema_expr.cpp#L5786-L5823`

## Args-of (`intrinsic.args-of`)

### `intrinsic.args-of.type-arg-array` — args_of yields generic type arguments

`args_of::<T>()` requires one type argument and yields `[Type; N]` listing T's generic type arguments; for non-generic T the result is `[Type; 0]`. The array length is fixed at mono once T is concrete.

*Divergence:* Logos addition.

*Related:* `intrinsic.args-count-of.arg-count`

*Source:* `src/compiler/sema_expr.cpp#L5185-L5211`

## Args count (`intrinsic.args-count-of`)

### `intrinsic.args-count-of.arg-count` — args_count_of yields generic-arg count

`args_count_of::<T>()` requires one type argument and yields `i64` = number of T's generic type arguments (0 for primitive or non-generic struct).

*Divergence:* Logos addition.

*Related:* `intrinsic.args-of.type-arg-array`

*Source:* `src/compiler/sema_expr.cpp#L5213-L5233`

## Closures (`intrinsic.closure`)

### `intrinsic.closure.drop-glue` — Owned closures drop their owned captures then free an escaping env

Dropping an owned closure value runs per-closure-id drop glue, `__closure_drop__<id>(env_ptr)` (deduplicated per closure-id), that drops each owned droppable capture (the narrow captured FIELD when a per-capture narrow field type is set — RFC-2229-style disjoint capture — else the whole captured root), then, if the env is heap-allocated (an escaping closure), frees the env. Its symbol is stored at closure-env field 0.

*Source:* `src/compiler/mlir_gen_impl.hpp#L494-L510`

## Unknown callee (`intrinsic.unknown-callee`)

### `intrinsic.unknown-callee.passthrough` — Unrecognized callee is not a type intrinsic

A callee not matching any recognized type-intrinsic name yields no lowering here (the dispatcher returns nothing), leaving the call to ordinary resolution.

*Source:* `src/compiler/sema_expr.cpp#L5828`

## `cfg!` (`intrinsic.cfg`)

### `intrinsic.cfg.macro` — cfg! evaluates to a bool

`cfg!(predicate)` evaluates the configuration predicate at compile time and yields a `bool` literal.

*Source:* `src/compiler/sema_expr.cpp#L18118-L18121`

## `env!` (`intrinsic.env`)

### `intrinsic.env.macro` — env! / option_env! read environment at compile time

`env!("VAR")` yields the value of environment variable VAR as a `&str` literal and is a compile error if unset; `option_env!("VAR")` yields the value or an empty `&str` if unset.

*Divergence:* option_env! returns an empty &str tombstone rather than Option&lt;&str&gt;.

*Source:* `src/compiler/sema_expr.cpp#L18289-L18316`

## `file!` (`intrinsic.file`)

### `intrinsic.file.macro` — file! / module_path! positional macros

`file!()` yields the current file path and `module_path!()` yields the current package name, each as a `&str` (`Slice<u8>`) string literal.

*Source:* `src/compiler/sema_expr.cpp#L18228-L18236`

## `line!` (`intrinsic.line`)

### `intrinsic.line.macro` — line! / column! positional macros

`line!()` yields the current source line as `u32`; `column!()` yields `u32` 0 (columns are not tracked).

*Divergence:* column!() always returns 0 rather than the true column.

*Source:* `src/compiler/sema_expr.cpp#L18221-L18227`

## `include!` (`intrinsic.include`)

### `intrinsic.include.expr-only` — include! splices a file as an expression

`include!("path")` reads the file at compile time and re-parses its contents as an expression spliced at the call site; only expression-position include! is supported (item-position is a compile error). Paths are resolved relative to the including file.

*Divergence:* Rust supports item-position include!; Logos supports only expression position.

*Source:* `src/compiler/sema_expr.cpp#L18238-L18244`, `src/compiler/sema_expr.cpp#L17686-L17784`

## `include_str!` (`intrinsic.include-str`)

### `intrinsic.include-str.macro` — include_str! / include_bytes! embed file contents

`include_str!("path")` and `include_bytes!("path")` read the file at compile time (path relative to the including file) and yield its contents as a `&str` (`Slice<u8>`) literal; both forms collapse to the same representation since `str` is `Slice<u8>`. Unreadable files are a compile error.

*Divergence:* Rust's include_bytes! has type &[u8;N] distinct from &str; in Logos both are Slice&lt;u8&gt;.

*Source:* `src/compiler/sema_expr.cpp#L18252-L18282`

## `concat!` (`intrinsic.concat`)

### `intrinsic.concat.macro` — concat! string-literal concatenation

`concat!(a, b, …)` concatenates string, integer (decimal, suffix-stripped), and bool (`true`/`false`) literals at compile time into a single `&str` (`Slice<u8>`) literal. Non-literal args are a compile error. String escapes \n \t \r \\ \" \0 are decoded.

*Divergence:* Floats and char literals are not supported (Rust supports them).

*Source:* `src/compiler/sema_expr.cpp#L18318-L18324`, `src/compiler/sema_expr.cpp#L17836-L17920`

## `concat_bytes!` (`intrinsic.concat-bytes`)

### `intrinsic.concat-bytes.macro` — concat_bytes! byte-array concatenation

`concat_bytes!(…)` concatenates byte-string literals (`b"…"`), byte-char literals (`b'X'`), and integer literals in range 0..=255 (decimal/0x/0o/0b, suffix-allowed) at compile time, yielding a `[u8; N]` array literal. Out-of-range integers, dangling/unknown escapes, and unsupported args are compile errors.

*Source:* `src/compiler/sema_expr.cpp#L18326-L18331`, `src/compiler/sema_expr.cpp#L17922-L18084`

## `stringify!` (`intrinsic.stringify`)

### `intrinsic.stringify.macro` — stringify! returns raw token text

`stringify!(…)` yields the raw source text between the parentheses as a `&str` (`Slice<u8>`) literal, without macro expansion of the contents.

*Source:* `src/compiler/sema_expr.cpp#L18333-L18346`

## `dbg!` (`intrinsic.dbg`)

### `intrinsic.dbg.macro` — dbg! prints and returns its argument

`dbg!(expr)` eprints `[file:line] expr = <Debug>` and evaluates to the value of `expr` (ownership passes through). `dbg!()` prints just `[file:line]` and yields `()`.

*Source:* `src/compiler/sema_expr.cpp#L18436-L18472`

## `compile_error!` (`intrinsic.compile-error`)

### `intrinsic.compile-error.macro` — compile_error! emits a compile-time error

`compile_error!("msg")` takes one string-literal argument and emits that message as a compile-time error.

*Source:* `src/compiler/sema_expr.cpp#L18392-L18409`

## `matches!` (`intrinsic.matches`)

### `intrinsic.matches.macro` — matches! tests a pattern

`matches!(expr, pattern [if guard])` evaluates to `true` iff `expr` matches the pattern (with optional guard), else `false`; lowered to `match (expr) { pattern => true, _ => false }`. The first top-level comma splits expr from the pattern.

*Source:* `src/compiler/sema_expr.cpp#L18411-L18434`

## Strings / str (`intrinsic.str`)

### `intrinsic.str.from-raw-fatptr` — str_from_raw builds a str fat pointer

`str_from_raw(ptr: *const u8, len: i64) -> str` constructs a string slice as a two-field fat pointer `{data: ptr, len: i64}` in fresh storage; the `len` argument is coerced to i64 before being stored.

*Source:* `src/compiler/mlir_gen_expr.cpp#L2245-L2261`

### `intrinsic.str.str-from-raw` — str_from_raw constructs a str fat pointer

`str_from_raw(ptr: *const u8, len: i64) -> str` is a compiler intrinsic taking exactly 2 arguments; it yields a value of type `&[u8]`/str fat-pointer. Wrong arity is an error.

*Divergence:* Logos addition: no Rust equivalent free function.

*Source:* `src/compiler/sema_expr.cpp#L3117-L3127`

## `vec!` (`intrinsic.vec`)

### `intrinsic.vec.builtin-macro` — vec! is a compiler builtin list-literal macro

`vec!(a, b, c)` / `vec![a, b, c]` constructs a `Vec` of its elements. A user-defined `vec` fn_macro/token_macro in scope overrides the builtin. With a known renderable element type E (from a `let v: Vec<E>` annotation), it lowers to a push-block `{ let mut __v: Vec<E> = vec_new::<E>(); __v.push(e0); …; __v }` (no Copy bound). Otherwise it lowers to `vec_from_arr([…])` (Copy-bound, inference-driven); `vec!()` empty lowers to `vec_new::<_>()`.

*Source:* `src/compiler/sema_expr.cpp#L18134-L18213`

## Drop (`intrinsic.drop`)

### `intrinsic.drop.drop-in-place-glue` — drop_in_place glue runs the concrete type's full drop

`drop_in_place(T)` is a function taking a pointer to a T that runs T's full recursive drop; for a Copy or drop-less type it is an emitted no-op. It is slot 0 of every vtable. Size/align drop slots distinct from Rust's are present (slots 1,2); no separate dealloc slot because deallocation = libc free.

*Source:* `src/compiler/mlir_gen_dyn.cpp#L971-L998`

### `intrinsic.drop.closure-env-drop-glue` — closure drop glue drops captures then frees heap env

A closure's drop glue takes the env pointer and drops each owned droppable capture at env field i+1 (field 0 reserved for the function pointer); under RFC-2229 narrowing the dropped type is the captured narrow field type when present, else the root capture type. If the env is heap-allocated (escaping closure), the env block is freed after the captures are dropped.

*Source:* `src/compiler/mlir_gen_dyn.cpp#L1001-L1053`

### `intrinsic.drop.recursive-by-type` — Drop recurses structurally by type shape

Dropping a value recurses by type: a struct runs its user `impl Drop` then drops each field; a tuple drops each element; an enum drops the payload of the active variant; an array drops each element; a reference/pointer/scalar drops nothing. Nesting (array-of-struct, struct-with-array-field) is handled recursively.

*Related:* `intrinsic.drop.owner-drops-fields-after-user-drop`

*Source:* `src/compiler/mlir_gen_impl.hpp#L1182-L1195`

### `intrinsic.drop.owner-drops-fields-after-user-drop` — Owner drop runs user Drop then drops fields; nested by-value self stops at user Drop

At the top level (owner semantics), after a value's user `impl Drop` runs, its fields/payload are ALSO dropped by the owner. A nested (non-top-level) drop calls only the user `impl Drop` and stops, because the by-value `self` consumes its own fields at the drop body's scope end.

*Related:* `intrinsic.drop.recursive-by-type`

*Source:* `src/compiler/mlir_gen_impl.hpp#L1186-L1193`

### `intrinsic.drop.skip-moved-out-paths` — Moved-out sub-values are skipped during drop

Drop of a value suppresses sub-values that were moved out: a dotted path (relative to the value) whose segment exactly matches a child skips that child's drop entirely; a deeper path recurses into the child with the remainder so only the moved leaf is suppressed while its siblings still drop.

*Related:* `intrinsic.drop.recursive-by-type`

*Source:* `src/compiler/mlir_gen_impl.hpp#L1190-L1195`

### `intrinsic.drop.owning-dyn-handle` — Drop of owning Box&lt;dyn&gt; calls vtable[0], frees data, frees handle

Dropping an owning `Box<dyn Trait>` whose binding storage is the 8-byte heap handle to a 16-byte {data, vtable} fat pair runs (null-guarded): load data and vtable; call vtable[0](data) (drop_in_place: concrete destructor + owned fields); free(data); free(handle).

*Related:* `layout.dyn.box-dyn-collapses-to-trait-object`, `intrinsic.drop.dyn-in-place`

*Source:* `src/compiler/mlir_gen_impl.hpp#L1196-L1201`

### `intrinsic.drop.owning-slice` — Drop of owning Box&lt;[T]&gt; drops each element then frees the buffer

Dropping an owning `Box<[T]>` fat slice (value {data, len}) drops each element via a runtime loop over `len` (only when T is droppable) and then frees the heap buffer.

*Source:* `src/compiler/mlir_gen_impl.hpp#L1202-L1204`

### `intrinsic.drop.owning-custom-dst` — Drop of owning Box&lt;Foo&gt; custom-DST drops prefix + tail then frees

Dropping an owning custom-DST `Box<Foo>` drops the droppable prefix fields plus the tail elements (runtime loop over the fat-pointer length carried in the {data, len} pair) and then frees the block.

*Related:* `layout.dst.slice-tail-ref-is-fat`

*Source:* `src/compiler/mlir_gen_impl.hpp#L1205-L1208`

### `intrinsic.drop.dyn-in-place` — Move-out drop of an unsized dyn tail runs vtable[0] only

Dropping the concrete payload behind a `&dyn` fat pair in place (the move-out drop of an unsized `dyn` tail) runs only vtable[0](data) (the concrete Drop) with NO free and NO refcount change; the underlying block is freed separately by the owner.

*Related:* `intrinsic.drop.owning-dyn-handle`

*Source:* `src/compiler/mlir_gen_impl.hpp#L1209-L1215`

### `intrinsic.drop.dyn-virtual-dispatch` — Dropping a borrowed `dyn Trait` handle dispatches virtually

Dropping a non-owning `dyn Trait` fat handle {data, vtable} in place invokes the destructor through the vtable's slot 0 (dynamic dispatch on the runtime concrete type), passing `data` as the sole argument. A null `data` (a moved-from/zeroed handle) skips the call.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L517-L548`

### `intrinsic.drop.box-dyn-frees-data` — Dropping `Box<dyn Trait>` runs the destructor then frees

Dropping an owning `Box<dyn Trait>` fat handle {data, vtable} runs `drop_in_place(data)` via vtable slot 0, then frees the single heap block at `data`. A null `data` skips both the destructor call and the free.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L550-L608`

### `intrinsic.drop.rc-arc-dyn-refcount` — Dropping `Rc<dyn>`/`Arc<dyn>` decrements strong count, frees at zero

Dropping an owning `Rc<dyn Trait>`/`Arc<dyn Trait>` fat handle decrements the strong-reference counter in the value's header (`Arc`: atomic RMW subtract, seq_cst ordering; `Rc`: plain load-decrement-store). Only when the decremented count reaches zero does it run `drop_in_place` on the value (vtable slot 0) and free the whole backing block. A null `data` (moved-from handle) skips the entire sequence.

*Note:* This codegen path performs no weak-count bookkeeping for the dyn case (per the in-source note); docs/DIVERGENCES.md §B (~line 93) records `Rc<dyn Tr>`/`Arc<dyn Tr>` as migrated (2026-06-02) to the real `Rc`/`Arc` struct repr with a custom-DST tail — it is unclear from this slice alone whether this fat-pair drop path is the current primary path or a residual/legacy one for a narrower case.

*Source:* `src/compiler/mlir_gen_stmt.cpp#L610-L659`

## Sizeof pack (`intrinsic.sizeof-pack`)

### `intrinsic.sizeof-pack.length-of-type-pack` — sizeof...(T) yields pack length as u64

`sizeof...(T)` (SIZEOF_PACK) is a value-position expression whose evaluation yields the length of type-parameter pack `T` as a `u64`.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L272`
