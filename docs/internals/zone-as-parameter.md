# Zone as a Parameter — Design

A successor framing to `zoned-types-design.md` (and a refinement of
`ref-repr-design.md` §6). The thesis: **there is no "zoned vs ordinary"
binary. Every object lives in some _zone_; the heap is just the default
zone.** A zone is a generic parameter `Z: Zone`, so one source template
monomorphises into per-zone implementations.

Status: **design (2026-06-05).** Builds on machinery that already exists
(GATs, RefRepr-per-type, mono). The only genuinely new codegen is one
RefRepr descriptor (the relative pointer) — isolated in §5.

---

## 1. Why a parameter, not a flag

Today the compiler carries two parallel memory paths — the ordinary heap
path and the Writ/zoned path — gated by a binary marker (`#[zoned2]`).
A binary breaks the moment there are >2 zone kinds, and the two paths are
exactly the drifting-parallel-code that rots (cf. the four divergent
`size` computations). Victor's reframing removes the binary:

- ordinary objects = `zone(Heap)`
- Writ data = `zone(Writ)`
- (later) `zone(Stack)`, `zone(Arena)`, `zone(Persistent)`, …

`Heap` is not special-cased; it is the **identity instance** of the `Zone`
parameter (absolute pointer, `malloc`/`free`, no relocation). Everything
per-zone (pointer representation, allocation, drop) becomes a
property *of the zone*, looked up uniformly — derive-from-foundation.

### Scope constraints (Victor, 2026-06-05)

- Zones are **memory-independent**: no mixing of zones inside one
  container. A `Vec<T, Z>` lives wholly in `Z`.
- **`Heap` is the gluing zone**: a heap object MAY hold references into
  other zones. The reverse (a Writ object holding a raw heap pointer)
  is forbidden — it would break serialisation/position-independence (an absolute
  heap ptr inside serialisable Writ data). Reference
  legality is therefore **directional** and per-zone-kind.
- **`Stack` is a special zone** with its own (lifetime/escape) rules.
- **Closed set** of zone kinds (`Heap`, `Writ`, `Stack`, …), each with
  compiler support for its borrow rules. NOT open user-defined zones —
  the *representation* is parameterisable, but the *safety rules* are
  per-kind compiler logic, so the kinds are a fixed roster until the
  rules themselves are abstracted (far off).

---

## 2. Zone is the lower layer under `Storage` (NOT a parallel abstraction)

The Writ fabric already has a storage-strategy abstraction with GATs:

```
pub trait Storage { type Elem; fn storage_len(..); fn storage_get(..); }
pub trait DynamicStorage: OwningStorage { fn storage_push(..); ... }
pub trait Datatype  { type View<S: Storage>; }   // GAT, works today
```

Its leaves **hardcode a zone choice**: `PrimVec<T>` = heap-backed,
`ZoneSlice<T>` = Writ-zone-backed, `MmapView<T>` = borrowed. That
hardcoding IS the implicit zone axis. Lifting it to a parameter collapses
the parallel leaves:

```
// before: two near-identical Storage leaves
struct PrimVec<T>   { ... }   impl DynamicStorage for PrimVec<T>   { /* heap   */ }
struct ZoneSlice<T> { ... }   impl OwningStorage  for ZoneSlice<T> { /* writ */ }

// after: ONE zone-generic backing
struct ZVec<T, Z: Zone> { buf: Z::Ptr<T>, len: i64, cap: i64 }
impl<T, Z: Zone> DynamicStorage for ZVec<T, Z> { /* one source */ }
```

So `Zone` is **the primitive `Storage` builds on**, not a sibling: a
`DynamicStorage` uses `Z::Ptr<T>` for its buffer and `Z::alloc`/`Z::free`
for growth. The `Storage`/`Datatype::View<S>` GAT machinery stays; its
leaves stop hand-coding the zone.

---

## 3. The mechanism — `trait Zone` (rides on GATs, which we already have)

