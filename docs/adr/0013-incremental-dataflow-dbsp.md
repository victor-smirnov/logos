# ADR 0013 — Incremental Dataflow Engine (DBSP over RExpr)

- Status: **Proposed** (design-only; no engine code in this ADR)
- Date: 2026-07-04
- Supersedes / extends: builds on ADR 0011 (Writ schemas), ADR 0012 (WQL). Does **not** change the IR.
- Scope gate: this is the **critical-path gate** whose maturity gates the Hest dataflow language and the logosc-in-Logos rewrite.

---

## 0. Context and hard constraints

The WQL/Datalog stack ships two batch consumers of one relational IR (`RExpr`, `stdlib/std/wql/ir.logos`): the static emitter (`rexpr_walk.logos`, queue-1) and the runnable interpreter (`deem.logos`, queue-2). Both compute the **batch least fixpoint** by semi-naïve evaluation. What is missing is **incremental maintenance**: given a materialized relation and a batch of ±changes to its sources, recompute only the affected output rows instead of re-running the whole query.

This ADR designs that engine as a **DBSP-style** (signed-weight Z-set) incremental regime *over the existing RExpr*, not a new IR.

### 0.1 HARD CONSTRAINT — single-threaded, plain Logos (user, 2026-07-04)

The engine is an **ordinary single-threaded high-level Logos program**, exactly like the compiler itself. Concretely, and non-negotiably for this slice:

- **NO fibers, NO channels, NO concurrency substrate, NO parallelism** inside the engine.
- Deltas propagate through operators as **plain in-process function calls and loops**.
- An **"epoch" is a LOGICAL batching concept**, not a concurrency mechanism: *apply a batch of ±deltas → run the incremental update to quiescence → repeat*. There are **no barriers-as-threads**; the "barrier" is simply the point in a straight-line loop where one epoch's fixpoint has converged and the next batch is admitted.
- The Drools-style reaction feedback loop is a **plain loop**.
- Distribution / dataflow-fabric (Hest) is a **separate, later** concern and must not leak into this design. Everywhere prior settled notes said "fibers/channels/concurrency", read **"plain single-threaded loop / function call"**.

This is deliberately a **basic** implementation. Performance-oriented arrangements, persistence, and any fabric mapping are explicitly deferred (see R6, and the change-capture fork §6).

### 0.2 Invariants inherited (must not be broken)

- **Set / weighted-set semantics** identical to the batch oracle (dedup via shared hash buckets; `deem.logos:2739-2746`).
- **Termination / determinism is sacred**: confluent within an epoch, ordered dispatch, no silent round caps (a silent cap changes semantics — `rexpr_walk.logos:4255-4259`).
- **Recursion admissibility** keyed to `agg_algebra` (`rexpr_walk.logos:66-69`): SEMILATTICE (min/max) safe in fixpoint; GROUP (sum/count/avg) is a named compile error in recursion; default = GROUP/unknown ⇒ reject-in-recursion.
- **IR is frozen**: the normalized RExpr tree (`RLimit?(RDistinct?(RProj(RSort?(body),sel)))`, `rexpr_walk.logos:243`) is consumed *as-is*; the incremental engine is a **third execution regime**, alongside queue-1 emit and queue-2 interpret.

---

## 1. Decision (summary)

1. **Z-sets are represented as a Writ schema** (dogfooding ADR 0011): a delta row = `{ row: <tuple-row TOM>, w: i64 }`, ℤ multiplicity weight. A Δ-batch is a Writ array of such rows. This makes deltas serializable / relocatable for free.
2. **Every RExpr operator gets an explicit DELTA-DISCIPLINE class** — LINEAR (Δ passes through, stateless), BILINEAR (join family: `Δ(A⋈B)=ΔA⋈B + A⋈ΔB + ΔA⋈ΔB`), or STATEFUL (distinct, antijoin, aggregate). Aggregates split on the **algebraic-structure column**: GROUP = O(1)-via-inverse; SEMILATTICE = stateful re-min/re-max on retraction. Full table in §3.
3. **Recursion incrementalizes as nested Δ-streams / DRed** (delta-through-fixpoint), reusing the *exact* semi-naïve loop shape the batch already runs (`deem.logos:3750-3832`, `rexpr_walk.logos:4095-4259`) — same `total`/`delta` watermark structure, now driven by external ±deltas across epochs.
4. **Synchronous epoch model** (barrier-per-epoch, *logical*). Frontier/progress protocols (Timely/Naiad) are deferred.
5. **Change-capture fork RESOLVED for slice-1 = (A) mutation-instrumentation**; (B) CoW/snapshot-diff over persistent Writ is **design-for** (needed for R6). Rationale in §6.
6. **First slice** (§8): plain single-threaded, non-recursive, linear ops + exactly one incremental bilinear join against an in-memory arranged input, driven by ±delta batches, **differential-tested against the batch interpreter oracle** tuple-for-tuple with weights.

---

## 2. Z-set as a Writ schema (representation)

### 2.1 The delta row

A Z-set is a finite map `tuple → ℤ` (multiplicity/weight). We represent one entry as a Writ schema over a TOM (ADR 0011 mechanism, `ir.logos:4-6`):

```
schema ZRow : code(<query-category>_…_ZROW) {
    row: WRef<TupleRow> = 0,   // the atom: an already-schema'd tuple-row TOM
    w:   i64            = 1,   // signed multiplicity; +k inserts, -k retracts
}
```

- The `row` field is the **existing** rel-row representation — rel rows are already tuple-typed schema'd TOMs (`RtVal` element conversion `wany_to_rt`, `deem.logos:712`). No new row layout.
- A **Δ-batch** = a Writ array of `ZRow` (a TOM with `count` + fixed slot edges, exactly like `SExprArr`/`RAggArr`, `ir.logos:58-68`).
- A **materialized relation under maintenance** = a Z-set: the same `ZRow` array, with weights ≥ 0 after `distinct` (see §3), or raw ℤ multiplicities on internal streams.

