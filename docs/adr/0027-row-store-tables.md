# ADR 0027 — row-store tables and indexes: Memoria as an OLTP engine

Status: ACCEPTED as direction (Victor + Claude PAIR, 2026-08-17). One
sub-decision is OPEN and marked as such; it changes structure, not bytes, and
the slices that do not depend on it can start.
Scope: two new container kinds (clustered and heap tables), the `Row` /
`TableMetadata` pair they store, the container-registry and dispatch extensions
they need, and the Deem-side consequences. Builds on ADR 0020 (container plane),
0023 (node-stream CRUD), 0024 (typed plan IR), 0025 (batch/cursor plane).

## Problem

Memoria today can hold a document collection — `Vector<Writ>` is roughly
MongoDB's model — and cannot hold a table. Every row carries its own schema, so
a fixed-schema workload pays document overhead on every row, and Deem has no
source whose relation is wider than two columns.

**MEASURED, not asserted.** A Writ row is an `ObjectMap`: open addressing,
initial capacity 8, grows x2 above load 0.75, slot = 2 AnyVal = 16 B, header
`{count, cap, data}` = 24 B.

| fields | geometry | bytes/row | payload | overhead |
|---|---|---|---|---|
| 4 | cap 8 | 24 + 128 = 152 | 32 | x4.75 |
| 6 | cap 16 | 24 + 256 = 280 | 48 | x5.8 |

And the decomposition is NOT "the schema is repeated": key strings are already
interned per arena, so a field costs one 8-byte key reference, not its name. What
repeats per row is the 24-byte header, the key word per field, and — the
dominant term — **the empty slots of a per-row hash table**: a power-of-2 table
at load <= 0.75 wastes 25-50%. Every row carries a hash index over its own
fields.

## Why not the packed column buffer

`PdtBuf` (the port of `PackedDataTypeBufferT<DataType, Indexed, Columns,
Ordering>`) is a vectorised columnar format oriented to READS, and it is the
wrong substrate for a write-oriented wide table for three reasons, two of them
hard:

1. **A hard width ceiling.** `pdtbuf_format` asserts
   `slots <= 64` ("slot count exceeds the PkdAlloc bitmap word"), and a VLE
   dimension takes TWO slots. So ~64 FSE columns or ~32 VLE columns — a wider
   table does not format, at all.
2. **It must fit one block.** At the default `leaf_bytes = 4096`, a 2-column map
   holds ~250 rows/leaf; a 50-column table at 8 B/cell holds ~10, before
   per-dimension metadata and the SUM index. Fanout collapses and every insert
   splits.
3. **Write cost is linear in width.** One row insert is N dimension insertions,
   each pricing and shifting its own payload under the exact-prepare law
   (`prepared_bytes == consumed_bytes`, asserted per mutation). The row store
   writes one byte range, one prepare/commit.

`Row` and `TableMetadata` are zones, not packed buffers: no block-fitting
constraint, no slot ceiling.

## The substrate is already named

`stdlib/mem/bt/btfl.logos`, the N-DataStream free-layout B+tree prototype
(Memoria `bt_fl`), states its own client list:

> bt_fl stays the reusable substrate (**vector-of-structs, queue, table-rows**
> sit on the same prototype without multimap baggage)

Multimap is its only client today. Both tables here are further clients, so
neither is a new subsystem: records of N streams ordered relative to each other,
layout carried by an SSRLE symbol sequence as the last stream of every leaf, a
cut at any structure position projecting into each stream via `rank_eq`. A
record whose runs do not fit a leaf is the prototype's ordinary case — which is
exactly the "zone spans blocks" mechanism the row store needs, already built.

---

## D1 — two tables, not one

**`ClusteredTable`** — over `Multimap`-shaped bt_fl: stream 0 is the ordered
navigation key (MAX measure in branches). Every clustered table has a primary
key.

**Heap table** — keyed by a SYSTEM identity, not by ordinal. An ordinal is a bad
row identifier: it does not survive a deletion. The fix is a system `u64` column,
an OID, so the heap table is logically `Map<u64, Row>` — a monotone synthetic key
where the clustered table has the user's. Insertion takes `max(OID) + 1`, which
means appends at the end only; for a non-clustered table that is not a
constraint.

⚠ **BOTH TABLES ARE THEREFORE ONE SHAPE**, and the emitter arm's parameter is
narrower than D1 first said: stream 0 is an ordered key in both cases, and the
only difference is WHO SUPPLIES IT — the user's primary key, or a generated OID.
That makes S5's "one arm, parameterised" nearly free rather than a discipline to
maintain.

