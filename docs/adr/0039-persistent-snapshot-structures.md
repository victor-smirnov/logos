# ADR 0039. Memory models, persistence and atomic commitment (Memoria)

Status: ACCEPTED (Victor, 2026-10-15; O2, O4, O6 decided, see Open; the 2026-10-15 inventory amendments D12 and rows M2g, M3v, M9 added). Proposed 2026-10-10. Part of the set under [0032](0032-logos-core-composition.md). Refines [0032](0032-logos-core-composition.md) D7 / row C7 and [0033](0033-incremental-deem-dbsp-translator.md) D6 / row I8; relates to [0034](0034-hest-core-graph-layer.md) D2 / D7 (radii, placement). Inputs: [0017](0017-deem-storage-engine.md), [0018](0018-memoria-port.md), [0020](0020-memoria-container-interface-plane.md), [0027](0027-row-store-tables.md).

Victor, 2026-10-10: «Я не против сделать уровень инкрементности снэпшотами Мемории, но тогда Меморию нужно будет универсализировать… логический концептуальный уровень персистентных снэпшотируемых структур данных и множество возможных реализаций.» And, on the framing: «за системообразующий принцип возьми модели и иерархии памяти, и рассматривай Меморию как систему абстракций над ними. Модель памяти является частью модели вычислений. В обычных ЯП обе модели идут в качестве аксиом, производных от одной-двух целевых архитектур (x86_64/ARM). Logos от этого уходит… есть Rust-bias, наследующий x86_64 (одноранговое адресное пространство, когерентные кэши), но это просто артефакт исторической преемственности. Logos не зависит от аппаратной модели, хотя и для своего удобства структурирует предпочтительную модель в виде LCM.» And: «неизменяемые CoW-узлы, снапшоты, коммиты и дельты — это просто способ организовать персистентность. А последняя нужна для организации распределенного режима, так как предоставляет ценный atomic commitment, на котором можно удобно строить более сложные уровни изоляции.» And: «Writ как частный случай Мемории. Ограниченный по размерам перемещаемый объект с функциями иммутабельности. Writ не персистентен, но Мемория добавляет персистентность и снимает ограничения на размер над Writ данными, организуя последние в контейнеры.»

## Problem

### The memory model is an axiom in other languages; in Logos it is not

A conventional language takes its model of computation, and the memory model inside it, as axioms derived from one or two target architectures: one flat address space, caches kept coherent by hardware, volatile RAM, persistence reached only through IO calls. Rust inherits exactly this (raw pointers into one address space; `&mut` uniqueness and `Send` / `Sync` stated against coherent memory). Logos targets machines where these axioms fail: LCM (internally synchronous xPUs, distributed memory and function blocks, no cache coherence, possibly unreliable channels, ADR 0034), devices with their own memory and storage (a storage device running its own logic), hosts where RAM is a cache over durable media. The x86-host model is one instance among these, not the definition.

### Distributed execution needs atomic commitment; Memoria supplies it, but is described as a library

Once memory is distributed and not coherent (LCM, devices, RAM over durable media, several hosts), a state change that spans more than one agent or level is not made visible by a store instruction; it needs ATOMIC COMMITMENT: a set of writes becomes visible and durable as a whole, or not at all. Persistence is what provides it, and isolation levels (snapshot isolation, serializability, the consistency of a replicated or incremental computation) are built on top of it. Memoria is Logos' persistence framework, so it is the supplier of atomic commitment; ADR 0033 D6 depends on exactly this (a tick is a commit; the parent version is the circuit's z⁻¹). Today the code presents it as a container library with one store and the memory model implicit, and the commitment it provides is local to one RAM level.

### Measured state (2026-10-10)

