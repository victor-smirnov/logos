# Patterns

Pattern syntax and matching semantics of Logos — binding forms, literal/range/composite/or-patterns, refutability, `match` evaluation, and Writ patterns — extracted from the grammar (`tools/peg_gen/grammars/*.peg`), sema (`src/compiler/sema_*`), borrow-check, and MLIR codegen layers.

## Identifier patterns (`ident`)

### `pat.ident.const-value-not-binding` — Bare identifier pattern naming a module const matches by value

In `match scrutinee { NAME => ... }`, if `NAME` names a module-level `const`, the arm's pattern is evaluated (ctfe) against the const's retained initializer AST and lowered as a value pattern (int/bool/char), rather than binding the scrutinee to a fresh local named `NAME`.

Evidence: `src/compiler/sema_impl.hpp#L2899-L2903`

### `pat.ident.bare-no-payload-variant` — Bare identifier resolving to a no-payload enum variant is a variant pattern

When the scrutinee is an enum and a bare identifier (not `_`) names a no-payload variant of that enum, the identifier is treated as a variant pattern (refutable) rather than an irrefutable binding. This covers prelude variants (None/Some/Ok/Err) and user enums matched without the `Enum::` qualifier.

Evidence: `src/compiler/sema_stmt.cpp#L4793-L4817`

### `pat.ident.variant-alias` — Bare ident resolving to a use-imported nullary variant is a variant pattern

A bare identifier pattern that matches a `use Type.{V, ..}` variant alias and names a nullary (no-payload) variant is a variant pattern, not a fresh binding; the scrutinee enum must match, else error.

Evidence: `src/compiler/sema_stmt.cpp#L5106-L5132`

### `pat.ident.module-const-value` — Bare ident resolving to a module const is a value pattern

A bare identifier that names a module-level const is a value (refutable) pattern, not a binding: its initializer is ctfe-evaluated and matched. Bool/int/char consts emit a literal pattern. A non-ctfe-evaluable const initializer is an error.

Evidence: `src/compiler/sema_stmt.cpp#L5133-L5158` · `src/compiler/sema_stmt.cpp#L5247-L5251`

### `pat.ident.const-str-guard` — str-typed const pattern lowers to a str_eq guard

A const pattern of `str` (`Slice<u8>`) type against a str scrutinee binds a synthetic name and gates the arm with `str_eq(synth, CONST)`; requires the stdlib `str_eq` to be in scope, else error.

Evidence: `src/compiler/sema_stmt.cpp#L5159-L5168` · `src/compiler/sema_stmt.cpp#L5214-L5241`

### `pat.ident.const-bytearray-guard` — [u8; N] const pattern lowers to an element-wise equality guard

A const pattern of `[u8; N]` type against a `[u8; N]` scrutinee (matching array length) binds a synthetic name and gates the arm with the AND-chain `synth[i] == CONST[i]` for all i in 0..N.

Evidence: `src/compiler/sema_stmt.cpp#L5169-L5213`

### `pat.ident.const-nonscalar-unsupported` — Other non-scalar const patterns rejected

A const pattern whose value is neither int/bool/char nor the supported str/[u8;N] guard cases is an error ('non-scalar type').

> **Uncertainty.** Reflects current support boundary, not a permanent language restriction.

Evidence: `src/compiler/sema_stmt.cpp#L5242-L5246`

### `pat.ident.binding-and-mut` — Bare ident is a binding; `mut` recorded for mutable binding

A bare identifier not resolving to a variant or const is a fresh binding (`_` is non-binding/wildcard). A `mut x` binding is recorded so the binding is introduced as mutable.

Evidence: `src/compiler/sema_stmt.cpp#L5254-L5264`

## Bindings (`bind`)

### `pat.bind.wildcard-and-underscore-no-binding` — Wildcard '_' introduces no binding

A wildcard pattern binds a name only when that name is non-empty and not '_'; the literal '_' (and unnamed wildcards) bind nothing. Variant-data sub-bindings named '_' are likewise skipped.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3235-L3238` · `src/compiler/mlir_gen_stmt.cpp#L3257-L3259`

### `pat.bind.named-aggregate-binds-place` — Aggregate binding binds the place; scalar binding copies

Binding a name to a struct/zoned-struct/tuple value binds the storage address (place) without copying; binding a name to a scalar loads the value and stores it into a fresh local. Struct bindings additionally record the bound place's struct shape so field access GEPs through it.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3520-L3570`

### `pat.bind.fat-pointer-binds-pointer` — Slice/closure/trait-object binding binds the fat-pointer place

Binding a name to a slice, closure, or trait-object value (a 16-byte inline fat pair) binds the storage pointer (stored into a pointer-typed local), so the value convention is uniform with a pointer-valued scalar.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3541-L3561`

### `pat.bind.tuple-binds-elements-deref-ref` — Tuple binding binds each element, dereferencing a reference scrutinee

A tuple pattern binds each sub-pattern to the corresponding element (by index); a `&(T,U)`/`&mut (T,U)` scrutinee is dereferenced to obtain element types before binding.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3572-L3597`

### `pat.bind.variant-payload-binds-after-deref` — Variant payload binding dereferences reference layers then binds payload

Binding a variant-data pattern dereferences every `&`/`&mut` layer (e.g. `&&Option<T>` binds payload as `&&T`) to reach the inline enum storage, resolves the tagged-enum spec off the underlying enum type, then binds the variant's payload sub-patterns.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3598-L3633`

### `pat.bind.or-shared-bindings` — Or-pattern bindings share one storage per name across alternatives

An or-pattern's alternatives bind the same set of names with the same types; one shared local is allocated per bound name and each alternative binds into the shared local, so the join point observes a single storage per name.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3634-L3669`

### `pat.bind.struct-shorthand-and-subpatterns` — Struct pattern binding: shorthand binds field by its name

A struct pattern binds each named field; field shorthand `{x}` binds the field value to `x` (aggregate fields bind the place, scalar fields copy), `{x: a}` rebinds via the sub-pattern, and refutable subs bind their inner names.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3670-L3719`

### `pat.bind.ref-bind-binds-address` — `ref x` binds a borrow (the address), not a copy

A `ref x` binding (and default-binding-mode ref) binds `x : &T` to the scrutinee place's address without loading or copying; a ref-to-struct binds the pointer and records struct shape for field access, a scalar ref alloca-wraps the address so `*x` derefs one level.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3720-L3747`

### `pat.bind.tuple-destructure` — Tuple pattern destructures by position

A tuple pattern is irrefutable and binds each element positionally from the tuple's inline storage, recursing per element (wildcard, scalar, nested tuple, enum-payload, or-pattern).

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3857-L3862` · `src/compiler/mlir_gen_stmt.cpp#L3944-L3957`

### `pat.bind.struct-field-shorthand-rename` — Struct pattern field binding forms

In a struct pattern, `S { x }` binds field x under name x (shorthand); `S { x: a }` binds field x under name a (rename); both bind a struct-typed field as a place (its address, preserving mutation through the binding) and a scalar field by value.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3993-L4032` · `src/compiler/mlir_gen_stmt.cpp#L3998-L4024`

### `pat.bind.struct-field-ref` — ref binding to a struct field

In a struct pattern, `S { x: ref px }` binds px to a reference to field x; for a struct-typed field px aliases the field place (so `px.f` projects through it), for a scalar field px holds the field address.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4033-L4063`

### `pat.bind.struct-field-nested-subpattern` — Nested refutable sub-pattern in struct field

A refutable sub-pattern on a struct field (`S { x: Variant(..) }`, nested struct/tuple/or) binds its inner names recursively against the field place.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4064-L4075`

### `pat.bind.slice-array-fixed` — Slice pattern over a fixed array

A slice pattern over an array [T; N] binds prefix elements at indices 0,1,... and suffix elements at indices N-suffix_count,...,N-1; each element binds by value (`x`) or by reference (`ref x` → pointer to element).

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4080-L4120`

### `pat.bind.slice-dynamic-rest` — Slice pattern over a dynamic slice with rest binding

A slice pattern over a dynamic slice &[T] binds prefix elements at offsets 0,1,...; suffix elements relative to the runtime length at len-suffix_count+i; and a named rest `xs @ ..` to a first-class sub-slice {data+prefix, len-prefix-suffix} usable for `.len()` and re-matching.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4121-L4199` · `src/compiler/mlir_gen_stmt.cpp#L4164-L4174` · `src/compiler/mlir_gen_stmt.cpp#L4180-L4198`

### `pat.bind.at-binding` — @ pattern binds whole value and recurses

An `name @ subpat` pattern binds name to the whole matched value (or its place) and then binds the nested names of subpat against the same scrutinee.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4204-L4217`

### `pat.bind.ref-binding` — ref binding produces a reference to the scrutinee

A `ref name` pattern binds name to a reference (pointer) to the scrutinee place rather than moving/copying the value.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4219-L4245`

### `pat.bind.ref-pat-transparent` — &pat unwraps one reference level

A reference pattern `&pat` / `&mut pat` matches a reference scrutinee by binding the inner pattern against the referent.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4246-L4251`

### `pat.bind.or-pattern-bindings` — Or-pattern binds a consistent name set

An or-pattern `p1 | p2 | ...` binds the same set of names for every alternative; bindings are extracted from a single alternative's structure.

> **Uncertainty.** Codegen extracts from the first alternative only; the requirement that all alternatives bind the same names is enforced earlier (sema), not in this unit.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4252-L4260`

### `pat.bind.whole-value-binding` — Named wildcard binds (moves) the whole scrutinee

A named binding pattern `name` binds the entire scrutinee: an owned struct binding aliases the scrutinee's storage (by-pointer, single drop ownership transfers, scrutinee marked moved) while a scalar/other binding takes the value.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4261-L4292` · `src/compiler/mlir_gen_stmt.cpp#L4266-L4282`

### `pat.bind.variant-and-wild-introduce-bindings` — Pattern bindings introduced into scope

declare_pat_bindings(pat): a VariantData pattern declares each of its sub-bindings (by name, at its bind-slot) into the current scope; a Wild pattern declares its own binding only when its name is non-empty and not "_".

Evidence: `src/compiler/borrow_check.cpp#L1487-L1512`

### `pat.bind.wildcard-no-binding` — `_` binding name introduces no variable

A binding whose name is `_` introduces no variable into scope (across variant-data, tuple, struct-field, and wildcard patterns). This prevents a phantom binding from scheduling a drop on a payload the user discarded with `_`.

Evidence: `src/compiler/sema_stmt.cpp#L5691-L5694` · `src/compiler/sema_stmt.cpp#L5702-L5705` · `src/compiler/sema_stmt.cpp#L5726-L5734`

### `pat.bind.struct-generic-subst` — Struct pattern substitutes concrete type-args into field types

When matching a struct pattern against a concrete generic struct `S<A,B>`, each bound field's type is the field's declared type with the struct's type parameters substituted by the scrutinee's concrete type-args (e.g. `match s { S { x, y } }` over `S<u8,u16>` binds x:u8, y:u16). A `&`/`&mut`/`*` scrutinee is dereferenced first to obtain the type-args.

Evidence: `src/compiler/sema_stmt.cpp#L5768-L5783` · `src/compiler/sema_stmt.cpp#L5800-L5806`

### `pat.bind.default-binding-mode-struct` — Default binding modes for struct shorthand fields under a reference scrutinee

