// Logos project — https://github.com/victor-smirnov/logos
//
// grammar_ast: NamedCode schema for the internal grammar IR.
//
// A parsed .peg file becomes a Hermes document with this structure:
//
//   Grammar (root ObjectMap, string-keyed):
//     "meta"    → MetaInfo
//     "imports" → Array<Import>
//     "exports" → Array<String>        (exported rule names)
//     "fields"  → Array<NameDecl>      (%fields → NamedCode<uint8_t>)
//     "nodes"   → Array<NameDecl>      (%nodes  → NamedCode<int32_t>)
//     "tokens"  → Array<TokenDecl>
//     "prec"    → Array<PrecLevel>
//     "rules"   → Array<Rule>
//
//   MetaInfo    (TinyObjectMap): { NAME, VERSION, NAMESPACE, OUTPUT }
//   Import      (TinyObjectMap): { PATH, ALIAS }
//   NameDecl    (TinyObjectMap): { NAME, CODE }   e.g. { "LEFT", 1 }
//
//   TokenDecl   (TinyObjectMap): { NAME, KIND, PATTERN }
//     KIND: TOKEN_LITERAL | TOKEN_REGEX | TOKEN_SKIP
//
//   PrecLevel   (TinyObjectMap): { ASSOC, TOKENS }
//     ASSOC: ASSOC_LEFT | ASSOC_RIGHT | ASSOC_NONE
//
//   Rule        (TinyObjectMap): { NAME, ALTS }
//     ALTS: Array<Alt>
//
//   Alt         (TinyObjectMap): { SEQ, ACTION }
//     SEQ:    Array<Item>
//     ACTION: TinyObjectMap  (field → ActionExpr, optional)
//
//   Item variants (TinyObjectMap, discriminated by CODE):
//     RULE_REF   { CODE, NAME, GRAMMAR }    grammar = import alias, empty = local
//     TOKEN_REF  { CODE, NAME }
//     LITERAL    { CODE, VALUE }             inline string literal e.g. "+"
//     OPT        { CODE, ITEM }              item?
//     REP        { CODE, ITEM, MIN, MAX }    item*  → 0,-1   item+ → 1,-1
//     GROUP      { CODE, ALTS }              (alt1 / alt2)
//     LOOKAHEAD  { CODE, ITEM }              &item
//     NEG_AHEAD  { CODE, ITEM }              !item
//
//   ActionExpr variants (TinyObjectMap, discriminated by CODE):
//     CAPTURE    { CODE, INDEX }             $1, $2, ...
//     BOOL_LIT   { CODE, VALUE }             true / false
//     INT_LIT    { CODE, VALUE }             integer literal
//     STR_LIT    { CODE, VALUE }             string literal
//     ARRAY_CAPTURE { CODE }                 $...  (collect all captures)

#pragma once

#include <stdint.h>

#include <logos/core/named_code.hpp>

namespace logos::peg_gen::ast {

using Key  = NamedCode<uint8_t>;
using Code = NamedCode<int32_t>;

// ---------------------------------------------------------------------------
// TinyObjectMap field keys  (shared across node types where names align)
// ---------------------------------------------------------------------------

inline constexpr Key NAME      {"NAME",      0};
inline constexpr Key CODE      {"CODE",      1};
inline constexpr Key VALUE     {"VALUE",     2};
inline constexpr Key PATH      {"PATH",      3};
inline constexpr Key ALIAS     {"ALIAS",     4};
inline constexpr Key PATTERN   {"PATTERN",   5};
inline constexpr Key KIND      {"KIND",      6};
inline constexpr Key ASSOC     {"ASSOC",     7};
inline constexpr Key TOKENS    {"TOKENS",    8};
inline constexpr Key ALTS      {"ALTS",      9};
inline constexpr Key SEQ       {"SEQ",       10};
inline constexpr Key ACTION    {"ACTION",    11};
inline constexpr Key ITEM      {"ITEM",      12};
inline constexpr Key GRAMMAR   {"GRAMMAR",   13};  // import alias for cross-grammar refs
inline constexpr Key MIN       {"MIN",       14};
inline constexpr Key MAX       {"MAX",       15};  // -1 = unbounded
inline constexpr Key INDEX     {"INDEX",     16};  // capture index $n
inline constexpr Key VERSION   {"VERSION",   17};
inline constexpr Key NAMESPACE {"NAMESPACE", 18};
inline constexpr Key OUTPUT    {"OUTPUT",    19};
inline constexpr Key FIELDS    {"FIELDS",    20};  // GROUP_DECL: array of NameDecls
inline constexpr Key GROUP_NAME {"GROUP_NAME", 21}; // RULE / NameDecl: group name

// ---------------------------------------------------------------------------
// Node type discriminants  (stored as value of CODE field)
// ---------------------------------------------------------------------------

// Grammar sections
inline constexpr Code META_INFO     {"META_INFO",     0};
inline constexpr Code IMPORT        {"IMPORT",        1};
inline constexpr Code NAME_DECL     {"NAME_DECL",     2};  // %fields / %nodes entry
inline constexpr Code GROUP_DECL    {"GROUP_DECL",    7};  // %fields group block
inline constexpr Code TOKEN_DECL    {"TOKEN_DECL",    3};
inline constexpr Code PREC_LEVEL    {"PREC_LEVEL",    4};
inline constexpr Code RULE          {"RULE",          5};
inline constexpr Code ALT           {"ALT",           6};

// Item kinds
inline constexpr Code RULE_REF      {"RULE_REF",      10};
inline constexpr Code TOKEN_REF     {"TOKEN_REF",     11};
inline constexpr Code LITERAL       {"LITERAL",       12};
inline constexpr Code OPT           {"OPT",           13};  // ?
inline constexpr Code REP           {"REP",           14};  // * or +
inline constexpr Code GROUP         {"GROUP",         15};  // ( ... )
inline constexpr Code LOOKAHEAD     {"LOOKAHEAD",     16};  // &
inline constexpr Code NEG_AHEAD     {"NEG_AHEAD",     17};  // !

// Action expression kinds
inline constexpr Code CAPTURE       {"CAPTURE",       20};  // $n  (n >= 1)
inline constexpr Code ARRAY_CAPTURE {"ARRAY_CAPTURE", 21};  // $...
inline constexpr Code BOOL_LIT      {"BOOL_LIT",      22};
inline constexpr Code INT_LIT       {"INT_LIT",       23};
inline constexpr Code STR_LIT       {"STR_LIT",       24};
inline constexpr Code FOLD_CAPTURE  {"FOLD_CAPTURE",  25};  // $0  (fold accumulator)

// ---------------------------------------------------------------------------
// Token kind values  (stored as value of KIND field in TokenDecl)
// ---------------------------------------------------------------------------

inline constexpr Code TOKEN_LITERAL {"TOKEN_LITERAL", 30};  // "keyword"
inline constexpr Code TOKEN_REGEX   {"TOKEN_REGEX",   31};  // /pattern/
inline constexpr Code TOKEN_SKIP    {"TOKEN_SKIP",    32};  // %skip

// ---------------------------------------------------------------------------
// Associativity values  (stored as value of ASSOC field in PrecLevel)
// ---------------------------------------------------------------------------

inline constexpr Code ASSOC_LEFT    {"ASSOC_LEFT",    40};
inline constexpr Code ASSOC_RIGHT   {"ASSOC_RIGHT",   41};
inline constexpr Code ASSOC_NONE    {"ASSOC_NONE",    42};

} // namespace logos::peg_gen::ast
