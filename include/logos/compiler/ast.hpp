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

// (Batch H — classes removed; STATIC_FN/STATIC_CALL kept for struct static methods)
inline constexpr Code STATIC_FN   {"STATIC_FN",    97};   // static method in impl block (fn Self::name)
inline constexpr Code STATIC_CALL {"STATIC_CALL",  98};   // Type::method(args) static dispatch
inline constexpr Code FOR_EACH    {"FOR_EACH",     99};   // for item in array { }

// Batch J — tuples
inline constexpr Code TUPLE_TYPE  {"TUPLE_TYPE",  100};   // (i32, bool) in type position
inline constexpr Code TUPLE_LIT   {"TUPLE_LIT",   101};   // (1, true) expression
inline constexpr Code TUPLE_INDEX {"TUPLE_INDEX", 102};   // t.0, t.1 — numeric field access

// Batch K — tagged unions (enums with data)
inline constexpr Code ENUM_LIT_DATA   {"ENUM_LIT_DATA",   104};  // Option::Some(42)
inline constexpr Code PAT_VARIANT_DATA{"PAT_VARIANT_DATA", 105}; // Some(x) in match

// Batch O — slices
inline constexpr Code SLICE_TYPE      {"SLICE_TYPE",      111};  // &[T] type
inline constexpr Code RANGE_EXPR      {"RANGE_EXPR",      112};  // lo..hi expression

// Batch L — traits
inline constexpr Code TRAIT_DEF       {"TRAIT_DEF",       106};  // trait Name { ... }
inline constexpr Code IMPL_BLOCK      {"IMPL_BLOCK",      107};  // impl Trait for Type { ... }

// Batch N — closures
inline constexpr Code CLOSURE_EXPR    {"CLOSURE_EXPR",    109};  // |x: i32| -> i32 { ... }

// Misc
inline constexpr Code FIELD_INDEX_WRITE {"FIELD_INDEX_WRITE", 113}; // a.ptr[i] = val
inline constexpr Code ADDR_OF_MUT       {"ADDR_OF_MUT",       114}; // &mut expr
inline constexpr Code DEREF_WRITE       {"DEREF_WRITE",       115}; // *ptr = val;

// Variadic generics
inline constexpr Key IS_VARIADIC  {"IS_VARIADIC",  32};    // variadic type param / param flag
inline constexpr Code PACK_EXPAND {"PACK_EXPAND", 117};    // args... pack expansion

// Dynamic dispatch
inline constexpr Code IMPL_TYPE   {"IMPL_TYPE",   116};    // impl Trait type
inline constexpr Code DYN_TYPE    {"DYN_TYPE",    118};    // &dyn Trait type

// Associated types
inline constexpr Code ASSOC_TYPE_DEF  {"ASSOC_TYPE_DEF",  119};  // type Item; in trait
inline constexpr Code ASSOC_TYPE_IMPL {"ASSOC_TYPE_IMPL", 120};  // type Item = T; in impl
inline constexpr Code ASSOC_TYPE_REF  {"ASSOC_TYPE_REF",  121};  // T::Item type reference
inline constexpr Code TRY_EXPR        {"TRY_EXPR",        122};  // expr? — propagate Err early
inline constexpr Code LET_DESTRUCT    {"LET_DESTRUCT",    123};  // let (a, b) = expr;
inline constexpr Code WHERE_CLAUSE    {"WHERE_CLAUSE",    124};  // where T: Trait clause
inline constexpr Code CLOSURE_TYPE      {"CLOSURE_TYPE",      125};  // |T1, T2| -> R closure type
inline constexpr Code COMPOUND_ASSIGN   {"COMPOUND_ASSIGN",   126};  // x += expr (desugared in sema)
inline constexpr Code ARR_FILL_LIT     {"ARR_FILL_LIT",     127};  // [value; N] array fill literal
inline constexpr Code DEREF_FIELD_WRITE{"DEREF_FIELD_WRITE", 128};  // (*ptr).field = expr;

