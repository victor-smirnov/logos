# Memory Subsystem Analysis & Design

## Seastar's Memory Model

Seastar implements a **Shared-Nothing Memory Model** that is tightly coupled with its shard-nothing execution model.

### Key Architectural Features

1.  **Partitioned Heap**:
    -   System memory is divided equally among all logical cores (shards) at startup.
    -   Each shard manages its own pool of memory.
    -   `malloc`/`new` are replaced to allocate from the local shard's pool.

2.  **Address-Encoded Ownership**:
    -   Seastar maps memory such that the ownership (CPU ID) is encoded directly into the virtual address bits (e.g., `0x...<cpu_id>...`).
    -   **Benefit**: `free(ptr)` can determine in $O(1)$ operations (bitmasking) whether the pointer belongs to the current shard or a remote one, without lookups.

3.  **Cross-Core Freeing**:
    -   If `free(ptr)` detects the pointer belongs to a remote shard `R`, it does **not** take a lock on `R`'s allocator.
    -   Instead, it pushes the pointer into a lock-free **Cross-CPU Free List** owned by `R`.
    -   Shard `R` periodically drains this list to reclaim memory.

4.  **Allocators**:
    -   **Slab Allocator**: For small objects (most common).
    -   **Page Allocator**: For large objects, managing spans of pages (buddy allocator style).

## Logos Deviation: Shared Immutable Data

The user highlighted a critical difference from Seastar (which was optimized for ScyllaDB's KV-sharding).
-   **Seastar**: Pure shared-nothing. Requires message passing for *everything*.
-   **Logos**: Uses persistent data structures where the **shared part is immutable**.
-   **Implication**: We do **not** need a fully coherent cache for this data (since it doesn't change). We ideally want a mechanism where cores can directly access the same physical pages for read-only immutable data, bypassing the message passing overhead for reads. NoC (Network-on-Chip) style messaging is effectively used for coordination/writes, but reads can be direct RAM access.

## Requirements for Logos

Logos shares the shared-nothing philosophy (*for mutable state*) but has unique requirements due to **Green Fibers** and **Immutable Sharing**.


### 1. Fiber Stack Allocation
Green fibers require stack segments.
-   **Pattern**: Very frequent allocation/deallocation (fiber creation/destruction or segment growth).
-   **Size**: fixed block sizes (e.g., 4KB, 8KB).
-   **Requirement**: specialized **Object Pool** for stack segments to avoid general allocator overhead.

### 2. Message Passing Arena
As decided in the SMP analysis:
-   Inter-core messages use a **Ring Buffer Arena**.
-   Payloads are allocated *inside* the ring buffer.
-   This bypasses the general purpose allocator for the most high-frequency data path.

## Design Recommendations

### 1. Adopt Partitioned Heap
We should adopt Seastar's strategy of `mmap`ing a huge region and partitioning it.
-   **Why**: avoids lock contention of the system allocator (`malloc`).
-   **Implementation**: Use `mmap` with `MAP_ANONYMOUS` and potentially `MAP_HUGETLB` for performance.

### 2. Fast Cross-Core Free
The bit-masked address check is brilliant.
-   **Proposal**: Reserve a large virtual address space (e.g., 44 bits).
-   Segment it so `HighBits` = `ShardID`.
-   `free(ptr)`:
    ```cpp
    shard_id owner = (ptr >> SHARD_SHIFT);
    if (owner == current_shard) {
        local_free(ptr);
    } else {
        remote_free(owner, ptr); // Push to lock-free SPSC queue
    }
    ```

### 3. Specialized Allocators
Usage | Allocator Strategy
--- | ---
**Fiber Stacks** | **LIFO Pool** (Stack). Hot cache reuse.
**Messages** | **Ring Buffer Arena**. Zero allocation.
**Shared Immutable Data** | **Shared Block Allocator**. 4K-aligned raw buffers (B-Tree nodes). **Zero metadata in block**. Lifecycle managed externally (e.g., handles/cache).
**Shared Documents** | **Shared Slab Allocator**. Relocatable, offset-based (like FlatBuffers/JSON). Shared between cores/processes. Variable size.
**General Objects** | **Slab Allocator** (Seastar-style, Per-shard).
**Large Private Blobs** | **Page Allocator** (Buddy system).


## Nuance: Stack vs Heap for Messages
-   **Synchronous Messages**: Allocated on the **Waiting Fiber's Stack**.
    -   Zero dynamic allocation.
    -   Requires the message to be `trivially_destructible` or carefully managed.
-   **Asynchronous Messages**: Allocated in the **Ring Buffer Arena** (if space permits) or fall back to **Slab Allocator** (if too large for arena slots, though arena is preferred).
