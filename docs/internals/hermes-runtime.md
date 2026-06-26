# Hermes Runtime

Hermes is the data substrate. The runtime is implemented in C++ under [src/hermes/](../../src/hermes/) and exposed to Logos via the standard library at `stdlib/std/hermes`.

This page describes the runtime model. For the user-facing language view, see [Hermes in Logos](../language/hermes.md).

## Core Concepts

### Zone

A *zone* is a single contiguous block of memory between 4 bytes and 4 GB. All objects in one Hermes document live in one zone, with internal pointers stored as offsets — the entire graph is relocatable as bytes.

A zone is not tied to the heap. It can live on the heap (the default), inside a B+Tree page, inside a memory-mapped file, inside a network buffer, or as a sub-zone within a larger Hermes container.

**Mutability.** Zones come in two flavors:

- **Mutable** — supports allocation and structural updates. The active form during construction.
- **Immutable** — append-only or fully frozen, suitable for serialization, sharing, and rodata embedding.

Construction proceeds in the Mutable form; serialization, sharing, and rodata embedding use the Immutable form.

**Ownership hierarchy.** Allocation responsibility is split, and the contract is intentionally asymmetric:

- **Owners** allocate, grow, and free the zone they own. A heap-resident `Hermes` document is its own owner.
- **`HermesView`s** never allocate or free. A view holds a handle to someone else's zone and bumps a refcount on the owner; releasing the view drops the refcount. That is the entire interaction.

The Hermes runtime does not decide when memory becomes available or unavailable. It only **reports usage** — "currently in use by N views" — and the owner reads that signal and acts on it as it sees fit (keep resident, evict when RC=0, defer, batch, …).

This split is load-bearing for embedding Hermes data inside other storage systems. The canonical use case is disk-cache integration: code that walks Hermes data inside a B+Tree leaf page operates through a `HermesView` over that page; the view's refcount keeps the cache from evicting the page until the last view drops. When the count returns to zero, the cache regains the freedom to evict — Hermes does not push that decision.

The same pattern applies to memory-mapped files, RDMA buffers, accelerator-shared regions, and nested sub-zones within a larger document.

### Datatype, Storage, View

Hermes is being refactored around a Datatype × Storage × View architecture:

- **Datatype** — semantic type (a `Map`, `Decimal`, `String`, user schema, …).
- **Storage** — physical layout (inline bytes, pointer-to-arena, fat reference, …).
- **View** — borrowed handle over a Datatype + Storage with a lifetime.

GAT-like associated types relate the three. `UnsizedPayload` covers variable-length types. `Meta + Atom` model the small-object case. The refactor is in progress.

### Type Registry

Every Hermes value carries an 8-byte schema type code. The registry is global per process:

- A non-contiguous low band is reserved for system types — small scalars/containers near the bottom, typed arrays at 2101–2110, maps at 3101–3104, and so on (see `include/logos/hermes/type_codes.hpp`).
- User type codes are derived as a 56-bit slice of the SHA-256 of the type definition.
- A `TypedValue` wrapper (`TYPEDVALUE = 4115`) covers values whose type is unregistered at runtime.

Registered standard types must use their direct type code, not `TypedValue` — this is enforced.

### Operation Dispatch

The C++ runtime dispatches each operation (stringify, clone, encode/decode, …) by `switch`-ing on the 8-byte type code read from the vlen `TypeTag` that precedes every object — see the walkers in `stringify.cpp`, `clone.cpp`, and `binary_codec.cpp`. There is no C++ trait-object registry; the per-type-code tag-dispatch table from Hermes1 was retired in favour of these direct walkers.

On the *Logos* side, the corresponding behaviours are expressed as blanket trait impls (e.g. stringify / equality / hashing over Hermes values); these are language-level traits, not C++ types.

`HermesStatic` (rodata literals) is read-only by construction and length-prefixed.

## Files

