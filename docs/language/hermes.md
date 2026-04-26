# Hermes in Logos

Hermes is Logos's data substrate: a relocatable, tagged, schema-aware object graph format with native support for maps, arrays, typed arrays, decimals, strings, and user-defined datatypes.

This page is the user-level view. For the runtime architecture, see [Hermes Runtime](../internals/hermes-runtime.md).

## Hermes Is Built Into the Language

Hermes is **not** a library you import and call. It is part of Logos itself:

- **`@{...}` and `@[...]` are literal forms in the grammar.** The Logos parser produces Hermes literal AST nodes the same way it produces array or struct literals. There is no macro layer, no DSL, no string interpolation.
- **Capture is type-checked at sema time.** `$ident` and `${expr}` inside a Hermes literal are real expressions seen by the semantic analyzer. Type errors against the target Hermes shape are reported at compile time, not at runtime.
- **View types live in the type system.** `HermesCtrView<'a>` and view types over individual datatypes are real Logos types with lifetimes, not opaque handles. The borrow checker tracks them.
- **The trait dispatch surface is unified.** `HermesStringify`, `HermesEqual`, `HermesHash`, `HermesClone`, `HermesRelease`, `HermesRead`, `HermesWrite` are ordinary Logos traits; user datatypes implement them the same way they implement any other trait.
- **Static literals fold to rodata.** Module-scope Hermes literals become `HermesStatic` blobs in the binary, length-prefixed and read-only. There is no runtime parsing for them.
- **Schema codes are part of the type identity.** A Logos type's content-addressed hash and its Hermes type code come from the same source; the language and the data substrate share one notion of "what type is this."

The result is that idiomatic Logos code mixes plain values, structured data, and persistent/serialized data without an FFI boundary, a DSL, or a code-generation step. A Hermes document is just a value.

## What Hermes Gives You

- A single binary format used for storage, RPC, and IPC.
- A document is a *zone* (arena) plus a root object pointer; the entire graph is relocatable as bytes.
- Maps, arrays, typed arrays (`I32`, `U64`, …), decimals, strings, booleans, integers, and user datatypes coexist in one type system.
- Schemas describe user types; a global schema registry maps 8-byte type codes to definitions.
- Views (`HermesCtrView<'a>`, value views) let you read a Hermes graph without copying or owning it.

## Building a Document

There are two ways to construct a Hermes document.

**Hermes literals** (`@{...}`, `@[...]`) are part of Logos syntax. They produce a `Hermes` document directly and support capture (see below):

```logos
use std.hermes.ctr;

let id: i32 = 42;
let doc: Hermes = @{ "id": $id, "ok": true, "tags": ["fast", "safe"] };
```

**Parsing a text form** at runtime, when the data is not a literal — for example coming from a file or network. The text grammar is the same Hermes surface syntax, but `parse` does *not* perform Logos-side capture; the input is interpreted purely as Hermes text.

```logos
let mut doc: Hermes = Hermes::new(8192);
doc.parse(r#"
    {
        name: "widget",
        version: 42,
        i32_array: <I32> [1, 2, 3, 4]
    }
"#)?;
```

The text parser lives in `src/hermes/text_parser.cpp`. Documents can also be built programmatically with `Map::set`, `Array::push`, etc.; the showcase example walks through this path.

## Stringifying

```logos
let s: String = doc.to_string();
print_string(&s);
```

Each Hermes type chooses its own short or long stringification form (e.g. inline for primitives, `@Type() = init` for tagged decimals); the router only dispatches.

## Capture

Capture is the bridge between Logos values and Hermes literals. Inside a `@{...}` / `@[...]` literal, `$ident` splices a Logos variable and `${expr}` splices an arbitrary expression into the document at construction time:

```logos
let id: i32  = 42;
let flag: bool = true;
let a: i32 = 10;
let b: i32 = 5;

let doc1: Hermes = @{ "id": $id };
let doc2: Hermes = @{ "ok": $flag };
let doc3: Hermes = @{ "sum": ${a + b} };
```

