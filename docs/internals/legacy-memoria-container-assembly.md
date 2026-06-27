# Legacy Memoria — Container & BTree Node Assembly

> **Scope.** This document describes the **inherited "big" Memoria**, the
> mature C++ codebase at `~/cxx/memoria/{core,containers,containers-api}` that
> Logos's `std.data.persistent` ("mini-Memoria") draws inspiration from. It is
> a reference note: how containers and B-tree nodes are *assembled* there, and
> which idioms we want to translate into Logos metaprogramming rather than
> port verbatim. Nothing in this file describes Logos code — see
> [`std.data.persistent`](../../stdlib/std/data/persistent/) and the
> `project_persistent_*` memory entries for the Logos-side design.

The legacy codebase is heavy template metaprogramming (TMP) on C++20. Container
classes, node classes, dispatchers and packed-leaf encodings are all synthesised
at compile time from typelists. What follows is the call-graph of types you walk
through to understand a single instantiation like `Ctr<Map<Varchar, Writ>>`.

## 1. Tag classes name the parts

`memoria/prototypes/bt/bt_names.hpp` declares a flat list of empty classes —
`ToolsName`, `FindName`, `InsertName`, `BranchVariableName`, `LeafFixedName`,
`CoWOpsRName`, `CtrRApiName`, ... — one per *part* (concern) of a container.
A part is a coherent slice of behaviour: find logic, insert logic, leaf-fixed
specialisation, CoW operations, container-specific read API, and so on.

Each part lives in its own header (`bt_c_find.hpp`, `bt_c_insert.hpp`,
`map_cr_api.hpp`, ...) where it **partial-specialises** a template like
`Container<Types, FindName>`, inheriting from a `Base` that resolves to the
*next* part in the list. The whole container is built by chained inheritance
through that list.

Tags are pure keys — no methods, no data. Identity is by C++ type.

## 2. `BTTypes<Profile, ContainerName>` — the per-container declaration

`memoria/prototypes/bt/bt_factory.hpp` defines the primary `BTTypes` template
with default lists (`ContainerPartsList`, `RWContainerPartsList`,
`FixedBranchContainerPartsList`, `VariableLeafContainerPartsList`,
`NodeTypesList`, ...) and slots for the user — `StreamDescriptors`,
`CommonContainerPartsList`, `RWCommonContainerPartsList` default to empty
typelists.

Each container specialises `BTTypes`. For Map (see
`memoria/containers/map/map_factory.hpp`):

```cpp
template <typename Profile, typename Key_, typename Value_>
struct BTTypes<Profile, Map<Key_, Value_>>:
        public BTTypes<Profile, BTSingleStream>,
        public ICtrApiTypes<Map<Key_, Value_>, Profile>
{
    using Key   = Key_;
    using Value = Value_;

    using CommonContainerPartsList = MergeLists<
        typename Base::CommonContainerPartsList, map::CtrRApiName>;
    using RWCommonContainerPartsList = MergeLists<
        typename Base::RWCommonContainerPartsList, map::CtrWApiName>;

    using LeafKeyStruct   = typename map::MapKeyStructTF<Key_>::Type;
    using LeafValueStruct = typename map::MapValueStructTF<Value_>::Type;

    using StreamDescriptors = TL<
        bt::StreamTF<
            TL<TL<StreamSize>, TL<LeafKeyStruct>, TL<LeafValueStruct>>,
            map::MapBranchStructTF>
    >;
};
```

What this contributes:

- Adds two map-specific parts (`map::CtrRApiName`, `map::CtrWApiName`) to the
  inherited common-parts lists.
- Picks **packed-struct codecs** for keys and values via "type functions"
  (`MapKeyStructTF<Key>::Type`) — these resolve `Varchar` to a packed
  variable-length codec, `i64` to a fixed-size codec, and so on.
- Declares the `StreamDescriptors` typelist that the leaf/branch builders
  consume to compute the actual on-disk layout of leaves and branches.

`BTTypes<Profile, BT>` is the prototype default; map-specialisation extends it.
This is the *declaration* of the container; assembly happens in `CtrTF`.

## 3. `CtrTF<Profile, BT, ContainerTypeName>` — the assembly factory

The non-trivial half of `bt_factory.hpp`. Given `Profile` and the user-facing
container name (e.g. `Map<Varchar, Writ>`), `CtrTF` produces:

- `BranchStreamsStructList` / `LeafStreamsStructList` by running
  `PackedBranchStructListBuilder` / `PackedLeafStructListBuilder` over
  `StreamDescriptors`.
- A two-way Fixed-vs-Variable selection per layer:
  ```cpp
  using CtrListLeaf = IfThenElse<
      LeafSizeType == PackedDataTypeSize::FIXED,
      typename ContainerTypes::FixedLeafContainerPartsList,
      typename ContainerTypes::VariableLeafContainerPartsList>;
  ```
  — Memoria has separate parts for fixed-size and variable-size leaf/branch
  storage; the right one is chosen by `PackedListStructSizeType` over the
  computed struct list.
