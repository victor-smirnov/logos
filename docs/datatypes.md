# Datatypes in Logos — a guided tour

This tutorial shows how the Datatype / Storage / View system works and
how to add a new Hermes datatype of your own. For the formal rules see
`openspec/specs/datatypes.md`.

## Why three concepts?

A Logos **Datatype** is never a single thing. It is always a trio:

1. **Representation** — the bytes on disk / in a zone. Must be stable:
   you can `memcpy` it to shared memory and another core reads it with
   no translation.
2. **View** — what your code actually talks to. A lightweight handle
   (usually `ptr + len`, or a primitive). Created on demand.
3. **Storage** — the thing that holds the bytes. Vec-like heap buffer?
   A slice of a Hermes zone? An mmap'd read-only view? Same Datatype,
   same View, different backing.

The same `HermesString` datatype lives in at least two storages (zone
slab and SoA `Buffer<HermesString>`); both produce the same `StringView`
to the caller.

## The storage ladder

```
Storage          — len + indexed read
  ├─ OwningStorage      — + drop
  │    └─ DynamicStorage — + new + push + clear     (Vec-like)
  └─ BorrowedStorage     — read-only slice
```

Pick the narrowest trait your code needs. A function that only reads
should take `S: Storage`. A push-only accumulator needs
`S: DynamicStorage`.

## The Datatype ↔ Storage bridge

```
trait Datatype {
    type View<S: Storage>;          // how to see element i of storage S
}

trait Container: Datatype {
    type Store: DynamicStorage;     // my canonical backing
    type ViewInStore;               // = View<Store>
    // bridge methods used by Buffer<DT>
}
```

You don't use `Datatype` directly very often — most of the time you
reach for `Container` because you want a `Buffer<DT>`. The
`ViewInStore` assoc type is a convenience: Buffer works in monomorphic
view-terms without tripping on GAT instantiation.

## The `Buffer<DT>` container

```logos
let mut b: Buffer<I64> = Buffer::<I64>::new();
b.push(42);
b.push(-7);
let x = b.get(0);       // 42
```

For strings:

```logos
let mut b: Buffer<HermesString> = Buffer::<HermesString>::new();
b.push(StringView { ptr: ..., len: 5 });
let v = b.get(0);       // StringView into packed bytes
```

`Buffer` doesn't care whether your Datatype is `Primitive` (plain POD
element) or a complex unsized beast — it picks the right `Store`
through the `Container` bridge.

## Two blanket shortcuts

### Primitive — one-liner for plain PODs

```logos
impl Primitive for I64 { type Prim = i64; }
// I64 now has Datatype + Container + Buffer<I64> automatically.
```

A *Primitive* Datatype is one where storing and viewing are the same
thing — `Buffer<I64>` is literally a `PrimVec<i64>` under the hood.

### UnsizedPayload — two lines for variable-tail types

The class of "fixed meta + variable-length tail" datatypes (strings,
bignums, decimals, blobs, arrays) shares a SoA backing shape:
`Vec<Meta>` + offsets + `Vec<Atom>`. A future blanket will collapse this
to:

```logos
impl UnsizedPayload for HermesString { type Meta = ();      type Atom = u8; }
impl UnsizedPayload for BigInteger   { type Meta = ();      type Atom = u32; }
impl UnsizedPayload for BigDecimal   { type Meta = DecMeta; type Atom = u32; }
```

For now the interface is declared; implementations still write their
own Storage until the blanket lands.

## Writing a new Hermes datatype — the checklist

Suppose you want to add a `Uuid` datatype (fixed 16 bytes):

```logos
#[type_code = 42]
pub datatype Uuid { pub bytes: [u8; 16] }

impl Uuid { /* zone-side methods: init, bytes, … */ }

// Option A (it fits in AnyVal): nothing more — inline encoding handles it.

// Option B (zone-allocated Primitive-ish), quickest path:
pub struct UuidView { pub bytes: [u8; 16] }
impl Copy for UuidView {}
impl Datatype for Uuid { type View<S: Storage> = UuidView; }
// + Container with PrimVec<UuidView> as Store, bridge methods.
```

Or, once the `UnsizedPayload`-blanket lands:

```logos
impl UnsizedPayload for Uuid { type Meta = (); type Atom = u8; }
// Done. You get Buffer<Uuid> for free.
```

## What to read next

- `openspec/specs/datatypes.md` — invariants, known limitations, every
  rule made explicit.
- `openspec/specs/hermes-wire-format.md` — byte-level representation,
  TypeTag encoding, AnyVal slots.
- `stdlib/hermes/string.logos` — the fully-worked-out example.
- `stdlib/logos/lang/datatypes.logos` — the traits themselves.
