# Bug catalog: Lexical

**Group**: 14 — Lexical
**Grammar source**: `%tokens` block in [tools/peg_gen/grammars/logos.peg](../../tools/peg_gen/grammars/logos.peg) — no `%rules` productions; lexer is regex-driven from token regex/literals.
**Reference doc**: [docs/spec/lexical.md](../spec/lexical.md)
**Implementation entry points**:
- Generated lexer in `build/tools/peg_gen/` (regenerated from `logos.peg` `%tokens`)
- Numeric literal parsing — see [feedback_literal_saturation](../../../.claude/projects/-home-victor-devel-logos/memory/feedback_literal_saturation.md)
- Identifier rules — see [feat_unicode_parser](../../../.claude/projects/-home-victor-devel-logos/memory/feat_unicode_parser.md)

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/lexical/`

## Bugs

### B-lx-01: Empty source file → ASSERTION CRASH

**Severity**: P0 hard (compiler crash)
**Status**: fixed-in-M0.2 (loader emits "cannot read" + parser error-recovery; clean exit 1)
**Repro**: `B14/` — empty `main.logos`.
**Observed**: `[LOGOS ASSERTION FAILURE] Requirement: LOGOS-PARSE-001`.
**Expected**: Clean syntax error: "expected `package` declaration".
**Suspected root**: Same family as B-mv-05/06/07/08 — parser asserts when expected token isn't found instead of producing an error node.
**Tags**: `tech-debt:assertion-as-diagnostic`, `oversight:simple`

### B-lx-02: UTF-8 BOM at file start → ASSERTION CRASH

**Severity**: P0 hard (compiler crash)
**Status**: fixed-in-M0.2 (parser error recovery; "parse error in module" + exit 1)
**Repro**: `B15/` — file starts with `0xEF 0xBB 0xBF` (UTF-8 BOM) followed by `package main; fn main() -> i32 { return 0; }`.
**Observed**: `[LOGOS ASSERTION FAILURE] Requirement: LOGOS-PARSE-001`.
**Expected**: Either silently skip the BOM (most lexers do) or reject with "unexpected byte sequence at start of file".
**Suspected root**: Same as B-lx-01 — lexer fails on the first byte not being ASCII or expected token. Logos rejects all non-ASCII at lexer level (per [feat_unicode_parser](../../../.claude/projects/-home-victor-devel-logos/memory/feat_unicode_parser.md)) but does so via assertion.
**Tags**: `tech-debt:assertion-as-diagnostic`, `oversight:simple`

### B-lx-03: Non-ASCII identifier silently rejected with cryptic syntax error — improved

**Severity**: P1 diagnostic
**Status**: improved (2026-05-07) — when a syntax error fires and the offending line contains any non-ASCII byte, the message appends `(note: identifiers must be ASCII; non-ASCII bytes found on this line)`. Doesn't fix the underlying limitation (non-ASCII idents are still rejected) — that's a language-design call tracked by [feat_unicode_parser](../../../.claude/projects/-home-victor-devel-logos/memory/feat_unicode_parser.md) — but the user-visible diagnostic now points at the actual problem.
**Repro**: `B02/` —
```logos
package main;
fn привет() -> i32 { return 0; }
fn main() -> i32 { return 0; }
```
**Observed**: `syntax error near 'fn' at line 2`. The error doesn't say "non-ASCII identifier".
**Expected**: "non-ASCII characters not allowed in identifiers".
**Suspected root**: Lexer rejects the byte but produces a generic syntax error.
**Tags**: `tech-debt:misleading-diagnostic`, `design:incomplete`

### B-lx-04: u64 literal overflow silently saturates (known)

**Severity**: P0 (silent miscompile, known)
**Status**: fixed-in-Sprint2.3 (same overflow check covers all literals)
**Repro**: `B04/` (also B-ex-07, B-he-04)
```logos
let x: u64 = 12345678901234567890u64;  // > u64 max for some values
```
**Observed**: Compiles cleanly; literal silently saturates.
**Expected**: "integer literal out of range for u64".
**Tags**: `tech-debt:literal-saturation-no-error`

### B-lx-05: Unterminated string literal — diagnostic spans rest of file

**Severity**: P1 diagnostic
**Status**: fixed (string lexer bounds unterminated literal to one line; clean "syntax error near \'\"unterminated;\'" diagnostic)
**Repro**: `B07/` —
```logos
fn main() -> i32 {
    let s: &[u8] = "unterminated;
    return 0;
}
```
**Observed**: `syntax error near '"unterminated;\n    return 0;\n}'` — the error message includes the whole rest of the file as the unterminated literal.
**Expected**: Bound the diagnostic to a single line: "unterminated string literal at line N".
**Suspected root**: Lexer's string-literal pattern is greedy; on missing closing `"` it consumes to EOF.
**Tags**: `tech-debt:diagnostic-imprecise`

### B-lx-06: Nested block comments (`/* outer /* inner */ ... */`) don't work