// Safe references (borrow-checked)
inline constexpr Code REF_TYPE        {"REF_TYPE",          129};  // &T    (shared reference)
inline constexpr Code MUT_REF_TYPE    {"MUT_REF_TYPE",      130};  // &mut T (exclusive mutable reference)
inline constexpr Code LIFETIME_PARAM  {"LIFETIME_PARAM",    131};  // 'a in <'a, T> type parameter list
inline constexpr Key  LIFETIME        {"LIFETIME",           40};   // lifetime string on Ref/MutRef nodes

// Unsafe
inline constexpr Code UNSAFE_BLOCK    {"UNSAFE_BLOCK",      132};  // unsafe { ... }
inline constexpr Code CONST_PARAM     {"CONST_PARAM",       133};  // const N: usize parameter
inline constexpr Code TUPLE_FIELD_WRITE{"TUPLE_FIELD_WRITE", 134}; // var.N = value;  tuple field write
inline constexpr Code LIT_FLOAT        {"LIT_FLOAT",         135}; // float literal 3.14
inline constexpr Code FIELD_COMPOUND_ASSIGN {"FIELD_COMPOUND_ASSIGN", 136}; // s.field op= expr
inline constexpr Code INDEX_COMPOUND_ASSIGN {"INDEX_COMPOUND_ASSIGN", 137}; // arr[i] op= expr
inline constexpr Code PAT_NEG_INT     {"PAT_NEG_INT",     138}; // negative int pattern: -42
inline constexpr Code PAT_OR          {"PAT_OR",          139}; // OR pattern: 1 | 2 | 3
inline constexpr Code FIELD_SHORTHAND {"FIELD_SHORTHAND", 140}; // struct field shorthand: Point { x, y }
inline constexpr Code LET_ELSE        {"LET_ELSE",        141}; // let Pat = expr else { block };
inline constexpr Code LABELED_LOOP    {"LABELED_LOOP",    142}; // 'label: loop/for/while
inline constexpr Code PAT_TUPLE       {"PAT_TUPLE",       143}; // tuple pattern: (a, b)
inline constexpr Code FN_PTR_TYPE     {"FN_PTR_TYPE",     144}; // fn(T) -> R function pointer type
inline constexpr Code DEREF_FIELD_COMPOUND_ASSIGN {"DEREF_FIELD_COMPOUND_ASSIGN", 145}; // (*ptr).field op= expr
inline constexpr Code TUPLE_FIELD_COMPOUND_ASSIGN {"TUPLE_FIELD_COMPOUND_ASSIGN", 146}; // var.N op= expr
inline constexpr Code FIELD_INDEX_COMPOUND_ASSIGN {"FIELD_INDEX_COMPOUND_ASSIGN", 147}; // s.field[i] op= expr

// Hermes datatypes
inline constexpr Code DATATYPE    {"DATATYPE",    148};  // datatype definition (C POD layout)

// Annotations
inline constexpr Code ANNOTATION  {"ANNOTATION",  150};  // #[name], #[name=val], #[name(args)]

// Tag-dispatch types
inline constexpr Code TAGGED_TYPE {"TAGGED_TYPE", 151};  // &tagged<TS> Trait thin pointer

// Associated constants
inline constexpr Code ASSOC_CONST_DEF  {"ASSOC_CONST_DEF",  152};  // const NAME: T;         in trait
inline constexpr Code ASSOC_CONST_IMPL {"ASSOC_CONST_IMPL", 153};  // const NAME: T = expr;  in impl

