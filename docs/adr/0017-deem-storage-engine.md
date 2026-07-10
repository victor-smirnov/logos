# ADR 0017 — the Deem storage engine («data»): persistent absorbed, completed, integrated

Status: ACCEPTED (direction reviewed by the user — four accents incorporated). Date: 2026-07-10.

## 0. Context and mandate

Deem needs a storage engine. The `persistent` subsystem (`stdlib/std/data/persistent/`,
3.1 kLoC) is a well-tested **in-memory** confluently-persistent B+tree map — CoW
path-copy, snapshot DAG with fork/release, container directory, runtime-typed
inline keys/values (DView, incl. variable-width strings), snapshot-stable
iterators, and a working deserialize/revive kernel (`node_arc_from_parts` +
`block_code`) — but everything beyond in-memory is a named seam, not an
implementation: no disk write path, no WAL/recovery, no large/Ref values, no
merge-commit API (the `CommitEntry.parents` Vec is ready; `register_commit`
hard-codes one parent), no tombstone compaction, placeholder concurrency.

Mandate (user): persistent is not wanted as a standalone Logos offering — **Deem
absorbs it fully**, renamed `data`; Deem dictates its direction and completion
plan; the semantic work (ADR 0015 continuation) comes after.

## 1. Decision

`stdlib/std/data/persistent/*` → **`stdlib/std/deem/data/*`**, namespaces
`logos.std.deem.data.*`. The standalone surface dies; the API reshapes for its
one customer — **but through ABSTRACTIONS, not nails** (user accent #1): Deem's
engine code programs against interface traits (a fact-base/store-backend
boundary in the spirit of `IStore`), and `deem.data` is their implementation.
The precedent is fresh: source traits made the walker source-type-blind; the
same discipline keeps the engine storage-blind. Deem's public contract grows
only Deem-level notions (durable fact bases, branches, journal replay) —
B+tree internals stay ABI-excluded like the rest of the engine.

**Durability model (user accent #2): NO WAL — periodic state dump.**
Persistent was designed as a pure in-memory store and stays one: durability =
serializing the store's state to disk at chosen commits and loading it back
(the revive kernel is the load half, already proven). The loss window is
"since the last dump", BY DESIGN — more than adequate for the practical tier
this engine serves. Real transactional storage (WAL, crash consistency,
recovery) is explicitly a SEPARATE future project — the Memoria-in-Logos
port. This ADR must not grow toward it; lightweight is a feature (accent #3:
persistent is deliberately much simpler than Memoria internally — keep it so;
Memoria's MemoryStore serves as a HINT for the commit-history design, not as
a template to reproduce).

Deem's demand ladder (established by survey, evidence in §5) IS the completion
order. Three demand drivers:

- **D1 — durable FactStore.** Fact identity is already serialization-shaped:
  `FsKey` owns its bytes precisely so identity survives transient nodes;
  pointer-bit handles are the only non-durable part, and the store already
  re-canonicalizes. ADR 0013 §6 reserves change-capture producer **(B) —
  CoW/snapshot-diff over persistent Writ — behind the same `ZBatch` seam**:
  "no engine change to adopt it". The socket predates this ADR.
- **D2 — S4 fork/merge (ADR 0015 §5).** Fork must be O(delta) over
  confluently-persistent structures — literally this subsystem, named there as
  "the unfinished `persistent` subsystem". Law I4: fork/merge is EDB-only, IDB
  re-derives incrementally. Merge v0 = `select_one` (total, trivial); v1 =
  `edb_union` with conflicts surfacing as `violation` facts. The pending
  "persistent-vs-copy" decision RESOLVES to persistent by this ADR.
- **D3 — journal durability.** The S3 replay journal (`jn_*`, flat ΔEDB pools +
  control atoms) is a deterministic rebuild primitive by design: initial source
  + journal ⇒ engine state (steps-budgets only). It is a WAL that exists and
  isn't written anywhere. Persisting it doubles as crash recovery.

Secondary demands: the SchemaCatalog must persist/reconstruct alongside any
durable fact base (flat name-keyed columns, late-bound edge targets —
serialization-friendly); ±weights must persist EXACTLY (they are the abelian
state making retraction correct; transient negatives are meaningful);
R4 why-provenance stays deliberately unstored (Design A: reconstruct on
demand) — durable witnesses (Design B) would need an operator-signature change
and is out of scope here.

**Ordering choice: integrate first, disk second.** The engine semantics Deem
wants (fact SET in containers, epochs = commits, fork/merge) are exercisable
over the in-memory backend today; the disk backend then lands beneath an
already-proven API. This unblocks ADR-0015 S4 after slice P2, without waiting
for a single byte of file I/O.

## 2. What moves, what renames, what dies

- Move: `bt/{node,shuttle,cow,descent,mutate,iter}.logos`, `store.logos`,
  `handle.logos`, `descent.genos.md` → `stdlib/std/deem/data/`.
- Namespaces: `logos.std.deem.data.*` → `logos.std.deem.data.*`
  (`…deem.data.bt.*` for the tree layer). Import direction: deem.data imports
  nothing from deem's query layer (data is the bottom of the deem stack);
  `incr.logos`/`incr_rec.logos` import data.
- DView stays where it is (`logos.lang.writ.dview`) — it is a lang-tier value
  view, not storage.
- Tests migrate with a `data_` prefix (`persistent_fork_chain` →
  `data_fork_chain` etc.); the `stdlib/std/data/` directory empties and goes.
- Known stale spot fixed in passing: `bt/descent.logos` claims the
  `subtree_size` SUM column is unmaintained — it IS maintained on
  insert/remove/split; the stale comment and the dead O(N) fallback go.

## 3. Slices

**P0 — absorption.** Mechanical move/rename per §2, zero behavior change,
full suite green. (One sitting.)

**P1 — FactStore over data (D1).** The fact SET (`node`/`key`/`present` +
canonical arena) becomes a `data` container per source relation: key = the
encoded `FsKey` tuple (variable-width string keys are proven), value = the
canonical fact bytes. One store snapshot per FactStore; **epoch = commit**
(`take_delta` boundary ⇒ `Snap::commit`), giving every epoch a queryable,
fork-able identity for free. The SchemaCatalog serializes into a reserved
container of the same store. Change capture stays producer (A)
(instrumentation) initially — the `ZBatch` seam is untouched, so producer (B)
(snapshot-diff between commits) can replace it later without engine changes.
Gate: the full incremental suite green over the new FactStore + an
epoch-history test (facts of epoch N readable after N+k).

**P2 — commit history + merge commits + S4 v0 (D2).** Commit HISTORY is
nearly absent and gets built here (user accent #3, "надо делать всё"), with
Memoria's MemoryStore as the hint and lightweight as the constraint: named
branches/heads (a head = a named moving ref to a commit), commit metadata
(epoch, an optional label), history walking (`log(head)`), and multi-parent
`register_commit` exposed (`Store::merge(winner, losers) -> SnpId` — the
struct is ready). On top, in the reasoner: `fork(cond)` = store fork +
assumption ΔEDB; `select_one` merge = the winner's commit with losers as
extra parents, journaled as `merged(...)`. **This closes ADR-0015 S4's
gate.** Gate: fork-two-branches/diverge/select-one where the merged history
is queryable (`branch(...)`/`merged(...)` as EDB), losers released.

**P3 — journal as a SPECIALIZED container (D3; user accent #4).** The `jn_*`
flat pools become a dedicated append-only container TYPE inside the store —
per the subsystem's own architecture rule, a new container = a new
LeafBody/CFG, not a new tree. The journal's role is REPLAY AND QUERYABLE
HISTORY (the ADR-0015 §4 law), NOT crash consistency (no WAL exists — accent
#2); it is dumped and loaded with the rest of the store's state. Gate: engine
state rebuilt from a loaded store's journal ≡ live engine.

**P4 — periodic state dump / load.** NOT a transactional backend: a
serializer that walks the store (DAG + blocks, page format keyed by the
existing `block_code`) and writes one consistent image at a chosen commit;
load = read + revive (`node_arc_from_parts`, already proven). Dump policy is
the caller's (every N epochs / on demand). Loss window = since the last dump,
by design. Gate: dump at commit N, load into a fresh process, state ≡ commit
N (containers, DAG, journal, catalog).

**P5 — hygiene, on demand.** Tombstone/DAG compaction (release works, slots
leak); large/Ref values via overflow blocks (inline DView covers current fact
shapes — needed when facts carry blobs); SWMR concurrency (the mutex
placeholder becomes real when Deem serves concurrent readers).

## 4. Non-goals

- A general-purpose stdlib collections surface (died with the absorption).
- **WAL, crash consistency, transactional durability, recovery** — the
  Memoria-in-Logos project owns these; this engine dumps and loads.
- The genos codegen layer for descents (orthogonal track; hand-written
  instantiations stay).
- Stored why-provenance (R4 Design B) — reconstruct-on-demand stands; the
  journal gives replay-based witnesses if ever needed.
- Cross-process access; distributed anything.
- Growing toward Memoria's internal complexity — lightweight is a stated
  design property, not a temporary condition.

## 5. Evidence base

Survey reports 2026-07-10 (persistent inventory; Deem storage demands):
FactStore/`FsKey`/`ZBatch` — `stdlib/std/deem/incr.logos:57-315`; engines'
cross-epoch state — `incr.logos:339-457,828-861`, `incr_rec.logos:129-333`;
producer (B) reservation — ADR 0013 §6; S4 semantics + substrate naming — ADR
0015 §5 + I4; persistent API/tests/gaps — `stdlib/std/data/persistent/*`
(20 pass tests + 1 fail guard; disk/WAL/merge/compaction absent, seams named
in `store.logos:6-15`, `handle.logos:22,318`, `bt/node.logos:142`).
