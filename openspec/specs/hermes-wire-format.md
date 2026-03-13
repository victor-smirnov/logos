# Hermes Wire Format Specification

This document is the formal specification of the Hermes binary wire format — the bit-exact layout of Hermes data in memory and on the wire. It complements `hermes-abi.json` (machine-readable type registry) and `hermes.md` (high-level port specification).

**Scope:** This document specifies what is necessary for binary stability and cross-implementation interoperability. Implementation details (GC algorithm, thread-local pools, View layer API) are covered in `hermes.md` with source code as the source of truth.

## 1. Byte Order

All multi-byte integers and floating-point values are stored in **little-endian** byte order unless explicitly noted otherwise.

## 2. ShortTypeCode Encoding

Every arena-allocated object has a type tag stored in the bytes **immediately before** the object's start address.

### 2.1 In-Memory Layout

A `ShortTypeCode` is a packed `uint64_t` with three fields:

```
Bit 63                                              Bit 0
┌──────────────────────────────────┬─────────┬───────┐
│  type_code (56 bits)             │ desc    │ len   │
│  [63:8]                          │ [7:3]   │ [2:0] │
└──────────────────────────────────┴─────────┴───────┘
```

| Field       | Bits    | Description |
|-------------|---------|-------------|
| `code_len`  | [2:0]   | Number of *additional* bytes beyond the first. Range 0-7. `full_code_len = code_len + 1` gives total byte count (1-8). |
| `descriptor`| [7:3]   | 5-bit type-specific flags. Known values: 0=data, 1=`HERMES_OBJECT_ARRAY`, 2=`HERMES_OBJECT_MAP`. |
| `type_code` | [63:8]  | 56-bit type hash. For core types, a statically assigned small integer. |

### 2.2 code_len Computation

`code_len(type_hash)` returns the minimum number of bytes needed to encode `type_hash`, minus one:

```
code_len(0)     = 0   (null type code, special case)
code_len(1..255)     = 1   → full_code_len = 2 bytes
code_len(256..65535) = 2   → full_code_len = 3 bytes
code_len(65536..16777215) = 3  → full_code_len = 4 bytes
...
code_len(max 56-bit) = 7  → full_code_len = 8 bytes
```

All core Hermes types have `type_hash < 256`, so they use **2-byte tags**.

### 2.3 Writing a Type Tag

Tags are written **backwards** from the object start address:

```
for c in 0..full_code_len:
    *(base - c - 1) = (short_type_code >> (c * 8)) & 0xFF
```

Example for `BigInt` (type_hash=26, descriptor=0):
- `ShortTypeCode = (26 << 8) | 1 | 0 = 0x1A01`
- Byte at `base - 1`: `0x01` (contains code_len=1)
- Byte at `base - 2`: `0x1A` (contains type_hash low byte)

### 2.4 Reading a Type Tag

```
first_byte = *(object_addr - 1)
code_len = first_byte & 0x07
raw_val = 0
for c in 0..code_len (inclusive):
    raw_val |= (*(object_addr - c - 1)) << (c * 8)
short_type_code = ShortTypeCode(raw_val)
```

### 2.5 Core Type Tag Values

These are the exact `ShortTypeCode` uint64_t values and their 2-byte arena representations:

| Type | type_hash | descriptor | ShortTypeCode (hex) | Arena bytes [base-2, base-1] |
|------|-----------|------------|---------------------|------------------------------|
| TinyInt | 20 | 0 | `0x1401` | `0x14 0x01` |
| UTinyInt | 21 | 0 | `0x1501` | `0x15 0x01` |
| SmallInt | 22 | 0 | `0x1601` | `0x16 0x01` |
| Integer | 23 | 0 | `0x1701` | `0x17 0x01` |
| USmallInt | 24 | 0 | `0x1801` | `0x18 0x01` |
| UInteger | 25 | 0 | `0x1901` | `0x19 0x01` |
| BigInt | 26 | 0 | `0x1A01` | `0x1A 0x01` |
| UBigInt | 27 | 0 | `0x1B01` | `0x1B 0x01` |
| Varchar | 28 | 0 | `0x1C01` | `0x1C 0x01` |
| Varbinary | 29 | 0 | `0x1D01` | `0x1D 0x01` |
| Real | 30 | 0 | `0x1E01` | `0x1E 0x01` |
| Double | 31 | 0 | `0x1F01` | `0x1F 0x01` |
| Timestamp | 32 | 0 | `0x2001` | `0x20 0x01` |
| Boolean | 37 | 0 | `0x2501` | `0x25 0x01` |
| Uid256 | 40 | 0 | `0x2801` | `0x28 0x01` |
| Uid64 | 42 | 0 | `0x2A01` | `0x2A 0x01` |
| TinyObjectMap | 98 | 2 | `0x6211` | `0x62 0x11` |
| Hermes (container DT) | 98 | 0 | `0x6201` | `0x62 0x01` |
| Object | 99 | 0 | `0x6301` | `0x63 0x01` |
| ObjectArray | 100 | 1 | `0x6409` | `0x64 0x09` |
| ObjectMap | 101 | 2 | `0x6511` | `0x65 0x11` |
| Datatype | 102 | 0 | `0x6601` | `0x66 0x01` |
| TypedValue | 103 | 0 | `0x6701` | `0x67 0x01` |
| Parameter | 104 | 0 | `0x6801` | `0x68 0x01` |
| Array\<Integer\> | 105 | 1 | `0x6909` | `0x69 0x09` |

