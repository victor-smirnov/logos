# Logos Datatypes — View/Storage Architecture Specification

This document specifies the `Datatype` system in `stdlib/logos/lang/datatypes.logos`
and its Hermes implementations. Status: **living spec**, edited as the
architecture evolves. Pair with `docs/datatypes.md` for the tutorial view
and with the stdlib module header for inline context.

## 1. Core Principle — Representation vs Processing

A Logos *Datatype* is the bridge between two separate concerns:

1. **Representation** — how the bytes are laid out in memory. Must be
   stable, relocatable via zone-relative offsets, and shareable across
   processes and compute accelerators without transformation.
2. **Processing** — the value as seen by the code reading it
   (`StringView`, `DecimalView`, `i64`, …). Lightweight projection built
   on demand from a Storage.

Separating these is what enables zone-allocated Hermes objects to be
shared with no serialization cost, while the processing code works with
natural value types.

## 2. Trait Hierarchy

### 2.1 Storage Hierarchy

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

Three orthogonal axes (owns vs borrowed, grows vs fixed, read-only vs
mutable) collapse to a linear hierarchy:

| Trait              | Owns? | Grows? | Mutation? | Example backing     |
|--------------------|-------|--------|-----------|---------------------|
| `Storage`          | any   | any    | read-only | *base capability*   |
| `OwningStorage`    | yes   | any    | read-only | fixed zone slab     |
| `DynamicStorage`   | yes   | yes    | push      | `PrimVec<T>`        |
| `BorrowedStorage`  | no    | no     | read-only | mmap slice, zone view|

`DynamicStorage: OwningStorage` because a growing backing must take
responsibility for freeing its buffer.

### 2.2 Datatype and Container

```
trait Datatype {
    type View<S: Storage>;
}

trait Container: Datatype {
    type Store: DynamicStorage;
    type ViewInStore;
    // bridge to Store's DynamicStorage methods (current workaround for
    // limited static dispatch on associated-type receivers):
    fn new_store() -> Self::Store;
    fn drop_store(s);
    fn store_len(s) -> u64;
    fn store_push(s, v: Self::ViewInStore);
    fn store_get(s, idx) -> Self::ViewInStore;
}
```

- `Datatype` is *pure* — it declares one thing: how to derive a View
  from a Storage. GAT `View<S>` keeps the [Storage × View] matrix open.
- `Container` pairs a Datatype with its **canonical container-store**
  (used by `Buffer<DT>`). `ViewInStore` is the monomorphic
  `View<Store>`; declaring it explicitly avoids a current type-equality
  limitation in generic code that uses GAT-instantiated assoc types.

### 2.3 UnsizedPayload

Marker trait for the class of datatypes with the shape *fixed meta +
variable-length tail* (HermesString, BigInteger, BigDecimal, VarBinary,
ObjectArray…):

```
trait UnsizedPayload {
    type Meta;   // () for strings/bignums, DecMeta for Decimal, …
    type Atom;   // u8 for strings/digits, u32 for bigint limbs, AnyVal for array
}
```

A future blanket `impl<DT: UnsizedPayload> Container for DT` will
provide a generic SoA backing store (`Vec<Meta>` + offsets +
`Vec<Atom>`) so each such datatype needs only two associated-type
declarations to gain a `Buffer<DT>`.

## 3. Buffer<DT>

```
struct Buffer<DT: Container> { storage: DT::Store }
```

Wrapper owning one container-store per datatype. All operations delegate
through the Container bridge:

- `Buffer::<DT>::new()` → `DT::new_store()`
- `buf.len()`           → `DT::store_len(&buf.storage)`
- `buf.push(v)`         → `DT::store_push(&mut buf.storage, v)` with `v: DT::ViewInStore`
- `buf.get(i)`          → `DT::store_get(&buf.storage, i)`
- `drop`                → `DT::drop_store(&mut buf.storage)`

## 4. Zone representation (Hermes)

Zone-allocated Datatypes carry a **TypeTag** prefix (variable-length 1-8
byte encoding, see `hermes-wire-format.md` §2). Relative offsets replace
pointers so zone memory relocates without fix-ups.

Zone and `Buffer<DT>` are two concrete *storages* for the same Datatype;
the View returned by either is structurally identical (e.g. both give a
`StringView`) so user code remains uniform.

## 5. Invariants

- **View-Store consistency.** For any `DT: Container`,
  `DT::ViewInStore ≡ DT::View<DT::Store>`. Enforced by convention; the
  compiler does not yet unify GAT-instantiated assoc types with their
  declared monomorphic aliases.
- **View is cheap.** Constructing a View from a Storage element is O(1)
  and allocation-free. It may be a pointer+length pair, a primitive, or
  a small POD. Expensive decoding belongs elsewhere.
- **Storage owns lifetime.** All memory held by a Store is released by
  `drop_store`. Views never outlive their Store — the borrow checker
  enforces this via normal lifetime rules.
- **Relocatable representation.** Zone-allocated Datatypes use
  zone-relative offsets; copying the zone does not invalidate any
  internal reference.

## 6. Extension Points

Adding a new Hermes datatype:

1. Define the zone layout (struct with `pub` fields; relative offsets
   for any inner references).
2. Annotate with `#[type_code=N]` — see `hermes-wire-format.md`.
3. Define a View struct (usually `ptr + len` or a plain POD).
4. `impl Datatype`:  `type View<S: Storage> = MyView;`
5. If the datatype should be usable in `Buffer<DT>`:
   - Define a Storage struct, `impl Storage/OwningStorage/DynamicStorage`.
   - `impl Container` with `Store`, `ViewInStore`, bridge methods.
6. When `UnsizedPayload`-blanket lands: `impl UnsizedPayload for MyType`
   with `Meta`/`Atom` replaces steps 5 entirely.

## 7. Known Limitations

- `DT::Store::storage_new()` (static call on assoc-type receiver) is a
  parse/resolution gap. Bridge methods on Container are the current
  workaround.
- `View<S>` comparison through `types_equal` ignores `gat_args`. `Container::ViewInStore` sidesteps by keeping the comparison on a
  plain assoc type. A future pass should make GAT-instantiation
  comparisons structural.
- No blanket `impl<DT: UnsizedPayload> Container for DT` yet — requires
  the monomorphizer to thread nested assoc types (`DT::Meta` inside
  `Vec<DT::Meta>`) through trait resolution.

## 8. References

- `stdlib/logos/lang/datatypes.logos` — core traits & blankets.
- `stdlib/hermes/string.logos`, `scalar.logos` — exemplar impls.
- `openspec/specs/hermes.md`, `hermes-wire-format.md` — Hermes-specific
  representation rules.
- `openspec/specs/type-system.md` — underlying Logos memory model.
