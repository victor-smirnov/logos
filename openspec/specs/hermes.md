# Hermes: Detailed Port Specification

This document is the authoritative specification for porting Hermes from Memoria to Logos. It covers the complete internal architecture, data layout, algorithms, API surface, parser, serialization, and tools.

**Related specifications:**
- `hermes-abi.json` — Machine-readable type registry: all DataTypes, their type hashes, binary layouts, embeddability rules, and encoding properties. **The** authoritative source for type code assignments.
- `hermes-wire-format.md` — Formal binary wire format: bit-exact encoding of ShortTypeCode, ERelativePtr, arena layout, container structures, and cross-runtime interoperability requirements. **The** authoritative source for binary stability guarantees.

## 1. Memory Model

### 1.1 Arena Allocator
Hermes objects live in an arena -- a contiguous (or chunked) memory region with bump-pointer allocation.

**Two allocation modes:**
- `GROWABLE_SINGLE_CHUNK`: single contiguous buffer, doubled on overflow. Used for immutable/compacted documents.
- `MULTI_CHUNK`: new chunks allocated as needed (default 4KB). Used for mutable documents under construction.

**Allocation mechanics:**
- `allocate_space(size, alignment, tag_size)`: finds room for `size` bytes at `alignment`, ensuring `tag_size` bytes of space *before* the object for the type tag.
- Tag is written *backwards* from object start address. If alignment gap >= tag_size, tag fits in the gap at zero extra cost.
- Objects may have gaps between them (alignment).

**Chunk structure:**
```
struct Chunk {
    UniquePtr<uint8_t> memory;  // owned buffer
    size_t capacity;            // allocated capacity
    size_t size;                // used bytes
};
```

Arena owns a `vector<Chunk>`. `head()` = last chunk (current allocation target). `tail()` = first chunk (contains document header).

**Object allocation:**
```cpp
template <typename T, typename... Args>
T* allocate_object(Args&&... args) {
    ShortTypeCode tag = TypeHashV<T>;
    size_t tag_size = tag.full_code_len();
    void* ptr = allocate_space(sizeof(T), alignof(T), tag_size);
    write_type_tag(ptr, tag);
    return new (ptr) T(std::forward<Args>(args)...);
}
```

Some types have dynamic size (`UseObjectSize = true`), where `T::object_size(args...)` determines allocation size instead of `sizeof(T)`.

**Copying GC / Deep Copy:**
Deep copy = allocate new arena, traverse object graph, copy each object into new arena, fix up relative pointers. `DeepCopyState` tracks already-copied objects (deduplication map) to handle shared references. This *is* the GC: compactification creates a clean arena with no garbage.

**Thread-local arena pool:**
`get_local_instance()` provides a thread-local reusable arena for temporary allocations. `PoolableArena` integrates with object pools for efficient reuse.

