# Round 2026-09-05c — TARGET ROWS, written BEFORE touching the compiler

## The subject is NOT a soundness-queue row.

The owner fixed the subject: a COMPILE-TIME regression. The soundness queue is
held, not advanced — queue gate rc 0 on 39 rows before and after, ceiling 0.
No row is bought and none is sold. The target below is a GATE, not a row:

    logos_02_semantic_core_pass_wql_domain_static_extremes   (TIMEOUT 120, RED)

and its three census-gate dependents (FIXTURES_SETUP logos_facts_all /
logos_facts_glob), which it takes down with it.

## Why this block and not a queue row

Every open queue row is reached through the compiler this fixture no longer
finishes. `logos_02_..._wql_domain_static_extremes` is a FIXTURES_SETUP
producer: while it is red the facts directory is not written and the three
`logos_09_*` census gates cannot run at all. A queue row closed under a red
producer is closed against an unmeasured tree. The ceiling is restored first.

## The shape that has paid every time

The prompt asks for "an ARM THAT EXISTS reached through a fact the code does
not carry". That is exactly this root, in the performance direction:

  `MLIRGenImpl::resolve_method_symbol` (src/compiler/mlir_gen_impl.hpp) is a
  PURE FUNCTION of (struct_name, method_name, pkg) over an immutable `prog_`.
  The arm that exists is the answer it already computed. The fact the code does
  not carry is THAT IT ALREADY COMPUTED IT: 29 666 calls, 62 distinct keys.

## Named targets, in order

1. `resolve_method_symbol` — memoise on (struct_name|method_name|pkg).
   MEASURED: 4.41x on the red fixture (30.72 s -> 6.97 s), cost 0 on
   `ctest -L bc` (2670 tests, rc 0) and queue ceiling 0.
2. `MLIRGenImpl::gen_for`'s per-iteration induction copy (mlir_gen_stmt.cpp,
   96fdf6235) — CONFIRMED LIVE in emitted IR, priced ~9% on loop-bound run
   time. Real, but NOT sufficient for phenomenon B's global 20%. Do not land a
   revert: the copy is Rust-correct semantics (a fresh binding per iteration).
   It wants an -O0-cheap spelling, not removal.
3. Phenomenon B's remaining root — NOT FOUND this round. See the record.

## What is explicitly NOT a target

- The prompt's census-allocation lead. REFUTED with numbers (see the record):
  7 of its 8 sites are already guarded, and arming the WHOLE census costs
  0.65 s of 31 s.
- Any weakening of the 120 s property. It is the sensor that caught this.
