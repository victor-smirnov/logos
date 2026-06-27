# Writ — Minimal Type-Safe, BC-Integrated Container (plan)

Goal (Victor, 2026-06-06): a **minimal, type-safe, borrow-check-integrated**
Writ container holding, for now, **primitive types and `Array<HAny>`**
(the Writ analog of `Array<AnyVal>`). Concrete subset of the §9 roadmap in
[writ-design.md](writ-design.md).

## Current state (what already exists)

| §9 part | Status | Where |
|---|---|---|
| 1. self-relative `i64` refs | **DONE** | `#[rel_ptr]`/RelOffset descriptor (typed `RelPtr<T>`), `RelAny` (type-erased), `WritTagSystem` (tagged dispatch), pin rule, `ptr_rel_compatible` |
| 2. never-move segment container | **DONE** | `stdlib/lang/writ/{allocator,container}.logos` — `Segment` (self_describing DST), `Allocator` (segment list, bump, append-on-full, reach assert), `Writ`, `writ2_new/alloc/alloc_n/alloc_bytes/free` |
| 3. placement + thin reads | **PARTIAL** | raw-pointer typed placement works; tagged placement (`write_tag`) works; **no** type-safe views, **no** safe API |
| 4. growing `&mut self` + alloc capability | not started | (deferred for minimal — see below) |
| 5. `Rc<dyn Resident>` + holder/view + BC | not started | **but** the minimal BC integration needs NO new machinery (below) |
| 6. copy-compaction | not started | (deferred) |

**Borrow-checker verdict (explored 2026-06-06):** production-ready for what we
need. `fn get<'a>(self: &'a C, …) -> &'a T` ties the returned view's lifetime to
the container via lifetime elision; E0716 temp-borrow detection rejects
`let v = make().get(0)`; `&mut` exclusivity + disjoint-field borrows enforced. So
**a view = an ordinary `&'a` borrow of the container** gives us "BC-integrated"
for free. The `Rc<dyn Resident>`/`SuperRc` ESCAPE story (part 5 full) is only
needed when a reference outlives the holder scope — **deferred**; the minimal
container hands out in-scope `&'a` views.

## The gap to the goal

1. The container is owned/managed by hand (`writ2_free` is manual + `unsafe`) —
   no RAII, no type-safety at the owner level.
2. The public API is all `unsafe fn … -> *mut T` — raw pointers leak; not type-safe.
3. No `HAny` (the 8-byte heterogeneous slot) and no `Array<HAny>`.

## Design decisions (recommendations — confirm before building)

