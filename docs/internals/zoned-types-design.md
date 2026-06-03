# Zoned Types — Design

Integrating Hermes/Memoria *zones* into the Logos type system so that
work with relocatable, offset-addressed memory is **memory-safe by
construction** and the compiler can optimize it. The forcing function
is **safe work with Memoria's `PackedAllocator`** (big Memoria); the
two compiler optimizations (base-caching, refcount elision) are
derived side-effects of the same borrow invariant, not a separate
pass.

Status: **design accepted (2026-06-02), implementation not started.**
Companion to [hermes-runtime.md](hermes-runtime.md),
[big-memoria-architecture.md](big-memoria-architecture.md), the
language-level [Hermes](../language/hermes.md) page, and the deferred
custom-DST work (memory `project_box_unsized_customdst`).

This document is the canonical statement of the model. It supersedes
the ad-hoc lifetime machinery in `stdlib/lang/hermes/{own,zone,mem_holder}`
(those are scaffold — see §9).

> **Superseded in part by [Hermes2](hermes2-design.md) (2026-06-03).**
> Hermes2 is the target architecture, built in parts in parallel with the
> current Hermes (which then retires wholesale). It removes *relocation*
> entirely: objects never move in place (growth appends segments,
> compaction copies to a fresh container), references become
> **self-relative `i64`** (no base threaded anywhere), and the only
> run-time guarantee left is **liveness of immutable data** via a coarse
> holder-level `Rc<dyn Resident>`. This **replaces** §2 (relocation
> model), §4.1 (reference forms — `RelPtr` flips base-relative → self-
> relative), and reframes §5/§6 (residency = snapshot liveness, not
> anti-relocation insurance). The borrow-foundation thesis (§3), the
> `(B)` smart-pointer fix (§11b, landed), and the type roster (§8) carry
> over unchanged.

---

## 1. Three levels: Zone / PackedAllocator / Buffer

Three distinct things that must not be conflated (each correction below
was a real mis-step during design):

1. **Zone** — a logical *relocatable region addressed by offset-from-a-base*.
   This is the type-system primitive everything else is phrased over.
   References inside a zone are **not pointers, not even fat pointers** —
   they are plain `u32` offsets relative to the zone base.

2. **PackedAllocator** — a **universal data type** that *recursively
   carves individual zones* inside a backing buffer. It is **not itself
   a zone** and **not the relocation domain**; it is the carving
   mechanism. It can live inside a zone, be a field of an object, and
   nest (a `PackedAllocator` is itself a `PackedAllocatable`, so
   allocators embed in allocators — e.g. Memoria's `SearchableSequence`
   is a `PackedAllocator` with `METADATA/INDEX/SYMBOLS` blocks where
   `INDEX` is itself a `PackedAllocator`).

