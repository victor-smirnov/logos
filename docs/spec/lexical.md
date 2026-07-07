# Lexical Structure

Scope: token-level rules (`domain = lex`) for the Logos source language plus the two auxiliary surfaces it ships with — the **Writ** data language and the **HRPC** interface-definition language. Source layers: PEG grammar token definitions (`tools/peg_gen_cpp/grammars/*.peg`) and the hand-rolled compiler literal decoders/validators (`src/compiler/*`). Each `###` id is the permanent linkable address of one rule.

## Keywords

### `lex.keyword.reserved-set` — Reserved keyword set

The following are reserved keywords matched as distinct tokens and unavailable as ordinary identifiers: `continue`, `quote_item`, `quote_expr`, `quote_ty`, `template`, `package`, `instantiate`, `eidos`, `genos`, `auto`, `metacall`, `resource`, `static`, `return`, `extern`, `struct`, `schema`, `union`, `match`, `while`, `break`, `false`, `trait`, `const`, `type`, `impl`, `enum`, `loop`, `else`, `true`, `for`, `use`, `mut`, `let`, `dyn`, `tagged`, `pub`, `new`, `fn`, `if`, `in`, `as`, `where`, `unsafe`, `move`, `typeof`, `offset_of`, `ref`, `null`, `async`, `await`.

**Divergence.** Adds Logos-specific keywords absent in Rust: `quote_item`/`quote_expr`/`quote_ty`/`template`/`package`/`instantiate`/`eidos`/`genos`/`auto`/`metacall`/`resource`/`schema`/`tagged`/`new`/`typeof`/`offset_of`/`null`; lacks Rust keywords (`mod`, `pub(crate)`, `crate`, `self`, `Self`, `fn`-async forms, etc.) handled elsewhere.

Evidence: `tools/peg_gen_cpp/grammars/logos.peg#L333-L381`

### `lex.keyword.async-await-reserved` — async/await reserved but unused

`async` and `await` are tokenized as keywords but reserved with no grammar use (kept for a future stackless-coroutine path on wasm32/64).

**Note (uncertainty).** Reserved-without-use status is stated in the source comment; actual rejection behavior in the surface grammar is defined in the `%rules` unit. Testability: untestable.

Evidence: `tools/peg_gen_cpp/grammars/logos.peg#L383-L386`

## Identifiers and lifetimes

### `lex.token.ident` — Identifier token

`IDENT = [a-zA-Z_][a-zA-Z0-9_]*` — ASCII letter/underscore followed by ASCII alphanumerics/underscores.

**Divergence.** Identifiers are ASCII-only; Rust permits Unicode (XID) identifiers and raw identifiers `r#name`.

Evidence: `tools/peg_gen_cpp/grammars/logos.peg#L473`

### `lex.token.lifetime` — Lifetime token

`LIFETIME = '[a-z_][a-z0-9_]*` — an apostrophe followed by a lowercase-initiated identifier (no closing apostrophe).

**Divergence.** Lifetime names must start with a lowercase letter or `_`; uppercase-initial lifetimes (allowed in Rust) are not recognized.

Evidence: `tools/peg_gen_cpp/grammars/logos.peg#L472`

## Identifier encoding

### `lex.ident.ascii-only` — Identifiers are ASCII-only

Identifiers consist of ASCII bytes only; a source line containing a non-ASCII (high-bit, `>= 0x80`) byte at the point of a syntax error is diagnosed as an identifier encoding error, since non-ASCII bytes cannot form a valid identifier token.

**Divergence.** Diverges from Rust, which accepts Unicode (XID) identifiers.

Evidence: `src/compiler/module_loader.cpp#L1361-L1377`

## Punctuation and operators

### `lex.punct.symbols` — Punctuation and operator tokens

Single- and multi-character delimiter/operator tokens are recognized, including:

```logos
{ } [ ] ( ) : :: , ; -> => . .. ..= ... * & && | || ! ? = == != < <= > >= << >> <<= >>= + - / % += -= *= /= %= &= |= ^= ^ @ # $
```

Multi-character operators are matched in longest-match-first order (e.g. `<<=` before `<<`, `..=` and `...` before `..`, `&&` before `&`).

