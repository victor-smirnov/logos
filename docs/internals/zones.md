# Zones

A **zone** is the foundational memory model under everything Writ/ZType. It is a
multi-segment memory region with special invariants that make the data inside it
**position-independent** (relocatable/serializable as bytes) and **isolated**
from other zones.

This page is the canonical statement of the zone model. The type-system
integration (borrow rules, residency, `PackedAllocator` coupling) lives in
[zoned-types-design.md](zoned-types-design.md) and builds on this; the
high-level object-graph interface over zones is [Writ](../language/writ.md).

> **Layering.** Zones (this page) sit *below* **ZTypes** (`#[zoned]` types — the
> typed citizens of a zone) which sit *below* **Writ** (one high-level optic over
> ZTypes: an object graph + jq-like tooling). Writ is *a* superstructure over
> zones, not the only possible one — other optics (e.g. SoA/columnar, à la
> Memoria) are also ZTypes over zones.

## What a zone is

- A **multi-segment** region of memory. It grows by **appending segments**, never
  by moving what is already there.
- Owned by a single **holder** (`MemHolder`, refcounted). The holder owns the
  whole segment set; when its refcount hits zero, the entire zone — all segments
  — is freed at once. (Zone ≈ *arena* in the implementation.)
- The **unit of allocation, free, and compaction is the zone**, not the
  individual object. Zone objects are never individually freed.

## Special properties

