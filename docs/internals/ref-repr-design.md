# Reference Representation (`RefRepr`) — Design

A compiler refactor that consolidates **how reference-like types are
represented** into a single descriptor + registry, so the codegen
dispatches through it instead of ~50 scattered `switch (kind)` sites.

Status: **design (2026-06-03), Phase 0 not started.** Companion to
[hermes2-design.md](hermes2-design.md) (its `RelPtr<T>` is the first
storage/compute specialization this enables).

> **Goal / success criterion.** Adding a new kind of fat pointer (and
> there will be more — wider-than-16B pairs, segmented/cursor refs, the
> hermes2 `RelPtr` family) must reduce to a **series of point changes in
> the registrar(s)** — define one `RefRepr` descriptor (usually by
> composing existing axis variants) and register it. It must touch **none**
> of the ~50 codegen sites. Today every new pointer kind effectively
> "rewrites the whole compiler" (the same per-kind switch re-edited in 50
> places); this ends that.
>
> This is **not** a plugin system / attribute DSL (premature — real
> plugins are far off). It is compiler-internal, hand-written C++:
> one descriptor interface, one registry, ~50 dispatch sites.

---

## 1. The storage/compute split (the principle)

Logos deliberately separates two representations of a reference, bridged
by a runtime conversion (Victor, 2026-06-03):

- **storage** — how it sits in memory (struct field, array element,
  zone): **compact** (a thin 8-byte handle, or a relative offset). Stores
  only what is **not recoverable**; metadata that can be re-derived is
  omitted.
- **compute** — how code manipulates it (SSA value, locals, operations):
  **whatever the code needs**, typically a Rust-shaped fat pair
  `{data, metadata}`.
- **conversion** — `materialize` (load: storage → compute, *rebuilding*
  recoverable metadata) and `lower` (store: compute → storage, *dropping*
  it).

Contrast with Rust, which uses **one** form (fat-by-value) for both
storage and compute — simple, but stores metadata redundantly in every
slot and is non-relocatable. Logos puts **Rust-conformance at the compute
layer** (code sees Rust-shaped fat pointers) and treats **storage as a
compaction** with explicit conversion. `RelPtr<T>` (thin self-relative
offset in storage, fat pointer in compute, metadata from the in-band
object header) is the first non-trivial instance — not a one-off
divergence, but a point in this general design space.

---

## 2. The `RefRepr` descriptor

One descriptor per reference-like type, returned by `repr_of(TypeRef)`.
Every representation-specific codegen site calls a method on it instead
of switching on the kind.

```
struct RefRepr {
  // STORAGE — the in-memory slot (field / element / zone)
  mlir::Type storage_type();            // LLVM type of the slot
  uint64_t   storage_size(), storage_align();

  // COMPUTE — the SSA value the code manipulates (canonical materialized form)
  mlir::Type value_type();

  // CONVERSION — the bridge (the heart)
  mlir::Value materialize(mlir::Value slot_addr, ConvCtx);  // load + rebuild meta
  void        lower(mlir::Value value, mlir::Value slot_addr, ConvCtx); // store + drop meta

  // SEMANTIC OPS — over the compute value
  mlir::Value data(mlir::Value value);        // address half
  mlir::Value meta(mlir::Value value);        // metadata half (len / vtable / none)
  mlir::Value is_null(mlir::Value value);     // data == 0
  mlir::Value construct(mlir::Value data, mlir::Value meta, ConvCtx); // from_raw_parts
};

// Conversion context — what materialize/lower need beyond the slot address.
struct ConvCtx {
  mlir::Value anchor_addr;  // the slot's own address (self-relative offsets)
  mlir::Value base_ptr;     // zone/buffer base (base-relative offsets)
  // … extended as new storage strategies need it
};
```

For Rust-fat reprs `materialize`/`lower` are near-identity (storage ==
value, a 16-byte load/store). For `RelPtr` they do the offset±anchor
arithmetic + in-band metadata recovery.

---

## 3. The three internal axes (how descriptors are built)

A descriptor is composed from three orthogonal axes — internal building
blocks so a new kind reuses existing variants rather than writing
everything from scratch:

1. **Pointee-shape** → the compute metadata + `value_type`/`meta`/
   `construct`:
   - `Thin` — `Sized` pointee, no metadata (one word).
   - `FatLen` — slice, metadata = length.
   - `FatVtable` — trait object, metadata = vtable ptr.
   - `FatCustomDst` — custom-DST, metadata = in-band size/len.
   - `Wide` — metadata wider than one word (a 3+-word value).
