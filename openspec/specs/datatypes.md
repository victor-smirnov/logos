# Logos Datatypes — Genos / Eidos / Storage / View Specification

Living spec; edited as the architecture evolves.  Pair with `docs/datatypes.md`
for the tutorial view and with the stdlib module headers for inline context.

## 1. Vocabulary

Four layers, named after their Greek/Platonic-Aristotelian roots to pair with
the language name λόγος:

| Term     | Role |
|----------|------|
| `genos`  | **Logical datatype family.** "What the data means" — Varchar, Integer, Decimal, Uuid, Duration. Stable semantic identity, cross-tag-system. Carries `type_code` and the trait contract (mandatory operations). |
| `eidos`  | **Canonical byte layout.** A concrete, zone-relocatable, shareable form of a genos within one tag system. Carries the physical representation (`HermesString` zone layout, `IntegerInline` AnyVal bits). |
| `struct` | **In-process type.** Regular Logos struct.  Process-local, non-relocatable.  Includes alternate storages (`HermesStringStorage`), view types, local computational mirrors. |
| *view*   | **Processing projection.** Lightweight handle (`StringView = ptr + len`, `DecimalView`, …) used while code computes.  Built on demand from a storage. |

Each genos has one `type_code` shared across all tag systems (the numeric name
of its semantic identity).  Each (genos, tag-system) pair has **exactly one
eidos** at a time — a hard invariant, enforced at dispatch registration.
Dispatch through a tag system for a given `type_code` unambiguously selects the
one registered eidos's handler.

## 2. Principle — Representation ↔ Processing

Genos + eidos fix **representation**:
- byte layout is stable,
- relative addressing keeps objects zone-relocatable,
- eide are shareable across processes / accelerators / wire formats.

Storage + view fix **processing**:
- storages hold bytes in a specific container (zone slab, SoA buffer, mmap
  view),
- views are the compact value form that code reads and writes.

Separating the two is what lets zone-allocated Hermes objects be shared with no
serialisation cost while the processing code works with natural value types.

## 3. One Genos, Many Eide

A genos can have different eide **across** tag systems (one per system):

```
genos Varchar (type_code = 28)
  │
  ├── eidos HermesString       in HermesZoneTagSystem       (vlen prefix + UTF-8 in zone)
  ├── eidos HermesStringWire   in HermesWireTagSystem        (future, wire-safe layout)
  └── ...                      in future tag systems
```

Inline-representable integers and their zone-allocated counterparts are
**separate genera**, not two eide of one Integer:

```
genos TinyInt     type_code=20   (i8)
genos UTinyInt    type_code=21   (u8)
genos SmallInt    type_code=22   (i16)
genos Integer     type_code=23   (i24 — fits in AnyVal inline)
genos USmallInt   type_code=24   (u16)
genos UInteger    type_code=25   (u24)
genos I64         type_code=26   (i64 — too big for inline, lives in zone)
genos U64         type_code=27   (u64)
genos Varchar     type_code=28   (string)
genos Boolean     type_code=37
```

The split by value range is a deliberate design choice: values that fit in
AnyVal's 24-bit payload get an inline eidos in `HermesInlineTagSystem`; larger
integers have zone eide in `HermesZoneTagSystem`.  The two tag systems share
the same code space but register different eide — Integer has an eidos in
inline-TS, I64 has an eidos in zone-TS, neither has one in the other.

## 4. Storage Hierarchy

Processing storages live in regular Logos structs and are orthogonal to
genos/eidos.  Linear trait hierarchy:

```
trait Storage {
    type Elem;
    fn storage_len(s) -> u64;
    fn storage_get(s, idx) -> Self::Elem;
}

trait OwningStorage: Storage {
    fn storage_drop(s);
}

trait DynamicStorage: OwningStorage {
    fn storage_new() -> Self;
    fn storage_push(s, v: Self::Elem);
    fn storage_clear(s);
}

trait BorrowedStorage: Storage { /* read-only marker */ }
```

| Trait              | Owns? | Grows? | Example                       |
|--------------------|-------|--------|-------------------------------|
| `Storage`          | any   | any    | *base capability*             |
| `OwningStorage`    | yes   | any    | fixed zone slab               |
| `DynamicStorage`   | yes   | yes    | `PrimVec<T>`, SoA containers  |
| `BorrowedStorage`  | no    | no     | mmap slice, zone view         |

`DynamicStorage: OwningStorage` because a growing backing must take
responsibility for freeing.

## 5. Datatype and Container Traits

```
trait Datatype {
    type View<S: Storage>;
}

trait Container: Datatype {
    type Store: DynamicStorage;
    type ViewInStore;
    // bridge methods (current workaround for limited static dispatch on
    // associated-type receivers)
    fn new_store() -> Self::Store;
    fn drop_store(s);
    fn store_len(s) -> u64;
    fn store_push(s, v: Self::ViewInStore);
    fn store_get(s, idx) -> Self::ViewInStore;
}
```

- `Datatype` is pure: it maps a storage to a view via the GAT `View<S>`.
- `Container` pairs a datatype with its canonical container-storage used by
  `Buffer<DT>`.  `ViewInStore` is the monomorphic `View<Store>`.

