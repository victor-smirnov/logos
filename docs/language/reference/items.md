# Items

Items are top-level declarations: anything that may appear directly under a `package` (i.e. at file scope) or inside an `impl` block. The grammar rule is `item` ([logos.peg:398](../../../tools/peg_gen/grammars/logos.peg#L398)).

A file always starts with a `package` declaration, optionally followed by `use` declarations, then any number of items.

```logos
package my.lib;

use logos.std.io;
use logos.mem.collections.vec;

// items go here
```

## Visibility

`pub` makes the item visible across packages. The `pub` keyword precedes the item kind (`pub fn`, `pub struct`, `pub use`, etc.). Items without `pub` are package-private. Field-level visibility on struct fields uses `pub`; absent → package-private.

There is no finer-grained visibility (no `pub(crate)`, `pub(super)` etc.).

## `use`

```
use logos.std.io;
use logos.mem.collections.hashmap;   // brings `HashMap` into scope
pub use foo.Bar;       // re-export
```

A `use` brings names from another package into scope. The path is dotted (`a.b.c`), not slash-separated. `pub use` re-exports the imported name from the current package.

## `const` / module-level `let`

```logos
let MAX: i32 = 1024;
```

Module-level constants are introduced with `let NAME: type = expr;` — the right-hand side is compile-time evaluated. There is no `const` keyword for value bindings; `const` is reserved for `<const N: T>` generic parameters and for `*const T` pointers.

> `pub` on a module-level `let` is not yet wired through the import system — cross-package reads of a `pub let` constant are not supported end-to-end; expose a `pub fn` accessor for that case.

## `type` aliases

```logos
type Bytes = &[u8];
pub type Pair<A, B> = (A, B);
```

A `type` alias introduces a new name for an existing type. Aliases may be generic (`type Pair<A, B> = ...`).

## `fn`

```logos
fn add(a: i32, b: i32) -> i32 { a + b }
pub fn parse<T>(input: &[u8]) -> Result<T, ParseError> where T: FromBytes { ... }
unsafe fn raw_load(p: *const u8) -> u8 { *p }
fn new<T>() -> Self { ... }              // `new` is allowed as a free-fn / method name
```

Components, in order:

1. Optional `pub`, `unsafe`.
2. `fn` keyword.
3. Name (`IDENT` or `new`).
4. Optional generic parameter list `<...>`.
5. Parameter list `(...)`.
6. Optional return type `-> T`. Absent → returns `()`.
7. Optional `where` clause.
8. Body block `{ ... }`.

Parameters are `name: type`, optionally with `&self` / `&mut self` shorthand inside `impl` blocks. A trailing parameter may be variadic (`xs: T...`) — see [Generics & Traits](generics-traits.md#variadic-parameters).

### `extern fn`

```logos
extern fn write(fd: i32, buf: *const u8, len: usize) -> isize;
extern fn printf(fmt: *const u8, ...) -> i32;     // C-style varargs
```

Declarations only — no body. Bound at link time. The `, ...` form marks a C-style variadic function (e.g. `printf`); regular Logos variadic generics use `<T...>` plus a `T...` parameter pack.

## `struct`

```logos
struct Point {
    x: f64,
    y: f64,

    fn norm2(&self) -> f64 { return self.x * self.x + self.y * self.y; }
    static fn origin() -> Self { return Point { x: 0.0, y: 0.0 }; }
}

pub struct Pair<A, B> {
    pub fst: A,
    pub snd: B,

    static fn new(a: A, b: B) -> Self { return Pair { fst: a, snd: b }; }
    fn fst_ref(&self) -> &A { return &self.fst; }
}
```

A struct definition may contain fields and methods in the same block — for both plain and generic structs (`Self` and the `&self`/`&mut self` shorthand resolve exactly as in an `impl` block). Field-level `pub` controls per-field visibility. Equivalently, methods may live in a separate `impl` block.

The grammar also accepts an alternative spelling with a leading `#` token (`struct #Name { ... }`) used by metaprogramming for AST templates — see [Metaprogramming](metaprog.md).

A struct marked `#[zoned]` is a **Writ datatype** (C POD layout, zone-relative offsets, no heap pointers). `#[zoned] struct` is the sole canonical form for datatypes — see [`#[zoned]`](attributes.md#zoned) and [Writ](writ.md).

### Methods

Inside a struct (or `impl`) block, methods come in three flavours:

- **`fn name(self: Self, ...) -> R`** — instance method. The first parameter explicitly names `self`. `&self` / `&mut self` is sugar for `self: &Self` / `self: &mut Self`.
- **`static fn name(...)`** — associated function with no `self`. Called as `TypeName::name(...)`.
- **`fn new(...) -> Self`** / `static fn new(...)` — `new` is allowed as a method name; conventionally a constructor.

### Tuple structs and explicit instantiations

```logos
struct NewType(i32);              // tuple struct
#[type_code=42] struct Array<i32>;   // explicit instantiation declaration (no body)
```

The body-less form (`struct Type;`) binds annotations to a generic instantiation — see [Attributes](attributes.md#type_code).

## `enum`

```logos
enum Color { Red, Green, Blue }
enum Maybe<T> { None, Some(T) }
enum Wire : u32 { Open = 0, Closed = 1 }
enum Many<T> { All(...T) }       // variadic-payload variant
```

Variants are listed comma-separated. A variant may have:

- No payload: `Red`.
- Tuple payload: `Some(T)`, `Pair(A, B)`.
- A variadic payload `(...T)` — pulls in a parameter pack.
- An explicit discriminant: `X = 5`, `Y = -3`.

The optional `: type` after the enum name picks the backing integer type for C-style enums (i32 default).

## `trait`

```logos
trait Display {
    fn fmt(&self, out: &mut Writer) -> Result<(), io::Error>;
}

pub trait Iterator {
    type Item;
    fn next(&mut self) -> Option<Self::Item>;
}

pub auto trait Send {}
pub unsafe trait Sync {}
trait Ord: PartialOrd + Eq { ... }      // supertraits
```

A `trait` is a contract type. Items inside:

- **Method declarations** with or without bodies (default methods).
- **Associated types**: `type Item;` — required. May be generic (`type Item<U>;`).
- **Associated constants**: `const MAX: usize;`.

Modifiers:

- **`auto`** — marker trait auto-implemented from field types (cf. `Send`, `Sync`).
- **`unsafe`** — implementing the trait requires `unsafe impl`.

(`genos` is **not** a trait synonym — it is the form-specification declaration described in [`genos` (form specifications)](#genos-form-specifications) below.)

### Supertraits

`trait Foo: Bar + Baz` — implementations of `Foo` must also implement `Bar` and `Baz`.

## `impl`

```logos
impl Point { fn norm(&self) -> f64 { ... } }                    // standalone impl
impl Display for Point { fn fmt(&self, ...) -> ... { ... } }    // trait impl
impl<T> Stack<T> { fn push(&mut self, x: T) { ... } }            // generic standalone
impl<T> Iterator for Stack<T> { ... }                            // generic trait impl
unsafe impl<T> Send for Bag<T> { }                               // unsafe impl
impl Container<i32> for Box<i32> { ... }                         // partial specialisation
```

Inside an `impl` block:

- **Methods** — same shape as `fn` items, with `self`-parameter shorthand.
- **`type Item = T;`** — associated-type implementations.
- **`const MAX: usize = 64;`** — associated-constant implementations.

Trait impls may be partially specialised: `impl Container<i32> for Box<i32>` is more specific than a generic blanket and the compiler dispatches accordingly.

## `genos` (form specifications)

```logos
genos pmap_descend_to_n<K: ContainerOrd, V: Container, CFG>
    requires column SUM(subtree_size) over branch.children
{ … }
```

A `genos` is a **semi-formal, parametric form specification** — Logos syntax with relaxed type rules, expressing the *shape and invariants* of an algorithm or data structure as the one-per-family canonical statement of intent. It is executable through a minimal interpreter and acts as the conformance oracle for the metaprog-generated instantiations beneath it.

`genos` is **not** a synonym for `trait` and **not** a datatype declaration form (datatypes use `#[zoned] struct`). See [overview: the genos layer](../overview.md) and [internals: metaprogramming](../../internals/metaprog.md#genos--algorithmic-and-structural-templates). The feature is design-stage: `genos` is a reserved keyword but is not yet parsed in any item position (and no interpreter exists yet).

## `template <decl>`

```logos
template struct Pair<A, B> { fst: A, snd: B }
template fn map<F>(...) { ... }
```

`template` marks the wrapped declaration as **data, not a binding** — a Writ AST node that metafunctions consume via `template_of::<X>()`. Sema skips templates entirely; their inner names are not registered, so referencing them as ordinary types yields the standard "unknown type" diagnostic.

### Conceptual model (most of which is not yet implemented)

Templates are a **syntactic-level** code generator, distinct from generics:

- A template takes *template parameters* (names, types, packs, consts) and produces a declaration — a struct, an enum, a function, an impl block, or **another generic** struct/fn. The output is an ordinary item that lives in the module like any other.
- Generics are *type-level*: monomorphisation substitutes type / const arguments into a fixed shape. A template, by contrast, generates the shape itself (it can produce N field declarations from a `T...` pack, for example, which monomorphisation cannot).
- Template parameters need not equal the generic parameters of the output. A template parameterised over a `Name` can produce `struct #Name<A, B> { ... }` — the resulting `Name<A, B>` is then used through ordinary monomorphisation.
- Inside a template body, `#X` references a template parameter (placeholder); bare `X` is an ordinary in-scope name (e.g. an output-level generic parameter).
- Triggers: planned forms are `apply_template<Tpl, args...>() -> Item` (library metafunction) and `#[apply(args...)]` on the template declaration. There is intentionally no implicit type-use trigger — `Foo<i32, u64>` where `Foo` is a template would be ambiguous about whether the args are template-args or output's generic-args.

**Current status (2026-04-30):** essentially nothing of the above is implemented end-to-end. `template <decl>` parses and is silently dropped; `template_of::<X>()` returns a handle with `name()` / `type_param_count()` accessors; the body is not persisted, no placeholders are recognised, no `apply` mechanism exists. See the [Metaprogramming](metaprog.md) page for what does run today, and `metaprog-quote-slice5.md` / `template-body-expansion.md` (planning notes) for the slice-by-slice path forward.

## Attributes (`#[...]`)

Attributes precede an item. Multiple attributes stack:

```logos
#[derive_clone]
#[type_code=0x42]
pub struct Foo { ... }
```

See [Attributes](attributes.md) for the full list and forms.

## Roadmap

- **Module-level `pub let`** — currently parses but `pub` is not honoured by import resolution.
- **Tuple struct field access** — limited support; named structs work end-to-end.
- **Visibility refinements** — `pub(crate)`-style scoping not on the roadmap; flat `pub` is intentional.
