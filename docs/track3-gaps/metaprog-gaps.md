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
| MP-mc-01 | `metacall { … }` at non-expression positions | ✅ Closed (2026-05-11) — three positions wired through ctfe::eval_expr: `arr_type` SIZE, `arr_fill_lit` SIZE, `variant_def` discriminant. Block tail expression evaluated; integer result becomes the literal value. Logos's replacement for Rust's const-eval at these positions. | n/a | C6-cc-02 / C6-cc-03 / K10-co-01 all closed via this. | `[i64; metacall { N }]`, `[v; metacall { N }]`, `enum E { V = metacall { N } }` |
| MP-mc-02 | peg_gen segfaults on 4+ action-map fields | ✅ Closed (2026-05-11) — root cause was in `src/hermes/view.cpp:make_object_map` passing `log2_buckets` straight as `initial_capacity` (arg-name vs semantics mismatch). So default cap=3→rounded-to-4 instead of 1<<3=8; adding a 4th field triggered rehash 4→8 which has a separate bug in ObjectMap::rehash. Fix: convert log2_buckets via `1u << log2_buckets`. Capacity-4 rehash bug still present in object_map.hpp but no longer hit on normal paths. Worth a follow-up valgrind pass on rehash to harden it. | Adding a `KW_METACALL block` alt to certain productions (let_stmt, arr_type, variant_def) seg-faulted peg_gen. | n/a (own tool) | now stable |
