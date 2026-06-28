# Lexical Structure

Scope: token-level rules for the three lexed surfaces — the Logos source language, the Writ data language, and the HRPC interface-definition language. Source layers: PEG grammars (`tools/peg_gen/grammars/{logos,writ,hrpc}.peg`) plus the C++ literal-decode paths in `src/compiler/`.

## Keywords

### `lex.keyword.reserved-set` — Reserved keyword set

The following are reserved keywords matched as distinct tokens and unavailable as ordinary identifiers: `continue`, `quote_item`, `quote_expr`, `quote_ty`, `template`, `package`, `instantiate`, `eidos`, `genos`, `auto`, `metacall`, `static`, `return`, `extern`, `struct`, `union`, `match`, `while`, `break`, `false`, `trait`, `const`, `type`, `impl`, `enum`, `loop`, `else`, `true`, `for`, `use`, `mut`, `let`, `dyn`, `tagged`, `pub`, `new`, `fn`, `if`, `in`, `as`, `where`, `unsafe`, `move`, `typeof`, `offset_of`, `ref`, `null`, `async`, `await`.

**Divergence:** Adds Logos-specific keywords absent in Rust: `quote_item`/`quote_expr`/`quote_ty`/`template`/`package`/`instantiate`/`eidos`/`genos`/`auto`/`metacall`/`tagged`/`new`/`typeof`/`offset_of`/`null`; lacks Rust keywords (`mod`, `pub(crate)`, `crate`, `self`, `Self`, fn-async forms, etc.) handled elsewhere.

**Source:** `tools/peg_gen/grammars/logos.peg#L328-L380`

### `lex.keyword.async-await-reserved` — async/await reserved but unused

`async` and `await` are tokenized as keywords but reserved with no grammar use (kept for a future stackless-coroutine path on wasm32/64).

**Uncertainty:** Reserved-without-use status is stated in the source comment; actual rejection behavior in the surface grammar is defined in the `%rules` unit.

**Source:** `tools/peg_gen/grammars/logos.peg#L377-L380`

## Punctuation and Operators

### `lex.punct.symbols` — Punctuation and operator tokens

Single- and multi-character delimiter/operator tokens are recognized, including: `{` `}` `[` `]` `(` `)` `:` `::` `,` `;` `->` `=>` `.` `..` `..=` `...` `*` `&` `&&` `|` `||` `!` `?` `=` `==` `!=` `<` `<=` `>` `>=` `<<` `>>` `<<=` `>>=` `+` `-` `/` `%` `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `^` `@` `#` `$`. Multi-character operators are matched in longest-match-first order (e.g. `<<=` before `<<`, `..=` and `...` before `..`, `&&` before `&`).

**Source:** `tools/peg_gen/grammars/logos.peg#L382-L434`

## Literals (Logos)

### `lex.literal.integer` — Integer literal syntax and width suffixes

An integer literal matches an optional leading `-`, then a decimal (`[0-9][0-9_]*`), hex (`0x[0-9a-fA-F_]+`), binary (`0b[01_]+`), or octal (`0o[0-7_]+`) magnitude, with `_` digit separators, optionally suffixed by a width tag drawn from `{i8, i16, i24, i32, i56, i64, i128, u8, u16, u24, u32, u56, u64, u128, usize, isize}`.

**Divergence (A11):** width set includes Writ-fabric widths `i24`/`u24`/`i56`/`u56` beyond Rust's `{8,16,32,64,128}`+size. Also: a leading `-` is part of the integer token itself (Rust treats `-` as a separate unary operator).

**Source:** `tools/peg_gen/grammars/logos.peg#L457`

### `lex.literal.float` — Float literal syntax

A float literal matches an optional leading `-`, an integer part, a mandatory `.` with a fractional part (both `[0-9][0-9_]*`), an optional exponent `([eE][+-]?[0-9][0-9_]*)`, and an optional suffix `f32` or `f64`. `_` digit separators are permitted.

**Divergence:** A leading `-` is part of the float token (Rust parses `-` as separate unary minus). A fractional part is mandatory (no `1.` form); the float-width suffix set is `{f32, f64}`.

**Source:** `tools/peg_gen/grammars/logos.peg#L456`

### `lex.literal.string` — String, raw-string, and byte-string literals

`STRING = "([^"\\]|\\.)*"` (escapes via backslash). `RAW_STRING = r"[^"]*"` (no escape processing, no embedded `"`). `BYTE_STRING = b"([^"\\]|\\.)*"`.

**Divergence:** Raw strings support only the bare `r"..."` form (no `r#"..."#` hash-delimited variant), so a raw string cannot contain a `"`.

