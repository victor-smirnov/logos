# Parser gaps surfaced by Track 3 imports

Each row is a real gap surfaced by porting a rustc tests/ui test;
the entry stays here until the gap is closed. When closed, drop the
row and update the import row in RUSTC-PROVENANCE.md.

| ID | Surface | Gap | Surfaced by | Repro |
|---|---|---|---|---|
| P3-pg-01 | Type position: contiguous `&&T` / `&&&T` greedy `&`-stacks | Lexer emits `&&` as a single AND-AND token; type-grammar doesn't unpick it. Memory `feat_sprint62_63.md` claims B-ty-07 fixed `&&T` — appears narrow (works in some positions, not in let-binding type ascription). | `tests/imported/pass/parser/reference-whitespace-parsing.logos` (trimmed) | `let _: &&i64 = &&1;` — "syntax error near '='" |
| P3-pg-02 | Type position: whitespace-tolerant `&` stacking (`& & T`) | Even if `&&T` works, Logos parser does not tolerate whitespace between `&` tokens at type position the way Rust does. | same | `let _: & & i64 = ...;` — "syntax error near '&'" |
| P3-pg-03 | Unicode (non-language gap, deferred) | (a) Unicode identifiers (UAX #31) — `utf8_idents-rpass.rs`; (b) Unicode whitespace in source — `parser-unicode-whitespace.rs`. String-literal `\u{...}` escape sequences work via lexer escape decoding, but generating actual UTF-8 in source bodies isn't supported. | rustc unicode-escape-sequences.rs / utf8_idents-rpass.rs / parser-unicode-whitespace.rs | n/a — tests not imported until lexer is unicode-aware |

## Closing rules

- Fix the parser, add a focused regression to `tests/logos/pass/parser_<name>.logos`, then re-port the original rustc test (un-trim).
- Or: justify why Logos diverges (e.g. ambiguity argument), document in `docs/divergences/`, and leave the test trimmed.