// Pattern matching extensions
inline constexpr Code PAT_STRUCT  {"PAT_STRUCT",  154};  // Point { x: p, y } struct pattern
inline constexpr Code PAT_SLICE   {"PAT_SLICE",   155};  // [a, b] or [first, .., last] slice pattern
inline constexpr Code PAT_RANGE   {"PAT_RANGE",   156};  // 0..=9 inclusive integer range pattern
inline constexpr Code PAT_AT      {"PAT_AT",      157};  // n @ sub_pat binding
inline constexpr Code PAT_REF     {"PAT_REF",     158};  // &pat or &mut pat reference pattern
inline constexpr Code PAT_FIELD   {"PAT_FIELD",   159};  // named field in struct pattern
inline constexpr Code PAT_REST    {"PAT_REST",    160};  // .. rest in struct/slice pattern
inline constexpr Code CHAIN_FIELD_WRITE          {"CHAIN_FIELD_WRITE",          161};  // a.b.c = val
inline constexpr Code CHAIN_FIELD_COMPOUND_ASSIGN{"CHAIN_FIELD_COMPOUND_ASSIGN", 162};  // a.b.c op= val
inline constexpr Code HERMES_MAP   {"HERMES_MAP",   163};  // @{k:v,...} Hermes map literal
inline constexpr Code HERMES_ARRAY {"HERMES_ARRAY", 164};  // @[v,...] Hermes array literal
inline constexpr Code HERMES_STR   {"HERMES_STR",   165};  // string value in Hermes literal
inline constexpr Code HERMES_INT   {"HERMES_INT",   166};  // integer value in Hermes literal
inline constexpr Code HERMES_FLOAT {"HERMES_FLOAT", 167};  // float value in Hermes literal
inline constexpr Code HERMES_BOOL  {"HERMES_BOOL",  168};  // bool value in Hermes literal
inline constexpr Code HERMES_NULL  {"HERMES_NULL",  169};  // null in Hermes literal
inline constexpr Code HERMES_ENTRY        {"HERMES_ENTRY",        170};  // key:val pair in Hermes map
inline constexpr Code HERMES_TYPED_ARRAY  {"HERMES_TYPED_ARRAY",  171};  // @<ElemType>[v,...] typed array literal

// Index field key for tuple_field_write_stmt (integer field index)
inline constexpr Key  INDEX           {"INDEX",              43};   // integer index (tuple field write)

// Visibility
inline constexpr Key IS_PUB    {"IS_PUB",    33};           // visibility flag (1 = pub)
inline constexpr Key PAT       {"PAT",       34};            // pattern for if let / while let
inline constexpr Key GUARD     {"GUARD",     35};            // guard condition in match arm
inline constexpr Key EXPR      {"EXPR",      36};            // expression-body match arm value
inline constexpr Key NAMES     {"NAMES",     37};            // name list for tuple destructuring
inline constexpr Key WHERE     {"WHERE",     38};            // where clause node
inline constexpr Key IS_CONST  {"IS_CONST",  39};            // const fn marker
inline constexpr Key IS_UNSAFE {"IS_UNSAFE", 42};            // unsafe fn marker
inline constexpr Key IS_MOVE   {"IS_MOVE",   44};            // move closure marker
inline constexpr Key IS_REF    {"IS_REF",    45};            // &self / &mut self param shorthand
inline constexpr Key BASE      {"BASE",      46};            // struct update base expression (..base)
inline constexpr Key LABEL     {"LABEL",     47};            // loop label (e.g. "'outer")
inline constexpr Key SUPERS    {"SUPERS",    48};            // supertrait bound list on TRAIT_DEF
inline constexpr Key LO_NEG    {"LO_NEG",    49};            // lo bound is negative (PAT_RANGE)
inline constexpr Key HI_NEG    {"HI_NEG",    50};            // hi bound is negative (PAT_RANGE)
inline constexpr Key KEY       {"KEY",       51};            // map key in HERMES_ENTRY (string or int token)
inline constexpr Key IMPL_TYPE_PARAMS{"IMPL_TYPE_PARAMS", 41}; // impl<T> own type params
inline constexpr int32_t VIS_PRIVATE = 0;
inline constexpr int32_t VIS_PUBLIC  = 1;

// ── Group: mod (MODULE, USE) ────────────────────────────────────────────────
// Fields local to module-level nodes.  Slot numbers here share the 0..51 space
// with the globals but can reuse slots that are never populated on mod nodes.
namespace mod {
    inline constexpr Key PATH_PARTS {"PATH_PARTS", 16};  // array of {NAME} per component after the first
}

} // namespace logos::compiler::ast
