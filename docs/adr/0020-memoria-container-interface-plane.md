# ADR 0020 — Memoria: the container-interface plane of Deem, and Canon, its design orchestrator

- Status: **ACCEPTED** (user, 2026-07-13; pair-designed 2026-07-12..13; wave-0
  prototype landed on main — the design survived implementation unchanged)
- Date: 2026-07-13
- Builds on: ADR 0016 (mappings first-class; since c246699b/e8689a51 the whole
  query surface is language items — `deem`/`mapping`/`rel`; `deem!` retired,
  0.10.0), ADR 0017 (epoch = commit; journal + replay), ADR 0018 (Memoria
  port; NO compilation firewall §2.4; registry `(ctr_hash, block_hash) →
  BlockOps`), ADR 0013 (DBSP incremental engine, ±-provenance), ADR 0015
  (completeness-oracle pattern — the orchestrator here is its separate
  prototype), the checker-enforced view-invalidation contract
  (6adf7149/d5e232ff).
- Supersedes: the "structure of views" application-interface plan implicit in
  ADR 0018 §1 (the C++ containers-api surface as the model for the Logos API).

---

## 0. Context

C++ Memoria's hardest engineering was not algorithms but the API plane: a
layered stack (raw packed structures → containers → clients) separated by a
compilation firewall (CF), with bulk data crossing the CF as buffers of views.
The CF conflated three concerns: (a) compile-time insulation (a C++ template
pathology), (b) abstraction over storage engines, (c) forced batching of data
transfer (virtual-call cost made per-element access across the CF
unaffordable).

In Logos: (a) is structural — modules/binary archives insulate compilation;
(b) is trait-level, monomorphized; (c) dissolves — cross-layer calls inline,
so batching becomes an access-pattern choice, not an architecture. Views are
language objects whose validity the borrow checker enforces (a live view of a
block blocks every `&mut` on it — the invalidation contract).

Old Memoria's container interface was designed "for all occasions" because no
query engine existed above it. Now one does: **Deem is the primary consumer**.
The API is therefore designed top-down — from the needs of Deem's execution
model, with old Memoria as a capability reference (not an interface source),
down to physical layout (pkd).

## 1. Decision: the level map

```
Deem plane      rels, adornments, static plans, Z-sets            (the language)
   ↑ mappings — generated projections (item pipeline; attribute-derive for externals)
Memoria         container-interface language: kinds, measures,    (THIS ADR)
                cursors, batch ops, delta plane
   ↑ impl
implementations b+tree & friends over pkd                          (placement: §7)
   ↑
pkd             packed physical layout (blocks, slots, columns)
```

- **Memoria is part of Deem — fully integrated from day one.** Memoria
  declarations are LANGUAGE ITEMS immediately (user decision: no library/
  macro incubation phase), riding the machinery the deem arc rehearsed:
  contextual keywords (no reserved identifiers), RAW_TEXT bodies, DEF/DONE
  node pairs so binary-archive consumers re-register declarations
  (cross-module Canon reasoning needs exactly this), ABI break as the
  routine price of a language arc (the 0.10.0 precedent). The concrete item
  surface is the first-wave syntax spike's deliverable. Container
  IMPLEMENTATIONS incubate in `conuco/memoria`; the language work lands in
  the language tree directly.
- **Memoria is an ONTOLOGY of data structures with two facets**: descriptive
  (kinds, measures, laws, capabilities — the ontology proper, Canon's rules
  are its axioms) and operational (real traits with real signatures —
  cursors, views, builders — compiling to monomorphized calls). The fact
  mirror stitches them: every ontological assertion is pinned to a
  compilable artifact. The operational facet is what keeps this ontology
  from floating away from executable reality.
- Containers are NOT necessarily tables. From the planner's seat a container
  is whatever its mapping projects: table, index (an access path), queue (a
  pure delta source), associative memory (a soft source). The interface level
  speaks *data-structure semantics*; mappings translate to *relational
  semantics*.

## 2. Requirements, derived top-down from Deem

Read plane (static plans):

