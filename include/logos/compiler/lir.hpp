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

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
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

struct PatVariant { std::string enum_name; std::string variant; int32_t disc; };
struct PatInt     { int32_t value; };
struct PatBool    { bool value; };
struct PatWild    { std::string name; };   // _ or named wildcard (name may be "_")

using Pattern = std::variant<PatVariant, PatInt, PatBool, PatWild>;

struct LMatchArm {
    Pattern   pat;
    LBlockPtr body;   // arm body (single stmts are wrapped in a 1-stmt block)
};

// ── Expression node payloads ──────────────────────────────────────────────

struct ELitInt    { int64_t value; };
struct ELitBool   { bool value; };
struct ELitStr    { std::string value; };

struct EVarRef    { std::string name; };

struct EEnumLit   {
    std::string enum_name;
    std::string variant;
    int32_t     disc;           // discriminant value (i32)
};

struct ECall      {
    std::string                   callee;
    std::vector<const LogosType*> type_args;  // empty for non-generic calls
    std::vector<LExprPtr>         args;
};

struct EMethodCall {
    LExprPtr               receiver;
    std::string            method;
    std::vector<LExprPtr>  args;
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
    // target type is LExpr::type
};

// ── Expression node ───────────────────────────────────────────────────────

struct LExpr {
    const LogosType* type = nullptr;   // always set; error_t() on ill-typed nodes
    std::variant<
        ELitInt, ELitBool, ELitStr, EVarRef, EEnumLit,
        ECall, EMethodCall, EBinOp, EUnary, EAddrOf, EDeref,
        EFieldRead, EIndexRead, EStructLit, EArrLit, ECast
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
};

struct SFor {
    std::string      var;
    LExprPtr         lo;
    LExprPtr         hi;
    bool             inclusive;
    LBlockPtr        body;
};

struct SLoop      { LBlockPtr body; };
struct SBreak     {};
struct SContinue  {};

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

struct SExprStmt  { LExprPtr expr; };

struct SMatch {
    LExprPtr               scrut;
    std::vector<LMatchArm> arms;
};

// ── Statement node ────────────────────────────────────────────────────────

struct LStmt {
    uint32_t line = 0;             // source line (0 = unknown)
    std::variant<
        SLet, SAssign, SReturn, SIf, SWhile, SFor, SLoop,
        SBreak, SContinue, SFieldWrite, SIndexWrite, SExprStmt, SMatch
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
};

struct LFunction {
    std::string              name;
    std::vector<TypeParam>   type_params;  // TypeVar names (generic def, empty otherwise)
    std::vector<LParam>      params;
    const LogosType*         ret_type  = nullptr;
    LBlock                   body;
    bool                     is_extern = false;

    // Specialisation support (set by sema, cleared by mono after instantiation).
    // is_specialization == true  →  this is a specialisation of `name`.
    // spec_patterns: one LogosType* per type-param position; may contain TypeVar
    //   for partial specialisations (e.g. fn foo<*T> → pattern = *const TypeVar<T>).
    bool                          is_specialization = false;
    std::vector<const LogosType*> spec_patterns;
};

struct LField {
    std::string      name;
    const LogosType* type;
};

struct LStructDef {
    std::string              name;
    std::vector<TypeParam>   type_params;  // empty for non-generic structs
    std::vector<LField>      fields;
    std::vector<LFunction>   methods;
};

struct LVariant {
    std::string name;
    int32_t     disc;
};

struct LEnumDef {
    std::string            name;
    std::vector<LVariant>  variants;
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

struct LProgram {
    SemaResult             diags;

    TypePool               type_pool;  // owns all LogosType*

    std::vector<LStructDef>  structs;
    std::vector<LEnumDef>    enums;
    std::vector<LFunction>   functions;        // free functions and extern fn
    std::vector<LFunction>   specializations;  // fn specialisations (consumed by mono)
    std::vector<LConst>      consts;
    std::vector<LTypeAlias>  type_aliases;

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
lir::LProgram sema_lower(const std::vector<hermes::HermesCtr>& asts,
                          const std::vector<std::string>& filenames = {});

} // namespace logos::compiler