- **Two persistent stacks, neither stated against a memory model.**
  - `logos.mem.bt` (11 631 lines): `trait Snapshot` (`map.logos:72-135`) — id plane (`snapshot_id`, `snapshot_uid` → `Uid256`), lifecycle (`commit`, `remove`, `status`), lineage (`snapshot_parent`, `has_parent`, `set_as_branch`, `set_as_master`), and residency pins whose leaf form takes a block address (`leaf_pin(*const u8)`, `:132`). `trait Store` is an empty marker (`:52`); no trait says "fork" or "open a container". `Ctr` has `size` only (`:143`). `BtLeaf` / `BtBranch` (`:242-335`) are node-layout traits. `MemoryStore` / `MemorySnapshot` (`memstore.logos`): CoW nodes in RAM, a snapshot DAG with ONE parent per snapshot (`:95`), named branches, status ACTIVE / COMMITTED / DROPPED (`:81-83`), history queries (`:920-1019`), `store_pack`, whole-store `store_save` / `store_load` (`:1196,1281`). There is no transactional backing store under RAM.
  - `logos.lcm.deem.data` (ADR 0017 survivor): `trait IStore` (`store.logos:63`) with multi-parent merge commits (`:80`; `Store::merge` select-one, `handle.logos:409-423`), labels, first-parent `log`; `Arc<dyn BTreeNode>` nodes; no save / load, no pack. Consumers: 28 `data_*` / `derive_branch_node_*` pass fixtures, 1 fail fixture, two examples, `derive_branch_node.logos`.
- **Node placement is a downcast to one concrete store.** Node allocation, child-ref resolution and the directory (`root_of` / `set_root`) are inherent methods of `MemorySnapshot`, reached by a `dyn_data` downcast in every Canon-generated family (`container_item.logos:1228,1231,2688,2695,3055,3062`); `map.logos:70-71` anticipates "a `trait NodeStore: Snapshot` once a second store lands".
- **No difference between snapshots** is computed anywhere (ADR 0017 P1 deferred it; ADR 0020 `:97` asks for `changes(since) → Z-set`; ADR 0033 D6 assumes it).
- **No description of a memory model exists in the language.** The tiers (`module_manifest.cpp:50-62`: lang "synthesizable, static memory", mem + malloc, lcm + virtualised IO / threads / fibers, std + processes / OS IO) are the closest thing, enforced by link order only (ADR 0035 D5 restates them as effect bounds).
- **Writ already is the bounded, relocatable, non-persistent case.** A Writ document lives in a never-move segment arena (`writ/allocator.logos`); references inside data are self-relative. `compactify` copies a document into one contiguous block, "relocatable + serializable as one unit" (`writ/compactify.logos`). `WritObjectData` (`writ/objdata.logos`, ADR 0027 OPEN-1) is one object frozen into a position-independent run, read-only by API (views are `&`), changed by materialise → edit → re-serialize. Nothing in Writ commits, versions or survives the process. Memoria stores Writ data only at one point: `WritObjectData` is the planned key/value cell of a container column (ADR 0027), framed by a u16 in the key column (≤ 64 KiB); `row.logos` and `compactify.logos` are its only users outside `objdata.logos`. ADR 0034 D2 already requires cross-radius payloads to be Writ-representable (`Reloc`).
- **The relational interface of a structure is already model-free.** The source traits (`MapSource`, `OrderedMapSource`, `PositionalSource`, `logos.std.wql.source`) and members (`rel` / `op` / `size` / `order` / `unique`) say what a structure computes, not where its bytes live.
- **Consumers of the current store API**: 104 pass and 19 fail fixtures name `store_new`, `create_ctr`, `memstore` or `Snapshot` (32 `memoria_*`).

## Decisions

### D1. The memory model is an explicit part of the model of computation

A memory model is described, not assumed. It is a hierarchy of LEVELS and the EDGES between them:

- a level states its addressing (byte-addressed / block- or page-addressed / key-addressed), its coherence domain (which agents observe a write, and when; coherent, incoherent, single-agent), volatility, access and failure-atomicity granularity (byte, line, block, page, object), and a cost class (latency, bandwidth);
- an edge states how data moves between two levels (fill, write-back, copy, replication, publication) and with what guarantee (none, atomic per unit, transactional per commit).

