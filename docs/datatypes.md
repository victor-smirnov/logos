# Datatypes in Logos — a guided tour

This tutorial walks through the data-representation layers in Logos.  For the
formal rules, see `openspec/specs/datatypes.md`.

## The four layers

Logos leans on Greek/Platonic vocabulary to pair with the language name λόγος:

1. **`genos`** — the *logical datatype*.  "What the data means."
   `Varchar`, `Integer`, `Decimal`, `Uuid`.  Stable semantic identity, carries
   the `type_code`.
2. **`eidos`** — a *canonical byte layout* of a genos.  "How the data looks
   on disk / in a zone."  Relocatable, shareable across processes and
   accelerators.
3. **`struct`** — regular in-process Logos type.  Includes alternate storages
   (`HermesStringStorage` for SoA columns), view types, computational
   mirrors.  Does not cross process boundaries.
4. **view** — a lightweight processing handle (`StringView = ptr + len`)
   built on demand from some storage.  What user code actually computes on.

A genos can have several eide, but only **one per tag system**: inside a
given representation context (zone, wire format, …) the logical type maps to
a single concrete layout.  Different tag systems can choose different eide
for the same genos.

## Storage ladder

```
Storage          — len + indexed read
  ├─ OwningStorage      — + drop
  │    └─ DynamicStorage — + new + push + clear   (Vec-like)
  └─ BorrowedStorage     — read-only slice
```

Pick the narrowest your code needs.  A function that only reads takes
`S: Storage`; a push-only accumulator wants `S: DynamicStorage`.

## Datatype ↔ Storage bridge

```
trait Datatype {
    type View<S: Storage>;          // how to see element i of storage S
}

trait Container: Datatype {
    type Store: DynamicStorage;     // canonical backing
    type ViewInStore;               // = View<Store>
    // thin bridge methods used by Buffer<DT>
}
```

Most user code interacts with `Container`, because that's what `Buffer<DT>`
requires.  `Datatype` is the pure abstract layer; `Container` pairs it with
its default container-storage.

## `Buffer<DT>`

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

`Buffer` doesn't care whether your type is `Primitive` (plain POD) or an
unsized beast — it picks the right `Store` through the `Container` bridge.

## Two blanket shortcuts

### `Primitive` — one-liner for plain PODs

```logos
impl Primitive for I64 { type Prim = i64; }
// I64 now gets Datatype + Container + Buffer<I64> automatically.
```

A *Primitive* datatype is one where storing and viewing are identical types —
`Buffer<I64>` is literally a `PrimVec<i64>` under the hood.

### `UnsizedPayload` — two lines for variable-tail types

The class of "fixed meta + variable-length tail" eide (strings, bignums,
decimals, blobs, arrays) shares a SoA backing shape:
`Vec<Meta>` + offsets + `Vec<Atom>`.  A future blanket will collapse this to:

```logos
impl UnsizedPayload for HermesString { type Meta = ();      type Atom = u8; }
impl UnsizedPayload for BigInteger   { type Meta = ();      type Atom = u32; }
impl UnsizedPayload for BigDecimal   { type Meta = DecMeta; type Atom = u32; }
```

(The blanket itself is still pending monomorphizer work — for now each such
eidos spells its storage out by hand.)

## Adding a new Hermes datatype — the checklist

Suppose you want a `Uuid` genos (fixed 16 bytes):

1. Fix the **genos**: name, `type_code`, trait contract
   (`len() -> 16`, `bytes() -> *const u8`, compare, hash).

2. Choose an **eidos** for each tag system where `Uuid` should exist.  For
   now that's just the zone tag system:

    ```logos
    #[type_code = 42]
    pub eidos Uuid { pub bytes: [u8; 16] }

    impl Uuid { /* zone-side methods: init, bytes, equals, … */ }
    ```

3. Define a **view** for processing:

    ```logos
    pub struct UuidView { pub bytes: [u8; 16] }
    impl Copy for UuidView {}
    ```

4. Hook up `Datatype` + `Container`:

    ```logos
    impl Datatype  for Uuid { type View<S: Storage> = UuidView; }
    impl Container for Uuid { /* Store = PrimVec<UuidView>, bridge methods */ }
    ```

Once the `UnsizedPayload`-blanket lands, a variable-tail eidos like
`HermesString` will shrink to:

```logos
impl UnsizedPayload for HermesString { type Meta = (); type Atom = u8; }
```

## Current state of the vocabulary in the source

The concepts described here are stable; the Logos grammar has not yet grown a
dedicated `genos` keyword.  At the source level:

- `#[type_code=N]` still lives on the `eidos` declaration (migration
  pending).
- The genus-role is carried by trait names (a trait `Varchar` unifying all
  eide of that logical type).  When the `genos` keyword lands, such traits
  become `pub genos Varchar { … }`.
- Tag-dispatch tables key on eidos names today; they'll migrate to
  `(tag-system, type_code)` once genos is a first-class surface concept.

## Where to read next

- `openspec/specs/datatypes.md` — invariants, known limitations, every rule
  made explicit.
- `openspec/specs/hermes-wire-format.md` — byte-level representation,
  TypeTag encoding, AnyVal slots.
- `stdlib/hermes/string.logos` — the fully-worked-out example (HermesString
  as an eidos of the Varchar genos).
- `stdlib/logos/lang/datatypes.logos` — the traits themselves.
