#pragma once

#include <cstdint>

namespace logos::hermes::schema {

inline constexpr uint64_t CATEGORY_SHIFT = 48;
inline constexpr uint64_t CATEGORY_MASK  = 0xFFFFULL << CATEGORY_SHIFT;
inline constexpr uint64_t VARIANT_MASK   = (1ULL << CATEGORY_SHIFT) - 1;

inline constexpr uint64_t CAT_UNSET    = 0x0000ULL << CATEGORY_SHIFT;
inline constexpr uint64_t CAT_AST      = 0x0001ULL << CATEGORY_SHIFT;
inline constexpr uint64_t CAT_TYPE     = 0x0002ULL << CATEGORY_SHIFT;
inline constexpr uint64_t CAT_LIR_EXPR = 0x0003ULL << CATEGORY_SHIFT;
inline constexpr uint64_t CAT_LIR_STMT = 0x0004ULL << CATEGORY_SHIFT;
inline constexpr uint64_t CAT_LIR_PAT  = 0x0005ULL << CATEGORY_SHIFT;
inline constexpr uint64_t CAT_SYMBOLS  = 0x0006ULL << CATEGORY_SHIFT;
inline constexpr uint64_t CAT_DIAG     = 0x0007ULL << CATEGORY_SHIFT;

constexpr uint64_t ast(int32_t code)       { return CAT_AST      | uint64_t(uint32_t(code)); }
constexpr uint64_t type(int32_t kind)      { return CAT_TYPE     | uint64_t(uint32_t(kind)); }
constexpr uint64_t lir_expr(int32_t code)  { return CAT_LIR_EXPR | uint64_t(uint32_t(code)); }
constexpr uint64_t lir_stmt(int32_t code)  { return CAT_LIR_STMT | uint64_t(uint32_t(code)); }
constexpr uint64_t lir_pat(int32_t code)   { return CAT_LIR_PAT  | uint64_t(uint32_t(code)); }
constexpr uint64_t symbol(int32_t code)    { return CAT_SYMBOLS  | uint64_t(uint32_t(code)); }
constexpr uint64_t diag(int32_t code)      { return CAT_DIAG     | uint64_t(uint32_t(code)); }

constexpr uint64_t category_of(uint64_t schema_code) { return schema_code & CATEGORY_MASK; }
constexpr uint64_t variant_of(uint64_t schema_code)  { return schema_code & VARIANT_MASK; }

} // namespace logos::hermes::schema
