// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Logos Typed IR (L-IR) — produced by the semantic analysis pass.
//
// L-IR is the compiler's first fully-typed intermediate representation.
// Every expression node carries a `const LogosType* type`.  All name
// lookups, parentheses, and type aliases have been resolved.  The IR
// is suitable for monomorphisation (Batch D) without re-running sema.
//
// Ownership: all LogosType* pointers inside an LProgram are owned by
// LProgram::type_pool (a TypePool value).  Do not outlive LProgram.
//
// Node types use std::variant + std::unique_ptr for recursive sub-nodes.

#pragma once

#include <logos/compiler/sema.hpp>   // LogosType, TypePool, SemaResult
#include <logos/compiler/str_map.hpp>
#include <unordered_set>
#include <string>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace logos::compiler::lir {

// ── Forward declarations ──────────────────────────────────────────────────

struct LExpr;
struct LStmt;
struct LBlock;

using LExprPtr  = std::unique_ptr<LExpr>;
using LBlockPtr = std::unique_ptr<LBlock>;

// ── Patterns (for match arms) ─────────────────────────────────────────────

struct PatVariant { std::string enum_name; std::string variant; int64_t disc; };
struct PatInt     { int64_t value; };
struct PatBool    { bool value; };
struct PatWild    { std::string name; };   // _ or named wildcard (name may be "_")

// Pattern with payload bindings: Option::Some(x) => { use x }
struct PatVariantData {
    std::string enum_name;
    std::string variant;
    int64_t     disc;
    std::vector<std::string>        bindings;      // bound variable names
    std::vector<const LogosType*>   binding_types;  // their types
};

// OR pattern: 1 | 2 | 3 — each alternative must be a non-OR pattern.
struct PatOr;
// Tuple pattern: (a, b, c) — matches a tuple, binding each element.
struct PatTuple;
// Range pattern: lo..=hi inclusive integer range.
// G4 note: lo/hi are int64_t; u64 values > INT64_MAX are unsupported (sema rejects via intlit_fits).
struct PatRange { int64_t lo; int64_t hi; };
// Struct pattern: Point { x: p, y } — binds/tests struct fields.
struct PatStruct;
// Slice pattern: [a, b] or [first, .., last].
struct PatSlice;
// @ binding: name @ sub_pat — binds scrutinee to name AND tests sub_pat.
struct PatAt;
// ref / ref mut binding — binds scrutinee by reference.
struct PatRefBind {
    std::string      name;
    bool             is_mut;
    const LogosType* bind_type;  // &T or &mut T
};
// Reference pattern: &pat or &mut pat — strips one level of indirection.
struct PatRefPat;

using Pattern = std::variant<
    PatVariant, PatInt, PatBool, PatWild, PatVariantData, PatOr, PatTuple,
    PatRange, PatStruct, PatSlice, PatAt, PatRefBind, PatRefPat>;

struct PatOr   { std::vector<Pattern> alts; };
struct PatTuple {
    std::vector<std::string>        bindings;      // bound variable names (or "_" to skip)
    std::vector<const LogosType*>   binding_types; // their types (filled by sema)
    std::vector<Pattern>            subs;          // sub-pattern per element (parallel to bindings)
};
// PatFieldBinding: sub empty = shorthand binding, sub[0] = explicit sub-pattern.
struct PatFieldBinding {
    std::string          field_name;
    std::vector<Pattern> sub;   // 0 = shorthand, 1 = explicit
};
struct PatStruct {
    std::string                   struct_name;
    std::vector<PatFieldBinding>  fields;
    bool                          has_rest;
};
struct PatSlice {
    std::vector<Pattern>  prefix;   // elements before ..
    std::vector<Pattern>  rest;     // 0 = no rest, 1 = has rest (..)
    std::vector<Pattern>  suffix;   // elements after ..
};
struct PatAt {
    std::string          name;
    std::vector<Pattern> sub;   // exactly 1 element: the inner pattern
    const LogosType*     type;
};
struct PatRefPat {
    std::vector<Pattern> inner;  // exactly 1 element: the dereferenced pattern
    bool                 is_mut;
};