### 2.2 Why a Writ schema and not a native `HashMap`

- **Layout-independent change capture**: a Δ is a set of `±atom` rows; it does not depend on how the source relation is physically laid out. This is the ADR 0011 dogfood payoff.
- **Serializable / relocatable for free**: a `ZRow` array is a Writ object; checkpoint/migration of engine state is ≈ free (the Hest asset later, *not* used now).
- **Registration path already exists**: the schema-catalog seam (`SchemaCatalog`, `deem.logos:163`) maps schema-code → typed fields at runtime; the **rodata blob channel** (`from_static`/`merge_static`, `deem.logos:315-366`) is the designated metadata path. `ZRow`/`ZBatch` register through the same `add_schema`/`add_field` builder calls as every other IR schema — **no new mechanism**.

### 2.3 In-memory index / arrangement (the "arranged input")

For the join, an input is **arranged** as an in-memory index keyed on the join key: `HashMap<K, Vec<ZRow>>` (or `HashMap<K, Vec<(row, weight)>>`). This is the **same structure the batch hash-join already builds** (`join_key_caps`, `rexpr_walk.logos:1040-1062`) — we reuse it, now kept *incrementally* (the "integrated" side): each Δ to that input applies `+w`/`-w` into the bucket and drops zero-weight rows. Arrangement is an **in-process data structure**, single-threaded; no sharing, no fabric.

---

## 3. Delta discipline — per-operator table (the core of the ADR)

Notation: `A_int` = integrated (arranged) value of a stream (its accumulated Z-set up to the current epoch); `ΔA` = this epoch's incoming delta. Output delta given per operator.

| RExpr node (code) | Class | Incremental delta rule | State kept |
|---|---|---|---|
| `RScan` `…0000` | **SOURCE** | `Δout = Δin` (the change-captured ±batch for this source) | none (source of Δ) |
| `RFilter` `…0002` (σ) | **LINEAR** | `Δout = σ(Δin)` — apply predicate per delta row, carry weight | none |
| `RProj` `…0003` (π) | **LINEAR** | `Δout = π(Δin)` — project each delta row, carry weight; weights may **coalesce** (same projected tuple ⇒ add weights) | none |
| `∪` (union of bodies into a rel) | **LINEAR** | `Δout = ΣΔin_i` — merge, add weights per tuple | none |
| `RJoin` `…0004` (⋈) | **BILINEAR** | `Δout = ΔA⋈B_int + A_int⋈ΔB + ΔA⋈ΔB`; weights **multiply** on match (Z-set product), keys via arranged index | `A_int`, `B_int` arrangements (§2.3) |
| `REdge` `…0001` | **BILINEAR, non-arrangeable** | source of the step depends on the outer bound row (`ir.logos:249-250`) ⇒ **no build-once index**; always nested-loop: `Δout = Δ(outer)⋈step(outer) + outer_int⋈Δ(step)`; the step collection re-derives per outer row | outer arrangement only; step recomputed |
| `RAnti` `…0005` (▷) | **STATEFUL / non-monotone** | maintain `matchcount[left_row]` = number of supporting right matches. Left row is in output iff `matchcount==0`. On `ΔB`: for each affected left key, if count `0→>0` emit `-left`, if `>0→0` emit `+left`. On `ΔA`: emit `±left` gated by current count. | `matchcount: HashMap<LeftRow,i64>` per left key |
| `RAggr` `…0006` (γ) | **STATEFUL — split on `agg_algebra`** | see §3.1 | per-group accumulator |
| `RDistinct` `…000A` (δ) | **STATEFUL** | multiplicity counting: keep `mult[tuple]: i64`; on `Δ`, `mult += w`; emit `+tuple` when `mult` crosses `≤0→>0`, `-tuple` when `>0→≤0`. This is the canonical Z-set `distinct`; it **clamps** internal ℤ weights back to {0,1} set semantics | `mult: HashMap<Tuple,i64>` |
| `RFix` `…0007` | **RECURSION** | nested Δ-stream / DRed — §4 | per-rel `total`/`delta`, plus stateful-operator state above |
| `RSort` `…0008` (τ) | **non-incremental** | top-of-plan; recompute on demand from maintained input (defer) | — |
| `RLimit` `…0009` | **non-incremental** | top-of-plan first-n; defer (order-dependent, needs sorted arrangement) | — |

### 3.1 Aggregates — keyed to the algebraic-structure column (`agg_algebra`)

The load-bearing split (`rexpr_walk.logos:66-69`, mirrored in the incremental engine):