GATs are present, tested, and used (`Datatype::View<S: Storage>`; pass
tests `gat_basic`/`gat_self_item_projection`/`assoc_type_projection_dispatch`).
**Empirically verified (2026-06-05):** `struct Buf<T, Z: Zone> { p:
Z::Ptr<T>, len: i64 }` compiles, runs, and the GAT field resolves in
layout position IDENTICALLY to writing the concrete type (`gat == direct`).
So the zone-as-GAT design is unblocked today.

```
pub trait Zone {
    // The zone's pointer / borrow forms (GAT). Heap → raw absolute;
    // Writ → self-relative offset (its own RefRepr descriptor, §5).
    type Ptr<T>;            // owning/raw pointer in this zone
    type Ref<T>;            // shared borrow in this zone (avoids new `&Z T` syntax)
    type RefMut<T>;         // exclusive borrow in this zone

    // NO relocation property. Neither zone moves objects (Victor 2026-06-05):
    // Writ2 = NEVER-move segments; self-relative ptrs serve POSITION-
    // INDEPENDENCE (serialize / mmap / surviving a container-level block move),
    // NOT runtime relocation — that was Writ1's disease. A zone is just
    // {pointer representation, allocation}. Container shifting (Vec /
    // PackedAllocator grow → memcpy) is a separate, zone-agnostic mechanic
    // (heap has it too); self-relative ptrs survive it transparently.
    unsafe fn alloc(ctx: Self::Ctx, bytes: usize) -> Self::Ptr<u8>;
    unsafe fn free(p: Self::Ptr<u8>);
}

struct Heap;
impl Zone for Heap {
    type Ptr<T>    = *mut T;          // existing RefRepr: ThinPtr (8B absolute)
    type Ref<T>    = &T;
    type RefMut<T> = &mut T;
    unsafe fn alloc(ctx: HeapCtx, n: usize) -> *mut u8 { return malloc(n); }
    unsafe fn free(p: *mut u8) { free_(p); }
}

struct Writ;
impl Zone for Writ {
    type Ptr<T>    = HRel<T>;         // self-relative i64 — NEW RefRepr (§5)
    type Ref<T>    = HRel<T>;
    type RefMut<T> = HRelMut<T>;
    unsafe fn alloc(ctx: *mut Arena, n: usize) -> HRel<u8> { /* segment append (never moves) */ }
    unsafe fn free(_p: HRel<u8>) { /* en-masse — no per-object free */ }
}
```

Generalisation-over-zones = ordinary generics over `Z: Zone`. One
`impl<T, Z: Zone> Vec<T, Z> { fn push(..) {..} }` monomorphises per zone:
representation differences come from `Z::Ptr<T>` (RefRepr-per-type),
behavioural differences from the `Zone` methods (`alloc`/`free`).

**Two design knobs that are genuinely Victor's:**
1. Surface borrow: use `Z::Ref<T>` (GAT, no new syntax) vs a first-class
   `&Z T` reference type. Recommendation: `Z::Ref<T>` first — it needs no
   grammar/TypeRef change, and `&T` stays exactly `Heap::Ref<T>`.
2. Whether `Stack` joins now or later — see §6.

---

## 4. What already exists (no new work)

| Piece | Status |
|---|---|
| GAT decl / impl / projection-with-args / mono multi-step | DONE (tested) |
| GAT in a layout-bearing struct field | DONE (probed `gat == direct`) |
| RefRepr-per-type descriptor + registry | DONE (Phases 0–3.5) |
| `Heap::Ptr<T>` = `*mut T` → RefRepr `ThinPtr` | DONE (existing) |
| trait-method customisation points (alloc/free) | DONE (ordinary traits) |
| `Storage`/`Datatype::View<S>` GAT storage axis | DONE (in fabric) |

The zone parameterisation rides almost entirely on this. The selection
(`which RefRepr for `Z::Ptr<T>`?`) is just "which concrete type the GAT
projects to" — already how RefRepr works (one descriptor per type).

---

## 5. What remains — the ISOLATED Phase-4 codegen

GATs give the *selection*; they do not *build* the relative pointer. Three
remaining pieces, in order of how-new:

### 5a. The relative-pointer RefRepr descriptor (the one real codegen item)

`HRel<T>` = a self-relative `i64` offset. It is a new RefRepr built by
composing existing axes (see `ref-repr-design.md` §3):

- **storage-strategy = `RelOffset(self)`**: storage is the `i64` offset;
  `materialize(slot_addr) = slot_addr + load(slot_addr)` (absolute ptr in
  compute), `lower(target, slot_addr) = store(slot_addr, target − slot_addr)`.
- **meta-recovery**: `None` for a sized pointee; `InBandHeader` for a
  self-describing DST (recover tail len from the object header — reuses the
  `#[self_describing]` machinery already landed).
- **ConvCtx**: `materialize`/`lower` need the slot's own address
  (`anchor_addr`). The `RefRepr` interface already carries `ConvCtx {
  anchor_addr, base_ptr }`; the load/store sites must *pass* it. This is
  the one place the descriptor API is exercised beyond the near-identity
  (heap) case — and the reason `materialize`/`lower` were made first-class
  in Phase 3.

Everything else (field GEP, `data`/`meta`, `is_null`, layout) flows
through the descriptor unchanged — that is the whole point of RefRepr.

### 5b. Per-zone borrow rules + directional gluing (borrow-check, not codegen)

- Directional gluing: `Heap → {Writ,Stack,…}` allowed; `Writ → Heap`
  rejected. JUSTIFICATION = **position-independence/serialisability**, NOT
  relocation (corrected 2026-06-05): an absolute heap pointer inside Writ
  data would break on serialise/mmap, so Writ fields must be self-relative
  (zone-internal); heap data isn't serialised, so it may glue to any zone.
- En-masse free changes drop semantics: no per-object `Drop` for `Writ`-zone
  objects — the zone is freed wholesale.
- NOT a relocation rule: there is no "no `&mut` across grow" zone wall —
  neither zone relocates. Container-level reference invalidation (`&mut Vec`
  during `push`) is ordinary Rust borrow-check, identical in both zones.

These are per-zone-kind passes in the borrow checker, keyed on the zone
of the reference/object — new logic, independent of the GAT/RefRepr work.

### 5c. Allocation impls

`Heap::alloc` = `malloc`; `Writ::alloc` = segment-append (the existing
writ2 allocator). Ordinary trait-method bodies; no compiler change.

---

## 6. Open questions / risks

- **GAT-instantiation comparison.** `fabric.logos:84` already works around
  a limitation: `Container` declares an explicit `ViewInStore` "to
  side-step the GAT-instantiation comparison inside generic code" — i.e.
  comparing two GAT instantiations for equality inside a generic body is
  fragile. The zone design leans harder on GAT projection in generic
  containers; **stress-test this** (deep nesting, `Z::Ptr<T>` as the Self
  of a dispatched trait) before committing.
- **`Stack` as a zone is more reframe than mechanism.** The borrow checker
  already enforces stack lifetime/escape rules. Folding them under
  `zone(Stack)` is conceptually clean (one axis) but risks re-plumbing
  working logic. Recommendation: land `Heap`/`Writ` as the parameter
  first; bring `Stack` under the umbrella once the mechanism is stable.
- **Don't fork `Storage`.** Implement `Zone` as the layer `Storage` sits
  on (§2), collapsing `PrimVec`/`ZoneSlice` into one zone-generic backing.
  A `Zone` built parallel to `Storage` re-creates the very two-path drift
  this design exists to remove.

---

## 7. Summary

The spine: **`Zone` is a marker-type parameter `Z: Zone` whose GAT `Ptr<T>`/
`Ref<T>` select a per-zone pointer representation (RefRepr) and whose
methods supply allocation.** Heap is the identity instance.
Generalisation over zones is ordinary generics; the machinery (GATs,
RefRepr-per-type, mono) already exists and is verified. The remaining work
is one RefRepr descriptor (the relative pointer, §5a), per-zone borrow
rules (§5b), and allocation bodies (§5c) — not a new type-system axis.