Capture is type-checked, coerced when safe, and supports `as<T>[...]` casts for typed arrays. It is **only** available in `@`-literal syntax — `doc.parse(...)` over a runtime text string does not interpret `$` or `${...}`.

## View Types

A view is a borrow into a Hermes zone. Views come in two flavours:

- **`HermesCtrView<'a>`** — a fat borrow of a whole document, lifetime-bound to its source.
- **Per-type views** — e.g. a planned `DecimalView` carrying base + pointer.

Views are non-owning by default. If a view must escape the function it was produced in, the compiler turns it into an *owning* view (`OView<T>`) that holds a reference count on the underlying memory holder. See [Ownership](ownership.md#views-and-owning-references).

`HermesStatic` is a separate flavour: a length-prefixed, read-only document baked into rodata. Hermes literals at module scope produce `HermesStatic`.

## Three Serialization Modes

A Hermes value has three interchangeable representations. The same logical document can be written or read in any of them; the choice is driven by what the consumer needs.

### Zero-Copy

The native in-memory layout. The whole document lives in one zone with internal pointers as offsets, so the bytes on the heap, on disk, or in shared memory are *the same bytes* — there is no parse step on read.

Use it for:

- **Storage and database objects** — Hermes values can be persisted as-is and accessed directly without deserialization, including random access into nested structure.
- **Fast inter-thread and inter-process communication** — pass a zone pointer (or shared-memory handle); no copying, no parsing.
- **Accelerator offload** — zero-copy bytes can be shared with devices that have their own address space (GPUs, smart NICs, FPGAs). The relocatable offset structure is what makes this safe.

### Binary Serial

A compact, validated wire format intended for network use. More compact than zero-copy because it drops alignment slack and arena bookkeeping; *validated* on decode so a trusted-but-compromised peer cannot hand you a malformed document and have it interpreted as one. The codec is in `src/hermes/binary_codec.cpp`.

Use it for:

- **RPC payloads** (HRPC frames Hermes documents in this form).
- **Long-haul transport** where size matters and the receiver does not trust the sender.

### SDN (String Data Notation)

The human-readable text form. Every Hermes type knows how to print itself as SDN and how to parse itself back. SDN is the form you see in source code (`@{...}`, `@[...]`), in stringified output (`doc.to_string()`), and in `r#"..."#` literals fed to `Hermes::parse`.

Use it for:

- **Human interaction** — config files, debug output, logs, REPL.
- **Universal tunnel** — runtimes or systems that have no Hermes adapter can still emit and consume Hermes data as text. SDN is the lingua franca for non-Hermes-aware participants.

### Round-Tripping

Any Hermes value can move between all three forms losslessly. The text parser, binary codec, and zero-copy clone are wired through the same trait surface, so every datatype that registers itself with the runtime gets all three for free.

## Schemas and Type Codes

Every Hermes value carries an 8-byte schema type code. Codes 1–128 are reserved for the standard registry (Map, Array, String, Decimal, …); user types receive a 56-bit code derived from the SHA-256 of the type definition.

A `TypedValue` (tag 106) is the fallback wrapper for *unregistered* types. Standard registered types (Decimal, etc.) must use their direct type code, not `TypedValue`.

## Maps, Arrays, Sets

`Map<K, V>` and `Array<T>` are the workhorse containers. There is no native `Set<T>` yet — until one lands, use `Map<K, ()>` (a.k.a. `ObjectMap<K, null>`) for set semantics, especially for string pools and type interning.

Typed arrays (`<I32>`, `<U64>`, etc.) store their elements unboxed and are dramatically more compact for primitive payloads.

## Comprehensions Producing Hermes

`@[...]` and `@{...}` accept comprehension forms — `@[expr for x in iter if guard]` produces a Hermes document with an `ObjectArray` root, and `@{k: v for x in iter}` produces one with a Map root. This is the most direct way to build a structured Hermes value from a Logos collection. See [Comprehensions](comprehensions.md).

## Sizing and Scope

Hermes containers cover the size range **4 bytes to 4 GB**, with the **sweet spot at 1–10 disk blocks of 4 KB**. Below that, per-container overhead dominates.

Above the sweet spot, *read* performance stays competitive: a static (immutable) 4 GB container is not meaningfully slower to traverse than a comparable graph of heap objects — internal pointers are offsets, layout is dense, and link traversal is still O(1). The constraint at the upper end is **garbage collection**, not access. Hermes uses a simple copying collector, not a generational one, so collection cost grows linearly with container size, and cycle handling allocates extra working memory during the copy. Containers that grow to gigabyte scale are best treated as effectively immutable, or split.

The mental model is *document*, not *database*: a Hermes value is an arbitrarily structured object graph with **O(1) link traversal** within one container. It is meant to be the unit of storage, the unit of transport, and the unit of access — not the universe of data.

For **large, deeply-structured data**, the right shape is *many* Hermes containers, with one container referencing another through application-level identifiers (URL-like references — not pointers). A future system layer (Memoria-style containers) will host such graphs and use Hermes containers as the leaf data type. From Hermes's point of view, every container is self-contained; cross-container references are application data, not part of the runtime's pointer mesh.

## Zones and Ownership

A Hermes container is, internally, a **zone** — a single contiguous block of memory of arbitrary size between 4 bytes and 4 GB. The zone is the unit of allocation, the unit of relocation, and the unit of garbage collection.

Zones are not tied to the heap. A zone can live:

- on the heap (the default for newly constructed documents);
- inside another container — for example, embedded in a B+Tree page, in a memory-mapped file, in a network buffer, or inside a parent Hermes value.

Ownership of a zone is **hierarchical**. The Hermes API distinguishes two roles:

- **Owners** allocate, grow, and free the zone. A heap-resident `Hermes` document is its own owner.
- **`HermesView`s** hold a handle to someone else's zone. A view does not allocate or free anything; it only **reports that the memory is in use**, by holding a reference count on the owner.

The contract is asymmetric: the owner decides *when* the memory is allocated and freed; Hermes only signals *whether* the memory is currently in use, through the refcount. The owner is free to act on that signal however it wants — keep the page resident, evict it once RC drops to zero, defer eviction, batch it, etc.

The canonical use case is disk-cache integration: when code walks Hermes data that lives inside a B+Tree leaf page in a cache, it operates through a `HermesView` over that page. The view bumps the page's refcount; the cache will not evict a page with a non-zero count. When all views go away the count drops, and the cache regains the freedom to evict. Hermes never makes the eviction decision — it only makes the *in-use* fact observable.

The same pattern applies to memory-mapped files, RDMA buffers, accelerator-shared regions, and embedded sub-zones within a larger document.

## Memory Management

Memory inside a Hermes container is managed by a **simple copying GC**: walk the reachable set from the root, copy it into a fresh container, drop or reuse the old one. There is no mark-and-sweep, no generational hierarchy, no concurrent collector — the whole algorithm is "copy what you can reach, throw the rest away."

This works because:

- A container is bounded in size, so a full copy is cheap.
- All internal pointers are offsets, so the copied bytes need no pointer rewriting.
- There are no cross-container raw pointers to fix up — references between containers are application-level identifiers, opaque to the GC.

In practice this means: Hermes does not need a heap manager in the conventional sense. Compaction *is* the GC. You copy when you want to reclaim, and you do not copy otherwise.

## When to Use Hermes

Use Hermes when you need:

- A self-describing object graph that survives serialization without a custom codec.
- A document-shaped unit of storage, transport, or IPC (1–10 × 4 KB is the sweet spot).
- Schema-tagged values that participate in the global type system.
- Zero-copy access from disk, across processes, or to accelerators.

For purely transient in-memory work, plain Logos types (`struct`, `Array<T>`, `Map<K, V>`) are usually the right choice. For large, deeply-structured datasets that exceed the per-container range, model the data as a graph of Hermes containers linked by application-level references, with Memoria-style containers (future) hosting the graph.

## Examples

- [examples/hermes_round_trip.logos](../../examples/hermes_round_trip.logos) — minimal parse + stringify.
- [examples/hermes_showcase.logos](../../examples/hermes_showcase.logos) — capture, typed arrays, view types, and the broader API surface.
