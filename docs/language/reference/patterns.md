# Patterns

Patterns appear in `match` arms, `let` destructures, `if let` / `while let` / `let ... else` heads, and function parameter positions (limited). The grammar entry is `pattern` ([logos.peg:1040](../../../tools/peg_gen/grammars/logos.peg#L1040)). A `pattern` is one or more `pat_single` joined by `|` (or-patterns).

A pattern is either *irrefutable* — guaranteed to match (e.g. `name`, `(a, b)` against a tuple type) — or *refutable* — may fail to match (e.g. `Some(x)`, `0`). `let` and function parameters require irrefutable patterns; `match` arms, `if let`, and `let ... else` accept refutable ones.

## Bindings and Wildcards

```logos
x          // bind to `x`
_          // ignore (does not bind a name)
_count     // bind `_count` (leading underscore = "intentionally unused")
ref x      // bind by reference: takes `&T` instead of moving
ref mut x  // bind by mutable reference
n @ pat    // bind whole value to `n` and also recurse into `pat`
```

`x` is parsed as `PAT_WILD` with `NAME: x` ([logos.peg:1161](../../../tools/peg_gen/grammars/logos.peg#L1161)) — the same node code is used for `_` (with `NAME: _`). Whether the name binds is determined later by sema.

## Literal Patterns

```logos
0
-3
true   false
"hello"
0..=9            // inclusive range (exclusive `..` not yet a pattern form)
-5..=-1          // negative range bounds
```

Float literal patterns are not supported — use guards (`if x == 1.5`).

## Reference Patterns

```logos
&x          // matches a `&T`, binds `x: T`
&mut x      // matches a `&mut T`
```

The `&` here is part of the pattern syntax, not the borrow operator. Reference patterns peel off one level of borrow.

## Tuple Patterns

```logos
(a, b)
(_, _, c)
(x, y,)             // trailing comma OK
```

Tuples patterns must include at least two elements (single-element form `(x,)` is parsed as a tuple expression at the literal level, but here `(x,)` is rejected by `pat_single` — use `x` directly).

## Struct Patterns

```logos
Point { x, y }                    // field shorthand
Point { x: a, y: b }              // rename
Point { x, .. }                   // `..` to ignore the rest
Pair {}                           // empty struct or "match anything"
```

Field shorthand (`x` alone) binds the field's value to a same-named local. Use `..` (`PAT_REST`) to ignore unnamed fields.

## Enum / Variant Patterns

```logos
Color::Red                         // PAT_VARIANT
Maybe::Some(x)                     // PAT_VARIANT_DATA — single payload
Pair::Both(a, b)                   // multiple payloads
Maybe::Some(_)                     // ignore payload
```

Variant patterns must use the qualified `Type::Variant` form. Bare `Some(x)` (without the enum name) is not accepted.

## Slice / Array Patterns

```logos
[]                       // empty slice
[a, b, c]                // exactly 3 elements
[head, ..]               // length ≥ 1
[first, .., last]        // first and last, anything between
[.., last]
```

Slice patterns work on arrays and `&[T]` slices. The `..` rest binding is positional only — there is no `name @ ..` to capture the middle.

## Or-Patterns

```logos
0 | 1 | 2          => "small",
Color::Red | Color::Blue => "primary-ish",
```

All branches of an `|` must bind the same set of names with the same types. Or-patterns may appear at any nesting depth.

## Hermes Patterns

```logos
@null
@true   @false
@42     @-7
@"hello"
@[a, b, c]                  // hermes array — exact length
@[head, ..]                 // hermes array — prefix-match
@{ "name": n, "age": a }    // hermes map — must contain these keys
@{}                         // empty map
@<I32>[..]                  // typed array — any-length match
@<I32, AnyVal>{..}          // typed map — any-content match
```

Hermes patterns destructure `@{...}` / `@[...]` SDN values. Inner values inside a `@`-pattern don't repeat the `@`. See [Hermes](hermes.md) for the wire types these match against.

## `@` Bindings

```logos
n @ 1..=9              // bind `n` AND require 1..=9
e @ Maybe::Some(_)     // bind whole `e`, also assert variant
```

`name @ pattern` binds `name` to the whole matched value and recurses into the inner pattern. Useful when you need both the structure check and the original value.

## Guards

A `match` arm may add an `if expr` guard after the pattern:

```logos
match value {
    n if n > 0  => "positive",
    n if n < 0  => "negative",
    _           => "zero",
}
```

The guard runs after the pattern matches; if it returns false, matching falls through to the next arm. Guards may reference any names bound by the pattern.

## Exhaustiveness

`match` is checked for exhaustiveness:

- C-style enums must list every variant or include a wildcard.
- Tagged enums benefit from variant inference but currently still demand a wildcard for "future-compatibility" cases (see Roadmap).
- Integer matches require a wildcard arm unless every value is covered (rare).
- Boolean matches must cover both `true` and `false`.

## Roadmap

- **Exclusive range patterns** — `0..n` not yet a pattern form (only `..=`).
- **Char / byte patterns** — pending the char primitive.
- **Box / Rc / smart-pointer patterns** — currently require manual `match (*p) { ... }`.
- **`name @ ..`** in slice rest — not yet supported.
- **Pattern types in function parameters** — `fn f((a, b): (i32, i32))` parses but tuple-destructure-in-params is fragile under generics.