1. **Self-relative references.** A reference *inside* a zone is not a machine
   pointer — it is a **self-relative offset** (the offset is relative to the
   reference's own location, so no base needs to be threaded anywhere). This is
   what makes a zone's contents position-independent.
2. **Never-move (within a zone).** An object never moves *relative to its zone* —
   growth appends a segment, it does not shift what is already there; compaction
   copies into a *fresh* zone. So an intra-zone self-relative offset never goes
   stale from "the object moved underneath me". (Relocating the zone *as a whole*
   is a separate question — a single-segment zone permits it; see below.)
3. **Position-independence / serializability.** Because every intra-zone
   reference is self-relative and no absolute pointers are stored, a zone (or its
   **compacted single-segment blob**) can be `memcpy`'d, relocated, memory-mapped,
   or written to disk **as bytes with no fixups**. The in-memory layout *is* the
   wire format (zero-copy).
4. **Self-description (at the ZType layer).** Objects carry their own
   `type_code`/`size` in-band, so the bytes are self-contained. (This is a ZType
   property, listed here because it is what makes position-independence useful.)

## Single- vs multi-segment, and movability

A zone has two physical forms; they differ in whether the zone *as a whole* may be
moved in the address space:

- **Single-segment** — one contiguous block. Every internal reference is
  self-relative, so the whole block is **freely relocatable**: `memcpy`/`mmap` it
  anywhere, only the external holder's base pointer changes. The simple, normal
  form — what a sealed/compacted zone is.
- **Multi-segment** — several blocks. Relocating it would mean moving *all*
  segments **in parallel** (preserving every cross-segment self-relative offset),
  which is impractical and has **no real use case** ⟹ a multi-segment zone is
  **never moved as a whole**; its segments stay put for its lifetime.

Multi-segment is purely the **construction** form: building a large object graph (a
Writ graph) grows by appending segments. Once built, for convenience it is
**compacted into a single-segment zone** — the movable / serializable / embeddable
rigid block.

**Segment resize + base stability.** A zone segment may **grow or shrink**. Hard
invariant: external objects holding references *into* the zone assume the zone
**base does not change** under them — a resize must preserve the base, so those
external refs (holders/roots in the zero zone) survive it.

## Isolation, and the zero/gluing zone

**Zones are isolated.** A self-relative reference is only meaningful within its
own zone; it **cannot cross a zone boundary**. Direct cross-zone references are
forbidden by construction — there is no self-relative offset that addresses
another zone.

The only sanctioned inter-zone link is an **indirect handle**, not a pointer:
an [`ExternalRef`](../../include/logos/writ/external_ref.hpp) carries only
`(arena_id, obj_id)` and is resolved through the global arena pool + directory —
never a raw remote pointer. (This matches the Memoria rule: cross-container
references are application-level identifiers, not pointers.)

**The zero zone (the gluing zone).** The ordinary program memory — **heap and
stack** — is treated as a special "zone zero". Unlike the isolated zones, objects
in the zero zone **may hold references *into* zones**: a stack/heap value (an
owning holder `Rc<…>`/`MemHolder`, a root, an ordinary Logos value) can point at
a zone object. The zero zone is the **glue** that connects the isolated zones to
the running program — it is where the roots and holders that keep zones alive
live.

```
   zero zone (heap/stack) ── glue ──┐  may reference INTO zones
        holder ─────────────────────┼──▶ ┌──────────── zone A ────────────┐
        root  ──────────────────────┘    │ obj ⟶ obj (self-relative)      │
                                         │ segment | segment | segment    │
                                         └────────────────────────────────┘
   zone A  ⇎  zone B   : no direct refs; only ExternalRef(arena_id,obj_id)
```

## Lifecycle

- **Construct** — a zone is created with a head segment, owned by a holder.
- **Grow** — append a segment (the holder's segment set extends); existing
  segments and offsets stay valid (never-move).
- **Seal** — the mutable→immutable transition. A sealed zone never grows again ⟹
  its layout is permanently frozen ⟹ maximally optimizable / embeddable / rodata.
- **Compact** — copy the live data into a fresh **single-segment** zone (the rigid,
  freely-relocatable blob used for serialization / `.wr0` / rodata embedding). This
  is the routine multi-segment→single-segment conversion after construction: build
  in a growing multi-segment zone, then compact for convenience and movability.
- **Free** — drop the holder; the whole segment set is released at once.

## Mutability as a zone state

A zone is in one of two states, surfaced to the type system as a parameter
(`Zone<Mutable>` / `Zone<Immutable>`, see [zone-as-parameter.md](zone-as-parameter.md)):

- **Mutable** — supports allocation and growth (the construction phase).
- **Immutable / sealed** — append-only frozen; suitable for sharing, rodata
  embedding, and serialization.

## Relationship to the rest of the system

- **ZTypes** (`#[zoned]` types) are the typed inhabitants of a zone — their
  fields are self-relative, their layout obeys the zone rules. A non-zoned Logos
  type cannot live in a zone (it may hold a machine pointer, which the zone rule
  forbids).
- **Writ** is the object-graph optic over zones+ZTypes (containers as graph
  nodes, dynamic `WAny` values, navigation/serialization). Other optics are
  possible over the same zones.
- **The zero zone** is where ordinary Logos code holds onto zones (via holders)
  and reads them (via views borrowed from the holder).

## Open questions / to confirm with the architect

> First draft — the model has evolved across several re-conceptualizations and
> the older texts carry contradictory noise. Flagging where this page may be
> ahead of / behind the intended model:

1. **Self-relative offset width / shape.** Older docs use a base-relative `u32`;
   the current Writ direction is self-relative `i64` (no base threaded). Confirm
   the canonical form and whether `RelPtr<T>` is the surfaced type.
2. **"Zero zone" terminology.** Is "zero zone / gluing zone" the term you want, or
   a different name for heap+stack-as-glue?
3. **Inter-zone = `ExternalRef` only?** Confirm `ExternalRef`(arena_id,obj_id)
   through the pool is *the* (and only) sanctioned cross-zone mechanism, vs other
   handle forms.
4. **Zone vs arena vs segment vocabulary.** This page treats zone ≈ arena
   (holder-owned segment set). Confirm whether "zone" and "arena" should be
   distinguished in user-facing vs internal language.
5. **PackedAllocator-carved nested zones.** The Memoria packed case (one buffer,
   nested allocators, sibling-shift coupling) — keep that entirely in
   zoned-types-design.md as the advanced case, or summarize here?
