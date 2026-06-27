# Syntax

Surface-syntax reference. The authoritative grammar is the PEG in `tools/peg_gen`; this is the human-readable summary.

## File Structure

A `.logos` file declares a package and may use other modules:

```logos
package my_package;

use logos.std.io;
use logos.mem.collections.vec;
```

A package can contain functions, types (`struct`, `enum`, `datatype`), trait declarations, trait impls, constants, and free `fn` definitions.

## Primitive Types

| Type | Notes |
|------|-------|
| `i8`, `i16`, `i32`, `i64` | Signed integers. |
| `u8`, `u16`, `u32`, `u64` | Unsigned integers. |
| `f32`, `f64` | IEEE-754 floats. |
| `bool` | `true` / `false`. |
| `()` | Unit. |
| `str` | Borrowed string slice. |
| `String` | Owned, growable string (in `std`). |

Integer literals are decimal by default, with optional type suffix: `7i8`, `42u32`. Without a suffix, the type is inferred from context; literals do not silently saturate (a `u64` literal preserves its full bit pattern).

Float literals use the standard `1.5`, `3.14` form and infer to `f32` or `f64` from context. `bool as f32` widens through unsigned-to-float conversion (so `true as f32 == 1.0`).

## Implicit Widening

Logos performs **safe** implicit integer widening at expression sites: `u32 → i64`, `i32 → i64`, `u8 → u32`, etc. The compiler picks `zext` or `sext` based on the source signedness. No truncating conversion is implicit; use `as` for that.

## Variables

```logos
let x: i64 = 1;          // immutable
let mut y: i64 = 2;      // mutable
y = y + 1;
```

Type annotations are optional when the type can be inferred from the initializer.

## Functions

```logos
fn add(a: i64, b: i64) -> i64 {
    return a + b;
}

fn no_return() {           // unit return is implicit
}
```

## Structs and Enums

```logos
struct Point {
    x: i64,
    y: i64,
}

enum Shape {
    Circle(i64),                   // tuple-like payload
    Rect { w: i64, h: i64 },       // named-field payload
    Empty,                         // no payload
}
```

Trailing commas in struct, enum-variant, and array literals are accepted. Struct update syntax (`Foo { x: 1, ..base }`) is supported.

## Pattern Matching

```logos
match shape {
    Shape::Circle(r)         => area_circle(r),
    Shape::Rect { w, h }     => w * h,
    Shape::Empty             => 0,
}
```

`let-else` is supported: `let Pattern = expr else { return; };` — the `else` block must diverge.

`if let` is supported: `if let Pattern = expr { ... }`. If-let chains combine multiple bindings with `&&`: `if let Some(a) = x && let Some(b) = y { ... }`.

Tuple patterns (`(a, b) => ...`), OR patterns (`Dir::E | Dir::W => ...`), and labeled loops (`'outer: for ... { break 'outer; }`) are all supported.

## Expressions and Operators

Standard operator precedence. Boolean operators short-circuit (`&&`, `||`). The `==` and `!=` operators are structural for primitive types and dispatched via traits for user types.

The postfix `?` operator early-returns from a function whose return type is `Result<T, E>`:

```logos
fn read_config() -> Result<Config, Error> {
    let raw: String = read_file("config")?;
    let cfg: Config = Config::parse(&raw)?;
    return Result::Ok(cfg);
}
```

## Statements

- `let`, `let mut`
- `return expr;`
- `break;`, `continue;` — inside loops
- `while cond { ... }`
- `loop { ... }` — infinite loop, exits via `break`
- `for x in iter { ... }` — iterator-based (depends on the `Iterator` trait, which is partially implemented)

## Comprehensions

List and map comprehensions are part of the surface syntax: `[expr for x in iter if guard]` produces a `Vec<T>`, and `{k: v for x in iter}` produces a `HashMap<K, V>`. The `@`-prefixed forms (`@[...]`, `@{...}`) produce Writ documents directly. See [Comprehensions](comprehensions.md).

## References

- `&x` — shared (immutable) borrow
- `&mut x` — exclusive (mutable) borrow
- `*r` — dereference

`&` has tight precedence in cast positions; if you need to combine with `as`, parenthesize.

See [Ownership](ownership.md) for the rules the borrow checker enforces.

## Generics

```logos
fn first<T>(xs: &Vec<T>) -> &T {
    return xs.borrow(0);
}

struct Pair<A, B> {
    first: A,
    second: B,
}
```

Trait bounds use `where` clauses (or inline `T: Trait` syntax). Monomorphization is the only generic implementation strategy; there are no runtime generic dispatches except via explicit trait objects (where supported).

See [Generics and Traits](generics-traits.md) for the full picture.

## Modules and Visibility

Today: a `package` is a compilation unit; cross-package references go through `use` paths rooted in package names. `pub` controls visibility on items.

`pub const` works within a package; cross-package `pub const` reads are not yet supported end-to-end — define a `pub fn` accessor for that case. See [Roadmap](../roadmap.md).

## Source Encoding

Logos source is currently restricted to ASCII, including comments. Unicode support is planned.

## Known Quirks

- Cross-package `pub const` reads are not fully supported end-to-end; use a `pub fn` accessor instead (same-package `pub const` works).
- Calling any `extern` function requires an `unsafe { }` block at the call site.
- The `str` type is itself a slice (roughly `&[u8]`); write `let s: str = "hello"`, not `let s: &str = "hello"`.
- Pointer types must always carry mutability: `*const T` or `*mut T`. Bare `*T` is a parse error.
- `usize` and `isize` are aliases for `u64` and `i64`.

These are tracked in the [Roadmap](../roadmap.md).