## 3. EmbeddingRelativePtr (ERelativePtr)

The fundamental 8-byte polymorphic slot used as element type in ObjectArray and TinyObjectMap values.

### 3.1 Discriminant

```
buffer[7] bit 0:
  0 → pointer mode (relative pointer to arena object)
  1 → value mode (inline embedded small value)
```

### 3.2 Pointer Mode

Stores a byte-rotated signed relative offset:

**Encode offset → stored uint64_t:**
```
uint64_t stored = ((uint64_t)offset >> 8) | ((uint64_t)offset << 56)
```

**Decode stored uint64_t → offset:**
```
uint64_t top_byte = stored >> 56
int64_t offset = (int64_t)((stored << 8) | top_byte)
```

**Dereference:** `target = (uint8_t*)&this_erelptr + offset`

**Null check:** `stored_u64 == 0` (all zeros = null pointer).

**Invariant:** The offset is always even (arena alignment >= 2), so the low bit of `offset & 0xFF` is 0, and after rotation it becomes bit 0 of `buffer[7]`, which is 0 in pointer mode. This is the mechanism that makes the discriminant work.

### 3.3 Value Mode

```
Byte layout (little-endian uint64_t):
  buffer[0..6]  = value data (up to 7 bytes, zero-padded high bytes)
  buffer[7]     = (type_hash << 1) | 0x01
```

**Tag extraction:** `tag = buffer[7] >> 1` (7-bit type_hash, range 0-127).

**Value extraction:** read `sizeof(ValueType)` bytes from `buffer[0..]`.

**Embeddability rule:** A type T can be embedded iff:
1. `DataTypeTraits<T>::isFixedSize == true`
2. `sizeof(ViewType) < 8` (value fits in 7 bytes)
3. `TypeHash<T> < 128` (tag fits in 7 bits)

### 3.4 Embeddable Types

| Type | TypeHash | Value Size | Embedded Tag Byte |
|------|----------|-----------|-------------------|
| TinyInt | 20 | 1 | `0x29` = (20<<1)\|1 |
| UTinyInt | 21 | 1 | `0x2B` |
| SmallInt | 22 | 2 | `0x2D` |
| Integer | 23 | 4 | `0x2F` |
| USmallInt | 24 | 2 | `0x31` |
| UInteger | 25 | 4 | `0x33` |
| Real | 30 | 4 | `0x3D` |
| Time | 35 | 4 | `0x47` |
| Boolean | 37 | 1 | `0x4B` |

Types NOT embeddable (size == 8): BigInt, UBigInt, Double, Timestamp, Date, Uid64, Uid256.

## 4. Variable-Length Integer Encoding (vlen_u64_56)

Used for string length prefixes and other size values.

### 4.1 Encoding

```
if value < 249:
    write 1 byte: value
else:
    N = number of bytes needed to represent value (1..7)
    write 1 byte: 248 + N
    write N bytes: value in little-endian
```

### 4.2 Decoding

```
first = read 1 byte
if first < 249:
    value = first
else:
    N = first - 248
    value = read N bytes, little-endian
```

### 4.3 Maximum Value

56-bit unsigned integer (7 bytes): `0x00FFFFFFFFFFFFFF`. Values above this cannot be encoded.

## 5. Arena String (ArenaDataTypeContainer\<Varchar\>)

Variable-length UTF-8 string stored as a tagged arena object.

### 5.1 Layout

```
[ShortTypeCode tag: 2 bytes (before object addr)]
[vlen_u64_56 length: 1-8 bytes]
[UTF-8 data: length bytes, no null terminator]
```

### 5.2 Object Size

`total_object_bytes = vlen_prefix_size(length) + length`

The tag is not counted in `object_size` — it occupies the gap before the object.

### 5.3 Hash Function

