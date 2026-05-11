# Consts / typeck / inference gaps surfaced by Track 3 imports

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| K10-co-01 | Arithmetic expression at array-length position | Deferred — same MP-mc-01 metacall gap as C6-cc-02. `[T; metacall { 2 * 4 }]` should be the Logos idiom, but metacall isn't admitted there. Until metacall position is wired, the rustc test can't be ported. | `[T; 2*4]` rejected. | `arithmetic-expr-in-array-len` (literal inlined) | `[0i64; 2i64 * 4i64]` |
| K10-co-02 | `null` as identifier (fn name / etc.) | ✅ Closed (`77316df`, Sprint 1.1) | `null` was KW_NULL in logos.peg; now admitted as identifier in fn/field/call positions. | `unify-return-ty` (renamed `null` → `null_p`) | `fn null<T>() -> *const T { … }` |
| K10-co-03 | `mem::transmute` | Divergence — Logos has explicit `as`/deref equivalents; no transmute planned. | Multiple typeck tests rely on transmute. | `unify-return-ty` (transmute → cast) | `mem::transmute(0_usize)` |
