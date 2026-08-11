# ADR 0025 — The batch-cursor plane: one logical data representation for query execution

Status: ACCEPTED (design pinned 2026-08-10, Victor + Claude PAIR).
Three subsidiary forks carry adopted recommendations, marked `[REC]` where they
appear (§4, §10, §11); each is one paragraph to reverse.
Scope: the data plane between storage (heap, Writ, Memoria) and Deem's emitted
query code; the lifetime/pinning contract that makes zero-copy sound. Builds on
ADR 0024 (typed plan IR); feeds ADR 0013 (DBSP) and the streaming-Deem line.

## Problem

Query execution has no logical data representation — it has ten physical ones,
and every consumer branches on which one it holds. Measured on the tree at
`ca6184ab`:

| # | representation | pull | re-pass | random access | free size | order as a FACT |
|---|---|---|---|---|---|---|
| 1 | `&[T]` heap slice (`is_slice`) | indexed loop | yes | yes | yes | recorded nowhere |
| 2 | family `…Walk` (Memoria) | `next()` per row | no (contract) | no | declared (`size`) | exists physically, recorded nowhere |
| 3 | drained iterator → `Vec` + `_sl` | — | yes | yes | yes | no |
| 4 | `__rel_X_sl` (fixpoint output) | indexed | yes | yes | yes | no |
| 5 | `__dl_X_sl` (semi-naive delta) | indexed | — | — | — | no |
| 6 | `VecIter`/`HashMapIter`/`BTreeMapIter` | `next()` | no | no | len exists | BTreeMap's exists physically, recorded nowhere |
| 7 | Writ: prebuilt `Vec<WritEdgeRow>` | indexed | yes | yes | yes | no |
| 8 | join buckets: `HashMap<K, Vec<i64>>` (INDICES into a slice) OR rows | two shapes | | | | |
| 9 | sort: `__ix0: Vec<i64>` permutation over an INDEXED source | 17 sites in rexpr_walk | | | | |
| 10 | query output: always `Vec<elem>` | | | | | |

Consequences, each a recurring defect class: materialization is implicit
prelude text, not an algebra node with a `why`; "read exactly once" is a global
side-channel proof (`plan_mark_single_pass`) instead of a property of a type;
three parallel booleans (`is_slice`/`rel_stream`/`rel_iter`) restate one fact;
`order by` demands `Indexed`, which only slices have, so an ordered container
is drained and re-sorted; every new storage adds a branch to every consumer.
The per-row `Walk.next()` is a container method per row, and a leaf-boundary
crossing re-descends (CoW forbids sibling pointers), so even the streamed path
pays a per-row constant the leaf already amortizes.

## Decision (summary)

One logical object: a **stream of bounded batches of typed rows**.

1. The pull unit is a BATCH, not a row: `next() -> Option<B>`; an EMPTY batch
   is legal (min size 0). Less communication; a leaf, an epoch, a keepalive are
   all one shape.
