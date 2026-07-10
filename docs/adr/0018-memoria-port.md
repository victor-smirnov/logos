# ADR 0018 — Memoria → Logos: conuco/memoria + the storage seam

Status: ACCEPTED — scope §0.1, decisions §4 (user, 2026-07-10). Goal: базовая
Мемория в Logos + полноценная интеграция с инкрементным Deem. Date: 2026-07-10.

## 0. Mandate

Port Memoria (C++, `/home/victor/cxx/memoria`) to Logos as `conuco/memoria` — the
first conuco incubator project. Within the port, build a maximally lightweight
store + containers for everyday Deem use. `persistent` (`stdlib/std/deem/data`)
is demoted to a MOCK: it exists to pin down the storage/container interface
design and the Deem wiring, then the Memoria-ported implementations take over.
Maximize code sharing between mock and port. The C++ code is debugged — use it
as an ORACLE (equivalent op sequences on both sides, compare) or port directly.

## 0.1 Scope (user, verbatim spirit — the framing that settles it)

**Logos IS Memoria in the form of a programming language.** A good half of
Memoria is already implemented as Logos itself: Writ (= Hermes), metaprog
(= the TMP framework + mbt codegen), and Deem exists on top (Memoria never had
it). The port therefore takes only what the LANGUAGE doesn't already embody:

TAKE:
1. Algorithms & data structures + the patterns around them (B+tree ops,
   CoW protocols, chunk iteration, refcounted DAG GC, …);
2. PackedAllocator (and the packed in-block layout discipline);
3. MemoryStore / SWMRStore — API + implementation;
4. Store API, Snapshot API, Container API (the contract surfaces).

DO NOT TAKE:
1. The TMP framework (typelists/mixins/dispatchers/BTTypes/CtrTF + mbt) —
   metaprog IS its replacement;
2. Hermes / HRPC / Reactors / the Testing framework — Writ IS Hermes; RPC and
   runtimes are out of scope; testing uses OUR harness (the C++ tests serve
   as semantics reference, not as an adopted framework).

## 1. Survey results (three scouts, 2026-07-10)

### 1.1 Sizes and the porting quotient

| Layer | C++ kLoC | Portable essence | Notes |
|---|---|---|---|
| Store interfaces (`stores-api/`) | 1.5 | design source | our Logos API surface |
| Registration/metadata (`containers-api/`) | 6.9 | <1k | registry itself is small |
| Memory CoW store | 3.8 | ~2k | port FIRST |
| SWMR store family | 8.8 | later | start `lite_raw` (0.2k entry, no file I/O) |
| B+tree prototypes (`bt`, `bt_ss`) | 19.4 | 2–3k | algorithms real; wrapping is ballast |
| Packed structures | 17.6 | ~2k | PackedAllocator + PackedDataTypeBuffer |
| Containers (Map/Set/Vector/Collection) | ~3 | ~1k | on `bt_ss` |
| Assembly TMP (mixins/dispatchers/typelists) | ~8 | **~0** | replaced by macros outright |
| LMDB / OLTP / reactor / asio / seastar / hermes | ~35 | 0 | skip (Writ IS our Hermes) |

Bottom line: **~56 kLoC of container C++ + ~28 kLoC of store C++ reduce to an
estimated 5–8 kLoC of Logos**, because roughly half of Memoria's source exists
to simulate metaprogramming C++ lacks:

- `CtrPart`/`IterPart` mixin inheritance chains over `TypeList`s → ONE flat
  struct emitted by a container macro.
- `NDT0` node dispatcher + `PackedDispatcher` slot dispatcher (compile-time
  unrolled if-else chains) → plain `match` emitted from the same declaration.
- `BTTypes`/`CtrTF` two-stage trait derivation + `Linearize`/list-tree algebra
  → macro-time computation producing struct literals.
