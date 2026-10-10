# ADR 0033. Incremental Deem: DBSP as a translator over DPlan

Status: PROPOSED for pair review (2026-10-10). Part of the set under [0032](0032-logos-core-composition.md). Supersedes the engine design of [0013](0013-incremental-dataflow-dbsp.md) (its §3 operator table and §9 oracle equality are kept). Parent: [0031](0031-deem-core-layer.md) §3.4 ("evaluation strategy is a translator family"). Victor, 2026-10-10: «сделали не то, что собирались. Инкрементность нужно продумывать заново.»

## Problem

### What was designed

ADR 0013 (2026-07-04): Z-sets; a Δ-discipline per operator (LINEAR: filter, project, union; BILINEAR: join, `Δ(A⋈B) = ΔA⋈B + A⋈ΔB + ΔA⋈ΔB`; STATEFUL: distinct, antijoin, aggregates split by algebra GROUP / SEMILATTICE); composition by the chain rule; synchronous epochs with feedback across a delay; change capture behind a source-agnostic `ZBatch` seam ((A) mutation instrumentation, (B) CoW snapshot diff); DRed for recursion with cycles (scalar weights are unsound on cyclic support, slice-5); a first provenance cut (R4); differential equality `batch(D ⊕ Δ) == incremental(batch(D), Δ)`.

### What happened

The engine was built in the queue-2 interpreter (`IncrJoin`, `IncrRec`, `FactStore`, `AggState`, `FactHistory`) and deleted with it at P5 (e1dd0ac5e, 2026-08-09; loss ledger `docs/deem-interpreter-deletion-census.md` §6 L1-L11). The compiled tier grew a different thing between 2026-08-06 and ADR 0031 R7.4:

- a per-query HANDLE for group entries only: `<q>_incremental`, `<q>_apply(h, src, wd)` (transactional on a shadow copy), `<q>_epoch`, `<q>_retract`, `<q>_snapshot`; state `__gk/__gc/__ga_*/__gm_*`, weighted stored sides `__s0/__s0w/__s1/__s1w` (`rexpr_walk.logos:6004-8787`);
- admission by a first-failure cascade of 24 decline grounds (`dplan.logos:963-1243`, texts `why.logos:1256-1341`): no join chain of 2+ steps, no anti-join, no `edge`, no self-join, no `where` before the group, no `order by` / `limit` / `distinct`, no generics, no user aggregate, no `str` key / aggregate / result, …;
- recursive SCC drivers: insert-only `_scc_i`, DRed phases `_od` / `_odp` / `_cpt`, phase 3 = `_i` re-entered; the rel-backed `<q>_retract` is measured at 1.15-1.9x the cost of recomputing (`why.logos:1386`);
- no recursive min/max under maintenance (`rel_not_incr`, `DR_MINMAX`);
- change capture: the caller passes delta rows in the query's slice parameter; no Z-set type, no capture from containers or Memoria, no provenance;
- size: ≈4 700 lines (emitter ≈3 865, planning ≈770, grounds ≈140); 27 pass fixtures, 4 fail fixtures, 6 gates (`incr_eligibility_gate`, `incr_retraction_gate`, `incr_scc_driver_gate`, `incr_mutrec_refusal_gate`, the Soufflé oracle's incremental tier with 225 snapshot floors, `plan_ground_census` head 114).

### Diagnosis

The compiled tier maintains materialised views of particular query SHAPES. It has no algebra: a shape is admitted by enumerating what it may not contain, so every new construct is a new ground. This is the failure ADR 0031 removed from the one-shot path (110 match arms on surface shape), reproduced in the incremental path. DBSP removes it by construction: incrementality is a rewrite of the plan, operator by operator, `Q^Δ = D ∘ Q ∘ I`, with `(Q₁ ∘ Q₂)^Δ = Q₁^Δ ∘ Q₂^Δ`; linear operators are their own Δ; the per-key state of a join falls out of the product rule; semi-naive evaluation is derived, not invented.

## Decisions

### D1. Incrementalisation is a DPlan → DPlan translation

The incremental translator takes the one-shot DPlan of a program and produces a circuit DPlan. It is a member of the strategy family (ADR 0031 §3.4) beside one-shot and semi-naive; no emitter branches on "incremental".

- New DPlan node kinds: `Delay` (z⁻¹), `Integrate` (I), `Differentiate` (D), `Distinct^Δ` (threshold crossing), `Arrange` reused as the integral of a join side.
- Per-operator rules (ADR 0013 §3, unchanged in content): `Scan` = source Δ; `Filter`, `Project`, union: Q^Δ = Q, weights carried and coalesced; `Probe` (join): product rule over the two integrals; anti-join: per-left-key match count, emit on 0 ↔ >0 crossings; `distinct`: multiplicity count, emit on crossings; aggregates: by algebra class (D5).
- Composition by the chain rule over the DPlan graph; a node's state is exactly the integrals its rule names.
- Envelope (`order by`, `limit`, `first`): applied to the maintained output on read (`snapshot`), not maintained incrementally, unless the source's declared order makes it a prefix read. Today these are decline grounds; under D1 they stop being refusals.
- **Admission is the translation succeeding.** A refusal names the node and the missing rule; there is no list of shapes. Acceptance metric: the 24 grounds of `incr_admit` are deleted; what remains refused is refused by a rule gap that a fixture pins.

### D2. Language-level types

- `ZSet<T>`: a Z-set in normal form (distinct rows, nonzero `i64` weight), `T: Eq + Hash`. Operations: `insert(t, w)`, `iter() -> (&T, i64)`, `+`, `neg`, `distinct`.
- `Delta<T>` = `ZSet<T>` read as a change.
- The circuit of a program is a generated type with `step(&mut self, in: <Prog>Delta) -> Result<<Prog>Delta, ElError>` (one tick: all input deltas, all output deltas) and read accessors per maintained relation. `step` is transactional: on `Err` the state is the pre-step state (today's `_apply` shadow copy, or a CoW snapshot under D6).
- The handle API of the compiled tier (`_apply`, `_epoch`, `_retract`) is replaced by `step`; per-source convenience wrappers may be generated.

### D3. Recursion: nested time by default, DRed as a measured alternative

- Default: DBSP nested streams. A recursive SCC is an inner circuit iterated to quiescence per outer tick; its Δ form is derived by the same rules; retraction through cycles is correct by construction (the inner integrals carry derivation rounds, the cyclic-support defect of scalar weights does not arise). Cost: inner integrals per round, memory O(rounds × Δ).
- DRed (`_od` / `_odp` / `_cpt` / re-derive) stays a second translation of a recursive SCC, chosen by the planner with its ground when measured cheaper (memory budget, large SCCs with small deletions). It is not the default because its measured retraction is slower than recompute today.
- The planner may choose RECOMPUTE for an SCC (ground: estimated Δ-cost > recompute cost). Recompute is a translation too, not a fallback inside an emitter.

### D4. Recursive lattice aggregates

Recursive min/max (lattice rels, any `Ord` value since 2026-10-09) under maintenance:
- insertion: monotone improvement, the existing absorb gate, Δ = improved bests;
- retraction: the retracted best is replaced by the next best through the fixpoint; this needs the support of each best (argmin witness: which body row produced it). The translator keeps per-key support sets (Flix precedent: lattice fixpoints with provenance bound the over-delete frontier). Until that row lands, a lattice SCC translates to RECOMPUTE with its ground, never to a refusal.

### D5. Weights and accumulators

- GROUP aggregates (count, integer sum, integer avg as (sum, count)): Δ by inverse, O(1); checked arithmetic, overflow is `Err` at the tick, the tick rolls back.
- SEMILATTICE (min, max, any `Ord`): per-group value multiset, rescan on retraction of the best (ADR 0013 §3.1, slice-4 design); an ordered multiset when the value type has `Ord` (O(log n) rescan).
- Non-group algebras: `f64` sum / avg have no exact inverse (D ∘ I ≠ id under rounding). They translate to per-group recompute from the group's integral, in the integral's order, so the maintained value equals the one-shot value bit for bit. An exact accumulator (`xsum`-class) is a later opt-in with its own row.
- The semiring is a parameter of the translator: ℤ (Z-sets) now; ℕ[X] (provenance), tropical (min, +) later (D8).

### D6. Change capture

Sources declare their delta interface beside their relations:

```
trait DeltaSource {
    type Row: Eq + Hash;
    type Version;
    fn delta_since(&self, v: &Self::Version) -> Delta<Self::Row>;
    fn version(&self) -> Self::Version;
}
```

- Memoria containers implement it by snapshot diff: the parent snapshot IS the z⁻¹ of the circuit; a tick = a Memoria transaction; checkpoint, fork and replay of the circuit state follow from CoW snapshots.
- `mem` containers (Vec, HashMap, BTreeMap) get it through an instrumenting wrapper type (ADR 0013 option A), not by changing the containers.
- Slices and caller-built deltas stay supported: the caller passes `Delta<T>` to `step`.

### D7. Effects at the boundary

Output deltas of a tick are visible only after the tick commits. Effects driven by outputs (messages, writes, mail) are executed by the boundary, with an idempotency key per output row. Feedback into the circuit enters as the next tick's input (a delay); there is no way to feed a tick's own output into itself. Effect and capability checks: ADR 0035, 0036.

### D8. Provenance

The translator carries an optional witness per derived row: the rule and the support rows of one derivation (why-provenance), maintained under retraction (a row whose witness dies is re-witnessed or retracted). Query surface: `explain(row)` returns the derivation tree. Full ℕ[X] provenance (all derivations) is the semiring generalisation of D5; not in the first rows. This restores loss-ledger L4 on the compiled path.

### D9. Oracle

For every incremental fixture, the gate asserts per tick `snapshot_k == one_shot(D ⊕ Δ₁ ⊕ … ⊕ Δ_k)` with the one-shot DPlan of the same program, over random Δ sequences (multi-tick is mandatory: the integrate-order bug passes a single tick, ADR 0013 §10.3). The Soufflé oracle's incremental tier stays as the independent check. A metamorphic property is added: `step(Δ); step(-Δ)` returns to the pre-state.

### D10. Replacement

The compiled tier (`emit_incremental`, `_apply` / `_epoch` / `_retract` / `_snapshot`, `_scc_i`, `_od` / `_odp` / `_cpt`, `incr_admit`, `DIncr`, the 24 grounds, `stamp_rel_incr_shape`) is deleted in the row that replaces it, after a census: each of the 27 pass fixtures is ported to `step`; each of the 4 fail fixtures is re-derived (admitted with a new answer, or refused by a named rule gap); the 6 gates are rewritten against the circuit, with pinned numbers re-derived by hand, never relaxed.

## Rows

| Row | Content | Acceptance |
|---|---|---|
| I0 | census: every handle fn, ground, fixture and gate mapped to its fate under D1-D10 | ledger file, pinned by a gate |
| I1 | `ZSet<T>` / `Delta<T>` in stdlib (`lang` tier) | unit fixtures; normal-form invariant gate |
| I2 | translator for non-recursive programs: Scan, Filter, Project, union, Probe, anti-join, distinct, GROUP aggregates; `step` emission; D9 oracle | the non-recursive pass fixtures of the old tier ported and green; D9 per tick |
| I3 | SEMILATTICE and f64 aggregates (D5), envelope on read | min/max/f64 fixtures; bit-equality with one-shot |
| I4 | recursion by nested time (D3) | all recursive incremental fixtures; retraction through a cycle (`b⇄c`) correct |
| I5 | delete the compiled tier (D10) | ≈4 700 lines gone; refusal fixtures re-derived |
| I6 | DRed and RECOMPUTE as alternative SCC translations with grounds | planner choice traced; measured against I4 |
| I7 | lattice recursion with support sets (D4) | SSSP under edge deletion vs independent Bellman-Ford |
| I8 | change capture: `DeltaSource`, Memoria snapshot diff, `mem` wrapper (D6) | Memoria-backed circuit fixture: checkpoint, fork, replay |
| I9 | provenance witness and `explain` (D8) | chain, diamond, cycle, retract-and-rewitness fixtures |

## Open

- O1. Surface: is every `deem` program circuit-capable (a `step` is generated on demand), or is incrementality requested per item (`incremental deem …`)? Recommendation: on demand, by use of the circuit type; no keyword.
- O2. Integral storage policy: keep vs recompute is a plan decision with a cost; where the integrals live (heap, Memoria) is a property of the source. Needs the cost model of ADR 0024 S4 extended to memory.
- O3. Tick granularity for interactive programs (one request per tick vs batches): a property of the driver, not of the translator; settled with ADR 0034.
- O4. The DPlan nodes `Delay` / `Integrate` are shared with Hest (ADR 0034 D3). Naming and ownership of the node definitions settled there.