Evidence: `tools/peg_gen_cpp/grammars/logos.peg#L389-L440`

## Numeric literals

### `lex.literal.integer` — Integer literal syntax and width suffixes

An integer literal matches an optional leading `-`, then a decimal (`[0-9][0-9_]*`), hex (`0x[0-9a-fA-F_]+`), binary (`0b[01_]+`), or octal (`0o[0-7_]+`) magnitude, with `_` digit separators, optionally suffixed by a width tag drawn from `{i8, i16, i24, i32, i56, i64, i128, u8, u16, u24, u32, u56, u64, u128, usize, isize}`.

**Divergence.** A11: width set includes Writ-fabric widths `i24`/`u24`/`i56`/`u56` beyond Rust's `{8,16,32,64,128}`+size. Also: a leading `-` is part of the integer token itself (Rust treats `-` as a separate unary operator).

Evidence: `tools/peg_gen_cpp/grammars/logos.peg#L463`

### `lex.literal.float` — Float literal syntax

A float literal matches an optional leading `-`, an integer part, a mandatory `.` with a fractional part (both `[0-9][0-9_]*`), an optional exponent `([eE][+-]?[0-9][0-9_]*)`, and an optional suffix `f32` or `f64`. `_` digit separators are permitted.

**Divergence.** A leading `-` is part of the float token (Rust parses `-` as separate unary minus). A fractional part is mandatory (no `1.` form); float-width suffix set is `{f32, f64}`.

Evidence: `tools/peg_gen_cpp/grammars/logos.peg#L462`

### `lex.literal.int-overflow-i64` — Unsuffixed integer literal must fit i64/u64 (64-bit) magnitude

An integer literal's magnitude is rejected if it exceeds 64-bit representable range: an unsigned magnitude must fit `u64`; a negated literal's magnitude must not exceed `2^63` (`INT64_MIN` is representable, anything past overflows). Literals are parsed in base 10, or `0x`/`0X` hex, `0b`/`0B` binary, `0o`/`0O` octal, with `_` digit separators ignored; parsing stops at the first character that is not a valid digit for the base (the type suffix).

**Divergence.** Rust default integer literal type is `i32`; here the raw overflow bound is 64-bit (`i64`/`u64`), with per-suffix bounds layered at the call site.

Evidence: `src/compiler/sema_impl.hpp#L4577-L4602`

### `lex.literal.int-radix-and-separators` — Integer literal radix prefixes and digit separators

Integer literals support an optional leading `-`, the radix prefixes `0x`/`0X` (hex), `0b`/`0B` (binary), `0o`/`0O` (octal), default decimal, and `_` digit separators which are ignored in the value. Digits outside the active base terminate the numeric body (beginning the optional suffix).

Evidence: `src/compiler/sema_impl.hpp#L4604-L4654`, `src/compiler/sema_impl.hpp#L4659-L4678`

### `lex.literal.int128-magnitude` — 128-bit integer literal magnitude

An integer literal targeting `i128`/`u128` is accumulated as a 128-bit unsigned magnitude (sign applied by the caller) and is rejected only if its magnitude exceeds 128 bits; 64-bit-overflowing values round-trip intact.

**Divergence.** Logos provides `i128`/`u128` literals; magnitude bound is 128 bits rather than 64.

Evidence: `src/compiler/sema_impl.hpp#L4659-L4703`

### `lex.literal.digit-separator-placement` — Digit-separator `_` placement validity

A `_` digit separator is valid only between two digits: it may not be the first or last character of a digit group, and consecutive `__` separators are forbidden. A digit group containing any character not accepted by its radix predicate is invalid.

**Note (uncertainty).** Rust permits leading/trailing and repeated underscores in some positions; exact conformance not confirmed from this unit.

Evidence: `src/compiler/sema_impl.hpp#L4705-L4720`

### `lex.literal.int-format` — Well-formed integer literal grammar

A valid integer literal = optional `-`, then either a radix-prefixed body (`0x` hex / `0b` binary / `0o` octal) or a decimal body, with valid digit-separator placement, optionally followed by exactly one recognised integer suffix from `{usize, isize, i128, i64, i56, i32, i24, i16, i8, u128, u64, u56, u32, u24, u16, u8}`.