⚠ **`max(OID) + 1` IS CHEAP BUT MUST NOT BE DERIVED FROM THE TREE.** Cheap:
stream 0 already carries a MAX measure in branch nodes for navigation, so the
maximum is O(1) from the root — the measure the clustered table needs anyway is
the one that allocates OIDs. But an empty table has no maximum, so deleting the
last row would restart the sequence and RESURRECT retired identifiers, which is
exactly what an OID exists to prevent. The next OID is therefore state, and it
belongs in the container's metadata document (D5) where it costs nothing — the
same discipline as D3's monotone column IDs, for the same reason.

**The base container is `Map<K, V>`** (D8), with one nuance: for `V == Writ` it
is a Map over **BTFL**, not over BTSS as today. That is expressible without a new
mechanism — the factory generates per CONFIG and the config carries the value
type, so the substrate is chosen by the same arm that chooses the key supplier.
⚠ It must be the SAME branch point, not a second one: ADR 0026 measured what
divergent branches in this emitter cost (a `bool` read-back rule present in three
sites of four, and a whole container kind that could not be built).

The two differ in stream 0 (ordered key vs ordinal) and share everything else:
the row format, the metadata pair, the assembly path, the Deem relation.

## D2 — `Row` is a structure inside Writ, not a container

A dedicated container for `Row` would cost its own root overhead. Instead `Row`
is an object type inside a Writ document and `TableMetadata` is an ordinary Writ
container (a map over columns). The price is uniformity's: 16 bytes per row — the
8-byte document header plus the tag's padded 8-byte slot (D4). Everything else — arena, `AnyVal`,
tags, `ArenaString`, arrays, nested objects, the copy/clone/serialize kernel,
self-relative anchoring — is reused unchanged.

Access is through the metadata, by design:

```logos
let meta: TableMetadata = table.get_metadata();
let row:  Row           = table.get_row(0);
let v                   = meta.get(row, 0);   // value of column with ID 0
```

## D3 — the `Row` format

Same IDEA as `TinyObjectMap` — presence bitmap, popcount indexing, values
compacted in key order — and a separate object type, not a generalisation of it.
(TOM's `schema_type_code_` word discriminates schemas built OVER a TOM —
`LirArenaRoot`, `ImportTable`, AST node classes, `match` variant dispatch — 60+
live sites. A row needs no discriminator: it has one schema and it is external.)

```
[DocumentHeader]  8 B  — an at-rest AnyVal (D4: it stays 8)
[TypeTag]         8 B  — variable-length, padded to the 8-byte boundary
[Row, 8-aligned:]
    atom 0        : bits[0:47] presence bitmap | bits[48:63] size   (<= 64K columns)
    atoms 1..M-1  : presence bitmap
    AnyVal[]      : inline, compacted in column-ID order, present columns only
                    (large values ride the Ref arm into the same arena)
```

Consequences, each load-bearing:

* **`M` is derived from the ROW's own highest set bit, not from the table's max
  column ID.** Column IDs only grow and are never reused, so a long-lived table's
  ID space is sparse and high; sizing `M` from the table would make every row pay
  for the schema's whole history. Reading a column above a row's own maximum is
  "absent" by bounds.
* **No `version_tag`.** Monotonic IDs plus NULL-as-absent give schema evolution
  directly: a row predating a column simply has no bit for it, a dropped column
  is marked dead in metadata and its stale bits are ignored. This holds under one
  rule, which is therefore part of the format: **a column's type change allocates
  a NEW ID.** (Values are self-describing, so a reader would SEE a wrong type —
  but reuse must still be forbidden.)
* **NULL is free.** `AnyVal`'s zero word is null and the bitmap bit is 0, so an
  absent value occupies nothing. What the bitmap and the null word distinguish —
  SQL NULL versus "this column did not exist in this row's era" — is a semantic
  decision for the metadata, not the format.
* **Self-description survives.** `AnyVal` is one 8-byte word: `word & 1 == 1` is
  a Pod carrying a 7-bit type code and an i56 value; otherwise a self-relative
  Ref to a tagged object. A scalar column carries its own type inside the word we
  already pay for — no per-value `TypeTag`, no per-row schema redundancy, and a
  row remains decodable without its metadata. Metadata carries names, order,
  declared types and liveness — not decodability.