1. **scan** — tuple enumeration in leaf order, span-per-leaf, zero-copy views.
2. **seek by bound prefix** — the adornment path; every join's inner loop.
   Must compile to shuttle descent (log n), not scan+filter (§10 parity gate).
3. **range** — `k ∈ [a, b)` for comparisons and merge joins.
4. **ordered metadata** — the planner must KNOW enumeration order (merge
   joins, streaming aggregates without re-sort).
5. **cardinality / rank / sum** — join-ordering estimates + sublinear
   aggregates (range count, top-k, select_k). Ranked/summed trees are the
   Memoria capability SQL engines lack; the deferred sum-index gets its
   "need" here — from the planner, not from bt.
6. **projection pushdown** — read only needed columns ⇒ leaves are COLUMNAR
   (C++ PackedDataTypeBuffer re-derived as a requirement, not inherited).

Incremental plane (DBSP):

7. **deltas per epoch** — `changes(since) → Z-set` (epoch = commit, journal +
   replay already exist per ADR 0017).
8. **snapshot anchoring** — every read borrows `&Snapshot`; committed CoW
   snapshots are immutable ⇒ shared borrows of arbitrary extent are sound by
   construction. Writers take the `&mut` path — the view-invalidation
   contract one level up.

Write plane:

9. Deem delivers writes as **Z-set batches per commit** ⇒ the primary write
   path is `apply_delta(sorted batch) + commit`, i.e. bulk-build/merge — old
   Memoria's killer feature becomes the NORMAL write mode. Point
   assert/retract is secondary sugar. Rules-with-effects (ASP) stay out of
   scope; the DML statement layer over PURE queries is in — §4.3.

## 3. The interface language: three orthogonal axes

The language separates, without mixing:

- **A. Element contract** — what `T` must be: `Fst`/`StableLayout`/
  `EntryBytes` (already built at pkd level).
- **B. Kind semantics** — Map/Vector/Queue/… as traits with laws. Laws are
  not type-system-expressible; the oracle harness checks them (property
  tests), and STRUCTURAL invariants are Deem queries over low-level pkd/bt
  rels ("branch max = child max", "offs monotone", "back-refs closed") —
  verification in the same language as access.
- **C. Measure declarations** — which monoids annotate the tree.

The core theft from academia is the finger-tree measurement framework
(Hinze/Paterson): a tree annotated in a monoid, search = descent by a
predicate over branch summaries. Old Memoria's branch keys and sum indexes ARE
this: max-key = monoid max ⇒ seek; count = (+,0) ⇒ rank/select; sum(len) ⇒
byte offsets. **find/rank/select/range/seek are ONE operation — predicate
descent over measures** — so the interface core stays small. Shuttles are
zippers (Huet) + task logic; cursors are zipper state + a snapshot borrow.
Stepanov's iterator-category lattice is the discipline precedent for the
capability lattice; Calcite traits / System R "interesting orders" for the
planner profile; Okasaki for the persistence vocabulary; monoid comprehension
calculus (Fegaras/Maier) if the projection bridge ever wants a correctness
proof (mapping = monoid homomorphism).

The capability lattice (RelScan/RelSeek-by-adornment/RelRange/RelOrdered/
RelRank/RelDeltas) is NOT the container interface — it is the **output
profile of a mapping**: what the planner sees after projection. Containers
declare semantics (B) and measures (C); profiles are derived (§5).

View contract (until explicit lifetimes exist): view-returning interface
methods take `&self` + scalars only, so elision ties the result to the
receiver unambiguously — the checker's `result_borrows_self` heuristic makes
this a hard API rule, not a convention.

### Result-set materialization: the view-schema policy, refined

Old Memoria's data-lifting technique — lightweight structures of views
(view-schemas) — survives in the Deem plane, refined into a two-mode COLUMN
policy for tuple flow:

- **by-value** — primitive/small columns (`Fst` ∧ `sizeof ≤ 32B`, incl.
  composite fixed keys) are COPIED into tuples: copying ≤ 32B beats
  indirection and frees the tuple from the borrow.
