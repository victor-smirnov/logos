# Consts / typeck / inference gaps surfaced by Track 3 imports

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| K10-co-01 | Arithmetic expression at array-length position | ✅ Closed (2026-05-11) — via MP-mc-01 partial: `arr_fill_lit` SIZE now admits `metacall { … }`, ctfe-evaluated to integer at sema. | `[T; 2*4]` rejected at parser; the Logos idiom is `[0i64; metacall { 2 * 4 }]`. | `arithmetic-expr-in-array-len` (un-trimmed via metacall) | `[0i64; metacall { 2i64 * 4i64 }]` |
| K10-co-02 | `null` as identifier (fn name / etc.) | ✅ Closed (`77316df`, Sprint 1.1) | `null` was KW_NULL in logos.peg; now admitted as identifier in fn/field/call positions. | `unify-return-ty` (renamed `null` → `null_p`) | `fn null<T>() -> *const T { … }` |
| K10-co-03 | `mem::transmute` | Divergence — Logos has explicit `as`/deref equivalents; no transmute planned. | Multiple typeck tests rely on transmute. | `unify-return-ty` (transmute → cast) | `mem::transmute(0_usize)` |
| K10-co-04 | Divergent (`return`-only / `panic`-only) expression usable at non-void type position | Open — Logos has no `!`/never-type inference; an expression whose tail is `return` (or `panic`) is typed `void`, so `({ return n },)` fails as "expected (i32), got (void)" inside a tuple literal. Workaround at the source: append a trailing dummy value (`{ return n; 0i32 }`) so the block's type matches the surrounding context. Real fix is bottom-type propagation through expression positions. | A block whose tail diverges keeps `void` type instead of unifying with the required type. | `expr/return-in-block-tuple` (un-import blocked), `expr/if-panic-all` (worked around) | `let _: (i32,) = ({ return 0 },);` ⇒ "expected (i32), got (void)" |
