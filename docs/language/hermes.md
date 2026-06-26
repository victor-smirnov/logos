# Hermes in Logos

Hermes is Logos's data substrate: a relocatable, tagged, schema-aware object graph format with native maps, arrays, typed arrays, decimals, strings, and user-defined datatypes. This is the user-level view; for runtime architecture see [Hermes Runtime](../internals/hermes-runtime.md).

## Hermes Is Built Into the Language

Hermes is **not** an imported library; it is part of Logos itself:

- **`@{...}` and `@[...]` are grammar literal forms.** The parser produces Hermes literal AST nodes like array or struct literals. No macro layer, no DSL, no string interpolation.
- **Capture is type-checked at sema.** `$ident` and `${expr}` inside a Hermes literal are real expressions; type errors against the target shape are reported at compile time.
- **View types live in the type system.** `HView2` and per-datatype views are real Logos types tracked by the borrow checker.
- **Unified trait dispatch.** `HermesStringify`, `HermesEqual`, `HermesHash`, `HermesClone`, `HermesRelease`, `HermesRead`, `HermesWrite` are ordinary Logos traits; user datatypes implement them like any other.
- **Static literals fold to rodata.** Module-scope Hermes literals become `HermesStatic` blobs — length-prefixed, read-only, no runtime parsing.
- **Schema codes are part of type identity.** A type's content-addressed hash and its Hermes type code share one source; language and data substrate share one notion of "what type is this."

Result: idiomatic Logos mixes plain values, structured data, and persistent/serialized data with no FFI boundary, DSL, or codegen step. A Hermes document is just a value.

## What Hermes Gives You

- One binary format for storage, RPC, and IPC.
- A document = a *zone* (arena) + root object pointer; the whole graph is relocatable as bytes.
- Maps, arrays, typed arrays (`I32`, `U64`, …), decimals, strings, booleans, integers, and user datatypes in one type system.
- Schemas for user types; a global registry maps 8-byte type codes to definitions.
- Views (`HView2`, value views) read a Hermes graph without copying or owning it.

## Building a Document

**Hermes literals** (`@{...}`, `@[...]`) produce a `Hermes` document directly and support capture:

```logos
use logos.lang.hermes.tmpl;
use logos.lang.hermes.container;
use logos.lang.hermes.anyval;
use logos.lang.rc;

let id: i32 = 42;
let doc: Rc<Hermes> = @{ "id": $id, "ok": true, "tags": ["fast", "safe"] };
```

**Parsing text** at runtime (file, network). Same Hermes surface grammar, but `parse` does *not* perform Logos-side capture — input is interpreted purely as Hermes text:

```logos
use logos.lang.hermes.container;
use logos.lang.hermes.anyval;
use logos.mem.hermes.parser;
use logos.lang.rc;

let ctr: Rc<Hermes> = hermes_rc(8192);
let h: &Hermes = ctr.deref();
let root: HAny = unsafe { parse(h, r#"
    {
        name: "widget",
        version: 42,
        i32_array: <I32> [1, 2, 3, 4]
    }
"#) };
```

Text parser: `src/hermes/text_parser.cpp`. Documents can also be built programmatically (`Map::set`, `Array::push`, …) — the showcase example walks this path.

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

let doc1: Rc<Hermes> = @{ "id": $id };
let doc2: Rc<Hermes> = @{ "ok": $flag };
let doc3: Rc<Hermes> = @{ "sum": ${a + b} };
```

Capture is type-checked, coerced when safe, and supports `as<T>[...]` casts for typed arrays. Available **only** in `@`-literal syntax — `doc.parse(...)` does not interpret `$`/`${...}`.

## View Types

A view is a borrow into a Hermes zone:

- **`HView2`** — borrow into a document zone, lifetime-bound to its source.
- **Per-type views** — e.g. a planned `DecimalView` carrying base + pointer.

Views are non-owning by default; an owning document is held by `Rc<Hermes>`, which keeps a refcount on the memory holder. See [Ownership](ownership.md#views-and-owning-references). `HermesStatic` is a separate flavour: a length-prefixed read-only document in rodata, produced by module-scope literals.

## Three Serialization Modes

One logical document, three interchangeable representations, chosen by consumer need.

### Zero-Copy

The native in-memory layout: one zone, internal pointers as offsets, so heap/disk/shared-memory bytes are *the same bytes* — no parse on read. Use for:

- **Storage and database objects** — persist as-is, direct access without deserialization, including random access into nested structure.
- **Fast inter-thread/inter-process communication** — pass a zone pointer or shared-memory handle; no copy, no parse.
- **Accelerator offload** — share bytes with devices owning their own address space (GPUs, smart NICs, FPGAs); the relocatable offset structure makes it safe.

### Binary Serial

Compact validated wire format for network use: drops alignment slack and arena bookkeeping, *validated* on decode so a compromised peer cannot hand you a malformed document. Codec: `src/hermes/binary_codec.cpp`. Use for **RPC payloads** (HRPC frames Hermes this way) and **long-haul transport** where size matters and the receiver does not trust the sender.

### SDN (String Data Notation)

The human-readable text form; every type prints and parses itself. SDN appears in source (`@{...}`, `@[...]`), in `stringify(root)` output, and in `r#"..."#` fed to `parse`. Use for **human interaction** (config, debug output, logs, REPL) and as the **universal tunnel** — non-Hermes-aware systems emit/consume Hermes data as text.