- `CtrList` and `RWCtrList`: the final chain-inheritance backbones, formed by
  `MergeLists<>` of base parts, container parts, leaf-storage parts, and
  branch-storage parts.
- `BlockDispatchers`: see §4.
- `BranchNodeEntry_`: a tuple per stream computed by `BranchNodeEntryBuilder`,
  used as the parent-node summary type.
- A nested `Types` struct exporting everything downstream code needs:
  `LeafType`, `BranchType`, `Position`, `IteratorBranchNodeEntry`,
  `LeafPathT<i>`, `BranchPathT<i>`, `TargetType<LeafPath>`, `KeyOrderingType`,
  `LeafPackedStruct<Ctr, Path>`, ...
- Finally:
  ```cpp
  using Type   = Ctr<CtrTypes>;     // read-only container class
  using RWType = Ctr<RWCtrTypes>;   // read-write
  ```

`Ctr<...>` is the public container class. Its full member surface is the union
of every part's specialisation, materialised by inheritance.

## 4. Node assembly — `NodePageAdaptor` + `BTreeDispatchers`

`memoria/prototypes/bt/nodes/node_list_builder.hpp` builds the runtime
node-class set:

- `BranchNodeTypes<BranchNode>` and `LeafNodeTypes<LeafNode>` are templates
  parametrised by the user's node template (defaulting to `bt::BranchNode` and
  `bt::LeafNode`).
- `BTTypes::NodeTypesList = TL<BranchNodeTypes<BranchNode>, LeafNodeTypes<LeafNode>>`.
- `NodeTypeListBuilder<BranchTypes, LeafTypes, NodeTypesList>` recursively
  walks that list, applying `NodePageAdaptor<NodeT, Types>` to each entry, and
  produces three flat lists: `AllTypesList`, `LeafTypesList`,
  `BranchTypesList`.
- `BTreeDispatchers<DispatcherTypes>` exposes
  `NodeDispatcher<CtrT, AllDTypes>` (and `Leaf-`, `Branch-`, `Default-`,
  `Tree-` variants) — these are the type-erased, hash-keyed dispatchers used at
  runtime to recover the concrete node class from a serialised block.

The actual node class — `bt::LeafNode<Types>` in
`prototypes/bt/nodes/leaf_node.hpp` — derives from `Types::NodeBase`
(`TreeNodeBase<Metadata, BlockType>`), declares
`RootMetadataList = MergeLists<Metadata, PackedTuple<BranchExtData>,
PackedTuple<LeafExtData>, PackedMap<Varchar,Varchar>, PackedMap<Varchar,CtrID>>`,
and a `Dispatcher = PackedDispatcher<StreamDispatcherStructList>` that walks
the packed substreams stored inside the node's `PackedAllocator`. Its
`serialize` / `deserialize` / `cow_serialize` / `cow_resolve_ids` /
`for_each_child_node` all defer to `Dispatcher::dispatchNotEmpty(...)` over a
small functor (`SerializeFn`, `CowSerializeFn`, `MemCowResolveIDSFn`, ...).

## 5. Type-hash as runtime identity

```cpp
template <typename Types>
struct TypeHash<bt::LeafNode<Types>> {
    static const uint64_t Value = HashHelper<
        TypeHashV<typename Node::Base>, Node::VERSION,
        true, TypeHashV<typename Types::Name>>;
};
```

Every monomorphisation of every node template gets a stable 64-bit hash. The
serialised block carries `header_.ctr_type_hash()` / `header_.block_type_hash()`;
the dispatcher uses those to recover the right C++ class on read.

This is the legacy analogue of what we already do in Logos with the
`WritTypeTagSystem`/type-code dispatch (see memory: `feat_tag_dispatch`,
`project_writ_trait_registry`). Same pattern, different mechanism.

## 6. Packed-struct list builder — the "what does a leaf actually look like"

The `StreamDescriptors` declared in `BTTypes` is purely declarative. The real
work is in:

- `PackedLeafStructListBuilder` — walks the descriptors, applies
  per-substream type-functions (TFs), produces `LeafStreamsStructList` (the
  literal list of packed-struct types laid out inside a leaf, in order).
- `PackedBranchStructListBuilder` — analogous for branch nodes (one packed
  struct per stream that holds the parent-side summary, e.g. accumulated key
  for ordering).
- `PackedDispatchersListBuilder` — assigns substream indices and produces the
  dispatcher list used by the leaf to walk its substreams.

The packed structs themselves (`PackedDataTypeBuffer<...>`,
`PackedTuple<...>`, `PackedMap<...>`) live in `memoria/core/packed/`. Each one
is a POD layout with an SO-wrapper (`...SO`) that carries the run-time ext-data
(codec dictionaries, ordering tags, etc.) needed to interpret the buffer.

## 7. Variability axes — what gets selected at the type level

Across the assembly there are several orthogonal axes, all resolved at the
type level:

