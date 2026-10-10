# ADR 0039. Persistent snapshottable structures: one logical level, many implementations

Status: PROPOSED for pair review (2026-10-10). Part of the set under [0032](0032-logos-core-composition.md). Refines [0032](0032-logos-core-composition.md) D7 / row C7 and [0033](0033-incremental-deem-dbsp-translator.md) D6 / row I8. Inputs: [0017](0017-deem-storage-engine.md), [0018](0018-memoria-port.md), [0020](0020-memoria-container-interface-plane.md), [0027](0027-row-store-tables.md). Victor, 2026-10-10: «Я не против сделать уровень инкрементности снэпшотами Мемории, но тогда Меморию нужно будет универсализировать, и это должно быть включено в планы. Например, в виде двух уровней: логический концептуальный уровень персистентных снэпшотируемых структур данных и множество возможных реализаций (где текущая Мемория — один из вариантов).»

## Problem

ADR 0033 D6 makes the parent snapshot the z⁻¹ of an incremental Deem circuit and lets Memoria containers capture changes by snapshot diff. That ties incremental Deem to one storage implementation unless the snapshot is a concept with several implementations. Today it is not.

### Measured state (2026-10-10)

- **Two persistent stacks, neither a logical level.**
  - `logos.mem.bt` (11 631 lines): `trait Snapshot` (`map.logos:72-135`) is the public object-safe tier — id plane (`snapshot_id`, `snapshot_uid` → `Uid256`), lifecycle (`commit`, `remove`, `status`), lineage (`snapshot_parent`, `has_parent`, `set_as_branch`, `set_as_master`), and residency pins whose leaf form takes a block address (`leaf_pin(*const u8)`, `:132`). `trait Store` is an empty marker (`:52`, "lifecycle hand-written per store"); no trait says "fork" or "open a container". `Ctr` has `size` only (`:143`). The tree traits `BtLeaf` / `BtBranch` (`:242-335`) are block-specific.
  - `MemoryStore` / `MemorySnapshot` (`memstore.logos`): a snapshot DAG with ONE parent per snapshot (`SnapDesc.parent`, `:95`), named branches, status ACTIVE / COMMITTED / DROPPED (`:81-83`), `store_fork*`, history queries (`store_linear_history`, `store_children_of`, `store_heads`, `:920-1019`), `store_pack` (reclaims dropped leaves), `store_save` / `store_load` (`:1196,1281`, committed snapshots only).
  - `logos.lcm.deem.data` (ADR 0017 survivor): `trait IStore` (`store.logos:63`) with MULTI-PARENT merge commits (`register_merge_commit`, `:80`; `Store::merge` select-one, `handle.logos:409-423`), commit labels, a first-parent `log`; `Arc<dyn BTreeNode>` tree; no save/load, no pack. Consumers: 28 `data_*` / `derive_branch_node_*` pass fixtures, 1 fail fixture, two examples, `derive_branch_node.logos`.
- **The privileged tier is a downcast to one concrete type.** Node allocation, child-ref resolution and the directory (`root_of` / `set_root`) are inherent methods of `MemorySnapshot`, reached by a `dyn_data` downcast hard-coded in every Canon-generated family (`container_item.logos:1228,1231,2688,2695,3055,3062`, "generated for MemoryStore"). `map.logos:70-71` already names the missing piece: "a `trait NodeStore: Snapshot` once a second store lands".
- **No diff between snapshots.** Nothing computes the changes from one snapshot to another (grep: none). ADR 0017 P1 deferred it ("producer (B)", `0017:106-116`); ADR 0020 asks for `changes(since) → Z-set` (`0020:97`); ADR 0033 D6 assumes it.
- **No in-memory persistent structures** besides the two CoW b+trees (no HAMT, RRB, persistent list / map); `mem.collections.BTreeMap` is a sorted array mutated in place.
- **The C++ Memoria port seam of ADR 0018** (`IStore` / `ISnapshot` / `ICtr`, `0018:179-186`) does not exist under those names; `conuco/memoria/src/store/store.logos` is a 14-line stub; the real seam is `logos.mem.bt.map`.
- **The relational interface is already logical.** The query plane's source traits (`MapSource`, `OrderedMapSource`, `PositionalSource`, `logos.std.wql.source`, #342) and the source members (`rel` / `op` / `size` / `order` / `unique`, #322) describe a structure by what relational operations it executes, independently of its representation.
- **Consumers of the current store API**: 104 pass and 19 fail fixtures name `store_new`, `create_ctr`, `memstore` or `Snapshot` (32 of them `memoria_*`).