### Round-Tripping

Any value moves losslessly between all three forms. Text parser, binary codec, and zero-copy clone are wired through the same trait surface, so every registered datatype gets all three for free.

## Schemas and Type Codes

Every Hermes value carries an 8-byte schema type code. Codes 1–128 are reserved for the standard registry (Map, Array, String, Decimal, …); user types get a 56-bit code from the SHA-256 of the type definition. `TypedValue` (tag 106) is the fallback wrapper for *unregistered* types only — standard registered types must use their direct code.

## Maps, Arrays, Sets

`Map<K, V>` and `Array<T>` are the workhorses. No native `Set<T>` yet — use `Map<K, ()>` (a.k.a. `ObjectMap<K, null>`) for set semantics (string pools, type interning). Typed arrays (`<I32>`, `<U64>`, …) store elements unboxed — dramatically more compact for primitive payloads.

## Comprehensions Producing Hermes

`@[expr for x in iter if guard]` produces a document with an `ObjectArray` root; `@{k: v for x in iter}` one with a Map root — the most direct way to build a structured Hermes value from a Logos collection. See [Comprehensions](comprehensions.md).

## Sizing and Scope

Containers cover **4 bytes to 4 GB**, sweet spot **1–10 disk blocks of 4 KB**; below that, per-container overhead dominates. Above it, *read* performance stays competitive — a static (immutable) 4 GB container traverses about as fast as a comparable heap-object graph (offset pointers, dense layout, O(1) link traversal). The upper-end constraint is **GC**, not access: the simple copying collector's cost grows linearly with container size, and cycle handling allocates extra working memory during the copy. Gigabyte-scale containers should be treated as effectively immutable, or split.

Mental model: *document*, not *database* — an arbitrarily structured object graph with **O(1) link traversal** within one container; the unit of storage, transport, and access, not the universe of data. For large deeply-structured data, use *many* containers referencing each other through application-level identifiers (URL-like, not pointers). A future system layer (Memoria-style containers) will host such graphs with Hermes containers as the leaf type; from Hermes's view every container is self-contained, and cross-container references are application data, invisible to the runtime's pointer mesh.

## Zones and Ownership

A container is internally a **zone** — one contiguous memory block, 4 B–4 GB; the unit of allocation, relocation, and GC. Zones are not heap-bound: a zone can live on the heap (default for new documents) or inside another container — a B+Tree page, a memory-mapped file, a network buffer, a parent Hermes value.

Zone ownership is **hierarchical**, with two roles:

- **Owners** allocate, grow, free. A heap-resident `Hermes` document owns itself.
- **`HermesView`s** hold a handle to someone else's zone, allocating/freeing nothing — they only **report the memory is in use** via a refcount on the owner.

The contract is asymmetric: the owner decides *when* memory is allocated/freed; Hermes only signals *whether* it is in use. The owner acts on that signal as it likes — keep resident, evict at RC 0, defer, batch. Canonical case: disk-cache integration — code walking Hermes data inside a B+Tree leaf page operates through a `HermesView` that bumps the page refcount; the cache will not evict a non-zero-count page; when views go away, the cache regains eviction freedom. Hermes never makes the eviction decision, only makes the *in-use* fact observable. The same pattern covers memory-mapped files, RDMA buffers, accelerator-shared regions, embedded sub-zones.

## Memory Management

In-container memory is managed by a **simple copying GC**: walk the reachable set from the root, copy into a fresh container, drop or reuse the old. No mark-and-sweep, no generations, no concurrent collector. This works because: containers are size-bounded (full copy is cheap); internal pointers are offsets (no rewriting); no cross-container raw pointers exist (inter-container references are application-level, opaque to GC). Hermes needs no conventional heap manager — compaction *is* the GC: copy when you want to reclaim, don't otherwise.

## When to Use Hermes

Use Hermes for:

- A self-describing object graph surviving serialization without a custom codec.
- A document-shaped unit of storage/transport/IPC (1–10 × 4 KB sweet spot).
- Schema-tagged values participating in the global type system.
- Zero-copy access from disk, across processes, or to accelerators.

For purely transient in-memory work, plain Logos types (`struct`, `Array<T>`, `Map<K, V>`) are usually right. For datasets exceeding the per-container range, model as a graph of Hermes containers linked by application-level references, with Memoria-style containers (future) hosting the graph.

## Examples

- [examples/hermes_round_trip.logos](../../examples/hermes_round_trip.logos) — minimal parse + stringify.
- [examples/hermes_showcase.logos](../../examples/hermes_showcase.logos) — capture, typed arrays, view types, broader API surface.
