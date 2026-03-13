# Logos Platform Architecture

## 1. System Context

Logos is a new project that absorbs the functionality of the Memoria Framework through a full rewrite. Memoria provides the proven algorithms and data structures; Logos reimplements them in a new codebase designed from scratch for AI-driven development.

The stack has three major layers, built in three phases:

```
┌─────────────────────────────────────────────────────────────┐
│                      Phase 3: Data Stack                     │
│                                                              │
│  ┌───────────┐  ┌──────────┐  ┌────────────┐               │
│  │Containers │  │ Storage  │  │  Semantic  │               │
│  │(B+Tree)   │  │ Engines  │  │  Graphs   │               │
│  └───────────┘  └──────────┘  └────────────┘               │
├──────────────────────────────────────────────────────────────┤
│                   Phase 2: Compiler Stack                     │
│                                                              │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌───────┐ ┌────────┐ │
│  │Parser│ │Types │ │LLVM/ │ │Interp│ │ RETE  │ │Datalog │ │
│  │      │ │Check │ │MLIR  │ │      │ │Engine │ │Engine  │ │
│  └──────┘ └──────┘ └──────┘ └──────┘ └───────┘ └────────┘ │
│                                                              │
│  ┌──────────────────────────────────────┐                    │
│  │    M-Code (Hermes Document ASTs)     │                    │
│  └──────────────────────────────────────┘                    │
├──────────────────────────────────────────────────────────────┤
│                  Phase 1: Runtime Foundation                  │
│                                                              │
│  ┌─────────┐  ┌──────┐  ┌─────────────────┐  ┌───────────┐ │
│  │ Hermes  │  │ HRPC │  │    Reactor       │  │  Custom   │ │
│  │ (data)  │  │(comm)│  │ (green fibers    │  │  Clang    │ │
│  │         │  │      │  │  + io_uring)     │  │           │ │
│  └─────────┘  └──────┘  └─────────────────┘  └───────────┘ │
└──────────────────────────────────────────────────────────────┘
```

## 2. Phase 1 Components

### 2.1 Custom Clang (Green Fiber Support)
Two-color execution model:
- **Green code** (`[[logos::green]]`): segmented stacks, lightweight fibers
- **Red code** (default): system stack, full C/C++ ABI compatibility
- **Trampolines**: auto-generated at green↔red call boundaries

