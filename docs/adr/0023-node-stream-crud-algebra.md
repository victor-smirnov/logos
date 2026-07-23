# ADR 0023 — Node stream-CRUD algebra + fused-kernel metaprogramming

Status: ACCEPTED (design pinned 2026-07-24, Victor + Claude PAIR).
Scope: the node-level mutation machinery of Memoria/Logos (leaf AND branch);
the metaprogramming plane that compiles it per schema.

## Problem

Node mutations exist as hand-unrolled special cases, each hardcoding stream
composition and cell types: `btss_upsert_at` (key+val, u64 FSE),
`btss_upsert_vals_at` (VLE twin), `btfl_leaf_try_insert_n` (N-stream entry +
rank projection + SSRLE), `btfl_branch_insert_child` (branch rows), the split
bodies. Five unrollings of ONE pattern:

> apply to a node a BATCH of per-stream operations: price it exactly as a
> whole, commit atomically, in an order that never invalidates positions.

BTSS-level code must not know what streams MEAN (keys, values, balloons) nor
their cell types. In Memoria/C++ the equivalent machinery (substream
dispatchers/accumulators) is TMP and was never finished cleanly. Logos has
first-class metaprogramming; this code is its prime target — but the ops must
first be closed into an ALGEBRA so ONE generator implements all of it
("всё и сразу"), not a forest of one-off metaprograms.

## The algebra

### Objects

* **NodeSchema** — the node's stream list; per stream a KIND: `FSE(width)`,
  `VLE`, `SSRLE(alphabet)`, `CHILD`. A branch is the SAME shape (MAXKEY +
  CNT columns + child column) — one algebra covers leaf and branch. Carried
  as a WritStatic document (one configuration plane with StoreCfg); lifted
  from today's PdtDescr + Canon verdicts.
* **Pos** — a position within a stream. Cross-stream position LINKS (the
  SSRLE rank projection) are algebra objects, not btfl folklore: the
  structure stream's position homomorphism `proj_s(p) = rank_eq(p, s)`.
* **Datum** — the "object with data": an OWN POD, tuple-shaped (the C++
  precedent). THREE carriers, split by execution path:

  1. **LeafBatchDatum** — the BULK carrier for leaf INS/DEL over n rows:
     columnar, one component per touched stream (FSE cell runs | VLE span
     sets | SSRLE runs), positionally matched to the schema. Executes
     through per-stream PdtBuf + (at 2+ streams) PkdSeq. The simple case.
  2. **BranchDatum** — the branch row form (sep, counts[N], child ref).
     ONE carrier serves both branch batch ops and branch INDIVIDUAL ops.
  3. **Individual ops (leaf)** — a SEPARATE execution path from batches:
     the datum is the TUPLE of the logical schema's Datatype Views — for
     `Map<K, V>` the pair (K-View, V-View); nothing more structured than an
     explicit or implicit tuple, no columnar carrier in between. This is
     the path WITH_ENTRY/UPD ride and where user-fn kernel fusion happens.
     Branch individual ops ride carrier 2.

  Shared requirements: read side is zero-copy views over block bytes; write
  side carries enough geometry to PRICE (lengths for VLE, run shapes for
  SSRLE) without materializing.

### Atoms (per stream `s`)

```
INS(s, pos, n, datum_s)    insert n rows
DEL(s, pos, n)             remove n rows
UPD(s, pos, datum_s)       replace; OPTIMAL form is a function of KIND:
                             FSE   → cell set (O(1), no geometry motion)
                             VLE   → price-on-PRE-remove state → remove →
                                     insert (the no-value-lost law)
                             SSRLE → run edit
```

`R` (read) is the query plane; it shares Pos/Datum but not the two-phase.
Split/merge (`MOV` to sibling) are OUT OF SCOPE here — a separate task.