## Decisions

### D1. Two levels

- **Logical level (Core).** Traits and laws for versioned, persistent, snapshottable data: what a snapshot is, what forking and committing mean, what a persistent structure promises, and how changes between versions are read. No blocks, pages, allocators or node layouts.
- **Implementation level (many).** Each implementation provides the logical traits and declares its properties (durability, cost of fork, cost of diff, concurrency). Memoria's CoW b+tree on a block store is one implementation; Canon is a factory of implementations, not the owner of the interface (ADR 0032 D9).

A program, a Deem query and an incremental circuit are written against the logical level; choosing an implementation is a binding decision, not a rewrite.

### D2. The logical vocabulary

| Trait | Promise |
|---|---|
| `Version` | an opaque, totally comparable-within-a-lineage identity of a state (`Uid256` remains the boundary representation) |
| `Snapshot` | an immutable state: reads only; `version()`, `parents()` (≥ 0; more than one after a merge) |
| `Workspace` | a mutable state forked from a snapshot: writes; `commit() -> Snapshot`, `abort()` |
| `VersionStore` | the lineage: `fork(&Snapshot) -> Workspace`, `heads()`, named branches, `log`, `merge` (D6), `release` |
| `Persistent<S>` | a structure `S` living in a snapshot / workspace: opened by id, read through `&Snapshot`, written through `&mut Workspace` |
| `Versioned` (change capture) | `delta_since(&Version) -> Delta<Row>` and `version()`: the change of a structure's relation between two versions of one lineage |

The relational face of a persistent structure is its source declaration (ADR 0032 D4): a persistent ordered map is an `OrderedMapSource` with its `op` / `size` / `order` / `unique` members, plus `Versioned`. ADR 0033's `DeltaSource` is this `Versioned` trait; it is not declared separately.

The block-specific traits (`BtLeaf`, `BtBranch`, `NodeProfile`, `BlockStore`) and residency pins move to the implementation level. Pins become an implementation capability (`Resident`), asked for only by code that already knows it runs on a paged implementation (ADR 0025 §7).

### D3. Laws

Each law is a property every implementation must satisfy, checked by one law suite run against every implementation (row P5):

1. A committed snapshot never changes: every read of it returns the same answer.
2. A fork's writes are invisible to its parent and to sibling forks.
3. Change capture is exact: for versions `v ≤ w` of one lineage, `state(w) = state(v) ⊕ delta_since(v)` at `w`, as Z-sets over the structure's relation.
4. `delta_since(version())` is empty.
5. Composition: `delta_since(u)` at `w` equals `delta_since(u)` at `v` plus `delta_since(v)` at `w`, for `u ≤ v ≤ w`.
6. Fork cost is not proportional to the state: an implementation declares its fork cost class, and `O(state)` is admitted only for an implementation that declares it (a debugging copy).
7. Release: a released snapshot's resources are reclaimed once no workspace or reader holds it; reading a released snapshot is impossible by construction (lifetimes), not a runtime error.

### D4. Implementations

| Implementation | What | Fork | Diff | Durability |
|---|---|---|---|---|
| **Memoria** | today's `logos.mem.bt` CoW b+tree on a block store, Canon families | O(1) (shared directory root) | tree diff skipping shared subtrees by snapshot-id stamp | `store_save` / `load`; later a real block store |
| **In-memory persistent** | structurally shared trees (persistent B-tree / HAMT for maps, RRB for vectors) on the heap, no block store | O(1) | same skipping diff over shared nodes | none (process memory) |
| **Journaled `mem`** | an instrumenting wrapper over `Vec` / `HashMap` / `BTreeMap` keeping a change log per version (ADR 0013 option A) | O(1) (a log position) | the log slice between versions | none |
| later: distributed, log-structured | the same traits over a transport / log | per implementation | per implementation | per implementation |

The in-memory implementation is the default for ordinary programs and tests (no store to open); Memoria where persistence or out-of-core size is needed; the journaled wrapper for existing code that owns plain collections.

### D5. The privileged tier becomes a trait

The `dyn_data` downcast to `MemorySnapshot` in generated code is replaced by an implementation-level trait (`NodeStore: Snapshot`, as `map.logos:70-71` anticipates) that a block-based implementation provides; a Canon family is generated against `NodeStore`, not against `MemorySnapshot`. A family can then live in any block-based implementation; a non-block implementation (in-memory persistent) gets its own families or a generic one.

### D6. Merge

