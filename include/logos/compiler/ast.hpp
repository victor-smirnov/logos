// Logos project — https://github.com/victor-smirnov/logos
//
// Logos AST node codes and field keys.
//
// Every AST node is a Writ TinyObjectMap. The CODE field (key 0) is the
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
// §4 module system: visibility sub-node on item decls (FN/STRUCT/ENUM/TRAIT/
// CONST/…) produced by the grammar `pub_vis` rule. NAME=="module" → pub(module)
// (module-linkage); present-but-no-NAME → plain pub. Reuses the ELSE slot — item
// decls never carry an else-branch.
inline constexpr Key VIS      {"VIS",      10};
inline constexpr Key OP       {"OP",       11};  // operator string ("+", "==", etc.)
inline constexpr Key LHS      {"LHS",      12};  // left-hand side
inline constexpr Key RHS      {"RHS",      13};  // right-hand side
inline constexpr Key CALLEE   {"CALLEE",   14};  // call target
inline constexpr Key ARGS     {"ARGS",     15};  // call arguments
inline constexpr Key USES     {"USES",     17};  // use declarations array
inline constexpr Key QUAL_PARTS {"QUAL_PARTS", 17};  // T2-28: package-path segments (after RECEIVER) on a qualified CALL/GENERIC_CALL; reuses USES slot (call nodes never carry module USES)
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
inline constexpr Key IS_AUTO    {"IS_AUTO",     29}; // auto trait marker (1 = auto)

// ── Node codes ───────────────────────────────────────────────────────────

// Top-level
inline constexpr Code MODULE      {"MODULE",       1};
inline constexpr Code PACKAGE     {"PACKAGE",      2};   // package declaration
inline constexpr Code USE         {"USE",          3};   // use declaration
inline constexpr Code USE_VARIANTS{"USE_VARIANTS", 241}; // CP-cm-02 + GR-gp-02: `use pkg.Path.Type.{V1, V2, …};` — enum-variant bare-name shorthand (uppercase Type). Also `use pkg.{a, b, c};` (lowercase TYPE_NAME) — grouped sub-package import.

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
inline constexpr Code LIT_BYTES   {"LIT_BYTES",  234};   // P4-pm-07: `b"..."` byte-string literal at expression position; sema decodes escapes and lowers to an `[u8; N]` array literal.

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
inline constexpr Code SIZEOF_PACK {"SIZEOF_PACK", 203};    // sizeof...(T) — pack length as u64
inline constexpr Code QUOTE_TY    {"QUOTE_TY",    204};    // Slice 1 of quote_ty epic: `quote_ty! { type }`. TYPE = inner type AST; sema lowers to a Type{kind,name,size} struct literal.
inline constexpr Code ANTIQUOT_TYPE {"ANTIQUOT_TYPE", 205};  // `$ident` in a type position inside `quote_ty! { ... }`. NAME = identifier of a Type-valued binding; sema lowers `quote_ty! { Foo<$t> }` to `type_apply("Foo", [t])`.
inline constexpr Code ANTIQUOT_PACK {"ANTIQUOT_PACK", 206};  // `$ident...` pack-splice of an Array<Type>-valued binding in a type-arg list. NAME = the binding. Two sites: (1) inside `quote_ty! { Foo<$ts...> }` → `type_apply("Foo", ts)` (build a named type); (2) in a generic CALL's type-args `f::<$ts...>()` → marker TypeVar `__splicepack$<ident>`, expanded at mono by chasing the producer (field_types_of/args_of/type_refs_of/tuple_elems_of/typelist_tail) so a recursive generic fn folds over reflected components.
inline constexpr Code ASSOC_EQ_BIND {"ASSOC_EQ_BIND", 207};  // `Name = Type` clause inside a trait bound's `<...>` (ADR 0008). NAME = assoc-type name; TYPE = bound rhs type AST.
inline constexpr Code INSTANTIATE_DECL {"INSTANTIATE_DECL", 208};  // `instantiate Foo<T>;` / `pub instantiate Foo<T>;` — pre-instantiation root pin (C++ `template class Foo<int>;` analog). TYPE = target type AST; IS_PUB = lib-site re-export marker.
inline constexpr Code METACALL_ITEM {"METACALL_ITEM", 209};  // `metacall <call_expr>;` at module-item position. VALUE = inner call AST. Driver runs the JIT'd callee returning QuoteItemBlob and splices its emitted items at top level via `logos_emit_item_blob_subst`; METACALL_ITEM node is then marked consumed.
inline constexpr Code METACALL_ITEM_DONE {"METACALL_ITEM_DONE", 210};  // Set by the driver after a METACALL_ITEM has been invoked; sema item-dispatch silently skips. Never parsed.

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

