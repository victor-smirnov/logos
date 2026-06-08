# Hermes2 `&mut` carries its zone — fat mutable reference (design + plan)

Status: **DONE — compiler feature + both containers migrated (2026-06-08).**
Commits: 3eb14084 (compiler: marker / FatZoneMut repr / zone_mut_ref+zone_of /
field+method access / spill returnability), b09d371b (Array<HAny> migrated +
reborrow-peel / method-self / cast sites), 9b235faf (HMap migrated). Both
`Array<HAny>` and `HMap` dropped their `alloc` field — the zone now rides the fat
`&mut`. Toy test zone_mut_fat_ref + full hermes2 suite + showcase pass, valgrind-
clean, L4 5568/5568.

REMAINING (next phase — the dynamic mutable-traversal surface, not yet built):
`HAnyMut` {obj, zone} value (mutable dual of HAny) + `root()`/`root_mut()` entry
points + `Array::get_mut`/`array_at_mut` / `HMap::*_at_mut` (resolve a child to a
fat `&mut` carrying the parent's zone via zone_of(self)).

(original design below.)
 Realises hermes2-design §9 part 4
("growing `&mut self` carrying `(self_ptr, allocator)`"). Goal: a `&mut T` to a
zone-resident growable container is a FAT reference `{data, zone}` carrying its
zone (the `Allocator`); the zone rides the reference and cascades through mutable
traversal, so grow methods reach the allocator from `&mut self` with no stored
field and no threaded `&Hermes2`. Read `&T` stays THIN (reading never grows).

## Why (the model)

A `Hermes2` container owns an `Allocator` = a list of never-move, never-grow
segments = its **zone**. Mutation that grows must allocate in that zone. Three ways
to reach the allocator from a deep `&mut child`:
- store it in the object — rejected: the allocator is a heap object; its place is
  on the *reference* or *passed*, not baked into every resident (Victor).
- thread `&mut Hermes2` — aliases the children being mutated (two `&mut` from one
  container → borrow conflict). `&Hermes2` via interior-mut works but forces the
  container through every mutation call.
- **carry it on the `&mut`** — the allocator travels with the reference; `h` is
  never needed mid-traversal; it cascades parent→child. ← this design.

Symmetry:

| | read | mutate (+grow) |
|---|---|---|
| any-value | `HAny` {abs ptr} (thin) | `HAnyMut` {abs ptr, zone} |
| reference | `&Array<HAny>` / `&HString` (thin) | `&mut Array<HAny>` (fat {ptr, zone}) |

`root_mut() → &mut Array (fat) → get_mut(i) → HAnyMut (same zone) → as_array_mut()
→ &mut Array (fat) → …` — the zone threads through, `h` appears nowhere.

## Representation

A fat `&mut` is structurally the existing `{data, meta}` fat pair (slice/dyn/
DstRef) with `meta = *mut Allocator`. Reuse `repr_construct` / `repr_data` /
`repr_meta` / the 16-byte fat storage. NEW only: a `RefReprKind::FatZoneMut` and
the classification that routes `&mut MarkedType` to it. `&MarkedType` (shared)
stays `ThinPtr`. So Ref-vs-MutRef of a marked type pick thin-vs-fat.

## Compiler steps

1. **Marker** `#[zone_mut]` on a struct → `SemaStructInfo.zone_mut` + `LStructDef.
   zone_mut` (mirror `self_describing`/`pinned`).
2. **ref_repr_of**: `MutRef(zone_mut sized type)` → `FatZoneMut` (16B {data, zone});
   `Ref(...)` → `ThinPtr` (unchanged). size/llvm-type via the fat-pair helpers.
3. **Construction intrinsic** `zone_mut_ref::<T>(ptr: *mut T, zone: *mut Allocator)
   -> &mut T` — builds the fat pair (model on `dst_from_raw_parts`). Factories
   (`h.array()`, `h.map()`) use it.
4. **Extraction intrinsic** `zone_of(r: &mut T) -> *mut Allocator` — `repr_meta` of
   the fat ref. Grow methods use it instead of the (removed) `alloc` field.
5. **Field/method access** on a fat `&mut self`: the data half (`repr_data`) is the
   object pointer — thread into the receiver→struct-ptr path (like DstRef data
   extraction). Meta is touched only by `zone_of`.
6. **Reborrow / store / load / pass** of a fat `&mut` — reuse the fat-pair sites.
7. **Borrow check**: a fat `&mut` is ONE `&mut` borrow (exclusivity, unchanged);
   the zone half is a carried capability, not a tracked borrow.

## stdlib

- Mark `Array<T>` / `HMap` `#[zone_mut]`; **remove the `alloc` field**; factories
  build the fat ref via `zone_mut_ref`; grow uses `zone_of(self)`.
- `HAnyMut` — a value `{obj: *mut u8, zone: *mut Allocator}` (the mutable dual of
  HAny). `as_array_mut()/as_map_mut() -> &mut Array/&mut HMap` (fat, via
  `zone_mut_ref`); built by `arr.get_mut(i)` (zone = `zone_of(self)`). Typed
  children get direct `arr.array_at_mut(i) -> &mut Array` (no HAnyMut); HAnyMut is
  for the dynamic case.

## Spike (first)

Steps 1-7 minimal for **`Array<HAny>` only**: mark it, `h.array()` returns fat
`&mut`, `push` grows via `zone_of(self)`, the `alloc` field gone, a test
push+grow+get. Then extend to HMap + add HAnyMut / `*_at_mut`.

## Risk

A new ref repr threaded through codegen — same class as the thin-DstRef work
(silent-codegen if a site is missed). Mitigate: behavior tests + valgrind per
step; gate on the hermes2 subset. Reuses the fat-pair machinery, so less new
codegen than building a repr from scratch.