- **GROUP** (sum / count / avg — abelian group): **O(1)-via-inverse**. Keep per-group accumulator `acc[key]`. On `+row` with value `v`: `acc[key] += v` (count: `+w`; avg: keep `(sum,count)` pair). On `-row`: `acc[key] -= v` — the group **inverse** makes retraction O(1). Emit `-old_agg, +new_agg` for the group when `acc[key]` changes; drop the group and emit `-old_agg` when it empties. **No re-scan.**
- **SEMILATTICE** (min / max — idempotent, *no inverse*): **stateful re-min / re-max on retraction.** Insertion is O(1): `best[key] = meet(best[key], v)` (min) / `join` (max), emit only on strict improvement — this mirrors the batch absorb gate exactly (`emit_rel_push_agg`, `rexpr_walk.logos:3494-3524`, strict `<`/`>` at `:3500`). **Retraction is the hard case**: retracting the current best re-opens the group and requires the *next* best.

  **RESOLVED (slice-4, `deem.logos` `AggState.vmap`/`vbest` + `ag_run`).** Per `(group, agg-column)` maintain an eager value-multiplicity map `vmap: value → ℤ live-count` plus a cached `vbest`:
  - **Insert** (`+row`, weight `w`): `vmap[v] += w`; if `v` newly present (`0→>0`) then `vbest = meet/join(vbest, v)` — **O(1) improve** (first live value seeds `vbest`; subsequent values only overwrite on strict improvement, mirroring the batch absorb gate).
  - **Retract non-best** (`-row`, `vmap[v]` stays `>0`, or drops to 0 but `v ≠ vbest`): `vmap[v] -= w`, drop the key at 0 — **O(1)**, no emit-relevant change to `vbest`.
  - **Retract the current best to 0**: drop the key, then **rescan `vmap`'s remaining live keys** for the new extremum — **O(#distinct live values in that group/column only, never the whole relation)**. If `vmap` is now empty this coincides with `gcount==0` ⇒ the **empty-group drop** path (only `−old` staged, `live=false`), and `vbest` is left stale-but-unread.
  - **Integrate-after-compute**: the pre-epoch `vbest` is snapshotted once per touched slot *before* any mutation; the staged group-row Δ is `−(key, old_best) / +(key, new_best)`. Invariant: `gcount>0 ⇔ vmap non-empty ⇔ vbest valid`; `gcount` is the single liveness driver for GROUP and SEMILATTICE alike, so mixed count/sum/avg + min/max columns coexist by per-column `agg_fnid` dispatch (parallel independent arrays, zero extra liveness state).

  This is the U2-corrected **multiset** strategy (line above). **Count-of-best is REJECTED** — the U2 review established it saves nothing: the "re-scan for next best" needs the group's live value set regardless, so count-of-best is only a lazy materialization of the same multiset, not an independent storage saving. An **ordered structure (BTree / heap)** giving an O(log n) rescan instead of the O(#distinct) linear scan is a **deferred optimization** (correctness-first; extremum churn is low in SSSP/reachability R2 workloads, so the linear rescan is rarely hit). Scope of slice-4 is the **non-recursive single-scan aggregate**; recursive re-min-in-fixpoint (R2, §5, DRed-for-lattices) is unaffected — this slice does not touch `RExpr::Fix`.

  Value column is **i64** (ordered total lattice; f64/str rejected — NaN breaks total order, `rexpr_walk.logos:3754`) — same constraint as the batch oracle.

  Differential coverage: `tests/logos/pass/query_incr_join_fuzz.logos` variants r14 (min) / r15 (max) / r16 (mixed count+min+max), 40 deterministic-LCG seeds × 12 epochs, weighted-multiset compare vs the queue-2 batch min/max oracle recomputed from clamped truth each epoch; the `val = (j*37)%101` column (with a deliberate index-5/10 within-group duplicate) forces best-churn — delete-current-best → re-min, delete-all → empty-drop, re-add-smaller → O(1) improve, retract-one-of-duplicate-best → best-unchanged. Guard: `query_incr_guard.logos` r14/r15/r16 assert min/max/mixed now ACCEPT.

### 3.2 Weight algebra summary (why the classes are what they are)

- LINEAR ops are **ℤ-module homomorphisms**: `f(ΔA) = Δf(A)`, so the delta passes straight through, weights preserved/added.
- BILINEAR ops are **ℤ-bilinear**: the product rule `Δ(A⋈B) = ΔA⋈B_int + A_int⋈ΔB + ΔA⋈ΔB` is exactly the discrete Leibniz rule over Z-sets; weights multiply on the join match.
- STATEFUL ops are **non-linear** (distinct clamps to a set; antijoin is non-monotone; semilattice-agg has no inverse) ⇒ they must retain per-key state and emit *derived* deltas when a threshold is crossed.

---

## 4. Recursion — incremental form (nested Δ-streams / DRed)

### 4.1 Batch shape being incrementalized

Both batch consumers realize the identical semi-naïve loop (single-threaded, plain loop):

```
for each SCC in condensation-topo order:            # reg.order[p]
  if non-recursive: run each member body once
  else (recursive SCC):
    SEED: run bodies with NO in-SCC source once     # first delta
    loop {                                            # to quiescence
      PROMOTE: delta ← next-delta; total ∪= delta    # watermarks on one append-only Vec
      if every member delta empty { break }          # least fixpoint reached
      VARIANTS: for each recursive body, for each in-SCC source occurrence:
        rctx_set_ovr(that scan)                       # variant = loop variable, NO IR rewrite
        run body                                      # reads delta at the overridden scan
    }
```

Storage: one append-only `Vec` per rel with `total=[0..tot)`, `delta=[dlo..dhi)` watermarks (`deem.logos:2739-2746`, `:3817`); variant = `rctx.ovr_w`, not an IR rewrite.

### 4.2 Incremental (DRed) form

Incremental recursion = **delta-through-fixpoint**. Two motions, both plain loops:

1. **Inner fixpoint (same as batch)**: within one epoch, iterate the semi-naïve loop above to quiescence. The *only* change is the seed: instead of seeding from base bodies only, seed from **the external ΔEDB for this epoch** propagated through the recursive rules. Monotone (positive) recursion just grows `total`; the loop converges when all member deltas are empty — **unchanged termination argument**. **Obligation (review U4): incremental recursion MUST re-apply an incremental `distinct` per rel per round** (the §3 `RDistinct` multiplicity clamp), because the batch loop only terminates thanks to per-round dedup via the rel shadow-set (`RelCtx.hmp`, `deem.logos:2739-2746`): without re-clamping each round, raw ℤ multiplicities from a cycle keep the round-delta non-empty forever and the "all deltas empty ⇒ break" quiescence test never fires. Monotone-termination transfers from batch ONLY under this per-round distinct.

