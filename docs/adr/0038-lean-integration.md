# ADR 0038. Lean integration

Status: PROPOSED for pair review (2026-10-10). Part of the set under [0032](0032-logos-core-composition.md). Depends on [0037](0037-specifications-and-smt.md) (contracts, VCs, certificates) and [0035](0035-effects.md) (`pure`, `total`).

## Problem

- SMT discharges first-order, mostly quantifier-light obligations. Induction over data structures, properties of recursive definitions, higher-order lemmas and algebraic laws are outside what SMT proves reliably.
- Models now write Lean 4 proofs well; an obligation that a model can prove in Lean is cheaper than one a human must argue.
- Several Logos components rest on theory that has, or needs, a mechanised semantics: Deem (stratified least fixpoint; magic sets; the DBSP Δ rules of ADR 0033, whose theory has a Lean mechanisation by its authors), the Hest determinism contract, the planned redesign of the language from a logical core.
- `certified` verification (ADR 0037 D6) needs an independent checker for solver certificates.
- Nothing Lean-related exists in the tree.

## Decisions

### D1. Lean is an external proof backend

Lean 4 is not a dependency of the compiler build. Proofs are checked by a separate gate (`lake build` over the repo's `proofs/` tree), like the Soufflé oracle. The compiler never runs Lean during an ordinary build; it checks that each obligation it emits has a proof whose statement hash matches (D3).

### D2. Export of Logos definitions

An exporter translates a fragment of Logos into Lean 4 definitions:

- structs → `structure`, enums → `inductive`, fixed-width integers → `BitVec n` (with `Int` / `Nat` for spec types, ADR 0037 O3);
- `pure` + `total` fns → `def` (recursion with its `decreases` measure → `termination_by`);
- contracts → theorem statements;
- Deem rules → inductive predicates (a rel's least fixpoint is the inductive closure of its rules).

The exporter is written in Logos (everything-in-Logos) and tested by round-trip on its fragment: an exported definition evaluated in Lean agrees with the Logos fn on generated inputs.

### D3. Obligations that SMT does not discharge

An obligation marked `#[prove(lean)]`, or returned `unknown` by the solvers, is exported as a Lean theorem with `sorry` into `proofs/<package>/<obligation-hash>.lean`. A proof is written there (by a model or a human). The compiler records the statement hash; a changed statement makes the proof stale and the gate red. A `sorry` in a proof file fails the gate under `critical` and `certified` profiles; under `default` it is reported, not fatal.

### D4. Certificate checking

For the `certified` profile, SMT certificates (cvc5 Alethe / LFSC) are checked by an independent checker. Options: a Lean-based reconstruction (the certificate replayed as a Lean proof) or a standalone checker. Recommendation: Lean reconstruction, so the trusted base is the Lean kernel plus the exporter, not a third checker.

### D5. Semantics models

Mechanised models of the semantics the compiler relies on, each proving the soundness of a rewrite once per rule (not per program):

| Model | Proves | Consumer |
|---|---|---|
| Deem Core semantics (stratified least fixpoint, envelopes) | Core → Core passes: magic sets, fusion, fact desugar, live rels | ADR 0031 R3 passes |
| DBSP over Deem Core | each Δ rule of ADR 0033 D1 / D5; nested-time recursion | ADR 0033 translator |
| Hest graphs (KPN + epochs) | determinism of graphs without `Merge`; backend equivalence of fused vs concurrent | ADR 0034 D4, D8 |
| effect inference | soundness of the effect rules w.r.t. an operational model of the core | ADR 0035 |

The models are the specification the compiler's rules are checked against (oracle independence: the model is written from the theory, not from the implementation). The language-core redesign (2026-08-11 direction) uses the same models as its first axioms.

### D6. Trusted base

Stated per profile and reported in builds: Lean kernel; the exporter (D2); the solver when not `certified`; the compiler's VC generator (ADR 0037 D2), which is itself a candidate for a Lean model later.

## Rows

| Row | Content | Acceptance |
|---|---|---|
| L0 | `proofs/` tree, pinned Lean toolchain, `lake` gate, statement-hash check | a stale proof turns the gate red |
| L1 | exporter for types and `pure` + `total` fns; round-trip test | generated-input agreement on the exported fragment |
| L2 | `#[prove(lean)]` and `unknown` → theorem stubs (D3) | an induction obligation (list reverse involution) proved in Lean and accepted |
| L3 | Deem rules → inductive predicates; Core semantics model | magic-set soundness proved for the corpus's adornment shapes |
| L4 | DBSP model; ADR 0033 Δ rules proved | each rule a theorem; a planted wrong rule fails |
| L5 | certificate reconstruction for `certified` (D4) | a cvc5 certificate replayed in Lean |

## Open

- O1. Mathlib: depend on it (large, slow builds, rich library) or a minimal library. Recommendation: Mathlib for D5 models, none for D3 obligations unless needed.
- O2. Lean toolchain pin and update policy (Lean releases monthly).
- O3. Proof maintenance under refactoring: statement-hash stability across renames (hash the normalised statement, not the text).
- O4. Direction Lean → Logos (extracting executable Logos from Lean definitions): out of scope.
