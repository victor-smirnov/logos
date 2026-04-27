// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// LirBuilder — single construction surface for L-IR nodes.
//
// Design: ADR 0005. Stage 3f writes the existing variant types
// (lir::ELitInt, lir::EBinOp, ...) and returns owned LExprPtr/LStmtPtr.
// Stage 3g switches the implementation to write directly into a Hermes
// zone; only this file changes, sema callers stay put.
//
// The builder grows lazily — methods are added when sema sites migrate.
// No method exists ahead of its first caller.

#pragma once

#include <logos/compiler/lir.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace logos::compiler {

class LirBuilder {
public:
    explicit LirBuilder(lir::LProgram& prog) noexcept : prog_(prog) {}

    // ── Expression leaves ────────────────────────────────────────────────

    lir::LExprPtr lit_int   (int64_t v, TypeRef ty);
    lir::LExprPtr lit_bool  (bool v,    TypeRef ty);
    lir::LExprPtr lit_str   (std::string v, TypeRef ty);
    lir::LExprPtr lit_float (double v,  TypeRef ty);
    lir::LExprPtr var_ref   (std::string name, TypeRef ty);
    lir::LExprPtr addr_of   (std::string var_name, TypeRef ty);
    lir::LExprPtr pack_expand(std::string var_name, TypeRef ty);
    lir::LExprPtr size_of   (TypeRef elem_type, TypeRef ty);
    lir::LExprPtr type_code_of(TypeRef elem_type, TypeRef ty);
    lir::LExprPtr reflect_of(TypeRef elem_type, TypeRef ty);

    // ── Composite expressions ────────────────────────────────────────────

    lir::LExprPtr bin_op   (std::string op,
                            lir::LExprPtr lhs,
                            lir::LExprPtr rhs,
                            TypeRef ty);
    lir::LExprPtr unary    (std::string op, lir::LExprPtr operand, TypeRef ty);
    lir::LExprPtr deref    (lir::LExprPtr operand, TypeRef ty);
    lir::LExprPtr cast     (lir::LExprPtr operand, TypeRef ty);
    lir::LExprPtr field_read(lir::LExprPtr receiver, std::string field, TypeRef ty);
    lir::LExprPtr index_read(lir::LExprPtr receiver, lir::LExprPtr index, TypeRef ty);
    lir::LExprPtr tuple_index(lir::LExprPtr receiver, uint32_t index, TypeRef ty);
    lir::LExprPtr slice_index(lir::LExprPtr slice, lir::LExprPtr index, TypeRef ty);
    lir::LExprPtr arr_lit  (std::vector<lir::LExprPtr> elems, TypeRef ty);
    lir::LExprPtr tuple_lit(std::vector<lir::LExprPtr> elems, TypeRef ty);
    lir::LExprPtr try_expr(lir::LExprPtr inner, int32_t ok_disc, int32_t err_disc, TypeRef ty);
    lir::LExprPtr call(std::string callee,
                       std::vector<TypeRef> type_args,
                       std::vector<lir::LExprPtr> args,
                       TypeRef ty);
    lir::LExprPtr block_expr(lir::LBlock* block,
                             lir::LExprPtr result, TypeRef ty);
    lir::LExprPtr struct_lit(std::string name,
                             std::vector<std::pair<std::string, lir::LExprPtr>> fields,
                             TypeRef ty);
    lir::LExprPtr enum_lit(std::string enum_name, std::string variant,
                           int64_t disc, TypeRef ty);
    lir::LExprPtr closure_box(lir::EClosure* inner, TypeRef ty);
    // Coerce an existing EClosureBox expr into an FnPtr-typed closure_box.
    // Sets inner->as_fn_ptr=true and re-emits via closure_box() so the Hermes
    // mirror reflects the new type. Caller is responsible for verifying that
    // the input is an EClosureBox with no captures.
    lir::LExprPtr closure_to_fnptr(lir::LExpr* arg, TypeRef new_ty);
    lir::LExprPtr closure_call(lir::LExprPtr callee,
                               std::vector<lir::LExprPtr> args, TypeRef ty);
    lir::LExprPtr fn_ptr_call(lir::LExprPtr callee,
                              std::vector<lir::LExprPtr> args, TypeRef ty);
    lir::LExprPtr addr_of_temp(lir::LExprPtr inner, bool is_mut, TypeRef ty);
    lir::LExprPtr slice_lit(lir::LExprPtr base, lir::LExprPtr len, TypeRef ty);
    lir::LExprPtr slice_len(lir::LExprPtr slice, TypeRef ty);
    lir::LExprPtr slice_ptr(lir::LExprPtr slice, TypeRef ty);
    lir::LExprPtr ptr_arith(lir::EPtrArith::Op op, lir::LExprPtr lhs,
                            lir::LExprPtr rhs, TypeRef ty);
    lir::LExprPtr ptr_diff(bool by_byte, lir::LExprPtr lhs,
                           lir::LExprPtr rhs, TypeRef ty);
    lir::LExprPtr enum_lit_data(std::string enum_name, std::string variant,
                                int64_t disc,
                                std::vector<lir::LExprPtr> payload,
                                TypeRef ty);
    lir::LExprPtr method_call(lir::LExprPtr receiver, std::string method,
                              std::string resolved_symbol,
                              std::vector<TypeRef> type_args,
                              std::vector<lir::LExprPtr> args,
                              int32_t vtable_index, TypeRef ty);
    lir::LExprPtr hermes_cast(lir::LExprPtr operand, std::string build_fn,
                              TypeRef ty);

