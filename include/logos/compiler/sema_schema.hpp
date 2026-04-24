#pragma once

// Hermes schema keys for compiler-internal type and generics objects.
//
// Phase 2 of the compiler-on-Hermes migration (see plans/snappy-knitting-kay.md).
// These Key constants describe the on-zone layout of LogosType / TraitBound /
// TypeParam when they live as TinyObjectMap nodes inside a Hermes document.
//
// The variant discriminator for a LogosType node is stored in the TinyObjectMap
// header via `set_schema_type_code(schema::type(int32_t(kind)))` — no KIND key
// is needed, the header is the single source of truth.

#include <cstdint>
#include <logos/core/named_code.hpp>

namespace logos::compiler::sema_schema {

using Key = logos::NamedCode<uint8_t>;

// ── LogosType keys ─────────────────────────────────────────────────────────
//
// Every Kind uses only the subset of keys relevant to it; unused keys are
// simply absent from the map (TinyObjectMap is sparse).

// Ptr / Ref / MutRef
inline constexpr Key POINTEE           {"POINTEE",          0}; // RelPtr<LogosType>
inline constexpr Key MUT_PTR           {"MUT_PTR",          1}; // u8 (Ptr only)
inline constexpr Key LIFETIME          {"LIFETIME",         2}; // Varchar (Ref/MutRef)

// Array / Slice
inline constexpr Key ELEM              {"ELEM",             3}; // RelPtr<LogosType>
inline constexpr Key ARR_SIZE          {"ARR_SIZE",         4}; // u64
inline constexpr Key ARR_SIZE_VAR      {"ARR_SIZE_VAR",     5}; // Varchar (symbolic N)

// Struct / ZonedStruct / Enum
inline constexpr Key STRUCT_NAME       {"STRUCT_NAME",      6}; // Varchar
inline constexpr Key ENUM_NAME         {"ENUM_NAME",        7}; // Varchar
inline constexpr Key PKG_NAME          {"PKG_NAME",         8}; // Varchar

// Generic instantiation
inline constexpr Key TYPE_ARGS         {"TYPE_ARGS",        9}; // Array<RelPtr<LogosType>>
inline constexpr Key LIFETIME_ARGS     {"LIFETIME_ARGS",   10}; // Array<Varchar>

// Tuple
inline constexpr Key TUPLE_ELEMS       {"TUPLE_ELEMS",     11}; // Array<RelPtr<LogosType>>

// Closure / FnPtr
inline constexpr Key CLOSURE_PARAMS    {"CLOSURE_PARAMS",  12}; // Array<RelPtr<LogosType>>
inline constexpr Key CLOSURE_RET       {"CLOSURE_RET",     13}; // RelPtr<LogosType>

// TraitObject / TaggedPtr
inline constexpr Key TRAIT_NAME        {"TRAIT_NAME",      14}; // Varchar

// TypeVar
inline constexpr Key TYPE_VAR_NAME     {"TYPE_VAR_NAME",   15}; // Varchar

// AssocType
inline constexpr Key ASSOC_BASE        {"ASSOC_BASE",      16}; // RelPtr<LogosType>
inline constexpr Key ASSOC_TYPE_NAME   {"ASSOC_TYPE_NAME", 17}; // Varchar
inline constexpr Key GAT_ARGS          {"GAT_ARGS",        18}; // Array<RelPtr<LogosType>>

// ConstVar
inline constexpr Key CONST_VAL         {"CONST_VAL",       19}; // i64 (std::optional absent → key absent)

// Cached TypeUID (32-byte datatype, filled lazily on first request)
inline constexpr Key TYPE_UID          {"TYPE_UID",        20}; // TypeUID datatype

// ── TraitBound keys ────────────────────────────────────────────────────────
//
// TraitBound lives in its own TinyObjectMap with schema_type_code derived from
// a dedicated Kind enum (separate namespace; currently only one shape).

namespace trait_bound {
inline constexpr Key TRAIT_NAME        {"TRAIT_NAME",       0}; // Varchar
inline constexpr Key TYPE_ARGS         {"TYPE_ARGS",        1}; // Array<RelPtr<LogosType>>
}

// ── TypeParam keys ─────────────────────────────────────────────────────────

namespace type_param {
inline constexpr Key NAME              {"NAME",             0}; // Varchar
inline constexpr Key BOUNDS            {"BOUNDS",           1}; // Array<RelPtr<TraitBound>>
inline constexpr Key IS_VARIADIC       {"IS_VARIADIC",      2}; // u8
inline constexpr Key IS_CONST          {"IS_CONST",         3}; // u8
inline constexpr Key CONST_TYPE        {"CONST_TYPE",       4}; // RelPtr<LogosType>
}

} // namespace logos::compiler::sema_schema
