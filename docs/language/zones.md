# Zones

A **zone** is the foundational memory model under everything Writ/ZType. It is a multi-segment memory region with special invariants that make the data inside it **position-independent** (relocatable/serializable as bytes) and **isolated** from other zones.

This page is the canonical statement of the zone model. The type-system integration (borrow rules, residency, `PackedAllocator` coupling) lives in [zoned-types-design.md](../internals/zoned-types-design.md) and builds on this; the high-level object-graph interface over zones is [Writ](writ.md).

> **Layering.** Zones (this page) sit *below* **ZTypes** (`#[zoned]` types — the typed citizens of a zone) which sit *below* **Writ** (one high-level optic over ZTypes: an object graph + jq-like tooling). Writ is *a* superstructure over zones, not the only possible one — other optics (e.g. SoA/columnar, à la Memoria) are also ZTypes over zones.

## What a zone is

- A **multi-segment** region of memory. Segments may grow, but base must be stable. It usually grows by **appending segments** — what is already in it does not shift. This "never-move" is about **objects keeping their position *within* the zone** (§2), **not** about the zone being pinned to an address: a zone *as a whole* **can be relocatable** under the right conditions (a single-segment zone is freely movable — see [movability](#single--vs-multi-segment-and-movability) below).
- **Lifetime-managed by a holder**, integrated with reference counting at the language / type-system level. The holder governs the segment set and releases the whole zone (all segments) at once when its count drops.
- The **unit of allocation, free, and compaction is the zone**, not the individual object. Zone objects are never individually freed.

> **Zone vs arena.** A *zone* is the logical region with the invariants on this page. An *arena* is merely one **memory-allocation strategy used inside** a zone (the current one — it could be replaced by another). They are **not** synonyms: zone is the concept, arena an implementation detail. (The `arena_id` / "arena pool" naming in the code identifies a zone by its current backing allocator.)

> **The reference count need not be per-zone.** Integration with RC at the language level does *not* mean every zone carries its own dedicated counter. The management mechanism may be **grouped and indirect**: one holder / residency pin can govern *many* zones at once — e.g. a single count on a backing page/region/resource that hosts a group of zones — with the refcount living on that shared resource, not on each zone (this is the `Resident` / `SuperRc` consolidation — one pin per page, erased — described in [zoned-types-design.md §6](../internals/zoned-types-design.md)). "Holder" here is the *logical* owner; its physical form ranges from a per-zone `MemHolder` to a shared, indirect pin over a whole group.

## Special properties

1. **Self-relative references.** A reference *inside* a zone is not a machine pointer — it is a **self-relative `i64` offset** (relative to the reference's own location, so no base needs to be threaded anywhere). The width is **fixed at 64 bits on every target** — even on 32-bit, never a pointer-width type — so the encoding is word-size-independent. This is what makes a zone's contents position-independent.
2. **Never-move (within a zone).** An object never moves *relative to its zone* — growth appends a segment, it does not shift what is already there; compaction copies into a *fresh* zone. So an intra-zone self-relative offset never goes stale from "the object moved underneath me". (Relocating the zone *as a whole* is a separate question — a single-segment zone permits it; see below.) Because compaction *is* a copy of live data into a fresh zone, the model makes a **simple copying collector** natural — but **implies, without requiring**, one (a zone may instead be freed wholesale, never collected).
3. **Position-independence / serializability.** Because every intra-zone reference is self-relative and no absolute pointers are stored, a zone (or its **compacted single-segment blob**) can be `memcpy`'d, relocated, memory-mapped, or written to disk **as bytes with no fixups**. The in-memory layout *is* the wire format (zero-copy).
4. **Self-description (at the ZType layer).** Objects carry their own `type_code`/`size` in-band, so the bytes are self-contained. (This is a ZType property, listed here because it is what makes position-independence useful.)
5. **Architecture-independent layout.** ZTypes have a **stable, standard struct layout** (à la `repr(C)`) that does **not** depend on processor word size: fixed field types/sizes/alignment, `i64` offsets throughout, no `usize`/pointer-width fields. The *layout* decodes identically on any target — so a zone is portable *across machines* (e.g. 32 ↔ 64-bit), not merely relocatable within one address space. **Caveat:** this independence is over **layout**, not over the byte representation of the **scalar base types** — endianness and integer/float formats (`f32`/`f64` = IEEE-754) are *not* abstracted away. So cross-machine portability holds **between machines that share the same base-type representation** — i.e. one fixed scalar-wire convention, which is a single global choice and **much simpler to control** than layout independence (not a per-type concern). That convention is part of the [Logos Compute Model](../lcm/README.md) (LCM), which will specify the agreed base-data-type formats.

## Single- vs multi-segment, and movability

A zone has two physical forms; they differ in whether the zone *as a whole* may be moved in the address space:

- **Single-segment** — one contiguous block. Every internal reference is self-relative, so the whole block is **freely relocatable**: `memcpy`/`mmap` it anywhere, only the external holder's base pointer changes. The simple, normal form — what a sealed/compacted zone is.
- **Multi-segment** — several blocks. Relocating it would mean moving *all* segments **in parallel** (preserving every cross-segment self-relative offset), which is impractical and has **no real use case** ⟹ a multi-segment zone is **never moved as a whole**; its segments stay put for its lifetime.

Multi-segment is purely the **construction** form: building a large object graph (a Writ graph) grows by appending segments. Once built, for convenience it is **compacted into a single-segment zone** — the movable / serializable / embeddable rigid block.

**What a segment may undergo — bounded by external references.** The governing rule: a segment may undergo **any change that does not invalidate live references into the zone from the root zone (stack/heap)**. Concretely — **growing the size is safe** (existing data keeps its address, so those refs survive); **shrinking, or any reallocation that changes the base, is unsafe** (it would dangle them). Base stability under live references is the hard invariant: growth respects it, shrink / relocate do not.

**Relocation is borrow-gated (safety).** "Freely relocatable" is a *capability*, not a free-for-all. Holding a raw reference into a zone from the root zone (heap/stack) is on its own **partially unsafe** — the segment could move or shrink out from under it. So the rule: **a zone segment cannot be moved, shrunk, or otherwise rebased while live references into it exist from the heap/stack** (a base-preserving in-place grow is fine). It is enforced **both at the compiler level and in the type system**: a reference into a zone **borrows** it, and any base-changing op needs exclusive access, so a borrowed zone cannot be moved or rebased — the `Vec::push` invalidates `&vec[i]` rule, lifted to zones. This rides on the **base borrow checker** (expected to cover it as-is).

## ZType residency, movement, and escaping

ZType *values* are **zone-resident**, which constrains how they move:

- **Not stack-allocatable and not memory-movable** as ordinary values — a ZType lives inside a zone and is addressed through it. A few small special cases are exempt — notably **`WAny`** (a pod value that may sit in a register / on the stack).
- **The *zone* itself, however, may be backed by stack memory** — no restriction, exactly as it may live on the heap, in an `mmap`'d file, or inside another container's bytes. Objects in a stack-backed zone are still **zone-resident, not stack allocations**: the zone discipline (self-relative offsets, etc.) holds wherever the backing bytes sit — *where the zone lives* and *how its objects are addressed* are independent.
- **Escaping a zone absolutizes the references.** If a ZType is exceptionally copied or moved *out* of its zone (into the root zone), its internal **self-relative offsets are converted to absolute pointers** — outside a zone there is no self-relative frame, and absolute pointers live only in the root zone. (The inverse, *entering* a zone, links/interns references back to self-relative offsets.) This conversion **already works for `WAny`** but is **not yet generalized** to all eligible ZTypes.

## What zones enable

Self-relative `i64` encoding (§1) + architecture-independent layout (§5) + single-segment movability together make a (single-segment) zone a **fully self-contained, portable blob**:

- **Zero-copy IPC.** `mmap` the *same* zone into several processes' address spaces at once (shared memory). Each process sees it at a different base, but every internal reference is self-relative, so it just works — no serialize/deserialize at the boundary.
- **Persistence.** Write a zone to disk and `mmap` it back later (or into another process). The bytes *are* the format; loading is a memory-map, not a parse.
- **Across architectures.** All of the above works **between different machines and word sizes** (32 ↔ 64-bit) — the layout carries no pointer-width or base-dependent state, so the bytes mean the same thing on any machine that **shares the base-type representation** (§5 caveat: endianness / scalar formats must match).

This is the payoff of the invariants, and the reason ZTypes refuse machine pointers and pointer-width fields.

## Isolation, and the root zone

**Zones are isolated.** A self-relative reference is only meaningful within its own zone; it **cannot cross a zone boundary**. Direct cross-zone references are forbidden by construction — there is no self-relative offset that addresses another zone.

The only sanctioned inter-zone link is an **object identifier**, never a direct (machine) address. The identifier names the **target zone's identity** plus the object's **relative** location within it (a "zone base + offset" address, e.g. base + 0 for the zone's root object); the absolute address is reconstructed **only at resolution time** by looking the zone up and adding the offset to its *current* base. The reference is the identifier, **not** the object's address. (This is the Memoria rule too: cross-container references are application-level identifiers, not pointers.)

The exact identifier representation is **not** part of the zone model — it is left to the implementation. *As one example,* the Logos C++ compiler uses an [`ExternalRef`](../../include/logos/writ/external_ref.hpp) carrying `(arena_id, obj_id)`, resolved through a global arena pool + directory. That is a concrete, **non-standard** mechanism — illustrative of the rule, not a normative part of it.

**The root zone.** The ordinary program memory — **heap and stack** — is the **root zone**. Unlike the isolated zones, objects in the root zone **may hold references *into* zones**: a stack/heap value (an owning holder `Rc<…>`/`MemHolder`, a root, an ordinary Logos value) can point at a zone object. The root zone is the **glue** that connects the isolated zones to the running program — it is where the **roots** and holders that keep zones alive live (hence the name).

**The root zone is the inverse of a regular zone — that is precisely its job:**

- it is **not relocatable** — it *is* the whole address space, so there is nothing to move it relative to;
- it holds **absolute machine pointers**, not self-relative offsets — the ordinary program memory model;
- those absolute pointers may point **into child zones** (the glue direction).

The converse stays forbidden: a regular zone object can never hold an absolute pointer, so it cannot point into the root zone or directly into another zone. Absolute pointers live **only** in the root zone; self-relative offsets live **only** inside (regular) zones.

```
   root zone (heap/stack) ── glue ──┐  may reference INTO zones
        holder ─────────────────────┼──▶ ┌──────────── zone A ────────────┐
        root  ──────────────────────┘    │ obj ⟶ obj (self-relative)      │
                                         │ segment | segment | segment    │
                                         └────────────────────────────────┘
   zone A  ⇎  zone B   : no direct refs; only ExternalRef(arena_id,obj_id)
```

## Nesting

Zones can be **hierarchically nested** — a zone may live inside another zone's bytes. Nesting changes **nothing** about the rules: every invariant above holds **at each level** — each zone is self-relative internally, isolated at its boundary, and governed/freed as a unit. The **root zone sits at the top** of every such hierarchy (absolute pointers down into the outermost zones; self-relative offsets the rest of the way down).

The *allocation mechanism* that carves nested zones (`PackedAllocator`) is out of scope for this page — it gets its own dedicated document (and may ultimately live in the new Memoria).

## Lifecycle

- **Construct** — a zone is created with a head segment, owned by a holder.
- **Grow** — enlarge the zone while keeping the **base stable**, two ways: grow a segment **in place** where the allocator permits bounded base-preserving enlargement (e.g. a buddy allocator), or **append a new segment**. Either way existing offsets stay valid (never-move).
- **Seal** — the mutable→immutable transition. After it the zone never grows again ⟹ its layout is permanently frozen ⟹ maximally optimizable / embeddable / rodata. Sealing is **enforced at the type level** (statically via `Zone<Immutable>`, or dynamically) — **the zone stores no "sealed" bit**. Immutability is standard language-provided semantics, not state inside the zone's bytes.
- **Compact** — copy the live data into a fresh **single-segment** zone (the rigid, freely-relocatable blob used for serialization / `.wr0` / rodata embedding). This is the routine multi-segment→single-segment conversion after construction: build in a growing multi-segment zone, then compact for convenience and movability.
- **Free** — drop the holder; the whole segment set is released at once.

## Mutability as a zone state

A zone is in one of two states, surfaced to the type system as a parameter (`Zone<Mutable>` / `Zone<Immutable>`, see [zone-as-parameter.md](../internals/zone-as-parameter.md)):

- **Mutable** — supports allocation and growth (the construction phase).
- **Immutable / sealed** — append-only frozen; suitable for sharing, rodata embedding, and serialization.

This state is **tracked by the type system, not stored in the zone** — there is no runtime "sealed" flag in the bytes. The language enforces it statically (the `Zone<Immutable>` type) or, where needed, dynamically at boundaries.

## Relationship to the rest of the system

- **ZTypes** (`#[zoned]` types) are the typed inhabitants of a zone — their fields are self-relative, their layout obeys the zone rules. A non-zoned Logos type cannot live in a zone (it may hold a machine pointer, which the zone rule forbids).
- **ZTypes are `!Drop` — no destructors.** A zone object has **no guaranteed destructor call**: the zone is freed as a whole (objects are never individually freed), and objects are relocated / serialized / `mmap`'d / `memcpy`'d as bytes, so a destructor could never reliably run. A ZType *might* technically have one, but the system would not invoke it — so destructors are **forbidden** on ZTypes and the type system enforces it (a `!Drop` bound).
- **Writ** is the object-graph optic over zones+ZTypes (containers as graph nodes, dynamic `WAny` values, navigation/serialization). Other optics are possible over the same zones.
- **The root zone** is where ordinary Logos code holds onto zones (via holders) and reads them (via views borrowed from the holder).

## Modeling decisions (settled)

Confirmed with the architect (this page supersedes the contradictory noise in the older zoned/Writ texts):

- **Offset encoding** — `i64` self-relative, fixed width on every target (§1, §5); never `u32`/base-relative.
- **Architecture-independent** *layout* — stable `repr(C)`-style layout, no pointer-width fields; mmap / persist / share across processes and architectures. **Caveat:** independence is over layout only; scalar base-type representation (endianness, IEEE-754, integer encoding) must match — one fixed global wire convention (part of the [Logos Compute Model](../lcm/README.md)), simple to control.
- **Root zone** = heap + stack; the *inverse* of a regular zone (non-relocatable, absolute pointers, may point into child zones). Absolute pointers live only here; self-relative offsets only inside zones.
- **Inter-zone refs** go through **object identifiers** (zone identity + relative "base + offset"), never direct addresses — the absolute address is reconstructed only at lookup time. The concrete identifier representation is implementation-defined (e.g. the C++ compiler's non-standard `ExternalRef`), not part of the model.
- **Zone ≠ arena** — arena is an allocation mechanism inside a zone, not the zone.
- **ZTypes are `!Drop`** — no destructors (no guaranteed invocation); type-system enforced.
- **Nesting** — zones may nest hierarchically; all invariants hold at each level. The carving allocator (`PackedAllocator`) is a separate document (possibly in the new Memoria).
