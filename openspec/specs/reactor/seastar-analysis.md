# Seastar Architecture Analysis for Logos

## Overview

Seastar is a high-performance C++ framework based on a shared-nothing, thread-per-core architecture. This model eliminates lock contention and cache bouncing by ensuring that each core (shard) operates on its own local data. Communication between shards is explicit and asynchronous, handled via message passing.

This report analyzes Seastar's inter-thread communication mechanisms and its `future<T>` implementation, with a focus on their applicability to the Logos Reactor and Green Fibers.

## Inter-Thread Communication (SMP)

Seastar's Symmetric Multi-Processing (SMP) module is the backbone of its cross-core interaction.

### The Shared-Nothing Model
- **Shards**: The system is divided into shards, where 1 shard = 1 OS thread pinned to a CPU core.
- **Local Memory**: Each shard has its own memory allocator and object instances. There is no shared global state guarded by locks.

### Message Passing: `smp::submit_to`
The primary primitive for inter-thread communication is `smp::submit_to(shard_id, lambda)`.
- **Mechanism**: Use `submit_to` to execute a lambda on a specific remote shard.
- **Return Value**: It returns a `future<T>` which resolves when the remote execution completes and the result is sent back to the originating shard.
- **Lifetime**: It handles moving arguments and lambdas to the remote core, ensuring memory safety across shard boundaries.

### Implementation Details (`smp_message_queue`)
Under the hood, `submit_to` uses `smp_message_queue`:
- **Topology**: A complete graph of queues. Each pair of shards $(A, B)$ has two unidirectional queues: $A \to B$ and $B \to A$.
- **Data Structure**: Seastar utilizes `boost::lockfree::spsc_queue`, but **Logos will use a custom-built lock-free SPSC queue** to strictly avoid Boost dependencies.
- **Batching**: Messages are batched to amortize the cost of cache line transfers and inter-processor interrupts (IPIs).
- **Polling**: Shards periodically poll their incoming queues for new tasks as part of the reactor loop.

**Relevance to Logos:**
For Logos, adopting a `submit_to` equivalent is highly recommended. It fits the "Green Fiber" model perfectly: a fiber on Core A can `await` a `submit_to(Core B, ...)` call, effectively blocking the fiber (but not the thread) until Core B returns the result.
- **No Boost**: We will implement our own lock-free SPSC queue for the message passing layer, avoiding `boost::lockfree`.

### Queue Buffer Strategy: Bounded vs Unbounded

**Decision**: **Bounded Ring Buffer Arena**.

### High-Level Protocol Support (Future)

The low-level SPSC Ring Buffer serves as the "L2 Transport" for higher-level application protocols.
*   **Streaming/RPC**: The system will eventually support complex protocols (conceptually similar to HTTP/2 / QUIC) regarding multiplexing and streaming.
*   **Integration**: These protocols (already developed in a separate project) will run *on top* of the raw message passing layer, utilizing the "Shared Document" allocator for large payloads.
*   **Current Focus**: We implement the raw Ring Buffer + Allocator foundation first.

## Scalability Considerations: SPSC vs MPSC (200+ Cores)

The user raised a valid concern: with a fully connected mesh of SPSC queues on a massive machine (e.g., 200 cores), each core must poll 200 incoming queues.

**Trade-off Analysis:**
1.  **SPSC (Seastar approach)**:
    -   *Pros*: Zero contention on write. When Core A sends to Core B, only Core A touches the head and only Core B touches the tail. No atomic RMW (Read-Modify-Write) instructions needed, only memory barriers.
    -   *Cons*: **Polling overhead**. The consumer loop involves $O(N)$ checks. On 200 cores, checking 199 empty queues is expensive (cache pollution + CPU cycles), even if individual checks are just reading a cached integer.

2.  **MPSC (One queue per core)**:
    -   *Pros*: **O(1) Poll**. The consumer checks exactly one queue.
    -   *Cons*: **Writer Contention**. If 200 cores try to send to Core 0 simultaneously, they all fight for exclusive access to Core 0's tail pointer. This causes massive **cache line bouncing** (false sharing's angry cousin). The tail cache line will continuously invalidate across 200 L1 caches, stalling the pipeline.