// Writ datatypes
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
inline constexpr Code WRIT_MAP   {"WRIT_MAP",   163};  // @{k:v,...} Writ map literal
inline constexpr Code WRIT_ARRAY {"WRIT_ARRAY", 164};  // @[v,...] Writ array literal
inline constexpr Code WRIT_STR   {"WRIT_STR",   165};  // string value in Writ literal
inline constexpr Code WRIT_INT   {"WRIT_INT",   166};  // integer value in Writ literal
inline constexpr Code WRIT_FLOAT {"WRIT_FLOAT", 167};  // float value in Writ literal
inline constexpr Code WRIT_BOOL  {"WRIT_BOOL",  168};  // bool value in Writ literal
inline constexpr Code WRIT_NULL  {"WRIT_NULL",  169};  // null in Writ literal
inline constexpr Code WRIT_ENTRY        {"WRIT_ENTRY",        170};  // key:val pair in Writ map
inline constexpr Code WRIT_TYPED_ARRAY  {"WRIT_TYPED_ARRAY",  171};  // @<ElemType>[v,...] typed array literal
inline constexpr Code WRIT_NEG_INT      {"WRIT_NEG_INT",      172};  // negative integer in Writ literal: @-42
inline constexpr Code WRIT_TYPED_MAP    {"WRIT_TYPED_MAP",    173};  // @<K,V>{...} typed map literal
inline constexpr Code WRIT_CAP_IDENT   {"WRIT_CAP_IDENT",    174};  // $x capture of identifier; NAME(1) = var name
inline constexpr Code WRIT_CAP_EXPR    {"WRIT_CAP_EXPR",     175};  // ${expr} capture of expression; VALUE(7) = expr
inline constexpr Code WRIT_ARR_TYPE    {"WRIT_ARR_TYPE",     176};  // <ElemType>[] type expression (type-position); TYPE(3) = elem name
inline constexpr Code WRIT_MAP_TYPE    {"WRIT_MAP_TYPE",     177};  // <K,V>{} type expression (type-position); TYPE(3)=key, RET_TYPE(6)=val
inline constexpr Code LIST_COMP          {"LIST_COMP",           178};  // [elem for x in iter (if guard)?]; VALUE=elem, NAME=var, ITER=iter, GUARD?=pred
inline constexpr Code MAP_COMP           {"MAP_COMP",            179};  // {k: v for x in iter (if guard)?}; KEY=kexpr, VALUE=vexpr, NAME=var, ITER=iter, GUARD?=pred
inline constexpr Code WRIT_LIST_COMP   {"WRIT_LIST_COMP",    180};  // @[elem for x in iter (if guard)?]; VALUE=elem, NAME=var, ITER=iter, GUARD?=pred
inline constexpr Code WRIT_MAP_COMP    {"WRIT_MAP_COMP",     181};  // @{k: v for x in iter (if guard)?}; KEY=kexpr, VALUE=vexpr, NAME=var, ITER=iter, GUARD?=pred
inline constexpr Code PAT_WRIT_NULL    {"PAT_WRIT_NULL",     182};  // @null pattern (AnyVal == null)
inline constexpr Code PAT_WRIT_BOOL    {"PAT_WRIT_BOOL",     183};  // @true / @false pattern (AnyVal bool); VALUE(7) = true/false
inline constexpr Code PAT_WRIT_INT     {"PAT_WRIT_INT",      184};  // @<int> / @-<int> pattern (AnyVal i24); VALUE(7) = integer, IS_NEG(5)? = true
inline constexpr Code PAT_WRIT_STR     {"PAT_WRIT_STR",      185};  // @"..." pattern (AnyVal Varchar); VALUE(7) = string literal
inline constexpr Code PAT_WRIT_MAP     {"PAT_WRIT_MAP",      186};  // @{k: pat,...} pattern; ITEMS
inline constexpr Code PAT_WRIT_ARR     {"PAT_WRIT_ARR",      187};  // @[pat,...] pattern; ITEMS
inline constexpr Code PAT_WRIT_MAP_ENTRY {"PAT_WRIT_MAP_ENTRY", 188}; // KEY(8)=str key, VALUE(7)=sub-pattern
inline constexpr Code PAT_WRIT_TYPED_ARR {"PAT_WRIT_TYPED_ARR", 189}; // @<T>[..]; TYPE
inline constexpr Code PAT_WRIT_TYPED_MAP {"PAT_WRIT_TYPED_MAP", 190}; // @<K,V>{..}; TYPE, RET_TYPE?
inline constexpr Code TYPEOF_TYPE          {"TYPEOF_TYPE",         191}; // typeof(expr) type-position — compile-time type of expr; VALUE(7) = expr
inline constexpr Code OFFSET_OF            {"OFFSET_OF",           255}; // offset_of!(Type, field) — compile-time byte offset; TYPE = type_ref, NAME = field ident
inline constexpr Code SCHEMA_DEF           {"SCHEMA_DEF",          256}; // ADR 0011: schema S : code(expr)? { name: type = key, … } — typed view over a map-like Writ object. NAME, FIELDS(=SCHEMA_FIELD_DEF array), CODE_EXPR? clause sub-node.
inline constexpr Code SCHEMA_FIELD_DEF     {"SCHEMA_FIELD_DEF",    257}; // ADR 0011: schema field binding. NAME, TYPE, VALUE(=key const-expr, optional → positional), IS_PUB?
inline constexpr Code SCHEMA_ENUM_DEF      {"SCHEMA_ENUM_DEF",     258}; // ADR 0011: schema enum E { V(S), … } — closed union over schemas. NAME, FIELDS(=VARIANT_DEF array: NAME=variant, TYPE=concrete schema), CODE_EXPR? category clause.
inline constexpr Code ANNOT_KV             {"ANNOT_KV",            192}; // named annotation arg: #[A(key=lit)]; NAME(1)=key, VALUE(7)=literal node
inline constexpr Code ANNOT_POS            {"ANNOT_POS",           193}; // positional annotation arg: #[A(lit)]; VALUE(7)=literal node
inline constexpr Code ANNOT_ARR            {"ANNOT_ARR",           194}; // annotation array literal [lit,...]; ITEMS(2)=sub-literal nodes
inline constexpr Code META_BLOCK           {"META_BLOCK",          195}; // meta @{...} block; VALUE(7)=writ_lit node
inline constexpr Code GENOS_DEF            {"GENOS_DEF",           196}; // genos declaration; same structure as TRAIT_DEF
inline constexpr Code BLOCK_STMT           {"BLOCK_STMT",          197}; // bare scoping block { stmts... }; BODY = block
inline constexpr Code METACALL             {"METACALL",            198}; // metacall <call_expr>; VALUE = inner call AST
inline constexpr Code WRIT_BLOB          {"WRIT_BLOB",         199}; // sema-internal: pre-serialised Writ static blob (driver splice from metacall WritStatic return); VALUE = raw blob bytes (Varchar)
inline constexpr Code QUOTE_ITEM           {"QUOTE_ITEM",          200}; // Slice 4 of metaprog-quote: `quote_item! { item* }`. ITEMS = array of parsed item AST nodes; sema deep-clones them into a fresh module, serialises bytes, and rewrites the node into a WritStatic literal whose &str value is the splice-ready blob.
inline constexpr Code QUOTE_EXPR           {"QUOTE_EXPR",          201}; // Slice 7 of metaprog-quote: `quote_expr! { expr }`. VALUE = parsed expr AnyVal; sema deep-clones it as the root of a fresh Writ doc, sets schema_type_code=ast(CODE), and emits an ExprBlob.
inline constexpr Code REPEAT_GROUP         {"REPEAT_GROUP",        202}; // Slice 8 of metaprog-quote: `#(body)sep*` inside quote_*! body. VALUE = body expr; OP = separator (0=none, 1=`,`, 2=`&&`). Outside a quote_*! body the sema rejects it.
inline constexpr Code WRIT_TYPE_LIT      {"WRIT_TYPE_LIT",     211}; // `<type:T>` inside @-literal. TYPE(3) = simple_type AST child (TYPE_REF or GENERIC_INST) — supports primitives, bare structs, type-params in scope, and generic instantiations like Vec<u8> (3a'). Sema resolves through the type system and embeds the resolved type as a first-class Writ Type value (kind, uid, canonical name).
inline constexpr Code LIT_WSTATIC          {"LIT_WSTATIC",         212}; // WritStatic literal at type-arg position: Foo::<@{...}>. VALUE(2) = nested writ_lit AST. Sema lowers to Kind::WStaticLit with byte-hash identity over the AST (content-only, position-free).
inline constexpr Code CFG_SLOT_TYPE        {"CFG_SLOT_TYPE",       213}; // Type-position `<type:CFG.SLOT>`: extract the type at top-level slot SLOT of WritStatic-typed CFG (const-generic param or type alias). NAME(1) = CFG ident, KEY(51) = slot ident. Sema lowers to Kind::CfgSlotType; mono_subst resolves once CFG is bound.
inline constexpr Code GENERIC_REF          {"GENERIC_REF",         214};
inline constexpr Code PAT_UNIT             {"PAT_UNIT",            149}; // `()` unit pattern in let-destruct / match
inline constexpr Code PAT_FLOAT            {"PAT_FLOAT",           215}; // float literal pattern: `match x { 3.14 => ... }`. VALUE(7) = float source text.
inline constexpr Code PAT_BYTES            {"PAT_BYTES",           216};
inline constexpr Code PAT_STR              {"PAT_STR",             245}; // string-literal pattern: `match s { "foo" => ... }`. VALUE(7) = string source text (incl. quotes).
inline constexpr Code LET_PAT              {"LET_PAT",             217}; // `let <pat> = expr;` — irrefutable destructure beyond simple ident/tuple.
inline constexpr Code DOUBLE_REF_TYPE      {"DOUBLE_REF_TYPE",     218}; // `&&T` resolved by sema as nested REF_TYPE.
inline constexpr Code DOUBLE_REF_MUT_TYPE  {"DOUBLE_REF_MUT_TYPE", 219}; // `&&mut T`.
inline constexpr Code DEREF_COMPOUND       {"DEREF_COMPOUND",     220}; // `*p op= val;` — sema lowers to deref_write of *p = *p OP val.
inline constexpr Code CHAINED_CMP          {"CHAINED_CMP",        221}; // `a < b < c` (2+ comparators); sema rejects with helpful diag (B-ex-08).
inline constexpr Code PAREN_TYPE           {"PAREN_TYPE",         222}; // `(T)` — paren-wrapped type; sema unwraps to inner (B-ty-09).
inline constexpr Code TAIL_EXPR            {"TAIL_EXPR",          223}; // `expr` (no SEMI) at stmt position; sema synthesizes implicit `return expr` for non-void fns (B-fn-06).
inline constexpr Code LIT_CHAR             {"LIT_CHAR",           224}; // `'X'` Unicode scalar literal. VALUE = original char-lit text including quotes.
inline constexpr Code FN_MACRO_CALL        {"FN_MACRO_CALL",      225}; // `name!(args)` / `name![args]` — function-style macro invocation. CALLEE(8) = ident, ARGS(11) = items array. Sema resolves CALLEE against #[fn_macro] fns and lowers via metacall pipeline with ARGS as ExprBlobs.
inline constexpr Code FN_MACRO_CALL_ITEM   {"FN_MACRO_CALL_ITEM", 226}; // `name!{...}` at module item position. Same RAW_TEXT capture as FN_MACRO_CALL but routes through the metacall_item pipeline (callee returns ItemList / QuoteItemBlob → items spliced).
inline constexpr Code FN_MACRO_CALL_ITEM_DONE{"FN_MACRO_CALL_ITEM_DONE", 227}; // Set by the driver after a FN_MACRO_CALL_ITEM has been spliced.
inline constexpr Code PAT_CHAR             {"PAT_CHAR",           228}; // char-literal pattern (`'A'` in match arm). VALUE = original CHAR_LIT text.
inline constexpr Code PAT_CHAR_RANGE       {"PAT_CHAR_RANGE",     229}; // char-range pattern (`'a' ..= 'z'`). LHS / RHS = original CHAR_LIT text for each endpoint.
inline constexpr Code INVOKE_EXPR          {"INVOKE_EXPR",        230}; // `(expr)(args)` — IIFE / expression-as-callee (P4-pm-16). RECEIVER = callee expression, ARGS = arg-list. Sema routes through closure-call / fn-ptr-call.
inline constexpr Code BREAK_EXPR           {"BREAK_EXPR",         231}; // `break` in expression position (P3-pg-04). Sema lowers as SBreak stmt + Error-typed sentinel so type-checks accept.
inline constexpr Code CONTINUE_EXPR        {"CONTINUE_EXPR",      232}; // `continue` in expression position (P3-pg-04). Same shape as BREAK_EXPR.
inline constexpr Code RETURN_EXPR          {"RETURN_EXPR",        233}; // `return` in expression position (P3-pg-04). Same shape — bare form only.
inline constexpr Code NESTED_FN            {"NESTED_FN",          235}; // `fn name(params) [-> T] { body }` at stmt position. Sema lowers to a let-bound closure (no captures expected). NAME, PARAMS, RET_TYPE?, BODY.
inline constexpr Code UNSIZED_SLICE_TYPE   {"UNSIZED_SLICE_TYPE", 236}; // Phase 1B: bare `[T]` — unsized slice type. TYPE = element. Resolves to Kind::UnsizedSlice.
inline constexpr Code DOC_LINE_LIT         {"DOC_LINE_LIT",       237}; // `/// text` outer doc-comment line. VALUE = raw token text including the leading `///`. sema_collect strips prefix and accumulates into pending_doc_ for attachment to the next item.
inline constexpr Code INNER_DOC_LIT        {"INNER_DOC_LIT",      238}; // `//! text` inner doc-comment line (module-level summary). VALUE = raw token text including the leading `//!`. Accumulated into LProgram.module_inner_doc.
inline constexpr Code DOC_BLOCK_LIT        {"DOC_BLOCK_LIT",      239}; // `/** ... */` outer block doc-comment. Phase A.4.
inline constexpr Code INNER_DOC_BLOCK_LIT  {"INNER_DOC_BLOCK_LIT",240}; // `/*! ... */` inner block doc-comment. Phase A.4.
inline constexpr Code INNER_ANNOTATION     {"INNER_ANNOTATION",   242}; // Three-layer split Phase 3.2: `#![name]` / `#![name(args)]` / `#![name=val]` — file/module-level inner attribute. Currently used only for `#![no_implicit_prelude]`. NAME = ident; ARGS / VALUE optional, same shape as ANNOTATION.
inline constexpr Code DESTRUCTURE_ASSIGN   {"DESTRUCTURE_ASSIGN", 243}; // G149-7 (RFC 2909): destructuring assignment into existing places. OP=0 tuple / 1 array (NAMES=pat_binding_list), 2 struct (NAME=struct, FIELDS=pat_field_list); VALUE=rhs. Sema desugars to a temp let + per-place assigns.
inline constexpr Code PLACE_ASSIGN         {"PLACE_ASSIGN",       244}; // G163-2: general place write — postfix-chain lvalue (a[i][j], (*p).0, deep mixes) the specialized writes miss. RECEIVER = place read-expr, VALUE = rhs. Sema lowers to deref_write(&mut place, rhs).
inline constexpr Code AUTO_TRAIT_BOUND     {"AUTO_TRAIT_BOUND",   246}; // logos-core 2.4(c): `+ Send`/`+ Sync` auto-trait bound on a trait object. NAME = bound identifier. Collected into the DYN_TYPE's ITEMS array alongside type-args; resolve_type filters by CODE to fold into TraitObject's const_val bits (Send=8, Sync=9) so the unsize coercion can enforce `T: Bound` at the coercion site.
inline constexpr Code AUTO_LIFE_BOUND      {"AUTO_LIFE_BOUND",    247}; // logos-core 2.4(c): `+ 'a` lifetime bound on a trait object. NAME = lifetime label. Recorded by grammar but not yet enforced — closes against §2.1 region_infer wiring.
inline constexpr Code ANNOT_CALL           {"ANNOT_CALL",         248}; // logos-core §6.8: nested combinator inside an annot_args list (`#[cfg(all(unix, target_arch="x86_64"))]`). NAME = head ident (all/any/not/...); ARGS = nested annot_args list. evaluate_cfg_annotation handles ANNOT_CALL recursively to support cfg combinators in attribute position.
inline constexpr Code EXTERN_BLOCK         {"EXTERN_BLOCK",       249}; // logos-core §6.7: `extern "ABI" { extern_fn_def* }` block form. VALUE = ABI string literal (omitted ≡ default Logos-internal); ITEMS = array of EXTERN_FN nodes — each individually carries VALUE=block-ABI or its own per-fn override. sema_collect splices the block's items into the module-level item stream with the block's ABI applied as a default to children that don't have their own.
inline constexpr Code IF_LET_CHAIN         {"IF_LET_CHAIN",       250}; // logos-core §6.4: `if let P1 = e1 && let P2 = e2 && cond { THEN } else { ELSE }`. ITEMS = list of LET_CHAIN_LET / LET_CHAIN_COND segments. THEN/ELSE branches like IF. Sema desugars to a flat sequence of refutable binds + cond checks; chain falls to ELSE on any failure.
inline constexpr Code LET_CHAIN_LET        {"LET_CHAIN_LET",      251}; // §6.4: let-bind seg of IF_LET_CHAIN. PAT = pattern, VALUE = scrutinee.
inline constexpr Code LET_CHAIN_COND       {"LET_CHAIN_COND",     252}; // §6.4: bool-cond seg of IF_LET_CHAIN. VALUE = bool expression.
inline constexpr Code UNION_DEF            {"UNION_DEF",          253}; // logos-core §6.1: `union NAME { f1: T1, f2: T2, … }`. NAME = union type name; FIELDS = field array (same shape as STRUCT); TYPE_PARAMS optional. Sema treats unions as Struct-shaped types with `is_union=true`; layout = max-of-fields aligned to max-alignment; field access requires `unsafe` block.
inline constexpr Code STATIC_DEF           {"STATIC_DEF",         254}; // logos-core §6.2: `static [mut] NAME: T = expr;` — true global storage (one llvm.mlir.global per item; stable address). NAME/TYPE/VALUE same shape as CONST_DEF; IS_MUT only on the `mut` form (reads/writes require `unsafe`, Rust `items.static.mut.safety`); no VALUE ⇒ extern-block declaration (external linkage). Sema: module_statics_ (all) + module_static_muts_ (mut only).

