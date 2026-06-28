# Expressions

An *expression* produces a value. The grammar entry point is `expr` ([logos.peg:2392](../../../tools/peg_gen/grammars/logos.peg#L2392)). Expressions are built from a precedence chain: `expr → range_expr → log_expr → cmp_expr → bitor_expr → bitxor_expr → bitand_expr → shift_expr → add_expr → mul_expr → cast_expr → unary_expr → atom → primary_expr`. Each level binds tighter than the one above it (ranges sit at the top, just below the assignment statement level).

Many statements are expressions in disguise: `if`, `match`, `loop`, blocks, and `unsafe { ... }` are all usable as primary expressions and yield values via tail-position evaluation or `break <value>`.

## Operator Precedence

Highest to lowest binding:

| Level | Operators | Associativity | Notes |
|-------|-----------|---------------|-------|
| Postfix | `.f` `.f(...)` `.0` `[i]` `?` | left | atom chain |
| Unary | `*x` `&x` `&mut x` `-x` `!x` | right | prefix |
| Cast | `x as T` | left | chains: `x as i64 as i24` |
| Multiplicative | `*` `/` `%` | left | |
| Additive | `+` `-` | left | |
| Shift | `<<` `>>` | left | |
| Bitwise AND | `&` | left | |
| Bitwise XOR | `^` | left | |
| Bitwise OR | `\|` | left | |
| Comparison | `==` `!=` `<` `<=` `>` `>=` | non-chainable | at most one per level |
| Logical | `&&` `\|\|` | left | one level — parenthesise when mixing |
| Assignment | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | — | statement-level only |

Bitwise operators have four distinct precedence levels (tightest to loosest: `<<`/`>>`, then `&`, then `^`, then `|`), matching Rust — so `a & b | c` parses as `(a & b) | c`. The logical operators `&&` and `||`, by contrast, share **one** level (a divergence from Rust, where `&&` binds tighter than `||`) — so parenthesise when mixing them. Comparisons cannot chain: `a < b < c` parses but is rejected during semantic analysis (use `a < b && b < c`).

## Atoms and Postfix Chains

`atom` is `primary_expr` followed by zero or more postfix suffixes ([logos.peg:2662](../../../tools/peg_gen/grammars/logos.peg#L2662)):

```logos
xs.len()              // METHOD_CALL
pair.0                // TUPLE_INDEX
point.x               // FIELD_READ
buf[i]                // INDEX_READ
parse(s)?             // TRY_EXPR — early-return on Err / None
```

Method calls dispatch on the receiver's type: inherent methods first, then trait methods. `?` propagates `Result::Err` / `Option::None` through the enclosing function — see [Statements](statements.md#return) and [feat: ? operator](../../README.md).

## Unary Operators

```logos
*p           // dereference (raw or reference)
&x           // shared borrow
&mut x       // exclusive borrow
-n           // arithmetic negate (signed only)
!b           // bool not / bitwise not on integers
```

`&mut` is a single token sequence, not `&` followed by `mut x`. The grammar recognises `AMP KW_MUT unary_expr` as one production ([logos.peg:2650](../../../tools/peg_gen/grammars/logos.peg#L2650)).

## Casts

```logos
let n: i64 = (count as i64) * 8;
let p = (raw as *const u8);
```

`as` performs explicit numeric, pointer, and tag conversions. It is the only way to narrow integers, change signedness, or convert between integer and pointer. Implicit widening (`u8 → u32`, `i32 → i64`) does not need `as` — see [Types → Implicit Widening](types.md#implicit-widening).

## Primary Expressions

The `primary_expr` rule lists every leaf form ([logos.peg:2697](../../../tools/peg_gen/grammars/logos.peg#L2697)). Ordering matters: more specific alternatives (e.g. `struct_lit`, `call_expr`) come before bare `IDENT` so the parser commits early.

### Literals

```logos
42            // IntLit (widens to context)
42i64         // typed integer literal
1.5           // FloatLit
1.5f32        // typed float literal
true  false   // bool
"hello\n"     // string literal (escape-aware)
r"raw\n"      // raw string (no escapes)
'a'           // char literal
b"hi"         // byte-string literal → [u8; 2]
()            // unit value
```

See [Lexical](lexical.md#numeric-literals) for the integer/float regex and suffix list.

### Variable references and calls

```logos
x                     // VAR_REF
add(1, 2)             // CALL — IDENT(args)
Vec::new()            // STATIC_CALL — Type::name(args)
parse::<u32>("42")    // GENERIC_CALL — turbofish on the function
HashMap::<K,V>::new() // turbofish on the type
```

A bare identifier is parsed as `VAR_REF`; the grammar's earlier alternatives ensure `Foo { ... }`, `Foo(...)`, and `Foo::Bar` win first when applicable.

### Struct literals

```logos
Point { x: 1.0, y: 2.0 }
Pair { fst: a, snd: b }
Pair::<i32, i32> { fst: 1, snd: 2 }   // generic_struct_lit (turbofish)
Config { debug: true, ..base }        // struct_update_lit
Config { ..base }                     // all-from-base form
```

Field shorthand: a bare `IDENT` inside `{ ... }` desugars to `name: name`. Trailing commas are allowed.

### Tuple literals

```logos
()                    // unit
(x, y)                // 2-tuple
(x,)                  // 1-tuple — trailing comma required
(a, b, c, d)          // n-tuple
```

A single-element tuple needs the trailing comma; otherwise `(x)` is a parenthesised expression.

### Array literals

```logos
[1, 2, 3]             // ARR_LIT
[0; 16]               // arr_fill_lit — N copies of the value
[v; sizeof...(P)]     // pack-sized fill (variadic length, see Generics)
```

The fill form `[v; N]` accepts `N` as an integer literal, a named const, a const-generic parameter, a `sizeof...(P)` form, or a `metacall { … }` block whose tail integer is evaluated at compile time (`[v; metacall { compute() }]` — Logos's replacement for Rust const-expression lengths).

### Enum literals

```logos
Color::Red                    // no payload
Maybe::Some(42)               // tuple payload
Maybe::<i32>::None            // (turbofish via call_expr) — see Generics
```

### Comprehensions

```logos
[x * 2 for x in xs]                    // list_comp
[x for x in xs if x > 0]
{ x: x * 2 for x in xs }               // map_comp — single binder
{ x: x for x in xs if x > 0 }
```

List and map comprehensions desugar to `for`-loop accumulators. The binder is a single `IDENT` and the iterand must be an array or slice (not `Vec` — iterate `&v[..]`). They are eager; lazy iterators come from explicit `Iterator` chains. A list comprehension requires `Vec` + `vec_new` in scope (`use logos.mem.collections.vec;`) and a map comprehension requires `HashMap` + `hashmap_new` (`use logos.mem.collections.hashmap;`); without the import the comprehension is ill-formed.

### Writ literals

```logos
@{"name": "Alice", "age": 30}        // writ map
@[1, 2, 3]                            // writ array
@<I32>[1, 2, 3]                       // typed dense array
@<I32, AnyVal>{1: @42}                // typed map
@"hello"  @42  @true  @null           // scalar SDN values
@[x for x in xs]                      // writ list comprehension
```

`@`-prefixed literals build immutable Writ values stored in rodata (`WritStatic`). Inner values inside a Writ literal don't need their own `@`. See [Writ](writ.md) for the wire format and view types.

`$ident` and `${expr}` inside a Writ literal are *capture* placeholders that splice runtime values into the literal — they require `metacall` context or run-time builder semantics, see [Metaprogramming](metaprog.md#capture).

### Closures

```logos
|x| x + 1                          // implicit return type
|x: i32| -> i32 { x + 1 }          // explicit param + return
|| -> i32 { 42 }                   // no params
move |x| x + offset                // move-capture
```

Capture mode is inferred per variable: a variable the closure only *reads* is captured by value (copied into the environment); a variable the body *mutates* (assign / field-write / index-write / deref-write) is captured by reference so the write propagates. `move` forces all captures by value. See [Types → Function and Closure Types](types.md#function-and-closure-types).

### Control-flow as expression

```logos
let m = if cond { a } else { b };
let v = match x { 0 => "zero", _ => "other" };
let r = loop { if done { break 42; } };
let z = unsafe { *raw_ptr };
```

`if`, `match`, `loop`, blocks, and `unsafe` blocks all have an expression form. Their value comes from the tail expression of the chosen branch, or — for `loop` — from the value passed to `break`.

`if let` and `while let` introduce pattern-matched bindings:

```logos
if let Some(x) = parse(s) { use(x); }
while let Some(x) = it.next() { ... }
```

### `?` (try) operator

```logos
fn read() -> Result<u32, IoError> {
    let s = open("f")?;            // early return on Err
    let n = parse(s)?;
    Ok(n)
}
```

Postfix `?` short-circuits to the enclosing function's `Result` / `Option` type. When the inner and outer error types differ, `?` inserts `E_outer::from(inner_err)` (requires `impl From<E_inner> for E_outer`); non-`Result`/`Option` operands dispatch through `Try` / `FromResidual`. See [feat: try operator](../../README.md).

### Ranges

```logos
a..b    a..=b    a..    ..b    ..=b    ..
```

The six range forms desugar to stdlib `Range` structs (`RangeI32` / `RangeI64` / `RangeOfIncl<T>`, …) implementing `Iterator`, so `for i in 0..n {}` works; bounds must be integer-typed. Range sits at the top of the value-expression precedence cascade (below it: the logical operators).

### Metaprogramming forms

```logos
metacall expand_filter::<T...>()      // call form — splice fn return at compile time
metacall (a + b * cube(3))             // paren-expr form — arbitrary expression
metacall { let s = compute(); s + 1 }  // block form — full Logos statements
sizeof...(P)                           // length of a variadic pack
quote_item! { fn helper() { ... } }    // typed AST literal — items
quote_expr! { x + y }                  // typed AST literal — expression
quote_ty!   { Vec<u32> }               // typed AST literal — type
type_of::<T>()                         // sema-side: resolve T to a Type value
template_of::<X>()                     // sema-side: bind to AST node of X
```

`metacall` evaluates the bracketed code at compile time and splices the result as a literal. Three forms — call, paren-expr, block — share a single mechanism (synthesised JIT thunk → invoke → splice). Block / paren-expr cannot capture enclosing-fn locals or contain a nested `metacall`. Quote forms produce typed AST literals consumed by metafunctions. `type_of` / `template_of` are sema-side intrinsics that bake compile-time results into the IR. See [Metaprogramming](metaprog.md#metacall).

### Macro-style repeat groups

```logos
#(name = value),*    // repeat with comma separator
#(stmt;)*            // repeat with `;` separator (no trailing)
#(item)&*            // repeat with `&` separator
```

The `#(...)<sep>*` form expands a fragment once per element of any pack referenced inside. Used inside `quote_*!` bodies for variadic code generation.

## Assignment and Compound Assignment

Assignment is a *statement*, not an expression — there is no `(x = y)` value. Forms:

```logos
x = expr;
x += expr;       // and -= *= /= %= &= |= ^= <<= >>=
x.f = expr;
x.f[i] = expr;
*p = expr;       // dereference write
arr[i] = expr;
```

See [Statements → Assignment](statements.md#assignment).

## Roadmap

- **Method-call generic args** — `xs.iter::<T>()` is reserved syntax; deduction usually suffices today.
- **Block-expression value capture** — `let x = { ... };` works for tail-expression but bare statement blocks have edge cases around early-return.
