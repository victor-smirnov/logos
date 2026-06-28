# Expressions and Intrinsics

Scope: every expression-form (`expr`) and compiler-intrinsic (`intrinsic`) semantic rule of Logos. Rules are extracted from the compiler source layers — PEG grammar (`tools/peg_gen/grammars`), semantic analysis (`src/compiler/sema_*`), monomorphization (`src/compiler/mono_*`), and MLIR code generation (`src/compiler/mlir_gen_*`) — and grouped by their id middle-segment. Each rule's `id` is its permanent linkable address and is preserved verbatim.

<a id="expr-domain"></a>
# `expr` — Expressions

## Literals (literal)

### `expr.literal.float-format-and-suffix` — Float literal format, underscores, and suffix typing

A float literal must be well-formed; underscores are stripped from the digits; a recognized 3-char float suffix sets the literal's concrete type (e.g. f32/f64) while a suffix-less float literal has the inference type FloatLit.

_Source: `src/compiler/sema_expr.cpp#L999-L1013`_


### `expr.literal.kinds` — Primary literal forms

Primary literals: integer, float, char, string, raw string, byte string, and `true`/`false` booleans. A byte-string literal lowers to a `[u8; N]` array literal of its decoded bytes (escapes \n \t \r \0 \\ \" \x.. supported).

_Source: `tools/peg_gen/grammars/logos.peg#L2762-L2773`, `tools/peg_gen/grammars/logos.peg#L2764-L2768`_


## Literals (lit)

### `expr.lit.char-is-unicode-scalar` — Char literal is a Unicode scalar

A char literal `'X'` denotes a single Unicode scalar value, decoded to a `u32` scalar.

_Source: `tools/peg_gen/grammars/logos.peg#L292`, `tools/peg_gen/grammars/logos.peg#L296`_


## Integer literals (int-lit)

### `expr.int-lit.malformed` — Malformed integer literal is rejected

An integer literal whose textual form is not a valid integer literal is a compile error: 'malformed integer literal'.

_Source: `src/compiler/sema_expr.cpp#L223-L226`_


### `expr.int-lit.negate-fold` — Leading unary minus folds into integer literal for range check

A leading unary minus is folded into the integer literal before range checking, so the magnitude is bounded by |min| rather than max (e.g. `-128i8` is valid, equal to i8::MIN).

_Source: `src/compiler/sema_expr.cpp#L219-L221`, `src/compiler/sema_expr.cpp#L262-L271`_


### `expr.int-lit.overflow-reject` — Integer literals that exceed their type range are rejected

An integer literal whose value cannot be represented (would silently saturate/truncate) is a compile error: 'integer literal out of range'. ≤64-bit literals are bound-checked against i64/destination range; literals with u128/i128 suffix are bound-checked against the 128-bit range.

_Source: `src/compiler/sema_expr.cpp#L233-L235`, `src/compiler/sema_expr.cpp#L249-L252`_


### `expr.int-lit.suffix-range` — Suffixed integer literal bound-checked against suffix type

A suffixed integer literal `Nsuf` is given type `suf` and its magnitude is bound-checked against that type's range: signed types permit |min| (e.g. i8 down to -128, up to 127), unsigned types permit 0..2^N-1. Exceeding the bound is 'integer literal out of range for its suffix type'.

_Source: `src/compiler/sema_expr.cpp#L255-L293`_


### `expr.int-lit.unsigned-negative` — Negative value with unsigned suffix is rejected

A negative integer literal with an unsigned suffix (u8/u16/u32/u64/u128) is a compile error: 'negative value with unsigned suffix'.

_Source: `src/compiler/sema_expr.cpp#L238-L241`, `src/compiler/sema_expr.cpp#L283-L286`_


### `expr.int-lit.unsuffixed-type` — Unsuffixed integer literal has inferred-integer type

An integer literal without a suffix is given a polymorphic integer-literal type whose concrete type is resolved later by destination-type coercion; only suffixed literals get a fixed primitive type at lowering.

_Source: `src/compiler/sema_expr.cpp#L256-L258`, `src/compiler/sema_expr.cpp#L292-L293`_


## Integer literals (litint)

### `expr.litint.i128-two-halves` — 128-bit integer literal assembled from low and high words

A 128-bit integer literal's value is the 128-bit integer whose low 64 bits are value and high 64 bits are value_hi; neither half is discarded.

_Source: `src/compiler/mlir_gen_expr.cpp#L291-L297`_


### `expr.litint.usize-pointer-sized` — usize/isize literals are pointer-sized

A usize- or isize-typed integer literal is encoded at the target's pointer bit-width (e.g. 64 on a 64-bit target), not the default 32, so high bits are well-defined.

_Source: `src/compiler/mlir_gen_expr.cpp#L270-L279`_


### `expr.litint.width-by-type` — Integer literal bit-width from its inferred type

An integer literal is encoded at the bit-width of its inferred type: i8/u8=8, i16/u16=16, i24/u24=24, i32/u32=32, i56/u56=56, i64/u64=64, i128/u128=128, bool=1. usize/isize use the target pointer bit-width. An untyped integer literal (IntLit) defaults to 32 bits, widening to 64 bits when its value falls outside [INT32_MIN, INT32_MAX].

**Divergence.** A: i24/u24/i56/u56 are Logos-only integer widths (no Rust equivalent).

_Source: `src/compiler/mlir_gen_expr.cpp#L253-L298`_


## Float literals

### `expr.litfloat.f32-vs-f64` — Float literal precision from type, default f64

A float literal typed f32 is encoded as a 32-bit float; otherwise it is encoded as a 64-bit float (f64 is the default).

_Source: `src/compiler/mlir_gen_expr.cpp#L301-L311`_


## Boolean literals

### `expr.litbool.zero-one` — Boolean literal encoding

A boolean literal is a 1-bit integer: true=1, false=0.

_Source: `src/compiler/mlir_gen_expr.cpp#L313-L315`_


## Character literals

### `expr.char-lit.escapes` — Character literal escape sequences

A char literal `'c'` accepts the escapes \n \t \r \0 \\ \' \" ; \xNN (exactly 2 hex digits, byte 0..255); and \u{H..} (1..6 hex digits in braces). Any other escape is a compile error.

_Source: `src/compiler/sema_expr.cpp#L314-L374`_


### `expr.char-lit.unicode-scalar` — char value must be a valid Unicode scalar

A \u{H..} char value must be a Unicode scalar value: ≤ U+10FFFF and not in the surrogate range U+D800..U+DFFF; otherwise it is a compile error. A char literal lowers to a value of type `char`.

_Source: `src/compiler/sema_expr.cpp#L364-L368`, `src/compiler/sema_expr.cpp#L402`_


### `expr.char-lit.utf8-body` — Multibyte char literal body decoded as one UTF-8 codepoint

A char literal whose body is a single multibyte character is decoded as exactly one UTF-8 codepoint; a malformed or length-mismatched UTF-8 body is a compile error.

_Source: `src/compiler/sema_expr.cpp#L376-L401`_


## String literals

### `expr.str.as-bytes-identity` — &str.as_bytes() is the identity

Because `&str` is represented as `Slice<u8>` (same fat-pointer ABI as `&[u8]`), `s.as_bytes()` on a `Slice<u8>` receiver returns the receiver verbatim with no conversion.

**Divergence.** Logos models &str as Slice<u8> (writ/string-repr); identity conversion.

_Source: `src/compiler/sema_expr.cpp#L6472-L6481`_


### `expr.str.method-forwarding` — &str methods forward to stdlib free functions

On a `Slice<u8>` (= `&str`) receiver, the methods {starts_with→str_starts_with, ends_with→str_ends_with, contains→str_contains, eq_str→str_eq, cmp→str_cmp, index_of→str_index_of, find→str_index_of, trim→str_trim, trim_start→str_trim_start, trim_end→str_trim_end, split→split} desugar to a call of the named stdlib free function with the receiver as the first argument, when that function exists.

_Source: `src/compiler/sema_expr.cpp#L6482-L6517`_


## Byte-string literals

### `expr.bytes-lit.type` — Byte-string literal has type [u8; N]

A byte-string literal `b"…"` lowers to an array literal of type `[u8; N]` where N is the decoded byte count; it accepts the escapes \n \t \r \0 \\ \' \" and \xNN (2 hex digits). Unknown or malformed escapes are compile errors.

_Source: `src/compiler/sema_expr.cpp#L405-L471`_


## Array literals (array-lit)

### `expr.array-lit.bracket-comma` — Array literal

An array literal is a comma-separated element list in brackets: `[e0, e1, ...]`.

_Source: `src/compiler/sema_render.cpp#L333-L344`_


## Array literals (arr-lit)

### `expr.arr-lit.const-pack-expand` — Const-pack array expansion builds a symbolic-length array

An array literal `[N...]` over a `<const N...: T>` pack with a single pack-expand element of const-var element type builds a `[T; sizeof...(N)]` symbolic-length array; monomorphization later replaces the single pack-expand element with one integer literal per pack member.

_Source: `src/compiler/sema_expr.cpp#L10858-L10873`_


### `expr.arr-lit.dyn-hint-unsize` — &dyn Trait element hint unifies concrete refs via unsize coercion

Under a `[&dyn Trait; N]` annotation, a heterogeneous array of distinct `&Concrete` refs unifies to `&dyn Trait` when every element is compatible with, or unsize-coercible to, the dyn element type. Each not-already-`&dyn` element is wrapped in an explicit dyn-coercion cast (building the fat pointer / vtable per element); the homogeneity check is then skipped.

_Source: `src/compiler/sema_expr.cpp#L10629-L10677`_


### `expr.arr-lit.empty-needs-hint` — Empty array literal element type comes from an annotation hint

An empty array literal `[]` takes its element type from an enclosing `[T;N]`/`[T]`/`&[T]` annotation or return-type hint, building `[T;0]` (which borrows to an empty `&[T]`). Without such a hint the element type is unknown and a warning is emitted.

_Source: `src/compiler/sema_expr.cpp#L10529-L10548`_


### `expr.arr-lit.fnptr-hint` — FnPtr element hint unifies distinct FnItems

Under a `[fn(...) -> R; N]` annotation, a heterogeneous array of distinct function items coerces to a common function-pointer element type when every element is compatible with the hint; each non-matching element is cast to the hint and the hint becomes the element type.

_Source: `src/compiler/sema_expr.cpp#L10603-L10628`_


### `expr.arr-lit.homogeneous` — Array literal elements must be mutually compatible and range-checked

Absent a unifying hint, all array-literal elements must be pairwise compatible; the element type is the numeric unification of the elements. Integer-literal elements (including those nested in array/tuple literal elements, and element 0 retroactively against a later concrete anchor) are range-checked against the inferred concrete element type, reporting an out-of-range error per offending element/sub-element.

_Source: `src/compiler/sema_expr.cpp#L10678-L10843`_


### `expr.arr-lit.intlit-i64-widen` — IntLit element type widens to i64 on overflow of i32

When the inferred element type is the untyped integer-literal type, it is upgraded to i64 if any element value overflows the i32 range; otherwise it stays IntLit so annotation-based coercion (e.g. `[i64;N] = [1,2,3]`) remains applicable.

_Source: `src/compiler/sema_expr.cpp#L10844-L10856`_


### `expr.arr-lit.scalar-hint-adopt` — Concrete scalar element hint retypes literal elements up front

When an array literal has a concrete scalar integer/float element hint and every element is either already of the hint type or an in-range integer/float literal, all literal elements are retyped to the hint and the hint becomes the element type. An integer literal that does not fit the hinted width is an error (not a silent fall-back to the default int type).

_Source: `src/compiler/sema_expr.cpp#L10554-L10602`_


## Array expressions

### `expr.array.literal-forms` — Array literal and fill forms

Array literals: element list `[e1, e2, …]` and fill form `[value; N]` where N is an integer literal, a named const, `sizeof...(P)` (variadic pack length), or a `metacall` block. The fill form is preferred over the list form to resolve ambiguity.

_Source: `tools/peg_gen/grammars/logos.peg#L2863-L2873`, `tools/peg_gen/grammars/logos.peg#L2703-L2704`_


### `expr.array.struct-element-by-value` — Aggregate array elements are stored by value

When an array or array-typed struct field has aggregate element type (inline struct or nested array), each element initializer's value (not a pointer to it) is copied into the element slot. This makes returning and storing `[Struct; N]` / `[[T; M]; N]` by value well-defined.

_Source: `src/compiler/mlir_gen.cpp#L937-L972`, `src/compiler/mlir_gen.cpp#L1126-L1160`_


## Array fill expressions

### `expr.arr-fill.repeat-literal` — Array fill literal repeats the element to length N

`[v; N]` produces an array literal of element type T (= type of v) with N copies; the element is re-lowered for each slot. N must be a positive integer; the element IntLit is left unresolved so struct-literal type inference can widen it.

_Source: `src/compiler/sema_expr.cpp#L11461-L11529`, `src/compiler/sema_expr.cpp#L11517-L11528`_


### `expr.arr-fill.size-metacall` — Array fill length via metacall splice

`[v; metacall { <expr> }]` evaluates the block's tail expression by compile-time evaluation (CTFE), and the integer result becomes the array length. The metacall block must contain an integer tail expression. This is Logos's replacement for Rust const-eval at the array-length position.

**Divergence.** Logos explicit-metacall model replaces Rust const-expression array lengths.

_Source: `src/compiler/sema_expr.cpp#L11486-L11516`_


### `expr.arr-fill.size-sizeof-pack` — Array fill length via sizeof...(P)

`[v; sizeof...(P)]` where P is an in-scope type parameter yields a single-element array literal whose length is symbolic (`__sizeof_pack:P`); monomorphization repeats the element to the variadic pack's expanded length. Any spread operator other than `sizeof` is rejected; an undefined P is an error.

**Divergence.** Logos variadic-pack feature.

_Source: `src/compiler/sema_expr.cpp#L11468-L11485`_


## Tuple literals

### `expr.tuple-lit.one-elem-trailing-comma` — One-element tuple requires trailing comma

A tuple literal is `(e0, e1, ...)`; a single-element tuple is distinguished from a parenthesized expression by a mandatory trailing comma: `(e,)`.

_Source: `src/compiler/sema_render.cpp#L318-L331`_


## Tuple expressions

### `expr.tuple.unit-and-element-typing` — Tuple literal: unit, expected-type widening, overflow upgrade

`()` is the unit value of type `()`. Each tuple element is widened to its expected element type from a tuple type hint; an int-literal element that overflows i32 is upgraded to i64; the tuple type is the tuple of element types.

_Source: `src/compiler/sema_expr.cpp#L1585-L1633`_


## Range expressions

### `expr.range.desugar-range-struct` — lo..hi / lo..=hi desugar to stdlib Range constructors

A range expression requires integer bounds. Exclusive `lo..hi` lowers to `range_i32`/`range_i64`; inclusive `lo..=hi` lowers to the generic `range_incl_of` (RangeOfIncl<T>). The bound width is i64 if either bound is wider than 32 bits or an integer literal overflows i32, else i32; both bounds are widened to that bound type. Missing stdlib constructors are an error.

**Divergence.** Ranges are nominal stdlib structs (RangeI32/RangeI64/RangeOfIncl), not language built-ins

_Source: `src/compiler/sema_expr.cpp#L1310-L1387`_


### `expr.range.family` — Range expressions

Range value-expressions: `lo..hi` (half-open), `lo..=hi` (inclusive), `lo..` (from), `..hi` (to), `..=hi` (to-inclusive), `..` (full). An omitted side leaves the corresponding bound unspecified. Sema lowers each to a stdlib Range struct implementing `Iterator`. Range sits at the top of the value-expression precedence cascade (below it: logical operators).

_Source: `tools/peg_gen/grammars/logos.peg#L2392-L2409`_


## Never / diverging expressions

### `expr.never.fallback-on-diverging-callee` — Never-fallback for unbound type params with diverging callee

During type-argument inference, an unbound type parameter falls back to `!` (Never) only when its callee body always diverges (panic-tail / `loop{}`-tail), matching Rust-2024 `!`-fallback narrowed to: variable unbound AND callee body always diverges.

_Source: `src/compiler/sema_impl.hpp#L3731-L3736`_


## Name references

### `expr.name.innermost-scope-wins` — Name resolution: innermost binding wins, then module consts

A name resolves to its innermost in-scope local binding (shadowing-correct); if no local binding exists it falls back to a module-level const; otherwise it is unresolved.

_Source: `src/compiler/sema_impl.hpp#L2358-L2379`_


## Variable references

### `expr.var-ref.bare-variant-alias` — Imported no-payload enum variant usable as a bareword

A no-payload enum variant brought into scope via `use Type.{V, …};` (or the prelude bareword `None`) can be referenced as a bare identifier, constructing that variant; payload-carrying variants require call syntax.

_Source: `src/compiler/sema_expr.cpp#L511-L571`_


### `expr.var-ref.const-param-value-use` — Const-generic parameter usable in value position

A const-generic parameter `<const N: T>` referenced in expression position evaluates to a value of its underlying numeric type T (default i64); monomorphization substitutes the concrete constant.

_Source: `src/compiler/sema_expr.cpp#L481-L490`_


### `expr.var-ref.fn-item-type` — Bare function name has a distinct per-function fn-item type

A function name used as a value has a zero-sized fn-item type unique to that function (distinct type per function/instantiation), which auto-coerces to the corresponding `fn(T)->R` pointer type at value-use sites.

_Source: `src/compiler/sema_expr.cpp#L491-L510`_


### `expr.var-ref.undefined` — Reference to an undefined name is an error

A variable reference whose name resolves to no local binding, const-generic parameter, function, enum variant, or unit struct is a compile error: 'undefined variable'.

_Source: `src/compiler/sema_expr.cpp#L583-L584`_


### `expr.var-ref.unit-struct-value` — Unit struct name in value position constructs it

A bare name of a known zero-field, non-generic struct in value position constructs that struct (unit-struct construction); a fielded struct still requires `S { … }` form.

**Related.** `expr.var-ref.undefined`

_Source: `src/compiler/sema_expr.cpp#L573-L582`_


## Path expressions

### `expr.path.assoc-const-disambiguation` — `Type::member` not naming an enum variant is tried as an associated const

When a `Name::member` path parses as an enum literal but `Name` is not a known enum, it is resolved as an associated const access in order: (1) inherent assoc const `impl Name { const member }`; (2) trait assoc const `<Tr>::member` for any trait `Tr` impl'd for `Name`; (3) generic assoc-const projection when `Name` is a bound type parameter. The const's value AST is lowered once and cached.

_Source: `src/compiler/sema_expr.cpp#L11604-L11638`, `src/compiler/sema_expr.cpp#L11691-L11700`_


### `expr.path.method-as-fn-pointer` — Path to a non-generic method in value position becomes a fn pointer

A path `Type::method` (or `Trait::method`) used in value position, not naming a variant or const, denotes a function-pointer value when it resolves to a single non-generic method: its type is `FnPtr(param_types) -> ret`. For a trait-qualified head, resolution succeeds only when exactly one impl of the trait is in scope; otherwise it is ambiguous.

_Source: `src/compiler/sema_expr.cpp#L11639-L11680`, `src/compiler/sema_expr.cpp#L11773-L11814`_


### `expr.path.typaram-static-method-call` — `Z::method::<..>(args)` on a bound type parameter

A call `Z::method::<TArgs>(args)` where `Z` is a type parameter bound by a trait declaring a static `method` dispatches to the bound's static method, disambiguated from generic enum-variant construction by `Z` being a bound type parameter.

_Source: `src/compiler/sema_expr.cpp#L11815-L11837`_


## Static references

### `expr.static.extern-access-unsafe` — Accessing an extern static requires unsafe

Any access to an extern static outside an `unsafe` block is a compile error (Rust items.extern.static), with the same local/const-param shadowing suppression as mutable statics.

**Related.** `expr.static.mut-read-unsafe`

_Source: `src/compiler/sema_expr.cpp#L604-L607`, `src/compiler/sema_expr.cpp#L620-L623`_


### `expr.static.mut-read-unsafe` — Reading a mutable static requires unsafe

Reading a `static mut` outside an `unsafe` block is a compile error (Rust items.static.mut.safety); the gate is suppressed when the name is shadowed by a local binding or a const-generic parameter.

_Source: `src/compiler/sema_expr.cpp#L595-L628`_


## Associated-constant references

### `expr.assoc-const.generic-typeparam-projection` — T::CONST on an abstract type-param lowers to a per-impl accessor call

`T::CONST` where T is an abstract type-param bound by a trait declaring `const CONST` lowers to a zero-arg accessor call `T__kassoc_CONST()`; monomorphization rewrites `T__` to the concrete type and the per-impl accessor supplies the value.

_Source: `src/compiler/sema_impl.hpp#L3938-L3943`_


## Binary operators

### `expr.binop.bitwise-and-shift-set` — Integer bitwise and shift operators

`&`,`|`,`^` are bitwise and/or/xor; `<<` is logical left shift. `&&`/`||` applied to already-i1 values reduce to bitwise and/or.

_Source: `src/compiler/mlir_gen_expr.cpp#L903-L908`_


### `expr.binop.comparison-signedness` — Ordering comparisons select signed/unsigned by type

`<`/`>`/`<=`/`>=` use unsigned comparison when the LHS type is unsigned (u8..u128) or bool, signed comparison otherwise. bool is treated as unsigned so that `false < true` holds (i1 false=0 < true=1).

**Divergence.** bool ordering forced unsigned to preserve Rust's `false < true` despite i1 signed representation; documented inline as Rust-conformant intent.

_Source: `src/compiler/mlir_gen_expr.cpp#L1134-L1156`_


### `expr.binop.div-rem-signedness` — Division and remainder select signed/unsigned by type

`/` and `%` lower to unsigned division/remainder when the LHS type is unsigned (u8..u128), signed division/remainder otherwise.

_Source: `src/compiler/mlir_gen_expr.cpp#L885-L902`_


### `expr.binop.divergent-rhs-no-merge` — Diverging RHS of short-circuit yields no result

If the RHS of `&&`/`||` diverges (e.g. `c || return false`), the expression has no value and control does not reach the merge point; the result is taken solely from the short-circuit branch.

**Uncertainty.** Inferred from terminator check around the RHS store; the language-visible effect is that divergence propagates.

_Source: `src/compiler/mlir_gen_expr.cpp#L714-L724`_


### `expr.binop.float-width-unification` — Mixed float-width binop unification

When operands are floats of different widths: an untyped float literal operand is coerced to the typed operand's float type; if both are typed, the narrower is widened to the wider.

**Related.** `coerce.intlit.to-integer-typevar-float`

_Source: `src/compiler/mlir_gen_expr.cpp#L798-L820`_


### `expr.binop.int-to-float-promotion` — Mixed int/float binop promotes integer to float

When one operand is a float and the other an integer, the integer is converted to the float operand's type: unsigned-to-float if the integer type is unsigned (u8..u128), signed-to-float otherwise.

_Source: `src/compiler/mlir_gen_expr.cpp#L766-L797`_


### `expr.binop.integer-operand-widening` — Mixed integer-width binop widens narrower operand

When the two operands of a binary operator are integers of unequal width, the narrower is widened to the wider operand's width before the operation: zero-extension if the narrow operand's type is unsigned (u8/u16/u24/u32/u56/u64/u128) or bool, sign-extension otherwise.

_Source: `src/compiler/mlir_gen_expr.cpp#L732-L765`_


### `expr.binop.integer-overflow-trap` — Checked +/-/* trap on overflow

Integer `+`, `-`, `*` are checked: on overflow execution aborts (trap). Signed/unsigned overflow detection selects checked signed vs unsigned arithmetic by the LHS type's signedness. Intentional wrapping must use the `wrapping_add`/`wrapping_sub`/`wrapping_mul` intrinsics, which emit the unchecked operation.

**Divergence.** A13: always traps on integer +/-/* overflow regardless of build profile (Rust wraps in release, panics in debug); explicit wrapping_* for wraparound.

_Source: `src/compiler/mlir_gen_expr.cpp#L835-L884`_


### `expr.binop.parenthesized` — Binary operator is infix

A binary operation is written `lhs OP rhs` with OP an infix operator token.

_Source: `src/compiler/sema_render.cpp#L121-L126`_


### `expr.binop.pointer-equality` — Pointer == / != compares addresses

When operands are pointers (and not the deref-eligible reference-to-primitive case), `==`/`!=` compare pointer addresses.

_Source: `src/compiler/mlir_gen_expr.cpp#L1080-L1133`_


### `expr.binop.precedence-cascade` — Binary operator precedence

Binary precedence, lowest→highest: logical (`&&`/`||`) < comparison (`==` `!=` `<=` `>=` `<` `>`) < bitor `|` < bitxor `^` < bitand `&` < shift (`<<` `>>`) < additive (`+` `-`) < multiplicative (`*` `/` `%`) < `as`-cast < unary. All binary levels are left-associative.

_Source: `tools/peg_gen/grammars/logos.peg#L2585-L2636`, `tools/peg_gen/grammars/logos.peg#L2602-L2606`_


### `expr.binop.ptr-null-compare` — Pointer compared only against integer literal 0

A raw pointer may be compared (== / != / relational) with an integer literal, but the literal must be 0; comparing a pointer with any non-zero literal is an error.

```logos
ptr == 0
```

_Source: `src/compiler/sema_expr.cpp#L2274-L2289`_


### `expr.binop.ref-prim-autoderef-eq` — == / != on references to primitives dereferences

For `==`/`!=` where both operands are references (`&T`/`&mut T`) to the same primitive scalar type, the operands are dereferenced and the underlying values compared (value equality), rather than comparing the reference addresses. Matches the PartialEq-for-&T blanket impl.

_Source: `src/compiler/mlir_gen_expr.cpp#L1080-L1121`_


### `expr.binop.shift-right-signedness` — Right shift is arithmetic or logical by signedness

`>>` performs a logical (zero-filling) shift when the LHS integer type is unsigned (u8..u128), and an arithmetic (sign-filling) shift otherwise.

_Source: `src/compiler/mlir_gen_expr.cpp#L909-L922`_


### `expr.binop.short-circuit-logical` — Logical && / || short-circuit

For `a && b`: if `a` is false the result is false and `b` is not evaluated; otherwise the result is `b`. For `a || b`: if `a` is true the result is true and `b` is not evaluated; otherwise the result is `b`. Both produce a bool (i1).

_Source: `src/compiler/mlir_gen_expr.cpp#L688-L728`_


### `expr.binop.str-eq-by-content` — str equality compares contents via str_eq

== / != between two str operands (both Slice<u8> with u8 element) desugar to a call to stdlib `str_eq` (content comparison); != negates the result. With no `str_eq` in scope, falls back to (incorrect) pointer comparison.

_Source: `src/compiler/sema_expr.cpp#L2194-L2221`_


### `expr.binop.str-relational-cmp` — str ordering via str_cmp compared to 0

Relational operators {<,<=,>,>=} between two str operands desugar to `str_cmp(lhs, rhs) OP 0`, where str_cmp returns lexicographic -1/0/1 (i32).

_Source: `src/compiler/sema_expr.cpp#L2223-L2250`_


### `expr.binop.string-vs-str-eq` — String == str views String as str

For == and !=, when one operand is the struct String and the other is str (Slice<u8>), the String operand is viewed as str via .as_str() so the comparison proceeds through the str equality path.

```logos
s == "lit"
```

**Divergence.** Mirrors Rust `impl PartialEq<str> for String`.

_Source: `src/compiler/sema_expr.cpp#L1782-L1808`_


### `expr.binop.tuple-lexicographic-order` — Tuple ordering is lexicographic

For two tuples of equal arity with all-primitive element types, `<`/`<=`/`>`/`>=` compare lexicographically (left-to-right element priority), folding right-to-left as `lt_i || (eq_i && rest)`; the all-equal result is false for strict (`<`,`>`) and true for non-strict (`<=`,`>=`). `>`/`>=` are the operand-swapped forms of `<`/`<=`. Per-element comparison uses unsigned ordering for unsigned/bool/char element types and signed otherwise.

_Source: `src/compiler/mlir_gen_expr.cpp#L1006-L1078`_


### `expr.binop.tuple-structural-eq` — Tuple == / != is structural

For two tuples of equal arity with all-primitive element types, `==` is the conjunction of element-wise `==` and `!=` is its negation; comparison is performed per element (float elements compared with float equality). Tuples containing non-primitive elements (str, nested tuple, struct) are not structurally compared by this rule.

**Uncertainty.** Restriction to all-primitive fields is an implementation limitation noted as a follow-up, not a language design intent.

_Source: `src/compiler/mlir_gen_expr.cpp#L923-L1004`_


### `expr.binop.unknown-operator` — Unknown binary operator is an error

A binary operator not in the recognized set is rejected as an unknown binary operator.

_Source: `src/compiler/sema_expr.cpp#L2466-L2467`_


## Comparison operators

### `expr.cmp.chained-comparison-forbidden` — Chained comparisons are not supported

A chained comparison such as `a < b < c` is rejected; it must be written `a < b && b < c`.

_Source: `src/compiler/sema_expr.cpp#L1079-L1086`_


### `expr.cmp.no-chained-comparisons` — Chained comparisons rejected

A comparison chain with 2+ comparators in a row (`a < b < c`) is rejected at sema with the diagnostic "chained comparisons not supported; use `a < b && b < c`". It parses (CHAINED_CMP) but is not a valid program.

_Source: `tools/peg_gen/grammars/logos.peg#L289`_


### `expr.cmp.non-chainable` — Comparison operators are non-chainable

Comparison operators are non-chainable: at most one comparison per level is well-formed. A chain of 2+ comparators (e.g. `a < b < c`) is parsed as a distinct CHAINED_CMP node so sema can reject it with a dedicated diagnostic rather than a generic syntax error.

**Divergence.** Rust-conformant outcome (chained comparison is an error); Logos detects it grammatically for a better diagnostic.

_Source: `tools/peg_gen/grammars/logos.peg#L2589-L2600`, `tools/peg_gen/grammars/logos.peg#L2424-L2431`_


## Unary operators

### `expr.unary.double-ref` — Double address-of

`&&v` (lexed as a single AND token) is the double address-of of `v`: it lowers to `&(&v)` with type `& & typeof(v)`. If `typeof(v)` is an error type the whole expression is an error.

_Source: `src/compiler/sema_expr.cpp#L2476-L2486`_


### `expr.unary.neg-literal-fold` — Negated integer literal folds the sign

`-L` where L is an integer literal is parsed as the single negative literal `-L`, so the minimum suffixed value (e.g. `-128i8`) is accepted even though the bare positive literal would be out of range for its type.

_Source: `src/compiler/sema_expr.cpp#L2591-L2598`_


### `expr.unary.neg-numeric` — Unary minus requires a numeric operand

`-x` on a non-struct operand requires `x` to be of numeric type (else a type error); the result type equals the operand type.

_Source: `src/compiler/sema_expr.cpp#L2625-L2640`_


### `expr.unary.neg-unsigned-rejected` — Unary minus on an unsigned type is rejected

`-x` where `x` has any unsigned integer type (u8/u16/u24/u32/u56/u64/u128) is a compile error; an explicit cast to a signed type is required (e.g. `-(x as i64)`).

**Divergence.** Extra unsigned widths u24/u56 are Logos-only (A11); the unary-minus-on-unsigned rejection itself matches Rust (no `Neg` impl for unsigned).

_Source: `src/compiler/sema_expr.cpp#L2628-L2639`_


### `expr.unary.negation` — Unary minus

`-x` negates: floating-point negation for floats, `0 - x` for integers.

_Source: `src/compiler/mlir_gen_expr.cpp#L1166-L1172`_


### `expr.unary.not` — Unary not is logical on bool, bitwise on integers

`!x` is logical NOT (XOR with 1) when `x` is bool (i1) and bitwise complement (XOR with all-ones) when `x` is a wider integer. Applying `!` to a non-integer type is an error.

_Source: `src/compiler/mlir_gen_expr.cpp#L1173-L1189`_


### `expr.unary.not-bool-or-integer` — Unary ! is logical-not on bool and bitwise-not on integers

`!x` requires `x` to be `bool` (result `bool`) or an integer type (result = operand type; an untyped integer literal becomes `i32`); any other operand type is a type error.

_Source: `src/compiler/sema_expr.cpp#L2641-L2650`_


### `expr.unary.operator-overload` — Unary operators dispatch to Neg/Not impls on struct operands

For a struct operand, `-x` resolves to the `Neg::neg(self)->Self` method and `!x` to the `Not::not(self)->Self` method via mangled `<Type>__neg`/`<Type>__not` signature lookup; when found, the unary expression becomes that method call.

_Source: `src/compiler/sema_expr.cpp#L2605-L2622`_


### `expr.unary.operator-set` — Unary / prefix operators

Prefix unary operators (highest binding among operators): `*` deref, `&` borrow, `&mut` mutable borrow, `-` negate, `!` not. `&&v` (lexed as the AND token) means a double reference and lowers to nested address-of.

_Source: `tools/peg_gen/grammars/logos.peg#L2648-L2656`_


### `expr.unary.prefix-no-space` — Unary operators are prefix

Unary operators (`&`, `!`, `-`, etc.) are prefix and bind directly to their operand with no intervening space: `OP operand`.

_Source: `src/compiler/sema_render.cpp#L128-L133`_


## Cast expressions

### `expr.cast.as-chain` — as-cast chaining

`as`-casts (`v as T`) bind below unary operators and chain left-associatively, so `x as T1 as T2` folds as `(x as T1) as T2`.

_Source: `tools/peg_gen/grammars/logos.peg#L2638-L2646`, `tools/peg_gen/grammars/logos.peg#L2632`_


### `expr.cast.as-keyword` — Cast syntax

A cast is written `expr as Type`.

_Source: `src/compiler/sema_render.cpp#L135-L139`_


### `expr.cast.byte-string-to-array` — Byte-string literal is [u8; N]

A byte-string literal `b"..."` at expression position lowers to an array literal of type `[u8; N]` (escapes decoded).

_Source: `tools/peg_gen/grammars/logos.peg#L302`_


## Assignment

### `expr.assign.compound-op-set` — Compound assignment operators

The compound-assignment operators are `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`. A compound-assign statement is `place OP value ;` where `place` is an atom (postfix-chained lvalue) and `value` is a full `expr`.

_Source: `tools/peg_gen/grammars/logos.peg#L2324-L2327`_


### `expr.assign.dataref-field-unsafe` — DataRef<ZonedStruct> field write desugars via mut_ptr and needs unsafe

`p.field = v` where `p: DataRef<Z>` with `Z` a zoned struct desugars to `{ let t = p.mut_ptr(); (*t).field = v; }` (the DerefMut analog); it requires an `unsafe` context, `p` must be a mutable binding, and `v` must be type-compatible with the field type.

_Source: `src/compiler/sema_stmt.cpp#L7170-L7211`, `src/compiler/sema_stmt.cpp#L7303-L7312`_


### `expr.assign.deref-write` — Dereference write statement

`* p = v ;` writes value `v` through dereferenced place `p` (a `unary_expr`). `* p OP v ;` performs compound assignment through a bare dereference and is defined to lower to `*p = *p OP v`.

**Divergence.** Logos addition: distinct DEREF_WRITE/DEREF_COMPOUND statement forms; semantics match Rust place-expression assignment.

_Source: `tools/peg_gen/grammars/logos.peg#L2335-L2340`_


### `expr.assign.drop-before-replace` — Field assignment drops old value first

Assigning to a field place over an owned local root drops the place's prior value before the store, provided the value is live (root owned, definitely-initialized, no overlapping moved-out path) and droppable; assigning to a path also lifts drop-suppression for the covered (equal-or-deeper) moved paths so the scope-end drop releases the new value.

**Divergence.** Rust-conformant (expr.assign.drop-target / B8)

_Source: `src/compiler/sema_stmt.cpp#L7237-L7299`, `src/compiler/sema_stmt.cpp#L7450-L7455`, `src/compiler/sema_stmt.cpp#L7461-L7462`_


### `expr.assign.index-mut-desugar` — Indexed assignment uses IndexMut

For a type implementing `IndexMut`, `a[i] = v` desugars to a store through `*index_mut(&mut a, i)` (the trait method produces the writable place); the receiver `a` must be a mutable binding.

_Source: `src/compiler/sema_stmt.cpp#L7106-L7163`, `src/compiler/sema_stmt.cpp#L7225-L7236`_


### `expr.assign.place-nesting-bound` — Deeply-nested assignment targets rejected

A place-write target is accepted only for shapes the address-of machinery can lower: a bare variable, `*p`, and index/field/tuple-index chains bounded over those roots; deeper nestings (e.g. 3-level `a[i][j][k]`, `g.rows[i].cells[j].v`) are rejected with a clean diagnostic rather than miscompiled.

**Uncertainty.** The exact accepted shape set is defined by place_write_supported/place_field_base_ok recursion; bound is an implementation limitation, not a language-design boundary.

_Source: `src/compiler/sema_stmt.cpp#L6929-L6940`, `src/compiler/sema_stmt.cpp#L7316-L7321`_


### `expr.assign.place-only` — Assignment LHS must be an assignable place

The left side of a compound place assignment must be a genuine lvalue shape: an index `a[i]`, field access `a.f`, tuple index `a.N`, or dereference `*p`. Any other LHS (call result, literal, arithmetic) is rejected as 'not an assignable place'.

_Source: `src/compiler/sema_stmt.cpp#L7213-L7224`_


### `expr.assign.type-mismatch` — Assignment value must match place type

The assigned value's type must be compatible with the place's type (modulo `#[rel_ptr]`↔`*T` relations); otherwise a type-mismatch error is raised. Before the store the value is integer-widened to the place type, and the place type hints enum/struct literal RHS resolution.

_Source: `src/compiler/sema_stmt.cpp#L7354-L7373`, `src/compiler/sema_stmt.cpp#L7448`_


### `expr.assign.union-field-safe` — Writing a union field is safe

Writing to a union field is safe (no `unsafe` required for the write): the place-write LHS suppresses the union unsafe gate that otherwise applies when reading a union field.

**Divergence.** Rust-conformant (items.union.fields.write-safety)

_Source: `src/compiler/sema_stmt.cpp#L7325-L7331`_


## Compound assignment

### `expr.compound-assign.base-op-strip` — Compound-assign base operator

A compound-assign token `op=` denotes the binary operator `op` obtained by stripping the trailing `=`; the place is the receiver and the right side is the value operand.

_Source: `src/compiler/sema_stmt.cpp#L2277-L2294`_


### `expr.compound-assign.index-mut-dispatch` — Compound-assign through IndexMut on a struct

`a[i] op= v` where `a` has struct type with an `IndexMut` impl lowers to `*index_mut(&mut a, i) = (*index(&a, i)) op v`, using the `Index` read accessor for the current value when present (else `index_mut`); `a` must be `mut`, and the rhs must be compatible with the indexed output type.

_Source: `src/compiler/sema_stmt.cpp#L2400-L2467`_


### `expr.compound-assign.op-trait-mapping` — Compound-assign operator → *Assign trait/method

Each compound-assign operator `op=` maps to a trait + method: `+=`→AddAssign::add_assign, `-=`→SubAssign::sub_assign, `*=`→MulAssign::mul_assign, `/=`→DivAssign::div_assign, `%=`→RemAssign::rem_assign, `&=`→BitAndAssign::bitand_assign, `|=`→BitOrAssign::bitor_assign, `^=`→BitXorAssign::bitxor_assign, `<<=`→ShlAssign::shl_assign, `>>=`→ShrAssign::shr_assign. Operators outside this set have no *Assign trait.

_Source: `src/compiler/sema_stmt.cpp#L2260-L2274`_


### `expr.compound-assign.opassign-dispatch` — Compound-assign dispatches via *Assign impl when present

For a place of struct type S, if an impl of the operator's *Assign trait exists for S (matched by concrete or base struct name), `place op= rhs` lowers to the in-place call `op_assign(&mut place, rhs)` (void result, no assign-back). The trait method's Rhs parameter need not equal Self: the impl is selected by the actual rhs operand type, falling back to the Self-Rhs signature if the rhs-typed one does not resolve.

**Divergence.** Rust-conformant operator-overload semantics; Logos struct-name-keyed impl lookup.

_Source: `src/compiler/sema_stmt.cpp#L2315-L2351`, `src/compiler/sema_stmt.cpp#L2484-L2509`_


### `expr.compound-assign.opassign-fallback-binop` — Compound-assign without *Assign impl desugars to read-modify-write

Absent a matching *Assign impl, `place op= rhs` desugars to `place = (place) op rhs` (read-twice / double-eval of the place), dispatching `op` through the corresponding binary-operator trait (Add/Sub/…), which constructs a fresh Self.

_Source: `src/compiler/sema_stmt.cpp#L2304-L2305`, `src/compiler/sema_stmt.cpp#L2361-L2363`, `src/compiler/sema_stmt.cpp#L2511-L2525`_


### `expr.compound-assign.place-too-nested` — Compound-assign target nesting limit

A compound-assign target too deeply nested to write in place is rejected with guidance to bind an intermediate `&mut` reference.

**Uncertainty.** Implementation-capability limit rather than a designed language restriction.

_Source: `src/compiler/sema_stmt.cpp#L2472-L2477`_


### `expr.compound-assign.type-mismatch` — Compound-assign RHS type-compatibility

In the read-modify-write path, the rhs type must be compatible with the place type; otherwise "compound assignment: type mismatch — expected T, got U".

_Source: `src/compiler/sema_stmt.cpp#L2353-L2360`, `src/compiler/sema_stmt.cpp#L2512-L2518`_


### `expr.compound-assign.var-immutable` — Compound-assign requires a mutable place

`x op= e` requires `x` to be declared `mut`; an immutable target is rejected: "compound assignment to immutable variable".

_Source: `src/compiler/sema_stmt.cpp#L2301-L2302`, `src/compiler/sema_stmt.cpp#L2416-L2417`_


### `expr.compound-assign.var-undefined` — Compound-assign to undefined variable is an error

`x op= e` where `x` is not a bound variable is rejected: "compound assignment to undefined variable".

_Source: `src/compiler/sema_stmt.cpp#L2295-L2300`_


## Field-write assignment

### `expr.field-write.chain-auto-deref` — Chained field assignment auto-dereferences pointer segments

In a chained field assignment a.b.c...x = v, each intermediate path segment whose field type is a pointer-to-struct is dereferenced (one load) before descending, while embedded (non-pointer) struct segments are addressed in place; the final segment is the assignment target.

_Source: `src/compiler/mlir_gen_stmt.cpp#L2897-L2950`_


## Dereference-write assignment

### `expr.deref-write.aggregate-by-value-copy` — Deref-write of an aggregate/fat value copies the full footprint by value

A deref/place write `*p = v` where v is an aggregate or fat value copies the full value footprint, not an 8-byte pointer: a struct/zoned-struct, tuple, embedded datatype, or fixed-array pointee is memcpy'd by size; a closure or slice value copies its 16-byte fat pair; a bare fat `dyn` (TraitObject) or slice-tailed custom-DST destination copies 16 bytes via the reference repr. An enum pointee copies its inline {disc,payload} footprint (this is how `Option::take`/`*self = None` mutate through inline storage); a C-like (discriminant-only) enum falls to a scalar store.

_Source: `src/compiler/mlir_gen_stmt.cpp#L1281-L1311`, `src/compiler/mlir_gen_stmt.cpp#L1312-L1366`, `src/compiler/mlir_gen_stmt.cpp#L1367-L1386`_


### `expr.deref-write.drop-before-replace` — Deref/place write drops the old owned value before overwriting

Writing through a pointer/place to an owned droppable location runs the OLD value's destructor before the store, after the RHS has been materialized (so a self-referencing `p = f(&*p)` reads the old buffer before it is freed). Drop-before-store applies only to live owned droppable places.

_Source: `src/compiler/mlir_gen_stmt.cpp#L1225-L1234`_


## Field access

### `expr.field.autoderef-via-deref` — Field access auto-derefs through Deref

For receiver `r` of struct type S that has no field `f` but `S: Deref<Target=U>`, `r.f` is equivalent to `(*r).f`; the deref step repeats (bounded, up to 16 levels) until a type bearing field `f` is reached. Generalizes Box/Rc/Arc and any user Deref uniformly.

**Related.** `expr.field.ref-peel`

_Source: `src/compiler/sema_expr.cpp#L9166-L9181`_


### `expr.field.dataref-ergonomic-read` — DataRef<T> ergonomic field read

For receiver `p: DataRef<T>` where T is a zoned struct having field `f`, `p.f` is equivalent to `p.ptr().f`. The access requires an `unsafe` context.

**Uncertainty.** DataRef is a Logos-specific zone/Writ type; no direct Rust analogue.

_Source: `src/compiler/sema_expr.cpp#L9440-L9458`_


### `expr.field.dot-access` — Field access

Named field access is `receiver.field`.

_Source: `src/compiler/sema_render.cpp#L282-L295`_


### `expr.field.dst-prefix-positional` — Prefix (non-tail) field access on a DstRef is positional

For a fat-pointer receiver to a custom-DST struct, a non-tail prefix field is addressed positionally: its byte offset is computed by walking the sized prefix fields (with the DstRef's type-args substituted), and the field is read by dereferencing `data_ptr + offset` typed as the field type. This works uniformly for generic and non-generic DST instances, including those with no registered monomorphized layout.

**Divergence.** Custom-DST model — see DIVERGENCES B2.

_Source: `src/compiler/sema_expr.cpp#L9394-L9429`_


### `expr.field.dst-ref-unsafe` — Field read through a non-self-describing &DstStruct requires unsafe

Field access on a fat-pointer (DstRef) receiver `&DstStruct` requires an `unsafe` context, EXCEPT when the struct is `#[self_describing]` (its tail length is recovered in-band, so the borrow is a complete safe reference).

**Divergence.** Custom-DST raw-pointer-shaped field access — see DIVERGENCES B2.

_Source: `src/compiler/sema_expr.cpp#L9275-L9281`_


### `expr.field.dst-tail-dyn` — dyn-tail projection on a DstRef shares the carried vtable

For a fat-pointer receiver to a custom-DST struct whose last field has unsized-dyn type `dyn Tr`, `r.tail` yields a `&dyn Tr` fat pair `{ data = base + prefix_byte_size, vtable = the receiver's OWN carried vtable }`. The tail's metadata is the wide pointer's metadata (no static vtable lookup). The dyn prefix offset is aligned to pointer width (8) since the concrete payload alignment is not known statically.

**Divergence.** Custom-DST dyn-tail model — see DIVERGENCES B2/B3.

**Uncertainty.** Conservative 8-byte alignment for dyn tails noted as over-aligning vs Rust.

_Source: `src/compiler/sema_expr.cpp#L9330-L9335`, `src/compiler/sema_expr.cpp#L9346-L9368`_


### `expr.field.dst-tail-slice` — Slice-tail projection on a DstRef

For a fat-pointer receiver to a custom-DST struct whose last field `tail` has unsized-slice type `[T]`, `r.tail` yields a slice `{ data_ptr + prefix_byte_size, len }` reusing the fat pointer's len half; prefix_byte_size is the offset after all sized prefix fields, aligned to size_of(T) (capped at 8). Slice mutability follows the receiver: `(&mut Foo).tail: &mut [T]`, `(&Foo).tail: &[T]`.

**Divergence.** Custom-DST model — see DIVERGENCES B2.

_Source: `src/compiler/sema_expr.cpp#L9296-L9345`, `src/compiler/sema_expr.cpp#L9369-L9393`_


### `expr.field.hoist-droppable-rvalue-temp` — Droppable fresh-rvalue field base is hoisted to a statement temp

When a field is read off a fresh owned rvalue base of a move (droppable) type (`make().x`), the base is hoisted into a named statement-scoped temporary so it lives to end of statement and its Drop runs at scope exit; the field is then read from that local. A place or borrow base is left untouched.

_Source: `src/compiler/sema_expr.cpp#L9151-L9164`_


### `expr.field.inline-vs-pointer-field-descent` — Field access descends in place for inline fields, loads for pointer fields

Accessing field `f` of a struct: if `f` is an inline-embedded aggregate or a scalar-represented named type, the field lives in place and its address is the access target (so a `&mut self` method mutates the original storage). Only a genuine pointer-typed field denotes a separate object and is loaded to descend into it.

_Source: `src/compiler/mlir_gen.cpp#L789-L805`, `src/compiler/mlir_gen.cpp#L813-L829`_


### `expr.field.name-from-field-or-name-slot` — Field name resolved from FIELD then NAME slot

The accessed field name is taken from the FIELD slot; if empty (e.g. a substituted antiquotation that landed at the field-name position via NAME_VAR→NAME rewrite), it falls back to the NAME slot.

**Uncertainty.** Fallback is a metaprog-substitution artifact, not a user-facing surface rule.

_Source: `src/compiler/sema_expr.cpp#L9147-L9150`_


### `expr.field.not-a-struct-error` — Field read receiver must be a struct/class

A field read whose receiver does not resolve to a struct or class type is an error ('receiver is not a struct or class'), except during metaprog discovery when the receiver (or its pointee) is already of error type, in which case the error type is propagated silently.

_Source: `src/compiler/sema_expr.cpp#L9460-L9478`_


### `expr.field.pub-access` — Private field access restricted to defining package

A non-`pub` field is accessible only within the package that defines the struct (checked via check_pub_access against the struct's package). Variadic field families (`name_<n>`) are matched by prefix for the access check.

**Related.** `module.vis.pub-field`

_Source: `src/compiler/sema_expr.cpp#L9486-L9528`_


### `expr.field.raw-ptr-unsafe` — Field read through raw pointer requires unsafe

Reading a field through a raw pointer receiver (type `*const T`/`*mut T`) is only permitted inside an `unsafe` context; otherwise it is an error.

_Source: `src/compiler/sema_expr.cpp#L9182-L9184`, `src/compiler/sema_expr.cpp#L9251`_


### `expr.field.ref-peel` — Field access peels reference layers

For receiver of reference-like type, `r.f` peels extra reference layers via explicit derefs so a multiply-referenced base (`&&S`) accesses the field of the underlying struct: `r.f` for `r: &&S` ≡ `(*r).f`. One reference layer remains for the single-level field projection.

**Related.** `expr.field.autoderef-via-deref`

_Source: `src/compiler/sema_expr.cpp#L9252-L9264`_


### `expr.field.self-describing-thin-tail` — Self-describing DST tail through a thin raw pointer

For a thin raw pointer `p: *const/*mut Self` to a `#[self_describing]` struct whose last field is the unsized-slice tail, `p.tail` yields a slice `{ (p as *u8)+prefix_offset, dst_len(p) }`, where prefix_offset is the natural-aligned byte offset after all sized prefix fields and the tail length is recovered by calling the struct's `SelfDescribing::dst_len` method. Slice mutability follows the pointer's mutability.

**Divergence.** Custom-DST / self-describing model — see DIVERGENCES B2.

**Related.** `expr.field.dst-tail-slice`

_Source: `src/compiler/sema_expr.cpp#L9185-L9248`_


### `expr.field.tuple-index` — Tuple / field access

Postfix `.field` reads a named field and `.N` (integer) reads the Nth tuple/tuple-struct element.

_Source: `tools/peg_gen/grammars/logos.peg#L2684-L2685`, `tools/peg_gen/grammars/logos.peg#L2678-L2679`_


### `expr.field.union-read-unsafe` — Union field read requires unsafe

Reading a field of a union requires an enclosing `unsafe` block (only one field is active at a time). Writing to a union field is safe; the read-safety check is suppressed when the access is the LHS of an in-place write.

_Source: `src/compiler/sema_expr.cpp#L9495-L9509`_


### `expr.field.unknown-field-error` — Unknown field is an error

Reading a field name not declared on the resolved struct type is an error ('struct S has no field f').

_Source: `src/compiler/sema_expr.cpp#L9481-L9485`_


## Tuple indexing

### `expr.tuple-index.access` — Tuple/tuple-struct .N indexing with auto-deref

`recv.N` indexes a tuple (auto-deref through `&`/`&mut`) returning the Nth element type, or reads field N of a tuple-struct (auto-deref through `&Foo`/`&mut Foo`) with the struct's type-params substituted by the receiver's type-args. An out-of-range index is an error.

_Source: `src/compiler/sema_expr.cpp#L1636-L1697`_


### `expr.tuple-index.aggregate-element-by-address` — Tuple-index of inline-aggregate element yields its address

A tuple index `t.k` whose element type is a struct, tagged enum, slice, closure, trait object, or nested tuple yields the address of the inline element slot (the value being pointer-represented); scalar elements are loaded by value. A by-value tuple result is first materialized into storage before address computation.

_Source: `src/compiler/mlir_gen_expr.cpp#L3128-L3160`, `src/compiler/mlir_gen_expr.cpp#L3140-L3159`_


### `expr.tuple-index.auto-deref-receiver` — Tuple-index auto-derefs a reference receiver

A tuple index `t.k` where `t: &(..)`/`&mut(..)`/`*(..)` (reference/pointer whose pointee is a tuple) operates on the pointee tuple; the receiver pointer is used directly as the tuple address.

_Source: `src/compiler/mlir_gen_expr.cpp#L3113-L3120`_


### `expr.tuple-index.dot-number` — Tuple index access

Tuple element access uses a numeric field after a dot: `receiver.N`.

_Source: `src/compiler/sema_render.cpp#L297-L303`_


## Index expressions

### `expr.index.autoderef` — Autoderef at index position through Deref

A struct receiver at index position without its own `Index` impl is dereferenced through its `Deref` impl(s) until an indexable type appears, mirroring method-resolution autoderef. The walk is bounded to 4 steps. If a step yields a Slice or trait-object (fat) value, that value is taken directly as the receiver.

_Source: `src/compiler/sema_expr.cpp#L10399-L10424`_


### `expr.index.bracket` — Index expression

Indexing is written `receiver[index]`.

_Source: `src/compiler/sema_render.cpp#L305-L312`_


### `expr.index.generic-index-via-method` — Generic-struct Index impl routed through method-call machinery

When a struct impls `Index` but no concrete `__index` symbol exists yet (a generic impl, e.g. `impl<T> Index for Vec<T>`), `v[i]` lowers to `*v.index(i)` via the method-call path. The element type is the impl's `Index<Idx, Output>` second trait-arg with the struct's type-args substituted for the impl's type params (matched positionally against `TypeVar`s in the impl target pattern); the index is widened to the substituted `Idx` when it is not a type variable.

**Related.** `expr.index.user-index-read`

_Source: `src/compiler/sema_expr.cpp#L10454-L10485`_


### `expr.index.indexmut-place` — Mutable index place requires IndexMut, shared requires Index

For an index place `&mut a[i]` the receiver type must impl `IndexMut`; for `&a[i]` an `Index` impl suffices. The place lowers to a call of the impl's `__index_mut` / `__index` method (the unique 2-parameter candidate), returning the reference produced by that method directly (no extra deref). Trait presence is checked against both the concrete struct name and the base (generic) struct name.

**Related.** `expr.index.user-index-read`

_Source: `src/compiler/sema_expr.cpp#L10258-L10268`, `src/compiler/sema_expr.cpp#L10300-L10305`_


### `expr.index.integer-required` — Built-in index requires an integer index

For built-in (non-user-Index) indexing the index expression must have integer type; otherwise an `array index must be integer` error is reported.

_Source: `src/compiler/sema_expr.cpp#L10489-L10490`_


### `expr.index.place-real-slot` — Index-place receiver uses the real variable slot

When the index-place receiver is a plain variable, its address is taken from the real variable slot (`&mut v`), not a spilled copy, so the mutation through `IndexMut` persists. A receiver already of reference/pointer kind is passed through unchanged; other receiver shapes materialize a temporary reference.

_Source: `src/compiler/sema_expr.cpp#L10286-L10296`_


### `expr.index.ptr-to-dyn-loads-handle` — Indexing a *dyn Trait pointer loads an 8-byte dyn handle per slot

Indexing a `*const/*mut dyn Trait` (pointer whose pointee is a trait object) strides by pointer width per slot and loads the dyn handle; `p[0]` is the index form of `*p`.

_Source: `src/compiler/mlir_gen_expr.cpp#L2872-L2881`_


### `expr.index.range-slice` — Range indexing produces a sub-slice

A range index `recv[lo..hi]`, `recv[lo..]`, `recv[..hi]`, `recv[..]`, or inclusive `recv[lo..=hi]` produces a sub-slice `&[T]` via `slice_get_range(recv, lo, hi)`. The receiver must be a slice, array (decayed to `&[T]` via addr-of + slice-coercion), or reference-to-slice; otherwise an error is reported. Missing `lo` defaults to 0; missing `hi` defaults to INT64_MAX (clamped to len); an inclusive upper bound is lowered as `hi+1`. Bounds are widened to i64. `slice_get_range` must be in scope (`use logos.lang.slice`).

**Divergence.** Range-slicing relies on stdlib `slice_get_range`; open/inclusive ends are clamped to length rather than panicking on out-of-range as Rust does.

_Source: `src/compiler/sema_expr.cpp#L10328-L10389`_


### `expr.index.raw-ptr-unsafe` — Indexing through a raw pointer requires unsafe

Indexing a value of raw-pointer kind (`*const`/`*mut`) is only permitted inside an `unsafe` context; outside one it is an error.

_Source: `src/compiler/sema_expr.cpp#L10506-L10508`_


### `expr.index.read` — Index expression

`e[i]` is a postfix index-read; with a range index (`s[a..b]`, `s[a..]`, `s[..b]`, `s[..]`) it produces a slice.

_Source: `tools/peg_gen/grammars/logos.peg#L2690-L2691`, `tools/peg_gen/grammars/logos.peg#L2394-L2396`_


### `expr.index.read-write-same-slot` — Indexed read and write address identical slot

`s[i]` as an lvalue (for `&mut s[i]` or `s[i] = v`) computes the same element address as the by-value read: a slice loads the data pointer from descriptor field 0 then strides by the element slot type; an array strides from its storage; a pointer/ref variable strides from the loaded pointer value (indexing the pointee, or the pointee array's element for `*mut [T;N]`); a pointer field of fat elements loads the buffer base then strides by the 16-byte fat slot. Element stride equals the element type's place-slot type in every case so reads and writes never address different slots.

**Related.** `layout.place.element-slot-by-repr`

_Source: `src/compiler/mlir_gen_expr.cpp#L1288-L1392`_


### `expr.index.receiver-kind` — Built-in index receiver must be array, slice, or pointer/reference

A built-in index `a[i]` requires the receiver to be a Slice, Array, raw Ptr, or reference (`Ref`/`MutRef`); any other receiver kind is a type error. Slice indexing lowers to a dedicated slice-index operation; an array/ref/ptr yields the element type, auto-dereferencing a single reference/pointer layer (and through a `[T;N]` array pointee) to the element.

_Source: `src/compiler/sema_expr.cpp#L10492-L10526`_


### `expr.index.ref-to-array-decays-to-element-pointer` — Indexing through a reference/pointer to an array strides by element

Indexing a value of type `&[T;N]`, `&mut [T;N]`, or `*[T;N]` uses the SSA pointer directly as the address of element 0 and strides by the element type; the array is not loaded by value.

_Source: `src/compiler/mlir_gen_expr.cpp#L2842-L2859`_


### `expr.index.ref-to-slice-retype` — Indexing a reference-to-slice GEPs through the fat-pointer pair

When the receiver type is a reference to a slice (`Ref/MutRef -> Slice`, e.g. `&s` where `s: &[T]`), it is retyped to the pointee Slice rather than loaded, so `(&s)[i]` indexes the underlying `{data,len}` pair and yields element type `T` instead of the whole slice.

_Source: `src/compiler/sema_expr.cpp#L10311-L10326`_


### `expr.index.unsigned-index-extension` — Unsigned index extended to 64-bit

When an index expression has an unsigned integer type narrower than 64 bits, it is zero-extended to 64 bits before being used as a GEP index.

_Source: `src/compiler/mlir_gen_expr.cpp#L1305-L1315`, `src/compiler/mlir_gen_expr.cpp#L1383-L1389`_


### `expr.index.unsigned-index-zero-extends` — Unsigned index operand zero-extends to 64-bit

When indexing with an unsigned integer index (u8/u16/u24/u32/u56/u64/u128) narrower than 64 bits, the index is zero-extended to 64 bits before address computation.

_Source: `src/compiler/mlir_gen_expr.cpp#L2970-L2979`_


### `expr.index.unsigned-zero-extend` — Unsigned index operand is zero-extended to the index width

An index expression of an unsigned integer type (u8/u16/u24/u32/u56/u64/u128) is zero-extended to the address-index width before address computation, so e.g. u8(200) indexes element 200 rather than being sign-extended to a negative offset.

_Source: `src/compiler/mlir_gen_stmt.cpp#L3041-L3051`, `src/compiler/mlir_gen_stmt.cpp#L3140-L3159`_


### `expr.index.user-index-read` — Index read dispatches to user Index impl as *recv.index(i)

`a[i]` for a struct `a` that impls `Index<Idx, Output>` lowers to `*(a.index(i))`: the impl's `__index` method (unique 2-param candidate) is called with a materialized `&a` receiver and the index, and the result reference is dereferenced to yield the element place. The integer-literal index is widened to the formal index parameter type. User `Index` dispatch is attempted before the built-in integer-index check, so an impl may accept non-integer keys.

**Related.** `expr.index.indexmut-place`, `expr.index.generic-index-via-method`

_Source: `src/compiler/sema_expr.cpp#L10396-L10453`_


## Slice indexing

### `expr.slice-index.element-projection` — Slice indexing strides by element footprint

Indexing a slice `s[i]` loads the data pointer from field 0 and GEPs by index. Struct/ZonedStruct elements lay out inline (stride = sizeof(element)) and the element address is returned (caller copies aggregates by value); fat-reference elements (16B pairs stored inline) stride by the pair footprint and the slot address is returned; thin scalar/reference elements are loaded by value. An unsigned index narrower than 64 bits is zero-extended for the GEP.

_Source: `src/compiler/mlir_gen_expr.cpp#L5175-L5238`_


## Slice expressions

### `expr.slice.len-and-ptr-projection` — Slice .len() and .ptr() project metadata and data halves

For a fat slice receiver, `.len()` yields the metadata half (field 1, i64) and `.ptr()`/data yields the data half (field 0). For a thin #[self_describing] DstRef receiver, `.len()` is recovered in-band via dst_len(header_ptr) since it carries no out-of-band metadata.

_Source: `src/compiler/mlir_gen_expr.cpp#L5263-L5284`_


### `expr.slice.len-as-ptr-builtin` — Built-in slice/str length and pointer methods

On a slice receiver, `recv.len()` yields the element count as `i64`, and `recv.as_ptr()` yields the data pointer as `*const u8`. These are intrinsic (not user-resolved).

_Source: `src/compiler/sema_expr.cpp#L6463-L6471`_


## Dereference

### `expr.deref.aggregate-pointer-identity` — *p on aggregate-typed pointee is a no-op reinterpret

`*p` whose result type is a struct, tuple, array, slice, or trait-object yields the same pointer value (no load), since those types are pointer-represented; subsequent field/index access or by-value copy handles the byte-level move.

_Source: `src/compiler/mlir_gen_expr.cpp#L1762-L1802`_


### `expr.deref.box-move-out` — Dereferencing a move-typed Box moves the value out and frees the box

`*b` where `b` is a bare variable of type `Box<T>` and `T` is a move (non-Copy) type consumes `b`, moves the boxed value out, and frees the box without dropping the content (Rust's built-in `*b` move; desugars to `box_take::<T>(b)` with `T` inferred). This is the canonical `let s = *b` form. For a Copy element `T`, `*b` copies the value out and leaves `b` live (normal deref path); non-Box derefs also use the normal copy/deref path.

_Source: `src/compiler/sema_expr.cpp#L2688-L2725`, `src/compiler/sema_stmt.cpp#L1915-L1922`_


### `expr.deref.generic-autoderef-via-method-call` — Generic Deref/DerefMut autoderef lowers as a method call

Autoderef of a receiver whose type implements Deref/DerefMut — including a generic impl (Box/Rc/Arc) whose `deref` is not a concrete symbol at sema time — is emitted as a `deref()`/`deref_mut()` method call (monomorphized later), and the resulting Target place type is obtained by substituting the Deref impl's target pattern against the receiver type.

_Source: `src/compiler/sema_impl.hpp#L4242-L4251`_


### `expr.deref.non-pointer-identity` — Dereference of a non-pointer value is identity

`*x` where `x` is neither a raw pointer nor a reference (and has no Deref impl) yields `x` unchanged rather than an error.

**Divergence.** Rust rejects `*x` on a non-pointer; Logos relaxes it to identity to accept faithful Rust loop imports (B3-bg-07), since `for i in &v` already yields T not &T.

_Source: `src/compiler/sema_expr.cpp#L2669-L2680`_


### `expr.deref.prefix-star` — Dereference operator

Dereference is written with prefix `*`: `*expr`.

_Source: `src/compiler/sema_render.cpp#L314-L316`_


### `expr.deref.raw-ptr-unsafe` — Raw pointer dereference requires unsafe

`*p` where `p` is a raw pointer `*T` is only permitted inside an unsafe context; otherwise it is an error. The result type is the pointee type.

_Source: `src/compiler/sema_expr.cpp#L2681-L2685`_


### `expr.deref.scalar-load` — *p on scalar-typed pointee loads the value

`*p` whose result type is a scalar (integer, float, bool, char) or a C-like enum loads the value of the pointee type from the pointer.

_Source: `src/compiler/mlir_gen_expr.cpp#L1811-L1819`_


### `expr.deref.tagged-enum-identity` — *p on a tagged enum yields the storage pointer

A tagged enum is pointer-to-inline-storage, so `*p` over a `&Enum`/`*Enum` to a tagged enum yields the same pointer (no load); only C-like enums load.

_Source: `src/compiler/mlir_gen_expr.cpp#L1811-L1816`_


### `expr.deref.user-deref-impl` — Dereference routes through a Deref impl

`*x` for any type implementing Deref (Box/Rc/Arc/user, including generic impls) lowers to `*(x.deref())` via the generic-aware method machinery.

_Source: `src/compiler/sema_expr.cpp#L2658-L2668`_


## Address-of (addr-of)

### `expr.addr-of.index-place` — &f[i] over user Index is a place reference

`&f[i]` over a value implementing the Index trait yields the place reference returned by `index()` directly (no intermediate deref or temporary).

_Source: `src/compiler/sema_expr.cpp#L2554-L2558`_


### `expr.addr-of.mut-array-whole` — &mut arr references the whole array

`&mut arr` for `arr: [T; N]` produces `&mut [T; N]` (a reference to the whole array); coercion to a `&mut [T]` slice parameter occurs separately at the call site.

_Source: `src/compiler/sema_expr.cpp#L1107-L1116`_


### `expr.addr-of.mut-deref-reborrow` — &mut *p reborrows through a pointer/reference

`&mut *p` where p is a Ptr/MutRef/Ref preserves an explicit AddrOfTemp(Deref(p)) shape so it is treated as a reborrow (distinct from a rebind), yielding `&mut Pointee`; for a struct with a DerefMut impl it lowers to `p.deref_mut()`.

_Source: `src/compiler/sema_expr.cpp#L1118-L1139`_


### `expr.addr-of.range-index-identity` — &a[range] is the slice value itself

`&e` where `e` is a range-index `a[i..j]` that already has slice kind `&[T]` yields that slice value unchanged (no additional reference wrapper), since the slice kind is already the borrowed fat form.

_Source: `src/compiler/sema_expr.cpp#L2562-L2569`_


### `expr.addr-of.static` — Address-of a module static is the global's stable address

`&S` where S is an unshadowed module static of non-array type yields the global's own address (a `'static` reference), preserving address identity; the reference is `&mut` iff S is a `mut` static. (No fresh stack copy is materialized.)

_Source: `src/compiler/sema_expr.cpp#L2498-L2509`_


### `expr.addr-of.static-mut` — &mut on a module static yields the global address

`&mut STATIC` for an unshadowed module static (that is not an array) produces a `&mut T` to the global's address rather than materializing a temporary.

_Source: `src/compiler/sema_expr.cpp#L1100-L1106`_


### `expr.addr-of.temp-materialize` — &<rvalue> spills to a stack temporary

`&e` for a non-place expression `e` materializes `e` into a stack temporary and yields `&typeof(e)`. If `typeof(e)` is an error type the expression is an error.

_Source: `src/compiler/sema_expr.cpp#L2559-L2588`_


## Address-of (addrof)

### `expr.addrof.enum-single-level` — & over an enum is one level of indirection

A tagged enum is represented as a pointer to its inline {discriminant,payload} storage; `&enum` therefore yields that storage address directly (one indirection level, like `&struct`), not a pointer-to-pointer. A C-like (scalar-discriminant) enum is spilled to a slot whose address is the reference.

_Source: `src/compiler/mlir_gen_expr.cpp#L1442-L1447`, `src/compiler/mlir_gen_expr.cpp#L1724-L1741`_


### `expr.addrof.module-const-temp` — &CONST materializes a temporary slot

Taking the address of a module-level const that has no local storage materializes a fresh stack slot, stores the const value, and yields that slot's address as the reference.

_Source: `src/compiler/mlir_gen_expr.cpp#L1403-L1415`_


### `expr.addrof.mut-place-element-address` — &mut over an index/field/tuple place yields the real element address

`&[mut] <place>` over a place expression (`a[i]`, `(*p).0`, `s.f`, nested mixes) yields the actual element/field address computed with the correct per-element stride — never the address of a by-value copy — so writes through the resulting reference reach the original aggregate.

_Source: `src/compiler/mlir_gen_expr.cpp#L1456-L1477`, `src/compiler/mlir_gen_expr.cpp#L1512-L1559`, `src/compiler/mlir_gen_expr.cpp#L1560-L1653`_


### `expr.addrof.reborrow-pointer-identity` — &[mut] *r is identity on r

Reborrowing `&[mut] *r` where r holds a reference or raw pointer (`&T`/`&mut T`/`*T`) is equivalent to the pointer value r itself (no extra indirection). A fat `&mut T` reborrowed to a thin reference (`&T`) is peeled to its data half; reborrowed to another fat `&mut T` it keeps the full pair.

_Source: `src/compiler/mlir_gen_expr.cpp#L1479-L1510`_


### `expr.addrof.ref-param-rebind` — &p on a reference parameter rebinds to a single shared slot

When `&p` (or `&mut p`) is taken on a parameter of reference type, the parameter's value is spilled once to a slot and the binding is rebound to that slot, so subsequent reads and further `&p` operations share one storage location (write-through for `&mut` chains).

_Source: `src/compiler/mlir_gen_expr.cpp#L1428-L1441`_


### `expr.addrof.temp-aggregate-spill` — & over a by-value aggregate temporary extends its lifetime via a slot

`&<temp>` where the operand is a by-value aggregate (struct, tuple, array, slice, trait-object, enum produced by a call) spills the temporary once to a stack slot, and that slot is the reference (temporary lifetime extension). Aggregates already held by pointer are returned unchanged.

_Source: `src/compiler/mlir_gen_expr.cpp#L1704-L1746`_


### `expr.addrof.var-place-identity` — &x yields the address of x's own storage

`&x` / `&mut x` over a local or parameter denotes the address of that binding's storage slot. A by-value binding (scalar, by-value-fat, or pointer-family) is first spilled to its own stack slot whose address is the reference; a slot-backed binding (aggregate, address-holding) hands back its existing slot address directly.

_Source: `src/compiler/mlir_gen_expr.cpp#L1398-L1447`_


## Raw-pointer expressions

### `expr.raw-ptr.arith-unsafe` — Raw-pointer arithmetic methods require unsafe

On a raw-pointer (`Ptr`) receiver, the offset methods {add, sub, byte_add, byte_sub} and the distance methods {offset_from, byte_offset_from} are built-in, each require an `unsafe` context, take exactly one argument, and have result type: offset → the receiver pointer type (argument coerced to `i64`); distance → `i64` (argument must be a pointer).

_Source: `src/compiler/sema_expr.cpp#L6615-L6660`_


### `expr.raw-ptr.is-null-safe` — Raw-pointer is_null is safe; user impl wins

On a raw-pointer receiver, `p.is_null()` is safe (no dereference), takes zero arguments, and lowers to `(p as i64) == 0 : bool` — UNLESS the pointee type declares an inherent `is_null` method, in which case that user-defined method is dispatched instead.

**Divergence.** Logos lets an inherent fn on the pointee shadow the built-in pointer is_null.

_Source: `src/compiler/sema_expr.cpp#L6661-L6692`_


## Method receivers

### `expr.receiver.ref-autoderef-to-struct` — Reference/pointer receiver auto-derefs to the struct

A method/field receiver of type `&S`, `&mut S`, `*const S`, or `*mut S` (pointee a struct or zoned-struct) resolves to the address of the pointed-to struct: one level of reference/pointer is stripped to obtain the struct object for field/method access.

_Source: `src/compiler/mlir_gen.cpp#L716-L772`, `src/compiler/mlir_gen.cpp#L844-L859`, `src/compiler/mlir_gen.cpp#L869-L879`_


## Function calls

### `expr.call.arg-coercions` — Implicit coercions applied per argument at a call

Each value argument is, in order, retyped if a bare payload-less enum literal, coerced closure→fn-ptr, array-ref↔slice coerced, implicitly mut-reborrowed, struct-unsize coerced (e.g. `Rc<A>`→`Rc<dyn Tr>`), and integer-widened toward the (substituted) parameter type before type checking.

**Related.** `coerce.unsize.struct-smart-ptr`

_Source: `src/compiler/sema_expr.cpp#L4267-L4275`_


### `expr.call.arg-count` — Call argument count must match

A non-variadic call must supply exactly as many value arguments as the function has parameters; a variadic call must supply at least the fixed parameter count. Otherwise it is an error.

_Source: `src/compiler/sema_expr.cpp#L4235-L4237`, `src/compiler/sema_expr.cpp#L4262-L4265`_


### `expr.call.arg-formal-hint-propagation` — Formal parameter types hint argument inference

When a free-function call's callee is uniquely resolvable (a generic entry, or exactly one candidate), each argument is lowered with the corresponding formal parameter type as an inference hint: a closure-literal arg adopts the formal's Fn-family signature (TypeVar formal: from its Fn-family bound; FnPtr/Closure formal: used directly), a payload-carrying enum-literal arg adopts a fully-concrete enum formal, a tuple-literal arg adopts a Tuple formal, and an array-literal arg adopts the element type of a Slice/Array formal with non-TypeVar element. Hints from generic (unresolved) formals are NOT applied.

**Uncertainty.** Hint applicability conditions inferred from the per-kind lambdas; exact resolution precedence (generic vs single-candidate) is implementation-derived.

_Source: `src/compiler/sema_expr.cpp#L3026-L3113`_


### `expr.call.arg-type-compatible` — Argument type must be compatible with parameter type

After argument coercions, each argument's type must be compatible with the (substituted) corresponding parameter type, or satisfy a `&T`->`dyn` reference match; an incompatible argument yields an "expected X, got Y" error. Parameters whose type is Error, TypeVar, or AssocType (and Error-typed arguments) are exempt. For non-Error, non-TypeVar, non-AssocType parameters, variance is additionally checked.

_Source: `src/compiler/sema_expr.cpp#L3247-L3256`, `src/compiler/sema_expr.cpp#L3503-L3513`, `src/compiler/sema_expr.cpp#L4276-L4287`_


### `expr.call.arg-variance-check` — Argument passing enforces variance

Each argument/parameter pair is variance-checked at the call site (lifetime/subtyping soundness).

_Source: `src/compiler/sema_expr.cpp#L3234`, `src/compiler/sema_expr.cpp#L3257`, `src/compiler/sema_expr.cpp#L3514`_


### `expr.call.arity-exact` — Non-vararg call arity must match

For a non-vararg function, the argument count must equal the declared parameter count; otherwise an error 'expected N args, got M'.

_Source: `src/compiler/sema_expr.cpp#L3242-L3244`, `src/compiler/sema_expr.cpp#L3499-L3501`_


### `expr.call.arity-vararg-minimum` — Vararg call requires at least the fixed-parameter count

For a vararg function, the argument count must be >= the number of declared (fixed) parameters; fewer is an error 'expected at least N args, got M'. Only the fixed parameters are type-checked against formals.

_Source: `src/compiler/sema_expr.cpp#L3219-L3241`, `src/compiler/sema_expr.cpp#L3475-L3498`_


### `expr.call.callable-arg-move` — By-value move-type arguments to a callable are moved

In a closure/fn-ptr call `f(args)`, a by-value concrete move-type argument transfers ownership into the callee (source marked moved). By-reference parameters and TypeVar-typed arguments are excluded.

_Source: `src/compiler/sema_expr.cpp#L2979-L2997`_


### `expr.call.callable-arity-and-args` — Closure/fn-ptr call arity and argument typing

A closure/fn-ptr call requires argument count to equal the callable's parameter count; each argument is coerced to its parameter type and must be type-compatible; the result type is the callable's return type, or `()` (void) if absent.

_Source: `src/compiler/sema_expr.cpp#L2928-L2978`, `src/compiler/sema_expr.cpp#L2998-L3000`_


### `expr.call.callable-autoderef-ref` — Call auto-derefs a reference to a callable

`x(args)` where `x: &fn(..)` / `&mut fn(..)` / `&F` (reference to a callable or Fn-bounded param) auto-dereferences the reference to load the inner fn-pointer/closure before calling.

_Source: `src/compiler/sema_expr.cpp#L2864-L2880`, `src/compiler/sema_expr.cpp#L2892-L2904`_


### `expr.call.callable-field` — Call of a callable struct field

If `s.m(args)` finds no method `m` but struct `s` has a field named `m` whose type is a fn-pointer/fn-value or closure, the expression is lowered as a field read followed by a fn-ptr call (fn-value kind) or closure call (closure kind), returning that callable's return type.

**Divergence.** Rust requires explicit `(s.m)(args)` to call a callable field; bare `s.m(args)` is method-only

_Source: `src/compiler/sema_expr.cpp#L8701-L8728`_


### `expr.call.callable-resolution` — Callee resolution to closure or fn-pointer

A call `x(args)` treats `x` as callable when its type is a Closure or fn-value kind; `Box<dyn Fn*>` (Box<Closure>) is unwrapped to its inner Closure, and an Fn-bounded generic type-param `F` is treated as a closure with the bound's `fn_params`/`fn_ret` signature.

**Divergence.** A10: dyn Fn* collapses to the bare Closure type.

_Source: `src/compiler/sema_expr.cpp#L2845-L2926`_


### `expr.call.closure-hint-from-fn-bound` — Closure param/return types inferred from callee Fn-family bound

For a generic free fn `fn f<F>(g: F) where F: FnOnce(A)->R`, an un-annotated closure argument infers its parameter and return types from the bound's Fn-family signature `(A)->R` (missing return → unit).

_Source: `src/compiler/sema_expr.cpp#L3031-L3051`_


### `expr.call.divergent-never-return` — A call to a `-> !` function (or panic) is divergent

A call/macro-call node is divergent if its callee is `panic` or if any resolved candidate's return type is Never; marker-macros (unreachable!/todo!/unimplemented!) divert through panic!.

_Source: `src/compiler/sema.cpp#L1702-L1722`_


### `expr.call.divergent-never-returning` — Calls to `-> !` functions diverge

A direct or macro call to a Never-returning function (`fn foo() -> !`, including panic/abort/exit) is a divergent syntactic position; this generalises away special-casing of `panic` once the Never type exists.

_Source: `src/compiler/sema_impl.hpp#L3791-L3796`_


### `expr.call.intlit-fit-aggregate` — Integer-literal elements of array/tuple args must fit narrowed element types

When an array-literal or tuple-literal argument is checked against an Array/Tuple formal, each untyped integer-literal element (recursively through nested arrays/tuples) must fit the corresponding narrowed element type; overflow is an error naming the element index.

_Source: `src/compiler/sema_expr.cpp#L3263-L3322`, `src/compiler/sema_expr.cpp#L3520-L3579`_


### `expr.call.intlit-fit-scalar` — Integer-literal argument must fit the formal's integer type

An untyped integer-literal argument coerced to an integer parameter type is an error if its value does not fit that type's range ('value V does not fit in T').

_Source: `src/compiler/sema_expr.cpp#L3235-L3239`, `src/compiler/sema_expr.cpp#L3515-L3519`_


### `expr.call.intlit-fits` — Integer-literal argument must fit the parameter type

An integer-literal argument (including literal elements nested in array- and tuple-literal arguments, recursively) must fit within the target integer type; a value out of range is an error.

_Source: `src/compiler/sema_expr.cpp#L4288-L4293`, `src/compiler/sema_expr.cpp#L4294-L4353`_


### `expr.call.macro-overloads-not-callable-as-fn` — fn_macro/token_macro overloads are not callable via plain call syntax

A `#[fn_macro]` or `#[token_macro]` overload of a name is invocable only via `name!(...)` syntax; plain `name(...)` call resolution excludes such overloads.

_Source: `src/compiler/sema_expr.cpp#L3336-L3344`_


### `expr.call.move-by-value-args` — By-value move-type arguments are marked moved

By-value arguments of move (non-Copy) type at a call are marked moved so their scope-exit Drop does not fire on storage whose ownership transferred to the callee.

**Related.** `borrow.move.by-value-call`

_Source: `src/compiler/sema_expr.cpp#L4358-L4363`_


### `expr.call.overload-best-match-scoring` — Overload resolution scores exact(2) over compatible(1); ties broken by local package

Among arity-matching non-generic candidates, each is scored by its worst param match: exact (types_equal) = 2, compatible-only = 1; if any param is incompatible the candidate is rejected. The unique highest-scoring candidate wins. A score tie is ambiguous, broken by preferring the candidate whose package equals the current package (local shadows imported); an unbroken tie is an 'ambiguous call' error.

_Source: `src/compiler/sema.cpp#L1724-L1793`_


### `expr.call.prelude-enum-shorthand` — Some/Ok/Err call shorthand constructs enum literals

When `Some`, `Ok`, or `Err` is not resolvable as a function, the call is treated as the corresponding `Option::Some` / `Result::Ok` / `Result::Err` enum-variant literal (honoring any enum type hint for parameter substitution). `None` is not handled here (it is a bare-ident path).

_Source: `src/compiler/sema_expr.cpp#L3381-L3403`_


### `expr.call.pub-access-check` — Free-function call respects visibility

A free-function call checks the callee's pub/package/module-only visibility against the call site; an inaccessible callee is an error.

_Source: `src/compiler/sema_expr.cpp#L3216`, `src/compiler/sema_expr.cpp#L3406-L3411`_


### `expr.call.static-turbofish-before-method` — Static-call turbofish precedes method name

In an associated/static call, turbofish type arguments attach to the receiver type and precede the `::method` segment: `Recv::<T>::method(args)`.

**Divergence.** Rust places the turbofish after the method for trait/inherent fns (e.g. T::method::<U>); Logos surface form puts it before the method name on the type path.

_Source: `src/compiler/sema_render.cpp#L203-L241`_


### `expr.call.tuple-struct-ctor` — Tuple-struct constructor call

`Foo(a0, .., a_{n-1})` where Foo is a tuple struct constructs a struct literal with positional fields named "0".."n-1"; argument count must equal the field count; for a generic tuple struct the struct type-args are inferred by unifying each argument type against the declared field type.

_Source: `src/compiler/sema_expr.cpp#L2783-L2842`_


### `expr.call.turbofish-free-fn` — Free-function turbofish placement

Explicit type arguments to a free-function call use turbofish after the callee name and before the argument list: `callee::<T1, T2>(args)`.

_Source: `src/compiler/sema_render.cpp#L172-L201`_


### `expr.call.undefined-function-error` — Call to an undefined function is an error

A call whose callee resolves to no function (and is not a prelude enum shorthand) is an error 'call to undefined function', except in metaprog mode where it is permitted to pass through with error type.

_Source: `src/compiler/sema_expr.cpp#L3377-L3404`_


### `expr.call.unsafe-context` — Calling an unsafe function requires unsafe context

A call to a function marked `unsafe` is an error unless it occurs inside an unsafe context; this applies to both inferred and explicit-turbofish call paths.

_Source: `src/compiler/sema_expr.cpp#L3995-L3997`_


### `expr.call.unsafe-context-required` — Calling an unsafe fn requires an unsafe context

A call to a function declared `unsafe` is an error unless it occurs inside an unsafe context.

_Source: `src/compiler/sema_expr.cpp#L3217-L3218`, `src/compiler/sema_expr.cpp#L3409-L3410`_


## Static / UFCS path calls

### `expr.static-call.arg-count-and-type-check` — Static call arity and per-argument type checking

A non-generic static call checks argument count against the parameter list (error on mismatch) and coerces then type-checks each argument against its parameter (error on incompatibility). By-value move-typed args (and owning Box<dyn>) are marked moved so scope-end drops do not fire on transferred locals.

_Source: `src/compiler/sema_expr.cpp#L13621-L13643`_


### `expr.static-call.array-default` — `<[E; N]>::default()` synthesizes elementwise default

`default()` with no args on an array type (named via a non-generic alias `type M = [E; N]`) synthesizes `[E::default(); N]`; if the element type has no Default impl it is an error. Arrays carry no `__default` symbol.

```logos
type M = [i32; 4]; let a = M::default();
```

_Source: `src/compiler/sema_expr.cpp#L13183-L13196`_


### `expr.static-call.enum-variant-vs-static-method` — `Enum::Name(...)` constructs a variant only when Name is a variant

When the class is an enum (directly or via a non-generic type-alias to an enum), `Enum::Name(args)` lowers as a variant construction iff Name matches a declared variant; otherwise it falls through to ordinary static-method resolution (trait-impl-on-enum).

_Source: `src/compiler/sema_expr.cpp#L13113-L13146`_


### `expr.static-call.generic-method-infers-type-args` — Generic static method infers concrete type-args outside generic context

A generic static method (type-params from the enclosing impl) called outside a generic context (no TypeVar/AssocType in value or explicit type-args) is resolved by turbofish args if present, else by argument inference, then routed through the generic-call finisher to trigger the concrete instantiation. Inside a generic body, it is emitted with TypeVar type-args (or turbofish) and the return type substituted, for mono to rename to the concrete struct method.

_Source: `src/compiler/sema_expr.cpp#L13538-L13618`_


### `expr.static-call.qualified-path-drops-package-prefix` — `pkg.path.Type::method()` resolves on the last segment as the type

In a qualified static call `pkg.path.Type::member(args)`, the LAST dotted segment names the type/class; the package prefix is dropped (type/method resolution and arg lowering are not package-filtered, only free-fn lookups are).

_Source: `src/compiler/sema_expr.cpp#L13087-L13094`_


### `expr.static-call.self-resolves-to-impl-type` — `Self::method()` resolves Self to the impl's concrete type

Inside an impl body, `Self::method()` resolves `Self` to the impl's concrete type name (struct/zoned-struct via concrete name, enum via enum name) before static-method resolution, equivalent to writing the type name.

_Source: `src/compiler/sema_expr.cpp#L13099-L13111`_


### `expr.static-call.trait-qualified-ufcs` — Trait-qualified UFCS `Trait::method(recv, ...)`

When the class names a TRAIT (not a struct/enum/datatype/type-param) and args are non-empty, `Trait::method(recv, ...)` dispatches on the first argument's concrete receiver type (auto-derefed through refs/ptrs): struct/zoned-struct by name, enum by name, or primitive by type_str. The rewrite to `<recv-type>__<method>` commits only if that concrete symbol actually resolves; otherwise normal resolution and error reporting proceed.

**Divergence.** Rust-conformant (DIVERGENCES.md: trait-qualified UFCS supported)

_Source: `src/compiler/sema_expr.cpp#L13198-L13248`_


### `expr.static-call.turbofish-concrete-partial-spec` — Turbofish on a partial-spec static call builds the concrete mangled name

For `Type::<A, B>::method(...)` where a concrete partial-spec impl registers methods under the concrete mangled name, if base lookup misses and all turbofish args are concrete (non-TypeVar), the concrete instantiation name (datatype vs struct) is built and the symbol re-resolved.

_Source: `src/compiler/sema_expr.cpp#L13296-L13328`_


### `expr.static-call.type-alias-resolution` — Static calls resolve non-generic type aliases to the target type

A non-generic type alias used as a static-call class resolves to its target struct/zoned-struct (using the concrete name when type-args are present) before mangling the method symbol.

_Source: `src/compiler/sema_expr.cpp#L13149-L13164`_


### `expr.static-call.type-param-shadows-struct` — In-scope abstract type-param shadows a same-name concrete type

A bounded type-param used as the static-call class (`S::method` with `S: Bound`) dispatches through the trait bound and NOT through a same-name concrete struct in scope; an active abstract type-param (resolves to a TypeVar) suppresses concrete-symbol lookup so resolution falls to generic-static dispatch.

_Source: `src/compiler/sema_expr.cpp#L13250-L13263`, `src/compiler/sema_expr.cpp#L13268-L13269`_


### `expr.static-call.unsafe-requires-unsafe-context` — Calling an unsafe static method requires an unsafe context

A call to an unsafe static method outside an unsafe context is an error.

_Source: `src/compiler/sema_expr.cpp#L13532-L13533`, `src/compiler/sema_expr.cpp#L13409-L13410`_


## Method calls (method)

### `expr.method.arg-type-compat` — Method argument type compatibility

After coercion, each method argument type must be compatible with its substituted parameter type; an incompatibility is an error.

_Source: `src/compiler/sema_expr.cpp#L8888-L8896`_


### `expr.method.arity-check` — Method call argument count must match

A method call must supply exactly `param_count - 1` explicit arguments (excluding the implicit `self` receiver); for a zero-parameter signature the expected count is 0. A mismatch between supplied and expected explicit argument counts is an error ('expected N args, got M').

_Source: `src/compiler/sema_expr.cpp#L7492-L7497`, `src/compiler/sema_expr.cpp#L8867-L8871`_


### `expr.method.array-len-builtin` — len() on a fixed array is a compile-time constant

`a.len()` where `a` has fixed-array type `[T; N]` evaluates to the compile-time size `N` as an `i64` literal; no runtime call is emitted.

**Divergence.** Result type is i64 (Logos stdlib uses i64 for lengths), not usize as in Rust.

_Source: `src/compiler/sema_expr.cpp#L7280-L7284`_


### `expr.method.auto-ref-receiver` — Primitive/value receiver is auto-referenced for &self

When the method's `self` is `&self`/`&mut self` but the receiver is a by-value primitive (i8/i16/i32/i64, u8/u16/u32/u64, f32/f64, bool, char), the receiver value is materialized into storage and a pointer to it is passed as the self argument.

_Source: `src/compiler/mlir_gen_expr.cpp#L2565-L2599`, `src/compiler/mlir_gen_expr.cpp#L2581-L2588`_


### `expr.method.auto-ref-self` — Auto-reference/auto-address receiver for &Self / &mut Self / *Self methods

If the resolved method's first formal parameter is `&Self`/`&mut Self` (or `*const Self`/`*mut Self`) and the receiver is a non-reference, non-pointer value, the receiver is automatically taken by reference (resp. raw address-of) with the matching mutability before the call.

**Related.** `expr.method.autoref-ladder`

_Source: `src/compiler/sema_expr.cpp#L8283-L8294`, `src/compiler/sema_expr.cpp#L8303-L8324`, `src/compiler/sema_expr.cpp#L8581-L8589`_


### `expr.method.autoderef-lowest-priority` — By-value-self via auto-deref is lowest dispatch priority

A method whose `self` is by value, reachable only by auto-dereferencing a `&T`/`&mut T`/`*T` receiver, is selected only if no exact or auto-ref candidate at the current deref level matches. When chosen, the receiver is auto-dereferenced (copying/moving the pointee out, subject to downstream Copy/move borrow checks).

**Divergence.** Mirrors Rust autoderef order: try T/&T/&mut T at a deref level before stepping deeper.

**Related.** `expr.method.autoref-ladder`

_Source: `src/compiler/sema_expr.cpp#L8484-L8491`, `src/compiler/sema_expr.cpp#L8524-L8557`, `src/compiler/sema_expr.cpp#L8563-L8580`_


### `expr.method.autoref-ladder` — Method receiver auto-ref ladder

When resolving `r.m(args)`, candidate receiver types are tried in order: the receiver type T as-is, then `&T`, then `&mut T` (and for primitive/raw receivers also `*const T`, `*mut T`). The first signature-matching method wins; if matched against an autoref'd variant, the receiver is wrapped with the corresponding `&`/`&mut` address-of before the call.

**Related.** `expr.method.autoderef-lowest-priority`, `expr.method.auto-ref-self`

_Source: `src/compiler/sema_expr.cpp#L8137-L8154`, `src/compiler/sema_expr.cpp#L8386-L8420`, `src/compiler/sema_expr.cpp#L8503-L8520`_


### `expr.method.blanket-on-primitive` — Value blanket impls dispatch on primitive receivers

A value blanket impl (`impl<T> Trait for T`) is reachable on a primitive receiver (enabling From→Into, TryFrom→TryInto, identity Borrow, etc.) before the not-a-struct error is reported.

_Source: `src/compiler/sema_expr.cpp#L8330-L8335`_


### `expr.method.deref-autoderef-resolution` — Method resolution autoderefs through Deref/DerefMut

If the receiver is a struct with no direct method named `m` (no candidate keyed by concrete or base struct name), and the struct implements `Deref<Target>`, the receiver is dereferenced to `Target` and resolution retries; iterated up to a fixed bound (16). A method defined on the outer type always wins over a Deref-target method.

_Source: `src/compiler/sema_expr.cpp#L7203-L7238`_


### `expr.method.deref-step-prefers-mut` — Per-step DerefMut chosen when target method needs &mut self

At each autoderef step, if the Deref target has a candidate method `m` whose first parameter is `&mut Self` and the receiver type implements DerefMut, the mutable DerefMut step is taken so the resulting receiver is a mutable place (`&mut Target`) rather than the shared `&Target` an immutable Deref would yield. Falls back to Deref when no DerefMut impl exists.

_Source: `src/compiler/sema_expr.cpp#L7170-L7202`, `src/compiler/sema_expr.cpp#L7234-L7237`_


### `expr.method.dyn-vtable-dispatch` — Method call on a trait-object receiver dispatches via vtable

A method call `recv.m(..)` where `recv: dyn Trait` (or `&dyn Trait`/`&mut dyn Trait`, i.e. a reference whose pointee is a trait object) and the method has a vtable slot is dispatched dynamically through the receiver's vtable at that slot; references to a trait object load the dyn handle once before dispatch.

_Source: `src/compiler/mlir_gen_expr.cpp#L2554-L2559`_


### `expr.method.generic-struct-base-fallback` — Generic-struct methods resolvable under the base type name

For a receiver of a monomorphized generic struct type (e.g. `Foo$G1$i32`), if no method is found under the concrete name, methods registered under the base struct name (`Foo`) are tried, with the struct's type parameters substituted from the receiver's type arguments.

_Source: `src/compiler/sema_expr.cpp#L8460-L8478`, `src/compiler/sema_expr.cpp#L8591-L8651`_


### `expr.method.intlit-fits` — Integer-literal argument range check

An integer-literal argument (including elements of array/tuple literals, recursively) must fit in the target integer parameter type; an out-of-range literal is an error.

_Source: `src/compiler/sema_expr.cpp#L8897-L8961`_


### `expr.method.mut-ref-to-shared-demotion` — &mut T receiver may call a &self method

A `&mut T` receiver may dispatch to a method declared on `&T` (shared self): for resolution the `&mut T` is coerced to `&T` (same pointee, weaker mutability); the receiver value is reused unchanged since `&mut`/`&` share ABI.

_Source: `src/compiler/sema_expr.cpp#L8231-L8245`_


### `expr.method.no-method-error` — No method on receiver type

If no method, blanket-impl, multi-trait collision, or callable field matches `s.m`, the call is an error "'S' has no method 'm'".

_Source: `src/compiler/sema_expr.cpp#L8729-L8730`_


### `expr.method.not-a-struct-error` — Method on non-struct receiver with no resolution is an error

If no method resolves for a primitive/non-struct receiver, it is a compile error 'receiver is not a struct'. Exception: in metaprog mode, an `<error>`-typed receiver (or `&`/`*` to an `<error>` pointee) silently propagates `<error>` without diagnostic.

_Source: `src/compiler/sema_expr.cpp#L8336-L8349`_


### `expr.method.pub-access-check` — Method visibility enforced at call site

A resolved method call is subject to the method's pub/module-only visibility; calling a non-visible method from outside its allowed scope is an error.

_Source: `src/compiler/sema_expr.cpp#L8734`_


### `expr.method.raw-ptr-call-requires-unsafe` — Method call through a raw pointer requires unsafe

Dispatching a method when the receiver type is a raw pointer (`*const`/`*mut`), including `*mut dyn Trait`/`*const dyn Trait`, requires an `unsafe` context; outside `unsafe` it is an error. The raw pointer is peeled to its pointee for dispatch.

_Source: `src/compiler/sema_expr.cpp#L7301-L7312`, `src/compiler/sema_expr.cpp#L7324-L7327`_


### `expr.method.raw-ptr-recv-unsafe` — Method call through raw pointer requires unsafe

Calling a method on a receiver of raw-pointer type requires an `unsafe` context; otherwise it is an error. The raw pointer is auto-dereferenced to its pointee for method resolution.

_Source: `src/compiler/sema_expr.cpp#L8743-L8746`_


### `expr.method.receiver-multiref-autoderef` — Method receiver peels surplus reference layers

For a method call `r.m(...)`, if the receiver type is a (non-raw) reference-like type whose pointee is itself reference-like (`&&T`, `&&mut T`, etc.), the extra reference layers are removed by explicit derefs until a single reference layer remains: `r.m()` for `r:&&T` ≡ `(*r).m()`. Raw pointers (`*const`/`*mut`) are not peeled here.

_Source: `src/compiler/sema_expr.cpp#L7124-L7130`_


### `expr.method.ref-blanket-impl` — Generic reference blanket impl dispatch

An `impl<T> Trait for &T` is reachable from a reference receiver `&U`: T is bound to the pointee U, the receiver is auto-referenced, and the call is monomorphized with T=U.

**Related.** `expr.method.ref-impl-target`

_Source: `src/compiler/sema_expr.cpp#L8156-L8177`_


### `expr.method.ref-impl-target` — Dispatch to impls declared on reference receiver types

An `impl Trait for &T` (or `&mut T`) provides methods reachable by a `&T`/`&mut T` receiver; these are preferred over auto-deref to T. For a struct pointee both the concrete-arg form and the base form are tried; for a non-struct pointee the impl target is keyed by the full receiver type string.

_Source: `src/compiler/sema_expr.cpp#L8156-L8161`, `src/compiler/sema_expr.cpp#L8358-L8379`, `src/compiler/sema_expr.cpp#L8396-L8409`_


### `expr.method.ref-impl-typeparam-subst` — Reference-impl method binds pointee type args

When dispatching through a reference impl on a generic struct (`impl<T> Foo for &Pair<T>`), the impl/struct type parameters are bound from the pointee's type arguments; non-generic returns are substituted, generic methods are monomorphized with the derived args.

**Related.** `expr.method.ref-impl-target`

_Source: `src/compiler/sema_expr.cpp#L8421-L8453`_


### `expr.method.self-is-first-arg` — Receiver passed as method's first argument

A method call lowers to a call whose argument 0 is the receiver (self) and arguments 1..n are the call's explicit arguments; explicit argument i maps to callee parameter i+1.

_Source: `src/compiler/mlir_gen_expr.cpp#L2683-L2702`_


### `expr.method.str-slice-alias` — str method lookup aliases &[u8]

When a receiver's type renders as `&[u8]` (the representation of `str`) and no method is found under that name, methods registered under `str__<method>` are tried as a fallback.

**Uncertainty.** str is modeled as Slice<u8>/&[u8]; alias is a representation detail surfaced as a resolution rule.

_Source: `src/compiler/sema_expr.cpp#L8186-L8195`_


### `expr.method.turbofish-bypasses-inference` — Method-level turbofish supplies explicit type args

A method call may carry an explicit turbofish `recv.m::<T1,T2>(args)`; the supplied type arguments become the method's type parameters and downstream per-arg type-param inference from argument types is bypassed.

_Source: `src/compiler/sema_expr.cpp#L7241-L7265`, `src/compiler/sema_expr.cpp#L7504-L7510`_


### `expr.method.turbofish-method-args` — Method turbofish supplies type args verbatim, else inferred

For a generic method `r.m::<A,..>(args)`, the explicit turbofish type arguments are used verbatim (positionally); missing trailing args are errors/placeholders. With no turbofish, method-level type args are inferred from arguments with seed `Self = typeof(recv)`; failure to infer is a compile error.

_Source: `src/compiler/sema_expr.cpp#L8265-L8282`_


### `expr.method.unsafe-context` — Calling an unsafe method requires an unsafe context

A method-call expression `r.m(..)` whose resolved method is declared `unsafe` is a compile error unless it occurs inside an unsafe context (`unsafe { .. }` block or unsafe fn).

_Source: `src/compiler/sema_expr.cpp#L8259-L8261`_


### `expr.method.unsafe-method-requires-unsafe` — Calling an unsafe trait method requires unsafe

Calling a trait method declared `unsafe` outside an `unsafe` context is an error.

_Source: `src/compiler/sema_expr.cpp#L7487-L7490`_


### `expr.method.unsafe-required` — Unsafe method requires unsafe context

Calling a method marked `unsafe` outside an `unsafe` context is an error.

_Source: `src/compiler/sema_expr.cpp#L8735-L8736`_


### `expr.method.vec-get-move-out-rejected` — Vec::get of a non-Copy element is rejected

`v.get(i)` on a receiver resolving (through one reference layer) to `Vec<E>` where `E` is a non-Copy (move) type is an error: it would move an element out of borrowed Vec storage, aliasing and double-freeing on drop. The fix is `.borrow(i)` for `&E`, or `.remove(..)`/`.pop()` to take ownership. Copy elements are permitted.

_Source: `src/compiler/sema_expr.cpp#L7139-L7160`_


## Method calls (method-call)

### `expr.method-call.turbofish-after-name` — Method-call turbofish placement

A method call is `receiver.method(args)`; explicit type arguments are turbofish placed after the method name: `receiver.method::<T>(args)`.

_Source: `src/compiler/sema_render.cpp#L243-L280`_


## Invocation

### `expr.invoke.arity-and-arg-types` — Closure/fn-ptr call arity and argument typing

A closure or fn-ptr call must supply exactly the parameter count; each argument is coerced to its parameter type and a non-error argument type incompatible with the parameter type is an error; variance is checked per argument.

_Source: `src/compiler/sema_expr.cpp#L6221-L6243`_


### `expr.invoke.callable-receiver` — Expression-as-callee (IIFE) must be callable

`(expr)(args)` invokes the receiver expression: a Closure-typed receiver lowers to a closure call, an fn-value-kind (fn-ptr) receiver to a fn-ptr call, and a TypeVar receiver bounded by an Fn/FnMut/FnOnce family bound synthesizes a closure type from that bound for arity/arg checks (without retyping the receiver). A receiver of any other type is a non-callable error.

_Source: `src/compiler/sema_expr.cpp#L6187-L6294`_


### `expr.invoke.expression-callee` — Expression-as-callee invocation

`(expr)(args)` invokes the value produced by `expr` as a callee, routed through closure-call or fn-ptr-call.

_Source: `tools/peg_gen/grammars/logos.peg#L298`_


## Turbofish type arguments

### `expr.turbofish.generic-ref` — Turbofish generic reference and static call

`IDENT::<T,…>` is a generic reference (explicit type arguments to a function/item). `IDENT::<T,…>::METHOD` is a static call on the type-applied receiver.

_Source: `tools/peg_gen/grammars/logos.peg#L2756-L2760`_


## Struct literals

### `expr.struct-lit.anyval-raw-constructor` — AnyVal struct-literal constructor

`AnyVal { raw: e }` is a valid constructor expression that yields the scalar value of `e`. The literal must contain exactly one field named `raw`; any other field set is rejected.

**Divergence.** Logos built-in; struct-literal syntax over a scalar type

**Related.** `layout.anyval.scalar-i32`

_Source: `src/compiler/mlir_gen.cpp#L885-L903`_


### `expr.struct-lit.duplicate-field-error` — Struct-lit may not initialize a field twice

Initializing the same field more than once in a struct literal is a 'duplicate field' error.

_Source: `src/compiler/sema_expr.cpp#L9900-L9905`, `src/compiler/sema_expr.cpp#L10077-L10082`_


### `expr.struct-lit.dyn-auto-bounds-at-field-init` — Auto-trait bounds checked at dyn field-init coercion

When a field value is coerced to a field type that is a dyn-trait with auto-trait bounds (e.g. `&dyn Trait + Send`), the value's type must satisfy those auto-trait bounds.

_Source: `src/compiler/sema_expr.cpp#L10098-L10101`_


### `expr.struct-lit.explicit-type-args-seed-inference` — Explicit type args seed struct-lit inference

In a struct literal `S::<A1,...,Ak> { ... }` for generic `S`, supplied type args are bound positionally to S's type-params (up to the number of params) and used to seed the inferred-arg map; each supplied arg is resolved and ignored if it resolves to an error type.

_Source: `src/compiler/sema_expr.cpp#L9696-L9713`_


### `expr.struct-lit.field-init` — Struct field initializers and shorthand

A struct field initializer is `name: expr` or the shorthand `name` (FIELD_SHORTHAND, binding the in-scope variable of that name). Tuple-struct fields may be initialized by their numeric name `S { 0: a, 1: b }` since fields of `struct S(T0,T1)` are named "0"/"1".

_Source: `tools/peg_gen/grammars/logos.peg#L2843-L2861`, `tools/peg_gen/grammars/logos.peg#L2851-L2855`_


### `expr.struct-lit.field-init-and-shorthand` — Struct literal field forms

A struct literal is `Name { f: v, ... }`; fields are either `name: value` (FIELD_INIT) or shorthand `name` (FIELD_SHORTHAND). The name may carry turbofish type args `Name::<T> { ... }`.

_Source: `src/compiler/sema_render.cpp#L346-L385`_


### `expr.struct-lit.field-type-mismatch-error` — Struct-lit field value must be compatible with declared field type

Each initialized field's value type must be compatible with the field's declared type (after substituting struct type-params into the declared type), else a 'expected X, got Y' error; comparison is deferred to mono when the substituted field type still contains a TypeVar/ConstVar/CfgSlotType/AssocType. A closure value coercible to the declared fn-ptr type is accepted.

_Source: `src/compiler/sema_expr.cpp#L9906-L9953`, `src/compiler/sema_expr.cpp#L9916-L9921`, `src/compiler/sema_expr.cpp#L9926-L9944`_


### `expr.struct-lit.field-value-moved` — Move-typed field values are consumed by the literal

When constructing a struct literal, each field value whose type is a move type is marked moved (consumed) in the surrounding scope, preventing later use and double-drop.

_Source: `src/compiler/sema_expr.cpp#L10023-L10033`, `src/compiler/sema_expr.cpp#L10223-L10227`_


### `expr.struct-lit.field-variance-check` — Variance check at struct-lit field initialization

Each field initialization is variance-checked between the value type and the declared field type in permissive mode (the struct's lifetime args are bound at the construction site, so elided source regions are filled by the caller's region inference); the check is skipped when the field type still contains a type-param.

_Source: `src/compiler/sema_expr.cpp#L9954-L9961`, `src/compiler/sema_expr.cpp#L10092-L10097`_


### `expr.struct-lit.forms` — Struct literal forms

Struct literals: `T { f: e, … }`, generic `T::<A,…> { f: e, … }`, and functional-update `T { f: e, .. base }` / `T { .. base }` / `T { .. base, f: e }`. Explicit fields always override the base regardless of field order.

_Source: `tools/peg_gen/grammars/logos.peg#L2818-L2838`, `tools/peg_gen/grammars/logos.peg#L2823-L2831`_


### `expr.struct-lit.full-explicit-args-select-spec` — Fully-supplied type args select a matching specialization

If all type args of a generic struct are explicitly supplied and a matching (full or partial) specialization exists, the literal's field set and field types are taken from that specialization rather than the primary template.

_Source: `src/compiler/sema_expr.cpp#L9715-L9719`, `src/compiler/sema_expr.cpp#L9766-L9777`_


### `expr.struct-lit.functional-update` — Functional struct update `..base` fills unset fields

A struct literal may end with a functional-update base `S { ..., ..base }`. The `base` expression must have struct type S (same struct name); every field not explicitly initialized is read from base via field-read, with the struct's type-params substituted into each carried field's declared type (generic path). A base of differing struct type is an error.

_Source: `src/compiler/sema_expr.cpp#L9970-L10013`, `src/compiler/sema_expr.cpp#L10171-L10213`, `src/compiler/sema_render.cpp#L386-L391`_


### `expr.struct-lit.infer-nested-typevar` — Recursive inference of nested struct type-params

A struct type-param appearing nested inside a compound field type (generic struct/enum type-args, array/pointer element, tuple element, or fn-ptr/closure parameter and return types) is inferred by parallel structural walk of the declared field type and the field value type; only the struct's own as-yet-uninferred type-params are bound, and binding to an Error/IntLit/FloatLit value type is skipped.

_Source: `src/compiler/sema_expr.cpp#L9730-L9764`, `src/compiler/sema_expr.cpp#L9819-L9823`_


### `expr.struct-lit.infer-typevar-from-array-field` — Infer T from `[T; N]` field via element type

For a field declared `[T; N]` with type-param element T, T is inferred from the element type of an array-typed field value; an IntLit element defaults to T's hint (else i32).

_Source: `src/compiler/sema_expr.cpp#L9792-L9805`_


### `expr.struct-lit.infer-typevar-from-field` — Infer struct type-param from a directly-typed field value

A struct type-param `T` used directly as a field's declared type is inferred from that field's value type; an uninferred-T field value of IntLit type defaults to T's hint (else i32), and of FloatLit type defaults to T's hint (else f64).

_Source: `src/compiler/sema_expr.cpp#L9779-L9791`_


### `expr.struct-lit.infer-typevar-from-ptr-field` — Infer T from `*T`/`&T`/`&mut T` field via pointee

For a field declared as a pointer/reference to type-param T (`*T`, `&T`, `&mut T`), T is inferred from the pointee of a ref-like field value type, provided that pointee is not an error type.

_Source: `src/compiler/sema_expr.cpp#L9806-L9818`_


### `expr.struct-lit.intlit-fits-field` — IntLit field value must fit the declared field type

An integer-literal field value must fit within the declared field type's range; otherwise a 'value V does not fit in T' error. The same fit-check applies element-wise to array-literal, tuple-literal, and nested array/tuple-literal field values against the corresponding narrow element types.

_Source: `src/compiler/sema_expr.cpp#L9962-L9967`, `src/compiler/sema_expr.cpp#L10102-L10168`_


### `expr.struct-lit.missing-field-error` — All non-union struct fields must be initialized

Every field of a non-union struct must be initialized (directly, via variadic expansion, or via `..base`); an uninitialized field is a 'field not initialized' error.

_Source: `src/compiler/sema_expr.cpp#L10015-L10021`, `src/compiler/sema_expr.cpp#L10215-L10221`_


### `expr.struct-lit.outlives-check` — Struct `where 'a: 'b` outlives constraints enforced at literal

A struct literal must satisfy the struct's declared lifetime outlives constraints (`where 'a: 'b`), checked against the literal's lifetime args, the struct's field types, and the supplied field values.

_Source: `src/compiler/sema_expr.cpp#L10035-L10041`, `src/compiler/sema_expr.cpp#L10232-L10238`_


### `expr.struct-lit.uninferred-typevar-fallback-hint` — Fallback type-param resolution from hint then error

Any struct type-param not inferred from fields is resolved from the expected-type hint if available; a param still unresolved after the hint becomes an error type (poisoning the instantiation). The hint struct type also supplies type-args positionally and variadic params consume the hint's trailing type-args.

_Source: `src/compiler/sema_expr.cpp#L9825-L9856`_


### `expr.struct-lit.union-single-field` — Union literals initialize exactly one field; missing-field check skipped

For a union struct, the all-fields-initialized check is suppressed: a union literal initializes only one (active) field by design.

**Divergence.** A6

_Source: `src/compiler/sema_expr.cpp#L10015-L10021`, `src/compiler/sema_expr.cpp#L10215-L10221`_


### `expr.struct-lit.unknown-field-error` — Struct-lit may not name a field absent from the definition

A field name in a struct literal that is neither a field of the effective struct definition nor a variadic-field expansion is an 'unknown field' error.

_Source: `src/compiler/sema_expr.cpp#L9878-L9899`, `src/compiler/sema_expr.cpp#L10049-L10076`_


### `expr.struct-lit.variadic-field-expansion` — Variadic struct field accepts expansion names `name_*`

A variadic struct field named `name` accepts literal field names of the form `name_<suffix>`; each such expansion value is type-checked against the variadic field's type and the variadic field is marked initialized.

**Divergence.** A6

_Source: `src/compiler/sema_expr.cpp#L9882-L9897`, `src/compiler/sema_expr.cpp#L10052-L10074`_


## Constructors

### `expr.ctor.prelude-option-result-shorthand` — Bare Some/Ok/Err prelude variant constructor

If no function named `Some`/`Ok`/`Err` resolves, a bare call `Some(x)`/`Ok(x)`/`Err(x)` constructs the corresponding `Option`/`Result` variant, provided that enum (with that variant) is in scope; a user-defined function of the same name shadows this (function lookup runs first).

_Source: `src/compiler/sema_expr.cpp#L5921-L5942`_


### `expr.ctor.variant-alias-shorthand` — Bare enum-variant constructor via use-alias

A `use Enum.{V, …};` import registers variant aliases; a bare call `V(payload)` whose name is an imported variant alias constructs that enum's variant `V` (typed via enum-literal lowering with payload typing), when no function of that name resolved.

**Divergence.** Logos `use Type.{V}` variant-import surface (pkg `.` / item `::` path model)

_Source: `src/compiler/sema_expr.cpp#L5943-L5953`_


## Enum literals

### `expr.enum-lit.arg-type-compat` — Payload argument type compatibility

Each non-variadic payload argument's type must be compatible with its resolved formal payload type; an incompatibility is ill-formed ("arg i: expected X, got Y").

_Source: `src/compiler/sema_expr.cpp#L12528-L12535`_


### `expr.enum-lit.args-shape` — Enum-literal argument list shape

The payload argument list of an enum literal is accepted either as a direct sequence of argument expressions or as a map containing an ITEMS sequence; both forms denote the same ordered payload list.

_Source: `src/compiler/sema_expr.cpp#L12321-L12348`_


### `expr.enum-lit.arity` — Non-variadic variant arity

For a non-variadic variant, the number of payload arguments must equal the number of declared payload types; otherwise the program is ill-formed ("expects N args, got M").

_Source: `src/compiler/sema_expr.cpp#L12524-L12527`_


### `expr.enum-lit.dyn-payload-arg` — Concrete payload into a dyn-typed enum slot widens the type arg

When the hint pins a type parameter to a trait-object-wrapping type (e.g. `Box<dyn Tr>`) but the payload argument is a concrete coercible value (e.g. `Box<Sq>`), the constructed enum's type argument records the dyn type while the payload expression stays concrete; the store later unsize-fattens it into the dyn slot.

_Source: `src/compiler/sema_expr.cpp#L12097-L12119`_


### `expr.enum-lit.forms` — Enum variant literal forms

Enum variants are written `E::V` (unit), `E::V(args)` (tuple payload), `E::V { f: e, … }` (struct-shape payload), with optional turbofish `E::V::<T,…>`. The qualified-as form `<T as Trait>::V` and dotted-package-prefix form `pkg.path.E::V` are also accepted. Struct-shape variant fields are resolved by name to positional indices.

_Source: `tools/peg_gen/grammars/logos.peg#L2787-L2816`_


### `expr.enum-lit.intlit-fit` — Integer-literal payload range check

An integer-literal payload argument whose constant value does not fit in the target integer type's range is ill-formed; this check recurses into array-literal elements and tuple-literal elements (and their nested array/tuple sub-elements) of the payload type.

_Source: `src/compiler/sema_expr.cpp#L12536-L12542`, `src/compiler/sema_expr.cpp#L12543-L12608`_


### `expr.enum-lit.intlit-payload-fits` — Integer-literal payload must fit the declared payload type

An integer-literal payload argument (directly, or as an element of an array/tuple payload, recursively) must fit within the declared narrow integer payload type; an out-of-range value is an error.

_Source: `src/compiler/sema_expr.cpp#L12180-L12251`_


### `expr.enum-lit.nested-hint-projection` — Per-payload type hint via outer-hint projection

When the surrounding expected type is `E<A1..An>` for the same enum `E`, each payload slot whose formal type is a TypeVar receives a per-argument expected-type hint computed by substituting `E`'s type parameters with the outer hint's type-args; this lets a nested enum literal (e.g. inner `Result::Ok` inside `Option::Some(Result::Ok(42))`) lower with its own concrete enum hint.

_Source: `src/compiler/sema_expr.cpp#L12301-L12320`, `src/compiler/sema_expr.cpp#L12327-L12338`_


### `expr.enum-lit.payload-arity-check` — Non-variadic variant payload arity must match

For a non-variadic variant, the number of supplied payload arguments must equal the declared payload arity; mismatch is an error `<E>::<V> expects N args, got M`. Each payload argument's type must be compatible with the declared (substituted) payload type.

_Source: `src/compiler/sema_expr.cpp#L12168-L12180`_


### `expr.enum-lit.payload-type-inference` — Generic enum type-arg inference from payload and hint

For a generic enum, each type parameter is inferred from the corresponding payload: a bare-TypeVar payload binds the param to the argument's type; a structural payload type is unified against the argument to extract nested bindings. Unresolved integer/float literal payloads default to i32/f64 unless the surrounding hint pins the param to a concrete type, in which case the hint wins and the literal is widened to it. Params still unresolved after payload inference are filled from a matching enum hint.

_Source: `src/compiler/sema_expr.cpp#L12059-L12138`, `src/compiler/sema_expr.cpp#L12082-L12127`_


### `expr.enum-lit.self-resolves-to-enclosing-enum` — `Self::Variant` resolves to the enclosing enum

Inside an `impl Enum` body, the path head `Self` in a unit-variant or struct/tuple-shaped variant literal resolves to the enclosing enum's name, provided `Self` is bound to a type of enum kind.

_Source: `src/compiler/sema_expr.cpp#L11585-L11590`, `src/compiler/sema_expr.cpp#L11732-L11737`_


### `expr.enum-lit.struct-shape-named-fields` — Struct-shaped variant literal `E::V { f: e, .. }`

A struct-shaped variant literal binds named field initializers (and shorthands `name` ⇒ `name` var-ref) to the variant's declared payload fields by name, producing positional payload in declaration order. Errors: unknown field name, field specified more than once, missing field(s) (all reported together), and using `{}` form on a non-struct-shape variant. An empty struct-shape variant `E::Empty {}` is accepted with empty payload.

_Source: `src/compiler/sema_expr.cpp#L11853-L11966`_


### `expr.enum-lit.type-alias-peel` — Variant path through a non-generic enum type alias

A variant-literal path head that names a non-generic type alias whose aliased type is an enum is rewritten to the underlying enum name before variant lookup; generic aliases are not peeled here.

_Source: `src/compiler/sema_expr.cpp#L11591-L11598`, `src/compiler/sema_expr.cpp#L11738-L11745`_


### `expr.enum-lit.unit-payload-kept` — Unit payload retained, not elided

A unit-typed payload argument (e.g. `()` in `Result::Ok(())`) is retained as a real payload entry; void/unit payloads are not filtered out.

_Source: `src/compiler/sema_expr.cpp#L12299-L12300`, `src/compiler/sema_expr.cpp#L12321-L12348`_


### `expr.enum-lit.unit-variant-hint-type-args` — Payload-less variant on a generic enum infers type args from the surrounding hint

A payload-less variant of a generic enum (e.g. `Option::None`) takes its type arguments from the surrounding type hint when the hint is the same enum with a matching type-arg arity; otherwise the result type is the bare (un-parameterized) enum.

_Source: `src/compiler/sema_expr.cpp#L11704-L11725`_


### `expr.enum-lit.unknown-enum-error` — Unknown enum / unknown variant diagnostics

A variant-literal path whose head names no enum (after Self/alias resolution and all assoc-const/fn-ptr fallbacks) is an error `unknown enum '<name>'`; a known enum with no matching variant is an error `enum '<E>' has no variant '<V>'`.

_Source: `src/compiler/sema_expr.cpp#L11681-L11682`, `src/compiler/sema_expr.cpp#L11701-L11702`, `src/compiler/sema_expr.cpp#L11838-L11847`_


### `expr.enum-lit.unknown-variant` — Enum literal references an existing variant

In an enum literal `E::V(args)`, `V` must be a declared variant of enum `E`; otherwise the program is ill-formed (diagnostic "enum 'E' has no variant 'V'").

_Source: `src/compiler/sema_expr.cpp#L12287-L12293`_


### `expr.enum-lit.variadic` — Variadic variant payload checking

For a variadic variant, every payload argument is checked for compatibility against (and integer-literal fit within) the single pack element type (the first declared payload type), with no arity constraint.

_Source: `src/compiler/sema_expr.cpp#L12524-L12527`, `src/compiler/sema_expr.cpp#L12610-L12628`_


## If expressions

### `expr.if.branch-result-coercion` — If-expression coerces both branch values to the result type

An if-expression of type T evaluates the condition then both branches; each non-diverging branch value is numerically coerced to T and stored into a shared result slot, whose value is the if-expression's result. Aggregate branch values are spilled to a stack slot so both branches store a pointer when T is pointer-represented.

_Source: `src/compiler/mlir_gen_expr.cpp#L3710-L3780`_


### `expr.if.branch-type-compatible` — if-expr branches must have compatible types

In an `if` expression, the THEN and ELSE branch types must be mutually compatible (one assignable to the other); incompatible non-error, non-never branch types are an error. The result type is the unification (LUB) of the two branch types.

_Source: `src/compiler/sema_expr.cpp#L14000-L14037`_


### `expr.if.cond-must-be-bool` — if condition must be bool

The condition of a non-`let` `if` must have type `bool`; the error/never types are also accepted (error recovery and diverging conditions).

_Source: `src/compiler/sema_expr.cpp#L13901-L13906`_


### `expr.if.divergent-branch-skips-merge` — Diverging if-branch omits its merge edge

If a branch body diverges (e.g. `break`/`return` that already terminates the block), the if-expression omits that branch's result-store and merge branch; the merge point's predecessors simply exclude the diverging edge.

_Source: `src/compiler/mlir_gen_expr.cpp#L3742-L3759`, `src/compiler/mlir_gen_expr.cpp#L3763-L3773`_


### `expr.if.let-chain` — if let-chain

An `if` may chain conditions with `&&` where the first segment is a `let` binding: `if let P = e && seg (&& seg)* { THEN } [else …]`. Each subsequent seg is either `let P = e` or a bare condition (level `cmp_expr_ns`). The chain requires the first segment to be a `let` and at least two `&&`-joined segments. Desugars to nested matching: all let-patterns must match and all conditions hold for THEN.

_Source: `tools/peg_gen/grammars/logos.peg#L2342-L2381`_


### `expr.if.let-condition` — if and if-let

`if cond { ... }` takes a boolean condition; `if let PAT = expr { ... }` matches a pattern. An `else` branch is either a block or a chained `else if`.

_Source: `src/compiler/sema_render.cpp#L395-L420`_


### `expr.if.let-desugars-to-match` — if-let expression lowers to a two-arm match

`if let P = e { THEN } else { ELSE }` in expression position is equivalent to `match e { P => THEN, _ => ELSE }`; the pattern's bindings are in scope only within THEN, and the result type is that of the THEN branch.

_Source: `src/compiler/sema_expr.cpp#L13815-L13897`_


### `expr.if.never-branch-skipped` — Never/error branch yields the other branch's type

A branch typed `!` (never) or error contributes no type to an `if` expression: the expression's type is the other branch's type. `!` behaves as a subtype of every type at the join. A branch whose final statement is `return`/`break`/`continue` (or a diverging tail call) is typed `!`.

**Related.** `expr.block.tail-divergent-call-never`

_Source: `src/compiler/sema_expr.cpp#L13959-L13970`, `src/compiler/sema_expr.cpp#L13998-L14005`_


### `expr.if.no-struct-lit-cond` — if/while/for condition restricts struct literals

In `if`/`while`/`for` condition position the scrutinee uses the no-struct-lit expression grammar (`expr_ns`): a top-level `IDENT { … }` is NOT parsed as a struct literal, so the brace opens the control-flow block. A struct literal in condition position must be parenthesized. Restriction applies only to the top-level primary; inside parens/brackets/calls full `expr` resumes.

_Source: `tools/peg_gen/grammars/logos.peg#L2411-L2417`, `tools/peg_gen/grammars/logos.peg#L2512-L2516`_


### `expr.if.requires-else-in-expr-position` — if/if-let in expression position requires else

An `if` or `if let` used as an expression (yielding a value) must have an `else` branch; an `if` without `else` is only valid in statement position.

_Source: `src/compiler/sema_expr.cpp#L13820-L13823`, `src/compiler/sema_expr.cpp#L13913-L13916`_


### `expr.if.single-let-guard` — if-let with single guard condition

`if let P = e && cond { THEN } [else ELSE]` (single let plus trailing condition) desugars to `match e { P if cond => THEN, _ => ELSE }`; the let scrutinee is parsed at `cmp_expr_ns` so the `&&` belongs to the guard.

_Source: `tools/peg_gen/grammars/logos.peg#L2357-L2364`_


### `expr.if.void-branches-still-evaluated` — Void if-expression still evaluates both branches

An if-expression of unit type `()` still emits and evaluates both branch bodies (for their side effects such as panics/writes) and yields a synthetic unit value; the branches are not dropped despite producing no value.

_Source: `src/compiler/mlir_gen_expr.cpp#L3715-L3724`, `src/compiler/mlir_gen_expr.cpp#L3775-L3779`_


## If-let chains

### `expr.if-let-chain.fall-to-else-on-failure` — if-let chain falls to else on any segment failure

`if let P1 = e1 && let P2 = e2 && cond { THEN } else { ELSE }` evaluates a flat sequence of refutable binds and boolean conditions left-to-right; any failed bind or false condition takes the ELSE branch.

_Source: `tools/peg_gen/grammars/logos.peg#L318-L320`_


### `expr.if-let-chain.min-two-segments` — if-let chain requires at least two segments

An `if let ... && ...` chain must contain at least two segments (let-bindings and/or conditions); fewer is an error. The chain desugars inside-out into nested `if let`/`if` with the `else` branch duplicated at each fall-through.

**Uncertainty.** ELSE duplication at each fall-through is documented as an accepted limitation, not a fundamental rule.

_Source: `src/compiler/sema_expr.cpp#L13745-L13797`_


## Match expressions

### `expr.match.arm-after-catchall-unreachable` — arm after a catch-all `_` arm is unreachable

A match arm that follows an unguarded catch-all (`_`) arm is unreachable and is diagnosed (closes B-pt-07 expr position).

_Source: `src/compiler/sema_stmt.cpp#L8946-L8959`_


### `expr.match.arm-block-tail-is-value` — block arm yields its tail expression, not an implicit return

A block-form arm (`pat => { stmts }`) yields its trailing expression as the arm value (tail-as-return disabled inside match arms). A non-diverging block arm whose last statement is not an expression is a diagnostic ('block arm must end with an expression or always return'). A block arm all of whose paths diverge contributes Error and is skipped in unification.

_Source: `src/compiler/sema_stmt.cpp#L9414-L9467`_


### `expr.match.arm-first-match-order` — Arms tested top-to-bottom; first match wins

Arms are evaluated in source order; the first arm whose pattern matches (and whose guard, if any, holds) is selected, and remaining arms are not tested.

_Source: `src/compiler/mlir_gen_expr.cpp#L4294-L4738`, `src/compiler/mlir_gen_expr.cpp#L4337`, `src/compiler/mlir_gen_expr.cpp#L4734-L4736`_


### `expr.match.arm-forms` — match arm syntax

`match scrutinee { PAT [if GUARD] => RHS, ... }`; each arm has an optional `if`-guard and an arm body that is either a block or an expression followed by a comma.

_Source: `src/compiler/sema_render.cpp#L422-L447`_


### `expr.match.arm-requires-body` — every arm must have an expr or block body

A match arm must have either an expression body (`=> expr`) or a block body (`=> { ... }`); an arm with neither is a diagnostic.

_Source: `src/compiler/sema_stmt.cpp#L9412-L9471`_


### `expr.match.enum-discriminant-dispatch` — Match on enum dispatches by discriminant

For an enum scrutinee, arm selection compares the scrutinee's discriminant against each arm's variant discriminant. A payload-carrying enum (with TaggedEnumInfo) loads its discriminant from its storage; a fieldless/C-like enum's value IS its i32 discriminant.

_Source: `src/compiler/mlir_gen_expr.cpp#L3841-L3876`, `src/compiler/mlir_gen_expr.cpp#L4382-L4390`, `src/compiler/mlir_gen_expr.cpp#L4721-L4737`_


### `expr.match.exhaustive-bool` — match on bool must cover true and false

A `match` on a `bool` scrutinee without a wildcard arm must have both a `true` and a `false` unguarded literal arm; a missing case is diagnosed.

_Source: `src/compiler/sema_stmt.cpp#L9681-L9694`_


### `expr.match.exhaustive-enum` — match on enum must be exhaustive

A `match` on an enum scrutinee without a wildcard/catch-all arm (and without AST-level proof of exhaustiveness for nested patterns) must cover every constructible variant; uncovered variants are reported as 'missing variant(s)'. A variant all of whose (substituted) payload types are uninhabited is unconstructable and need not be covered.

**Related.** `expr.match.exhaustive-enum-uninhabited`

_Source: `src/compiler/sema_stmt.cpp#L9603-L9680`_


### `expr.match.exhaustive-enum-uninhabited` — uninhabited-payload variants are exempt from exhaustiveness

Exhaustiveness substitutes the scrutinee's type-arguments into each variant's (generic) payload types before the uninhabited check; a variant with any uninhabited payload (e.g. `Result<T, Void>`'s Err) is unconstructable and omitting its arm remains exhaustive (T2-29).

**Related.** `expr.match.exhaustive-enum`

_Source: `src/compiler/sema_stmt.cpp#L9650-L9675`_


### `expr.match.exhaustive-no-default-arm` — Exhaustive discrete match needs no fallthrough default

A match over `bool` covering both `true` and `false` (or a wildcard), or over an enum covering every variant (or a wildcard), is exhaustive; no implicit fall-through arm is required and the non-matching path is unreachable.

_Source: `src/compiler/mlir_gen_expr.cpp#L4229-L4293`_


### `expr.match.fnitem-arms-lub-fnptr` — distinct fn-item arms LUB to the common fn-pointer type

When two arms produce distinct FnItem values with the same signature (e.g. `=> a_f` and `=> b_f`), the match result type is the corresponding `fn(...)->R` pointer type, since FnItem→FnItem coercion is rejected; both arms coerce to that FnPtr.

**Divergence.** Rust-conformant: matches Rust LUB for fn-item match arms.

**Related.** `expr.match.result-type-lub`

_Source: `src/compiler/sema_stmt.cpp#L9502-L9523`_


### `expr.match.guard-after-bindings` — Guard evaluated after pattern bindings, fall-through on false

An arm guard `if cond` is evaluated only after the arm's pattern matches and its bindings are in scope; the guard may reference those bindings. If the guard is false, control falls through to the next arm rather than selecting this arm.

_Source: `src/compiler/mlir_gen_expr.cpp#L4318-L4339`_


### `expr.match.guard-bool` — match guard must be bool

An arm guard expression (`pat if <guard> =>`) must have type `bool` (or Error); any other type is a diagnostic.

_Source: `src/compiler/sema_stmt.cpp#L9343-L9348`_


### `expr.match.guarded-arm-not-exhaustive` — guarded arms do not count toward exhaustiveness

An arm with a guard (`if`) does not contribute to exhaustiveness coverage; only unguarded patterns are counted as covering variants/wildcards.

_Source: `src/compiler/sema_stmt.cpp#L9612`, `src/compiler/sema_stmt.cpp#L9618-L9623`, `src/compiler/sema_stmt.cpp#L9639-L9640`_


### `expr.match.intlit-result-widen` — integer-literal match result widens to i64 on i32 overflow

If the inferred match result type is the unconstrained integer-literal type, and any arm's literal value exceeds the i32 range (> INT32_MAX or < INT32_MIN), the result type is fixed to i64.

**Related.** `expr.match.result-type-lub`

_Source: `src/compiler/sema_stmt.cpp#L9535-L9550`_


### `expr.match.never-arm-ignored` — Never-typed (diverging) arms do not constrain the result type

An arm whose value type is `!` (Never) contributes no type to the match result; Never is a subtype of every type. If the accumulated result type is still `!` or Error, the next arm's type replaces it.

**Related.** `expr.match.result-type-lub`

_Source: `src/compiler/sema_stmt.cpp#L9494-L9501`_


### `expr.match.result-type-lub` — match-expression result type is the LUB of its arms

The type of a `match` expression is the least-upper-bound of its arms' value types. Arms are unified left-to-right: error-typed and Never-typed arms contribute no type; numeric arms unify via numeric-LUB. If two arms have types that are mutually incompatible (neither `types_compatible` direction holds) the match is a type error.

**Related.** `expr.match.never-arm-ignored`, `expr.match.fnitem-arms-lub-fnptr`, `expr.match.intlit-result-widen`

_Source: `src/compiler/sema_stmt.cpp#L9497-L9534`_


### `expr.match.scrutinee-autoderef` — Match auto-derefs reference/pointer scrutinees

When the scrutinee type is a chain of `&` / `&mut` / `*` over an enum (arbitrary depth, e.g. `&&Option<T>`), `match` peels all reference layers and matches against the underlying value: `match &e { ... }` behaves identically to `match e { ... }`.

_Source: `src/compiler/mlir_gen_expr.cpp#L3823-L3877`_


### `expr.match.str-literal-arm-guard` — string-literal arms lower to wildcard + str-eq guard

A top-level string-literal arm (`match s { "foo" => ... }`) matches via a wildcard pattern plus a synthesized `str_eq(scrutinee, "foo")` guard, AND-ed ahead of any user guard; the scrutinee is hoisted into a synthetic local first (G172-1).

_Source: `src/compiler/sema_stmt.cpp#L9034-L9067`, `src/compiler/sema_stmt.cpp#L9193-L9211`, `src/compiler/sema_stmt.cpp#L9350-L9359`_


### `expr.match.temp-scrutinee-dropped` — a droppable rvalue scrutinee is dropped after the match value

When the scrutinee of a match-expression is a droppable move-type rvalue (not a place: not a var/field/tuple-index/deref/index read), it is bound to a synthetic local and dropped on every exit path. On fall-through the temporary is dropped after the match result is bound (unless an arm moved its payload); an arm that returns drops it via its own drop set.

**Related.** `borrow.match.scrutinee-moved-by-binding`

_Source: `src/compiler/sema_stmt.cpp#L8875-L8937`, `src/compiler/sema_stmt.cpp#L8884-L8903`_


### `expr.match.value-result-type` — Match expression yields a single value of the common arm type

A `match` used as an expression evaluates to the value of the selected arm; every arm body's value is coerced to the match's result type. Arms whose body diverges (does not fall through) contribute no value.

_Source: `src/compiler/mlir_gen_expr.cpp#L3789-L3807`, `src/compiler/mlir_gen_expr.cpp#L4346-L4351`, `src/compiler/mlir_gen_expr.cpp#L4743`_


### `expr.match.writ-pattern-needs-view` — Writ patterns require a view scrutinee

A match arm containing a Writ scalar pattern (PAT_WRIT_NULL/BOOL/INT/STR/MAP/ARR/TYPED_ARR/TYPED_MAP, including inside an or-pattern) requires the scrutinee to be a Writ view (Writ, WritView, or WritStatic; use `&` to borrow); otherwise a diagnostic is emitted.

**Divergence.** Logos extension: Writ structured-data pattern matching (not in Rust).

_Source: `src/compiler/sema_stmt.cpp#L8961-L9003`_


## Loop expressions

### `expr.loop.as-expr-type` — loop expression type: ! if no break-value, () if value-less break

A `loop {...}` used as an expression has type `!` (never) when no `break v` is reachable and the loop diverges, type `()` when a value-less `break` is reached, and the common break-value type when `break v` is reached.

_Source: `src/compiler/sema_expr.cpp#L1504-L1547`_


### `expr.loop.empty-loop-diverges` — `loop {}` with no break is divergent (`!`)

A `loop { .. }` containing no `break` reaching its frame is a diverging expression of type `!` (Never), not `()`. Only `loop` (not `for`/`while`) reads its break frame to become a value-yielding expression; its value type is the unified type of `break <expr>` values targeting its frame.

_Source: `src/compiler/sema_impl.hpp#L3655-L3673`_


## Break expressions

### `expr.break.label-must-be-in-scope` — `break`/`continue 'label` require the label in scope

`break 'label` / `continue 'label` are valid only when `'label` names a currently-active labelled loop; an out-of-scope label is rejected. A labelled `break 'label v` attributes its value type to the frame matching the label; an unlabelled break targets the innermost loop frame.

_Source: `src/compiler/sema_impl.hpp#L3634-L3638`, `src/compiler/sema_impl.hpp#L3655-L3666`_


### `expr.break.value-loop-typing` — break value selects the loop's value type

A `break value` (optionally labeled) attributes its value type to the target loop frame; the frame's value type is the unification (numeric) of all break values, making the loop a value-yielding expression.

_Source: `src/compiler/sema_expr.cpp#L1438-L1458`_


## Return expressions

### `expr.return.implicit-tail` — Tail expression is implicit return

A trailing expression with no terminating `;` at statement position synthesizes an implicit `return expr` for a non-void function.

_Source: `tools/peg_gen/grammars/logos.peg#L291`_


## Try (`?`) operator

### `expr.try.heterogeneous-error-from` — ? converts inner error via From when error types differ

For `e?` with `e: Result<T,E_inner>` in a function returning `Result<U,E_outer>` where E_inner != E_outer, the Err path returns `Err(E_outer::from(err))`, requiring `impl From<E_inner> for E_outer`; absence of that impl is an error.

_Source: `src/compiler/sema_expr.cpp#L1220-L1306`_


### `expr.try.ok-unwrap-err-propagate` — `expr?` unwraps Ok or early-returns Err

`expr?` on a Result-like tagged enum loads the discriminant: on the Ok variant it yields the Ok payload value; on the Err variant it reconstructs an Err value carrying the original error payload and immediately returns it from the enclosing function. The expression's value is the unwrapped Ok payload.

_Source: `src/compiler/mlir_gen_expr.cpp#L5563-L5681`_


### `expr.try.operator` — Try operator

Postfix `e?` is the try operator; it propagates the error/none case of a Result/Option-like value and yields the success payload.

_Source: `tools/peg_gen/grammars/logos.peg#L2692-L2693`_


### `expr.try.result-option-extract` — ? on Result/Option extracts or early-returns

`e?` where `e: Result<T,E>` extracts Ok(v) and early-returns Err(e); where `e: Option<T>` extracts Some(v) and early-returns None. It is valid only inside a function whose return type is the same enum (Result resp. Option); otherwise an error.

_Source: `src/compiler/sema_expr.cpp#L1153-L1217`, `src/compiler/sema_expr.cpp#L1307`_


### `expr.try.trait-dispatch-from-residual` — ? on non-Result/Option dispatches via Try/FromResidual

`e?` where e is neither stdlib Result nor Option desugars through the Try/FromResidual surface: `match e.branch() { Continue(c) => c, Break(r) => return RetType::from_residual(r) }`. The receiver RetType is taken from the enclosing function's declared return type (Logos does not infer trait Self from context); an undeterminable return type is an error.

**Divergence.** Receiver for from_residual is explicit from fn ret type (no contextual Self inference)

_Source: `src/compiler/sema_expr.cpp#L1167-L1195`_


## Control flow (control)

### `expr.control.break-continue-return-in-value-position` — break/continue/return usable in expression position

`break`, `continue`, and `return` may appear in expression position (Never-typed); the bare `return` form carries no value. They type-check as `!`/Never so surrounding expressions accept them.

_Source: `tools/peg_gen/grammars/logos.peg#L299-L301`_


### `expr.control.never-position` — Diverging control-flow as expression

`return [e]`, `break [label] [e]`, and `continue [label]` may appear in expression position with type `!` (never), permitting forms like `let x = if c { v } else { return e };` and `_ => break`.

_Source: `tools/peg_gen/grammars/logos.peg#L2716-L2728`_


## Control flow (control-flow)

### `expr.control-flow.diverging-is-never` — break/continue/return in expression position have type !

`break`, `continue`, and `return` used in expression position have type `!` (never), which coerces to/unifies with any surrounding expected type. `continue`/`break` outside any loop are errors. `return e` in expression position checks e against the function's return type.

_Source: `src/compiler/sema_expr.cpp#L1393-L1462`_


## Tail expressions

### `expr.tail.implicit-return-in-fn-body` — Tail expression is an implicit return only in fn-body context

A block's trailing tail-expression acts as an implicit return when lowering a fn body, but not inside block-as-expression contexts (match-arm body, unsafe-block-as-expr, if-as-expr) where the tail is the block's value rather than a function return.

_Source: `src/compiler/sema_impl.hpp#L3675-L3678`_


## Block expressions

### `expr.block.as-value` — Block / control constructs as expressions

`{ … }` blocks, `unsafe { … }`, `loop { … }`, `if … {} else {}`, and `match … {}` are all primary expressions producing a value (block/loop yield their tail/break value).

_Source: `tools/peg_gen/grammars/logos.peg#L2711-L2715`_


### `expr.block.empty-is-void` — Empty block has type ()

A block expression `{}` with no statements evaluates to the unit/void type `()`.

_Source: `src/compiler/sema_expr.cpp#L13653-L13656`_


### `expr.block.tail-divergent-call-never` — Block with diverging tail call types as !

If a block's final tail expression is a call to a `-> !` (diverging) callee, the block types as the never type `!`; the diverging call is still emitted and the block contributes no concrete value type to its context.

**Related.** `expr.if.never-branch-skipped`

_Source: `src/compiler/sema_expr.cpp#L13692-L13696`_


### `expr.block.tail-expr-value` — Block value is its trailing tail expression

The type and value of a block `{ s1; ...; e }` are those of its final element when that element is a tail/expression statement (or a non-statement expression form); a block whose final element is a `let`, destructuring-let, `return`, or `;`-terminated expr-stmt produces no tail value and types as `()`.

_Source: `src/compiler/sema_expr.cpp#L13676-L13724`_


### `expr.block.tail-return-adopts-value-type` — Block ending in `return e` adopts e's type

A block whose final statement is `return e` is non-diverging in the value system: the block's result type is taken as `typeof(e)` even though no value is produced, so the divergent block is usable at a non-void expected type (e.g. inside a tuple/struct literal). The `return` is still lowered and executed.

**Divergence.** No real `!`/never subtyping for tail-return; the return-value's type is adopted as a block-type proxy instead of `!`.

**Uncertainty.** Behavior is a stated workaround pending full never-type support.

_Source: `src/compiler/sema_expr.cpp#L13664-L13672`, `src/compiler/sema_expr.cpp#L13706-L13720`_


### `expr.block.value-block-scopes-let` — Value-producing block scopes its own let bindings

A value-producing block expression `{ stmts; result }` introduces a new lexical scope: a `let` at the block's top level that shadows an outer binding of the same name is visible only inside the block, and the outer binding is restored when the block's value is produced; the block does not clobber the outer slot.

```logos
let x = 1; let y = { let x = 100; x + 1 }; // x still == 1, y == 101
```

_Source: `src/compiler/mlir_gen_expr.cpp#L5494-L5557`_


## Postfix expressions

### `expr.postfix.chain` — Postfix operator chain

A primary expression may be followed by zero or more left-associative postfix suffixes: method call `.m(args)` (optionally `.m::<T>(args)` with explicit turbofish type args), expression-callee invocation `e(args)`, field read `.field`, tuple index `.N`, indexing `[i]`, and the try operator `?`. Chains parse left-to-right (`a.b.c`, `a.f().b`).

_Source: `tools/peg_gen/grammars/logos.peg#L2658-L2694`_


## Closures

### `expr.closure.body-is-drop-boundary` — Closure body scope is a drop boundary

A closure body is lowered in its own scope that is a drop boundary: a `return` inside the body drops only the closure's own frames, not the enclosing function's locals captured by the closure (those are owned by their original bindings or borrowed by the env).

_Source: `src/compiler/sema_expr.cpp#L14243-L14247`, `src/compiler/sema_expr.cpp#L14334-L14338`_


### `expr.closure.body-own-unsafe-scope` — Closure body does not inherit enclosing unsafe context

A closure body is lowered as its own scope and does not inherit the enclosing `unsafe` context; the inside-unsafe state is reset to false for the body and restored afterward.

_Source: `src/compiler/sema_expr.cpp#L14274-L14278`, `src/compiler/sema_expr.cpp#L14332-L14333`_


### `expr.closure.boxing-escapes` — A closure assigned to a Box<...Fn...> escapes

A closure lowered against an expected type that peels (through a Box / struct wrapper) to a callable Fn type is treated as escaping: its captured environment lives on the heap. A bare or reference-wrapped Fn expectation (e.g. an iterator-adapter argument) does not escape and keeps a stack environment.

_Source: `src/compiler/sema_expr.cpp#L14787-L14793`_


### `expr.closure.capture-borrow-of-var` — Taking the address of a variable in a closure body captures it

`&x` or `&mut x` appearing in a closure body captures the whole root variable `x` from the enclosing scope, just as a plain read would.

_Source: `src/compiler/sema_expr.cpp#L14584-L14587`_


### `expr.closure.capture-by-free-variable` — Closures capture free variables resolving in an enclosing scope

A closure captures exactly those names used in its body that are not its own parameters and that resolve to a binding in an enclosing scope; each captured name's type is the enclosing binding's type.

_Source: `src/compiler/sema_expr.cpp#L14388-L14432`, `src/compiler/sema_expr.cpp#L14421-L14432`_


### `expr.closure.capture-by-ref-on-mutation` — Mutating a captured variable forces by-reference capture

A captured variable that the closure body mutates is captured by reference. Mutation includes: assignment to the variable, field writes / multi-level (chained) field writes through it, indexed writes into it, and an auto `&mut` of the variable produced as a method receiver. A by-value capture of a mutated variable would lose the write.

_Source: `src/compiler/sema_expr.cpp#L14594-L14602`, `src/compiler/sema_expr.cpp#L14699-L14704`, `src/compiler/sema_expr.cpp#L14724-L14746`_


### `expr.closure.capture-disjoint-fields` — Disjoint closure capture by precise field path (RFC 2229)

When a closure body accesses fields of a variable through a pure `root.field*` dotted chain, the capture is the precise path rather than the whole root; multiple paths off the same root are widened to their lowest-common-ancestor path. If the access head is not a pure VarRef/FieldRead chain (e.g. `(*box).x`), the whole root is captured instead.

**Related.** `expr.closure.capture-free-vars`

_Source: `src/compiler/sema_expr.cpp#L14563-L14569`, `src/compiler/sema_expr.cpp#L14805`_


### `expr.closure.capture-drop-order` — Source-scope-dropped captures drop with the closure in capture order

Captures whose destructor the source scope still runs are dropped at the closure binding's slot in capture order, not at their own variable-order slots, matching Rust's closure capture drop order.

_Source: `src/compiler/sema_expr.cpp#L14892-L14897`_


### `expr.closure.capture-free-vars` — Closure captures the free variables referenced in its body

A closure literal captures exactly the set of variables from the enclosing scope that its body references (transitively through every expression and statement form), excluding the closure's own parameters and variables bound locally inside the body. A bare variable reference `x` captures the whole root `x`.

_Source: `src/compiler/sema_expr.cpp#L14539-L14546`, `src/compiler/sema_expr.cpp#L14691-L14773`, `src/compiler/sema_expr.cpp#L14801`_


### `expr.closure.disjoint-field-capture` — Closures capture disjoint fields (RFC-2229)

When a closure body reads a precise dotted field path `root.x.y` rooted at a captured variable, the capture is recorded at that path; multiple paths off the same root are widened to their lowest common ancestor segment (`lca("p.x","p.y")="p"`, widening to a larger/less precise borrow which is sound). The capture's slot is sized at the leaf field type when the path walks entirely through plain `Struct` fields; otherwise the whole root is captured. Paths are extracted only when the head is a plain variable reference followed by field reads (indexing or deref-through-box falls back to whole-variable capture).

```logos
let g = |p: &Pt| { use(p.x); use(p.y); };
```

_Source: `src/compiler/sema_expr.cpp#L14433-L14528`, `src/compiler/sema_expr.cpp#L14455-L14482`, `src/compiler/sema_expr.cpp#L14486-L14506`_


### `expr.closure.env-capture-binding` — A capturing closure binds captures from an environment record

A capturing closure is a {fn_ptr, env_ptr} value; the body receives env_ptr as a hidden leading parameter and each capture is bound from env field i+1 (env field 0 reserved for drop glue). Aggregate (struct/array/tuple/enum/dyn) captures are stored/bound by pointer; scalar captures are stored by value and re-allocated locally in the body.

_Source: `src/compiler/mlir_gen_dyn.cpp#L1843-L1849`, `src/compiler/mlir_gen_dyn.cpp#L1957-L2048`, `src/compiler/mlir_gen_dyn.cpp#L2216-L2228`_


### `expr.closure.escaping-env-owns-captures` — Escaping move closure owns droppable captures in its environment

An escaping (heap-environment / boxed) `move` closure that captures a droppable struct/array/tuple/enum moves it into the closure environment by value; the environment's drop glue drops it, so the originating scope does not. A non-escaping (stack-environment) `move` closure borrows the source storage, so the source scope still drops the value unless the body itself already moved the capture onward.

**Related.** `expr.closure.boxing-escapes`

_Source: `src/compiler/sema_expr.cpp#L14855-L14888`_


### `expr.closure.expr-body-yields-value` — Expression-body closure yields its expression

A closure with an expression body `|y| expr` (no braces) is lowered as if its body were `return expr;`; the closure result is the value of `expr`.

```logos
let f = |y| y * 2;
```

_Source: `src/compiler/sema_expr.cpp#L14284-L14290`_


### `expr.closure.hint-peels-callable-wrappers` — Closure-formal hint peels through refs/pointers and single-arg wrappers to a callable

When inferring closure param types from an expected type, the expected type is peeled (up to 8 levels) through `&T`/`&mut T`/`*T` (to pointee) and through a Struct/ZonedStruct with exactly one type argument (to that argument) until a Closure or FnPtr type is reached; the resulting callable's parameter list supplies the param-type hints. This lets `Box<dyn Fn(..)>`/`&dyn Fn(..)`-typed contexts still drive inference.

```logos
let b: Box<dyn Fn(i32) -> i32> = box_new(|x| x + 1);
```

_Source: `src/compiler/sema_expr.cpp#L14082-L14099`, `src/compiler/sema_expr.cpp#L14138-L14148`_


### `expr.closure.infer-params-from-fn-bound` — Untyped closure literal infers parameter types from an Fn-family bound

An untyped closure literal (`|x| ..`) appearing where an Fn-family-bounded type-param is expected infers its parameter types from the bound's signature, after the active substitution is applied; peeling through Ref/MutRef/Ptr and single-type-arg wrappers (`Box<dyn Fn(..)>`) to expose the inner callable type.

_Source: `src/compiler/sema_impl.hpp#L3944-L3959`_


### `expr.closure.move-marks-moved` — move closure consumes its move-type captures at the capture site

In a `move` closure, each captured variable (or, for an escaping narrow capture, the captured field path) whose type is a move type is marked moved at the closure site, making subsequent use of that variable/path a use-after-move error. Copy-type captures are not consumed.

_Source: `src/compiler/sema_expr.cpp#L14811-L14848`_


### `expr.closure.mut-bind-param` — `|mut x|` binds a mutable copy of the parameter

A closure parameter written `mut x` (IS_MUT, not a ref-bind) takes its argument under a synthetic name and binds the user-visible `x` as a mutable local initialized from the synthetic param (`let mut x = synth;`). The synthetic name is not entered into the sema scope, so move-typed params do not receive double drop glue.

```logos
let f = |mut x: i32| { x += 1; x };
```

_Source: `src/compiler/sema_expr.cpp#L14199-L14212`, `src/compiler/sema_expr.cpp#L14248-L14256`, `src/compiler/sema_expr.cpp#L14296-L14303`_


### `expr.closure.mutated-capture-by-reference` — Mutated captures are captured by reference

A captured variable that is the target of a mutation in the body (assignment / field write / index write / deref write) is captured by reference so the mutation propagates to the outer binding rather than to a local env copy. A write-only target (no prior read of its base) is still added to the capture set as a whole-variable capture.

**Divergence.** Capture mode is inferred per-variable from usage (read-only vs mutated), conceptually aligned with Rust closure capture-mode inference.

_Source: `src/compiler/sema_expr.cpp#L14395-L14420`_


### `expr.closure.narrow-move-requires-escape` — Narrow (field) move capture applies only to escaping closures; user Drop on root forces whole-var

RFC-2229 narrow move capture (moving only a field path, leaving sibling fields usable) applies only when the closure escapes; a non-escaping narrow capture moves nothing and the root keeps ownership. However, a `move` closure capturing a path whose root type has a user `impl Drop` captures the whole variable (so the value drops with the closure); mere drop glue from droppable fields keeps disjoint capture.

**Related.** `expr.closure.capture-disjoint-fields`, `expr.closure.escaping-env-owns-captures`

_Source: `src/compiler/sema_expr.cpp#L14820-L14854`_


### `expr.closure.nested-transitive-capture` — Outer closure transitively captures a nested closure's free vars

A closure literal nested in another closure's body causes the outer closure to capture the nested closure's free variables. If the nested closure captures a variable by reference (mutates it), the outer closure must also capture that variable by reference; otherwise the nested write would target the outer's by-value copy and be lost.

**Related.** `expr.closure.capture-by-ref-on-mutation`

_Source: `src/compiler/sema_expr.cpp#L14640-L14656`_


### `expr.closure.param-type-inference-from-hint` — Untyped closure params infer types from expected fn signature

For a closure literal `|x, y| …` whose parameters carry no type annotation, each untyped parameter's type is taken from the corresponding formal of the expected callable type at the call site (the closure-formal hint), by positional index. The hint is consulted only for params that lack both a TYPE and a NAMES (tuple-destructure) node.

```logos
let f: fn(i32) -> i32 = |x| x + 1;
```

_Source: `src/compiler/sema_expr.cpp#L14137-L14158`_


### `expr.closure.ref-bind-param` — `|ref x: T|` binds x as &T

A closure parameter written `ref x: T` (IS_REF with an explicit TYPE) takes its argument by value of type T under a synthetic name and binds the user-visible `x` to `&T` aliasing the synthetic param. IS_REF without a TYPE is the `&self`/`&mut self` shorthand, not a ref-bind.

```logos
let f = |ref x: i32| *x + 1;
```

**Divergence.** Logos closure ref-binding param syntax; no direct Rust equivalent.

_Source: `src/compiler/sema_expr.cpp#L14191-L14206`, `src/compiler/sema_expr.cpp#L14257-L14259`, `src/compiler/sema_expr.cpp#L14304-L14311`_


### `expr.closure.return-type-inference` — Closure return type inferred from first non-void return

A closure without an explicit `-> R` annotation infers its return type by scanning the lowered body (recursing into if/while/loop/block) for return statements and adopting the type of the first return value whose type is neither Void nor Error; if none is found the return type is `()` (void). During body lowering of an unannotated closure the expected return type is left unset so `return X;` is not strictly type-checked against it.

```logos
let f = |x: i32| { if x > 0 { return 1; } 2 };
```

_Source: `src/compiler/sema_expr.cpp#L14229-L14231`, `src/compiler/sema_expr.cpp#L14275-L14277`, `src/compiler/sema_expr.cpp#L14340-L14386`_


### `expr.closure.tuple-destructure-param` — `|(a, b): (T1, T2)|` destructures a tuple parameter

A closure parameter written `(a, b, …): (T1, T2, …)` takes a single synthetic tuple-typed parameter and binds each user name to the corresponding tuple element (`let a = synth.0; let b = synth.1; …`), with `_` sub-patterns skipped. Element bindings are only emitted when the param type is a Tuple type; bindings are positional up to the lesser of name-count and tuple arity.

```logos
let f = |(a, b): (i32, i32)| a + b;
```

_Source: `src/compiler/sema_expr.cpp#L14159-L14188`, `src/compiler/sema_expr.cpp#L14260-L14268`, `src/compiler/sema_expr.cpp#L14312-L14326`_


### `expr.closure.uniform-drop-glue-slot` — Closure env carries a uniform drop-glue slot

Every closure env reserves field 0 for a `drop_glue: ptr` slot for a uniform drop protocol; the slot holds the address of generated drop glue when the closure owns droppable captures or has a heap env (which must be freed), otherwise null (drop is a no-op).

_Source: `src/compiler/mlir_gen_dyn.cpp#L1843-L1849`, `src/compiler/mlir_gen_dyn.cpp#L2117-L2165`_


### `expr.closure.writ-capture-exprs` — Writ literal $-captures count as closure captures

Variables referenced via `$`-capture expressions inside a Writ literal in a closure body are captured by the enclosing closure.

**Related.** `expr.closure.capture-free-vars`

_Source: `src/compiler/sema_expr.cpp#L14681-L14687`_


## List comprehensions

### `expr.list-comp.bind-scope` — Comprehension binds the loop variable in value/guard scope

The loop variable `x` is bound (immutable, element type) in a new scope covering the value/key expressions and the guard; it is not visible outside the comprehension.

_Source: `src/compiler/sema_expr.cpp#L10939-L10946`, `src/compiler/sema_expr.cpp#L11030-L11037`, `src/compiler/sema_expr.cpp#L11142-L11149`, `src/compiler/sema_expr.cpp#L11275-L11283`_


### `expr.list-comp.desugar-vec` — List comprehension desugars to Vec build loop

A list comprehension `[value for x in iter (if guard)?]` desugars to a block that binds `let mut v: Vec<T> = vec_new::<T>()`, iterates `x` over `iter`, (optionally gated by `guard`) calls `Vec::push(&mut v, value)`, and evaluates to `v`. T is the iterator element type; the block's type is `Vec<T>`.

**Divergence.** Logos-specific surface syntax (Python-style comprehension); not present in Rust.

_Source: `src/compiler/sema_expr.cpp#L10885-L10986`_


### `expr.list-comp.iter-array-or-slice-only` — Comprehension iterables restricted to array/slice

The iterable of any comprehension form must have type `[T; N]` (array) or `[T]` (slice); any other iterator type is rejected. Element type defaults to i32 when the array/slice element type is absent.

**Divergence.** Narrower than Rust: only concrete array/slice, no IntoIterator/Iterator protocol.

**Uncertainty.** i32 default for missing elem type is a fallback; normally elem type is always present.

_Source: `src/compiler/sema_expr.cpp#L10896-L10907`, `src/compiler/sema_expr.cpp#L11002-L11013`, `src/compiler/sema_expr.cpp#L11112-L11123`, `src/compiler/sema_expr.cpp#L11245-L11256`_


### `expr.list-comp.requires-vec-import` — List comprehension requires Vec in scope

A list comprehension is ill-formed unless the `Vec` struct and the generic `vec_new` function are visible (via `use logos.mem.collections.vec;`).

**Divergence.** Logos-specific: surface sugar depends on a stdlib import being present.

_Source: `src/compiler/sema_expr.cpp#L10909-L10921`_


## Map comprehensions

### `expr.map-comp.desugar-hashmap` — Map comprehension desugars to HashMap build loop

A map comprehension `{key: value for x in iter (if guard)?}` desugars to a block that binds `let mut m: HashMap<K,V> = hashmap_new::<K,V>()`, iterates `x` over `iter`, (optionally gated by `guard`) calls `HashMap::insert(&mut m, key, value)`, and evaluates to `m`. K = type of `key`, V = type of `value`; block type is `HashMap<K,V>`.

**Divergence.** Logos-specific surface syntax; not present in Rust.

_Source: `src/compiler/sema_expr.cpp#L10992-L11090`_


### `expr.map-comp.requires-hashmap-import` — Map comprehension requires HashMap in scope

A map comprehension is ill-formed unless the `HashMap` struct and the generic `hashmap_new` function are visible (via `use logos.mem.collections.hashmap;`).

**Divergence.** Logos-specific.

_Source: `src/compiler/sema_expr.cpp#L11015-L11026`_


## Comprehensions (general)

### `expr.comprehension.list-and-map` — List and map comprehensions

List comprehension `[expr for x in iter (if pred)?]` and map comprehension `{kexpr: vexpr for x in iter (if pred)?}` produce a collection by iterating `iter`, binding `x`, optionally filtering by `pred`.

**Divergence.** Logos addition: Python-style comprehensions; not present in Rust.

_Source: `tools/peg_gen/grammars/logos.peg#L2875-L2885`_


## Formatting (fmt)

### `expr.fmt.arg-id-kind` — Explicit-index vs named argument id

If the first arg_id char is a digit it is parsed as an explicit positional index; if it is an alphabetic char or `_` it is parsed as a named-argument identifier ([A-Za-z_][A-Za-z0-9_]*).

_Source: `src/compiler/sema_fmt.cpp#L75-L89`, `src/compiler/sema_fmt.cpp#L157-L165`_


### `expr.fmt.brace-escape` — Doubled braces escape a literal brace

In a format string, `{{` denotes a literal `{` and `}}` denotes a literal `}`; each doubled brace contributes exactly one brace to the literal output and is not treated as a placeholder delimiter.

_Source: `src/compiler/sema_fmt.cpp#L121-L134`_


### `expr.fmt.fill-align` — Fill+align detection

A fill character is recognized only when immediately followed by an alignment marker (`<`,`>`,`^`), forming a 2-char fill+align prefix; a bare alignment marker uses the default fill; `<`=Left, `>`=Right, `^`=Center.

_Source: `src/compiler/sema_fmt.cpp#L176-L196`_


### `expr.fmt.implicit-positional-counter` — Implicit positional argument assignment

Placeholders without an explicit arg_id are assigned consecutive positional indices starting at 0, incremented per implicit placeholder; explicit-index and named placeholders do not advance this counter.

_Source: `src/compiler/sema_fmt.cpp#L166-L169`_


### `expr.fmt.placeholder-syntax` — Placeholder grammar

A placeholder has form `{` arg_id? (`:` format_spec)? `}` where arg_id is either an unsigned integer (explicit positional index) or an identifier (named argument); absence of arg_id means the next implicit positional argument.

_Source: `src/compiler/sema_fmt.cpp#L148-L170`_


### `expr.fmt.precision-requires-number` — Precision dot requires a number

A `.` in the format spec must be followed by an unsigned-integer precision; a `.` not followed by a digit is a compile error.

**Divergence.** Rust additionally permits `.*` and `.N$` precision forms; Logos here requires a literal number after `.`.

_Source: `src/compiler/sema_fmt.cpp#L224-L235`_


### `expr.fmt.spec-field-order` — Format spec field ordering

After `:` the format spec fields appear in fixed order: (fill align)? sign? `#`? `0`? width? (`.` precision)? type? where align in {`<`,`>`,`^`}, sign in {`+`,`-`}, width and precision are unsigned integers, and type is a single char.

_Source: `src/compiler/sema_fmt.cpp#L172-L256`_


### `expr.fmt.type-char-set` — Format type chars select a formatting trait

The type char selects the formatting trait: `?`=Debug, `x`=LowerHex, `X`=UpperHex, `o`=Octal, `b`=Binary, `e`=LowerExp, `E`=UpperExp; absence means Display; any other char before `}` is a compile error (`unknown type char`).

_Source: `src/compiler/sema_fmt.cpp#L237-L256`, `src/compiler/sema_fmt.cpp#L43-L55`_


### `expr.fmt.unmatched-close-brace` — Unescaped `}` is an error

A `}` that is not part of a `}}` escape and does not close a placeholder is a compile error (`unmatched `}``); use `}}` to emit a literal `}`.

_Source: `src/compiler/sema_fmt.cpp#L135-L142`_


### `expr.fmt.unmatched-open-brace` — Unterminated placeholder is an error

A `{` opening a placeholder must be closed by a matching `}`; if the placeholder body ends without `}`, it is a compile error (`unmatched `{``).

_Source: `src/compiler/sema_fmt.cpp#L259-L265`_


## Formatting (format)

### `expr.format.arg-widen-to-i64` — format() widens each argument to i64 by signedness

The format() built-in passes a parallel [i32 tags] and [i64 data] array: each argument is widened to i64 — pointers via ptrtoint, unsigned integers narrower than 64 bits via zero-extension, all other integers via sign-extending coercion. The type tag is computed per argument type (e.g. i32→0, i64→1, ptr/slice→2, bool→3, u8→4, u32/u16→5, u64/u24/u56/u128→6, i8→7; i16→i32, i24/i56/i128→i64).

_Source: `src/compiler/mlir_gen_expr.cpp#L5290-L5373`_


### `expr.format.requires-text-import` — format() requires std.lang.text import

Using the format() built-in requires the __format_impl runtime function to be available, provided by importing std.lang.text; absence is a compile error.

_Source: `src/compiler/mlir_gen_expr.cpp#L5375-L5386`_


## Drop semantics

### `expr.drop.closure-env-glue` — Closure drop runs the captured environment's drop glue

Dropping a closure value ({fn, env} 16-byte handle) loads env = handle[1]; if env != null, loads glue = env[0]; if glue != null, calls glue(env). A non-owning closure has a null env (or null glue) so its drop is a guarded no-op. Closures are not auto-recursed via the needs-drop predicate; their drop is driven explicitly.

_Source: `src/compiler/mlir_gen_stmt.cpp#L868-L869`, `src/compiler/mlir_gen_stmt.cpp#L996-L1034`_


### `expr.drop.dynamic-flag` — Dynamic drop flag for conditionally-initialized variables

A `let mut x: T;` declared without an initializer whose initialization is not statically determinable (an assignment nested inside a conditional/loop deeper than its declaration) gets a hidden runtime i8 drop flag (0 = empty, 1 = live). Each assignment drops the old value only if the flag is set then sets it; scope-exit/return drops only if the flag is set. Variables whose every assignment is straight-line (statically dominates) are flag-free with statically-placed drops.

_Source: `src/compiler/mlir_gen_impl.hpp#L314-L333`_


### `expr.drop.enum-user-drop-then-variant` — Enum drop: user Drop runs first, else variant-switched payload recursion

Dropping an enum value first calls its user `impl Drop` if a drop symbol actually exists (a by-value self that consumes the payload; nested enums then stop). Absent a real user Drop, drop switches on the loaded discriminant and, for each variant carrying a droppable payload field, recurses into that field. Variants whose payload needs no drop emit no work; a wholly drop-less enum drops nothing.

_Source: `src/compiler/mlir_gen_stmt.cpp#L939-L983`, `src/compiler/mlir_gen_stmt.cpp#L946-L950`, `src/compiler/mlir_gen_stmt.cpp#L951-L982`_


### `expr.drop.flag-uninit-conditional` — Conditionally/late-initialized variables drop only when live

A variable that may be uninitialized at a drop point runs its destructor only if it currently holds a live value. With dynamic tracking a per-variable drop flag (0/1) is consulted at runtime (flag==1 → drop, else no-op). With static tracking the destructor is emitted only when the variable is statically known to be assigned at that point; an early return before first assignment, the !c arm of a conditional init, or a never-assigned variable drops nothing.

**Divergence.** Logos drop flags / static drop tracking (B8). Models Rust's conditional drop flags.

_Source: `src/compiler/mlir_gen_stmt.cpp#L1184-L1214`_


### `expr.drop.owning-box-dst` — Drop of an owning custom-DST box (Box<Foo> with [T] tail)

Dropping an owning custom-DST handle (Box<Foo> where Foo = {prefix fields..., [T] tail}) over a non-null data pointer: (1) drop each droppable prefix field (in declaration order, skipping ref/ptr fields and fields that don't need drop), (2) drop the tail's elements over the runtime length len at element stride layout_of(T).size, then (3) free the whole heap block. A null data pointer (a moved-from handle) drops nothing and frees nothing.

_Source: `src/compiler/mlir_gen_stmt.cpp#L658-L753`, `src/compiler/mlir_gen_stmt.cpp#L680-L689`, `src/compiler/mlir_gen_stmt.cpp#L704-L743`, `src/compiler/mlir_gen_stmt.cpp#L750`_


### `expr.drop.owning-box-dyn` — Drop of an owning Box<dyn Trait> fat handle is uniform across storage sites

An owning trait-object handle (inline {data,vtable} fat pair, e.g. Box<dyn>/Rc<dyn>/Arc<dyn>) drops by running vtable[0] (drop_in_place) on data followed by the kind-specific release (Box: free data; Rc/Arc: decrement strong count, free at last reference). This drop is uniform across every storage site — local, struct field, return temp, Vec/tuple/array element — reached via ordinary aggregate field recursion, not only a top-level local.

_Source: `src/compiler/mlir_gen_stmt.cpp#L846-L855`, `src/compiler/mlir_gen_stmt.cpp#L1049-L1052`_


### `expr.drop.owning-box-slice` — Drop of an owning Box<[T]> fat slice

Dropping an owning Box<[T]> ({data,len} fat slice) over a non-null data pointer: if T is droppable, drop each element i in [0,len) at data + i*stride (stride = layout_of(T).size, min 1), then free the heap buffer; if T is not droppable, only free the buffer. A null data pointer (moved-from) is a no-op.

_Source: `src/compiler/mlir_gen_stmt.cpp#L755-L817`, `src/compiler/mlir_gen_stmt.cpp#L768-L771`, `src/compiler/mlir_gen_stmt.cpp#L781-L815`_


### `expr.drop.ref-ptr-noop` — References and raw pointers are never dropped

Dropping a value of kind &T, &mut T, or *T (Ref/MutRef/Ptr) is a no-op: a reference/pointer does not own its referent, so dropping it runs no destructor and frees nothing. This also holds for fields/elements of those kinds during recursive drop.

_Source: `src/compiler/mlir_gen_stmt.cpp#L845`, `src/compiler/mlir_gen_stmt.cpp#L708`, `src/compiler/mlir_gen_stmt.cpp#L908`, `src/compiler/mlir_gen_stmt.cpp#L929`, `src/compiler/mlir_gen_stmt.cpp#L976`_


### `expr.drop.scope-order-user-then-children` — Scope drop runs the variable's own Drop before recursing its children

At scope end a variable's own user drop function (if any) is invoked first, then its owned sub-values (struct fields, tuple elements, enum payload, array elements, owning slice/DST/closure) are recursively dropped. Children moved out are skipped (see expr.drop.skip-moved-paths). A moved-out unsized `dyn` tail runs only the concrete Drop via vtable[0](data) with NO free (the enclosing block is freed separately).

_Source: `src/compiler/mlir_gen_stmt.cpp#L1053-L1099`, `src/compiler/mlir_gen_stmt.cpp#L1101-L1181`_


### `expr.drop.skip-moved-paths` — Moved-out fields/elements are suppressed during scope drop

Scope-end drop of an owning aggregate suppresses sub-values that were moved out, identified by dotted field/element paths. An exact path ("f" or "i") skips the whole field/element; a deeper path ("f.g") recurses but suppresses only the moved leaf, so its siblings still drop. This prevents double-free of a value already moved elsewhere.

_Source: `src/compiler/mlir_gen_stmt.cpp#L823-L838`, `src/compiler/mlir_gen_stmt.cpp#L1107-L1159`, `src/compiler/mlir_gen_stmt.cpp#L910-L918`_


### `expr.drop.struct-user-drop-then-fields` — Struct drop: user Drop runs first, then field recursion governed by ownership

Dropping a struct/zoned-struct value first calls its user `impl Drop` (if one exists) which owns the value. A nested (non-top-level) struct then STOPS — the by-value self of the user drop already consumed the fields, so recursing them would double-drop. A top-level owner, or a struct with NO user Drop, recurses its droppable fields in REVERSE declaration order (skipping ref/ptr/non-droppable fields and statically moved-out field paths).

_Source: `src/compiler/mlir_gen_stmt.cpp#L880-L920`, `src/compiler/mlir_gen_stmt.cpp#L891-L897`, `src/compiler/mlir_gen_stmt.cpp#L905-L918`_


### `expr.drop.tuple-array-reverse` — Tuple and array element drop in reverse order

Dropping a tuple drops its droppable elements in reverse index order; dropping a fixed array [T;N] drops each of the N elements when T is droppable. Ref/ptr elements and non-droppable elements are skipped, and statically moved-out tuple element positions are suppressed.

**Divergence.** Rust drops array elements in forward (index-ascending) order; tuple reverse-order is conformant. Array order here is N forward but element-by-element; flagged as possibly observable only via Drop side effects.

_Source: `src/compiler/mlir_gen_stmt.cpp#L922-L938`, `src/compiler/mlir_gen_stmt.cpp#L985-L995`_


## Unsafe expressions

### `expr.unsafe.block` — Unsafe block

`unsafe { ... }` is an unsafe block whose body is an ordinary block.

_Source: `tools/peg_gen/grammars/logos.peg#L1825-L1827`_


### `expr.unsafe.block-in-expr-position` — unsafe block as expression

An `unsafe { ... }` block may appear in expression position (e.g. as a let initializer).

_Source: `src/compiler/sema_render.cpp#L538-L542`_


## Unsafe blocks

### `expr.unsafe-block.tail-value` — unsafe block in expression position yields its tail value

An `unsafe { ... }` in expression position evaluates its statements with unsafe permitted and yields the trailing expression's value (not a return); with no trailing expression it has type `()`.

_Source: `src/compiler/sema_expr.cpp#L1549-L1582`_


## sizeof-pack expressions

### `expr.sizeof-pack.spelling` — sizeof...(T) on a type-parameter pack

The pack-size operator must be spelled `sizeof...(T)` where T is an in-scope type parameter; it yields a u64. A different operator name or an unknown type parameter is an error.

_Source: `src/compiler/sema_expr.cpp#L1053-L1069`_


## Writ values

### `expr.writ.array` — Writ untyped array literal

An untyped Writ array `@[...]` lowers each element as a recursive Writ value in order.

**Divergence.** Logos addition (Writ literals).

_Source: `src/compiler/sema_expr.cpp#L15131-L15143`_


### `expr.writ.bool` — Writ bool literal

A Writ bool node yields a boolean Writ value; the value is true iff its byte payload is present and nonzero.

**Divergence.** Logos addition (Writ literals).

_Source: `src/compiler/sema_expr.cpp#L15021-L15025`_


### `expr.writ.capturable-types` — Types capturable by $-capture into a Writ value

A captured Logos expression is admissible into a Writ @-literal iff its type is: a scalar integer (i8/i16/i32/i64/u8/u16/u32/u64) or bool (coerced to inline AnyVal); F32/F64/float-literal (zone-allocated F64); AnyVal or a string-view struct; a pointer to u8 (*const u8 / *mut u8, captured as C-string varchar); or a u8 slice (str/&[u8], captured as varchar with length). Other types are not capturable.

**Divergence.** Logos addition (Writ captures).

_Source: `src/compiler/sema_expr.cpp#L15325-L15350`_


### `expr.writ.capture-not-standalone` — $-capture is not a standalone expression

A `$`-capture node (WRIT_CAP_IDENT / WRIT_CAP_EXPR) is only valid inside a writ value literal; appearing as a standalone expression is an error.

_Source: `src/compiler/sema_expr.cpp#L1489-L1494`_


### `expr.writ.capture-outside-context` — $-capture only inside capturable @-literal

A $-capture ($ident or $expr) in a Writ value is a compile error unless it occurs inside a capturable @-literal context.

**Divergence.** Logos addition (Writ captures).

_Source: `src/compiler/sema_expr.cpp#L15319-L15323`_


### `expr.writ.cfg-slot-type` — WritStatic const-generic slot type

A slot of a WritStatic-typed const-generic is referenced as `<type:CFG.slot.path>` with dot-separated step names.

**Divergence.** Logos-specific const-generic/Writ syntax.

_Source: `src/compiler/sema_render.cpp#L517-L531`_


### `expr.writ.cfg-slot-type-literal` — <type:CFG.path> at writ-value position

`<type:CFG.path>` resolves the config path eagerly and must denote a concrete top-level alias; if it resolves to a const-generic config-slot parameter (kind CfgSlotType) it is rejected with a compile error (parametric Writ literals are not supported).

**Divergence.** Logos addition (Writ/CFG type literals).

**Uncertainty.** Restriction is stated as a current limitation in the source.

_Source: `src/compiler/sema_expr.cpp#L14982-L15009`_


### `expr.writ.embedded-type-lit` — Embedded type in Writ literal

A Logos type can be embedded inside a Writ literal as `<type:T>`.

**Divergence.** Logos-specific Writ syntax.

_Source: `src/compiler/sema_render.cpp#L510-L516`_


### `expr.writ.float-suffix` — Writ float literal: suffix stripping

A Writ float literal accepts an optional `f32` or `f64` suffix which is stripped before parsing the value as a double-precision float.

**Divergence.** Logos addition (Writ literals).

_Source: `src/compiler/sema_expr.cpp#L15052-L15060`_


### `expr.writ.int-suffix-and-radix` — Writ integer literal: suffix stripping and radix

A Writ integer literal accepts an optional numeric-type suffix (i8/i16/i24/i32/i56/i64/i128, u8/u16/u24/u32/u56/u64/u128, usize, isize) which is stripped before parsing, an optional leading '-', and a radix prefix: `0x` = hexadecimal, `0b` = binary, otherwise decimal. The resulting magnitude is negated if the sign was present.

**Divergence.** Logos addition (Writ literals); note i24/i56/u24/u56 width suffixes.

_Source: `src/compiler/sema_expr.cpp#L15027-L15050`_


### `expr.writ.map-entry-colon` — Writ map entry syntax

A Writ map literal `@{ ... }` contains comma-separated entries `key: value`; nested scalar values omit the `@` prefix in inner position.

**Divergence.** Logos-specific Writ syntax.

_Source: `src/compiler/sema_render.cpp#L479-L497`_


### `expr.writ.map-keys` — Writ map literal keys (string or integer)

An untyped Writ map `@{...}` has entries whose key is either a quoted string (quote-stripped and escape-processed like a Writ string) or an integer; an integer key is negated when the entry carries the negative-key marker. Values are recursively lowered Writ values.

**Divergence.** Logos addition (Writ literals).

_Source: `src/compiler/sema_expr.cpp#L15088-L15129`_


### `expr.writ.neg-int` — Writ negative integer literal

A Writ negative-integer node yields an integer Writ value equal to the negation of the parsed decimal magnitude.

**Divergence.** Logos addition (Writ literals).

_Source: `src/compiler/sema_expr.cpp#L15012-L15016`_


### `expr.writ.null` — Writ null literal

A Writ null node yields the null Writ value.

**Divergence.** Logos addition (Writ literals).

_Source: `src/compiler/sema_expr.cpp#L15018-L15019`_


### `expr.writ.outer-at-prefix` — Writ literal outer `@` prefix

Writ (data) literals in expression position are introduced with a leading `@`: `@null`, `@true`/`@false`, `@INT`, `@-INT`, `@FLOAT`, `@"str"`, `@{ ... }` (map), `@[ ... ]` (array).

**Divergence.** Logos-specific Writ data-literal syntax; no Rust equivalent.

_Source: `src/compiler/sema_render.cpp#L463-L509`_


### `expr.writ.sdn-literal` — Writ SDN literals

Writ structured-data literals use the `@` sigil: `@{k:v,…}` map, `@[v,…]` array, `@"s"` string, `@42`/`@-1` int, `@<float>` float, `@true`/`@false` bool, `@null`. Typed forms `@<Elem>[…]` (dense array) and `@<K,V>{…}` / `@<K>{…}` (typed map). Comprehension forms `@[expr for x in iter (if p)?]` and `@{k:v for …}`. Only the outermost literal needs the `@` sigil; inner values are plain.

**Divergence.** Logos addition: Writ self-describing data-notation literals.

_Source: `tools/peg_gen/grammars/logos.peg#L2887-L2923`_


### `expr.writ.string-escapes` — Writ string literal: quote stripping and escapes

A Writ string literal has surrounding double-quotes stripped and recognizes escape sequences \n, \t, \r, \\, \", \0; an unrecognized escape `\x` is kept literally as backslash followed by x.

**Divergence.** Logos addition (Writ literals); escape set is a fixed subset.

_Source: `src/compiler/sema_expr.cpp#L15062-L15086`_


### `expr.writ.type-literal` — Writ type-literal <type:T>

A Writ value `<type:T>` embeds a Logos type T as a first-class value. T is resolved as a type (primitives, structs, in-scope type-params, and generic instantiations like Vec<u8> all permitted). The value carries (kind, type-uid, canonical-name) where the name is the canonical printed form (e.g. "Vec<u8>") and serves as the value's identity label.

**Divergence.** Logos addition: Writ first-class type values have no Rust equivalent.

_Source: `src/compiler/sema_expr.cpp#L14937-L14979`_


### `expr.writ.type-literal-unknown-bare` — Bare type-name in <type:T> must be a known type or in-scope type-param

When `<type:T>` names a bare type identifier that is neither a resolvable known type nor an in-scope type-param, it is a compile error; the diagnostic directs the user to declare T as a type-param of the enclosing const (`pub const X<T>: WritStatic = ...`) or use a concrete type.

**Divergence.** Logos addition (Writ type literals).

_Source: `src/compiler/sema_expr.cpp#L14954-L14966`_


### `expr.writ.typed-array-elem-types` — Typed Writ array element types

A typed Writ array `@<E>[...]` requires E to be one of I8, U8, I16, U16, I32, U32, I64, U64, F32, F64; any other element type is a compile error.

**Divergence.** Logos addition (Writ literals).

_Source: `src/compiler/sema_expr.cpp#L15145-L15168`_


### `expr.writ.typed-array-i32-bounds` — @<I32> array element range check

Each integer element of an `@<I32>[...]` typed array is bounds-checked at compile time to the i32 range [-2147483648, 2147483647]; out-of-range values are a compile error.

**Divergence.** Logos addition (Writ literals).

_Source: `src/compiler/sema_expr.cpp#L15190-L15203`_


### `expr.writ.typed-array-no-captures` — Typed Writ arrays reject $-captures

Within a typed Writ array `@<E>[...]`, a $-capture element ($ident or $expr) is a compile error because typed arrays store raw element values rather than AnyVal; an untyped `@[...]` literal must be used instead.

**Divergence.** Logos addition (Writ literals/captures).

_Source: `src/compiler/sema_expr.cpp#L15174-L15187`_


### `expr.writ.typed-map-key-discipline` — Typed integer-map key discipline

In a typed integer-keyed Writ map, a string key is a compile error (integer maps require integer keys); integer keys are negated when marked negative, and are bounds/sign-checked per key type: I32 to [-2^31, 2^31-1], U32 to [0, 2^32-1], U64 to non-negative.

**Divergence.** Logos addition (Writ literals).

_Source: `src/compiler/sema_expr.cpp#L15255-L15311`_


### `expr.writ.typed-map-types` — Typed Writ map key/value types

A typed Writ map `@<K>{...}` or `@<K,V>{...}` requires K ∈ {I32, U32, I64, U64, Varchar} and, if V is given, V == AnyVal; any other key or value type is a compile error. Varchar keys produce the same representation as the untyped object map.

**Divergence.** Logos addition (Writ literals).

_Source: `src/compiler/sema_expr.cpp#L15209-L15252`_


## Writ literals

### `expr.writ-lit.capture-context-save-restore` — Nested @-literals do not clobber the outer capture context

Lowering an @-literal establishes a fresh capture context for the duration of the literal and restores the prior context afterward, so a static @-literal nested inside a `${expr}` capture does not disturb outer `$`-captures.

_Source: `src/compiler/sema_expr.cpp#L15407-L15421`_


### `expr.writ-lit.int-small-inline-else-boxed` — Writ literal integer encoding: i24-inline vs boxed i64

In a Writ SDN literal, an integer in [-2^23, 2^23-1] is encoded inline as a 24-bit value; any integer outside that range is boxed as a 64-bit value.

_Source: `src/compiler/mlir_gen_expr.cpp#L5831-L5836`_


### `expr.writ-lit.result-type` — @-literal result type depends on presence of captures

An @-literal with no captures has type `WritStatic`; an @-literal with one or more `$`-captures has the return type of `writ_build_from_template` (an Rc<Writ>), which requires `use logos.lang.writ.tmpl;` to be in scope.

_Source: `src/compiler/sema_expr.cpp#L15422-L15444`_


### `expr.writ-lit.value-kinds` — Writ literal value kinds and their encodings

A Writ SDN literal value is one of: null; bool (0/1); int (see int encoding); float (boxed f64); string; array (homogeneous scalar arrays I8..F64 use a typed array, otherwise an object array); map (integer-keyed I32/U32/I64/U64 use a typed map, otherwise an object map keyed by string); type (a tiny map carrying kind/uid/name); or capture/PARAM (an inline placeholder bound to a value index, substituted at runtime).

**Divergence.** Logos addition (Writ SDN literals); no Rust equivalent.

**Uncertainty.** Writ is a Logos-specific data substrate (zoned SDN); these encodings are language-level data-literal semantics, not a Rust feature.

_Source: `src/compiler/mlir_gen_expr.cpp#L5759-L5882`, `src/compiler/mlir_gen_expr.cpp#L5820-L5882`_


## Writ capture

### `expr.writ-capture.capturable-types` — Set of types capturable in an @-literal

A value may be captured into an @-literal iff its type is one of: integer scalars i8/i16/i32/i64/u8/u16/u32/u64, bool (→ inline AnyVal); f64/f32/FloatLit (→ zone-allocated F64, type_code 31); AnyVal (passthrough) or StringView (→ varchar) struct types; `*const u8`/`*mut u8` (→ C-string varchar); or `str`/`&[u8]` slice of u8 (→ length-bearing varchar). All other types are rejected.

**Divergence.** Logos addition: @-literal (Writ) capture has no Rust analogue.

_Source: `src/compiler/sema_expr.cpp#L15325-L15350`, `src/compiler/sema_expr.cpp#L15360-L15367`, `src/compiler/sema_expr.cpp#L15387-L15394`_


### `expr.writ-capture.context-required` — $-capture requires a capturable @-literal context

A `$ident` or `${expr}` capture node is only valid lexically inside a capturable @-literal (Writ) context; using one elsewhere is an error.

_Source: `src/compiler/sema_expr.cpp#L15319-L15323`_


### `expr.writ-capture.expr-no-dedup` — ${expr} captures are never deduplicated

A `${expr}` capture (WRIT_CAP_EXPR) lowers its inner expression and always allocates a fresh capture value index (no deduplication, since the expression may have side effects).

_Source: `src/compiler/sema_expr.cpp#L15381-L15399`_


### `expr.writ-capture.ident-dedup` — Identical $ident captures share one value slot

Two `$ident` captures of the same identifier name reuse the same capture value index (deduplicated), while each occurrence consumes a distinct parameter slot.

_Source: `src/compiler/sema_expr.cpp#L15368-L15380`_


### `expr.writ-capture.ident-lookup` — $ident capture resolves a variable by name

A `$ident` capture (WRIT_CAP_IDENT) resolves `ident` against the enclosing scope; an unknown variable is an error.

_Source: `src/compiler/sema_expr.cpp#L15352-L15359`_


## Writ comprehensions

### `expr.writ-comp.guard-must-be-bool` — Writ comprehension guard must be bool

In a writ list/map comprehension the `guard` expression must have type `bool`; any other type is rejected (errors on Error type are swallowed to avoid cascades).

_Source: `src/compiler/sema_expr.cpp#L11158-L11172`, `src/compiler/sema_expr.cpp#L11305-L11318`_


## Writ list comprehensions

### `expr.writ-list-comp.desugar` — Writ list comprehension desugars to a Writ array builder loop

A writ list comprehension `@[value for x in iter (if guard)?]` desugars to a block that binds `let mut c = writ_list_comp_new(cap_hint)` (yielding the builder's return type, e.g. Rc<Writ>), iterates `x` over `iter`, coerces `value` to AnyVal, (optionally gated by `guard`) calls `writ_list_comp_push(&c, value)`, and evaluates to `c`. cap_hint = arr_size*8+128 for arrays of known size, else 128.

**Divergence.** Logos-specific Writ data-substrate sugar; no Rust equivalent.

_Source: `src/compiler/sema_expr.cpp#L11098-L11226`_


### `expr.writ-list-comp.requires-builder-import` — Writ list comprehension requires comp_builder import

A writ list comprehension is ill-formed unless arity-1 `writ_list_comp_new` and arity-2 `writ_list_comp_push` are visible (via `use logos.lang.writ.comp_builder;`).

**Divergence.** Logos-specific.

_Source: `src/compiler/sema_expr.cpp#L11125-L11135`_


## Writ map comprehensions

### `expr.writ-map-comp.desugar` — Writ map comprehension desugars to a Writ object-map builder loop

A writ map comprehension `@{key: value for x in iter (if guard)?}` desugars to a block that binds `let mut c = writ_map_comp_new(cap_hint, slot_hint)`, iterates `x` over `iter`, coerces `value` to AnyVal, (optionally gated by `guard`) calls `writ_map_comp_put(&c, key, value)`, and evaluates to `c`. slot_hint = arr_size (else 64); cap_hint = arr_size*48+256 (else 4096).

**Divergence.** Logos-specific Writ sugar; no Rust equivalent.

_Source: `src/compiler/sema_expr.cpp#L11231-L11375`_


### `expr.writ-map-comp.key-must-be-str` — Writ map comprehension key must be str

In a writ map comprehension v1 the `key` expression must have type `str` (a `&[u8]` slice with u8 element); any other key type is rejected.

**Divergence.** Logos-specific (v1 limitation: string keys only).

_Source: `src/compiler/sema_expr.cpp#L11285-L11296`_


### `expr.writ-map-comp.requires-builder-import` — Writ map comprehension requires comp_builder import

A writ map comprehension is ill-formed unless arity-2 `writ_map_comp_new` and arity-3 `writ_map_comp_put` are visible (via `use logos.lang.writ.comp_builder;`).

**Divergence.** Logos-specific.

_Source: `src/compiler/sema_expr.cpp#L11258-L11268`_


<a id="intrinsic-domain"></a>
# `intrinsic` — Intrinsics

## sizeof

### `intrinsic.sizeof.byte-size` — sizeof yields byte size

`sizeof::<T>()` requires exactly one type argument and yields `i64` = byte size of T.

**Divergence.** Logos spelling of size_of; result is i64 (Rust mem::size_of -> usize).

_Source: `src/compiler/sema_expr.cpp#L5703-L5716`_


### `intrinsic.sizeof.unified-layout-size` — sizeof::<T>() yields the padded layout size

`sizeof::<T>()` evaluates to a 64-bit compile-time constant equal to the type's full size including inter-field and trailing alignment padding (e.g. `{i32,i64}` => 16, not 12), drawn from the single unified layout used by all other size queries.

```logos
sizeof::<(i32, i64)>() == 16
```

_Source: `src/compiler/mlir_gen_expr.cpp#L5398-L5405`_


## sizeof (variadic pack)

### `intrinsic.sizeof-pack.length-of-type-pack` — sizeof...(T) yields pack length

`sizeof...(T)` is a value-position expression yielding the length of the type pack `T` as a `u64`.

_Source: `tools/peg_gen/grammars/logos.peg#L271`_


## alignof

### `intrinsic.alignof.unified-layout-align` — alignof::<T>() yields layout alignment, min 1

`alignof::<T>()` evaluates to a 64-bit compile-time constant equal to the type's alignment from the unified layout; if the layout reports alignment 0 the result is 1.

_Source: `src/compiler/mlir_gen_expr.cpp#L5408-L5412`_


## align-of

### `intrinsic.align-of.alignment` — align_of yields alignment

`align_of::<T>()` requires exactly one type argument and yields `i64` = alignment of T.

**Divergence.** Result is i64 (Rust mem::align_of -> usize).

_Source: `src/compiler/sema_expr.cpp#L5718-L5731`_


## offset-of

### `intrinsic.offset-of.compile-time-byte-offset` — offset_of! yields compile-time field offset

`offset_of!(Type, field)` evaluates at compile time to the byte offset of `field` within `Type`'s ABI layout, as an i64 constant.

_Source: `tools/peg_gen/grammars/logos.peg#L323`_


### `intrinsic.offset-of.form` — offset_of! intrinsic

`offset_of!(Type, field)` yields the byte offset of `field` within `Type`.

_Source: `tools/peg_gen/grammars/logos.peg#L2729-L2730`_


### `intrinsic.offset-of.generic-subst` — offset_of! substitutes the type's generic args

When the struct is generic, the concrete type arguments of `T` are substituted into the field types before computing sizes/alignments, so `offset_of!` reflects the layout of the concrete instantiation.

_Source: `src/compiler/sema_expr.cpp#L17649-L17659`_


### `intrinsic.offset-of.struct-only` — offset_of! requires a struct type

The type argument of `offset_of!` must resolve to a struct or zoned-struct type; otherwise it is a compile error. The named struct must be known.

_Source: `src/compiler/sema_expr.cpp#L17635-L17648`_


### `intrinsic.offset-of.syntax` — offset_of! signature

`offset_of!(Type, field)` requires both a type argument and a field name; either missing is a compile error.

_Source: `src/compiler/sema_expr.cpp#L17630-L17634`_


### `intrinsic.offset-of.value` — offset_of! yields a compile-time i64 byte offset

`offset_of!(T, f)` evaluates to an `i64` constant equal to the byte offset of field `f` within `T`'s layout, computed by sequentially laying out fields: each field is placed at the next position aligned up to its alignment, then advanced by its byte size. Result type is `i64`.

**Divergence.** Rust's offset_of! yields usize; Logos yields i64.

_Source: `src/compiler/sema_expr.cpp#L17657-L17681`_


## bits

### `intrinsic.bits.count-ops-return-u32` — Bit-count intrinsics return u32

`popcount_u64`, `leading_zeros_u64`, `trailing_zeros_u64` take a u64 and return u32 (the count is truncated to 32 bits). `bswap_u64` and `bitreverse_u64` take and return u64.

_Source: `src/compiler/mlir_gen_expr.cpp#L2235-L2265`_


### `intrinsic.bits.ctlz-cttz-zero-defined` — Leading/trailing-zero count is defined at zero

`leading_zeros_u64`/`trailing_zeros_u64` are defined for a zero input (no poison): a zero operand yields the bit width.

_Source: `src/compiler/mlir_gen_expr.cpp#L2248-L2253`_


### `intrinsic.bits.u64-bit-ops` — u64 bitwise intrinsics

`popcount_u64`, `leading_zeros_u64`, `trailing_zeros_u64` each take 1 u64 argument and return u32; `bswap_u64`, `bitreverse_u64` each take 1 u64 argument and return u64. Wrong arity is an error. (Lower to the corresponding LLVM intrinsics; ctlz/cttz are non-poison at zero.)

**Divergence.** Logos addition: explicit free-function bit-op intrinsics.

_Source: `src/compiler/sema_expr.cpp#L3186-L3204`_


## Pointer arithmetic

### `intrinsic.ptr-arith.element-vs-byte-scaling` — Pointer arithmetic scales by pointee for Add/Sub, by byte for ByteAdd/ByteSub

Pointer arithmetic offsets the base pointer by `offset` elements (each step = sizeof(pointee)) for Add/Sub; for ByteAdd/ByteSub the offset is in bytes (pointee treated as i8). The offset operand is normalized to a 64-bit integer. Sub and ByteSub negate the offset.

_Source: `src/compiler/mlir_gen_expr.cpp#L5414-L5453`_


## Pointer difference

### `intrinsic.ptr-diff.byte-and-element` — Pointer difference: raw byte distance or element count

Pointer difference computes `(usize)lhs - (usize)rhs`; when by-byte it is that raw byte distance, otherwise it is the signed quotient `byte_distance / sizeof(pointee)` giving the element count between the two pointers.

_Source: `src/compiler/mlir_gen_expr.cpp#L5456-L5486`_


## Wrapping arithmetic

### `intrinsic.wrapping.silent-twos-complement` — wrapping_add/sub/mul opt out of overflow trapping

`wrapping_add`, `wrapping_sub`, `wrapping_mul` perform two's-complement add/sub/mul that wraps silently and explicitly opts out of the runtime overflow trap applied to `+`/`-`/`*`. Operands of differing integer width are zero-extended to the wider width before the operation.

_Source: `src/compiler/mlir_gen_expr.cpp#L1839-L1881`_


## Atomics

### `intrinsic.atomic.default-ordering-seqcst` — Non-ordered atomics are sequentially consistent

An atomic operation invoked through the non-`_ord` form has sequentially-consistent ordering. CAS/cas_weak use seq-cst for both success and failure orderings.

_Source: `src/compiler/mlir_gen_expr.cpp#L2001-L2004`, `src/compiler/mlir_gen_expr.cpp#L2088-L2102`, `src/compiler/mlir_gen_expr.cpp#L2118-L2133`_


### `intrinsic.atomic.nonliteral-ordering-fallback` — Non-literal ordering observably over-synchronizes

When the `Ordering` argument of an `_ord` atomic is not a compile-time `Ordering` enum literal, the operation behaves at least as strongly as the requested ordering (a stronger ordering is always sound). Observable behavior is never weaker than the dynamic argument requests.

**Uncertainty.** Strength-monotone soundness is the stated semantic; exact runtime ordering for a non-literal arg is a target-dependent implementation choice (release/seq_cst) and not language-normative.

_Source: `src/compiler/mlir_gen_expr.cpp#L2025-L2077`, `src/compiler/mlir_gen_expr.cpp#L1969-L1976`_


### `intrinsic.atomic.ordering-enum-layout` — Ordering enum discriminant layout

The `Ordering` enum has fixed discriminants: Relaxed=0, Acquire=1, Release=2, AcqRel=3, SeqCst=4. The `_ord` atomic variants take an `Ordering` value as the trailing argument(s) which selects the memory ordering of the operation; for cas/cas_weak the two trailing args are (success, failure) orderings.

_Source: `src/compiler/mlir_gen_expr.cpp#L1983-L2000`, `src/compiler/mlir_gen_expr.cpp#L2135-L2154`, `src/compiler/mlir_gen_expr.cpp#L2149-L2154`_


### `intrinsic.atomic.primitive-set` — Atomic intrinsic family

The language exposes atomic primitives over 32- and 64-bit integer cells, each in a default and an `_ord` form: load{32,64}, store{32,64}, fetch_add{32,64}, cas{32,64} and cas_weak{32,64}, swap{32,64} (xchg), fetch_{or,and,xor,sub}{32,64}. load/store/fetch_add/cas/cas_weak/swap/fetch_* on width W operate on iW values at a pointer; the result type matches the cell width (load, RMW return the cell value; store returns 0:i32; cas returns the success bit).

_Source: `src/compiler/mlir_gen_expr.cpp#L2118-L2189`, `src/compiler/mlir_gen_expr.cpp#L2001-L2117`_


## type-of

### `intrinsic.type-of.type-struct` — type_of constructs a Type reflection struct

`type_of::<T>()` requires exactly one type argument and yields a `Type` struct literal with fields {kind: u32 (from __type_kind_of__), name: &[u8] (from __type_name_of__), size: i64 (size_of T), align: i64 (align_of T), uid: u64 (type_uid of T)}. Each component is concretized at mono.

**Divergence.** Logos addition (type reflection).

_Source: `src/compiler/sema_expr.cpp#L5142-L5183`_


## type-code-of

### `intrinsic.type-code-of.compute` — type_code_of derivation for zoned structs

For a concrete ZonedStruct T, type_code_of(T) = an explicit `#[type_code=N]` annotation on T (keyed by `pkg::Name`) if present, else a hash derived as type_hash_56bit(type_hash_23(canonical)) of the package-qualified canonical name, with raw codes < 128 biased up by +128 (reserving 0..127).

_Source: `src/compiler/sema_expr.cpp#L4649-L4707`_


### `intrinsic.type-code-of.signature` — type_code_of arity and result type

`type_code_of::<T>()` requires exactly one type argument and evaluates to a `u64` type code.

**Divergence.** Logos addition (Writ/zoned reflection intrinsic).

_Source: `src/compiler/sema_expr.cpp#L4634-L4647`, `src/compiler/sema_expr.cpp#L4712`_


### `intrinsic.type-code-of.typevar-defer` — type_code_of defers on TypeVar-bearing arguments

If T is a bare TypeVar, or a generic ZonedStruct any of whose type-args is a TypeVar, `type_code_of::<T>()` is deferred to monomorphization so each concrete instantiation gets its own type code; non-zoned non-typevar types yield code 0.

_Source: `src/compiler/sema_expr.cpp#L4677-L4712`_


### `intrinsic.type-code-of.writ-code` — type_code_of yields the Writ type code

`type_code_of::<T>()` yields `u64`, the Writ type_code of a concrete datatype = SHA-256 of "package::Name" truncated to 56 bits, shifted to >= 128 if needed (codes 1-127 reserved for inline AnyVal). For non-datatype T it yields 0.

**Divergence.** Logos addition (Writ substrate).

_Source: `src/compiler/sema_expr.cpp#L5733-L5737`_


## type-uid

### `intrinsic.type-uid.nominal-u64` — type_uid is nominal identity

`type_uid::<T>()` requires one type argument and yields `u64`: a NOMINAL 64-bit type identity (hash of the canonical named type string), so distinct nominal types differ even at identical layout (unlike type_hash). It is the low 64 bits of the 128-bit type UID and equals the `.uid` field exposed by type_of.

**Divergence.** Logos addition.

**Related.** `intrinsic.type-uid-hi.high-half`, `intrinsic.type-hash.structural-u64`

_Source: `src/compiler/sema_expr.cpp#L5088-L5102`, `src/compiler/sema_expr.cpp#L5172-L5174`_


## type-uid-hi

### `intrinsic.type-uid-hi.high-half` — type_uid_hi is the high half of the 128-bit UID

`type_uid_hi::<T>()` requires one type argument and yields `u64`, the HIGH 64 bits of the 128-bit nominal type UID; together with type_uid (low half) they form a 128-bit TypeId.

**Divergence.** Logos addition.

**Related.** `intrinsic.type-uid.nominal-u64`

_Source: `src/compiler/sema_expr.cpp#L5103-L5115`_


## type-hash

### `intrinsic.type-hash.structural-u64` — type_hash is layout-structural

`type_hash::<T>()` requires one type argument and yields `u64`: a structural FNV-1a-64 hash of T's layout — primitives map to fixed codes; struct/tuple/array/ptr hash a tag plus the recursive hashes of constituents, with NO struct/field names. Two structurally identical layouts hash equal; generic instances hash through their substituted args (Foo<i32> != Foo<u32>).

**Divergence.** Logos addition.

**Related.** `intrinsic.type-uid.nominal-u64`

_Source: `src/compiler/sema_expr.cpp#L5073-L5087`_


## type-refs-of

### `intrinsic.type-refs-of.pack-array` — type_refs_of reflects a type pack

`type_refs_of::<T...>()` yields `[Type; N]` with one Type value per pack member, substituted after pack expansion at mono. When the pack reduces to a single type-variable pack, the placeholder array carries a pack-size marker so let-bound/return types lift to the concrete `[Type; N]` automatically.

**Divergence.** Logos addition.

_Source: `src/compiler/sema_expr.cpp#L5670-L5701`_


## typelist

### `intrinsic.typelist.probe-family` — typelist O(1) probes over a type pack

Over L's type-pack (L.type_args()), one type argument required: `typelist_len::<L>() -> i64`; `typelist_head::<L>() -> Type` (error if pack empty); `typelist_nth::<L>(i) -> Type` requiring exactly one i64 index arg (out-of-range = error); `typelist_tail::<L>() -> [Type; N-1]`. Substituted after L is concrete.

**Divergence.** Logos addition.

_Source: `src/compiler/sema_expr.cpp#L5393-L5457`_


## is-same

### `intrinsic.is-same.two-type-args` — is_same arity and result

`is_same::<T1, T2>()` requires exactly two type arguments and yields `bool`; structural/identity equality of T1 and T2 is resolved post-substitution at mono. Wrong arity is a compile error.

**Divergence.** Logos addition.

_Source: `src/compiler/sema_expr.cpp#L5018-L5026`_


## is-kind

### `intrinsic.is-kind.predicate-family` — Type-kind predicate family

The predicates is_ptr / is_ref / is_mut_ref / is_struct / is_zoned / is_enum / is_tuple / is_slice / is_array / is_integer / is_signed / is_unsigned / is_float / is_bool / is_primitive each take exactly one type argument and yield `bool`, resolved against the substituted T at mono. Wrong arity is a compile error.

**Divergence.** Logos addition.

_Source: `src/compiler/sema_expr.cpp#L5127-L5140`_


## is-data-plain-of

### `intrinsic.is-data-plain-of.copyable-predicate` — is_data_plain_of predicates DataPlain layout

`is_data_plain_of::<T>()` yields `bool`: true iff T is a DataPlain datatype (no relative-pointer fields). Array wrappers are stripped ([D; N] checks D). Non-datatype types (scalars, ordinary structs) always yield true; a generic (type-arg-bearing) zoned datatype yields false (conservative); an unknown datatype defaults to true.

**Divergence.** Logos addition (zoned/Writ datatypes).

_Source: `src/compiler/sema_expr.cpp#L5739-L5779`_


## Reflection

### `intrinsic.reflect.apply-generic` — apply_generic(g: Type, args) instantiates a generic constructor

`apply_generic(g, args)` (callee __apply_generic__) instantiates the generic constructor described by Type value `g` (produced by generic_of) with `args`, routing through the same struct allocation as type_apply. The template name is recovered from g's `Type` struct-literal `name` field (a string literal); both operands are chased through VarRef let-bindings (max 8 hops).

**Divergence.** A6 (Logos-only type-level composition intrinsic)

**Uncertainty.** Slice ends mid-statement at L2119; only name recovery from g is visible in-unit, the remaining instantiation logic continues past the unit boundary.

**Related.** `intrinsic.reflect.type-apply`

_Source: `src/compiler/mono_clone.cpp#L2085-L2119`_


### `intrinsic.reflect.args-of` — args_of::<T>() yields T's generic type arguments

args_of::<T>() produces a [Type; N] descriptor array of the generic type-arguments of T (in order); for a non-generic T the array is empty.

**Related.** `intrinsic.reflect.type-descriptor-array`

_Source: `src/compiler/mono_clone.cpp#L2780-L2783`_


### `intrinsic.reflect.datatype` — reflect on a concrete datatype

`reflect::<T>()` requires exactly one type argument. A bare TypeVar T is deferred to mono. Otherwise T must be a concrete (non-generic, no type-args) ZonedStruct datatype; it registers a reflect request for `pkg::T` and yields a `WritStatic`.

_Source: `src/compiler/sema_expr.cpp#L4877-L4899`_


### `intrinsic.reflect.deferred-fold-after-subst` — Type-introspection intrinsics fold after substitution at mono

Type-trait/type-introspection intrinsics taking type-args are not evaluated at sema; each lowers to a magic `__<name>__` call carrying its type-args, and is folded to a concrete value only after monomorphization substitutes those type-args. Inside a generic body where T is still a type variable the call is preserved (never frozen to 'TypeVar' semantics).

**Divergence.** Logos addition: compile-time type reflection intrinsics (no Rust equivalent).

_Source: `src/compiler/sema_expr.cpp#L5014-L5017`, `src/compiler/sema_expr.cpp#L5079-L5087`, `src/compiler/sema_expr.cpp#L5142-L5146`_


### `intrinsic.reflect.field-count-of` — field_count_of::<T>() yields struct field count

field_count_of::<T>() evaluates at compile time to an i64 literal equal to the number of declared fields of T when T is a struct (or zoned struct) type; for any non-struct or unresolvable T it is 0. The struct template is matched by name, preferring a package-qualified match (T.pkg) and falling back to name-only.

_Source: `src/compiler/mono_clone.cpp#L2703-L2730`_


### `intrinsic.reflect.field-names-of` — field_names_of::<T>() yields array of field-name strings

field_names_of::<T>() evaluates at compile time to an array [&str; N] whose elements are the declared field names of struct T in declaration order; for non-struct or unresolvable T it is the empty array. Struct lookup prefers a package-qualified match and falls back to name-only.

_Source: `src/compiler/mono_clone.cpp#L2733-L2768`_


### `intrinsic.reflect.field-types-of` — field_types_of::<T>() yields substituted struct field types

field_types_of::<T>() produces a [Type; N] descriptor array of the field types of struct (or zoned struct) T in declaration order, with the struct template's type parameters substituted by T's actual type arguments (positional binding of template params to T.type_args); empty for non-struct or unresolvable T.

**Related.** `intrinsic.reflect.type-descriptor-array`

_Source: `src/compiler/mono_clone.cpp#L2797-L2827`_


### `intrinsic.reflect.has-trait-of` — has_trait_of::<Trait>(t: Type) -> bool folds at monomorphization

`has_trait_of::<Trait>(t)` (callee __has_trait_of__) folds to a `bool` literal during monomorphization. The concrete type T is recovered from t's `Type` struct-literal `uid` field, which must be a `__type_uid_of__::<T>()` call (after chasing VarRef through let-bindings, max 8 hops); T is substituted with the active type substitution. The result is `true` iff T (named by its concrete struct name, enum name, or type_str, truncated at any `$G` generic-suffix) has an impl of Trait, computed recursively over concrete and blanket impls.

**Divergence.** A6 (Logos-only metaprog/reflection intrinsic; no Rust equivalent)

_Source: `src/compiler/mono_clone.cpp#L1588-L1652`_


### `intrinsic.reflect.reify-type` — reify_type(t: Type) -> Type recovers a source TypeRef and re-emits Type

`reify_type(t)` (callee __reify_type__) recovers a concrete TypeRef from a direct Type-producer argument and re-emits a fresh `Type` struct literal. Supported argument shapes (after chasing VarRef through let-bindings, max 8 hops): (1) a `Type` struct literal whose `uid` field is `__type_uid_of__::<T>()` → T substituted; (2) a `__typelist_head__`/`__typelist_nth__` call → the indexed pack element. A missing argument is fatal; any other (unsupported) shape is a fatal compile-time error.

**Divergence.** A6 (Logos-only reflection intrinsic)

**Related.** `intrinsic.reflect.type-struct-shape`

_Source: `src/compiler/mono_clone.cpp#L1741-L1834`_


### `intrinsic.reflect.tuple-count-of` — tuple_count_of::<T>() yields tuple arity

tuple_count_of::<T>() evaluates at compile time to an i64 literal equal to the number of element types of T when T is a tuple type, and to 0 for any non-tuple T.

_Source: `src/compiler/mono_clone.cpp#L2685-L2698`_


### `intrinsic.reflect.tuple-elems-of` — tuple_elems_of::<T>() yields tuple element types

tuple_elems_of::<T>() produces a [Type; N] descriptor array of the element types of T when T is a tuple; empty otherwise.

**Related.** `intrinsic.reflect.type-descriptor-array`

_Source: `src/compiler/mono_clone.cpp#L2790-L2796`_


### `intrinsic.reflect.type-apply` — type_apply(name, args: [Type;N]) -> Type instantiates a struct template

`type_apply(name, args)` (callee __type_apply__) instantiates the struct template named `name` (a string literal; surrounding quote chars stripped) with the TypeRefs recovered from `args` and folds to a `Type` value for the instantiation. `name` must be a string literal (else fatal). The instantiated type's `pkg_name` is taken from the matching template in the program's struct table. Each element TypeRef is recovered from the same producer shapes reify_type accepts (Type struct-lit uid call, or typelist head/nth); a non-recognized producer element is a fatal compile-time error.

**Divergence.** A6 (Logos-only type-level composition intrinsic)

**Related.** `intrinsic.reflect.type-struct-shape`, `intrinsic.reflect.reify-type`

_Source: `src/compiler/mono_clone.cpp#L1841-L2083`_


### `intrinsic.reflect.type-apply-pack-splice` — type_apply pack-splice fast path over Type-array intrinsics

When the `args` operand of type_apply is itself a Type-array producer intrinsic, its element TypeRefs are spliced directly into the template instantiation instead of requiring an array-literal shape: `type_refs_of` contributes its (substituted) type-args as the pack; `args_of::<T>` contributes T's type-args; `typelist_tail::<T>` contributes T's pack minus its first element; `tuple_elems_of::<T>` contributes T's tuple element types (only when T is a Tuple). Otherwise `args` must be an array literal (else fatal).

**Divergence.** A6 (Logos-only variadic type-pack splice)

**Related.** `intrinsic.reflect.type-apply`

_Source: `src/compiler/mono_clone.cpp#L1878-L1972`_


### `intrinsic.reflect.type-descriptor-array` — type-reflection intrinsics produce [Type; N] descriptors

args_of::<T>(), type_refs_of, tuple_elems_of, typelist_tail, and field_types_of each evaluate at compile time to an array [Type; N] of struct literals. Each Type element has fields {kind: u32 = the type's kind tag, name: &str = the type's printed name, size: i64 = size_of, align: i64 = align_of, uid: u64 = a canonical 64-bit type hash}. N and the per-element source types are determined per-intrinsic (see related rules).

_Source: `src/compiler/mono_clone.cpp#L2774-L2869`_


### `intrinsic.reflect.type-struct-shape` — Reflected Type value layout {kind,name,size,align,uid}

A reflected `Type` value materialized by a folding reflection intrinsic is the struct `Type` with fields `kind: u32` = the type's Kind tag, `name: &[u8]` = type_str(T), `size: i64` = size_of(T), `align: i64` = align_of(T), and `uid: u64` = a canonical 64-bit type hash (type_hash_64bit ∘ type_hash_23 ∘ type_id_canon). The compiler records uid→T so the value can be reified back to T.

**Divergence.** A6 (Logos-only reflection value)

**Related.** `intrinsic.reflect.typelist-head-nth`, `intrinsic.reflect.reify-type`, `intrinsic.reflect.type-apply`

_Source: `src/compiler/mono_clone.cpp#L1716-L1730`, `src/compiler/mono_clone.cpp#L1810-L1833`, `src/compiler/mono_clone.cpp#L2074-L2082`_


### `intrinsic.reflect.typeinfo-rodata` — reflect requests TypeInfo rodata

`reflect::<T>() -> WritStatic` is a compile-time request that registers T for reflection so a TypeInfo global is emitted; the expression resolves to the address of that emitted TypeInfo rodata.

**Divergence.** Logos addition.

_Source: `src/compiler/sema_expr.cpp#L5781-L5784`_


### `intrinsic.reflect.typelist-head-nth` — typelist_head/nth::<L>(i) -> Type folds to a Type struct literal

`typelist_head::<L>()` and `typelist_nth::<L>(i)` (callees __typelist_head__/__typelist_nth__) fold to a single `Type { kind, name, size, align, uid }` struct literal describing element idx of L's type-arg pack: head uses idx=0; nth requires `i` to be a literal int. A missing type argument, a non-literal nth index, or an index outside [0, pack.size()) is a fatal compile-time error.

**Divergence.** A6 (Logos-only type-level pack intrinsic)

_Source: `src/compiler/mono_clone.cpp#L1672-L1731`_


### `intrinsic.reflect.typelist-len` — typelist_len::<L>() -> i64 folds to the pack arity

`typelist_len::<L>()` (callee __typelist_len__) folds to an `i64` literal equal to the number of type arguments in L's type-argument pack (0 when L is absent). O(1) compile-time probe; the canonical L is `TypeList<T...>`.

**Divergence.** A6 (Logos-only type-level pack intrinsic)

_Source: `src/compiler/mono_clone.cpp#L1657-L1668`_


### `intrinsic.reflect.typelist-tail` — typelist_tail::<T>() drops the first type argument

typelist_tail::<T>() produces a [Type; N] descriptor array of T's generic type-arguments excluding the first (i.e. the tail beginning at index 1); empty when T has fewer than two type arguments.

**Related.** `intrinsic.reflect.type-descriptor-array`

_Source: `src/compiler/mono_clone.cpp#L2784-L2789`_


### `intrinsic.reflect.writ-trait` — reflect on a writ trait registers a reflect request

`reflect::<Tr>()` where Tr names a writ trait (is_writ) registers a reflect request for `pkg::Tr` and evaluates to a `WritStatic` reflection of that trait/datatype.

**Divergence.** Logos addition (Writ reflection intrinsic).

_Source: `src/compiler/sema_expr.cpp#L4851-L4876`_


## field-count-of

### `intrinsic.field-count-of.struct-field-count` — field_count_of yields struct field count

`field_count_of::<T>()` requires one type argument and yields `i64` = number of declared fields of struct T (0 for non-struct or unknown-struct T).

**Divergence.** Logos addition.

_Source: `src/compiler/sema_expr.cpp#L5562-L5582`_


## field-reflect

### `intrinsic.field-reflect.types-and-names` — field_types_of / field_names_of reflect struct fields

`field_types_of::<T>()` yields `[Type; N]` of T's field types and `field_names_of::<T>()` yields `[&[u8]; N]` of T's field names; each requires one type argument; non-struct T yields empty arrays. At mono field types are substituted via the SubstMap built from the struct template's type_params -> T.type_args().

**Divergence.** Logos addition.

_Source: `src/compiler/sema_expr.cpp#L5584-L5613`_


## variant-reflect

### `intrinsic.variant-reflect.enum-family` — Enum-variant decompose intrinsics

Each requires one type argument E: `variant_count_of::<E>() -> i64`; `variant_names_of::<E>() -> [&[u8]; N]`; `variant_payload_counts_of::<E>() -> [i64; N]`; `variant_payload_types_flat_of::<E>() -> [Type; M]`. For non-enum or unknown E all yield 0 / empty arrays.

**Divergence.** Logos addition.

_Source: `src/compiler/sema_expr.cpp#L5629-L5668`_


## args-of

### `intrinsic.args-of.type-arg-array` — args_of yields generic type arguments

`args_of::<T>()` requires one type argument and yields `[Type; N]` listing T's generic type arguments; for non-generic T the result is `[Type; 0]`. The array length is fixed at mono once T is concrete.

**Divergence.** Logos addition.

**Related.** `intrinsic.args-count-of.arg-count`

_Source: `src/compiler/sema_expr.cpp#L5185-L5211`_


## args-count-of

### `intrinsic.args-count-of.arg-count` — args_count_of yields generic-arg count

`args_count_of::<T>()` requires one type argument and yields `i64` = number of T's generic type arguments (0 for primitive or non-generic struct).

**Divergence.** Logos addition.

**Related.** `intrinsic.args-of.type-arg-array`

_Source: `src/compiler/sema_expr.cpp#L5213-L5233`_


## generic-of

### `intrinsic.generic-of.signature` — generic_of requires a bare struct/enum name

`generic_of::<X>()` requires its single type-argument to be a bare named struct or enum (a TYPE_REF or GENERIC_INST with a NAME); the name must resolve to a declared struct or enum in the current program, otherwise a compile error.

**Divergence.** Logos addition (compile-time reflection intrinsic).

_Source: `src/compiler/sema_expr.cpp#L4517-L4551`_


### `intrinsic.generic-of.unapplied-ctor` — generic_of yields a handle for an unapplied generic constructor

`generic_of::<X>()` yields a Type-shaped value-handle for the unapplied generic constructor X (struct or enum) with kind=Generic, name=X, size=arity, and UID = FNV-1a of "generic:X".

**Divergence.** Logos addition.

_Source: `src/compiler/sema_expr.cpp#L5615-L5619`_


### `intrinsic.generic-of.value` — generic_of yields a Type descriptor

`generic_of::<X>()` evaluates to a `Type` struct literal with kind = Generic, name = X, size = X's type-parameter arity (count of declared type params), align = 0, and a uid = FNV-1a hash of "generic:" ++ X.

_Source: `src/compiler/sema_expr.cpp#L4552-L4573`_


## template-of

### `intrinsic.template-of.decl-handle` — template_of yields a Template handle to a declaration

`template_of::<X>()` resolves X at sema, locates the declaration item named X in the current AST root, and yields a `Template { raw: AnyVal { raw: <offset> } }` baking that declaration's arena offset as a u32 literal (same-AST scope).

**Divergence.** Logos addition.

_Source: `src/compiler/sema_expr.cpp#L5621-L5627`_


### `intrinsic.template-of.lowering` — template_of lowers to runtime AST-node anchoring

`template_of::<X>()` lowers to `template_of_at(off)` where `off` is the holder-relative AST node offset of the matching top-level item, producing a `Template` whose `raw` is anchored to the module-AST OView base at runtime.

_Source: `src/compiler/sema_expr.cpp#L4612-L4631`_


### `intrinsic.template-of.signature` — template_of requires a top-level item name in the current file

`template_of::<X>()` requires its single type-argument to be a bare named item; X must name a top-level declaration in the current source file, otherwise a compile error. It also requires `use logos.std.compiler.metaprog` (the `template_of_at` shim) to be in scope.

**Divergence.** Logos addition (metaprogramming intrinsic).

_Source: `src/compiler/sema_expr.cpp#L4576-L4632`_


## tuple-count-of

### `intrinsic.tuple-count-of.elem-count` — tuple_count_of yields tuple element count

`tuple_count_of::<T>()` requires one type argument and yields `i64` = number of elements in tuple T (0 for non-tuple T).

**Divergence.** Logos addition.

**Related.** `intrinsic.tuple-elems-of.elem-types`

_Source: `src/compiler/sema_expr.cpp#L5516-L5534`_


## tuple-elems-of

### `intrinsic.tuple-elems-of.elem-types` — tuple_elems_of yields tuple element types

`tuple_elems_of::<T>()` requires one type argument and yields `[Type; N]` of T's element types; empty array for non-tuple T.

**Divergence.** Logos addition.

**Related.** `intrinsic.tuple-count-of.elem-count`

_Source: `src/compiler/sema_expr.cpp#L5536-L5560`_


## tuple-all-eq

### `intrinsic.tuple-all-eq.chain-expand` — tuple_all_eq expands an element-wise eq chain

`tuple_all_eq::<T>(a, b)` expands to the conjunction `a.0.eq(&b.0) && ... && a.{N-1}.eq(&b.{N-1})`. If T is a concrete tuple the chain is expanded at sema; if any element is a type variable a `__tuple_all_eq__` placeholder is emitted and expanded at mono once T's arity is concrete.

**Divergence.** Logos addition (variadic-tuple support).

_Source: `src/compiler/sema_expr.cpp#L5459-L5471`_


### `intrinsic.tuple-all-eq.concrete-expansion` — tuple_all_eq concrete expansion via per-element eq

For a fully concrete tuple T = (T0,..,Tn-1), `tuple_all_eq::<T>(a,b)` expands to the `&&`-conjunction over i of `Ti::eq(&a.i, &b.i)`, where each `eq` impl is resolved by candidate lookup on `<Ti>__eq` requiring a 2-parameter signature `(&Ti, &Ti)`. If no `eq` impl exists for some element type, it is a compile error.

_Source: `src/compiler/sema_expr.cpp#L4469-L4514`_


### `intrinsic.tuple-all-eq.signature` — tuple_all_eq arity and tuple constraint

`tuple_all_eq::<T>(a, b)` requires exactly one type argument T which must be a tuple type, and exactly two value arguments; otherwise a compile error. Result type is `bool`. An empty tuple yields the constant `true`.

**Divergence.** Logos addition (variadic-tuple support intrinsic).

_Source: `src/compiler/sema_expr.cpp#L4413-L4451`_


### `intrinsic.tuple-all-eq.typevar-defer` — tuple_all_eq defers to mono on unbound tuple elements

If any element type of the tuple T is an unbound TypeVar, `tuple_all_eq::<T>(a,b)` is deferred to monomorphization as a `__tuple_all_eq__` call carrying T; otherwise it is expanded at sema time.

**Related.** `mono.subst.const-arg`

_Source: `src/compiler/sema_expr.cpp#L4452-L4468`_


## tuple-each-field-debug

### `intrinsic.tuple-each-field-debug.requires-tuple` — tuple_each_field_debug formats every tuple field

`tuple_each_field_debug::<T>(self, f)` requires one type argument that MUST be a tuple type (else compile error) and exactly two value arguments; result type is the enclosing function's return type. It Debug-formats every field of T into Formatter f, deferring to a `__tuple_each_field_debug__` placeholder expanded at mono.

**Divergence.** Logos addition.

_Source: `src/compiler/sema_expr.cpp#L5473-L5514`_


## has-trait

### `intrinsic.has-trait.t-trait-bool` — has_trait queries impl tables

`has_trait::<T, Trait>()` requires two type arguments and yields `bool`: whether concrete T implements Trait, resolved at mono against the same impl tables (concrete + recursive blanket lookup) that drive method dispatch. The second argument is read by its identifier name only (passed as a string literal arg), not resolved as a type. Missing T or empty Trait name is a compile error.

**Divergence.** Logos addition.

**Related.** `intrinsic.has-trait-of.type-method`

_Source: `src/compiler/sema_expr.cpp#L5235-L5270`_


## has-trait-of

### `intrinsic.has-trait-of.lowering` — has_trait_of dispatches to runtime helper with trait name

`has_trait_of::<Trait>(t)` lowers to a call `__has_trait_of__(name, t)` where `name` is the trait's identifier passed as a `[u8]` string literal; the trait is identified by name only.

**Uncertainty.** Trait identity is by bare name string; package-qualification semantics not enforced at this site.

_Source: `src/compiler/sema_expr.cpp#L4400-L4410`_


### `intrinsic.has-trait-of.signature` — has_trait_of arity and shape

`has_trait_of::<Trait>(t)` requires exactly one trait type-argument (a single named type in the turbofish) and exactly one value argument; violating either is a compile error. It evaluates to `bool`.

```logos
let b: bool = has_trait_of::<Display>(x);
```

**Divergence.** Logos addition (reflection intrinsic); no Rust equivalent.

_Source: `src/compiler/sema_expr.cpp#L4367-L4410`_


### `intrinsic.has-trait-of.type-method` — has_trait_of is the Type-method form of has_trait

`has_trait_of::<Trait>(t: Type) -> bool` recovers concrete T from the value t's Type.uid field and runs the same impl-table recursion as has_trait.

**Divergence.** Logos addition.

**Related.** `intrinsic.has-trait.t-trait-bool`

_Source: `src/compiler/sema_expr.cpp#L5272-L5276`_


## has-annotation

### `intrinsic.has-annotation.const-fold` — has_annotation is a compile-time annotation check

`has_annotation::<T, A>()` requires exactly two type arguments and const-folds to `bool`: true iff datatype T carries a user annotation of annotation-type A. A must be a known annotation datatype (else compile error); the check matches against T's declared annotation instances by fully-qualified or simple name.

**Divergence.** Logos addition (annotation metaprogramming).

_Source: `src/compiler/sema_expr.cpp#L5786-L5823`_


## get-annotation

### `intrinsic.get-annotation.option-result` — get_annotation yields the annotation instance as Option<A>

`get_annotation::<T, A>() -> Option<A>` const-folds to `Some(A{...})` if datatype T carries annotation A, else `None`.

**Divergence.** Logos addition.

**Related.** `intrinsic.has-annotation.const-fold`

_Source: `src/compiler/sema_expr.cpp#L5825-L5827`_


### `intrinsic.get-annotation.signature` — get_annotation arity and annotation-type constraint

`get_annotation::<T, A>()` requires exactly two type arguments; A must be a ZonedStruct that is an annotation type. `Option` must be in scope. Result type is `Option<A>`.

**Divergence.** Logos addition (compile-time annotation reflection intrinsic).

_Source: `src/compiler/sema_expr.cpp#L4901-L4938`_


### `intrinsic.get-annotation.value` — get_annotation materializes the annotation instance

`get_annotation::<T, A>()` returns `Option::None` if T carries no annotation of type A; otherwise `Option::Some(A{...})` where the A literal is reconstructed field-by-field from the stored annotation values (int/float/bool/string/enum/array kinds), matched by annotation fqn or bare name.

_Source: `src/compiler/sema_expr.cpp#L4942-L5010`_


## dyn intrinsics

### `intrinsic.dyn.from-parts` — dyn_from_parts assembles a fat dyn pointer

`dyn_from_parts::<Trait>(data, vtable)` assembles a fat trait-object pointer as the 16-byte {data, vtable} pair from the two raw half pointers, yielding `*mut dyn Trait`; the trait argument is irrelevant to layout (uniform).

_Source: `src/compiler/mlir_gen_expr.cpp#L1922-L1941`_


### `intrinsic.dyn.tagged-dispatch-tier-split` — tagged-trait dispatch splits at type_code 223 into table vs lookup

Dispatch through a `&tagged<TS> Trait` reads the object's type_code (i64) at its known offset, then for type_code < 223 (tier-1) indexes a static dispatch table by type_code, and for type_code >= 223 (tier-2) calls a tier-2 lookup function with the type_code, in both cases obtaining a function pointer through which the call is made indirectly with (obj_ptr, args...).

**Uncertainty.** The 223 threshold and dispatch sequence are described in a comment heading the next unit's function; the full mechanism is in gen_tagged_dispatch.

_Source: `src/compiler/mlir_gen_dyn.cpp#L1273-L1283`_


### `intrinsic.dyn.vtable-of` — vtable_of::<Trait, T> yields the static vtable address

`vtable_of::<Trait, T>()` returns `*const u8`, the address of the static vtable for `impl Trait for T`, with Trait given as a string argument and T as the first type argument.

_Source: `src/compiler/mlir_gen_expr.cpp#L1906-L1921`_


### `intrinsic.dyn.vtable-slot0-is-drop` — Trait-object vtable slot 0 is drop_in_place; supertrait vtables nested

A trait object's vtable carries the concrete type's drop_in_place at slot 0 (called for dynamic destruction) and includes super-vtable pointer slots for each supertrait, each pointing at the supertrait's vtable global.

**Uncertainty.** Slot-0 = drop is stated by the drop-sequence comments; exact remaining vtable slot ordering is not specified in this unit.

**Related.** `intrinsic.drop.owning-dyn-handle`

_Source: `src/compiler/mlir_gen_impl.hpp#L966-L975`, `src/compiler/mlir_gen_impl.hpp#L1101-L1104`_


## dyn-from-parts

### `intrinsic.dyn-from-parts.fat-trait-ptr` — dyn_from_parts builds a trait object from raw halves

`dyn_from_parts::<Trait>(data: *mut u8, vtable: *const u8) -> *mut dyn Trait` forms a fat {data, vtable} trait-object pointer. Exactly one trait type argument (its own type-args, if any, are carried so the produced object matches a parameterized `dyn Trait<...>` annotation, skipping lifetime/auto-trait bound sub-nodes) and exactly two value arguments are required. Trait must be a known, object-safe trait. The result is the bare canonical TraitObject (matching `*mut dyn`/`&dyn`), not a thin pointer.

**Divergence.** Logos addition.

**Related.** `intrinsic.vtable-of.static-vtable-addr`

_Source: `src/compiler/sema_expr.cpp#L5314-L5391`_


## vtable-of

### `intrinsic.vtable-of.static-vtable-addr` — vtable_of yields a static vtable address

`vtable_of::<Trait, T>() -> *const u8` yields the address of the static vtable for `impl Trait for T`. Trait is read by NAME (must be a known trait, else error); T is resolved as a type and substituted at mono. Missing trait name or type is a compile error; an unknown trait name is a compile error.

**Divergence.** Logos addition.

**Related.** `intrinsic.dyn-from-parts.fat-trait-ptr`

_Source: `src/compiler/sema_expr.cpp#L5278-L5312`_


## dst-from-raw-parts

### `intrinsic.dst-from-raw-parts.unsafe` — dst_from_raw_parts requires unsafe and a custom-DST struct

`dst_from_raw_parts::<S>(ptr, len)` (and `_mut`) requires unsafe context, exactly one type argument S that is a (Zoned)Struct whose last field resolves to `[T]` or `dyn Trait` (directly is_dst or via type-parameter substitution), and exactly two value arguments.

**Divergence.** Logos addition (custom-DST construction intrinsic).

_Source: `src/compiler/sema_expr.cpp#L4740-L4802`_


### `intrinsic.dst-from-raw-parts.value` — dst_from_raw_parts builds a fat DstRef

`dst_from_raw_parts::<S>(ptr, len)` produces a `DstRef` to S ({data, len} fat-pair, same ABI as a slice); the `_mut` callee sets the DstRef mut flag. The length argument is widened to i64. The DstRef carries S's type-args for later tail-element field access.

_Source: `src/compiler/sema_expr.cpp#L4803-L4812`_


## slice-from-raw

### `intrinsic.slice-from-raw.ptr-len` — slice_from_raw builds a slice fat pointer

`slice_from_raw::<T>(ptr: *const T, len: i64) -> &[T]` requires exactly one type argument and exactly two value arguments; it materialises a slice fat-pointer of element type T (uniform fat-pointer layout shared with str_from_raw). Wrong type-arg count or value-arg count is a compile error.

**Divergence.** Logos addition (unsafe raw-parts constructor).

_Source: `src/compiler/sema_expr.cpp#L5032-L5057`_


## Drop intrinsics

### `intrinsic.drop.box-dyn-frees-data` — Dropping `Box<dyn Trait>` runs drop_in_place then frees the single heap block

Dropping an owning `Box<dyn Trait>` calls drop_in_place(data) via the vtable, then frees the data pointer (the single heap block holding the concrete value).

_Source: `src/compiler/mlir_gen_stmt.cpp#L547-L605`_


### `intrinsic.drop.closure-env-drop-glue` — closure drop glue drops captures then frees heap env

A closure's drop glue takes the env pointer and drops each owned droppable capture at env field i+1 (field 0 reserved for the function pointer); under RFC-2229 narrowing the dropped type is the captured narrow field type when present, else the root capture type. If the env is heap-allocated (escaping closure), the env block is freed after the captures are dropped.

_Source: `src/compiler/mlir_gen_dyn.cpp#L1001-L1053`_


### `intrinsic.drop.drop-in-place-glue` — drop_in_place glue runs the concrete type's full drop

`drop_in_place(T)` is a function taking a pointer to a T that runs T's full recursive drop; for a Copy or drop-less type it is an emitted no-op. It is slot 0 of every vtable. Size/align drop slots distinct from Rust's are present (slots 1,2); no separate dealloc slot because deallocation = libc free.

_Source: `src/compiler/mlir_gen_dyn.cpp#L971-L998`_


### `intrinsic.drop.dyn-in-place` — Move-out drop of an unsized dyn tail runs vtable[0] only

Dropping the concrete payload behind a `&dyn` fat pair in place (the move-out drop of an unsized `dyn` tail) runs only vtable[0](data) (the concrete Drop) with NO free and NO refcount change; the underlying block is freed separately by the owner.

**Related.** `intrinsic.drop.owning-dyn-handle`

_Source: `src/compiler/mlir_gen_impl.hpp#L1112-L1118`_


### `intrinsic.drop.dyn-virtual-dispatch` — Dropping a `dyn` value calls the destructor via vtable slot 0 with null guard

Dropping a trait-object value loads its data and vtable pointers; if data is non-null it calls vtable slot 0 (drop_in_place) on the data pointer. A null (moved-from/zeroed) data pointer skips the call.

_Source: `src/compiler/mlir_gen_stmt.cpp#L514-L545`, `src/compiler/mlir_gen_stmt.cpp#L591-L597`_


### `intrinsic.drop.owner-drops-fields-after-user-drop` — Owner drop runs user Drop then drops fields; nested by-value self stops at user Drop

At the top level (owner semantics), after a value's user `impl Drop` runs, its fields/payload are ALSO dropped by the owner. A nested (non-top-level) drop calls only the user `impl Drop` and stops, because the by-value `self` consumes its own fields at the drop body's scope end.

**Related.** `intrinsic.drop.recursive-by-type`

_Source: `src/compiler/mlir_gen_impl.hpp#L1089-L1096`_


### `intrinsic.drop.owning-custom-dst` — Drop of owning Box<Foo> custom-DST drops prefix + tail then frees

Dropping an owning custom-DST `Box<Foo>` drops the droppable prefix fields plus the tail elements (runtime loop over the fat-pointer length carried in the {data, len} pair) and then frees the block.

**Related.** `layout.dst.slice-tail-ref-is-fat`

_Source: `src/compiler/mlir_gen_impl.hpp#L1108-L1111`_


### `intrinsic.drop.owning-dyn-handle` — Drop of owning Box<dyn> calls vtable[0], frees data, frees handle

Dropping an owning `Box<dyn Trait>` whose binding storage is the 8-byte heap handle to a 16-byte {data, vtable} fat pair runs (null-guarded): load data and vtable; call vtable[0](data) (drop_in_place: concrete destructor + owned fields); free(data); free(handle).

**Related.** `layout.dyn.box-dyn-collapses-to-trait-object`, `intrinsic.drop.dyn-in-place`

_Source: `src/compiler/mlir_gen_impl.hpp#L1099-L1104`_


### `intrinsic.drop.owning-slice` — Drop of owning Box<[T]> drops each element then frees the buffer

Dropping an owning `Box<[T]>` fat slice (value {data, len}) drops each element via a runtime loop over `len` (only when T is droppable) and then frees the heap buffer.

_Source: `src/compiler/mlir_gen_impl.hpp#L1105-L1107`_


### `intrinsic.drop.rc-arc-dyn-refcount` — Dropping `Rc<dyn>`/`Arc<dyn>` decrements strong and frees the RcInner at zero

Dropping an owning `Rc<dyn>`/`Arc<dyn>` recovers the RcInner block start as data − round_up(8, align(T)) (align read from vtable slot 2), decrements the strong count (Arc: seq-cst atomic; Rc: plain load/store) at offset 0, and only when it reaches zero calls drop_in_place(data) and frees the whole RcInner block.

**Uncertainty.** RcInner layout {strong i32, weak i32, T val} with val at round_up(8, align) and vtable layout {drop, size, align} are codegen ABI conventions inferred from comments; the dyn path frees on strong==0 without weak bookkeeping.

_Source: `src/compiler/mlir_gen_stmt.cpp#L606-L656`_


### `intrinsic.drop.recursive-by-type` — Drop recurses structurally by type shape

Dropping a value recurses by type: a struct runs its user `impl Drop` then drops each field; a tuple drops each element; an enum drops the payload of the active variant; an array drops each element; a reference/pointer/scalar drops nothing. Nesting (array-of-struct, struct-with-array-field) is handled recursively.

**Related.** `intrinsic.drop.owner-drops-fields-after-user-drop`

_Source: `src/compiler/mlir_gen_impl.hpp#L1085-L1098`_


### `intrinsic.drop.skip-moved-out-paths` — Moved-out sub-values are skipped during drop

Drop of a value suppresses sub-values that were moved out: a dotted path (relative to the value) whose segment exactly matches a child skips that child's drop entirely; a deeper path recurses into the child with the remainder so only the moved leaf is suppressed while its siblings still drop.

**Related.** `intrinsic.drop.recursive-by-type`

_Source: `src/compiler/mlir_gen_impl.hpp#L1093-L1098`_


## vec

### `intrinsic.vec.builtin-macro` — vec! is a compiler builtin list-literal macro

`vec!(a, b, c)` / `vec![a, b, c]` constructs a `Vec` of its elements. A user-defined `vec` fn_macro/token_macro in scope overrides the builtin. With a known renderable element type E (from a `let v: Vec<E>` annotation), it lowers to a push-block `{ let mut __v: Vec<E> = vec_new::<E>(); __v.push(e0); …; __v }` (no Copy bound). Otherwise it lowers to `vec_from_arr([…])` (Copy-bound, inference-driven); `vec!()` empty lowers to `vec_new::<_>()`.

_Source: `src/compiler/sema_expr.cpp#L18134-L18213`_


## str

### `intrinsic.str.from-raw-fatptr` — str_from_raw builds a str fat pointer

`str_from_raw(ptr: *const u8, len: i64) -> str` constructs a string slice as a two-field fat pointer {data: ptr, len: i64}; the length argument is coerced to i64.

**Related.** `layout.dst.slice-fatptr`

_Source: `src/compiler/mlir_gen_expr.cpp#L2216-L2233`_


### `intrinsic.str.str-from-raw` — str_from_raw constructs a str fat pointer

`str_from_raw(ptr: *const u8, len: i64) -> str` is a compiler intrinsic taking exactly 2 arguments; it yields a value of type `&[u8]`/str fat-pointer. Wrong arity is an error.

**Divergence.** Logos addition: no Rust equivalent free function.

_Source: `src/compiler/sema_expr.cpp#L3117-L3127`_


## closure

### `intrinsic.closure.drop-glue` — Owned closures drop their owned captures then free an escaping env

Dropping an owned closure value runs per-closure drop glue that drops each owned droppable capture (the narrow captured field when RFC-2229 phase-2 narrowing applies, else the whole captured root) and, if the env is heap-allocated (escaping closure), frees the env.

_Source: `src/compiler/mlir_gen_impl.hpp#L438-L454`_


## zone

### `intrinsic.zone.zone-of` — zone_of recovers the Writ zone pointer of a fat &mut T

`zone_of(r: &mut T) -> *mut u8` takes exactly 1 argument and yields the metadata half of the fat reference reinterpreted as a `*mut u8` (dual of zone_mut_ref). Wrong arity is an error.

**Divergence.** Logos addition: Writ/zone memory model intrinsic.

_Source: `src/compiler/sema_expr.cpp#L3129-L3137`_


## zone-mut-ref

### `intrinsic.zone-mut-ref.unsafe` — zone_mut_ref signature and unsafe requirement

`zone_mut_ref::<T>(ptr, zone)` requires unsafe context, exactly one type argument T, and exactly two value arguments.

**Divergence.** Logos addition (zoned-reference construction intrinsic).

_Source: `src/compiler/sema_expr.cpp#L4820-L4843`_


### `intrinsic.zone-mut-ref.value` — zone_mut_ref builds a fat &mut T carrying the zone

`zone_mut_ref::<T>(ptr, zone)` produces a fat `&mut T` whose data slot = ptr and whose metadata slot = zone pointer cast to i64.

_Source: `src/compiler/sema_expr.cpp#L4844-L4847`_


## matches!

### `intrinsic.matches.macro` — matches! tests a pattern

`matches!(expr, pattern [if guard])` evaluates to `true` iff `expr` matches the pattern (with optional guard), else `false`; lowered to `match (expr) { pattern => true, _ => false }`. The first top-level comma splits expr from the pattern.

_Source: `src/compiler/sema_expr.cpp#L18411-L18434`_


## fmt

### `intrinsic.fmt.tuple-debug-synth` — Debug formatting of tuples is synthesized element-wise

Debug formatting of a tuple T=(t0,..,t_{n-1}) emits an open delimiter, then for each element the element's Debug rendering separated by separators, then a close delimiter (a distinct close form for n==1). Each non-tuple element is formatted by a recursive `fmt` method-call dispatched on the element's Debug impl; a nested tuple element recurses through the same builder. A &[u8] slice element is formatted as `str`.

_Source: `src/compiler/mono_clone.cpp#L2646-L2682`, `src/compiler/mono_clone.cpp#L2663-L2671`_


## dbg!

### `intrinsic.dbg.macro` — dbg! prints and returns its argument

`dbg!(expr)` eprints `[file:line] expr = <Debug>` and evaluates to the value of `expr` (ownership passes through). `dbg!()` prints just `[file:line]` and yields `()`.

_Source: `src/compiler/sema_expr.cpp#L18436-L18472`_


## stringify!

### `intrinsic.stringify.macro` — stringify! returns raw token text

`stringify!(…)` yields the raw source text between the parentheses as a `&str` (`Slice<u8>`) literal, without macro expansion of the contents.

_Source: `src/compiler/sema_expr.cpp#L18333-L18346`_


## concat!

### `intrinsic.concat.macro` — concat! string-literal concatenation

`concat!(a, b, …)` concatenates string, integer (decimal, suffix-stripped), and bool (`true`/`false`) literals at compile time into a single `&str` (`Slice<u8>`) literal. Non-literal args are a compile error. String escapes \n \t \r \\ \" \0 are decoded.

**Divergence.** Floats and char literals are not supported (Rust supports them).

_Source: `src/compiler/sema_expr.cpp#L18318-L18324`, `src/compiler/sema_expr.cpp#L17836-L17920`_


## concat-bytes!

### `intrinsic.concat-bytes.macro` — concat_bytes! byte-array concatenation

`concat_bytes!(…)` concatenates byte-string literals (`b"…"`), byte-char literals (`b'X'`), and integer literals in range 0..=255 (decimal/0x/0o/0b, suffix-allowed) at compile time, yielding a `[u8; N]` array literal. Out-of-range integers, dangling/unknown escapes, and unsupported args are compile errors.

_Source: `src/compiler/sema_expr.cpp#L18326-L18331`, `src/compiler/sema_expr.cpp#L17922-L18084`_


## include!

### `intrinsic.include.expr-only` — include! splices a file as an expression

`include!("path")` reads the file at compile time and re-parses its contents as an expression spliced at the call site; only expression-position include! is supported (item-position is a compile error). Paths are resolved relative to the including file.

**Divergence.** Rust supports item-position include!; Logos supports only expression position.

_Source: `src/compiler/sema_expr.cpp#L18238-L18244`, `src/compiler/sema_expr.cpp#L17686-L17784`_


## include-str!

### `intrinsic.include-str.macro` — include_str! / include_bytes! embed file contents

`include_str!("path")` and `include_bytes!("path")` read the file at compile time (path relative to the including file) and yield its contents as a `&str` (`Slice<u8>`) literal; both forms collapse to the same representation since `str` is `Slice<u8>`. Unreadable files are a compile error.

**Divergence.** Rust's include_bytes! has type &[u8;N] distinct from &str; in Logos both are Slice<u8>.

_Source: `src/compiler/sema_expr.cpp#L18252-L18282`_


## env!

### `intrinsic.env.macro` — env! / option_env! read environment at compile time

`env!("VAR")` yields the value of environment variable VAR as a `&str` literal and is a compile error if unset; `option_env!("VAR")` yields the value or an empty `&str` if unset.

**Divergence.** option_env! returns an empty &str tombstone rather than Option<&str>.

_Source: `src/compiler/sema_expr.cpp#L18289-L18316`_


## cfg!

### `intrinsic.cfg.macro` — cfg! evaluates to a bool

`cfg!(predicate)` evaluates the configuration predicate at compile time and yields a `bool` literal.

_Source: `src/compiler/sema_expr.cpp#L18118-L18121`_


## file!

### `intrinsic.file.macro` — file! / module_path! positional macros

`file!()` yields the current file path and `module_path!()` yields the current package name, each as a `&str` (`Slice<u8>`) string literal.

_Source: `src/compiler/sema_expr.cpp#L18228-L18236`_


## line!

### `intrinsic.line.macro` — line! / column! positional macros

`line!()` yields the current source line as `u32`; `column!()` yields `u32` 0 (columns are not tracked).

**Divergence.** column!() always returns 0 rather than the true column.

_Source: `src/compiler/sema_expr.cpp#L18221-L18227`_


## compile-error!

### `intrinsic.compile-error.macro` — compile_error! emits a compile-time error

`compile_error!("msg")` takes one string-literal argument and emits that message as a compile-time error.

_Source: `src/compiler/sema_expr.cpp#L18392-L18409`_


## Metaprogramming

### `intrinsic.metaprog.reify-type` — reify_type round-trips a Type value at mono time

`reify_type(t: Type) -> Type` takes exactly 1 argument and lowers to the `__reify_type__` mono intercept, which substitutes the argument and re-emits a fresh `Type` struct literal from its uid. Wrong arity is an error.

**Divergence.** Logos addition: type-reflection metaprogramming intrinsic.

_Source: `src/compiler/sema_expr.cpp#L3139-L3154`_


### `intrinsic.metaprog.type-apply` — type_apply / apply_generic instantiate a type-level template

`type_apply(name: &[u8], args: [Type; N]) -> Type` and `apply_generic(g: Type, args: [Type; N]) -> Type` each take exactly 2 arguments and lower to the `__type_apply__` / `__apply_generic__` mono intercepts, which recover concrete TypeRefs from each element and emit a fresh `Type` struct literal for `Name<T0,...>`. Wrong arity is an error.

**Divergence.** Logos addition: type-level composition metaprogramming intrinsics.

_Source: `src/compiler/sema_expr.cpp#L3156-L3184`_


## wstatic-hash-of

### `intrinsic.wstatic-hash-of.u64` — wstatic_hash_of identity hash

`wstatic_hash_of::<CFG>()` requires exactly one type argument and yields `u64`, the byte-hash identity of a WritStatic value; folded at mono once CFG is a concrete WStaticLit.

**Divergence.** Logos addition.

_Source: `src/compiler/sema_expr.cpp#L5064-L5072`_


## Marker-panic intrinsics

### `intrinsic.marker-panics.macro` — unreachable! / todo! / unimplemented! marker macros

`unreachable!`, `todo!`, and `unimplemented!` are thin wrappers around `panic!` with default prefix messages ("internal error: entered unreachable code", "not yet implemented", "not implemented"); with args they panic with `"<prefix>: {}"` filled by `format!(args)`. They type as `!` (Never) and are valid in any expression position.

_Source: `src/compiler/sema_expr.cpp#L18348-L18390`_


## Unknown-callee handling

### `intrinsic.unknown-callee.passthrough` — Unrecognized callee is not a type intrinsic

A callee not matching any recognized type-intrinsic name yields no lowering here (the dispatcher returns nothing), leaving the call to ordinary resolution.

_Source: `src/compiler/sema_expr.cpp#L5828`_


