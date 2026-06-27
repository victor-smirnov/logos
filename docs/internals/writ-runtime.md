# Writ Runtime

Writ is the data substrate. The runtime is implemented in C++ under [src/writ/](../../src/writ/) and exposed to Logos via the standard library at `stdlib/std/writ`.

This page describes the runtime model. For the user-facing language view, see [Writ in Logos](../language/writ.md).

## Core Concepts

### Zone

A *zone* is a multi-segment region of memory. All objects in one Writ document live in one zone; intra-graph links are stored as **self-relative `i64` offsets** (`RelativePtr`), so the whole graph is relocatable as bytes and a document addresses a practically 64-bit space — there is no 4 GB ceiling (that cap belongs to the compiler's `arena_offset_t`/TypePool IR arena, a separate subsystem; see [Compiler Architecture](architecture.md)). A multi-segment zone is compacted to a single contiguous segment for serialization or embedding. The full model — never-move, isolation, the root zone, `!Drop` ZTypes — is in [Zones](../language/zones.md).

A zone is not tied to the heap. It can live on the heap (the default), inside a B+Tree page, inside a memory-mapped file, inside a network buffer, or as a sub-zone within a larger Writ container.

**Mutability.** Zones come in two flavors:

- **Mutable** — supports allocation and structural updates. The active form during construction.
- **Immutable** — append-only or fully frozen, suitable for serialization, sharing, and rodata embedding.

Construction proceeds in the Mutable form; serialization, sharing, and rodata embedding use the Immutable form.

**Ownership hierarchy.** Allocation responsibility is split, and the contract is intentionally asymmetric:

- **Owners** allocate, grow, and free the zone they own. A heap-resident `Writ` document is its own owner.
- **`WritView`s** never allocate or free. A view holds a handle to someone else's zone and bumps a refcount on the owner; releasing the view drops the refcount. That is the entire interaction.

The Writ runtime does not decide when memory becomes available or unavailable. It only **reports usage** — "currently in use by N views" — and the owner reads that signal and acts on it as it sees fit (keep resident, evict when RC=0, defer, batch, …).

This split is load-bearing for embedding Writ data inside other storage systems. The canonical use case is disk-cache integration: code that walks Writ data inside a B+Tree leaf page operates through a `WritView` over that page; the view's refcount keeps the cache from evicting the page until the last view drops. When the count returns to zero, the cache regains the freedom to evict — Writ does not push that decision.

The same pattern applies to memory-mapped files, RDMA buffers, accelerator-shared regions, and nested sub-zones within a larger document.

### Datatype, Storage, View

Writ is being refactored around a Datatype × Storage × View architecture:

- **Datatype** — semantic type (a `Map`, `Decimal`, `String`, user schema, …).
- **Storage** — physical layout (inline bytes, pointer-to-arena, fat reference, …).
- **View** — borrowed handle over a Datatype + Storage with a lifetime.

GAT-like associated types relate the three. `UnsizedPayload` covers variable-length types. `Meta + Atom` model the small-object case. The refactor is in progress.

### Type Registry

Every Writ value carries an 8-byte schema type code. The registry is global per process:

- A non-contiguous low band is reserved for system types — small scalars/containers near the bottom, typed arrays at 2101–2110, maps at 3101–3104, and so on (see `include/logos/writ/type_codes.hpp`).
- User type codes are derived as a 56-bit slice of the SHA-256 of the type definition.
- A `TypedValue` wrapper (`TYPEDVALUE = 4115`) covers values whose type is unregistered at runtime.

Registered standard types must use their direct type code, not `TypedValue` — this is enforced.

### Operation Dispatch

The C++ runtime dispatches each operation (stringify, clone, encode/decode, …) by `switch`-ing on the 8-byte type code read from the vlen `TypeTag` that precedes every object — see the walkers in `stringify.cpp`, `clone.cpp`, and `binary_codec.cpp`. There is no C++ trait-object registry; the per-type-code tag-dispatch table from legacy was retired in favour of these direct walkers.

On the *Logos* side, the corresponding behaviours are expressed as blanket trait impls (e.g. stringify / equality / hashing over Writ values); these are language-level traits, not C++ types.

`WritStatic` (rodata literals) is read-only by construction and length-prefixed.

## Files

| File | Purpose |
|------|---------|
| [arena.cpp](../../src/writ/arena.cpp) | Zone allocator. |
| [text_parser.cpp](../../src/writ/text_parser.cpp) | Text-format parser. |
| [stringify.cpp](../../src/writ/stringify.cpp) | Type-code-dispatched stringification walker. |
| [binary_codec.cpp](../../src/writ/binary_codec.cpp) | Wire-format encode/decode. |
| [clone.cpp](../../src/writ/clone.cpp) | Cross-zone deep clone. |
| [map.hpp](../../include/logos/writ/map.hpp), [typed_array.hpp](../../include/logos/writ/typed_array.hpp) | Container implementations (header-only). |
| [document.hpp](../../include/logos/writ/document.hpp), [view.hpp](../../include/logos/writ/view.hpp) | Document handle and view materialization (header-only). |
| [arena_pool.cpp](../../src/writ/arena_pool.cpp), [external_ref.cpp](../../src/writ/external_ref.cpp), [import_table.cpp](../../src/writ/import_table.cpp) | Multi-arena pool, cross-arena refs, import tables. |

`exerciser_*.cpp` files are standalone C++ programs that drive each subsystem; they predate full Logos coverage and remain useful for low-level testing.

## Three Serialization Modes

A Writ value has three interchangeable on-the-wire forms; every registered datatype implements all three through the same trait surface. The user-facing summary is in [language/writ.md](../language/writ.md#three-serialization-modes); this section covers the implementation split.

| Mode | Implementation | Use |
|------|----------------|-----|
| **Zero-copy** | The native in-memory layout: a zone of bytes plus a root offset. Persist or transmit the bytes verbatim; reads are pointer arithmetic, no parse step. Cross-zone copies go through [clone.cpp](../../src/writ/clone.cpp). | Storage / DB objects with direct access into nested structure, fast IPC via shared-memory handles, accelerator offload to devices with separate address spaces (GPU/SmartNIC/FPGA). The relocatable offset structure is what makes that safe. |
| **Binary serial** | Compact, validated wire format. [binary_codec.cpp](../../src/writ/binary_codec.cpp). Drops alignment slack and arena bookkeeping; the decoder *validates* before exposing the result, so a trusted-but-compromised peer cannot smuggle malformed data through. | RPC payloads (HRPC), long-haul network transport. |
| **SDN (String Data Notation)** | Human-readable text. [text_parser.cpp](../../src/writ/text_parser.cpp) + [stringify.cpp](../../src/writ/stringify.cpp). The same syntax appears in `@{...}` literals, `r#"..."#` strings fed to `parse`, and `to_string()` output. | Human interaction (config, debug, logs, REPL). Universal tunnel for participants that have no Writ adapter — they can produce or consume Writ data as plain text. |

All three round-trip losslessly: the text parser, binary codec, and zero-copy clone all dispatch through the same trait registry, so every registered datatype gets all three forms for free.

## Three-Implementation Strategy

The long-term plan is three native implementations:

1. **Logos** — reference implementation, source of truth.
2. **Rust** — derived from the Logos implementation.
3. **C++** — current implementation, becomes a follower as the Logos one matures.

All three share one wire format. Conformance tooling lives per language. There is **no FFI between them** — interoperation is by passing zone pointers, with each language reading the format directly.

## Layer 2 (Deferred)

Cross-language Logos↔C++ schema sync, plus link-time collision detection on user type codes, is deferred until the Writ Logos API stabilizes further.

## Capture Pipeline

Writ capture (the `$ident` / `${expr}` syntax inside `@{...}` and `@[...]` literals) is implemented end to end: surface syntax, AST representation, capture-site collection in sema, template codegen, value coercion, `as<T>[...]` typed-array casts, integration with the parser-level template machinery, and view-type integration with the `WritRead`/`WritWrite` trait split.
