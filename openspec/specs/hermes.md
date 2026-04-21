# Hermes: Detailed Port Specification

This document is the authoritative specification for porting Hermes from Memoria to Logos. It covers the complete internal architecture, data layout, algorithms, API surface, parser, serialization, and tools.

**Related specifications:**
- `hermes-abi.json` — Machine-readable type registry: all DataTypes, their type hashes, binary layouts, embeddability rules, and encoding properties. **The** authoritative source for type code assignments.
- `hermes-wire-format.md` — Formal binary wire format: bit-exact encoding of ShortTypeCode, AnyVal, arena layout, container structures, and cross-runtime interoperability requirements. **The** authoritative source for binary stability guarantees.

## 1. Memory Model

### 1.1 Arena Allocator
Hermes objects live in an arena -- a contiguous (or chunked) memory region with bump-pointer allocation.

**Two allocation modes:**
- `GROWABLE_SINGLE_CHUNK`: single contiguous buffer, doubled on overflow. Used for immutable/compacted documents.
- `MULTI_CHUNK`: new chunks allocated as needed (default 4KB). Used for mutable documents under construction.

**Allocation mechanics:**
- `allocate(size, alignment, tag)`: finds room for `size` bytes at `alignment`, ensuring space *before* the returned address for the `TypeTag` bytes. Tag is written backwards from the object start. If the alignment gap >= tag size, the tag fits at zero extra cost.
- `allocate_raw(size, alignment)`: no type tag (used for untagged structures like `DocumentHeader`).
- Objects may have gaps between them (alignment padding).

**Chunk structure:**
```cpp
struct Chunk {
    std::unique_ptr<uint8_t[]> memory;  // owned buffer
    size_t capacity;                    // allocated capacity
    size_t used;                        // used bytes
};
```

Arena owns a `vector<Chunk>`. The tail chunk contains `DocumentHeader` at offset 0. The current chunk (head) is where new allocations go.

Some types have dynamic size (e.g. `ArenaString`), where the object size depends on runtime data (e.g. string length). The arena's `allocate()` method takes a `TypeTag` and writes it before the object address.

**Deep Copy:**
`Document::deep_copy()` allocates a new arena, traverses the object graph, and copies each reachable object. Since all offsets are segment-relative, pointer fixup is straightforward: subtract the old base, add the new base. Result is a clean, compacted single-chunk arena.

**Thread-local arena pool:** planned for future; not currently implemented.

### 1.2 Relative Pointers

