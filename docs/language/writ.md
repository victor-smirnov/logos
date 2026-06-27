# Writ in Logos

Writ is Logos's data substrate: a relocatable, tagged, schema-aware **generic object graph** with native maps, arrays, typed arrays, decimals, strings, and user-defined datatypes conformat with [ZType requirements](zones.md). This is the user-level view; for runtime architecture see [Writ Runtime](../internals/writ-runtime.md).

> **The name.** *Writ* is from Old English *ġewrit* — "a writing", an authoritative written record (as in *a writ*, *holy writ*: a formal written instrument). It fits because a Writ value **is** a self-describing written record — the in-memory bytes *are* the record, durable and portable, not a message about one. (The earlier name, *Hermes*, named a messenger — apt for transport, wrong for a storage format that *holds* data rather than *carries* it.) It also pairs with **Logos** (Greek *λόγος*, "the word / reason"): Logos is the word; a Writ is the *written* word. The messaging layer that carries Writs between systems is [**Hest**](hest.md) (HRPC = "Hest RPC").

## A Dynamic Object Graph over ZTypes

Writ is built in layers. Underneath it are **ZTypes** — the `#[zoned]`, zone-resident typed values of [Zones](zones.md). Over them Writ provides the semantics of a **high-level, dynamic, generic object graph**: maps, arrays, typed arrays, strings, decimals, and user datatypes as nodes, linked by self-relative references, navigated and reshaped at runtime.