**Source:** `tools/peg_gen/grammars/logos.peg#L453-L455`

### `lex.literal.char` — Char literal

A char literal `CHAR_LIT = '(\\.|[^'\\])'` is a single `\`-escape or one Unicode codepoint between apostrophes; it is matched BEFORE LIFETIME so `'A'` (with closing apostrophe) wins over a lifetime read. The body decodes to the scalar codepoint value.

**Source:** `tools/peg_gen/grammars/logos.peg#L458-L465`

### `lex.literal.char-before-lifetime` — Char-vs-lifetime disambiguation

When the source could begin either a char literal or a lifetime, the lexer prefers the char literal: `'a'` lexes as a char, `'a` (no closing apostrophe) lexes as a lifetime.

**Source:** `tools/peg_gen/grammars/logos.peg#L458-L466`

## String Escapes and Char Decoding

### `lex.str.escape-set` — String literal escape set

String literals support the escape sequences `\\`, `\"`, `\n`, `\r`, `\t`, `\0`, and `\xHH` (two hex digits) for control bytes `< 0x20`; all other bytes appear verbatim.

**Source:** `src/compiler/sema_render.cpp#L41-L65`

### `lex.char.utf8-scalar-decode` — Char literal multi-byte UTF-8 decode

A char literal body whose first byte `>= 0x80` is decoded as a multi-byte UTF-8 sequence: lead byte `110xxxxx` => 2 bytes, `1110xxxx` => 3, `11110xxx` => 4; any other lead byte is an error (`invalid UTF-8`). Fewer than the required continuation bytes is an error (`truncated UTF-8`). The scalar value is the assembled code point.

**Source:** `src/compiler/sema_stmt.cpp#L4401-L4412`

## Tokens (Logos)

### `lex.token.lifetime` — Lifetime token

`LIFETIME = '[a-z_][a-z0-9_]*` — an apostrophe followed by a lowercase-initiated identifier (no closing apostrophe).

**Divergence:** Lifetime names must start with a lowercase letter or `_`; uppercase-initial lifetimes (allowed in Rust) are not recognized.

**Source:** `tools/peg_gen/grammars/logos.peg#L466`

### `lex.token.ident` — Identifier token

`IDENT = [a-zA-Z_][a-zA-Z0-9_]*` — ASCII letter/underscore followed by ASCII alphanumerics/underscores.

**Divergence:** Identifiers are ASCII-only; Rust permits Unicode (XID) identifiers and raw identifiers `r#name`.

**Source:** `tools/peg_gen/grammars/logos.peg#L467`

## Comments and Whitespace (Logos)

### `lex.comment.doc-tokens` — Doc comments emitted as tokens

Doc comments are lexed as real tokens (not skipped): `DOC_LINE = ///[^\n]*` (outer line), `DOC_INNER = //![^\n]*` (inner module-level line), `DOC_BLOCK = /**...*/` (outer block), `DOC_BLOCK_INNER = /*!...*/` (inner block). These attach as documentation to the following item / enclosing module.

**Source:** `tools/peg_gen/grammars/logos.peg#L436-L450`

### `lex.comment.skip` — Whitespace and ordinary comments skipped

Inter-token skip whitespace is `[ \t\n\r]+`; ordinary line comments `//[^\n]*` and block comments `/*...*/` are skipped. The `///`, `//!`, `/**`, `/*!` doc forms are excluded from the skip rules so their dedicated doc-comment tokens win.

**Source:** `tools/peg_gen/grammars/logos.peg#L469-L472`, `tools/peg_gen/grammars/logos.peg#L436-L439`

## Writ Data Language

### `lex.writ.punctuation` — Writ punctuation tokens

Writ lexes the fixed punctuators: `LBRACE='{'`, `RBRACE='}'`, `LBRACKET='['`, `RBRACKET=']'`, `LPAREN='('`, `RPAREN=')'`, `LANGLE='<'`, `RANGLE='>'`, `COLON=':'`, `COMMA=','`, `EQUALS='='`, `DOLLAR='$'`.

**Source:** `tools/peg_gen/grammars/writ.peg#L48-L59`

### `lex.writ.bool-null-keywords` — Writ boolean and null literal keywords

The keywords `TRUE='true'`, `FALSE='false'`, `NULL='null'` are reserved literal tokens denoting boolean true/false and the null value.

**Source:** `tools/peg_gen/grammars/writ.peg#L62-L64`

### `lex.writ.string-literal` — Writ string literal