struct LMatchArm {
    Pattern                  pat;
    LBlockPtr                body;   // arm body (single stmts are wrapped in a 1-stmt block)
    std::optional<LExprPtr>  guard;  // if-guard: arm only matches when guard is true
};

// ── Hermes SDN literal tree ───────────────────────────────────────────────

struct HermesVal;
using HermesValPtr = std::unique_ptr<HermesVal>;

struct HVNull  {};
struct HVBool  { bool value; };
struct HVInt   { int64_t value; };
struct HVFloat { double value; };
struct HVStr   { std::string value; };

struct HVMapEntry {
    std::variant<std::string, int64_t> key;
    HermesValPtr val;
};

struct HVMap   {
    std::vector<HVMapEntry> entries;
    std::string key_type;  // "" = ObjectMap (tc=101); "I32" = MapI32AnyVal (tc=105)
};
struct HVArray {
    std::vector<HermesValPtr> elements;
    std::string elem_type;  // "" = AnyVal (ObjectArray tc=100); "I32" = ArrayI32 tc=104; "U64" = ArrayU64 tc=108
};

// Runtime capture placeholder: $x or ${expr} inside an @-literal.
// param_index: position of this PARAM slot in the template blob (0-based, unique per slot).
// value_index: deduplicated capture value (multiple slots can share one value_index if
//              they capture the same pure identifier — one coercion, multiple writes).
struct HVCapture {
    uint32_t param_index;   // slot position in template (unique)
    uint32_t value_index;   // which resolved value to use (deduplicated)
};

struct HermesVal {
    std::variant<HVNull, HVBool, HVInt, HVFloat, HVStr, HVMap, HVArray, HVCapture> kind;
};

// A Hermes SDN literal (@{...}, @[...], @scalar) lowered to a tree.
// If has_captures == false: pure compile-time blob (current ZoneBuilder path).
// If has_captures == true:  template blob + runtime substitution.
//   capture_exprs[v] = Logos expression for value_index v.
//   capture_types[v] = resolved LogosType* for value_index v (for coercion).
//   capture_param_count = total number of PARAM slots in template.
struct EHermesLit {
    HermesValPtr root;
    bool has_captures = false;
    std::vector<LExprPtr>                    capture_exprs;   // one per unique value
    std::vector<const struct LogosType*>     capture_types;   // one per unique value
    uint32_t                                 capture_param_count = 0; // total slots
};

// ── Expression node payloads ──────────────────────────────────────────────

struct ELitInt    { int64_t value; };
struct ELitFloat  { double value; };
struct ELitBool   { bool value; };
struct ELitStr    { std::string value; };

struct EVarRef    { std::string name; };

struct EEnumLit   {
    std::string enum_name;
    std::string variant;
    int64_t     disc;           // discriminant value (fits backing type, default i32)
};

// Enum variant with payload: Option::Some(42)
struct EEnumLitData {
    std::string           enum_name;
    std::string           variant;
    int64_t               disc;
    std::vector<LExprPtr> payload;  // payload values
};

struct ECall      {
    std::string                   callee;
    std::vector<const LogosType*> type_args;  // empty for non-generic calls
    std::vector<LExprPtr>         args;
};

struct EMethodCall {
    LExprPtr                      receiver;
    std::string                   method;
    // Concrete function symbol selected by sema for direct calls.
    // Empty means "resolve by receiver type + method name" in later phases.
    std::string                   resolved_symbol{};
    std::vector<const LogosType*> type_args;  // [NEW] for generic methods
    std::vector<LExprPtr>         args;
    int32_t                       vtable_index = -1;  // -1 = direct (struct), >=0 = virtual (class)
    std::string                   resolved_type{};    // class/struct where method is defined (for inherited methods)
    // Tag-dispatch info: set when receiver is &tagged<TS> Trait.
    // When non-empty, codegen reads type_code via TS and dispatches through
    // the @__logos_tag_dispatch_<tag_system>_<trait_name>_<method> table.
    std::string                   tag_system{};       // e.g. "DataTypeTagSystem" (empty = not tagged dispatch)
    std::string                   tag_trait{};        // e.g. "Stringify"
};