Segmented stacks: small initial segment (e.g. 4KB), grow on demand by allocating new segments. No stack copying (unlike Go's contiguous model), deterministic behavior. Stack overflow detected by guard page or explicit check at function prologue.

Green fibers = green function + own segmented stack + scheduler context. Thousands per core, non-preemptive, yield at IO points.

### 2.2 IO Reactor
Full reimplementation (not ported from Memoria). One reactor per CPU core, pinned.

- **IO backend**: io_uring exclusively (Linux 5.6+, no epoll fallback)
- **Concurrency**: M:1 green fiber scheduling on each core
- **Cross-core**: lock-free SPSC/MPSC message queues (no shared memory, no locks)
- **Primitives**: spawn, yield, sleep, join, channels, fiber-aware mutexes
- **IO**: file (io_uring), network TCP (io_uring), timers

No thread pool, no work stealing. Each core manages its own fibers. Simplicity and determinism over theoretical flexibility.

### 2.3 Hermes (Ported from Memoria)
Same algorithms, same binary format, new codebase. Wire-compatible with Memoria.

Core capabilities:
- Arena allocator with relative pointers (relocatable, zero-copy)
- Tagged type system (1-32 byte tags, 56-bit integer optimization)
- TinyObjectMap (16-byte overhead, PopCnt O(1) lookup, keys 0..51)
- Arrays: ObjectArray, TypedArray<T>
- Maps: ObjectMap (Varchar→Object), TypedMap<K,V>
- Datatypes with parametric constructors
- Three serialization formats: zero-copy, text, binary
- Copying GC (cheap for small documents)
- HermesPath query language
- Jinja-like template engine
- Schema processor
- Profiles (pico/nano/micro/basic)

### 2.4 HRPC (Ported from Memoria)
Session-based bidirectional streaming RPC over Hermes.

- TCP transport integrated with reactor (green fibers)
- Service endpoint registry (256-bit UIDs)
- Zero-copy intra-process messaging (immutable Hermes documents)
- IDL tooling

## 3. Phase 2 Components

### 3.1 M-Code Data Model
All code model entities are small Hermes documents (TinyObjectMap-based). Large structures decomposed across Memoria containers.

Entity types: Module, Assembly, Function, Class, Variable, TypeRef, Statement, Expression, Rule, Query, CodeBlock, Metadata.

Type references: 192-256 bit hash codes of normalized declarations. Code registry: native C++ function binding descriptors.

### 3.2 Logos Language
AI-first, human-readable.

- S-expression-based syntax, 1:1 mapping to Hermes AST
- Dependent types (incremental: refinement types → full dependent types)
- Ownership/borrowing (Own<T>, Ref<T>, MutRef<T>)
- Dual paradigm: imperative CF + RETE data-flow rules
- Native Hermes integration (inline documents, comprehensions)
- Concurrency: green fiber spawn, channels, structured concurrency
- Pattern matching
- Module system

### 3.3 Compiler Pipeline
```
Logos source text
  → Lexer → tokens
  → Parser → M-Code AST (Hermes document, directly)
  → Type checker (inference, ownership, dependent types)
  → M-Code AST (typed, verified)
  → LLVM/MLIR lowering (custom Logos MLIR dialect → standard dialects)
  → LLVM optimization passes
  → Native code (JIT or AOT)
```

Alternative paths:
- M-Code AST → Interpreter (REPL, compile-time metaprogramming)
- M-Code AST → RETE network compilation (for rule definitions)
- M-Code AST → Datalog query plan (for query definitions)

### 3.4 RETE Engine
Forward-chaining rule system for Complex Event Processing.

Alpha network (filters) → Beta network (joins) → Conflict resolution → Reaction execution. Working memory: Hermes document collections. Incremental: fact changes propagate through network. Integration with interpreter and compiled code.

Self-applicability: runtime metrics (execution time, memory pressure, etc.) exposed as facts. RETE rules can fire on runtime state changes → programs that monitor and adapt own execution.

### 3.5 Datalog Engine
Backward-chaining declarative queries. Superset of SQL semantics (adds recursion, deduction). Semi-naive evaluation. Integration with RETE (consumes forward-chained events).

## 4. Phase 3 Components

### 4.1 Core Data Structures
Ported from Memoria: packed allocator, partial sum trees, searchable sequences, SSRLE, LOUDS trees, wavelet trees, associative memory.

### 4.2 Container Framework
B+Tree-based, block-organized (4K-1MB blocks). CoW and ephemeral variants. Multistream B+Trees for hierarchical containers. Storage-agnostic (IStore interface). Types: Set, Map, Multimap, Vector, Sequence, hierarchical.

### 4.3 Storage Engines
- **MemoryStore**: confluently persistent, MWMR, in-memory
- **SWMRStore**: SSD-optimized, durable, SWMR

Three-level memory management:
1. Inside Hermes document: arena + copying GC (no per-object overhead)
2. Hot data (recent snapshots): tracing GC (most CoW copies are short-lived)
3. Cold data (historical): reference counting (stable, rare deletions)

Generational boundary: snapshots surviving N commits promote from tracing to RC tier.

### 4.4 Semantic Graphs
RDF-like triples/quads in Hermes. Graph containers. SPARQL-like queries via Datalog. Integration with associative memory.

## 5. Key Design Properties

### 5.1 Code = Data (Hermes Documents in Containers)
All code model entities are Hermes documents. Large structures decomposed into many small documents stored in Memoria containers (B+Tree). Each document fits in a single storage block (4-8KB), keeping copying GC cheap. Container provides indexing, versioning (CoW), and zero-copy access. Code is stored, queried, versioned, transmitted, and hardware-accelerated like any other data.

### 5.2 AI-First Generation
Unambiguous S-expression syntax. Dependent types allow AI to emit proofs. Ownership model is mathematically verifiable. Rich metadata on every AST node. AI generates structure (Hermes AST), not text.

### 5.3 Self-Applicability (HOCP)
Runtime metrics are first-class events. RETE rules fire on internal state changes. Programs observe and adapt their own execution. FCRS inductive bias operationalized as language primitive.

### 5.4 Two-Color Concurrency
Green fibers (segmented stacks) for lightweight concurrency. Red functions (system stack) for system compatibility. Trampolines at boundaries. No viral coroutine problem. Thousands of fibers per core.

### 5.5 C++ as Implementation Language, Not Target
Logos is written in C++, but C++ interop is secondary. Standard library starts in C++, migrates to Logos. Generated code passes through LLVM, not C++ text. Programmers will stop reading generated code as they stopped reading assembly.