A Writ STRING is a double-quote-delimited sequence `/"([^"\\]|\\.)*"/`: any char except `"` or `\`, or a backslash followed by any single char (escape).

**Source:** `tools/peg_gen/grammars/writ.peg#L65`

### `lex.writ.integer-literal` — Writ integer literal with radix and suffix

A Writ INTEGER is an optional leading `-` followed by a hex (`0x`/`0X`), binary (`0b`/`0B`), octal (`0o`/`0O`), or decimal magnitude, with an optional suffix: `_(u|s)(8|16|32|64)` (sized) or C-style `ull`|`ul`|`ll`|`u`. Regex: `/[-]?(0[xX][0-9a-fA-F]+|0[bB][01]+|0[oO][0-7]+|[0-9]+)(_(u|s)(8|16|32|64)|ull|ul|ll|u)?/`.

**Divergence:** Data-language lexer (Writ), not Logos source; C-style suffixes `ull`/`ul`/`ll`/`u` and `_s32`-style signed suffix differ from Rust integer-literal suffixes.

**Source:** `tools/peg_gen/grammars/writ.peg#L66`

### `lex.writ.float-literal` — Writ float literal

A Writ FLOAT is an optional `-`, optional integer part, a mandatory `.` with a fractional part, optional exponent (`[eE][+-]?digits`), and an optional `f`|`d` type suffix. Regex: `/[-]?[0-9]*\.[0-9]+([eE][+-]?[0-9]+)?[fd]?/`. The fractional part is required (a `.` must be followed by `>=1` digit).

**Divergence:** Requires a fractional digit after `.`; bare-integer floats and leading-dot are governed by this regex (no trailing-dot form); `f`/`d` suffixes.

**Source:** `tools/peg_gen/grammars/writ.peg#L67`

### `lex.writ.ident` — Writ identifier

A Writ IDENT matches `/[a-zA-Z_][a-zA-Z0-9_]*/` and is used both as map keys and as type names.

**Source:** `tools/peg_gen/grammars/writ.peg#L70`

### `lex.writ.skip-whitespace-comments` — Writ skipped whitespace and comments

Between tokens Writ skips: whitespace `/[ \t\n\r]+/`, line comments `/\/\/[^\n]*/`, and non-greedy block comments `/\/\*.*?\*\//`. These produce no tokens.

**Source:** `tools/peg_gen/grammars/writ.peg#L73-L75`

## HRPC Interface-Definition Language

### `lex.hrpc.keywords` — HRPC IDL keywords

The HRPC interface-definition language reserves the keyword tokens: `package`, `import`, `option`, `message`, `enum`, `service`, `rpc`, `returns`, `stream`, `repeated`, `optional`, `required`, `map`, `oneof`.

**Uncertainty:** HRPC is a separate IDL surface (RPC schema), not the Logos source language.

**Source:** `tools/peg_gen/grammars/hrpc.peg#L56-L70`

### `lex.hrpc.punctuation` — HRPC punctuation tokens

HRPC punctuation tokens: `{` `}` `(` `)` `<` `>` `;` `=` `,` `.` (LBRACE RBRACE LPAREN RPAREN LANGLE RANGLE SEMICOLON EQUALS COMMA DOT).

**Source:** `tools/peg_gen/grammars/hrpc.peg#L73-L82`

### `lex.hrpc.string-literal` — HRPC string literal

A STRING literal is a double-quoted sequence matching `/"([^"\\]|\\.)*"/`: any chars except quote/backslash, or a backslash escaping any single char, delimited by `"`.

**Source:** `tools/peg_gen/grammars/hrpc.peg#L85`

### `lex.hrpc.integer-literal` — HRPC integer literal

An INTEGER literal is one or more decimal digits `/[0-9]+/`; no sign, base prefix, or separators.

**Source:** `tools/peg_gen/grammars/hrpc.peg#L86`

### `lex.hrpc.identifier` — HRPC identifier

An IDENT matches `/[a-zA-Z_][a-zA-Z0-9_]*/`: leading ASCII letter or underscore, followed by ASCII letters, digits, or underscores.

**Source:** `tools/peg_gen/grammars/hrpc.peg#L89`

### `lex.hrpc.whitespace-skip` — HRPC whitespace is insignificant

Whitespace `/[ \t\n\r]+/` is skipped between tokens and is not significant.

**Source:** `tools/peg_gen/grammars/hrpc.peg#L92`

### `lex.hrpc.comments` — HRPC comments

HRPC supports line comments `/\/\/[^\n]*/` (to end of line) and block comments `/\/\*.*?\*\//` (non-greedy, may span lines); both are skipped as trivia.

**Source:** `tools/peg_gen/grammars/hrpc.peg#L93-L94`