struct EBinOp {
    std::string op;             // "+", "-", "==", "&&", ...
    LExprPtr    lhs;
    LExprPtr    rhs;
};

struct EUnary {
    std::string op;             // "-" or "!"
    LExprPtr    operand;
};

// & address-of: returns alloca pointer for a variable (does not dereference)
struct EAddrOf {
    std::string var_name;
};

// Address of a temporary rvalue: &expr where expr is not a named variable.
// Codegen spills the inner expression to an anonymous alloca.
struct EAddrOfTemp {
    LExprPtr inner;
    bool     is_mut = false;  // true → &mut T, false → &T
};

struct EDeref {
    LExprPtr operand;
};

struct EFieldRead {
    LExprPtr    receiver;
    std::string field;
};

struct EIndexRead {
    LExprPtr receiver;
    LExprPtr index;
};

struct EStructLit {
    std::string name;
    std::vector<std::pair<std::string, LExprPtr>> fields;
};

struct EArrLit {
    std::vector<LExprPtr> elems;
    // element type is LExpr::type->elem
};

struct ECast {
    LExprPtr operand;
    // target type is LExpr::type.
    // For Hermes typed container casts (e.g. &[i32] as <I32>[]):
    //   hermes_build_fn names the stdlib builder (e.g. "hermes_build_array_i32").
    //   source type is Slice; result type is Hermes.
    std::string hermes_build_fn = {};  // empty = ordinary numeric/pointer cast
};

// Class heap allocation: new ClassName { field: val, ... }
// Returns a *mut ClassName (pointer to heap-allocated class instance).
struct ENew {
    std::string class_name;
    std::vector<std::pair<std::string, LExprPtr>> fields;
};

// if cond { then_val } else { else_val }  — used when if is an expression.
// Both branches must yield the same type.
struct EIfExpr {
    LExprPtr cond;
    LExprPtr then_val;
    LExprPtr else_val;
};

// Match expression arm: pattern [guard] => expr
struct EMatchArm {
    Pattern                  pat;
    std::optional<LExprPtr>  guard;
    LExprPtr                 value;
};

// match expr { pat => val, ... } — produces a value
struct EMatchExpr {
    LExprPtr               scrut;
    std::vector<EMatchArm> arms;
};

// Tuple literal: (a, b, c)
struct ETupleLit {
    std::vector<LExprPtr> elems;
};

// Tuple element access: t.0, t.1
struct ETupleIndex {
    LExprPtr  receiver;
    uint32_t  index;
};

// Closure: wrapper for the full EClosure (defined after LBlock).
// Uses unique_ptr to break the dependency cycle.
struct EClosure;
struct EClosureBox {
    std::unique_ptr<EClosure> inner;
};

// Closure call: closure(args...)
struct EClosureCall {
    LExprPtr              callee;
    std::vector<LExprPtr> args;
};

// Call via fn(T) -> R bare function pointer (no env_ptr, no fat pointer).
struct EFnPtrCall {
    LExprPtr              callee;  // EVarRef to the fn-ptr variable
    std::vector<LExprPtr> args;
};

// Slice construction: &arr (whole array → slice) or &arr[lo..hi]
struct ESliceLit {
    LExprPtr base;    // pointer to first element
    LExprPtr len;     // length as i64
};

// Slice element access: s[i]
struct ESliceIndex {
    LExprPtr slice;
    LExprPtr index;
};

// Slice length: s.len()
struct ESliceLen {
    LExprPtr slice;
};

// Slice / str as_ptr: s.as_ptr() → *const u8
struct ESlicePtr {
    LExprPtr slice;
};

// format() compiler built-in: format("x={}, y={}", x, y)
// Returns *mut u8 (heap-allocated, caller frees via format_free).
// The compiler builds tags[] and data[] arrays and calls __format_impl.
struct EFormatCall {
    LExprPtr                    fmt;        // format string expr
    std::vector<LExprPtr>       args;       // arguments (without fmt)
    std::vector<const LogosType*> arg_types; // parallel to args, resolved at sema
};