What makes the graph *dynamic* is **tagged memory**: every object is prefixed by a small **variable-length type tag** (see [Schemas and Type Codes](#schemas-and-type-codes)). Code can read that tag and **dispatch on a value's type at runtime** — exactly as a dynamically-typed language inspects a value — with no static knowledge of the shape.

That buys the defining property: **the flexibility of a dynamically-typed high-level language at the speed of a statically-typed one.** You build, inspect, and dispatch over arbitrary structured data at runtime (the dynamic half), while the bytes keep a compiler-known, zero-copy, unboxed layout with no parse step (the static half).

## Writ Is Built Into the Language

Writ is **not** an imported library; it is part of Logos itself:

- **`@{...}` and `@[...]` are grammar literal forms.** The parser produces Writ literal AST nodes like array or struct literals. No macro layer, no DSL, no string interpolation.
- **Capture is type-checked at sema.** `$ident` and `${expr}` inside a Writ literal are real expressions; type errors against the target shape are reported at compile time.
- **View types live in the type system.** `WView2` and per-datatype views are real Logos types tracked by the borrow checker.
- **Unified trait dispatch.** `WritStringify`, `WritEqual`, `WritHash`, `WritClone`, `WritRelease`, `WritRead`, `WritWrite` are ordinary Logos traits; user datatypes implement them like any other.
- **Static literals fold to rodata.** Module-scope Writ literals become `WritStatic` blobs — length-prefixed, read-only, no runtime parsing.
- **Schema codes are part of type identity.** A type's content-addressed hash and its Writ type code share one source; language and data substrate share one notion of "what type is this."

Result: idiomatic Logos mixes plain values, structured data, and persistent/serialized data with no FFI boundary, DSL, or codegen step. A Writ document is just a value.

## What Writ Gives You

- One binary format for storage, RPC, and IPC.
- A document = a *zone* + a root object pointer; the whole graph is relocatable as bytes.
- Maps, arrays, typed arrays (`I32`, `U64`, …), decimals, strings, booleans, integers, and user datatypes in one type system.
- Schemas for user types; a global registry maps 8-byte type codes to definitions.
- Views (`WView2`, value views) read a Writ graph without copying or owning it.

## Building a Document

**Writ literals** (`@{...}`, `@[...]`) produce a `Writ` document directly and support capture:

```logos
use logos.lang.writ.tmpl;
use logos.lang.writ.container;
use logos.lang.writ.anyval;
use logos.lang.rc;

let id: i32 = 42;
let doc: Rc<Writ> = @{ "id": $id, "ok": true, "tags": ["fast", "safe"] };
```

**Parsing text** at runtime (file, network). `parse_writ` takes Writ text and returns a fresh, owned `Writ` with the parsed graph already set as its root — the one-call form of *new container + parse + set root*. The document is RAII-freed on drop; navigate straight from `doc.root()`. Same Writ surface grammar as the `@`-literals, but parsing does *not* perform Logos-side capture (`$ident` / `${expr}`) — input is interpreted purely as Writ text:

```logos
use logos.lang.writ.container;   // Writ
use logos.mem.writ.parser;       // parse_writ

let doc: Writ = parse_writ(r#"
    {
        name: "widget",
        version: 42,
        i32_array: <I32> [1, 2, 3, 4]
    }
"#);
let root: WAny = doc.root();   // null on parse error — check doc.root().is_null()
```

Parser: `stdlib/mem/writ/parser.logos` (`parse_writ`, and the lower-level `parse(h, text)` that fills an existing container). Documents can also be built programmatically (`Map::set`, `Array::push`, …) — the showcase example walks this path.

## Stringifying

```logos
let s: String = stringify(root);
print_string(&s);
```

Each type chooses its own short/long stringification form (inline for primitives, `@Type() = init` for tagged decimals); the router only dispatches.

## Capture

Inside a `@{...}`/`@[...]` literal, `$ident` splices a Logos variable and `${expr}` an arbitrary expression at construction time:

```logos
let id: i32  = 42;
let flag: bool = true;
let a: i32 = 10;
let b: i32 = 5;

let doc1: Rc<Writ> = @{ "id": $id };
let doc2: Rc<Writ> = @{ "ok": $flag };
let doc3: Rc<Writ> = @{ "sum": ${a + b} };
```

Capture is type-checked, coerced when safe, and supports `as<T>[...]` casts for typed arrays. Available **only** in `@`-literal syntax — `doc.parse(...)` does not interpret `$`/`${...}`.

## View Types

A view is a borrow into a Writ zone:

- **`WView2`** — borrow into a document zone, lifetime-bound to its source.
- **Per-type views** — e.g. a planned `DecimalView` carrying base + pointer.

Views are non-owning by default; an owning document is held by `Rc<Writ>`, which keeps a refcount on the memory holder. See [Ownership](ownership.md#views-and-owning-references). `WritStatic` is a separate flavour: a length-prefixed read-only document in rodata, produced by module-scope literals.

## Three Serialization Modes

One logical document, three interchangeable representations, chosen by consumer need.

### Zero-Copy

The native in-memory layout: one zone, internal pointers as offsets, so heap/disk/shared-memory bytes are *the same bytes* — no parse on read. Use for:

- **Storage and database objects** — persist as-is, direct access without deserialization, including random access into nested structure.
- **Fast inter-thread/inter-process communication** — pass a zone pointer or shared-memory handle; no copy, no parse.
- **Accelerator offload** — share bytes with devices owning their own address space (GPUs, smart NICs, FPGAs); the relocatable offset structure makes it safe.

### Binary Serial

Compact validated wire format for network use: drops alignment slack and arena bookkeeping, *validated* on decode so a compromised peer cannot hand you a malformed document. Codec: `src/writ/binary_codec.cpp`. Use for **RPC payloads** (HRPC frames Writ this way) and **long-haul transport** where size matters and the receiver does not trust the sender.

### SDN (String Data Notation)

The human-readable text form; every type prints and parses itself. SDN appears in source (`@{...}`, `@[...]`), in `stringify(root)` output, and in `r#"..."#` fed to `parse`. Use for **human interaction** (config, debug output, logs, REPL) and as the **universal tunnel** — non-Writ-aware systems emit/consume Writ data as text.

### Round-Tripping

Any value moves losslessly between all three forms. Text parser, binary codec, and zero-copy clone are wired through the same trait surface, so every registered datatype gets all three for free.

## Schemas and Type Codes

Every Writ object is tagged with a **type code**, stored as a **variable-length tag** in the bytes immediately before the object: a **single byte** for codes ≤ 222 (which covers the whole standard registry and then some), or a lead byte plus little-endian code bytes for larger codes — **1 to 8 bytes** in all. Codes **1–128** are reserved for the standard registry (Map, Array, String, Decimal, …); a user type gets a **content-addressed** code (the low 56 bits of the SHA-256 of its definition). This prefix tag is what makes runtime type dispatch a single read. `TypedValue` (tag 106) is the fallback wrapper for *unregistered* types only — standard registered types must use their direct code. Wire format: `include/logos/writ/type_tag.hpp`.

## Maps, Arrays, Sets

`Map<K, V>` and `Array<T>` are the workhorses. No native `Set<T>` yet — use `Map<K, ()>` (a.k.a. `ObjectMap<K, null>`) for set semantics (string pools, type interning). Typed arrays (`<I32>`, `<U64>`, …) store elements unboxed — dramatically more compact for primitive payloads.

## Comprehensions Producing Writ

`@[expr for x in iter if guard]` produces a document with an `ObjectArray` root; `@{k: v for x in iter}` one with a Map root — the most direct way to build a structured Writ value from a Logos collection. The form deliberately mirrors **Python's list/dict comprehensions**: it is one of a family of syntactic-sugar forms (alongside the `@{…}` / `@[…]` literals and capture) for constructing Writ data directly in program text, rather than through builder calls. See [Comprehensions](comprehensions.md).

## Sizing and Scope

Self-relative offsets are **`i64`**, so a document addresses a practically 64-bit space — there is **no 4 GB ceiling** (that cap belonged to the older narrow-offset format). What bounds size now is reclamation, not the format: memory is managed by a **copying/compacting garbage collector** whose cost grows linearly with live size and needs extra working memory during the copy — so a graph *can* be large but *should not* be. There is also a practical floor: below the **1–10 disk blocks of 4 KB** sweet spot, per-container overhead dominates. Read performance is not the constraint at size — a static (immutable) large container traverses about as fast as a comparable heap-object graph (offset pointers, dense layout, O(1) link traversal). Treat multi-gigabyte containers as effectively immutable, or split them.

Mental model: *document*, not *database* — an arbitrarily structured object graph with **O(1) link traversal** within one container; the unit of storage, transport, and access, not the universe of data. For large deeply-structured data, use *many* containers referencing each other through application-level identifiers (URL-like, not pointers). A future system layer (Memoria-style containers) will host such graphs with Writ containers as the leaf type; from Writ's view every container is self-contained, and cross-container references are application data, invisible to the runtime's pointer mesh.

## Zones and Ownership

> The zone memory model — multi-segment layout, self-relative `i64` offsets, isolation, the root zone, `!Drop` ZTypes — is specified in [Zones](zones.md) (canonical). This section is the Writ-level view.

A container is internally a **zone** — a multi-segment region. Objects never move within the zone, so their self-relative `i64` offsets stay valid for the zone's whole life; the zone is the unit of allocation, relocation, and reclamation. Zones are not heap-bound: a zone can live on the heap (default for new documents) or inside another container — a B+Tree page, a memory-mapped file, a network buffer, a parent Writ value.

Zone ownership is **hierarchical**, with two roles:

- **Owners** allocate, grow, free. A heap-resident `Writ` document owns itself.
- **`WritView`s** hold a handle to someone else's zone, allocating/freeing nothing — they only **report the memory is in use** via a refcount on the owner.

The contract is asymmetric: the owner decides *when* memory is allocated/freed; Writ only signals *whether* it is in use. The owner acts on that signal as it likes — keep resident, evict at RC 0, defer, batch. Canonical case: disk-cache integration — code walking Writ data inside a B+Tree leaf page operates through a `WritView` that bumps the page refcount; the cache will not evict a non-zero-count page; when views go away, the cache regains eviction freedom. Writ never makes the eviction decision, only makes the *in-use* fact observable. The same pattern covers memory-mapped files, RDMA buffers, accelerator-shared regions, embedded sub-zones.

## Memory Management

Memory is reclaimed by a **copying/compacting garbage collector** — a copying GC, but **on demand, not in the background**, and running **no destructors** (ZTypes are `!Drop`). To reclaim, walk the reachable set from the root, copy it into a fresh zone, then drop or reuse the old one. It is cheap because: zones are size-bounded (a full copy is cheap); internal references are self-relative offsets (the copy needs no pointer rewriting); no cross-zone raw pointers exist (inter-container references are application-level, opaque to the collector). Collection is the only reclamation step — copy when you want to reclaim, don't otherwise — so Writ needs no conventional heap manager. Within a zone's life, segments only grow (append); shrinking or moving a zone's base is the borrow-gated relocation specified in [Zones](zones.md).

## Metadata and RTTI

Because a Writ document is just bytes with no parse step, Writ is Logos's carrier for **metadata**. Annotations and **RTTI / reflection data** are emitted as **zero-serialized Writ containers in the process `.rodata`** — `WritStatic` blobs, read-only and length-prefixed, fixed up at link time. Reading them is a pointer dereference, not a deserialization: `reflect::<T>()` hands back a `WritStatic` view over the type's reflection document. One uniform, parse-free path covers **both system metadata** (type layouts, schemas, registry codes) **and application-level metadata** (user annotations) — any metadata is available at runtime at the cost of a memory read.

## Writ as the Compiler's Substrate

Logos dogfoods Writ at the deepest level — it is the format `logosc` itself runs on:

- **The compiler's internal IR is Writ.** `logosc` is currently written in C++, and its AST and LIR are Writ object graphs — every AST node is a Writ map (a `CODE` discriminant plus typed children). The compiler is, in effect, a Writ data-processing pipeline.
- **Libraries ship their AST as embedded Writ.** A compiled library embeds the program's AST/LIR as **zero-serialized Writ** inside the library file; the compiler **memory-maps it back with no parse**, so loading a dependency is an `mmap`, not a re-parse — the basis for fast builds and precompiled generics.
- **Logos metaprograms produce IR that C++ reads.** A metaprogram written *in Logos* can construct compiler IR (Writ) that the C++ compiler then consumes — both sides speak the same format, so there is no marshalling boundary between Logos-authored and C++-authored compiler code.
- **The compiler can be heterogeneous.** Because the IR is a language-neutral Writ graph, compiler components can be written in Logos *or* another language and interoperate through it — e.g. codegen stays in C++ (it drives MLIR), while front-end and metaprogramming passes can move to Logos.

See [Compiler Architecture](../internals/architecture.md) and [Metaprogramming](../internals/metaprog.md).

## When to Use Writ

Use Writ for:

- A self-describing object graph surviving serialization without a custom codec.
- A document-shaped unit of storage/transport/IPC (1–10 × 4 KB sweet spot).
- Schema-tagged values participating in the global type system.
- Zero-copy access from disk, across processes, or to accelerators.

For purely transient in-memory work, plain Logos types (`struct`, `Array<T>`, `Map<K, V>`) are usually right. For datasets exceeding the per-container range, model as a graph of Writ containers linked by application-level references, with Memoria-style containers (future) hosting the graph.

## TinyObjectMap: the Thesis in One Container

The container that best embodies "dynamic flexibility at static speed" is the **TinyObjectMap** — in the stdlib, `WMap<Wu6, WAny>` (the *bitmap-indexed* map). It is the Writ workhorse: every `logosc` AST node *is* one.

Its entire header is **24 bytes** — a `u64` packing a **52-bit presence bitmap** + 6-bit capacity + 6-bit size, a `u64` **schema code** (the node-class tag), and a self-relative pointer to a separate, key-ordered array of `WAny` values. Keys are small integers (0–51); values are dynamic `WAny`s; the value array is dense and held in key order.

That layout buys both halves of the thesis at once:

- **Static-object speed.** A field lookup is `bitmap & (1 << key)`, a popcount for the dense index, and one indexed load — no hashing, no probing, no per-entry key storage. It costs about what a struct field offset costs, except the field *set* is chosen at runtime. The node is a tight, cache-friendly block.
- **Dynamic-typing flexibility.** Any subset of the 52 keys may be present, each value is a dynamically-typed `WAny`, and the `schema_type_code` lets a reader recognize the node's class with no prior schema. So one container type represents arbitrarily-shaped records — every distinct AST node kind is the *same* TinyObjectMap with a different set of keys present, navigable by code that has never seen that shape.

It is also **byte-identical across C++ and Logos** (`include/logos/writ/tiny_object_map.hpp` ⟷ `stdlib/lang/writ/wmap.logos`): both sides read and write the same 24-byte layout, which is what makes the heterogeneous-compiler story above mechanical rather than a bridge.

## Examples

- [examples/writ_round_trip.logos](../../examples/writ_round_trip.logos) — minimal parse + stringify.
- [examples/writ_showcase.logos](../../examples/writ_showcase.logos) — capture, typed arrays, view types, broader API surface.
