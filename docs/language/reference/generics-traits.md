# Generics and Traits

Logos generics are *monomorphic*: every concrete instantiation is emitted as its own code and layout. There is no late-bound dispatch on generic parameters except through trait objects (`&dyn Trait`, `&tagged<TS> Trait`). Generic parameters come in three flavours:

- **Type parameters**: `<T>`, `<T: Bound>`, `<T...>` (variadic pack).
- **Lifetime parameters**: `<'a>`, `<'a, T>`.
- **Const parameters**: `<const N: usize>`, `<const N...: T>` (variadic scalar pack).

The grammar entry is `type_param_list` ([logos.peg:1547](../../../tools/peg_gen/grammars/logos.peg#L1547)).

## Capability baseline — roughly C++20

Logos generics are, in expressive terms, **on par with C++20**: full variadic
parameters (type packs and const packs — see below), full const generics over
arbitrary scalar types (not just `usize`), trait bounds in the role of
concepts, partial specialisation via blanket impls, and associated types /
constants. The one C++20 feature not carried over is template-template
parameters — their role is filled instead by the **metafunction system**
(see [metaprog.md](metaprog.md)), which treats types as data and has no
direct analogue in C++ TMP. Affine types and the borrow checker are a
separate axis, taken from Rust.

What this means in practice:

- Memoria-class assembly (typelist algebra, fixed/variable selection,
  per-stream packed-struct dispatching) is expressible without dropping into
  metafunctions; anything that Memoria/C++ wires up through
  `MergeLists` / `IfThenElse` at the type level can be written in Logos
  using variadics + const generics + bounds.
- Metafunctions are layered on top when the task requires **reflection**
  ("does `T` implement `Clone`?", "what are its fields?") or **code
  synthesis** (`quote_*!`) — precisely the work that C++ TMP performs at
  enormous complexity cost and that Rust proc-macros cannot do at all.

## Type Parameters

```logos
fn id<T>(x: T) -> T { x }
struct Pair<A, B> { fst: A, snd: B }
trait Iterator { type Item; fn next(&mut self) -> Option<Self::Item>; }
```

Type parameters are introduced inside `< ... >` after the item name and used as types in the body. Bounds attach with `:`:

```logos
fn sum<T: Add + Default>(xs: &[T]) -> T { ... }
fn keyed<K: Hash + Eq, V>(m: &HashMap<K, V>) -> ... { ... }
```

Multiple bounds combine with `+`. A `where` clause supports the same set on item declarations:

```logos
fn parse<T>(s: &str) -> Result<T, ParseError>
    where T: FromStr + Default
{ ... }
```

## Lifetime Parameters

```logos
fn first<'a>(xs: &'a [u32]) -> &'a u32 { &xs[0] }
struct Slice<'a, T> { data: &'a [T] }
impl<'a> Iterator for Cursor<'a> { ... }
```

Lifetimes are declared the same way as type parameters and used in `&'a T` / `&'a mut T` borrows. See [Ownership](ownership.md) for inference rules and the borrow checker.

## Const Parameters

```logos
fn zeros<const N: usize>() -> [u8; N] { [0; N] }
struct Buf<const CAP: usize> { data: [u8; CAP] }
fn at<const I: usize, T>(t: &(T, T)) -> &T where const I < 2 { ... }
```

Const parameters are compile-time scalars. They participate in:

- Array size positions: `[T; N]`.
- Arithmetic in type-level expressions (limited).
- Return-type expressions.

Variadic scalar packs `<const N...: T>` accept zero or more compile-time scalars of the same type.

See [memory: feat_const_variadic_mvp](../../README.md) for the full feature matrix.

## Variadic (Pack) Parameters

```logos
fn print_all<T...>(args: T...)         { #(println("{}", args);)* }
fn count<T...>() -> u64                { sizeof...(T) }   // sizeof... is u64
type Tuple<T...>  = (T...);
```

A trailing `...` after a type parameter introduces a *type pack*. The pack name can be:

- **Expanded** in argument positions: `args: T...`, `tup: (T...,)`.
- **Counted** at compile time: `sizeof...(T)`.
- **Iterated over** with the `#(... pat ...)<sep>*` repeat-group form (inside `quote_*!` and similar metaprogramming contexts).

Variadic *value* parameters (`xs: T...`) accept a sequence of arguments expanded into a pack at the call site. Variadic generics interoperate with the `[T; sizeof...(P)]` array-length form ([memory: feat_arr_type_sizeof_pack_gap](../../README.md)).

### `extern fn` C-style varargs

`extern fn printf(fmt: *const u8, ...) -> i32;` uses the `, ...` form (literal `...` token) for ABI-level variadic interop with C, distinct from `<T...>` Logos packs.

## Generic Application

```logos
let v = Vec::<u32>::new();
let m = HashMap::<String, u64>::new();
let p = Pair::<i32, &[u8]> { fst: 1, snd: b"" };
parse::<u32>("42")
```

The turbofish `::<T, U>` is required at *call sites* and *enum-variant construction* — Logos does not yet do type-argument deduction across function arguments.

In *type positions*, `<T, U>` follows the type name without the `::`:

```logos
fn build() -> Result<Vec<u32>, ParseError> { ... }
```

## Traits

```logos
trait Display {
    fn fmt(&self, out: &mut Writer) -> Result<(), io::Error>;
}

trait Iterator {
    type Item;                          // associated type — required
    fn next(&mut self) -> Option<Self::Item>;
}

trait Add<Rhs = Self> {                 // (default type-param syntax planned, see Roadmap)
    type Output;
    fn add(self, rhs: Rhs) -> Self::Output;
}
```

A trait body may contain:

- **Methods** (declarations, optionally with default bodies).
- **Associated types**: `type Item;` — required by every implementor.
- **Associated constants**: `const MAX: usize;`.

### Supertraits

```logos
trait Ord: PartialOrd + Eq { ... }
```

Implementing `Ord` requires also implementing `PartialOrd` and `Eq`.

### Auto traits

```logos
pub auto trait Send {}
pub auto trait Sync {}
```

Auto traits are auto-implemented whenever every field type also implements the trait, used for thread-safety markers. See [memory: feat_auto_traits](../../README.md).

### Unsafe traits

```logos
pub unsafe trait RawAccess { ... }
unsafe impl RawAccess for Foo { ... }
```

Implementing an `unsafe trait` requires `unsafe impl`, signalling the implementor has manually upheld the trait's contract.

## Implementations

```logos
impl Point { fn norm(&self) -> f64 { ... } }                // standalone
impl Display for Point { ... }                              // trait impl
impl<T> Iterator for Stack<T> { ... }                       // generic
impl<T: Clone> Vec<T> { fn dup(&self) -> Vec<T> { ... } }   // bounded
impl Container<i32> for Box<i32> { ... }                    // partial spec
```

Inside an `impl` block:

- Methods: same shape as `fn`, with `&self` / `&mut self` shorthand.
- `type Item = ...;` — associated-type bindings.
- `const MAX: usize = 64;` — associated-constant bindings.

### Partial specialisation

```logos
impl<T> Container<T> for Box<T> { ... }       // generic blanket
impl Container<i32> for Box<i32> { ... }      // more specific — wins for T=i32
```

The compiler picks the most specific impl that applies. Conflicts (two equally-specific impls) are diagnosed.

### Blanket impls and orphan rule

```logos
impl<T: Display> Show for T { ... }   // blanket impl for any Display
```

The orphan rule allows a blanket impl only when either the trait or the type's defining package is the same as the impl's package — this prevents two packages from independently implementing the same `(trait, type)` pair.

## Trait Objects

```logos
fn write_each(items: &[&dyn Display], out: &mut Writer) -> Result<(), io::Error> {
    for item in items { item.fmt(out)?; }
    Ok(())
}
```

`&dyn Trait` is a fat pointer `(data, vtable)`. The vtable is generated per `(impl Trait for Type)` pair.

`&tagged<TS> Trait` is the *tag-dispatched* variant — a thin pointer whose first 1–8 bytes carry a `type_code` index into a per-trait dispatch table. Used for Hermes-zoned objects where adding a vtable would inflate every instance. See [memory: feat_tag_dispatch](../../README.md).

## `where` Clauses

```logos
fn merge<K, V>(a: HashMap<K, V>, b: HashMap<K, V>) -> HashMap<K, V>
    where K: Hash + Eq,
          V: Clone
{ ... }
```

`where` clauses go after the parameter / return type list and before the body. They accept the same bound forms as inline `T: Bound`.

## Roadmap

- **Type-argument deduction at call sites** — turbofish currently mandatory; deduction across function arguments planned.
- **Higher-kinded polymorphism** — `Container<F>` where `F` is itself a generic constructor — out of scope.
- **Default type parameters** — `trait Add<Rhs = Self>` parses as a special case in stdlib but is not a general feature yet.
- **GATs (generic associated types)** — partially supported (`type Item<U>;`); some bound forms still rough.
- **Mixed packs** — combining `<T...>` and `<const N...: U>` in the same signature is rejected by `mono_scan` ([memory: feat_const_variadic_mvp](../../README.md)).