// Variadic pack expansion: args... in function body.
// Expanded by mono into individual EVarRef nodes.
struct EPackExpand {
    std::string var_name;  // the pack variable being expanded (e.g. "rest")
};

// sizeof::<T>() — size in bytes of type T, computed at compile time via GEP trick.
struct ESizeOf {
    const LogosType* elem_type = nullptr;
};

// Raw-pointer arithmetic intrinsic methods:
//   p.byte_add(n)         — byte_add: offset n bytes, result same pointer type
//   p.byte_sub(n)         — byte_sub: offset -n bytes
//   p.add(n) / p.sub(n)   — scale n by sizeof(pointee), then byte offset
//   p.byte_offset_from(q) — (p - q) in bytes, result i64
//   p.offset_from(q)      — (p - q) / sizeof(pointee), result i64
struct EPtrArith {
    enum Op { ByteAdd, ByteSub, Add, Sub };
    Op       op;
    LExprPtr ptr;
    LExprPtr offset;
};

struct EPtrDiff {
    bool     by_byte;   // true = byte distance, false = element distance
    LExprPtr lhs;
    LExprPtr rhs;
};

// type_code_of::<T>() — Hermes wire-format type_code of T.  Deferred to mono
// so each instantiation of a generic function gets T's own type_code.
struct ETypeCodeOf {
    const LogosType* elem_type = nullptr;
};

// Try expression: expr? — extract Ok(v) or early-return Err(e).
// inner must have enum type "Result" with 2 type args [T, E].
// ok_disc / err_disc are the discriminant values for Ok and Err variants.
// The ETry expression itself has type T (the Ok payload type).
struct ETry {
    LExprPtr inner;
    int32_t  ok_disc  = 0;   // discriminant of Ok  (typically 0)
    int32_t  err_disc = 1;   // discriminant of Err (typically 1)
};

// Represents an inline block of statements returning a final value
struct EBlockExpr {
    std::unique_ptr<LBlock> block;
    LExprPtr result; // may be null if it evaluates to void
};


// reflect::<T>() — returns HermesStatic view of T's TypeInfo rodata global.
struct EReflectOf { const LogosType* type; };

// ── Expression node ───────────────────────────────────────────────────────

struct LExpr {
    const LogosType* type = nullptr;   // always set; error_t() on ill-typed nodes
    std::variant<
        ELitInt, ELitFloat, ELitBool, ELitStr, EVarRef, EEnumLit, EEnumLitData,
        ECall, EMethodCall, EBinOp, EUnary, EAddrOf, EAddrOfTemp, EDeref,
        EFieldRead, EIndexRead, EStructLit, EArrLit, ECast, ENew, EIfExpr,
        ETupleLit, ETupleIndex, ESliceLit, ESliceIndex, ESliceLen, ESlicePtr,
        EClosureBox, EClosureCall, EFnPtrCall, EFormatCall, EPackExpand,
        ETry, EMatchExpr, ESizeOf, ETypeCodeOf, EBlockExpr,
        EHermesLit, EPtrArith, EPtrDiff, EReflectOf
    > kind;
};

// ── Statement node payloads ───────────────────────────────────────────────

struct SLet {
    std::string      name;
    const LogosType* type;         // concrete type (annotations resolved; IntLit → i32)
    bool             is_mut;
    LExprPtr         value;
};

struct SAssign    { std::string name; LExprPtr value; };

struct SReturn    { LExprPtr value; };   // value is null for void return

// else_: null → no else; block with single SIf → else-if chain
struct SIf {
    LExprPtr                  cond;
    LBlockPtr                 then_;
    std::optional<LBlockPtr>  else_;
};

struct SWhile {
    LExprPtr  cond;
    LBlockPtr body;
    std::string label;  // optional loop label (e.g. "'outer"), empty = unlabeled
};

struct SFor {
    std::string      var;
    LExprPtr         lo;
    LExprPtr         hi;
    bool             inclusive;
    LBlockPtr        body;
    std::string      label;  // optional loop label, empty = unlabeled
};

