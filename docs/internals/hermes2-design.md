# Hermes2 — Design

The **target architecture** for Logos's Hermes subsystem. Hermes2
replaces relocation-based zoned storage (move-on-grow + base-relative
offsets + per-handle residency) with a model where **nothing ever moves
in place**, references are **self-relative**, and the only run-time
guarantee left to provide is **liveness of immutable data** — supplied
by a coarse, holder-level refcount (a copying-GC residency root).

Status: **design accepted (2026-06-03), implementation not started.**
Implemented **in parts, in parallel with the current Hermes**
(`stdlib/lang/hermes/*`); the current variant retires **wholesale** once
Hermes2 reaches parity. Companion to and partial successor of
[zoned-types-design.md](zoned-types-design.md): the borrow-foundation
thesis (§3 there), the smart-pointer `(B)` fix (§11b there, already
landed), and the type roster (§8 there) carry over; the relocation
model (§2), the reference forms (§4.1), and the residency story (§5/§6)
are **superseded by this document**.

> **One-line thesis.** Make objects *never move*, and the hard problem
> ("memory safety under relocation": stale `self`, base-threading,
> re-acquisition of deep references) **stops existing** — it is replaced
> by the easy problem ("keep immutable data alive while referenced"),
> which a single holder-level refcount solves. Compaction is the only
> thing that frees space, and it does so by **copying to a fresh
> container, leaving the old one untouched** — a copying garbage
> collector.

---

## 1. The reduction (why this is simpler, not just different)

The current model fights *relocation*: a buffer `grow` reallocates,
moving the base and (in the packed case) shifting every block after the
resized one. Consequences cascade — references dangle, `self` of a
mutating method dangles mid-method, deep references must be re-acquired
through the container, and a per-handle residency mechanism is needed to
know what is still alive. Each of those is a real cost paid on the hot
path.

Hermes2 removes the *cause*:

| Current (relocation) | Hermes2 (never-move) |
|---|---|
| `grow` = `realloc` → base moves, blocks shift | `grow` = **append a new segment**; existing data stays put |
| references = base-relative `u32`; need the base | references = **self-relative `i64`**; the anchor is the reference's own address (always at hand) |
| `self` of an allocating method is invalidated by its own `grow` | `self` **never moves** → no re-resolution, ever |
| deep reference survives a resize only via re-acquire (O(depth)) | nothing is invalidated by growth → references stay valid for the data's lifetime |
| compaction defragments **in place** (shift) | compaction **copies to a fresh container**; the old one is immutable and untouched |
| residency: needs to track validity under movement | residency: only **liveness of immutable data** → one holder-level refcount |

What remains to guarantee shrinks to a single concern — **liveness** —
and the borrow checker is freed from policing use-after-relocation
entirely (there is no relocation).

---

## 2. References: self-relative `i64` (compiler-derived, not an explicit type)

A zoned reference stores the **signed byte distance from its own storage
address to the target**, not an offset from a zone base. This is **not a
user-written `RelPtr<T>`** (that explicit type was removed 2026-06-04) —
it is the **compiler's storage representation** for an ordinary pointer
field inside a `#[zoned2]` struct, derived from context and converted
transparently. The full model (typed `zoned T`, tagged/erased refs,
`HAny`, niches) and the codegen plumbing live in
[ref-repr-design.md](ref-repr-design.md) §6; this section states the
storage mechanics it rests on.

```
storage:  i64 delta                 // 8 bytes in the zoned slot
read:     *T = (&field) + delta     // materialize: relative → absolute
write:    delta = target − (&field) // lower: absolute → relative
```

Consequences:

- **No base is threaded anywhere.** The anchor is `&field`, which is
  inherently available at the point of access (you are reading the field
  out of its storage). The `base` half of every fat pointer in the old
  model **disappears** — for reads *and* writes.
- **Resolution is in-place.** A `RelPtr` is meaningful only at its
  storage location; it cannot be carried "bare" in a register and
  resolved later. The compiler resolves it at the field-access site,
  producing an ordinary absolute `*T` — which, because nothing moves, is
  then valid for the lifetime of the target's container.