2. A batch is a typed VIEW over storage, generic over layout WITHOUT dynamic
   dispatch: row-major (`RowsBatch<R>` = `&[R]`) and columnar (`ColsBatch` =
   per-column `PdtCol` wrappers over the leaf's own PdtBuf slots). The emitter
   knows the batch type from the source's declaration and monomorphizes the
   access spelling. (Almost) zero copy: the batch borrows the leaf.
3. Capabilities are orthogonal traits on the stream type, not shapes:
   `Rewind`, `SizedStream`, `OrderedBy<K>`, `Landed<K>`. Existence of a
   capability = trait membership (a type cannot drift from the code — the S4c
   lesson); cost and exactness stay DECLARATIONS (`op … exact`, `size`) because
   types cannot carry them.
4. Materialization becomes an explicit, priced plan node: `Drain`, `Sort`,
   `Arrange`. `Buffer<R>` (today's Vec) is the degenerate stream — one packet,
   implements everything. Read-once stops being a proof and becomes typing: a
   second use of a non-`Rewind` stream is a plan-level error whose repair is an
   inserted `Drain` with its ground.
5. Zero-copy is made sound by a two-part, store-invariant PIN CONTRACT
   (§7): "the snapshot is alive" (altitude: snapshot) + "this leaf's in-memory
   image is resident" (altitude: batch). Cursors are BORROWS — no reference
   counting appears on any path the query compiler emits.

## 1. The pull protocol

```logos
trait BatchStream<B> { fn next(&mut self) -> Option<B>; }
// None      = exhausted
// Some(b), b.len() == 0 = legal: empty leaf, empty epoch, no-progress tick
```

Row-at-a-time is a CONSUMER pattern (the inner loop), not a protocol. The one
emitted scan shape:

```logos
while let Some(b) = s.next() {
    let n = b.len(); let mut i = 0i64;
    while i < n { /* clauses over b @ i */ i = i + 1i64; }
}
```

A heap slice is a stream of EXACTLY ONE batch — the whole `&[T]`. The old
indexed loop is literally the inner loop of the degenerate case, so the
`is_slice` branch does not get adapted, it dissolves; codegen for slice sources
must stay byte-comparable (S1 gate). Drained sources, `__rel_X_sl`, an epoch's
delta: all one-packet streams.

## 2. The batch: two layouts, declaration-selected

* **`RowsBatch<R>`** — `&[R]`: heap collections, fixpoint outputs, deltas.
* **`ColsBatch`** — the Memoria leaf AS IT IS: per-column `PdtCol` wrappers
  around the leaf's PdtBuf slots. FSE column → slice-like indexation over the
  payload; VLE/DST column → `at(i)` computed from offsets+payload on access
  (str views). The column wrapper IS the PdtBuffer in a convenient shell —
  indexation comes with it; no intermediate view-buffer is built.

Read mode vs write mode: the `(allocator, slot)` re-resolve-per-call idiom of
`stdlib/mem/bt/view.logos` exists because a WRITE can resize slots. A read
batch over a pinned leaf (§7) may resolve payload pointers ONCE and cache —
the leaf is immutable (committed snapshot) or BC-stable (no `&mut` possible
while the batch lives). Two modes of one wrapper, not two types.

Layout genericity "без фанатизма": the emitter is a metaprogram — it reads the
batch layout from the source's declaration (the natspec rel entry gains a
layout marker next to `%<ret-ty>`) and spells access accordingly
(`b.row(i).k` / `b.col_k().at(i)`) in ONE emitter function. A third layout is
a new row in that function, not a new branch in every consumer. Selection
vectors (filtered batch without copy) are deliberately NOT in v1 — recorded as
an axis.

**The batch is ONE FORM FOR ALL STORES (Victor, 2026-08-10).** PdtBuffer has
no dyn and needs none: a leaf's column region has the same shape whichever
store resolved the block, so `ColsBatch`/`PdtCol` are one concrete type per
config — which is exactly what lets the layers ABOVE it go dyn (§7b) without
a per-row cost. The batch is the currency that crosses the dyn boundary.

## 3. Capabilities

```logos
trait Rewind        { fn rewind(&mut self); }       // re-land; multi-pass
trait SizedStream   { fn size(&self) -> u64; }      // free, no drain
trait OrderedBy<K>  { … }                           // batches arrive in K order, intra-batch sorted
trait Landed<K>     { fn seek(&mut self, k: K); }   // ordered positioning
```

`[REC]` Existence = trait membership of the producer's return type, asked by
the planner via metaprog trait queries (the seam `join_key_caps` already
anticipates). Cost, exactness, and the operation set remain declarations
(`op <rel>.<col> <cmp> = <fn> [exact]`, `size <rel> = <fn>`) — ADR 0024 S6 is
unchanged by this ADR. Rationale: a declaration can lie about code; a type
cannot; but cost is data-dependent and inexpressible as a type.

The order fact this table finally records: family walks and `BTreeMapIter`
declare `OrderedBy<K>`; `HashMapIter` does not. Immediate harvest: `order by`
on a column whose source already delivers that order is a NO-OP — today it is
a full drain plus a 17-site `__ix0` permutation; `limit` over it becomes a
bounded walk.

## 4. Adapters as plan nodes

* `Drain : BatchStream<B> → Buffer` — today's implicit prelude drain, now an
  AccessPlan step with a ground (`"drained: second use"` / `"order by"` /
  `"rel block"`), visible in `explain()`.
* `Sort : BatchStream<B> × key → Buffer(OrderedBy)` — a sorted buffer is
  `Indexed` + ordered; the `__ix0` shape disappears.
* `Arrange : BatchStream<B> × key → HashIndex<K,R>` — the join build side;
  buckets hold ROWS, the bucket-of-indices/bucket-of-rows split dies.
  (Columnar build side — indices INTO retained batches — is an axis, not v1.)

`Buffer<R>` implements every capability; "a Vec is an eagerly-drained stream,
never the reverse" stops being a comment and becomes the type lattice.
`plan_mark_single_pass` and the `rel_stream`/`rel_iter`/`is_slice` booleans are
deleted in favor of typed uses: consuming a non-`Rewind` stream twice cannot be
spelled; the plan inserts `Drain` and says why.

## 5. Memoria mapping: the cursor moves by LEAVES

Landing constructors are unchanged in role (`__ctr_at_/from_/upto_` — the
pushdown plane of ADR 0024 S6 is untouched); what changes is what they return:
a leaf-batch stream. `next()` hands out the CURRENT leaf's window as a
`ColsBatch` (the `hi` bound trimmed inside the leaf via `lower_bound`), then
descends for the next leaf. One descent per LEAF instead of a container method
per row; n/fanout descents per scan — the asymptotics the CoW no-sibling-
pointer rule already imposes, with the per-row constant gone.

No compilation firewall exists in Logos Memoria — the batch type is the leaf's
own typed view, fully inlined into the query code: zero-copy AND
zero-abstraction at once. The C++ original lifts leaf data through the CF via
lightweight views; here the PdtBuf-backed column wrapper is passed straight to
the emitted loop.

## 6. Other storages

* Heap `Vec<T>`/`&[T]`: one-packet `RowsBatch` stream (§1). `HashMapIter`:
  row batches, no order, point-probe ops as today.
* Writ: the prebuilt `Vec<WritEdgeRow>` is a one-packet stream now; a
  `WritWalk` cursor streaming document-walk batches is a later slice.
* rel fixpoint outputs and semi-naive deltas: one-packet streams; an EPOCH is
  one batch — including the empty one, which is a meaningful tick (§10).

## 7. Lifetime and pinning — what makes zero-copy sound

**The altitude theorem.** CoW + structural sharing give: a block reachable
from snapshot S is LIVE while S's tree shares are held, and IMMUTABLE if S is
committed. Therefore a cursor never pins a BLOCK for liveness; it needs
exactly two properties:

* **existence** — S's tree shares are not released (granularity: snapshot);
* **stability** — no mutation through THIS snapshot while reading
  (granularity: container; purely BC).

**The ladder (RC minimized, BC maximized):**

1. *Stability — zero counters.* Read ops take `&self`, mutations `&mut self`;
   the batch is `#[borrow_carrying]`-tied to the cursor, the cursor to `&C`.
   `insert` while a cursor lives is a COMPILE error (the view.logos idiom, one
   level up). This also legalizes scanning an uncommitted (writable) snapshot:
   BC supplies the stability that immutability would.
2. *Existence — one already-paid count.* The chain batch ← cursor ← `&C` ←
   handle-OWNING-the-snapshot-wrapper extends snapshot liveness transitively
   through the wrapper count that `MemorySnapshot::remove` already documents
   ("the entry stays until the LAST wrapper closes"). No new increments on the
   read path — not per block, batch, or cursor.
3. *Owned cursor — the only place counting is added, opt-in.* A cursor that
   must cross a scope (task handoff, REPL, a query stream ESCAPING its
   caller's scope) clones the wrapper: one pin per cursor LIFETIME. It is
   legal ONLY over a COMMITTED snapshot (`status == SNAP_COMMITTED`, one
   branch at open): it borrows no `&C`, so stability must come from
   immutability. It is the only Send shape. One walk core, two constructors —
   the owned form wraps the borrowed one plus the pin token, never a second
   implementation.
   ⚠ A STREAMING QUERY OUTPUT IS NOT AUTOMATICALLY THIS CASE. A stream a
   `deem` RETURNS crosses the query FN's boundary, not the caller's scope: as
   a borrow-carrying return value tied to the source arguments
   (`-> impl BatchStream<B> + '_` — the shape family producers already have),
   it composes into pipelines under rung 1–2 with ZERO counting. Rung 3 is
   reached only when the stream ESCAPES the handle's scope. §12.

**Why not RC-per-cursor:** a join probe opens a cursor PER OUTER ROW
(`__ctr_at_(c,k)`); counting there is per-row traffic bought for nothing BC
does not already give. Why not BC-only: the owned cases above would become
inexpressible rather than safe.

**The pin contract at the Snapshot seam — store-invariant, two parts:**

| part | altitude | MemoryStore | SWMR | OLTP |
|---|---|---|---|---|
| "snapshot alive" | snapshot | wrapper hold (existing count) | GC ROOT: pinned snapshots are roots for the tracing GC | epoch/lease |
| "leaf image resident" | batch (= leaf) | **no-op (unit type)** — handles ARE addresses, no eviction | cache-page pin: the ARC+BC pair | epoch covers |

RECLAMATION is per-store and NOT part of the contract: MemoryStore's CoW
per-block RC is its memory management substrate and stays (structural sharing
is runtime- and data-dependent — BC cannot express it); disk stores do NOT use
CoW RC as the primary mechanism — they combine an ARC block cache with a
tracing GC. Residency-during-use is the **ARC + BC pair**: BC yields
deterministic pin windows (batch borrow = pin; Drop/advance = unpin — a leaked
pin is unspellable), ARC evicts only among unpinned pages. One pin pair per
batch, i.e. per leaf — never per row or per access. Both pin parts are
METHODS ON THE GENERALIZED `dyn Snapshot` (§7b) — a uniform call whose body
is a no-op in MemoryStore — so the emitted query code is not merely one shape
for all stores: it is one CODE, store-agnostic, with no per-store
substitution left on the query side.

**Requirements this puts on the Memoria API (none exists today):**

1. `Walk`-style cursors hold `*const C` — a convention, checked by nothing.
   Becomes a `#[borrow_carrying]` tie to `&C`.
2. The handle `{snap: *mut MemorySnapshot, ctr_id}` is a raw pointer with no
   tie to the wrapper whose Drop releases trees — AND it names a concrete
   store. Becomes owner of (or borrower from) a wrapper of the GENERALIZED
   snapshot (`dyn Snapshot`, §7b) — the knot of ladder rung 2, generalized on
   day one rather than at store #2. **First slice: without it the borrow
   chain does not close.**
   ⚠ SUPERSEDED AS A DESCRIPTION OF THE TREE (re-measured 2026-08-11): the
   sentence above was stale when written. The generated handle has been
   `{snap: Arc<dyn Snapshot>, ctr_id}` — an OWNER of the generalized wrapper,
   two-tier interface included — since `c03c30c3`, an ancestor of the very
   tree this ADR was measured on. Rung 2 therefore existed already; what did
   NOT exist was rung 1, and it was missing on BOTH sides: the generated
   mutators took `&self` and the generated cursors held raw pointers with no
   recorded borrow, so `insert` while a cursor held the adopted leaf was
   admitted — a dangling view, since `insert` `tree_release`s the old tree.
   Landed with req. 1 below: mutators take `&mut self`, all six generated
   cursor/walk types are `#[borrow_carrying]`, and the refusal is pinned as a
   PAIR (`fail/ctr_family_mut_while_cursor` + `pass/ctr_family_cursor_then_mut`)
   because it is a conjunction — either half alone stays permissive.
   Declared, not closed here: `tree()` / `block()` still hand out raw views
   around the borrow (the privileged tier's hatch), and
   `create_ctr_rc`/`open_ctr_rc` (zero consumers, measured) cannot mutate
   through `Rc` at all under the new signatures.
3. The Snapshot seam vocabulary gains the two-part pin capability (the table
   above), spelled per store by the emitter.
4. `remove()`/future GC currently see only the wrapper count; after (2)
   cursors are covered transitively — no separate mechanism.
5. The owned cursor owes a Drop-unpin (the `*r = new` drop path is closed
   since `df79e048`; the substrate exists).
6. A borrow-carrying cursor must be unable to cross a task boundary — the
   owned form is the only Send shape. The concurrency model has no such
   predicate yet; it must grow one.
7. (§12, PARKED until the `queued` form) A lazy-producer API — a generator on
   pinned fibers or a task feeding a bounded queue — with KILL-ON-DROP:
   dropping the stream must stop the producer and run its frame's drops, or
   the leaf pin leaks with the frame.
8. (§12, PARKED until the `queued` form) Borrows THROUGH the producer: the
   stream handle is borrow-carrying over the deem's source arguments; the
   producer must not outlive them — the producer-side half of requirement 6.

**Known limit, stated:** two handles to the SAME container in one snapshot
alias past BC — the same limit view.logos declares intra-node. Backstop is a
store-level check, not the type system.

## 7b. The dyn boundary (Victor, 2026-08-10: "предусмотри это сразу")

The store-facing surfaces are generalized NOW, not at store #2:

* **`dyn Snapshot`** — for container work every store's snapshot has the SAME
  API; the handle holds a wrapper of `dyn Snapshot` (the `Arc<dyn Snapshot>`
  shape `store_fork_arc` already returns), and the two pin parts (§7) are its
  methods. This SUPERSEDES the container⟂store deferral ("`trait Snapshot`
  stays a minimal marker; capability→trait when store #2 arrives") for the
  INTERFACE; the emitter-parameterized decision (b) survives only INSIDE the
  container family's implementation, where per-store node specifics live.
* **`dyn` container interfaces** — a family's consumer-facing surface (the
  landing constructors, the batch source) hides which store implements it:
  `next_batch(&mut) -> Option<ColsBatch>` is dyn-able precisely because the
  batch is one concrete form (§2). Payoff: a compiled query binds the CONFIG,
  not the store — one binding serves every store carrying that family.
* **The batch itself — never dyn.** PdtBuffer is one shape for all stores;
  the inner loop over a batch is monomorphic and inlined.

**The frequency invariant that prices this:** every dyn call sits at
per-query (open, landing), per-descent-node (`get_child`), or per-LEAF
(`next_batch`, pin/unpin) frequency. **No dyn call is per-row.** The batch
protocol is what holds that line — which is the second reason (after §12's
fiber suspend) that the batch, not the row, had to be the protocol unit.

## 8. Aggregates (later slice)

γ over a batch stream is a single-pass fold — "the aggregate revisits rows
after grouping" (today's materialization ground) is an emitter shape, not a
semantic need, for accumulator aggregates. A fold over `ColsBatch` runs down a
column slice — the vectorizable shape; taken when the two ~720-line aggregate
emitters are unified, not before.

## 9. What this deletes

The `is_slice`/`rel_stream`/`rel_iter` triple (one fact, now a type); the 17
`__ix0` sites (one `Sort` adapter); the two bucket shapes (one `Arrange`); the
drain-vs-stream prelude split (one protocol, `Drain` explicit);
`plan_mark_single_pass` as a proof (typing + adapter insertion); the per-row
container-method walk (leaf batches).

## 10. DBSP seam

A delta stream is `BatchStream<Weighted<R>>`; an epoch is one batch; an empty
epoch is `Some(empty)` — a tick, not an end. `Arrange` is DBSP's arranged
input; a `Buffer` kept across epochs is z⁻¹. `[REC]` The weight does NOT enter
the core row model: ordinary queries pay nothing; the incremental tier rides
the same plane with a wrapped row type.

## 11. Vocabulary home

`[REC]` Capability traits (`Rewind`, `SizedStream`, `OrderedBy`, `Landed`,
`BatchStream`, batch types) live at the language tier next to `Iterator`
(`logos.lang.iter` or a sibling `logos.lang.stream` module) — they are
general-language notions (think ExactSizeIterator in core). The query plane
consumes them; source hub traits (`OrderedMapSource` etc.) move OUT of
`logos.mem.bt.map` into the query plane's own module, closing the recorded
"vocabulary rooted in one supplier" defect.

## 12. Deem-call pipelines and the fiber-backed pull form

Today every query copies its result into a returned `Vec` — correct, poorly
composable: `q2` over `q1`'s result pays a full materialization at the seam.
With batch streams both halves of composition exist on this plane:

* CONSUMING a stream: a `deem` source parameter whose type is a batch stream —
  the S4c path as it stands (membership-checked, planned as non-`Rewind`,
  non-`SizedStream`).
* RETURNING a stream: `deem q(m: &Hs…) -> impl BatchStream<B> + '_` — a
  borrow-carrying return tied to the source arguments. Pipelines within one
  scope are rung 1–2 (zero counting, §7); only an ESCAPING stream is rung 3.

**RETURNING in general is a push/pull inversion, and the inverter is a
QUEUE (Victor, 2026-08-10: fibers deferred — do not complicate yet).** The
emitter produces push-shaped bodies (`__out.push(row)` inside a fused nest).
Between that body and a pull consumer stands a queue — and **a `Vec` IS the
degenerate queue**: eager producer, no bound. So the forms, in order of
arrival:

* `direct` — single-loop streamable plans: the stream is a state struct (the
  Walk shape), no queue at all, no cost. Exists from S5 day one.
* `buffered` — ANY other body, NOW: the body runs as emitted into a `Buffer`,
  and the Buffer is served through the same `impl BatchStream` return type.
  Semantically a `Drain` at the output seam, and the plan records it as one
  (`"buffered: body is not single-loop"`), so the seam materialization the
  pipeline was meant to remove is at least VISIBLE while it remains.
* `queued` — LATER, the upgrade this paragraph exists to reserve: the `Vec`
  becomes a bounded queue fed incrementally; who stands on the producer side
  (a fiber `yield`ing per batch, a task) is decided THEN, not now. The
  surface does not move: all three forms inhabit the same return type, so
  the upgrade swaps an implementation, never a caller.

What survives of the fiber analysis, recorded for the `queued` step so it is
not rediscovered: one suspend per BATCH (= leaf), never per row — batching is
what prices a lazy producer in; an empty batch is the liveness tick for
budgeted/anytime execution; the GOTTPOFF "no fibers in metacall JIT"
constraint is compile-time-only and does not apply to emitted runtime code;
requirements 7–8 (§7) are the substrate the producer side will owe — PARKED
until `queued`, not v1 obligations. Operator-over-batch-queues is the runtime
shape of the DBSP netlist and of Hest's dataflow — Deem pipelines and Hest
converge on this plane when `queued` lands.

## Axes ledger

source-kind × layout(rows/cols) × batch bound(leaf/whole/epoch) ×
capability(4) × adapter(3) × consumer(scan/build/probe/sort/γ/fixpoint) ×
pass-count × order × weightedness × pin(existence/residency) ×
snapshot state(committed/writable) × cursor form(borrowed/owned).
v1 exercises narrow: Memoria map scan/point/range + heap slice through the one
scan shape; every other cell is declared, not silently absent.

## Slices

* **S0 — vocabulary + Buffer + leaf batches.** The traits; `RowsBatch`/
  `ColsBatch`/`PdtCol` (read mode); `Buffer`; the family emits a leaf-batch
  producer beside the per-row walk (both live; the per-row form dies in S1).
  Gate: trait-membership questions answerable from the planner.
* **S1 — one scan shape.** The emitter's three scan branches collapse; slices
  ride as one-packet streams. Gates: slice-source codegen byte-comparable OR
  measured equal (objdump/callgrind — the S4k standard); Memoria scan via leaf
  batches, oracle = same rows against the container's own per-row cursor +
  descent count (leaves, not rows). Requires Memoria API req. 1–2 — req. 2 in
  its §7b form (`dyn Snapshot` in the handle), so the dyn boundary lands here,
  not as a later migration.
* **S2 — adapters as plan nodes.** `Drain`/`Sort`/`Arrange` in AccessPlan with
  grounds; delete the side-channel booleans and `plan_mark_single_pass`;
  `explain()` names every materialization. Gate: the refusal census (why-
  vocabulary) re-derived, no silence where a drain happens.
* **S3 — order and limit as facts.** `OrderedBy` no-op sort; bounded walks.
  Gate: an `order by` over the seek column emits NO `Sort` node and the trace
  says why.
* **S4 — aggregates fold single-pass** (with the emitter unification).
* **S5 — streaming query output + pipelines (§12).** `-> impl BatchStream`
  return surface, TWO forms now: `direct` for single-loop plans, `buffered`
  (Vec-as-degenerate-queue behind the same type, recorded as a Drain) for
  every other body; `queued` deferred with req. 7–8. Pipeline fixture: two
  chained deems, oracle = pull count (direct-over-direct: no seam
  materialization; buffered: exactly one, and the plan SAYS so) + same rows.
  Owned cursor as the escape shape.
* **S6 — WritWalk batches; weighted batches** (the DBSP seam §10).

## Consequences

The execution plane stops multiplying representations: a new storage
implements batch producers + declares ops, and every consumer already
compiles against it. Materialization becomes visible, priced, and justified —
the "justification drifts from mechanism" class loses its habitat on this
axis, because the mechanism IS the justification carrier (types + plan nodes).
The Memoria API changes (handle, seam pin vocabulary) are prerequisites and
are co-owned with the container⟂store plane (ADR 0020).

Cost: rexpr_walk's scan/join/sort/aggregate emission is rewritten across the
slices; the Memoria handle change is ABI-breaking (bump owed as its own step).

## References

- ADR 0024 (typed plan IR; S6 declared operation sets — unchanged here).
- ADR 0020 (container⟂store split; emitter-parameterized store specifics).
- ADR 0013 (DBSP; §10).
- ADR 0023 (node stream-CRUD algebra — the WRITE-side sibling of this plane).
- `stdlib/mem/bt/view.logos` — the borrow-carrying idiom this generalizes.
- `stdlib/mem/pkd/pdtbuf.logos` — the columnar leaf the batches wrap.
- `1f2dabe1` cursor navigation — the walk this ADR turns into leaf batches.
