# rustc-parity micro-benchmark oracle

Measures logosc runtime-codegen parity vs rustc on small, single-kernel
workloads. Each `<name>.logos` has an algorithm-identical `<name>.rs` twin.

## Method
`run.sh` builds three binaries per bench — **logos-safe** (`-O2`, overflow checks
on), **logos-release** (`-O2 -C overflow-checks=off`), **rustc** (`-O`) — and
measures kernel cost with `perf stat` via the **difference method**:
`count(2R) − count(R)` isolates exactly R kernel iterations, cancelling each
runtime's fixed process-startup instructions/cycles. Instructions are
deterministic (one sample); cycles take the best of `TRIALS`.

- **instructions/rep** = codegen density (ratio ≈ 1.0 ⇒ codegen at parity)
- **cycles/rep** = real speed (vectorization ⇒ fewer cycles / higher IPC)

Each kernel mutates its data every rep (or varies with the rep index) so the rep
loop is non-hoistable and survives optimization; the accumulator is consumed at
exit to defeat DCE.

Run: `bash run.sh` (env: `R`, `TRIALS`, `BUILD`). Needs `perf` (paranoid ≤ 1).

## Baseline (2026-06-29, 12c/24t box, R=4000)
logos-release vs rustc -O:

| bench     | instr ratio | cycles ratio | note                                   |
|-----------|-------------|--------------|----------------------------------------|
| arith     | 1.00×       | 1.01×        | vectorizable reduce                    |
| dot       | 1.00×       | 0.98×        | dot product                            |
| fib       | 0.99×       | 1.03×        | recursion / call overhead              |
| fnv       | 1.00×       | 1.00×        | byte-loop hash (safe-mode traps: wraps)|
| structsum | 1.06×       | 1.21×        | array-of-structs field via `&T` — gap  |

**logosc -O2 (release) is at parity with rustc -O** across the corpus. The lone
outlier is `structsum` (+6% instr / +21% cycles) — the next lever to chase.

`logos-safe` (overflow checks on) costs ~3× the instructions of `logos-release`
on arithmetic loops (the per-op overflow branch blocks vectorization) — the
measured price of Logos's default safety. `fnv` overflows by design, so its
safe build traps (correctly); release/rustc both wrap and match.