- **by-view** — everything else (str, blobs, nested entries) flows as views:
  borrow-carrying tuples anchored to `&Snapshot` — the mechanism already
  proven at the Writ level (`#[borrow_carrying]`).

Column mode is DERIVED, not hand-declared: a Canon rule over `entry_shape`
facts (`fst(col) ∧ size(col) ≤ threshold → by_value; else by_view`). The
threshold (32B default) is metaprogram configuration (the WritStatic config
space), not a hardcoded constant.

Without fanaticism: both scenarios are engine-level. A consumer/algorithm may
request OWNED results (materialize mode — result must outlive the snapshot,
or the algorithm prefers ownership/locality); the engine then copies
DIRECTLY at extraction — no intermediate view-schema is built and then
deep-copied. View mode remains the zero-copy default for pipelined plans.

## 4. Kinds: first wave and their projections

First wave (accepted): **Map (ordered), Vector, Queue.**

| kind | native vocabulary | planner projection |
|---|---|---|
| Map<K,V> | find/range/cursor, apply_delta | EDB rel: scan+seek+range (+rank with count measure) |
| Vector<T> | select_k/rank, spans | EDB rel with POSITION column; O(log n) positional adornment |
| Queue<T> | push_batch/poll/subscribe | **not a rel — a pure delta source** (Z-set stream, no integrated state) |

Queue closes the DBSP square: table = integrated stream, queue =
differentiated table; the interface level spans both poles of one algebra.
(The same queue interface is Hest's future substrate — designed with both
consumers in mind, gated on neither.)

Second wave: **Index** — not an independent rel but an ACCESS PATH of another
rel (seek-only profile; the planner rewrites base-rel seeks into probes);
**AssocMemory** — a "soft rel": probe = top-k by similarity, a score column,
no enumerable extension; planner-wise an oracle source (same slot as
built-ins). The bridge toward the reasoner-registry endgame; listed with a
profile now, implemented much later.

### 4.1 Beyond enumerable columns: system measures and dense domains

The plane's relational model is OPEN along two axes; both are proven Memoria
capabilities (C++ instance), and both enter the VOCABULARY now regardless of
implementation wave.

**System measures — frequency and weighted sampling.** Any container may
attach the dedicated system attribute `freq`. As a measure (monoid `sum w`,
prefix sums in branch nodes) it derives O(log n) weighted sampling: draw
r ∈ [0, total), predicate-descend by cumulative weight. No new machinery —
axis C (§3) paying out: a decoration ANY kind can add; Canon rule
`measure(C, sum freq) → can_sample(C)`. Two consumers: query-level sampling
operators (probabilistic/approximate algorithms) and the optimizer's own
cardinality estimation. Sampling is nondeterministic choice — outside
classical Datalog semantics — so it enters plans as an explicit
operator/source profile, not as a change to rel semantics. Staging: `freq`
is the natural SECOND measure for the Vector first load — it forces open
question §9.1 (heterogeneous measures) early, where the design risk sits.

**Dense domains — hypercube encoding (associative memory 2).** Column
domains need not be enumerable: AM2's domains are (pseudo-)continuum, and a
stored "tuple" is a HYPERCUBE (product of intervals), not a point. One box
encodes a whole cross-product of ranges — the compact representation of
many-to-many relations that enumerable-only models handle badly (|A|×|B|
materialization). Formally: constraint relations — generalized tuples as
conjunctions of interval constraints (the constraint-database line,
Kanellakis/Kuper/Revesz); matching = stab / box-intersection queries. The
vocabulary carries the axis explicitly: `domain(col, enumerable(card) |
dense)`, `encoding(rel, points | boxes)`; capabilities `can_stab` /
`can_intersect`. AM2 joins AM1 as the second associative kind — AM1 is SOFT
(score, no exact extension), AM2 is EXACT but intensional (constraint
tuples). Kind implementation: second wave+; the domain axis enters the fact
schema NOW so nothing retrofits.

### 4.2 The extension law: how container capabilities enter the plane

