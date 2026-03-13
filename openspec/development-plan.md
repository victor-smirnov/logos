# Logos Development Plan

## Constraints
- Primary developer: AI (via Cursor IDE and similar tools)
- Human review and build: Victor Smirnov
- Implementation language: C++23 (Clang, custom fork with green fibers)
- Compiler backend: LLVM/MLIR
- Source of functionality: Memoria Framework (reimplemented, not linked)
- Build: CMake + Ninja, VCPKG for dependencies
- Platform: Linux (Ubuntu LTS)

## Project Structure
```
logos/
├── agent/                 # AI bootstrap (Synthea identity + cognitive architecture)
├── openspec/              # Specifications and design documents
├── memoria/               # Memoria reference (gitignored, read-only reference)
│
├── clang/                 # Custom Clang fork (green fiber support)
│   └── ...
│
├── hermes/                # Hermes data format (ported from Memoria)
│   ├── include/logos/hermes/
│   └── lib/
│
├── hrpc/                  # HRPC protocol (ported from Memoria)
│   ├── include/logos/hrpc/
│   └── lib/
│
├── reactor/               # IO reactor (new, green fibers)
│   ├── include/logos/reactor/
│   └── lib/
│
├── compiler/              # Logos compiler
│   ├── include/logos/compiler/
│   │   ├── ast/           # M-Code AST (Hermes-backed)
│   │   ├── parser/        # Logos parser
│   │   ├── types/         # Type checker, dependent types, ownership
│   │   ├── codegen/       # LLVM/MLIR backend
│   │   └── interp/        # Interpreter (REPL, compile-time)
│   └── lib/
│
├── rete/                  # RETE forward-chaining engine
│   ├── include/logos/rete/
│   └── lib/
│
├── datalog/               # Datalog backward-chaining engine
│   ├── include/logos/datalog/
│   └── lib/
│
├── containers/            # Data containers (ported from Memoria, Phase 3)
│   ├── include/logos/containers/
│   └── lib/
│
├── storage/               # Storage engines (ported from Memoria, Phase 3)
│   ├── include/logos/storage/
│   └── lib/
│
├── stdlib/                # Logos standard library (C++ initially, migrates to Logos)
│   └── ...
│
├── tools/                 # CLI tools
│   ├── logos-cli/         # Compiler + REPL
│   └── logos-lsp/         # Language server
│
└── tests/
    ├── hermes/
    ├── hrpc/
    ├── reactor/
    ├── compiler/
    ├── rete/
    └── integration/
```

---

## Phase 1: Runtime Foundation

Phase 1 is split into two sub-phases. Phase 1A builds the verification infrastructure and validates it on real Memoria code. Phase 1B uses that infrastructure to port Hermes and the rest of the runtime.

### 1.0 Verification Framework (Phase 1A)

**Goal:** Build the runtime observability and self-checking infrastructure that all subsequent development relies on. Validate on real cases from Memoria before porting begins.

**Deliverables:**
- [ ] `LOGOS_ASSERT` macro: structured assertion with requirement ID, formatted context, source location, call chain capture
- [ ] `LOGOS_TRACE` macro: checkpoint instrumentation writing to SQLite (tag, key-value data, timestamp, thread ID)
- [ ] Call chain capture via `-finstrument-functions` + thread-local ring buffer (~100 lines runtime)
- [ ] SQLite trace database: schema for assertions + traces (see `specs/verification-framework.md`)
- [ ] Trace summarizer: script producing hierarchical summaries (Level 0 overview → Level 1 per-requirement → Level 2 full detail)
- [ ] Exerciser harness: minimal framework for writing component exerciser programs
- [ ] Coverage checker: script verifying all spec requirement IDs appear in implementation assertions
- [ ] CMake integration: build flags for instrumentation on/off, trace database path configuration
- [ ] Validation: apply framework to 2-3 real Memoria components (e.g., arena allocator, vlen encoding, TinyObjectMap) to verify the approach works before full Hermes port

