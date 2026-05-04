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

Logos has 12 distinct LHS shapes, each with a plain (`=`) and a compound (`+= -= *= /= %= &= |= ^= <<= >>=`) form. The grammar distinguishes them as separate productions because each requires its own borrow-check + emit path.

### LHS shapes (grammar productions)

| # | Production | Plain `=` example | Compound `+=` example |
|---|---|---|---|
| 1 | `assign_stmt` | `x = expr;` | (handled by `compound_assign_stmt`) |
| 2 | `compound_assign_stmt` | — | `x += expr;` (bare ident only) |
| 3 | `field_write_stmt` | `s.field = expr;` | — |
| 4 | `field_compound_assign_stmt` | — | `s.field += expr;` |
| 5 | `chain_field_write_stmt` | `a.b.c = expr;` | — |
| 6 | `chain_field_compound_assign_stmt` | — | `a.b.c += expr;` |
| 7 | `tuple_field_write_stmt` | `x.0 = expr;` | — |
| 8 | `tuple_field_compound_assign_stmt` | — | `x.0 += expr;` |
| 9 | `index_write_stmt` | `arr[i] = expr;` | — |
| 10 | `index_compound_assign_stmt` | — | `arr[i] += expr;` |
| 11 | `field_index_write_stmt` | `s.field[i] = expr;` | — |
| 12 | `field_index_compound_assign_stmt` | — | `s.field[i] += expr;` |
| 13 | `deref_write_stmt` | `*p = expr;` | — |
| 14 | `deref_field_write_stmt` | `(*p).field = expr;` | — |
| 15 | `deref_field_compound_assign_stmt` | — | `(*p).field += expr;` |

The grammar productions live in [logos.peg](../../../tools/peg_gen/grammars/logos.peg) under `assign_stmt`, `compound_assign_op`, `compound_assign_stmt`, etc. The `compound_assign_op` production is shared:

```peg
compound_assign_op <- PLUSEQ / MINUSEQ / STAREQ / SLASHEQ / PERCENTEQ
                    / AMPEQ / PIPEEQ / CARETEQ / SHLEQ / SHREQ
```

Semantics: `lhs op= rhs` desugars to `lhs = lhs op rhs` with the lhs evaluated **once** for the read AND the write. Sema produces a single LIR statement (e.g. `SCompoundAssign`) rather than separate read/write for compounds.

### Coverage gaps and known asymmetries

Some LHS shapes have plain-write but no compound-write (or vice-versa) wired up. The compound permutations are an active source of bugs (the catalog in `docs/baghunt/statements.md` will enumerate). Concretely:

- **Chain depth limit**: `chain_field_path` parses arbitrary depth, but lowering / borrow-check stop after 2 hops in older paths. Deep chains (`a.b.c.d.e = ...`) may parse but mis-lower in some receivers.
- **Deref chain**: `(*p).field` is supported; `(**p).field` (double-deref before field) is not parsed as a write LHS.
- **Index of method-call result**: `obj.method()[i] = expr;` does not parse; you must bind the result first.
- **Compound on tuple-field of deref**: `(*p).0 += 1;` is not parsed. The grammar's tuple-compound-assign requires a bare ident receiver.

Compound forms are wired up symmetrically with their plain counterparts where they exist, but the symmetry should be verified group-by-group during bug-hunt.

### Operator semantics

| Op | Read kind | Notes |
|---|---|---|
| `+=` `-=` `*=` `/=` `%=` | arithmetic | Integer-only when LHS is integer; float for floats; mixed coerces per [int_widening](../../../.claude/projects/-home-victor-devel-logos/memory/feat_int_widening.md). |
| `&=` `|=` `^=` | bitwise | Integers + bool. No short-circuit. |
| `<<=` `>>=` | shift | Shift count interpreted as unsigned; signed RHS rejected. Right-shift on signed is arithmetic; on unsigned is logical. |

Overflow on `+=` etc. follows the same wrapping/abort rules as the binary operator (`+`, `-`, `*`); see [Expressions → Arithmetic](expressions.md#arithmetic).

### Borrow-check + mutability

Plain `=` and compound `op=` both require the LHS to resolve to a place expression that the borrow-checker considers writable: the receiver chain must originate at a `let mut` binding, a `&mut T` parameter, or a raw `*mut T`. Compound forms additionally require that the LHS be readable (no exclusivity violation between the implicit read and write — they're treated as one access).

Assignment is **statement-only**: there is no `(x = y)` expression. To use the result of a write inside an expression, use a temporary `let`.

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
