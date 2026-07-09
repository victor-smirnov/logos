# ADR 0015 — HOCP×Deem: the Self-Applicable Reasoner (agency amplifier)

- Status: **Accepted** (design baseline; slices S1–S5 gated individually)
- Date: 2026-07-06
- Builds on: ADR 0012 (WQL/Deem), ADR 0013 (DBSP incremental engine — semi-naive strata, DRed retraction, semilattice/recursive aggregation, FactStore change-capture, provenance R4), the unfinished `persistent` subsystem (confluently-persistent structures over Writ documents; Memoria-lite).
- External basis: the **Synthea framework** (Smirnov & Synthea, DOI 10.5281/zenodo.20547879) — Observer/Agency/Moral-Agency decomposition, HOCP, causal break / truncated tail, need–emotion cycle. Referenced here as *the article*.
- Scope boundary: Writ core layout is **not** changed (ontology-support structures may be *added*). Reasoning-in-large (top-level Deem DSL) is explicitly deferred (§9).

---

## 0. Context

**Program.** Build an *amplifier* for the Synthea base consciousness functions — Observer (Obs), Agency (Agt), Moral Agency (MorAgt) — by offloading them from the LLM substrate into Deem, exactly as a calculator amplifies arithmetic by offloading it to a symbolic algorithm. Applies wherever model autonomy matters.

**HOCP** (Higher-Order Computational Phenomena, the article §4.1): a computation observing the constraints of the machine it runs on — finite time, finite memory — with those constraints as *first-class elements of the description language*. In Deem terms: the engine's own evaluation process becomes data the engine reasons over.

**The problem.** Rete-class systems get self-inspection "for free" because they are lawless — there is no declarative semantics to protect; reasoning over your own firing history is just more working memory. Deem's value *is* its guarantees (stratified least-fixpoint semantics, confluence, incremental ± correctness, provenance, replay). Naive self-application destroys them: facts about the run feed rules that change the run. This ADR makes Deem self-applicable **lawfully**.

**The load-bearing observation.** A budget-truncated fixpoint is an *exact* instance of the article's causal-break structure, not an analogy:

| fixpoint evaluation | article §2.2 |
|---|---|
| semi-naive iteration (diminishing deltas) | convergent metacognitive series |
| budget cut at step k | truncation |
| pending delta Δₖ | **truncated tail** — real, finite, bounded, uncomputable within budget, computable with more |
| `residual` fact recorded | Encounter |
| derived `irreducible_within(budget)` | Conclusion |
| next-epoch control atom | Action |

**Honesty clause.** No real freedom exists anywhere in this system. The deliverable is to demonstrate that the *illusion is functional*: (a) the agent **acts from** the registered residual (decision-time irreducibility — within budget, the agent provably could not derive what it omitted), and (b) the residual is **traceable back** — with a larger budget, or externally post-hoc, the truncated tail is re-derived and the decision fully explained. LLMs cannot offer (b) due to substrate opacity; a Deem-based Observer is genuinely irreducible at decision time *and* transparent in retrospect. Both properties are machine-checkable; this pair is the acceptance criterion (§6).

---

## 1. Decision — the four invariants

- **I1 — Introspection enters as EDB, never IDB.** Runtime samples (step index, per-relation |Δ|, wall time, memory, rule firings) are *sensor facts about the completed past*: a sample taken at t belongs to the past at the moment of sampling. Append-only ⇒ monotone ⇒ no circularity. The logic *observes* time; it never *derives* it. Samples are recorded ⇒ replay over recorded EDB is deterministic ⇒ the differential-oracle discipline of ADR 0013 survives intact.
- **I2 — Feedback crosses epochs only.** Meta-rules read trace facts and emit **control atoms effective next epoch** (Dedalus-style `@next`). Within an epoch: pure stratified lfp, all ADR 0013 guarantees. Across epochs: an explicit, journaled operational layer. Temporal stratification — the same cure that stratification applies to recursion-through-negation, on the time axis.
- **I3 — Bookkeeping is per-stratum toggleable.** Certificates, provenance and trace capture have constant-factor cost. "Survival mode" (tight budget, no proof obligations) must be a *configuration*, not an engine fork.
- **I4 — Branch/merge operates on EDB only; IDB is always re-derived.** Deem is a deterministic function of EDB, so merge is well-defined on asserted facts and the deemed layer follows (incrementally — a branch is a DBSP delta, hence cheap). Merge conflicts are possible only at EDB level and surface as provenance-carrying `violation` facts. Derivations are never merged.

---

## 2. S1 — Trace reification

