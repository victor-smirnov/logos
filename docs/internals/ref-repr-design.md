# Reference Representation (`RefRepr`) — Design

A compiler refactor that consolidates **how reference-like types are
represented** into a single descriptor + registry, so the codegen
dispatches through it instead of ~50 scattered `switch (kind)` sites.

Status: **design (2026-06-03), Phase 0 not started.** Companion to
[writ2-design.md](writ2-design.md) (its `RelPtr<T>` is the first
storage/compute specialization this enables).

> **Goal / success criterion.** Adding a new kind of fat pointer (and
> there will be more — wider-than-16B pairs, segmented/cursor refs, the
> writ2 `RelPtr` family) must reduce to a **series of point changes in
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
compaction** with explicit conversion. The zoned reference family (§6) is
the first non-trivial instance: a pointer field inside a `#[zoned2]`
struct is a thin self-relative offset in storage, an absolute pointer in
compute, with metadata recovered (where needed) from the in-band object
header. (The explicit `RelPtr<T>` type was removed 2026-06-04 — the
compiler derives relativity from the zoned context; reintroduce only if a
first-class relative-pointer value is ever needed.)

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

  // NICHES — invalid storage bit-patterns the enum-layout may use to pack a
  // discriminant for free (e.g. a zoned pointer's low bit is always 0 since
  // zoned objects are ≥2-aligned; a zoned *reference* is additionally non-null).
  // `HAny = enum { Ref(zoned) | Pod }` and `Option<zoned T>` rely on this.
  NicheSet niches();
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
| zoned `&WritString` (in `#[zoned2]`) | FatCustomDst | RelOffset(i64, self) | InBandHeader | **8B / 16B** |
| zoned `&SizedT` (in `#[zoned2]`) | Thin | RelOffset(i64, self) | — | 8B / 8B |

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
  self-referential heap custom-DST (`Segment { next: *mut Segment, … }`)
  closes here, in **one place**, not across the compiler. (Heap DSTs use
  the Rust-fat repr — distinct from the zoned reprs of §6.)
  - **`#[self_describing]` landed (2026-06-04, c4183d47 + Segment
    conversion).** The self-ref-DST bug closed via a DST *split*: a
    struct whose tail length is recoverable from an in-band prefix field
    (e.g. `Segment.cap`) is marked `#[self_describing]`, and a RAW
    `*const/*mut Self` to it stays **THIN** (8B, kind=Ptr) instead of
    fattening to a 16B DstRef — so the self-referential `next` link is a
    plain thin pointer, no fat self-reference. `&Self`/`&mut Self`/
    `Box<Self>` keep the fat DstRef (no in-band-length contract).
    Implemented at the Ptr→DstRef canonicalisation in *both* sema
    (`resolve_type` + `subst_type_sema`, pub-check-free flag lookup) and
    mono (`mono_subst`, gated on `tv.kind()==Ptr`); the flag rides
    `LStructDef.self_describing` through clone + the `.hm0` boundary
    (re-collected from the attribute on stdlib recompile). stdlib
    `Segment` is now a real `[u8]`-tail DST. Prefix-field read/write +
    self-ref chain through the thin pointer all work; Rc/Arc&lt;dyn&gt;
    keep the fat path. **Open:** typed tail access (`&seg.data` as a
    bounded `[u8]`) mistypes today (the allocator uses raw byte
    arithmetic + `cap`, so doesn't need it); and `sizeof::<Segment>()`
    returns the *header* size (tail offset) for an unsized DST — a Logos
    divergence (Rust `size_of` requires `Sized`), convenient here but the
    Rust-faithful query is `offset_of!`. Both are follow-ups, not blockers.
- **Phase 3.5 — enum niche optimization** (new compiler feature, §7):
  consume `RefRepr::niches()` in the enum-layout so a discriminant packs
  into invalid bit-patterns. Prerequisite for `HAny` and
  `Option<zoned T>`. **DONE (2026-06-08):** two niche kinds —
  `NullPtr` (Option<&T>-shape, pre-existing) and `LowBit` (a 2-variant
  enum with one `&T`/`&mut T` arm to an align≥2 pointee + one ≤63-bit
  integer arm packs into ONE word; disc = the low bit, value arm stored
  `(v<<1)|1`, pointer arm stored raw). All routed through the existing
  enum chokepoints (`enum_store_disc` no-op / `enum_load_disc` low-bit
  decode / `enum_payload_ptr` runtime-selects the payload address by the
  low bit — value→`word>>1` temp, pointer→the enum slot itself, mirroring
  the NullPtr binding). Test `pass/niche_low_bit.logos`. The `HAny`
  niche-enum (`enum { Ref(*zoned) | Pod }`) is the next step (Phase 4).
- **Phase 4 — add the zoned reference reprs** (§6, the first real
  storage/compute specializations): typed `zoned T` (untagged), erased
  tagged zoned ptr, and `HAny`. writ2 zoned pointer *fields* become
  auto-relative via these reprs — no `RelPtr<T>`, no hand-rolled resolve.
  Validates the abstraction end-to-end.

After Phases 0-2, Phase 3/3.5/4 and every future pointer kind are point
changes in the registrar (+ the one-time niche feature) — the success
criterion.

---

## 6. The zoned reference model (the first real client)

Zoned objects split into **tagged** and **untagged**, and you pay for a
tag only where dynamic dispatch is actually needed (heterogeneous data) —
never for container internals whose types are known.

- **untagged → a parameterized reference, no tag.** A pointer to a known
  type. In a `#[zoned2]` struct it stores as a **self-relative `i64`
  offset**; in compute it is an **absolute pointer**, converted
  transparently on read/write (`materialize`/`lower`). Two flavors mirror
  Rust's raw/ref split: `*zoned T` (nullable; niche = low-bit-0 from ≥2
  alignment) and `&zoned T` (non-null; niches = low-bit-0 **and** non-null).
