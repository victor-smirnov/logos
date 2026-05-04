# Big Memoria — Container Architecture Digest

A reference for the legacy C++ Memoria container codebase at
`~/cxx/memoria/containers/include/memoria/`. Two purposes:

1. **Terminology** — what the recurring words (Profile, Stream, Substream,
   BT / BT_SS / BT_FL, Branch/Leaf node, SO, NDT, Shuttle, Iterator, Batch
   update, CoW) actually mean *as used in this codebase*.
2. **Bridge** — map each concept onto the current
   `stdlib/std/data/persistent/` ("mini-memoria") and call out the
   extension hooks that pmap_v2 needs to keep open if it is ever to grow into
   the full prototype framework.

Style: dense, with citations of files and key types. Code excerpts only where
the exact signature is load-bearing.

---

## 0a. How mini-memoria deliberately differs from big Memoria

The doc below describes big Memoria's full C++ design. Before reading, fix
in mind which of those mechanisms mini-memoria adopts vs which it deliberately
drops. The differences are not gaps to be closed — they are scope:

- **Nodes are simple buffers, not packed structures.** Mini-memoria's
  `BTreeNode<K, V, CFG>` holds nested `Buffer<K>` / `Buffer<V>` /
  `PrimVec<...>`, each a small allocation of its own. Big Memoria's nodes
  live in a single 4K-aligned block managed by `PackedAllocator`, with
  packed substreams placed inside that block via byte offsets. Consequence
  for mini-memoria:
  - No alignment / block-size constraints on node payload.
  - No `PackedAllocator` machinery, no in-place size-class growth, no
    on-block resize-and-rebalance.
  - Substreams of big Memoria collapse to fields-of-the-node-struct here.
  - Fields can have different lifetimes and grow independently.
- **In-memory only.** No `IBlockOperations`, no swap, no on-disk format,
  no serialise/deserialise, no `cow_resolve_ids`, no after-deserialisation
  fixups, no DTOs vs view types. Mini-memoria's `Snapshot` / `MutSnapshot`
  hold raw `*mut BTreeNode<K, V, CFG>` directly; the `LockingStore` owns
  a commit DAG keyed by simple i64 commit ids, no UID infrastructure.
- **Single in-process Profile.** Big Memoria parameterises by `Profile`
  (e.g. `CowProfile`, `NoCowProfile`) which selects BlockID type
  (UID256 for CoW, simpler for NoCoW), StoreType (`ICowStore<Profile>`),
  Block layout, ID resolution policy — and lets multiple "memorias"
  coexist in one app, each with their own ID space and store. Mini-memoria
  has one implicit profile: malloc-backed nodes, raw pointers as block IDs,
  i64 commit ids. Profile-as-explicit-parameter is an extension hook
  (see §14), not a current feature.
- **Container has one tier of types: `<K, V, const CFG>`.** Big Memoria's
  `BTTypes<Profile, ContainerSelector>` chains through specialisations
  (BTTypes<CowProfile, BTSingleStream> ← BTTypes<CowProfile, Map<K,V>>) to
  build a deep types-bag. Mini-memoria has K, V, and a HermesStatic CFG;
  the assembler-metafn does the role of the BTTypes specialisation chain
  but at code-emission time, not via TMP type-list manipulation.
- **No multi-stream containers.** Mini-memoria is BT_SS-equivalent in
  shape today (one logical stream: keys + values). Multi-stream and
  BT_FL-style structures (Multimap, hierarchical containers) are
  out-of-scope until / unless mini-memoria grows the structure-stream
  hook documented in §14.

What this document still covers: every big-Memoria concept, with a note in
§14 mapping it to mini-memoria today and to the extension hook (or
explicit non-goal) it should remain. The structural-difference list above
is the lens; the rest of the doc is the substance.

---

## 0. The shape of the codebase

```
containers/include/memoria/
├── prototypes/
│   ├── bt/        ← the multi-stream B+tree prototype (umbrella)
│   ├── bt_ss/     ← single-stream specialisation of bt
│   ├── bt_fl/     ← free-layout specialisation of bt
│   └── composite/
└── containers/
    ├── map/       ← BT_SS instance — Map<K,V>
    ├── set/       ← BT_SS instance — Set<K>
    ├── multimap/  ← BT_FL instance — Multimap<K,V>
    ├── seq_dense/, sequence/, vector/, allocation_map/, collection/
```

There is one prototype framework (`bt`) and concrete user containers
(`map/`, `set/`, `multimap/`, …) which plug into it through partial
specialisations of one trait struct, `BTTypes<Profile, ContainerName>`, and
one factory, `CtrTF<Profile, ContainerName, T>`.

A container at the C++ level is therefore:

- a class-name selector tag (e.g. `Map<K,V>`, `Set<K>`, `Multimap<K,V>`,
  `BTSingleStream`, `BTFreeLayout`, `BT`),
- one or more partial specialisations of `BTTypes<Profile, …>` that fill in
  type lists (stream descriptors, container-parts lists, key/value types),
- a `CtrTF<…>::Type` typedef which assembles everything into a final
  `Ctr<CtrTypes>` class via mp11 / typelist meta.

---

## 1. Profile, Container, Types

### 1.1 `Profile`

`Profile` is the **backend / storage / persistence model** parameter — a
tag type, never instantiated. It's the constellation of typedefs and
policies that together define what "one Memoria instance" means. The
practical consequence: **multiple Profiles can coexist in one application
process** — each defines its own ID space, its own store, its own
CoW/no-CoW policy, its own ID size class. Different containers in the
same app can live in different memorias (e.g. an in-process working set
on a no-CoW profile + a snapshot-DAG persisted store on a CoW profile),
without their type-level identities crossing.

`BTTypes<Profile, ContainerTypeSelector>` is therefore a 2-axis trait —
the container axis (Map/Set/Multimap…) and the storage-profile axis are
orthogonal.

Concrete profiles live under `containers-api/include/memoria/profiles/`.
Read `profiles/impl/cow_profile.hpp` (110 lines) for the canonical
example. A profile is a specialisation of `ProfileTraits<Profile>` that
exports a fixed set of associated typedefs; from `cow_profile.hpp`:

```cpp
template <>
struct ProfileTraits<CowProfile>: ApiProfileTraits<CoreApiProfile> {
    using CtrID;          // container identifier
    using CtrSizeT;       // size scalar (e.g. int64_t)
    using SnapshotID;     // commit / snapshot identifier
    using BlockGUID     = UID256;
    using BlockID       = CowBlockID<BlockGUID>;
    using Profile       = CowProfile;
    using Block         = AbstractPage<BlockID, SnapshotID>;
    using StoreType     = ICowStore<Profile>;
    using BlockShared   = PageShared<StoreType, Block, BlockID>;
    template <typename TargetBlockType>
    using SharedBlockPtrTF =
        CowSharedBlockPtr<TargetBlockType, StoreType, BlockShared>;
    static constexpr bool IsCoW = true;
    static BlockID    make_random_block_id();
    static BlockGUID  make_random_block_guid();
    static CtrID      make_random_ctr_id();
    static SnapshotID make_random_snapshot_id();
};
```

What each axis does:

- **`CtrID`, `SnapshotID`, `CtrSizeT`** — identity & size-scalar choices.
  Different profiles can use different sizes (UUIDs vs i64) without
  contaminating container code, which only sees the typedefs.
- **`BlockID`, `BlockGUID`, `Block`** — block-level identity. CoW
  profiles use 256-bit GUIDs (`UID256`) so block versions never collide
  across snapshot history; no-CoW profiles can use cheaper schemes.
- **`StoreType`** — the abstract storage interface (`ICowStore<Profile>`
  for CoW, separate API for no-CoW). Gives the container access to "load
  block by id", "register new block", "fork at commit", etc.
- **`SharedBlockPtrTF`** — fat shared-block-ptr factory. The container
  never holds raw block pointers; it holds these smart-ptr-like wrappers
  around (block id + cache-resolved raw ptr + retain/release on the
  store).
- **`IsCoW`** (bool) — drives `CoWOpsRName` vs `NoCoWOpsRName` selection
  in `bt_factory.hpp`. The container parts list specialises on this.
- **`make_random_*` factories** — random-ID generators per ID type.
  Profile-specific because UID256 generation is different from i64.

Sibling profiles to read for contrast: `profiles/impl/no_cow_profile.hpp`
(simpler, no version IDs), `profiles/impl/cow_lite_profile.hpp` (lighter
CoW with smaller IDs).

The container code never `new`s storage or chooses a CoW policy at the
container level: it asks the profile. *That* is what makes one container
class work across in-memory, on-disk-CoW, on-disk-no-CoW backends without
recompiling anything but the profile.

(Mini-memoria has one implicit profile baked in: malloc-backed nodes,
raw `*mut BTreeNode<K, V, CFG>` as block ID, i64 commit ids in
`LockingStore`, CoW-by-rc. Profile as an explicit axis is an extension
hook — see §14.)

### 1.2 `BTTypes<Profile, ContainerTypeName>`

The central trait. Every concrete container specialises it. From
`prototypes/bt/bt_factory.hpp`:

```cpp
template <typename Profile_, typename ContainerTypeSelector>
struct BTTypes {
    using Profile  = Profile_;
    using CtrSizeT = ProfileCtrSizeT<Profile>;

    using ContainerPartsList   = TypeList<…>;   // read-side parts
    using RWContainerPartsList = TypeList<…>;   // write-side parts

    using FixedBranchContainerPartsList    = …;
    using VariableBranchContainerPartsList = …;
    using FixedLeafContainerPartsList      = …;
    using VariableLeafContainerPartsList   = …;

    using NodeTypesList = TypeList<
        bt::BranchNodeTypes<bt::BranchNode>,
        bt::LeafNodeTypes<bt::LeafNode>
    >;
    using StreamDescriptors = TypeList<>;       // ← filled in by user

    template <typename Types_> using CtrBaseFactory   = bt::BTreeCtrBase<Types_>;
    template <typename Types_> using RWCtrBaseFactory = RWCtrBase<Types_>;
};
```

Specialisations at the user level (e.g. for `Map<K,V>`) inherit one of three
"prototype variants" — `BTTypes<Profile, BT>`, `BTTypes<Profile,
BTSingleStream>`, `BTTypes<Profile, BTFreeLayout>` — and append:

- their own `CommonContainerPartsList` / `RWCommonContainerPartsList`
  entries (the API-surface parts),
- a concrete `StreamDescriptors` typelist describing the columnar layout.

### 1.3 `CtrTF<Profile, ContainerName, T>`

`CtrTF` is the container *type factory*. Reading `bt_factory.hpp` lines
184-401: it pulls `BTTypes<…>::ContainerPartsList`,
`RWContainerPartsList`, `…LeafPartsList`, etc. and merges them into one big
`CtrList` / `RWCtrList`. It also computes:

- `BranchStreamsStructList`, `LeafStreamsStructList` — the flattened lists
  of packed-structs that actually live in branch/leaf nodes;
- `LeafSizeType` (FIXED/VARIABLE) and `BranchSizeType`, which select between
  `FixedLeafContainerPartsList` vs `VariableLeafContainerPartsList`;
- `BlockDispatchers = bt::BTreeDispatchers<DispatcherTypes>` — the runtime
  hash-dispatcher over node types.

The `Type` member is `Ctr<CtrTypes>` — the final container class.

---

## 2. Streams

A **stream** is a *column* through the tree: one logical sequence of values,
indexed by a per-stream `CtrSizeT`. A multi-stream container holds several
parallel columns whose ordering is co-defined.

In `BTTypes<Profile, X>::StreamDescriptors` each entry is a `bt::StreamTF`
of the form

```cpp
bt::StreamTF<
    TL<                              // leaf-side substream tree
        TL<StreamSize>,              //   — an element-count substream
        TL<LeafKeyStruct>,           //   — the key column
        TL<LeafValueStruct>          //   — the value column
    >,
    map::MapBranchStructTF            // branch-side struct factory
    /* optional IdxRangeList */
>
```

(see `containers/map/map_factory.hpp:73-82`). The `StreamTF` template
parameters are `<LeafType, BranchStructTF, IdxRangeList=…>`. The first
parameter is a *tree* of typelists, not a flat list — substreams group into
a "substream tree" so that the dispatcher can subset them.

### Where streams live structurally

A stream is realised as **parallel packed-structs in every leaf** plus
**aggregated index entries in every branch**. The relationship is built by
`PackedLeafStructListBuilder` and `PackedBranchStructListBuilder` in
`prototypes/bt/tools/bt_tools_packed_struct_list_builder.hpp`.

- For each `StreamTF<L, BranchStructTF, …>` the leaf side keeps `L`
  literally (a list of substream packed-structs).
- The branch side is built by `BTStreamDescritorsBuilder<FlattenLeafTree<L>,
  BranchStructTF, SumType>`: it derives a sibling tree of *index* packed-
  structs (sums / fenwick / mins / maxes — the choice is `BranchStructTF`).