2. **Retraction / DRed (over-deletion → re-derivation)**: a **negative** EDB delta can invalidate derived facts. The standard DRed three-phase, run as plain loops within the epoch:
   - **Over-delete**: propagate `-` deltas through the recursive rules to a fixpoint, tentatively retracting every fact whose derivation *could* depend on a deleted fact.
   - **Re-derive**: re-run the rules over surviving facts to a fixpoint, re-inserting any over-deleted fact that still has an alternative derivation.
   - **Net**: the epoch's output delta is the *net* of tentative-retract minus re-derive.
   **CORRECTION (slice-5, 2026-07-04 — the earlier premise was WRONG for cyclic graphs).** The prior claim — "under pure signed-weight Z-set semantics with `distinct`, DRed is subsumed by weight cancellation for linear+distinct recursion" — is **FALSE whenever the graph has cycles**, and slice-5 proved it (test `query_incr_tc_retract_gap`, case R-spurious). A single scalar weight per maintained tuple does **not** capture derivation structure through a cycle: with edges `{a→b, b→c, c→b}`, deleting `a→b` should retract `(a,b)`/`(a,c)`, but the surviving `b⇄c` cycle keeps re-deriving them, so a scalar `−`-propagation nets weight ≥ 1 and they **spuriously survive**. This circular-support case is *exactly* why DRed exists. Weight cancellation only suffices for **acyclic** derivation (DAG closures); the general recursive case needs **mark-based DRed** (over-delete then re-derive, tracking support) or the full DBSP nested-Δ iteration lifting (more than a scalar weight on the final relation). **Slice-5 ships INSERTIONS only** (monotone growth — genuinely incremental, differentially verified) and pins the retraction gap with the go/no-go test above; correct retraction = a later slice via mark-based DRed. This section is the design-for target for R1/R2.

### 4.3 Recursive aggregates — R2, the hardest (design-for now)

The recursive semilattice aggregate landed *statically* (`emit_rel_push_agg` + `emit_scc_fn`, `rexpr_walk.logos:3494-3524, 4142-4147`): a candidate `(key,arg)` absorbed against a persistent per-key **best-map** `__best_<rel>`, kept iff strictly improving, every improvement pushed to `__out` = the round's improvement delta. Convergence: monotone min/max over a finite reachable key set converges in ≤|keys|−1 waves; a minting head over a negative-weight (min)/positive-weight (max) cycle has **no least fixpoint** and is deliberately **not capped** (`:4255-4259`).

Incremental form:

- **Insertions only** (monotone): identical to batch — an improving `(key,arg)` overwrites `best`, emits `-old,+new`, re-triggers dependents. This is a straight delta through the existing best-map loop; **serve-for early** once slice-1 lands.
- **Retractions** (the hard case = §3.1 semilattice-on-retraction *inside* a fixpoint): retracting the current best of a group forces a **re-min**, and the re-min result must itself re-propagate through the recursion (a shorter path was deleted ⇒ downstream distances grow). This is DRed specialized to a lattice: over-delete the best, re-derive from the next-best across the fixpoint. **Design-for; precedent = Flix** (user-defined lattices with fixpoint semantics). The general regime is **semiring-matmul-to-fixpoint** (bool = transitive closure, (min,+) = SSSP, (max,×) = reliability; `project_datalog_hardware_mapping.md:16`). GROUP aggregates remain a **named compile error in recursion** (unchanged; counting paths in a cycle diverges).

