// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>

namespace logos::hermes {

// Hermes2 wire/in-band type codes — the SINGLE source of truth shared with the
// Logos stdlib (stdlib/lang/hermes2/anyval.logos H2_*/HA_*/HT_* and the container
// HArrTag/HIntKeyTag codes). C++ and Logos read the same bytes, so these MUST stay
// identical to the Logos constants.
//
// NOTE — string code divergence: Logos hermes2 uses H2_STRING = 130, whereas the
// historical Hermes1 / HermesTag uses HermesString = 28. We follow the Logos value
// (the layout spec). If the canonical Hermes string code should be 28, change it on
// the Logos side first and update here in lockstep.
namespace tc {

// ── inline Pod scalar codes (7-bit; in an AnyVal Pod word) ──
inline constexpr uint64_t HA_I56  = 1;    // generic inline int (i56 fallback)
inline constexpr uint64_t HA_BOOL = 2;
inline constexpr uint64_t HT_I8   = 20;
inline constexpr uint64_t HT_U8   = 21;
inline constexpr uint64_t HT_I16  = 22;
inline constexpr uint64_t HT_I24  = 23;
inline constexpr uint64_t HT_U16  = 24;
inline constexpr uint64_t HT_U24  = 25;

// ── Ref (tagged arena object) codes ──
inline constexpr uint64_t STRING     = 130;   // HString  (Logos H2_STRING; Hermes1 used 28)
inline constexpr uint64_t ARRAY      = 100;   // HArray<HVal>      (ObjectArray)
inline constexpr uint64_t MAP        = 101;   // HMap<HString,HVal> (ObjectMap)
inline constexpr uint64_t TINYMAP    = 98;    // HMap<Hu6,HVal>     (TinyObjectMap)
inline constexpr uint64_t DECIMAL    = 102;   // HDecimal
inline constexpr uint64_t PARAMETER  = 127;   // HParameter
inline constexpr uint64_t TYPEDVALUE = 4115;  // HTypedValue

// boxed wide scalars (don't fit the 56-bit inline Pod)
inline constexpr uint64_t I64 = 26;
inline constexpr uint64_t U64 = 27;
inline constexpr uint64_t F32 = 30;
inline constexpr uint64_t F64 = 31;

// typed packed arrays HArray<T> (ArrayU8..ArrayF64)
inline constexpr uint64_t ARRAY_U8  = 2101;
inline constexpr uint64_t ARRAY_U16 = 2102;
inline constexpr uint64_t ARRAY_U32 = 2103;
inline constexpr uint64_t ARRAY_U64 = 2104;
inline constexpr uint64_t ARRAY_I8  = 2105;
inline constexpr uint64_t ARRAY_I16 = 2106;
inline constexpr uint64_t ARRAY_I32 = 2107;
inline constexpr uint64_t ARRAY_I64 = 2108;
inline constexpr uint64_t ARRAY_F32 = 2109;
inline constexpr uint64_t ARRAY_F64 = 2110;

// dense int-keyed maps HMap<K,HVal> (MapI32AnyVal..MapU64AnyVal)
inline constexpr uint64_t MAP_I32 = 3101;
inline constexpr uint64_t MAP_U32 = 3102;
inline constexpr uint64_t MAP_I64 = 3103;
inline constexpr uint64_t MAP_U64 = 3104;

} // namespace tc
} // namespace logos::hermes