Related: `lex.literal.int-suffix`, `lex.literal.digit-separator-placement`.

Evidence: `src/compiler/sema_impl.hpp#L4722-L4767`

### `lex.literal.float-format` — Well-formed float literal grammar

A valid float literal = optional `-`, then a mantissa with either (a) a decimal point `.` with valid digit groups before and after, or (b) no decimal point but a mandatory exponent (`e`/`E` with optional `+`/`-` and digits) — a bare integer with no point and no exponent is NOT a float literal — optionally with an `e`/`E` exponent in case (a), and optionally followed by exactly one float suffix `f32` or `f64`.

Evidence: `src/compiler/sema_impl.hpp#L4769-L4808`

### `lex.literal.int-suffix` — Integer literal type suffix

An integer literal may carry an explicit type suffix selecting its kind: `i8`/`i16`/`i24`/`i32`/`i56`/`i64`/`i128`, `u8`/`u16`/`u24`/`u32`/`u56`/`u64`/`u128`, `usize`, `isize`; the suffix follows the (optionally radix-prefixed) digit body. Absence of a recognised suffix yields the unsuffixed literal type.

**Divergence.** Includes Logos-specific suffixes `i24`/`u24`/`i56`/`u56` absent in Rust.

Related: `type.integer.kind-set`.

Evidence: `src/compiler/sema_impl.hpp#L4812-L4841`

### `lex.literal.float-suffix` — Float literal type suffix

A float literal may carry the suffix `f32` or `f64` selecting `F32`/`F64`; absence yields the unsuffixed float-literal type.

Evidence: `src/compiler/sema_impl.hpp#L4843-L4847`

## String, raw-string, and byte-string literals

### `lex.literal.string` — String, raw-string, and byte-string literals

`STRING = "([^"\\]|\\.)*"` (escapes via backslash). The bare `RAW_STRING` grammar token is `r"[^"]*"`, but the hand-rolled literal decoder also accepts hash-delimited raw strings `r#"..."#` / `r##"..."##` (see `lex.litstr.raw-hash-count`), which CAN contain `"`. `BYTE_STRING = b"([^"\\]|\\.)*"`.

**Note (uncertainty).** The PEG token regex under-specifies raw strings vs the actual decoder; behavior corrected against the compiler (raw strings are Rust-like, hash-delimited forms supported), so this is NOT a divergence.

Related: `lex.litstr.raw-hash-count`.

Evidence: `tools/peg_gen_cpp/grammars/logos.peg#L459-L461`

### `lex.litstr.raw-hash-count` — Raw string `r#"..."#` delimiter stripping

A raw string literal begins with `r` followed by N `#` characters (N `>= 0`) then `"`; its content is the bytes between the opening `"` and the matching closing `"` followed by the same N `#`. Escape sequences are NOT processed inside a raw string.

Evidence: `src/compiler/mlir_gen_expr.cpp#L317-L336`

### `lex.litstr.escape-sequences` — String literal escape decoding

In a non-raw string literal, `\` introduces an escape: `\n`=LF, `\t`=TAB, `\r`=CR, `\\`=backslash, `\0`=NUL, `\"`=quote, `\'`=apostrophe. `\xNN` decodes exactly two hex digits to one byte. `\u{H..}` decodes the hex codepoint and emits its UTF-8 encoding (1-4 bytes). A malformed or unknown escape is left literal (backslash retained).

Evidence: `src/compiler/mlir_gen_expr.cpp#L338-L402`

### `lex.str.escape-set` — String literal escape set

String literals support the escape sequences `\\`, `\"`, `\n`, `\r`, `\t`, `\0`, and `\xHH` (two hex digits) for control bytes `< 0x20`; all other bytes appear verbatim.

**Note.** This is the emit/render side (`sema_render`). It agrees with the decode-side escape set of `lex.litstr.escape-sequences`, but the render set intentionally omits `\'` and `\u{...}` (a `'` needs no escaping inside a string; codepoints re-emit as raw UTF-8 bytes). Not a conflict — different directions (encode vs decode).

Evidence: `src/compiler/sema_render.cpp#L41-L65`

## Character literals

