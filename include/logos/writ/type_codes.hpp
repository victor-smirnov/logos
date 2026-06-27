// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>

namespace logos::writ {

// Writ wire/in-band type codes — the SINGLE source of truth shared with the
// Logos stdlib (stdlib/lang/writ2/anyval.logos H2_*/WA_*/WT_* and the container
// WArrTag/WIntKeyTag codes). C++ and Logos read the same bytes, so these MUST stay
// identical to the Logos constants.
//
// NOTE — string code divergence: Logos writ2 uses H2_STRING = 130, whereas the
// historical Writ1 / WritTag uses WritString = 28. We follow the Logos value
// (the layout spec). If the canonical Writ string code should be 28, change it on
// the Logos side first and update here in lockstep.
namespace tc {

// ── inline Pod scalar codes (7-bit; in an AnyVal Pod word) ──
inline constexpr uint64_t WA_I56  = 1;    // generic inline int (i56 fallback)
inline constexpr uint64_t WA_BOOL = 2;
inline constexpr uint64_t WT_I8   = 20;
inline constexpr uint64_t WT_U8   = 21;
inline constexpr uint64_t WT_I16  = 22;
inline constexpr uint64_t WT_I24  = 23;
inline constexpr uint64_t WT_U16  = 24;
inline constexpr uint64_t WT_U24  = 25;

// ── Ref (tagged arena object) codes ──
inline constexpr uint64_t STRING     = 130;   // WString  (Logos H2_STRING; Writ1 used 28)
inline constexpr uint64_t ARRAY      = 100;   // WArray<WAny>      (ObjectArray)
inline constexpr uint64_t MAP        = 101;   // WMap<WString,WAny> (ObjectMap)
inline constexpr uint64_t TINYMAP    = 98;    // WMap<Hu6,WAny>     (TinyObjectMap)
inline constexpr uint64_t DECIMAL    = 102;   // WDecimal
inline constexpr uint64_t PARAMETER  = 127;   // WParameter
inline constexpr uint64_t TYPEDVALUE = 4115;  // WTypedValue

// boxed wide scalars (don't fit the 56-bit inline Pod)
inline constexpr uint64_t I64 = 26;
inline constexpr uint64_t U64 = 27;
inline constexpr uint64_t F32 = 30;
inline constexpr uint64_t F64 = 31;

// typed packed arrays WArray<T> (ArrayU8..ArrayF64)
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

// dense int-keyed maps WMap<K,WAny> (MapI32AnyVal..MapU64AnyVal)
inline constexpr uint64_t MAP_I32 = 3101;
inline constexpr uint64_t MAP_U32 = 3102;
inline constexpr uint64_t MAP_I64 = 3103;
inline constexpr uint64_t MAP_U64 = 3104;

} // namespace tc
} // namespace logos::writ