FNV-1a over each byte of the UTF-8 data (used for ObjectMap key hashing).

## 6. TinyObjectMap Layout

The primary structured data container in Hermes, used for M-Code entities, AST nodes, and structured records.

### 6.1 Physical Layout

```
[Tag: 2 bytes (0x62 0x11)]
[header_: 8 bytes (uint64_t)]
[data_: 8 bytes (RelativePtr to value array)]
```

Total in-arena footprint: 16 bytes + tag.

### 6.2 Header Bit Fields

```
Bit 63                                    Bit 0
┌────────┬──────────┬────────────────────────────┐
│ size   │ capacity │ key bitmap                  │
│ [63:58]│ [57:52]  │ [51:0]                      │
└────────┴──────────┴────────────────────────────┘
```

- **Key bitmap** (52 bits): Bit K is set if key K (uint8_t, 0-51) is present.
- **Capacity** (6 bits): Allocated size of the value array.
- **Size** (6 bits): Number of entries currently stored.

### 6.3 Value Array

Pointed to by `data_`. Contains `capacity` slots of `ERelativePtr` (8 bytes each). Only the first `size` slots are valid.

### 6.4 Lookup Algorithm

```python
def get(key: uint8):
    assert key < 52
    key_mask = 1 << key
    if not (header & key_mask):
        return None
    mask_below = (1 << key) - 1
    pos = popcount(header & mask_below & BITMAP_MASK)
    return data[pos]
```

Where `BITMAP_MASK = 0x000FFFFFFFFFFFFF` (low 52 bits).

### 6.5 Key Code Conventions

Key codes are application-defined. Examples from Memoria:

**DSL Method:** NAME=1, METADATA=2, ARGUMENTS=3, CONSTANTS=4, RETURN_TYPE=5, CODE=6.

**HermesPath/Template AST:** Codes defined per node type (0-50 range).

## 7. ObjectArray Layout

### 7.1 Vector Representation (Growable)

```
[Tag: 2 bytes]
[size_: 8 bytes (uint64_t)]
[capacity_: 8 bytes (uint64_t)]
[data_: 8 bytes (RelativePtr to ERelativePtr[])]
```

Total: 24 bytes + tag. Data array contains `capacity` ERelativePtr slots.

### 7.2 Inline Array Representation (Fixed)

```
[Tag: 2 bytes]
[size_: 8 bytes (uint64_t)]
[capacity_: 8 bytes (uint64_t)]
[array_[0]: 8 bytes (ERelativePtr)]
[array_[1]: 8 bytes (ERelativePtr)]
...
[array_[capacity-1]: 8 bytes]
```

Total: `16 + capacity * 8` bytes + tag.

### 7.3 Element Access

Each element is an `ERelativePtr`, resolved via the pointer/value mode discriminant (Section 3).

## 8. ObjectMap Layout

### 8.1 Top-Level Structure

```
[Tag: 2 bytes]
[size_: 8 bytes (uint64_t) — total entry count]
[buckets_capacity_: 8 bytes (uint64_t) — log2 of bucket array length]
[buckets_: 8 bytes (RelativePtr to RelativePtr<Bucket>[])]
```

Bucket array length = `1 << buckets_capacity_`. Each slot is a `RelativePtr<Bucket>` (8 bytes), null if empty.

### 8.2 Bucket Structure (Untagged)

```
[size: 4 bytes (uint32_t)]
[capacity: 4 bytes (uint32_t)]
[keys[0]: 8 bytes (RelativePtr<ArenaString>)]
[keys[1]: 8 bytes]
...
[keys[capacity-1]: 8 bytes]
[padding to align values]
[values[0]: 8 bytes (ERelativePtr)]
[values[1]: 8 bytes]
...
[values[capacity-1]: 8 bytes]
```

Keys and values are in SoA (Structure of Arrays) layout within each bucket. Lookup is linear scan within the bucket.

### 8.3 Hash Function

FNV-1a (64-bit) over the key string bytes.

### 8.4 Bucket Index

```
bucket_idx = hash & ((1 << buckets_capacity_) - 1)
```

## 9. Document Structure

### 9.1 Zero-Copy Format

A Hermes document in zero-copy format is a single contiguous memory segment:

```
Offset 0: DocumentHeader (8 bytes)
  [root: RelativePtr<void> — offset to root object]

Offset 8+: Arena objects (tagged, aligned)
  [alignment gap / tag bytes]
  [Object 1]
  [alignment gap / tag bytes]
  [Object 2]
  ...
```

This segment is directly memory-mappable. All pointers are relative to their own address, so the entire segment can be relocated without fixup.

### 9.2 Compactification