The logical level admits multi-parent snapshots (`parents()`), because `logos.lcm.deem.data` already has merge commits and branch workflows need them. The merge OPERATION is a policy, not a law: `select_one` (ADR 0017 D2 v0, what `deem.data` implements) first; a relational merge (`edb_union` / three-way over a structure's relation, ADR 0017 v1) later, with conflicts reported as data. An implementation that cannot merge says so (`merge` is an optional capability). Schema-divergent merge (ADR 0020 open question) stays open.

### D7. Incremental Deem binds to the logical level

ADR 0033's circuit state lives in a `Workspace`; a tick is a commit; the z⁻¹ of the circuit is the parent snapshot; input deltas of `Versioned` sources are read by `delta_since`. Checkpoint, fork and replay of a circuit are logical-level operations available on every implementation. The ADR 0033 I8 acceptance runs ONE circuit on two implementations (in-memory persistent and Memoria) and requires equal answers per tick.

### D8. What happens to the two stacks

- `logos.mem.bt` becomes the Memoria implementation of the logical level: `Snapshot` / `Ctr` split into the logical traits (D2) and the implementation traits (D5); `MemoryStore` implements `VersionStore` (single-parent today; `parents()` returns one).
- `logos.lcm.deem.data`: its multi-parent merge and labels are the reference for D6; they are ported into the logical level and the Memoria implementation (ADR 0032 O3: port the missing property first), then the package is deleted with its consumers moved (ADR 0032 C7). It does not survive as a third implementation: its tree duplicates `mem.bt` without persistence or pack.

### D9. Naming and placement

The logical traits live in Core's `mem` tier (a persistent structure is a data structure, not IO); implementations live where their dependencies allow (in-memory persistent in `mem`, Memoria's store layer in `mem` with its block-store backends in `lcm` / `std` when they do IO). Names are settled in review (O1).

## Rows

| Row | Content | Acceptance |
|---|---|---|
| P0 | census: every use of `Snapshot` / `Ctr` / `Store` / `store_*` / `create_ctr` / `MemorySnapshot` downcast / `logos.lcm.deem.data`, mapped to its trait under D2 / D5 | ledger file, pinned by a gate |
| P1 | logical traits (D2) and laws (D3) in the `mem` tier | trait fixtures; the law suite (P5) compiles against a trivial reference implementation |
| P2 | Memoria as an implementation: `logos.mem.bt` split into logical and implementation traits; `NodeStore` replaces the `dyn_data` downcast in Canon (D5) | every `memoria_*` / `ctr_*` / `deem_*` store fixture green unchanged in answers; the downcast census → 0 |
| P3 | merge and labels ported from `deem.data` (D6, D8); `deem.data` deleted, its fixtures ported | `data_merge_heads`, `data_dag_topology` ported and green; package gone |
| P4 | change capture `delta_since` in Memoria (shared-subtree skipping diff) | laws 3-5 on randomized histories; diff cost measured against state size |
| P5 | the law suite: one set of property tests run against every implementation | a planted law violation in a test implementation turns it red |
| P6 | in-memory persistent implementation (map, ordered map, vector) with `delta_since` | law suite green; Deem source fixtures run on it |
| P7 | journaled `mem` wrapper (D4) | law suite green on the wrapper |
| P8 | ADR 0033 binds to the logical level (D7): circuit state in a `Workspace`, I8 on two implementations | one circuit, two implementations, equal per-tick answers |

Ordering against the set: P0-P2 before ADR 0033 I2 (so the translator's state model is written against D2); P3 with ADR 0032 C7; P4-P8 before ADR 0033 I8. ADR 0033 I2-I7 do not wait for P4-P8: they run on caller-provided deltas.

## Open

- O1. Names: `Workspace` vs `WriteSnapshot` vs Memoria's own vocabulary; `Versioned` vs `DeltaSource` (ADR 0033 uses the latter).
- O2. Version ordering across branches: `delta_since` is defined within a lineage (`v ≤ w`); whether it is also defined between siblings (as a three-way diff through the common ancestor) or refused.
- O3. Granularity of change capture: per structure (as above) or per snapshot (all structures at once, the natural unit of a Deem tick over several sources).
- O4. Concurrency: SWMR (ADR 0018 M5) as an implementation property or a law of the logical level.
- O5. Persistence as a law or a capability: the in-memory implementation has none; durability is declared per implementation, and a program that needs it asks for an implementation that declares it.
- O6. Whether `Ctr` survives as a logical notion (a named structure in a snapshot directory) or is replaced by `Persistent<S>` opened by id.
