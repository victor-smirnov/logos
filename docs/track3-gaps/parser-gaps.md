# Parser gaps surfaced by Track 3 imports

Each row is a real gap surfaced by porting a rustc tests/ui test;
the entry stays here until the gap is closed. When closed, drop the
row and update the import row in RUSTC-PROVENANCE.md.

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| P3-pg-01 | Type position: contiguous `&&T` / `&&&T` greedy `&`-stacks | ✅ Closed (2026-05-11) — `ref_pointee` now recurses through `ref_type`, so arbitrary-depth `&&&&T` and whitespace-tolerant `& & & & T` both parse at type position. Pattern-form (`let &&_ = …`) still depends on C6-cc-04 (Divergence). | Lexer emits `&&` as a single AND-AND token; type-grammar didn't unpick stacks deeper than 2. | `reference-whitespace-parsing` (un-trimmed to depth 5) | `let r: &&&&&usize = &…;` works |
| P3-pg-02 | Type position: whitespace-tolerant `&` stacking (`& & T`) | ✅ Closed (`2bedcbb`, Sprint 2) — `&&v` parsing fix accompanied the coercion sweep. | Even if `&&T` works, Logos parser did not tolerate whitespace between `&` tokens at type position the way Rust does. | same | `let _: & & i64 = ...;` — "syntax error near '&'" |
| P3-pg-03 | Unicode (non-language gap, deferred) | ✅ Closed entry-side (`2bedcbb`) — identifier/whitespace unicode out of scope; string `\u{...}` works. | (a) Unicode identifiers (UAX #31); (b) Unicode whitespace in source. String-literal `\u{...}` escape sequences work via lexer. | rustc unicode-escape-sequences.rs / utf8_idents-rpass.rs / parser-unicode-whitespace.rs | n/a — tests not imported until lexer is unicode-aware |

## Closing rules

- Fix the parser, add a focused regression to `tests/logos/pass/parser_<name>.logos`, then re-port the original rustc test (un-trim).
- Or: justify why Logos diverges (e.g. ambiguity argument), document in `docs/divergences/`, and leave the test trimmed.