struct SLoop {
    LBlockPtr        body;
    const LogosType* result_type = nullptr;  // non-null when loop yields a value
    std::string      break_slot;             // alloca name for the break value (non-empty ↔ result_type != null)
    std::string      label;                  // optional loop label, empty = unlabeled
};
struct SBreak     { LExprPtr value; std::string label; };  // label: target loop label (may be empty)
struct SContinue  { std::string label; };                   // label: target loop label (may be empty)
struct SBlock     { LBlockPtr body; };  // scoping block statement

struct SFieldWrite {
    std::string receiver;
    std::string field;
    LExprPtr    value;
};

struct SIndexWrite {
    std::string arr;
    LExprPtr    index;
    LExprPtr    value;
};

// a.field[index] = value — field index write (e.g. self.ptr[i] = val)
struct SFieldIndexWrite {
    std::string receiver;   // struct/class variable
    std::string field;      // pointer-typed field name
    LExprPtr    index;
    LExprPtr    value;
};

// (*ptr_var).field = value  — field write through a named pointer variable
struct SDerefFieldWrite {
    std::string receiver;    // variable name (holds *mut ClassName)
    std::string type_name;   // class or struct name of the pointee
    std::string field;
    LExprPtr    value;
};

// a.mid_field.field = value  — chained field write (2 levels deep)
// Emitted as two GEPs: outer struct → mid field ptr → inner field ptr → store.
struct SChainFieldWrite {
    std::string receiver;    // outer variable name
    std::string mid_field;   // intermediate field name
    std::string field;       // final field name
    LExprPtr    value;
};

struct SExprStmt  { LExprPtr expr; };

struct SDelete    { LExprPtr expr; };   // delete ptr — call free on a class pointer

// *ptr = value;  — write through a raw pointer
struct SDerefWrite { LExprPtr ptr; LExprPtr value; };

// var.N = value;  — tuple field write (N is a small integer index)
struct STupleWrite {
    std::string      receiver;      // local variable holding the tuple
    uint32_t         index;         // field index (0, 1, ...)
    LExprPtr         value;
    const LogosType* recv_type = nullptr;  // LogosType of the tuple variable
};

// for item in array { body } — iterates over a fixed-size array
struct SForEach {
    std::string      var;         // loop variable name (item)
    LExprPtr         iter;        // the array or slice expression
    const LogosType* elem_type;   // element type
    int64_t          arr_size;    // static array size; 0 for slices
    bool             is_slice = false;  // true → iter is &[T] (dynamic length from fat pointer)
    LBlockPtr        body;
};

struct SMatch {
    LExprPtr               scrut;
    std::vector<LMatchArm> arms;
};

// let-else: let Pat = expr else { block (must diverge) };
// After this statement, the pattern's bindings are in scope.
struct SLetElse {
    Pattern               pat;        // the irrefutable-or-test pattern
    LExprPtr              scrut;      // scrutinee expression
    LBlockPtr             else_block; // must-diverge block
};

// Auto-generated drop call: Type__drop(var) at scope exit
struct SDrop {
    std::string      var_name;
    std::string      drop_fn;          // user's explicit drop (may be empty)
    const LogosType* type;
    bool             drop_fields = false;  // auto-drop droppable fields after drop_fn
};

// ── Statement node ────────────────────────────────────────────────────────

struct LStmt {
    uint32_t line = 0;             // source line (0 = unknown)
    std::variant<
        SLet, SAssign, SReturn, SIf, SWhile, SFor, SLoop,
        SBreak, SContinue, SBlock, SFieldWrite, SIndexWrite, SFieldIndexWrite, SExprStmt, SMatch, SDelete, SForEach, SDerefWrite,
        SDrop, SDerefFieldWrite, STupleWrite, SLetElse, SChainFieldWrite
    > kind;
};

// ── Block ─────────────────────────────────────────────────────────────────

struct LBlock {
    std::vector<LStmt> stmts;
};

// ── Top-level declarations ────────────────────────────────────────────────

