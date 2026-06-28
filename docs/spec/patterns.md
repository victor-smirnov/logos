# Patterns

Scope: pattern syntax, refutability, binding modes, exhaustiveness, and lowering for `match`, `let`, `let ... else`, `for`, and Writ patterns. Source layers: the PEG grammar (`tools/peg_gen/grammars/logos.peg`) and the sema/borrow-check layer (`src/compiler/sema_stmt.cpp` and siblings). Each rule `id` is its permanent linkable address.

## Wildcard Patterns

### `pat.wild.ident` — Identifier / wildcard pattern

A bare identifier is an irrefutable binding pattern (the matched value is bound to the name; `_` is the anonymous wildcard).

**Source:** `tools/peg_gen/grammars/logos.peg#L2237-L2238`

## Wildcard Patterns (sema)

### `pat.wildcard.underscore-non-binding` — `_` and empty name are non-binding wildcards

A wildcard pattern named `_` (or unnamed) introduces no binding and reserves no slot; any other name in a wildcard position is a binding that reserves a fresh dense slot.

**Source:** `src/compiler/sema_stmt.cpp#L3004-L3010`

## Identifier Bindings

### `pat.binding.ident-or-wildcard` — Binding and wildcard patterns

A wildcard pattern is `_`; a named binding pattern is the identifier itself.

**Source:** `src/compiler/sema_render.cpp#L553-L557`

### `pat.binding.bare-name-vs-variant-or-const` — Bare name resolving to a no-payload variant or module const is not a binding

A bare identifier pattern that names a payload-less enum variant or a module-level const is a constant/variant pattern, not a new binding; otherwise it introduces a binding. `_` is never a binding.

**Source:** `src/compiler/sema_stmt.cpp#L4174-L4194`

### `pat.binding.or-alt-shared-slot` — Or-pattern alternatives share binding slots; distinct patterns start fresh

Within one top-level pattern, repeated binding names across or-pattern alternatives map to the SAME dense slot; binding-slot allocation is reset at the start of each top-level pattern, so separate match arms / let patterns allocate independent slots.

**Source:** `src/compiler/sema_stmt.cpp#L3012-L3023`, `src/compiler/sema_stmt.cpp#L3951-L3956`

### `pat.binding.default-by-ref-mode` — Default binding modes wrap payload bindings by reference

Under a `&`/`&mut` scrutinee, every plain named payload binding binds by-reference: the binding type is wrapped in `&`/`&mut` once per scrutinee ref-layer, with the outermost layer carrying mut iff any peeled layer was `&mut`. Bindings to `_` and synthesized slots are exempt.

**Divergence:** Rust-conformant (RFC 2005); historical move-only-type restriction now lifted

**Source:** `src/compiler/sema_stmt.cpp#L3252-L3265`, `src/compiler/sema_stmt.cpp#L3915-L3949`

### `pat.binding.explicit-ref-mut` — `ref`/`ref mut` payload binding wraps type in &/&mut

An explicit `ref v` (resp. `ref mut v`) sub-pattern in a variant payload binds by reference: the binding type is wrapped in `&` (resp. `&mut`), binding the payload slot's address rather than a load. Explicit ref overrides default-binding-mode wrapping.

**Source:** `src/compiler/sema_stmt.cpp#L3680-L3690`, `src/compiler/sema_stmt.cpp#L3734-L3748`, `src/compiler/sema_stmt.cpp#L3918-L3923`

## Binding Modes and Move/Ref Semantics

### `pat.bind.variant-and-wild-introduce-bindings` — Pattern bindings introduced into scope

A variant-data pattern injects each of its binding names into scope; a wildcard pattern binds its name into scope only when the name is non-empty and not `_`.

**Source:** `src/compiler/borrow_check.cpp#L1477-L1501`

### `pat.bind.wildcard-no-binding` — `_` binding name introduces no variable

A binding whose name is `_` introduces no variable into scope (across variant-data, tuple, struct-field, and wildcard patterns). This prevents a phantom binding from scheduling a drop on a payload the user discarded with `_`.

**Source:** `src/compiler/sema_stmt.cpp#L5691-L5694`, `src/compiler/sema_stmt.cpp#L5702-L5705`, `src/compiler/sema_stmt.cpp#L5726-L5734`

### `pat.bind.struct-generic-subst` — Struct pattern substitutes concrete type-args into field types

When matching a struct pattern against a concrete generic struct `S<A,B>`, each bound field's type is the field's declared type with the struct's type parameters substituted by the scrutinee's concrete type-args (e.g. `match s { S { x, y } }` over `S<u8,u16>` binds x:u8, y:u16). A `&`/`&mut`/`*` scrutinee is dereferenced first to obtain the type-args.

**Source:** `src/compiler/sema_stmt.cpp#L5768-L5783`, `src/compiler/sema_stmt.cpp#L5800-L5806`

### `pat.bind.default-binding-mode-struct` — Default binding modes for struct shorthand fields under a reference scrutinee

