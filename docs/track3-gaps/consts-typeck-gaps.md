# Consts / typeck / inference gaps surfaced by Track 3 imports

| ID | Surface | Gap | Surfaced by | Repro |
|---|---|---|---|---|
| K10-co-01 | Arithmetic expression at array-length position | `[T; 2*4]` rejected — only a single integer literal or generic `const N: i64` is accepted. Top-level `const N: i64 = 2*4;` parses but `[T; N]` not (S8-en-01 / C6-cc-02 family — top-level `const` not yet usable as array length). | `arithmetic-expr-in-array-len` (literal inlined) | `[0i64; 2i64 * 4i64]` ⇒ "syntax error near '2i64'" |
| K10-co-02 | `null` as identifier (fn name / etc.) | `null` is KW_NULL in logos.peg; rejected as identifier. Renaming required. Cross-link to S8-st-01 (same keyword-shadows-ident family). | `unify-return-ty` (renamed `null` → `null_p`) | `fn null<T>() -> *const T { … }` |
| K10-co-03 | `mem::transmute` | Logos has no `mem::transmute`; explicit `as` cast or `*` deref of a raw pointer is the equivalent. Multiple typeck tests rely on transmute. | `unify-return-ty` (transmute → cast) | `mem::transmute(0_usize)` |