**Key design:**
- Optimistic convergence model: trust the model, verify via runtime self-checks, escalate on non-convergence
- Structured feedback for LLM consumption: queryable SQLite, hierarchical summarization, spec-linked diagnostics
- Minimal footprint: ~500-800 lines of C++ infrastructure, simple Python summarizer
- Selective instrumentation: per-tag enable/disable at runtime, `no_instrument_function` for hot paths

**Spec:** [`specs/verification-framework.md`](specs/verification-framework.md)

### 1.1 Custom Clang (Green Fibers)

**Goal:** Clang fork that supports two-color execution model.

**Deliverables:**
- [ ] `[[logos::green]]` function attribute → compile for segmented stack
- [ ] Segmented stack runtime (allocate/extend/shrink stack segments)
- [ ] Trampoline generation at green↔red call boundaries
- [ ] Stack overflow detection for green stacks
- [ ] Integration with CMake build system

**Key design:**
- Green functions use segmented stack (small initial segment, grows on demand)
- Red functions use system stack (default, full C++ ABI compatibility)
- Trampoline at boundary: save green context → switch to red stack (or vice versa)
- Green fibers: just green functions with their own segmented stack + scheduler context

**Reference:** Go's goroutine stack model (segmented → contiguous, but we stay segmented for simplicity and determinism). Also: GCC split-stacks, historical Rust segmented stacks.

### 1.2 IO Reactor

**Goal:** Thread-per-core reactor with green fibers, replacing Memoria's multiple backends.

**Deliverables:**
- [ ] Core reactor loop (one per CPU core, io_uring-based)
- [ ] Green fiber scheduler (M:1 on each core, non-migrating)
- [ ] Fiber primitives: spawn, yield, sleep, join
- [ ] Synchronization: channels, mutexes (fiber-aware, not OS-level)
- [ ] Timer subsystem
- [ ] File IO (io_uring)
- [ ] Network IO (io_uring): TCP accept/connect/read/write
- [ ] Signal handling
- [ ] Graceful shutdown

**Key design:**
- One reactor thread per core, pinned (no migration)
- All IO via io_uring (no epoll fallback, Linux 5.6+ only)
- Green fibers: lightweight, non-preemptive, yield at IO points
- Cross-core communication: lock-free message passing (SPSC/MPSC queues)
- No thread pool, no work stealing (each core has its own fiber pool)

### 1.3 Hermes Port (Phase 1B)

**Goal:** Port Hermes from Memoria into standalone library within Logos, using the verification framework from Phase 1A.

**Deliverables:**
- [ ] Arena allocator (contiguous segments, relative pointers)
- [ ] Tagged type system (2-byte tags for core types, type hash)
- [ ] Core types: integers (8/16/32/64), floats, booleans, strings (Varchar)
- [ ] EmbeddingRelativePtr (pointer/value mode, 7-byte embedding)
- [ ] TinyObjectMap (16-byte overhead, PopCnt-based)
- [ ] ObjectArray, TypedArray<T>
- [ ] ObjectMap (Varchar→Object), TypedMap<K,V>
- [ ] Datatypes with parametric constructors
- [ ] Serialization: zero-copy, text (stringify/parse), binary
- [ ] Copying GC (deep copy / compactification)
- [ ] HermesPath query language
- [ ] Template engine (Jinja-like)
- [ ] Schema processor (CheckStructureState)
- [ ] Hermes profiles (pico/nano/micro/basic)
- [ ] Immutability enforcement
- [ ] All spec invariants from `hermes-abi.json` implemented as LOGOS_ASSERT
- [ ] Exerciser programs for all scenarios from spec
- [ ] Fuzz testing for container operations and serialization round-trips

**Key decision:** This is a *port*, not a wrapper. New namespace (`logos::hermes::`), new build target, but same algorithms and data layout for wire compatibility (see `specs/hermes-wire-format.md`).

**Verification:** Component is done when Definition of Done criteria are met (see `agent/09_development_methodology.md`).

### 1.4 HRPC Port

**Goal:** Port HRPC protocol with reactor integration.