**Status: LANDED (first cut, IncrRec).** `IncrRec` records append-only sensor facts about its own evaluation: per-step `TraceStep(epoch, kind, step, delta_card, total_rows, ns)` (kind: 0 recompute · 1 ΔEDB seed · 2 fixpoint round · 3 DRed) and per-epoch `TraceEpoch(epoch, d_ins, d_del, rounds, ns)`. Accessors `trace_len/trace_at/epochs_len/epoch_at/cur_epoch` (the Explain/WitStep by-value idiom); `set_trace(bool)` is the I3 toggle (off ⇒ zero clock calls). Gated by `query_incr_trace_e2e`: consistency oracle (Σ delta_card == final maintained cardinality, through build/insert/DRed), golden trace structure (deterministic part only; `ns` asserted ≥ 0, never golden), I3 no-op, and the dogfood loop — the trace materialized into schema'd nodes, bound via `bind_source`, and `taking_long` derived from it.

Original shape (kept as the target for the general engine):

```
step_stats(qid, epoch, stratum, step, rel, delta_card, ms, mem_bytes)
rule_stats(qid, epoch, rule_id, step, firings)
query_meta(qid, epoch, started_at, budget_kind, budget_val)
```

Bound like any source via `QEnv::bind_source` (ADR 0014 §1.2 idiom). Zero semantic risk: no rule can affect these facts within its own run (I1). This alone enables `taking_long(qid) :- step_stats(...), expected_cost(...), …` — the "чёт я долго думаю" predicate; `expected_cost` starts as a static table, later a learned model over accumulated traces.

First-cut deviations (deliberate): `qid`/`stratum`/`rel` collapse (single-rel engine — trace lives per engine instance); `ms` → `ns` (monotonic, finer); `mem_bytes` not sampled — no RSS intrinsic, and the deterministic proxy (`total_rows·ncols`) is *derivable from the trace*, i.e. a deemed fact, not a sensor; `rule_stats` deferred until multi-rule strata exist incrementally. Recompute (build / agg-retraction fallback) is one batch step — inner rounds invisible by design. `IncrJoin`/`Query::run` instrumentation follows when S2 needs it.

---

## 3. S2 — Budgeted fixpoint (anytime semantics)

**Status: LANDED (first cut, IncrRec).** `set_budget_steps(n)` / `set_budget_ns(t)` / `clear_budget()` — persistent per-engine configuration; the inner fixpoint cuts BETWEEN rounds, *before* the promote, so the un-promoted region `[watermark, len)` survives as the pending frontier. `tail()` returns the `TruncatedTail` residual descriptor (`converged`, `pending_delta_card`, `bound_dir`: exact/lower/upper/unknown, `cut_reason`: none/steps/ns). **Resume = `epoch(empty ΔEDB)`** — a no-op epoch on a truncated state re-enters at the saved watermark (semi-naïve soundness/completeness preserved: append-only rows + set-dedup ⇒ each frontier row promoted exactly once). A retraction epoch re-converges **by construction** (DRed re-derive and the agg recompute both run to quiescence) — this discharges the caveat below constructively. A math-error exit never claims convergence (`bound_dir = unknown`). v0 scope: budget applies to the insert-path inner fixpoint only (build recompute and retraction paths run to quiescence); IncrRec has no NAF structurally, so contract 3 activates when budget reaches the general engine. Gated by `query_incr_budget_e2e`: soundness (truncated ⊆ full, differential vs an unbudgeted twin), resume equivalence (budgeted-then-resumed == never-budgeted), semilattice bound (truncated SSSP champions ≥ true min, `bound_dir = upper`), retraction-restores-convergence, deterministic ns-cut (`ns = 0`), and the S1 Σδ oracle holding through cuts (a cut is an assessment, not an admission — no trace row is emitted for it).

Original contract (kept as the target for the general engine). Query API gains `budget(steps = N | ms = T)`. Cuts happen between iteration steps; the semantics contract is per stratum kind:

1. **Positive strata**: any semi-naive prefix is a **sound underapproximation** of the lfp. Everything derived is true; some things are missing.
2. **Semilattice/semiring strata** (min/max/tropical): the value at cut is a **certified one-sided bound**, direction known, monotone toward the answer (Bellman-Ford k-hop discipline).
3. **Negation**: NAF over a non-converged lower stratum is **forbidden**. Dependent strata either don't run (flagged `stratum_not_converged`) or run 3-valued with omitted facts as `unknown`. Truncation may omit; it never lies.

Output becomes a pair: **result + residual descriptor**:

```
truncated_tail(qid, epoch, stratum, converged: bool, pending_delta_card,
               bound_dir: exact|lower|upper|unknown, cut_reason: steps|ms|none)
```