- The ENTIRE external codegen pipeline (`mbt/`, a Clang LibTooling tool +
  Inja templates + 2-pass CMake build, driven by `[[clang::annotate]]` configs)
  → a compile-time loop over (container × store-config) pairs with item
  emission. This is the single biggest structural simplification.

### 1.2 What must be ported faithfully (the load-bearing ideas)

1. **PackedAllocator** — ported 1-TO-1 as an OBJECT design (user,
   2026-07-10; the languages are close enough): `PackedAllocatable`
   {allocator_offset — the SELF-RELATIVE back-reference to the owning
   allocator} is AGGREGATED as the first field of EVERY packed structure
   (aggregation, not inheritance), the allocator itself included. This makes
   packed structures SELF-SUFFICIENT: a structure asks ITS OWN allocator to
   `resize_block(self, size)`, and the allocator, when out of space, asks
   ITS parent (`enlarge → resize → parent.resize_block(this)`) — recursive
   to any nesting depth. The slot bitmap (RAW_MEMORY vs ALLOCATABLE) is
   load-bearing: on shifts, ONLY allocatable slots get their back-reference
   re-pointed (`move_element_data → set_allocator_offset(this)`); raw bytes
   move untouched (interiors are self-relative). Shrink auto-packs nested
   allocators. Blocks stay CONTIGUOUS + RELOCATABLE. Follow-up task (user):
   befriend the borrow checker — the end state has NO unsafe in the packed
   layer; until then unsafe is confined inside impl bodies.
2. **PackedDataTypeBuffer** — the columnar leaf stream: packed array of a
   datatype (fixed or length-prefixed variable), optional nested running-sum
   index (fan-out 32) for O(log n) rank/range. The unified successor of the
   old PkdFTree/PkdVLETree. One fixed + one variable variant suffice initially.
3. **B+tree algorithms** (`prototypes/bt/container/`): insert/find, branch/
   leaf split+merge (fixed & variable), walk, CoW-vs-in-place branch ops as a
   config axis (NOT a container axis). **STREAMING/BATCH container creation
   is a KILLER FEATURE (user, 2026-07-10)** — Memoria's bulk-build path
   (`bt_c_insert_batch_*`, `bt_tools_batch_input.hpp`: feed a stream of
   entries, nodes fill bottom-up without per-element descent) is one of the
   framework's core differentiators vs other B+tree libraries and is ported
   FIRST-CLASS, not as an afterthought bolted onto point inserts.
4. **Shuttles are the traversal abstraction, NOT ballast** (user correction,
   2026-07-10): stateful objects walking the tree forward AND backward,
   TASK-SPECIFIC — an OPEN set (find, skip, select, rank, custom per
   container/query), unlike the one-or-two iterator shapes of C++ STL
   containers. Iterators EXIST but are IMPLEMENTED VIA shuttles. The port
   keeps shuttles as a first-class trait (walk state + branch-summary
   consumption + direction); what collapses is only the C++ template-functor
   HIERARCHY around them. Chunk cursors (leaf-granularity, zero-copy spans,
   `next_chunk()`) are built ON shuttles; the legacy element-wise iterator
   mixin layer stays behind. Echo in the mock: persistent's bt/shuttle.logos
   + derive_branch_node's per-op shuttle wrappers already carry the concept.
5. **The store model** (memory CoW): snapshot DAG of `HistoryNode`
   {parent, children, status ∈ ACTIVE/COMMITTED/DROPPED/DATA_LOCKED,
   snapshot_id, refs}; named branches + `master` as pointers into the DAG;
   per-snapshot container directory (root map CtrID→BlockID); block header
   {uid, **ctr_type_hash**, atomic refs}; CoW on refs>1; deferred refcounted
   GC of DAG nodes; dump/load = flat binary tagged records
   (METADATA / HISTORY_NODE / DATA_BLOCK / CHECKSUM).