All in-arena pointers store **segment-relative offsets** — offsets from the beginning of the arena segment. Dereference always requires the segment base address to be passed explicitly. This model is stable across realloc (the offset doesn't change when the segment moves) and simpler than self-relative pointers.

**`RelativePtr<T>`:** holds `arena_offset_t` (a 32-bit unsigned offset). `get(base)` returns `base + offset`. Null = `NULL_OFFSET` sentinel.

**`AnyVal`:** 8 bytes. Can store *either* a segment-relative pointer *or* an embedded small value. Previously called `EmbeddingRelativePtr`/`ERelativePtr` in Memoria; renamed for clarity.
- Discriminant: bit 0 of byte[7] (most-significant byte in little-endian layout): `0` = pointer, `1` = value.
- **Pointer mode:** 32-bit segment-relative offset in bytes[0..3]; bytes[4..7] = 0. High bytes being zero ensures the discriminant bit is 0 without any encoding tricks.
- **Value mode:** bytes[0..6] = value (up to 7 bytes), byte[7] = `(type_hash << 1) | 1`. Values ≤ 7 bytes can be embedded (integers, floats, bool).

See `hermes-wire-format.md §3` for the exact bit-level encoding.

### 1.3 Document Structure
```
DocumentHeader {
    AnyVal root;  // root object of the document (pointer mode)
}
```
Header is allocated untagged at offset 0 of the arena segment. `root` points to the top-level Hermes object via a segment-relative offset.

## 2. Type System

### 2.1 Type Tags
Every arena-allocated object has a type tag stored *before* its address. Tags are 1-8 bytes (encoded in `ShortTypeCode`).

```
ShortTypeCode (uint64_t):
  bits [0:2]   = code_len (0-7, number of additional bytes beyond first)
  bits [3:7]   = descriptor (5 bits, type-specific flags)
  bits [8:63]  = type code (56 bits)
```

`full_code_len()` = `code_len + 1` (1-8 bytes total).

Reading a tag: start from the byte before the object, read `(byte & 0x7) + 1` bytes total, reassemble into `ShortTypeCode`.

Common types (BigInt, Varchar, etc.) have small codes (< 256) → 2-byte tags → typically fit in alignment gaps (most objects align to 2+ bytes) → zero overhead.

### 2.2 Core Datatypes

See `hermes-abi.json` for the complete machine-readable type registry. Summary:

| Logos Type   | C++ View        | TypeHash | Tag Size |
|-------------|-----------------|----------|----------|
| TinyInt     | int8_t          | 20       | 2 bytes  |
| UTinyInt    | uint8_t         | 21       | 2 bytes  |
| SmallInt    | int16_t         | 22       | 2 bytes  |
| USmallInt   | uint16_t        | 24       | 2 bytes  |
| Integer     | int32_t         | 23       | 2 bytes  |
| UInteger    | uint32_t        | 25       | 2 bytes  |
| BigInt      | int64_t         | 26       | 2 bytes  |
| UBigInt     | uint64_t        | 27       | 2 bytes  |
| Real        | float           | 30       | 2 bytes  |
| Double      | double          | 31       | 2 bytes  |
| Boolean     | bool            | 37       | 2 bytes  |
| Varchar     | U8StringView    | 28       | 2 bytes  |
| Hermes      | Hermes       | 98       | 2 bytes  |
| Uid256      | UID256          | 40       | 2 bytes  |
| Uid64       | uint64_t        | 42       | 2 bytes  |

Fixed-size types (integers, floats, bool) can be embedded in `AnyVal` when `TypeTraits<T>::embeddable == true` (type_hash < 128, sizeof(T) < 8).

### 2.3 Datatypes with Constructors
`Datatype` = name (Varchar) + optional type parameters (ObjectArray) + optional constructor arguments (ObjectArray) + C++ type extras (const/volatile/pointer/ref qualifiers).

Example: `Decimal(10, 2)` → name="Decimal", constructor=[10, 2].
Example: `Array<Int>` → name="Array", parameters=[Datatype("Int")].

`DatatypeData` struct in arena:
```
struct DatatypeData {
    RelativePtr<ArenaString> name;      // type name
    RelativePtr<ObjectArrayData> params; // type parameters (nullable)
    RelativePtr<ObjectArrayData> ctr;    // constructor args (nullable)
    CxxTypeExtras extras;               // const/volatile/ptr/ref
};
```

Type identity: SHA256 hash of normalized C++ type declaration → `UID256`. Used for type registry lookup.

### 2.4 Type Traits System

Compile-time type metadata is expressed through `TypeTraits<T>` specializations (in `type_registry.hpp`):

```cpp
template <> struct TypeTraits<int32_t> {
    static constexpr uint64_t hash = type_hash::Integer;  // 23
    static constexpr bool fixed_size = true;
    static constexpr bool embeddable = true;
    static constexpr TagDescriptor descriptor = TagDescriptor::Data;
};
```

Convenience helpers:
- `type_tag_for<T>()` → `TypeTag` for arena allocation
- `is_embeddable<T>()` → bool, true if T fits in AnyVal value mode

There is no runtime polymorphic `TypeReflection` registry. Type identity at runtime is handled by reading the `TypeTag` from the arena bytes before each object.

## 3. Container Types

### 3.1 TinyObjectMap (`Map<uint8_t, AnyVal>`)
The workhorse for structured objects (M-Code entities, template AST nodes, etc.).

**Layout:**
```
struct {
    uint64_t header_;           // bitmap + size + capacity
    RelativePtr<Value> data_;   // pointer to value array
};
```

`header_` layout (64 bits):
- bits [0:51] = key bitmap (which keys 0..51 are present)
- bits [52:57] = capacity (6 bits, max 52)
- bits [58:63] = size (6 bits, max 52)

**Lookup:** `PopCnt(header & mask_below_key)` gives position in value array. O(1).

**Values:** `AnyVal` (8 bytes each) — either pointer to arena object or embedded small value.

Total overhead: 16 bytes (header + data pointer). This is minimal for a dynamic key-value structure.

### 3.2 ObjectArray (`Vector<AnyVal>`)
Dynamic array of heterogeneous objects.

```
struct ObjectArrayData {
    uint64_t size_;
    uint64_t capacity_;
    RelativePtr<AnyVal> data_;
};
```

Standard dynamic array semantics. Each element is an `AnyVal` (pointer or embedded value).

### 3.3 TypedArray (`Array<T>` / `Vector<T>`)
Homogeneous arrays.

`Array<T>`: inline storage (flexible array member `T array_[1]`), fixed after allocation. `UseObjectSize` pattern.
`Vector<T>`: separate data buffer via `RelativePtr<T>`, dynamically resizable.

### 3.4 ObjectMap (`Map<RelativePtr<ArenaString>, AnyVal>`)
String-keyed hash map.

```
struct Map<Key, Value> {
    uint64_t size_;
    uint64_t buckets_capacity_;    // log2 of bucket array size
    RelativePtr<BucketRelPtr> buckets_;
};
```

Buckets are open-addressing chains. Each bucket stores keys and values in parallel arrays (SoA layout for cache efficiency). Hash function: FNV-1a.

### 3.5 ArenaString (`ArenaDataTypeContainer<Varchar>`)
Variable-length UTF-8 string.

```
[vlen-encoded length][raw UTF-8 bytes]
```

Length encoded with `u64_56_vlen` (1-8 bytes, optimized for short strings). No null terminator. String content is inline (flexible array member pattern).

## 4. View Layer

Every arena object has a corresponding *View* class providing the public API.

**`ViewBase`:** all typed views inherit from this. Stores `arena_offset_t offset_` + `MemHolder* holder_` (12 bytes, non-owning). Access to the object: `base() + offset_` → raw pointer. Access to the arena: `holder_->arena()`.

**Typed views:**
- `TinyMapView` — accesses `TinyObjectMap` via `get(key)`, `put(key, val)`, etc.
- `ArrayView` — accesses `ObjectArray` via `get(index)`, `push_back(val)`, etc.
- `MapView` — accesses `ObjectMap` via `get(key)`, `put(key, val)`, `for_each(fn)`.
- `StringView` — accesses `ArenaString`, returns `std::string_view`.
- `DatatypeView` — accesses `DatatypeData`, returns name/params/ctr.
- `ParameterView` — accesses `ParameterData`, returns parameter name.
- `ObjectView` — universal view wrapping an `AnyVal`. Dispatches to concrete type.

**`MemHolder`:** owns the `Arena` (via `std::shared_ptr<Arena>`) and provides the segment base pointer. `MemHolder::base()` → `uint8_t*`.

**Ownership:**
- Non-owning views (`TinyMapView`, `ArrayView`, etc.) are cheap (12 bytes). The caller must ensure the `MemHolder` outlives the view.
- `Own<View>` wraps a view with shared ownership of its `MemHolder`. Provides RAII lifetime management. Use `Own<TinyMapView>`, `Own<ArrayView>`, etc. for returned values that outlive the calling scope.

**Cross-arena operations:** `put(key, ObjectView)` / `push_back(ObjectView)` detect cross-arena writes (different `MemHolder`) and deep-copy the value into the target arena automatically.

## 5. Parser

### 5.1 Technology

The Hermes text parser is a hand-written recursive descent parser (`src/hermes/text_parser.cpp`, ~940 lines). No external parsing library is used. The canonical grammar is defined in `tools/peg_gen/grammars/hermes.peg` (PEG format), which also serves as the machine-readable source for generating a parser via `peg_gen`.

### 5.2 Grammar Summary

```
value        := typed_value / map / array / string / float / integer /
                "true" / "false" / "null"
map          := '{' (map_entry (',' map_entry)*)? ','? '}'
map_entry    := (string | identifier) ':' value
array        := '[' (value (',' value)*)? ','? ']'
typed_value  := datatype '(' value ')'
datatype     := IDENT ('<' (datatype (',' datatype)*)? '>')?
```

Integer literals: optional `-` sign, then `0x…` (hex), `0b…` (binary), `0o…` (octal), or decimal digits. Optional type suffix: `_u8`, `_u16`, `_u32`, `_u64`, `_s8`, `_s16`, `_s32`, `_s64`, `ull`, `ul`, `ll`, `u`.

Float literals: optional `-` sign, decimal digits, `.`, decimal digits, optional exponent `e±…`. Optional suffix: `f` (float32), `d` (float64).

Whitespace and comments are skipped: `//` line comments, `/* */` block comments.

### 5.3 Text Serialization (Stringify)
`stringify(doc, stream)` / `stringify(doc, pretty=true)` in `src/hermes/stringify.cpp`. Produces compact or indented JSON-like text that round-trips through the parser.

## 6. HermesPath

JMESPath-inspired query language for navigating Hermes documents.

**AST:** Parsed into Hermes TinyObjectMap nodes (code = data pattern). AST codes defined in `TplASTCodes`.

**Evaluation:** `HermesASTInterpreter` — visitor pattern over AST nodes. Supports: identifiers, sub-expressions, index expressions, array items, bracket specifiers, comparisons, logical operators (and/or/not), functions, multi-select (hash/list), pipe expressions, flatten, filter, slice.

**Integration:** `Object::search(query)` parses and evaluates a HermesPath expression against the object.

## 7. Template Engine

Jinja-like syntax: `{{ expr }}` for output, `{% for/if/set/elif/else/endif/endfor %}` for control flow.

**Parsing:** Hand-written recursive descent parser (`src/hermes/template.cpp`). Produces template AST as Hermes `TinyObjectMap` nodes. Expressions use the HermesPath sub-grammar.

**Rendering:** `TplRenderer` walks AST, evaluates expressions via the HermesPath interpreter, maintains variable stack (`TplVarStack`), writes to output stream.

**Whitespace control:** `{%- -%}` strips whitespace, `{%+ +%}` preserves. Default: strip empty first/last lines around blocks.

## 8. Schema Processor

`CheckStructureState` validates arena integrity:
- Allocation bitmap: no overlapping allocations
- Cycle detection bitmap: no infinite loops in object graph
- Bounds checking: all pointers within arena
- Type tag verification: tag matches expected type
- Size/capacity invariants for all containers

Schema enforcement beyond structural validation is extensible (language-server-like interactive mode planned).

## 9. Serialization Formats

### 9.1 Zero-Copy
Arena memory segment externalized as raw bytes. Relocatable (relative pointers). Integrity checkable. Fastest format.

### 9.2 Text
Human-readable. Round-trip preserving (parse → stringify → parse). Grammar described in §5.2.

### 9.3 Binary
Dense encoding. `SerializationState` / `DeserializationState` handle traversal and reconstruction. Faster than text, denser than zero-copy. Details in serialization.hpp.

## 10. Profiles

| Profile | Features |
|---------|----------|
| pico    | Fixed-size arrays, TinyObjectMap, Int56, strings |
| nano    | + Int56→Object map |
| micro   | + all integer/float types, semantic graph types |
| basic   | + dynamic (growable) containers |

Profiles are compile-time configuration selecting which types and containers are available. Reduces binary size for constrained environments.

## 11. Implementation Notes

### What Was Ported vs. Reimplemented
The Logos Hermes implementation is a **clean-room rewrite**, not a port of Memoria code. Memoria served as a reference for algorithms and data layouts (especially wire format), but the C++ code is new:
- No Boost dependencies (Spirit, Phoenix, Fusion, Regex)
- No ICU dependency
- No Memoria template metaprogramming patterns
- No `ska::flat_hash_map` — ObjectMap uses a simple open-addressing scheme
- No `shared_ptr` in the arena itself — `MemHolder` with manual lifetime management

### C++23 Standard Library Only
Hermes uses only the C++23 standard library (`std::string_view`, `std::format`, `std::span`, `std::shared_ptr` for `MemHolder`) plus SQLite (via `logos_verification`). No other external dependencies.

### Namespace
All types live in `logos::hermes::`. The `logos::hermes::type_hash::` sub-namespace holds compile-time type hash constants.

## 12. Binary Stability & Cross-Runtime Interop

See `hermes-wire-format.md` for the full formal specification.

### 12.1 ABI Documents

- **`hermes-abi.json`**: Machine-readable registry of all data types. Intended for code generation — a binding generator for Java/JS/Python/Rust can read this file and produce typed accessors, serializers, and deserializers automatically.
- **`hermes-wire-format.md`**: Bit-exact specification of all binary encodings. Frozen interface for binary stability.

### 12.2 Cross-Runtime Strategy

**ARC runtimes** (C++, Rust, CPython, Swift): Full native arena implementation. Zero-copy memory mapping. Direct AnyVal access via segment base + offset.

**Tracing-GC runtimes** (Java, JavaScript, Go): Hermes Wire Codec — a lightweight serialization/deserialization layer that converts between Hermes binary format and native objects. No arena allocation needed in the target runtime. Schema-driven code generation from `hermes-abi.json` for typed access patterns.

## 13. Known Gaps and Future Work

1. **Schema processor:** `CheckStructureState` — structural validation of arena integrity (allocation bitmap, cycle detection, bounds checking) — is not yet implemented.

2. **Profiles:** Compile-time feature selection (pico/nano/micro/basic) for constrained environments is not implemented.

3. **Thread-local arena pools:** Not implemented. For green-fiber world (Phase 1B reactor), pools should be fiber-local or per-reactor.

4. **Fuzz testing:** Arena and container operations have no fuzzing harness yet.

5. **Extended numeric types:** Varbinary, BigDecimal, Decimal, TimestampWithTZ, TimeWithTZ defined in `hermes-abi.json` but not implemented in TypeTraits or containers.