### `lex.literal.char` — Char literal

A char literal `CHAR_LIT = '(\\.|[^'\\])'` is a single `\`-escape or one Unicode codepoint between apostrophes; it is matched BEFORE `LIFETIME` so `'A'` (with closing apostrophe) wins over a lifetime read. The body decodes to the scalar codepoint value.

Evidence: `tools/peg_gen_cpp/grammars/logos.peg#L464-L471`

### `lex.literal.char-before-lifetime` — Char-vs-lifetime disambiguation

When the source could begin either a char literal or a lifetime, the lexer prefers the char literal: `'a'` lexes as a char, `'a` (no closing apostrophe) lexes as a lifetime.

Evidence: `tools/peg_gen_cpp/grammars/logos.peg#L464-L472`

### `lex.char.utf8-scalar-decode` — Char literal multi-byte UTF-8 decode

A char literal body whose first byte `>= 0x80` is decoded as a multi-byte UTF-8 sequence: lead byte `110xxxxx` => 2 bytes, `1110xxxx` => 3, `11110xxx` => 4; any other lead byte is an error (`invalid UTF-8`). Fewer than the required continuation bytes is an error (`truncated UTF-8`). The scalar value is the assembled code point. Testability: transitive.

Evidence: `src/compiler/sema_stmt.cpp#L4401-L4412`

## Comments

### `lex.comment.doc-tokens` — Doc comments emitted as tokens

Doc comments are lexed as real tokens (not skipped): `DOC_LINE = ///[^\n]*` (outer line), `DOC_INNER = //![^\n]*` (inner module-level line), `DOC_BLOCK = /**...*/` (outer block), `DOC_BLOCK_INNER = /*!...*/` (inner block). These attach as documentation to the following item / enclosing module. Testability: transitive.

Evidence: `tools/peg_gen_cpp/grammars/logos.peg#L442-L456`

### `lex.comment.skip` — Whitespace and ordinary comments skipped

Inter-token skip whitespace is `[ \t\n\r]+`; ordinary line comments `//[^\n]*` and block comments `/*...*/` are skipped. The `///`, `//!`, `/**`, `/*!` doc forms are excluded from the skip rules so their dedicated doc-comment tokens win.

Evidence: `tools/peg_gen_cpp/grammars/logos.peg#L476-L478`, `tools/peg_gen_cpp/grammars/logos.peg#L442-L445`

## Writ data-language tokens

### `lex.writ.punctuation` — Writ punctuation tokens

Writ lexes the fixed punctuators: `LBRACE='{'`, `RBRACE='}'`, `LBRACKET='['`, `RBRACKET=']'`, `LPAREN='('`, `RPAREN=')'`, `LANGLE='<'`, `RANGLE='>'`, `COLON=':'`, `COMMA=','`, `EQUALS='='`, `DOLLAR='$'`. Testability: untestable.

Evidence: `tools/peg_gen_cpp/grammars/writ.peg#L48-L59`

### `lex.writ.bool-null-keywords` — Writ boolean and null literal keywords

The keywords `TRUE='true'`, `FALSE='false'`, `NULL='null'` are reserved literal tokens denoting boolean true/false and the null value. Testability: untestable.

Evidence: `tools/peg_gen_cpp/grammars/writ.peg#L62-L64`

### `lex.writ.string-literal` — Writ string literal

A Writ `STRING` is a double-quote-delimited sequence `"([^"\\]|\\.)*"`: any char except `"` or `\`, or a backslash followed by any single char (escape). Testability: untestable.

Evidence: `tools/peg_gen_cpp/grammars/writ.peg#L65`

### `lex.writ.integer-literal` — Writ integer literal with radix and suffix

A Writ `INTEGER` is an optional leading `-` followed by a hex (`0x`/`0X`), binary (`0b`/`0B`), octal (`0o`/`0O`), or decimal magnitude, with an optional suffix: `_(u|s)(8|16|32|64)` (sized) or C-style `ull`|`ul`|`ll`|`u`. Regex: `[-]?(0[xX][0-9a-fA-F]+|0[bB][01]+|0[oO][0-7]+|[0-9]+)(_(u|s)(8|16|32|64)|ull|ul|ll|u)?`.