Containers can do more than Datalog. Using Deem as their computational
frontend poses a dilemma: extend Deem toward Memoria, or restrict containers
to Datalog. The second is REJECTED (user decision); the first proceeds by
LAW, not by precedent — "carefully" has exact mathematical content:

- **Axis 1 — annotations (semiring provenance, Green/Tannen).** K-relations:
  facts carry annotations from a commutative semiring; least-fixpoint
  semantics is preserved for ω-continuous K. Boolean K = classical Datalog;
  ℕ = bags; [0,1] = probabilities/scores (AM1); tropical = min-cost; **ℤ =
  Z-sets — DBSP already IS a semiring-Datalog instance**. Frequencies,
  weights, similarity scores, costs enter here — a change of annotation
  domain inside blessed theory, not a semantics break.
- **Axis 2 — stratified operators.** Non-monotone / nondeterministic
  capabilities (sample, top-k, select_k, argmin) enter as operators at
  STRATUM BOUNDARIES: they consume a fully-computed relation, produce a
  relation, and are FORBIDDEN inside recursion — the same law and the same
  machinery as stratified negation (already shipped).
- **Domain generalizations preserving fixpoints** need neither axis:
  constraint relations (AM2) keep Datalog semantics as-is.

Three tests for any capability entering the relational plane:

1. It is expressible as (a) a semiring annotation, (b) a stratified
   operator/source, or (c) a fixpoint-preserving domain generalization.
   Fits none → it does not enter (see the hatch).
2. **Nondeterminism is externalized**: `sample` takes an explicit seed —
   `db × seed → tuple` is deterministic and replayable (the oracle harness
   and incremental re-runs depend on this).
3. **The incrementality contract is declared in the profile**:
   `differentiated` (DBSP-differentiable) | `re-derived` (region recompute) |
   `index-backed-fresh` (e.g. the prefix-sum sampler holds no state — every
   call is fresh from the index).

**The escape hatch dissolves the dilemma structurally**: Deem is the
frontend, not a prison. Memoria kinds are ordinary monomorphized Logos
objects; their native APIs stay public and directly usable. "Restricting
containers to Datalog" cannot happen by construction — the relational plane
is a projection, not the only door. A capability that fails the tests is not
lost; it is simply not relational (consistent with the Deem-judiciously
rule: loops and group-bys are plain Logos).

Engineering note: the generalized-K engine is NOT built now. The law is
stated; instances arrive on demand (freq/sampling first, AM1 scores second);
the internal algebra is prepared — DBSP is already the ℤ instance.

### 4.3 DML: the statement layer and the ingestion pipeline

Batch construction is Memoria's strongest side — every container supports it,
and it is ~90% of the generalized constructor's complexity; relational tables
build at hundreds of MB of dense data PER CORE. The plane therefore carries a
DML layer: INSERT, UPDATE, DELETE, INSERT FROM SELECT, CREATE FROM SELECT.

This does NOT reopen §2.9's rejection of rules-with-effects: DML is a
STATEMENT layer over pure queries. All five forms reduce to one shape —

```
INSERT  q              = q → +Δ             → apply_delta + commit
DELETE  WHERE q        = q(keys) → −Δ       → apply_delta + commit
UPDATE  SET… WHERE q   = q → (−old, +new)   → apply_delta + commit
INSERT FROM SELECT     = same, q = arbitrary plan
CREATE  FROM SELECT    = CTAS: new container, schema from q's signature
```

— a pure query producing a Z-set, applied atomically at commit. Statement
reads see the snapshot the statement runs against; the delta births epoch
S+1 (ADR 0017). In §4.2 terms, effects are the third boundary-operator kind:
**sinks at the OUTERMOST plan boundary — never inside recursion, never in
rule semantics**. Datalog stays pure.

**The pipe is where the MB/s live**: the plan's output streams DIRECTLY into
the bulk builder (bottom-up leaf filling, no per-element descent); data is
copied exactly once — into the new leaves. The pipe's contract is ORDER: the
builder requires sorted input; the planner knows plan output order (§2.4
interesting orders) — matching order → direct streaming, otherwise a sort
operator intervenes. Classical CTAS machinery, monomorphized and zero-copy
to the leaf.