- **tagged → a type-erased reference + tag dispatch.** Until the tag is
  read the referent is opaque (`Object`/`Variant`/`Union`); the canonical
  op is *read tag → narrow to a typed compute form → process* (a built-in
  match the compiler materializes per branch — the general case of which
  in-band metadata recovery is the static, single-branch instance).
- **`HAny = enum { Ref(*zoned) | Pod(embedded) }`** — the AnyVal,
  modeled as an ordinary **niche-packed enum** (discriminant in the
  pointer's low-bit niche; `Ref` = even word = self-relative offset, `Pod`
  = low-bit-1 = inline tagged value à la today's AnyVal). In zoned storage
  it is one 8-byte word; copied into compute, the `Ref` arm's pointer
  becomes absolute. The embedded-POD encoding reuses AnyVal's
  (i56/u56/bool/…); short-string-inline (≤6B) is deferred to its own
  design session.

**Ordinary typed pointer fields** in a `#[zoned2]` struct are the untagged
case: relative in storage, absolute in compute, no tag. **All** pointer
fields of a `#[zoned2]` struct auto-convert (not every struct can be
`#[zoned2]`, so not every pointer needs converting). A `#[zoned2]` struct
**cannot be stack-allocated** (sema constraint — self-relative offsets are
pointless/fragile off-zone). `#[zoned2]` is a **temporary** attribute
distinct from writ1's `#[zoned]` (explicit `RelPtr`) so the compiler
doesn't conflate the two storage models during the migration; merged once
writ1 retires.

This whole model is just a population of the `RefRepr` registry: typed
`zoned T`, erased-tagged, and `HAny` are descriptors; their niches
feed the enum-layout (§7); the relative↔absolute conversion is their
`materialize`/`lower`.

---

## 7. Prerequisite: enum niche optimization (new)

Logos enums today are value-repr `{disc, payload}` with **no niche
optimization** — a discriminant always costs its own bits. The zoned
model needs niche-packing: `HAny`'s `Ref|Pod` discriminant must live
in the pointer's low-bit, and `Option<zoned T>` must use `null` as `None`.
So this is a **new compiler feature, built as part of the Writ2 plan**
(Phase 3.5): the enum-layout queries `RefRepr::niches()` (invalid storage
bit-patterns) and, when a payload field offers a niche, encodes the
discriminant there instead of a separate tag — exactly Rust's
`Option<&T>` / `NonNull` niche optimization. Scope: at minimum low-bit and
null-pointer niches; general niche-packing (enum-of-enums, range niches)
can follow. **§7 DONE (F2, 2026-06-08, commit 9e132ea0)** — see §7 note above.

---

## 8. Phase 4 — the zoned reference repr (F3): implementation map

Decided 2026-06-08 (full `*zoned` RefRepr). Surface: **`#[zoned2]` on the
niche ENUM** (not a `*zoned T` wrapper type — same context-derives-relativity
philosophy as `#[zoned2]` on structs / the removed `RelPtr<T>`). A `#[zoned2]`
niche enum (HAny-shape) has the **storage/compute split**: the `Ref` arm is a
self-relative offset AT-REST (in memory, behind a pointer) and an absolute,
movable pointer as a VALUE. The bridge is the compiler-owned generalization of
today's `ha_materialize`/`ha_lower`:

- **materialize(slot)** — load word `r`; `r==0`→null (raw 0); `r&1==1`→Pod
  (raw `r`, position-independent); else Ref→`slot + r` (absolute). The access
  location IS the anchor (exactly RelOffset's anchor convention).
- **lower(val, slot)** — `val==0`→store 0; `val&1==1`→store `val` (Pod); else
  Ref→store `val − slot` (delta).

This is RelOffset's conversion, *gated by the low bit* (Pod arm = identity).

**Phase 1 (mechanism + toy) — exact sites:**
1. sema accept `#[zoned2]` on enums: `sema_impl.hpp:1227` add
   `| bit(AttrTarget::Enum)`; `SemaEnumInfo` (`sema_impl.hpp:2225`) add
   `bool zoned2=false`; `collect_enum` (`sema_collect.cpp:1898`) set it from
   `pending_annots` (mirror the struct path at `sema_collect.cpp:1596`; the
   enum item's annots are live in `pending_annots` at the dispatch ~1423).
2. propagate: `lir.hpp` `LEnumDef` (line 914) add `bool zoned2=false`;
   `lower_enum_def` (`sema_decl.cpp:1141`) set `ed.zoned2`; `clone_enum_def`
   (`mono_clone.cpp:5369`) add `nd.zoned2 = tmpl.zoned2`.
3. mlir: `TaggedEnumInfo` (`mlir_gen_impl.hpp:66`) add `bool zoned=false`;
   `register_tagged_enum` (`mlir_gen_types.cpp`) set `info.zoned = ed.zoned2`.
4. mlir: two helpers (model on `repr_materialize`/`repr_lower` RelOffset case,
   `mlir_gen_expr.cpp:4890`/`4908`): `zoned_enum_materialize(slot)` /
   `zoned_enum_lower(val, slot)` doing the conditional-offset above.
5. **threading — CRUX (found 2026-06-08, validated):** threading on a *plain*
   `*p` deref of a `#[zoned2]` enum is **PROVENANCE-UNSOUND**. Enums are
   by-pointer (the value IS a ptr to the 8-byte word alloca); a `&HAny` to a
   *value-form* local is the same `*HAny` type as a ptr into at-rest storage,
   so `*p` cannot tell "materialize this at-rest slot" from "no-op, already a
   value" — materializing a value-form word (absolute) would wrongly add the
   slot address. The at-rest-ness MUST be carried at the **type** level. Hence
   the `*zoned T` / `&zoned T` flavor (§6) is REQUIRED, not optional: only a
   `*zoned T` deref/assign materialises/lowers; a plain `*T` is the value form.
   So step 5 is: introduce `*zoned T`/`&zoned T` (grammar + a flag on the Ptr
   type, threaded through sema/mono/TypeUID/serialize like `mut_ptr`), and route
   `zoned_enum_materialize`/`zoned_enum_lower` on `*zoned`-deref READ
   (`EDerefView` 1629) / WRITE (`SDerefWrite` 1071). The `zoned_enum_*` helpers
   (step 4) are landed and correct; only the *trigger* moves from
   "enum-pointee-is-zoned" to "pointer-is-`*zoned`".
6. toy `pass/zoned_enum_basic.logos`: `#[zoned2] enum Z { Ref(&i64), Val(i32) }`;
   store `Z::Ref(&x)` into a `*zoned Z` slot, assert the stored word ≠ the
   absolute address (it's a delta), read back via `*slot`, match → deref → x.

**Phase 1b — LANDED (commit 39ac9d16).** `*zoned T`/`*zoned mut T` parse via a
CONTEXTUAL `zoned` (grammar `STAR IDENT type_ref`; not reserved, so `#[zoned]`
attrs are unaffected); `zoned` rides in Ptr's free `const_val` bit 0 (interns/
serialises/equates via existing plumbing — no P2-11 trap); deref-read
materialises, deref-write lowers, gated on `zoned_ptr() && zoned_niche_enum_info`.
Test `pass/zoned_ptr_basic.logos`. A plain `*T` deref is unchanged (value form) —
the provenance ambiguity is resolved by the pointer TYPE.

**Phase 2 — LANDED (commit e23f6400, validated).** `.add(i)` PRESERVES the
`*zoned` type, so buffer element access already routes through the Phase-1b
bridge — no extra codegen. Test `pass/zoned_buffer.logos` proves the
relocation-safety invariant (same target at two slots → two different at-rest
deltas, both materialising to the same absolute ref). `HArray.data: *zoned HAny`
will Just Work.

**Phase 3 — re-express HAny, retire HAnyRel + ha_materialize/ha_lower.** The
foundation is ready; this is the stdlib application. HAny becomes a `#[zoned2]`
niche enum and its buffers become `*zoned HAny`. The Pod-encoding fork (still
open, coupled to the Writ1 port's inline-scalar set): HAny's Pod is a 63-bit
`(value<<7)|code` tagged word with no 63-bit primitive. Options:
  (a) add `i63`;
  (b) struct Pod arm `{code:u8, value:i56}` + extend the niche to pack a ≤63-bit
      STRUCT payload (most Rust-faithful — Rust niches DO pack struct payloads);
  (c) a no-shift "raw tagged word" niche flavor (`LowBitRaw`): Pod stored RAW
      (low-bit-1 guaranteed by the `HAny::pod` builder), Ref low-bit-0 +
      relativised — preserves the EXACT current wire word, simplest.
Recommendation: (c) for an exact-encoding-preserving first cut, revisit (b) when
the port needs richer inline payloads. Decide alongside the port's scalar set.

**Phase 3** re-expresses HAny + retires HAnyRel. WRINKLE: HAny's Pod is a
63-bit `(value<<7)|code` composite — F2's `(v<<1)|1` shift reproduces the
exact current word IF the Pod arm can hold 63 bits, but there is no 63-bit
primitive (max `i56`). Decide then: (a) add `i63`, (b) struct Pod arm
`{code:u8, value:i56}` + extend niche to ≤63-bit struct payloads (most
Rust-faithful), or (c) a no-shift "raw tagged word" Pod flavor. Affects the
wire-encoding the hbs/Writ1 port depends on — resolve before Phase 3.