6. **Registration** (the mechanism the user named): per-profile global
   registry — `(ctr_type_hash, block_type_hash) → BlockOps`,
   `ctr_type_hash → ContainerOps` (check/walk/create_ctr_instance/clone),
   `type-signature → CtrInstanceFactory` (create fresh). Every root block
   carries `ctr_type_hash`; open = registry lookup; unregistered = error, no
   scanning. Registration calls are generated per (container × profile) and
   run at startup. In Logos: a compile-time-built const table via macros —
   kills Memoria's init-order/mutex complications.
7. **TypeHash** — stable 64-bit codes: hand-assigned literals for core types,
   compile-time structural MD5 for composites. Logos analog exists
   (schema_code / type_hash_56bit); needs a mapping table if oracle-level
   byte compatibility is ever wanted.

### 1.3 What Memoria does NOT have that we already built (preserve!)

- **Merge commits.** Memoria's DAG has NO merge primitive — cross-branch data
  movement is `copy_ctr_from`/`import_ctr_from` only. Our ADR-0017 P2
  select_one merge (multi-parent commits) and S4 branch semantics are an
  EXTENSION to carry into the port's store design.
- **Epoch-history + ΔEDB journal wiring for Deem** (ADR-0017 P1/P3) — rehosts
  on the seam traits unchanged.

### 1.4 Profiles → WritStatic

Memoria Profile = empty tag type + `ProfileTraits` specialization: BlockID
width/format (UID256 vs CowBlockID<UID64>), CoW flag, store iface, id factories,
TypeHash literal. Exactly our `const STORE_CFG: WritStatic` pattern (ADR-0017
already proved it: `@{"name", "ctr_id", "snp_id", "block_id"}`). Concrete C++
profiles: CowProfile, CowLiteProfile (u64-ish ids — the lightweight one),
NoCowProfile (disabled). Logos v0: ONE lite config (u64 ids, CoW), more later.

### 1.5 Oracle strategy

Memoria's test harness is a seeded property framework with YAML failure-replay
(`TestState` + `MMA_STATE_FILEDS`, seed capture, subprocess runner). Entry
points by leverage: (1) `StoreTestBench`/`SWMRStoreTest` (store semantics,
reactor-free variants incl. `testMemCoW`), (2) `MapTest`/`SetTest`/`VectorTest`
(container semantics), (3) packed tests (byte-level, only if/when format
compatibility becomes a goal). Plan: a shared op-log format (seeded op
sequences) driven through both sides; the YAML replay format is the template.

## 2. Proposed shape (for discussion)

### 2.0 Build (user, 2026-07-10)

conuco/memoria is a SEPARATE project temporarily living in this repo. It
builds via **lforge** (its own `lforge.hermes`, multiple lib targets), NOT via
the repo CMake. lforge builds lib targets serially in topo order today —
parallelizing independent `--emit-module` targets is a planned lforge
improvement along the way.

### 2.1 conuco/memoria layout

```
conuco/memoria/
  lforge.hermes           # lforge project: several lib targets (pkd, bt, ctr, store-mem)
  src/pkd/                # PackedAllocator, PackedDataTypeBuffer (fse+vle)
  src/bt/                 # tree algorithms, generic over macro-emitted node layouts
  src/ctr/                # container declarations: map, set, vector, collection
  src/store/mem/          # memory CoW store (HistoryNode DAG, branches, dump/load)
  src/store/swmr/         # later: lite_raw → mapped
  tests/                  # property tests + oracle op-log driver
```

### 2.2 The stdlib seam (per the extras-tier decision)

stdlib keeps dyn-compatible traits: `IStore`/`ISnapshot`/`ICtr` (+ chunk-iter
trait), DView-keyed, no method type-params; FactStore programs against them;
the instantiating module REGISTERS its container flavours in the Store
(Memoria-style). `persistent` = mock impl of the seam; `conuco/memoria` = the
real one. Deem's P1–P3 layers (epochs/branches/journal) move onto the seam
unchanged.

