// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <logos/hermes/type_tag.hpp>

namespace logos::hermes {

// Compile-time type hash constants from hermes-abi.json.
// These are the canonical identifiers for all core Hermes types.
namespace type_hash {
    // Integer types
    inline constexpr uint64_t I8            = 20;  // int8_t
    inline constexpr uint64_t U8            = 21;  // uint8_t
    inline constexpr uint64_t I16           = 22;  // int16_t
    inline constexpr uint64_t I24           = 23;  // int32_t
    inline constexpr uint64_t U16           = 24;  // uint16_t
    inline constexpr uint64_t U24           = 25;  // uint32_t
    inline constexpr uint64_t I64           = 26;  // int64_t
    inline constexpr uint64_t U64           = 27;  // uint64_t

    // Variable-length
    inline constexpr uint64_t HermesString  = 28;  // UTF-8 string
    inline constexpr uint64_t Varbinary     = 29;  // raw bytes

    // Floating point
    inline constexpr uint64_t F32           = 30;  // float (IEEE 754 binary32)
    inline constexpr uint64_t F64           = 31;  // double (IEEE 754 binary64)

    // Temporal
    inline constexpr uint64_t Timestamp     = 32;  // int64_t, microseconds since epoch
    inline constexpr uint64_t TimestampWithTZ = 33;
    inline constexpr uint64_t Date          = 34;  // int64_t, days since epoch
    inline constexpr uint64_t Time          = 35;  // int32_t, microseconds since midnight
    inline constexpr uint64_t TimeWithTZ    = 36;  // int64_t

    // Boolean
    inline constexpr uint64_t Bool          = 37;  // uint8_t: 0=false, 1=true

    // UIDs
    inline constexpr uint64_t Uid256        = 40;  // 32 bytes, SHA-256
    inline constexpr uint64_t Uid128        = 41;  // 16 bytes
    inline constexpr uint64_t Uid64         = 42;  // uint64_t

    // Logos Document (at arena offset 0 — logical code; no in-memory tag).
    inline constexpr uint64_t Document      = 3;

    // Compound / container markers — byte-for-byte aligned with the Logos
    // stdlib wire format (see stdlib/hermes/typetag.logos).
    inline constexpr uint64_t TinyObjectMap = 98;  // TinyObjectMap (document-style)
    inline constexpr uint64_t Object        = 99;  // universal tagged value
    inline constexpr uint64_t Array         = 100; // heterogeneous AnyVal array
    inline constexpr uint64_t ObjectMap     = 101; // string-keyed map
    inline constexpr uint64_t Decimal       = 102; // fixed-precision decimal (inline limbs)
    inline constexpr uint64_t Type          = 107; // Logos Type (component-metaprog slice 1): TinyObjectMap with schema_type_code=Type carrying {kind:u32, uid:u64, name:Varchar}.
    inline constexpr uint64_t Parameter     = 127; // query parameter ?name / @-literal capture slot
    inline constexpr uint64_t Datatype      = 1001; // type declaration (multi-byte wire tag)

    // Typed Array<T> variants (2100-range, multi-byte wire tags).
    inline constexpr uint64_t ArrayU8       = 2101;
    inline constexpr uint64_t ArrayU16      = 2102;
    inline constexpr uint64_t ArrayU32      = 2103;
    inline constexpr uint64_t ArrayU64      = 2104;
    inline constexpr uint64_t ArrayI8       = 2105;
    inline constexpr uint64_t ArrayI16      = 2106;
    inline constexpr uint64_t ArrayI32      = 2107;
    inline constexpr uint64_t ArrayI64      = 2108;
    inline constexpr uint64_t ArrayF32      = 2109;
    inline constexpr uint64_t ArrayF64      = 2110;

    // Typed Map<K, AnyVal> variants (3100-range).
    inline constexpr uint64_t MapI32AnyVal  = 3101;
    inline constexpr uint64_t MapU32AnyVal  = 3102;
    inline constexpr uint64_t MapI64AnyVal  = 3103;
    inline constexpr uint64_t MapU64AnyVal  = 3104;

    // TypedValue (4100-range, wraps unregistered types).
    inline constexpr uint64_t TypedValue    = 4115;
}

// TypeTraits: compile-time properties of a Hermes data type.
// Specialized for each C++ type that maps to a Hermes type.
template <typename T>
struct TypeTraits {
    // Deliberately left undefined — specializations provide:
    //   static constexpr uint64_t hash;
    //   static constexpr bool fixed_size;
    //   static constexpr bool embeddable;
    //   static constexpr TagDescriptor descriptor;
};

// Macro to reduce boilerplate for fixed-size primitive type specializations.
#define HERMES_FIXED_TYPE_TRAITS(CppType, Hash, Embeddable) \
    template <> struct TypeTraits<CppType> { \
        static constexpr uint64_t hash = Hash; \
        static constexpr bool fixed_size = true; \
        static constexpr bool embeddable = Embeddable; \
        static constexpr TagDescriptor descriptor = TagDescriptor::Data; \
    }

// Embeddable in the 4-byte AnyVal value-mode layout (24-bit payload slot):
//   i8/u8/i16/u16/bool   — always fit
//   i32/u32              — only when the runtime value fits in 24 bits;
//                          the `embeddable` flag is `true` here to mark the
//                          type as eligible for embedding, but callers
//                          (or Logos codegen) must range-check at runtime.
// i64/u64/f32/f64 live in the arena and AnyVal holds an offset to them.
HERMES_FIXED_TYPE_TRAITS(int8_t,   type_hash::I8,    true);
HERMES_FIXED_TYPE_TRAITS(uint8_t,  type_hash::U8,    true);
HERMES_FIXED_TYPE_TRAITS(int16_t,  type_hash::I16,   true);
HERMES_FIXED_TYPE_TRAITS(uint16_t, type_hash::U16,   true);
HERMES_FIXED_TYPE_TRAITS(int32_t,  type_hash::I24,   true);
HERMES_FIXED_TYPE_TRAITS(uint32_t, type_hash::U24,   true);
HERMES_FIXED_TYPE_TRAITS(int64_t,  type_hash::I64,   false);
HERMES_FIXED_TYPE_TRAITS(uint64_t, type_hash::U64,   false);
HERMES_FIXED_TYPE_TRAITS(float,    type_hash::F32,   false);  // not embeddable in 4-byte AnyVal
HERMES_FIXED_TYPE_TRAITS(double,   type_hash::F64,   false);

#undef HERMES_FIXED_TYPE_TRAITS

// Convenience: get the TypeTag for a C++ type at compile time.
template <typename T>
constexpr TypeTag type_tag_for() noexcept {
    return TypeTag(TypeTraits<T>::hash, TypeTraits<T>::descriptor);
}

// Convenience: check if a C++ type can be embedded in a AnyVal.
template <typename T>
constexpr bool is_embeddable() noexcept {
    return TypeTraits<T>::embeddable && sizeof(T) < 8 && TypeTraits<T>::hash < 128;
}

} // namespace logos::hermes