2. **Storage-strategy** → `storage_type` + `materialize`/`lower`:
   - `ByValue` — storage == value (Rust-fat); conversion is a 16-byte
     load/store.
   - `ThinHandle` — storage = data ptr only; metadata recovered on load.
   - `RelOffset` — storage = a (signed) offset; data = `anchor + off`
     (self-relative) or `base + off` (base-relative).
3. **Meta-recovery** (for thin/relative storage) → how `materialize`
   rebuilds metadata:
   - `None` — `Sized`, nothing to rebuild.
   - `InBandHeader` — read size/type from the object header.
   - `External` — from a sidecar/dictionary.

A concrete `RefRepr` = (pointee-shape × storage-strategy × meta-recovery):

| Type | pointee | storage | meta | storage / value |
|---|---|---|---|---|
| `&[T]` | FatLen | ByValue | in value | 16B / 16B |
| `&dyn Tr` | FatVtable | ByValue | in value | 16B / 16B |
| `&T` (`Sized`) | Thin | ByValue | — | 8B / 8B |
| `RelPtr<HermesString>` | FatCustomDst | RelOffset(i64, self) | InBandHeader | **8B / 16B** |
| `RelPtr<SizedT>` | Thin | RelOffset(i64, self) | — | 8B / 8B |

**Extensibility (the payoff):**
- *Wider than 16B* → a new pointee-shape (`Wide`, multi-word value);
  storage strategies compose unchanged.
- *New storage specialization* → a new storage-strategy + meta-recovery;
  pointee-shapes compose unchanged.
- Either way: implement the axis variant once + register the descriptor.
  The ~50 dispatch sites need no edits.

---

## 4. The refactor surface (the ~50 sites — see inventory)

The dispatch sites that must route through `repr_of(type)->…` instead of
hardcoding the kind, by category (full file:line inventory tracked
separately):

1. **Type lowering** — `logos_to_mlir` per-kind (`ptr_type` vs
   `slice_llvm_type`) → `repr->value_type()`.
2. **Field/aggregate layout** — `register_struct` / enum-payload inline
   field types → `repr->storage_type()`.
3. **Size/align** — `layout_of`, `sema_abi_byte_size` → `repr->storage_size/align()`.
4. **Load/read** — `EDeref` (return-ptr-vs-load), fat-field read,
   `slice_ptr`/`slice_len`, `place_slot_type` → `materialize`/`data`/`meta`.
5. **Store/write** — `SDerefWrite` (memcpy-16-vs-scalar), field write,
   let-binding store → `lower`.
6. **Construction** — `slice_lit`, `dst_from_raw_parts`, closure box,
   call-result spill → `construct`.
7. **Casts** — fat↔thin, thin→fat, int→fat → conversions between reprs.
8. **ABI** — `fn_call_ret_llvm_type`, `make_fn_type`, parameter binding →
   `repr->value_type()` + by-value/sret policy.
9. **is_null / null** → `repr->is_null()` / null `construct`.

(Out of scope: borrow-check provenance, drop/move/clone, auto-traits,
mono substitution, pretty-printing — those switch on kind for *other*
reasons, not representation.)

---

## 5. Phased plan (behavior-preserving, each step gated on full L4)

- **Phase 0 — scaffolding, no behavior change.** Introduce `RefRepr` +
  `repr_of(TypeRef)`. Implement descriptors that **exactly reproduce
  today's behavior** (including the current — buggy — DstRef). Route no
  site yet. Dead code; L4 unchanged.
- **Phase 1 — migrate load/store/extract/construct** (cat. 4-6, ~20
  sites) to dispatch through the descriptor. Per-batch, behavior-
  preserving, L4 after each.
- **Phase 2 — migrate type/layout/ABI** (cat. 1-3, 7-9) through the
  descriptor.
- **Phase 3 — fix DstRef** (now localized to its descriptor's
  `materialize`/`lower`): consistent storage↔compute conversion. The
  self-referential custom-DST (`next: *mut Self`) closes here, in **one
  place**, not across the compiler.
- **Phase 4 — add `RelPtr<T>` as a descriptor** (the first storage/compute
  specialization): hermes2 `RelPtr` becomes a registry entry, not
  hand-rolled stdlib resolve. Validates the abstraction end-to-end.

After Phases 0-2, Phase 3/4 and every future pointer kind are point
changes in the registrar — the success criterion.
