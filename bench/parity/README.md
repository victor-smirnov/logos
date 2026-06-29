# rustc-parity micro-benchmark oracle

Measures logosc runtime-codegen parity vs rustc on small single-kernel
workloads. Each `<name>.logos` has an algorithm-identical `<name>.rs` twin.

## Method
`run.sh` builds, per bench: **lg-rel** (`logosc -O2 -C overflow-checks=off`),
**rustc** (`rustc -O`) — both generic x86-64 / SSE2 baseline — plus **lg-nat**
and **rc-nat** (each `-C target-cpu=native`, AVX-512 on this Zen5 box). It
measures kernel cost with `perf` via the **difference method**
`count(2R) − count(R)`, which cancels each runtime's fixed process-startup. **R
is auto-scaled per binary** so a run is ~`TARGET_CYCLES` (default 300M) — the
difference method is unreliable at small R (subtracting two near-equal large
counts amplifies noise; an early R=4000 run spuriously showed arith 1.6× slow).
Instructions are deterministic (one sample); cycles take the best of `TRIALS`.

Each kernel mutates its data every rep (or varies with the rep index) so the rep
loop is non-hoistable and survives optimization; the accumulator is consumed at
exit to defeat DCE.

Run: `bash run.sh` (env `TRIALS`, `TARGET_CYCLES`, `BUILD`). Needs `perf`
(paranoid ≤ 1).

Columns: **i-base** = instruction ratio lg-rel/rustc (codegen density, baseline);
**c-base** = cycle ratio at baseline; **c-nat** = cycle ratio at native; **nat-spd**
= logos native speedup vs its own baseline. Ratio ≈ 1.0 = parity; < 1 = logos
faster/denser.

## Baseline (2026-06-29, Zen5 12c/24t; logosc with the SLP + math-intrinsic fixes)

| bench     | i-base | c-base | c-nat | nat-spd | note                                  |
|-----------|--------|--------|-------|---------|---------------------------------------|
| arith     | 1.00×  | 0.99×  | ~     | 2.9×    | vectorizable reduce                   |
| dot       | 0.99×  | 1.00×  | ~     | 3.5×    | dot product                           |
| fib       | 1.00×  | 1.03×  | 1.03× | 0.99×   | recursion / calls                     |
| fnv       | 0.99×  | 0.99×  | 1.00× | 1.00×   | byte-loop hash                        |
| structsum | 1.06×  | 1.18×  | 0.99× | 0.93×   | array-of-structs; unroll-factor gap   |
| nbody     | 1.23×  | 1.10×  | 1.11× | 1.00×   | float / struct math (SLP+sqrt-intr)   |
| fannkuch  | 1.20×  | 1.40×  | 1.44× | —       | int / array-permute; **diffuse IPC gap** |
| spectral  | 1.40×  | 1.00×  | 1.01× | ~       | float div-bound; instr gap masked by 1/d latency |

**logosc is at parity with rustc on the vectorizable + scalar micro-benches**
(instruction density 0.99–1.23×; baseline cycles 0.99–1.19× on all but fannkuch).
`-C target-cpu=native` is a large win on vectorizable loops — **~2.9–3.5× over the
SSE2 baseline** for arith/dot — because logosc otherwise never emits AVX.

The larger benches surfaced two codegen fixes (libm-call→intrinsic; SLP
vectorization was never enabled) — see git log.

**Pattern: instruction density degrades on complex code.** Micro-benches are at
instruction parity (≈1.0×), but the larger programs show logosc emitting ~20–40%
MORE instructions than rustc (nbody 1.23×, fannkuch 1.20×, spectral 1.40×). The
CYCLE impact varies by how the bottleneck masks it: spectral is division-latency-
bound → 1.00× cycles despite 1.40× instructions; nbody ~1.1×; fannkuch 1.40×
cycles (the instruction surplus + a worse IPC, 1.11 vs 1.31, both hurt). Likely
roots (a future codegen-quality dig): weaker CSE / redundant address computation /
register allocation vs rustc's MIR-optimized IR. structsum's baseline 1.18× is a
separate unroll-FACTOR gap (rustc reduces 8-wide vs logosc 4-wide; parity at native).

Caveat: fine native head-to-head cycle ratios (`c-nat` for arith/dot) are noisy
and omitted (`~`) — AVX-512 down-clocks the core, so sub-runs vary run to run;
the robust signals are the deterministic instruction parity, the baseline cycle
parity, and the large native speedup.

`logos-safe` (overflow checks on, not shown) costs ~3× the instructions of
`logos-release` on arithmetic loops (the per-op overflow branch blocks
vectorization) — the measured price of Logos's default safety. `fnv` overflows
by design so its safe build traps (correct); release/rustc both wrap and match.
