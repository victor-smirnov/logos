# ADR 0037. Specifications and SMT offload

Status: PROPOSED for pair review (2026-10-10). Part of the set under [0032](0032-logos-core-composition.md). Depends on [0035](0035-effects.md) (`pure`, `total`) and on the MIR-like linearisation of [0028](0028-datalog-engine-borrowck-traits.md) (BIR). Followed by [0038](0038-lean-integration.md).

## Problem

- No solver, contract or proof machinery exists anywhere in the tree (src/, stdlib/, scripts/, tools/, CMake). Solvers appear only as intent: `docs/internals/metaprog.md:48` ("constraint solving via Z3 … near-term priority"), ADR 0004:192 ("verification stack (Z3, Datalog) is core"), ADR 0020:425 ("EL ⊂ Datalog ⊂ solvers ladder … the SMT floor later").
- Existing oracles are Soufflé only (`src/compiler/dl`, `dl_export.logos`, `tools/dlog`).
- Const length expressions (`[T; N + 1]`) are evaluated only when every leaf is concrete (`sema.cpp:6292-6305`, `mono_subst.cpp:35`); there is no symbolic equality (`N + 1` vs `1 + N`).
- The synthesizable subset (ADR 0034 D7, ADR 0035 `synth`) needs guarantees stronger than tests: the cost of a hardware error is not comparable to a software one.
- Critical code (payments, security) is verified formally and, on top, manually; the manual part reads the specification, not the implementation, so the specification must be small and machine-checked against the code.
- Lowerings that a test corpus cannot cover (Deem Core → DPlan rewrites, the ADR 0033 Δ translation, graph → netlist) need per-artifact checking (translation validation).

## Decisions

### D1. Contracts and ghost code in the language

Model: Verus (Rust + SMT), with ownership as the frame rule.

- `requires`, `ensures` on fns; `invariant` on loops and on types (checked at construction and after `&mut` borrows end); `decreases` on loops and recursive fns (discharges `Diverge`, ADR 0035).
- Specification expressions are Logos expressions over `pure` + `total` fns, plus `forall`, `exists` (over typed or bounded domains), `old(e)`, `result`.
- `ghost` items and variables: erased at codegen; their bodies must be `pure` + `total`.
- Refinements on types: `type Port = u8 where self < 64;` (a type invariant on a newtype).
- Nothing is required by default; D6 says where verification is mandatory.

### D2. Verification conditions

VCs are generated from the linearised IR (BIR, the ADR 0028 S4 form): weakest preconditions over the CFG. Ownership gives frames: a call may modify only places reachable through its `&mut` arguments and owned arguments; everything else is preserved without annotation (the Prusti / Verus insight). Integers are bitvectors of their width; checked arithmetic yields an overflow side condition that, once discharged, removes that `Panic` origin (ADR 0035 D7).

### D3. Solver interface

- VCs are emitted as SMT-LIB 2 text; solvers run out of process (z3, cvc5; Bitwuzla for bitvector-heavy hardware VCs), as a portfolio with a time limit.
- Results are cached by the hash of the normalised VC; a build with an unchanged VC does not call a solver.
- `sat` returns a model mapped back to source values; the model is also emitted as a failing test case (counterexample → fixture).
- `unknown` / timeout is a failure with the VC saved, never a pass.
- `unsat` cores are mapped to the contract clauses used (which assumptions a proof needed).

### D4. Solvers are oracles

Same policy as Soufflé and CIRCT: the solver is an external oracle, not part of the compiler build. Its `unsat` is trusted by default. Under the `certified` profile (D6), an `unsat` must come with a proof certificate (cvc5 Alethe or LFSC) checked by an independent checker (ADR 0038 D4). A solver written in Logos ("everything in Logos") is a later option, not a precondition.

### D5. Uses beyond contracts

1. Indexed types: symbolic equality and inequality of const expressions in types (`[T; N + 1]` vs `[T; 1 + N]`, `Bits<W>` widths for hardware), with the const-length machinery as the front.
2. Translation validation: per-artifact equivalence checks for (a) Deem Core → Core rewrites and the ADR 0033 translator on small bounded instances, (b) graph → netlist lowerings (bounded model checking, the circt-lec / circt-bmc formulation reimplemented over this interface).
3. Deem: an SMT-backed source for constraint queries (the ADR 0020 ladder EL ⊂ Datalog ⊂ solvers), and Deem rules as specifications: an invariant stated as a Deem rule over a program's state (`violation(…) :- …` must be empty) is checked incrementally at run time (ADR 0033) and, for bounded state, statically by SMT.
4. Planner licences: algebraic properties a query relies on (associativity / commutativity of a fold, monotonicity) stated on the fn and proved once, enabling reorderings that ADR 0031 R5.3 had to refuse.

### D6. Profiles

| Profile | Where | Requirement |
|---|---|---|
| default | everywhere | contracts optional; written contracts are verified |
| `synth` | fns bound `synth` (ADR 0035) | no `Panic` origin left undischarged; `decreases` on every loop; refinements on ports |
| `critical` | packages marked critical in the manifest | every `pub` fn has `requires` / `ensures`; no `unknown` |
| `certified` | on top of `critical` or `synth` | every `unsat` certified (D4) |

### D7. Diagnostics for models

A failed VC reports: the contract clause, the path through the fn (statement spans), the counterexample values, and the unsat core of the closest successful neighbour if any. All as data (`--emit-facts` relations), so a model can query them, not parse prose.

## Rows

| Row | Content | Acceptance |
|---|---|---|
| V0 | SMT-LIB 2 emitter for a VC AST, solver runner (z3, cvc5), cache | round-trip fixtures; cache hit gate |
| V1 | const expressions in types through SMT (D5.1) | `[T; N + 1]` = `[T; 1 + N]` fixtures; a false equality refused with the counterexample |
| V2 | `requires` / `ensures` / `invariant` / `decreases` / `ghost` syntax and HIR; spec expressions typed as `pure` + `total` | parse and typing fixtures |
| V3 | WP over BIR for straight-line code and loops with invariants; ownership frames | verified fixtures: binary search, bounded buffer, checked arithmetic |
| V4 | overflow and bounds side conditions removing `Panic` origins | effect sets shrink, measured on the corpus |
| V5 | counterexample → fixture; D7 diagnostics as facts | a failing contract yields a runnable failing test |
| V6 | profiles (D6) in manifests | a `critical` package without contracts fails |
| V7 | translation validation of the ADR 0033 translator on bounded instances (D5.2a) | each Δ rule checked once per change of the rule |

## Open

- O1. Specification syntax: Rust-attribute style (`#[requires(...)]`) vs clauses in the signature (Verus). Recommendation: clauses (they are part of the fn's meaning, not metadata).
- O2. Quantifier instantiation (triggers): automatic first, explicit triggers when measured necessary.
- O3. Arithmetic in specifications: mathematical integers for spec-only expressions (Verus `int`) vs bitvectors everywhere. Recommendation: `int` / `nat` spec types, bitvectors for executable code.
- O4. Long-term: own solver in Logos, or permanent external solvers with certificates.