struct LParam {
    std::string      name;
    const LogosType* type;
    bool             is_variadic = false;  // variadic pack parameter
};

// EClosure — defined after LParam and LBlock (both needed).
struct EClosure {
    std::string                     closure_id;
    std::vector<LParam>             params;
    const LogosType*                ret_type = nullptr;
    LBlock                          body;
    bool                            is_move = false;
    std::vector<std::string>        captures;
    std::vector<const LogosType*>   capture_types;
    // When true: non-capturing closure coerced to fn ptr; emitted without env_ptr.
    bool                            as_fn_ptr = false;
};

struct LFunction {
    std::string              name;
    std::vector<TypeParam>   type_params;    // TypeVar names (generic def, empty otherwise)
    std::vector<std::string> lifetime_params; // Lifetime param names, e.g. ["'a", "'b"]
    std::vector<LParam>      params;
    const LogosType*         ret_type  = nullptr;
    LBlock                   body;
    bool                     is_extern = false;
    bool                     is_vararg = false;
    bool                     is_pub    = false;

    // Specialisation support (set by sema, cleared by mono after instantiation).
    // is_specialization == true  →  this is a specialisation of `name`.
    // spec_patterns: one LogosType* per type-param position; may contain TypeVar
    //   for partial specialisations (e.g. fn foo<*T> → pattern = *const TypeVar<T>).
    bool                          is_specialization = false;
    std::vector<const LogosType*> spec_patterns;

    // Set when this function was loaded from a binary module (.hermes0 in .a).
    // Non-generic functions with this flag are forward-declared only; the linker
    // resolves them from the archive's .o member.
    bool from_binary_module = false;

    // Impl-level type params (with their bounds) that were stripped from
    // type_params when this method was attached to a generic struct template.
    // Preserved so mono can check whether the impl bound is satisfied for the
    // struct's concrete type args before instantiating this method.
    // Each entry: bound on struct's type-param at position `index` within the
    // struct's type_params.  Empty when no impl-level type params.
    std::vector<TypeParam>        impl_type_params;
};

struct LField {
    std::string      name;
    const LogosType* type;
    bool             is_variadic = false;
};

// User annotation (Java-like). Values are literal-only (variant a of the plan):
// string, int, float, bool, enum variant, or homogeneous-literal array.
struct LAnnotationValue {
    enum class Kind { Int, Float, Bool, Str, Enum, Array };
    Kind                          kind = Kind::Int;
    int64_t                       i = 0;                 // Int, Bool (0/1)
    double                        f = 0.0;               // Float
    std::string                   s;                     // Str, Enum.enum_name, Enum.variant concat
    std::string                   enum_name;             // Enum
    std::string                   enum_variant;          // Enum
    std::vector<LAnnotationValue> arr;                   // Array
};

struct LAnnotationInstance {
    std::string                                  ann_name;     // annotation datatype name (e.g. "Deprecated")
    std::string                                  ann_pkg;      // package that declared it
    // Resolved fully-qualified name (pkg::Name); used for equality in has_annotation.
    std::string                                  ann_fqn;
    // Ordered list of (field_name, value) pairs as specified at the use site.
    // Positional args get resolved to field names during sema.
    std::vector<std::pair<std::string, LAnnotationValue>> kv;
};

struct LStructDef {
    std::string              name;
    std::string              pkg;            // package that declares this struct/datatype
    std::vector<TypeParam>   type_params;    // empty for non-generic structs
    std::vector<std::string> lifetime_params; // e.g. ["'a", "'z"]; erased at codegen
    std::vector<LField>      fields;
    std::vector<LFunction>   methods;
    bool                     is_pub        = false;
    bool                     is_zoned   = false;  // Hermes datatype (C POD layout)
    uint64_t                 type_code     = 0;      // explicit #[type_code=N]; 0 = auto-assign
    bool                     is_data_plain = true;   // no relative-ptr fields → value-copyable
    bool                     from_binary_module = false;  // loaded from binary archive
    // SHA-256 of canonical type string, truncated to 23 bytes; all-zero = not yet computed
    // (zero for generic templates — hashed at instantiation time in mono_pass).
    std::array<uint8_t, 23>  type_hash     = {};

