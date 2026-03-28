// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <string_view>
#include <logos/hermes/document.hpp>

namespace logos::hermes {

// AST node type codes (stored in TinyObjectMap key 0).
namespace path_ast {
    inline constexpr uint8_t CODE          = 0;
    inline constexpr uint8_t LEFT          = 1;
    inline constexpr uint8_t RIGHT         = 2;
    inline constexpr uint8_t NAME          = 3;
    inline constexpr uint8_t VALUE         = 4;
    inline constexpr uint8_t COMPARATOR    = 5;
    inline constexpr uint8_t ARGS          = 6;
    inline constexpr uint8_t EXPRESSIONS   = 7;
    inline constexpr uint8_t KEYS          = 8;
    inline constexpr uint8_t START         = 9;
    inline constexpr uint8_t STOP          = 10;
    inline constexpr uint8_t STEP          = 11;

    // Node types (stored as value of key CODE).
    inline constexpr int32_t IDENTITY           = 0;
    inline constexpr int32_t IDENTIFIER         = 1;
    inline constexpr int32_t RAW_STRING         = 2;
    inline constexpr int32_t SUBEXPRESSION      = 3;
    inline constexpr int32_t INDEX_EXPRESSION   = 4;
    inline constexpr int32_t ARRAY_ITEM         = 5;
    inline constexpr int32_t FLATTEN            = 6;
    inline constexpr int32_t SLICE              = 7;
    inline constexpr int32_t LIST_WILDCARD      = 8;
    inline constexpr int32_t HASH_WILDCARD      = 9;
    inline constexpr int32_t FILTER             = 10;
    inline constexpr int32_t COMPARATOR_EXPR    = 11;
    inline constexpr int32_t NOT_EXPR           = 12;
    inline constexpr int32_t OR_EXPR            = 13;
    inline constexpr int32_t AND_EXPR           = 14;
    inline constexpr int32_t PIPE               = 15;
    inline constexpr int32_t FUNCTION_CALL      = 16;
    inline constexpr int32_t MULTISELECT_LIST   = 17;
    inline constexpr int32_t MULTISELECT_HASH   = 18;
    inline constexpr int32_t PAREN              = 19;
    inline constexpr int32_t CURRENT_NODE       = 20;
    inline constexpr int32_t HERMES_VALUE       = 21;
    inline constexpr int32_t EXPR_ARGUMENT      = 22;

    // Comparator values (stored as value of key COMPARATOR).
    inline constexpr int32_t CMP_LT  = 0;
    inline constexpr int32_t CMP_LE  = 1;
    inline constexpr int32_t CMP_EQ  = 2;
    inline constexpr int32_t CMP_GE  = 3;
    inline constexpr int32_t CMP_GT  = 4;
    inline constexpr int32_t CMP_NE  = 5;
}

// Parse a HermesPath expression into an AST (stored as Hermes objects in a document).
HermesCtr parse_path(std::string_view expr);

// Evaluate a HermesPath expression against a data document.
// Returns the result as a new HermesCtr.
HermesCtr eval_path(const HermesCtr& data, std::string_view expr);

// Evaluate a pre-parsed AST against a data value.
// The ast_root should be a TinyObjectMap AST node.
// data_root is the value to query.
// Returns result in a new document.
HermesCtr eval_path_ast(void* data_root, void* ast_root, Arena& data_arena);

} // namespace logos::hermes