- **D1 — TWO hand-rolled types, value-form vs at-rest-form** (revised after the
  String/move discussion). Both `{ raw: i64 }`, bit-identical layout, bit-0
  discriminant (`1`→inline **Pod** primitive; `0`→**Ref**; `raw==0`=null); Pod is
  identical in both (position-independent), only the Ref half differs:
  - `HAny` — the **value form**: Ref = an **absolute** pointer to the tagged
    object. Movable by memcpy (like `String`'s `ptr`); lives on stack/heap/in
    registers/by-value. Dispatch via `WritTagSystem` (materialize → `*const u8`
    → read_tag).
  - `HAnyRel` — the **at-rest form** in an arena slot: Ref = a **self-relative**
    delta `target − &slot` (anchor = the slot's own address).
  - Bridge: `materialize(slot: *const HAnyRel) -> HAny` (Ref: `&slot+delta`)
    and `lower(v: HAny, dst: *mut HAnyRel)` (Ref: `target−&dst`); identity
    for Pod. Array `get` materialises, `put` lowers. Distinct types make the
    conversion mandatory (type-safe — can't use a relative slot as an absolute value).
  - **Later (more canonical):** make these niche-packed `enum { Ref | Pod }` once
    compiler niche-packing of a self-relative Ref lands. Hand-rolled structs now.
- **D2 — type-safety boundary**: the container *internals* stay `unsafe` (raw
  segment arithmetic); the *public* API is safe — no `unsafe`/raw pointer in
  user code. Reads return `&'a` views tied to `&'a self`.
- **D3 — container is RAII**: add `impl Drop for Writ` (free-en-masse), making
  it a proper owned root (no manual free, no use-after-free). It becomes a
  move-type owning its segments.
- **D4 — array growth in the minimal phase**: grow by allocating a fresh backing
  buffer in the arena and re-pointing the array header's `RelPtr` (old buffer
  stays live-but-dead until the container drops — design §3 "no per-object
  reclamation"). This is container-rooted growth, so we do **not** need part-4's
  `&mut self`-carries-allocator capability yet.

## Phased plan (each phase L4-gated + valgrind-clean)

### Phase 0 — RAII + type-safe owner shell  *(D3)*
- `impl Drop for Writ` → `allocator_free` en masse; remove the need for manual
  `writ2_free`. `Writ` becomes a non-movable-after-handout owned root.
- Safe `Writ::new(seg_size)`; the raw `writ2_alloc*` stay `unsafe` internals.
- Test: create + auto-drop a container; valgrind clean. Existing
  `writ2_container`/`writ2_allocator` tests stay green.

### Phase 0.5 — movable self-relative refs (value-form absolute, like `String::ptr`)
The reduction that lets a rel_ptr / `HAny` value move by **plain memcpy**,
exactly as Rust's (weaker-than-C++, no move-ctor) move relocates a `String`'s
absolute `ptr`:
- A rel_ptr type's **value/compute representation is an absolute pointer** (not
  the i64 delta). Locals / args / returns / register / stack values hold the
  absolute form → move = memcpy, **no divergence, no re-anchor codegen**.
- The **self-relative i64 delta is only the at-rest encoding in arena (zoned)
  storage**; `materialize` (read arena→value) / `lower` (write value→arena) are
  the boundary — already built for `#[rel_ptr]` field read/write.
- **Pin relaxes:** a standalone rel_ptr / `HAny` *value* is movable (its
  value-form is absolute). The relative form never appears in a movable value.
  The only relative-form-in-memory case — a whole **arena-resident** struct with
  rel_ptr fields — is **never moved** (arena objects accessed via views;
  reclamation is copy-compaction, which converts through absolute = materialize).
- Verify during impl what a rel_ptr **local** currently lowers to (delta vs ptr);
  make it the absolute value-form. Update [writ-design.md](writ-design.md)
  §2 ("cannot be carried bare" → "carried bare as the absolute compute form;
  re-lowered only on store into zoned storage").
- Tests: move / return / stack / copy a `HAny` (and a `RelAny` value) — the
  target still resolves after relocation, including across a segment boundary.

### Phase 1 — `HAny` (the Writ AnyVal)  *(D1)*
- `stdlib/lang/writ/anyval.logos`: `HAny { raw: i64 }` + constructors
  (`pod_i64`/`pod_bool`/… and `from_ref(slot, target)`), accessors
  (`is_pod`/`is_ref`/`is_null`, `as_i64`/…, `resolve(self) -> *const u8`).
  Wide primitives that don't fit inline are stored as a tagged zone object and
  referenced (Ref) — but the minimal set keeps i64/bool inline; deciding the
  inline-width budget is part of this phase.
- `resolve` materialises a Ref self-relatively (`(self as i64) + (raw & ~1)`),
  then `WritTagSystem` recovers the type for dispatch.
- Test: round-trip a Pod (primitive) and a Ref (self-relative, resolves correctly
  **across a segment boundary** — never-move proof).

### Phase 2 — type-safe placement + BC-tied views  *(D2, BC)*
- Safe placement: `Writ::put<T>(&mut self, v: T) -> Handle<T>` (or a slot
  index) — wraps `writ2_alloc` + init, leaks no raw pointer.
- Safe read: `Writ::get<'a>(&'a self, h) -> &'a T` — view tied to `&'a self`
  by existing lifetime elision; BC enforces view ≤ container, exclusivity, E0716.
- Tests (pass + fail): store/read primitives type-safely; **fail** tests that the
  borrow checker rejects (a) using a view after the container drops, (b) mutating
  the container while a view is live.

### Phase 3 — `Array<HAny>` + primitive arrays  *(D4)*
- `stdlib/lang/writ/array.logos`: `ZArray { len: i64, cap: i64, data: RelPtr<…> }`
  — header in a segment, element buffer in the arena, `data` a self-relative
  pointer. Mirrors legacy `Array<AnyVal>` but self-relative + safe API:
  `new_array(&mut Writ, cap)`, `push(&mut self, HAny)`,
  `get<'a>(&'a self, i) -> &'a HAny`, `len`. Append-grow per D4.
- Test: build an `Array<HAny>` of inline primitives **and** a nested
  `Array<HAny>` (Ref element); read back across segment boundaries; verify
  sums; BC-tied views; growth past one segment.

### Phase 4 — the minimal document (integration)
- A `WritDoc` whose root is an `Array<HAny>`; build a small heterogeneous
  document (ints + a nested array) entirely through the safe API; end-to-end
  type-safe + BC-clean + valgrind-clean. Optional: dump the blob and confirm it
  is self-contained (self-relative deltas only — relocatable).

## Explicitly deferred (not in the minimal goal)
- §9 part 4: growing `&mut self` carrying an allocator capability (we use
  container-rooted growth — D4).
- §9 part 5 (full): `Rc<dyn Resident>` / `SuperRc` for references that **escape**
  the holder scope + RC-elision (we use in-scope `&'a` views).
- §9 part 6: copy-compaction (copying GC).
- Nested `PackedAllocator` coupling; the atomic/`Arc` residency variant.
- Wide / arbitrary user datatypes (only primitives + `Array<HAny>` now).
