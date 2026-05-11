# Consts / typeck / inference gaps surfaced by Track 3 imports

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| K10-co-01 | Arithmetic expression at array-length position | Divergence — Logos has no const-eval at type / array-length position; use `metacall` to compute at compile time, then splice the literal. | `[T; 2*4]` rejected — only a single integer literal or generic `const N: i64` is accepted. | `arithmetic-expr-in-array-len` (literal inlined) | `[0i64; 2i64 * 4i64]` ⇒ "syntax error near '2i64'" |
| K10-co-02 | `null` as identifier (fn name / etc.) | ✅ Closed (`77316df`, Sprint 1.1) | `null` was KW_NULL in logos.peg; now admitted as identifier in fn/field/call positions. | `unify-return-ty` (renamed `null` → `null_p`) | `fn null<T>() -> *const T { … }` |
| K10-co-03 | `mem::transmute` | Divergence — Logos has explicit `as`/deref equivalents; no transmute planned. | Multiple typeck tests rely on transmute. | `unify-return-ty` (transmute → cast) | `mem::transmute(0_usize)` |