**Conclusion & Recommendation:**
For high-performance systems, **contention is usually the greater evil than polling overhead**. MPSC scales poorly under load ("thundering herd" on the tail). SPSC scales perfectly under load but suffers when idle or under low-fan-in traffic.

**Mitigation (Logos Approach):**
We should stick to **SPSC** to avoid the disastrous writer contention of MPSC, but we must mitigate the $O(N)$ polling cost:
-   **No-Data Flag / Eventmask**: Use a shared `std::atomic<uint64_t>` (or a bigger bitmask for >64 cores) where producers set a bit when they enqueue. The consumer only scans the queues corresponding to set bits.
-   **Hierarchical Poll**: Group cores (e.g., by NUMA node) and check groups before checking individual queues.
-   **Sleeping**: If the bitmask is empty, the core can sleep (Wait-on-Address / `futex`) until notified, saving power and cycles.

-   **Sleeping**: If the bitmask is empty, the core can sleep (Wait-on-Address / `futex`) until notified, saving power and cycles.

### Queue Buffer Strategy: Bounded vs Unbounded

**Recommendation: Bounded Ring Buffer (Array-based)**

1.  **Memory & Cache**:
    -   **Bounded (Ring Buffer)**: Uses a pre-allocated contiguous array. This is **cache-optimal**. Pushing/popping is just incrementing an integer index masked by size. No dynamic allocation per message.
    -   **Unbounded (Linked List)**: Requires per-node allocation.
        -   *Synchronous*: Can allocate the node on the **Fiber Stack** (intrusive), avoiding `malloc`.
        -   *Asynchronous*: Requires **Heap/Pool allocation** since the sender does not wait.
        -   *Issue*: Regardless of allocation source, pointer chasing destroys cache locality compared to an array.

### Advanced Strategy: Ring Buffer Arena (Recommended)

The user proposed a **Ring Buffer Arena** to store not just pointers, but the **message payload itself** contiguously in the buffer.

-   **Mechanism**: The queue is a raw byte array (`std::byte[]`).
-   **Allocation**: `producer.allocate(size)` returns a pointer *inside* the ring buffer.
-   **Zero Copy**: The message is constructed in-place (placement new) into the allocated slot.
-   **Variable Length**: Messages of different sizes can be packed tightly.

**Wrap-around Handling:**
Variable-sized messages complicate the end of the buffer.
-   **Padding Strategy**: If a message doesn't fit at the end of the ring, insert a special `PADDING` message (header only) to fill the gap, and allocate the real message at the start (`index 0`). This avoids split-messages and keeps reading simple.

**Benefit**: This yields the **maximum possible cache locality**. A consumer reading the queue reads a linear stream of instructions/data, perfectly prefetchable by hardware, with zero pointer chasing to external stack/heap locations.


### Deadlock Detection: Zero-Overhead Monitor

To ensure reliability without punishing the "fast path" (normal operation), we can implement a **Distributed Wait-For Graph**.

**Mechanism:**
1.  **Shared State**: Each shard exposes a `std::atomic<int16_t> blocked_on_shard` (initialized to `-1`).
2.  **Fast Path (Success)**: When `enqueue()` succeeds, **do nothing**. Zero atomic writes. Zero overhead.
3.  **Slow Path (Full)**:
    -   When `enqueue()` hits a full buffer: `blocked_on_shard.store(target_id, mo_relaxed)`.
    -   While polling/yielding: Perform a cycle check (optional) or let a background **Watchdog Thread** scan the array.
    -   On Resume: `blocked_on_shard.store(-1, mo_relaxed)`.

**Detection Logic**:
The Watchdog reads the atomic array. If it sees `blocked_on[A] == B` and `blocked_on[B] == A` (or a longer cycle), a deadlock is confirmed. This allows us to detect hangs even if the re-entrant polling logic fails or livelocks.



2.  **Backpressure**:
    -   **Bounded**: Provides natural backpressure. If the ring is full, the producer fiber is forced to wait (yield). This prevents a fast producer from OOM-ing the system by flooding a slow consumer.
    -   **Unbounded**: Can grow indefinitely, hiding bottlenecks until the system runs out of memory.

**Deadlock Warning**:
With bounded queues, if Shard A is full sending to B, and Shard B is full sending to A, and both wait, we have a deadlock.
-   **Solution**: The "wait for space" operation must also **process incoming messages**. While waiting for the output queue to drain, the shard must service its input queue to relieve pressure on the other side.