    // ── Post-construction retype ────────────────────────────────────────
    // Update the type of an already-built LExpr and re-emit its Hermes
    // mirror so view readers see the new type. Used for literal-type
    // narrowing (FloatLit/IntLit → f32/f64/concrete) at annotation sites
    // in lower_let / lower_return / assoc-const lookup. Must only be
    // called when no enclosing structure has snapshotted `e` yet — i.e.
    // before the LExpr is fed into a parent builder call.
    void retype_expr(lir::LExpr* e, TypeRef new_ty);

    // ── Statement constructors ──────────────────────────────────────────
    // Build an LStmt and eager-emit its Hermes mirror. After this call,
    // `stmt_ref_of(s)` is valid (mirror_offset_ is set). Used by sub-slice
    // 2.0 to eliminate the make_stmt/eager-emit gap (gap memo
    // feat_lir_mirror_eager_emit_gaps): every kind that's safe for eager
    // emit (i.e. construction is final-state, no later mutation of children
    // or reparenting of child blocks) gets its own builder method here.

    lir::LStmt stmt_expr(lir::LExprPtr expr, uint32_t line);
    lir::LStmt stmt_break(lir::LExprPtr value, std::string label, uint32_t line);
    lir::LStmt stmt_continue(std::string label, uint32_t line);
    lir::LStmt stmt_return(lir::LExprPtr value, uint32_t line);
    lir::LStmt stmt_assign(std::string name, lir::LExprPtr value, uint32_t line);
    lir::LStmt stmt_deref_write(lir::LExprPtr ptr, lir::LExprPtr value, uint32_t line);

    // ── Adopt-style: pre-built variants populated incrementally ─────────────
    // Stage 3g deletes these along with the variant types. Until then, sema
    // sites that fill multi-field variants in place hand the result here so
    // LExpr construction still goes through the builder.
    lir::LExprPtr call_v       (lir::ECall ec,        TypeRef ty);
    lir::LExprPtr method_call_v(lir::EMethodCall mc,  TypeRef ty);
    lir::LExprPtr if_expr_v    (lir::EIfExpr eif,     TypeRef ty);
    lir::LExprPtr hermes_lit_v (lir::EHermesLit lit,  TypeRef ty);
    lir::LExprPtr match_expr_v (lir::EMatchExpr me,   TypeRef ty);

    // … grows as sema sites migrate; do not pre-populate.

private:
    lir::LProgram& prog_;  // reserved for Stage 3g (Hermes zone access)
};

} // namespace logos::compiler