* **Projection becomes meaningful.** Column i's slot is `popcount(bitmap below
  i)`, i.e. `ceil(i/64)` popcounts — not a decode of columns 0..i-1. `select (a,
  b)` over a 50-column table does not cost `select *`. No Deem source has had a
  reason for projection pushdown before; this is the first.

| 4-column row | bytes | payload | overhead |
|---|---|---|---|
| `ObjectMap` today | 24 + 8x16 = 152 | 32 | x4.75 |
| **`Row` as a Writ structure** | 8 hdr + 8 tag + 8 atom + 32 = **56** | 32 | **x1.75** |

(With D4's withdrawn 4-byte header it would have been 44 / x1.375. The 12 bytes
are the price of not forking the document format; see D4.)

## D4 — WITHDRAWN: `DocumentHeader` stays 8 bytes

An earlier draft shrank `DocumentHeader` from 8 bytes to 4, on the reading that
it holds a root OFFSET: the tag would then fit the remaining 4 bytes of the same
8-byte slot and a row would save 8 bytes.

**It does not hold an offset. It holds an `AnyVal`** — checked on both sides:

```cpp
struct DocumentHeader { AnyVal root; };      // include/logos/writ/document.hpp
```
```logos
pub root: UnsafeCell<i64>,                   // stdlib/lang/writ/container.logos
// "the top-level WAny stored as its raw 8-byte word (0 = null/unset)"
```

and an `AnyVal` has three arms: null, a **Pod** carrying a 7-bit type code with an
i56 value INLINE, and a self-relative Ref. So a document whose root is a scalar
holds it in the header with no arena allocation at all, and 4 bytes cannot carry
an i56.

Shrinking would therefore mean choosing one of: kill the Pod arm (every scalar
root costs an allocation plus a tag — a silent regression across all of Writ),
shrink Pod to ~i24 (a third behaviour class where there is one today), or fork
the format so only `Row` has the short header (no breakage elsewhere, but two
document shapes to tell apart).

WITHDRAWN. The 8 bytes are not worth any of the three today. `Row` pays the
ordinary Writ overhead — 8-byte header plus the tag's 8-byte slot — and the
format question can be reopened if the per-row bytes ever start to matter.

## D5 — container metadata, three modes — for EVERY container

**A container may carry a `CtrMetadata` document, and what the container IS is a
fact in that document, not a property of its type.** If the metadata is a
`TableMetadata`, the container is a Deem table; the table's kind — clustered or
not — is a field there too. So "is this a table?" is answered by READING, which
is the same discipline `ctr_meta` already serves ("needs NO compile-time
knowledge of the container's type: `container_ids()` + this is a full catalog of
an opened store").

⚠ This collapses D1 one step further. The two tables were one emitter shape, then
one library shape; they may be ONE WRAPPER reading a discriminator, since
clustered and heap differ in who supplies the key and that is a metadata fact.
Decide when S6/S7 are written, not before — but do not build two types out of
habit.

The mechanism is not the table's. Every container gets the same one: a small Writ
document that lives in the root block while it is small and spills to its own
container when it is not. For a table that document happens to be
`TableMetadata`; for anything else it is whatever that container needs. The
table's requirement is therefore satisfied by generalising a facility the store
already half has, not by adding a table-specific one.

1. **In the root block** — for small metadata. This mode largely EXISTS: a
   container root already carries two Writ metadata slots, the schema slot
   (`ctr_publish_meta`) and a client slot (`set_ctr_user_meta`); both survive
   root changes and the block grows to fit.
2. **In an associated container** (`Vector<u64>`-shaped), fitting entirely in a
   leaf, read in place from it.
3. **Same, assembled into a buffer/zone** when it spans leaves.

**DECIDED: the mode is DERIVED FROM SIZE, never recorded.** The input is a
`TableMetadata` (a Writ container); its size chooses where it goes. Nothing
stores a mode number, so nothing can drift out of agreement with the bytes.

The reader needs no mode field either, because the root slot's CONTENT
discriminates: it holds either the metadata document inline (mode 1) or a
reference to the container holding it (modes 2/3) — the same self-description
the rest of Writ relies on, applied one level up.

Mode 1's threshold is not a new constant, it exists: `META_MAX_BLOCK` = 1 MB
(`stdlib/mem/bt/meta.logos`), "nodes grow by doubling up to 1 MB", and the
comment there already names the overflow destination — "a metadata fragment past
that belongs in a store-side `Map<str, Writ>`, not in the node". Mode 2 is that
sentence, implemented.

Modes 2 and 3 are not a stored property either: they are the SAME
in-place-versus-assemble decision as `RowRef`/`RowBuf` (D10/D11), taken at read
time from whether the document fits one leaf. One mechanism, three users — rows,
metadata, and any future large value.

⚠ 1 -> 2 promotion is automatic but STRUCTURAL: it allocates a container and
registers it in the dirmap with `top_level = 0` (D7). A metadata write can
therefore create a container, and the promotion must be transactional with the
write that triggered it — a half-promoted table has its metadata in neither
place.

## D6 — metadata is cached in a per-snapshot instance pool

Re-reading metadata on every table access is too expensive. When a container's
wrapper is first opened its metadata is read and kept IN the wrapper; opening the
same container again in the same snapshot reuses it.

That requires a **bijection `CtrID -> wrapper`**, held by the snapshot wrapper.
Without it "the same container" is not a well-defined thing and two wrappers
would hold two copies of one document.

THE ORACLE HAS THIS, and the port should follow it
(`~/cxx/memoria/containers/include/memoria/core/container/ctr_instance_pool.hpp`):

```cpp
std::unordered_map<CtrID, CtrRefHolder*> instances_;          // the bijection
CtrPtr get(const CtrID& ctr_id, StoreT store)                 // reuse or miss
CtrPtr put_new_instance(const CtrID&, std::unique_ptr<CtrT>&&)
```

held per snapshot as `std::shared_ptr<CtrInstancePool<Profile>> instance_pool_`
(`stores/.../memory_cow/common/snapshot_base_cow.hpp`), constructed in the
snapshot's constructor.

⚠ THE DETAIL THAT MAKES IT A CACHE rather than a registry: `release_ctr_instance`
does NOT erase the entry when the last handle dies. It moves the holder,
detaches the instance from the store, and keeps it in the map; a later `get`
finds `!is_in_use()` and REATTACHES it. So reopening a container in the same
snapshot is a reattach, not a re-read — which is precisely the property that
makes the metadata read amortised.

WHAT THE PORT MUST DECIDE, because Logos is not C++ here:

* **Aliasing.** Our container handle is a VALUE (`{snap, ctr_id}`). A bijection
  to a shared wrapper introduces aliasing, and two `&mut` paths to one wrapper is
  exactly what the borrow checker refuses. The shape that survives: the POOL owns
  the wrapper and the cached metadata, the handle stays a value carrying a
  counted reference, and every mutable path goes through the pool. C++ solves it
  with `shared_ptr`; we cannot copy that verbatim.
* **Invalidation.** Metadata evolves APPEND-ONLY (monotone column IDs, D3), so a
  mutable snapshot refreshes a cached document rather than invalidating it. No
  reader can be looking at a column that stops existing.
* **Scope.** The pool is PER SNAPSHOT, which is not an implementation detail but
  the correctness condition: a fork must see its own metadata, and a cache shared
  across snapshots would hand it the parent's.
* **The API already anticipates this.** `create_ctr_rc` / `open_ctr_rc`
  (`stdlib/lcm/canon/metaclass.logos`) return `Rc<C::Handle>` on a thread-local
  Rc plane, and the comment beside them names the destination outright: "the
  design destination is a pool of ready handles inside the snapshot (Drop ->
  pool — open question), which these signatures already permit". So the pool
  needs no API change, and the Rc plane answers the aliasing question above by
  construction: the pool owns, handles are counted clones.
  The oracle answers the flagged open question too — `release_ctr_instance` does
  NOT erase on last release; it detaches and keeps the entry, and the next `get`
  reattaches.
* **What it saves, measured against the current path.** `ctr_meta(ctr_id)` today
  is `root_of(ctr_id)` — an O(log D) dirmap descent — plus reading the document
  out of the root block, on EVERY access. That is the per-call cost the cache
  removes.
* **It is also the applied layer's hook (D8).** An application-level wrapper —
  a table, an index — needs somewhere to keep derived state, and the container
  wrapper is where it belongs: the bijection already makes "the same container" a
  single object, so anything hung on it is shared by construction and released
  with the snapshot. The wrapper therefore offers a general interface for that,
  not a table-specific field. `TableMetadata` is then simply the first client of
  a facility every applied wrapper will want.

## D7 — `top_level` in the container registry

The registry is the **dirmap**: a CoW `Map<CtrID, root>` whose leaf values are
container roots. Listing must distinguish roles — `ClusteredTable` is top-level,
its metadata container is nested — so the registry grows a bitmap column, one
bit per container, `1` = top-level. Memoria upstream has this.

⚠ The dirmap is a `Map`, and wave-0 admits **exactly two** entry columns
("wave-0 `kind ordered_map` supports exactly two entry columns"). A third column
is the same declaration-plane restriction the tables themselves need lifted, so
either it is lifted first, or the bits are packed into the existing value cell,
or the flags live in a parallel container. Lifting it is the honest fix and is
shared work with D9.

## D8 — the applied layer lives ABOVE Memoria, and Memoria does not learn about it

An earlier draft of this ADR extended block-level dispatch so a superstructure
could reuse a base container and still be recognised: a second type code beside
the base's, in the block header or in root metadata.

**That is rejected, and the oracle rejected it first.** The pair that once
carried it (`container_type_hash_` for the base, `owner_type_hash_` for the
applied) is gone from Memoria, and the reason is not an accident of refactoring:
pushing APPLIED containers down to Memoria's level is overengineering. Memoria's
business is base containers — `Map<K, V>` and the rest — and what a container is
FOR belongs to the layer that has a purpose.

So:

* `ClusteredTable` and the heap table are APPLICATION-LEVEL wrappers over a base
  container and its cursors. Memoria stores `Map<K, Writ>`; that it is a table is
  not a fact Memoria holds, checks, or dispatches on.
* No second type code. No block-header change. No dispatch extension. The
  question that OPEN-2 asked — whether dispatch ever starts at a block with no
  root in hand — dissolves, because there is no applied code to recover.
* Where an applied wrapper needs to cache state, it does so through an interface
  ON the Memoria container wrapper (D6), which already exists to hold exactly
  that: the per-snapshot instance pool makes "the same container" a single
  object, so state hung on it is shared by construction and dies with the
  snapshot.

⚠ CONSEQUENCE THIS ADR MUST FOLLOW THROUGH ON, and it is structural: if the
table is a wrapper rather than a container kind, then it needs no `kind` arm in
`container_item.logos` at all. What the factory must learn is only the base —
`Map<K, V>` over BTFL when `V` is a Writ — and the table becomes library code
plus a hand-written Deem source declaration, exactly the shape
`impl OrderedMapSource for BTreeMap` already has for a hand-written type. That
shrinks S5 to the base arm and turns S6/S7 into library slices. It also removes
the drift surface ADR 0026 warned about, because no new emitter arm appears.

## D9 — the relation widens

No source today declares a relation wider than two columns: `OrderedMapSource`
is `entry(key, val)`, `PositionalSource` is `row(pos, val)`, and the container
emitter states the wave-0 restriction outright. A table's relation is
`entry(c1..cN)`. This ADR needs that lifted, and lifting it also closes ADR 0026
F6 and F8 (`order`, `unique` — capabilities a source has and cannot state), so
the declaration vocabulary should grow once, for all of them:

    rel entry(c1: T1, …, cN: TN);
    op  entry.<col> <cmp> = <producer> exact;
    size entry = <fn>;
    order entry = <col>;
    unique entry = <col>;          -- new; closes 0026 F8
    nullable entry.<col>;          -- new; the table's own need

## D10 — a key's value sequence IS the row's buffer

For a clustered table, `K -> sequence` is not "many rows per key": the sequence
is the BUFFER holding that row's Writ document.

* **fits a block** — the in-block buffer is used DIRECTLY, no copy. It is 8-byte
  aligned, which is exactly what a Writ document needs: objects are 8-aligned and
  every reference resolves self-relatively from its own address, so the document
  is readable where it lies;
* **spans blocks** — it is read into a memory buffer first, and only then is it a
  document.

So "a zone split across blocks" needs nothing new — it is bt_fl's existing run
mechanism — and the two cases ARE D11's `RowRef` / `RowBuf`, which makes that
split a consequence of the storage model rather than a choice this ADR makes.

## D12 — a secondary index points at the row's IDENTITY

For a clustered table a secondary index entry is `indexed value -> PRIMARY KEY
value`, not a physical address: rows move under CoW and split, so nothing else is
stable. A lookup is therefore two descents — the index's, then the table's — and
that is the cost of the design, accepted.

The heap table takes the same shape with its OID in place of the primary key, so
indexes are uniform over both tables: `key -> identity`, where identity is
whatever stream 0 holds. One mechanism, and it needs no knowledge of which table
kind it indexes.

## D11 — four consequences that are decisions, not details

**Row-view provenance.** "Fits a block — read in place; spans — assemble into a
buffer" gives one `Row` behind two ownerships: a borrow of a leaf, or ownership
of a temporary. One type over two provenances is the class the D1 borrow-checker
arc spent thirteen rounds on and the reason `#[borrow_carrying]` exists (ADR
0025). DECISION: two types — `RowRef` (holder borrows the leaf, residency
applies) and `RowBuf` (holder owns the assembled arena) — with an explicit
constructor from the first to the second. The fast path stays a borrow and says
so in the type.