### 1.2 Relative Pointers
All in-arena pointers are relative (offset from the pointer's own address).

**`RelativePtr<T>`:** 8 bytes. Stores `int64_t offset_`. `get()` returns `this_addr + offset_`. Null = offset 0.

**`EmbeddingRelativePtr<T>` (aka `ERelativePtr`):** 8 bytes. Can store *either* a relative pointer *or* an embedded small value.
- Bit 0 of last byte: 0 = pointer mode, 1 = value mode.
- In pointer mode: 56-bit offset (top byte stores low bits of offset, remaining 7 bytes store rest). Same idea as tagged pointers but at the byte level.
- In value mode: first N bytes = value, last byte = `(tag << 1) | 1`. Values up to 7 bytes can be embedded (56-bit integers, floats, short strings).

This is the key optimization enabling 56-bit integers and small-value embedding without pointer indirection.

### 1.3 Document Structure
```
DocumentHeader {
    ERelativePtr root;  // root object of the document
}
```
Header is allocated untagged at the start of the arena. `root` points to the top-level Hermes object.

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
| Hermes      | HermesCtr       | 98       | 2 bytes  |
| Uid256      | UID256          | 40       | 2 bytes  |
| Uid64       | uint64_t        | 42       | 2 bytes  |

Fixed-size types (integers, floats, bool) can be embedded in `ERelativePtr` when their tag code < 128 and value fits in 7 bytes.

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

### 2.4 Type Reflection System
Global registry mapping `ShortTypeCode` → `TypeReflection` and `UID256` → `TypeReflection`.

`TypeReflection` provides virtual methods for:
- Stringify, deep copy, comparison, equality
- Import/export between documents
- Embedding in `ERelativePtr`
- Structure checking (integrity validation)
- Container wrapping (GenericArray/GenericMap)
- Type conversion

Registration: `register_type_reflection()` at static init time. Every datatype must be registered.

## 3. Container Types

### 3.1 TinyObjectMap (`Map<uint8_t, ERelativePtr>`)
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

**Values:** `ERelativePtr` (8 bytes each) — either pointer to arena object or embedded small value.

Total overhead: 16 bytes (header + data pointer). This is minimal for a dynamic key-value structure.

### 3.2 ObjectArray (`Vector<ERelativePtr>`)
Dynamic array of heterogeneous objects.

```
struct Vector<ERelativePtr> {
    uint64_t size_;
    uint64_t capacity_;
    RelativePtr<ERelativePtr> data_;
};
```

Standard dynamic array semantics. Each element is an `ERelativePtr` (pointer or embedded value).

### 3.3 TypedArray (`Array<T>` / `Vector<T>`)
Homogeneous arrays.

`Array<T>`: inline storage (flexible array member `T array_[1]`), fixed after allocation. `UseObjectSize` pattern.
`Vector<T>`: separate data buffer via `RelativePtr<T>`, dynamically resizable.

### 3.4 ObjectMap (`Map<RelativePtr<ArenaString>, ERelativePtr>`)
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

Every arena object has a corresponding *View* class providing the public API. Views are lightweight (pointer + mem_holder reference).

**Key types:**
- `ObjectView` — universal tagged value holder. Contains `ValueStorage` (union of address, small_value, embedded).
- `HermesCtrView` — document container. Owns or references arena. Factory methods, parsing, serialization.
- `ArrayView<DT>` — typed array view.
- `MapView<KeyDT, ValueDT>` — typed map view.
- `DatatypeView` — datatype declaration view.
- `TypedValueView` — value + type pair.
- `ParameterView` — query parameter placeholder.

**Ownership:** Views hold a non-atomic `LWMemHolder*` reference to the document. The document itself may use atomic reference counting for cross-thread sharing. Within a single thread, views are zero-cost.

**`GenericArray` / `GenericMap`:** Abstract interfaces for type-erased collection access. Implementations (`TypedGenericArray<T>`, `TypedGenericMap<K,V>`) are pooled thread-locally for efficiency.

## 5. Parser

### 5.1 Technology
Boost.Spirit.Qi with Unicode support (`boost::spirit::qi::unicode`). Grammar produces Hermes objects directly during parsing via semantic actions — no intermediate AST.

### 5.2 Grammar Summary

```
document     := type_directory? value
value        := string | number | bool | null | array | map |
                typed_value | type_declaration | parameter | typed_container
string       := quoted_string ('@' type_declaration)?  |  raw_string
number       := integer_literal | float_literal
integer_literal := (hex | bin | oct | dec) suffix?
bool         := "true" | "false"
null         := "null"
array        := '[' (value (',' value)*)? ']'
map          := '{' (map_entry (',' map_entry)*)? '}'
map_entry    := (string | identifier) ':' value
typed_value  := '@' type_declaration '=' value
typed_container := '<' type_param (',' type_param)* '>' (typed_array | typed_map)
type_declaration := datatype_name ('<' type_params '>')? ('(' ctr_args ')')? qualifiers?
parameter    := '?' identifier
type_directory := '#{' (identifier ':' type_declaration (',' ...))? '}'
comment      := '//' ... EOL
```

Integer suffixes: `_u8`, `_u16`, `_u32`/`u`, `_u64`/`ull`/`ul`, `_s8`, `_s16`, `_s32` (default), `_s64`/`ll`.
Float suffixes: `f` (float32), `d` (float64).

### 5.3 Builder Pattern
`HermesCtrBuilder` is a thread-local stateful helper used during parsing:
- Maintains the target `HermesCtr` document
- String deduplication (string_registry_)
- Type directory (type_registry_)
- Nested value construction (reference counting for nested `hermes_value` rules)

### 5.4 Text Serialization (Stringify)
Each type implements `stringify(ostream, DumpFormatState)`. `DumpFormatState` controls indentation, newlines, raw vs quoted strings.

`StringifyCfg` has a `StringifySpec` controlling formatting (compact vs pretty, spaces, newlines).

String escaping: two modes — standard escaped (`"..."`) and raw (`'...'`).

## 6. HermesPath

JMESPath-inspired query language for navigating Hermes documents.

**AST:** Parsed into Hermes TinyObjectMap nodes (code = data pattern). AST codes defined in `TplASTCodes`.

**Evaluation:** `HermesASTInterpreter` — visitor pattern over AST nodes. Supports: identifiers, sub-expressions, index expressions, array items, bracket specifiers, comparisons, logical operators (and/or/not), functions, multi-select (hash/list), pipe expressions, flatten, filter, slice.

**Integration:** `Object::search(query)` parses and evaluates a HermesPath expression against the object.

## 7. Template Engine

Jinja-like syntax: `{{ expr }}` for output, `{% for/if/set/elif/else/endif/endfor %}` for control flow.

**Parsing:** Boost.Spirit grammar produces template AST as Hermes TinyObjectMap nodes. Expressions use HermesPath grammar.

**Rendering:** `TplRenderer` walks AST, evaluates expressions via `HermesASTInterpreter`, maintains variable stack (`TplVarStack`), writes to output stream.

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

## 11. Dependencies (Memoria → Logos Port)

### Must Port
- `core/arena/` — complete (arena, relative_ptr, tiny_map, vector, array, string, map, hash_fn)
- `core/hermes/` — complete (all headers + all lib/*.cpp)
- `core/datatypes/` — core.hpp, traits.hpp, varchars/
- `core/reflection/` — typehash.hpp, reflection.hpp, type_signature.hpp
- `core/strings/` — U8String, U8StringView, format, string_buffer
- `core/memory/` — shared_ptr, malloc, ptr_cast, object_pool
- `core/tools/` — bitmap, bitmap_select, span, result, optional, uid_256, arena_buffer, type_name
- `core/linked/` — linked_hash (FNV hasher)
- `core/flat_map/` — flat_hash_map (ska::flat_hash_map)
- `core/bignum/` — codec implementations for numeric types
- `core/exceptions/` — exception framework

### External Dependencies
- Boost.Spirit.Qi (parser)
- Boost.Phoenix (parser semantic actions)
- Boost.Fusion (struct adaptation)
- Boost.Regex (Unicode iterators for parser)
- ICU (Unicode support, regex)
- fmt (formatting)
- hash-library (SHA256 for type hashes)

### Port Strategy
1. Copy arena/ as-is (minimal changes, core data layout must be wire-compatible)
2. Port hermes/ headers, adapting namespace (`memoria::` → `logos::`)
3. Port hermes/ lib/*.cpp, adapting includes
4. Port required core/ utilities
5. Port parser (keep Boost.Spirit for now, consider replacement later)
6. Port HermesPath
7. Port template engine
8. Write comprehensive tests comparing Logos Hermes output with Memoria reference

## 12. Binary Stability & Cross-Runtime Interop

See `hermes-wire-format.md` for the full formal specification.

### 12.1 ABI Documents

- **`hermes-abi.json`**: Machine-readable registry of all data types. Intended for code generation — a binding generator for Java/JS/Python/Rust can read this file and produce typed accessors, serializers, and deserializers automatically.
- **`hermes-wire-format.md`**: Bit-exact specification of all binary encodings. Frozen interface for binary stability.

### 12.2 Cross-Runtime Strategy

**ARC runtimes** (C++, Rust, CPython, Swift): Full native arena implementation. Zero-copy memory mapping. Direct ERelativePtr access.

**Tracing-GC runtimes** (Java, JavaScript, Go): Hermes Wire Codec — a lightweight serialization/deserialization layer that converts between Hermes binary format and native objects. No arena allocation needed in the target runtime. Schema-driven code generation from `hermes-abi.json` for typed access patterns.

## 13. Known Improvement Opportunities

1. **Parser:** Boost.Spirit is powerful but compile-time heavy and hard to maintain. Consider hand-written recursive descent for Logos. Parser is ~900 lines of grammar — manageable.

2. **Thread-local pools:** Current code uses many `thread_local` pools. In green-fiber world, these should be fiber-local or per-reactor. Needs adaptation for Logos reactor.

3. **String handling:** Multiple string types (U8String, U8StringView, std::string, ArenaString). Simplify in Logos port.

4. **Error handling:** Mix of exceptions and `MEMORIA_MAKE_GENERIC_ERROR`. Standardize in Logos.

5. **Deep copy deduplication:** Uses flat_hash_map for pointer mapping. Could use arena-local structure for better locality.

6. **EmbeddingRelativePtr bit layout:** Current encoding uses byte rotation for offset storage. Thoroughly documented in `hermes-wire-format.md` §3. Wire-compatible across implementations.