- **Niche, not just a null sentinel.** Zoned objects are ≥2-aligned, so a
  pointer delta's low bit is always 0; a *reference* (non-null) adds the
  null niche. These invalid bit-patterns feed enum niche-packing
  (`Option<zoned T>`, and `HAny`'s `Ref|Pod` discriminant) — see
  ref-repr §6-7.
- **Position-independent / serializable — for a *rigid block*.** Self-relative
  deltas survive relocation only if every internal address *difference* is
  preserved, i.e. the block moves rigidly. A **single segment** (one `malloc`)
  is such a block → relocatable/serializable (memcpy the segment; all internal
  deltas shifted by the same amount stay valid). A **live multi-segment**
  container is **not** relocatable as a set (segments sit at arbitrary malloc
  addresses; inter-segment deltas break if their relative positions change) — it
  is only valid *in place* (segments are append-only and never move, §3).
  Serializing / relocating a multi-segment container therefore first **compacts**
  it (§4) into a single growing segment, which is then a rigid relocatable blob.
- **A rel_ptr's value form is absolute (it moves like `String`).** The i64 delta
  is only the at-rest encoding in zoned storage; a rel_ptr *value* (local, arg,
  return, register) is the resolved absolute pointer and moves by plain memcpy —
  see [hermes2-minimal-container-plan.md](hermes2-minimal-container-plan.md)
  Phase 0.5. This refines the "cannot be carried bare" stance above: it is
  carried bare *as the absolute compute form*, re-lowered only on store into
  zoned storage.

### Reference forms by role

| Form | What it is | Carries | Used for |
|---|---|---|---|
| zoned pointer field | in-zone stored reference, `i64` self-relative (compiler-derived, ref-repr §6) | nothing (the delta is the whole thing) | a zone object referencing another (field, array element) |
| `HAny` | `enum { Ref(*zoned) \| Pod }` (≈ AnyVal), niche-packed | one 8-byte word | a heterogeneous slot / client-facing value |
| **read view** | resolved absolute `*const T` (thin) | nothing — it's a plain pointer | reading inside a scope where the holder is known live |
| **mut receiver** | the object + an allocator capability | `(self_ptr, allocator)` — **no base** | a method that may `grow` |
| `SuperRc<T>` / holder | residency-retaining owning handle | `Rc<dyn Resident>` + loc | a reference that **escapes** the holder's scope (§4) |

`&self` of a zone type is **thin** (a resolved pointer). `&mut self` of a
*growing* method additionally carries the allocator; a `&mut self` that
only writes a fixed-size field in place (set-in-place, no growth) needs
**no** allocator — same bits as `&self`, different (exclusive) borrow.

### Why `&hermes_obj` cannot become `&mut hermes_obj`

Two independent walls agree (a good sign — the unsound op is
*unrepresentable*, not merely *disallowed*):

1. **Rust's rule.** There is no safe `&T → &mut T` coercion; only the
   reverse downgrade. Upgrading would violate exclusivity.
2. **Representation.** `&` carries no `alloc_fn`, so a growing `&mut`
   cannot even be assembled from it. Stronger still: **many `&hermes_obj`
   have no allocator at all** — views over rodata, over a sealed buffer,
   over someone else's segment set. There is physically nothing to grow
   into.

The legal directions: `&mut self → &self` (drop the allocator half), and
"obtain `&mut` **from the owning root**" (`root.get_mut(path)`, which
threads the allocator in). The allocator enters the system **only
through the owner**, never conjured at a borrow site — the §7
"mutation is rooted" rule of [zoned-types-design.md](zoned-types-design.md).

---

## 3. Segment allocator: never-move, free-en-masse

A Hermes2 container owns a **list of segments** (independently
`malloc`'d chunks). Allocation bumps within the current segment; when it
is full, a **new segment is appended** — existing segments are **never
moved or reallocated**. The container frees all its segments **en masse**
on drop. (This is how the Memoria Hermes already works.)

- **Growth is O(1)** (append) with no copy and no invalidation — strictly
  cheaper than `realloc`-growth, which pays amortized O(size) copies and
  invalidates every pointer on each capacity overflow.
- **No per-object reclamation.** Space is reclaimed only by dropping the
  whole container or by compaction (§4). This fits the
  build → use → (seal) → drop document/transaction lifecycle.

### The one invariant, and why it is free

Self-relative `i64` requires every pair of segments in one container to
be within signed reach (±2⁶³). On any real 64-bit target this holds with
enormous margin — user virtual addresses span ≤ 2⁴⁷ (4-level paging) or
≤ ~2⁵⁶ (LA57), so two segments *anywhere* differ by far less than 2⁶³
(margin ≥ 2⁷, typically 2¹⁶). The allocator carries a cheap
`debug_assert` in `add_segment`; it cannot fire on current hardware
(only on a malformed layout or a hypothetical >57-bit space).

**Decision (recorded): the offset is `i64`, not narrowed.** A 4-byte
(`i32`, ±2 GiB) variant was considered — it would make the invariant
hold *by construction* via a per-container reserved virtual region and
halve in-zone pointer width. **Rejected:** the 4-byte saving is not worth
a reserved-region allocator, a 2 GiB per-container cap, and the extra
machinery. `i64` keeps the segment allocator fully unconstrained (any
`malloc`) and the invariant free.

---

## 4. Compaction = copy, liveness = one refcount

### Compaction is a copying GC

Compaction does **not** defragment in place. It **builds a fresh
container** holding only the live data reachable from the root,
rewriting self-relative deltas as it copies. The **old container is
never touched** — it stays immutable and valid until its last reference
drops. There is therefore **no in-place shift anywhere in the model**;
the only relocation is "copy to a new container," and copies never
invalidate references into the *old* one.

Cost is exactly a copying collector's, and **not new** — the
single-segment variant pays the same to compact:

- **O(live)** copy + a **trace** of live objects from the root (to know
  what to copy and to rewrite deltas) + a **transient 2× peak** (old and
  new coexist during the copy).
- Crucially, growth and compaction are **decoupled**: the O(live) copy is
  paid **when compaction is chosen** (seal / defrag), not forced on every
  capacity overflow as `realloc` does. The GC pause is under the
  program's control.

### Liveness via `Rc<dyn Resident>` (the only run-time bookkeeping)

Because old versions are immutable and never move, the *only* thing that
must be tracked at run time is **whether a version (segment set) is still
referenced**. That is a **coarse, holder-level refcount** — one count per
container/version, **not per object**:

- **`Resident`** — a segment set (a version). Erased behind
  `dyn Resident`; its `drop` frees the segments.
- **holder = `Rc<dyn Resident>` (a.k.a. `SuperRc<T>`)** — the residency
  root. While a holder lives, its version's data is alive and its thin
  views are valid.
- **view** — a thin resolved pointer, valid while its **holder** lives.
- **escape rule.** A reference that escapes the holder's scope must carry
  the holder (`SuperRc`, +1 refcount). A reference that does not escape
  is a bare thin pointer, with the borrow checker proving the holder
  outlives it (**RC-elision** — the retain/release are dead code).

This is the `let (holder, view) = ctr.get(0)` model: the holder is the
GC root that keeps the (immutable) version alive; the view borrows from
it; the borrow checker ties `view`'s lifetime to `holder`'s.

The counter is **non-atomic (`Rc`, not `Arc`) for now** — Hermes2 has no
multi-threaded data sharing; the atomic/`Arc` story is a separate effort
*after* this lands. (Per the standing memory-management direction.)

---

## 5. The borrow checker's (reduced) role

With nothing moving in place and liveness handled by the holder
refcount, the borrow checker no longer polices use-after-relocation. It
provides exactly two things:

1. **Mutation exclusivity.** A `&mut` (set-in-place or grow) excludes
   other references for its duration — ordinary Rust exclusivity, lifted
   to the container. Growth that *appends* does **not** need to kill read
   references (nothing they point at moves) — a property *more*
   permissive than `Vec` (`push` need not invalidate `&elem`), and sound
   precisely because of never-move.
2. **`view` ↔ `holder` lifetime coupling.** A view may not outlive the
   holder it borrows from (the E0716-style check; the temporary-borrow
   fix landed for the surface form `let v = make().view()`).

It does **not** enforce liveness (the refcount does) and does **not**
guard against relocation (there is none).

---

## 6. Nested coupling (Memoria `PackedAllocator`) — forward note

The standalone container above is the first and primary target. The
nested case — a Hermes2 container embedded in another relocatable host,
or Memoria's `PackedAllocator` carving sub-zones inside one block — is a
**later phase** and must preserve the same invariant: **no in-place
shift**. Two facts make this tractable and keep self-relative pointers
cheap:

- Within a packed block, sub-objects are addressed by **index through a
  layout dictionary**, *not* by stored cross-pointers (Memoria's design).
  So there are few stored `RelPtr`s to fix up, and the dictionary is the
  allocator's own concern.
- A self-relative pointer's fix-up under an in-place shift is *worse*
  than a base-relative one (a moved zone's **outbound** refs also break,
  since the ref's own address moved — not just inbound). This asymmetry
  is the reason the nested case, when built, should also prefer
  **copy-compaction over in-place shift**, matching the standalone model
  rather than diverging from it.

The general nesting indirection (when the container's own control block
can move because *its* host relocates) is the `ptr_to_ptr_to_base`
shape; the standalone root never needs it.

---

## 7. Divergences from Rust to bless (register when implemented)

To add to [DIVERGENCES.md](../DIVERGENCES.md) once landed (do **not**
register an unimplemented divergence as live):

- **Thin, self-describing, self-relative zoned references.** A
  `RelPtr<T>` is a single `i64` self-relative offset — a position-
  independent reference whose ABI is a signed displacement from its own
  address, not a machine pointer. Combined with in-band object headers
  (`size`, `type_code`), zone references are thin and the blob is fully
  self-contained (relocatable + serializable). Stronger than stable
  Rust's only thin custom DST (`extern type`, opaque-sized).
- **Never-move, copy-compacted container.** Growth appends; the only
  space reclamation that moves data is a wholesale copy to a fresh
  container (copying GC), leaving the old version immutable. References
  are never invalidated by growth.

---

## 8. What this replaces (retired wholesale at the end)

Hermes2 supersedes the current scaffold in `stdlib/lang/hermes/*` and its
runtime support. During implementation the two coexist; at parity the
current variant is removed in one batch.

| Current | Hermes2 |
|---|---|
| `MemHolder` (base ptr + capacity + RC-chain + `destroyer` callback) | the container = segment list + `Rc<dyn Resident>` holder |
| `RelPtr<T>` = base-relative `u32` (needs external base) | `RelPtr<T>` = self-relative `i64` (no base) |
| `DataRef<T>` / `DataOwn<T>` = `(holder, offset)` + per-object `rc` | thin view (no base) + `SuperRc<T>` for escapes; **per-object RC deleted** |
| `realloc`-grow that moves the buffer and invalidates pointers | append-segment growth (never moves) + copy-compaction |
| base threaded through every method | no base anywhere; allocator only on growing `&mut self` |

The smart-pointer trait-object representation (`Rc/Arc<dyn>` as structs
over custom-DST inner — the `(B)` fix) is **already landed** and is
independent of Hermes2.

---

## 9. Implementation plan (in parts, parallel)

Each part is self-contained, gated on the full suite (L4), and lands
*alongside* the current Hermes (new modules, e.g.
`stdlib/lang/hermes2/*`), so nothing breaks until the final cutover.

1. **`RelPtr<T>` self-relative `i64`** — the reference type + compiler
   resolution at field-access (`&field + delta`), null sentinel
   (`delta==0`), intrinsic-backed ops. Pure leaf; no allocator yet.
2. **Never-move segment container** — segment list, bump-within-segment,
   append-on-full, free-en-masse, the `add_segment` reach assert. A
   standalone `Hermes2` root owning segments.
3. **Object placement + thin read views** — allocate typed objects into
   segments, in-band headers, thin resolved `&self` reads; field/array
   access through `RelPtr`. No mutation-with-growth yet.
4. **Growing `&mut self` + allocator capability** — `&mut self` carrying
   `(self_ptr, allocator)`; append-growth methods; the set-in-place vs
   grow capability split; `& → &mut` impossible by construction.
5. **`Rc<dyn Resident>` residency + holder/view** — non-atomic holder,
   `let (holder, view)` split, escape ⇒ carry `SuperRc`, RC-elision when
   not escaping; the `view ↔ holder` borrow-check coupling.
6. **Copy-compaction (copying GC)** — trace from root, copy live to a
   fresh container, rewrite deltas; old `Resident` freed by RC when its
   last holder drops; decoupled from growth.
7. **Parity + cutover** — port the Hermes consumers to Hermes2, then
   remove `stdlib/lang/hermes/*` and its runtime support in one batch.

Later / separate: nested `PackedAllocator` coupling (§6); the
atomic/`Arc` (multi-threaded) residency variant.