The x86-host model is one description (a coherent byte-addressed RAM level; durable storage reachable by IO). LCM is Logos' preferred structured model: per-xPU synchronous local memory, distributed memory without global coherence, message edges. A device that runs logic beside its media is another description. The description is data the compiler and the runtime read (placement, ADR 0034 D7; effect and capability bounds, ADR 0035 / 0036); its surface form is O1.

The Rust-inherited parts of the language (raw pointers, `&mut` aliasing rules, `Send` / `Sync`) are the semantics INSIDE one coherence domain. State that crosses a level boundary or a coherence domain is published by an atomic commit (D2) and read as a committed version, never through a pointer.

### D2. Persistence supplies atomic commitment; isolation is built on it

The primitive Memoria contributes to the model of computation is ATOMIC COMMITMENT over a memory model: a workspace's writes are published as one version, on a level and to the agents of that level, all or nothing. Everything distributed is built on it:

- **Isolation levels** are policies over versions: snapshot isolation (a reader holds one committed version), serializability (commit validates against versions committed since the fork), select-one or relational merge (D8). They are layered above the commit, not inside the structures.
- **Replication and movement between levels** move committed versions; what is committed does not change, so a replica or a cached copy of it needs no coherence protocol.
- **Change** between two versions (`delta_since`) is what a consumer must receive to catch up: a replica, a remote radius (ADR 0034), an incremental circuit (ADR 0033).

