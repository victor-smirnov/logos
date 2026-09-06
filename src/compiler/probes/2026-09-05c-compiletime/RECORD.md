# Round 2026-09-05c (pricing) — the compile-time regression

build: 6dd76d6d10202933 (read back exactly after restore; identical to STEP 1)
L1: 758 + 173 rc 0 · queue gate rc 0 on 39 · probe-log-lint 221

## STEP 1, with corrections to the prompt

soundness_queue 39 (tier1 6 / tier2 6 / tier3 25 / tier4 2), bc_admits 98,
bc_admits_blocked 25. Queue gate rc 0.

⚠ CORRECTION (repeat of 90ada803d's, still not in the prompt): the STEP 1 gate
command self-reports GATE BROKEN without `LOGOS_LIB_DIR`. CMake supplies it via
the test's ENVIRONMENT property. With `LOGOS_LIB_DIR=$PWD/build/lib/logos` the
same command is rc 0.

## TWO PHENOMENA, BISECTED IN THE STORE

Reproduced the handed-down series exactly (29-44 s for ~800 builds, then 774
59.5 / 782 53.1 / 791 98.0 / 813 120.1 FAILED). The corpus-wide paired-median
walk sharpens both ranges — neither is where the prompt put it:

  A  superlinear, large programs only.  `22e7e9c75..163f043bc` (build 756->774)
     NOT cumulative over 662->791 as handed down. `deem_incr_static_retract_e2e`
     is 13.1 s at 756 and 34.6 s at 774 — one step, 2.6x. The intervening
     paired medians are 1.014 / 0.996 / 0.995 / 1.009 / 1.004 / 1.004: flat.
  B  global, whole corpus.  `a9c7b67fd..96fdf6235` (build 813->830), median
     **1.191**, NOT 791->830. 792->813 is 1.004.

⚠ B IS REAL CODE, NOT LOAD — the control the shared box demands. Over every
pair of builds sharing a HEAD (pure noise), the paired median stays in
0.951-1.013, n>2000 each. 1.191 is far outside that band.

## THE PROMPT'S CENSUS LEAD IS REFUTED, WITH NUMBERS

Two independent measurements, either one sufficient:

1. Of the 8 named sites, **7 are already guarded** — `sema_impl.hpp` x4 sit
   inside `census_meet_`, which returns on `!census_armed()` before it builds
   any string; `sema.cpp` 10824/10827 are inside a `census_armed() || on(...)`
   block; 11177 is inside `if (census_armed())`. Exactly ONE is unguarded,
   `borrow_check.cpp::declare_pat_bindings`.
2. Arming the WHOLE census — every site, every allocation, all 36 buckets —
   costs **0.65 s of 31 s (2.1%)**: disarmed 30.90 s, armed 31.55 s. The one
   unguarded site's share is a subset of that.

And its arrival count is **15 704 per compile**, not the 15 793 294 the prompt
reports. `--stats` closes it: `borrow` is 585 ms of 31 589 ms (1.9%) and
`sema+lower` 1 880 ms (6.0%). The census sites cannot be 20% of anything.

## ROOT OF A, BY PROFILE

`--stats` puts **88%** of the compile in `codegen+write` (27 826 / 31 589 ms).
The `--emit-*` short-circuits decompose it without an edit: `--emit-mlir` 33.2 s,
`--emit-llvm` 30.4, `--emit-llvm-opt` 29.5, object 31.7 — MLIR->LLVM, the opt
pipeline and the object write are all FREE. The whole cost is `mlir_gen`.

Inside it: `MLIRGenImpl::resolve_method_symbol`, a linear scan of
`prog_->structs` then `prog_->functions`. 163f043bc wrapped its body in a
`search` lambda and made every `drop` query run it TWICE — once for a qualified
`<T>__Drop__drop` key, then again for the plain one. Censused before any edit:
29 666 arrivals, all `drop`, and the qualified pre-search hits **0 times**, so
it never exits early and always pays the full scan. 172 568 880 function
iterations per compile.

perf is unavailable here (`perf_event_paranoid` 4) and gdb cannot attach
(`ptrace_scope` 1); `--stats` plus the emit short-circuits did the job with no
sampler.

## PRICE (interleaved in ONE binary, 4 runs each, medians)

| arm      | wall    | ratio | scan.fn     | codegen+write | -L bc | queue |
|----------|---------|-------|-------------|---------------|-------|-------|
| HEAD     | 30.72 s | 1.00  | 172 568 880 | 26 782 ms     | -     | rc 0  |
| dropq0   | 16.14 s | 1.90x |  86 284 440 | 12 817 ms     | -     | -     |
| dropmemo |  6.97 s | 4.41x | (62 keys)   | -             | rc 0  | rc 0  |

`dropq0` halves every column exactly — that is the proof the phase IS this loop.
`dropmemo` (memoise on struct|method|pkg) is the fundable arm: **29 666 calls,
62 distinct keys**, and 6.97 s is below the PRE-regression 28.6 s. 163f043bc did
not create the quadratic; it doubled one that was already there.

## B: RANGE FOUND, ONE MECHANISM CONFIRMED, ROOT NOT CLOSED

96fdf6235's `MLIRGenImpl::gen_for` change gives the loop variable its own
per-iteration slot. CONFIRMED LIVE by reading the emitted IR — the loop body
block gains `load i64, ptr %2` + `store ptr %1`, and the use then reads the
copy. At -O0 nothing removes it. Priced by an IR-level control (edit the .ll,
build both with clang, interleave): 400 M iterations, 0.38 s vs 0.35 s = ~9% on
loop-bound RUN time.

That is real but NOT SUFFICIENT for a 20% global median, and it is aimed at the
wrong half: for a median-cost fixture the harness is 0.80 s idle of which the
COMPILE is 0.74 s, so B is a compile-time effect. B's root is NOT closed.
⚠ Do not revert the copy: a fresh binding per iteration is Rust-correct, and
96fdf6235 bought a queue row with it. It wants a cheaper spelling.

⚠ The residual is also not A: `rms.*` is EMPTY on the median fixture and
`dropq0` moves it 0.79 -> 0.78 s. A and B are separate roots, as handed down.

## WHAT DESERVES FUNDING

1. `dropmemo`. 4.41x, cost 0 on 2670 `-L bc` and queue ceiling 0, and it takes
   the fixture from 120 s (RED) to ~7 s of compile — with margin under the 120 s
   property rather than beside it. Price the stdlib and `fail` columns first:
   `-L bc` contains neither, and rule 5 says cost 0 there is not a safety claim.
2. Root B properly. It needs the one thing this round did not spend: a binary at
   `a9c7b67fd`, then `--stats` phase-by-phase against HEAD on three fixtures.
   The range is 4 commits and only `sema_expr.cpp` / `sema_stmt.cpp` /
   `sema_impl.hpp` / `borrow_check.cpp` are candidates for a compile-time cost.