// Index field key for tuple_field_write_stmt (integer field index)
inline constexpr Key  META            {"META",               16};   // meta @{...} block node on struct/trait/datatype declarations (reuses PATH_PARTS slot; these node types never co-exist)
inline constexpr Key  RAW_TEXT        {"RAW_TEXT",            7};   // raw source text captured from a balanced delim group on FN_MACRO_CALL (reuses VALUE slot — fn-macro nodes never carry a literal payload)
inline constexpr Key  DOC             {"DOC",                50};   // joined `///` outer-doc text on item-level decls (reuses HI_NEG slot — PAT_RANGE never appears at item level)
inline constexpr Key  INDEX           {"INDEX",              43};   // integer index (tuple field write)

// Visibility
inline constexpr Key IS_PUB    {"IS_PUB",    33};           // visibility flag (1 = pub)
inline constexpr Key PAT       {"PAT",       34};            // pattern for if let / while let
inline constexpr Key GUARD     {"GUARD",     35};            // guard condition in match arm
inline constexpr Key EXPR      {"EXPR",      36};            // expression-body match arm value
inline constexpr Key NAMES     {"NAMES",     37};            // name list for tuple destructuring
inline constexpr Key WHERE     {"WHERE",     38};            // where clause node
inline constexpr Key IS_UNSAFE {"IS_UNSAFE", 42};            // unsafe fn marker
inline constexpr Key IS_NEGATIVE{"IS_NEGATIVE",43};          // negative impl marker (impl !Trait for X {})
inline constexpr Key IS_MOVE   {"IS_MOVE",   44};            // move closure marker
inline constexpr Key IS_REF    {"IS_REF",    45};            // &self / &mut self param shorthand
inline constexpr Key BASE      {"BASE",      46};            // struct update base expression (..base)
inline constexpr Key LABEL     {"LABEL",     47};            // loop label (e.g. "'outer")
inline constexpr Key SUPERS    {"SUPERS",    48};            // supertrait bound list on TRAIT_DEF
inline constexpr Key LO_NEG    {"LO_NEG",    49};            // lo bound is negative (PAT_RANGE)
inline constexpr Key HI_NEG    {"HI_NEG",    50};            // hi bound is negative (PAT_RANGE)
inline constexpr Key KEY       {"KEY",       51};            // map key in WRIT_ENTRY (string or int token)
inline constexpr Key CODE_EXPR {"CODE_EXPR", 6};             // ADR 0011: `code(expr)` clause sub-node on SCHEMA_DEF/SCHEMA_ENUM_DEF (reuses RET_TYPE; AST TOM keys are 0..51)
inline constexpr Key PATH      {"PATH",      22};            // sub-node {ITEMS:[str,…]} for N-deep chain field write (reuses FIELDS slot — chain stmts never carry struct field defs)
inline constexpr Key NAME_VAR  {"NAME_VAR",  38};            // antiquot var name for `#ident` placeholder inside quote_*! body (reuses WHERE slot)
// VARIANT_DEF reuses LO_NEG (49) as "discriminant is negative" flag.
// PAT_WRIT_INT reuses LO_NEG (49) as "integer is negative" flag — same semantics.
// WRIT_ENTRY reuses LO_NEG (49) as "negation flag" (writ entries never have LO_NEG).
// WRIT_TYPED_MAP reuses TYPE (3) for key type and RET_TYPE (6) for val type.
inline constexpr Key IMPL_TYPE_PARAMS{"IMPL_TYPE_PARAMS", 41}; // impl<T> own type params
inline constexpr Key HRTB_BINDERS    {"HRTB_BINDERS",     41}; // `for<'a, 'b>` binder list on a TRAIT_BOUND (reuses IMPL_TYPE_PARAMS slot — trait bounds never carry impl-type-params). Value is a sub-node {ITEMS:[LIFETIME str,...]}.
inline constexpr Key RELAXED         {"RELAXED",          39}; // `?Trait` relaxed-bound marker on TRAIT_BOUND (Phase 1: only `?Sized` accepted; opts out of implicit Sized bound on the parent type param).
inline constexpr Key TYPE_NAME       {"TYPE_NAME",         3}; // CP-cm-02: enum-type name in USE_VARIANTS. Reuses TYPE slot — USE never carries a type AST node.
inline constexpr Key VARIANTS        {"VARIANTS",         22}; // CP-cm-02: array of variant-name sub-nodes (each {NAME}) in USE_VARIANTS. Reuses FIELDS slot — USE never carries struct fields.
inline constexpr int32_t VIS_PRIVATE = 0;
inline constexpr int32_t VIS_PUBLIC  = 1;

