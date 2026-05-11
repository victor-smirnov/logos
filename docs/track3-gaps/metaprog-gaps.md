# Metaprog gaps surfaced by Track 3 imports

Logos uses `metacall { … }` (JIT-evaluated Logos splice) as the
compile-time-computation channel that Rust's const-eval / const-fn /
const-as-discriminant cover. Whenever a rustc import test depends on
const-eval-at-position-X, the Logos idiom is `metacall { … }` at
position X — and any position X where `metacall` is not admitted by
the grammar is itself a gap in metacall coverage, not a Divergence.

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date);
`Deferred` — real gap, needs its own slice.

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| MP-mc-01 | `metacall { … }` at non-expression positions | Partial (2026-05-11) — `arr_fill_lit` SIZE position closed: `[v; metacall { <expr> }]` now lowers via ctfe::eval_expr. Sema branch in `lower_arr_fill_lit`. Remaining: `arr_type` SIZE (`[T; metacall { N }]`) and `variant_def` discriminant (`V = metacall { N }`) blocked by **MP-mc-02 (peg_gen bug)** — adding more than 2 `KW_METACALL block` references / a new alt to `arr_type` segfaults peg_gen. | `[T; metacall { N }]` and `enum E { V = metacall { N } }` still parse-fail. | K10-co-01 closed via this; C6-cc-02 / C6-cc-03 still Deferred pending MP-mc-02. | `[0i64; metacall { 2 * 4 }]` works |
| MP-mc-02 | peg_gen segfaults when `arr_type` or `variant_def` get a `KW_METACALL block` alt added | Deferred — bug in our own PEG generator (`tools/peg_gen/peg_gen`). Reproducer: add a second production with `KW_METACALL block` alt; generator seg-faults during AST emit. Likely off-by-one / fixed-size buffer in alt-handling for already-popular tokens. ~half-day with valgrind. Blocking MP-mc-01 finish (and through it C6-cc-02 / C6-cc-03). | n/a | (own tool) | n/a |