### 2.3 Container declaration (replaces BTTypes+CtrTF+mbt)

One macro item per container, in the spirit of:

```logos
container Map<K: Datum, V: Datum> for BtSs {
    streams  { size: StreamSize, key: Column<K>, value: Column<V> }
    branch   { sum size }
    chunk    { key() -> K::View, value() -> V::View }
}
```

emitting: node structs (leaf/branch layouts over PackedAllocator slots), the
node-kind `match` dispatchers, tree-op instantiations, chunk cursor, ctr-ops +
factory + registration entries, TypeHash. (Exact surface = main discussion
topic.)

## 3. Staging (proposal)

- **M0** — conuco build wiring (first conuco project: module manifest, cmake
  target, test tier) + seam ADR execution in stdlib (traits + persistent-as-mock).
- **M1** — `pkd`: PackedAllocator + fixed/variable PackedDataTypeBuffer;
  oracle = C++ packed tests' semantics (not bytes).
- **M2** — `bt` + `ctr`: Map<u64,u64> end-to-end over a trivial arena;
  container macro v0; chunk iterators.
- **M3** — memory CoW store: DAG, branches+master, refcounted blocks,
  registry, dump/load; Map over store; oracle = `testMemCoW`/`StoreTestBench`
  op sequences. PLUS our merge-commit extension (S4).
- **M4** — Deem rehosting: FactStore over the seam → epochs/branches/journal
  green over memoria store (existing ADR-0017 gate tests as the suite).
- ~~M5 — SWMR lite (`lite_raw` → mapped)~~ — NOT NOW (user, 2026-07-10): when
  SWMR lands it is built DIRECTLY ON FIBERS, without mmap (io_uring-era file
  I/O per the concurrency model). Current goal ends at M4: базовая Мемория +
  полная интеграция с инкрементным Deem.

### 2.4 No compilation firewall (user, 2026-07-10)

Memoria heavily uses the compilation-firewall pattern — the `containers-api/`
vs `containers/` split, virtual `ICtrApi<CtrName,Profile>` wrappers over
concrete containers, type-erased `CtrReferenceable`, explicit template
instantiation confined to generated `.cpp` TUs — to keep its internal
templates OUT of client translation units, purely for C++ compile speed. In
Logos NONE of this is reproduced: modules precompile their generics, clients
see concrete container types directly, and the api/impl mirror-header split
collapses into one definition.

Type erasure survives ONLY where it is load-bearing at RUNTIME, not at
compile time: (a) the registry's operations vtables (block/ctr dispatch by
type hash — a raw block must resolve to code without static knowledge), and
(b) the stdlib↔module seam traits (IStore/ISnapshot/ICtr), where dyn is the
module boundary itself. Everything else is direct, monomorphized calls.

## 4. Design decisions (user, 2026-07-10)

1. **Container declaration = WritStatic configs**, exactly the persistent
   pattern (`pub const MapCfg: WritStatic = @{...}` + const-generic
   `<const CFG: WritStatic>`); syntactic sugar for container declaration comes
   LATER, designed once real usage exists. Containers have MANY configuration
   parameters — treat the config space as a small DATABASE: Deem/EL is the
   query/validation tool over it (config introspection, constraint checking,
   derived defaults). The §2.3 `container` item sketch is deferred material
   for that sugar, not v0.
2. **Profiles, plural, as in Memoria** — different ID widths per profile:
   BOTH u64 (lite/everyday) AND UID256 profiles will exist. Profile =
   WritStatic config value; the same container source instantiates per
   profile (the mbt cross-product becomes a metaprog loop).
3. **NO byte compatibility with C++ Memoria — «ломаем всё»**. The C++ side is
   a semantics reference only; formats, layouts, hashes are free to diverge.
4. **Threads only for now** (mutex-based, ThreadsMemoryStore analog); the
   fibers variant comes when the pinned-fiber runtime matures.
