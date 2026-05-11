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
| MP-mc-01 | `metacall { … }` at array-length / enum-discriminant / type position | Deferred — grammar admits metacall only at expr-position and item-position; needs new alts in `arr_type`, `arr_fill_lit`, and `variant_def` (and likely a `meta_int_expr` sub-production that wraps `KW_METACALL block`). After grammar, mlir-gen / sema must consume the spliced literal in those slots. ~3-5 days. | `[T; metacall { 2 * 4 }]` ⇒ "syntax error near ';'"; `enum E { V = metacall { N } }` ⇒ "syntax error near 'enum'". | C6-cc-02 / C6-cc-03 / K10-co-01 share this root cause; closing MP-mc-01 unblocks their tests. | `[i64; metacall { 8 }]` |