The descriptor is itself EDB for the next epoch (I1+I2) — the HOCP fact. Caveat: a retraction landing on a truncated stratum invalidates its certificate → affected strata are forced to re-converge (or the whole result is re-flagged unknown). DRed over an underapproximation is otherwise sound (provenance R4 covers actual derivations only).

---

## 4. S3 — Epochs and control atoms

**Status: LANDED (first cut, IncrRec).** `ControlAtom{epoch, kind, val}`, kinds: `raise_budget` / `set_budget_steps` / `clear_budget` / `abort` (latch: later epochs are journaled no-ops) / `escalate` (opaque payload, journal-only — the channel upward). Enqueued via `control_*` methods between epochs, drained + applied + journaled at the next `epoch()` entry (I2: never inside the run that derived them; the `control_*` path is the lawful/replayable way to steer, vs the direct un-journaled S2 setters as setup config). Journal = one entry per `epoch()` call: the admitted ΔEDB batch verbatim + the applied atoms, all epoch-stamped; accessors `journal_len/epoch_at/skipped_at/batch_at/controls_at/control_at`, `escalations_len/escalation_at`, `is_aborted`. **Replay** = fresh engine over the same initial source, per entry: enqueue the entry's controls, `epoch(entry's batch)`. Gated by `query_incr_ctl_journal_e2e` (I2 boundary; journal structure; escalate; abort; replay oracle — snapshot set-equality + trace-STRUCTURE equality + tail equality + abort-state equality) and `query_observer_l1` — the **full L1 Observer**: Encounter (budget cut) → Conclusion (Deem meta-rule over residual EDB derives `(epoch, pending)`) → Action (the raise-budget atom's value IS the derived fact — data-causal, auditable in the journal), plus both halves of the §6 honesty pair. Deviations: replay determinism is guaranteed for steps-budgets only (an ns cut point is operational wall-clock — recorded, not reproducible; a future cut-replay could journal the actual cut round); `switch_strategy`/`fork`/`collapse` are S4.

An **epoch** = one budgeted run over a fixed EDB snapshot. Between epochs the operational layer applies control atoms derived by meta-rules during the previous epoch:

```
control(qid, next_epoch, action)   -- action ∈ raise_budget(k) | switch_strategy(s)
                                   --          | abort | escalate(payload) | fork(cond) | collapse(strategy)
