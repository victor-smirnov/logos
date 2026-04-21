// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <string_view>
#include <logos/hermes/document.hpp>
#include <logos/hermes/named_code.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// AST node field keys and type codes.
namespace path_ast {
    using Key  = NamedCode<uint8_t>;   // TinyObjectMap field key
    using Code = NamedCode<int32_t>;   // node type discriminant or comparator value

    // Field keys (stored in TinyObjectMap).
    inline constexpr Key CODE          {"CODE",        0};
    inline constexpr Key LEFT          {"LEFT",        1};
    inline constexpr Key RIGHT         {"RIGHT",       2};
    inline constexpr Key NAME          {"NAME",        3};
    inline constexpr Key VALUE         {"VALUE",       4};
    inline constexpr Key COMPARATOR    {"COMPARATOR",  5};
    inline constexpr Key ARGS          {"ARGS",        6};
    inline constexpr Key EXPRESSIONS   {"EXPRESSIONS", 7};
    inline constexpr Key KEYS          {"KEYS",        8};
    inline constexpr Key START         {"START",       9};
    inline constexpr Key STOP          {"STOP",       10};
    inline constexpr Key STEP          {"STEP",       11};

    // Node types (stored as value of key CODE).
    inline constexpr Code IDENTITY           {"IDENTITY",         0};
    inline constexpr Code IDENTIFIER         {"IDENTIFIER",       1};
    inline constexpr Code RAW_STRING         {"RAW_STRING",       2};
    inline constexpr Code SUBEXPRESSION      {"SUBEXPRESSION",    3};
    inline constexpr Code INDEX_EXPRESSION   {"INDEX_EXPRESSION", 4};
    inline constexpr Code ARRAY_ITEM         {"ARRAY_ITEM",       5};
    inline constexpr Code FLATTEN            {"FLATTEN",          6};
    inline constexpr Code SLICE              {"SLICE",            7};
    inline constexpr Code LIST_WILDCARD      {"LIST_WILDCARD",    8};
    inline constexpr Code HASH_WILDCARD      {"HASH_WILDCARD",    9};
    inline constexpr Code FILTER             {"FILTER",          10};
    inline constexpr Code COMPARATOR_EXPR    {"COMPARATOR_EXPR", 11};
    inline constexpr Code NOT_EXPR           {"NOT_EXPR",        12};
    inline constexpr Code OR_EXPR            {"OR_EXPR",         13};
    inline constexpr Code AND_EXPR           {"AND_EXPR",        14};
    inline constexpr Code PIPE               {"PIPE",            15};
    inline constexpr Code FUNCTION_CALL      {"FUNCTION_CALL",   16};
    inline constexpr Code MULTISELECT_LIST   {"MULTISELECT_LIST",17};
    inline constexpr Code MULTISELECT_HASH   {"MULTISELECT_HASH",18};
    inline constexpr Code PAREN              {"PAREN",           19};
    inline constexpr Code CURRENT_NODE       {"CURRENT_NODE",    20};
    inline constexpr Code HERMES_VALUE       {"HERMES_VALUE",    21};
    inline constexpr Code EXPR_ARGUMENT      {"EXPR_ARGUMENT",   22};

    // Comparator values (stored as value of key COMPARATOR).
    inline constexpr Code CMP_LT  {"CMP_LT", 0};
    inline constexpr Code CMP_LE  {"CMP_LE", 1};
    inline constexpr Code CMP_EQ  {"CMP_EQ", 2};
    inline constexpr Code CMP_GE  {"CMP_GE", 3};
    inline constexpr Code CMP_GT  {"CMP_GT", 4};
    inline constexpr Code CMP_NE  {"CMP_NE", 5};
}

// Parse a HermesPath expression into an AST (stored as Hermes objects in a document).
[[nodiscard]] logos::expected<Hermes> parse_path(std::string_view expr) noexcept;

// Evaluate a HermesPath expression against a data document.
[[nodiscard]] logos::expected<Hermes> eval_path(const Hermes& data,
                                                    std::string_view expr) noexcept;

// Evaluate a pre-parsed AST against a data value.
[[nodiscard]] logos::expected<Hermes> eval_path_ast(void* data_root,
                                                        void* ast_root,
                                                        Arena& data_arena) noexcept;

} // namespace logos::hermes
