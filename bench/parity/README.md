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

## Baseline (2026-06-29, Zen5 12c/24t)

| bench     | i-base | c-base | c-nat | nat-spd |
|-----------|--------|--------|-------|---------|
| arith     | 1.00×  | 0.99×  | ~     | 2.9×    |
| dot       | 0.99×  | 1.00×  | ~     | 3.5×    |
| fib       | 1.00×  | 1.03×  | 1.03× | 0.99×   |
| fnv       | 0.99×  | 0.99×  | 1.00× | 1.00×   |
| structsum | 1.06×  | 1.19×  | 0.99× | 0.93×   |

**logosc is at parity with rustc.** Instruction density is parity across the
board (0.99–1.06×); baseline cycles are parity (0.99–1.19×, structsum the lone
mild outlier — closes to 0.99× at native). `-C target-cpu=native` is a large win
on vectorizable loops — **~2.9–3.5× over the SSE2 baseline** for arith/dot,
neutral on serial code (fib/fnv) — because logosc otherwise never emits AVX.

Caveat: fine native head-to-head cycle ratios (`c-nat` for arith/dot) are noisy
and omitted (`~`) — AVX-512 down-clocks the core, so sub-runs vary run to run;
the robust signals are the deterministic instruction parity, the baseline cycle
parity, and the large native speedup.

`logos-safe` (overflow checks on, not shown) costs ~3× the instructions of
`logos-release` on arithmetic loops (the per-op overflow branch blocks
vectorization) — the measured price of Logos's default safety. `fnv` overflows
by design so its safe build traps (correct); release/rustc both wrap and match.