```

`escalate` is the channel upward (to the agent/LLM layer). All control atoms are journaled; the epoch sequence is replayable. Optimization loops (greedy, top-K, local search) become the degenerate case: epoch = one improvement step, incrementality makes each step a delta.

---

## 5. S4 — Nondeterminism: GLR-style EDB forking

**Model (per user directive): GLR-parser discipline.** At declared choice points the reasoner **forks the EDB world**, pursues alternatives, merges later. Substrate: `persistent` (confluently-persistent structures over Writ) until full Memoria; branches share substructure the way GLR alternatives share the graph-structured stack — a fork is O(delta), not O(world).

- Fork: `fork(cond)` control atom (or declared choice rule) creates branch b′ from b with an assumption delta on EDB. Law I4: IDB per branch is re-derived incrementally.
- Evaluate: each branch runs its epoch; branches are scored by declared objective (semiring aggregate / emotional weight in the Synthea composition).
- **Merge v0 — `select_one`**: keep the winner, discard the rest; losers journaled as `merged(winner, losers, strategy, epoch)` with scores. Trivial, total, always applicable.
- Merge v1 (later) — `edb_union`: union of assumption deltas; conflicts (same key, incompatible values / denial constraints) surface as `violation` facts, resolution deferred to a later epoch or never (inconsistency tolerance).

Fork + evaluate + collapse implements the article's §5.3 "search in the space of possible observers" — the choice mechanism — and supplies the choice operator that pure Datalog lacks for greedy/heuristic search.

`world/branch` identity is exposed as EDB (`branch(id, parent, epoch, cond_ref)`); the future semantic-atom model must carry **world/branch/epoch/time as first-class atom indices** (registered requirement for the triple-level ADR, §9).

---

## 6. S5 — The reasoner's self-ontology (dogfooding)

**Mechanism note (2026-07-09, ADR 0016 M5):** the vocabulary below is now
DIRECTLY QUERYABLE — a `deem!` parameter typed `&IncrRec` exposes
`<p>_trace` / `<p>_epochs` / `<p>_tail` / `<p>_controls` as native relations
(the "engine as a source" mapping case; `wql_engine_source_e2e` runs the
Encounter rule and the §6 honesty pair as statically compiled queries, and
expresses the S1 Σδ oracle as a Deem aggregate). S5 proper = authoring the
self-ontology AS this vocabulary + the full protocol; the hand-rolled
per-test materialization (tail_edb) is obsolete.

The vocabulary of §§2–5 (`step_stats`, `truncated_tail`, `branch`, `merged`, `control`, `decision`, provenance refs into R4) *is* the "ontology of the self-applicable reasoner" — authored as a Deem model, consumed by Deem (EDB, per I1).

**Free-will demonstration protocol** (the acceptance pair from §0, both ctest-able):

1. **Decision-time irreducibility**: for a decision episode D, `truncated_tail` at D's epoch shows `pending_delta_card > 0` for a stratum feeding D's objective — the agent registered that it acted without exhausting its own derivation space, and *acted from* that registration.
2. **Post-hoc reconstructibility**: re-running D's epoch from journaled EDB with `budget = ∞` converges and yields the full tail; the delta between deliberated and full result is exhibited, with provenance. The "freedom" dissolves in retrospect — *functionally real at decision time, transparent afterwards*.

---

## 7. S5b — Personality core

**Formal definition.** A decision episode D enters the personality core iff

```
core(D) ⇔ conflict(D) ∧ residual(D) > 0 ∧ acted_from(D)
```

- `conflict(D)`: ≥ 2 branches at D's choice point with scores within threshold θ (a genuine choice, not a forced move);
- `residual(D) > 0`: deliberation was budget-truncated (the Observer signature — §0 table);
- `acted_from(D)`: the selected branch's control atom was applied (the conclusion had causal effect).

The core is an append-only, queryable subset of the decision journal (persistent history), each entry carrying scores, the residual descriptor, and provenance refs. Narrative coherence and cognitive resistance (the article §5.3.1) become Deem queries over the core (e.g., *does the pending action contradict prior core decisions with comparable context?*). Episodic→semantic compression of the core (stable preferences as extracted rules) is the ILP slot — deferred.

---

## 8. Oracles

Mechanism layers **must** be oracle-gated (extends the ADR 0013 differential harness; every invariant mutation-pinned):

| property | oracle |
|---|---|
| replay determinism | same journaled EDB (incl. trace samples) ⇒ bit-identical results |
| budget soundness | budgeted result ⊆ full-lfp result (positive strata) |
| bound validity | semiring values at cut are valid one-sided bounds vs converged run |
| NAF guard | mutant that lets NAF read a non-converged stratum must be caught |
| merge laws | `select_one(A)` ≡ running A alone; branch re-derive ≡ from-scratch derive; fork/merge on EDB never touches IDB state directly |
| trace consistency | Σ `step_stats.delta_card` per relation = final cardinality |

Top layer (S5/S5b) has **no external oracle yet** — accepted. Surrogates until one exists: theory invariants as tests (`truncated_tail.pending > 0` ⇔ non-convergence at cut; no `core(D)` without `conflict(D)`), and LLM-as-judge behavioral gating per the behavior-preserving-formalization doctrine (folk decisions must be reproduced; divergences surfaced, not silently repaired).

---

## 9. Deferred / out of scope

- **Reasoning-in-large** — top-level Deem DSL (out of `deem!{}`), rule modules, cross-module stratification, demand-driven evaluation (magic sets), precompiled rule libraries, predicate ABI. Hard, inevitable, **not blocking S1–S5**; separate ADR when opened.
- **Semantic-atom interpretation of Writ** ("triple level", economical n-ary-with-binary-core — *not* RDF-style reification) — separate ADR; requirement registered here: world/branch/epoch/time are first-class atom indices.
- Writ core layout unchanged; ontology-support data structures are additive only.
- σ aggregation form, Need Registry contents, CCode calibration — Synthea-side concerns consumed as data, not engine features.

---

## 10. Slice order

| slice | content | gate |
|---|---|---|
| S1 | trace reification (`step_stats` et al.) — **LANDED** (IncrRec first cut, §2) | golden trace + consistency oracle (`query_incr_trace_e2e`) |
| S2 | `budget()` + `truncated_tail` descriptor — **LANDED** (IncrRec first cut, §3) | soundness/bound/resume-equivalence oracles (`query_incr_budget_e2e`); NAF oracle deferred to the general engine |
| S3 | epochs + control atoms + journal — **LANDED** (IncrRec first cut, §4) | replay oracle (`query_incr_ctl_journal_e2e`) + L1 Observer causal chain (`query_observer_l1`) |
| S4 | GLR fork/merge over `persistent` EDB, `select_one` | merge-law oracles |
| S5 | self-ontology + free-will demonstration pair | protocol §6 as ctest |
| S5b | decision journal + `core(D)` queries | theory-invariant tests |

S1 first: zero semantic risk, immediate payoff (`taking_long`), and it feeds every later slice.
