# Memoria runtime-typed containers

`logos.std.data.memoria` — universal containers whose key/value TYPE is fixed at
runtime (not a Logos generic param), so the body compiles ONCE in stdlib and many
user key/value types reuse it without per-type monomorphisation. Slower than a
static container (vtable dispatch on compare), but compiled once and applicable
across the whole Hermes datatype set.

## Why not HAny

`HAny` self-describes per value (tag inline/at-`obj[-1]`); important types
(i64/f64/decimal) box into the arena → per-value alloc + deref on every compare.
But a container's key type is fixed at creation → store the type ONCE, keys raw.

## Pieces (S0–S5)

- **KeyDesc** (`keydesc.logos`) — object-safe runtime descriptor (`dyn KeyDesc`,
  ZST impls; all behaviour in the vtable). `key_size` / `meta_is_ptr` / `cmp_raw`
  / `build_view` / `hash_raw` / `drop_raw`. Resolved by Hermes type-code via
  `desc_for(code)` (vtable_of + dyn_from_parts, same shape as the persistent
  loader's `node_arc_from_parts`). Descriptors: `U64Key`, `I64Key`, `DirI64Key`.
- **KeyView** = `{ data: *abs, meta: u64 }` — the two-block key view. `data` =
  absolute pointer to the raw key in a leaf (data zone). `meta` = one word,
  per-descriptor either an inline VALUE (small — Decimal prec/scale, DirI64Key
  direction) or an absolute POINTER (large meta block). Cross-zone joins are
  ABSOLUTE (relative refs valid only within one zone; data and metadata zones
  move independently) — assembled absolute at view time, never stored at-rest.
- **RawSlotBuf** (`rawbuf.logos`) — buffer of fixed raw slots, stride a RUNTIME
  value from the descriptor. push/get(slot)/set/insert_at/remove_at/truncate/
  clone/free + `push_ptr` (store a pointer by value).
- **OrdMap** (`tree.logos`) — runtime-typed ordered B+tree. One `Node` shape
  (is_leaf), all fields RawSlotBuf (keys / vals / kids-as-`*mut Node`). Leaf +
  branch split with root growth; binary descend/search via `cmp_raw`. The handle
  CACHES the (immutable) key+value descriptors and meta words — the hot path
  never re-resolves. Cursor walks in key order (path stack), assembling KeyView /
  value views; `cursor_take_*` materialises into an owning value.
  `ordmap_snapshot` = independent snapshot (deep clone; structural-sharing CoW is
  the optimisation that replaces it — the seam is `clone_node`/the snapshot fn).
- **HermesOrdVal** (`ordval.logos`) — client-side OWNING materialisation of a
  view, SBO ≤32B inline / heap overflow.

## Cached key meta is immutable

The key TYPE (and its container-wide meta: tag, Decimal scale, direction…) is set
at creation and never changes → cached BY VALUE in the handle, no invalidation,
valid across all snapshots. Mutable non-key metadata (if any) stays out of this
cache. `DirI64Key` (S5) demonstrates the path end-to-end: `key_meta` bit0 selects
ascending/descending; `cmp_raw` consumes it (order), `build_view` carries it
(view), the cursor yields the chosen order.

## Skeleton + thin surface → metaprog lift

The B+tree algorithm (descend / split / grow / clone / cursor) is a reusable
SKELETON independent of the key type. The container-specific SURFACE is tiny:
the descriptor wiring + cached meta in `OrdMap`, the public `ordmap_*` entry
points. Because the key type is RUNTIME, the surface does NOT parameterise over
a key type — so a future metaprog handler (cf. `#[derive_branch_node]`, the
quasi-quote codegen in `logos.std.compiler.metaprog`) only needs to EMIT the thin
surface (handle struct + entry fns + descriptor registration) and call the shared
skeleton. Adding a new universal container then becomes a declaration, not a hand
-written tree. That lift is the next stage; the split here is what makes it a
refactor rather than a rewrite.

## Scope / задел (flagged)

- Mutable in-place nodes; snapshot = deep clone (structural-sharing CoW pending).
- Removal: no rebalance yet.
- Owning key/value drop: `drop_raw` is wired at leaves (POD no-op); deep clone of
  owning elements needs `clone_raw` (not yet added).
- Instance-level (per-key-varying) metadata: slot provided in the model, not
  implemented (no such datatypes yet).
- meta-by-POINTER large metadata: the `meta_is_ptr` path exists; first user
  (e.g. a large-meta Decimal) lands with that descriptor.