Under a `&`/`&mut` struct scrutinee, a shorthand field binding of a move-only field type T binds by reference (`&T` / `&mut T` matching the scrutinee's mutability) rather than moving the field out; Copy field types bind by value. Error and bare-TypeVar field types are excluded from the reference promotion.

> **Divergence.** RFC 2005 default binding modes (Rust-conformant intent).

Evidence: `src/compiler/sema_stmt.cpp#L5792-L5816`

### `pat.bind.or-alts-same-bindings` — Or-pattern alternatives bind identical names and types

All alternatives of an or-pattern bind the same set of names with the same types; bindings are declared from the first alternative.

Evidence: `src/compiler/sema_stmt.cpp#L5719-L5725` · `src/compiler/sema_stmt.cpp#L5829-L5834`

### `pat.bind.ref-pat-strips-ref` — Reference pattern strips one reference layer

A reference pattern `&p` binds its inner pattern against the pointee of a `&`/`&mut` scrutinee type; against a non-reference scrutinee the inner type is Error.

Evidence: `src/compiler/sema_stmt.cpp#L5744-L5751`

### `pat.bind.slice-rest-is-subslice` — Slice pattern element and named-rest types

In a slice pattern, prefix and suffix sub-patterns bind against the element type T of the scrutinee. A named rest `xs @ ..` binds the sub-slice as `&[T]` (slice type), not an element; an anonymous `..` rest binds nothing.

Evidence: `src/compiler/sema_stmt.cpp#L5819-L5828`

### `pat.bind.mut-binding-mode` — `mut` binding pattern marks variable mutable

A wildcard-name binding written with `mut` (tracked per pattern) declares the bound variable as mutable.

Evidence: `src/compiler/sema_stmt.cpp#L5726-L5733`

### `pat.bind.ref-does-not-move` — ref bindings and _ borrow/discard rather than move

A `ref`-bound binding borrows its place and a `_` (anonymous wildcard) discards it; neither consumes the matched value, recursively through nested tuple and struct sub-patterns. A by-value name binding (including struct-field shorthand `{ name }`) consumes the value.

Evidence: `src/compiler/sema_stmt.cpp#L7652-L7676` · `src/compiler/sema_stmt.cpp#L7733-L7769` · `src/compiler/sema_stmt.cpp#L7742`

## Binding modes and scope (`binding`)

### `pat.binding.wildcard-underscore-no-bind` — `_` binds nothing

A wildcard `_` (and an empty binding name) matches any value and introduces no binding; a named wildcard introduces a binding to a copy of the matched value.

Evidence: `src/compiler/mlir_gen_expr.cpp#L3893-L3903` · `src/compiler/mlir_gen_expr.cpp#L4950`

### `pat.binding.scope-limited-to-arm` — Pattern bindings are scoped to their arm

Names bound by an arm's pattern (and guard) are visible only within that arm's guard and body; they are removed from scope when the arm completes and are not visible to other arms.

Evidence: `src/compiler/mlir_gen_expr.cpp#L4345` · `src/compiler/mlir_gen_expr.cpp#L4359`

### `pat.binding.ident-or-wildcard` — Binding and wildcard patterns

A wildcard pattern is `_`; a named binding pattern is the identifier itself.

Evidence: `src/compiler/sema_render.cpp#L553-L557`

### `pat.binding.bare-name-vs-variant-or-const` — Bare name resolving to a no-payload variant or module const is not a binding

A bare identifier pattern that names a payload-less enum variant or a module-level const is a constant/variant pattern, not a new binding; otherwise it introduces a binding. `_` is never a binding.

Evidence: `src/compiler/sema_stmt.cpp#L4174-L4194`

### `pat.binding.or-alt-shared-slot` — Or-pattern alternatives share binding slots; distinct patterns start fresh

Within one top-level pattern, repeated binding names across or-pattern alternatives map to the SAME dense slot; binding-slot allocation is reset at the start of each top-level pattern, so separate match arms / let patterns allocate independent slots.

Evidence: `src/compiler/sema_stmt.cpp#L3012-L3023` · `src/compiler/sema_stmt.cpp#L3951-L3956`

### `pat.binding.default-by-ref-mode` — Default binding modes wrap payload bindings by reference

Under a `&`/`&mut` scrutinee, every plain named payload binding binds by-reference: the binding type is wrapped in `&`/`&mut` once per scrutinee ref-layer, with the outermost layer carrying mut iff any peeled layer was `&mut`. Bindings to `_` and synthesized slots are exempt.

> **Divergence.** Rust-conformant (RFC 2005); historical move-only-type restriction now lifted

Evidence: `src/compiler/sema_stmt.cpp#L3252-L3265` · `src/compiler/sema_stmt.cpp#L3915-L3949`

### `pat.binding.explicit-ref-mut` — `ref`/`ref mut` payload binding wraps type in &/&mut

An explicit `ref v` (resp. `ref mut v`) sub-pattern in a variant payload binds by reference: the binding type is wrapped in `&` (resp. `&mut`), binding the payload slot's address rather than a load. Explicit ref overrides default-binding-mode wrapping.

Evidence: `src/compiler/sema_stmt.cpp#L3680-L3690` · `src/compiler/sema_stmt.cpp#L3734-L3748` · `src/compiler/sema_stmt.cpp#L3918-L3923`

## Tuple bindings in `let` (`tuple-bind`)

### `pat.tuple-bind.let` — Let-binding tuple pattern

A let-binding tuple pattern admits `()` (unit), `..` rest (expanded to the right number of `_` skips), nested tuples `(a, (b, c))`, and identifier bindings. Rest fills remaining positions so names land on the correct tuple slots.

Evidence: `tools/peg_gen/grammars/logos.peg#L1943-L1960`

## Wildcard patterns (`wild`)

### `pat.wild.ident` — Identifier / wildcard pattern

A bare identifier is an irrefutable binding pattern (the matched value is bound to the name; `_` is the anonymous wildcard).

Evidence: `tools/peg_gen/grammars/logos.peg#L2237-L2238`

## Wildcard `_` (non-binding) (`wildcard`)

### `pat.wildcard.underscore-non-binding` — `_` and empty name are non-binding wildcards

A wildcard pattern named `_` (or unnamed) introduces no binding and reserves no slot; any other name in a wildcard position is a binding that reserves a fresh dense slot.

Evidence: `src/compiler/sema_stmt.cpp#L3004-L3010`

## `@` bindings (`at`)

### `pat.at.binds-whole-and-recurses` — `name @ sub` binds the whole value and matches sub

An `@`-pattern `name @ sub` binds name to the entire matched value and additionally requires the sub-pattern to match; matching dispatches on the sub-pattern (range, literal, or variant). An `@` over an irrefutable sub (e.g. `n @ _`) always matches.

Evidence: `src/compiler/mlir_gen_expr.cpp#L4191-L4210` · `src/compiler/mlir_gen_expr.cpp#L4673-L4720`

### `pat.at.binding-with-refutable-sub` — `name @ subpat` binds the whole value and gates on subpat

An at-pattern `name @ subpat` binds `name` to the entire scrutinee value and matches iff `subpat` matches. The sub-pattern may be an or-pattern (matches if any alternative — int/bool/range — matches), a range (inclusive bounds, signedness per scrutinee), or a scalar pattern (int/bool/variant-discriminant equality).

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4445-L4534`

### `pat.at.binding` — At-binding pattern

`name @ subpat` binds `name` to the value matched by `subpat`. `ref name @ subpat` binds by reference.

Evidence: `tools/peg_gen/grammars/logos.peg#L2043-L2052`

### `pat.at.binds-whole-and-sub` — @-pattern binds whole value at scrutinee type

An `name @ subpat` pattern binds `name` to the whole scrutinee value at the scrutinee type (falling back to the error type if unknown) while also matching `subpat` against the same scrutinee type.

Evidence: `src/compiler/sema_stmt.cpp#L4724-L4740`

## `@` bindings in payloads (`at-binding`)

### `pat.at-binding.payload-bind-and-guard` — `n @ sub` binds payload and applies sub-pattern guard

An `@`-binding `n @ <sub>` in a variant payload binds the payload to name `n` and additionally gates the arm with the refutable guard of `<sub>` (range/literal/variant) built against `n`; `n @ _` binds with no guard.

Evidence: `src/compiler/sema_stmt.cpp#L3504-L3509` · `src/compiler/sema_stmt.cpp#L3769-L3788`

## `ref` bindings and reference patterns (`ref`)

### `pat.ref.scalar-deref-test` — Reference pattern with scalar inner derefs then compares

Matching `&<scalar>` against a `&T`/`&mut T` scrutinee dereferences the reference (loads the pointee) and matches iff the loaded value equals the inner pattern's scalar discriminant.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4671-L4703`

### `pat.ref.redundant-ref-peel-over-tagged-enum` — Leading `&` on enum pattern over deref'd tagged-enum scrutinee is peeled

When a `&Enum` scrutinee has been auto-deref'd to its tagged-enum form, an explicit reference pattern `&E::Variant{..}` / `&E::Variant(x)` / `&S{..}` has its redundant leading `&` removed so the inner variant/struct pattern is tested through the normal payload-extracting path (the no-payload C-like `&E::A` case keeps the dedicated reference handler).

> **Uncertainty.** Auto-deref of &Enum to tagged form is established upstream in the same function; this unit only handles the resulting peel.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4323-L4338`

### `pat.ref.binding-mode` — ref / ref mut / mut bindings

`ref x` binds the matched place by shared reference; `ref mut x` by mutable reference; `mut x` introduces a fresh mutable binding by value.

Evidence: `tools/peg_gen/grammars/logos.peg#L2053-L2063`

### `pat.ref.reference-pattern` — Reference patterns

`&pat` and `&mut pat` match a reference, peeling one scrutinee reference layer. `&&pat` / `&&mut pat` (lexed as `AND`) peels two layers, producing nested reference patterns.

Evidence: `tools/peg_gen/grammars/logos.peg#L2064-L2085`

### `pat.ref.amp-mut` — Reference pattern

A reference pattern is `&pat` or `&mut pat`.

Evidence: `src/compiler/sema_render.cpp#L651-L659`

### `pat.ref.scrutinee-reference` — Reference pattern requires reference scrutinee

A reference pattern `&pat`/`&mut pat` requires a reference scrutinee. `&mut pat` requires a `&mut` scrutinee; `&pat` accepts both `&` and `&mut`. A non-reference scrutinee is an error. The inner pattern is matched against the pointee type.

Evidence: `src/compiler/sema_stmt.cpp#L4742-L4771`

## `ref`/`ref mut` binding semantics (`ref-binding`)

### `pat.ref-binding.added-indirection-depth` — `ref`/`ref mut` binding added-indirection depth

In a `ref`/`ref mut` pattern binding over an enum-variant payload field, sema wraps the binding's declared type in one extra `&`/`&mut` layer per match-ergonomics indirection. The ADDED depth = (ref-layers of the binding type) − (thin-ref layers already present on the payload type, which fold INTO the binding type and are not new indirection). A fat-ref (dyn/slice) payload contributes 0 thin layers and is excluded (`added_depth` reported as non-positive), deferring to dedicated fat-payload binding paths.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L19-L49`

### `pat.ref-binding.nested-depth-chain` — Multi-layer `ref` binding chains intermediate reference slots

When a `ref`/`ref mut` binding's added-indirection depth N is greater than 1 (match ergonomics threading a `ref` bind through nested references, e.g. matching through `&&Option<T>`), N−1 intermediate one-word stack slots are materialized, each holding the address of the previous layer; the final binding slot holds the depth-N reference value. Reading the bound name therefore requires N dereferences to reach the underlying value.

> **Uncertainty.** Depth>1 chaining path is documented in adjacent comments and implemented generically, but no concrete depth>1 example appears in this slice to confirm end-to-end.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L144-L161`

### `pat.ref-binding.binds-by-reference` — `ref`/`ref mut` binding takes a reference to the matched place

A `ref x` / `ref mut x` binding binds `x` to a `&T` / `&mut T` reference of the scrutinee type `T` (the place's address) rather than moving the value.

Evidence: `src/compiler/sema_stmt.cpp#L4774-L4792`

## `ref` binding (place reference) (`refbind`)

### `pat.refbind.binds-place-reference` — `ref`/`ref mut` binds a reference to the matched place

A `ref r` / `ref mut r` binding binds r to a reference (address) of the matched scrutinee or sub-place, without copying; dereferencing r reads through to the original value. A `ref`-binding pattern is irrefutable.

Evidence: `src/compiler/mlir_gen_expr.cpp#L4161-L4186` · `src/compiler/mlir_gen_expr.cpp#L4378-L4379`

## Reference patterns (`&pat`) (`refpat`)

### `pat.refpat.peels-reference` — `&pat` pattern matches through a reference

A reference pattern `&pat` / `&mut pat` over a reference-typed scrutinee dereferences the scrutinee and matches the inner pattern against the pointee. Over an already-derefed tagged-enum scrutinee, a redundant leading `&` on a variant/struct pattern is peeled.

Evidence: `src/compiler/mlir_gen_expr.cpp#L4187-L4190` · `src/compiler/mlir_gen_expr.cpp#L4306-L4312` · `src/compiler/mlir_gen_expr.cpp#L4646-L4672`

## Match ergonomics (`ergonomics`)

### `pat.ergonomics.deref-scrutinee` — Match ergonomics peel all &/&mut/* layers

Pattern matching peels all `&`, `&mut`, and `*` layers of the scrutinee type to obtain the concrete payload shape, so a pattern over `&&Enum<T>` (arbitrary depth) unifies against the inner `Enum<T>`.

> **Divergence.** Rust-conformant (RFC 2005 default binding modes)

Evidence: `src/compiler/sema_stmt.cpp#L3220-L3243` · `src/compiler/sema_stmt.cpp#L3828-L3851`

## Literal patterns (`lit`)

### `pat.lit.unit` — Unit pattern `()`

`()` is the unit pattern, matching the unit value.

Evidence: `tools/peg_gen/grammars/logos.peg#L2087-L2091`

### `pat.lit.integer` — Integer literal patterns

`N` matches an integer literal; `-N` matches a negative integer literal.

Evidence: `tools/peg_gen/grammars/logos.peg#L2186-L2189`

### `pat.lit.float-rejected` — Float-literal pattern rejected

A float-literal pattern parses but is rejected by sema with a diagnostic: float equality matching in patterns is deliberately not supported (IEEE equality semantics undefined).

> **Divergence.** Rust deprecated float patterns; Logos rejects them outright.

Evidence: `tools/peg_gen/grammars/logos.peg#L2195-L2199`

### `pat.lit.bytes-rejected` — Byte-string pattern rejected

A byte-string-literal pattern parses but is rejected by sema (pending &[u8] equality-matching codegen).

> **Uncertainty.** Status is provisional ("until codegen lands"); reflects current compiler behavior.

> **⚠ Conflict.** Conflicts with the `bytes` group ([`pat.bytes.slice-of-int-subpatterns`](#patbytesslice-of-int-subpatterns) et al.): this grammar-layer note says byte-string patterns are rejected by sema, while the sema-layer rules specify full lowering to fixed slice patterns of `u8` sub-patterns. The grammar note is likely stale; both are preserved.

Evidence: `tools/peg_gen/grammars/logos.peg#L2200-L2203`

### `pat.lit.string` — String-literal pattern

A string-literal pattern `"foo"` matches by string equality (lowered to a refutable `str_eq(scrut, "foo")` guard over a wildcard binding).

Evidence: `tools/peg_gen/grammars/logos.peg#L2204-L2207`

### `pat.lit.bool` — Bool patterns

`true` and `false` match the boolean values.

Evidence: `tools/peg_gen/grammars/logos.peg#L2208-L2211`

## Literal pattern semantics (`literal`)

### `pat.literal.string-content-compare` — String-literal pattern matches by content

Matching a value against a string-literal pattern compares string contents (via `str_eq`), not the two slice pointers. A raw `==` on string slices would pointer-compare; pattern matching uses content equality.

Evidence: `src/compiler/sema_impl.hpp#L489-L492`

### `pat.literal.int-bool-neg` — Literal patterns

Patterns may be integer literals (optionally negated with leading `-`), boolean literals (`true`/`false`), or unit `()`.

Evidence: `src/compiler/sema_render.cpp#L558-L572`

## Bool patterns (`bool`)

### `pat.bool.scrutinee-bool` — Bool pattern scrutinee constraint

A boolean-literal pattern requires a `bool` scrutinee; any other (non-error) scrutinee type is an error.

Evidence: `src/compiler/sema_stmt.cpp#L4445-L4455`

## Integer patterns (`int`)

### `pat.int.scrutinee-must-be-integer` — Integer pattern requires an integer scrutinee

An integer-literal pattern (incl. negated form) requires the scrutinee type to be an integer type; matching against a non-integer scrutinee is an error. The check is skipped when the scrutinee type is Error or `!` (never).

Evidence: `src/compiler/sema_stmt.cpp#L4313-L4321`

### `pat.int.value-must-fit` — Integer pattern value must fit the scrutinee integer type

The value of an integer-literal pattern must be representable in the scrutinee's integer type; an out-of-range value is an error.

Evidence: `src/compiler/sema_stmt.cpp#L4322-L4325`

## Float patterns (`float`)

### `pat.float.rejected-at-sema` — Float-literal patterns rejected

A float-literal pattern parses but is rejected at sema (not a valid match pattern).

> **Divergence.** Rust also forbids float patterns (deprecated/removed).

Evidence: `tools/peg_gen/grammars/logos.peg#L283`

### `pat.float.literal-rejected` — Float-literal patterns are rejected

A float-literal pattern is parsed but rejected as unsupported (IEEE-equality pattern semantics undecided).

> **Divergence.** Rust deprecated-but-still-accepts float patterns; Logos hard-rejects them.

Evidence: `src/compiler/sema_stmt.cpp#L4286-L4294`

## Char patterns (`char`)

### `pat.char.scalar-as-integer` — Char patterns lower to integer (Unicode scalar) patterns

A char-literal (and char-range) pattern is decoded to its Unicode scalar value and matched as an integer/range pattern, since `char` is a 4-byte Unicode scalar. Recognized escapes: `\n`,`\t`,`\r`,`\0`,`\\`,`\'`,`\"`,`\xHH` (exactly 2 hex digits), and `\u{HEX}`; a `\u` scalar must be <= U+10FFFF and outside the surrogate range U+D800..U+DFFF.

Evidence: `src/compiler/sema_stmt.cpp#L4330-L4396`

### `pat.char.scrutinee-char-or-int` — Char pattern scrutinee constraint

A char-literal pattern requires the scrutinee type to be `char` or an integer type; otherwise it is an error. The pattern matches the decoded code point as an integer constant.

Evidence: `src/compiler/sema_stmt.cpp#L4414-L4426`

## Char-range patterns (`char-range`)

### `pat.char-range.scrutinee-and-order` — Char range pattern constraints

A char-range pattern `lo..=hi` requires a `char` or integer scrutinee, and requires lo <= hi (decoded code points); lo > hi is an error. It matches the inclusive integer range [lo, hi].

Evidence: `src/compiler/sema_stmt.cpp#L4427-L4443`

## String-literal patterns (`str`)

### `pat.str.lowers-to-eq-guard` — String-literal pattern lowers to equality guard

A string-literal pattern `match s { "foo" => ... }` is matched by lowering to a `str_eq` guard.

Evidence: `tools/peg_gen/grammars/logos.peg#L312`

### `pat.str.position-restricted` — String-literal patterns allowed only in specific positions

String-literal patterns are supported only as a whole match arm (`match s { "foo" => .. }`), inside an enum-variant payload (`Some("foo")`), or as a tuple element (`("foo", _)`). In any other position (e.g. inside an array/slice pattern) a string-literal pattern is an error.

> **Divergence.** Rust permits string patterns in all pattern positions; Logos restricts them.

Evidence: `src/compiler/sema_stmt.cpp#L4296-L4312`

## Byte-string patterns (`bytes`)

### `pat.bytes.slice-of-int-subpatterns` — Byte-string pattern lowers to a fixed slice pattern of integer sub-patterns

A byte-string literal pattern `b"..."` matching N bytes is equivalent to a slice pattern of exactly N integer (u8) sub-patterns with no `..` rest: `[b0, b1, ..., b_{N-1}]`. It is an exact match (fixed length, no trailing rest).

> **⚠ Conflict.** Conflicts with [`pat.lit.bytes-rejected`](#patlitbytes-rejected) (grammar-layer note claiming sema rejection) — see note there.

Evidence: `src/compiler/sema_stmt.cpp#L3964-L3971` · `src/compiler/sema_stmt.cpp#L4051-L4062`

### `pat.bytes.escape-set` — Byte-string pattern escape sequences

Inside `b"..."` the recognized escapes are `\n`=0x0A, `\t`=0x09, `\r`=0x0D, `\0`=0x00, `\\`, `\'`, `\"`, and `\xHH` (two hex digits → byte HH). Any other escape is rejected; a malformed `\x` is rejected. Non-escaped bytes are taken verbatim.

Evidence: `src/compiler/sema_stmt.cpp#L3978-L4021`

### `pat.bytes.scrutinee-must-be-u8-array` — Byte-string pattern requires `[u8; N]` scrutinee

A byte-string pattern requires the scrutinee (after peeling a single `&`/`&mut` reference) to be a fixed-size array `[u8; N]`; otherwise it is an error. Dynamic `&[u8]` slice scrutinees are not supported.

> **Divergence.** Rust permits byte-string patterns against `&[u8]`/`&[u8; N]`; Logos requires fixed `[u8; N]` and rejects dynamic slices.

Evidence: `src/compiler/sema_stmt.cpp#L4025-L4050`

### `pat.bytes.length-must-match-array` — Byte-string pattern length must equal scrutinee array length

For a `[u8; N]` scrutinee, the byte-string literal's byte count must equal N; a mismatch is an error.

Evidence: `src/compiler/sema_stmt.cpp#L4040-L4044`

### `pat.bytes.ref-array-autoderef` — Byte-string pattern sees through a reference to an array

A byte-string pattern matches against `&[u8; N]` or `&mut [u8; N]` by peeling exactly one reference layer (default binding modes auto-deref the reference), so the pattern operates on the underlying array.

Evidence: `src/compiler/sema_stmt.cpp#L4025-L4033`

## Scalar leaf patterns (`scalar`)

### `pat.scalar.discriminant-equality` — Scalar leaf patterns match by discriminant/value equality

A leaf pattern that reduces to a scalar discriminant — an int literal (its value), a bool (true=1/false=0), or an enum variant (its discriminant) — matches iff the scrutinee equals that discriminant, compared in the scrutinee's integer type.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4297-L4308` · `src/compiler/mlir_gen_stmt.cpp#L4727-L4748`

## Unit patterns (`unit`)

### `pat.unit.no-binding` — `()` unit sub-pattern binds nothing

A `()` (unit) sub-pattern in a variant payload position introduces no binding.

Evidence: `src/compiler/sema_stmt.cpp#L3717`

## Range patterns (`range`)

### `pat.range.inclusive-bounds` — Range pattern matches lo <= scrut <= hi (both bounds inclusive)

A range pattern `lo..=hi` matches the scrutinee value s iff `lo <= s && s <= hi` (both bounds inclusive). The bound constants and the scrutinee are compared in the scrutinee's integer type; comparisons are unsigned when the scrutinee type is an unsigned integer or `char` (u8/u16/u24/u32/u56/u64/u128/usize/char), and signed otherwise.

Evidence: `src/compiler/mlir_gen_expr.cpp#L4394-L4404` · `src/compiler/mlir_gen_expr.cpp#L4458-L4482` · `src/compiler/mlir_gen_expr.cpp#L4679-L4699` · `src/compiler/mlir_gen_stmt.cpp#L4382-L4402`

### `pat.range.unsigned-predicate` — Range / scalar comparisons use unsigned ordering for unsigned scrutinees

For a scrutinee of unsigned integer type (u8/u16/u24/u32/u56/u64/u128), range-pattern bound comparisons use unsigned ordering (uge/ule); for signed scrutinees they use signed ordering (sge/sle).

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4309-L4317` · `src/compiler/mlir_gen_stmt.cpp#L4386-L4389`

### `pat.range.integer` — Integer range patterns

Integer range patterns include closed inclusive `lo..=hi`, closed exclusive `lo..hi`, and half-open forms `a..` (RangeFrom → [a, TYPE_MAX]), `..=b` (RangeToInclusive → [TYPE_MIN, b]), `..b` (RangeToExclusive → [TYPE_MIN, b-1]). Each endpoint may be negated (`-N`). Open bounds clamp to the scrutinee type's min/max.

Evidence: `tools/peg_gen/grammars/logos.peg#L2148-L2185`

### `pat.range.char` — Char patterns

`'a'` matches a char literal; `'a'..='z'` matches an inclusive char range.

Evidence: `tools/peg_gen/grammars/logos.peg#L2190-L2194`

### `pat.range.inclusive-only` — Range pattern is inclusive

A range pattern is written `lo..=hi` (inclusive); either bound may be negated with a leading `-`.

> **Uncertainty.** Renderer only emits `..=`; exclusive range patterns (if any) not represented here.

> **⚠ Conflict.** Conflicts with [`pat.range.exclusive-to-inclusive`](#patrangeexclusive-to-inclusive): this rule (from the sema renderer) states range patterns are written only as inclusive `lo..=hi`, while the sema lowering rule accepts an exclusive `lo..hi` form and lowers it to `lo..=(hi-1)`. Both are preserved; the exclusive form appears to be accepted by sema even if the renderer only emits the inclusive form.

Evidence: `src/compiler/sema_render.cpp#L633-L650`

### `pat.range.half-open-clamp` — Half-open range pattern clamps to scrutinee type bounds

In an integer range pattern, an omitted bound is clamped to the scrutinee integer type's min (for missing lo) or max (for missing hi); when the scrutinee type is unknown the bounds default to i32 range.

Evidence: `src/compiler/sema_stmt.cpp#L4654-L4676`

### `pat.range.scrutinee-integer` — Range pattern requires integer scrutinee

A range pattern requires an integer scrutinee type; a non-integer, non-error scrutinee is an error. A `never` scrutinee is exempted from this check.

> **Divergence.** Logos char ranges are handled separately (PAT_CHAR_RANGE); PAT_RANGE is integer-only.

Evidence: `src/compiler/sema_stmt.cpp#L4685-L4689`

### `pat.range.bounds-fit-type` — Range pattern bounds must fit scrutinee type

Both bounds of an integer range pattern must fit within the scrutinee integer type; a bound that does not fit is an error.

Evidence: `src/compiler/sema_stmt.cpp#L4690-L4698`

### `pat.range.exclusive-to-inclusive` — Exclusive range pattern lowered to inclusive minus one

An exclusive range pattern `lo..hi` is lowered as inclusive `lo..=(hi-1)`. An exclusive range with lo >= hi is an empty-range error. An inclusive range (default when unmarked) with lo > hi is an error.

> **⚠ Conflict.** Conflicts with [`pat.range.inclusive-only`](#patrangeinclusive-only) — see note there.

Evidence: `src/compiler/sema_stmt.cpp#L4699-L4720`

## Tuple patterns (`tuple`)

### `pat.tuple.element-binding-and-test` — Tuple pattern binds and tests elements positionally

A tuple pattern destructures the scrutinee positionally, binding each element sub-pattern; a tuple pattern with refutable element sub-patterns contributes a structural match test that AND-chains the element tests.

Evidence: `src/compiler/mlir_gen_expr.cpp#L3904-L3926` · `src/compiler/mlir_gen_expr.cpp#L4407-L4433`

### `pat.tuple.elementwise-binding` — Tuple pattern binds element-wise against tuple element types

A tuple pattern recurses into each sub-pattern paired positionally with the corresponding element type of the scrutinee tuple type; sub-patterns beyond the tuple's arity receive no type.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3240-L3248`

### `pat.tuple.structural-test` — Refutable tuple pattern matches element-wise structurally

A non-empty tuple pattern matches iff every sub-pattern matches the corresponding tuple element. The tuple value is its inline by-value storage (a pointer to its fields); element sub-patterns are tested against the fields in place.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4535-L4550`

### `pat.tuple.elem-rest` — Tuple-pattern rest element

A tuple-pattern element may be `..` (rest, converted to `_` wildcard skips preserving fixed arity) or an or-pattern of sub-patterns.

Evidence: `tools/peg_gen/grammars/logos.peg#L2015-L2026`

### `pat.tuple.shape` — Tuple pattern

`(a, b, ...)` is a tuple pattern (≥2 elements) admitting a `..` rest at any single position; `(x,)` (trailing comma) is a 1-tuple pattern, distinguished from a parenthesised pattern `(x)`.

Evidence: `tools/peg_gen/grammars/logos.peg#L2228-L2236`

### `pat.tuple.one-elem-trailing-comma` — Tuple pattern trailing comma

A tuple pattern is `(p0, p1, ...)`; a single-element tuple pattern requires a trailing comma `(p,)`.

Evidence: `src/compiler/sema_render.cpp#L608-L621`

### `pat.tuple.scrutinee-tuple-or-ref` — Tuple pattern over tuple or reference-to-tuple

A tuple pattern requires a tuple scrutinee, or a `&(T..)` / `&mut (T..)` scrutinee which is auto-dereferenced to the inner tuple (default binding mode). A non-tuple, non-(ref-to-tuple) scrutinee is an error.

Evidence: `src/compiler/sema_stmt.cpp#L4456-L4480`

### `pat.tuple.rest-expansion` — Tuple pattern `..` rest expansion

A tuple pattern may contain at most one `..` rest marker; a second `..` is an error. The rest is expanded into wildcard `_` skip entries inserted at the rest position so the explicit elements plus padding equal the tuple arity. More explicit elements than the arity (with `..`) is an error.

Evidence: `src/compiler/sema_stmt.cpp#L4481-L4520`

### `pat.tuple.element-kinds` — Allowed tuple pattern element kinds

Tuple pattern elements may be: wildcard/binding (`_`/name), integer/negative-integer/bool/range literals, variant-data patterns, string literals, and or-patterns. Any other element kind is an error ('only _, name, integer, bool, range, or variant patterns are supported').

Evidence: `src/compiler/sema_stmt.cpp#L4521-L4630`

### `pat.tuple.str-element-via-guard` — String-literal tuple element lowered to str_eq guard

A string-literal element of a tuple pattern binds the element to a synthesized name and adds a refutable `str_eq(synth, lit)` guard, rather than a value-equality test (a raw `==` would pointer-compare). Requires the refutable-guard context to be active.

> **Divergence.** Logos addition: tuple-arm codegen lacks a native str_eq dispatch, so string elements are desugared to guards.

Evidence: `src/compiler/sema_stmt.cpp#L4552-L4567` · `src/compiler/sema_stmt.cpp#L4600-L4617`

### `pat.tuple.single-alt-or-unwrap` — Single-alternative or-pattern element is unwrapped

The grammar wraps every tuple element in a PAT_OR node; a trivial single-alternative or-pattern is unwrapped and treated as its inner binding/wildcard/literal/variant. Multi-alternative or-patterns are kept as PatOr and their alternatives must be scalar (bindings inside multi-alt are dropped).

Evidence: `src/compiler/sema_stmt.cpp#L4568-L4623`

### `pat.tuple.arity-match` — Tuple pattern element count equals arity

After rest expansion, the number of tuple pattern elements must equal the tuple arity; a mismatch is an error.

Evidence: `src/compiler/sema_stmt.cpp#L4631-L4634`

### `pat.tuple.default-ref-move-only` — Default-ref binding for move-only tuple elements under shared borrow

When the tuple scrutinee is reached through a `&`/`&mut` (default binding mode), each tuple element binding whose element type is move-only (non-Copy, non-error, non-typevar) is bound by reference `&et`/`&mut et`; Copy elements are bound by value.

Evidence: `src/compiler/sema_stmt.cpp#L4456-L4461` · `src/compiler/sema_stmt.cpp#L4635-L4642`

## Tuple-struct patterns (`tuple-struct`)

### `pat.tuple-struct.bare-call-form` — Bare `Foo(a,b)` tuple-struct pattern destructures positional fields

An unqualified call-form pattern `Foo(p0, p1, ...)` whose name resolves to a tuple-struct destructures it as a struct pattern with synthetic positional field names "0","1",...; sub-pattern j binds field j.

Evidence: `src/compiler/sema_stmt.cpp#L3122-L3184`

### `pat.tuple-struct.arity-check` — Tuple-struct pattern arity must match (absent `..`)

Without a `..` rest, a tuple-struct pattern must supply exactly as many sub-patterns as the struct's field count; with `..`, the supplied count must not exceed the arity.

Evidence: `src/compiler/sema_stmt.cpp#L3168-L3175`

## Struct patterns (`struct`)

### `pat.struct.field-binding` — Struct pattern binds named fields, `..` ignores rest

A struct pattern `S { f0: x, .. }` matches by binding each named field's sub-pattern; shorthand `S { x }` binds field x; `S { .. }` binds nothing. A struct pattern is refutable iff any field sub-pattern is refutable, and its match test recurses into the refutable field sub-patterns.

Evidence: `src/compiler/mlir_gen_expr.cpp#L4071-L4160` · `src/compiler/mlir_gen_expr.cpp#L4378-L4381` · `src/compiler/mlir_gen_expr.cpp#L4434-L4457`

### `pat.struct.structural-test` — Refutable struct pattern matches all named field sub-patterns

A struct pattern `S { f: p, ... }` matches iff every field sub-pattern `p` matches the corresponding field; refutable field sub-patterns are tested structurally against the struct's in-place field storage.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4704-L4726`

### `pat.struct.field` — Struct-pattern field

A struct-pattern field is `..` (rest), `name: subpat`, `0: subpat` (tuple-struct field by index, resolved positionally), `ref name`, `ref mut name`, or a bare `name` shorthand binding the field to a same-named local.

Evidence: `tools/peg_gen/grammars/logos.peg#L1980-L1999`

### `pat.struct.shape` — Struct pattern

`Point { field_list }` / `Point {}` destructure a struct by named fields.

Evidence: `tools/peg_gen/grammars/logos.peg#L2138-L2142`

### `pat.struct.field-forms` — Struct pattern

A struct pattern is `Name { field [: subpat], ..., [..] }`; a field with no `: subpat` binds by field name (shorthand), and a trailing `..` ignores remaining fields.

Evidence: `src/compiler/sema_render.cpp#L661-L682`

### `pat.struct.field-shorthand-binds-name` — Struct field shorthand binds the field name

In a struct pattern, a field with no explicit sub-pattern (shorthand `Point { x }`) introduces a binding named after the field; a field with an explicit sub-pattern binds whatever that sub-pattern binds.

Evidence: `src/compiler/sema_stmt.cpp#L4201-L4209` · `src/compiler/sema_stmt.cpp#L4100-L4106`

### `pat.struct.unknown-name` — Struct pattern name must resolve

A struct pattern `N { .. }` requires `N` to resolve to a known struct or datatype; a type-alias `N` whose target is a Struct/ZonedStruct resolves transparently to the underlying struct (matched under the real name). Otherwise it is an error 'unknown struct'.

Evidence: `src/compiler/sema_stmt.cpp#L4821-L4847`

### `pat.struct.scrutinee-name-match` — Struct pattern must match scrutinee struct

If the scrutinee has a concrete (non-error, named) Struct type, a struct pattern's name must equal the scrutinee's struct name, else error 'struct pattern != scrutinee'.

Evidence: `src/compiler/sema_stmt.cpp#L4848-L4852`

### `pat.struct.field-exists` — Struct pattern field must exist

Each named field in a struct pattern must be a declared field of the struct; an unknown field name is an error 'has no field'.

Evidence: `src/compiler/sema_stmt.cpp#L4881-L4889`

### `pat.struct.rest-once-and-last` — Struct pattern `..` at most once and last

A struct pattern may contain at most one `..` rest element, and no named field may follow `..`; violations are errors.

Evidence: `src/compiler/sema_stmt.cpp#L4864-L4873`

### `pat.struct.exhaustive-fields` — Struct pattern must cover all fields unless `..`

A non-union struct pattern without `..` must name every field of the struct; an uncovered field is an error (suggesting `..`).

Evidence: `src/compiler/sema_stmt.cpp#L5006-L5016`

### `pat.struct.field-ref-shorthand` — `ref [mut] f` field shorthand binds a reference to the field

In a struct pattern, a field marked `ref` (optionally `ref mut`) with no explicit sub-pattern binds `f` to `&[mut] T` where `T` is the field type, equivalent to a `ref [mut] f` ref-binding sub-pattern.

Evidence: `src/compiler/sema_stmt.cpp#L4892-L4917`

### `pat.struct.shorthand-binds-field` — Shorthand field binds the field name

A plain shorthand field `{ f }` (no sub-pattern) binds a new variable named `f` to the field value; `_` is non-binding.

Evidence: `src/compiler/sema_stmt.cpp#L4971-L4976`

### `pat.struct.literal-field-guard` — Literal field sub-pattern lowers to a binding plus equality guard

A refutable literal field sub-pattern `S { f: <int|neg-int|bool|char> }` (in refutable context) binds the field to a fresh synthetic name and gates the arm with `synth == <literal>`.

Evidence: `src/compiler/sema_stmt.cpp#L4918-L4951`

### `pat.struct.refutable-sub-supported-kinds` — Limited set of refutable field sub-patterns

Refutable field sub-patterns are accepted only for kinds {Wild, RefBind, RefPat, At, Variant, VariantData, Tuple, Or, Range, Int, Bool, Struct}; other kinds (e.g. slice, writ) are an error 'not yet supported'.

> **Uncertainty.** List reflects current implementation coverage; the unsupported set is an implementation limitation rather than a settled language rule.

Evidence: `src/compiler/sema_stmt.cpp#L4952-L4969`

## Enum variant patterns (`variant`)

### `pat.variant.tuple` — Enum tuple-variant pattern

`E::V(args)` matches an enum tuple-variant payload; payload args are full nested patterns, possibly including `..` rest and or-patterns. A bare `Foo(a, b)` (no `::`) matches a tuple-struct when the name resolves as a tuple-struct rather than an enum.

Evidence: `tools/peg_gen/grammars/logos.peg#L2121-L2122` · `tools/peg_gen/grammars/logos.peg#L2132-L2137`

### `pat.variant.struct-shape` — Enum struct-variant pattern

`E::V { x, y: pat, .. }` / `E::V {}` match a struct-shaped enum variant; field names resolve to variant payload indices.

Evidence: `tools/peg_gen/grammars/logos.peg#L2123-L2129`

### `pat.variant.fieldless` — Fieldless variant pattern

`E::V` matches a fieldless (unit) enum variant.

Evidence: `tools/peg_gen/grammars/logos.peg#L2130-L2131`

### `pat.variant.path-and-data` — Variant pattern forms

An enum variant pattern is written `Enum::Variant` (data-less) or with data `Enum::Variant(args)`; the bare/tuple-struct form `Variant(args)` omits the enum qualifier.

Evidence: `src/compiler/sema_render.cpp#L573-L607`

### `pat.variant.prelude-shorthand-resolution` — bare variant names resolve via prelude/alias remap

A variant pattern written with only a variant name (no enum qualifier) resolves its enum: `Some`/`None` → `Option`, `Ok`/`Err` → `Result`, and otherwise via the importing module's variant aliases.

Evidence: `src/compiler/sema_stmt.cpp#L7922-L7944`

### `pat.variant.prelude-shorthand` — Prelude variant shorthand in patterns

Unqualified variant patterns `Some`/`None`/`Ok`/`Err` resolve to `Option`/`Result` variants when no enum qualifier is given and the prelude enum carries that variant.

Evidence: `src/compiler/sema_stmt.cpp#L3029-L3047` · `src/compiler/sema_stmt.cpp#L3094-L3114`

### `pat.variant.use-variant-alias` — `use Type.{V,..}` bare-variant alias resolves in patterns

A bare (unqualified) variant name in a pattern resolves to its enum when that name was imported via a `use Type.{V, ...}` variant-alias.

Evidence: `src/compiler/sema_stmt.cpp#L3048-L3053` · `src/compiler/sema_stmt.cpp#L3115-L3120`

### `pat.variant.type-alias-peel` — Type-alias to enum peels in variant patterns

A variant pattern `Alias::V` / `Alias::V(..)` where `type Alias<..> = Enum<..>` resolves the variant on the underlying enum; alias type-arguments do not affect which variant matches.

Evidence: `src/compiler/sema_stmt.cpp#L3055-L3066` · `src/compiler/sema_stmt.cpp#L3186-L3196`

### `pat.variant.unknown-enum-error` — Unknown enum / variant in pattern is an error

A variant pattern naming an enum not in scope, or a variant not declared by the resolved enum, is rejected.

Evidence: `src/compiler/sema_stmt.cpp#L3071-L3084` · `src/compiler/sema_stmt.cpp#L3202-L3208`

### `pat.variant.scrutinee-enum-match` — Variant pattern enum must equal scrutinee enum

When the scrutinee has a concrete enum type, a variant pattern naming a different enum is rejected.

Evidence: `src/compiler/sema_stmt.cpp#L3074-L3078`

### `pat.variant.type-arg-subst` — Generic enum type-args substitute into payload binding types

For a generic enum scrutinee `Enum<A,..>`, each variant payload binding's type is the declared payload type with the enum's type parameters substituted by the scrutinee's type-arguments (after peeling ref/ptr layers).

Evidence: `src/compiler/sema_stmt.cpp#L3236-L3242` · `src/compiler/sema_stmt.cpp#L3266-L3270` · `src/compiler/sema_stmt.cpp#L3825-L3857`

### `pat.variant.struct-shape-fields` — Struct-shape variant pattern resolves fields by name

A `E::V { f0, f1: p, .. }` pattern is allowed only for struct-shaped variants; named fields resolve to payload positions, shorthand `f` binds field `f` to name `f`, an unknown or duplicate field name is an error, and absent `..` every field must be specified.

Evidence: `src/compiler/sema_stmt.cpp#L3562-L3679`

### `pat.variant.tuple-shape-needs-parens` — Tuple-shape variant rejects brace pattern

A tuple-shaped variant (positional payload, no field names) cannot be matched with brace `{ .. }` pattern syntax.

Evidence: `src/compiler/sema_stmt.cpp#L3569-L3572`

### `pat.variant.nested-struct-tuple-destructure` — Irrefutable nested struct/tuple payload destructures in arm body

A nested struct- or tuple-pattern inside a variant payload binds the payload to a synthetic slot and emits an irrefutable `let <sub> = __synth;` destructure as an arm-body prologue.

Evidence: `src/compiler/sema_stmt.cpp#L3749-L3768`

### `pat.variant.unit-payload-binding` — Named binding against unit-typed payload is a zero-sized local

When a variant's payload types are all `()`, a `_` binding is dropped and a named binding is kept with a `()` binding type (a zero-sized local in scope), since unit fields are elided from the enum layout. The unit payload position itself is omitted from binding types.

> **Divergence.** Rust-conformant (rustc issue-41888 `Err(err)` over `Result<(),()>`)

Evidence: `src/compiler/sema_stmt.cpp#L3852-L3886`

### `pat.variant.binding-arity-check` — Variant payload binding count must match payload arity

The number of payload bindings in a variant-data pattern must equal the number of (non-unit) payload types of the variant.

Evidence: `src/compiler/sema_stmt.cpp#L3887-L3889`

## Variant payload binding (`payload`)

### `pat.payload.ref-to-struct-binds-place` — `ref` binding of a struct-typed payload binds a place

A `ref l` / `ref mut l` binding over a Struct/ZonedStruct-typed enum-variant payload field, at added-indirection depth exactly 1, binds `l` to the payload field's ADDRESS (a place) and records struct shape on it — equivalent to a `let l: &Struct` binding — so `l.field` resolves by GEP through `l`.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L117-L142`

### `pat.payload.thin-ref-struct-loads-pointer` — Thin-`&Struct` payload field loads the stored pointer

An enum-variant payload field declared `&Struct`/`&mut Struct`/`&ZonedStruct` (e.g. `E::S(&P)`, `Option<&P>`) holds a pointer inline; binding it LOADS that pointer value and tags it with struct shape, so field access on the bound name GEPs through the loaded pointer (not through the payload slot's own address).

Evidence: `src/compiler/mlir_gen_stmt.cpp#L163-L184`

### `pat.payload.inline-aggregate-binds-address` — Inline aggregate/bare-trait payload binds its address

An enum-variant payload field of Tuple/Slice/Closure type, or a bare `dyn Trait` value payload, held inline in the payload area binds the payload slot's ADDRESS as the bound value (no load) — the address IS the value for these inline-aggregate shapes; loading would read only the first machine word as the value.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L212-L221` · `src/compiler/mlir_gen_stmt.cpp#L262-L272`

### `pat.payload.move-type-binds-by-copy` — Move-type payload bound by value is copied to a fresh slot

Binding a move-type (needs-drop) inline Struct or Tuple enum-variant payload field BY VALUE copies the payload bytes to a freshly allocated slot rather than aliasing the scrutinee's own storage; the source's drop is suppressed for the matched scrutinee (the value logically moves out of it), so a later mutation of the scrutinee place inside the match arm must not retroactively alter the already-bound value.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L188-L211` · `src/compiler/mlir_gen_stmt.cpp#L222-L238`

### `pat.payload.scalar-binds-loaded-value` — Plain scalar payload field binds a loaded value

An enum-variant payload field that is none of {ref-binding, thin-ref-to-struct, inline struct, inline tuple, inline tagged-enum, dyn-trait} binds by LOADING its value out of the payload slot into a fresh (or shared) stack slot.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L270-L276` · `src/compiler/mlir_gen_stmt.cpp#L286-L294`

### `pat.payload.wildcard-skips-binding` — `_` payload position introduces no binding

In an enum-variant tuple-payload pattern, a field-binding position named `_` introduces no variable — no storage, no shape tracking is created for it; it purely consumes a payload position.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L95-L96`

### `pat.payload.dyn-trait-binds-handle` — `dyn Trait` payload field binds a trait-object handle

An enum-variant payload field of `dyn Trait` type — whether held inline as a bare trait-object value, or reached through a reference/raw-pointer (`&dyn Trait`/`&mut Trait`/`*dyn Trait`) — binds to a value tracked as a trait-object handle named by the trait, enabling dynamic dispatch through the bound name. The inline (bare) form binds the payload slot's address; the through-reference form binds the loaded handle value.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L252-L284`

## Union patterns (`union`)

### `pat.union.one-field-unsafe` — Union pattern names exactly one field inside unsafe

A pattern on a `union` must specify exactly one field (no `..`), and the match must occur inside an `unsafe` block (it reads the named field's memory). Violations are errors.

Evidence: `src/compiler/sema_stmt.cpp#L4981-L5005`

## Slice patterns (`slice`)

### `pat.slice.fixed-array-elements` — Slice pattern over a fixed array binds and tests positionally

A slice pattern `[a, b, ..]` over an array of length N binds prefix elements from index 0 upward and suffix elements (after `..`) from the tail at `N - suffix_count + i`; literal sub-patterns add positional equality constraints to the arm test.

Evidence: `src/compiler/mlir_gen_expr.cpp#L3927-L3978` · `src/compiler/mlir_gen_expr.cpp#L4513-L4565`

### `pat.slice.dynamic-length-gate` — Slice pattern over a dynamic slice gates on runtime length

A slice pattern over a runtime-length `&[T]` matches iff the runtime length equals the fixed element count (no `..`) or is at least the fixed count (trailing `..`). Prefix elements index from 0; suffix elements from `len - suffix_count + i`. Suffix elements after `..` are rejected by sema for dynamic slices; a named rest `rest @ ..` binds the sub-slice `{data + prefix_count, len - prefix_count - suffix_count}`.

Evidence: `src/compiler/mlir_gen_expr.cpp#L3979-L4068` · `src/compiler/mlir_gen_expr.cpp#L4566-L4645`

### `pat.slice.fixed-array-element-equality` — Slice pattern on fixed-size array matches by element equality

A slice pattern over a fixed-size array `[T; N]` (or a `&[T; N]`/`&mut [T; N]`) matches iff each non-wildcard prefix and suffix scalar sub-pattern equals the array element at its index; wildcard sub-patterns impose no constraint; suffix indices are `N - suffix_count + k`. A reference to the array is the array base pointer (single indirection).

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4551-L4611`

### `pat.slice.dynamic-length-check` — Slice pattern on dynamic slice gates on runtime length

A slice pattern over a dynamic slice `&[T]` (fat pointer {data, len}) matches iff: with no rest `..`, `len == prefix_count + suffix_count`; with a rest, `len >= prefix_count + suffix_count`; plus each non-wildcard scalar prefix-element equals the corresponding runtime element.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4612-L4670`

### `pat.slice.elem-rest` — Slice-pattern rest binding

A slice-pattern element may be `name @ ..` (binds the trailing/middle sub-slice to a name), `..` (anonymous rest), or a regular sub-pattern.

Evidence: `tools/peg_gen/grammars/logos.peg#L2004-L2013`

### `pat.slice.shape` — Slice pattern

`[elems]` / `[]` match a slice/array by element patterns.

Evidence: `tools/peg_gen/grammars/logos.peg#L2143-L2147`

### `pat.slice.scrutinee-array-or-slice` — Slice pattern requires array/slice scrutinee

A slice pattern `[..]` requires the scrutinee to be of array or slice type; the element sub-patterns are typed by the element type. A non-array/slice scrutinee is an error.

Evidence: `src/compiler/sema_stmt.cpp#L5024-L5032`

### `pat.slice.rest-once` — Slice pattern `..` at most once

A slice pattern may contain at most one `..` rest; elements before `..` form the prefix, elements after form the suffix. A second `..` is an error.

Evidence: `src/compiler/sema_stmt.cpp#L5043-L5058`

### `pat.slice.rest-binding` — Named `..` binds the rest sub-slice

A rest element written `name @ ..` binds `name` to the rest sub-slice; a bare `..` is anonymous.

Evidence: `src/compiler/sema_stmt.cpp#L5048-L5053`

### `pat.slice.array-length-check` — Fixed-array slice pattern length constraints

For a fixed-size array scrutinee: a slice pattern without `..` must have exactly `N` elements; with `..`, prefix+suffix count must not exceed `N`. Violations are errors.

Evidence: `src/compiler/sema_stmt.cpp#L5063-L5076`

## Rest patterns (`..`) (`rest`)

### `pat.rest.dotdot` — Rest pattern

A rest/ignore-remaining pattern is written `..` and may appear among struct or tuple subpatterns.

Evidence: `src/compiler/sema_render.cpp#L660-L660` · `src/compiler/sema_render.cpp#L675-L677`

### `pat.rest.single-only` — At most one `..` rest per tuple/tuple-struct pattern

A tuple-struct or tuple-variant pattern may contain at most one `..` rest; sub-patterns before the rest bind low positions and those after bind tail positions, with skipped positions binding nothing.

Evidence: `src/compiler/sema_stmt.cpp#L3140-L3167` · `src/compiler/sema_stmt.cpp#L3718-L3733`

## Grouped patterns (`group`)

### `pat.group.paren` — Parenthesised / grouped pattern

`(P)` is exactly P and `(P | Q | ...)` is a grouped or-pattern (inlined into a single or-pattern at that position). `(..)` matches any tuple binding nothing (irrefutable wildcard).

Evidence: `tools/peg_gen/grammars/logos.peg#L2212-L2227`

## Or-patterns (`or`)

### `pat.or.alternative-binding-uniformity` — Or-pattern alternatives must bind the same names

In an or-pattern `a | b | ...`, every alternative must bind the same set of names with the same payload shape; a match succeeds if any alternative matches. A wildcard or binding alternative is irrefutable and matches unconditionally.

Evidence: `src/compiler/mlir_gen_expr.cpp#L4211-L4224` · `src/compiler/mlir_gen_expr.cpp#L4483-L4512`

### `pat.or.bindings-from-first-alternative` — Or-pattern bindings are taken from its first alternative

The set of variable bindings introduced by an or-pattern (p1 | p2 | ...) is determined by its first alternative; all alternatives are required to bind the same names.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3263-L3267`

### `pat.or.any-alternative-matches` — Or-pattern matches if any alternative matches

An or-pattern `a | b | ...` matches iff at least one alternative matches; alternatives are tested in order and the first match selects the arm. An alternative that is a wildcard or a bare binding (`_`, `n`) is irrefutable and matches unconditionally.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4405-L4444` · `src/compiler/mlir_gen_stmt.cpp#L4420-L4428`

### `pat.or.alternatives` — Or-pattern

A pattern is one or more `pat_single` alternatives separated by `|`, with an optional leading `|`. A variant-payload arg may itself be an or-pattern `Some(A | B)`; a single alternative passes through transparently.

Evidence: `tools/peg_gen/grammars/logos.peg#L1962-L1978`

### `pat.or.alt-binding-consistency` — Or-pattern alternatives must bind identical variable sets (E0408)

Every alternative of a top-level or-pattern (e.g. a match-arm alternation `A | B =>`) must bind the same set of variable names; mismatched bindings across alternatives are rejected (E0408 analog).

Evidence: `src/compiler/sema_impl.hpp#L759-L765` · `src/compiler/sema_stmt.cpp#L8517-L8520`

### `pat.or.shared-binding-slot` — Or-pattern alternatives share one slot per binding name

Within a single top-level pattern, a binding name introduced in multiple Or-pattern alternatives (`Some(x) | Other(x)`) resolves to ONE shared storage slot; the slot is reserved at build time and the build-local name→slot map is cleared at each top-level pattern entry.

Evidence: `src/compiler/sema_impl.hpp#L2317-L2331`

### `pat.or.payload-distributes-per-alternative` — Or-pattern nested in a variant payload fans out into one arm per alternative

An or-pattern occurring inside a variant-payload position (`Some((a,_)|(_,a))`) is lowered by iterating its alternative indices and building one effective match arm per alternative (`Some(P)|Some(Q)` ⇒ arm-per-alt), each re-evaluating its own guard and bindings independently — matching rustc's per-alternative guard backtracking (a failing guard on one alternative does not skip the whole arm).

Evidence: `src/compiler/sema_impl.hpp#L4211-L4216`

### `pat.or.pipe-separated` — Or-pattern

Alternative patterns are combined with `|`: `p0 | p1 | ...`.

Evidence: `src/compiler/sema_render.cpp#L622-L632`

### `pat.or.same-binding-set` — Or-pattern alternatives must bind the same variable names

Every `|` alternative of an or-pattern must bind exactly the same set of variable names (E0408). Synthetic compiler-introduced bindings (e.g. `__refut_*`, `__pat_pld_*`, `__sve_*`) and the wildcard `_` are excluded from this check.

Evidence: `src/compiler/sema_stmt.cpp#L4074-L4142` · `src/compiler/sema_stmt.cpp#L4254-L4280`

### `pat.or.nested-descends-first-alt` — Binding collection descends only the first alternative of a nested or-pattern

When collecting bindings of a pattern that contains a nested or-pattern, only the first alternative is traversed; the nested or-pattern's own same-binding-set check guarantees the remaining alternatives bind identically.

Evidence: `src/compiler/sema_stmt.cpp#L4112-L4117` · `src/compiler/sema_stmt.cpp#L4214-L4221`

### `pat.or.flatten-and-at-unwrap` — or-patterns flatten and @-bindings unwrap for coverage

For exhaustiveness, or-patterns (`p1 | p2`) are flattened to their alternatives and `@`-bindings (`name @ p`) are unwrapped to their inner sub-pattern.

Evidence: `src/compiler/sema_stmt.cpp#L7904-L7918`

### `pat.or.semantics-distribution` — or-patterns match if any alternative matches

An or-pattern `P | Q` matches a scrutinee iff at least one alternative matches; binding-introducing or non-scalar alternatives are fanned out into independent effective arms (each with its own payload extraction and refutable guard), while pure scalar-literal alternatives (`int`/`bool`/`char`) that bind nothing share a single merged discriminant test. A variant payload or-pattern `Some(P|Q)` (single payload arg) is equivalent to `Some(P) | Some(Q)` and is likewise fanned out when any alternative is non-scalar.

Evidence: `src/compiler/sema_stmt.cpp#L8765-L8809` · `src/compiler/sema_stmt.cpp#L8810-L8842`

### `pat.or.single-alt-transparent` — Single-alternative or-pattern is transparent

A PAT_OR node with exactly one alternative (no `|`) is semantically equivalent to that single inner pattern. The grammar wraps every arm/element pattern in a single-alt or-wrapper, which is unwrapped to the inner pattern before matching/destructuring/binding.

Related: [`pat.or.alt-binding-consistency`](#patoralt-binding-consistency).

Evidence: `src/compiler/sema_stmt.cpp#L8272-L8276` · `src/compiler/sema_stmt.cpp#L8304-L8307` · `src/compiler/sema_stmt.cpp#L8384-L8388` · `src/compiler/sema_stmt.cpp#L8788-L8791` · `src/compiler/sema_stmt.cpp#L8837-L8841` · `src/compiler/sema_stmt.cpp#L8904-L8912`

### `pat.or.inner-bindingless-only` — Or-pattern inner must be bindingless

An or-pattern inner `V(A | B)` is lowered to a `match synth { A | B => true, _ => false }` guard only if every alternative binds nothing (literals, unit variants, bindingless-data variants, or a bare wildcard `_`); a binding-carrying alternative is rejected.

Evidence: `src/compiler/sema_stmt.cpp#L3391-L3413`

## Match guards (`guard`)

### `pat.guard.bind-then-evaluate` — Match guard evaluated after the arm's bindings are in scope

When an arm has a guard `if g`, the arm's pattern bindings are extracted and made visible to `g`; `g` is then evaluated as a boolean; if it holds the arm body runs (reusing the already-established bindings), otherwise control proceeds to the next arm. A failing guard does not fall through to other arms that share the same pattern test — the whole arm is skipped.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4346-L4358` · `src/compiler/mlir_gen_stmt.cpp#L4359-L4366`

## Refutable sub-pattern lowering (`refutable`)

### `pat.refutable.nested-variant-guard` — Nested variant inner pattern lowers to a synthesized guard

A nested variant inner pattern (e.g. `Some(Color::Red)`, `Some(Some(v))`) binds the outer payload to a synthetic name and gates the arm with a synthesized `match synth { <inner> => <check>, _ => false }`; binding-carrying inners additionally re-extract their bindings in the arm body via a let-else, composing to arbitrary depth.

> **Divergence.** A — guarded nested-variant arms need a catch-all for exhaustiveness (DIVERGENCES.md: finite-enum coverage of guarded arms not yet proven)

Evidence: `src/compiler/sema_stmt.cpp#L3284-L3453`

### `pat.refutable.range-inner-guard` — Range inner pattern lowers to `>= && <=` guard

A range inner pattern `V(lo..=hi)` (or `V(n @ lo..hi)`) binds the payload to `synth`/the @-name and gates the arm with `synth >= lo && synth <= hi`; an exclusive `lo..hi` lowers to `lo..=(hi-1)`. Under by-ref ergonomics the synth is dereferenced for the comparison.

Evidence: `src/compiler/sema_stmt.cpp#L3454-L3503`

### `pat.refutable.literal-inner-guard` — Literal inner pattern lowers to `==` guard (str via str_eq)

An int/neg-int/bool/char literal inner pattern binds the payload to `synth` and gates the arm with `synth == <literal>`; a string literal inner gates with `str_eq(synth, "..")` rather than pointer-comparing slices.

Evidence: `src/compiler/sema_stmt.cpp#L3510-L3560` · `src/compiler/sema_stmt.cpp#L3511-L3526`

### `pat.refutable.raw-pointer-rejected` — Match ergonomics excludes raw pointers

Binding-carrying nested-variant patterns over a raw-pointer (`*const`/`*mut`) scrutinee are rejected; match ergonomics (by-ref binding) applies only to `&`/`&mut`.

Evidence: `src/compiler/sema_stmt.cpp#L3334-L3336`

## Refutability semantics (`refute`)

### `pat.refute.wildcard-and-ref-irrefutable` — Wildcard and ref-binding patterns are irrefutable

A wildcard pattern `_`/binder and a `ref`-binding pattern impose no match constraint (their refutability test is always true).

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3283-L3285`

### `pat.refute.literal-int-bool-equality` — Integer/bool literal patterns test value equality

An integer or bool literal pattern matches iff the scrutinee value loaded at the scrutinee's type equals the literal (bool encoded 1/0); the literal constant is coerced to the scrutinee's element type before comparison.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3286-L3294`

### `pat.refute.range-inclusive-bounds` — Range pattern tests inclusive low<=v<=high

A range pattern matches iff lo <= v <= hi (both bounds inclusive). The comparison is unsigned when the scrutinee type is one of {u8,u16,u32,u64,usize,char,bool}, signed otherwise.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3278-L3281` · `src/compiler/mlir_gen_stmt.cpp#L3295-L3306`

### `pat.refute.tuple-elementwise-conjunction` — Tuple pattern matches by conjunction of element tests

A tuple pattern matches iff every sub-pattern matches its corresponding element (positional, by index); the overall test is the conjunction of element tests. Element addresses are taken in place; a tuple value is stored inline (the scrutinee place IS the tuple storage, no extra load).

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3307-L3326`

### `pat.refute.variant-discriminant-test` — Enum variant pattern tests discriminant equality only

A variant pattern (with or without payload) matches iff the scrutinee enum's discriminant equals the variant's discriminant (an i32 compare). Payload sub-patterns contribute only bindings; refutable inner sub-patterns are checked separately via the match guard channel, not by this test.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3327-L3383`

### `pat.refute.c-like-enum-is-discriminant` — C-like (all-nullary) enum value is its discriminant

An enum with no tagged payload (all-nullary) is represented as a bare i32 discriminant with no heap/struct storage; a variant pattern on it loads that i32 and compares to the variant discriminant.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3358-L3369`

### `pat.refute.ref-enum-peels-all-layers` — Variant test through references peels every reference layer

When the scrutinee type is `&`/`&mut` (to arbitrary depth) wrapping an enum, the discriminant test dereferences once per reference layer to reach the inline enum storage before reading the discriminant; the tagged-enum spec is resolved off the underlying enum type, not the reference wrapper.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3340-L3357` · `src/compiler/mlir_gen_stmt.cpp#L3374-L3377`

### `pat.refute.or-disjunction` — Or-pattern matches if any alternative matches

An or-pattern `P1 | P2 | ...` matches iff at least one alternative matches; the test is the disjunction of the alternatives' tests, each evaluated against the same scrutinee place and type.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3384-L3391`

### `pat.refute.struct-field-conjunction` — Struct pattern matches by conjunction of refutable field tests

A struct pattern matches iff every refutable named-field sub-pattern matches its field; the test is the conjunction over fields. Irrefutable field subs (bind / shorthand / wildcard / ref-bind) contribute no constraint. The scrutinee place IS the struct storage; each field is GEP'd by name.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3392-L3429`

### `pat.refute.ref-pattern-derefs-then-recurses` — Reference pattern `&P` dereferences then tests inner

A reference pattern `&P` / `&mut P` against a reference-typed scrutinee loads the reference (the pointed-to address) and tests P against the pointee type; against a non-reference scrutinee the inner is tested in place.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3430-L3444`

### `pat.refute.at-binding-tests-sub` — At-binding `name @ P` is the refutability of P

An at-binding pattern `name @ P` is irrefutable in its binding part; its match constraint is exactly the refutability test of the sub-pattern P.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3445-L3449`

### `pat.refute.array-pattern-no-length-gate` — Fixed-array slice pattern: no length gate, positional prefix/suffix tests

A slice pattern against a `[T;N]` array (or reference to one) has fixed length, so it imposes no length check; it tests prefix sub-patterns at indices 0.. and suffix sub-patterns at indices N-suffix_count.. , conjoining all non-wildcard sub-tests.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3450-L3480`

### `pat.refute.slice-pattern-length-gate` — Dynamic slice pattern gates on length then tests elements

A slice pattern against a dynamic `&[T]` first compares the runtime length: with a rest binding `..` the slice must have length >= prefix_count+suffix_count, otherwise exactly == prefix_count+suffix_count; on success prefix elements are tested positionally against the data pointer. This length comparison is conjoined with element sub-tests.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3481-L3508`

## `let` patterns (`let`)

### `pat.let.refutability-checked` — let with complex pattern checked for refutability

`let <pattern> = expr;` for a pattern beyond a simple ident/tuple is an irrefutable destructure: sema checks the pattern is irrefutable and lowers it via `match`.

Evidence: `tools/peg_gen/grammars/logos.peg#L285`

## `let`-`else` patterns (`let-else`)

### `pat.let-else.refutable-inner-guards` — Refutable inner-literal tests preserved in let-else

A let-else pattern with refutable inner sub-patterns (e.g. `let Some(1) = e else`) tests the inner literal in addition to the variant discriminant; these inner-value guards are evaluated AFTER the bindings are bound, not dropped.

```logos
let Some(1) = opt else { return; };
```

Evidence: `src/compiler/sema_stmt.cpp#L1593-L1602` · `src/compiler/sema_stmt.cpp#L1669`

### `pat.let-else.or-pattern-uniform-bindings` — or-pattern alternatives in let-else bind identical names/types

In an or-pattern let-else (`let A(x) | B(x) = v else …`) all alternatives must bind the same names with the same types; bindings are taken from the first alternative.

Evidence: `src/compiler/sema_stmt.cpp#L1650-L1658`

## `for`-loop patterns (`for-loop`)

### `pat.for-loop.destructure-pattern` — `for PATTERN in iter` destructures non-trivial patterns via body-prologue lets

A `for PATTERN in iter` loop variable that is not a bare single identifier (the NAME fast-path, handled by the caller before this reaches here) is bound by emitting body-prologue destructure `let`s against the per-element local, via the same nested-destructure mechanism used for match/if-let payloads. A PATTERN shape this path does not (yet) handle fails with a diagnostic rather than miscompiling silently.

> **Uncertainty.** Only the declaration + doc-comment are in this slice; the implementation body (which pattern shapes are actually handled) lives outside L3969-4492.

Evidence: `src/compiler/sema_impl.hpp#L4232-L4240`

### `pat.for-loop.tuple-only` — for-loop pattern restricted to tuple of names/nested-tuples

A `for <pat> in <iter>` loop pattern that is destructured in place must be a tuple pattern `(p0, ..., pn)` over a tuple-typed element; each element pattern must be a name, `_`, or a nested tuple pattern (recursed). Any other element sub-pattern (literal, struct, variant, range, etc.) is rejected; a non-tuple top-level pattern over a non-tuple element is rejected (`bind a name and destructure in the body`).

> **Divergence.** B4

> **Uncertainty.** Restriction is implementation-current, not a designed permanent language limit.

Evidence: `src/compiler/sema_stmt.cpp#L8292-L8297` · `src/compiler/sema_stmt.cpp#L8311-L8330`

### `pat.for-loop.ref-element-deref` — by-ref for-loop element is dereferenced before destructure

When the iterated element type is `&T`/`&mut T`, the loop binding is dereferenced to a value temporary of type `T` and the tuple pattern destructures that value (by-ref default binding modes are not applied).

> **Divergence.** B4

> **Uncertainty.** Inferred limitation per code comment ("default-binding-mode by-ref is a follow-up").

Evidence: `src/compiler/sema_stmt.cpp#L8277-L8291`

### `pat.for-loop.discard-underscore` — underscore element binds nothing

A `_` element in a for-loop tuple pattern introduces no binding (the tuple element is discarded).

Evidence: `src/compiler/sema_stmt.cpp#L8319-L8321`

## `match` semantics (`match`)

### `pat.match.diverging-scrutinee-dead-arms` — Diverging match scrutinee makes arms dead code

If a match's scrutinee diverges (e.g. `match return x { ... }`), evaluation of the scrutinee terminates control flow and the arms are unreachable.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3767-L3777`

### `pat.match.auto-deref-scrutinee` — Match auto-derefs reference scrutinees

Matching a scrutinee of type &T, &mut T, or *T against patterns for T behaves identically to matching T directly: the reference is transparently dereferenced for enum/tuple/struct discrimination and binding (default binding modes).

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3783-L3823` · `src/compiler/mlir_gen_stmt.cpp#L3824-L3836` · `src/compiler/mlir_gen_stmt.cpp#L3882-L3890`

### `pat.match.enum-discriminant-test` — Enum match tests the discriminant

Matching a tagged (data-carrying) enum value loads its discriminant and dispatches arms by discriminant equality; a fieldless C-like enum value is its discriminant directly (an integer), and a reference to a fieldless enum is dereferenced to compare the discriminant rather than the pointer.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3796-L3811` · `src/compiler/mlir_gen_stmt.cpp#L3812-L3821`

### `pat.match.exhaustive-no-default` — Exhaustive discrete match needs no reachable default

A match whose unguarded arms cover all values of a discrete scrutinee — a bool with both true and false (or a wildcard), an enum with every variant discriminant covered (or a wildcard), or a tuple with an irrefutable arm — is exhaustive; the fall-through default branch is unreachable.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L3850-L3927` · `src/compiler/mlir_gen_stmt.cpp#L3928-L3937`

### `pat.match.first-match-top-to-bottom` — Match arms tested top-to-bottom, first matching arm runs

A `match` evaluates arms in source order; the first arm whose pattern matches (and whose guard, if any, holds) executes and the match completes. Operationally the arms form an if/else chain where each arm's test branches to the arm body on success and to the next arm's test on failure; an irrefutable pattern terminates the chain.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4319-L4320` · `src/compiler/mlir_gen_stmt.cpp#L4379-L4381` · `src/compiler/mlir_gen_stmt.cpp#L4754`

### `pat.match.catchall-classification` — Catch-all arm = guardless wildcard or `_`

A match arm is an unconditional catch-all iff it has no guard and its pattern is the wildcard `_` (anonymous or explicitly named `_`); a single-alternative `PAT_OR` is unwrapped first, while a multi-alternative or-pattern is not a catch-all unless every alternative is wild.

Evidence: `src/compiler/sema_impl.hpp#L1816-L1833`

### `pat.match.ast-exhaustiveness-nested-enum-payload` — Exhaustiveness of nested enum-payload matches decided at the AST level

Exhaustiveness of a `match` whose patterns nest enum-payload sub-patterns (`Some(Some(v))`, `Some(None)`, `None`) is verified by recursing over the ORIGINAL (unguarded-arm) AST pattern nodes, descending into each variant's single payload — because the LIR-level exhaustiveness check skips guarded arms and cannot see through the desugared nested structure, and would otherwise spuriously report such a match as non-exhaustive.

Evidence: `src/compiler/sema_impl.hpp#L4241-L4246`

### `pat.match.nested-variant-payload-destructure` — Nested variant-payload sub-patterns bind via a synthesized irrefutable let-else prologue

A pattern with a nested variant payload sub-pattern (e.g. `Some(Some(v))`) binds the inner names through a body-prologue statement `let <sub-pattern> = <synth-local> else { loop {} };` against a synthesized local holding the payload, recursing for deeper nesting; the else arm is unreachable because the owning arm's guard already proved the match. The same mechanism is shared, byte-for-byte, by match arms, `if let`, and `while let`, so all three treat nested payload patterns identically; a `for_guard` mode omits this refutable let-else when only a guard expression (not a body) is being built.

Evidence: `src/compiler/sema_impl.hpp#L4217-L4231`

### `pat.match.mut-binding-modifier` — `mut x` match-arm pattern binds a mutable local

A `mut x` pattern in a match arm (`match scrut { mut z => .. }`) binds the name as a mutable local: build_pattern_impl records the name in a side-channel (current_pat_mut_names_) because the PatWild LIR mirror it lowers to carries no mutability flag, and bind_pattern_ref re-declares the binding as `mut` by consulting that side-channel.

Evidence: `src/compiler/sema_impl.hpp#L4247-L4251`

### `pat.match.refutable-payload-literal-guard` — Refutable literal sub-patterns inside a variant payload become AND-combined arm guards

A refutable literal sub-pattern nested inside a variant-payload struct/tuple pattern (`E::Foo { f: 1 }`, `Option::Some(2)`) lowers to a synthesized `binding == value` boolean predicate, using the synth binding name chosen for that payload slot, appended to the current arm's refutable-guard list; the match-arm builder AND-combines all such predicates into the arm's overall guard, so the arm additionally requires the nested literal(s) to match.

Evidence: `src/compiler/sema_impl.hpp#L4252-L4258`

### `pat.match.exhaustive-required` — match must be exhaustive

A `match` over a scrutinee of type T must cover every value of T. If a reachable value is uncovered the program is rejected (e.g. enum: 'match is not exhaustive — missing variant(s): ...'; bool: 'match on bool is not exhaustive — missing true/false').

Evidence: `src/compiler/sema_stmt.cpp#L7540-L7542` · `src/compiler/sema_stmt.cpp#L7577-L7579`

### `pat.match.uninhabited-trivially-exhaustive` — match on uninhabited scrutinee is exhaustive

A match whose scrutinee type is uninhabited — the `Never` type, or an enum with zero variants — is trivially exhaustive and requires no arms; a bare `match x {}` is accepted.

Evidence: `src/compiler/sema_stmt.cpp#L7470-L7479`

### `pat.match.wildcard-covers` — unguarded wildcard arm makes match exhaustive

An arm whose pattern is an unguarded wildcard `_` covers all remaining values, making the match exhaustive regardless of which enum variants or bool values are otherwise matched.

Evidence: `src/compiler/sema_stmt.cpp#L7480-L7486` · `src/compiler/sema_stmt.cpp#L7512`

### `pat.match.guarded-arm-not-counted` — guarded arms do not contribute to exhaustiveness

An arm carrying a guard (`if <cond>`) is not counted toward exhaustiveness coverage; only unguarded arms cover variants/values, since a guard may fail at runtime.

Evidence: `src/compiler/sema_stmt.cpp#L7482` · `src/compiler/sema_stmt.cpp#L7503` · `src/compiler/sema_stmt.cpp#L7570`

### `pat.match.enum-variant-coverage` — enum match coverage by variant

For an enum scrutinee, an unguarded `Variant` or `VariantData` pattern covers that variant (identified by discriminant); an or-pattern covers the union of its alternatives. A match without a wildcard is exhaustive iff every (constructable) variant is covered.

Evidence: `src/compiler/sema_stmt.cpp#L7487-L7542` · `src/compiler/sema_stmt.cpp#L7494-L7511`

### `pat.match.uninhabited-variant-omittable` — variant with uninhabited payload may be omitted

An enum variant whose payload (after substituting the scrutinee's type arguments into the enum's type parameters) contains an uninhabited type can never be constructed and need not have an arm; its omission does not break exhaustiveness.

```logos
match r: Result<i32, Void> { Ok(x) => x }  // Err arm omittable when Void is empty
```

Evidence: `src/compiler/sema_stmt.cpp#L7516-L7535`

### `pat.match.redundant-wildcard-warn` — redundant wildcard arm over fully-covered unit enum

A wildcard arm is reported unreachable ('unreachable wildcard arm: every variant of the enum is already covered explicitly') when the enum is non-empty, all its variants are unit (payload-free), and every variant is explicitly covered. For payload-bearing variants the warning is suppressed (disc-only coverage cannot distinguish refutable inner patterns).

Evidence: `src/compiler/sema_stmt.cpp#L7543-L7564`

### `pat.match.bool-exhaustive` — bool match must cover true and false

A match on a `bool` scrutinee without a wildcard arm must include unguarded `true` and `false` patterns; otherwise it is rejected as non-exhaustive.

Evidence: `src/compiler/sema_stmt.cpp#L7567-L7580`

### `pat.match.scrutinee-moved-by-binding` — match consumes scrutinee when an arm moves out a value

Matching a by-value (move-type) scrutinee whose arm binds out a value by name marks the scrutinee place moved. This applies to: whole-value bindings, struct destructures that bind a move-only field by value, tuple destructures that bind a move-only element by value, and variant-data arms that bind a move-only payload by value. `_` bindings and `ref` bindings move nothing and leave the scrutinee live.

Evidence: `src/compiler/sema_stmt.cpp#L7604-L7820` · `src/compiler/sema_stmt.cpp#L7624-L7629` · `src/compiler/sema_stmt.cpp#L7677-L7685` · `src/compiler/sema_stmt.cpp#L7711-L7716` · `src/compiler/sema_stmt.cpp#L7725-L7816`

### `pat.match.scrutinee-ref-peeled` — pattern matching peels references

When checking exhaustiveness against a scrutinee type, reference and pointer layers (`&T`, `&mut T`, `*T`) are peeled (up to a fixed depth) to the underlying type before variant analysis; matching `&E` covers `E`'s variants.

Evidence: `src/compiler/sema_stmt.cpp#L7897-L7901`

### `pat.match.nested-payload-exhaustive` — exhaustiveness recurses into single-field variant payloads

A variant covered only by a refutable single-field payload pattern is treated as covered iff the inner patterns collectively exhaust the (substituted) payload type; an all-wildcard payload, an empty payload-arg list, or a bare `Variant` pattern covers the variant fully.

Evidence: `src/compiler/sema_stmt.cpp#L7967-L7991`

### `pat.match.guard-bool` — match guard must be bool

A match-arm guard expression `pat if <e> =>` must have type `bool`.

Evidence: `src/compiler/sema_stmt.cpp#L8956-L8963`

### `pat.match.arm-after-catchall-unreachable` — arm after unguarded catch-all is unreachable

Any match arm appearing after a prior unguarded catch-all (`_`) arm is an error: unreachable arm.

Evidence: `src/compiler/sema_stmt.cpp#L8572-L8586`

### `pat.match.guard-backtracks-alternatives` — failing guard backtracks to remaining or-alternatives

Each fanned-out or-alternative is compiled as its own match arm with its own guard; when one alternative's guard fails, matching falls through to try the next alternative/arm (Rust backtracking under a failing guard).

Evidence: `src/compiler/sema_stmt.cpp#L8777-L8784` · `src/compiler/sema_stmt.cpp#L8823-L8827`

### `pat.match.string-literal-pattern` — string-literal pattern is content equality

A top-level string-literal arm `"foo" =>` (directly or as an or-pattern alternative) matches by content equality on the scrutinee (`str_eq(scrut, "foo")`), compiled as a wildcard pattern guarded by that equality test; the scrutinee is evaluated exactly once (hoisted into a temporary shared by all such arms).

Evidence: `src/compiler/sema_stmt.cpp#L8685-L8719` · `src/compiler/sema_stmt.cpp#L8900-L8920`

### `pat.match.guard-runs-only-on-match` — guard runs only after the pattern matches

An arm's effective guard is built by conjoining, with `&&` (synthesized guard first so it short-circuits before a user guard runs on a type/shape mismatch): any synthesized string-literal-equality guard, any synthesized Writ structural/scalar guard, the user's `if` guard, and any refutable-inner-pattern literal guards. The user guard therefore only executes once the pattern (and any synthesized structural test) has matched.

Evidence: `src/compiler/sema_stmt.cpp#L8964-L8998`

### `pat.match.scrutinee-eval-once` — match scrutinee evaluated exactly once

The match scrutinee is evaluated exactly once; for Writ-pattern, string-pattern, or droppable-temporary scrutinees it is hoisted into a synthetic local that all arms/guards reference instead of re-evaluating the scrutinee expression.

Evidence: `src/compiler/sema_stmt.cpp#L8622-L8683` · `src/compiler/sema_stmt.cpp#L8705-L8719` · `src/compiler/sema_stmt.cpp#L8514-L8545`

### `pat.match.temp-scrutinee-dropped` — temporary match scrutinee is dropped at match end

A move-typed match scrutinee that is a temporary (rvalue: call result, constructor, `?`; not a place such as a var/field/tuple-index/deref/index) is hoisted into a synthetic owned local and dropped on every exit path (fall-through and diverging arm exits); if an arm moves the payload the drop is suppressed (no double-free). A place scrutinee is owned by its existing binding and is left alone (not hoisted, not dropped by the match).

Evidence: `src/compiler/sema_stmt.cpp#L8514-L8545` · `src/compiler/sema_stmt.cpp#L8546-L8559`

### `pat.match.whole-value-binding-moves` — whole-value binding arm moves the scrutinee

An unguarded whole-value binding arm `x => ...` (an unguarded `PAT_WILD` carrying a real name) over an owned move-type scrutinee moves the scrutinee into `x` (equivalent to `let x = v; ...`), matching Rust's by-value match move; the scrutinee var is marked moved so it is not dropped a second time after the match. Guarded binding arms do not unconditionally move (the scrutinee stays conditionally live for later arms).

Evidence: `src/compiler/sema_stmt.cpp#L8561-L8570`

### `pat.match.arm-bindings-drop` — match-arm pattern bindings drop at arm end

Pattern bindings introduced by a match arm are dropped before the arm exits via fall-through, unless the body's tail expression moves the binding out (marked moved first so the drop is skipped) or the body already ends in `return` (handled by the frame's full-drop collection instead).

Evidence: `src/compiler/sema_stmt.cpp#L9064-L9103`

### `pat.match.tail-vs-stmt-position` — expression arms in tail vs statement position

An expression-form arm `pat => <e>` in a tail-position match produces the enclosing function's return value (lowers to `return <e>`); in statement position it is lowered as an expression-statement, evaluated for side effects only.

Evidence: `src/compiler/sema_stmt.cpp#L9029-L9040`

### `pat.match.exhaustiveness` — match must be exhaustive

A match must cover all cases of its scrutinee type; exhaustiveness is proved at the AST level over the arms' unguarded top-level patterns only — a user-guarded arm (`pat if e =>`) does not count toward exhaustiveness coverage, since it is not guaranteed to match.

Evidence: `src/compiler/sema_stmt.cpp#L9136-L9148`

### `pat.match.writ-pattern-view-scrutinee` — Writ patterns require a view scrutinee

A match using Writ scalar/structural patterns (null/bool/int/str/map/arr/typed-arr/typed-map) requires the scrutinee to be a Writ view (`Writ`, `WritView`, `WritStatic`, or a borrow thereof) and requires `use logos.lang.writ.pat;` (the `writ_pat_root`/`writ_pat_root_rc` helper resolvable); otherwise it is an error. The scrutinee is hoisted into a view local plus a derived root `AnyVal` local used by all synthesized structural guards.

Evidence: `src/compiler/sema_stmt.cpp#L8609-L8683`

### `pat.match.nested-payload-destructure-scope` — nested variant-payload bindings are visible to both guard and body

A nested sub-pattern inside a variant payload (tuple/struct destructuring within `E::V(pat)`) is realized as unconditional field/element `let` statements. These are prepended to the arm body so the bound names are live in the body; when the arm also carries a user guard, an independent parallel copy of the same destructuring is prepended (as a block-expression) ahead of the guard condition, so the guard can read the nested-payload names before the pattern-matched body runs.

Evidence: `src/compiler/sema_stmt.cpp#L8939-L8952` · `src/compiler/sema_stmt.cpp#L8999-L9019` · `src/compiler/sema_stmt.cpp#L9041-L9049`

### `pat.match.refutable-payload-guard` — refutable variant-payload literal sub-patterns compile to conjoined guards

A refutable literal sub-pattern nested inside a variant payload (e.g. `E::V { f: 1 }`, `Option::Some(1)`) is compiled as an unconditional binding plus an extra guard predicate testing the literal; all such predicates are conjoined with `&&` into the arm's effective guard alongside any synthesized/user guards, in any order (they read only fresh pattern-bound names and never have side effects).

> **Uncertainty.** The predicates themselves are constructed by build_pattern outside this slice; only the AND-merge into the effective guard is directly observed here.

Evidence: `src/compiler/sema_stmt.cpp#L8892-L8899` · `src/compiler/sema_stmt.cpp#L8986-L8998`

### `pat.match.writ-pattern-at-binding` — Writ @-pattern bindings synthesized twice (guard pass, body pass)

For an arm containing Writ scalar/structural patterns, the structural test and any Writ pattern bindings are synthesized twice from the same source pattern via the same helper: once producing the guard predicate, and — only when bindings exist — a second, independent pass regenerating parallel statements/bindings for the body scope under fresh temporary names, so the guard's and the body's synthesized locals never alias. Body bindings are then declared in the arm's pattern scope and their prologue `let`s prepended to the arm body.

> **Uncertainty.** build_writ_pat_guard's internal semantics live outside this slice; only the two-pass guard/body generation and the prepend mechanics are directly observed here.

Evidence: `src/compiler/sema_stmt.cpp#L8858-L8880` · `src/compiler/sema_stmt.cpp#L8935-L8938` · `src/compiler/sema_stmt.cpp#L9050-L9063`

### `pat.match.or-alt-binding-consistency` — or-pattern alternatives must bind the same names

Every alternative of a top-level or-pattern arm (`A | B =>`) must bind the identical set of variable names (E0408).

Evidence: `src/compiler/sema_stmt.cpp#L9125-L9128`

### `pat.match.or-fanout-bindings` — or-patterns with bindings/non-scalar shapes fan out into separate arms

An or-pattern arm whose alternatives bind variables or have non-scalar/refutable shapes is expanded into one synthetic arm per alternative, each lowered through the normal single-arm path. Pure scalar-literal or-patterns (only PAT_INT/PAT_BOOL/PAT_CHAR alternatives) stay merged into a single arm.

Evidence: `src/compiler/sema_stmt.cpp#L9075-L9142` · `src/compiler/sema_stmt.cpp#L9084-L9095`

### `pat.match.variant-payload-or-distribution` — variant whose single payload is a multi-alt or-pattern distributes

A variant-data pattern whose single payload argument is a multi-alternative or-pattern with at least one non-merge-safe alternative is distributed: one synthetic arm per payload alternative (B170-E).

Evidence: `src/compiler/sema_stmt.cpp#L9096-L9118` · `src/compiler/sema_stmt.cpp#L9135-L9139`

### `pat.match.refutable-inner-guard` — refutable inner sub-patterns become AND-ed payload guards

A refutable inner sub-pattern in a variant payload (e.g. literal/variant like `Foo::FooUint(1)` or `Option::Some(1)`) contributes a guard testing the payload value; these guards are AND-ed into the arm guard so the arm matches only when both the variant tag and the inner value match (G145-2).

Evidence: `src/compiler/sema_stmt.cpp#L9185-L9189` · `src/compiler/sema_stmt.cpp#L9369-L9383`

## Writ patterns (`writ`)

### `pat.writ.scalar` — Writ scalar patterns

`@null`, `@true`/`@false`, `@N`/`@-N`, and `@"str"` are writ scalar patterns matching writ null, bool, integer, and string values respectively.

> **Divergence.** Logos addition: Writ data-substrate patterns.

Evidence: `tools/peg_gen/grammars/logos.peg#L2092-L2106`

### `pat.writ.typed-container` — Writ typed map/array patterns

`@<T>{..}`, `@<T,R>{..}`, and `@<T>[..]` are typed writ map and array patterns annotating the matched container's element type(s).

> **Divergence.** Logos addition: Writ typed-container patterns.

Evidence: `tools/peg_gen/grammars/logos.peg#L2107-L2112`

### `pat.writ.container` — Writ map/array patterns

`@{ key: pat, ... }` / `@{}` match writ maps; `@[ elem, ... ]` / `@[]` match writ arrays. Array elements admit a trailing `..` to match length ≥ n; map keys are string literals.

> **Divergence.** Logos addition: Writ container patterns.

Evidence: `tools/peg_gen/grammars/logos.peg#L2028-L2041` · `tools/peg_gen/grammars/logos.peg#L2113-L2120`

### `pat.writ.match-only` — Writ scalar patterns only in match arms

Writ scalar patterns (`@null`, `@true`, `@false`, `@<int>`, `@"str"`, `@{...}`, `@[...]`, and typed array/map forms) are permitted only in `match` arms, not in if-let / while-let / let-bindings / nested pattern positions; elsewhere is an error. In a match arm they lower to a wildcard plus a synthesized guard.

> **Divergence.** Logos extension (Writ value patterns); no Rust equivalent.

Evidence: `src/compiler/sema_stmt.cpp#L5086-L5104`

### `pat.writ.scalar-leaves` — Writ scalar leaf patterns

Within a Writ value pattern (@{...}/@[...]), the scalar leaves are: null (`@null`), bool (`@true`/`@false`), integer (`@<int>`), and string (`@"..."`). Each tests the corresponding AnyVal scrutinee: null-ness, boolean equality, integer equality, and string equality respectively.

> **Divergence.** Writ pattern matching is a Logos addition (no Rust equivalent).

Evidence: `src/compiler/sema_stmt.cpp#L5293-L5334` · `src/compiler/sema_stmt.cpp#L5484-L5486`

### `pat.writ.int-i24-range` — Writ integer pattern fits i24

A Writ integer pattern `@<int>` value v must satisfy -2^23 <= v < 2^23 (i24 range); otherwise it is a compile error. The literal may carry a negation flag that negates the parsed magnitude.

> **Divergence.** Logos addition; i24 bound is Writ-specific.

Evidence: `src/compiler/sema_stmt.cpp#L5312-L5327`

### `pat.writ.map-shape` — Writ map pattern

A Writ map pattern `@{k: p, ...}` matches iff the scrutinee is a map AND, for each listed entry key k, the key is present and its slot value matches sub-pattern p (conjunction over all entries). An entry without a value sub-pattern requires only presence of the key. Map patterns are non-exhaustive: keys not listed are ignored.

> **Divergence.** Logos addition.

Evidence: `src/compiler/sema_stmt.cpp#L5495-L5524`

### `pat.writ.array-len-and-rest` — Writ array pattern length and rest

A Writ array pattern `@[p0, p1, ...]` matches iff the scrutinee is an array of exactly the listed element count and each element matches its sub-pattern. A trailing `..` rest changes the length check to >= (count of non-rest elements) and binds no further elements. `..` is permitted only as the LAST element; otherwise a compile error.

> **Divergence.** Logos addition.

Evidence: `src/compiler/sema_stmt.cpp#L5525-L5562`

### `pat.writ.typed-array-element-types` — Typed Writ array pattern element types

A typed Writ array pattern `@<T>[..]` matches iff the scrutinee has the array type-code for element type T. T must be one of {I8,U8,I16,U16,I32,U32,I64,U64,F32,F64,AnyVal}; any other element type is a compile error.

> **Divergence.** Logos addition.

Evidence: `src/compiler/sema_stmt.cpp#L5563-L5588`

### `pat.writ.typed-map-key-value-types` — Typed Writ map pattern key/value types

A typed Writ map pattern `@<K[,V]>{..}` matches iff the scrutinee has the map type-code for key type K. K must be one of {Varchar,I32,U32,I64,U64}; the value type V, if given, must be AnyVal. Any other key or value type is a compile error.

> **Divergence.** Logos addition.

Evidence: `src/compiler/sema_stmt.cpp#L5589-L5616`

### `pat.writ.wildcard-binding` — Named wildcard inside Writ pattern binds the AnyVal

A wildcard with a non-`_` name inside a Writ pattern binds that name to the current AnyVal sub-value and always matches; a `_` (or empty) name binds nothing.

> **Divergence.** Logos addition.

Evidence: `src/compiler/sema_stmt.cpp#L5487-L5494`

### `pat.writ.or-no-mixing` — Or-patterns may not mix Writ and non-Writ alternatives

In an or-pattern, if any alternative is a Writ pattern then all alternatives must be Writ patterns; mixing Writ patterns with non-Writ patterns is a compile error. An all-Writ or-pattern matches iff any alternative matches (disjunction).

> **Divergence.** Logos addition.

Evidence: `src/compiler/sema_stmt.cpp#L5641-L5664`

## Writ scalar pattern contexts (`writ-scalar`)

### `pat.writ-scalar.match-only-context` — Writ-scalar patterns are legal only inside an explicit match-over-writ context

A Writ-scalar pattern (PAT_WRIT_NULL/BOOL/INT) is accepted by build_pattern only while lowering match arms under an explicit "matching a Writ scrutinee" context, where it desugars to a runtime guard call evaluating the pattern against the AnyVal scrutinee. Outside that context, encountering a Writ-scalar pattern node is a diagnostic.

Evidence: `src/compiler/sema_impl.hpp#L4164-L4199`