`Datatype` may be implemented for a genos (trait-for-genos, shared across all
of its eide) or directly for an eidos (when only one eidos exists for the
genos, as is currently the case for everything in Hermes).

## 6. UnsizedPayload — Shape for "Fixed Meta + Variable Tail"

Marker trait for the class of eide that share the
"small metadata + variable-length tail" shape (HermesString, BigInteger,
BigDecimal, VarBinary, ObjectArray):

```
trait UnsizedPayload {
    type Meta;  // () for strings/bignums, DecMeta for Decimal, …
    type Atom;  // u8 for strings/digits, u32 for bigint limbs, AnyVal for array slots
}
```

A future blanket `impl<DT: UnsizedPayload> Container for DT` will provide a
generic SoA backing store (`Vec<Meta>` + offsets + `Vec<Atom>`).

## 7. Buffer<DT>

```
struct Buffer<DT: Container> { storage: DT::Store }
```

Wrapper owning one container-store per datatype.  Operations delegate through
the Container bridge; drop releases via `DT::drop_store`.  Views returned to
the caller have type `DT::ViewInStore`.

## 8. Zone Representation

Zone-allocated eide carry a **TypeTag** prefix (variable-length 1–8 byte
encoding, see `hermes-wire-format.md` §2).  The TypeTag's low bits encode the
genos `type_code`; tag system infers the eidos from the registration table.
Relative offsets replace pointers so zone memory relocates without fix-ups.

## 9. Invariants

- **One eidos per (genos, tag-system).** The dispatch table has no
  ambiguity; registering a second eidos for the same (genos, tag-system) is
  a link-time or tag-dispatch-codegen error.
- **Stable genos identity.** `type_code` of a genos is fixed across all tag
  systems.  Changing it is a wire-compat-breaking change for every format
  using that genos.
- **View-Store consistency.** For any `DT: Container`,
  `DT::ViewInStore ≡ DT::View<DT::Store>`.  Enforced by convention; the
  compiler does not yet unify GAT-instantiated assoc types with their
  declared monomorphic aliases.
- **View is cheap.** Constructing a View from a Storage element is O(1) and
  allocation-free.  Heavy decoding lives in explicit operations, not in view
  construction.
- **Storage owns lifetime.** All memory held by a store is released by
  `drop_store`.  Views never outlive their store; enforced via normal
  lifetimes.
- **Relocatable eide.** Zone-allocated eide use zone-relative offsets; copying
  the zone does not invalidate any internal reference.
- **Struct types are process-local.** `struct` and its instances do not cross
  process/accelerator boundaries.  Only eide (via their canonical byte
  layout) do.

## 10. Extension Points

Adding a new Hermes datatype:

1. Decide the **genos**: its `type_code`, name, mandatory operations (trait
   contract).
2. For each tag system where it should exist, declare an **eidos**:
   - define the zone / wire byte layout (struct with `pub` fields; relative
     offsets for any inner references),
   - mark it `pub eidos Name { … }` with the genos membership,
   - the tag-dispatch codegen registers it under the genos's `type_code`.
3. Define a **View** struct (usually `ptr + len` or a small POD).
4. `impl Datatype`: `type View<S: Storage> = MyView;`.
5. If the datatype should be usable in `Buffer<DT>`:
   - define a storage struct (`struct`), `impl Storage / OwningStorage /
     DynamicStorage` for it,
   - `impl Container` with `Store`, `ViewInStore`, bridge methods.
6. When the `UnsizedPayload`-blanket lands, steps 5 collapse to:
   `impl UnsizedPayload for MyEidos { type Meta = …; type Atom = …; }`.

## 11. Current State & Known Gaps

- **No `genos` keyword yet.** The concept is articulated here and will guide
  the grammar extension once the trait-level concept survives a few more
  Hermes iterations.  In the current code base, genus-role is carried by
  trait names (`Varchar` would be `trait Varchar`) and `#[type_code=N]` still
  lives on the eidos declaration.  Migration pending.
- **`DT::Store::storage_new()`** (static call on assoc-type receiver) is a
  parse/resolution gap.  Container's bridge methods are the workaround.
- **`View<S>` comparison** ignores `gat_args` in `types_equal`;
  `Container::ViewInStore` sidesteps by keeping comparison on a plain assoc
  type.  A future sema pass should make GAT-instantiation comparisons
  structural.
- **No blanket `impl<DT: UnsizedPayload> Container for DT`** yet — requires
  the monomorphizer to thread nested assoc types (`DT::Meta` inside
  `Vec<DT::Meta>`) through trait resolution.
- **Tag dispatch tables** currently key on eidos names; they will migrate to
  key on `(tag-system, type_code)` once the genos layer is explicit in the
  source language.

## 12. References

- `stdlib/logos/lang/datatypes.logos` — core traits & blankets.
- `stdlib/hermes/string.logos`, `scalar.logos` — exemplar eide.
- `openspec/specs/hermes.md`, `hermes-wire-format.md` — Hermes-specific
  representation rules.
- `openspec/specs/type-system.md` — underlying Logos memory model.
