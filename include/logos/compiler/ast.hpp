// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Logos AST node codes and field keys.
//
// Every AST node is a Hermes TinyObjectMap. The CODE field (key 0) is the
// node type discriminant. Remaining fields are node-specific.
//
// This file defines the constants — no runtime logic, no includes beyond
// NamedCode.  Safe to include from parsers, compiler passes, and tools.

#pragma once

#include <stdint.h>

#include <logos/core/named_code.hpp>

namespace logos::compiler::ast {

using Key  = logos::NamedCode<uint8_t>;   // TinyObjectMap field key (0..51)
using Code = logos::NamedCode<int32_t>;   // Node type discriminant

// ── Field keys ───────────────────────────────────────────────────────────
//
// Shared across node types. Not every node uses every key.

inline constexpr Key CODE     {"CODE",      0};  // node type (int32_t)
inline constexpr Key NAME     {"NAME",      1};  // identifier string
inline constexpr Key ITEMS    {"ITEMS",     2};  // child array (stmts, params, etc.)
inline constexpr Key TYPE     {"TYPE",      3};  // type reference
inline constexpr Key PARAMS   {"PARAMS",    4};  // parameter list
inline constexpr Key BODY     {"BODY",      5};  // function/loop body (block)
inline constexpr Key RET_TYPE {"RET_TYPE",  6};  // return type
inline constexpr Key VALUE    {"VALUE",     7};  // initializer / literal value
inline constexpr Key COND     {"COND",      8};  // condition (if/while)
inline constexpr Key THEN     {"THEN",      9};  // then branch
inline constexpr Key ELSE     {"ELSE",     10};  // else branch
inline constexpr Key OP       {"OP",       11};  // operator string ("+", "==", etc.)
inline constexpr Key LHS      {"LHS",      12};  // left-hand side
inline constexpr Key RHS      {"RHS",      13};  // right-hand side
inline constexpr Key CALLEE   {"CALLEE",   14};  // call target
inline constexpr Key ARGS     {"ARGS",     15};  // call arguments
inline constexpr Key PATH     {"PATH",     16};  // package path (dotted name)
inline constexpr Key USES     {"USES",     17};  // use declarations array
inline constexpr Key POINTEE  {"POINTEE",  18};  // pointee type for pointer types
inline constexpr Key MUTPTR   {"MUTPTR",   19};  // pointer mutability (bool)
inline constexpr Key RECEIVER {"RECEIVER", 20};  // receiver of field read / method call
inline constexpr Key FIELD    {"FIELD",    21};  // field name (string)
inline constexpr Key FIELDS   {"FIELDS",   22};  // field definitions array (in struct)
inline constexpr Key SIZE     {"SIZE",     23};  // array size (integer literal)
inline constexpr Key SRC_LINE  {"SRC_LINE",  24};  // source line number (uint32_t, 1-based)
inline constexpr Key IS_MUT    {"IS_MUT",    25};  // mutability flag (uint8_t, 1 = mut)
inline constexpr Key INCLUSIVE  {"INCLUSIVE",  26};  // for range: inclusive end (..=)
inline constexpr Key TYPE_PARAMS{"TYPE_PARAMS", 27}; // generic type parameter list
inline constexpr Key PARENT     {"PARENT",      28}; // parent class name (string)
inline constexpr Key IS_ABSTRACT{"IS_ABSTRACT", 29}; // abstract flag (bool)
inline constexpr Key IS_VARARG  {"IS_VARARG",   30}; // vararg flag for extern fn (bool)
inline constexpr Key ITER       {"ITER",        31}; // iterable expr for for-each

// ── Node codes ───────────────────────────────────────────────────────────

// Top-level
inline constexpr Code MODULE      {"MODULE",       1};
inline constexpr Code PACKAGE     {"PACKAGE",      2};   // package declaration
inline constexpr Code USE         {"USE",          3};   // use declaration

// Definitions
inline constexpr Code FN          {"FN",          10};
inline constexpr Code EXTERN_FN   {"EXTERN_FN",   11};   // extern fn (FFI, no body)
inline constexpr Code PARAM       {"PARAM",       12};

// Statements / blocks
inline constexpr Code BLOCK       {"BLOCK",       20};
inline constexpr Code LET         {"LET",         21};
inline constexpr Code RETURN      {"RETURN",      22};
inline constexpr Code IF          {"IF",          23};
inline constexpr Code EXPR_STMT   {"EXPR_STMT",   24};   // expression as statement

// Expressions
inline constexpr Code CALL        {"CALL",        30};
inline constexpr Code BINOP       {"BINOP",       31};
inline constexpr Code VAR_REF     {"VAR_REF",     32};
inline constexpr Code LIT_INT     {"LIT_INT",     33};
inline constexpr Code LIT_BOOL    {"LIT_BOOL",    34};
inline constexpr Code LIT_STR     {"LIT_STR",     35};   // string literal

// Type references
inline constexpr Code TYPE_REF    {"TYPE_REF",    40};
inline constexpr Code PTR_TYPE    {"PTR_TYPE",    41};   // *const T or *mut T

// Iteration 3 — structs, methods, control flow
inline constexpr Code STRUCT      {"STRUCT",      50};   // struct definition
inline constexpr Code FIELD_DEF   {"FIELD_DEF",   51};   // field declaration (name: type)
inline constexpr Code FIELD_INIT  {"FIELD_INIT",  52};   // field initializer in struct literal
inline constexpr Code STRUCT_LIT  {"STRUCT_LIT",  53};   // Point { x: 1, y: 2 }
inline constexpr Code FIELD_READ  {"FIELD_READ",  54};   // receiver.field
inline constexpr Code FIELD_WRITE {"FIELD_WRITE", 55};   // receiver.field = val
inline constexpr Code METHOD_CALL {"METHOD_CALL", 56};   // receiver.method(args)
inline constexpr Code ASSIGN      {"ASSIGN",      57};   // name = expr (local rebind)
inline constexpr Code WHILE       {"WHILE",       58};   // while cond { body }
inline constexpr Code DEREF       {"DEREF",       59};   // *ptr
inline constexpr Code PAREN_EXPR  {"PAREN_EXPR",  60};   // (expr) — parenthesised
inline constexpr Code UNARY       {"UNARY",       61};   // unary op: -, !, &
inline constexpr Code INDEX_READ  {"INDEX_READ",  62};   // arr[i]
inline constexpr Code INDEX_WRITE {"INDEX_WRITE", 63};   // arr[i] = val
inline constexpr Code ARR_TYPE    {"ARR_TYPE",    64};   // [T; N] array type
inline constexpr Code ARR_LIT     {"ARR_LIT",     65};
inline constexpr Code BREAK       {"BREAK",       66};
inline constexpr Code CONTINUE    {"CONTINUE",    67};
inline constexpr Code LOOP        {"LOOP",        68};
inline constexpr Code CAST        {"CAST",        69};
inline constexpr Code FOR         {"FOR",         70};   // for i in lo..hi { }
inline constexpr Code CONST_DEF  {"CONST_DEF",   71};   // const NAME: type = expr;
inline constexpr Code TYPE_ALIAS {"TYPE_ALIAS",  72};   // type NAME = type_ref;

// Iteration 4 — enums + match
inline constexpr Code ENUM        {"ENUM",        80};   // enum definition
inline constexpr Code VARIANT_DEF {"VARIANT_DEF", 81};   // variant inside enum
inline constexpr Code MATCH       {"MATCH",        82};   // match statement
inline constexpr Code MATCH_ARM   {"MATCH_ARM",    83};   // arm: pattern => body
inline constexpr Code PAT_VARIANT {"PAT_VARIANT",  84};   // Enum::Variant pattern
inline constexpr Code PAT_WILD    {"PAT_WILD",     85};   // _ or name wildcard pattern
inline constexpr Code ENUM_LIT    {"ENUM_LIT",     86};   // Enum::Variant expression
inline constexpr Code PAT_INT     {"PAT_INT",      87};   // integer literal pattern
inline constexpr Code PAT_BOOL    {"PAT_BOOL",     88};   // bool literal pattern

// Batch D — generics
inline constexpr Code TRAIT_BOUND {"TRAIT_BOUND",  89};   // bound in T: Trait1 + Trait2
inline constexpr Code TYPE_PARAM  {"TYPE_PARAM",   90};   // type parameter T or T: Bound
inline constexpr Code GENERIC_CALL{"GENERIC_CALL", 91};   // foo::<T>(args)
inline constexpr Code GENERIC_INST{"GENERIC_INST", 92};   // Vec<T> in type position

// Batch H — classes
inline constexpr Code CLASS       {"CLASS",        93};   // class definition
inline constexpr Code NEW_EXPR    {"NEW_EXPR",     94};   // new ClassName { ... }
inline constexpr Code DELETE_STMT {"DELETE_STMT",  95};   // delete expr;
inline constexpr Code ABSTRACT_FN {"ABSTRACT_FN",  96};   // abstract fn declaration
inline constexpr Code STATIC_FN   {"STATIC_FN",    97};   // static fn in class
inline constexpr Code STATIC_CALL {"STATIC_CALL",  98};   // ClassName::method(args)
inline constexpr Code FOR_EACH    {"FOR_EACH",     99};   // for item in array { }

// Batch J — tuples
inline constexpr Code TUPLE_TYPE  {"TUPLE_TYPE",  100};   // (i32, bool) in type position
inline constexpr Code TUPLE_LIT   {"TUPLE_LIT",   101};   // (1, true) expression
inline constexpr Code TUPLE_INDEX {"TUPLE_INDEX", 102};   // t.0, t.1 — numeric field access

// Visibility
inline constexpr int32_t VIS_PRIVATE = 0;
inline constexpr int32_t VIS_PUBLIC  = 1;

} // namespace logos::compiler::ast