**Divergence.** Data-language lexer (Writ), not Logos source; C-style suffixes `ull`/`ul`/`ll`/`u` and `_s32`-style signed suffix differ from Rust integer-literal suffixes. Testability: untestable.

Evidence: `tools/peg_gen_cpp/grammars/writ.peg#L66`

### `lex.writ.float-literal` — Writ float literal

A Writ `FLOAT` is an optional `-`, optional integer part, a mandatory `.` with a fractional part, optional exponent (`[eE][+-]?digits`), and an optional `f`|`d` type suffix. Regex: `[-]?[0-9]*\.[0-9]+([eE][+-]?[0-9]+)?[fd]?`. The fractional part is required (a `.` must be followed by `>=1` digit).

**Divergence.** Requires a fractional digit after `.`; bare-integer floats and leading-dot are governed by this regex (no trailing-dot form); `f`/`d` suffixes.

Evidence: `tools/peg_gen_cpp/grammars/writ.peg#L67`

### `lex.writ.ident` — Writ identifier

A Writ `IDENT` matches `[a-zA-Z_][a-zA-Z0-9_]*` and is used both as map keys and as type names. Testability: untestable.

Evidence: `tools/peg_gen_cpp/grammars/writ.peg#L70`

### `lex.writ.skip-whitespace-comments` — Writ skipped whitespace and comments

Between tokens Writ skips: whitespace `[ \t\n\r]+`, line comments `//[^\n]*`, and non-greedy block comments `/*.*?*/`. These produce no tokens. Testability: transitive.

Evidence: `tools/peg_gen_cpp/grammars/writ.peg#L73-L75`

## HRPC IDL tokens

### `lex.hrpc.keywords` — HRPC IDL keywords

The HRPC interface-definition language reserves the keyword tokens: `package`, `import`, `option`, `message`, `enum`, `service`, `rpc`, `returns`, `stream`, `repeated`, `optional`, `required`, `map`, `oneof`.

**Note (uncertainty).** HRPC is a separate IDL surface (RPC schema), not the Logos source language. Testability: untestable.

Evidence: `tools/peg_gen_cpp/grammars/hrpc.peg#L56-L70`

### `lex.hrpc.punctuation` — HRPC punctuation tokens

HRPC punctuation tokens: `{` `}` `(` `)` `<` `>` `;` `=` `,` `.` (`LBRACE` `RBRACE` `LPAREN` `RPAREN` `LANGLE` `RANGLE` `SEMICOLON` `EQUALS` `COMMA` `DOT`). Testability: untestable.

Evidence: `tools/peg_gen_cpp/grammars/hrpc.peg#L73-L82`

### `lex.hrpc.string-literal` — HRPC string literal

A `STRING` literal is a double-quoted sequence matching `"([^"\\]|\\.)*"`: any chars except quote/backslash, or a backslash escaping any single char, delimited by `"`. Testability: untestable.

Evidence: `tools/peg_gen_cpp/grammars/hrpc.peg#L85`

### `lex.hrpc.integer-literal` — HRPC integer literal

An `INTEGER` literal is one or more decimal digits `[0-9]+`; no sign, base prefix, or separators. Testability: untestable.

Evidence: `tools/peg_gen_cpp/grammars/hrpc.peg#L86`

### `lex.hrpc.identifier` — HRPC identifier

An `IDENT` matches `[a-zA-Z_][a-zA-Z0-9_]*`: leading ASCII letter or underscore, followed by ASCII letters, digits, or underscores. Testability: untestable.

Evidence: `tools/peg_gen_cpp/grammars/hrpc.peg#L89`

### `lex.hrpc.whitespace-skip` — HRPC whitespace is insignificant

Whitespace `[ \t\n\r]+` is skipped between tokens and is not significant. Testability: untestable.

Evidence: `tools/peg_gen_cpp/grammars/hrpc.peg#L92`

### `lex.hrpc.comments` — HRPC comments

HRPC supports line comments `//[^\n]*` (to end of line) and block comments `/*.*?*/` (non-greedy, may span lines); both are skipped as trivia. Testability: untestable.

Evidence: `tools/peg_gen_cpp/grammars/hrpc.peg#L93-L94`