    // User-annotation metadata.
    bool                             is_annotation_type = false;  // true → this datatype is itself a `#[annotation]` marker type
    std::vector<LAnnotationInstance> annotations;                  // user-annotations attached to this type
    std::shared_ptr<HermesVal>       meta_val;                     // meta @{...} block; null if absent

    // Specialisation support (mirrors LFunction).
    bool                          is_specialization = false;
    std::vector<const LogosType*> spec_patterns;
};


struct LVariant {
    std::string name;
    int64_t     disc;
    std::vector<const LogosType*> payload_types;  // empty = no payload (C-style)
    bool        is_variadic = false;              // variadic pack payload (...T)
};

struct LEnumDef {
    std::string              name;
    std::vector<TypeParam>   type_params;   // empty for non-generic enums
    std::vector<LVariant>    variants;
    const LogosType*         backing_type = nullptr;  // null = default (i32)
    bool has_payload() const {
        for (auto& v : variants)
            if (!v.payload_types.empty()) return true;
        return false;
    }
};

// ── Trait definition ──────────────────────────────────────────────────────

struct LTraitMethodSig {
    std::string              name;
    std::vector<LParam>      params;
    const LogosType*         ret_type = nullptr;
};

struct LAssocTypeDef {
    std::string              name;    // e.g. "Item"
    std::vector<TraitBound>  bounds;  // e.g. [Ord, Clone]
};

struct LTraitDef {
    std::string                    name;
    std::string                    pkg;                 // package that declares this trait/genos
    std::vector<LAssocTypeDef>     assoc_types;        // associated type declarations
    std::vector<LTraitMethodSig>   methods;
    std::vector<std::string>       type_params;         // empty for non-generic traits
    std::string                    tag_dispatch_system; // #[tag_dispatch(system_name)]; empty = none
    uint64_t                       type_code = 0;       // #[type_code=N] — genos identity;
                                                        // propagates to each eidos via `impl Trait for Eidos`
    bool                           is_genos  = false;   // declared with `genos` keyword (not `trait`)
    bool                           is_auto   = false;   // declared with `auto trait` (compiler-synthesized impls)
    std::shared_ptr<HermesVal>     meta_val;             // meta @{...} block; null if absent
};

struct LImplBlock {
    std::string              trait_name;
    std::string              target_type;  // concrete type name (e.g. "Point")
    std::vector<LFunction>   methods;
    // Associated type definitions: "Item" → i32
    StrMap<const LogosType*> assoc_types;
    bool                     is_unsafe = false;  // declared as `unsafe impl`

    // Blanket impl support: `impl<T: Bound> Trait for T` — target_type is a
    // type-parameter name; applies to every concrete type implementing Bound.
    bool        is_blanket   = false;
    std::string bound_trait;    // name of the bound trait (only used when is_blanket)
};

struct LConst {
    std::string      name;
    const LogosType* type;
    LExprPtr         value;
};

struct LTypeAlias {
    std::string      name;
    const LogosType* type;
};

// ── Program ───────────────────────────────────────────────────────────────
//
// Owns all LogosType* objects via type_pool.  All other pointers in this
// program graph point into type_pool or into the string arena — do not
// outlive LProgram.

// Metadata bound to a generic instantiation via an explicit instantiation declaration.
// E.g.: #[type_code=42] datatype Array<i32>;
struct LInstAnnotation {
    std::string       canonical_name;  // fully-qualified canonical type string, e.g. "pkg::Array<i32>"
    std::string       mangled_name;    // concrete struct name after monomorphization, e.g. "Array$G1$i32"
    uint64_t          type_code = 0;   // 0 = not specified
    // Pointer to the concrete LogosType for this instantiation (owned by type_pool).
    // Non-null when the annotation was created from a #[type_code=N] eidos Foo<T>;
    // declaration that had a resolved target type available at sema time.
    // Used by mono to demand struct instantiation even when no Logos code references
    // Foo<T> directly (e.g. blob literals like @<I32>[...] produce type_code-tagged
    // objects at C++ level without instantiating the Logos struct).
    const LogosType*  struct_type = nullptr;
};