How a model REALISES persistence is an implementation choice, not part of the abstraction: copy-on-write trees of immutable nodes with snapshots (today's `logos.mem.bt`), a log of writes replayed over a base, shadow paging, or an existing transactional store behind a seam. The vocabulary (D4) and the laws (D5) say what a committed version promises; D6 records how each covered model keeps that promise.

A program writes against the vocabulary; the same structures run under any memory model Memoria covers, and choosing one is a binding decision, not a rewrite.

### D3. Writ is the bounded case; Memoria lifts the bound and adds persistence

A Writ object is the unit of data in one memory model's terms: BOUNDED in size (one contiguous run, one arena), RELOCATABLE (self-relative references; a frozen run moves between levels and coherence domains as bytes, without fix-up), and IMMUTABLE when frozen (`WritObjectData`: read in place, changed only by producing a new run). It is NOT persistent: freezing gives an immutable value, not a committed version, and there is no lineage.

Memoria is the same data with the two limits removed:

- **Size.** A container organises Writ data into a structure over many bounded runs (a tree of blocks); no single run has to hold the whole value, and a reader touches only the runs on its path.
- **Persistence.** A container lives in a version (D2): its writes are published by an atomic commit, so the data gains durability, lineage and isolation, which Writ alone does not have.

Consequences:

- One data model across both: an element of a container is Writ data; a value read from a container is a Writ view; a value too large or too long-lived for one run becomes a container. The choice is size and persistence, not a different type system.
- Movement: a bounded value crosses a level or a radius as a frozen Writ run (ADR 0034 `Reloc`); an unbounded or persistent one crosses as a committed version and, to catch up, its delta (D2). Both are pointer-free.
- Immutability is shared: Writ's frozen run and Memoria's committed version are the same promise at two scales, so a reader holding either needs no coherence.

### D4. The logical vocabulary

| Trait | Promise |
|---|---|
| `Version` | an identity of a state, ordered within a lineage (`Uid256` remains its boundary representation) |
| `Snapshot` | an immutable cut: reads only; `version()`, `parents()` (≥ 0; more than one after a merge) |
| `Workspace` | a mutable state forked from a snapshot: writes; `commit() -> Snapshot`, `abort()` |
| `VersionStore` | the lineage: `fork(&Snapshot) -> Workspace`, `heads()`, named branches, `log`, `merge` (D8), `release` |
| `Ctr` | a named structure `S` in a cut's directory: opened by id, read through `&Snapshot`, written through `&mut Workspace` (O6: the notion stays; `Persistent<S>` is not introduced) |
| `Versioned` | `delta_since(&Version)`, `version()`: the change between two versions of one lineage, PER SNAPSHOT (O4) — one delta per cut, carrying a `Delta<Row>` for every container of the cut that changed; ADR 0033's `DeltaSource` is this trait |

A persistent structure's relational face is its source declaration (ADR 0032 D4): a persistent ordered map is an `OrderedMapSource` with its members, plus `Versioned`. Node-layout traits (`BtLeaf`, `BtBranch`, `NodeProfile`, `BlockStore`) and residency pins belong to D7, not to the vocabulary.

### D5. Laws

Checked by one law suite run under every memory model (row M6):

1. A committed snapshot never changes: every read of it returns the same answer, on every level it has been published to.
2. A fork's writes are invisible to its parent and to sibling forks.
3. Change capture is exact: for versions `v ≤ w` of one lineage, `state(w) = state(v) ⊕ delta_since(v)` at `w`, as Z-sets over the structure's relation.
4. `delta_since(version())` is empty.
5. Composition: for `u ≤ v ≤ w`, `delta_since(u)` at `w` = `delta_since(u)` at `v` + `delta_since(v)` at `w`.
6. Fork cost is not proportional to the state (the cost class is declared per memory model).
7. A released snapshot is reclaimed once nothing holds it; reading a released snapshot is impossible by construction (lifetimes), not a runtime error.

### D6. Memory models Memoria covers, and how more are added

| Model | Levels and edges | Fork | Diff | Durability |
|---|---|---|---|---|
| **in-memory** | one coherent RAM level; nodes in RAM, shared between snapshots (today's `MemoryStore`) | O(1) (shared directory root) | tree diff skipping shared subtrees by snapshot-id stamp | none beyond an explicit save / load |
| **external memory** | RAM as a cache level over a block device or KV level; fill and eviction edges; a transactional publication edge (commit makes the cut durable on the backing level; eviction writes nothing a committed cut does not already hold) | O(1) | the same diff, over resident or filled nodes | the backing level's |

One framework provides both: tree code, Canon families and the vocabulary are shared; the models differ in the level a node lives on and in what a commit does. A further model (distributed memory without coherence, a device-resident store) is added by describing its levels and edges (D1) and implementing the node-placement seam (D7) for them — not by a new library. The backing level of the external model is itself such a seam (block device, page file, an existing KV store), so existing storage systems are covered by implementing it.

### D7. Node placement is a trait of the model

The `dyn_data` downcast to `MemorySnapshot` is replaced by a placement trait (`NodeStore: Snapshot`, as `map.logos:70-71` anticipates): allocate, resolve, release a node on a level; read and write the directory of a cut. Canon families are generated against it; each memory model implements it. Residency pins (ADR 0025 §7) are a capability of a model with a cache level (`Resident`), asked for only by code bound to such a model.

### D8. Merge

The vocabulary admits multi-parent snapshots (`parents()`): `logos.lcm.deem.data` already has merge commits and branch workflows need them. The merge OPERATION is a policy, not a law: `select_one` (ADR 0017 D2 v0) first; a relational merge (three-way over a structure's relation, ADR 0017 v1) later, with conflicts reported as data. A model that cannot merge declares it. Schema-divergent merge (ADR 0020) stays open.

### D9. Incremental Deem and Hest bind to this layer

- ADR 0033: a circuit's state lives in a `Workspace`; a tick is a commit; the circuit's z⁻¹ is the parent snapshot; input deltas of `Versioned` sources are read by `delta_since`. Checkpoint, fork and replay are vocabulary operations under every model; under the external model a checkpoint is durable. ADR 0033 I8 runs one circuit under both models and requires equal answers per tick.
- ADR 0034: a synchrony radius lies inside one coherence domain; a cross-radius edge carries values that are not pointers (`Reloc`, ADR 0034 D2), typically snapshots and deltas of D2's structures. Placement (ADR 0034 D7) reads the memory-model description (D1).

### D10. What happens to the two stacks

- `logos.mem.bt` becomes the framework: `Snapshot` / `Ctr` split into the vocabulary (D4) and the placement trait (D7); `MemoryStore` is the in-memory model and implements `VersionStore` (single-parent today; `parents()` returns one).
- `logos.lcm.deem.data`: its multi-parent merge and labels are ported into the vocabulary and the framework (ADR 0032 O3), then the package is deleted with its consumers moved (ADR 0032 C7). It does not survive as a model: its tree duplicates `mem.bt` without persistence or pack.

### D12. Amendments from the 2026-10-15 inventory

A census of every persistent implementation in the tree (2026-10-15) found three facts the decisions above did not state:

- **Law 1 is not held by `logos.mem.bt` today.** `Snapshot::commit` flips a status; the generated writers (`insert`, `with_entry`, `remove`, `insert_batch`) do not read it and the nodes keep the writer's stamp, so a handle on a COMMITTED snapshot writes in place. `logos.lcm.deem.data` holds the law by construction (`Snap::commit(self)` consumes). Under D4 a write needs `&mut Workspace` and `commit(self)` consumes it; until that split lands, the writers refuse a non-ACTIVE snapshot (row M2g).
- **Only `ordered_map` and the directory are copy-on-write.** `btvec` (vector family) and `btfl` (multimap family) mutate shared nodes in place (their "v0 restriction"), so a fork of a snapshot holding one does not isolate it (law 2). Every family must satisfy the laws (row M3v).
- **The element model does not hold a Deem relation.** Keys are fixed-width primitives of at most 8 bytes, values fixed-width or `str`; there is no weight per entry and `insert_batch` admits only a batch that lands whole between two adjacent keys. An incremental circuit's integrals (ADR 0033) need composite row keys (M1w), a Z-set weight and a sorted merge-apply write (`apply_delta`, ADR 0020 requirement 9) (row M9).

### D11. Placement in tiers

The vocabulary and the in-memory model are in the `mem` tier (a persistent structure is a data structure, not IO). The backing-level seam of the external model, and any model whose edges do IO, live in `lcm` / `std` and carry the effects and capabilities of ADR 0035 / 0036.

## Rows

| Row | Content | Acceptance |
|---|---|---|
| M0 | memory-model description (D1): a declaration form for levels and edges; the x86-host and LCM descriptions written; the two D6 models described in it | the descriptions parse and are readable as data (a fixture queries a level's coherence domain) |
| M1 | census: every use of `Snapshot` / `Ctr` / `Store` / `store_*` / `create_ctr` / the `MemorySnapshot` downcast / `logos.lcm.deem.data`, mapped to D4 / D7 | ledger file, pinned by a gate — LANDED 2026-10-15: `logos_00_store_api_census`, ledger `tests/logos/store_api.ledger` (95 rows; uses: mem.bt 1029, deem.data 219, compiler 1; 13 downcast sites, 7 of them in 5 tests). The `Snapshot` trait methods are not counted (shared spelling); M2 prices them with the compiler |
| M1w | Writ ↔ Memoria (D3): container cells are `WritObjectData` runs (ADR 0027 OPEN-1 completed); reading a cell yields a Writ view; the frame limit stated per container | a container of Writ objects round-trips; a value over the frame limit is refused at INSERT, not truncated |
| M2 | vocabulary (D4) and laws (D5) in the `mem` tier | trait fixtures; the law suite compiles against a reference model — LANDED 2026-10-15: `logos.mem.version.{vocab,laws,reference}`. `Snapshot` / `Workspace` (`commit(self)`, `abort(self)`) / `VersionStore` (`type Ws: Workspace<Snap = Self::Snap>`, `release` consumes, `fork_cost`); versions are `u64` in this step, with the `Uid256` boundary unchanged. The law suite (laws 1, 2, 6 and lineage) runs against the reference model (full copy on fork, a sorted `Vec`, no shared algorithm with `mem.bt`) and against a canary whose forks share rows, which must violate laws 1 and 2 (`version_laws_reference`). Law 7 and law 1 by construction are pinned by refusals (`version_read_after_release_refuse`, `version_snapshot_write_refused`). Laws 3–5 arrive with `Versioned` (M5); `MemoryStore` becomes an instance at M3 |
| M2g | write-after-commit refused by `logos.mem.bt` writers (D12); retired by the D4 type split in M3 | a write through a handle on a committed snapshot is refused; fixture pair (refused / admitted on a fork) |
| M3 | the in-memory model as an instance: `logos.mem.bt` split into vocabulary and placement; `NodeStore` replaces the downcast in Canon (D7) | every `memoria_*` / `ctr_*` / `deem_*` store fixture green with unchanged answers; downcast census → 0 |
| M3v | `btvec` / `btfl` copy-on-write (D12) | a fork isolates a vector and a multimap: writes to the fork leave the parent's answers unchanged |
| M4 | merge and labels ported from `deem.data` (D8, D10); `deem.data` deleted, its fixtures ported | `data_merge_heads`, `data_dag_topology` ported and green; package gone |
| M5 | `delta_since` (shared-subtree skipping diff) | laws 3-5 on randomized histories; diff cost measured against state size |
| M6 | the law suite run under every model | a planted law violation in a test model turns it red |
| M7 | the external memory model: backing-level seam, fill / eviction / transactional publication; a block-device / page-file backend first | law suite green; a committed snapshot survives a process restart; store fixtures run unchanged under both models |
| M9 | element model for relations (D12): composite row keys over Writ cells (M1w), a weight per entry, `apply_delta(sorted batch)` | a Z-set of struct rows round-trips; an interleaving delta batch merges; weights cancel to absence |
| M8 | ADR 0033 binds to this layer (D9): circuit state in a `Workspace`, I8 under both models | one circuit, two models, equal per-tick answers |

Ordering (2026-10-15): M2g first (a soundness hole, independent); M3v with M3; M9 before M8 (not before ADR 0033 I2: the academic translator's integrals are written against D4 over a reference model, M2). Ordering against the set: M1-M3 (with M1w) before ADR 0033 I2 (so the translator's state model is written against D4); M4 with ADR 0032 C7; M5, M6, M8 before ADR 0033 I8 (M8 needs M7 for the second model). M0 runs alongside and feeds ADR 0034 D7 and ADR 0035 / 0036. ADR 0033 I2-I7 do not wait: they run on caller-provided deltas.

## Open

- O1. Surface of a memory-model description: a manifest section, a `memory_model` item, or Writ documents read by the toolchain. Recommendation: Writ documents (data, not syntax), with the tiers and LCM as the first instances.
- ~~O2~~ DECIDED 2026-10-15 (Victor): `Workspace` and `Versioned`.
- O3. `delta_since` across branches: within a lineage only (`v ≤ w`), or also between siblings through the common ancestor.
- ~~O4~~ DECIDED 2026-10-15 (Victor): PER SNAPSHOT — all containers of a cut are one workspace and share one logical history, so a delta is a cut's.
- O5. Concurrency inside a level (SWMR, ADR 0018 M5): a property of the model or a law.
- ~~O6~~ DECIDED 2026-10-15 (Victor): `Ctr` stays (a named structure in a cut's directory).
- O7. Whether a Memoria block (node) is itself a frozen Writ run, making D3 literal at the node level (one relocation and view machinery for both), or only a container's cells are Writ data.
- O8. Positioning of a walk: by ordinal or by key. The generated `LeafWalk` is bounded by ordinals (`at`/`endr`), so a key-bounded window pays the rank (`Branch::size_prefix` in `seek_key`) even when it holds at most one row. On a Deem point get over `Map<u64,u64>` that is about 295 of 12,134 instructions at `-O2` and 1,261 of 14,571 at `-O0`, against `find`, which descends without a rank (ADR 0026 F4, `bench/deem_point_get`, measured 2026-10-13). The walk vocabulary should let a key-bounded window carry a leaf position without an ordinal.