**Two mutation protocols meet.** A Writ object grows by appending to its arena; a
row inside a leaf grows under the container's byte budget with `prepared ==
consumed`. This is the one place where reuse ends and real work begins; it is its
own slice and does not block the format.

**What a batch is.** ADR 0025's pull unit is a LEAF and its access is columnar
(`key_at` / `val_at`). For a row store a batch is a set of ROWS and `val_at`
becomes "address the row, popcount to the column". The declaration does not move
(`rel entry = <producer>`), but the natspec `b` flag comes to mean a second
thing. Decide before the emitter arm exists — ADR 0026 measured what happens when
emitter arms diverge (a `bool` read-back rule present in three sites of four,
and `kind vector` with a bool column could not be built at all).

**A third emitter arm costs drift, not just code.** Each `kind` is an arm in
`container_item.logos`, and arms demonstrably drift. Both tables should share one
arm parameterised by stream 0's role, not become two.

---

## OPEN — one sub-decision

**OPEN-1 — `OrdDataType`: closed set or dynamic tag?** The mechanism stores a
value of an arbitrary type from a given set (comparable ones) as the container's
value, with the concrete type recorded in metadata. Either (a) the set is CLOSED
at compile time and we generate a dispatch over it, the metadata selecting an
arm — an extension of the existing per-config generation, where Deem's relation
columns stay statically typed; or (b) the type is fully dynamic and metadata is
the only source — which changes the shape of Deem's relation and is a different
piece of work. Almost everything downstream follows from this answer.

---

## Slices

Dependencies are real; the order below is the dependency order, and the two
groups marked ∥ are independent of each other.

| # | slice | depends on | note |
|---|---|---|---|
| S1 | `Row` object type + popcount access | — | format only; no container |
| S2 | `TableMetadata` as a Writ container | — ∥ | ordinary document; column IDs monotone |
| S3 | `RowRef` / `RowBuf` + the assembly path | S1 | D11; borrow-carrying |
| S4 | relation widening (`entry(c1..cN)`, `unique`, `nullable`) | — ∥ | also closes 0026 F6/F8 |
| S5 | base arm: `Map<K,V>` over BTFL for a Writ value | S1, S4, OPEN-1 | Memoria-level; no table kind |
| S6 | `ClusteredTable` wrapper | S5, S13 | LIBRARY, not a kind; PK = the base's key |
| S7 | heap-table wrapper (`Map<u64, Row>`, OID) | S5, S13 | LIBRARY; next-OID in metadata |
| S8 | metadata modes 2/3 | S2 | D5 |
| S9 | dirmap `top_level` | S4 or a packing decision | D7 |
| S11 | mutation protocol reconciliation | S3, S6/S7 | the real work |
| S12 | indexes | S6, S7 | D12; `key -> identity`, two descents |
| S13 | instance pool (`CtrID -> wrapper`) + metadata cache | S2 | D6; per snapshot |

## What this ADR does not do

It does not retire `Vector<Writ>`, and it does not compete with it. Since `Row`
is a Writ structure, the document container and the table share the ENTIRE value
machinery — they differ in the root object type and in where the schema lives.
Heterogeneity stays available where it is wanted and stops being charged to rows
that do not want it, from one codebase.

**It does not make a table a Memoria concept** (D8). Memoria gains one base
capability — `Map<K, V>` over BTFL when the value is a Writ — and nothing that
knows the word "table". The tables are wrappers above it.

It does not specify indexes beyond D12 and S12. A secondary index needs the
relation widening (S4) and the registry roles (S9) before it can be stated at
all, and it too is a wrapper: `key -> identity` over a base container.