// One entry in a tag-based dispatch table.
// Emitted by sema when `impl Trait for SomeDatatype` is lowered and the Trait
// carries a `#[tag_dispatch(TagSystemName)]` annotation.
//
// Tier-1 (type_code 128-222): contributes to a static [223 x ptr] array
//   global named `__logos_tag_dispatch_<tag_system>_<trait>_<method>`.
// Tier-2 (type_code 223+): contributes to a cuckoo hash (future work);
//   only a collision-detection symbol is emitted for now.
struct LDispatchEntry {
    std::string tag_system;       // e.g. "DataTypeTagSystem"
    std::string trait_name;       // e.g. "Stringify"
    std::string method_name;      // e.g. "stringify"  (unmangled trait method name)
    std::string fn_symbol;        // mangled impl fn, e.g. "Point__stringify"
    std::string impl_type_name;   // e.g. "Point"  (for diagnostics / collision symbol)
    uint64_t    type_code = 0;    // the datatype's type_code
};

// TypeInfo rodata global for reflect::<T>() intrinsic.
// symbol = "__logos_reflect__<type_hash_hex>", blob = packed Hermes doc (with 8-byte size prefix).
struct LReflectGlobal {
    std::string           symbol;
    std::vector<uint8_t>  blob;   // [u64 size_le][hermes_doc bytes...]
};

struct LProgram {
    SemaResult             diags;

    TypePool               type_pool;  // owns all LogosType*

    std::vector<LStructDef>      structs;
    std::vector<LStructDef>      struct_specializations;  // struct specs (consumed by mono)
    std::vector<LEnumDef>        enums;
    std::vector<LFunction>       functions;        // free functions and extern fn
    std::vector<LFunction>       specializations;  // fn specialisations (consumed by mono)
    std::vector<LConst>          consts;
    std::vector<LTypeAlias>      type_aliases;
    std::vector<LTraitDef>       traits;
    std::vector<LImplBlock>      impls;
    std::vector<LInstAnnotation> inst_annotations; // explicit instantiation declarations
    std::vector<LDispatchEntry>  dispatch_entries; // tag-dispatch table entries

    // Populated by sema when reflect::<T>() is lowered; consumed by reflection_emit pass.
    StrSet reflect_requests; // fqn of types to reflect

    // Populated by reflection_emit pass; consumed by mlir_gen.
    std::vector<LReflectGlobal> reflection_globals;

    // Phase 5: fully-qualified names of fns annotated `#[metaprogram_post_sema]`.
    // Collected during sema; consumed by the metaprog driver loop in main.cpp.
    std::vector<std::string> metaprog_post_sema_hooks;

    // Symbol names present in binary archives on the search path.
    // mlir_gen skips functions whose mangled name is in this set (they're
    // already compiled and will be found by the linker in the .a).
    StrSet binary_symbols;

    bool ok()                         const noexcept { return diags.ok(); }
    void print_diags(std::FILE* fp = stderr) const noexcept { diags.print(fp); }
};

} // namespace logos::compiler::lir

// ── Entry point ───────────────────────────────────────────────────────────

#include <logos/hermes/document.hpp>

namespace logos::compiler {

// Run semantic analysis and produce L-IR from all parsed module ASTs.
// The ASTs must remain alive for the duration of this call (string_views).
// filenames[i] is the source path for asts[i] — used in diagnostics.
// from_binary: parallel to asts/filenames; true means the file came from a
// binary module archive and its non-generic symbols should not be re-emitted.
lir::LProgram sema_lower(const std::vector<hermes::Hermes>& asts,
                          const std::vector<std::string>& filenames = {},
                          const std::vector<bool>& from_binary = {});

// Build TypeInfo rodata blobs for types in reflect_requests and annotated datatypes.
// Populates prog.reflection_globals with LReflectGlobal entries.
lir::LProgram reflection_emit(lir::LProgram prog);

} // namespace logos::compiler