A GROUP insert is not confined to mutating one leaf: past a leaf's capacity
it BUILDS SUBTREES (fill leaves from the batch, raise branch levels — the
bulk-build path; Memoria's batch input providers). Subtree CONSTRUCTION is
therefore an outcome of the batch atoms; GRAFTING the built subtree into the
host tree remains the BT protocol's job.

Implementation split (deliberate): BATCH atoms (n-row INS/DEL) lower onto
the columnar machinery (PdtBuf per stream, PkdSeq for the structure stream);
INDIVIDUAL updates lower onto the Datatype plane and are compiled (fused)
per schema. One algebra, two execution paths — the interpreter and the
kernel generator each cover both.

### Derived forms

* `ENTRY_INS(key, datum)` — the multi-stream logical-entry insert: one batch
  {INS per data stream at proj_s(p), INS into the structure stream last}.
* `ENTRY_DEL(key)` — symmetric.
* `WITH_ENTRY(key, fn)` — the generalized `with_value`
  (map_cw_api.hpp:181): `fn: Option<State> → Option<State>` where State is
  the container's LOGICAL entry state. One user function covers the quad:
  None→Some (insert), Some→Some' (update), Some→None (remove), None→None
  (no-op). UPSERT is its special case. **The fn's signature is dictated by
  the algebra + the container's logical schema** — the factory derives it
  (Map: Option<ValueView>; Multimap: the value-run view; etc.).

### Laws

1. **Exact pricing** — price(batch) = Σ price(atom), computed by the SAME
   prepares that commit. No estimate formulas, ever.
2. **Atomicity** — batch commits iff price ≤ free_space; no partial node
   states. The VLE replace discipline (price the insert on the PRE-remove
   state, so the post-remove commit cannot fail) is the general shape of
   conservative pricing for compound atoms.
3. **Position stability** — commit order never invalidates later positions:
   data streams before the structure stream; per-stream positions are
   independent images of the projection.
4. **View re-resolution** — every commit re-adopts its views (the VecCtr
   idiom: sibling slots move). The EXECUTOR owns this; generated code emits
   the re-resolutions and the reindex points.
5. **Projection homomorphism** — the structure stream's rank projection maps
   entry space into every stream's position space; batches are specified in
   entry space and lowered to per-stream positions by the algebra.

## Position: the bottom of the physical query plan

In Memoria/C++ composition was first-class AT THE API: most operations
return a cursor/iterator into the tree so the next op (typically
find+update) can sometimes skip its find part — which made the iterator
system and the container APIs very complex. Logos avoids that complexity BY
DESIGN: containers are driven primarily through DEEM (its query language).
This algebra is therefore LOW-LEVEL — it corresponds to the lowest tier of
the physical query-execution plan.

The division of labor:

* The algebra ITSELF carries only SIMPLE fusions — the find&update class
  (WITH_ENTRY is exactly that shape).
* Exploiting local DATA for query optimization is the OPTIMIZER's function
  (the Deem query engine, sitting above); containers serve it through a
  STANDARD API. The container-level contract: the container is handed a
  QUERY PLAN and returns an ITERATOR over the plan's result set.
* What the CONTAINER level owns is optimization by local STRUCTURE: the
  same algebra op admits different optimizations per PHYSICAL layout —
  `Map<u64, V>` differs deeply from `Map<str, V>` in implementation, and
  the u64 case admits far more (fixed cells, direct compares, O(1) UPD)
  than the str case (VLE geometry). THESE structure-driven optimizations
  of the algebra are made at the container level — the kernel generator's
  axis of specialization — and stay invisible above the standard API.

This makes the work a CO-DESIGN: the Deem query engine over Memoria
containers and the container API itself are designed together — the algebra
is their shared instruction set, kernel fusion (below) is the plan's
operator fusion specialized per schema.

## Kernel fusion — the metaprogramming plane

The executor of a batch against a concrete NodeSchema is what C++ TMP
unrolled. Here a METAFUNCTION reads the NodeSchema document and quote-emits a
SPECIALIZED kernel: the right prepare variant per stream kind, the summed
price, the guarded commits in law order, the re-resolutions.

The decisive advance over TMP: **user functions fuse into the kernel**.
`WITH_ENTRY`'s `fn` (and any decide/merge logic) is spliced INTO the
generated kernel body — user logic and schema-optimal mutation compile into
one straight-line kernel, no indirection at the decision point. Composition
of such functions with schema-directed specialization is, deliberately, a
small COMPILER inside Memoria; the quote system is its backend.

The REFERENCE INTERPRETER of the algebra (dynamic KIND dispatch, fn-ptr
user functions) defines the semantics, serves as the differential oracle for
generated kernels, and remains the execution path for interpreter-mode Deem.
Semantics is defined by THIS algebra: where legacy hand-unrolled behavior
disagrees, the legacy is discarded, not emulated.

## Staging

* **S1** — description layer: Op/Batch/NodeSchema/Datum types (data only).
* **S2** — reference interpreter + law tests (pricing, atomicity, rejection
  paths, projections); existing surfaces re-expressed as batches.
* **S3** — the kernel generator (metafunction over NodeSchema), differential
  gate vs the interpreter across schemas × batches; fusion of user fns.
* **S4** — surface rebase: btss_upsert_* die into algebra instantiations;
  btfl entry ops and branch row ops become batches; `with_value`-style
  WITH_ENTRY lands on the Map/Multimap boundary traits.

Non-goals here: split/merge (separate task), cross-node composition (stays
in the BT protocol skeleton, which sequences node-local batches).
