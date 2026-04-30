# Statements

A *statement* is a unit of execution that does not produce a value. Statements live inside `block`s ([logos.peg](../../../tools/peg_gen/grammars/logos.peg)) and are separated by `;`. Tail expressions inside a block (no trailing `;`) supply the block's value when the block is used in expression position — see [Expressions → Control-flow](expressions.md#control-flow-as-expression).

## `let` Bindings

```logos
let x = 42;
let y: i64 = compute();
let mut buf: Vec<u8> = Vec::new();
let (a, b) = pair;                     // tuple destructure
let Point { x, y } = origin;           // struct destructure (via let-else or pattern let)
let Some(v) = parse(s) else { return None; };
```

Forms:

- **`let name = expr;`** — type inferred from rhs.
- **`let name: T = expr;`** — explicit type, rhs widens / coerces to T.
- **`let mut name ...`** — binding is rebindable / interior-mutable through `&mut`.
- **`let (a, b, ...) = expr;`** — tuple destructure (`LET_DESTRUCT`).
- **`let pat = expr else { ... }`** — refutable bind with diverging else (`LET_ELSE`). The else block must diverge (`return`, `break`, `panic`, `loop`).

A bare `let pat = expr;` with a refutable pattern is rejected — use `if let` or `let ... else`.

## Assignment

```logos
x = expr;
arr[i] = expr;
*p = expr;
s.field = expr;
a.b.c = expr;             // up to 2 levels chain — see grammar
s.field[i] = expr;
x.0 = expr;               // tuple field write
```

Compound forms apply the operator before re-assigning:

```logos
x += 1;        // also -= *= /= %= &= |= ^= <<= >>=
s.count += 1;
arr[i] *= 2;
a.b.c -= delta;
```

Assignment is statement-only: there is no `(x = y)` expression.

## `return`

```logos
return;
return value;
```

`return` always exits the enclosing function. Trailing `;` or `,` is required (the grammar accepts either).

The tail-expression of a function body is also a return — `fn add(a: i32, b: i32) -> i32 { a + b }` returns `a + b` without an explicit `return`.

## Control Flow

### `if` / `if let`

`if` is an expression (see [Expressions → Control-flow](expressions.md#control-flow-as-expression)) but is also commonly used as a statement when the value is discarded:

```logos
if cond { do_thing(); }
if cond { ... } else if other { ... } else { ... }
if let Some(x) = parse(s) { use(x); }
```

### `while` / `while let`

```logos
while cond { ... }
while let Some(x) = it.next() { use(x); }
```

### `loop`

```logos
loop { if done { break; } step(); }
let v = loop { if done { break 42; } };   // value loop via break
```

`loop` has no condition. It is the only loop form whose value is non-unit (via `break <expr>`).

### `for`

```logos
for i in 0..n          { use(i); }    // exclusive range
for i in 0..=n         { use(i); }    // inclusive range
for x in xs            { use(x); }    // FOR_EACH — over an iterator
```

The range forms produce integer indices. The `for x in iter` form requires `iter` to implement `Iterator`. See [Generics & Traits](generics-traits.md#iterator).

### `break` / `continue`

```logos
break;
break value;
break 'outer;             // labelled
break 'outer value;
continue;
continue 'outer;
```

Labels are written `'name:` before a loop and referenced as `'name` after `break` / `continue`:

```logos
'outer: for i in 0..n {
    for j in 0..m {
        if grid[i][j] == target { break 'outer (i, j); }
    }
}
```

### `match`

```logos
match x {
    0       => "zero",
    1 | 2   => "small",
    n if n < 10 => "single digit",
    Maybe::Some(v) => use(v),
    _       => "other",
}
```

Each arm is `pattern => body` with optional `if guard`. The body can be an expression (with optional trailing `,`), a block, or a single statement. `match` is exhaustive — unmatched values produce a sema diagnostic (with caveats on tagged enums; see [Patterns](patterns.md)).

Like `if`, `match` doubles as an expression — the value of the executed arm becomes the value of the whole `match`.

## `unsafe` Block

```logos
unsafe {
    let v = *raw_ptr;
    write_volatile(addr, 0);
}
```

Inside `unsafe`, the following are permitted:

- Dereferencing raw pointers (`*p` where `p: *const T` / `*mut T`).
- Calling `unsafe fn`.
- Reading from / writing to `extern static mut`.
- Implementing `unsafe trait` items (in an `unsafe impl`, not here).

`unsafe` is also valid as an expression — `let n = unsafe { *p };` — see [Expressions](expressions.md).

## Expression Statement

Any expression followed by `;` is a statement; the value is dropped:

```logos
compute();
xs.push(item);
println("hello");
```

## `metacall` Statements

```logos
metacall emit_traits::<T...>();
```

A `metacall` at statement position splices items / statements from a metafunction at compile time. See [Metaprogramming](metaprog.md#metacall).

## Roadmap

- **`yield`** — keyword reserved; coroutine-yield form planned alongside the stackful-fiber lowering ([memory: feat_coroutines_design](../../README.md)).
- **`async` / `await`** — keywords reserved for the wasm32 stackless path; not on near-term roadmap for native targets.
- **Tuple destructure with type annotation** — `let (a, b): (i32, i64) = ...` parses but type-checks via field-by-field inference; full destructure-with-types still rough.