// ── Group: mod (MODULE, USE) ────────────────────────────────────────────────
// Fields local to module-level nodes.  Slot numbers here share the 0..51 space
// with the globals but can reuse slots that are never populated on mod nodes.
namespace mod {
    inline constexpr Key PATH_PARTS {"PATH_PARTS", 16};  // array of {NAME} per component after the first
    // §3 module system: `use pkg from <module>;`. FROM_MODULE = target module
    // name as a {NAME} sub-node (bare IDENT, or a quoted string sema strips);
    // FROM_KW = the contextual `from` keyword word (sema validates == "from").
    // Reuse BODY/THEN slots — USE/MODULE nodes never carry a body/then-branch.
    inline constexpr Key FROM_MODULE {"FROM_MODULE", 5};
    inline constexpr Key FROM_KW     {"FROM_KW",     9};
}

// P4-pm-01: fields scoped to VARIANT_DEF / ENUM_LIT_DATA / PAT_VARIANT_DATA
// nodes — they never carry a loop label, so slot 47 (global LABEL) is safe
// to reuse here. Grouping documents the intent and keeps the global slot
// space clean.
namespace variant {
    inline constexpr Key IS_STRUCT_SHAPE {"IS_STRUCT_SHAPE", 47};
}

} // namespace logos::compiler::ast