Consequences for the interface language:

- **Builder is first-class axis-B vocabulary** (not an afterthought): every
  kind declares its builder contract — ordered stream in → container out,
  with input-order requirements. Canon fact `builder(C, order_req)`; the
  planner matches plan order against it.
- **CTAS is the SANCTIONED materialization**: the no-gratuitous-
  materialization rule (ADR 0016) demands materialization be targeted and
  explicit — CREATE FROM SELECT is its legal form. The new container's
  schema derives from the query's column signature (Canon: entry_shape from
  the rel signature; kind/measures declared or defaulted).
- Horizon (not first wave, vocabulary must not obstruct it): a STANDING
  INSERT FROM SELECT + DBSP = a materialized view with automatic
  maintenance — the write and incremental planes converge.

### 4.4 DDL: managed tables vs mapped externals

Two source classes in the plane:

- **Deem-managed tables** — ordinary multi-column containers living in the
  store under Deem's management: full lifecycle via DDL (CREATE / ALTER /
  DROP TABLE).
- **Mapped externals** — objects whose shape is owned by their own
  declarations, projected via mappings: NO DDL; the mapping is the
  interface.

**The catalog is EDB, in Canon's vocabulary.** CREATE TABLE = write
`kind/entry_shape/measure/format_hash` facts into the store catalog +
allocate the container + register the format. This is EXACTLY Canon's fact
schema — one vocabulary, two lifetimes: compile-time facts about
declarations (Canon) and the runtime catalog of live store objects.
Consequence: **Canon's validation rules double as runtime DDL validators** —
rules are ordinary Deem rules and run in either tier.

**Schema is epoch-scoped: ALTER is non-destructive by construction.** In the
CoW store a schema change is a commit: new epoch, old snapshots keep reading
the old schema, time-travel includes schema. Migration paths — both
available, chosen per ALTER kind:

- **eager rewrite** via the §4.3 pipe: CTAS into the new format + swap —
  bulk speed (hundreds of MB/core) makes a full rewrite cheap;
- **lazy per-block**: the registry already supports format coexistence
  (`format_hash`); blocks upgrade on CoW touch — fit for metadata-only
  changes (add column with default).

**Static/dynamic tiers.** Statically declared managed tables are schema
items queries compile against; their DDL materializes at store open, where a
Deem query over the catalog checks compiled expectations against reality
(mismatch → error or migration) — the principled form of the migrations
story. Runtime CREATE serves the dynamic query tier (the ADR 0012 interp
path). §4.3's CTAS = CREATE TABLE + INSERT FROM SELECT, so CREATE enters
with the Map wave; ALTER is second wave+.

**Live self-evolution.** Standing computations (DBSP fixpoints, materialized
views, subscriptions) are BRANCH-ANCHORED consumers: runtime DDL creates a
new branch, and old fixpoints keep running on the old schema untouched —
their delta streams live there. Migrating a standing computation is
blue-green INSIDE the store, by construction: backfill on the new branch via
the CTAS pipe (bulk speed), delta catch-up, consumer switch, retire the old
branch. Zero-downtime schema evolution is a property of the construction,
not an ops procedure. Horizon: this is precisely the substrate property the
self-applicable-reasoner line (ADR 0015 / Synthea) requires — a system
reshaping its own schema while its reasoning fixpoints keep running. Open:
merge semantics for schema-DIVERGENT branches (the ADR 0017 multi-parent
merge meets DDL) — §9.

## 5. Canon — the design orchestrator

**Canon** (κανών — the measuring rod, whence "rule"; the judge-by-measure,
register-mate of Writ and Deem). **Role: orchestrator of container DESIGN —
never in the query-execution path.** It replaces, in principled form, what
mbt + the BTTypes/CtrTF template floors were in C++: the thing that derives,
from declarations, what exists, what is complete, and what to generate.