| Axis | Branches | Where it's resolved |
|---|---|---|
| Read-only vs read-write | `CtrList` vs `RWCtrList` | `CtrTF::Type` vs `CtrTF::RWType` |
| Fixed vs variable leaf | `FixedLeafContainerPartsList` vs `VariableLeafContainerPartsList` | `IfThenElse` on `LeafSizeType` from `PackedListStructSizeType<LeafStreamsStructList>` |
| Fixed vs variable branch | `FixedBranch...` vs `VariableBranch...` | `IfThenElse` on `BranchSizeType` |
| CoW vs in-place updates | `bt::CoWOpsRName` / `bt::CoWOpsWName` vs `NoCoW...` | `IfThenElse` on `ProfileTraits<Profile>::IsCoW` |
| Per-container parts | `CommonContainerPartsList` / `RWCommonContainerPartsList` | overridden in `BTTypes<Profile, ContainerName>` |
| Container extensions | `CtrExtensionsList` / `RWCtrExtensionsList` / `BlockIterStateExtensionsList` | profile-specific TFs |
| Key/value codec | `LeafKeyStruct`, `LeafValueStruct` | per-container TFs (e.g. `MapKeyStructTF<Key>::Type`) |
| Stream layout | `StreamDescriptors` typelist | per-container declaration |
| Container prototype | `BT`, `BTSingleStream`, `BTFixedLength`, `BTSingleStreamSimple`, ... | base of `BTTypes` specialisation |

The result is that adding a new container — e.g. a new Map specialisation, a
sequence, a multimap — boils down to:

1. Pick a prototype (`BTSingleStream`, `BTFixedLength`, ...).
2. Specialise `BTTypes<Profile, MyContainer<...>>` with the right
   `StreamDescriptors`, key/value TFs, and any extra parts.
3. Add per-container parts (read API, write API) as partial specialisations
   keyed on tag classes you declare.

No code generation, no preprocessor — just typelists and partial
specialisation.

## 8. Why this is interesting for Logos metaprog

The legacy assembly is, in essence, **a metaprogram written in the C++ type
system**. The inputs are typelists; the outputs are class hierarchies and
packed-struct layouts; the operators are `MergeLists`, `IfThenElse`,
`Linearize`, `mp_transform`, plus per-domain TFs.

What translates directly to our metaprogramming substrate:

- **Tag classes for parts → metafunction-keyed parts.** A Logos metafunction
  selects parts by query (`meta_has_trait`, `meta_kind_preds`) instead of by
  tag-list inheritance. No empty tag classes; the "which parts apply" decision
  is an explicit query result.
- **`IfThenElse` axes → `if`/`match` in metafunction.** Read-only vs RW,
  Fixed vs Variable, CoW vs no-CoW, etc. become ordinary control flow in a
  Logos metaprogram, not type-level conditionals.
- **`StreamDescriptors` → typed AST data.** The descriptor is a description
  of the leaf layout; we can author it as Logos data (`Vec<StreamDesc>`) and
  consume it in a metaprogram to synthesise the leaf struct via
  `quote_item!` / `quote_ty!`. No `Linearize`, no `mp_transform` — plain
  iteration.
- **`TypeHash<LeafNode<Types>>` → existing tag-dispatch.** We already mint a
  stable type-code per monomorphisation through `WritTypeTagSystem`; the
  legacy mechanism is a special case.
- **Chain inheritance → composed `impl` blocks (or `Pass` in Phase 2).** The
  "every part contributes methods through inheritance" pattern collapses into
  the Logos generative phase emitting the methods directly on the container
  type, possibly under a `Pass<Rewrites, Diagnostics>` umbrella when whole-
  program transforms land.

What does **not** translate, and we should not try to recreate:

- **Tag-list as the API surface.** In legacy Memoria a user composes a
  container by writing a `BTTypes` specialisation with the right list. In
  Logos the equivalent is a metafunction call — the input is data, not a
  typelist, and the output is items, not a class hierarchy.
- **`PackedDispatcher` over substream indices.** Our leaf storage sits on
  Writ containers and `Buffer<DT>`; the packed-substream zoo collapses to
  one `Buffer` parameterised by the key/value Datatype, plus pointer arrays
  for children. The "walk substreams" pattern becomes "walk one or two
  buffers", erasing most of the dispatcher machinery.
- **`NodePageAdaptor` / `BTreeDispatchers` typelist plumbing.** The legacy
  setup exists because C++ has no other way to do tag-keyed runtime dispatch;
  we have the dispatch mechanism as a first-class language feature.

## 9. Files touched in this overview

For future readers wanting to map these notes back to legacy code:

- Container declaration: `memoria/containers/map/map_factory.hpp`,
  `map_names.hpp`.
- Prototype: `memoria/prototypes/bt/bt_factory.hpp`, `bt_names.hpp`.
- Parts: `memoria/prototypes/bt/container/bt_c_*.hpp`,
  `memoria/containers/map/container/map_c[rw]_api.hpp`.
- Nodes: `memoria/prototypes/bt/nodes/{branch_node,leaf_node,node_dispatcher,
  node_list_builder,tree_metadata}.hpp`.
- Packed primitives: `memoria/core/packed/`.

This is reference-only material; nothing here is a target API for our Logos
mini-Memoria.