**Severity**: P2 design (vs Rust)
**Status**: fixed — replaced the naive "scan to first `*/`" loop in peg_gen's block-comment skip emitter with a depth-counted state machine: increment on `/*`, decrement on `*/`, continue until depth reaches 0. As a side effect, newlines inside multi-line block comments now correctly increment `line_` (the previous matcher silently lost them). Lock-in: pass test `nested_block_comment` covers two-level and three-level nesting.
**Repro**: `B08/` —
```logos
/* outer /* inner */ still in comment */
fn main() -> i32 { return 0; }
```
**Observed**: `syntax error near 'still' at line 2` — the lexer closes the comment at the FIRST `*/` and treats the rest as code.
**Expected**: Either support nested block comments (Rust-style — useful when commenting out code with comments) or document that nesting is not supported.
**Suspected root**: Block-comment regex in `%tokens` is non-recursive (typical regex limitation); proper nesting requires a counted-state lexer.
**Tags**: `design:incomplete`

### B-lx-07: Character literal `'A'` doesn't parse (lifetime collision)

**Severity**: P2 design (lexer ambiguity)
**Status**: fixed — proper Rust-style `char` type (4-byte Unicode scalar) added together with peek-ahead char-literal lexing. peg_gen's codegen recognises a `'(...)'` regex pattern shape and emits a hand-coded matcher placed BEFORE the LIFETIME matcher; if the closing apostrophe is found it's a CHAR_LIT, otherwise we fall through to LIFETIME (so `'a` still parses as a lifetime). `Kind::Char` is distinct from u32 — `c as u32` / `c as u8` required. Supports `'\\n'`, `'\\t'`, `'\\r'`, `'\\0'`, `'\\\\'`, `'\\''`, `'\\"'`, plus any single ASCII byte. Lock-in: pass test `char_literal` (literals + escapes + casts + lifetime co-existence); fail test `char_implicit_to_u32`.
**Repro**: `B10/` —
```logos
fn main() -> i32 { let c: u8 = 'A'; return c as i32; }
```
**Observed**: `syntax error near 'fn' at line 2`.
**Expected**: Either accept `'A'` as a character literal (Rust-style) or document that Logos has no char-literal syntax. The collision is between `'A'` (char) and `'A` (lifetime).
**Suspected root**: Lexer uses `'` exclusively for `LIFETIME` tokens. `'A'` parses as `LIFETIME 'A` followed by an unexpected `'`.
**Tags**: `tech-debt:lexer-greedy-collision`, `design:incomplete`

## Tag summary

| Tag | Open | Fixed | Total | Bugs |
|---|---|---|---|---|
| `design:incomplete` | 1 | 2 | 3 | B-lx-03, B-lx-06, B-lx-07 |
| `oversight:simple` | 0 | 2 | 2 | B-lx-01, B-lx-02 |
| `tech-debt:assertion-as-diagnostic` | 0 | 2 | 2 | B-lx-01, B-lx-02 |
| `tech-debt:diagnostic-imprecise` | 0 | 1 | 1 | B-lx-05 |
| `tech-debt:lexer-greedy-collision` | 0 | 1 | 1 | B-lx-07 |
| `tech-debt:literal-saturation-no-error` | 0 | 1 | 1 | B-lx-04 |
| `tech-debt:misleading-diagnostic` | 0 | 1 | 1 | B-lx-03 |

**Cluster updates after Lexical**:
- `tech-debt:assertion-as-diagnostic` now at **6 bugs** (B-mv-05/06/07/08, B-lx-01, B-lx-02). Strongest cluster — single architectural fix in parser error-recovery closes all 6 P0 crashes.
- `tech-debt:lexer-greedy-collision` now at **3 bugs** (B-ty-07 `&&`, B-ty-08 `||`, B-lx-07 `'A'`).

## Regression-confirmed (NOT bugs)

- **X01**: non-ASCII in comments accepted (surprising — was thought to be rejected per memory; perhaps a recent fix).
- **X03**: 1000-char identifier works.
- **X05**: hex / oct / bin literals work (0xFF, 0o17, 0b1010).
- **X09**: underscore in integer literal works (`1_000_000`).
- **X11**: line comments work.
- **X12**: typed integer suffixes work (i32/i64/u8 etc).
- **X13**: typed float suffixes work (f32/f64).

## Notes for Phase 3

- The `assertion-as-diagnostic` cluster is the **largest P0 cluster** in the entire bag-hunt: 6 confirmed crashes, all in the parser when input is malformed at file/token-boundary positions. Single arch fix (replace `LOGOS_ASSERT` with error productions in PEG).
- The `lexer-greedy-collision` cluster (3 bugs) suggests the PEG generator could benefit from context-aware tokenization, but that's a heavy lift. Easier path: document the requirements (space `& &mut`, no `||` zero-arg closures, no char literals).
- Numeric literal saturation (B-lx-04) is a one-site fix — just add bounds-check in `parse_int_literal`.
