// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <logos/hermes/type_tag.hpp>

namespace logos::hermes {

// Compile-time type hash constants from hermes-abi.json.
// These are the canonical identifiers for all core Hermes types.
namespace type_hash {
    // Integer types
    inline constexpr uint64_t TinyInt       = 20;  // int8_t
    inline constexpr uint64_t UTinyInt      = 21;  // uint8_t
    inline constexpr uint64_t SmallInt      = 22;  // int16_t
    inline constexpr uint64_t Integer       = 23;  // int32_t
    inline constexpr uint64_t USmallInt     = 24;  // uint16_t
    inline constexpr uint64_t UInteger      = 25;  // uint32_t
    inline constexpr uint64_t BigInt        = 26;  // int64_t
    inline constexpr uint64_t UBigInt       = 27;  // uint64_t

    // Variable-length
    inline constexpr uint64_t Varchar       = 28;  // UTF-8 string
    inline constexpr uint64_t Varbinary     = 29;  // raw bytes

    // Floating point
    inline constexpr uint64_t Real          = 30;  // float (IEEE 754 binary32)
    inline constexpr uint64_t Double        = 31;  // double (IEEE 754 binary64)

    // Temporal
    inline constexpr uint64_t Timestamp     = 32;  // int64_t, microseconds since epoch
    inline constexpr uint64_t TimestampWithTZ = 33;
    inline constexpr uint64_t Date          = 34;  // int64_t, days since epoch
    inline constexpr uint64_t Time          = 35;  // int32_t, microseconds since midnight
    inline constexpr uint64_t TimeWithTZ    = 36;  // int64_t

    // Boolean
    inline constexpr uint64_t Boolean       = 37;  // uint8_t: 0=false, 1=true

    // UIDs
    inline constexpr uint64_t Uid256        = 40;  // 32 bytes, SHA-256
    inline constexpr uint64_t Uid128        = 41;  // 16 bytes
    inline constexpr uint64_t Uid64         = 42;  // uint64_t

    // Compound / container markers
    inline constexpr uint64_t Hermes        = 98;  // document type
    inline constexpr uint64_t Object        = 99;  // universal tagged value
    inline constexpr uint64_t ObjectArray   = 100; // heterogeneous array
    inline constexpr uint64_t ObjectMap     = 101; // string-keyed map
    inline constexpr uint64_t Datatype      = 102; // type declaration
    inline constexpr uint64_t TypedValue    = 103; // value + type pair
    inline constexpr uint64_t Parameter     = 104; // query parameter ?name
    inline constexpr uint64_t TypedArrayBase = 105; // base for typed arrays
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

HERMES_FIXED_TYPE_TRAITS(int8_t,   type_hash::TinyInt,   true);
HERMES_FIXED_TYPE_TRAITS(uint8_t,  type_hash::UTinyInt,  true);
HERMES_FIXED_TYPE_TRAITS(int16_t,  type_hash::SmallInt,  true);
HERMES_FIXED_TYPE_TRAITS(uint16_t, type_hash::USmallInt, true);
HERMES_FIXED_TYPE_TRAITS(int32_t,  type_hash::Integer,   true);
HERMES_FIXED_TYPE_TRAITS(uint32_t, type_hash::UInteger,  true);
HERMES_FIXED_TYPE_TRAITS(int64_t,  type_hash::BigInt,    false);  // 8 bytes, can't embed
HERMES_FIXED_TYPE_TRAITS(uint64_t, type_hash::UBigInt,   false);
HERMES_FIXED_TYPE_TRAITS(float,    type_hash::Real,      true);
HERMES_FIXED_TYPE_TRAITS(double,   type_hash::Double,    false);  // 8 bytes, can't embed

#undef HERMES_FIXED_TYPE_TRAITS

// Convenience: get the TypeTag for a C++ type at compile time.
template <typename T>
constexpr TypeTag type_tag_for() {
    return TypeTag(TypeTraits<T>::hash, TypeTraits<T>::descriptor);
}

// Convenience: check if a C++ type can be embedded in a TaggedPtr.
template <typename T>
constexpr bool is_embeddable() {
    return TypeTraits<T>::embeddable && sizeof(T) < 8 && TypeTraits<T>::hash < 128;
}

} // namespace logos::hermes