**Deliverables:**
- [ ] HRPC core protocol (session, endpoint, streaming)
- [ ] TCP transport (on top of reactor's network IO)
- [ ] Bidirectional RPC
- [ ] Service endpoint registry
- [ ] IDL tools (schema → code generation)
- [ ] Zero-copy Hermes message passing (intra-process, green fibers)

**Phase 1 Exit Criteria:**
Programs can create Hermes documents, serialize/deserialize them, communicate via HRPC over TCP, run concurrent green-fiber workloads on the reactor with io_uring IO. Custom Clang compiles green/red code with proper stack management.

---

## Phase 2: Logos Compiler Stack

### 2.1 M-Code Data Model

**Goal:** Hermes-backed code model for Logos programs.

**Deliverables:**
- [ ] Code model types (TinyObjectMap-based):
  - Module, Assembly, Function, Method, Class, Struct, Enum
  - Variable, TypeRef (with ownership qualifier)
  - Statement variants, Expression variants
  - Rule (for RETE), Query (for Datalog)
  - CodeBlock, Metadata, Annotation
- [ ] Code registry (native function bindings)
- [ ] M-Code text format (human-readable, leveraging Hermes text serialization)
- [ ] Structural and semantic validation (Hermes schema)

### 2.2 Logos Language Design

**Goal:** Finalize language syntax and semantics.

**Deliverables:**
- [ ] Formal grammar (S-expression-based, unambiguous)
- [ ] Type system specification:
  - Dependent types (start with refinement types, extend incrementally)
  - Ownership/borrowing (Own<T>, Ref<T>, MutRef<T>)
  - Hermes datatypes as first-class types
  - Parametric types with constructors
- [ ] Execution model specification (CF + DF + rules)
- [ ] Module system (imports, exports, visibility)
- [ ] Pattern matching
- [ ] Hermes integration syntax (inline documents, comprehensions)
- [ ] Concurrency primitives (fiber spawn, channels, structured concurrency)
- [ ] Error handling model

### 2.3 Parser

**Goal:** Logos source → M-Code AST (Hermes document).

**Deliverables:**
- [ ] Lexer
- [ ] Recursive descent parser (directly produces Hermes AST, no intermediate IR)
- [ ] Error recovery and diagnostics
- [ ] Source location tracking (in Hermes metadata)

### 2.4 Type Checker

**Goal:** Verify type safety, ownership, dependent type constraints.

**Deliverables:**
- [ ] Type inference engine
- [ ] Ownership/borrow checker
- [ ] Dependent type constraint solver (incremental: refinement types first)
- [ ] Proof object generation (attached as Hermes metadata)
- [ ] Diagnostics with source locations

### 2.5 LLVM/MLIR Backend

**Goal:** M-Code → native code via LLVM/MLIR.

**Deliverables:**
- [ ] M-Code → MLIR lowering (custom Logos dialect → standard dialects)
- [ ] Ownership semantics → LLVM lifetime annotations
- [ ] Hermes operations → runtime library calls
- [ ] Green fiber integration (green function calling convention)
- [ ] JIT compilation (for REPL, hot-reload)
- [ ] AOT compilation (for deployment)
- [ ] Debug info generation (DWARF, source maps to Logos)

### 2.6 Interpreter

**Goal:** Tree-walking interpreter for REPL and compile-time metaprogramming.

**Deliverables:**
- [ ] M-Code AST walker
- [ ] Hermes manipulation from interpreted code
- [ ] Native function calls (via code registry)
- [ ] REPL (read-eval-print loop)
- [ ] Compile-time evaluation (metaprograms are M-Code run at compile time)

### 2.7 RETE Engine

**Goal:** Forward-chaining rule evaluation.

**Deliverables:**
- [ ] Alpha network (pattern → filter)
- [ ] Beta network (joins)
- [ ] Working memory (Hermes document collections)
- [ ] Conflict resolution (priority, specificity, recency)
- [ ] Rule compilation (Logos rule syntax → RETE network)
- [ ] Incremental updates (add/remove facts propagate)
- [ ] Integration with interpreter and compiled code

### 2.8 Datalog Engine

**Goal:** Backward-chaining declarative queries.

**Deliverables:**
- [ ] Datalog parser (or Logos subset for queries)
- [ ] Query planner
- [ ] Evaluation engine (backward chaining, semi-naive)
- [ ] Recursive queries (transitive closure)
- [ ] Integration with RETE (consume forward-chained events)

### 2.9 Tools

**Deliverables:**
- [ ] `logos-cli`: compiler driver + REPL
- [ ] `logos-lsp`: Language Server Protocol implementation
- [ ] `logos-fmt`: code formatter
- [ ] `logos-doc`: documentation generator

**Phase 2 Exit Criteria:**
Can write Logos source files with dependent types and ownership annotations. Compiler type-checks, compiles to native via LLVM/MLIR, and runs on green fibers. REPL works. RETE rules fire. Datalog queries evaluate. LSP provides IDE support.

---

## Phase 3: Data Stack

### 3.1 Core Data Structures

**Goal:** Port Memoria's building blocks.

**Deliverables:**
- [ ] Packed allocator (in-block memory management)
- [ ] Partial/prefix sum trees
- [ ] Searchable sequences (rank/select, 1-8 bit alphabets)
- [ ] Compressed symbol sequences (SSRLE)
- [ ] LOUDS trees (labeled, cardinal)
- [ ] Multiary wavelet trees
- [ ] Associative memory (multiscale spatial decomposition)

### 3.2 Container Framework

**Goal:** Port Memoria's B+Tree-based containers.

**Deliverables:**
- [ ] Block infrastructure (packed allocator, block IDs)
- [ ] B+Tree core (CoW and ephemeral variants)
- [ ] Multistream B+Trees
- [ ] Container type system (datatypes → container instantiation)
- [ ] Container types: Set, Map, Multimap, Vector, Sequence
- [ ] Hierarchical containers
- [ ] Logos-native container API (not just C++ API wrapper)

### 3.3 Storage Engines

**Goal:** Port Memoria's storage engines with hybrid GC.

**Deliverables:**
- [ ] MemoryStore (confluently persistent, MWMR)
- [ ] SWMRStore (SSD-optimized, durable)
- [ ] Hybrid GC: tracing for hot data, RC for cold historical data
- [ ] Transaction API (snapshots, branches, commit, rollback)
- [ ] Logos-native transaction syntax

### 3.4 Semantic Graphs

**Goal:** First-class graph support.

**Deliverables:**
- [ ] RDF-like triples/quads in Hermes
- [ ] Graph containers (using container framework)
- [ ] SPARQL-like query support (via Datalog engine)
- [ ] Integration with associative memory for efficient pattern matching

**Phase 3 Exit Criteria:**
Full data platform. Logos programs create, query, and persist data in versioned containers backed by durable storage. Semantic graphs work. Associative memory available for AI applications.

---

## Cross-Cutting Concerns

### Testing Strategy
- Unit tests per module (Hermes, HRPC, reactor, compiler components)
- Integration tests (end-to-end: Logos source → compile → run → verify output)
- Conformance tests (Hermes wire format compatibility with Memoria)
- Stress tests (reactor under load, concurrent fibers, large data sets)

### Documentation
- Openspec documents (this directory) for design decisions
- API documentation generated from code (logos-doc)
- Language reference (formal grammar + semantics)
- Tutorial/examples

### Performance
- Phase 1: reactor benchmarks (fiber throughput, IO latency)
- Phase 2: compiler benchmarks (compile time, generated code quality)
- Phase 3: container benchmarks (throughput, latency, compared to Memoria reference)

---

## Notes for AI Developer

- All code: C++23, latest stable Clang
- Namespace: `logos::hermes::`, `logos::hrpc::`, `logos::reactor::`, `logos::compiler::`, `logos::rete::`, etc.
- Follow modern C++ idioms (RAII, concepts, ranges, etc.) -- not bound by Memoria's older patterns
- Dependencies declared by AI, managed/linked by Victor
- Victor runs builds; AI does not execute build commands
- Hermes data layout must be wire-compatible with Memoria (same binary format)
- Green fiber support: initially via compiler attributes, segmented stack runtime
- LLVM/MLIR: use stable C++ API, version pinned by Victor