- **Facts** — a mirror of Memoria declarations, emitted natively by the
  item handlers (§1; pipeline-shape precedent: ADR 0014 docs):
  `kind(C, ordered_map)`,
  `measure(C, M)`, `entry_shape(C, S)`, `domain(col, enumerable(card) |
  dense)`, `encoding(rel, points | boxes)`, `leaf(C, L)`, `impl(C, Trait)`,
  `builder(C, order_req)`, `format_hash(Impl, H)`, …
- **Rules** — ordinary `mapping` modules over those facts: capability
  closure (`measure(C, count) ∧ annotated_tree(C) → can_rank(C)`), profile
  projection, column materialization modes (`col_mode` — §3), completeness
  queries ("what is missing for Vector to serve as a
  Map leaf?"). Capability composition is a transitive closure — exactly the
  Deem-appropriate shape; hand-listing explodes exactly where C++'s TMP floor
  did.
- **Harness** — the Memoria item handler (managed kinds) or attribute-
  derive (mapped externals) runs Canon via metacall at compile time and
  consumes its verdicts to emit the projection. First production instance of
  "the compiler invokes a reasoner".
- **A Deem APPLICATION, not a Deem extension**: Canon's own reasoning is
  classical stratified Datalog (capability closure = transitive closure,
  completeness = negation-as-failure). The §4.2 extension law serves the
  DATA plane, not Canon. Canon needs from the engine only: metacall
  execution, provenance, (later) incremental re-deem.
- **Judge, not doer** — the responsibility chain is rigid:
  `declarations → facts (compiler) → Canon verdicts (with provenance) →
  derive handlers consume verdicts → trama/metaprog emit → compiler
  compiles`. Canon never emits; metaprog never decides. If either crosses,
  mbt returns.
- **First instance of reasoner-driven codegen** — the rehearsal of
  compilation-as-deduction: Canon (containers) → the harness transfers to
  Nous (ontologies) → logosc-on-Logos (the whole language over the reasoner
  registry). The perf story (incremental re-deem) gets proven at Canon's
  scale (dozens of facts per module) before meeting logosc's (millions).
- **Two free wins**: ±-provenance ⇒ diagnostics are derivations ("C cannot be
  a Map leaf BECAUSE measure(count) is missing"); DBSP ⇒ incremental re-deem
  of profiles on per-module fact changes — a future compile-speed lever.
- **Separate from Nous, prototype FOR Nous** (user decision): the generic
  harness (facts + rules + completeness queries + provenance explanations) is
  the transferable part; the container vocabulary stays domain rule-modules.
  Thin generic core, domain as data.
- **First load: Vector** — smallest kind (one measure, count), exercises
  monoid descent, rank/select, the zipper cursor, and the full
  facts → rules → derive → generated-mapping chain.

Equational boundaries (honest): monoid laws, measure-homomorphism
correctness, kind laws (FIFO) are NOT Horn derivations — oracle property
tests now, the SMT floor later (EL ⊂ Datalog ⊂ solvers ladder).

Operation semantics as delta rules (wave direction for the oracle harness):
in a CoW/DBSP store the semantics of a mutating operation IS the delta it
commits, and a delta is a relation — so an operation SPEC is a rule deriving
the EXPECTED Z-set from the arguments and the snapshot
(`insert(k,v) ⊨ Δ = +{(k,v)} − {(k,old) | old = lookup(k)}`). The imperative
implementation (fast) is then property-checked against the declarative spec
(slow, exact) — the C++-Memoria-as-oracle pattern generalized to
spec-as-oracle. §4.3's DML reduction already carries the formalism. This is
specification and verification of semantics, not synthesis of
implementations.

## 6. The projection bridge

Direction (accepted): **interface → relational projection is generated**,
never "table generates container". For MANAGED kinds the bridge IS the item
pipeline (§1): the Memoria item's handler emits facts, runs Canon via
metacall, hands verdicts to metaprog — projection generation is the item's
compilation semantics, not a separate attribute. Hand-written = kind impl
over bt/pkd (semantics); generated = the projection (mechanics): scan from
cursor, seek from find, range from range-cursor, deltas from the journal.
Attribute-derive (`#[derive_rel_source]`, precedent `#[derive_graph_source]`)
remains for MAPPED EXTERNALS only (§4.4) — native objects projected via
mappings. Container configs remain WritStatic (ADR 0018) — the config space
is itself EDB, queryable by Deem; the loop closes.

## 7. Placement and versioning

- **Fundamental level (interface language + facts + rules + derive) → the
  language tree**, under the abi-diff gate; targets tools-stable early (the
  vocabulary is small and closed — it was derived from a finite requirement
  list, §2).
- **Implementations: OPEN** (decided separately, soon). Two compat contracts
  with different lifetimes: code ABI (lives with releases) vs **on-disk data
  formats** (must outlive releases). The ADR 0018 registry already decouples
  them: formats are identified by `ctr_type_hash`/`block_hash`, not by
  release. Both options stay open — in-tree with hash-locked formats, or a
  separate release train (the SDK-extras line). Prerequisite either way:
  `format_hash` is a first-class fact from day one, so format-compatibility
  questions ("which formats does this build read; what migrates on store
  upgrade") are Deem queries over the registry.

## 8. Non-goals

- No compilation firewall port (ADR 0018 §2.4 stands).
- No rules-with-effects (the DML statement layer §4.3 sits ABOVE pure
  queries; ASP later, if ever).
- Canon does not execute queries, emit code, or check types.
- No byte compatibility promises pre-stabilization (break-freely rule), but
  format identity is tracked by hash from the start.

## 9. Open questions

1. **Heterogeneous measures**: branch summaries are monoid TUPLES
   (max-key × count × sum-len). Tuple type with componentwise Monoid impl, or
   separate measure declarations composed by metaprog (more flexible — adding
   a measure doesn't change the container type — but needs derive machinery
   immediately)?
2. **Entry shape ↔ columns**: a schema-like row declaration from which derive
   gets both the columnar leaf layout and the rel columns — reuse Writ
   `schema` or a Memoria-native declaration (smells like the WritStatic
   config space)?
3. **Cursor genericity**: one generic zipper-state Cursor vs per-kind cursors.
4. **Implementation placement** (§7) — pending the format-versioning
   decision.
5. **Memoria item surface** — one `container` item with kind/entry/measure/
   builder clauses vs an item family; concrete grammar = the syntax spike.
6. **DDL surface** — schema items + store-open materialization vs an
   imperative catalog API (likely both, mirroring the static/dynamic query
   tiers); ALTER-kind → migration-path policy table; merge semantics for
   schema-divergent branches (multi-parent merge × DDL).
7. **When the Canon harness pattern transfers to Nous** (after Vector? after
   the composition wave?).

## 10. Staging

1. Syntax spike: the Memoria item(s) — grammar + AST nodes + sema
   registration + native fact emission (declarations mirror, reasoner-
   readable from day one) — co-designed with the minimal Vector interface.
2. Canon harness: rule modules + metacall invocation + provenance
   diagnostics. First load = Vector: kind decl + count measure → derived
   profile → generated rel-source.
3. **Parity gate** (the load-bearing benchmark): a bound-key seek through the
   generated projection compiles to the same code as a direct shuttle call —
   measured, not assumed. If parity fails, the whole plane pays query tax and
   the design iterates HERE before widening.
4. Map + Queue; write plane as Z-set batch apply (bulk path first) + the
   DML statement layer (§4.3). **Ingestion gate** (second load-bearing
   benchmark, peer of the parity gate): dense-data CTAS throughput, MB/s per
   core, oracle baseline = C++ Memoria.
5. Second wave (Index as access path; composition — where Canon starts
   earning its keep) + the implementation-placement decision.

pkd order of work, re-derived by §2: multi-column/columnar leaf first
(pushdown + Map), branch prefix-sums/rank second (§2.5), SSRLE later (a
single-column compression optimization, not a load-bearing capability).
