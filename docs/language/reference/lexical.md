# Lexical Structure

This page describes the surface tokens produced by the Logos lexer. The authoritative source is the `%tokens { … }` block in [tools/peg_gen/grammars/logos.peg](../../../tools/peg_gen/grammars/logos.peg) (lines 255–375).

## Source Encoding

Logos source files are byte sequences. The lexer is currently **ASCII-only** outside of string literals — non-ASCII bytes (including in line comments) are rejected. Inside a string literal, bytes are passed through verbatim, so UTF-8 string contents work today; UTF-8 source-level support (identifiers, comments) is planned.

See [Roadmap](roadmap.md#lexical) for status.

## Whitespace and Comments

Whitespace (`[ \t\n\r]+`) separates tokens and is otherwise ignored.

Two comment forms exist:

- Line: `// …` to end of line.
- Block: `/* … */` — non-nesting (the underlying regex is non-greedy `.*?`).

```logos
// line comment
/* block comment */
let x: i32 = /* inline */ 42;
```

## Identifiers

```
IDENT = [a-zA-Z_][a-zA-Z0-9_]*
```

ASCII letters, digits, and `_`. First character cannot be a digit. Keywords (see below) are reserved and never match an identifier.

## Lifetimes

```
LIFETIME = '[a-z][a-z0-9_]*
```

A leading apostrophe followed by a lowercase identifier: `'a`, `'static_lt`, `'b1`. Uppercase lifetime names are not lexed.

## Keywords

Reserved identifiers. Keywords with currently no grammar role are still reserved for future use.

| Active | | | |
|--------|-|-|-|
| `package` | `use` | `pub` | `extern` |
| `fn` | `struct` | `enum` | `trait` |
| `impl` | `type` | `const` | `static` |
| `let` | `mut` | `if` | `else` |
| `match` | `while` | `loop` | `for` |
| `in` | `break` | `continue` | `return` |
| `true` | `false` | `null` | `as` |
| `where` | `dyn` | `unsafe` | `move` |
| `ref` | `new` | `tagged` | `auto` |
| `typeof` | `meta` | `metacall` | `template` |
| `quote_item` | `quote_expr` | `quote_ty` | `eidos` |
| `genos` | | | |

| Reserved (no grammar role yet) |
|---|
| `async`, `await` — kept for the stackless-coroutine path on wasm32 targets where threads aren't available. |

## Numeric Literals

```
INTEGER = [-]?(0x[0-9a-fA-F_]+ | 0b[01_]+ | 0o[0-7_]+ | [0-9][0-9_]*)
          (i8|i16|i24|i32|i56|i64|i128|u8|u16|u24|u32|u56|u64|u128|usize|isize)?
FLOAT   = [-]?[0-9][0-9_]*\.[0-9][0-9_]*([eE][+-]?[0-9][0-9_]*)?(f32|f64)?
```

- Bases: decimal (default), hex `0x…`, binary `0b…`, octal `0o…`.
- `_` may appear anywhere in the digit run as a visual separator: `1_000_000`, `0xDEAD_BEEF`.
- Optional type suffix forces the literal's type: `42u8`, `0xFFi64`, `1.5f32`. Without a suffix the literal is `IntLit`/`FloatLit` and widens to context (see [Types](types.md)).
- Negative literals: the leading `-` is part of the token (the regex includes `[-]?`). In source positions where unary `-` would also work, both forms parse — `[-1, -2]` reads as a literal array; `0 - 1` is a binary expression.

> Note: leading `-` lexed inside the literal regex differs from `MINUS INTEGER` parsed at the grammar level (e.g. inside `[N; -1]` or generic args `f::<-3>()`). Both produce the same `LIT_INT` AST shape with a `LO_NEG` flag set on the negative variant.

> Floats without a fractional part — `1.` or `.5` — are **not** accepted; both digit runs around `.` are required.

## Boolean Literal

`true` and `false` are keyword tokens (`KW_TRUE`, `KW_FALSE`).

## String Literals

Two forms:

```
STRING     = "([^"\\] | \\.)*"               — escape-aware
RAW_STRING = r"[^"]*"  |  r#"…"#  |  r##"…"##  |  …   — no escapes
```

- `"hello\n"` — escape-aware. `\\` consumes the next byte literally; standard escapes (`\n`, `\t`, `\r`, `\\`, `\"`, `\0`) are recognised by the runtime.
- `r"…"` — raw form, no escape processing. Contents end at the first closing `"`.
- `r#"…"#`, `r##"…"##`, … — hash-fenced raw strings; the body ends at the matching `"` followed by the same number of `#`s, so the body may itself contain `"` (or `"#`, etc.). The grammar file shows only the unfenced regex; the hand-rolled lexer accepts any number of leading/trailing `#`s.

There is no character literal (`'a'`) and no byte-string literal (`b"…"`). Single-byte values are written as integer literals.

## Punctuation and Operators

Single-character: `{ } [ ] ( ) : , ; . * & | < > + - / % ! ? = ^ # @ $`.

Multi-character (matched longest-first):

| Form | Notes |
|------|-------|
| `->` | Function return arrow |
| `=>` | Match-arm body, annotation key sep |
| `::` | Path separator |
| `..` `..=` `...` | Range exclusive / inclusive / pack-expand |
| `==` `!=` `<=` `>=` | Comparison |
| `&&` `\|\|` | Short-circuit logic |
| `<<` `>>` | Bit shifts |
| `<<=` `>>=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` | Compound assignment |

## Token Disambiguation

The PEG performs longest-match on alternatives. Two cases are worth noting:

- **Generic args vs comparison**: `f::<T>` — the `<` after `::` enters generic-arg mode. Bare `<` after an expression position is comparison. The grammar uses `::<` as the unambiguous turbofish.
- **Block end vs object literal**: `{` after a control-flow expression starts a block; in expression-context inside parens or after `=` it can also start a struct literal.

## Outside the Lexer

The following are not tokens but parse-level constructs flagged here so they don't surprise readers of `logos.peg`:

- `KW_QUOTE_ITEM`/`KW_QUOTE_EXPR`/`KW_QUOTE_TY` are the keyword form `quote_item`, etc. The trailing `!` of `quote_item! { … }` is a separate `BANG` token.
- `#[…]` (annotation) is parsed at the grammar level; only the `HASH`, `LBRACKET`, `RBRACKET` tokens exist.

## Roadmap

- UTF-8 in identifiers and comments. Tracked via the language quirks list.
- Character and byte-string literals — pending.