Branch nodes therefore carry **aggregated separators / sums / counts** for
each stream's substreams, while leaves carry the *full* substream payload.
This is the substrate that lets a "find by sum" or "select by rank" walk
descend in O(log n) with column-wise pruning.

### Single-stream vs multi-stream

- **Map / Set** use one `StreamTF` (single-stream) — they are BT_SS
  instances.
- **Multimap** uses three `StreamTF`s — keys, values, and a *structure
  stream* (an SSRLE-encoded sequence of "which data stream does the next
  element come from"). It is a BT_FL instance.

`StreamSize` is a special "always-first substream" carrying the per-leaf
element count; it lets every node carry a consistent `CtrSizeT` aggregator
in branches without each container having to wire it manually.

---

## 3. Substreams

A **substream** is *one packed-struct slot inside one stream's leaf
payload*. Every substream is a self-contained, relocatable, in-place-
resizable byte block (a `PackedXxx`). The leaf node owns a `PackedAllocator`
that hands out aligned slots; substreams live in those slots.

### Anatomy

`prototypes/bt/nodes/leaf_node.hpp:64-96`:

```cpp
using LeafSubstreamsStructList   = typename Types::LeafStreamsStructList;
using BranchSubstreamsStructList = typename Types::BranchStreamsStructList;

using StreamDispatcherStructList = typename PackedDispatchersListBuilder<
        Linearize<LeafSubstreamsStructList>,
        Base::StreamsStart                    // first allocator slot index
>::Type;
using Dispatcher = PackedDispatcher<StreamDispatcherStructList>;
```

Each substream type is required to satisfy `IsPackedStructV<…>` and exposes
in-place ops (`splitTo`, `mergeWith`, `insert`, `removeSpace`, …) plus
`block_size()` accounting that the `PackedAllocator` uses to grow/shrink.

### Catalogue of packed-structs you'll find in `core/packed/`

(Stand-in list of types referenced from the prototype headers — these are
the substreams the map/set/multimap factories use):

- `PackedFSEArray<Key>` — fixed-size element array (e.g. `i64` keys).
- `PackedSizedArray<Value>` — sized array of fixed-size values.
- `PackedDataTypeBuffer<…>` — variable-length payload buffer for varchars
  / blobs (used heavily in CoW where block IDs are variable-encoded —
  `leaf_node.hpp:125-141`).
- `PackedSSRLESeq<DataStreams, 256, true>` — sparse-symbol RLE sequence,
  used by BT_FL as the *structure stream* (`btfl_tools.hpp:28`):
  `template <int32_t DataStreams> struct StructureStreamTF: HasType<…>`.
- `PackedTuple<ExtData…>` — packed tuple of substream-extension data.
- `PackedMap<K,V>` — used for per-tree metadata
  (`CtrPropertiesMap`, `CtrReferencesMap`).

### Why this granularity

1. **Zero-copy access.** A substream is read in place via a typed view
   (`leaf.template substream<LeafPath>()`), no copy.
2. **Independent resize.** When a key is inserted, the keys substream grows
   by `sizeof(Key)`, the values substream grows by its own stride, and the
   `PackedAllocator` shifts the others. No "row" struct exists in memory.
3. **Per-substream branch indexing.** The branch-side of each substream can
   be a different aggregator (sum tree for sizes, fenwick for keys ordered,
   max tree for keys range-min, …) — `BranchStructTF` is per-stream.
4. **Substream-group dispatch.** Algorithms can operate on a *subset* of
   substreams via `SubstreamGroupDispatcher` and `SubrangeDispatcher`
   (`bt_tools_substreamgroup_dispatcher.hpp` — `GroupDispatcher`,
   `SubsetDispatcher`).

### Substream paths

Algorithms address substreams by `IntList<…>` paths. From `bt_factory.hpp`:

```cpp
template <int32_t SubstreamIdx>
using LeafPathT   = typename list_tree::BuildTreePath<LeafStreamsStructList,   SubstreamIdx>::Type;
template <int32_t SubstreamIdx>
using BranchPathT = typename list_tree::BuildTreePath<BranchStreamsStructList, SubstreamIdx>::Type;
```

A `LeafPath` is a static path into the substream tree. The leaf SO turns it
into a typed view: `leaf.substream<LeafPath>().findForward(…)`.

---

## 4. The BT prototype

`prototypes/bt/` is the umbrella: a *multi-stream B+tree* with policy-
selected branch and leaf node implementations and a fixed/variable size
discriminator on each. Everything else (`bt_ss`, `bt_fl`, the concrete
containers) is layered on top by extending `BTTypes` and (sometimes)
`CtrTF`.

### Container parts

`bt_factory.hpp` collects the read-side under `ContainerPartsList`:

```cpp
using ContainerPartsList = TypeList<
    bt::ToolsName, bt::ToolsPLName, bt::ChecksName,
    bt::FindName, bt::LeafRCommonName, bt::WalkRName, bt::BlockName,
    IfThenElse<ProfileTraits<Profile>::IsCoW,
               bt::CoWOpsRName, bt::NoCoWOpsRName>
>;
```

and the write-side under `RWContainerPartsList`:

```cpp
using RWContainerPartsList = TypeList<
    bt::BaseWName, bt::InsertBatchCommonName, bt::RemoveBatchName,
    bt::UpdateName, bt::BranchCommonName, bt::LeafWCommonName,
    bt::InsertName, bt::WalkWName, bt::NodeCommonName,
    IfThenElse<ProfileTraits<Profile>::IsCoW,
               bt::CoWOpsWName, bt::NoCoWOpsWName>
>;
```

Each `XxxName` is just an empty tag class declared in `bt_names.hpp`; the
*body* of the part lives in `container/bt_c_*.hpp` and is glued onto the
inheritance chain by the `MEMORIA_V1_CONTAINER_PART_BEGIN(tag)` macro
(`container/bt_c_insert_batch_common.hpp:30` etc.). The full part list is
then merged through `MergeLists<…>` into a single typelist that
`Ctr<CtrTypes>` linearises into a base-class chain — every part contributes
methods to the final `Ctr` class, and refers to siblings via the standard
`this->self()` CRTP idiom.

### Files vs concepts

| concept                | files                                                   |
|------------------------|---------------------------------------------------------|
| read base / walk       | `bt_cr_base.hpp`, `bt_cr_walk.hpp`, `bt_cr_leaf_common.hpp` |
| write base / walk      | `bt_cw_base.hpp`, `bt_cw_walk.hpp`, `bt_cw_leaf_common.hpp` |
| tools / checks         | `bt_c_tools.hpp`, `bt_c_tools_pl.hpp`, `bt_c_checks.hpp`|
| find / read / update   | `bt_c_find.hpp`, `bt_c_read.hpp`, `bt_c_update.hpp`     |
| insert / remove single | `bt_c_insert.hpp`, (no `bt_c_remove.hpp` at top — bulk via batch) |
| insert/remove batch    | `bt_c_insert_batch_{common,fixed,variable}.hpp`, `bt_c_remove_batch.hpp` |
| branch / leaf split    | `bt_c_branch_{common,fixed,variable}.hpp`, `bt_c_leaf_{fixed,variable}.hpp` |
| block / node           | `bt_c_block.hpp`, `bt_c_node_common.hpp`                |
| persistence policy     | `bt_cr_cow.hpp`, `bt_cw_cow.hpp`, `bt_cr_no_cow.hpp`, `bt_cw_no_cow.hpp` |

### Node descriptor machinery

`prototypes/bt/nodes/`:

- `tree_metadata.hpp` — `BalancedTreeMetadata<Profile>`, persisted into
  the root.
- `branch_node.hpp` / `leaf_node.hpp` — `TreeNodeBase`, `BranchNode<Types>`,
  `LeafNode<Types>` POD classes that own a `PackedAllocator`.
- `branch_node_so.hpp` / `leaf_node_so.hpp` — `BranchNodeSO<CtrT, NodeT>`,
  `LeafNodeSO<CtrT, NodeT>`. **SO = Sparse Object**: a value-typed view
  (`ctr_*` + `node_*` pair) that exposes typed substream access without
  storing per-leaf v-tables. Every algorithm receives an SO, not a raw
  block pointer.
- `node_dispatcher.hpp` / `node_dispatcher_tree.hpp` — `NDT0<CtrT, Types,
  Idx>` recursive dispatcher.
- `node_list_builder.hpp` — derives the per-node-type lists from
  `BTTypes::NodeTypesList`.

---

## 5. BT_SS — single-stream

`prototypes/bt_ss/btss_factory.hpp` introduces a tag `BTSingleStream`:

```cpp
template <typename Profile>
struct BTTypes<Profile, BTSingleStream>: public BTTypes<Profile, BT> {
    using Base = BTTypes<Profile, BT>;

    using CommonContainerPartsList = MergeLists<
        typename Base::CommonContainerPartsList, btss::FindName>;

    using RWCommonContainerPartsList = MergeLists<
        typename Base::RWCommonContainerPartsList,
        btss::InsertName, btss::LeafCommonName, btss::RemoveName>;

    using FixedLeafContainerPartsList = MergeLists<
        typename Base::FixedLeafContainerPartsList, btss::LeafFixedName>;
    using VariableLeafContainerPartsList = MergeLists<
        typename Base::VariableLeafContainerPartsList, btss::LeafVariableName>;

    using BlockIteratorStatePartsList = MergeLists<
        typename Base::BlockIteratorStatePartsList, btss::IteratorBasicName>;
};
```

It does **not** override `StreamDescriptors` — the user (Map, Set) is
expected to pass exactly *one* `StreamTF`, and BT_SS specialises:

- single-stream `find` / `insert` / `remove` paths (no structure-stream
  juggling),
- a `btss_batch_input.hpp::BTSSCtrBatchInputProviderBase<CtrT>` that takes
  rows of `(key, value)` and packs them into the single stream's
  substreams,
- a basic iterator part `btss::IteratorBasicName`.

**Use BT_SS** when the container is "one ordered/unordered sequence of
homogeneous rows": Map, Set, Vector. **Reach for BT_FL** when there are
multiple intermixed value streams (multimap), nested structures, or
dynamic-arity rows.

---

## 6. BT_FL — Free Layout

`prototypes/bt_fl/btfl_factory.hpp`:

```cpp
struct BTFreeLayout {};

template <typename Profile>
struct BTTypes<Profile, BTFreeLayout>: public BTTypes<Profile, BT> {
    using CommonContainerPartsList   = MergeLists<…, btfl::MiscName, btfl::ChecksName>;
    using RWCommonContainerPartsList = MergeLists<…, btfl::InsertName,
                                                     btfl::LeafCommonName>;
    using BlockIteratorStatePartsList = MergeLists<…, btfl::IteratorBasicName>;
};

template <typename Profile, typename T>
class CtrTF<Profile, BTFreeLayout, T>: public CtrTF<Profile, BT, T> {
    struct Types: Base1::Types {
        static constexpr int32_t DataStreams        = BaseTypes::Streams - 1;
        static constexpr int32_t StructureStreamIdx = DataStreams;
        using DataSizesT = core::StaticVector<typename BaseTypes::CtrSizeT, DataStreams>;
    };
};
```

The defining BT_FL convention: **the last stream in `StreamDescriptors` is
the structure stream**, an SSRLE-encoded symbol sequence indicating which
of the (`Streams - 1`) data streams the next logical element belongs to.

A multimap with key-stream `K`, value-stream `V`, structure-stream `S` can
represent

```
K v v K K v v v K v
```

as `K` symbols and `v` symbols in `S`, with `K` and `v` indices into
their own streams. This generalises to arbitrary-arity nested structures
(KV, KVV, KVK…), trie-like sequences, and unordered set/hash-map shapes
(structure stream encodes bucket boundaries).

The price: every leaf carries a third packed-struct
(`PkdSSRLESeq<DataStreams, 256, true>`), and every find/skip/select must
also walk it. The benefit: one B+tree code path supports
multimap, sequence, vector-of-records, hash-map, sequence-with-cuts —
basically every advanced data structure in the codebase.

`btfl_structure_chunk_iter.hpp` and `btfl_batch_input_provider.hpp` are the
SSRLE-aware iterators / batch inputs.

---

## 7. Branch vs Leaf nodes

`TreeNodeBase` (`branch_node.hpp:64-…`) is the common base, allocator-
backed:

```cpp
template <typename Metadata, typename Header_>
class TreeNodeBase {
    Header header_;
    int32_t root_, leaf_, level_;
    BlockID next_leaf_id_;
    PackedAllocator allocator_;
    enum { METADATA, BRANCH_TYPES, LEAF_TYPES,
           CTR_PROPERTIES, CTR_REFERENCES, MAX_METADATA_NUM };
    static const size_t StreamsStart = MAX_METADATA_NUM;
    …
};
```

The `PackedAllocator` is the byte-arena for that block. The first
`StreamsStart` slots are reserved for tree metadata; every stream's
substreams come after.

### Branch node

`BranchNode<Types>` (`branch_node.hpp`, around line 200+) lays out:

- *aggregated* substreams — for each leaf substream, the branch holds an
  index packed-struct (sum / fenwick / max), keyed by (column, row).
- a child-id substream — packed array of `BlockID`.

A branch's slots in `PackedAllocator` therefore look like:
`[metadata…][branch_substream_0][branch_substream_1]…[child_ids]`.

### Leaf node

`LeafNode<Types>` (`leaf_node.hpp`) lays out:

- `[metadata…][leaf_substream_0][leaf_substream_1]…` — full per-stream
  payload, no child ids.

The split between branch and leaf substream lists is built in
`bt_factory.hpp:209-211`:

```cpp
using BranchStreamsStructList = typename
    bt::PackedBranchStructListBuilder<CtrSizeT, StreamDescriptors>::StructList;
using LeafStreamsStructList   = typename
    bt::PackedLeafStructListBuilder  <CtrSizeT, StreamDescriptors>::StructList;
```

### Sparse Object (SO)

`branch_node_so.hpp`, `leaf_node_so.hpp`. An SO is a thin
`(ctr_, node_*)` pair carrying compile-time substream typing on top of a
runtime block. Its job:

- expose `substream<LeafPath>()` returning a typed view over a packed-
  struct slot,
- expose `processStream<…>(Fn, args…)`, `dispatch_substreams(…)`,
  `dispatch_substreams_subset(…)` that walk the typed substream tree at
  compile time,
- forward block-level ops (size, free space, split, merge) to the typed
  underlying node.

Because the SO is value-typed and constructed per dispatch, there are no
virtual functions on the hot path — the compiler instantiates one body per
node-type-times-functor pair.

---

## 8. Node Dispatcher (NDT)

`prototypes/bt/nodes/node_dispatcher.hpp`. Class `NDT0<CtrT, Types, Idx>` is
a recursive dispatcher over `Types::List` (the linearised list of all
concrete node classes — branch/leaf × variable/fixed × any user
customisations). At each level it compares `node->block_type_hash()`
against the `Head::BLOCK_HASH` of the `Idx`-th type:

```cpp
template <typename Functor, typename... Args>
auto dispatch(const TreeNodePtr& node, Functor&& functor, Args&&... args) const
    -> decltype(functor.treeNode(std::declval<NodeSO&>(), std::forward<Args>(args)...))
{
    if (HASH == node->block_type_hash()) {
        NodeSO node_so(ctr_, static_cast<Head*>(node.block()));
        return functor.treeNode(node_so, std::forward<Args>(args)...);
    } else {
        return NextNDT0(ctr_).dispatch(node, std::forward<Functor>(functor), …);
    }
}
```

The terminating case (`Idx==0`) throws if no hash matches.

### Why this beats a v-table

- Each `Functor::treeNode(SO&, …)` is **fully monomorphised** over `(node-
  type, functor-type)`, so the compiler sees the typed substream tree and
  can inline / specialise everything (per-substream loops unrolled, indices
  resolved, packed-struct ops inlined).
- The dispatch chain is `O(node-types)` `if`s, *not* a runtime table walk.
  In practice node-types ≤ 4 (BranchFixed, BranchVariable, LeafFixed,
  LeafVariable) so this is a 1-4 cmp+je sequence.
- One block hash per concrete node-type means cross-version compatibility
  on disk: `BLOCK_HASH` is content-derived from the substream typelist.

---

## 9. Shuttles — the algorithm/iterator dispatch layer

`prototypes/bt/shuttles/`:

```
bt_shuttle_base.hpp           — ForwardShuttleBase / BackwardShuttleBase / UptreeShuttle
bt_shuttle_pkd_ops.hpp        — typed substream ops (PkdFindSumFwFn, …)
bt_find_shuttle.hpp           — find-by-key / find-by-sum / find-by-max
bt_skip_shuttle.hpp           — skip-N-elements
bt_select_shuttle.hpp         — select(rank, symbol) on SSRLE / sums
bt_rank_shuttle.hpp           — rank(pos, symbol) inverse of select
```

A **shuttle** is an algorithm-as-object. It travels through the dispatcher
visiting branch-then-leaf nodes, and accumulates state (partial sums,
prefixes, position) as it goes.

`bt_shuttle_base.hpp:82-122`:

```cpp
template <typename Types_>
class ForwardShuttleBase {
    using LeafNodeTypeSO   = typename Types_::LeafNodeSOType;
    using BranchNodeTypeSO = typename Types_::BranchNodeSOType;

    bool descending_{};
    bool simple_ride_{true};
    uint64_t branch_nodes_{};

    virtual ShuttleOpResult treeNode(const BranchNodeTypeSO& node, size_t start) = 0;
    virtual void            treeNode(const BranchNodeTypeSO& node, WalkCmd cmd, size_t start, size_t end) {}
    virtual ShuttleOpResult treeNode(const LeafNodeTypeSO& node) = 0;
    virtual void            treeNode(const LeafNodeTypeSO& node, WalkCmd cmd) {}
    virtual ShuttleEligibility treeNode(const LeafNodeTypeSO& node, const IteratorState& state) const { return ShuttleEligibility::YES; }
    virtual void start (const IteratorState&) = 0;
    virtual void finish(IteratorState&)       = 0;
};
```

`ShuttleOpResult` is `(position, found, empty)` — the position is a child
index for branches and a within-leaf offset for leaves. `WalkCmd` is the
post-order callback (`FIX_TARGET`, …) used to compensate state after the
descent finishes (e.g. subtract the over-counted child sum on a backtrack).

### Concrete shuttle anatomy — Find by sum

`bt_find_shuttle.hpp:50-141`, `FindSumForwardShuttleBase`:

- Holds `target_`, running `sum_`, `column_`, `search_type_` (LT/LE/GT/GE)
  and `LeafPath`.
- On a branch:
  ```cpp
  using BranchStream = typename BranchNodeTypeSO::template BuildBranchPath<LeafPath>;
  size_t column = BranchNodeTypeSO::template translateLeafIndexToBranchIndex<LeafPath>(column_);
  return node.template processStream<BranchStream>(
      PkdFindSumFwFn(search_type_), column, start, target_, sum_);
  ```
  i.e. dispatch into the substream tree, run the typed `PkdFindSumFwFn`
  on the matching index struct.
- On a leaf: `tree.findForward(search_type_, column_, leaf_start_, target_-sum_)`
  on the actual data substream, returning a local position.

### Skip / select / rank are isomorphic

`SkipForwardShuttleBase` — skip N elements forward in stream `Stream` —
uses `IntList<Stream>` as `LeafPath` and `SearchType::GT` against the
StreamSize substream. `SelectForwardShuttleBase` — select(rank, symbol)
— uses an SSRLE substream (BT_FL structure stream) as path. Same
template, different leaf-ops binding.

This is the iterator/algorithm layer for *all* containers built on bt.
Map::find, Multimap::seek_value, Sequence::rank — every one is a Shuttle
parameterised over a `LeafPath` and a substream-op.

---

## 10. Iterators

`prototypes/bt/bt_iterator.hpp` and `iterator/bt_bis_base.hpp`. Iterators
are **block iterators with a tree path**:

```cpp
MEMORIA_V1_BT_ITERATOR_BASE_CLASS_NO_CTOR_BEGIN(BTBlockIteratorStateBase)
public:
    using TreeNodePtr      = typename Types::TreeNodePtr;
    using TreeNodeConstPtr = typename Types::TreeNodeConstPtr;
    using Position         = typename Types::Position;  // StaticVector<CtrSizeT, Streams>
    using TreePathT        = TreePath<TreeNodeConstPtr>;
private:
    TreePathT path_;       // root-to-leaf node-pointer stack
public:
    BTBlockIteratorStateBase() : Base() {}
    …
};
```

The iterator state is the `TreePathT` (vector of node pointers, one per
level), plus a per-stream local position (`Position` is `StaticVector
<CtrSizeT, Streams>`), plus container pointer. Movement is performed by
shuttles, not by the iterator itself: the iterator constructs a shuttle,
hands it `path_` and per-stream pos, the shuttle returns updated state,
the iterator commits.

Container-specific iterator parts (`btss::IteratorBasicName`,
`btfl::IteratorBasicName`, `multimap::*ChunkImpl`) add typed accessors
(`key()`, `value()`) on top of the block iterator state.

### Cursor semantics & concurrent updates

The path is *captured* — once you have an iterator, you have pointers to
specific blocks. In CoW mode, mutating ops produce *new* blocks; an
iterator over the old snapshot remains valid because the old blocks live
on. In no-CoW mode, the iterator may invalidate on mutation; the
`InsertBatch*` parts use `BlockUpdateManager` (`bt_factory.hpp:341`) to
manage in-flight cursors.

---

## 11. Batch updates

`bt_c_insert_batch_common.hpp` defines the abstraction:

```cpp
struct ILeafProvider {
    virtual TreeNodePtr get_leaf()              = 0;
    virtual Checkpoint  checkpoint()            = 0;
    virtual void        rollback(const Checkpoint&) = 0;
    virtual CtrSizeT    size() const            = 0;
};

class InsertBatchResult {
    size_t   idx_;
    CtrSizeT subtree_size_;
};
```

`InsertBatchCommonName` provides leaf-by-leaf insertion that consumes a
producer:

- The user supplies a `BTSSCtrBatchInputProvider` (BT_SS) or
  `BTFLCtrBatchInputProvider` (BT_FL) that walks an input source and
  packs leaves up to the size limit.
- `InsertBatchCommonName` consumes leaves from the provider and grafts
  them in. If a leaf doesn't fit at the target position it splits the
  current leaf and re-tries.
- `Checkpoint` lets the algorithm roll back if the upstream tree update
  fails partway.

The split between `InsertBatchFixedName` and `InsertBatchVariableName` is
the FIXED/VARIABLE leaf size discriminator (`bt_factory.hpp:282-292`).
Fixed-size leaves admit O(1) "fits / doesn't-fit" reasoning; variable-size
ones must consult the packed-struct's `block_size()`.

`bt_c_remove_batch.hpp` is the dual: range-remove with merge of adjacent
leaves below half-fill.

```cpp
void ctr_remove_entries(TreePathT& from_path, Position& from_idx,
                        TreePathT& to_path,   Position& to_idx,
                        bool merge = true);
```

---

## 12. CoW / no-CoW

Two persistence modes coexist as orthogonal parts. From `bt_factory.hpp`:

```cpp
IfThenElse<ProfileTraits<Profile>::IsCoW, bt::CoWOpsRName,  bt::NoCoWOpsRName>
IfThenElse<ProfileTraits<Profile>::IsCoW, bt::CoWOpsWName,  bt::NoCoWOpsWName>
```

### `bt_cr_cow.hpp` (CoW read ops)

```cpp
MEMORIA_V1_CONTAINER_PART_BEGIN(bt::CoWOpsRName)
    void ctr_unref_block(const BlockID& block_id) {
        auto& self = this->self();
        self.store().unref_block(block_id);
    }
MEMORIA_V1_CONTAINER_PART_END
```

### `bt_cr_no_cow.hpp`

```cpp
MEMORIA_V1_CONTAINER_PART_BEGIN(bt::NoCoWOpsRName)
MEMORIA_V1_CONTAINER_PART_END     // empty body
```

What differs:

- **CoW write side** (`bt_cw_cow.hpp`) — every block mutation goes through
  `store().mutable_block(...)` which clones-on-write; insert/remove paths
  that previously touched a block in place now go through a "clone, mutate,
  publish" sequence; `ctr_unref_block` is the per-snapshot ARC that the
  store uses to free unreferenced subtrees.
- **No-CoW write side** (`bt_cw_no_cow.hpp`) — direct in-place mutation;
  no ref counting; no clone-on-mutate.

What's shared:

- *All* of the algorithmic parts above (`bt_c_find`, `bt_c_insert`, the
  shuttles, the iterator path machinery) — they go through `store()` /
  `mutable_block()` abstractions and don't see the CoW choice.

This means the persistence policy is a *swappable axis*, not a fork.

---

## 13. Container parts pattern (the assembler)

The mechanism that makes the whole thing extensible is the
**`MergeLists` + `MEMORIA_V1_CONTAINER_PART_BEGIN(tag)` macro pattern**.

A part:

```cpp
MEMORIA_V1_CONTAINER_PART_BEGIN(bt::InsertBatchCommonName)
    using typename Base::BlockID;
    using typename Base::TreeNodePtr;
    …
    void ctr_insert_batch_to_node(…)   { … }
    InsertBatchResult ctr_insert_batch_at(…) { … }
MEMORIA_V1_CONTAINER_PART_END
```

The macro expands to a class template `XxxName<Base, Types>` parameterised
over the previous part in the chain. Then `bt_factory.hpp` linearises the
typelist into an inheritance ladder so that the final `Ctr<CtrTypes>`
inherits from every part, transitively.

`MergeLists<…>` is the joining operation; concrete containers compose their
own `RWCommonContainerPartsList` etc. by `MergeLists<Base::…, NewParts…>`.

The four orthogonal axes are:

1. **Read vs Write** (CtrList vs RWCtrList).
2. **Branch FIXED vs VARIABLE** (RWFixedBranchContainerPartsList vs RWVariableBranchContainerPartsList).
3. **Leaf FIXED vs VARIABLE** (analogous).
4. **CoW vs no-CoW** (selected by `ProfileTraits<Profile>::IsCoW`).

Plus the *prototype* axis — BT_SS / BT_FL append their own parts.
Plus the *container* axis — Map / Set / Multimap append their own API parts
(`map::CtrRApiName`, `map::CtrWApiName`).

This is the "open assembler" pattern — every assembly point is a typelist,
every implementation site is a part, no part is mandatory. The user can
append (almost) anything by overriding `BTTypes<Profile, MyContainer>` and
defining a part with `MEMORIA_V1_CONTAINER_PART_BEGIN(my::Name)`.

---

## 14. Bridge to mini-memoria (`pmap_v2`)

This section is the actionable one. For each big-Memoria concept, what it
maps to in the current `stdlib/std/data/persistent/`, and what
extension hook pmap_v2 should keep open if it is ever to grow.

Reminder from §0a: mini-memoria deliberately differs from big Memoria on
three structural axes — nodes are nested buffers instead of packed
4K blocks, in-memory only (no disk machinery), single implicit profile.
The advice below targets the **conceptual** extension points (substream
addressability, branch/leaf split, iterator state shape, profile axis,
shuttles); it does **not** advocate adopting `PackedAllocator` or any
on-block byte layout — those are out of scope.

### 14.1 What pmap_v2 currently is

(per `feat_pmap_v2_architecture.md`)

- 5 files: `btree.logos`, `view.logos`, `mutv.logos`, `store.logos`,
  `cfg.logos`.
- One concrete shape: `BTreeNode<K, V, const CFG: HermesStatic>` —
  *one* stream (key column + value column glued), *one* node type
  (no branch/leaf split at the type level — single struct, branching at
  `level/leaf` flags), *one* persistence mode (CoW path-copy).
- Generic over `<K, V, CFG>`; `CFG` is a HermesStatic carrying at least
  `fanout`. Phase-1 assembler `validate_pmap_cfg(cfg) -> i32` does shape
  checks.
- `LockingStore` is a typed-erased malloc-arena DAG owner; snapshots are
  tracked, but there is no profile abstraction. CoW vs no-CoW is fixed.

### 14.2 Concept-by-concept mapping

| Big Memoria                            | pmap_v2 today                              | Extension hook to keep open |
|----------------------------------------|--------------------------------------------|-----------------------------|
| `Profile`                              | `LockingStore` (single, hard-coded)        | Treat `CFG` as also the profile carrier OR add a separate `STORE: HermesStatic` const-generic. **Don't** bake `LockingStore` into the type signature of `BTreeNode`. |
| `BTTypes<Profile, X>`                  | implicit — methods are free-fns prefixed `_c`/`pmv2_` | Keep the *trait dispatcher* convention: every container method should be a free fn `op_X(view_or_mut, …)`, never a method on a concrete struct. That's what lets you later swap `BTreeNode` for `BTreeBranch` + `BTreeLeaf` without changing call sites. |
| `StreamDescriptors` (TypeList of `StreamTF`) | hard-coded "key column + value column" inside `BTreeNode` | Lift the **column shape** into CFG. Even if today CFG only carries `fanout`, document `cfg.streams: [{key_ty, val_ty}]` as a future field and have `cfg.logos` *ignore-but-pass-through* unknown CFG slots. |
| Stream / Substream                     | implicit — keys & values are SoA inside the node | Tag the existing key/value arrays as "substream 0" and "substream 1". Even informally — comment-block. When you later add a third column (e.g. SSRLE structure stream for set/multimap reuse), the node layout must be substream-indexed, not field-named. |
| Packed-struct catalogue                | inline `[K; N]` / `[V; N]` arrays          | Extract leaf-key-storage and leaf-value-storage to per-substream "ops" structs (`KeyStore::insert(node, idx, k)`) so they can be swapped (FSE → variable-codec → RLE) without rewriting `node_*_c`. |
| `BranchNode` / `LeafNode` distinction  | one struct, `is_leaf` flag                 | Long-term — split. Short-term — make sure all access goes through `node_view_keys(n) -> &[K]`, never `n.keys`, so a future `BTreeBranch::children()` substitution is mechanical. |
| `*_so.hpp` (Sparse Object views)       | `PMapView<K, V, CFG>` is the closest analog | Already correct in shape (CFG-templated view + free-fn API). Keep view types **value-typed** (no inheritance), and don't add per-method-monomorphic `PMapMutFanout4` etc. — the View is the SO. |
| `NDT0` runtime hash dispatch           | n/a — single node type                     | When you split branch/leaf, write a small `dispatch_by_kind(node, on_branch_fn, on_leaf_fn)` *function*. **Don't** introduce a virtual table or trait-object — the SO's monomorphisation property is the whole point. |
| Shuttles                               | n/a — `node_get_rec_c` is a recursive function | Iterators / range queries are explicitly noted as deferred. When they land, copy the **shuttle pattern**: a shuttle = a struct + a `descend(branch, start)`/`descend_leaf(leaf)` pair, *not* a Rust-style trait-object iterator. The shuttle owns its accumulator (sum, prefix, position). |
| Iterators (TreePath + Position)        | n/a                                        | Iterator state must be `TreePath<NodePtr>` + per-stream `[CtrSizeT; Streams]`, not a single "current leaf + offset". Even single-stream — the shape generalises. |
| Batch updates (ILeafProvider, Checkpoint, rollback) | n/a                                  | When you add bulk-insert: do **not** reuse single-key `pmap_mut_insert` in a loop — make a separate path that builds full leaves from a producer, with explicit rollback checkpoint. The producer/checkpoint duo from `bt_c_insert_batch_common.hpp` is the right shape. |
| CoW vs no-CoW as orthogonal axes       | only CoW (path-copy)                       | Do **not** assume CoW everywhere in the algorithm code. Funnel every "I'm about to mutate this block" through `store_mutable_block(store, id) -> *mut Node`. In the CoW build that clones; in a future no-CoW build that returns the same pointer. |
| Container parts (`bt_c_*` + `MergeLists`) | free-fn `_c` / `pmv2_` prefixes         | The current free-fn convention IS the parts-list pattern done in flat namespaces. To grow: keep separating concerns into separate `.logos` files (`btree.logos` = node ops, `view.logos` = read ops, `mutv.logos` = write ops, `store.logos` = persistence ops). When BT_FL-style structure-stream is added, it goes into a new file `btree_structure.logos`, not bolted into `btree.logos`. |
| BT_FL (multimap, set-on-mmap, …)       | n/a                                        | The CFG-driven layout claim ("fanout drives layout") is the embryo of this. Generalise by allowing `CFG.streams` to be a list. When you reach for a Set on top of pmap_v2, do it by adding a structure-stream column `[bool; N]` (per-key sentinel) — and **not** by writing a separate `pset.logos` from scratch. |

### 14.3 Premature pessimisations to avoid in pmap_v2

In rough priority order — these are design decisions that, if locked in
now, would force a rewrite when generalising toward big-Memoria:

1. **Fixing the node struct.** `struct BTreeNode<K, V, CFG> { keys: Buffer<K>, vals: Buffer<V>, children: PrimVec<Ptr>, … }` looks fine for one container. Bake "exactly two value substreams + one child column" into the algorithm code and you've forfeited multimap, set-on-pmap, sequence-on-pmap. *Mitigation* (without going to packed layout): every accessor reads/writes through *named substream functions* — `node_keys(n)` / `node_values(n)` / `node_substream(n, idx)` — never `n.keys` directly. Then a future BranchNode + LeafNode split, or adding a third buffer per node, only changes the accessors, not the call sites.
2. **Carrying `LockingStore` in `pmap_v2` types.** Today `Snapshot<K,V,CFG>` references `LockingStore`. If `Profile` is ever to mean anything (transient arena, mmap'd file, RDMA), the store must be a const-generic or a runtime trait object behind a single allocator handle, not a concrete `LockingStore`. *Mitigation:* alias it now: `type DefaultStore = LockingStore;` and use the alias everywhere.
3. **Method-on-`PMapView` style.** Every fn that takes `&PMapView<K,V,CFG>` and reads-only is fine; every fn that takes `&mut PMapMut<K,V,CFG>` and mutates is fine. The trap is to start writing `impl<K,V,CFG> PMapMut<K,V,CFG> { fn insert(...) }` because Logos allows it — that locks the API surface to one container. *Mitigation:* keep the free-fn convention. The "container API" is a list of free fns; that *is* the container parts list.
4. **Single iterator state.** Resist `(leaf_ptr, idx)` iterator state. `(TreePath, Position)` is one extra `Vec<NodePtr>`, but it's the only way to support concurrent iterators across CoW snapshots and BT_FL structure-stream walks.
5. **CFG as a flat record.** Phase-1 of the assembler does shape checks. Document up front that CFG is **a recursive Hermes record with at least these slots**: `fanout: i32`, `key_ty`, `val_ty`, `streams: [...]`, `branch_storage`, `leaf_storage`. Even if `validate_pmap_cfg` only checks `fanout` today, every other field should be tolerated as `null`. Adding fields later will break code that pattern-matches the CFG schema.
6. **Linear-search-only vs binary-search-only choice.** Big Memoria's branch index can be sum / fenwick / radix / max — chosen per stream. `node_get_rec_c` (linear) and `node_get_rec_binary_c` (binary) is the right embryo, but the choice should be a CFG slot (`leaf_search_kind`), not a separate function name. *Mitigation:* let CFG carry `leaf_search: enum {Linear, Binary, …}` and dispatch on it inside one fn.
7. **Sealing the `_c` / `pmv2_` prefixes.** They exist because the Logos module loader globalises function names. If/when that's lifted, the prefixes become dead weight on the API. *Mitigation:* document them as a current loader limitation, not a permanent naming convention.
8. **No "structure stream" hook.** The first non-trivial generalisation (set, multimap) will need an SSRLE column. Even before SSRLE exists in stdlib, write `node_structure_kind(node, idx) -> i32` returning a constant `0` for pmap_v2. Future containers override it.

### 14.4 Order in which to grow pmap_v2 toward big-Memoria

1. **Internal substream offset table** in `BTreeNode` (mechanical refactor).
2. **Branch / leaf split** at the struct level, with a kind-tag.
3. **Iterator with TreePath + Position** (still single-stream).
4. **Shuttle abstraction** for find/skip/select (single-stream first).
5. **Batch insert** with provider + checkpoint.
6. **Profile abstraction** — `STORE: HermesStatic` slot on top of CFG.
7. **No-CoW backend** as a second profile (transient arena).
8. **Multi-stream CFG** — `cfg.streams: [...]`.
9. **Structure stream** — first BT_FL-style container (multimap).
10. **Container parts as separate `.logos` files**, merged at build time.

Steps 1-5 don't require new compiler features. Step 6 needs the CFG/profile
distinction landing cleanly. Step 9 needs SSRLE in stdlib (or a stand-in
packed sequence type).

---

## Open questions

The following are concepts I saw referenced but couldn't confirm purely
from the headers in this read:

- **`BlockUpdateManager<CtrTypes>`** (`bt_factory.hpp:341`) — looks like an
  in-flight cursor invalidation tracker for no-CoW writes; the body lives
  outside `prototypes/bt/`.
- **Hash collision handling on `BLOCK_HASH`.** `NDT0` matches by
  `block_type_hash()` — there's a `static_assert` somewhere generating these
  hashes per-node-type, but I didn't trace its disambiguation logic.
- **Exact contract of `WalkCmd::FIX_TARGET`** post-order callback — clearly
  used by find-by-sum to subtract the over-counted child sum, but the full
  state-machine of walk commands isn't documented in the shuttle base.
- **`BTSSCtrBatchInputProviderBase`'s relationship to `CtrInputBuffer`** —
  the buffer type comes through `CtrT::Types::CtrInputBuffer` and is
  defined further out; the producer/consumer protocol's full lifecycle
  (back-pressure, leaf-fill, merging) is in `btss_batch_input.hpp` past
  what I read.
- **Concurrent iteration semantics on no-CoW.** Code paths exist
  (`BlockUpdateManager`) but the precise guarantees aren't pinned down in
  these headers.

Citations in this document are accurate as of the files read; I have not
verified every cross-reference against the current build.
