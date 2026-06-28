# Expressions

Scope: expression-domain and intrinsic-domain rules of the Logos language (`expr.*` and `intrinsic.*` ids). Each rule id is a permanent, linkable address; do not rename or merge ids. Source layers: extracted from the `sema_expr`, `sema_stmt`, `sema_render`, `mono_clone`, and grammar rule artifacts under `tools/spec-extract/rules/`.

## Literals (generic)

### `expr.lit.char-is-unicode-scalar` — Char literal is a Unicode scalar

A char literal `'X'` denotes a single Unicode scalar value, decoded to a `u32` scalar.

*Source:* `tools/peg_gen/grammars/logos.peg#L292`, `tools/peg_gen/grammars/logos.peg#L296`

## Literals

### `expr.literal.float-format-and-suffix` — Float literal format, underscores, and suffix typing

A float literal must be well-formed; underscores are stripped from the digits; a recognized 3-char float suffix sets the literal's concrete type (e.g. f32/f64) while a suffix-less float literal has the inference type FloatLit.

*Source:* `src/compiler/sema_expr.cpp#L999-L1013`

### `expr.literal.kinds` — Primary literal forms

Primary literals: integer, float, char, string, raw string, byte string, and `true`/`false` booleans. A byte-string literal lowers to a `[u8; N]` array literal of its decoded bytes (escapes \n \t \r \0 \\ \" \x.. supported).

*Source:* `tools/peg_gen/grammars/logos.peg#L2762-L2773`, `tools/peg_gen/grammars/logos.peg#L2764-L2768`

## Integer literals

### `expr.int-lit.malformed` — Malformed integer literal is rejected

An integer literal whose textual form is not a valid integer literal is a compile error: 'malformed integer literal'.

*Source:* `src/compiler/sema_expr.cpp#L223-L226`

### `expr.int-lit.negate-fold` — Leading unary minus folds into integer literal for range check

A leading unary minus is folded into the integer literal before range checking, so the magnitude is bounded by |min| rather than max (e.g. `-128i8` is valid, equal to i8::MIN).

*Source:* `src/compiler/sema_expr.cpp#L219-L221`, `src/compiler/sema_expr.cpp#L262-L271`

### `expr.int-lit.overflow-reject` — Integer literals that exceed their type range are rejected

An integer literal whose value cannot be represented (would silently saturate/truncate) is a compile error: 'integer literal out of range'. ≤64-bit literals are bound-checked against i64/destination range; literals with u128/i128 suffix are bound-checked against the 128-bit range.

*Source:* `src/compiler/sema_expr.cpp#L233-L235`, `src/compiler/sema_expr.cpp#L249-L252`

### `expr.int-lit.suffix-range` — Suffixed integer literal bound-checked against suffix type

A suffixed integer literal `Nsuf` is given type `suf` and its magnitude is bound-checked against that type's range: signed types permit |min| (e.g. i8 down to -128, up to 127), unsigned types permit 0..2^N-1. Exceeding the bound is 'integer literal out of range for its suffix type'.

*Source:* `src/compiler/sema_expr.cpp#L255-L293`

### `expr.int-lit.unsigned-negative` — Negative value with unsigned suffix is rejected

A negative integer literal with an unsigned suffix (u8/u16/u32/u64/u128) is a compile error: 'negative value with unsigned suffix'.

*Source:* `src/compiler/sema_expr.cpp#L238-L241`, `src/compiler/sema_expr.cpp#L283-L286`

### `expr.int-lit.unsuffixed-type` — Unsuffixed integer literal has inferred-integer type

An integer literal without a suffix is given a polymorphic integer-literal type whose concrete type is resolved later by destination-type coercion; only suffixed literals get a fixed primitive type at lowering.

*Source:* `src/compiler/sema_expr.cpp#L256-L258`, `src/compiler/sema_expr.cpp#L292-L293`

## Char literals

### `expr.char-lit.escapes` — Character literal escape sequences

A char literal `'c'` accepts the escapes \n \t \r \0 \\ \' \" ; \xNN (exactly 2 hex digits, byte 0..255); and \u{H..} (1..6 hex digits in braces). Any other escape is a compile error.

*Source:* `src/compiler/sema_expr.cpp#L314-L374`

### `expr.char-lit.unicode-scalar` — char value must be a valid Unicode scalar

A \u{H..} char value must be a Unicode scalar value: ≤ U+10FFFF and not in the surrogate range U+D800..U+DFFF; otherwise it is a compile error. A char literal lowers to a value of type `char`.

*Source:* `src/compiler/sema_expr.cpp#L364-L368`, `src/compiler/sema_expr.cpp#L402`

### `expr.char-lit.utf8-body` — Multibyte char literal body decoded as one UTF-8 codepoint

A char literal whose body is a single multibyte character is decoded as exactly one UTF-8 codepoint; a malformed or length-mismatched UTF-8 body is a compile error.

*Source:* `src/compiler/sema_expr.cpp#L376-L401`

## Byte-string literals

### `expr.bytes-lit.type` — Byte-string literal has type [u8; N]

A byte-string literal `b"…"` lowers to an array literal of type `[u8; N]` where N is the decoded byte count; it accepts the escapes \n \t \r \0 \\ \' \" and \xNN (2 hex digits). Unknown or malformed escapes are compile errors.

*Source:* `src/compiler/sema_expr.cpp#L405-L471`

## Array literals

### `expr.arr-lit.const-pack-expand` — Const-pack array expansion builds a symbolic-length array

An array literal `[N...]` over a `<const N...: T>` pack with a single pack-expand element of const-var element type builds a `[T; sizeof...(N)]` symbolic-length array; monomorphization later replaces the single pack-expand element with one integer literal per pack member.

*Source:* `src/compiler/sema_expr.cpp#L10858-L10873`

### `expr.arr-lit.dyn-hint-unsize` — &dyn Trait element hint unifies concrete refs via unsize coercion

Under a `[&dyn Trait; N]` annotation, a heterogeneous array of distinct `&Concrete` refs unifies to `&dyn Trait` when every element is compatible with, or unsize-coercible to, the dyn element type. Each not-already-`&dyn` element is wrapped in an explicit dyn-coercion cast (building the fat pointer / vtable per element); the homogeneity check is then skipped.

*Source:* `src/compiler/sema_expr.cpp#L10629-L10677`

### `expr.arr-lit.empty-needs-hint` — Empty array literal element type comes from an annotation hint

An empty array literal `[]` takes its element type from an enclosing `[T;N]`/`[T]`/`&[T]` annotation or return-type hint, building `[T;0]` (which borrows to an empty `&[T]`). Without such a hint the element type is unknown and a warning is emitted.

*Source:* `src/compiler/sema_expr.cpp#L10529-L10548`

### `expr.arr-lit.fnptr-hint` — FnPtr element hint unifies distinct FnItems

Under a `[fn(...) -> R; N]` annotation, a heterogeneous array of distinct function items coerces to a common function-pointer element type when every element is compatible with the hint; each non-matching element is cast to the hint and the hint becomes the element type.

*Source:* `src/compiler/sema_expr.cpp#L10603-L10628`

### `expr.arr-lit.homogeneous` — Array literal elements must be mutually compatible and range-checked

Absent a unifying hint, all array-literal elements must be pairwise compatible; the element type is the numeric unification of the elements. Integer-literal elements (including those nested in array/tuple literal elements, and element 0 retroactively against a later concrete anchor) are range-checked against the inferred concrete element type, reporting an out-of-range error per offending element/sub-element.

*Source:* `src/compiler/sema_expr.cpp#L10678-L10843`

### `expr.arr-lit.intlit-i64-widen` — IntLit element type widens to i64 on overflow of i32

When the inferred element type is the untyped integer-literal type, it is upgraded to i64 if any element value overflows the i32 range; otherwise it stays IntLit so annotation-based coercion (e.g. `[i64;N] = [1,2,3]`) remains applicable.

*Source:* `src/compiler/sema_expr.cpp#L10844-L10856`

### `expr.arr-lit.scalar-hint-adopt` — Concrete scalar element hint retypes literal elements up front

When an array literal has a concrete scalar integer/float element hint and every element is either already of the hint type or an in-range integer/float literal, all literal elements are retyped to the hint and the hint becomes the element type. An integer literal that does not fit the hinted width is an error (not a silent fall-back to the default int type).

*Source:* `src/compiler/sema_expr.cpp#L10554-L10602`

## Array literals

### `expr.array-lit.bracket-comma` — Array literal

An array literal is a comma-separated element list in brackets: `[e0, e1, ...]`.

*Source:* `src/compiler/sema_render.cpp#L333-L344`

## Arrays

### `expr.array.literal-forms` — Array literal and fill forms

Array literals: element list `[e1, e2, …]` and fill form `[value; N]` where N is an integer literal, a named const, `sizeof...(P)` (variadic pack length), or a `metacall` block. The fill form is preferred over the list form to resolve ambiguity.

*Source:* `tools/peg_gen/grammars/logos.peg#L2863-L2873`, `tools/peg_gen/grammars/logos.peg#L2703-L2704`

## Tuple literals

### `expr.tuple-lit.one-elem-trailing-comma` — One-element tuple requires trailing comma

A tuple literal is `(e0, e1, ...)`; a single-element tuple is distinguished from a parenthesized expression by a mandatory trailing comma: `(e,)`.

*Source:* `src/compiler/sema_render.cpp#L318-L331`

## Struct literals

### `expr.struct-lit.duplicate-field-error` — Struct-lit may not initialize a field twice

Initializing the same field more than once in a struct literal is a 'duplicate field' error.

*Source:* `src/compiler/sema_expr.cpp#L9900-L9905`, `src/compiler/sema_expr.cpp#L10077-L10082`

### `expr.struct-lit.dyn-auto-bounds-at-field-init` — Auto-trait bounds checked at dyn field-init coercion

When a field value is coerced to a field type that is a dyn-trait with auto-trait bounds (e.g. `&dyn Trait + Send`), the value's type must satisfy those auto-trait bounds.

*Source:* `src/compiler/sema_expr.cpp#L10098-L10101`

### `expr.struct-lit.explicit-type-args-seed-inference` — Explicit type args seed struct-lit inference

In a struct literal `S::<A1,...,Ak> { ... }` for generic `S`, supplied type args are bound positionally to S's type-params (up to the number of params) and used to seed the inferred-arg map; each supplied arg is resolved and ignored if it resolves to an error type.

*Source:* `src/compiler/sema_expr.cpp#L9696-L9713`

### `expr.struct-lit.field-init` — Struct field initializers and shorthand

A struct field initializer is `name: expr` or the shorthand `name` (FIELD_SHORTHAND, binding the in-scope variable of that name). Tuple-struct fields may be initialized by their numeric name `S { 0: a, 1: b }` since fields of `struct S(T0,T1)` are named "0"/"1".

*Source:* `tools/peg_gen/grammars/logos.peg#L2843-L2861`, `tools/peg_gen/grammars/logos.peg#L2851-L2855`

### `expr.struct-lit.field-init-and-shorthand` — Struct literal field forms

A struct literal is `Name { f: v, ... }`; fields are either `name: value` (FIELD_INIT) or shorthand `name` (FIELD_SHORTHAND). The name may carry turbofish type args `Name::<T> { ... }`.

*Source:* `src/compiler/sema_render.cpp#L346-L385`

### `expr.struct-lit.field-type-mismatch-error` — Struct-lit field value must be compatible with declared field type

Each initialized field's value type must be compatible with the field's declared type (after substituting struct type-params into the declared type), else a 'expected X, got Y' error; comparison is deferred to mono when the substituted field type still contains a TypeVar/ConstVar/CfgSlotType/AssocType. A closure value coercible to the declared fn-ptr type is accepted.

*Source:* `src/compiler/sema_expr.cpp#L9906-L9953`, `src/compiler/sema_expr.cpp#L9916-L9921`, `src/compiler/sema_expr.cpp#L9926-L9944`

### `expr.struct-lit.field-value-moved` — Move-typed field values are consumed by the literal

When constructing a struct literal, each field value whose type is a move type is marked moved (consumed) in the surrounding scope, preventing later use and double-drop.

*Source:* `src/compiler/sema_expr.cpp#L10023-L10033`, `src/compiler/sema_expr.cpp#L10223-L10227`

### `expr.struct-lit.field-variance-check` — Variance check at struct-lit field initialization

Each field initialization is variance-checked between the value type and the declared field type in permissive mode (the struct's lifetime args are bound at the construction site, so elided source regions are filled by the caller's region inference); the check is skipped when the field type still contains a type-param.

*Source:* `src/compiler/sema_expr.cpp#L9954-L9961`, `src/compiler/sema_expr.cpp#L10092-L10097`

### `expr.struct-lit.forms` — Struct literal forms

Struct literals: `T { f: e, … }`, generic `T::<A,…> { f: e, … }`, and functional-update `T { f: e, .. base }` / `T { .. base }` / `T { .. base, f: e }`. Explicit fields always override the base regardless of field order.

*Source:* `tools/peg_gen/grammars/logos.peg#L2818-L2838`, `tools/peg_gen/grammars/logos.peg#L2823-L2831`

### `expr.struct-lit.full-explicit-args-select-spec` — Fully-supplied type args select a matching specialization

If all type args of a generic struct are explicitly supplied and a matching (full or partial) specialization exists, the literal's field set and field types are taken from that specialization rather than the primary template.

*Source:* `src/compiler/sema_expr.cpp#L9715-L9719`, `src/compiler/sema_expr.cpp#L9766-L9777`

### `expr.struct-lit.functional-update` — Functional struct update `..base` fills unset fields

> NOTE: 2 rule artifacts share this id with differing statements/evidence. Both are surfaced below; reconcile during spec review.

**Variant 1.** With `S { ..., ..base }`, the `base` expression must have struct type S (same struct name); every field not explicitly initialized is read from base via field-read, with the struct's type-params substituted into the carried field's declared type (generic path). A base of differing struct type is an error.

*Source:* `src/compiler/sema_expr.cpp#L9970-L10013`, `src/compiler/sema_expr.cpp#L10171-L10213`

**Variant 2.** A struct literal may end with a functional-update base `..expr` that supplies remaining fields.

*Source:* `src/compiler/sema_render.cpp#L386-L391`

### `expr.struct-lit.infer-nested-typevar` — Recursive inference of nested struct type-params

A struct type-param appearing nested inside a compound field type (generic struct/enum type-args, array/pointer element, tuple element, or fn-ptr/closure parameter and return types) is inferred by parallel structural walk of the declared field type and the field value type; only the struct's own as-yet-uninferred type-params are bound, and binding to an Error/IntLit/FloatLit value type is skipped.

*Source:* `src/compiler/sema_expr.cpp#L9730-L9764`, `src/compiler/sema_expr.cpp#L9819-L9823`

### `expr.struct-lit.infer-typevar-from-array-field` — Infer T from `[T; N]` field via element type

For a field declared `[T; N]` with type-param element T, T is inferred from the element type of an array-typed field value; an IntLit element defaults to T's hint (else i32).

*Source:* `src/compiler/sema_expr.cpp#L9792-L9805`

### `expr.struct-lit.infer-typevar-from-field` — Infer struct type-param from a directly-typed field value

A struct type-param `T` used directly as a field's declared type is inferred from that field's value type; an uninferred-T field value of IntLit type defaults to T's hint (else i32), and of FloatLit type defaults to T's hint (else f64).

*Source:* `src/compiler/sema_expr.cpp#L9779-L9791`

### `expr.struct-lit.infer-typevar-from-ptr-field` — Infer T from `*T`/`&T`/`&mut T` field via pointee

For a field declared as a pointer/reference to type-param T (`*T`, `&T`, `&mut T`), T is inferred from the pointee of a ref-like field value type, provided that pointee is not an error type.

*Source:* `src/compiler/sema_expr.cpp#L9806-L9818`

### `expr.struct-lit.intlit-fits-field` — IntLit field value must fit the declared field type

An integer-literal field value must fit within the declared field type's range; otherwise a 'value V does not fit in T' error. The same fit-check applies element-wise to array-literal, tuple-literal, and nested array/tuple-literal field values against the corresponding narrow element types.

*Source:* `src/compiler/sema_expr.cpp#L9962-L9967`, `src/compiler/sema_expr.cpp#L10102-L10168`

### `expr.struct-lit.missing-field-error` — All non-union struct fields must be initialized

Every field of a non-union struct must be initialized (directly, via variadic expansion, or via `..base`); an uninitialized field is a 'field not initialized' error.

*Source:* `src/compiler/sema_expr.cpp#L10015-L10021`, `src/compiler/sema_expr.cpp#L10215-L10221`

### `expr.struct-lit.outlives-check` — Struct `where 'a: 'b` outlives constraints enforced at literal

A struct literal must satisfy the struct's declared lifetime outlives constraints (`where 'a: 'b`), checked against the literal's lifetime args, the struct's field types, and the supplied field values.

*Source:* `src/compiler/sema_expr.cpp#L10035-L10041`, `src/compiler/sema_expr.cpp#L10232-L10238`

### `expr.struct-lit.uninferred-typevar-fallback-hint` — Fallback type-param resolution from hint then error

Any struct type-param not inferred from fields is resolved from the expected-type hint if available; a param still unresolved after the hint becomes an error type (poisoning the instantiation). The hint struct type also supplies type-args positionally and variadic params consume the hint's trailing type-args.

*Source:* `src/compiler/sema_expr.cpp#L9825-L9856`

### `expr.struct-lit.union-single-field` — Union literals initialize exactly one field; missing-field check skipped

For a union struct, the all-fields-initialized check is suppressed: a union literal initializes only one (active) field by design.

*Divergence:* A6

*Source:* `src/compiler/sema_expr.cpp#L10015-L10021`, `src/compiler/sema_expr.cpp#L10215-L10221`

### `expr.struct-lit.unknown-field-error` — Struct-lit may not name a field absent from the definition

A field name in a struct literal that is neither a field of the effective struct definition nor a variadic-field expansion is an 'unknown field' error.

*Source:* `src/compiler/sema_expr.cpp#L9878-L9899`, `src/compiler/sema_expr.cpp#L10049-L10076`

### `expr.struct-lit.variadic-field-expansion` — Variadic struct field accepts expansion names `name_*`

A variadic struct field named `name` accepts literal field names of the form `name_<suffix>`; each such expansion value is type-checked against the variadic field's type and the variadic field is marked initialized.

*Divergence:* A6

*Source:* `src/compiler/sema_expr.cpp#L9882-L9897`, `src/compiler/sema_expr.cpp#L10052-L10074`

## Enum literals

### `expr.enum-lit.arg-type-compat` — Payload argument type compatibility

Each non-variadic payload argument's type must be compatible with its resolved formal payload type; an incompatibility is ill-formed ("arg i: expected X, got Y").

*Source:* `src/compiler/sema_expr.cpp#L12528-L12535`

### `expr.enum-lit.args-shape` — Enum-literal argument list shape

The payload argument list of an enum literal is accepted either as a direct sequence of argument expressions or as a map containing an ITEMS sequence; both forms denote the same ordered payload list.

*Source:* `src/compiler/sema_expr.cpp#L12321-L12348`

### `expr.enum-lit.arity` — Non-variadic variant arity

For a non-variadic variant, the number of payload arguments must equal the number of declared payload types; otherwise the program is ill-formed ("expects N args, got M").

*Source:* `src/compiler/sema_expr.cpp#L12524-L12527`

### `expr.enum-lit.dyn-payload-arg` — Concrete payload into a dyn-typed enum slot widens the type arg

When the hint pins a type parameter to a trait-object-wrapping type (e.g. `Box<dyn Tr>`) but the payload argument is a concrete coercible value (e.g. `Box<Sq>`), the constructed enum's type argument records the dyn type while the payload expression stays concrete; the store later unsize-fattens it into the dyn slot.

*Source:* `src/compiler/sema_expr.cpp#L12097-L12119`

### `expr.enum-lit.forms` — Enum variant literal forms

Enum variants are written `E::V` (unit), `E::V(args)` (tuple payload), `E::V { f: e, … }` (struct-shape payload), with optional turbofish `E::V::<T,…>`. The qualified-as form `<T as Trait>::V` and dotted-package-prefix form `pkg.path.E::V` are also accepted. Struct-shape variant fields are resolved by name to positional indices.

*Source:* `tools/peg_gen/grammars/logos.peg#L2787-L2816`

### `expr.enum-lit.intlit-fit` — Integer-literal payload range check

An integer-literal payload argument whose constant value does not fit in the target integer type's range is ill-formed; this check recurses into array-literal elements and tuple-literal elements (and their nested array/tuple sub-elements) of the payload type.

*Source:* `src/compiler/sema_expr.cpp#L12536-L12542`, `src/compiler/sema_expr.cpp#L12543-L12608`

### `expr.enum-lit.intlit-payload-fits` — Integer-literal payload must fit the declared payload type

An integer-literal payload argument (directly, or as an element of an array/tuple payload, recursively) must fit within the declared narrow integer payload type; an out-of-range value is an error.

*Source:* `src/compiler/sema_expr.cpp#L12180-L12251`

### `expr.enum-lit.nested-hint-projection` — Per-payload type hint via outer-hint projection

When the surrounding expected type is `E<A1..An>` for the same enum `E`, each payload slot whose formal type is a TypeVar receives a per-argument expected-type hint computed by substituting `E`'s type parameters with the outer hint's type-args; this lets a nested enum literal (e.g. inner `Result::Ok` inside `Option::Some(Result::Ok(42))`) lower with its own concrete enum hint.

*Source:* `src/compiler/sema_expr.cpp#L12301-L12320`, `src/compiler/sema_expr.cpp#L12327-L12338`

### `expr.enum-lit.payload-arity-check` — Non-variadic variant payload arity must match

For a non-variadic variant, the number of supplied payload arguments must equal the declared payload arity; mismatch is an error `<E>::<V> expects N args, got M`. Each payload argument's type must be compatible with the declared (substituted) payload type.

*Source:* `src/compiler/sema_expr.cpp#L12168-L12180`

### `expr.enum-lit.payload-type-inference` — Generic enum type-arg inference from payload and hint

For a generic enum, each type parameter is inferred from the corresponding payload: a bare-TypeVar payload binds the param to the argument's type; a structural payload type is unified against the argument to extract nested bindings. Unresolved integer/float literal payloads default to i32/f64 unless the surrounding hint pins the param to a concrete type, in which case the hint wins and the literal is widened to it. Params still unresolved after payload inference are filled from a matching enum hint.

*Source:* `src/compiler/sema_expr.cpp#L12059-L12138`, `src/compiler/sema_expr.cpp#L12082-L12127`

### `expr.enum-lit.self-resolves-to-enclosing-enum` — `Self::Variant` resolves to the enclosing enum

Inside an `impl Enum` body, the path head `Self` in a unit-variant or struct/tuple-shaped variant literal resolves to the enclosing enum's name, provided `Self` is bound to a type of enum kind.

*Source:* `src/compiler/sema_expr.cpp#L11585-L11590`, `src/compiler/sema_expr.cpp#L11732-L11737`

### `expr.enum-lit.struct-shape-named-fields` — Struct-shaped variant literal `E::V { f: e, .. }`

A struct-shaped variant literal binds named field initializers (and shorthands `name` ⇒ `name` var-ref) to the variant's declared payload fields by name, producing positional payload in declaration order. Errors: unknown field name, field specified more than once, missing field(s) (all reported together), and using `{}` form on a non-struct-shape variant. An empty struct-shape variant `E::Empty {}` is accepted with empty payload.

*Source:* `src/compiler/sema_expr.cpp#L11853-L11966`

### `expr.enum-lit.type-alias-peel` — Variant path through a non-generic enum type alias

A variant-literal path head that names a non-generic type alias whose aliased type is an enum is rewritten to the underlying enum name before variant lookup; generic aliases are not peeled here.

*Source:* `src/compiler/sema_expr.cpp#L11591-L11598`, `src/compiler/sema_expr.cpp#L11738-L11745`

### `expr.enum-lit.unit-payload-kept` — Unit payload retained, not elided

A unit-typed payload argument (e.g. `()` in `Result::Ok(())`) is retained as a real payload entry; void/unit payloads are not filtered out.

*Source:* `src/compiler/sema_expr.cpp#L12299-L12300`, `src/compiler/sema_expr.cpp#L12321-L12348`

### `expr.enum-lit.unit-variant-hint-type-args` — Payload-less variant on a generic enum infers type args from the surrounding hint

A payload-less variant of a generic enum (e.g. `Option::None`) takes its type arguments from the surrounding type hint when the hint is the same enum with a matching type-arg arity; otherwise the result type is the bare (un-parameterized) enum.

*Source:* `src/compiler/sema_expr.cpp#L11704-L11725`

### `expr.enum-lit.unknown-enum-error` — Unknown enum / unknown variant diagnostics

A variant-literal path whose head names no enum (after Self/alias resolution and all assoc-const/fn-ptr fallbacks) is an error `unknown enum '<name>'`; a known enum with no matching variant is an error `enum '<E>' has no variant '<V>'`.

*Source:* `src/compiler/sema_expr.cpp#L11681-L11682`, `src/compiler/sema_expr.cpp#L11701-L11702`, `src/compiler/sema_expr.cpp#L11838-L11847`

### `expr.enum-lit.unknown-variant` — Enum literal references an existing variant

In an enum literal `E::V(args)`, `V` must be a declared variant of enum `E`; otherwise the program is ill-formed (diagnostic "enum 'E' has no variant 'V'").

*Source:* `src/compiler/sema_expr.cpp#L12287-L12293`

### `expr.enum-lit.variadic` — Variadic variant payload checking

For a variadic variant, every payload argument is checked for compatibility against (and integer-literal fit within) the single pack element type (the first declared payload type), with no arity constraint.

*Source:* `src/compiler/sema_expr.cpp#L12524-L12527`, `src/compiler/sema_expr.cpp#L12610-L12628`

## Writ literals

### `expr.writ-lit.capture-context-save-restore` — Nested @-literals do not clobber the outer capture context

Lowering an @-literal establishes a fresh capture context for the duration of the literal and restores the prior context afterward, so a static @-literal nested inside a `${expr}` capture does not disturb outer `$`-captures.

*Source:* `src/compiler/sema_expr.cpp#L15407-L15421`

### `expr.writ-lit.result-type` — @-literal result type depends on presence of captures

An @-literal with no captures has type `WritStatic`; an @-literal with one or more `$`-captures has the return type of `writ_build_from_template` (an Rc<Writ>), which requires `use logos.lang.writ.tmpl;` to be in scope.

*Source:* `src/compiler/sema_expr.cpp#L15422-L15444`

## Path expressions

### `expr.path.assoc-const-disambiguation` — `Type::member` not naming an enum variant is tried as an associated const

When a `Name::member` path parses as an enum literal but `Name` is not a known enum, it is resolved as an associated const access in order: (1) inherent assoc const `impl Name { const member }`; (2) trait assoc const `<Tr>::member` for any trait `Tr` impl'd for `Name`; (3) generic assoc-const projection when `Name` is a bound type parameter. The const's value AST is lowered once and cached.

*Source:* `src/compiler/sema_expr.cpp#L11604-L11638`, `src/compiler/sema_expr.cpp#L11691-L11700`

### `expr.path.method-as-fn-pointer` — Path to a non-generic method in value position becomes a fn pointer

A path `Type::method` (or `Trait::method`) used in value position, not naming a variant or const, denotes a function-pointer value when it resolves to a single non-generic method: its type is `FnPtr(param_types) -> ret`. For a trait-qualified head, resolution succeeds only when exactly one impl of the trait is in scope; otherwise it is ambiguous.

*Source:* `src/compiler/sema_expr.cpp#L11639-L11680`, `src/compiler/sema_expr.cpp#L11773-L11814`

### `expr.path.typaram-static-method-call` — `Z::method::<..>(args)` on a bound type parameter

A call `Z::method::<TArgs>(args)` where `Z` is a type parameter bound by a trait declaring a static `method` dispatches to the bound's static method, disambiguated from generic enum-variant construction by `Z` being a bound type parameter.

*Source:* `src/compiler/sema_expr.cpp#L11815-L11837`

## Variable references

### `expr.var-ref.bare-variant-alias` — Imported no-payload enum variant usable as a bareword

A no-payload enum variant brought into scope via `use Type.{V, …};` (or the prelude bareword `None`) can be referenced as a bare identifier, constructing that variant; payload-carrying variants require call syntax.

*Source:* `src/compiler/sema_expr.cpp#L511-L571`

### `expr.var-ref.const-param-value-use` — Const-generic parameter usable in value position

A const-generic parameter `<const N: T>` referenced in expression position evaluates to a value of its underlying numeric type T (default i64); monomorphization substitutes the concrete constant.

*Source:* `src/compiler/sema_expr.cpp#L481-L490`

### `expr.var-ref.fn-item-type` — Bare function name has a distinct per-function fn-item type

A function name used as a value has a zero-sized fn-item type unique to that function (distinct type per function/instantiation), which auto-coerces to the corresponding `fn(T)->R` pointer type at value-use sites.

*Source:* `src/compiler/sema_expr.cpp#L491-L510`

### `expr.var-ref.undefined` — Reference to an undefined name is an error

A variable reference whose name resolves to no local binding, const-generic parameter, function, enum variant, or unit struct is a compile error: 'undefined variable'.

*Source:* `src/compiler/sema_expr.cpp#L583-L584`

### `expr.var-ref.unit-struct-value` — Unit struct name in value position constructs it

A bare name of a known zero-field, non-generic struct in value position constructs that struct (unit-struct construction); a fielded struct still requires `S { … }` form.

*Related:* `expr.var-ref.undefined`

*Source:* `src/compiler/sema_expr.cpp#L573-L582`

## Static references

### `expr.static.extern-access-unsafe` — Accessing an extern static requires unsafe

Any access to an extern static outside an `unsafe` block is a compile error (Rust items.extern.static), with the same local/const-param shadowing suppression as mutable statics.

*Related:* `expr.static.mut-read-unsafe`

*Source:* `src/compiler/sema_expr.cpp#L604-L607`, `src/compiler/sema_expr.cpp#L620-L623`

### `expr.static.mut-read-unsafe` — Reading a mutable static requires unsafe

Reading a `static mut` outside an `unsafe` block is a compile error (Rust items.static.mut.safety); the gate is suppressed when the name is shadowed by a local binding or a const-generic parameter.

*Source:* `src/compiler/sema_expr.cpp#L595-L628`

## Turbofish

### `expr.turbofish.generic-ref` — Turbofish generic reference and static call

`IDENT::<T,…>` is a generic reference (explicit type arguments to a function/item). `IDENT::<T,…>::METHOD` is a static call on the type-applied receiver.

*Source:* `tools/peg_gen/grammars/logos.peg#L2756-L2760`

## Unary operators

### `expr.unary.double-ref` — Double address-of

`&&v` (lexed as a single AND token) is the double address-of of `v`: it lowers to `&(&v)` with type `& & typeof(v)`. If `typeof(v)` is an error type the whole expression is an error.

*Source:* `src/compiler/sema_expr.cpp#L2476-L2486`

### `expr.unary.neg-literal-fold` — Negated integer literal folds the sign

`-L` where L is an integer literal is parsed as the single negative literal `-L`, so the minimum suffixed value (e.g. `-128i8`) is accepted even though the bare positive literal would be out of range for its type.

*Source:* `src/compiler/sema_expr.cpp#L2591-L2598`

### `expr.unary.neg-numeric` — Unary minus requires a numeric operand

`-x` on a non-struct operand requires `x` to be of numeric type (else a type error); the result type equals the operand type.

*Source:* `src/compiler/sema_expr.cpp#L2625-L2640`

### `expr.unary.neg-unsigned-rejected` — Unary minus on an unsigned type is rejected

`-x` where `x` has any unsigned integer type (u8/u16/u24/u32/u56/u64/u128) is a compile error; an explicit cast to a signed type is required (e.g. `-(x as i64)`).

*Divergence:* Rust also rejects unary `-` on unsigned types (no `Neg` impl); Logos diagnostic is bespoke (B-ex-04). Extra widths u24/u56 are Logos-only (A11).

*Source:* `src/compiler/sema_expr.cpp#L2628-L2639`

### `expr.unary.not-bool-or-integer` — Unary ! is logical-not on bool and bitwise-not on integers

`!x` requires `x` to be `bool` (result `bool`) or an integer type (result = operand type; an untyped integer literal becomes `i32`); any other operand type is a type error.

*Source:* `src/compiler/sema_expr.cpp#L2641-L2650`

### `expr.unary.operator-overload` — Unary operators dispatch to Neg/Not impls on struct operands

For a struct operand, `-x` resolves to the `Neg::neg(self)->Self` method and `!x` to the `Not::not(self)->Self` method via mangled `<Type>__neg`/`<Type>__not` signature lookup; when found, the unary expression becomes that method call.

*Source:* `src/compiler/sema_expr.cpp#L2605-L2622`

### `expr.unary.operator-set` — Unary / prefix operators

Prefix unary operators (highest binding among operators): `*` deref, `&` borrow, `&mut` mutable borrow, `-` negate, `!` not. `&&v` (lexed as the AND token) means a double reference and lowers to nested address-of.

*Source:* `tools/peg_gen/grammars/logos.peg#L2648-L2656`

### `expr.unary.prefix-no-space` — Unary operators are prefix

Unary operators (`&`, `!`, `-`, etc.) are prefix and bind directly to their operand with no intervening space: `OP operand`.

*Source:* `src/compiler/sema_render.cpp#L128-L133`

## Binary operators

### `expr.binop.parenthesized` — Binary operator is infix

A binary operation is written `lhs OP rhs` with OP an infix operator token.

*Source:* `src/compiler/sema_render.cpp#L121-L126`

### `expr.binop.precedence-cascade` — Binary operator precedence

Binary precedence, lowest→highest: logical (`&&`/`||`) < comparison (`==` `!=` `<=` `>=` `<` `>`) < bitor `|` < bitxor `^` < bitand `&` < shift (`<<` `>>`) < additive (`+` `-`) < multiplicative (`*` `/` `%`) < `as`-cast < unary. All binary levels are left-associative.

*Source:* `tools/peg_gen/grammars/logos.peg#L2585-L2636`, `tools/peg_gen/grammars/logos.peg#L2602-L2606`

### `expr.binop.ptr-null-compare` — Pointer compared only against integer literal 0

A raw pointer may be compared (== / != / relational) with an integer literal, but the literal must be 0; comparing a pointer with any non-zero literal is an error.

```logos
ptr == 0
```

*Source:* `src/compiler/sema_expr.cpp#L2274-L2289`

### `expr.binop.str-eq-by-content` — str equality compares contents via str_eq

== / != between two str operands (both Slice<u8> with u8 element) desugar to a call to stdlib `str_eq` (content comparison); != negates the result. With no `str_eq` in scope, falls back to (incorrect) pointer comparison.

*Source:* `src/compiler/sema_expr.cpp#L2194-L2221`

### `expr.binop.str-relational-cmp` — str ordering via str_cmp compared to 0

Relational operators {<,<=,>,>=} between two str operands desugar to `str_cmp(lhs, rhs) OP 0`, where str_cmp returns lexicographic -1/0/1 (i32).

*Source:* `src/compiler/sema_expr.cpp#L2223-L2250`

### `expr.binop.string-vs-str-eq` — String == str views String as str

For == and !=, when one operand is the struct String and the other is str (Slice<u8>), the String operand is viewed as str via .as_str() so the comparison proceeds through the str equality path.

```logos
s == "lit"
```

*Divergence:* Mirrors Rust `impl PartialEq<str> for String`.

*Source:* `src/compiler/sema_expr.cpp#L1782-L1808`

### `expr.binop.unknown-operator` — Unknown binary operator is an error

A binary operator not in the recognized set is rejected as an unknown binary operator.

*Source:* `src/compiler/sema_expr.cpp#L2466-L2467`

## Comparison

### `expr.cmp.chained-comparison-forbidden` — Chained comparisons are not supported

A chained comparison such as `a < b < c` is rejected; it must be written `a < b && b < c`.

*Source:* `src/compiler/sema_expr.cpp#L1079-L1086`

### `expr.cmp.no-chained-comparisons` — Chained comparisons rejected

A comparison chain with 2+ comparators in a row (`a < b < c`) is rejected at sema with the diagnostic "chained comparisons not supported; use `a < b && b < c`". It parses (CHAINED_CMP) but is not a valid program.

*Divergence:* B-ex-08

*Source:* `tools/peg_gen/grammars/logos.peg#L289`

### `expr.cmp.non-chainable` — Comparison operators are non-chainable

Comparison operators are non-chainable: at most one comparison per level is well-formed. A chain of 2+ comparators (e.g. `a < b < c`) is parsed as a distinct CHAINED_CMP node so sema can reject it with a dedicated diagnostic rather than a generic syntax error.

*Divergence:* Rust-conformant outcome (chained comparison is an error); Logos detects it grammatically for a better diagnostic.

*Source:* `tools/peg_gen/grammars/logos.peg#L2589-L2600`, `tools/peg_gen/grammars/logos.peg#L2424-L2431`

## Casts

### `expr.cast.as-chain` — as-cast chaining

`as`-casts (`v as T`) bind below unary operators and chain left-associatively, so `x as T1 as T2` folds as `(x as T1) as T2`.

*Source:* `tools/peg_gen/grammars/logos.peg#L2638-L2646`, `tools/peg_gen/grammars/logos.peg#L2632`

### `expr.cast.as-keyword` — Cast syntax

A cast is written `expr as Type`.

*Source:* `src/compiler/sema_render.cpp#L135-L139`

### `expr.cast.byte-string-to-array` — Byte-string literal is [u8; N]

A byte-string literal `b"..."` at expression position lowers to an array literal of type `[u8; N]` (escapes decoded).

*Source:* `tools/peg_gen/grammars/logos.peg#L302`

## Dereference

### `expr.deref.box-move-out` — *box of a move-type element moves the value out

> NOTE: 2 rule artifacts share this id with differing statements/evidence. Both are surfaced below; reconcile during spec review.

**Variant 1.** `*b` where `b` is a bare variable of type `Box<T>` and T is a move (non-Copy) type consumes `b`, moves the value out, and frees the box (via `box_take::<T>` with T inferred). For a Copy element, `*b` copies and leaves `b` live.

*Source:* `src/compiler/sema_expr.cpp#L2688-L2725`

**Variant 2.** `let s = *b` where `b: Box<T>` and `T` is a move type moves the boxed value out (consuming `b`) and frees the block without dropping the content (Rust's built-in `*b` move). Copy-T Box and non-Box derefs use the normal copy/deref path.

*Source:* `src/compiler/sema_stmt.cpp#L1915-L1922`

### `expr.deref.non-pointer-identity` — Dereference of a non-pointer value is identity

`*x` where `x` is neither a raw pointer nor a reference (and has no Deref impl) yields `x` unchanged rather than an error.

*Divergence:* Rust rejects `*x` on a non-pointer; Logos relaxes it to identity to accept faithful Rust loop imports (B3-bg-07), since `for i in &v` already yields T not &T.

*Source:* `src/compiler/sema_expr.cpp#L2669-L2680`

### `expr.deref.prefix-star` — Dereference operator

Dereference is written with prefix `*`: `*expr`.

*Source:* `src/compiler/sema_render.cpp#L314-L316`

### `expr.deref.raw-ptr-unsafe` — Raw pointer dereference requires unsafe

`*p` where `p` is a raw pointer `*T` is only permitted inside an unsafe context; otherwise it is an error. The result type is the pointee type.

*Source:* `src/compiler/sema_expr.cpp#L2681-L2685`

### `expr.deref.user-deref-impl` — Dereference routes through a Deref impl

`*x` for any type implementing Deref (Box/Rc/Arc/user, including generic impls) lowers to `*(x.deref())` via the generic-aware method machinery.

*Source:* `src/compiler/sema_expr.cpp#L2658-L2668`

## Address-of

### `expr.addr-of.index-place` — &f[i] over user Index is a place reference

`&f[i]` over a value implementing the Index trait yields the place reference returned by `index()` directly (no intermediate deref or temporary).

*Source:* `src/compiler/sema_expr.cpp#L2554-L2558`

### `expr.addr-of.mut-array-whole` — &mut arr references the whole array

`&mut arr` for `arr: [T; N]` produces `&mut [T; N]` (a reference to the whole array); coercion to a `&mut [T]` slice parameter occurs separately at the call site.

*Source:* `src/compiler/sema_expr.cpp#L1107-L1116`

### `expr.addr-of.mut-deref-reborrow` — &mut *p reborrows through a pointer/reference

`&mut *p` where p is a Ptr/MutRef/Ref preserves an explicit AddrOfTemp(Deref(p)) shape so it is treated as a reborrow (distinct from a rebind), yielding `&mut Pointee`; for a struct with a DerefMut impl it lowers to `p.deref_mut()`.

*Source:* `src/compiler/sema_expr.cpp#L1118-L1139`

### `expr.addr-of.range-index-identity` — &a[range] is the slice value itself

`&e` where `e` is a range-index `a[i..j]` that already has slice kind `&[T]` yields that slice value unchanged (no additional reference wrapper), since the slice kind is already the borrowed fat form.

*Source:* `src/compiler/sema_expr.cpp#L2562-L2569`

### `expr.addr-of.static` — Address-of a module static is the global's stable address

`&S` where S is an unshadowed module static of non-array type yields the global's own address (a `'static` reference), preserving address identity; the reference is `&mut` iff S is a `mut` static. (No fresh stack copy is materialized.)

*Source:* `src/compiler/sema_expr.cpp#L2498-L2509`

### `expr.addr-of.static-mut` — &mut on a module static yields the global address

`&mut STATIC` for an unshadowed module static (that is not an array) produces a `&mut T` to the global's address rather than materializing a temporary.

*Source:* `src/compiler/sema_expr.cpp#L1100-L1106`

### `expr.addr-of.temp-materialize` — &<rvalue> spills to a stack temporary

`&e` for a non-place expression `e` materializes `e` into a stack temporary and yields `&typeof(e)`. If `typeof(e)` is an error type the expression is an error.

*Source:* `src/compiler/sema_expr.cpp#L2559-L2588`

## Range expressions

### `expr.range.desugar-range-struct` — lo..hi / lo..=hi desugar to stdlib Range constructors

A range expression requires integer bounds. Exclusive `lo..hi` lowers to `range_i32`/`range_i64`; inclusive `lo..=hi` lowers to the generic `range_incl_of` (RangeOfIncl<T>). The bound width is i64 if either bound is wider than 32 bits or an integer literal overflows i32, else i32; both bounds are widened to that bound type. Missing stdlib constructors are an error.

*Divergence:* Ranges are nominal stdlib structs (RangeI32/RangeI64/RangeOfIncl), not language built-ins

*Source:* `src/compiler/sema_expr.cpp#L1310-L1387`

### `expr.range.family` — Range expressions

Range value-expressions: `lo..hi` (half-open), `lo..=hi` (inclusive), `lo..` (from), `..hi` (to), `..=hi` (to-inclusive), `..` (full). An omitted side leaves the corresponding bound unspecified. Sema lowers each to a stdlib Range struct implementing `Iterator`. Range sits at the top of the value-expression precedence cascade (below it: logical operators).

*Source:* `tools/peg_gen/grammars/logos.peg#L2392-L2409`

## Assignment

### `expr.assign.compound-op-set` — Compound assignment operators

The compound-assignment operators are `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`. A compound-assign statement is `place OP value ;` where `place` is an atom (postfix-chained lvalue) and `value` is a full `expr`.

*Source:* `tools/peg_gen/grammars/logos.peg#L2324-L2327`

### `expr.assign.dataref-field-unsafe` — DataRef<ZonedStruct> field write desugars via mut_ptr and needs unsafe

`p.field = v` where `p: DataRef<Z>` with `Z` a zoned struct desugars to `{ let t = p.mut_ptr(); (*t).field = v; }` (the DerefMut analog); it requires an `unsafe` context, `p` must be a mutable binding, and `v` must be type-compatible with the field type.

*Source:* `src/compiler/sema_stmt.cpp#L7170-L7211`, `src/compiler/sema_stmt.cpp#L7303-L7312`

### `expr.assign.deref-write` — Dereference write statement

`* p = v ;` writes value `v` through dereferenced place `p` (a `unary_expr`). `* p OP v ;` performs compound assignment through a bare dereference and is defined to lower to `*p = *p OP v`.

*Divergence:* Logos addition: distinct DEREF_WRITE/DEREF_COMPOUND statement forms; semantics match Rust place-expression assignment.

*Source:* `tools/peg_gen/grammars/logos.peg#L2335-L2340`

### `expr.assign.drop-before-replace` — Field assignment drops old value first

Assigning to a field place over an owned local root drops the place's prior value before the store, provided the value is live (root owned, definitely-initialized, no overlapping moved-out path) and droppable; assigning to a path also lifts drop-suppression for the covered (equal-or-deeper) moved paths so the scope-end drop releases the new value.

*Divergence:* Rust-conformant (expr.assign.drop-target / B8)

*Source:* `src/compiler/sema_stmt.cpp#L7237-L7299`, `src/compiler/sema_stmt.cpp#L7450-L7455`, `src/compiler/sema_stmt.cpp#L7461-L7462`

### `expr.assign.index-mut-desugar` — Indexed assignment uses IndexMut

For a type implementing `IndexMut`, `a[i] = v` desugars to a store through `*index_mut(&mut a, i)` (the trait method produces the writable place); the receiver `a` must be a mutable binding.

*Source:* `src/compiler/sema_stmt.cpp#L7106-L7163`, `src/compiler/sema_stmt.cpp#L7225-L7236`

### `expr.assign.place-nesting-bound` — Deeply-nested assignment targets rejected

A place-write target is accepted only for shapes the address-of machinery can lower: a bare variable, `*p`, and index/field/tuple-index chains bounded over those roots; deeper nestings (e.g. 3-level `a[i][j][k]`, `g.rows[i].cells[j].v`) are rejected with a clean diagnostic rather than miscompiled.

*Uncertainty:* The exact accepted shape set is defined by place_write_supported/place_field_base_ok recursion; bound is an implementation limitation, not a language-design boundary.

*Source:* `src/compiler/sema_stmt.cpp#L6929-L6940`, `src/compiler/sema_stmt.cpp#L7316-L7321`

### `expr.assign.place-only` — Assignment LHS must be an assignable place

The left side of a compound place assignment must be a genuine lvalue shape: an index `a[i]`, field access `a.f`, tuple index `a.N`, or dereference `*p`. Any other LHS (call result, literal, arithmetic) is rejected as 'not an assignable place'.

*Source:* `src/compiler/sema_stmt.cpp#L7213-L7224`

### `expr.assign.type-mismatch` — Assignment value must match place type

The assigned value's type must be compatible with the place's type (modulo `#[rel_ptr]`↔`*T` relations); otherwise a type-mismatch error is raised. Before the store the value is integer-widened to the place type, and the place type hints enum/struct literal RHS resolution.

*Source:* `src/compiler/sema_stmt.cpp#L7354-L7373`, `src/compiler/sema_stmt.cpp#L7448`

### `expr.assign.union-field-safe` — Writing a union field is safe

Writing to a union field is safe (no `unsafe` required for the write): the place-write LHS suppresses the union unsafe gate that otherwise applies when reading a union field.

*Divergence:* Rust-conformant (items.union.fields.write-safety)

*Source:* `src/compiler/sema_stmt.cpp#L7325-L7331`

## Compound assignment

### `expr.compound-assign.base-op-strip` — Compound-assign base operator

A compound-assign token `op=` denotes the binary operator `op` obtained by stripping the trailing `=`; the place is the receiver and the right side is the value operand.

*Source:* `src/compiler/sema_stmt.cpp#L2277-L2294`

### `expr.compound-assign.index-mut-dispatch` — Compound-assign through IndexMut on a struct

`a[i] op= v` where `a` has struct type with an `IndexMut` impl lowers to `*index_mut(&mut a, i) = (*index(&a, i)) op v`, using the `Index` read accessor for the current value when present (else `index_mut`); `a` must be `mut`, and the rhs must be compatible with the indexed output type.

*Source:* `src/compiler/sema_stmt.cpp#L2400-L2467`

### `expr.compound-assign.op-trait-mapping` — Compound-assign operator → *Assign trait/method

Each compound-assign operator `op=` maps to a trait + method: `+=`→AddAssign::add_assign, `-=`→SubAssign::sub_assign, `*=`→MulAssign::mul_assign, `/=`→DivAssign::div_assign, `%=`→RemAssign::rem_assign, `&=`→BitAndAssign::bitand_assign, `|=`→BitOrAssign::bitor_assign, `^=`→BitXorAssign::bitxor_assign, `<<=`→ShlAssign::shl_assign, `>>=`→ShrAssign::shr_assign. Operators outside this set have no *Assign trait.

*Source:* `src/compiler/sema_stmt.cpp#L2260-L2274`

### `expr.compound-assign.opassign-dispatch` — Compound-assign dispatches via *Assign impl when present

For a place of struct type S, if an impl of the operator's *Assign trait exists for S (matched by concrete or base struct name), `place op= rhs` lowers to the in-place call `op_assign(&mut place, rhs)` (void result, no assign-back). The trait method's Rhs parameter need not equal Self: the impl is selected by the actual rhs operand type, falling back to the Self-Rhs signature if the rhs-typed one does not resolve.

*Divergence:* Rust-conformant operator-overload semantics; Logos struct-name-keyed impl lookup.

*Source:* `src/compiler/sema_stmt.cpp#L2315-L2351`, `src/compiler/sema_stmt.cpp#L2484-L2509`

### `expr.compound-assign.opassign-fallback-binop` — Compound-assign without *Assign impl desugars to read-modify-write

Absent a matching *Assign impl, `place op= rhs` desugars to `place = (place) op rhs` (read-twice / double-eval of the place), dispatching `op` through the corresponding binary-operator trait (Add/Sub/…), which constructs a fresh Self.

*Source:* `src/compiler/sema_stmt.cpp#L2304-L2305`, `src/compiler/sema_stmt.cpp#L2361-L2363`, `src/compiler/sema_stmt.cpp#L2511-L2525`

### `expr.compound-assign.place-too-nested` — Compound-assign target nesting limit

A compound-assign target too deeply nested to write in place is rejected with guidance to bind an intermediate `&mut` reference.

*Uncertainty:* Implementation-capability limit rather than a designed language restriction.

*Source:* `src/compiler/sema_stmt.cpp#L2472-L2477`

### `expr.compound-assign.type-mismatch` — Compound-assign RHS type-compatibility

In the read-modify-write path, the rhs type must be compatible with the place type; otherwise "compound assignment: type mismatch — expected T, got U".

*Source:* `src/compiler/sema_stmt.cpp#L2353-L2360`, `src/compiler/sema_stmt.cpp#L2512-L2518`

### `expr.compound-assign.var-immutable` — Compound-assign requires a mutable place

`x op= e` requires `x` to be declared `mut`; an immutable target is rejected: "compound assignment to immutable variable".

*Source:* `src/compiler/sema_stmt.cpp#L2301-L2302`, `src/compiler/sema_stmt.cpp#L2416-L2417`

### `expr.compound-assign.var-undefined` — Compound-assign to undefined variable is an error

`x op= e` where `x` is not a bound variable is rejected: "compound assignment to undefined variable".

*Source:* `src/compiler/sema_stmt.cpp#L2295-L2300`

## Field access

### `expr.field.autoderef-via-deref` — Field access auto-derefs through Deref

For receiver `r` of struct type S that has no field `f` but `S: Deref<Target=U>`, `r.f` is equivalent to `(*r).f`; the deref step repeats (bounded, up to 16 levels) until a type bearing field `f` is reached. Generalizes Box/Rc/Arc and any user Deref uniformly.

*Related:* `expr.field.ref-peel`

*Source:* `src/compiler/sema_expr.cpp#L9166-L9181`

### `expr.field.dataref-ergonomic-read` — DataRef<T> ergonomic field read

For receiver `p: DataRef<T>` where T is a zoned struct having field `f`, `p.f` is equivalent to `p.ptr().f`. The access requires an `unsafe` context.

*Uncertainty:* DataRef is a Logos-specific zone/Writ type; no direct Rust analogue.

*Source:* `src/compiler/sema_expr.cpp#L9440-L9458`

### `expr.field.dot-access` — Field access

Named field access is `receiver.field`.

*Source:* `src/compiler/sema_render.cpp#L282-L295`

### `expr.field.dst-prefix-positional` — Prefix (non-tail) field access on a DstRef is positional

For a fat-pointer receiver to a custom-DST struct, a non-tail prefix field is addressed positionally: its byte offset is computed by walking the sized prefix fields (with the DstRef's type-args substituted), and the field is read by dereferencing `data_ptr + offset` typed as the field type. This works uniformly for generic and non-generic DST instances, including those with no registered monomorphized layout.

*Divergence:* Custom-DST model — see DIVERGENCES B2.

*Source:* `src/compiler/sema_expr.cpp#L9394-L9429`

### `expr.field.dst-ref-unsafe` — Field read through a non-self-describing &DstStruct requires unsafe

Field access on a fat-pointer (DstRef) receiver `&DstStruct` requires an `unsafe` context, EXCEPT when the struct is `#[self_describing]` (its tail length is recovered in-band, so the borrow is a complete safe reference).

*Divergence:* Custom-DST raw-pointer-shaped field access — see DIVERGENCES B2.

*Source:* `src/compiler/sema_expr.cpp#L9275-L9281`

### `expr.field.dst-tail-dyn` — dyn-tail projection on a DstRef shares the carried vtable

For a fat-pointer receiver to a custom-DST struct whose last field has unsized-dyn type `dyn Tr`, `r.tail` yields a `&dyn Tr` fat pair `{ data = base + prefix_byte_size, vtable = the receiver's OWN carried vtable }`. The tail's metadata is the wide pointer's metadata (no static vtable lookup). The dyn prefix offset is aligned to pointer width (8) since the concrete payload alignment is not known statically.

*Divergence:* Custom-DST dyn-tail model — see DIVERGENCES B2/B3.

*Uncertainty:* Conservative 8-byte alignment for dyn tails noted as over-aligning vs Rust.

*Source:* `src/compiler/sema_expr.cpp#L9330-L9335`, `src/compiler/sema_expr.cpp#L9346-L9368`

### `expr.field.dst-tail-slice` — Slice-tail projection on a DstRef

For a fat-pointer receiver to a custom-DST struct whose last field `tail` has unsized-slice type `[T]`, `r.tail` yields a slice `{ data_ptr + prefix_byte_size, len }` reusing the fat pointer's len half; prefix_byte_size is the offset after all sized prefix fields, aligned to size_of(T) (capped at 8). Slice mutability follows the receiver: `(&mut Foo).tail: &mut [T]`, `(&Foo).tail: &[T]`.

*Divergence:* Custom-DST model — see DIVERGENCES B2.

*Source:* `src/compiler/sema_expr.cpp#L9296-L9345`, `src/compiler/sema_expr.cpp#L9369-L9393`

### `expr.field.hoist-droppable-rvalue-temp` — Droppable fresh-rvalue field base is hoisted to a statement temp

When a field is read off a fresh owned rvalue base of a move (droppable) type (`make().x`), the base is hoisted into a named statement-scoped temporary so it lives to end of statement and its Drop runs at scope exit; the field is then read from that local. A place or borrow base is left untouched.

*Source:* `src/compiler/sema_expr.cpp#L9151-L9164`

### `expr.field.name-from-field-or-name-slot` — Field name resolved from FIELD then NAME slot

The accessed field name is taken from the FIELD slot; if empty (e.g. a substituted antiquotation that landed at the field-name position via NAME_VAR→NAME rewrite), it falls back to the NAME slot.

*Uncertainty:* Fallback is a metaprog-substitution artifact, not a user-facing surface rule.

*Source:* `src/compiler/sema_expr.cpp#L9147-L9150`

### `expr.field.not-a-struct-error` — Field read receiver must be a struct/class

A field read whose receiver does not resolve to a struct or class type is an error ('receiver is not a struct or class'), except during metaprog discovery when the receiver (or its pointee) is already of error type, in which case the error type is propagated silently.

*Source:* `src/compiler/sema_expr.cpp#L9460-L9478`

### `expr.field.pub-access` — Private field access restricted to defining package

A non-`pub` field is accessible only within the package that defines the struct (checked via check_pub_access against the struct's package). Variadic field families (`name_<n>`) are matched by prefix for the access check.

*Related:* `module.vis.pub-field`

*Source:* `src/compiler/sema_expr.cpp#L9486-L9528`

### `expr.field.raw-ptr-unsafe` — Field read through raw pointer requires unsafe

Reading a field through a raw pointer receiver (type `*const T`/`*mut T`) is only permitted inside an `unsafe` context; otherwise it is an error.

*Source:* `src/compiler/sema_expr.cpp#L9182-L9184`, `src/compiler/sema_expr.cpp#L9251`

### `expr.field.ref-peel` — Field access peels reference layers

For receiver of reference-like type, `r.f` peels extra reference layers via explicit derefs so a multiply-referenced base (`&&S`) accesses the field of the underlying struct: `r.f` for `r: &&S` ≡ `(*r).f`. One reference layer remains for the single-level field projection.

*Related:* `expr.field.autoderef-via-deref`

*Source:* `src/compiler/sema_expr.cpp#L9252-L9264`

### `expr.field.self-describing-thin-tail` — Self-describing DST tail through a thin raw pointer

For a thin raw pointer `p: *const/*mut Self` to a `#[self_describing]` struct whose last field is the unsized-slice tail, `p.tail` yields a slice `{ (p as *u8)+prefix_offset, dst_len(p) }`, where prefix_offset is the natural-aligned byte offset after all sized prefix fields and the tail length is recovered by calling the struct's `SelfDescribing::dst_len` method. Slice mutability follows the pointer's mutability.

*Divergence:* Custom-DST / self-describing model — see DIVERGENCES B2.

*Related:* `expr.field.dst-tail-slice`

*Source:* `src/compiler/sema_expr.cpp#L9185-L9248`

### `expr.field.tuple-index` — Tuple / field access

Postfix `.field` reads a named field and `.N` (integer) reads the Nth tuple/tuple-struct element.

*Source:* `tools/peg_gen/grammars/logos.peg#L2684-L2685`, `tools/peg_gen/grammars/logos.peg#L2678-L2679`

### `expr.field.union-read-unsafe` — Union field read requires unsafe

Reading a field of a union requires an enclosing `unsafe` block (only one field is active at a time). Writing to a union field is safe; the read-safety check is suppressed when the access is the LHS of an in-place write.

*Source:* `src/compiler/sema_expr.cpp#L9495-L9509`

### `expr.field.unknown-field-error` — Unknown field is an error

Reading a field name not declared on the resolved struct type is an error ('struct S has no field f').

*Source:* `src/compiler/sema_expr.cpp#L9481-L9485`

## Tuple indexing

### `expr.tuple-index.access` — Tuple/tuple-struct .N indexing with auto-deref

`recv.N` indexes a tuple (auto-deref through `&`/`&mut`) returning the Nth element type, or reads field N of a tuple-struct (auto-deref through `&Foo`/`&mut Foo`) with the struct's type-params substituted by the receiver's type-args. An out-of-range index is an error.

*Source:* `src/compiler/sema_expr.cpp#L1636-L1697`

### `expr.tuple-index.dot-number` — Tuple index access

Tuple element access uses a numeric field after a dot: `receiver.N`.

*Source:* `src/compiler/sema_render.cpp#L297-L303`

## Index expressions

### `expr.index.autoderef` — Autoderef at index position through Deref

A struct receiver at index position without its own `Index` impl is dereferenced through its `Deref` impl(s) until an indexable type appears, mirroring method-resolution autoderef. The walk is bounded to 4 steps. If a step yields a Slice or trait-object (fat) value, that value is taken directly as the receiver.

*Source:* `src/compiler/sema_expr.cpp#L10399-L10424`

### `expr.index.bracket` — Index expression

Indexing is written `receiver[index]`.

*Source:* `src/compiler/sema_render.cpp#L305-L312`

### `expr.index.generic-index-via-method` — Generic-struct Index impl routed through method-call machinery

When a struct impls `Index` but no concrete `__index` symbol exists yet (a generic impl, e.g. `impl<T> Index for Vec<T>`), `v[i]` lowers to `*v.index(i)` via the method-call path. The element type is the impl's `Index<Idx, Output>` second trait-arg with the struct's type-args substituted for the impl's type params (matched positionally against `TypeVar`s in the impl target pattern); the index is widened to the substituted `Idx` when it is not a type variable.

*Related:* `expr.index.user-index-read`

*Source:* `src/compiler/sema_expr.cpp#L10454-L10485`

### `expr.index.indexmut-place` — Mutable index place requires IndexMut, shared requires Index

For an index place `&mut a[i]` the receiver type must impl `IndexMut`; for `&a[i]` an `Index` impl suffices. The place lowers to a call of the impl's `__index_mut` / `__index` method (the unique 2-parameter candidate), returning the reference produced by that method directly (no extra deref). Trait presence is checked against both the concrete struct name and the base (generic) struct name.

*Related:* `expr.index.user-index-read`

*Source:* `src/compiler/sema_expr.cpp#L10258-L10268`, `src/compiler/sema_expr.cpp#L10300-L10305`

### `expr.index.integer-required` — Built-in index requires an integer index

For built-in (non-user-Index) indexing the index expression must have integer type; otherwise an `array index must be integer` error is reported.

*Source:* `src/compiler/sema_expr.cpp#L10489-L10490`

### `expr.index.place-real-slot` — Index-place receiver uses the real variable slot

When the index-place receiver is a plain variable, its address is taken from the real variable slot (`&mut v`), not a spilled copy, so the mutation through `IndexMut` persists. A receiver already of reference/pointer kind is passed through unchanged; other receiver shapes materialize a temporary reference.

*Source:* `src/compiler/sema_expr.cpp#L10286-L10296`

### `expr.index.range-slice` — Range indexing produces a sub-slice

A range index `recv[lo..hi]`, `recv[lo..]`, `recv[..hi]`, `recv[..]`, or inclusive `recv[lo..=hi]` produces a sub-slice `&[T]` via `slice_get_range(recv, lo, hi)`. The receiver must be a slice, array (decayed to `&[T]` via addr-of + slice-coercion), or reference-to-slice; otherwise an error is reported. Missing `lo` defaults to 0; missing `hi` defaults to INT64_MAX (clamped to len); an inclusive upper bound is lowered as `hi+1`. Bounds are widened to i64. `slice_get_range` must be in scope (`use logos.lang.slice`).

*Divergence:* Range-slicing relies on stdlib `slice_get_range`; open/inclusive ends are clamped to length rather than panicking on out-of-range as Rust does.

*Source:* `src/compiler/sema_expr.cpp#L10328-L10389`

### `expr.index.raw-ptr-unsafe` — Indexing through a raw pointer requires unsafe

Indexing a value of raw-pointer kind (`*const`/`*mut`) is only permitted inside an `unsafe` context; outside one it is an error.

*Source:* `src/compiler/sema_expr.cpp#L10506-L10508`

### `expr.index.read` — Index expression

`e[i]` is a postfix index-read; with a range index (`s[a..b]`, `s[a..]`, `s[..b]`, `s[..]`) it produces a slice.

*Source:* `tools/peg_gen/grammars/logos.peg#L2690-L2691`, `tools/peg_gen/grammars/logos.peg#L2394-L2396`

### `expr.index.receiver-kind` — Built-in index receiver must be array, slice, or pointer/reference

A built-in index `a[i]` requires the receiver to be a Slice, Array, raw Ptr, or reference (`Ref`/`MutRef`); any other receiver kind is a type error. Slice indexing lowers to a dedicated slice-index operation; an array/ref/ptr yields the element type, auto-dereferencing a single reference/pointer layer (and through a `[T;N]` array pointee) to the element.

*Source:* `src/compiler/sema_expr.cpp#L10492-L10526`

### `expr.index.ref-to-slice-retype` — Indexing a reference-to-slice GEPs through the fat-pointer pair

When the receiver type is a reference to a slice (`Ref/MutRef -> Slice`, e.g. `&s` where `s: &[T]`), it is retyped to the pointee Slice rather than loaded, so `(&s)[i]` indexes the underlying `{data,len}` pair and yields element type `T` instead of the whole slice.

*Source:* `src/compiler/sema_expr.cpp#L10311-L10326`

### `expr.index.user-index-read` — Index read dispatches to user Index impl as *recv.index(i)

`a[i]` for a struct `a` that impls `Index<Idx, Output>` lowers to `*(a.index(i))`: the impl's `__index` method (unique 2-param candidate) is called with a materialized `&a` receiver and the index, and the result reference is dereferenced to yield the element place. The integer-literal index is widened to the formal index parameter type. User `Index` dispatch is attempted before the built-in integer-index check, so an impl may accept non-integer keys.

*Related:* `expr.index.indexmut-place`, `expr.index.generic-index-via-method`

*Source:* `src/compiler/sema_expr.cpp#L10396-L10453`

## Postfix

### `expr.postfix.chain` — Postfix operator chain

A primary expression may be followed by zero or more left-associative postfix suffixes: method call `.m(args)` (optionally `.m::<T>(args)` with explicit turbofish type args), expression-callee invocation `e(args)`, field read `.field`, tuple index `.N`, indexing `[i]`, and the try operator `?`. Chains parse left-to-right (`a.b.c`, `a.f().b`).

*Source:* `tools/peg_gen/grammars/logos.peg#L2658-L2694`

## Try operator

### `expr.try.heterogeneous-error-from` — ? converts inner error via From when error types differ

For `e?` with `e: Result<T,E_inner>` in a function returning `Result<U,E_outer>` where E_inner != E_outer, the Err path returns `Err(E_outer::from(err))`, requiring `impl From<E_inner> for E_outer`; absence of that impl is an error.

*Source:* `src/compiler/sema_expr.cpp#L1220-L1306`

### `expr.try.operator` — Try operator

Postfix `e?` is the try operator; it propagates the error/none case of a Result/Option-like value and yields the success payload.

*Source:* `tools/peg_gen/grammars/logos.peg#L2692-L2693`

### `expr.try.result-option-extract` — ? on Result/Option extracts or early-returns

`e?` where `e: Result<T,E>` extracts Ok(v) and early-returns Err(e); where `e: Option<T>` extracts Some(v) and early-returns None. It is valid only inside a function whose return type is the same enum (Result resp. Option); otherwise an error.

*Source:* `src/compiler/sema_expr.cpp#L1153-L1217`, `src/compiler/sema_expr.cpp#L1307`

### `expr.try.trait-dispatch-from-residual` — ? on non-Result/Option dispatches via Try/FromResidual

`e?` where e is neither stdlib Result nor Option desugars through the Try/FromResidual surface: `match e.branch() { Continue(c) => c, Break(r) => return RetType::from_residual(r) }`. The receiver RetType is taken from the enclosing function's declared return type (Logos does not infer trait Self from context); an undeterminable return type is an error.

*Divergence:* Receiver for from_residual is explicit from fn ret type (no contextual Self inference)

*Source:* `src/compiler/sema_expr.cpp#L1167-L1195`

## Function calls

### `expr.call.arg-coercions` — Implicit coercions applied per argument at a call

Each value argument is, in order, retyped if a bare payload-less enum literal, coerced closure→fn-ptr, array-ref↔slice coerced, implicitly mut-reborrowed, struct-unsize coerced (e.g. `Rc<A>`→`Rc<dyn Tr>`), and integer-widened toward the (substituted) parameter type before type checking.

*Related:* `coerce.unsize.struct-smart-ptr`

*Source:* `src/compiler/sema_expr.cpp#L4267-L4275`

### `expr.call.arg-count` — Call argument count must match

A non-variadic call must supply exactly as many value arguments as the function has parameters; a variadic call must supply at least the fixed parameter count. Otherwise it is an error.

*Source:* `src/compiler/sema_expr.cpp#L4235-L4237`, `src/compiler/sema_expr.cpp#L4262-L4265`

### `expr.call.arg-formal-hint-propagation` — Formal parameter types hint argument inference

When a free-function call's callee is uniquely resolvable (a generic entry, or exactly one candidate), each argument is lowered with the corresponding formal parameter type as an inference hint: a closure-literal arg adopts the formal's Fn-family signature (TypeVar formal: from its Fn-family bound; FnPtr/Closure formal: used directly), a payload-carrying enum-literal arg adopts a fully-concrete enum formal, a tuple-literal arg adopts a Tuple formal, and an array-literal arg adopts the element type of a Slice/Array formal with non-TypeVar element. Hints from generic (unresolved) formals are NOT applied.

*Uncertainty:* Hint applicability conditions inferred from the per-kind lambdas; exact resolution precedence (generic vs single-candidate) is implementation-derived.

*Source:* `src/compiler/sema_expr.cpp#L3026-L3113`

### `expr.call.arg-type-compatible` — Each argument must be type-compatible with its formal

> NOTE: 2 rule artifacts share this id with differing statements/evidence. Both are surfaced below; reconcile during spec review.

**Variant 1.** After coercion, each argument's type must be compatible with the corresponding parameter type (or satisfy a `&T`→`dyn` reference match); incompatibility is an error 'expected E, got G'. Error-typed args/params are exempt.

*Source:* `src/compiler/sema_expr.cpp#L3247-L3256`, `src/compiler/sema_expr.cpp#L3503-L3513`

**Variant 2.** After coercions, each argument's type must be compatible with the (substituted) parameter type; an incompatible non-error, non-TypeVar, non-AssocType parameter yields an "expected X, got Y" error. Variance is additionally checked for non-TypeVar/non-AssocType parameters.

*Source:* `src/compiler/sema_expr.cpp#L4276-L4287`

### `expr.call.arg-variance-check` — Argument passing enforces variance

Each argument/parameter pair is variance-checked at the call site (lifetime/subtyping soundness).

*Source:* `src/compiler/sema_expr.cpp#L3234`, `src/compiler/sema_expr.cpp#L3257`, `src/compiler/sema_expr.cpp#L3514`

### `expr.call.arity-exact` — Non-vararg call arity must match

For a non-vararg function, the argument count must equal the declared parameter count; otherwise an error 'expected N args, got M'.

*Source:* `src/compiler/sema_expr.cpp#L3242-L3244`, `src/compiler/sema_expr.cpp#L3499-L3501`

### `expr.call.arity-vararg-minimum` — Vararg call requires at least the fixed-parameter count

For a vararg function, the argument count must be >= the number of declared (fixed) parameters; fewer is an error 'expected at least N args, got M'. Only the fixed parameters are type-checked against formals.

*Source:* `src/compiler/sema_expr.cpp#L3219-L3241`, `src/compiler/sema_expr.cpp#L3475-L3498`

### `expr.call.callable-arg-move` — By-value move-type arguments to a callable are moved

In a closure/fn-ptr call `f(args)`, a by-value concrete move-type argument transfers ownership into the callee (source marked moved). By-reference parameters and TypeVar-typed arguments are excluded.

*Source:* `src/compiler/sema_expr.cpp#L2979-L2997`

### `expr.call.callable-arity-and-args` — Closure/fn-ptr call arity and argument typing

A closure/fn-ptr call requires argument count to equal the callable's parameter count; each argument is coerced to its parameter type and must be type-compatible; the result type is the callable's return type, or `()` (void) if absent.

*Source:* `src/compiler/sema_expr.cpp#L2928-L2978`, `src/compiler/sema_expr.cpp#L2998-L3000`

### `expr.call.callable-autoderef-ref` — Call auto-derefs a reference to a callable

`x(args)` where `x: &fn(..)` / `&mut fn(..)` / `&F` (reference to a callable or Fn-bounded param) auto-dereferences the reference to load the inner fn-pointer/closure before calling.

*Source:* `src/compiler/sema_expr.cpp#L2864-L2880`, `src/compiler/sema_expr.cpp#L2892-L2904`

### `expr.call.callable-field` — Call of a callable struct field

If `s.m(args)` finds no method `m` but struct `s` has a field named `m` whose type is a fn-pointer/fn-value or closure, the expression is lowered as a field read followed by a fn-ptr call (fn-value kind) or closure call (closure kind), returning that callable's return type.

*Divergence:* Rust requires explicit `(s.m)(args)` to call a callable field; bare `s.m(args)` is method-only

*Source:* `src/compiler/sema_expr.cpp#L8701-L8728`

### `expr.call.callable-resolution` — Callee resolution to closure or fn-pointer

A call `x(args)` treats `x` as callable when its type is a Closure or fn-value kind; `Box<dyn Fn*>` (Box<Closure>) is unwrapped to its inner Closure, and an Fn-bounded generic type-param `F` is treated as a closure with the bound's `fn_params`/`fn_ret` signature.

*Divergence:* A10: dyn Fn* collapses to the bare Closure type.

*Source:* `src/compiler/sema_expr.cpp#L2845-L2926`

### `expr.call.closure-hint-from-fn-bound` — Closure param/return types inferred from callee Fn-family bound

For a generic free fn `fn f<F>(g: F) where F: FnOnce(A)->R`, an un-annotated closure argument infers its parameter and return types from the bound's Fn-family signature `(A)->R` (missing return → unit).

*Source:* `src/compiler/sema_expr.cpp#L3031-L3051`

### `expr.call.divergent-never-return` — A call diverges iff callee is `panic` or returns Never

A CALL or FN_MACRO_CALL node is divergent (control never returns past it) iff the callee is the macro `panic` or any candidate fn for the callee has return type Never (!). Marker macros unreachable!/todo!/unimplemented! lower through panic! and are handled via the panic fast-path.

*Source:* `src/compiler/sema.cpp#L1702-L1722`

### `expr.call.intlit-fit-aggregate` — Integer-literal elements of array/tuple args must fit narrowed element types

When an array-literal or tuple-literal argument is checked against an Array/Tuple formal, each untyped integer-literal element (recursively through nested arrays/tuples) must fit the corresponding narrowed element type; overflow is an error naming the element index.

*Source:* `src/compiler/sema_expr.cpp#L3263-L3322`, `src/compiler/sema_expr.cpp#L3520-L3579`

### `expr.call.intlit-fit-scalar` — Integer-literal argument must fit the formal's integer type

An untyped integer-literal argument coerced to an integer parameter type is an error if its value does not fit that type's range ('value V does not fit in T').

*Source:* `src/compiler/sema_expr.cpp#L3235-L3239`, `src/compiler/sema_expr.cpp#L3515-L3519`

### `expr.call.intlit-fits` — Integer-literal argument must fit the parameter type

An integer-literal argument (including literal elements nested in array- and tuple-literal arguments, recursively) must fit within the target integer type; a value out of range is an error.

*Source:* `src/compiler/sema_expr.cpp#L4288-L4293`, `src/compiler/sema_expr.cpp#L4294-L4353`

### `expr.call.macro-overloads-not-callable-as-fn` — fn_macro/token_macro overloads are not callable via plain call syntax

A `#[fn_macro]` or `#[token_macro]` overload of a name is invocable only via `name!(...)` syntax; plain `name(...)` call resolution excludes such overloads.

*Source:* `src/compiler/sema_expr.cpp#L3336-L3344`

### `expr.call.move-by-value-args` — By-value move-type arguments are marked moved

By-value arguments of move (non-Copy) type at a call are marked moved so their scope-exit Drop does not fire on storage whose ownership transferred to the callee.

*Related:* `borrow.move.by-value-call`

*Source:* `src/compiler/sema_expr.cpp#L4358-L4363`

### `expr.call.overload-best-match-scoring` — Non-generic call resolution scores exact > compatible, errors on ambiguity

Among arity-matching non-generic candidates, each argument scores 2 if types_equal to the parameter else 1 if types_compatible (unless exact_only) else rejects the candidate; the candidate with the highest minimum-of-argument score wins. A tie is ambiguous and is broken by preferring a candidate whose package equals the current package (local shadows imported); an unresolved tie is a compile error "ambiguous call to '<name>'".

*Source:* `src/compiler/sema.cpp#L1724-L1793`

### `expr.call.prelude-enum-shorthand` — Some/Ok/Err call shorthand constructs enum literals

When `Some`, `Ok`, or `Err` is not resolvable as a function, the call is treated as the corresponding `Option::Some` / `Result::Ok` / `Result::Err` enum-variant literal (honoring any enum type hint for parameter substitution). `None` is not handled here (it is a bare-ident path).

*Source:* `src/compiler/sema_expr.cpp#L3381-L3403`

### `expr.call.pub-access-check` — Free-function call respects visibility

A free-function call checks the callee's pub/package/module-only visibility against the call site; an inaccessible callee is an error.

*Source:* `src/compiler/sema_expr.cpp#L3216`, `src/compiler/sema_expr.cpp#L3406-L3411`

### `expr.call.static-turbofish-before-method` — Static-call turbofish precedes method name

In an associated/static call, turbofish type arguments attach to the receiver type and precede the `::method` segment: `Recv::<T>::method(args)`.

*Divergence:* Rust places the turbofish after the method for trait/inherent fns (e.g. T::method::<U>); Logos surface form puts it before the method name on the type path.

*Source:* `src/compiler/sema_render.cpp#L203-L241`

### `expr.call.tuple-struct-ctor` — Tuple-struct constructor call

`Foo(a0, .., a_{n-1})` where Foo is a tuple struct constructs a struct literal with positional fields named "0".."n-1"; argument count must equal the field count; for a generic tuple struct the struct type-args are inferred by unifying each argument type against the declared field type.

*Source:* `src/compiler/sema_expr.cpp#L2783-L2842`

### `expr.call.turbofish-free-fn` — Free-function turbofish placement

Explicit type arguments to a free-function call use turbofish after the callee name and before the argument list: `callee::<T1, T2>(args)`.

*Source:* `src/compiler/sema_render.cpp#L172-L201`

### `expr.call.undefined-function-error` — Call to an undefined function is an error

A call whose callee resolves to no function (and is not a prelude enum shorthand) is an error 'call to undefined function', except in metaprog mode where it is permitted to pass through with error type.

*Source:* `src/compiler/sema_expr.cpp#L3377-L3404`

### `expr.call.unsafe-context` — Calling an unsafe function requires unsafe context

A call to a function marked `unsafe` is an error unless it occurs inside an unsafe context; this applies to both inferred and explicit-turbofish call paths.

*Source:* `src/compiler/sema_expr.cpp#L3995-L3997`

### `expr.call.unsafe-context-required` — Calling an unsafe fn requires an unsafe context

A call to a function declared `unsafe` is an error unless it occurs inside an unsafe context.

*Source:* `src/compiler/sema_expr.cpp#L3217-L3218`, `src/compiler/sema_expr.cpp#L3409-L3410`

## Method calls

### `expr.method.arg-type-compat` — Method argument type compatibility

After coercion, each method argument type must be compatible with its substituted parameter type; an incompatibility is an error.

*Source:* `src/compiler/sema_expr.cpp#L8888-L8896`

### `expr.method.arity-check` — Method call argument count must match

> NOTE: 2 rule artifacts share this id with differing statements/evidence. Both are surfaced below; reconcile during spec review.

**Variant 1.** A method call must supply exactly (param count − 1) explicit arguments (excluding the `self` receiver); a mismatch is an error.

*Source:* `src/compiler/sema_expr.cpp#L7492-L7497`

**Variant 2.** A method call must supply exactly `param_count - 1` explicit arguments (excluding the `self` receiver); a mismatch is an error.

*Source:* `src/compiler/sema_expr.cpp#L8867-L8871`

### `expr.method.array-len-builtin` — len() on a fixed array is a compile-time constant

`a.len()` where `a` has fixed-array type `[T; N]` evaluates to the compile-time size `N` as an `i64` literal; no runtime call is emitted.

*Divergence:* Result type is i64 (Logos stdlib uses i64 for lengths), not usize as in Rust.

*Source:* `src/compiler/sema_expr.cpp#L7280-L7284`

### `expr.method.auto-ref-self` — Auto-reference/auto-address receiver for &Self / &mut Self / *Self methods

If the resolved method's first formal parameter is `&Self`/`&mut Self` (or `*const Self`/`*mut Self`) and the receiver is a non-reference, non-pointer value, the receiver is automatically taken by reference (resp. raw address-of) with the matching mutability before the call.

*Related:* `expr.method.autoref-ladder`

*Source:* `src/compiler/sema_expr.cpp#L8283-L8294`, `src/compiler/sema_expr.cpp#L8303-L8324`, `src/compiler/sema_expr.cpp#L8581-L8589`

### `expr.method.autoderef-lowest-priority` — By-value-self via auto-deref is lowest dispatch priority

A method whose `self` is by value, reachable only by auto-dereferencing a `&T`/`&mut T`/`*T` receiver, is selected only if no exact or auto-ref candidate at the current deref level matches. When chosen, the receiver is auto-dereferenced (copying/moving the pointee out, subject to downstream Copy/move borrow checks).

*Divergence:* Mirrors Rust autoderef order: try T/&T/&mut T at a deref level before stepping deeper.

*Related:* `expr.method.autoref-ladder`

*Source:* `src/compiler/sema_expr.cpp#L8484-L8491`, `src/compiler/sema_expr.cpp#L8524-L8557`, `src/compiler/sema_expr.cpp#L8563-L8580`

### `expr.method.autoref-ladder` — Method receiver auto-ref ladder

When resolving `r.m(args)`, candidate receiver types are tried in order: the receiver type T as-is, then `&T`, then `&mut T` (and for primitive/raw receivers also `*const T`, `*mut T`). The first signature-matching method wins; if matched against an autoref'd variant, the receiver is wrapped with the corresponding `&`/`&mut` address-of before the call.

*Related:* `expr.method.autoderef-lowest-priority`, `expr.method.auto-ref-self`

*Source:* `src/compiler/sema_expr.cpp#L8137-L8154`, `src/compiler/sema_expr.cpp#L8386-L8420`, `src/compiler/sema_expr.cpp#L8503-L8520`

### `expr.method.blanket-on-primitive` — Value blanket impls dispatch on primitive receivers

A value blanket impl (`impl<T> Trait for T`) is reachable on a primitive receiver (enabling From→Into, TryFrom→TryInto, identity Borrow, etc.) before the not-a-struct error is reported.

*Source:* `src/compiler/sema_expr.cpp#L8330-L8335`

### `expr.method.deref-autoderef-resolution` — Method resolution autoderefs through Deref/DerefMut

If the receiver is a struct with no direct method named `m` (no candidate keyed by concrete or base struct name), and the struct implements `Deref<Target>`, the receiver is dereferenced to `Target` and resolution retries; iterated up to a fixed bound (16). A method defined on the outer type always wins over a Deref-target method.

*Source:* `src/compiler/sema_expr.cpp#L7203-L7238`

### `expr.method.deref-step-prefers-mut` — Per-step DerefMut chosen when target method needs &mut self

At each autoderef step, if the Deref target has a candidate method `m` whose first parameter is `&mut Self` and the receiver type implements DerefMut, the mutable DerefMut step is taken so the resulting receiver is a mutable place (`&mut Target`) rather than the shared `&Target` an immutable Deref would yield. Falls back to Deref when no DerefMut impl exists.

*Source:* `src/compiler/sema_expr.cpp#L7170-L7202`, `src/compiler/sema_expr.cpp#L7234-L7237`

### `expr.method.generic-struct-base-fallback` — Generic-struct methods resolvable under the base type name

For a receiver of a monomorphized generic struct type (e.g. `Foo$G1$i32`), if no method is found under the concrete name, methods registered under the base struct name (`Foo`) are tried, with the struct's type parameters substituted from the receiver's type arguments.

*Source:* `src/compiler/sema_expr.cpp#L8460-L8478`, `src/compiler/sema_expr.cpp#L8591-L8651`

### `expr.method.intlit-fits` — Integer-literal argument range check

An integer-literal argument (including elements of array/tuple literals, recursively) must fit in the target integer parameter type; an out-of-range literal is an error.

*Source:* `src/compiler/sema_expr.cpp#L8897-L8961`

### `expr.method.mut-ref-to-shared-demotion` — &mut T receiver may call a &self method

A `&mut T` receiver may dispatch to a method declared on `&T` (shared self): for resolution the `&mut T` is coerced to `&T` (same pointee, weaker mutability); the receiver value is reused unchanged since `&mut`/`&` share ABI.

*Source:* `src/compiler/sema_expr.cpp#L8231-L8245`

### `expr.method.no-method-error` — No method on receiver type

If no method, blanket-impl, multi-trait collision, or callable field matches `s.m`, the call is an error "'S' has no method 'm'".

*Source:* `src/compiler/sema_expr.cpp#L8729-L8730`

### `expr.method.not-a-struct-error` — Method on non-struct receiver with no resolution is an error

If no method resolves for a primitive/non-struct receiver, it is a compile error 'receiver is not a struct'. Exception: in metaprog mode, an `<error>`-typed receiver (or `&`/`*` to an `<error>` pointee) silently propagates `<error>` without diagnostic.

*Source:* `src/compiler/sema_expr.cpp#L8336-L8349`

### `expr.method.pub-access-check` — Method visibility enforced at call site

A resolved method call is subject to the method's pub/module-only visibility; calling a non-visible method from outside its allowed scope is an error.

*Source:* `src/compiler/sema_expr.cpp#L8734`

### `expr.method.raw-ptr-call-requires-unsafe` — Method call through a raw pointer requires unsafe

Dispatching a method when the receiver type is a raw pointer (`*const`/`*mut`), including `*mut dyn Trait`/`*const dyn Trait`, requires an `unsafe` context; outside `unsafe` it is an error. The raw pointer is peeled to its pointee for dispatch.

*Source:* `src/compiler/sema_expr.cpp#L7301-L7312`, `src/compiler/sema_expr.cpp#L7324-L7327`

### `expr.method.raw-ptr-recv-unsafe` — Method call through raw pointer requires unsafe

Calling a method on a receiver of raw-pointer type requires an `unsafe` context; otherwise it is an error. The raw pointer is auto-dereferenced to its pointee for method resolution.

*Source:* `src/compiler/sema_expr.cpp#L8743-L8746`

### `expr.method.receiver-multiref-autoderef` — Method receiver peels surplus reference layers

For a method call `r.m(...)`, if the receiver type is a (non-raw) reference-like type whose pointee is itself reference-like (`&&T`, `&&mut T`, etc.), the extra reference layers are removed by explicit derefs until a single reference layer remains: `r.m()` for `r:&&T` ≡ `(*r).m()`. Raw pointers (`*const`/`*mut`) are not peeled here.

*Source:* `src/compiler/sema_expr.cpp#L7124-L7130`

### `expr.method.ref-blanket-impl` — Generic reference blanket impl dispatch

An `impl<T> Trait for &T` is reachable from a reference receiver `&U`: T is bound to the pointee U, the receiver is auto-referenced, and the call is monomorphized with T=U.

*Related:* `expr.method.ref-impl-target`

*Source:* `src/compiler/sema_expr.cpp#L8156-L8177`

### `expr.method.ref-impl-target` — Dispatch to impls declared on reference receiver types

An `impl Trait for &T` (or `&mut T`) provides methods reachable by a `&T`/`&mut T` receiver; these are preferred over auto-deref to T. For a struct pointee both the concrete-arg form and the base form are tried; for a non-struct pointee the impl target is keyed by the full receiver type string.

*Source:* `src/compiler/sema_expr.cpp#L8156-L8161`, `src/compiler/sema_expr.cpp#L8358-L8379`, `src/compiler/sema_expr.cpp#L8396-L8409`

### `expr.method.ref-impl-typeparam-subst` — Reference-impl method binds pointee type args

When dispatching through a reference impl on a generic struct (`impl<T> Foo for &Pair<T>`), the impl/struct type parameters are bound from the pointee's type arguments; non-generic returns are substituted, generic methods are monomorphized with the derived args.

*Related:* `expr.method.ref-impl-target`

*Source:* `src/compiler/sema_expr.cpp#L8421-L8453`

### `expr.method.str-slice-alias` — str method lookup aliases &[u8]

When a receiver's type renders as `&[u8]` (the representation of `str`) and no method is found under that name, methods registered under `str__<method>` are tried as a fallback.

*Uncertainty:* str is modeled as Slice<u8>/&[u8]; alias is a representation detail surfaced as a resolution rule.

*Source:* `src/compiler/sema_expr.cpp#L8186-L8195`

### `expr.method.turbofish-bypasses-inference` — Method-level turbofish supplies explicit type args

A method call may carry an explicit turbofish `recv.m::<T1,T2>(args)`; the supplied type arguments become the method's type parameters and downstream per-arg type-param inference from argument types is bypassed.

*Source:* `src/compiler/sema_expr.cpp#L7241-L7265`, `src/compiler/sema_expr.cpp#L7504-L7510`

### `expr.method.turbofish-method-args` — Method turbofish supplies type args verbatim, else inferred

For a generic method `r.m::<A,..>(args)`, the explicit turbofish type arguments are used verbatim (positionally); missing trailing args are errors/placeholders. With no turbofish, method-level type args are inferred from arguments with seed `Self = typeof(recv)`; failure to infer is a compile error.

*Source:* `src/compiler/sema_expr.cpp#L8265-L8282`

### `expr.method.unsafe-context` — Calling an unsafe method requires an unsafe context

A method-call expression `r.m(..)` whose resolved method is declared `unsafe` is a compile error unless it occurs inside an unsafe context (`unsafe { .. }` block or unsafe fn).

*Source:* `src/compiler/sema_expr.cpp#L8259-L8261`

### `expr.method.unsafe-method-requires-unsafe` — Calling an unsafe trait method requires unsafe

Calling a trait method declared `unsafe` outside an `unsafe` context is an error.

*Source:* `src/compiler/sema_expr.cpp#L7487-L7490`

### `expr.method.unsafe-required` — Unsafe method requires unsafe context

Calling a method marked `unsafe` outside an `unsafe` context is an error.

*Source:* `src/compiler/sema_expr.cpp#L8735-L8736`

### `expr.method.vec-get-move-out-rejected` — Vec::get of a non-Copy element is rejected

`v.get(i)` on a receiver resolving (through one reference layer) to `Vec<E>` where `E` is a non-Copy (move) type is an error: it would move an element out of borrowed Vec storage, aliasing and double-freeing on drop. The fix is `.borrow(i)` for `&E`, or `.remove(..)`/`.pop()` to take ownership. Copy elements are permitted.

*Divergence:* Mirrors Rust's E0507 'cannot move out of index'; scoped here to Vec::get.

*Source:* `src/compiler/sema_expr.cpp#L7139-L7160`

## Method calls

### `expr.method-call.turbofish-after-name` — Method-call turbofish placement

A method call is `receiver.method(args)`; explicit type arguments are turbofish placed after the method name: `receiver.method::<T>(args)`.

*Source:* `src/compiler/sema_render.cpp#L243-L280`

## Static / associated calls

### `expr.static-call.arg-count-and-type-check` — Static call arity and per-argument type checking

A non-generic static call checks argument count against the parameter list (error on mismatch) and coerces then type-checks each argument against its parameter (error on incompatibility). By-value move-typed args (and owning Box<dyn>) are marked moved so scope-end drops do not fire on transferred locals.

*Source:* `src/compiler/sema_expr.cpp#L13621-L13643`

### `expr.static-call.array-default` — `<[E; N]>::default()` synthesizes elementwise default

`default()` with no args on an array type (named via a non-generic alias `type M = [E; N]`) synthesizes `[E::default(); N]`; if the element type has no Default impl it is an error. Arrays carry no `__default` symbol.

```logos
type M = [i32; 4]; let a = M::default();
```

*Source:* `src/compiler/sema_expr.cpp#L13183-L13196`

### `expr.static-call.enum-variant-vs-static-method` — `Enum::Name(...)` constructs a variant only when Name is a variant

When the class is an enum (directly or via a non-generic type-alias to an enum), `Enum::Name(args)` lowers as a variant construction iff Name matches a declared variant; otherwise it falls through to ordinary static-method resolution (trait-impl-on-enum).

*Source:* `src/compiler/sema_expr.cpp#L13113-L13146`

### `expr.static-call.generic-method-infers-type-args` — Generic static method infers concrete type-args outside generic context

A generic static method (type-params from the enclosing impl) called outside a generic context (no TypeVar/AssocType in value or explicit type-args) is resolved by turbofish args if present, else by argument inference, then routed through the generic-call finisher to trigger the concrete instantiation. Inside a generic body, it is emitted with TypeVar type-args (or turbofish) and the return type substituted, for mono to rename to the concrete struct method.

*Source:* `src/compiler/sema_expr.cpp#L13538-L13618`

### `expr.static-call.qualified-path-drops-package-prefix` — `pkg.path.Type::method()` resolves on the last segment as the type

In a qualified static call `pkg.path.Type::member(args)`, the LAST dotted segment names the type/class; the package prefix is dropped (type/method resolution and arg lowering are not package-filtered, only free-fn lookups are).

*Source:* `src/compiler/sema_expr.cpp#L13087-L13094`

### `expr.static-call.self-resolves-to-impl-type` — `Self::method()` resolves Self to the impl's concrete type

Inside an impl body, `Self::method()` resolves `Self` to the impl's concrete type name (struct/zoned-struct via concrete name, enum via enum name) before static-method resolution, equivalent to writing the type name.

*Source:* `src/compiler/sema_expr.cpp#L13099-L13111`

### `expr.static-call.trait-qualified-ufcs` — Trait-qualified UFCS `Trait::method(recv, ...)`

When the class names a TRAIT (not a struct/enum/datatype/type-param) and args are non-empty, `Trait::method(recv, ...)` dispatches on the first argument's concrete receiver type (auto-derefed through refs/ptrs): struct/zoned-struct by name, enum by name, or primitive by type_str. The rewrite to `<recv-type>__<method>` commits only if that concrete symbol actually resolves; otherwise normal resolution and error reporting proceed.

*Divergence:* Rust-conformant (DIVERGENCES.md: trait-qualified UFCS supported)

*Source:* `src/compiler/sema_expr.cpp#L13198-L13248`

### `expr.static-call.turbofish-concrete-partial-spec` — Turbofish on a partial-spec static call builds the concrete mangled name

For `Type::<A, B>::method(...)` where a concrete partial-spec impl registers methods under the concrete mangled name, if base lookup misses and all turbofish args are concrete (non-TypeVar), the concrete instantiation name (datatype vs struct) is built and the symbol re-resolved.

*Source:* `src/compiler/sema_expr.cpp#L13296-L13328`

### `expr.static-call.type-alias-resolution` — Static calls resolve non-generic type aliases to the target type

A non-generic type alias used as a static-call class resolves to its target struct/zoned-struct (using the concrete name when type-args are present) before mangling the method symbol.

*Source:* `src/compiler/sema_expr.cpp#L13149-L13164`

### `expr.static-call.type-param-shadows-struct` — In-scope abstract type-param shadows a same-name concrete type

A bounded type-param used as the static-call class (`S::method` with `S: Bound`) dispatches through the trait bound and NOT through a same-name concrete struct in scope; an active abstract type-param (resolves to a TypeVar) suppresses concrete-symbol lookup so resolution falls to generic-static dispatch.

*Source:* `src/compiler/sema_expr.cpp#L13250-L13263`, `src/compiler/sema_expr.cpp#L13268-L13269`

### `expr.static-call.unsafe-requires-unsafe-context` — Calling an unsafe static method requires an unsafe context

A call to an unsafe static method outside an unsafe context is an error.

*Source:* `src/compiler/sema_expr.cpp#L13532-L13533`, `src/compiler/sema_expr.cpp#L13409-L13410`

## Invocation

### `expr.invoke.arity-and-arg-types` — Closure/fn-ptr call arity and argument typing

A closure or fn-ptr call must supply exactly the parameter count; each argument is coerced to its parameter type and a non-error argument type incompatible with the parameter type is an error; variance is checked per argument.

*Source:* `src/compiler/sema_expr.cpp#L6221-L6243`

### `expr.invoke.callable-receiver` — Expression-as-callee (IIFE) must be callable

`(expr)(args)` invokes the receiver expression: a Closure-typed receiver lowers to a closure call, an fn-value-kind (fn-ptr) receiver to a fn-ptr call, and a TypeVar receiver bounded by an Fn/FnMut/FnOnce family bound synthesizes a closure type from that bound for arity/arg checks (without retyping the receiver). A receiver of any other type is a non-callable error.

*Source:* `src/compiler/sema_expr.cpp#L6187-L6294`

### `expr.invoke.expression-callee` — Expression-as-callee invocation

`(expr)(args)` invokes the value produced by `expr` as a callee, routed through closure-call or fn-ptr-call.

*Source:* `tools/peg_gen/grammars/logos.peg#L298`

## Unknown callee

### `intrinsic.unknown-callee.passthrough` — Unrecognized callee is not a type intrinsic

A callee not matching any recognized type-intrinsic name yields no lowering here (the dispatcher returns nothing), leaving the call to ordinary resolution.

*Source:* `src/compiler/sema_expr.cpp#L5828`

## Constructors

### `expr.ctor.prelude-option-result-shorthand` — Bare Some/Ok/Err prelude variant constructor

If no function named `Some`/`Ok`/`Err` resolves, a bare call `Some(x)`/`Ok(x)`/`Err(x)` constructs the corresponding `Option`/`Result` variant, provided that enum (with that variant) is in scope; a user-defined function of the same name shadows this (function lookup runs first).

*Source:* `src/compiler/sema_expr.cpp#L5921-L5942`

### `expr.ctor.variant-alias-shorthand` — Bare enum-variant constructor via use-alias

A `use Enum.{V, …};` import registers variant aliases; a bare call `V(payload)` whose name is an imported variant alias constructs that enum's variant `V` (typed via enum-literal lowering with payload typing), when no function of that name resolved.

*Divergence:* Logos `use Type.{V}` variant-import surface (pkg `.` / item `::` path model)

*Source:* `src/compiler/sema_expr.cpp#L5943-L5953`

## Array-fill expressions

### `expr.arr-fill.repeat-literal` — Array fill literal repeats the element to length N

`[v; N]` produces an array literal of element type T (= type of v) with N copies; the element is re-lowered for each slot. N must be a positive integer; the element IntLit is left unresolved so struct-literal type inference can widen it.

*Source:* `src/compiler/sema_expr.cpp#L11461-L11529`, `src/compiler/sema_expr.cpp#L11517-L11528`

### `expr.arr-fill.size-metacall` — Array fill length via metacall splice

`[v; metacall { <expr> }]` evaluates the block's tail expression by compile-time evaluation (CTFE), and the integer result becomes the array length. The metacall block must contain an integer tail expression. This is Logos's replacement for Rust const-eval at the array-length position.

*Divergence:* Logos explicit-metacall model replaces Rust const-expression array lengths.

*Source:* `src/compiler/sema_expr.cpp#L11486-L11516`

### `expr.arr-fill.size-sizeof-pack` — Array fill length via sizeof...(P)

`[v; sizeof...(P)]` where P is an in-scope type parameter yields a single-element array literal whose length is symbolic (`__sizeof_pack:P`); monomorphization repeats the element to the variadic pack's expanded length. Any spread operator other than `sizeof` is rejected; an undefined P is an error.

*Divergence:* Logos variadic-pack feature.

*Source:* `src/compiler/sema_expr.cpp#L11468-L11485`

## Vec expressions

### `intrinsic.vec.builtin-macro` — vec! is a compiler builtin list-literal macro

`vec!(a, b, c)` / `vec![a, b, c]` constructs a `Vec` of its elements. A user-defined `vec` fn_macro/token_macro in scope overrides the builtin. With a known renderable element type E (from a `let v: Vec<E>` annotation), it lowers to a push-block `{ let mut __v: Vec<E> = vec_new::<E>(); __v.push(e0); …; __v }` (no Copy bound). Otherwise it lowers to `vec_from_arr([…])` (Copy-bound, inference-driven); `vec!()` empty lowers to `vec_new::<_>()`.

*Source:* `src/compiler/sema_expr.cpp#L18134-L18213`

## Slices

### `expr.slice.len-as-ptr-builtin` — Built-in slice/str length and pointer methods

On a slice receiver, `recv.len()` yields the element count as `i64`, and `recv.as_ptr()` yields the data pointer as `*const u8`. These are intrinsic (not user-resolved).

*Source:* `src/compiler/sema_expr.cpp#L6463-L6471`

## Tuples

### `expr.tuple.unit-and-element-typing` — Tuple literal: unit, expected-type widening, overflow upgrade

`()` is the unit value of type `()`. Each tuple element is widened to its expected element type from a tuple type hint; an int-literal element that overflows i32 is upgraded to i64; the tuple type is the tuple of element types.

*Source:* `src/compiler/sema_expr.cpp#L1585-L1633`

## Raw pointers

### `expr.raw-ptr.arith-unsafe` — Raw-pointer arithmetic methods require unsafe

On a raw-pointer (`Ptr`) receiver, the offset methods {add, sub, byte_add, byte_sub} and the distance methods {offset_from, byte_offset_from} are built-in, each require an `unsafe` context, take exactly one argument, and have result type: offset → the receiver pointer type (argument coerced to `i64`); distance → `i64` (argument must be a pointer).

*Source:* `src/compiler/sema_expr.cpp#L6615-L6660`

### `expr.raw-ptr.is-null-safe` — Raw-pointer is_null is safe; user impl wins

On a raw-pointer receiver, `p.is_null()` is safe (no dereference), takes zero arguments, and lowers to `(p as i64) == 0 : bool` — UNLESS the pointee type declares an inherent `is_null` method, in which case that user-defined method is dispatched instead.

*Divergence:* Logos lets an inherent fn on the pointee shadow the built-in pointer is_null.

*Source:* `src/compiler/sema_expr.cpp#L6661-L6692`

## DST from raw parts

### `intrinsic.dst-from-raw-parts.unsafe` — dst_from_raw_parts requires unsafe and a custom-DST struct

`dst_from_raw_parts::<S>(ptr, len)` (and `_mut`) requires unsafe context, exactly one type argument S that is a (Zoned)Struct whose last field resolves to `[T]` or `dyn Trait` (directly is_dst or via type-parameter substitution), and exactly two value arguments.

*Divergence:* Logos addition (custom-DST construction intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4740-L4802`

### `intrinsic.dst-from-raw-parts.value` — dst_from_raw_parts builds a fat DstRef

`dst_from_raw_parts::<S>(ptr, len)` produces a `DstRef` to S ({data, len} fat-pair, same ABI as a slice); the `_mut` callee sets the DstRef mut flag. The length argument is widened to i64. The DstRef carries S's type-args for later tail-element field access.

*Source:* `src/compiler/sema_expr.cpp#L4803-L4812`

## Slice from raw parts

### `intrinsic.slice-from-raw.ptr-len` — slice_from_raw builds a slice fat pointer

`slice_from_raw::<T>(ptr: *const T, len: i64) -> &[T]` requires exactly one type argument and exactly two value arguments; it materialises a slice fat-pointer of element type T (uniform fat-pointer layout shared with str_from_raw). Wrong type-arg count or value-arg count is a compile error.

*Divergence:* Logos addition (unsafe raw-parts constructor).

*Source:* `src/compiler/sema_expr.cpp#L5032-L5057`

## dyn from parts

### `intrinsic.dyn-from-parts.fat-trait-ptr` — dyn_from_parts builds a trait object from raw halves

`dyn_from_parts::<Trait>(data: *mut u8, vtable: *const u8) -> *mut dyn Trait` forms a fat {data, vtable} trait-object pointer. Exactly one trait type argument (its own type-args, if any, are carried so the produced object matches a parameterized `dyn Trait<...>` annotation, skipping lifetime/auto-trait bound sub-nodes) and exactly two value arguments are required. Trait must be a known, object-safe trait. The result is the bare canonical TraitObject (matching `*mut dyn`/`&dyn`), not a thin pointer.

*Divergence:* Logos addition.

*Related:* `intrinsic.vtable-of.static-vtable-addr`

*Source:* `src/compiler/sema_expr.cpp#L5314-L5391`

## Zone mutable references

### `intrinsic.zone-mut-ref.unsafe` — zone_mut_ref signature and unsafe requirement

`zone_mut_ref::<T>(ptr, zone)` requires unsafe context, exactly one type argument T, and exactly two value arguments.

*Divergence:* Logos addition (zoned-reference construction intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4820-L4843`

### `intrinsic.zone-mut-ref.value` — zone_mut_ref builds a fat &mut T carrying the zone

`zone_mut_ref::<T>(ptr, zone)` produces a fat `&mut T` whose data slot = ptr and whose metadata slot = zone pointer cast to i64.

*Source:* `src/compiler/sema_expr.cpp#L4844-L4847`

## Zone expressions

### `intrinsic.zone.zone-of` — zone_of recovers the Writ zone pointer of a fat &mut T

`zone_of(r: &mut T) -> *mut u8` takes exactly 1 argument and yields the metadata half of the fat reference reinterpreted as a `*mut u8` (dual of zone_mut_ref). Wrong arity is an error.

*Divergence:* Logos addition: Writ/zone memory model intrinsic.

*Source:* `src/compiler/sema_expr.cpp#L3129-L3137`

## Block expressions

### `expr.block.as-value` — Block / control constructs as expressions

`{ … }` blocks, `unsafe { … }`, `loop { … }`, `if … {} else {}`, and `match … {}` are all primary expressions producing a value (block/loop yield their tail/break value).

*Source:* `tools/peg_gen/grammars/logos.peg#L2711-L2715`

### `expr.block.empty-is-void` — Empty block has type ()

A block expression `{}` with no statements evaluates to the unit/void type `()`.

*Source:* `src/compiler/sema_expr.cpp#L13653-L13656`

### `expr.block.tail-divergent-call-never` — Block with diverging tail call types as !

If a block's final tail expression is a call to a `-> !` (diverging) callee, the block types as the never type `!`; the diverging call is still emitted and the block contributes no concrete value type to its context.

*Related:* `expr.if.never-branch-skipped`

*Source:* `src/compiler/sema_expr.cpp#L13692-L13696`

### `expr.block.tail-expr-value` — Block value is its trailing tail expression

The type and value of a block `{ s1; ...; e }` are those of its final element when that element is a tail/expression statement (or a non-statement expression form); a block whose final element is a `let`, destructuring-let, `return`, or `;`-terminated expr-stmt produces no tail value and types as `()`.

*Source:* `src/compiler/sema_expr.cpp#L13676-L13724`

### `expr.block.tail-return-adopts-value-type` — Block ending in `return e` adopts e's type

A block whose final statement is `return e` is non-diverging in the value system: the block's result type is taken as `typeof(e)` even though no value is produced, so the divergent block is usable at a non-void expected type (e.g. inside a tuple/struct literal). The `return` is still lowered and executed.

*Divergence:* No real `!`/never subtyping for tail-return; the return-value's type is adopted as a block-type proxy instead of `!`.

*Uncertainty:* Behavior is a stated workaround pending full never-type support.

*Source:* `src/compiler/sema_expr.cpp#L13664-L13672`, `src/compiler/sema_expr.cpp#L13706-L13720`

## If expressions

### `expr.if.branch-type-compatible` — if-expr branches must have compatible types

In an `if` expression, the THEN and ELSE branch types must be mutually compatible (one assignable to the other); incompatible non-error, non-never branch types are an error. The result type is the unification (LUB) of the two branch types.

*Source:* `src/compiler/sema_expr.cpp#L14000-L14037`

### `expr.if.cond-must-be-bool` — if condition must be bool

The condition of a non-`let` `if` must have type `bool`; the error/never types are also accepted (error recovery and diverging conditions).

*Source:* `src/compiler/sema_expr.cpp#L13901-L13906`

### `expr.if.let-chain` — if let-chain

An `if` may chain conditions with `&&` where the first segment is a `let` binding: `if let P = e && seg (&& seg)* { THEN } [else …]`. Each subsequent seg is either `let P = e` or a bare condition (level `cmp_expr_ns`). The chain requires the first segment to be a `let` and at least two `&&`-joined segments. Desugars to nested matching: all let-patterns must match and all conditions hold for THEN.

*Source:* `tools/peg_gen/grammars/logos.peg#L2342-L2381`

### `expr.if.let-condition` — if and if-let

`if cond { ... }` takes a boolean condition; `if let PAT = expr { ... }` matches a pattern. An `else` branch is either a block or a chained `else if`.

*Source:* `src/compiler/sema_render.cpp#L395-L420`

### `expr.if.let-desugars-to-match` — if-let expression lowers to a two-arm match

`if let P = e { THEN } else { ELSE }` in expression position is equivalent to `match e { P => THEN, _ => ELSE }`; the pattern's bindings are in scope only within THEN, and the result type is that of the THEN branch.

*Source:* `src/compiler/sema_expr.cpp#L13815-L13897`

### `expr.if.never-branch-skipped` — Never/error branch yields the other branch's type

A branch typed `!` (never) or error contributes no type to an `if` expression: the expression's type is the other branch's type. `!` behaves as a subtype of every type at the join. A branch whose final statement is `return`/`break`/`continue` (or a diverging tail call) is typed `!`.

*Related:* `expr.block.tail-divergent-call-never`

*Source:* `src/compiler/sema_expr.cpp#L13959-L13970`, `src/compiler/sema_expr.cpp#L13998-L14005`

### `expr.if.no-struct-lit-cond` — if/while/for condition restricts struct literals

In `if`/`while`/`for` condition position the scrutinee uses the no-struct-lit expression grammar (`expr_ns`): a top-level `IDENT { … }` is NOT parsed as a struct literal, so the brace opens the control-flow block. A struct literal in condition position must be parenthesized. Restriction applies only to the top-level primary; inside parens/brackets/calls full `expr` resumes.

*Source:* `tools/peg_gen/grammars/logos.peg#L2411-L2417`, `tools/peg_gen/grammars/logos.peg#L2512-L2516`

### `expr.if.requires-else-in-expr-position` — if/if-let in expression position requires else

An `if` or `if let` used as an expression (yielding a value) must have an `else` branch; an `if` without `else` is only valid in statement position.

*Source:* `src/compiler/sema_expr.cpp#L13820-L13823`, `src/compiler/sema_expr.cpp#L13913-L13916`

### `expr.if.single-let-guard` — if-let with single guard condition

`if let P = e && cond { THEN } [else ELSE]` (single let plus trailing condition) desugars to `match e { P if cond => THEN, _ => ELSE }`; the let scrutinee is parsed at `cmp_expr_ns` so the `&&` belongs to the guard.

*Source:* `tools/peg_gen/grammars/logos.peg#L2357-L2364`

## if-let chains

### `expr.if-let-chain.fall-to-else-on-failure` — if-let chain falls to else on any segment failure

`if let P1 = e1 && let P2 = e2 && cond { THEN } else { ELSE }` evaluates a flat sequence of refutable binds and boolean conditions left-to-right; any failed bind or false condition takes the ELSE branch.

*Source:* `tools/peg_gen/grammars/logos.peg#L318-L320`

### `expr.if-let-chain.min-two-segments` — if-let chain requires at least two segments

An `if let ... && ...` chain must contain at least two segments (let-bindings and/or conditions); fewer is an error. The chain desugars inside-out into nested `if let`/`if` with the `else` branch duplicated at each fall-through.

*Uncertainty:* ELSE duplication at each fall-through is documented as an accepted limitation, not a fundamental rule.

*Source:* `src/compiler/sema_expr.cpp#L13745-L13797`

## Match expressions

### `expr.match.arm-after-catchall-unreachable` — arm after a catch-all `_` arm is unreachable

A match arm that follows an unguarded catch-all (`_`) arm is unreachable and is diagnosed (closes B-pt-07 expr position).

*Source:* `src/compiler/sema_stmt.cpp#L8946-L8959`

### `expr.match.arm-block-tail-is-value` — block arm yields its tail expression, not an implicit return

A block-form arm (`pat => { stmts }`) yields its trailing expression as the arm value (tail-as-return disabled inside match arms). A non-diverging block arm whose last statement is not an expression is a diagnostic ('block arm must end with an expression or always return'). A block arm all of whose paths diverge contributes Error and is skipped in unification.

*Source:* `src/compiler/sema_stmt.cpp#L9414-L9467`

### `expr.match.arm-forms` — match arm syntax

`match scrutinee { PAT [if GUARD] => RHS, ... }`; each arm has an optional `if`-guard and an arm body that is either a block or an expression followed by a comma.

*Source:* `src/compiler/sema_render.cpp#L422-L447`

### `expr.match.arm-requires-body` — every arm must have an expr or block body

A match arm must have either an expression body (`=> expr`) or a block body (`=> { ... }`); an arm with neither is a diagnostic.

*Source:* `src/compiler/sema_stmt.cpp#L9412-L9471`

### `expr.match.exhaustive-bool` — match on bool must cover true and false

A `match` on a `bool` scrutinee without a wildcard arm must have both a `true` and a `false` unguarded literal arm; a missing case is diagnosed.

*Source:* `src/compiler/sema_stmt.cpp#L9681-L9694`

### `expr.match.exhaustive-enum` — match on enum must be exhaustive

A `match` on an enum scrutinee without a wildcard/catch-all arm (and without AST-level proof of exhaustiveness for nested patterns) must cover every constructible variant; uncovered variants are reported as 'missing variant(s)'. A variant all of whose (substituted) payload types are uninhabited is unconstructable and need not be covered.

*Related:* `expr.match.exhaustive-enum-uninhabited`

*Source:* `src/compiler/sema_stmt.cpp#L9603-L9680`

### `expr.match.exhaustive-enum-uninhabited` — uninhabited-payload variants are exempt from exhaustiveness

Exhaustiveness substitutes the scrutinee's type-arguments into each variant's (generic) payload types before the uninhabited check; a variant with any uninhabited payload (e.g. `Result<T, Void>`'s Err) is unconstructable and omitting its arm remains exhaustive (T2-29).

*Related:* `expr.match.exhaustive-enum`

*Source:* `src/compiler/sema_stmt.cpp#L9650-L9675`

### `expr.match.fnitem-arms-lub-fnptr` — distinct fn-item arms LUB to the common fn-pointer type

When two arms produce distinct FnItem values with the same signature (e.g. `=> a_f` and `=> b_f`), the match result type is the corresponding `fn(...)->R` pointer type, since FnItem→FnItem coercion is rejected; both arms coerce to that FnPtr.

*Divergence:* Rust-conformant: matches Rust LUB for fn-item match arms.

*Related:* `expr.match.result-type-lub`

*Source:* `src/compiler/sema_stmt.cpp#L9502-L9523`

### `expr.match.guard-bool` — match guard must be bool

An arm guard expression (`pat if <guard> =>`) must have type `bool` (or Error); any other type is a diagnostic.

*Source:* `src/compiler/sema_stmt.cpp#L9343-L9348`

### `expr.match.guarded-arm-not-exhaustive` — guarded arms do not count toward exhaustiveness

An arm with a guard (`if`) does not contribute to exhaustiveness coverage; only unguarded patterns are counted as covering variants/wildcards.

*Source:* `src/compiler/sema_stmt.cpp#L9612`, `src/compiler/sema_stmt.cpp#L9618-L9623`, `src/compiler/sema_stmt.cpp#L9639-L9640`

### `expr.match.intlit-result-widen` — integer-literal match result widens to i64 on i32 overflow

If the inferred match result type is the unconstrained integer-literal type, and any arm's literal value exceeds the i32 range (> INT32_MAX or < INT32_MIN), the result type is fixed to i64.

*Related:* `expr.match.result-type-lub`

*Source:* `src/compiler/sema_stmt.cpp#L9535-L9550`

### `expr.match.never-arm-ignored` — Never-typed (diverging) arms do not constrain the result type

An arm whose value type is `!` (Never) contributes no type to the match result; Never is a subtype of every type. If the accumulated result type is still `!` or Error, the next arm's type replaces it.

*Related:* `expr.match.result-type-lub`

*Source:* `src/compiler/sema_stmt.cpp#L9494-L9501`

### `expr.match.result-type-lub` — match-expression result type is the LUB of its arms

The type of a `match` expression is the least-upper-bound of its arms' value types. Arms are unified left-to-right: error-typed and Never-typed arms contribute no type; numeric arms unify via numeric-LUB. If two arms have types that are mutually incompatible (neither `types_compatible` direction holds) the match is a type error.

*Related:* `expr.match.never-arm-ignored`, `expr.match.fnitem-arms-lub-fnptr`, `expr.match.intlit-result-widen`

*Source:* `src/compiler/sema_stmt.cpp#L9497-L9534`

### `expr.match.str-literal-arm-guard` — string-literal arms lower to wildcard + str-eq guard

A top-level string-literal arm (`match s { "foo" => ... }`) matches via a wildcard pattern plus a synthesized `str_eq(scrutinee, "foo")` guard, AND-ed ahead of any user guard; the scrutinee is hoisted into a synthetic local first (G172-1).

*Source:* `src/compiler/sema_stmt.cpp#L9034-L9067`, `src/compiler/sema_stmt.cpp#L9193-L9211`, `src/compiler/sema_stmt.cpp#L9350-L9359`

### `expr.match.temp-scrutinee-dropped` — a droppable rvalue scrutinee is dropped after the match value

When the scrutinee of a match-expression is a droppable move-type rvalue (not a place: not a var/field/tuple-index/deref/index read), it is bound to a synthetic local and dropped on every exit path. On fall-through the temporary is dropped after the match result is bound (unless an arm moved its payload); an arm that returns drops it via its own drop set.

*Related:* `borrow.match.scrutinee-moved-by-binding`

*Source:* `src/compiler/sema_stmt.cpp#L8875-L8937`, `src/compiler/sema_stmt.cpp#L8884-L8903`

### `expr.match.writ-pattern-needs-view` — Writ patterns require a view scrutinee

A match arm containing a Writ scalar pattern (PAT_WRIT_NULL/BOOL/INT/STR/MAP/ARR/TYPED_ARR/TYPED_MAP, including inside an or-pattern) requires the scrutinee to be a Writ view (Writ, WritView, or WritStatic; use `&` to borrow); otherwise a diagnostic is emitted.

*Divergence:* Logos extension: Writ structured-data pattern matching (not in Rust).

*Source:* `src/compiler/sema_stmt.cpp#L8961-L9003`

## matches! macro

### `intrinsic.matches.macro` — matches! tests a pattern

`matches!(expr, pattern [if guard])` evaluates to `true` iff `expr` matches the pattern (with optional guard), else `false`; lowered to `match (expr) { pattern => true, _ => false }`. The first top-level comma splits expr from the pattern.

*Source:* `src/compiler/sema_expr.cpp#L18411-L18434`

## Loop expressions

### `expr.loop.as-expr-type` — loop expression type: ! if no break-value, () if value-less break

A `loop {...}` used as an expression has type `!` (never) when no `break v` is reachable and the loop diverges, type `()` when a value-less `break` is reached, and the common break-value type when `break v` is reached.

*Source:* `src/compiler/sema_expr.cpp#L1504-L1547`

## Break

### `expr.break.value-loop-typing` — break value selects the loop's value type

A `break value` (optionally labeled) attributes its value type to the target loop frame; the frame's value type is the unification (numeric) of all break values, making the loop a value-yielding expression.

*Source:* `src/compiler/sema_expr.cpp#L1438-L1458`

## Return

### `expr.return.implicit-tail` — Tail expression is implicit return

A trailing expression with no terminating `;` at statement position synthesizes an implicit `return expr` for a non-void function.

*Divergence:* B-fn-06

*Source:* `tools/peg_gen/grammars/logos.peg#L291`

## Control flow

### `expr.control.break-continue-return-in-value-position` — break/continue/return usable in expression position

`break`, `continue`, and `return` may appear in expression position (Never-typed); the bare `return` form carries no value. They type-check as `!`/Never so surrounding expressions accept them.

*Source:* `tools/peg_gen/grammars/logos.peg#L299-L301`

### `expr.control.never-position` — Diverging control-flow as expression

`return [e]`, `break [label] [e]`, and `continue [label]` may appear in expression position with type `!` (never), permitting forms like `let x = if c { v } else { return e };` and `_ => break`.

*Source:* `tools/peg_gen/grammars/logos.peg#L2716-L2728`

## Control flow

### `expr.control-flow.diverging-is-never` — break/continue/return in expression position have type !

`break`, `continue`, and `return` used in expression position have type `!` (never), which coerces to/unifies with any surrounding expected type. `continue`/`break` outside any loop are errors. `return e` in expression position checks e against the function's return type.

*Source:* `src/compiler/sema_expr.cpp#L1393-L1462`

## Unsafe

### `expr.unsafe.block` — Unsafe block

`unsafe { ... }` is an unsafe block whose body is an ordinary block.

*Source:* `tools/peg_gen/grammars/logos.peg#L1825-L1827`

### `expr.unsafe.block-in-expr-position` — unsafe block as expression

An `unsafe { ... }` block may appear in expression position (e.g. as a let initializer).

*Source:* `src/compiler/sema_render.cpp#L538-L542`

## Unsafe blocks

### `expr.unsafe-block.tail-value` — unsafe block in expression position yields its tail value

An `unsafe { ... }` in expression position evaluates its statements with unsafe permitted and yields the trailing expression's value (not a return); with no trailing expression it has type `()`.

*Source:* `src/compiler/sema_expr.cpp#L1549-L1582`

## Comprehensions

### `expr.comprehension.list-and-map` — List and map comprehensions

List comprehension `[expr for x in iter (if pred)?]` and map comprehension `{kexpr: vexpr for x in iter (if pred)?}` produce a collection by iterating `iter`, binding `x`, optionally filtering by `pred`.

*Divergence:* Logos addition: Python-style comprehensions; not present in Rust.

*Source:* `tools/peg_gen/grammars/logos.peg#L2875-L2885`

## List comprehensions

### `expr.list-comp.bind-scope` — Comprehension binds the loop variable in value/guard scope

The loop variable `x` is bound (immutable, element type) in a new scope covering the value/key expressions and the guard; it is not visible outside the comprehension.

*Source:* `src/compiler/sema_expr.cpp#L10939-L10946`, `src/compiler/sema_expr.cpp#L11030-L11037`, `src/compiler/sema_expr.cpp#L11142-L11149`, `src/compiler/sema_expr.cpp#L11275-L11283`

### `expr.list-comp.desugar-vec` — List comprehension desugars to Vec build loop

A list comprehension `[value for x in iter (if guard)?]` desugars to a block that binds `let mut v: Vec<T> = vec_new::<T>()`, iterates `x` over `iter`, (optionally gated by `guard`) calls `Vec::push(&mut v, value)`, and evaluates to `v`. T is the iterator element type; the block's type is `Vec<T>`.

*Divergence:* Logos-specific surface syntax (Python-style comprehension); not present in Rust.

*Source:* `src/compiler/sema_expr.cpp#L10885-L10986`

### `expr.list-comp.iter-array-or-slice-only` — Comprehension iterables restricted to array/slice

The iterable of any comprehension form must have type `[T; N]` (array) or `[T]` (slice); any other iterator type is rejected. Element type defaults to i32 when the array/slice element type is absent.

*Divergence:* Narrower than Rust: only concrete array/slice, no IntoIterator/Iterator protocol.

*Uncertainty:* i32 default for missing elem type is a fallback; normally elem type is always present.

*Source:* `src/compiler/sema_expr.cpp#L10896-L10907`, `src/compiler/sema_expr.cpp#L11002-L11013`, `src/compiler/sema_expr.cpp#L11112-L11123`, `src/compiler/sema_expr.cpp#L11245-L11256`

### `expr.list-comp.requires-vec-import` — List comprehension requires Vec in scope

A list comprehension is ill-formed unless the `Vec` struct and the generic `vec_new` function are visible (via `use logos.mem.collections.vec;`).

*Divergence:* Logos-specific: surface sugar depends on a stdlib import being present.

*Source:* `src/compiler/sema_expr.cpp#L10909-L10921`

## Map comprehensions

### `expr.map-comp.desugar-hashmap` — Map comprehension desugars to HashMap build loop

A map comprehension `{key: value for x in iter (if guard)?}` desugars to a block that binds `let mut m: HashMap<K,V> = hashmap_new::<K,V>()`, iterates `x` over `iter`, (optionally gated by `guard`) calls `HashMap::insert(&mut m, key, value)`, and evaluates to `m`. K = type of `key`, V = type of `value`; block type is `HashMap<K,V>`.

*Divergence:* Logos-specific surface syntax; not present in Rust.

*Source:* `src/compiler/sema_expr.cpp#L10992-L11090`

### `expr.map-comp.requires-hashmap-import` — Map comprehension requires HashMap in scope

A map comprehension is ill-formed unless the `HashMap` struct and the generic `hashmap_new` function are visible (via `use logos.mem.collections.hashmap;`).

*Divergence:* Logos-specific.

*Source:* `src/compiler/sema_expr.cpp#L11015-L11026`

## Closures

### `expr.closure.body-is-drop-boundary` — Closure body scope is a drop boundary

A closure body is lowered in its own scope that is a drop boundary: a `return` inside the body drops only the closure's own frames, not the enclosing function's locals captured by the closure (those are owned by their original bindings or borrowed by the env).

*Source:* `src/compiler/sema_expr.cpp#L14243-L14247`, `src/compiler/sema_expr.cpp#L14334-L14338`

### `expr.closure.body-own-unsafe-scope` — Closure body does not inherit enclosing unsafe context

A closure body is lowered as its own scope and does not inherit the enclosing `unsafe` context; the inside-unsafe state is reset to false for the body and restored afterward.

*Source:* `src/compiler/sema_expr.cpp#L14274-L14278`, `src/compiler/sema_expr.cpp#L14332-L14333`

### `expr.closure.boxing-escapes` — A closure assigned to a Box<...Fn...> escapes

A closure lowered against an expected type that peels (through a Box / struct wrapper) to a callable Fn type is treated as escaping: its captured environment lives on the heap. A bare or reference-wrapped Fn expectation (e.g. an iterator-adapter argument) does not escape and keeps a stack environment.

*Source:* `src/compiler/sema_expr.cpp#L14787-L14793`

### `expr.closure.capture-borrow-of-var` — Taking the address of a variable in a closure body captures it

`&x` or `&mut x` appearing in a closure body captures the whole root variable `x` from the enclosing scope, just as a plain read would.

*Source:* `src/compiler/sema_expr.cpp#L14584-L14587`

### `expr.closure.capture-by-free-variable` — Closures capture free variables resolving in an enclosing scope

A closure captures exactly those names used in its body that are not its own parameters and that resolve to a binding in an enclosing scope; each captured name's type is the enclosing binding's type.

*Source:* `src/compiler/sema_expr.cpp#L14388-L14432`, `src/compiler/sema_expr.cpp#L14421-L14432`

### `expr.closure.capture-by-ref-on-mutation` — Mutating a captured variable forces by-reference capture

A captured variable that the closure body mutates is captured by reference. Mutation includes: assignment to the variable, field writes / multi-level (chained) field writes through it, indexed writes into it, and an auto `&mut` of the variable produced as a method receiver. A by-value capture of a mutated variable would lose the write.

*Source:* `src/compiler/sema_expr.cpp#L14594-L14602`, `src/compiler/sema_expr.cpp#L14699-L14704`, `src/compiler/sema_expr.cpp#L14724-L14746`

### `expr.closure.capture-disjoint-fields` — Disjoint closure capture by precise field path (RFC 2229)

When a closure body accesses fields of a variable through a pure `root.field*` dotted chain, the capture is the precise path rather than the whole root; multiple paths off the same root are widened to their lowest-common-ancestor path. If the access head is not a pure VarRef/FieldRead chain (e.g. `(*box).x`), the whole root is captured instead.

*Related:* `expr.closure.capture-free-vars`

*Source:* `src/compiler/sema_expr.cpp#L14563-L14569`, `src/compiler/sema_expr.cpp#L14805`

### `expr.closure.capture-drop-order` — Source-scope-dropped captures drop with the closure in capture order

Captures whose destructor the source scope still runs are dropped at the closure binding's slot in capture order, not at their own variable-order slots, matching Rust's closure capture drop order.

*Source:* `src/compiler/sema_expr.cpp#L14892-L14897`

### `expr.closure.capture-free-vars` — Closure captures the free variables referenced in its body

A closure literal captures exactly the set of variables from the enclosing scope that its body references (transitively through every expression and statement form), excluding the closure's own parameters and variables bound locally inside the body. A bare variable reference `x` captures the whole root `x`.

*Source:* `src/compiler/sema_expr.cpp#L14539-L14546`, `src/compiler/sema_expr.cpp#L14691-L14773`, `src/compiler/sema_expr.cpp#L14801`

### `expr.closure.disjoint-field-capture` — Closures capture disjoint fields (RFC-2229)

When a closure body reads a precise dotted field path `root.x.y` rooted at a captured variable, the capture is recorded at that path; multiple paths off the same root are widened to their lowest common ancestor segment (`lca("p.x","p.y")="p"`, widening to a larger/less precise borrow which is sound). The capture's slot is sized at the leaf field type when the path walks entirely through plain `Struct` fields; otherwise the whole root is captured. Paths are extracted only when the head is a plain variable reference followed by field reads (indexing or deref-through-box falls back to whole-variable capture).

```logos
let g = |p: &Pt| { use(p.x); use(p.y); };
```

*Source:* `src/compiler/sema_expr.cpp#L14433-L14528`, `src/compiler/sema_expr.cpp#L14455-L14482`, `src/compiler/sema_expr.cpp#L14486-L14506`

### `expr.closure.escaping-env-owns-captures` — Escaping move closure owns droppable captures in its environment

An escaping (heap-environment / boxed) `move` closure that captures a droppable struct/array/tuple/enum moves it into the closure environment by value; the environment's drop glue drops it, so the originating scope does not. A non-escaping (stack-environment) `move` closure borrows the source storage, so the source scope still drops the value unless the body itself already moved the capture onward.

*Related:* `expr.closure.boxing-escapes`

*Source:* `src/compiler/sema_expr.cpp#L14855-L14888`

### `expr.closure.expr-body-yields-value` — Expression-body closure yields its expression

A closure with an expression body `|y| expr` (no braces) is lowered as if its body were `return expr;`; the closure result is the value of `expr`.

```logos
let f = |y| y * 2;
```

*Source:* `src/compiler/sema_expr.cpp#L14284-L14290`

### `expr.closure.hint-peels-callable-wrappers` — Closure-formal hint peels through refs/pointers and single-arg wrappers to a callable

When inferring closure param types from an expected type, the expected type is peeled (up to 8 levels) through `&T`/`&mut T`/`*T` (to pointee) and through a Struct/ZonedStruct with exactly one type argument (to that argument) until a Closure or FnPtr type is reached; the resulting callable's parameter list supplies the param-type hints. This lets `Box<dyn Fn(..)>`/`&dyn Fn(..)`-typed contexts still drive inference.

```logos
let b: Box<dyn Fn(i32) -> i32> = box_new(|x| x + 1);
```

*Source:* `src/compiler/sema_expr.cpp#L14082-L14099`, `src/compiler/sema_expr.cpp#L14138-L14148`

### `expr.closure.move-marks-moved` — move closure consumes its move-type captures at the capture site

In a `move` closure, each captured variable (or, for an escaping narrow capture, the captured field path) whose type is a move type is marked moved at the closure site, making subsequent use of that variable/path a use-after-move error. Copy-type captures are not consumed.

*Source:* `src/compiler/sema_expr.cpp#L14811-L14848`

### `expr.closure.mut-bind-param` — `|mut x|` binds a mutable copy of the parameter

A closure parameter written `mut x` (IS_MUT, not a ref-bind) takes its argument under a synthetic name and binds the user-visible `x` as a mutable local initialized from the synthetic param (`let mut x = synth;`). The synthetic name is not entered into the sema scope, so move-typed params do not receive double drop glue.

```logos
let f = |mut x: i32| { x += 1; x };
```

*Source:* `src/compiler/sema_expr.cpp#L14199-L14212`, `src/compiler/sema_expr.cpp#L14248-L14256`, `src/compiler/sema_expr.cpp#L14296-L14303`

### `expr.closure.mutated-capture-by-reference` — Mutated captures are captured by reference

A captured variable that is the target of a mutation in the body (assignment / field write / index write / deref write) is captured by reference so the mutation propagates to the outer binding rather than to a local env copy. A write-only target (no prior read of its base) is still added to the capture set as a whole-variable capture.

*Divergence:* Capture mode is inferred per-variable from usage (read-only vs mutated), conceptually aligned with Rust closure capture-mode inference.

*Source:* `src/compiler/sema_expr.cpp#L14395-L14420`

### `expr.closure.narrow-move-requires-escape` — Narrow (field) move capture applies only to escaping closures; user Drop on root forces whole-var

RFC-2229 narrow move capture (moving only a field path, leaving sibling fields usable) applies only when the closure escapes; a non-escaping narrow capture moves nothing and the root keeps ownership. However, a `move` closure capturing a path whose root type has a user `impl Drop` captures the whole variable (so the value drops with the closure); mere drop glue from droppable fields keeps disjoint capture.

*Related:* `expr.closure.capture-disjoint-fields`, `expr.closure.escaping-env-owns-captures`

*Source:* `src/compiler/sema_expr.cpp#L14820-L14854`

### `expr.closure.nested-transitive-capture` — Outer closure transitively captures a nested closure's free vars

A closure literal nested in another closure's body causes the outer closure to capture the nested closure's free variables. If the nested closure captures a variable by reference (mutates it), the outer closure must also capture that variable by reference; otherwise the nested write would target the outer's by-value copy and be lost.

*Related:* `expr.closure.capture-by-ref-on-mutation`

*Source:* `src/compiler/sema_expr.cpp#L14640-L14656`

### `expr.closure.param-type-inference-from-hint` — Untyped closure params infer types from expected fn signature

For a closure literal `|x, y| …` whose parameters carry no type annotation, each untyped parameter's type is taken from the corresponding formal of the expected callable type at the call site (the closure-formal hint), by positional index. The hint is consulted only for params that lack both a TYPE and a NAMES (tuple-destructure) node.

```logos
let f: fn(i32) -> i32 = |x| x + 1;
```

*Source:* `src/compiler/sema_expr.cpp#L14137-L14158`

### `expr.closure.ref-bind-param` — `|ref x: T|` binds x as &T

A closure parameter written `ref x: T` (IS_REF with an explicit TYPE) takes its argument by value of type T under a synthetic name and binds the user-visible `x` to `&T` aliasing the synthetic param. IS_REF without a TYPE is the `&self`/`&mut self` shorthand, not a ref-bind.

```logos
let f = |ref x: i32| *x + 1;
```

*Divergence:* Logos closure ref-binding param syntax; no direct Rust equivalent.

*Source:* `src/compiler/sema_expr.cpp#L14191-L14206`, `src/compiler/sema_expr.cpp#L14257-L14259`, `src/compiler/sema_expr.cpp#L14304-L14311`

### `expr.closure.return-type-inference` — Closure return type inferred from first non-void return

A closure without an explicit `-> R` annotation infers its return type by scanning the lowered body (recursing into if/while/loop/block) for return statements and adopting the type of the first return value whose type is neither Void nor Error; if none is found the return type is `()` (void). During body lowering of an unannotated closure the expected return type is left unset so `return X;` is not strictly type-checked against it.

```logos
let f = |x: i32| { if x > 0 { return 1; } 2 };
```

*Source:* `src/compiler/sema_expr.cpp#L14229-L14231`, `src/compiler/sema_expr.cpp#L14275-L14277`, `src/compiler/sema_expr.cpp#L14340-L14386`

### `expr.closure.tuple-destructure-param` — `|(a, b): (T1, T2)|` destructures a tuple parameter

A closure parameter written `(a, b, …): (T1, T2, …)` takes a single synthetic tuple-typed parameter and binds each user name to the corresponding tuple element (`let a = synth.0; let b = synth.1; …`), with `_` sub-patterns skipped. Element bindings are only emitted when the param type is a Tuple type; bindings are positional up to the lesser of name-count and tuple arity.

```logos
let f = |(a, b): (i32, i32)| a + b;
```

*Source:* `src/compiler/sema_expr.cpp#L14159-L14188`, `src/compiler/sema_expr.cpp#L14260-L14268`, `src/compiler/sema_expr.cpp#L14312-L14326`

### `expr.closure.writ-capture-exprs` — Writ literal $-captures count as closure captures

Variables referenced via `$`-capture expressions inside a Writ literal in a closure body are captured by the enclosing closure.

*Related:* `expr.closure.capture-free-vars`

*Source:* `src/compiler/sema_expr.cpp#L14681-L14687`

## Formatting

### `expr.fmt.arg-id-kind` — Explicit-index vs named argument id

If the first arg_id char is a digit it is parsed as an explicit positional index; if it is an alphabetic char or `_` it is parsed as a named-argument identifier ([A-Za-z_][A-Za-z0-9_]*).

*Source:* `src/compiler/sema_fmt.cpp#L75-L89`, `src/compiler/sema_fmt.cpp#L157-L165`

### `expr.fmt.brace-escape` — Doubled braces escape a literal brace

In a format string, `{{` denotes a literal `{` and `}}` denotes a literal `}`; each doubled brace contributes exactly one brace to the literal output and is not treated as a placeholder delimiter.

*Source:* `src/compiler/sema_fmt.cpp#L121-L134`

### `expr.fmt.fill-align` — Fill+align detection

A fill character is recognized only when immediately followed by an alignment marker (`<`,`>`,`^`), forming a 2-char fill+align prefix; a bare alignment marker uses the default fill; `<`=Left, `>`=Right, `^`=Center.

*Source:* `src/compiler/sema_fmt.cpp#L176-L196`

### `expr.fmt.implicit-positional-counter` — Implicit positional argument assignment

Placeholders without an explicit arg_id are assigned consecutive positional indices starting at 0, incremented per implicit placeholder; explicit-index and named placeholders do not advance this counter.

*Source:* `src/compiler/sema_fmt.cpp#L166-L169`

### `expr.fmt.placeholder-syntax` — Placeholder grammar

A placeholder has form `{` arg_id? (`:` format_spec)? `}` where arg_id is either an unsigned integer (explicit positional index) or an identifier (named argument); absence of arg_id means the next implicit positional argument.

*Source:* `src/compiler/sema_fmt.cpp#L148-L170`

### `expr.fmt.precision-requires-number` — Precision dot requires a number

A `.` in the format spec must be followed by an unsigned-integer precision; a `.` not followed by a digit is a compile error.

*Divergence:* Rust additionally permits `.*` and `.N$` precision forms; Logos here requires a literal number after `.`.

*Source:* `src/compiler/sema_fmt.cpp#L224-L235`

### `expr.fmt.spec-field-order` — Format spec field ordering

After `:` the format spec fields appear in fixed order: (fill align)? sign? `#`? `0`? width? (`.` precision)? type? where align in {`<`,`>`,`^`}, sign in {`+`,`-`}, width and precision are unsigned integers, and type is a single char.

*Source:* `src/compiler/sema_fmt.cpp#L172-L256`

### `expr.fmt.type-char-set` — Format type chars select a formatting trait

The type char selects the formatting trait: `?`=Debug, `x`=LowerHex, `X`=UpperHex, `o`=Octal, `b`=Binary, `e`=LowerExp, `E`=UpperExp; absence means Display; any other char before `}` is a compile error (`unknown type char`).

*Source:* `src/compiler/sema_fmt.cpp#L237-L256`, `src/compiler/sema_fmt.cpp#L43-L55`

### `expr.fmt.unmatched-close-brace` — Unescaped `}` is an error

A `}` that is not part of a `}}` escape and does not close a placeholder is a compile error (`unmatched `}``); use `}}` to emit a literal `}`.

*Source:* `src/compiler/sema_fmt.cpp#L135-L142`

### `expr.fmt.unmatched-open-brace` — Unterminated placeholder is an error

A `{` opening a placeholder must be closed by a matching `}`; if the placeholder body ends without `}`, it is a compile error (`unmatched `{``).

*Source:* `src/compiler/sema_fmt.cpp#L259-L265`

### `intrinsic.fmt.tuple-debug-synth` — Debug formatting of tuples is synthesized element-wise

Debug formatting of a tuple T=(t0,..,t_{n-1}) emits an open delimiter, then for each element the element's Debug rendering separated by separators, then a close delimiter (a distinct close form for n==1). Each non-tuple element is formatted by a recursive `fmt` method-call dispatched on the element's Debug impl; a nested tuple element recurses through the same builder. A &[u8] slice element is formatted as `str`.

*Source:* `src/compiler/mono_clone.cpp#L2646-L2682`, `src/compiler/mono_clone.cpp#L2663-L2671`

## String expressions

### `expr.str.as-bytes-identity` — &str.as_bytes() is the identity

Because `&str` is represented as `Slice<u8>` (same fat-pointer ABI as `&[u8]`), `s.as_bytes()` on a `Slice<u8>` receiver returns the receiver verbatim with no conversion.

*Divergence:* Logos models &str as Slice<u8> (writ/string-repr); identity conversion.

*Source:* `src/compiler/sema_expr.cpp#L6472-L6481`

### `expr.str.method-forwarding` — &str methods forward to stdlib free functions

On a `Slice<u8>` (= `&str`) receiver, the methods {starts_with→str_starts_with, ends_with→str_ends_with, contains→str_contains, eq_str→str_eq, cmp→str_cmp, index_of→str_index_of, find→str_index_of, trim→str_trim, trim_start→str_trim_start, trim_end→str_trim_end, split→split} desugar to a call of the named stdlib free function with the receiver as the first argument, when that function exists.

*Source:* `src/compiler/sema_expr.cpp#L6482-L6517`

### `intrinsic.str.str-from-raw` — str_from_raw constructs a str fat pointer

`str_from_raw(ptr: *const u8, len: i64) -> str` is a compiler intrinsic taking exactly 2 arguments; it yields a value of type `&[u8]`/str fat-pointer. Wrong arity is an error.

*Divergence:* Logos addition: no Rust equivalent free function.

*Source:* `src/compiler/sema_expr.cpp#L3117-L3127`

## concat!

### `intrinsic.concat.macro` — concat! string-literal concatenation

`concat!(a, b, …)` concatenates string, integer (decimal, suffix-stripped), and bool (`true`/`false`) literals at compile time into a single `&str` (`Slice<u8>`) literal. Non-literal args are a compile error. String escapes \n \t \r \\ \" \0 are decoded.

*Divergence:* Floats and char literals are not supported (Rust supports them).

*Source:* `src/compiler/sema_expr.cpp#L18318-L18324`, `src/compiler/sema_expr.cpp#L17836-L17920`

## concat_bytes!

### `intrinsic.concat-bytes.macro` — concat_bytes! byte-array concatenation

`concat_bytes!(…)` concatenates byte-string literals (`b"…"`), byte-char literals (`b'X'`), and integer literals in range 0..=255 (decimal/0x/0o/0b, suffix-allowed) at compile time, yielding a `[u8; N]` array literal. Out-of-range integers, dangling/unknown escapes, and unsupported args are compile errors.

*Source:* `src/compiler/sema_expr.cpp#L18326-L18331`, `src/compiler/sema_expr.cpp#L17922-L18084`

## dbg! macro

### `intrinsic.dbg.macro` — dbg! prints and returns its argument

`dbg!(expr)` eprints `[file:line] expr = <Debug>` and evaluates to the value of `expr` (ownership passes through). `dbg!()` prints just `[file:line]` and yields `()`.

*Source:* `src/compiler/sema_expr.cpp#L18436-L18472`

## Writ expressions

### `expr.writ.array` — Writ untyped array literal

An untyped Writ array `@[...]` lowers each element as a recursive Writ value in order.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15131-L15143`

### `expr.writ.bool` — Writ bool literal

A Writ bool node yields a boolean Writ value; the value is true iff its byte payload is present and nonzero.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15021-L15025`

### `expr.writ.capturable-types` — Types capturable by $-capture into a Writ value

A captured Logos expression is admissible into a Writ @-literal iff its type is: a scalar integer (i8/i16/i32/i64/u8/u16/u32/u64) or bool (coerced to inline AnyVal); F32/F64/float-literal (zone-allocated F64); AnyVal or a string-view struct; a pointer to u8 (*const u8 / *mut u8, captured as C-string varchar); or a u8 slice (str/&[u8], captured as varchar with length). Other types are not capturable.

*Divergence:* Logos addition (Writ captures).

*Source:* `src/compiler/sema_expr.cpp#L15325-L15350`

### `expr.writ.capture-not-standalone` — $-capture is not a standalone expression

A `$`-capture node (WRIT_CAP_IDENT / WRIT_CAP_EXPR) is only valid inside a writ value literal; appearing as a standalone expression is an error.

*Source:* `src/compiler/sema_expr.cpp#L1489-L1494`

### `expr.writ.capture-outside-context` — $-capture only inside capturable @-literal

A $-capture ($ident or $expr) in a Writ value is a compile error unless it occurs inside a capturable @-literal context.

*Divergence:* Logos addition (Writ captures).

*Source:* `src/compiler/sema_expr.cpp#L15319-L15323`

### `expr.writ.cfg-slot-type` — WritStatic const-generic slot type

A slot of a WritStatic-typed const-generic is referenced as `<type:CFG.slot.path>` with dot-separated step names.

*Divergence:* Logos-specific const-generic/Writ syntax.

*Source:* `src/compiler/sema_render.cpp#L517-L531`

### `expr.writ.cfg-slot-type-literal` — <type:CFG.path> at writ-value position

`<type:CFG.path>` resolves the config path eagerly and must denote a concrete top-level alias; if it resolves to a const-generic config-slot parameter (kind CfgSlotType) it is rejected with a compile error (parametric Writ literals are not supported).

*Divergence:* Logos addition (Writ/CFG type literals).

*Uncertainty:* Restriction is stated as a current limitation in the source.

*Source:* `src/compiler/sema_expr.cpp#L14982-L15009`

### `expr.writ.embedded-type-lit` — Embedded type in Writ literal

A Logos type can be embedded inside a Writ literal as `<type:T>`.

*Divergence:* Logos-specific Writ syntax.

*Source:* `src/compiler/sema_render.cpp#L510-L516`

### `expr.writ.float-suffix` — Writ float literal: suffix stripping

A Writ float literal accepts an optional `f32` or `f64` suffix which is stripped before parsing the value as a double-precision float.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15052-L15060`

### `expr.writ.int-suffix-and-radix` — Writ integer literal: suffix stripping and radix

A Writ integer literal accepts an optional numeric-type suffix (i8/i16/i24/i32/i56/i64/i128, u8/u16/u24/u32/u56/u64/u128, usize, isize) which is stripped before parsing, an optional leading '-', and a radix prefix: `0x` = hexadecimal, `0b` = binary, otherwise decimal. The resulting magnitude is negated if the sign was present.

*Divergence:* Logos addition (Writ literals); note i24/i56/u24/u56 width suffixes.

*Source:* `src/compiler/sema_expr.cpp#L15027-L15050`

### `expr.writ.map-entry-colon` — Writ map entry syntax

A Writ map literal `@{ ... }` contains comma-separated entries `key: value`; nested scalar values omit the `@` prefix in inner position.

*Divergence:* Logos-specific Writ syntax.

*Source:* `src/compiler/sema_render.cpp#L479-L497`

### `expr.writ.map-keys` — Writ map literal keys (string or integer)

An untyped Writ map `@{...}` has entries whose key is either a quoted string (quote-stripped and escape-processed like a Writ string) or an integer; an integer key is negated when the entry carries the negative-key marker. Values are recursively lowered Writ values.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15088-L15129`

### `expr.writ.neg-int` — Writ negative integer literal

A Writ negative-integer node yields an integer Writ value equal to the negation of the parsed decimal magnitude.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15012-L15016`

### `expr.writ.null` — Writ null literal

A Writ null node yields the null Writ value.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15018-L15019`

### `expr.writ.outer-at-prefix` — Writ literal outer `@` prefix

Writ (data) literals in expression position are introduced with a leading `@`: `@null`, `@true`/`@false`, `@INT`, `@-INT`, `@FLOAT`, `@"str"`, `@{ ... }` (map), `@[ ... ]` (array).

*Divergence:* Logos-specific Writ data-literal syntax; no Rust equivalent.

*Source:* `src/compiler/sema_render.cpp#L463-L509`

### `expr.writ.sdn-literal` — Writ SDN literals

Writ structured-data literals use the `@` sigil: `@{k:v,…}` map, `@[v,…]` array, `@"s"` string, `@42`/`@-1` int, `@<float>` float, `@true`/`@false` bool, `@null`. Typed forms `@<Elem>[…]` (dense array) and `@<K,V>{…}` / `@<K>{…}` (typed map). Comprehension forms `@[expr for x in iter (if p)?]` and `@{k:v for …}`. Only the outermost literal needs the `@` sigil; inner values are plain.

*Divergence:* Logos addition: Writ self-describing data-notation literals.

*Source:* `tools/peg_gen/grammars/logos.peg#L2887-L2923`

### `expr.writ.string-escapes` — Writ string literal: quote stripping and escapes

A Writ string literal has surrounding double-quotes stripped and recognizes escape sequences \n, \t, \r, \\, \", \0; an unrecognized escape `\x` is kept literally as backslash followed by x.

*Divergence:* Logos addition (Writ literals); escape set is a fixed subset.

*Source:* `src/compiler/sema_expr.cpp#L15062-L15086`

### `expr.writ.type-literal` — Writ type-literal <type:T>

A Writ value `<type:T>` embeds a Logos type T as a first-class value. T is resolved as a type (primitives, structs, in-scope type-params, and generic instantiations like Vec<u8> all permitted). The value carries (kind, type-uid, canonical-name) where the name is the canonical printed form (e.g. "Vec<u8>") and serves as the value's identity label.

*Divergence:* Logos addition: Writ first-class type values have no Rust equivalent.

*Source:* `src/compiler/sema_expr.cpp#L14937-L14979`

### `expr.writ.type-literal-unknown-bare` — Bare type-name in <type:T> must be a known type or in-scope type-param

When `<type:T>` names a bare type identifier that is neither a resolvable known type nor an in-scope type-param, it is a compile error; the diagnostic directs the user to declare T as a type-param of the enclosing const (`pub const X<T>: WritStatic = ...`) or use a concrete type.

*Divergence:* Logos addition (Writ type literals).

*Source:* `src/compiler/sema_expr.cpp#L14954-L14966`

### `expr.writ.typed-array-elem-types` — Typed Writ array element types

A typed Writ array `@<E>[...]` requires E to be one of I8, U8, I16, U16, I32, U32, I64, U64, F32, F64; any other element type is a compile error.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15145-L15168`

### `expr.writ.typed-array-i32-bounds` — @<I32> array element range check

Each integer element of an `@<I32>[...]` typed array is bounds-checked at compile time to the i32 range [-2147483648, 2147483647]; out-of-range values are a compile error.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15190-L15203`

### `expr.writ.typed-array-no-captures` — Typed Writ arrays reject $-captures

Within a typed Writ array `@<E>[...]`, a $-capture element ($ident or $expr) is a compile error because typed arrays store raw element values rather than AnyVal; an untyped `@[...]` literal must be used instead.

*Divergence:* Logos addition (Writ literals/captures).

*Source:* `src/compiler/sema_expr.cpp#L15174-L15187`

### `expr.writ.typed-map-key-discipline` — Typed integer-map key discipline

In a typed integer-keyed Writ map, a string key is a compile error (integer maps require integer keys); integer keys are negated when marked negative, and are bounds/sign-checked per key type: I32 to [-2^31, 2^31-1], U32 to [0, 2^32-1], U64 to non-negative.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15255-L15311`

### `expr.writ.typed-map-types` — Typed Writ map key/value types

A typed Writ map `@<K>{...}` or `@<K,V>{...}` requires K ∈ {I32, U32, I64, U64, Varchar} and, if V is given, V == AnyVal; any other key or value type is a compile error. Varchar keys produce the same representation as the untyped object map.

*Divergence:* Logos addition (Writ literals).

*Source:* `src/compiler/sema_expr.cpp#L15209-L15252`

## Writ capture

### `expr.writ-capture.capturable-types` — Set of types capturable in an @-literal

A value may be captured into an @-literal iff its type is one of: integer scalars i8/i16/i32/i64/u8/u16/u32/u64, bool (→ inline AnyVal); f64/f32/FloatLit (→ zone-allocated F64, type_code 31); AnyVal (passthrough) or StringView (→ varchar) struct types; `*const u8`/`*mut u8` (→ C-string varchar); or `str`/`&[u8]` slice of u8 (→ length-bearing varchar). All other types are rejected.

*Divergence:* Logos addition: @-literal (Writ) capture has no Rust analogue.

*Source:* `src/compiler/sema_expr.cpp#L15325-L15350`, `src/compiler/sema_expr.cpp#L15360-L15367`, `src/compiler/sema_expr.cpp#L15387-L15394`

### `expr.writ-capture.context-required` — $-capture requires a capturable @-literal context

A `$ident` or `${expr}` capture node is only valid lexically inside a capturable @-literal (Writ) context; using one elsewhere is an error.

*Source:* `src/compiler/sema_expr.cpp#L15319-L15323`

### `expr.writ-capture.expr-no-dedup` — ${expr} captures are never deduplicated

A `${expr}` capture (WRIT_CAP_EXPR) lowers its inner expression and always allocates a fresh capture value index (no deduplication, since the expression may have side effects).

*Source:* `src/compiler/sema_expr.cpp#L15381-L15399`

### `expr.writ-capture.ident-dedup` — Identical $ident captures share one value slot

Two `$ident` captures of the same identifier name reuse the same capture value index (deduplicated), while each occurrence consumes a distinct parameter slot.

*Source:* `src/compiler/sema_expr.cpp#L15368-L15380`

### `expr.writ-capture.ident-lookup` — $ident capture resolves a variable by name

A `$ident` capture (WRIT_CAP_IDENT) resolves `ident` against the enclosing scope; an unknown variable is an error.

*Source:* `src/compiler/sema_expr.cpp#L15352-L15359`

## Writ comprehensions

### `expr.writ-comp.guard-must-be-bool` — Writ comprehension guard must be bool

In a writ list/map comprehension the `guard` expression must have type `bool`; any other type is rejected (errors on Error type are swallowed to avoid cascades).

*Source:* `src/compiler/sema_expr.cpp#L11158-L11172`, `src/compiler/sema_expr.cpp#L11305-L11318`

## Writ list comprehensions

### `expr.writ-list-comp.desugar` — Writ list comprehension desugars to a Writ array builder loop

A writ list comprehension `@[value for x in iter (if guard)?]` desugars to a block that binds `let mut c = writ_list_comp_new(cap_hint)` (yielding the builder's return type, e.g. Rc<Writ>), iterates `x` over `iter`, coerces `value` to AnyVal, (optionally gated by `guard`) calls `writ_list_comp_push(&c, value)`, and evaluates to `c`. cap_hint = arr_size*8+128 for arrays of known size, else 128.

*Divergence:* Logos-specific Writ data-substrate sugar; no Rust equivalent.

*Source:* `src/compiler/sema_expr.cpp#L11098-L11226`

### `expr.writ-list-comp.requires-builder-import` — Writ list comprehension requires comp_builder import

A writ list comprehension is ill-formed unless arity-1 `writ_list_comp_new` and arity-2 `writ_list_comp_push` are visible (via `use logos.lang.writ.comp_builder;`).

*Divergence:* Logos-specific.

*Source:* `src/compiler/sema_expr.cpp#L11125-L11135`

## Writ map comprehensions

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

## Reflection

### `intrinsic.reflect.apply-generic` — apply_generic(g: Type, args) instantiates a generic constructor

`apply_generic(g, args)` (callee __apply_generic__) instantiates the generic constructor described by Type value `g` (produced by generic_of) with `args`, routing through the same struct allocation as type_apply. The template name is recovered from g's `Type` struct-literal `name` field (a string literal); both operands are chased through VarRef let-bindings (max 8 hops).

*Divergence:* A6 (Logos-only type-level composition intrinsic)

*Uncertainty:* Slice ends mid-statement at L2119; only name recovery from g is visible in-unit, the remaining instantiation logic continues past the unit boundary.

*Related:* `intrinsic.reflect.type-apply`

*Source:* `src/compiler/mono_clone.cpp#L2085-L2119`

### `intrinsic.reflect.args-of` — args_of::<T>() yields T's generic type arguments

args_of::<T>() produces a [Type; N] descriptor array of the generic type-arguments of T (in order); for a non-generic T the array is empty.

*Related:* `intrinsic.reflect.type-descriptor-array`

*Source:* `src/compiler/mono_clone.cpp#L2780-L2783`

### `intrinsic.reflect.datatype` — reflect on a concrete datatype

`reflect::<T>()` requires exactly one type argument. A bare TypeVar T is deferred to mono. Otherwise T must be a concrete (non-generic, no type-args) ZonedStruct datatype; it registers a reflect request for `pkg::T` and yields a `WritStatic`.

*Source:* `src/compiler/sema_expr.cpp#L4877-L4899`

### `intrinsic.reflect.deferred-fold-after-subst` — Type-introspection intrinsics fold after substitution at mono

Type-trait/type-introspection intrinsics taking type-args are not evaluated at sema; each lowers to a magic `__<name>__` call carrying its type-args, and is folded to a concrete value only after monomorphization substitutes those type-args. Inside a generic body where T is still a type variable the call is preserved (never frozen to 'TypeVar' semantics).

*Divergence:* Logos addition: compile-time type reflection intrinsics (no Rust equivalent).

*Source:* `src/compiler/sema_expr.cpp#L5014-L5017`, `src/compiler/sema_expr.cpp#L5079-L5087`, `src/compiler/sema_expr.cpp#L5142-L5146`

### `intrinsic.reflect.field-count-of` — field_count_of::<T>() yields struct field count

field_count_of::<T>() evaluates at compile time to an i64 literal equal to the number of declared fields of T when T is a struct (or zoned struct) type; for any non-struct or unresolvable T it is 0. The struct template is matched by name, preferring a package-qualified match (T.pkg) and falling back to name-only.

*Source:* `src/compiler/mono_clone.cpp#L2703-L2730`

### `intrinsic.reflect.field-names-of` — field_names_of::<T>() yields array of field-name strings

field_names_of::<T>() evaluates at compile time to an array [&str; N] whose elements are the declared field names of struct T in declaration order; for non-struct or unresolvable T it is the empty array. Struct lookup prefers a package-qualified match and falls back to name-only.

*Source:* `src/compiler/mono_clone.cpp#L2733-L2768`

### `intrinsic.reflect.field-types-of` — field_types_of::<T>() yields substituted struct field types

field_types_of::<T>() produces a [Type; N] descriptor array of the field types of struct (or zoned struct) T in declaration order, with the struct template's type parameters substituted by T's actual type arguments (positional binding of template params to T.type_args); empty for non-struct or unresolvable T.

*Related:* `intrinsic.reflect.type-descriptor-array`

*Source:* `src/compiler/mono_clone.cpp#L2797-L2827`

### `intrinsic.reflect.has-trait-of` — has_trait_of::<Trait>(t: Type) -> bool folds at monomorphization

`has_trait_of::<Trait>(t)` (callee __has_trait_of__) folds to a `bool` literal during monomorphization. The concrete type T is recovered from t's `Type` struct-literal `uid` field, which must be a `__type_uid_of__::<T>()` call (after chasing VarRef through let-bindings, max 8 hops); T is substituted with the active type substitution. The result is `true` iff T (named by its concrete struct name, enum name, or type_str, truncated at any `$G` generic-suffix) has an impl of Trait, computed recursively over concrete and blanket impls.

*Divergence:* A6 (Logos-only metaprog/reflection intrinsic; no Rust equivalent)

*Source:* `src/compiler/mono_clone.cpp#L1588-L1652`

### `intrinsic.reflect.reify-type` — reify_type(t: Type) -> Type recovers a source TypeRef and re-emits Type

`reify_type(t)` (callee __reify_type__) recovers a concrete TypeRef from a direct Type-producer argument and re-emits a fresh `Type` struct literal. Supported argument shapes (after chasing VarRef through let-bindings, max 8 hops): (1) a `Type` struct literal whose `uid` field is `__type_uid_of__::<T>()` → T substituted; (2) a `__typelist_head__`/`__typelist_nth__` call → the indexed pack element. A missing argument is fatal; any other (unsupported) shape is a fatal compile-time error.

*Divergence:* A6 (Logos-only reflection intrinsic)

*Related:* `intrinsic.reflect.type-struct-shape`

*Source:* `src/compiler/mono_clone.cpp#L1741-L1834`

### `intrinsic.reflect.tuple-count-of` — tuple_count_of::<T>() yields tuple arity

tuple_count_of::<T>() evaluates at compile time to an i64 literal equal to the number of element types of T when T is a tuple type, and to 0 for any non-tuple T.

*Source:* `src/compiler/mono_clone.cpp#L2685-L2698`

### `intrinsic.reflect.tuple-elems-of` — tuple_elems_of::<T>() yields tuple element types

tuple_elems_of::<T>() produces a [Type; N] descriptor array of the element types of T when T is a tuple; empty otherwise.

*Related:* `intrinsic.reflect.type-descriptor-array`

*Source:* `src/compiler/mono_clone.cpp#L2790-L2796`

### `intrinsic.reflect.type-apply` — type_apply(name, args: [Type;N]) -> Type instantiates a struct template

`type_apply(name, args)` (callee __type_apply__) instantiates the struct template named `name` (a string literal; surrounding quote chars stripped) with the TypeRefs recovered from `args` and folds to a `Type` value for the instantiation. `name` must be a string literal (else fatal). The instantiated type's `pkg_name` is taken from the matching template in the program's struct table. Each element TypeRef is recovered from the same producer shapes reify_type accepts (Type struct-lit uid call, or typelist head/nth); a non-recognized producer element is a fatal compile-time error.

*Divergence:* A6 (Logos-only type-level composition intrinsic)

*Related:* `intrinsic.reflect.type-struct-shape`, `intrinsic.reflect.reify-type`

*Source:* `src/compiler/mono_clone.cpp#L1841-L2083`

### `intrinsic.reflect.type-apply-pack-splice` — type_apply pack-splice fast path over Type-array intrinsics

When the `args` operand of type_apply is itself a Type-array producer intrinsic, its element TypeRefs are spliced directly into the template instantiation instead of requiring an array-literal shape: `type_refs_of` contributes its (substituted) type-args as the pack; `args_of::<T>` contributes T's type-args; `typelist_tail::<T>` contributes T's pack minus its first element; `tuple_elems_of::<T>` contributes T's tuple element types (only when T is a Tuple). Otherwise `args` must be an array literal (else fatal).

*Divergence:* A6 (Logos-only variadic type-pack splice)

*Related:* `intrinsic.reflect.type-apply`

*Source:* `src/compiler/mono_clone.cpp#L1878-L1972`

### `intrinsic.reflect.type-descriptor-array` — type-reflection intrinsics produce [Type; N] descriptors

args_of::<T>(), type_refs_of, tuple_elems_of, typelist_tail, and field_types_of each evaluate at compile time to an array [Type; N] of struct literals. Each Type element has fields {kind: u32 = the type's kind tag, name: &str = the type's printed name, size: i64 = size_of, align: i64 = align_of, uid: u64 = a canonical 64-bit type hash}. N and the per-element source types are determined per-intrinsic (see related rules).

*Source:* `src/compiler/mono_clone.cpp#L2774-L2869`

### `intrinsic.reflect.type-struct-shape` — Reflected Type value layout {kind,name,size,align,uid}

A reflected `Type` value materialized by a folding reflection intrinsic is the struct `Type` with fields `kind: u32` = the type's Kind tag, `name: &[u8]` = type_str(T), `size: i64` = size_of(T), `align: i64` = align_of(T), and `uid: u64` = a canonical 64-bit type hash (type_hash_64bit ∘ type_hash_23 ∘ type_id_canon). The compiler records uid→T so the value can be reified back to T.

*Divergence:* A6 (Logos-only reflection value)

*Related:* `intrinsic.reflect.typelist-head-nth`, `intrinsic.reflect.reify-type`, `intrinsic.reflect.type-apply`

*Source:* `src/compiler/mono_clone.cpp#L1716-L1730`, `src/compiler/mono_clone.cpp#L1810-L1833`, `src/compiler/mono_clone.cpp#L2074-L2082`

### `intrinsic.reflect.typeinfo-rodata` — reflect requests TypeInfo rodata

`reflect::<T>() -> WritStatic` is a compile-time request that registers T for reflection so a TypeInfo global is emitted; the expression resolves to the address of that emitted TypeInfo rodata.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5781-L5784`

### `intrinsic.reflect.typelist-head-nth` — typelist_head/nth::<L>(i) -> Type folds to a Type struct literal

`typelist_head::<L>()` and `typelist_nth::<L>(i)` (callees __typelist_head__/__typelist_nth__) fold to a single `Type { kind, name, size, align, uid }` struct literal describing element idx of L's type-arg pack: head uses idx=0; nth requires `i` to be a literal int. A missing type argument, a non-literal nth index, or an index outside [0, pack.size()) is a fatal compile-time error.

*Divergence:* A6 (Logos-only type-level pack intrinsic)

*Source:* `src/compiler/mono_clone.cpp#L1672-L1731`

### `intrinsic.reflect.typelist-len` — typelist_len::<L>() -> i64 folds to the pack arity

`typelist_len::<L>()` (callee __typelist_len__) folds to an `i64` literal equal to the number of type arguments in L's type-argument pack (0 when L is absent). O(1) compile-time probe; the canonical L is `TypeList<T...>`.

*Divergence:* A6 (Logos-only type-level pack intrinsic)

*Source:* `src/compiler/mono_clone.cpp#L1657-L1668`

### `intrinsic.reflect.typelist-tail` — typelist_tail::<T>() drops the first type argument

typelist_tail::<T>() produces a [Type; N] descriptor array of T's generic type-arguments excluding the first (i.e. the tail beginning at index 1); empty when T has fewer than two type arguments.

*Related:* `intrinsic.reflect.type-descriptor-array`

*Source:* `src/compiler/mono_clone.cpp#L2784-L2789`

### `intrinsic.reflect.writ-trait` — reflect on a writ trait registers a reflect request

`reflect::<Tr>()` where Tr names a writ trait (is_writ) registers a reflect request for `pkg::Tr` and evaluates to a `WritStatic` reflection of that trait/datatype.

*Divergence:* Logos addition (Writ reflection intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4851-L4876`

## type_of

### `intrinsic.type-of.type-struct` — type_of constructs a Type reflection struct

`type_of::<T>()` requires exactly one type argument and yields a `Type` struct literal with fields {kind: u32 (from __type_kind_of__), name: &[u8] (from __type_name_of__), size: i64 (size_of T), align: i64 (align_of T), uid: u64 (type_uid of T)}. Each component is concretized at mono.

*Divergence:* Logos addition (type reflection).

*Source:* `src/compiler/sema_expr.cpp#L5142-L5183`

## type_code_of

### `intrinsic.type-code-of.compute` — type_code_of derivation for zoned structs

For a concrete ZonedStruct T, type_code_of(T) = an explicit `#[type_code=N]` annotation on T (keyed by `pkg::Name`) if present, else a hash derived as type_hash_56bit(type_hash_23(canonical)) of the package-qualified canonical name, with raw codes < 128 biased up by +128 (reserving 0..127).

*Source:* `src/compiler/sema_expr.cpp#L4649-L4707`

### `intrinsic.type-code-of.signature` — type_code_of arity and result type

`type_code_of::<T>()` requires exactly one type argument and evaluates to a `u64` type code.

*Divergence:* Logos addition (Writ/zoned reflection intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4634-L4647`, `src/compiler/sema_expr.cpp#L4712`

### `intrinsic.type-code-of.typevar-defer` — type_code_of defers on TypeVar-bearing arguments

If T is a bare TypeVar, or a generic ZonedStruct any of whose type-args is a TypeVar, `type_code_of::<T>()` is deferred to monomorphization so each concrete instantiation gets its own type code; non-zoned non-typevar types yield code 0.

*Source:* `src/compiler/sema_expr.cpp#L4677-L4712`

### `intrinsic.type-code-of.writ-code` — type_code_of yields the Writ type code

`type_code_of::<T>()` yields `u64`, the Writ type_code of a concrete datatype = SHA-256 of "package::Name" truncated to 56 bits, shifted to >= 128 if needed (codes 1-127 reserved for inline AnyVal). For non-datatype T it yields 0.

*Divergence:* Logos addition (Writ substrate).

*Source:* `src/compiler/sema_expr.cpp#L5733-L5737`

## type_uid

### `intrinsic.type-uid.nominal-u64` — type_uid is nominal identity

`type_uid::<T>()` requires one type argument and yields `u64`: a NOMINAL 64-bit type identity (hash of the canonical named type string), so distinct nominal types differ even at identical layout (unlike type_hash). It is the low 64 bits of the 128-bit type UID and equals the `.uid` field exposed by type_of.

*Divergence:* Logos addition.

*Related:* `intrinsic.type-uid-hi.high-half`, `intrinsic.type-hash.structural-u64`

*Source:* `src/compiler/sema_expr.cpp#L5088-L5102`, `src/compiler/sema_expr.cpp#L5172-L5174`

## type_uid_hi

### `intrinsic.type-uid-hi.high-half` — type_uid_hi is the high half of the 128-bit UID

`type_uid_hi::<T>()` requires one type argument and yields `u64`, the HIGH 64 bits of the 128-bit nominal type UID; together with type_uid (low half) they form a 128-bit TypeId.

*Divergence:* Logos addition.

*Related:* `intrinsic.type-uid.nominal-u64`

*Source:* `src/compiler/sema_expr.cpp#L5103-L5115`

## type_hash

### `intrinsic.type-hash.structural-u64` — type_hash is layout-structural

`type_hash::<T>()` requires one type argument and yields `u64`: a structural FNV-1a-64 hash of T's layout — primitives map to fixed codes; struct/tuple/array/ptr hash a tag plus the recursive hashes of constituents, with NO struct/field names. Two structurally identical layouts hash equal; generic instances hash through their substituted args (Foo<i32> != Foo<u32>).

*Divergence:* Logos addition.

*Related:* `intrinsic.type-uid.nominal-u64`

*Source:* `src/compiler/sema_expr.cpp#L5073-L5087`

## type_refs_of

### `intrinsic.type-refs-of.pack-array` — type_refs_of reflects a type pack

`type_refs_of::<T...>()` yields `[Type; N]` with one Type value per pack member, substituted after pack expansion at mono. When the pack reduces to a single type-variable pack, the placeholder array carries a pack-size marker so let-bound/return types lift to the concrete `[Type; N]` automatically.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5670-L5701`

## Type lists

### `intrinsic.typelist.probe-family` — typelist O(1) probes over a type pack

Over L's type-pack (L.type_args()), one type argument required: `typelist_len::<L>() -> i64`; `typelist_head::<L>() -> Type` (error if pack empty); `typelist_nth::<L>(i) -> Type` requiring exactly one i64 index arg (out-of-range = error); `typelist_tail::<L>() -> [Type; N-1]`. Substituted after L is concrete.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5393-L5457`

## vtable_of

### `intrinsic.vtable-of.static-vtable-addr` — vtable_of yields a static vtable address

`vtable_of::<Trait, T>() -> *const u8` yields the address of the static vtable for `impl Trait for T`. Trait is read by NAME (must be a known trait, else error); T is resolved as a type and substituted at mono. Missing trait name or type is a compile error; an unknown trait name is a compile error.

*Divergence:* Logos addition.

*Related:* `intrinsic.dyn-from-parts.fat-trait-ptr`

*Source:* `src/compiler/sema_expr.cpp#L5278-L5312`

## generic_of

### `intrinsic.generic-of.signature` — generic_of requires a bare struct/enum name

`generic_of::<X>()` requires its single type-argument to be a bare named struct or enum (a TYPE_REF or GENERIC_INST with a NAME); the name must resolve to a declared struct or enum in the current program, otherwise a compile error.

*Divergence:* Logos addition (compile-time reflection intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4517-L4551`

### `intrinsic.generic-of.unapplied-ctor` — generic_of yields a handle for an unapplied generic constructor

`generic_of::<X>()` yields a Type-shaped value-handle for the unapplied generic constructor X (struct or enum) with kind=Generic, name=X, size=arity, and UID = FNV-1a of "generic:X".

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5615-L5619`

### `intrinsic.generic-of.value` — generic_of yields a Type descriptor

`generic_of::<X>()` evaluates to a `Type` struct literal with kind = Generic, name = X, size = X's type-parameter arity (count of declared type params), align = 0, and a uid = FNV-1a hash of "generic:" ++ X.

*Source:* `src/compiler/sema_expr.cpp#L4552-L4573`

## template_of

### `intrinsic.template-of.decl-handle` — template_of yields a Template handle to a declaration

`template_of::<X>()` resolves X at sema, locates the declaration item named X in the current AST root, and yields a `Template { raw: AnyVal { raw: <offset> } }` baking that declaration's arena offset as a u32 literal (same-AST scope).

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5621-L5627`

### `intrinsic.template-of.lowering` — template_of lowers to runtime AST-node anchoring

`template_of::<X>()` lowers to `template_of_at(off)` where `off` is the holder-relative AST node offset of the matching top-level item, producing a `Template` whose `raw` is anchored to the module-AST OView base at runtime.

*Source:* `src/compiler/sema_expr.cpp#L4612-L4631`

### `intrinsic.template-of.signature` — template_of requires a top-level item name in the current file

`template_of::<X>()` requires its single type-argument to be a bare named item; X must name a top-level declaration in the current source file, otherwise a compile error. It also requires `use logos.std.compiler.metaprog` (the `template_of_at` shim) to be in scope.

*Divergence:* Logos addition (metaprogramming intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4576-L4632`

## sizeof

### `intrinsic.sizeof.byte-size` — sizeof yields byte size

`sizeof::<T>()` requires exactly one type argument and yields `i64` = byte size of T.

*Divergence:* Logos spelling of size_of; result is i64 (Rust mem::size_of -> usize).

*Source:* `src/compiler/sema_expr.cpp#L5703-L5716`

## sizeof pack

### `expr.sizeof-pack.spelling` — sizeof...(T) on a type-parameter pack

The pack-size operator must be spelled `sizeof...(T)` where T is an in-scope type parameter; it yields a u64. A different operator name or an unknown type parameter is an error.

*Source:* `src/compiler/sema_expr.cpp#L1053-L1069`

### `intrinsic.sizeof-pack.length-of-type-pack` — sizeof...(T) yields pack length

`sizeof...(T)` is a value-position expression yielding the length of the type pack `T` as a `u64`.

*Source:* `tools/peg_gen/grammars/logos.peg#L271`

## align_of

### `intrinsic.align-of.alignment` — align_of yields alignment

`align_of::<T>()` requires exactly one type argument and yields `i64` = alignment of T.

*Divergence:* Result is i64 (Rust mem::align_of -> usize).

*Source:* `src/compiler/sema_expr.cpp#L5718-L5731`

## offset_of

### `intrinsic.offset-of.compile-time-byte-offset` — offset_of! yields compile-time field offset

`offset_of!(Type, field)` evaluates at compile time to the byte offset of `field` within `Type`'s ABI layout, as an i64 constant.

*Source:* `tools/peg_gen/grammars/logos.peg#L323`

### `intrinsic.offset-of.form` — offset_of! intrinsic

`offset_of!(Type, field)` yields the byte offset of `field` within `Type`.

*Source:* `tools/peg_gen/grammars/logos.peg#L2729-L2730`

### `intrinsic.offset-of.generic-subst` — offset_of! substitutes the type's generic args

When the struct is generic, the concrete type arguments of `T` are substituted into the field types before computing sizes/alignments, so `offset_of!` reflects the layout of the concrete instantiation.

*Source:* `src/compiler/sema_expr.cpp#L17649-L17659`

### `intrinsic.offset-of.struct-only` — offset_of! requires a struct type

The type argument of `offset_of!` must resolve to a struct or zoned-struct type; otherwise it is a compile error. The named struct must be known.

*Source:* `src/compiler/sema_expr.cpp#L17635-L17648`

### `intrinsic.offset-of.syntax` — offset_of! signature

`offset_of!(Type, field)` requires both a type argument and a field name; either missing is a compile error.

*Source:* `src/compiler/sema_expr.cpp#L17630-L17634`

### `intrinsic.offset-of.value` — offset_of! yields a compile-time i64 byte offset

`offset_of!(T, f)` evaluates to an `i64` constant equal to the byte offset of field `f` within `T`'s layout, computed by sequentially laying out fields: each field is placed at the next position aligned up to its alignment, then advanced by its byte size. Result type is `i64`.

*Divergence:* Rust's offset_of! yields usize; Logos yields i64.

*Source:* `src/compiler/sema_expr.cpp#L17657-L17681`

## bits

### `intrinsic.bits.u64-bit-ops` — u64 bitwise intrinsics

`popcount_u64`, `leading_zeros_u64`, `trailing_zeros_u64` each take 1 u64 argument and return u32; `bswap_u64`, `bitreverse_u64` each take 1 u64 argument and return u64. Wrong arity is an error. (Lower to the corresponding LLVM intrinsics; ctlz/cttz are non-poison at zero.)

*Divergence:* Logos addition: explicit free-function bit-op intrinsics.

*Source:* `src/compiler/sema_expr.cpp#L3186-L3204`

## field_count_of

### `intrinsic.field-count-of.struct-field-count` — field_count_of yields struct field count

`field_count_of::<T>()` requires one type argument and yields `i64` = number of declared fields of struct T (0 for non-struct or unknown-struct T).

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5562-L5582`

## Field reflection

### `intrinsic.field-reflect.types-and-names` — field_types_of / field_names_of reflect struct fields

`field_types_of::<T>()` yields `[Type; N]` of T's field types and `field_names_of::<T>()` yields `[&[u8]; N]` of T's field names; each requires one type argument; non-struct T yields empty arrays. At mono field types are substituted via the SubstMap built from the struct template's type_params -> T.type_args().

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5584-L5613`

## Variant reflection

### `intrinsic.variant-reflect.enum-family` — Enum-variant decompose intrinsics

Each requires one type argument E: `variant_count_of::<E>() -> i64`; `variant_names_of::<E>() -> [&[u8]; N]`; `variant_payload_counts_of::<E>() -> [i64; N]`; `variant_payload_types_flat_of::<E>() -> [Type; M]`. For non-enum or unknown E all yield 0 / empty arrays.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5629-L5668`

## args_count_of

### `intrinsic.args-count-of.arg-count` — args_count_of yields generic-arg count

`args_count_of::<T>()` requires one type argument and yields `i64` = number of T's generic type arguments (0 for primitive or non-generic struct).

*Divergence:* Logos addition.

*Related:* `intrinsic.args-of.type-arg-array`

*Source:* `src/compiler/sema_expr.cpp#L5213-L5233`

## args_of

### `intrinsic.args-of.type-arg-array` — args_of yields generic type arguments

`args_of::<T>()` requires one type argument and yields `[Type; N]` listing T's generic type arguments; for non-generic T the result is `[Type; 0]`. The array length is fixed at mono once T is concrete.

*Divergence:* Logos addition.

*Related:* `intrinsic.args-count-of.arg-count`

*Source:* `src/compiler/sema_expr.cpp#L5185-L5211`

## tuple_count_of

### `intrinsic.tuple-count-of.elem-count` — tuple_count_of yields tuple element count

`tuple_count_of::<T>()` requires one type argument and yields `i64` = number of elements in tuple T (0 for non-tuple T).

*Divergence:* Logos addition.

*Related:* `intrinsic.tuple-elems-of.elem-types`

*Source:* `src/compiler/sema_expr.cpp#L5516-L5534`

## tuple_elems_of

### `intrinsic.tuple-elems-of.elem-types` — tuple_elems_of yields tuple element types

`tuple_elems_of::<T>()` requires one type argument and yields `[Type; N]` of T's element types; empty array for non-tuple T.

*Divergence:* Logos addition.

*Related:* `intrinsic.tuple-count-of.elem-count`

*Source:* `src/compiler/sema_expr.cpp#L5536-L5560`

## tuple field debug

### `intrinsic.tuple-each-field-debug.requires-tuple` — tuple_each_field_debug formats every tuple field

`tuple_each_field_debug::<T>(self, f)` requires one type argument that MUST be a tuple type (else compile error) and exactly two value arguments; result type is the enclosing function's return type. It Debug-formats every field of T into Formatter f, deferring to a `__tuple_each_field_debug__` placeholder expanded at mono.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5473-L5514`

## tuple_all_eq

### `intrinsic.tuple-all-eq.chain-expand` — tuple_all_eq expands an element-wise eq chain

`tuple_all_eq::<T>(a, b)` expands to the conjunction `a.0.eq(&b.0) && ... && a.{N-1}.eq(&b.{N-1})`. If T is a concrete tuple the chain is expanded at sema; if any element is a type variable a `__tuple_all_eq__` placeholder is emitted and expanded at mono once T's arity is concrete.

*Divergence:* Logos addition (variadic-tuple support).

*Source:* `src/compiler/sema_expr.cpp#L5459-L5471`

### `intrinsic.tuple-all-eq.concrete-expansion` — tuple_all_eq concrete expansion via per-element eq

For a fully concrete tuple T = (T0,..,Tn-1), `tuple_all_eq::<T>(a,b)` expands to the `&&`-conjunction over i of `Ti::eq(&a.i, &b.i)`, where each `eq` impl is resolved by candidate lookup on `<Ti>__eq` requiring a 2-parameter signature `(&Ti, &Ti)`. If no `eq` impl exists for some element type, it is a compile error.

*Source:* `src/compiler/sema_expr.cpp#L4469-L4514`

### `intrinsic.tuple-all-eq.signature` — tuple_all_eq arity and tuple constraint

`tuple_all_eq::<T>(a, b)` requires exactly one type argument T which must be a tuple type, and exactly two value arguments; otherwise a compile error. Result type is `bool`. An empty tuple yields the constant `true`.

*Divergence:* Logos addition (variadic-tuple support intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4413-L4451`

### `intrinsic.tuple-all-eq.typevar-defer` — tuple_all_eq defers to mono on unbound tuple elements

If any element type of the tuple T is an unbound TypeVar, `tuple_all_eq::<T>(a,b)` is deferred to monomorphization as a `__tuple_all_eq__` call carrying T; otherwise it is expanded at sema time.

*Related:* `mono.subst.const-arg`

*Source:* `src/compiler/sema_expr.cpp#L4452-L4468`

## has_trait

### `intrinsic.has-trait.t-trait-bool` — has_trait queries impl tables

`has_trait::<T, Trait>()` requires two type arguments and yields `bool`: whether concrete T implements Trait, resolved at mono against the same impl tables (concrete + recursive blanket lookup) that drive method dispatch. The second argument is read by its identifier name only (passed as a string literal arg), not resolved as a type. Missing T or empty Trait name is a compile error.

*Divergence:* Logos addition.

*Related:* `intrinsic.has-trait-of.type-method`

*Source:* `src/compiler/sema_expr.cpp#L5235-L5270`

## has_trait_of

### `intrinsic.has-trait-of.lowering` — has_trait_of dispatches to runtime helper with trait name

`has_trait_of::<Trait>(t)` lowers to a call `__has_trait_of__(name, t)` where `name` is the trait's identifier passed as a `[u8]` string literal; the trait is identified by name only.

*Uncertainty:* Trait identity is by bare name string; package-qualification semantics not enforced at this site.

*Source:* `src/compiler/sema_expr.cpp#L4400-L4410`

### `intrinsic.has-trait-of.signature` — has_trait_of arity and shape

`has_trait_of::<Trait>(t)` requires exactly one trait type-argument (a single named type in the turbofish) and exactly one value argument; violating either is a compile error. It evaluates to `bool`.

```logos
let b: bool = has_trait_of::<Display>(x);
```

*Divergence:* Logos addition (reflection intrinsic); no Rust equivalent.

*Source:* `src/compiler/sema_expr.cpp#L4367-L4410`

### `intrinsic.has-trait-of.type-method` — has_trait_of is the Type-method form of has_trait

`has_trait_of::<Trait>(t: Type) -> bool` recovers concrete T from the value t's Type.uid field and runs the same impl-table recursion as has_trait.

*Divergence:* Logos addition.

*Related:* `intrinsic.has-trait.t-trait-bool`

*Source:* `src/compiler/sema_expr.cpp#L5272-L5276`

## has_annotation

### `intrinsic.has-annotation.const-fold` — has_annotation is a compile-time annotation check

`has_annotation::<T, A>()` requires exactly two type arguments and const-folds to `bool`: true iff datatype T carries a user annotation of annotation-type A. A must be a known annotation datatype (else compile error); the check matches against T's declared annotation instances by fully-qualified or simple name.

*Divergence:* Logos addition (annotation metaprogramming).

*Source:* `src/compiler/sema_expr.cpp#L5786-L5823`

## get_annotation

### `intrinsic.get-annotation.option-result` — get_annotation yields the annotation instance as Option<A>

`get_annotation::<T, A>() -> Option<A>` const-folds to `Some(A{...})` if datatype T carries annotation A, else `None`.

*Divergence:* Logos addition.

*Related:* `intrinsic.has-annotation.const-fold`

*Source:* `src/compiler/sema_expr.cpp#L5825-L5827`

### `intrinsic.get-annotation.signature` — get_annotation arity and annotation-type constraint

`get_annotation::<T, A>()` requires exactly two type arguments; A must be a ZonedStruct that is an annotation type. `Option` must be in scope. Result type is `Option<A>`.

*Divergence:* Logos addition (compile-time annotation reflection intrinsic).

*Source:* `src/compiler/sema_expr.cpp#L4901-L4938`

### `intrinsic.get-annotation.value` — get_annotation materializes the annotation instance

`get_annotation::<T, A>()` returns `Option::None` if T carries no annotation of type A; otherwise `Option::Some(A{...})` where the A literal is reconstructed field-by-field from the stored annotation values (int/float/bool/string/enum/array kinds), matched by annotation fqn or bare name.

*Source:* `src/compiler/sema_expr.cpp#L4942-L5010`

## is_kind

### `intrinsic.is-kind.predicate-family` — Type-kind predicate family

The predicates is_ptr / is_ref / is_mut_ref / is_struct / is_zoned / is_enum / is_tuple / is_slice / is_array / is_integer / is_signed / is_unsigned / is_float / is_bool / is_primitive each take exactly one type argument and yield `bool`, resolved against the substituted T at mono. Wrong arity is a compile error.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5127-L5140`

## is_same

### `intrinsic.is-same.two-type-args` — is_same arity and result

`is_same::<T1, T2>()` requires exactly two type arguments and yields `bool`; structural/identity equality of T1 and T2 is resolved post-substitution at mono. Wrong arity is a compile error.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5018-L5026`

## is_data_plain_of

### `intrinsic.is-data-plain-of.copyable-predicate` — is_data_plain_of predicates DataPlain layout

`is_data_plain_of::<T>()` yields `bool`: true iff T is a DataPlain datatype (no relative-pointer fields). Array wrappers are stripped ([D; N] checks D). Non-datatype types (scalars, ordinary structs) always yield true; a generic (type-arg-bearing) zoned datatype yields false (conservative); an unknown datatype defaults to true.

*Divergence:* Logos addition (zoned/Writ datatypes).

*Source:* `src/compiler/sema_expr.cpp#L5739-L5779`

## marker panics

### `intrinsic.marker-panics.macro` — unreachable! / todo! / unimplemented! marker macros

`unreachable!`, `todo!`, and `unimplemented!` are thin wrappers around `panic!` with default prefix messages ("internal error: entered unreachable code", "not yet implemented", "not implemented"); with args they panic with `"<prefix>: {}"` filled by `format!(args)`. They type as `!` (Never) and are valid in any expression position.

*Source:* `src/compiler/sema_expr.cpp#L18348-L18390`

## stringify!

### `intrinsic.stringify.macro` — stringify! returns raw token text

`stringify!(…)` yields the raw source text between the parentheses as a `&str` (`Slice<u8>`) literal, without macro expansion of the contents.

*Source:* `src/compiler/sema_expr.cpp#L18333-L18346`

## include!

### `intrinsic.include.expr-only` — include! splices a file as an expression

`include!("path")` reads the file at compile time and re-parses its contents as an expression spliced at the call site; only expression-position include! is supported (item-position is a compile error). Paths are resolved relative to the including file.

*Divergence:* Rust supports item-position include!; Logos supports only expression position.

*Source:* `src/compiler/sema_expr.cpp#L18238-L18244`, `src/compiler/sema_expr.cpp#L17686-L17784`

## include_str!

### `intrinsic.include-str.macro` — include_str! / include_bytes! embed file contents

`include_str!("path")` and `include_bytes!("path")` read the file at compile time (path relative to the including file) and yield its contents as a `&str` (`Slice<u8>`) literal; both forms collapse to the same representation since `str` is `Slice<u8>`. Unreadable files are a compile error.

*Divergence:* Rust's include_bytes! has type &[u8;N] distinct from &str; in Logos both are Slice<u8>.

*Source:* `src/compiler/sema_expr.cpp#L18252-L18282`

## file!

### `intrinsic.file.macro` — file! / module_path! positional macros

`file!()` yields the current file path and `module_path!()` yields the current package name, each as a `&str` (`Slice<u8>`) string literal.

*Source:* `src/compiler/sema_expr.cpp#L18228-L18236`

## line!

### `intrinsic.line.macro` — line! / column! positional macros

`line!()` yields the current source line as `u32`; `column!()` yields `u32` 0 (columns are not tracked).

*Divergence:* column!() always returns 0 rather than the true column.

*Source:* `src/compiler/sema_expr.cpp#L18221-L18227`

## env!

### `intrinsic.env.macro` — env! / option_env! read environment at compile time

`env!("VAR")` yields the value of environment variable VAR as a `&str` literal and is a compile error if unset; `option_env!("VAR")` yields the value or an empty `&str` if unset.

*Divergence:* option_env! returns an empty &str tombstone rather than Option<&str>.

*Source:* `src/compiler/sema_expr.cpp#L18289-L18316`

## cfg!

### `intrinsic.cfg.macro` — cfg! evaluates to a bool

`cfg!(predicate)` evaluates the configuration predicate at compile time and yields a `bool` literal.

*Source:* `src/compiler/sema_expr.cpp#L18118-L18121`

## compile_error!

### `intrinsic.compile-error.macro` — compile_error! emits a compile-time error

`compile_error!("msg")` takes one string-literal argument and emits that message as a compile-time error.

*Source:* `src/compiler/sema_expr.cpp#L18392-L18409`

## Metaprogramming

### `intrinsic.metaprog.reify-type` — reify_type round-trips a Type value at mono time

`reify_type(t: Type) -> Type` takes exactly 1 argument and lowers to the `__reify_type__` mono intercept, which substitutes the argument and re-emits a fresh `Type` struct literal from its uid. Wrong arity is an error.

*Divergence:* Logos addition: type-reflection metaprogramming intrinsic.

*Source:* `src/compiler/sema_expr.cpp#L3139-L3154`

### `intrinsic.metaprog.type-apply` — type_apply / apply_generic instantiate a type-level template

`type_apply(name: &[u8], args: [Type; N]) -> Type` and `apply_generic(g: Type, args: [Type; N]) -> Type` each take exactly 2 arguments and lower to the `__type_apply__` / `__apply_generic__` mono intercepts, which recover concrete TypeRefs from each element and emit a fresh `Type` struct literal for `Name<T0,...>`. Wrong arity is an error.

*Divergence:* Logos addition: type-level composition metaprogramming intrinsics.

*Source:* `src/compiler/sema_expr.cpp#L3156-L3184`

## wstatic_hash_of

### `intrinsic.wstatic-hash-of.u64` — wstatic_hash_of identity hash

`wstatic_hash_of::<CFG>()` requires exactly one type argument and yields `u64`, the byte-hash identity of a WritStatic value; folded at mono once CFG is a concrete WStaticLit.

*Divergence:* Logos addition.

*Source:* `src/compiler/sema_expr.cpp#L5064-L5072`