## Futures (`future<T>`)

While Logos uses Green Fibers to avoid the "callback hell" of Continuation-Passing Style (CPS), `future<T>` remains a critical primitive.

### Role in Seastar
In Seastar, `future<T>` represents a value that will be available in the future. It is the glue between the reactor's event loop and the application logic.

### Structure
- **State**: A `future` can be in `available` (value/exception ready) or `unavailable` (pending) state.
- **Promise**: The producer side, `promise<T>`, sets the value.
- **Optimizations**: Seastar optimizes for the "fast path" where a future is immediately available (e.g., data already in cache). In this case, no dynamic allocation occurs for continuations.

### Relevance to Logos
Even with Green Fibers, `future<T>` is useful as a **Handle** for an asynchronous operation.
1.  **Fiber Synchronization**: A fiber can `await` a `future`. The reactor effectively "parks" the fiber and attaches a continuation to the `future` that "unparks" the fiber when capable.
2.  **Explicit Dataflow**: It provides a clear type-safe way to represent pending results, especially for operations that cross thread boundaries (like `submit_to`).

## Sharded Services (`sharded<T>`)

Seastar provides a `sharded<Service>` template to manage thread-local instances of a global service.
- **Deployment**: Creates an instance of `Service` on every shard.
- **Peering**: Allows instances to communicate with their peers on other shards (e.g., `local_service.container().invoke_on(other_shard, ...)`).
- **Map-Reduce**: Provides utilities like `invoke_on_all` to broadcast operations or gather statistics from all shards.

**Recommendation**: This pattern is ideal for services like the Object Store or Transaction Manager in Logos, where each core needs low-latency access to local partition data but must occasionally coordinate globally.

## Future Design: CPS vs. Blocking `get()`

The user raised a critical performance question: **Is Continuation-Passing Style (CPS) faster or slower than Green Fibers on modern "big" hardware?**

### The Performance of CPS (Seastar/C++20 coroutines)
CPS (used in Seastar's `then()` chains) fundamentally scatters logic.
1.  **Memory Layout**: Every `then()` creates a new promise/continuation object. Even with arenas, these objects are often non-contiguous in memory relative to the "logical" stack frame. This breaks **spatial locality** for the Data Cache (L1 D-Cache).
2.  **Instruction Cache**: CPS forces the compiler to break functions into disconnected blocks (lambdas). This **scatters the instruction stream**, hurting I-Cache locality and baffling the hardware prefetcher.
3.  **Pipeline Stalls**: Modern CPUs rely heavily on branch prediction. A chain of virtual calls (or indirect function pointers in a type-erased `std::function`) is much harder to predict than a conditional jump within a single function body.

### The Advantage of Green Fibers (Logos approach)
Green Fibers (stackful coroutines) maintain a **contiguous stack**.
1.  **Hot Cache**: The top of the fiber's stack stays hot in L1 cache. Local variables and return addresses are adjacent.
2.  **Prefetcher Friendly**: The hardware prefetcher loves linear access patterns. Standard imperative code execution flows linearly, pulling instructions into L1 I-Cache efficiently.
3.  **Context Switches**: While a fiber context switch (swapping registers) costs ~10-20ns, this is often *cheaper* than the aggregate cost of multiple L3 cache misses caused by traversing a fragmented generic CP-chain.

### Decision
**We will prioritize a synchronous-style `future.get()` API.**
-   `future<T>` will serve primarily as a synchronization primitive, not a composition primitive.
-   The "blocking" `get()` will effectively `yield` the current fiber until the value is ready, maintaining the imperative flow and stack locality.
-   `then()` will be avoided in the core API to prevent the "segmented stack" performance penalty.

### Green Function Coloring
It is important to note that adopting the synchronous `get()` style does **not** erase the distinction between blocking ("red") and asynchronous ("green") code.
-   **Explicit Coloring**: All code touching IO or waiting on futures is marked "green". It pays a small "green call overhead" (stack segment maintenance).
-   **Context Inheritance**: Color is typically an attribute inherited from the context of the declaration (and caller -- for lambdas).
-   **Compiler Flexibility**: The coloring is not a rigid type-system prison. We can toggle compiler flags to compile "green" code as "red" (running on the system stack) for debugging, sanitizers, or legacy tooling integration.