3. **Buffer** (e.g. a BTree node's heap allocation) — the **relocation /
   coupling domain**. A tree of zones carved by nested allocators shares
   one buffer; a resize anywhere ripples and shifts everything after it,
   so a resize invalidates pointers into **every** zone of that buffer.

> **Domain ≡ buffer ≡ one heap allocation.** Not the allocator, not a
> single zone. This identity is what makes the borrow model below sound.

---

## 2. Relocation: the single event and its consequences

There is exactly **one** relocation event: a buffer *grow* (`realloc`)
writes the new base and, for the Memoria packed case, **shifts the
offsets of all blocks after the resized one**. Memoria states the
invariant directly:

> *"any resize operation invalidates pointers to blocks going after the
> resized one."* — `memoria/core/packed/.../packed_allocator.hpp`

Consequences:

- **Every access resolves `base + offset` against the *current* base.**
  Never cache `base + offset` as a raw pointer across a possible resize.
- A zone object can move for **two** reasons: (a) the buffer realloced
  (base moved), (b) an earlier sibling grew (its offset-within-buffer
  moved). Both are covered by resolving through the owning descriptor,
  not a stored absolute pointer.

### Base resolution and the optimizer (derived, not a pass)

Lower a zone access as a real load — `obj = load(holder.base) + offset` —
and **do not over-promise invariance**. Then:

- In a region where the borrow checker proves no resize can occur (a
  shared borrow is live, see §3), LLVM's alias analysis hoists the
  `load holder.base` into a register via LICM/CSE **for free** — that is
  the base-cache.
- A resize is a `store holder.base`; if a relocating call is inlined,
  memory-SSA sees the store and refuses to hoist the reload past it →
  **correct by construction under inlining.**

**Inlining is the argument *for* load-based lowering**, against
scope-based register caching. "Base is stable within a function" is
*unsound* — inlining a grow drags the relocation point into the
function. Validity must be tied to **relocation points (stores to
base)**, not lexical function scope.

The strongest lever is **sealing**: a sealed (immutable) buffer never
grows ⟹ base is permanently invariant ⟹ `llvm.invariant` /
`readonly` / `noalias` may be emitted, but **only scoped to the sealed
region**, never globally.

---

## 3. The domain model (borrow foundation)

The whole design is **derived from the existing borrow checker**, not a
parallel "zone optimizer" (cf. memory `feedback_derive_from_foundation`).

**A zoned reference borrows the *buffer* (domain)** — not the zone, not
the allocator. This is exactly **`Vec::push` invalidating `&vec[i]`**,
lifted to the packed tree: `&elem` borrows the whole `Vec`; `push`
needs `&mut Vec`; exclusivity forbids growth while any element borrow
lives. Substitute "buffer" for "Vec".

The hazard Victor flagged — *"modification along one logical path
(grow Z1) invalidates reads along another (read Z2)"* — is precisely
this, and the borrow checker solves it **iff co-located zones share a
type-level domain**. If Z1 and Z2 are typed as independent (separate
holders, no shared region), the checker sees no conflict and admits the
unsound combination. So:

- Within a domain: `(holder, offset)` borrows that the checker couples.
- Across domains: **opaque application IDs** (`BlockID`), resolved via
  the store — *no* borrow relation. Cross-buffer references are never
  pointers (matches the Memoria "cross-container references are
  application-level identifiers" rule).

> **Domain boundary = offset-validity boundary = base-cache-validity
> boundary = borrow-exclusivity boundary.** One principle closes the
> optimizer, the safety story, and the multi-container (big-Memoria) case.

Two optimizations fall out of two analyses the language already needs:

| Optimization | Derived from |
|---|---|
| **base-cache in register** | borrow **exclusivity** — a live shared borrow ⟹ no `&mut`/grow ⟹ base invariant ⟹ cacheable (+ LLVM AA, §2) |
| **refcount elision** | **escape / liveness** analysis — a handle that doesn't escape ⟹ retain/release are dead |

---

## 4. Zone-DST objects: thin storage + fat materialization

Some zone types are **DSTs**. `HermesString` = a varint-encoded length
prefix followed by the bytes. It is **self-describing** (its length is
in-band).

This resolves the thin-vs-fat question: it is **not** thin *xor* fat.

- **In-zone storage is thin** (a `u32` offset) — the object is
  self-describing, so the offset is all you need to store.
- **Processing/outward form is fat** — materialized on demand by reading
  the in-band length.

The contrast with Rust DSTs is deliberate (a **blessed divergence**, §11):
Rust carries DST metadata *out-of-band in the pointer* (fat `&[T]`,
`&str`, vtable for `dyn`) because Rust DSTs are *non-intrusive headerless
views* — length is a property of the *view*, enabling sub-slices and
coercing any `Sized` to `dyn` without touching the data; and because
`size_of_val` is needed for **deallocation, moving, and "where the
object ends"**. Memoria objects are the inverse: **intrusive,
self-describing**, so the bytes carry their own size/type (`size`,
`type_code`) → the *source of truth is in-band*, references can be thin,
and a handle *may* duplicate the size "at hand" purely as a cache. We
lose headerless sub-slices into the middle (every object has a header) —
which Memoria does not need (sub-objects are addressed by index through
a layout dictionary, not by a bare slice).

### Three reference forms — chosen by context

| Form | What | Used for | Survives relocation? |
|---|---|---|---|
| `RelPtr<T>` (u32 offset) | thin **in-zone storage** | a zone object referencing another (field, `Array` element) | yes — offset is base-relative |
| `StringView` (base-provider + offset, re-resolves each read) | **safe boundary crossing** | passing out, holding across a possible resize | yes — re-reads base |
| `&str` (raw `ptr+len` fat) | **processing / slicing** | inside a no-resize window; `&s[1..3]` | no — dangles on realloc |

`RelPtr<T>` is to become a **distinct type, coercible to `u32`, with all
operations going through compiler intrinsics** (detail TBD — separate
discussion). Today it is a plain `{ offset: u32 }` with an
externally-supplied base; the safe version associates it with a domain.

### Boundary coercion (the `Array`-insert mechanic)

Storing a reference to a `HermesString` into an in-zone `Array` (which
holds `RelPtr<HermesString>`) requires lowering a fat reference to an
offset:

1. **Same-domain required.** An offset is meaningful only against *this*
   buffer's base. The `RelPtr` carries a domain; an `Array` of domain
   `'buf` accepts only a reference of domain `'buf`. Cross-domain →
   `BlockID`, not `RelPtr`.
2. **Two distinct operations:**
   - **link** — referent already in this buffer: `offset = ptr − base`,
     store the `u32`. Cheap.
   - **intern / copy** — referent elsewhere (heap / other buffer):
     allocate into this buffer, copy the bytes, then store the offset.
   Conflating them is a bug: `push(&s)` of a foreign `s` *must* copy, or
   the stored offset is garbage.
3. **Sub-slice asymmetry.** A whole self-describing `HermesString` stores
   as **one** offset (it knows its length). A bare `&str` sub-slice is
   **not** self-describing in-zone → not storable as a single offset
   (needs a fresh `HermesString` or an explicit `(offset, len)` pair).
   Slicing *outward* is free; storing a slice back *as a reference* is
   not.

---

## 5. Ownership model

**Decision: mutable Hermes containers are always independent of one
another.** Even when a Hermes zone is hosted inside a `PackedAllocator`,
that zone is **immutable — it cannot be resized in place** (technically
the abstraction permits it; we forbid it by design). Therefore:

- A **mutable Hermes container is the root owner of its own dynamic
  zone — like `Vec<T>` / `Box<T>`**: it owns a growable buffer, is
  affine (move-only), grows by reallocating *its own* memory, and frees
  on `Drop`. It never ripples into a `PackedAllocator`.
- A **Hermes hosted in a `PackedAllocator` is immutable / sealed**:
  read-only, no resize. `seal` is the transition (mutable → immutable);
  embedding copies the sealed bytes into a block. Mutating an embedded
  container = copy out, rebuild a fresh mutable Hermes, re-seal, re-embed
  — **copy-on-write at container granularity** (standard persistent
  pattern).

### Owning handle vs view — the `Vec`/`&[T]` split

The exact Rust analogy, and a hard constraint:

> `Hermes` : `HermesView<'a>` : (the sealed zone bytes)
> ::
> `Vec<T>`/`String` : `&[T]`/`&str` : `[T]`/`str`

- **`Hermes` (the owning handle) cannot be stored in a `BTreeNode` /
  embedded in a zone.** It holds a *heap base pointer* (`base`,
  `capacity`), and the zone rule forbids raw pointers — only offsets. So
  only its **sealed bytes** embed in a block; the embedded data is read
  through a `HermesView`. Exactly as a `Vec<T>` can't be serialized into
  a block but `[T]` can, viewed as `&[T]`.
- **Heap independence → only-OOM failure.** Heap allocations are
  independent: an update through one `Hermes` never moves another heap
  object. The only "ripple"-like failure is `realloc` failing → **OOM
  panic**. This is why the standalone heap case is the *simple, safe*
  one — no cross-object coupling, unlike a `PackedAllocator` buffer where
  growth shifts siblings (the coupled domain, §3). And since embedded
  Hermes is sealed/immutable, a *growing* Hermes never exists inside a
  coupled buffer — consistent.

### Implementation reality of `HermesView` (gap to close)

Today `HermesView<'a> = { base: *const u8, size: u64 }` + a `HermesRead`
trait (`base()`/`size()` + default reads). Read-only is **convention
only** — a `*const` pointer and the absence of write methods; the `'a`
is **decorative** (base is a raw pointer, not borrow-checked against the
source). The work: make read-only-ness and `'a` **actually enforced** —
no `HermesWrite` impl on the view, and `HermesView<'a>` genuinely borrows
its source for `'a` (lifetime tied to the owner / page guard, §6), not a
phantom.

Consequences:

- **The common path is ownership + borrows, zero refcount** — exactly
  `Vec`. A `HermesString` inside a Hermes lives as long as the container;
  access is a borrow `&'a` / `HermesView` tied to the container. `&mut
  Hermes` to grow invalidates live `&Hermes` (Vec::push invariant).
- **Per-object refcounts are deleted.** Zone objects are never freed
  individually — the **buffer** is the unit of free/compaction — so
  "how many handles point at this object" is irrelevant. Only "is the
  container alive" matters.
- The only remaining refcount is the **escape** case (§6).

---

## 6. Resource lifetime & type erasure

### Requirement (must be preserved)

A live view into data physically resident in a cached block must keep
that block **resident** (the cache will not evict a pinned page) for the
view's lifetime. Current Hermes does this by chaining a per-object
`Own`/`mem_holder` refcount up to the BTree block.

### A borrow does not pin at runtime — the owner does

`&'a T` is a *static* guarantee; it pins nothing at runtime. Something
must physically hold the backing storage resident for `'a` — the
**owner of the borrow**. For a standalone heap Hermes that owner is the
`Hermes`/`Box` on the stack; for a cached block it is a **page guard**
(RAII) that holds a runtime pin. The refcount did not vanish — it
**moved up** from per-object to the **eviction unit (the page)**,
**consolidated** (one pin per page instead of N), and became **RAII**.
The borrow checker makes it sound: a view cannot outlive its guard, so
*a live view into an unpinned block is unrepresentable*.

### Type erasure — the client never names a page guard

Client code knows nothing about pages, blocks, or a concrete page-guard
type (these differ per backend — cache, mmap, in-mem). The erasure point
is a trait, generalizing today's `mem_holder.destroyer` callback into a
proper trait object:

```
trait Resident { /* keeps backing bytes resident; release on Drop via vtable */ }
```

- Backends implement it (cache-page pin, mmap region, in-mem buffer
  owner, standalone malloc-Hermes). All differences live in the vtable.
- `Arc<dyn Resident>` (or `Rc<dyn Resident>` thread-local) is the erased
  shared pin — essentially today's heap `mem_holder` with `rc +
  vtable-release` replacing `rc + fn-callback`.

### `SuperRc<T>` — the unified owning handle (working name)

```
SuperRc<T> = { pin: Arc<dyn Resident>,  loc: <locator of T within the Resident's bytes> }
```

- Owning, clonable, **type-erased** escape/retain handle. `T` is the
  projected view type (`Hermes`, `HermesString`, …).
- **Deref/borrow** → a view of `T` resolved against `pin.base() + loc`.
  In scope, take `&*rc` without cloning.
- **Clone** → bump the pin refcount (shares the same page). Atomic
  (`Arc`) iff the backing store is shared across threads; non-atomic
  (`Rc`) thread-local — atomicity follows the `Resident`, not `T`.
- **Drop** → decrement; at zero the concrete `Resident::drop` fires →
  unpins the page and **cascades down the resource chain** (parent
  resource, mmap region, file handle…). The transitive "holds everything
  underneath" lives in the `Resident` impls' `Drop`, not in `SuperRc`.
- **Projection** → `rc.get(k) -> SuperRc<HermesString>`: clones the same
  `pin`, new `loc`. Navigating into a Hermes shares the *one* page pin
  (the `Rc::map` / `owning_ref` pattern).

### Client-facing roster

| Client sees | Hidden inside |
|---|---|
| `Container` (trait / backend parameter) | the concrete store: cache, mmap, in-mem |
| `container.iter()` | threads page guards as it advances |
| `SuperRc<T>` or a borrow | `Arc<dyn Resident>` = the pin |

Semantics preserved verbatim: *holding a value keeps its block resident*
(holding a `SuperRc<T>` keeps its `Arc<dyn Resident>` alive). Now **one**
refcount layer (on the `Resident`), erased.

### Zero-cost streaming via RC-elision

The client type is **uniform and erased** (`SuperRc<T>` carries the pin),
but **RC-elision** (the escape-analysis optimization, §3) removes the
inc/dec where a value provably does not escape its iterator scope:

- **retain-while-scanning** (hold values, keep iterating) — `Arc` alive,
  block held — transparent, as today.
- **streaming scan** (process-and-forget) — non-escape proven → pin
  traffic elided, borrow from the iterator.

The client does not choose; the optimizer does. `PageGuard` stays a
purely internal backend mechanism.

---

## 7. PackedAllocator safety (the forcing function)

Big Memoria needs **safe** `PackedAllocator`. The goal is to make its
UB classes *unrepresentable*; the optimizations are a side-effect.

### One root per heap object — enforced by an affine owner

The host (`BTreeBlock`/`BTreeNode`) is an ordinary heap object: header
(`id`, `size`, `type_code`) + a **DST tail** `allocator: PackedAllocator`
(making the host a DST). The host is **not** a zone type; it *hosts* the
PackedAllocator, which recursively carves zones inside it.

**Invariant: ≤ 1 *root* PackedAllocator per heap object.** Root growth =
realloc of the heap buffer (the root has no parent to ripple into). Two
roots in one buffer ⟹ growing one shifts the other ⟹ sibling
invalidation between "independent" domains.

**Enforcement: root-ness is a *capability held by the unique affine
heap-owning handle* (Box-like), not a distinct type.** One heap
allocation ⟺ one owner ⟺ one root — the same theorem as "one `Box` per
allocation." A second root cannot be minted: the only way to make a root
is the owning constructor that allocates a fresh buffer; there is no
"promote interior allocator to root" API. Making `RootAllocator` vs
`NestedAllocator` two *types* would be an anti-pattern — it is one
universal type in two roles, distinguished by *who owns the buffer*.
Needs only affine ownership (have it) + the DST tail (custom-DST, §10).

### Capability split (resize vs set-in-place)

PackedAllocator gives the exact boundary of "may relocate":

- **resize** (`enlarge`/`insert`/`remove`) — ripples to root ⟹
  **root-exclusive transaction**, invalidates the whole domain.
- **set-in-place** of a fixed-size field (`setSymbol`) — does **not**
  move anything ⟹ **disjoint `&mut`**; neighbors' reads survive.

A disjoint `&mut` to a *nested* allocator for resize is **not** allowed:
resizing a leaf is a `&mut` on the root (it ripples up); a method on
`&mut leaf` that internally grabs `&mut root` is whole/part aliasing.
**Mutation is rooted and addressed by index/path** — `root.resize(path,
n)`, never `&mut leaf` — which matches Memoria's existing API shape
(`Base::resize(SYMBOLS, …)`).

### Two-phase transaction, OOM = Result

Memoria's `PackedAllocatorUpdateState{allocated_, available_}` /
`inc_allocated(old, new)` is a pre-flight: **compute the total size
delta → check it fits (resizing the parent up the tree, possibly
OOM) → apply atomically** (`PkdStructUpdate`/`Multistep` = deferred
steps). We bless this as the canonical mutation model:

- The mutating phase is a **transaction** (`&mut` on the owner /
  `UpdateState` token) with a **single invalidation barrier** (apply)
  and a **single OOM check** — not N scattered per-call barriers.
- OOM is a **`Result`**, never an exception/panic — Logos is abort-only
  (no unwinding; memory: panic divergence), so OOM *must* be threaded as
  `Result` and is `must_use`.

### Why per-handle "transparent" pinning is rejected here

It would reintroduce per-handle refcounts (the very thing §5/§6 remove).
Residency is preserved by the `Resident`/`SuperRc` mechanism + RC-elision
instead.

---

## 8. Type roster & replacements

### New / canonical types

| Type | Role | Refcount |
|---|---|---|
| `Hermes` | owned mutable **root** container (Vec/Box-like), own growable buffer; holds a heap base ptr → **not embeddable in a zone/`BTreeNode`** (only its sealed bytes embed); growth never ripples to other heap objects (only failure = OOM panic) | none (affine) |
| `HermesView<'a>` | borrowed **read-only** view into the sealed bytes wherever they live (own buffer / `PackedAllocator` block / rodata); the only way to read embedded Hermes; `≈ &[T]/&str`. Read-only and `'a` must be *enforced* (no `HermesWrite`; real borrow), not convention | none |
| `SuperRc<T>` | **type-erased owning** escape/retain handle = `(Arc<dyn Resident>, loc)`; projects, derefs, drop cascades | one layer, on `Resident` |
| `dyn Resident` | erased "owner of resident bytes"; backend-specific Drop/release | n/a |
| `RelPtr<T>` | thin in-zone stored reference (u32 offset); domain-associated; intrinsic-backed (TBD) | none |

### Scaffold being replaced (currently in `stdlib/lang/hermes/*`)

| Today | Becomes |
|---|---|
| `DataOwn<T>` (two RC layers: holder + per-object heap `*mut i32`) | `SuperRc<T>` (one layer, on `Resident`) |
| `mem_holder` RC-chain + `destroyer` fn-callback | `Arc<dyn Resident>` (rc + vtable-release) |
| `DataRef<T>` (retains holder on creation) | a borrow `&'a` / `HermesView` projection; retain removed |
| exposed `PageGuard` | hidden; the container iterator materializes `SuperRc<T>` |
| per-object reference counting + future "optimize it away" work | deleted outright |

The current design literally mirrors the C++ Hermes, which was shaped by
a different type system. The borrow-checker-backed model removes the
machinery rather than optimizing it.

---

## 9. UB → closure (safety table)

| Footgun today (programmer discipline) | Closed by |
|---|---|
| **use-after-resize** — hold `T*` from `get(i)`, resize → dangles | handles borrow the **root**; resize = `&mut root` → borrow checker kills the stale handle (Vec/`&vec[i]` lifted to the tree) |
| **index/type confusion** — `get<Metadata>(SYMBOLS)` | typed indexed slots are part of the type; per-slot typed accessors |
| **OOM as exception** (no unwinding here) | `Result<_, PackedOOM>`, `must_use` |
| **aliasing in nested mutation** — `&mut` parent + child | rooted mutation addressed by index/path, never `&mut leaf` |
| **forgot to update dictionary / `allocator_offset_`** on shift | trusted `unsafe` core, encapsulated like `Vec`'s internals |
| **non-trivial type in a block** → breaks relocation/serialization | `T: Datatype` / `#[zoned]` bound; non-POD rejected at the type level |

---

## 10. Divergences from Rust to bless (register when implemented)

To add to [DIVERGENCES.md](../DIVERGENCES.md) once landed (do **not**
register an unimplemented divergence as live):

- **Thin, self-describing zoned DST.** A zoned DST reference is *thin*;
  its size/type lives **in-band** in the object header (`size`,
  `type_code`), not out-of-band in the pointer. Stronger than stable
  Rust (whose only thin custom DST, `extern type`, is opaque-sized). The
  fat form (`&str`/`StringView`) is *materialized on demand* from the
  in-band header. Justification: relocatability + serializability — the
  bytes must be self-contained; an out-of-band length would not travel
  with them.
- **`RelPtr<T>` as an intrinsic-backed offset type** — a reference whose
  ABI is a `u32` offset, not a machine pointer (detail TBD).

---

## 11. Implementation plan

Two stages. Stage 1 makes standalone Hermes fully type-safe and
refcount-free; Stage 2 adds the recursive `PackedAllocator` and its safe
coupling. Each numbered step gates the next with the full test suite
(see memory `feedback_test_levels`).

### Acceptance test (executable gate)

A **minimal type-erased storage model** that exercises the §6 lifetime /
erasure core end-to-end:

- A `Storage` holds blocks; each block backs a zone and is an
  independently free-able/evictable resource. The **block type is erased
  from client code** (`dyn Resident`).
- `storage.get(key)` hands out a `HermesView`-bearing handle that the
  client can **store in an array** (`Vec<Handle>`) and carry around —
  i.e. the handle **escapes** the call.
- **While the client holds the handle for block B, `Storage` cannot free
  B's memory.** Per-block granularity: it *can* free/evict other blocks
  and *can* be mutated concurrently.
- **`&Storage` borrowing is explicitly disallowed** as the mechanism —
  the handle escapes into a long-lived array and pins only its own block,
  so its lifetime cannot be tied to a whole-storage borrow.

This forces the **runtime per-block pin** (`Arc<dyn Resident>` /
`SuperRc`), not a static `&Storage` borrow — it is the escape path, not
the in-scope streaming path. It is essentially a minimal Memoria-cache
(storage = cache, block = page, handle = pinned view).

**Sequencing note:** the minimal harness needs only the §6 mechanism
(`trait Resident` + `Arc<dyn Resident>` + `SuperRc`/handle + a sized
`HermesView` over a stub block) — **no custom-DST**. So it is the ideal
*first executable spike*: it de-risks and pins down the
lifetime/erasure design (Steps 3–4) before the heavy custom-DST work
(Steps 1–2), and then stands as the Stage-1 acceptance gate once real
Hermes data flows through it.

### Stage 1 — type-safe, refcount-free standalone Hermes

**Step 1 — Preliminary: custom DST.** Bring up the custom-DST mechanism
that everything else needs (memory `project_box_unsized_customdst`,
register B2/B3). Minimum: a struct with an unsized last field; a *thin,
self-describing* reference to it (size read from the in-band header, §4);
in-place construction into a buffer (no stack value of an unsized type);
size loss through `.hm0` load fixed (the known front-gate). This is the
hardest prerequisite and the right first cut because §4/§7 both rest on
it. *Risk: deepest compiler work (mono / layout / loader); do it behind a
narrow feature before touching stdlib.*

**Step 2 — Custom DST in current Hermes objects.** Re-express
`HermesString` (and siblings: `ObjectArray`, decimal limb tail, …) as
custom DSTs with the three reference forms (§4) and the boundary
coercion (`RelPtr` ↔ view, same-domain checked, link vs intern,
sub-slice asymmetry). Introduce `RelPtr<T>` as the distinct
domain-associated, intrinsic-backed type. *This is the most tangible
slice and where the domain-tag / same-domain check is first exercised.*

**Step 3 — `Hermes → SuperRc<Hermes>`; delete the refcount zoo.** Land
`trait Resident` + `Arc<dyn Resident>` + `SuperRc<T>` (§6). Remove
`DataOwn`, the per-object `mem_holder` RC, `OView`, and the per-object
counters **together with the need to optimize them later** (§8). The
escape path is now `SuperRc<T>`; the common path is borrows.

**Step 4 — Rework the container/object API onto the borrow model.** All
of `Map`/`Array`/`ObjectMap`/`ctr`/iterators move to ownership + borrows
leaning on the borrow checker: `Hermes` = affine root owner; `&Hermes` /
`&mut Hermes` (grow = `&mut`, Vec::push invariant); iterators yield
borrows or `SuperRc<T>` with RC-elision on the hot path. **Exit
criterion: Hermes is fully type-safe with zero refcounts on the common
path**, RC only at explicit `SuperRc` shares.

### Stage 2 — recursive PackedAllocator + safe Hermes coupling

**Step 5 — Recursive `PackedAllocator`, type-safely coupled to Hermes.**
The host DST (`BTreeBlock`), one-root-per-heap-object via the affine
owner (§7), the capability split (resize transaction vs disjoint
set-in-place), the two-phase `UpdateState` transaction with `Result`
OOM, rooted index/path mutation, and `Resident` impls for cached pages
(so `SuperRc` pins blocks transparently). The UB→closure table (§9) is
the acceptance checklist.

### Cross-cutting (not separate steps)

- **Base-cache** and **RC-elision** are emergent from §3 once borrows
  carry the domain and codegen lowers base as a load with borrow-derived
  `readonly`/`noalias` (§2). Verify with disasm on a hot scan; do not
  build a bespoke pass.
- Register the §10 divergences in `DIVERGENCES.md` as each lands.

---

## 11b. Smart-pointer trait-object representation — the (B) fix

Surfaced by the acceptance spike: `Rc<dyn T>` / `Arc<dyn T>` are normalized
(`sema.cpp` ~4848, the `Box/Rc/Arc<dyn>` resolver) to an **owning
trait-object value `{data = ptr-to-val, vtable}`** (16B). This is
layout-INCOMPATIBLE with the generic `Rc<T>` struct `{inner: *mut
RcInner<T>}` (8B), whose methods read `self.inner.strong`. Consequences:

- Generic inherent methods (`clone_ref`, `strong_count`, `weak_count`,
  `downgrade`, …) can't run on `Rc<dyn>` — they read the fat pair as
  `{inner}` and crash. (Verified: rerouting the receiver type to a
  reconstructed `Rc<dyn>` struct compiles but segfaults.)
- So `.clone()` and drop are handled by **repr-aware specials**
  (`__smartptr_dyn_clone__`, `gen_drop_owning_dyn_handle`) that locate
  the `RcInner` header by backing up from `data` using the vtable's
  `size`/`align` slots (vtable = `[drop, size, align, methods…]`).
- Implicit unsize coercion `Rc<A> → Rc<dyn>` at a type-annotated `let`
  (and fn-arg/return) compiles but does **not** perform the unsize
  (GAP-C) → the value stays an 8B `Rc<A>` in a 16B slot → segfault. The
  explicit `as Rc<dyn>` cast does the unsize; `lower_cast` even carries a
  "CoerceUnsized not yet implemented" reject for the *struct*-typed
  target shape (which doesn't fire because the target normalizes to a
  trait-object).

**Interim (committed): `clone_ref`/`clone_rc` routed through the existing
`__smartptr_dyn_clone__` marker** (same semantics as `.clone()`). A
scaffold ([[feedback_workaround_is_temporary_scaffold]]); (B) replaces it.

### (B) — the fundamental fix

Stop collapsing `Rc<dyn T>` / `Arc<dyn T>` to an owning trait-object.
Keep them as the **struct `Rc<dyn T> = { inner: *mut RcInner<dyn T> }`**,
where `*mut RcInner<dyn T>` is a **custom-DST fat pointer `{RcInner-base,
vtable}`** (RcInner-base → the `RcInner` header, strong at offset 0; val
at `offset(val)`, dispatched via the vtable). Then:

- all generic inherent methods run directly (`self.inner.strong`,
  `self.inner.val`-dispatch) — no per-method markers;
- implicit `Rc<A> → Rc<dyn>` = a custom-DST **unsize of the `inner`
  pointer** (thin `*mut RcInner<A>` → fat `*mut RcInner<dyn>`), at
  cast/let/arg/return sites uniformly;
- `__smartptr_dyn_clone__` and `gen_drop_owning_dyn_handle` become
  **obsolete** (generic `Clone`/`Drop` for `Rc<dyn>` work);
- reuses the custom-DST machinery that already partly works — `RcInner<dyn>`
  as a struct-with-unsized-tail already constructs / derefs / drops.

`Box<dyn>` stays an owning trait-object (it owns the value directly, no
`RcInner`).

**Staging** (each step full-suite gated):
1. **Build the custom-DST foundation** — this is where (B) bottoms out, and
   it is the designated hardest prerequisite (B2/B3, memory
   `project_box_unsized_customdst`). A struct with a fixed header + an
   **unsized trait-object last field** (`struct Inner<T> { strong: i32,
   val: T }` with `T = dyn Tr`), and a fat pointer `*mut Inner<dyn Tr> =
   {base, vtable}` through which you can BOTH read a header field
   (`p.strong`) AND dispatch the tail (`(&p.val as &dyn Tr).v()`).
   **Currently segfaults** — the `as *mut Inner<dyn Tr>` cast does not
   build a proper fat pointer (repro: `sandbox/zoned-spike/bfound_B_foundation.logos`,
   target exit 42). Existing custom-DST support covers Box<[T]> slices and
   the `RcInner<dyn>` *owning-trait-object* `{data=val, vtable}` path, but
   NOT this struct-with-unsized-trait-tail + header-bearing fat pointer.
   This step makes the repro pass; everything below depends on it.

   **Precise root (2026-06-02 investigation):** the foundation probe's three
   cases isolate it — `Inner<A>` sized read OK; cast + header read
   (`id.strong`) OK; cast + **tail dispatch** (`(&id.val as &dyn Tr).v()`)
   **segfaults**, and `sizeof(*mut Inner<dyn Tr>) == 8` (THIN, not fat). Cause:
   `dyn Tr` in a type-arg / DST-tail position resolves to the **sized owning
   `TraitObject`** (16B value), so `Inner<dyn Tr>` is treated as a *sized*
   struct `{strong, val: 16B-owning-TO}` — `is_effective_dst` (sema.cpp:3439,
   which only accepts an `UnsizedSlice`/`UnsizedDyn` substituted tail) returns
   false, the `*mut` is thin, and the `Inner<A> → Inner<dyn>` cast drops the
   8B `A` payload into the 16B owning-TO slot → `&val as &dyn` reads a garbage
   vtable. This is exactly Rust's `Pointee::Metadata`-for-dyn + custom-DST
   unsize coercion (the deferred B3). Four coupled sub-changes:
   - (1) resolve `dyn` in a DST-tail/type-arg position as **unsized**
     (`UnsizedDyn`), not the sized owning `TraitObject`;
   - (2) `*mut`/`&Inner<dyn>` → a **DstRef carrying a vtable** (fat
     `{base, vtable}`), not a slice `len` (sema.cpp:3924 + DstRef metadata —
     reuse the existing `{data, vtable}` TraitObject fat-ptr layout);
   - (3) **unsize coercion** `*mut Inner<A>`(thin) → `*mut Inner<dyn>`(fat),
     attaching the concrete type's vtable (same synthesis as `&A as &dyn`),
     at the `as`-cast AND implicit coercion sites (also closes GAP-C);
   - (4) field projection: header via `base`; `&p.val as &dyn` reuses the
     carried vtable.
2. Change the `sema.cpp` ~4848 resolver: `Rc/Arc<dyn>` → struct
   `Rc/Arc<custom-dst inner>`, NOT `make_trait_object`.
3. Unsize coercion of the `inner` pointer at `as`-cast, then at the
   implicit coercion path (let/arg/return) — closes GAP-C.
4. Verify generic `clone_ref`/`strong_count`/`Clone`/`Drop` run directly;
   remove the `__smartptr_dyn_clone__`/`gen_drop_owning_dyn_handle`
   specials and the interim `clone_ref` marker; payload-trait dispatch
   (`Resident::base`) still works via the inner fat ptr's vtable.
5. Drop the explicit `as` from the acceptance + clone_ref tests.

Risk: touches coercion, layout, drop, clone, dyn dispatch, method
resolution + many existing `Rc<dyn>`/`Arc<dyn>` tests — a focused,
staged effort, not a point patch.

## 12. Open questions / deferred

- **`RelPtr<T>` exact shape** — distinct type, `u32`-coercible,
  intrinsic-backed operations. Separate discussion (Victor).
- **`SuperRc` final name** and whether `Rc`/`Arc` are one type
  parameterized by the `Resident`'s sharing or two types.
- **`UpdateState` surface in Logos** — explicit transaction token vs a
  `&mut`-method that internally batches; lifetime of the token.
- **Layout-order refinement** — a resize only invalidates blocks *after*
  the resized one; exploiting this (vs conservative whole-domain
  invalidation) is possible but fragile and **not** in the foundation.