**STATUS (slice-7, 2026-07-04 — SSSP / recursive min-aggregate lands in the queue-2 engine `deem.logos`).**
- **Compile path relaxed**: a single-rel recursive **semilattice** (min/max) aggregate head is now ADMITTED by `Query::compile` (the in-SCC aggregate-edge stratification check mirrors the static `plan_walker::check_stratified`), recorded on `QRelReg.rel_agg[ri]` (3 = min / 4 = max). GROUP-in-recursion (sum/count/avg) stays the **named non-stratifiable error**; multi-rel mutual recursion through an aggregate is a named reject. The recursive-aggregate body lowers to its underlying `RProj(RFilter?(RJoin(Scan, edges)), (key, aggarg))` pipeline (`lower_agg_body_recursive`) — the fold IS the push-time semilattice absorb (`qrel_push_agg`, a per-key `best: HashMap<i64,i64>` gated on strict improvement, the runtime twin of `emit_rel_push_agg`). Occ-detection / semi-naïve are reused unchanged.
- **INSERTIONS — GREEN, truly incremental** (`IncrRec.epoch`, ADR-blessed monotone improvement): a new / shorter / off-path edge is seeded off the ΔEDB and propagated to a best-fixpoint (the convergence guard fires on a non-well-founded cycle — `best.len()*4+16`, a diagnostic, never a silent cap). Differentially verified vs an INDEPENDENT in-Logos Bellman-Ford oracle (`tests/logos/pass/query_incr_sssp_fuzz.logos`).
- **RETRACTIONS — GREEN but NON-INCREMENTAL (correct via RECOMPUTE)**: the plain mark-based DRed (§4.2) CANNOT be reused for the min-aggregate — the additive-weight improvement log is **infinite on a cycle** (each loop grows `d` unboundedly), so a SET-semantics over-delete propagation does not terminate; an absorb-bounded (`od_best`) over-delete under-deletes superseded tainted rows (a stale champion then survives, and re-derive can only *lower* best, never raise it → wrong). The correct + terminating retraction this pass is a **full recompute over the surviving edge set** (the absorb fixpoint converges on cycles by monotone descent — "degrading toward recompute" is the design-blessed fallback, correctness > minimality). Differential-green for the decisive cases (delete the shortest-path edge → distance grows to next-best; delete-then-re-add round-trip; a positive-cycle-on-support delete re-routing without a stale short distance). **The incremental re-min-in-fixpoint (minimal DRed-for-lattices) remains the deferred R2 followup** — it needs argmin/predecessor provenance to bound the over-delete frontier and stop the additive-cycle blowup (Flix's approach); NOT a scalar-weight or plain-DRed reuse. Honest partial: insertions incremental, retraction recompute.

### 4.4 Termination and determinism under recursion

- **No silent caps** (invariant). The incremental loop uses the **same** "all deltas empty ⇒ break" quiescence test as batch; a non-converging spec is the *user's* non-well-founded program, diagnosed the same way batch diagnoses it (round counter `__rounds_<rel>`, exposed, not capped).
- **Determinism (scoped — review U3)**: determinism holds for the **epoch-final NETTED delta** (integrate-then-diff at quiescence), NOT for the raw per-step emissions. Stateful operators (`RDistinct`, `RAggr`, `RAnti`) emit on threshold *crossings*, so their intermediate ± stream is order-dependent (a `+` then `-` vs the reverse differ mid-epoch); only after integrating all intermediate deltas and reading the netted state is the result order-independent. The batch-oracle equality (§9) therefore compares **netted state at quiescence**, never a per-step emission trace — and external effects (§7) must observe only that netted delta for the same reason. Within this scope the operator graph is confluent (Z-set addition is commutative/associative; dispatch order = the fixed condensation-topo order `reg.order`) ⇒ the equality is order-independent and testable.

---

## 5. Epoch model (synchronous-first)

- **One epoch = one ±delta batch.** Processing: (1) admit ΔEDB batch; (2) run the incremental operator graph / recursive fixpoint to quiescence via the plain loop above; (3) the accumulated output Z-set delta is this epoch's result; (4) integrate deltas into arrangements/state; (5) admit next batch. This is **logical batching**, not a barrier-thread.
- **Synchronous / barrier-per-epoch** (logical): an epoch fully converges before the next is admitted. No frontier, no progress protocol, no multi-epoch-in-flight.
- **Deferred**: Timely/Naiad frontier & progress tracking, multi-version concurrency, any out-of-order epoch pipelining. These are only meaningful under concurrency, which is explicitly out of scope (§0.1).
- **Determinism** = confluent within an epoch + ordered dispatch (§4.4). This is R7, served-now.

---

## 6. Change-capture substrate — FORK RESOLVED

Two options were on the table (`project_writ_query_language.md:451`):

- **(A) Instrument the mutation API** (`WMap::set` etc.) → append a `±ZRow` to a per-source changelog at the mutation site.
- **(B) CoW / snapshot-diff** over persistent Writ versions (Memoria angle): diff two immutable Writ snapshots to synthesize the delta.

**Resolution: slice-1 = (A); design-for (B).**

Rationale:
- **(A) is the cheap, single-threaded first slice**: deltas are produced *at the mutation site* as `±weight` schema rows — no diffing pass, no second copy of the data, trivially correct, and it matches the "plain in-process" constraint. The changelog *is* the epoch's ΔEDB batch.
- **(B) is the persistent-index / arrangement story** needed for **R6** (Soufflé/Doop-class compiler-scale fact bases) and for cheap checkpoint/rewind. It couples to persistent Writ versioning (Memoria) and to arrangement persistence, both out of scope now. We **design the delta interface so it is source-agnostic**: the engine consumes a `ZBatch` regardless of whether it was produced by (A) instrumentation or (B) snapshot-diff. That keeps (B) a drop-in producer later — **no engine change** to adopt it.

**Scope statement (corrected, review)**: slice-1 exercises the **`ZBatch` seam** only — the engine consumes ±delta batches synthesized by the test harness. The actual `WMap::set` mutation-instrumentation (A) is **not yet wired/tested** in slice-1 (there is no live mutation source until a store exists); it is the designated producer behind the seam but remains untested until then. (B) CoW/snapshot-diff explicitly deferred. The claim is: the seam is proven by the harness driving it; (A) and (B) are drop-in producers, neither breaking the engine.

---

## 7. Requirements R1–R7 (each addressed)

From `project_hest_dataflow_language.md:64`.

| Req | What | Disposition | Rationale |
|---|---|---|---|
| **R1** | Recursion + stratified negation | **DONE (batch)** / **design-for (incremental)** | Batch recursion + antijoin already ship; incremental antijoin (`RAnti`, §3) and DRed recursion (§4) are designed here, implemented after slice-1. |
| **R2** | Recursive lattice/semiring aggregates through fixpoint | **DESIGN-FOR (into this ADR now)** — hardest | Batch form exists (§4.3, `emit_rel_push_agg`); incremental insertion is near-free, incremental retraction = re-min-in-fixpoint (DRed-for-lattices). Precedent Flix. Must not regress the "GROUP-in-recursion = named error" gate. |
| **R3** | Numeric domains (rationals for SDF balance, intervals for N-synchronous clocks) | **DESIGN-FOR** | Aggregate/value column is i64 in slice-1. Rationals/intervals are *lattice value types* that slot into the SEMILATTICE aggregate machinery (§3.1) once the value column generalizes beyond i64; the delta discipline is unchanged (still meet/join with re-min on retraction). Defer the value-type generalization; keep the aggregate interface value-type-parametric so it is a drop-in. |
| **R4** | Provenance / witness trees (derivation trees; semiring provenance) | **DESIGN-FOR** | A `ZRow` can carry an optional provenance edge (`WRef<ProvNode>`) recording the (rule, supporting rows) that produced it — the *same* Writ-schema mechanism (§2). Diagnostics ("cycle without delay: v1→v2→v3") = a walk of that tree. Not in slice-1 (adds per-row state), but the `ZRow` schema is designed with a reserved provenance slot so adding it is non-breaking. Selector-without-WHY loses half its value ⇒ this is design-for, not defer. |
| **R5** | Incremental DBSP | **THE GATE (this ADR)** / slice-1 serves the non-recursive core | — |
| **R6** | Soufflé/Doop-class perf + persistent indexes/arrangements | **DEFER** | Couples to change-capture fork (B) and persistent Writ. Slice-1 arrangements are plain in-memory `HashMap`. The `ZBatch`/arrangement interfaces are the seams that make (B) + persistence additive. |
| **R7** | Determinism / no timeouts | **SERVE-NOW** | Already an invariant: confluent within an epoch + ordered dispatch (§4.4, §5); no silent caps. |

**Reaction model (Drools-class)** — how it integrates (design-for; §0.1 makes the loop plain single-threaded):
- **Deduction = relations in ONE inner fixpoint, TMS-free via signed weights**: a derived fact's existence *is* its net weight; when its support vanishes (source retraction propagated through §3–§4), its weight nets to 0 and it disappears automatically — **no truth-maintenance bookkeeping**, the Z-set algebra *is* the TMS.
- **External effects = a THIN on-Δ boundary**: side-effects fire on the *output* delta of the deduction fixpoint (a callback per `±ZRow` at the top of plan), never inside the inner fixpoint. Effects observe only converged, netted deltas.
- **Nested fixpoints**: inner = deduction (guaranteed to converge under the monotone/stratified admissibility gates), outer = effects-governed. The outer loop admits the next epoch only after inner quiescence + effect dispatch.
- **Feedback crosses an epoch delay**: an effect that writes back into the EDB does so as the *next* epoch's ΔEDB batch — never within the current epoch. This is the single-threaded analog of a "delay register"; it guarantees the inner fixpoint always sees a fixed EDB and stays confluent. All of this is a **plain loop** (`project_hest_dataflow_language.md:25,58`).

---

## 8. FIRST SLICE — concrete spec (plain single-threaded, differential-testable)

**Goal**: non-recursive incremental maintenance of one relation = linear ops (`RFilter`/`RProj`/union) with **exactly one incremental bilinear `RJoin`** against an **arranged/integrated input**, driven by ±delta batches, differential-tested against the batch interpreter oracle.

Explicitly **out of slice-1**: recursion (`RFix`), `RAnti` retraction, semilattice-aggregate incrementalization (R2), `RSort`/`RLimit`, provenance, change-capture (B). No fibers/channels/concurrency anywhere.

### 8.1 Shape

- Take a normalized RExpr of the form `RProj(RFilter(RJoin(RScan A, RScan B, on)))` (linear ops around one join). The join's `B` side is **arranged**: `B_int : HashMap<K, Vec<(Row,i64)>>` keyed on the `on` key — the same structure `join_key_caps` builds (`rexpr_walk.logos:1040-1062`), kept incrementally. `A` side may be arranged or streamed.
- **Engine state** = `{ A_int, B_int arrangements; output Z-set out_int : HashMap<Row,i64> }`, all plain in-memory maps.

### 8.2 Epoch step (plain function, no concurrency)

Given `ΔA`, `ΔB` (each a `ZBatch` of `±ZRow`):
1. `ΔJ = join(ΔA, B_int) + join(A_int, ΔB) + join(ΔA, ΔB)` — the bilinear rule; weights multiply on key match.
2. `ΔF = filter(ΔJ)` — linear.
3. `ΔP = project(ΔF)` — linear; coalesce weights per projected tuple.
4. Apply `ΔP` into `out_int` (add weights; drop zero-weight rows). If the plan has a top `RDistinct`, clamp via multiplicity counting (§3, distinct rule) and emit the *set-semantics* delta.
5. Integrate: apply `ΔA` into `A_int`, `ΔB` into `B_int` (post-join, so the current epoch's inputs are the pre-delta integrated values — the standard DBSP "integrate after compute" order).

### 8.3 Termination / determinism (slice-1)

Non-recursive ⇒ **one pass, no loop**. Trivially terminating; determinism is the confluence of the bilinear rule + weight addition (§4.4). This makes slice-1 the *cleanest possible* differential target.

---

## 9. Differential-oracle harness plan

**Oracle = the queue-2 batch interpreter** (`deem.logos`), the runnable, data-driven consumer of the same IR. It computes the batch least fixpoint (`qrel_materialize`, `:3750-3832`; `exec_root`, `:4235`) and materializes a self-contained `QRows` (interned strings, `:4198`) with a stable API (`row_count/col_count/get_i64/get_f64/get_bool/get_str/get_node/is_null/col_ty`, `:3606-3631`). Sources bound via `QEnv::bind_source(name, warray)` / `bind_i64` etc. (`:558`).

### 9.1 The equality asserted

**Slice-1 harness preconditions (review U1) — assert before any compare:**
- **No rels** (`reg.n == 0`): slice-1 is a rel-free query (linear ops + one join), so `qrel_materialize` (`deem.logos:3750-3832`) is SKIPPED and the rel-materialization dedup (`:2739-2746`) never runs. The set/bag boundary for slice-1 is therefore the **top-level `RExpr::Distinct`** node (`deem.logos:3502`), NOT the rel shadow-set. The harness asserts `reg.n==0` and inspects the normalized plan for a top `RDistinct` to decide set-vs-bag mode.
- **Non-negative source clamp**: the LHS builds its source input as a **Writ array** (`bind_source` over a `warray`), which **cannot carry negative multiplicity**. So `D ⊕ Δ` must be clamped to non-negative per-row weights *before* materializing the LHS array (a delete that drives a row's count to 0 removes it; a delete of an absent row is a no-op on the source, though the RHS engine must still handle the transient negative — see below). The RHS `out_int` MAY hold transient negative weights mid-epoch (a `-` arriving before its balancing `+`); **compare only at quiescence** (§4.4 U3), where a correct engine has netted them.

For query `Q`, base data `D`, and a random ±delta batch `Δ`:

```
batch(D ⊕ Δ)  ==  incremental( batch(D), Δ )      # netted weighted multisets, at quiescence
```

- LHS: clamp `D ⊕ Δ` non-negative, run `Query::compile(text,&cat)` then `Query::run(env_with(D⊕Δ))` → `QRows` (the recomputed ground truth).
- RHS: materialize `batch(D)` once (the maintained `out_int`), drive the slice-1 engine with `Δ` (§8.2), read the maintained state back out as rows **after quiescence**.
- Compare **tuple-for-tuple with signed weights**: canonicalize both sides to a `HashMap<canonical_row, weight>` and assert equality of the maps. If the plan has a top `RDistinct`, weights are clamped to {0,1} on both sides before compare; else (bag mode) raw weights compared. Rows with net weight 0 are absent from both maps.

### 9.2 Row canonicalization

`QRows` gives typed column access; build a canonical key per row = tuple of `(col_ty, value)` across columns using `get_i64/get_f64/get_bool/get_str/get_node/is_null`. Two rows equal iff canonical keys equal. This sidesteps layout/interning differences (both sides intern independently). **Pre-build note (review): the RHS reads maintained `out_int` as raw `RtVal` rows, not `QRows`** — the canonicalizer must therefore cover BOTH paths: a `QRows`-row→key (LHS) and an `RtVal`-row→key (RHS) that produce the *same* canonical key for equal values (route `RtVal` through `wany_to_rt`/typed extraction so the `(col_ty,value)` tuple matches the `QRows` one). Canonicalize to one key type consumed by both sides.

### 9.3 Test generation (property-based, differential)

- **Data generator**: random source relations `A`, `B` over small integer domains (to force join-key collisions and multiplicities). Sizes swept small (0..N) so batch recompute is cheap.
- **Delta generator**: random sequence of ±insert/±delete against `A` and/or `B`, applied as one epoch (slice-1) — including deletes of rows that (a) exist, (b) don't exist (weight goes negative — must be handled), (c) are join-matched vs unmatched.
- **Multi-epoch**: apply a *sequence* of Δ's; after each epoch assert equality vs `batch(D ⊕ Σ deltas so far)`. Catches state-integration bugs that a single epoch misses.
- **Shrinking**: on failure, shrink to the minimal `(D, Δ)` that still diverges (the minimal-repro discipline).

### 9.4 Harness mechanics

- ctest subset: `cmake --build build -j12` then `ctest -R "wql|trama|query|ward"` from `build/`.
- Harness: `tests/logos/run_test.sh {pass|fail}` with `LOGOS_LIB_DIR=$PWD/build/lib/logos`.
- Full ctest = background only (>10-min ceiling).
- The differential test itself is a Logos program: it drives *both* the batch interpreter and the incremental engine in-process (both single-threaded) and asserts §9.1. Because the interpreter is the *already-shipping* oracle, the test needs no external reference implementation.
- **No claim of success without green** `-R "wql|trama|query|ward"` (and a background full-ctest on any compiler/stdlib change).

---

## 10. Self-critique

### 10.1 Forks genuinely RESOLVED

- **Change-capture (A vs B)**: resolved — (A) for slice-1, (B) design-for behind a source-agnostic `ZBatch` seam. This is a real resolution: (A) is fully specified and (B) is a drop-in producer, so adopting (B) later is non-breaking. **Solid.**
- **Concurrency**: resolved by fiat (user hard constraint) — plain single-threaded. No residual ambiguity.
- **Aggregate incrementalization split** (GROUP vs SEMILATTICE): resolved by reusing the *existing* `agg_algebra` column; GROUP = inverse, SEMILATTICE = re-min. The classification is already load-bearing in batch, so we inherit its correctness. **Solid.**

### 10.2 Forks/areas genuinely DEFERRED (honest)

- **R2 incremental retraction (re-min-in-fixpoint / DRed-for-lattices)** — the *non-recursive* semilattice retraction strategy is now **RESOLVED (slice-4, §3.1)**: eager value-multiplicity `vmap` + cached `vbest` + rescan-on-best-retraction (count-of-best rejected per U2). What remains deferred is only the **recursive** case — re-min *inside a fixpoint* (§4.3), where a retracted best must be re-derived through `RFix`/DRed. Slice-4 does not touch `RExpr::Fix`; the R2 recursion slice reuses the same `vmap`/`vbest` per-group machinery inside the DRed loop. **Non-recursive committed; recursive flagged, not hand-waved.**
- **DRed vs weight-cancellation boundary** (§4.2/§4.3): **RESOLVED by slice-5 — the earlier "weight-cancellation subsumes DRed for linear recursion" premise was WRONG for cyclic graphs.** Scalar-weight `−`-propagation is sound only for ACYCLIC derivation; cyclic support (`b⇄c`) spuriously survives external-support deletion (test `query_incr_tc_retract_gap` R-spurious). Correct retraction needs **mark-based DRed** even for *linear* recursion once cycles are possible. Slice-5 ships insertions-only + pins the gap; DRed retraction is its own later slice.
- **B (CoW/snapshot-diff)** and **R6 persistence**: cleanly deferred behind the `ZBatch` seam; no design debt beyond keeping that interface source-agnostic.
- **R3 numeric domains / R4 provenance**: deferred behind reserved schema slots (value-type-parametric aggregate; optional `ZRow` provenance edge). The risk is that "reserved slot" underestimates the interface change — provenance in particular threads through *every* operator (each must attach its rule to derived rows), which is more than a slot. **Honest risk: R4 may need an operator-signature change, not just a schema slot.**

### 10.3 Where the first slice could FAIL to be differential-testable

- **Bag vs set weight mismatch**: if the batch interpreter silently de-dups (set semantics) at a point where the incremental engine is still carrying raw ℤ multiplicities, the multiset compare (§9.1) diverges *spuriously*. Mitigation: the compare must clamp both sides identically at the plan's `RDistinct` boundary, and the harness must **assert the plan shape** (where distinct sits) before comparing. If a plan has *no* `RDistinct`, both sides must agree to run in bag mode. For slice-1 (rel-free, `reg.n==0`) the only dedup point is the top-level `RExpr::Distinct` (`deem.logos:3502`) — the rel-materialization dedup (`:2739-2746`) is never reached (§9.1 U1). The harness asserts this before choosing set-vs-bag mode.
- **Deletes of non-existent rows** (negative net weight): the incremental engine must not emit phantom `-` rows for tuples never present. If it does, `out_int` goes negative and the compare catches it — but only if the generator actually produces such deletes (§9.3 (b) mandates it). Risk = under-generation; mitigated by explicitly including that case.
- **Integrate-order bug** (§8.2 step 5 after step 1): if integration happens *before* the join computes, the `ΔA⋈ΔB` term is double-counted (ΔA already in A_int). This is the classic DBSP bug; the harness's multi-epoch mode (§9.3) is specifically what catches it — a single epoch from empty state can accidentally pass. **The multi-epoch sequence is load-bearing for correctness testing, not optional.**
- **REdge non-arrangeability**: `REdge` is out of slice-1, but if a test query lowers a path step to `REdge` unexpectedly, the slice-1 engine (which only handles arranged `RJoin`) would silently mishandle it. Mitigation: slice-1 harness must **reject/skip** any plan containing `REdge`/`RAnti`/`RAggr`/`RFix`/`RSort`/`RLimit` and assert the plan is within the linear+one-join fragment.

### 10.4 Where a delta rule is subtly wrong (watch-list)

- **`RProj` weight coalescing** (§3, LINEAR): projection is *not* injective — two distinct source tuples can project to the same output tuple. The delta rule MUST add weights on collision, else a `+1/+1` becomes `+1` (lost multiplicity) and set-semantics clamping later hides it in *some* cases but not under bag semantics. Correct rule: `Δout[π(t)] += w`. Called out so the implementation doesn't treat project as a 1:1 map.
- **`RDistinct` threshold direction** (§3): the emit condition is a *crossing* (`≤0→>0` emit `+`, `>0→≤0` emit `-`), not `w>0`. A naïve `if w>0 emit +` double-emits when a tuple already present gets another `+`. The multiplicity counter is mandatory.
- **`RAnti` on `ΔB`** (§3): the sign is inverted relative to intuition — a *new* right match (`+B`) causes a left row to *leave* the output (`-left`), and a retracted right match (`-B`) can cause a left row to *enter* (`+left`). Easy to get backwards; the matchcount `0↔>0` crossing is the guard. (Out of slice-1, but flagged for the R1 slice.)
- **Bilinear self-join**: if `A` and `B` are the *same* relation (self-join), a single ΔA must feed *both* input positions in one epoch: `Δ(A⋈A) = ΔA⋈A_int + A_int⋈ΔA + ΔA⋈ΔA`. Treating them as independent streams drops a cross term. Slice-1 should either exclude self-join or handle the aliasing explicitly (recommend: exclude from slice-1, add to the join-slice test matrix).

---

## 11. Consequences

- **Positive**: no new IR; the batch oracle already exists ⇒ the engine is differential-testable from day one. Z-set-as-Writ-schema dogfoods ADR 0011 and buys free serialization for the later Hest asset. Reusing `agg_algebra` and the semi-naïve loop shape means the incremental engine is *derived* from the batch engine (one mechanism), not a parallel reimplementation.
- **Negative / cost**: stateful operators (distinct, antijoin, aggregate) add per-key in-memory state that batch didn't keep; memory grows with the maintained relation. Acceptable at slice-1 (small), a real cost at R6 scale (deferred to arrangements/persistence).
- **Risk**: R2 incremental retraction and R4 provenance are the two places where "design-for" may prove to need more than the reserved seams (§10.2). Both are *out* of slice-1, so the risk is contained to their own future slices.
- **Gate**: slice-1 green under `-R "wql|trama|query|ward"` + a multi-epoch differential harness is the concrete exit criterion that unblocks the Hest dependency.

---

## 12. Review fixes applied (2026-07-04, before slice-1 code)

Adversarial review flagged three defects + two pre-build items; all applied inline above (design edits, not redesigns):

- **U1 — slice-1 oracle precondition** (§9.1, §10.3): re-anchored the set/bag dedup point to the top `RExpr::Distinct` (`deem.logos:3502`), NOT the rel-materialization dedup (`:2739-2746`) — slice-1 is rel-free (`reg.n==0`, `qrel_materialize` skipped). Mandated the harness assert `reg.n==0`, the non-negative source-array clamp on `D⊕Δ`, and compare-only-at-quiescence (RHS `out_int` may hold transient negatives mid-epoch).
- **U2 — count-of-best storage** (§3.1): corrected — it does not save value storage (re-scan needs the upstream integrated arrangement); it is a lazy variant of multiset, not an independent strategy.
- **U3 — determinism scope** (§4.4): scoped determinism to the epoch-final NETTED delta (integrate-then-diff at quiescence); raw per-step emissions of stateful ops are order-dependent. Effects (§7) observe only the netted delta.
- **U4 — distinct-per-round obligation** (§4.2): incremental recursion must re-apply incremental `distinct` per rel per round, else monotone termination does not transfer from batch (batch terminates only because `RelCtx.hmp` dedups each round).
- **Pre-build (canonicalizer)** (§9.2): the RHS reads raw `RtVal` rows; the canonicalizer must cover both the `QRows` (LHS) and `RtVal` (RHS) paths to one shared key type.
- **Pre-build (change-capture scope)** (§6): softened — slice-1 exercises the `ZBatch` seam only; `WMap::set` instrumentation is untested until a live mutation source exists.

Status → **ADR-READY for slice-1 implementation** (the fragment `RScan/RFilter/RProj/RJoin`, no recursion/aggregate/`REdge`/`RAnti`/`RSort`/`RLimit`; multi-epoch differential harness against the queue-2 batch oracle is the exit gate).