| File | Purpose |
|------|---------|
| [arena.cpp](../../src/hermes/arena.cpp) | Zone allocator. |
| [text_parser.cpp](../../src/hermes/text_parser.cpp) | Text-format parser. |
| [stringify.cpp](../../src/hermes/stringify.cpp) | Type-code-dispatched stringification walker. |
| [binary_codec.cpp](../../src/hermes/binary_codec.cpp) | Wire-format encode/decode. |
| [clone.cpp](../../src/hermes/clone.cpp) | Cross-zone deep clone. |
| [map.hpp](../../include/logos/hermes/map.hpp), [typed_array.hpp](../../include/logos/hermes/typed_array.hpp) | Container implementations (header-only). |
| [document.hpp](../../include/logos/hermes/document.hpp), [view.hpp](../../include/logos/hermes/view.hpp) | Document handle and view materialization (header-only). |
| [arena_pool.cpp](../../src/hermes/arena_pool.cpp), [external_ref.cpp](../../src/hermes/external_ref.cpp), [import_table.cpp](../../src/hermes/import_table.cpp) | Multi-arena pool, cross-arena refs, import tables. |

`exerciser_*.cpp` files are standalone C++ programs that drive each subsystem; they predate full Logos coverage and remain useful for low-level testing.

## Three Serialization Modes

A Hermes value has three interchangeable on-the-wire forms; every registered datatype implements all three through the same trait surface. The user-facing summary is in [language/hermes.md](../language/hermes.md#three-serialization-modes); this section covers the implementation split.

| Mode | Implementation | Use |
|------|----------------|-----|
| **Zero-copy** | The native in-memory layout: a zone of bytes plus a root offset. Persist or transmit the bytes verbatim; reads are pointer arithmetic, no parse step. Cross-zone copies go through [clone.cpp](../../src/hermes/clone.cpp). | Storage / DB objects with direct access into nested structure, fast IPC via shared-memory handles, accelerator offload to devices with separate address spaces (GPU/SmartNIC/FPGA). The relocatable offset structure is what makes that safe. |
| **Binary serial** | Compact, validated wire format. [binary_codec.cpp](../../src/hermes/binary_codec.cpp). Drops alignment slack and arena bookkeeping; the decoder *validates* before exposing the result, so a trusted-but-compromised peer cannot smuggle malformed data through. | RPC payloads (HRPC), long-haul network transport. |
| **SDN (String Data Notation)** | Human-readable text. [text_parser.cpp](../../src/hermes/text_parser.cpp) + [stringify.cpp](../../src/hermes/stringify.cpp). The same syntax appears in `@{...}` literals, `r#"..."#` strings fed to `parse`, and `to_string()` output. | Human interaction (config, debug, logs, REPL). Universal tunnel for participants that have no Hermes adapter — they can produce or consume Hermes data as plain text. |

All three round-trip losslessly: the text parser, binary codec, and zero-copy clone all dispatch through the same trait registry, so every registered datatype gets all three forms for free.

## Three-Implementation Strategy

The long-term plan is three native implementations:

1. **Logos** — reference implementation, source of truth.
2. **Rust** — derived from the Logos implementation.
3. **C++** — current implementation, becomes a follower as the Logos one matures.

All three share one wire format. Conformance tooling lives per language. There is **no FFI between them** — interoperation is by passing zone pointers, with each language reading the format directly.

## Layer 2 (Deferred)

Cross-language Logos↔C++ schema sync, plus link-time collision detection on user type codes, is deferred until the Hermes Logos API stabilizes further.

## Capture Pipeline

Hermes capture (the `$ident` / `${expr}` syntax inside `@{...}` and `@[...]` literals) is implemented end to end: surface syntax, AST representation, capture-site collection in sema, template codegen, value coercion, `as<T>[...]` typed-array casts, integration with the parser-level template machinery, and view-type integration with the `HermesRead`/`HermesWrite` trait split.