Under a `&`/`&mut` struct scrutinee, a shorthand field binding of a move-only field type T binds by reference (`&T` / `&mut T` matching the scrutinee's mutability) rather than moving the field out; Copy field types bind by value. Error and bare-TypeVar field types are excluded from the reference promotion.

**Divergence:** RFC 2005 default binding modes (Rust-conformant intent).

**Source:** `src/compiler/sema_stmt.cpp#L5792-L5816`

### `pat.bind.or-alts-same-bindings` — Or-pattern alternatives bind identical names and types

All alternatives of an or-pattern bind the same set of names with the same types; bindings are declared from the first alternative.

**Source:** `src/compiler/sema_stmt.cpp#L5719-L5725`, `src/compiler/sema_stmt.cpp#L5829-L5834`

### `pat.bind.ref-pat-strips-ref` — Reference pattern strips one reference layer

A reference pattern `&p` binds its inner pattern against the pointee of a `&`/`&mut` scrutinee type; against a non-reference scrutinee the inner type is Error.

**Source:** `src/compiler/sema_stmt.cpp#L5744-L5751`

### `pat.bind.slice-rest-is-subslice` — Slice pattern element and named-rest types

In a slice pattern, prefix and suffix sub-patterns bind against the element type T of the scrutinee. A named rest `xs @ ..` binds the sub-slice as `&[T]` (slice type), not an element; an anonymous `..` rest binds nothing.

**Source:** `src/compiler/sema_stmt.cpp#L5819-L5828`

### `pat.bind.mut-binding-mode` — `mut` binding pattern marks variable mutable

A wildcard-name binding written with `mut` (tracked per pattern) declares the bound variable as mutable.

**Source:** `src/compiler/sema_stmt.cpp#L5726-L5733`

### `pat.bind.ref-does-not-move` — ref bindings and _ borrow/discard rather than move

A `ref`-bound binding borrows its place and a `_` (anonymous wildcard) discards it; neither consumes the matched value, recursively through nested tuple and struct sub-patterns. A by-value name binding (including struct-field shorthand `{ name }`) consumes the value.

**Source:** `src/compiler/sema_stmt.cpp#L7652-L7676`, `src/compiler/sema_stmt.cpp#L7733-L7769`, `src/compiler/sema_stmt.cpp#L7742`

## Reference Patterns

### `pat.ref.binding-mode` — ref / ref mut / mut bindings

`ref x` binds the matched place by shared reference; `ref mut x` by mutable reference; `mut x` introduces a fresh mutable binding by value.

**Source:** `tools/peg_gen/grammars/logos.peg#L2053-L2063`

### `pat.ref.reference-pattern` — Reference patterns

`&pat` and `&mut pat` match a reference, peeling one scrutinee reference layer. `&&pat` / `&&mut pat` (lexed as `AND`) peels two layers, producing nested reference patterns.

**Source:** `tools/peg_gen/grammars/logos.peg#L2064-L2085`

### `pat.ref.amp-mut` — Reference pattern

A reference pattern is `&pat` or `&mut pat`.

**Source:** `src/compiler/sema_render.cpp#L651-L659`

### `pat.ref.scrutinee-reference` — Reference pattern requires reference scrutinee

A reference pattern `&pat`/`&mut pat` requires a reference scrutinee. `&mut pat` requires a `&mut` scrutinee; `&pat` accepts both `&` and `&mut`. A non-reference scrutinee is an error. The inner pattern is matched against the pointee type.

**Source:** `src/compiler/sema_stmt.cpp#L4742-L4771`

## Reference Bindings

### `pat.ref-binding.binds-by-reference` — `ref`/`ref mut` binding takes a reference to the matched place

A `ref x` / `ref mut x` binding binds `x` to a `&T` / `&mut T` reference of the scrutinee type `T` (the place's address) rather than moving the value.

**Source:** `src/compiler/sema_stmt.cpp#L4774-L4792`

## At-Bindings

### `pat.at.binding` — At-binding pattern

`name @ subpat` binds `name` to the value matched by `subpat`. `ref name @ subpat` binds by reference.

**Source:** `tools/peg_gen/grammars/logos.peg#L2043-L2052`

### `pat.at.binds-whole-and-sub` — @-pattern binds whole value at scrutinee type

An `name @ subpat` pattern binds `name` to the whole scrutinee value at the scrutinee type (falling back to the error type if unknown) while also matching `subpat` against the same scrutinee type.

**Source:** `src/compiler/sema_stmt.cpp#L4724-L4740`

## At-Bindings (sema)

### `pat.at-binding.payload-bind-and-guard` — `n @ sub` binds payload and applies sub-pattern guard

An `@`-binding `n @ <sub>` in a variant payload binds the payload to name `n` and additionally gates the arm with the refutable guard of `<sub>` (range/literal/variant) built against `n`; `n @ _` binds with no guard.

**Source:** `src/compiler/sema_stmt.cpp#L3504-L3509`, `src/compiler/sema_stmt.cpp#L3769-L3788`

## Match Ergonomics (Auto-Deref)

### `pat.ergonomics.deref-scrutinee` — Match ergonomics peel all &/&mut/* layers

Pattern matching peels all `&`, `&mut`, and `*` layers of the scrutinee type to obtain the concrete payload shape, so a pattern over `&&Enum<T>` (arbitrary depth) unifies against the inner `Enum<T>`.

**Divergence:** Rust-conformant (RFC 2005 default binding modes)

**Source:** `src/compiler/sema_stmt.cpp#L3220-L3243`, `src/compiler/sema_stmt.cpp#L3828-L3851`

## Literal Patterns (grammar)

### `pat.lit.unit` — Unit pattern `()`

`()` is the unit pattern, matching the unit value.

**Source:** `tools/peg_gen/grammars/logos.peg#L2087-L2091`

### `pat.lit.integer` — Integer literal patterns

`N` matches an integer literal; `-N` matches a negative integer literal.

**Source:** `tools/peg_gen/grammars/logos.peg#L2186-L2189`

### `pat.lit.float-rejected` — Float-literal pattern rejected

A float-literal pattern parses but is rejected by sema with a diagnostic: float equality matching in patterns is deliberately not supported (IEEE equality semantics undefined).

**Divergence:** Rust deprecated float patterns; Logos rejects them outright.

**Source:** `tools/peg_gen/grammars/logos.peg#L2195-L2199`

### `pat.lit.bytes-rejected` — Byte-string pattern rejected

A byte-string-literal pattern parses but is rejected by sema (pending &[u8] equality-matching codegen).

**Uncertainty:** Status is provisional ("until codegen lands"); reflects current compiler behavior.

**Source:** `tools/peg_gen/grammars/logos.peg#L2200-L2203`

### `pat.lit.string` — String-literal pattern

A string-literal pattern `"foo"` matches by string equality (lowered to a refutable `str_eq(scrut, "foo")` guard over a wildcard binding).

**Source:** `tools/peg_gen/grammars/logos.peg#L2204-L2207`

### `pat.lit.bool` — Bool patterns

`true` and `false` match the boolean values.

**Source:** `tools/peg_gen/grammars/logos.peg#L2208-L2211`

## Literal Patterns (render)

### `pat.literal.int-bool-neg` — Literal patterns

Patterns may be integer literals (optionally negated with leading `-`), boolean literals (`true`/`false`), or unit `()`.

**Source:** `src/compiler/sema_render.cpp#L558-L572`

## Boolean Patterns

### `pat.bool.scrutinee-bool` — Bool pattern scrutinee constraint

A boolean-literal pattern requires a `bool` scrutinee; any other (non-error) scrutinee type is an error.

**Source:** `src/compiler/sema_stmt.cpp#L4445-L4455`

## Integer Patterns

### `pat.int.scrutinee-must-be-integer` — Integer pattern requires an integer scrutinee

An integer-literal pattern (incl. negated form) requires the scrutinee type to be an integer type; matching against a non-integer scrutinee is an error. The check is skipped when the scrutinee type is Error or `!` (never).

**Source:** `src/compiler/sema_stmt.cpp#L4313-L4321`

### `pat.int.value-must-fit` — Integer pattern value must fit the scrutinee integer type

The value of an integer-literal pattern must be representable in the scrutinee's integer type; an out-of-range value is an error.

**Source:** `src/compiler/sema_stmt.cpp#L4322-L4325`

## Char Patterns

### `pat.char.scalar-as-integer` — Char patterns lower to integer (Unicode scalar) patterns

A char-literal (and char-range) pattern is decoded to its Unicode scalar value and matched as an integer/range pattern, since `char` is a 4-byte Unicode scalar. Recognized escapes: `\n`,`\t`,`\r`,`\0`,`\\`,`\'`,`\"`,`\xHH` (exactly 2 hex digits), and `\u{HEX}`; a `\u` scalar must be <= U+10FFFF and outside the surrogate range U+D800..U+DFFF.

**Source:** `src/compiler/sema_stmt.cpp#L4330-L4396`

### `pat.char.scrutinee-char-or-int` — Char pattern scrutinee constraint

A char-literal pattern requires the scrutinee type to be `char` or an integer type; otherwise it is an error. The pattern matches the decoded code point as an integer constant.

**Source:** `src/compiler/sema_stmt.cpp#L4414-L4426`

## Char-Range Patterns

### `pat.char-range.scrutinee-and-order` — Char range pattern constraints

A char-range pattern `lo..=hi` requires a `char` or integer scrutinee, and requires lo <= hi (decoded code points); lo > hi is an error. It matches the inclusive integer range [lo, hi].

**Source:** `src/compiler/sema_stmt.cpp#L4427-L4443`

## Float Patterns (Rejected)

### `pat.float.rejected-at-sema` — Float-literal patterns rejected

A float-literal pattern parses but is rejected at sema (not a valid match pattern).

**Divergence:** Rust also forbids float patterns (deprecated/removed).

**Source:** `tools/peg_gen/grammars/logos.peg#L283`

### `pat.float.literal-rejected` — Float-literal patterns are rejected

A float-literal pattern is parsed but rejected as unsupported (IEEE-equality pattern semantics undecided).

**Divergence:** Rust deprecated-but-still-accepts float patterns; Logos hard-rejects them.

**Source:** `src/compiler/sema_stmt.cpp#L4286-L4294`

## String Patterns

### `pat.str.lowers-to-eq-guard` — String-literal pattern lowers to equality guard

A string-literal pattern `match s { "foo" => ... }` is matched by lowering to a `str_eq` guard.

**Source:** `tools/peg_gen/grammars/logos.peg#L312`

### `pat.str.position-restricted` — String-literal patterns allowed only in specific positions

String-literal patterns are supported only as a whole match arm (`match s { "foo" => .. }`), inside an enum-variant payload (`Some("foo")`), or as a tuple element (`("foo", _)`). In any other position (e.g. inside an array/slice pattern) a string-literal pattern is an error.

**Divergence:** Rust permits string patterns in all pattern positions; Logos restricts them.

**Source:** `src/compiler/sema_stmt.cpp#L4296-L4312`

## Range Patterns

### `pat.range.integer` — Integer range patterns

Integer range patterns include closed inclusive `lo..=hi`, closed exclusive `lo..hi`, and half-open forms `a..` (RangeFrom → [a, TYPE_MAX]), `..=b` (RangeToInclusive → [TYPE_MIN, b]), `..b` (RangeToExclusive → [TYPE_MIN, b-1]). Each endpoint may be negated (`-N`). Open bounds clamp to the scrutinee type's min/max.

**Source:** `tools/peg_gen/grammars/logos.peg#L2148-L2185`

### `pat.range.char` — Char patterns

`'a'` matches a char literal; `'a'..='z'` matches an inclusive char range.

**Source:** `tools/peg_gen/grammars/logos.peg#L2190-L2194`

### `pat.range.inclusive-only` — Range pattern is inclusive

A range pattern is written `lo..=hi` (inclusive); either bound may be negated with a leading `-`.

**Uncertainty:** Renderer only emits `..=`; exclusive range patterns (if any) not represented here.

**Source:** `src/compiler/sema_render.cpp#L633-L650`

### `pat.range.half-open-clamp` — Half-open range pattern clamps to scrutinee type bounds

In an integer range pattern, an omitted bound is clamped to the scrutinee integer type's min (for missing lo) or max (for missing hi); when the scrutinee type is unknown the bounds default to i32 range.

**Source:** `src/compiler/sema_stmt.cpp#L4654-L4676`

### `pat.range.scrutinee-integer` — Range pattern requires integer scrutinee

A range pattern requires an integer scrutinee type; a non-integer, non-error scrutinee is an error. A `never` scrutinee is exempted from this check.

**Divergence:** Logos char ranges are handled separately (PAT_CHAR_RANGE); PAT_RANGE is integer-only.

**Source:** `src/compiler/sema_stmt.cpp#L4685-L4689`

### `pat.range.bounds-fit-type` — Range pattern bounds must fit scrutinee type

Both bounds of an integer range pattern must fit within the scrutinee integer type; a bound that does not fit is an error.

**Source:** `src/compiler/sema_stmt.cpp#L4690-L4698`

### `pat.range.exclusive-to-inclusive` — Exclusive range pattern lowered to inclusive minus one

An exclusive range pattern `lo..hi` is lowered as inclusive `lo..=(hi-1)`. An exclusive range with lo >= hi is an empty-range error. An inclusive range (default when unmarked) with lo > hi is an error.

**Source:** `src/compiler/sema_stmt.cpp#L4699-L4720`

## Tuple Patterns

### `pat.tuple.elem-rest` — Tuple-pattern rest element

A tuple-pattern element may be `..` (rest, converted to `_` wildcard skips preserving fixed arity) or an or-pattern of sub-patterns.

**Source:** `tools/peg_gen/grammars/logos.peg#L2015-L2026`

### `pat.tuple.shape` — Tuple pattern

`(a, b, ...)` is a tuple pattern (≥2 elements) admitting a `..` rest at any single position; `(x,)` (trailing comma) is a 1-tuple pattern, distinguished from a parenthesised pattern `(x)`.

**Source:** `tools/peg_gen/grammars/logos.peg#L2228-L2236`

### `pat.tuple.one-elem-trailing-comma` — Tuple pattern trailing comma

A tuple pattern is `(p0, p1, ...)`; a single-element tuple pattern requires a trailing comma `(p,)`.

**Source:** `src/compiler/sema_render.cpp#L608-L621`

### `pat.tuple.scrutinee-tuple-or-ref` — Tuple pattern over tuple or reference-to-tuple

A tuple pattern requires a tuple scrutinee, or a `&(T..)` / `&mut (T..)` scrutinee which is auto-dereferenced to the inner tuple (default binding mode). A non-tuple, non-(ref-to-tuple) scrutinee is an error.

**Source:** `src/compiler/sema_stmt.cpp#L4456-L4480`

### `pat.tuple.rest-expansion` — Tuple pattern `..` rest expansion

A tuple pattern may contain at most one `..` rest marker; a second `..` is an error. The rest is expanded into wildcard `_` skip entries inserted at the rest position so the explicit elements plus padding equal the tuple arity. More explicit elements than the arity (with `..`) is an error.

**Source:** `src/compiler/sema_stmt.cpp#L4481-L4520`

### `pat.tuple.element-kinds` — Allowed tuple pattern element kinds

Tuple pattern elements may be: wildcard/binding (`_`/name), integer/negative-integer/bool/range literals, variant-data patterns, string literals, and or-patterns. Any other element kind is an error ('only _, name, integer, bool, range, or variant patterns are supported').

**Source:** `src/compiler/sema_stmt.cpp#L4521-L4630`

### `pat.tuple.str-element-via-guard` — String-literal tuple element lowered to str_eq guard

A string-literal element of a tuple pattern binds the element to a synthesized name and adds a refutable `str_eq(synth, lit)` guard, rather than a value-equality test (a raw `==` would pointer-compare). Requires the refutable-guard context to be active.

**Divergence:** Logos addition: tuple-arm codegen lacks a native str_eq dispatch, so string elements are desugared to guards.

**Source:** `src/compiler/sema_stmt.cpp#L4552-L4567`, `src/compiler/sema_stmt.cpp#L4600-L4617`

### `pat.tuple.single-alt-or-unwrap` — Single-alternative or-pattern element is unwrapped

The grammar wraps every tuple element in a PAT_OR node; a trivial single-alternative or-pattern is unwrapped and treated as its inner binding/wildcard/literal/variant. Multi-alternative or-patterns are kept as PatOr and their alternatives must be scalar (bindings inside multi-alt are dropped).

**Source:** `src/compiler/sema_stmt.cpp#L4568-L4623`

### `pat.tuple.arity-match` — Tuple pattern element count equals arity

After rest expansion, the number of tuple pattern elements must equal the tuple arity; a mismatch is an error.

**Source:** `src/compiler/sema_stmt.cpp#L4631-L4634`

### `pat.tuple.default-ref-move-only` — Default-ref binding for move-only tuple elements under shared borrow

When the tuple scrutinee is reached through a `&`/`&mut` (default binding mode), each tuple element binding whose element type is move-only (non-Copy, non-error, non-typevar) is bound by reference `&et`/`&mut et`; Copy elements are bound by value.

**Source:** `src/compiler/sema_stmt.cpp#L4456-L4461`, `src/compiler/sema_stmt.cpp#L4635-L4642`

## Tuple-Binding Patterns

### `pat.tuple-bind.let` — Let-binding tuple pattern

A let-binding tuple pattern admits `()` (unit), `..` rest (expanded to the right number of `_` skips), nested tuples `(a, (b, c))`, and identifier bindings. Rest fills remaining positions so names land on the correct tuple slots.

**Source:** `tools/peg_gen/grammars/logos.peg#L1943-L1960`

## Tuple-Struct Patterns

### `pat.tuple-struct.bare-call-form` — Bare `Foo(a,b)` tuple-struct pattern destructures positional fields

An unqualified call-form pattern `Foo(p0, p1, ...)` whose name resolves to a tuple-struct destructures it as a struct pattern with synthetic positional field names "0","1",...; sub-pattern j binds field j.

**Source:** `src/compiler/sema_stmt.cpp#L3122-L3184`

### `pat.tuple-struct.arity-check` — Tuple-struct pattern arity must match (absent `..`)

Without a `..` rest, a tuple-struct pattern must supply exactly as many sub-patterns as the struct's field count; with `..`, the supplied count must not exceed the arity.

**Source:** `src/compiler/sema_stmt.cpp#L3168-L3175`

## Struct Patterns

### `pat.struct.field` — Struct-pattern field

A struct-pattern field is `..` (rest), `name: subpat`, `0: subpat` (tuple-struct field by index, resolved positionally), `ref name`, `ref mut name`, or a bare `name` shorthand binding the field to a same-named local.

**Source:** `tools/peg_gen/grammars/logos.peg#L1980-L1999`

### `pat.struct.shape` — Struct pattern

`Point { field_list }` / `Point {}` destructure a struct by named fields.

**Source:** `tools/peg_gen/grammars/logos.peg#L2138-L2142`

### `pat.struct.field-forms` — Struct pattern

A struct pattern is `Name { field [: subpat], ..., [..] }`; a field with no `: subpat` binds by field name (shorthand), and a trailing `..` ignores remaining fields.

**Source:** `src/compiler/sema_render.cpp#L661-L682`

### `pat.struct.field-shorthand-binds-name` — Struct field shorthand binds the field name

In a struct pattern, a field with no explicit sub-pattern (shorthand `Point { x }`) introduces a binding named after the field; a field with an explicit sub-pattern binds whatever that sub-pattern binds.

**Source:** `src/compiler/sema_stmt.cpp#L4201-L4209`, `src/compiler/sema_stmt.cpp#L4100-L4106`

### `pat.struct.unknown-name` — Struct pattern name must resolve

A struct pattern `N { .. }` requires `N` to resolve to a known struct or datatype; a type-alias `N` whose target is a Struct/ZonedStruct resolves transparently to the underlying struct (matched under the real name). Otherwise it is an error 'unknown struct'.

**Source:** `src/compiler/sema_stmt.cpp#L4821-L4847`

### `pat.struct.scrutinee-name-match` — Struct pattern must match scrutinee struct

If the scrutinee has a concrete (non-error, named) Struct type, a struct pattern's name must equal the scrutinee's struct name, else error 'struct pattern != scrutinee'.

**Source:** `src/compiler/sema_stmt.cpp#L4848-L4852`

### `pat.struct.field-exists` — Struct pattern field must exist

Each named field in a struct pattern must be a declared field of the struct; an unknown field name is an error 'has no field'.

**Source:** `src/compiler/sema_stmt.cpp#L4881-L4889`

### `pat.struct.rest-once-and-last` — Struct pattern `..` at most once and last

A struct pattern may contain at most one `..` rest element, and no named field may follow `..`; violations are errors.

**Source:** `src/compiler/sema_stmt.cpp#L4864-L4873`

### `pat.struct.exhaustive-fields` — Struct pattern must cover all fields unless `..`

A non-union struct pattern without `..` must name every field of the struct; an uncovered field is an error (suggesting `..`).

**Source:** `src/compiler/sema_stmt.cpp#L5006-L5016`

### `pat.struct.field-ref-shorthand` — `ref [mut] f` field shorthand binds a reference to the field

In a struct pattern, a field marked `ref` (optionally `ref mut`) with no explicit sub-pattern binds `f` to `&[mut] T` where `T` is the field type, equivalent to a `ref [mut] f` ref-binding sub-pattern.

**Source:** `src/compiler/sema_stmt.cpp#L4892-L4917`

### `pat.struct.shorthand-binds-field` — Shorthand field binds the field name

A plain shorthand field `{ f }` (no sub-pattern) binds a new variable named `f` to the field value; `_` is non-binding.

**Source:** `src/compiler/sema_stmt.cpp#L4971-L4976`

### `pat.struct.literal-field-guard` — Literal field sub-pattern lowers to a binding plus equality guard

A refutable literal field sub-pattern `S { f: <int|neg-int|bool|char> }` (in refutable context) binds the field to a fresh synthetic name and gates the arm with `synth == <literal>`.

**Source:** `src/compiler/sema_stmt.cpp#L4918-L4951`

### `pat.struct.refutable-sub-supported-kinds` — Limited set of refutable field sub-patterns

Refutable field sub-patterns are accepted only for kinds {Wild, RefBind, RefPat, At, Variant, VariantData, Tuple, Or, Range, Int, Bool, Struct}; other kinds (e.g. slice, writ) are an error 'not yet supported'.

**Uncertainty:** List reflects current implementation coverage; the unsupported set is an implementation limitation rather than a settled language rule.

**Source:** `src/compiler/sema_stmt.cpp#L4952-L4969`

## Slice and Array Patterns

### `pat.slice.elem-rest` — Slice-pattern rest binding

A slice-pattern element may be `name @ ..` (binds the trailing/middle sub-slice to a name), `..` (anonymous rest), or a regular sub-pattern.

**Source:** `tools/peg_gen/grammars/logos.peg#L2004-L2013`

### `pat.slice.shape` — Slice pattern

`[elems]` / `[]` match a slice/array by element patterns.

**Source:** `tools/peg_gen/grammars/logos.peg#L2143-L2147`

### `pat.slice.scrutinee-array-or-slice` — Slice pattern requires array/slice scrutinee

A slice pattern `[..]` requires the scrutinee to be of array or slice type; the element sub-patterns are typed by the element type. A non-array/slice scrutinee is an error.

**Source:** `src/compiler/sema_stmt.cpp#L5024-L5032`

### `pat.slice.rest-once` — Slice pattern `..` at most once

A slice pattern may contain at most one `..` rest; elements before `..` form the prefix, elements after form the suffix. A second `..` is an error.

**Source:** `src/compiler/sema_stmt.cpp#L5043-L5058`

### `pat.slice.rest-binding` — Named `..` binds the rest sub-slice

A rest element written `name @ ..` binds `name` to the rest sub-slice; a bare `..` is anonymous.

**Source:** `src/compiler/sema_stmt.cpp#L5048-L5053`

### `pat.slice.array-length-check` — Fixed-array slice pattern length constraints

For a fixed-size array scrutinee: a slice pattern without `..` must have exactly `N` elements; with `..`, prefix+suffix count must not exceed `N`. Violations are errors.

**Source:** `src/compiler/sema_stmt.cpp#L5063-L5076`

## Rest (`..`) Patterns

### `pat.rest.dotdot` — Rest pattern

A rest/ignore-remaining pattern is written `..` and may appear among struct or tuple subpatterns.

**Source:** `src/compiler/sema_render.cpp#L660-L660`, `src/compiler/sema_render.cpp#L675-L677`

### `pat.rest.single-only` — At most one `..` rest per tuple/tuple-struct pattern

A tuple-struct or tuple-variant pattern may contain at most one `..` rest; sub-patterns before the rest bind low positions and those after bind tail positions, with skipped positions binding nothing.

**Source:** `src/compiler/sema_stmt.cpp#L3140-L3167`, `src/compiler/sema_stmt.cpp#L3718-L3733`

## Parenthesized Groups

### `pat.group.paren` — Parenthesised / grouped pattern

`(P)` is exactly P and `(P | Q | ...)` is a grouped or-pattern (inlined into a single or-pattern at that position). `(..)` matches any tuple binding nothing (irrefutable wildcard).

**Source:** `tools/peg_gen/grammars/logos.peg#L2212-L2227`

## Unit Patterns

### `pat.unit.no-binding` — `()` unit sub-pattern binds nothing

A `()` (unit) sub-pattern in a variant payload position introduces no binding.

**Source:** `src/compiler/sema_stmt.cpp#L3717`

## Identifier, Const, and Variant-Alias Resolution

### `pat.ident.bare-no-payload-variant` — Bare identifier resolving to a no-payload enum variant is a variant pattern

When the scrutinee is an enum and a bare identifier (not `_`) names a no-payload variant of that enum, the identifier is treated as a variant pattern (refutable) rather than an irrefutable binding. This covers prelude variants (None/Some/Ok/Err) and user enums matched without the `Enum::` qualifier.

**Source:** `src/compiler/sema_stmt.cpp#L4793-L4817`

### `pat.ident.variant-alias` — Bare ident resolving to a use-imported nullary variant is a variant pattern

A bare identifier pattern that matches a `use Type.{V, ..}` variant alias and names a nullary (no-payload) variant is a variant pattern, not a fresh binding; the scrutinee enum must match, else error.

**Source:** `src/compiler/sema_stmt.cpp#L5106-L5132`

### `pat.ident.module-const-value` — Bare ident resolving to a module const is a value pattern

A bare identifier that names a module-level const is a value (refutable) pattern, not a binding: its initializer is ctfe-evaluated and matched. Bool/int/char consts emit a literal pattern. A non-ctfe-evaluable const initializer is an error.

**Source:** `src/compiler/sema_stmt.cpp#L5133-L5158`, `src/compiler/sema_stmt.cpp#L5247-L5251`

### `pat.ident.const-str-guard` — str-typed const pattern lowers to a str_eq guard

A const pattern of `str` (Slice<u8>) type against a str scrutinee binds a synthetic name and gates the arm with `str_eq(synth, CONST)`; requires the stdlib `str_eq` to be in scope, else error.

**Source:** `src/compiler/sema_stmt.cpp#L5159-L5168`, `src/compiler/sema_stmt.cpp#L5214-L5241`

### `pat.ident.const-bytearray-guard` — [u8; N] const pattern lowers to an element-wise equality guard

A const pattern of `[u8; N]` type against a `[u8; N]` scrutinee (matching array length) binds a synthetic name and gates the arm with the AND-chain `synth[i] == CONST[i]` for all i in 0..N.

**Source:** `src/compiler/sema_stmt.cpp#L5169-L5213`

### `pat.ident.const-nonscalar-unsupported` — Other non-scalar const patterns rejected

A const pattern whose value is neither int/bool/char nor the supported str/[u8;N] guard cases is an error ('non-scalar type').

**Uncertainty:** Reflects current support boundary, not a permanent language restriction.

**Source:** `src/compiler/sema_stmt.cpp#L5242-L5246`

### `pat.ident.binding-and-mut` — Bare ident is a binding; `mut` recorded for mutable binding

A bare identifier not resolving to a variant or const is a fresh binding (`_` is non-binding/wildcard). A `mut x` binding is recorded so the binding is introduced as mutable.

**Source:** `src/compiler/sema_stmt.cpp#L5254-L5264`

## Enum-Variant Patterns

### `pat.variant.tuple` — Enum tuple-variant pattern

`E::V(args)` matches an enum tuple-variant payload; payload args are full nested patterns, possibly including `..` rest and or-patterns. A bare `Foo(a, b)` (no `::`) matches a tuple-struct when the name resolves as a tuple-struct rather than an enum.

**Source:** `tools/peg_gen/grammars/logos.peg#L2121-L2122`, `tools/peg_gen/grammars/logos.peg#L2132-L2137`

### `pat.variant.struct-shape` — Enum struct-variant pattern

`E::V { x, y: pat, .. }` / `E::V {}` match a struct-shaped enum variant; field names resolve to variant payload indices.

**Source:** `tools/peg_gen/grammars/logos.peg#L2123-L2129`

### `pat.variant.fieldless` — Fieldless variant pattern

`E::V` matches a fieldless (unit) enum variant.

**Source:** `tools/peg_gen/grammars/logos.peg#L2130-L2131`

### `pat.variant.path-and-data` — Variant pattern forms

An enum variant pattern is written `Enum::Variant` (data-less) or with data `Enum::Variant(args)`; the bare/tuple-struct form `Variant(args)` omits the enum qualifier.

**Source:** `src/compiler/sema_render.cpp#L573-L607`

### `pat.variant.prelude-shorthand-resolution` — bare variant names resolve via prelude/alias remap

A variant pattern written with only a variant name (no enum qualifier) resolves its enum: `Some`/`None` → `Option`, `Ok`/`Err` → `Result`, and otherwise via the importing module's variant aliases.

**Source:** `src/compiler/sema_stmt.cpp#L7922-L7944`

### `pat.variant.prelude-shorthand` — Prelude variant shorthand in patterns

Unqualified variant patterns `Some`/`None`/`Ok`/`Err` resolve to `Option`/`Result` variants when no enum qualifier is given and the prelude enum carries that variant.

**Source:** `src/compiler/sema_stmt.cpp#L3029-L3047`, `src/compiler/sema_stmt.cpp#L3094-L3114`

### `pat.variant.use-variant-alias` — `use Type.{V,..}` bare-variant alias resolves in patterns

A bare (unqualified) variant name in a pattern resolves to its enum when that name was imported via a `use Type.{V, ...}` variant-alias.

**Source:** `src/compiler/sema_stmt.cpp#L3048-L3053`, `src/compiler/sema_stmt.cpp#L3115-L3120`

### `pat.variant.type-alias-peel` — Type-alias to enum peels in variant patterns

A variant pattern `Alias::V` / `Alias::V(..)` where `type Alias<..> = Enum<..>` resolves the variant on the underlying enum; alias type-arguments do not affect which variant matches.

**Source:** `src/compiler/sema_stmt.cpp#L3055-L3066`, `src/compiler/sema_stmt.cpp#L3186-L3196`

### `pat.variant.unknown-enum-error` — Unknown enum / variant in pattern is an error

A variant pattern naming an enum not in scope, or a variant not declared by the resolved enum, is rejected.

**Source:** `src/compiler/sema_stmt.cpp#L3071-L3084`, `src/compiler/sema_stmt.cpp#L3202-L3208`

### `pat.variant.scrutinee-enum-match` — Variant pattern enum must equal scrutinee enum

When the scrutinee has a concrete enum type, a variant pattern naming a different enum is rejected.

**Source:** `src/compiler/sema_stmt.cpp#L3074-L3078`

### `pat.variant.type-arg-subst` — Generic enum type-args substitute into payload binding types

For a generic enum scrutinee `Enum<A,..>`, each variant payload binding's type is the declared payload type with the enum's type parameters substituted by the scrutinee's type-arguments (after peeling ref/ptr layers).

**Source:** `src/compiler/sema_stmt.cpp#L3236-L3242`, `src/compiler/sema_stmt.cpp#L3266-L3270`, `src/compiler/sema_stmt.cpp#L3825-L3857`

### `pat.variant.struct-shape-fields` — Struct-shape variant pattern resolves fields by name

A `E::V { f0, f1: p, .. }` pattern is allowed only for struct-shaped variants; named fields resolve to payload positions, shorthand `f` binds field `f` to name `f`, an unknown or duplicate field name is an error, and absent `..` every field must be specified.

**Source:** `src/compiler/sema_stmt.cpp#L3562-L3679`

### `pat.variant.tuple-shape-needs-parens` — Tuple-shape variant rejects brace pattern

A tuple-shaped variant (positional payload, no field names) cannot be matched with brace `{ .. }` pattern syntax.

**Source:** `src/compiler/sema_stmt.cpp#L3569-L3572`

### `pat.variant.nested-struct-tuple-destructure` — Irrefutable nested struct/tuple payload destructures in arm body

A nested struct- or tuple-pattern inside a variant payload binds the payload to a synthetic slot and emits an irrefutable `let <sub> = __synth;` destructure as an arm-body prologue.

**Source:** `src/compiler/sema_stmt.cpp#L3749-L3768`

### `pat.variant.unit-payload-binding` — Named binding against unit-typed payload is a zero-sized local

When a variant's payload types are all `()`, a `_` binding is dropped and a named binding is kept with a `()` binding type (a zero-sized local in scope), since unit fields are elided from the enum layout. The unit payload position itself is omitted from binding types.

**Divergence:** Rust-conformant (rustc issue-41888 `Err(err)` over `Result<(),()>`)

**Source:** `src/compiler/sema_stmt.cpp#L3852-L3886`

### `pat.variant.binding-arity-check` — Variant payload binding count must match payload arity

The number of payload bindings in a variant-data pattern must equal the number of (non-unit) payload types of the variant.

**Source:** `src/compiler/sema_stmt.cpp#L3887-L3889`

## Union Patterns

### `pat.union.one-field-unsafe` — Union pattern names exactly one field inside unsafe

A pattern on a `union` must specify exactly one field (no `..`), and the match must occur inside an `unsafe` block (it reads the named field's memory). Violations are errors.

**Source:** `src/compiler/sema_stmt.cpp#L4981-L5005`

## Byte-String Patterns

### `pat.bytes.slice-of-int-subpatterns` — Byte-string pattern lowers to a fixed slice pattern of integer sub-patterns

A byte-string literal pattern `b"..."` matching N bytes is equivalent to a slice pattern of exactly N integer (u8) sub-patterns with no `..` rest: `[b0, b1, ..., b_{N-1}]`. It is an exact match (fixed length, no trailing rest).

**Source:** `src/compiler/sema_stmt.cpp#L3964-L3971`, `src/compiler/sema_stmt.cpp#L4051-L4062`

### `pat.bytes.escape-set` — Byte-string pattern escape sequences

Inside `b"..."` the recognized escapes are `\n`=0x0A, `\t`=0x09, `\r`=0x0D, `\0`=0x00, `\\`, `\'`, `\"`, and `\xHH` (two hex digits → byte HH). Any other escape is rejected; a malformed `\x` is rejected. Non-escaped bytes are taken verbatim.

**Source:** `src/compiler/sema_stmt.cpp#L3978-L4021`

### `pat.bytes.scrutinee-must-be-u8-array` — Byte-string pattern requires `[u8; N]` scrutinee

A byte-string pattern requires the scrutinee (after peeling a single `&`/`&mut` reference) to be a fixed-size array `[u8; N]`; otherwise it is an error. Dynamic `&[u8]` slice scrutinees are not supported.

**Divergence:** Rust permits byte-string patterns against `&[u8]`/`&[u8; N]`; Logos requires fixed `[u8; N]` and rejects dynamic slices.

**Source:** `src/compiler/sema_stmt.cpp#L4025-L4050`

### `pat.bytes.length-must-match-array` — Byte-string pattern length must equal scrutinee array length

For a `[u8; N]` scrutinee, the byte-string literal's byte count must equal N; a mismatch is an error.

**Source:** `src/compiler/sema_stmt.cpp#L4040-L4044`

### `pat.bytes.ref-array-autoderef` — Byte-string pattern sees through a reference to an array

A byte-string pattern matches against `&[u8; N]` or `&mut [u8; N]` by peeling exactly one reference layer (default binding modes auto-deref the reference), so the pattern operates on the underlying array.

**Source:** `src/compiler/sema_stmt.cpp#L4025-L4033`

## Or-Patterns

### `pat.or.alternatives` — Or-pattern

A pattern is one or more `pat_single` alternatives separated by `|`, with an optional leading `|`. A variant-payload arg may itself be an or-pattern `Some(A | B)`; a single alternative passes through transparently.

**Source:** `tools/peg_gen/grammars/logos.peg#L1962-L1978`

### `pat.or.pipe-separated` — Or-pattern

Alternative patterns are combined with `|`: `p0 | p1 | ...`.

**Source:** `src/compiler/sema_render.cpp#L622-L632`

### `pat.or.single-alt-transparent` — Single-alternative or-pattern is the inner pattern

An or-pattern node with exactly one alternative (no `|`) is equivalent to that single inner pattern.

> **CONFLICT FLAG:** another extraction of this id produced a differing statement. Both are surfaced below; resolve before publishing.

- Primary: An or-pattern node with exactly one alternative (no `|`) is equivalent to that single inner pattern.
- Alternate: A PAT_OR wrapper with exactly one alternative is semantically equivalent to that alternative (the grammar wraps every arm/element pattern in a single-alt or-wrapper which is unwrapped before matching).

**Source:** `src/compiler/sema_stmt.cpp#L4068-L4070`, `src/compiler/sema_stmt.cpp#L8130-L8134`, `src/compiler/sema_stmt.cpp#L8162-L8165`, `src/compiler/sema_stmt.cpp#L8486-L8489`, `src/compiler/sema_stmt.cpp#L8602-L8610`

### `pat.or.same-binding-set` — Or-pattern alternatives must bind the same variable names

Every `|` alternative of an or-pattern must bind exactly the same set of variable names (E0408). Synthetic compiler-introduced bindings (e.g. `__refut_*`, `__pat_pld_*`, `__sve_*`) and the wildcard `_` are excluded from this check.

**Source:** `src/compiler/sema_stmt.cpp#L4074-L4142`, `src/compiler/sema_stmt.cpp#L4254-L4280`

### `pat.or.nested-descends-first-alt` — Binding collection descends only the first alternative of a nested or-pattern

When collecting bindings of a pattern that contains a nested or-pattern, only the first alternative is traversed; the nested or-pattern's own same-binding-set check guarantees the remaining alternatives bind identically.

**Source:** `src/compiler/sema_stmt.cpp#L4112-L4117`, `src/compiler/sema_stmt.cpp#L4214-L4221`

### `pat.or.flatten-and-at-unwrap` — or-patterns flatten and @-bindings unwrap for coverage

For exhaustiveness, or-patterns (`p1 | p2`) are flattened to their alternatives and `@`-bindings (`name @ p`) are unwrapped to their inner sub-pattern.

**Source:** `src/compiler/sema_stmt.cpp#L7904-L7918`

### `pat.or.alt-binding-consistency` — or-pattern alternatives must bind identical names

Top-level arm or-alternations `A | B =>` must bind the same set of names in every alternative (E0408).

**Source:** `src/compiler/sema_stmt.cpp#L8517-L8520`

### `pat.or.semantics-distribution` — or-patterns match if any alternative matches

An or-pattern `P | Q` matches a scrutinee iff at least one alternative matches; binding-introducing or non-scalar alternatives are evaluated independently (each with its own payload extraction and refutable guard), while pure scalar-literal alternatives (`int`/`bool`/`char`) that bind nothing share a single merged discriminant test. A variant payload or-pattern `Some(P|Q)` is equivalent to `Some(P) | Some(Q)`.

**Source:** `src/compiler/sema_stmt.cpp#L8463-L8507`, `src/compiler/sema_stmt.cpp#L8521-L8531`

### `pat.or.inner-bindingless-only` — Or-pattern inner must be bindingless

An or-pattern inner `V(A | B)` is lowered to a `match synth { A | B => true, _ => false }` guard only if every alternative binds nothing (literals, unit variants, bindingless-data variants, or a bare wildcard `_`); a binding-carrying alternative is rejected.

**Source:** `src/compiler/sema_stmt.cpp#L3391-L3413`

## `let` Pattern Bindings

### `pat.let.refutability-checked` — let with complex pattern checked for refutability

`let <pattern> = expr;` for a pattern beyond a simple ident/tuple is an irrefutable destructure: sema checks the pattern is irrefutable and lowers it via `match`.

**Source:** `tools/peg_gen/grammars/logos.peg#L285`

## `let ... else` Patterns

### `pat.let-else.refutable-inner-guards` — Refutable inner-literal tests preserved in let-else

A let-else pattern with refutable inner sub-patterns (e.g. `let Some(1) = e else`) tests the inner literal in addition to the variant discriminant; these inner-value guards are evaluated AFTER the bindings are bound, not dropped.

```logos
let Some(1) = opt else { return; };
```

**Source:** `src/compiler/sema_stmt.cpp#L1593-L1602`, `src/compiler/sema_stmt.cpp#L1669`

### `pat.let-else.or-pattern-uniform-bindings` — or-pattern alternatives in let-else bind identical names/types

In an or-pattern let-else (`let A(x) | B(x) = v else …`) all alternatives must bind the same names with the same types; bindings are taken from the first alternative.

**Source:** `src/compiler/sema_stmt.cpp#L1650-L1658`

## Refutable Sub-Patterns (Inner Guards)

### `pat.refutable.nested-variant-guard` — Nested variant inner pattern lowers to a synthesized guard

A nested variant inner pattern (e.g. `Some(Color::Red)`, `Some(Some(v))`) binds the outer payload to a synthetic name and gates the arm with a synthesized `match synth { <inner> => <check>, _ => false }`; binding-carrying inners additionally re-extract their bindings in the arm body via a let-else, composing to arbitrary depth.

**Divergence:** A — guarded nested-variant arms need a catch-all for exhaustiveness (DIVERGENCES.md: finite-enum coverage of guarded arms not yet proven)

**Source:** `src/compiler/sema_stmt.cpp#L3284-L3453`

### `pat.refutable.range-inner-guard` — Range inner pattern lowers to `>= && <=` guard

A range inner pattern `V(lo..=hi)` (or `V(n @ lo..hi)`) binds the payload to `synth`/the @-name and gates the arm with `synth >= lo && synth <= hi`; an exclusive `lo..hi` lowers to `lo..=(hi-1)`. Under by-ref ergonomics the synth is dereferenced for the comparison.

**Source:** `src/compiler/sema_stmt.cpp#L3454-L3503`

### `pat.refutable.literal-inner-guard` — Literal inner pattern lowers to `==` guard (str via str_eq)

An int/neg-int/bool/char literal inner pattern binds the payload to `synth` and gates the arm with `synth == <literal>`; a string literal inner gates with `str_eq(synth, "..")` rather than pointer-comparing slices.

**Source:** `src/compiler/sema_stmt.cpp#L3510-L3560`, `src/compiler/sema_stmt.cpp#L3511-L3526`

### `pat.refutable.raw-pointer-rejected` — Match ergonomics excludes raw pointers

Binding-carrying nested-variant patterns over a raw-pointer (`*const`/`*mut`) scrutinee are rejected; match ergonomics (by-ref binding) applies only to `&`/`&mut`.

**Source:** `src/compiler/sema_stmt.cpp#L3334-L3336`

## `match` Construct

### `pat.match.exhaustive-required` — match must be exhaustive

A `match` over a scrutinee of type T must cover every value of T. If a reachable value is uncovered the program is rejected (e.g. enum: 'match is not exhaustive — missing variant(s): ...'; bool: 'match on bool is not exhaustive — missing true/false').

**Source:** `src/compiler/sema_stmt.cpp#L7540-L7542`, `src/compiler/sema_stmt.cpp#L7577-L7579`

### `pat.match.uninhabited-trivially-exhaustive` — match on uninhabited scrutinee is exhaustive

A match whose scrutinee type is uninhabited — the `Never` type, or an enum with zero variants — is trivially exhaustive and requires no arms; a bare `match x {}` is accepted.

**Source:** `src/compiler/sema_stmt.cpp#L7470-L7479`

### `pat.match.wildcard-covers` — unguarded wildcard arm makes match exhaustive

An arm whose pattern is an unguarded wildcard `_` covers all remaining values, making the match exhaustive regardless of which enum variants or bool values are otherwise matched.

**Source:** `src/compiler/sema_stmt.cpp#L7480-L7486`, `src/compiler/sema_stmt.cpp#L7512`

### `pat.match.guarded-arm-not-counted` — guarded arms do not contribute to exhaustiveness

An arm carrying a guard (`if <cond>`) is not counted toward exhaustiveness coverage; only unguarded arms cover variants/values, since a guard may fail at runtime.

**Source:** `src/compiler/sema_stmt.cpp#L7482`, `src/compiler/sema_stmt.cpp#L7503`, `src/compiler/sema_stmt.cpp#L7570`

### `pat.match.enum-variant-coverage` — enum match coverage by variant

For an enum scrutinee, an unguarded `Variant` or `VariantData` pattern covers that variant (identified by discriminant); an or-pattern covers the union of its alternatives. A match without a wildcard is exhaustive iff every (constructable) variant is covered.

**Source:** `src/compiler/sema_stmt.cpp#L7487-L7542`, `src/compiler/sema_stmt.cpp#L7494-L7511`

### `pat.match.uninhabited-variant-omittable` — variant with uninhabited payload may be omitted

An enum variant whose payload (after substituting the scrutinee's type arguments into the enum's type parameters) contains an uninhabited type can never be constructed and need not have an arm; its omission does not break exhaustiveness.

```logos
match r: Result<i32, Void> { Ok(x) => x }  // Err arm omittable when Void is empty
```

**Source:** `src/compiler/sema_stmt.cpp#L7516-L7535`

### `pat.match.redundant-wildcard-warn` — redundant wildcard arm over fully-covered unit enum

A wildcard arm is reported unreachable ('unreachable wildcard arm: every variant of the enum is already covered explicitly') when the enum is non-empty, all its variants are unit (payload-free), and every variant is explicitly covered. For payload-bearing variants the warning is suppressed (disc-only coverage cannot distinguish refutable inner patterns).

**Source:** `src/compiler/sema_stmt.cpp#L7543-L7564`

### `pat.match.bool-exhaustive` — bool match must cover true and false

A match on a `bool` scrutinee without a wildcard arm must include unguarded `true` and `false` patterns; otherwise it is rejected as non-exhaustive.

**Source:** `src/compiler/sema_stmt.cpp#L7567-L7580`

### `pat.match.scrutinee-moved-by-binding` — match consumes scrutinee when an arm moves out a value

Matching a by-value (move-type) scrutinee whose arm binds out a value by name marks the scrutinee place moved. This applies to: whole-value bindings, struct destructures that bind a move-only field by value, tuple destructures that bind a move-only element by value, and variant-data arms that bind a move-only payload by value. `_` bindings and `ref` bindings move nothing and leave the scrutinee live.

**Source:** `src/compiler/sema_stmt.cpp#L7604-L7820`, `src/compiler/sema_stmt.cpp#L7624-L7629`, `src/compiler/sema_stmt.cpp#L7677-L7685`, `src/compiler/sema_stmt.cpp#L7711-L7716`, `src/compiler/sema_stmt.cpp#L7725-L7816`

### `pat.match.scrutinee-ref-peeled` — pattern matching peels references

When checking exhaustiveness against a scrutinee type, reference and pointer layers (`&T`, `&mut T`, `*T`) are peeled (up to a fixed depth) to the underlying type before variant analysis; matching `&E` covers `E`'s variants.

**Source:** `src/compiler/sema_stmt.cpp#L7897-L7901`

### `pat.match.nested-payload-exhaustive` — exhaustiveness recurses into single-field variant payloads

A variant covered only by a refutable single-field payload pattern is treated as covered iff the inner patterns collectively exhaust the (substituted) payload type; an all-wildcard payload, an empty payload-arg list, or a bare `Variant` pattern covers the variant fully.

**Source:** `src/compiler/sema_stmt.cpp#L7967-L7991`

### `pat.match.guard-bool` — match guard must be bool

A match-arm guard expression `pat if <e> =>` must have type `bool`.

**Source:** `src/compiler/sema_stmt.cpp#L8655-L8660`

### `pat.match.arm-after-catchall-unreachable` — arm after unguarded catch-all is unreachable

Any match arm appearing after a prior unguarded catch-all (`_`) arm is an error: unreachable arm.

**Source:** `src/compiler/sema_stmt.cpp#L8270-L8284`

### `pat.match.guard-backtracks-alternatives` — failing guard backtracks to remaining or-alternatives

When a fanned-out or-alternative's guard fails, matching continues with the next alternative (Rust backtracking under a failing guard).

**Source:** `src/compiler/sema_stmt.cpp#L8475-L8482`

### `pat.match.string-literal-pattern` — string-literal pattern is content equality

A top-level string-literal arm `"foo" =>` matches by content equality on the scrutinee (`str_eq(scrut, "foo")`); the scrutinee is evaluated exactly once (hoisted into a temporary shared by all such arms).

**Source:** `src/compiler/sema_stmt.cpp#L8383-L8417`, `src/compiler/sema_stmt.cpp#L8613-L8618`, `src/compiler/sema_stmt.cpp#L8856-L8864`

### `pat.match.guard-runs-only-on-match` — guard runs only after the pattern matches

An arm's guard (synthesized structural/literal guard, string-eq guard, and user `if` guard) is conjoined with `&&` so it short-circuits and the user guard executes only when the pattern has matched.

**Source:** `src/compiler/sema_stmt.cpp#L8662-L8696`

### `pat.match.scrutinee-eval-once` — match scrutinee evaluated exactly once

The match scrutinee is evaluated exactly once; for Writ-pattern, string-pattern, or droppable-temporary scrutinees it is hoisted into a synthetic local that all arms/guards reference.

**Source:** `src/compiler/sema_stmt.cpp#L8212-L8243`, `src/compiler/sema_stmt.cpp#L8320-L8381`, `src/compiler/sema_stmt.cpp#L8407-L8417`

### `pat.match.temp-scrutinee-dropped` — temporary match scrutinee is dropped at match end

A move-typed match scrutinee that is a temporary (rvalue: call result, constructor, `?`; not a place such as a var/field/tuple-index/deref/index) is owned by the match and dropped on every exit path; if an arm moves the payload the drop is suppressed (no double-free). A place scrutinee is owned by its existing binding and not dropped by the match.

**Source:** `src/compiler/sema_stmt.cpp#L8202-L8243`, `src/compiler/sema_stmt.cpp#L8244-L8257`

### `pat.match.whole-value-binding-moves` — whole-value binding arm moves the scrutinee

An unguarded whole-value binding arm `x => ...` over an owned move-type scrutinee moves the scrutinee into `x` (equivalent to `let x = v;`); the moved scrutinee is not dropped a second time. Guarded binding arms do not unconditionally move (scrutinee stays live for later arms).

**Source:** `src/compiler/sema_stmt.cpp#L8259-L8268`

### `pat.match.arm-bindings-drop` — match-arm pattern bindings drop at arm end

Pattern bindings introduced by a match arm are dropped before the arm exits via fall-through, unless the body's tail moves the binding out (then its drop is suppressed) or the body ends in `return` (handled by full-frame drop collection).

**Source:** `src/compiler/sema_stmt.cpp#L8762-L8801`

### `pat.match.tail-vs-stmt-position` — expression arms in tail vs statement position

An expression-form arm `pat => <e>` in a tail-position match produces the function's return value (`return <e>`); in statement position it is evaluated for side effects only.

**Source:** `src/compiler/sema_stmt.cpp#L8727-L8738`

### `pat.match.exhaustiveness` — match must be exhaustive

A match over an enum/bool scrutinee must cover all cases; exhaustiveness is proved at the AST level over unguarded arms (user-guarded arms do not count toward exhaustiveness coverage).

**Source:** `src/compiler/sema_stmt.cpp#L8831-L8846`

### `pat.match.writ-pattern-view-scrutinee` — Writ patterns require a view scrutinee

A match using Writ scalar/structural patterns (null/bool/int/str/map/arr/typed-arr/typed-map) requires the scrutinee to be a Writ view (`Writ`, `WritView`, `WritStatic`, or a borrow thereof) and requires `use logos.lang.writ.pat;`; otherwise it is an error.

**Source:** `src/compiler/sema_stmt.cpp#L8286-L8358`

### `pat.match.or-alt-binding-consistency` — or-pattern alternatives must bind the same names

Every alternative of a top-level or-pattern arm (`A | B =>`) must bind the identical set of variable names (E0408).

**Source:** `src/compiler/sema_stmt.cpp#L9125-L9128`

### `pat.match.or-fanout-bindings` — or-patterns with bindings/non-scalar shapes fan out into separate arms

An or-pattern arm whose alternatives bind variables or have non-scalar/refutable shapes is expanded into one synthetic arm per alternative, each lowered through the normal single-arm path. Pure scalar-literal or-patterns (only PAT_INT/PAT_BOOL/PAT_CHAR alternatives) stay merged into a single arm.

**Source:** `src/compiler/sema_stmt.cpp#L9075-L9142`, `src/compiler/sema_stmt.cpp#L9084-L9095`

### `pat.match.variant-payload-or-distribution` — variant whose single payload is a multi-alt or-pattern distributes

A variant-data pattern whose single payload argument is a multi-alternative or-pattern with at least one non-merge-safe alternative is distributed: one synthetic arm per payload alternative (B170-E).

**Source:** `src/compiler/sema_stmt.cpp#L9096-L9118`, `src/compiler/sema_stmt.cpp#L9135-L9139`

### `pat.match.refutable-inner-guard` — refutable inner sub-patterns become AND-ed payload guards

A refutable inner sub-pattern in a variant payload (e.g. literal/variant like `Foo::FooUint(1)` or `Option::Some(1)`) contributes a guard testing the payload value; these guards are AND-ed into the arm guard so the arm matches only when both the variant tag and the inner value match (G145-2).

**Source:** `src/compiler/sema_stmt.cpp#L9185-L9189`, `src/compiler/sema_stmt.cpp#L9369-L9383`

## `for`-Loop Patterns

### `pat.for-loop.tuple-only` — for-loop pattern restricted to tuple of names/nested-tuples

A `for <pat> in <iter>` loop pattern that is destructured in place must be a tuple pattern `(p0, ..., pn)` over a tuple-typed element; each element pattern must be a name, `_`, or a nested tuple pattern. Any other element sub-pattern (literal, struct, variant, range, etc.) is rejected; a non-tuple top-level pattern over a non-tuple element is rejected (`bind a name and destructure in the body`).

**Divergence:** Subset of Rust for-loop pattern support (richer sub-patterns are a follow-up).

**Uncertainty:** Restriction is implementation-current, not a designed language limit.

**Source:** `src/compiler/sema_stmt.cpp#L8150-L8155`, `src/compiler/sema_stmt.cpp#L8184-L8188`

### `pat.for-loop.ref-element-deref` — by-ref for-loop element is dereferenced before destructure

When the iterated element type is `&T`/`&mut T`, the loop binding is dereferenced to a value temporary of type `T` and the tuple pattern destructures that value (by-ref default binding modes are not applied).

**Divergence:** A: no default-binding-mode by-ref propagation for refs in for-loop patterns.

**Uncertainty:** Inferred limitation per code comment.

**Source:** `src/compiler/sema_stmt.cpp#L8135-L8149`

### `pat.for-loop.discard-underscore` — underscore element binds nothing

A `_` element in a for-loop tuple pattern introduces no binding (the tuple element is discarded).

**Source:** `src/compiler/sema_stmt.cpp#L8177-L8179`

## Writ Patterns

### `pat.writ.scalar` — Writ scalar patterns

`@null`, `@true`/`@false`, `@N`/`@-N`, and `@"str"` are writ scalar patterns matching writ null, bool, integer, and string values respectively.

**Divergence:** Logos addition: Writ data-substrate patterns.

**Source:** `tools/peg_gen/grammars/logos.peg#L2092-L2106`

### `pat.writ.typed-container` — Writ typed map/array patterns

`@<T>{..}`, `@<T,R>{..}`, and `@<T>[..]` are typed writ map and array patterns annotating the matched container's element type(s).

**Divergence:** Logos addition: Writ typed-container patterns.

**Source:** `tools/peg_gen/grammars/logos.peg#L2107-L2112`

### `pat.writ.container` — Writ map/array patterns

`@{ key: pat, ... }` / `@{}` match writ maps; `@[ elem, ... ]` / `@[]` match writ arrays. Array elements admit a trailing `..` to match length ≥ n; map keys are string literals.

**Divergence:** Logos addition: Writ container patterns.

**Source:** `tools/peg_gen/grammars/logos.peg#L2028-L2041`, `tools/peg_gen/grammars/logos.peg#L2113-L2120`

### `pat.writ.match-only` — Writ scalar patterns only in match arms

Writ scalar patterns (`@null`, `@true`, `@false`, `@<int>`, `@"str"`, `@{...}`, `@[...]`, and typed array/map forms) are permitted only in `match` arms, not in if-let / while-let / let-bindings / nested pattern positions; elsewhere is an error. In a match arm they lower to a wildcard plus a synthesized guard.

**Divergence:** Logos extension (Writ value patterns); no Rust equivalent.

**Source:** `src/compiler/sema_stmt.cpp#L5086-L5104`

### `pat.writ.scalar-leaves` — Writ scalar leaf patterns

Within a Writ value pattern (@{...}/@[...]), the scalar leaves are: null (`@null`), bool (`@true`/`@false`), integer (`@<int>`), and string (`@"..."`). Each tests the corresponding AnyVal scrutinee: null-ness, boolean equality, integer equality, and string equality respectively.

**Divergence:** Writ pattern matching is a Logos addition (no Rust equivalent).

**Source:** `src/compiler/sema_stmt.cpp#L5293-L5334`, `src/compiler/sema_stmt.cpp#L5484-L5486`

### `pat.writ.int-i24-range` — Writ integer pattern fits i24

A Writ integer pattern `@<int>` value v must satisfy -2^23 <= v < 2^23 (i24 range); otherwise it is a compile error. The literal may carry a negation flag that negates the parsed magnitude.

**Divergence:** Logos addition; i24 bound is Writ-specific.

**Source:** `src/compiler/sema_stmt.cpp#L5312-L5327`

### `pat.writ.map-shape` — Writ map pattern

A Writ map pattern `@{k: p, ...}` matches iff the scrutinee is a map AND, for each listed entry key k, the key is present and its slot value matches sub-pattern p (conjunction over all entries). An entry without a value sub-pattern requires only presence of the key. Map patterns are non-exhaustive: keys not listed are ignored.

**Divergence:** Logos addition.

**Source:** `src/compiler/sema_stmt.cpp#L5495-L5524`

### `pat.writ.array-len-and-rest` — Writ array pattern length and rest

A Writ array pattern `@[p0, p1, ...]` matches iff the scrutinee is an array of exactly the listed element count and each element matches its sub-pattern. A trailing `..` rest changes the length check to >= (count of non-rest elements) and binds no further elements. `..` is permitted only as the LAST element; otherwise a compile error.

**Divergence:** Logos addition.

**Source:** `src/compiler/sema_stmt.cpp#L5525-L5562`

### `pat.writ.typed-array-element-types` — Typed Writ array pattern element types

A typed Writ array pattern `@<T>[..]` matches iff the scrutinee has the array type-code for element type T. T must be one of {I8,U8,I16,U16,I32,U32,I64,U64,F32,F64,AnyVal}; any other element type is a compile error.

**Divergence:** Logos addition.

**Source:** `src/compiler/sema_stmt.cpp#L5563-L5588`

### `pat.writ.typed-map-key-value-types` — Typed Writ map pattern key/value types

A typed Writ map pattern `@<K[,V]>{..}` matches iff the scrutinee has the map type-code for key type K. K must be one of {Varchar,I32,U32,I64,U64}; the value type V, if given, must be AnyVal. Any other key or value type is a compile error.

**Divergence:** Logos addition.

**Source:** `src/compiler/sema_stmt.cpp#L5589-L5616`

### `pat.writ.wildcard-binding` — Named wildcard inside Writ pattern binds the AnyVal

A wildcard with a non-`_` name inside a Writ pattern binds that name to the current AnyVal sub-value and always matches; a `_` (or empty) name binds nothing.

**Divergence:** Logos addition.

**Source:** `src/compiler/sema_stmt.cpp#L5487-L5494`

### `pat.writ.or-no-mixing` — Or-patterns may not mix Writ and non-Writ alternatives

In an or-pattern, if any alternative is a Writ pattern then all alternatives must be Writ patterns; mixing Writ patterns with non-Writ patterns is a compile error. An all-Writ or-pattern matches iff any alternative matches (disjunction).

**Divergence:** Logos addition.

**Source:** `src/compiler/sema_stmt.cpp#L5641-L5664`
