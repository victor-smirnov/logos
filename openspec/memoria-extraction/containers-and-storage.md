# Containers and Storage for Logos

## 1. How Logos Uses Containers

Memoria containers provide persistent, versioned, block-organized storage for large structured data. Logos uses them in three ways:

### 1.1 Code Storage
M-Code assemblies (Hermes documents) stored in containers:
- `Vector<Hermes>` -- sequence of M-Code modules
- `Map<Varchar, Hermes>` -- symbol-indexed module registry
- `Set<Varchar>` -- export/import sets
- Assemblies inherit CoW semantics → version control for code is free

### 1.2 Working Memory for RETE
Forward-chaining rule engine needs efficient dynamic storage:
- Facts as Hermes documents in `Collection<Hermes>`
- Indexed access via `Map` or `Multimap` for join operations
- CoW snapshots for transactional rule evaluation (rollback on conflict)

### 1.3 Application Data
Logos programs manipulate Memoria containers directly:
- `Set<T>`, `Map<K,V>`, `Multimap<K,V>`, `Vector<T>`, `Sequence<T>`
- Semantic graphs via Hermes SG primitives stored in containers
- Hierarchical containers (multistream B+Trees) for complex structures

## 2. Container API Pattern for Logos

From `store_api_common.hpp`, the pattern is:
```cpp
// Create container in a writable snapshot
auto ctr = create<Set<Varchar>>(snapshot, Set<Varchar>{});

// Find existing container
auto ctr = find<Set<Varchar>>(snapshot, ctr_id);

// Find or create
auto ctr = find_or_create<Set<Varchar>>(snapshot, Set<Varchar>{}, ctr_id);
```

Logos M-Code needs equivalent operations:
- `ctr.create(datatype_decl)` → new container in current transaction
- `ctr.find(id)` → lookup by CtrID
- `ctr.commit()` → commit transaction (snapshot)
- `ctr.branch()` → create new branch from current snapshot

## 3. Storage Engines Relevant to Logos

### 3.1 MemoryStore (Primary for Development)
- Confluently persistent, MWMR
- Git-like branching for code and data
- Best for: development, testing, compute-intensive tasks
- Code + data co-located in memory

### 3.2 SWMRStore (Primary for Production)
- SSD-optimized, durable commits
- SWMR transactions (one writer, many readers, no locks on readers)
- History and branches supported
- Relaxed durability mode (mark specific snapshots durable)
- Multi-phase commit (distributed transactions)
- Best for: persistent code storage, production data

### 3.3 OLTPStore (Future)
- LMDB-style memory management (no reference counting)
- Very high transaction rates
- No version history, no branches
- Best for: high-frequency transactional workloads

## 4. Memory Management Architecture

Three-level hybrid scheme, each optimized for its lifecycle pattern:

### Level 1: Inside Hermes Document
Arena allocation + copying GC. No per-object overhead. Documents are small (fit in one storage block), so GC is trivial (memcpy-scale).

### Level 2: Storage Engine -- Hot Data (Recent Snapshots)
Tracing GC. Most CoW block copies are short-lived (created during writes, quickly superseded). ARC on them wastes atomic counter operations on objects that will die soon anyway. Tracing GC collects them in batches without per-block atomic overhead.

### Level 3: Storage Engine -- Cold Data (Historical Snapshots)
Reference counting. Long-lived blocks, stable, rarely deleted. RC updates are infrequent, and deletion is deterministic and incremental (no stop-the-world).

Generational boundary between L2 and L3: snapshots that survive N commits promote from hot tier (tracing) to cold tier (RC). Maps naturally to SWMRStore's head (writable) vs committed history (read-only).

MAA implication: atomic RC over shared memory between cores doesn't scale (cache coherency traffic). Tracing GC for hot data eliminates this on the critical path.

## 5. Persistent Data Structures Properties

These properties are critical for Logos runtime:

### 4.1 Snapshot Isolation
- Every committed snapshot is immutable
- Readers never block writers (and vice versa)
- Iterative algorithms safe: data doesn't change between iterations
- Enables: safe concurrent rule evaluation, parallel query execution

### 4.2 Atomic Commitment
- Group of updates → single atomic snapshot
- Either all changes visible or none
- Enables: transactional RETE evaluation, atomic code deployment

### 4.3 Branching
- Branch from any committed snapshot
- Enables: speculative execution, "what-if" scenarios, A/B testing of code changes
- Code versioning as container versioning

### 4.4 Decentralization
- Patches: encapsulated branch transferable over network
- No distributed GC needed (explicit data exchange)
- Enables: distributed Logos agents exchanging code and data

## 6. Advanced Data Structures Available

### 5.1 Searchable Sequences (rank/select)
- Binary and multi-alphabet (1-8 bits)
- O(log N) rank and select
- Underlying primitive for hierarchical containers
- HW-accelerable (PopCnt, SelectN)

### 5.2 LOUDS Trees
- 2 bits per node, extremely compact
- Labeled (with associated data per node)
- Cardinal (fixed-degree, for spatial trees / quad trees)
- Used for: wavelet trees, associative memory, AST storage

### 5.3 Associative Memory
- D-dimensional relation with set semantics
- Multiscale decomposition → LOUDS-backed quad tree
- O(P*H + M) average lookup (P=bucket size, H=depth, M=recall)
- Supports function approximation and inversion
- HW-accelerable

### 5.4 Compressed Symbol Sequences (SSRLE)
- Run-length encoding for symbol sequences
- 1-8 bits per symbol alphabets
- CodeWords 16-64 bits, Segments up to 64 bytes
- ~14% overhead for binary case
- HW-accelerable (segment-level processing)

## 7. What Needs Building

### 6.1 Container Types for Code Model
- `Assembly` container type wrapping Vector<Hermes> + metadata
- Index structures for symbol lookup within assemblies
- Cross-reference tables for dependency resolution

### 6.2 RETE-Optimized Container
- Fact store with multi-index access (for alpha/beta nodes)
- Incremental update support (add/remove facts efficiently)
- Integration with Hermes schema for pattern matching

### 6.3 M-Code Module Loader
- Load assemblies from containers or files
- Resolve dependencies between modules
- Link native code registry entries
- Support for hot-reload (swap module in running system)
