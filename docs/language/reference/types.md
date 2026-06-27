# Types

This page enumerates every type form Logos accepts. The compiler-internal kinds live in `LogosType::Kind` ([include/logos/compiler/sema.hpp:39](../../../include/logos/compiler/sema.hpp#L39)); the surface syntax is the `type_ref` rule in [tools/peg_gen/grammars/logos.peg:811](../../../tools/peg_gen/grammars/logos.peg#L811).

A type is either a primitive, a composite (formed from other types), or a user-defined nominal type (`struct`, `enum`, datatype, trait object). Logos does not have row types, untagged unions, or subtyping.

## Primitives

### Integers

| Type | Width | Signed | Notes |
|------|-------|--------|-------|
| `i8`, `i16`, `i32`, `i64`, `i128` | 8/16/32/64/128 | yes | Standard widths |
| `i24`, `i56` | 24, 56 | yes | Pair with Writ inline `Integer`/`SmallInt` payload widths |
| `u8`, `u16`, `u32`, `u64`, `u128` | 8/16/32/64/128 | no | |
| `u24`, `u56` | 24, 56 | no | |
| `isize`, `usize` | platform pointer width | yes/no | First-class type names and integer-literal suffixes |

### Floats

| Type | Width | Notes |
|------|-------|-------|
| `f32` | 32-bit IEEE-754 | |
| `f64` | 64-bit IEEE-754 | Default for unsuffixed float literals |

### Other primitives

| Type | Notes |
|------|-------|
| `bool` | `true` / `false` |
| `char` | Unicode scalar value. `'a'` char literals; usable in `match`, including range patterns (`'a'..='z'`). |
| `()`  | Unit / void return type. Empty tuple. |

There is no separate `byte` type — single bytes are `u8`. Byte-string literals `b"…"` produce `[u8; N]`.

### Implicit Widening

Smaller integer types implicitly widen to larger ones at expression positions when the conversion is information-preserving (`u8 → u32`, `i32 → i64`, etc.). Cross-sign conversions and narrowing always require an explicit `as` cast. Unsuffixed integer literals (`IntLit`) take their type from context with no `as` needed: `let x: i64 = 42;`.

## References

Borrow-checked references — see [Ownership](ownership.md) for lifetime rules.

| Form | Meaning |
|------|---------|
| `&T` | Shared (read-only) reference. |
| `&mut T` | Exclusive mutable reference. |
| `&'a T`, `&'a mut T` | Reference with named lifetime. |
| `&[T]`, `&mut [T]` | **Slice**: fat pointer `(ptr, len)` over a contiguous run of `T`. |
| `&dyn Trait` | **Trait object**: fat pointer `(data, vtable)`. |
| `&tagged<TS> Trait` | **Tag-dispatched pointer**: thin pointer to an object whose 1–8 prefix bytes carry a `type_code` lookup index into the dispatch table for `TS`. |

`dyn Trait` (without `&`) also parses but is conventionally used through `&dyn` or `Box<dyn>`.

## Raw Pointers

| Form | Meaning |
|------|---------|
| `*const T` | Immutable raw pointer. Not borrow-checked; `unsafe` to dereference. |
| `*mut T`   | Mutable raw pointer. Not borrow-checked; `unsafe` to dereference. |

Use raw pointers at FFI boundaries, in `unsafe` blocks, and inside types implementing low-level data structures. Casting between `*const T` ↔ `*mut T` is allowed; casting to/from integer types via `as` is allowed and used for tag manipulation.

## Arrays and Slices

```logos
[T; N]         // fixed-size array; N is a constant integer literal
[T; sizeof...(P)]  // array length comes from a variadic pack's expanded count
&[T]           // slice borrow
&mut [T]       // mutable slice borrow
```

`N` may be:

- An integer literal: `[u8; 16]`.
- An `IDENT` previously bound by `<const N: ...>`: `[T; N]`.
- The pack-size form `sizeof...(P)` — the compiler keeps the size symbolic at sema time and substitutes the pack's length during monomorphisation.

Arrays are value types — they live where they're declared (stack, struct field, etc.) and are copied by value when small. Element access is `arr[i]` (bounds checked at runtime in safe code). Slice from an array via `&arr` (whole) or `&arr[..]` (planned, see [Roadmap](#roadmap)).

## Tuples

```logos
()                  // unit
(T,)                // single-element tuple — note the trailing comma
(T1, T2)            // pair
(T1, T2, T3, ...)   // arbitrary arity
```

Tuples are anonymous product types. Field access uses `.0`, `.1`, …. Destructuring uses tuple patterns: `let (a, b) = pair;`.

## Function and Closure Types

```logos
fn(T1, T2) -> R         // bare function pointer (single ptr)
|T1, T2| -> R           // closure type (used in parameter annotations)
```

- **`fn(...) -> R`** — `FnPtr` kind. A single code-pointer; no captures. Castable from a non-capturing closure.
- **`|...| -> R`** — `Closure` kind. Carries an environment alongside the code pointer; how the closure is stored (boxed vs. by-value) depends on the parameter or field type.

Closures are introduced by closure expressions: `|x| x + 1`. See [Expressions → Closures](expressions.md#closures).

## User-Defined Types

### Struct

```logos
struct Point { x: f64, y: f64 }
struct Pair<A, B> { fst: A, snd: B }
struct NewType(i32);            // tuple struct (single position)
```

A `struct` is a heap-or-stack-living named product type. Generic structs (`Pair<A, B>`) are monomorphised — every concrete instantiation gets its own emitted code/layout. See [Items → struct](items.md#struct).

### Enum

```logos
enum Maybe<T> { None, Some(T) }
enum Color { Red, Green, Blue }     // C-style — no payloads
enum Wire : u32 { Open = 0, Closed = 1 }  // explicit discriminant type
```

Enums are tagged sums. The `LogosType::Kind::Enum` variant is used both for C-style enums (passed by-value as the discriminant integer) and for tagged enums (passed by pointer; the runtime carries a discriminant + per-variant payload). The compiler picks the representation based on whether any variant has a payload.

### Datatype (`#[zoned] struct`)

```logos
#[zoned] pub struct AnyVal { pub raw: u32 }
```

A *Writ datatype*: a struct laid out for the Writ wire format (zone-relative, no heap pointers). Internally tagged `LogosType::Kind::ZonedStruct`. Datatypes interoperate with the [Writ layer](writ.md) and follow extra layout constraints (see internals docs).

## Generic Application

```logos
Vec<i32>
HashMap<String, u64>
Pair<i32, &[u8]>
```

A `simple_type` followed by a `<...>` argument list. Each argument is either a type (`type_ref`), a lifetime (`'a`), an integer literal (for `<const N: ...>` parameters), or a pack-expansion form (`T...`, `$ts...`).

Logos has no implicit type-argument deduction at call sites yet — generic functions without type-arg deduction must use the turbofish (`f::<T>()`); deduction across function arguments is planned.

## Special Type Forms

| Form | Meaning |
|------|---------|
| `impl Trait`        | Opaque return type — concrete type known to the compiler, hidden from the caller. Resolved during lowering. |
| `typeof(expr)`      | Compile-time type of `expr` without evaluating it. |
| `T::Item`           | Associated type — looked up on `T`'s impl of the relevant trait. May take type-args (`T::Item<U>`) for generic associated types. |
| `<I32>[]`           | Writ typed-array type, used in `as <I32>[]` casts. See [Writ](writ.md). |
| `<K,V>{}`           | Writ typed-map type. |
| `$T`                | Antiquotation — only legal inside `quote_ty! { ... }`. See [Metaprogramming](metaprog.md). |

## Type Variables and Constants in Generics

Inside a generic context, several forms appear that are *not* concrete types but stand for parameters resolved by the caller:

| Kind | Source | Notes |
|------|--------|-------|
| `TypeVar` | `<T>`, `<T...>` | Type parameter. The variadic form denotes a pack. |
| `ConstVar` | `<const N: T>`, `<const N...: T>` | Compile-time scalar parameter; usable in array sizes, return values, arithmetic. |
| `IntLit`, `FloatLit` | `42`, `1.5` | Unresolved literal type — widens to context. |
| `AssocType` | `T::Item` | Resolved during monomorphisation. |
| `ImplTrait` | `impl Trait` return | Resolved during lowering. |
| `Generic`  | `generic_of::<X>()` | Value-side handle for an unapplied generic constructor. No pool entry; only appears as the `kind` field of a `Type` value. |
| `Error`    | (internal) | Sentinel for ill-typed expressions. |

## Roadmap

- **Mixed packs** — combining `<T...>` and `<const N...: U>` in one signature. Currently rejected by `mono_scan`. See [Generics & Traits → Roadmap](generics-traits.md#roadmap).
- **Slice-from-array `&arr[..]`** — currently a grammar gap; whole-array borrow `&arr` works.
- **Higher-kinded polymorphism** — generic over `GenericType` without explicit arity. Out of scope.