Compactification creates a single-chunk arena by deep-copying the object graph:
1. Allocate new `GROWABLE_SINGLE_CHUNK` arena.
2. Deep-copy root object and all reachable objects via `DeepCopyState`.
3. Deduplication: shared sub-objects are copied once, subsequent references reuse the copy.
4. Result: clean, contiguous, garbage-free segment suitable for serialization.

### 9.3 Immutability

A compactified document in `GROWABLE_SINGLE_CHUNK` mode with no arena reference is immutable. The raw bytes can be shared across threads, memory-mapped from disk, or transmitted over the network.

## 10. Binary Serialization Format

The binary serialization format is a streaming encoding, distinct from the zero-copy format. It does not preserve arena layout but is denser.

### 10.1 Stream Structure

```
[4-byte prefix: reserved/chunk header]
[tagged objects, depth-first traversal]
```

### 10.2 Value Encoding

Each value starts with its `ShortTypeCode` written as a variable-length tag, followed by the value data:

- **Fixed-size primitives:** Tag + raw bytes (size determined by type).
- **Strings:** Tag + vlen_u64_56 length + UTF-8 data.
- **Arrays:** Tag + vlen size + elements (recursive).
- **Maps:** Tag + vlen size + (key, value) pairs (recursive).
- **TinyObjectMap:** Tag + header (8 bytes) + size * (key_code, value) pairs.
- **Datatype:** Tag + name + params + constructor + extras.
- **TypedValue:** Tag + datatype + value.

### 10.3 Serialization Primitives

| Operation | Encoding |
|-----------|----------|
| `write_u8(v)` | 1 byte |
| `write_u32(v)` | 4 bytes, little-endian |
| `write_u64(v)` | 8 bytes, little-endian |
| `write_u64_vl(v)` | 1 byte length (0-8) + N bytes LE |

## 11. Cross-Runtime Interoperability

### 11.1 ARC-Compatible Runtimes

Runtimes with deterministic memory management (C++, Rust, CPython, Swift) can implement the full arena model:
- Native `ArenaAllocator` with bump-pointer allocation
- `RelativePtr` / `ERelativePtr` with identical binary layout
- Direct memory-mapping of zero-copy segments
- Shared immutable documents via reference-counted arena ownership

### 11.2 Tracing-GC Runtimes (Java, JavaScript, etc.)

Full arena model is impractical. Instead, implement a **Hermes Wire Codec**:

**Reading (Decode):** Parse the zero-copy or binary stream into native objects:
- Hermes maps → native Map/Object
- Hermes arrays → native Array
- Hermes strings → native String
- Hermes integers → native int/long
- Hermes TinyObjectMap → native Map with integer keys (or named-field object if schema is known)

**Writing (Encode):** Serialize native objects into Hermes binary or zero-copy format:
- Allocate a temporary arena (can be a simple byte buffer)
- Write objects following the arena layout rules
- Emit the final byte segment

**Schema-Driven Optimization:** When the TinyObjectMap key schema is known (e.g., from a `.hermes-abi.json` descriptor), codecs can generate typed accessors instead of generic map lookups.

### 11.3 Bridge Requirements

For a language L to interoperate with Hermes:

| Capability | ARC Runtime | GC Runtime |
|-----------|-------------|------------|
| Read zero-copy segment | Direct memory access | Decode to native objects |
| Write zero-copy segment | Native arena construction | Encode from native objects |
| Read binary stream | Deserialize to arena | Decode to native objects |
| Write binary stream | Serialize from arena | Encode from native objects |
| Mutable documents | Full arena + GC support | Native objects only |
| Immutable documents | Direct mapping | Lazy decode or full decode |

### 11.4 Stability Guarantees

The following are **frozen** and must not change across versions without a major version bump:

1. `ShortTypeCode` encoding (byte layout, bit fields)
2. `EmbeddingRelativePtr` encoding (discriminant, rotation, tag)
3. `RelativePtr` encoding (int64_t offset)
4. `vlen_u64_56` encoding
5. `DocumentHeader` layout
6. Arena tag placement (before object, backwards)
7. Core type hash assignments (type_hash values in `hermes-abi.json`)
8. `TinyObjectMap` header bit layout
9. `ArenaString` encoding (vlen length + UTF-8 data)
10. Binary serialization wire format primitives

Changes to the following require a minor version bump:
- New type hash assignments
- New descriptor values
- New container type layouts
- Extension of vlen encoding range

Implementation details that are NOT part of the wire format and MAY change freely:
- Arena chunk management strategy
- GC / deep copy algorithm internals
- Thread-local pool implementation
- View layer API signatures
- Parser implementation technology
- Object pool strategy
