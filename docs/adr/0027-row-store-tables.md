# ADR 0027 — row-store tables and indexes: Memoria as an OLTP engine

Status: ACCEPTED as direction (Victor + Claude PAIR, 2026-08-17). Two
sub-decisions are OPEN and marked as such; they change structure, not bytes, and
the slices that do not depend on them can start.
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

**Heap table** — the same principle without a key: rows addressed by ordinal.
Not the existing `kind vector`, which is btvec-backed with FSE64 cells and is
for small fixed-size elements; this is bt_fl's "vector-of-structs" client — a
`Vector<T>` for LARGE `T`, where an element is a byte range that may span
blocks.

The two differ in stream 0 (ordered key vs ordinal) and share everything else:
the row format, the metadata pair, the assembly path, the Deem relation.

## D2 — `Row` is a structure inside Writ, not a container

A dedicated container for `Row` would cost its own root overhead. Instead `Row`
is an object type inside a Writ document and `TableMetadata` is an ordinary Writ
container (a map over columns). The price is uniformity's: 8 bytes for the
document header plus the type tag per row. Everything else — arena, `AnyVal`,
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
[DocumentHeader]  4 B  — the root, as u32 (see D4)
[TypeTag]              — fits the remaining 4 B of the first 8-byte slot: free
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

| | bytes/row | payload | overhead |
|---|---|---|---|
| `ObjectMap` today, 4 fields | 152 | 32 | x4.75 |
| `Row` as a dedicated container | 48 | 32 | x1.5 |
| **`Row` as a Writ structure** | **44** | 32 | **x1.375** |

## D4 — `DocumentHeader` shrinks to 4 bytes (BREAKING)

`DocumentHeader` is `{ AnyVal root; }` at offset 0 — 8 bytes. Objects are
8-aligned and a `TypeTag` is written in the bytes immediately before the object,
so a row-as-document pays 8 (header) + 8 (tag slot, padded to the boundary) = 16
B before its map begins. With a 4-byte header the tag occupies the remaining 4
bytes of the same 8-byte slot: **8 bytes saved per row**, and the tag becomes
free.

The root offset becomes u32, capping a document at 4 GB; a larger document must
allocate its root below 4 GB. For rows this costs nothing — a row's arena is
bounded by the leaf.

⚠ This is a wire and disk format change, pinned by the ABI layout gates
(`scripts/abi-check.sh`, the `.abi-layout` artefacts). It is its own slice, with
a version bump, and must not ride inside the `Row` slice.

## D5 — metadata storage, three modes

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
in-place-versus-assemble decision as `RowRef`/`RowBuf` (D9/D10), taken at read
time from whether the document fits one leaf. One mechanism, three users — rows,
metadata, and any future large value.

⚠ 1 -> 2 promotion is automatic but STRUCTURAL: it allocates a container and
registers it in the dirmap with `top_level = 0` (D6). A metadata write can
therefore create a container, and the promotion must be transactional with the
write that triggered it — a half-promoted table has its metadata in neither
place.

## D6 — `top_level` in the container registry

The registry is the **dirmap**: a CoW `Map<CtrID, root>` whose leaf values are
container roots. Listing must distinguish roles — `ClusteredTable` is top-level,
its metadata container is nested — so the registry grows a bitmap column, one
bit per container, `1` = top-level. Memoria upstream has this.

⚠ The dirmap is a `Map`, and wave-0 admits **exactly two** entry columns
("wave-0 `kind ordered_map` supports exactly two entry columns"). A third column
is the same declaration-plane restriction the tables themselves need lifted, so
either it is lifted first, or the bits are packed into the existing value cell,
or the flags live in a parallel container. Lifting it is the honest fix and is
shared work with D8.

## D7 — base containers and dispatch

Instantiating a full container for every superstructure over an existing type is
expensive; a superstructure should reuse a base container. But dispatch reads a
type code from the block header (`ctr_type_hash`), which then names the BASE, and
the applied type is unrecoverable.

Two places for the second code, an order of magnitude apart in cost:

* **block header** — a format change touching every block and the whole CoW
  path;
* **root metadata** — already exists (D5 mode 1), and the applied type is only
  needed where a container is opened through its root.

The second is preferable *iff* dispatch always begins at a root. **OPEN-2 below.**

## D8 — the relation widens

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
mechanism — and the two cases ARE D9's `RowRef` / `RowBuf`, which makes that
split a consequence of the storage model rather than a choice this ADR makes.

## D9 — four consequences that are decisions, not details

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

## OPEN — two sub-decisions

**OPEN-1 — `OrdDataType`: closed set or dynamic tag?** The mechanism stores a
value of an arbitrary type from a given set (comparable ones) as the container's
value, with the concrete type recorded in metadata. Either (a) the set is CLOSED
at compile time and we generate a dispatch over it, the metadata selecting an
arm — an extension of the existing per-config generation, where Deem's relation
columns stay statically typed; or (b) the type is fully dynamic and metadata is
the only source — which changes the shape of Deem's relation and is a different
piece of work. Almost everything downstream follows from this answer.

**OPEN-2 — is there dispatch from a block with no root in hand?** D7's cheap
answer (applied type in root metadata) holds only if every dispatch path starts
at a container root. If any path starts from an arbitrary block, the code must go
in the block header and D7 becomes a format change.

---

## Slices

Dependencies are real; the order below is the dependency order, and the two
groups marked ∥ are independent of each other.

| # | slice | depends on | note |
|---|---|---|---|
| S0 | `DocumentHeader` 4 B | — | BREAKING; ABI bump; own commit |
| S1 | `Row` object type + popcount access | S0 | format only; no container |
| S2 | `TableMetadata` as a Writ container | — ∥ | ordinary document; column IDs monotone |
| S3 | `RowRef` / `RowBuf` + the assembly path | S1 | D9; borrow-carrying |
| S4 | relation widening (`entry(c1..cN)`, `unique`, `nullable`) | — ∥ | also closes 0026 F6/F8 |
| S5 | one emitter arm, stream 0 parameterised | S1, S4, OPEN-1 | both tables |
| S6 | `ClusteredTable` over bt_fl | S5 | PK = stream 0; value seq = the row's buffer |
| S7 | heap table (keyless, ordinal) | S5 | bt_fl vector-of-structs |
| S8 | metadata modes 2/3 | S2 | D5 |
| S9 | dirmap `top_level` | S4 or a packing decision | D6 |
| S10 | base container + dispatch | OPEN-2 | D7 |
| S11 | mutation protocol reconciliation | S3, S6/S7 | the real work |
| S12 | indexes | S6, S7 | secondary; scope of a later ADR section |

## What this ADR does not do

It does not retire `Vector<Writ>`. Since `Row` is a Writ structure, the document
container and the table share the ENTIRE value machinery — they differ in the
root object type and in where the schema lives, which makes them siblings rather
than rivals. Heterogeneity stays available where it is wanted and stops being
charged to rows that do not want it.

It does not specify indexes beyond naming S12. A secondary index over a clustered
table is another bt_fl client and needs the relation widening (S4) and the
registry roles (S9) before it can be stated at all.
