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

    lir_view::ExprRef lit_int   (int64_t v, TypeRef ty);
    lir_view::ExprRef lit_int_128(uint64_t lo, uint64_t hi, TypeRef ty);
    lir_view::ExprRef lit_bool  (bool v,    TypeRef ty);
    lir_view::ExprRef lit_str   (std::string v, TypeRef ty);
    lir_view::ExprRef lit_float (double v,  TypeRef ty);
    lir_view::ExprRef var_ref   (std::string name, TypeRef ty, uint32_t slot = 0xFFFFFFFFu);
    lir_view::ExprRef addr_of   (std::string var_name, TypeRef ty);
    lir_view::ExprRef pack_expand(std::string var_name, TypeRef ty);
    lir_view::ExprRef size_of   (TypeRef elem_type, TypeRef ty);
    lir_view::ExprRef align_of  (TypeRef elem_type, TypeRef ty);
    lir_view::ExprRef generic_ref(std::string name,
                              std::vector<TypeRef> type_args,
                              TypeRef ty);
    lir_view::ExprRef type_code_of(TypeRef elem_type, TypeRef ty);
    lir_view::ExprRef reflect_of(TypeRef elem_type, TypeRef ty);

    // ── Composite expressions ────────────────────────────────────────────

    lir_view::ExprRef bin_op   (std::string op,
                            lir_view::ExprRef lhs,
                            lir_view::ExprRef rhs,
                            TypeRef ty);
    lir_view::ExprRef unary    (std::string op, lir_view::ExprRef operand, TypeRef ty);
    lir_view::ExprRef deref    (lir_view::ExprRef operand, TypeRef ty);
    lir_view::ExprRef cast     (lir_view::ExprRef operand, TypeRef ty);
    lir_view::ExprRef field_read(lir_view::ExprRef receiver, std::string field, TypeRef ty);
    lir_view::ExprRef index_read(lir_view::ExprRef receiver, lir_view::ExprRef index, TypeRef ty);
    lir_view::ExprRef tuple_index(lir_view::ExprRef receiver, uint32_t index, TypeRef ty);
    lir_view::ExprRef slice_index(lir_view::ExprRef slice, lir_view::ExprRef index, TypeRef ty);
    lir_view::ExprRef arr_lit  (std::vector<lir_view::ExprRef> elems, TypeRef ty);
    lir_view::ExprRef tuple_lit(std::vector<lir_view::ExprRef> elems, TypeRef ty);
    lir_view::ExprRef try_expr(lir_view::ExprRef inner, int32_t ok_disc, int32_t err_disc, TypeRef ty);
    lir_view::ExprRef call(std::string callee,
                       std::vector<TypeRef> type_args,
                       std::vector<lir_view::ExprRef> args,
                       TypeRef ty);
    lir_view::ExprRef block_expr(lir_view::BlockRef block,
                             lir_view::ExprRef result, TypeRef ty);
    lir_view::ExprRef struct_lit(std::string name,
                             std::vector<std::pair<std::string, lir_view::ExprRef>> fields,
                             TypeRef ty);
    lir_view::ExprRef enum_lit(std::string enum_name, std::string variant,
                           int64_t disc, TypeRef ty);
    lir_view::ExprRef closure_box(lir::EClosure* inner, TypeRef ty);
    // Coerce an existing EClosureBox expr into an FnPtr-typed closure_box.
    // Sets inner->as_fn_ptr=true and re-emits via closure_box() so the Hermes
    // mirror reflects the new type. Caller is responsible for verifying that
    // the input is an EClosureBox with no captures.
    lir_view::ExprRef closure_to_fnptr(lir_view::ExprRef arg, TypeRef new_ty);
    // Replace one element of an existing ETupleLit and re-emit its mirror so
    // view reads see the new child offset. Caller must ensure `tuple` is
    // ETupleLit and has no already-mirrored ancestors (mutating an embedded
    // child can't update parent mirrors, which is the parent-mirror
    // invalidation blocker).
    lir_view::ExprRef set_tuple_elem(lir_view::ExprRef tuple, size_t idx, lir_view::ExprRef new_value);
    lir_view::ExprRef closure_call(lir_view::ExprRef callee,
                               std::vector<lir_view::ExprRef> args, TypeRef ty);
    lir_view::ExprRef fn_ptr_call(lir_view::ExprRef callee,
                              std::vector<lir_view::ExprRef> args, TypeRef ty);
    lir_view::ExprRef addr_of_temp(lir_view::ExprRef inner, bool is_mut, TypeRef ty);

    // Re-emit a `&mut T` LIR expression for a second consuming use, wrapping
    // it as an implicit reborrow `AddrOfTemp(Deref(orig))` of the SAME type
    // — `orig` itself stays as the canonical place to clone for further
    // re-uses. This is the codegen-side complement to sema's
    // `try_implicit_reborrow_mut`: anywhere a mono/synthesizer builds N
    // calls reusing one `&mut T` arg, each subsequent reuse must be wrapped
    // (without it, borrow_check — which treats `&mut T` as a move-type —
    // consumes the arg on call #1 and rejects calls #2..#N).
    //
    // Returns `orig` unchanged if its type isn't `&mut T` (no reborrow
    // needed) or has no pointee (degenerate); callers can ignore the
    // distinction and always wrap.
    lir_view::ExprRef reuse_mut_ref(const lir_view::ExprRef& orig);
    lir_view::ExprRef slice_lit(lir_view::ExprRef base, lir_view::ExprRef len, TypeRef ty);
    lir_view::ExprRef slice_len(lir_view::ExprRef slice, TypeRef ty);
    lir_view::ExprRef slice_ptr(lir_view::ExprRef slice, TypeRef ty);
    lir_view::ExprRef ptr_arith(lir::EPtrArith::Op op, lir_view::ExprRef lhs,
                            lir_view::ExprRef rhs, TypeRef ty);
    lir_view::ExprRef ptr_diff(bool by_byte, lir_view::ExprRef lhs,
                           lir_view::ExprRef rhs, TypeRef ty);
    lir_view::ExprRef enum_lit_data(std::string enum_name, std::string variant,
                                int64_t disc,
                                std::vector<lir_view::ExprRef> payload,
                                TypeRef ty);
    lir_view::ExprRef method_call(lir_view::ExprRef receiver, std::string method,
                              std::string resolved_symbol,
                              std::vector<TypeRef> type_args,
                              std::vector<lir_view::ExprRef> args,
                              int32_t vtable_index, TypeRef ty);
    lir_view::ExprRef hermes_cast(lir_view::ExprRef operand, std::string build_fn,
                              TypeRef ty);

    // ── Post-construction retype ────────────────────────────────────────
    // Update the type of an already-built LExpr and re-emit its Hermes
    // mirror so view readers see the new type. Used for literal-type
    // narrowing (FloatLit/IntLit → f32/f64/concrete) at annotation sites
    // in lower_let / lower_return / assoc-const lookup. Must only be
    // called when no enclosing structure has snapshotted `e` yet — i.e.
    // before the LExpr is fed into a parent builder call.
    void retype_expr(lir_view::ExprRef e, TypeRef new_ty);

    // ── Statement constructors ──────────────────────────────────────────
    // Build an LStmt and eager-emit its Hermes mirror. After this call,
    // `stmt_ref_of(s)` is valid (mirror_ptr_ is set). Used by sub-slice
    // 2.0 to eliminate the make_stmt/eager-emit gap (gap memo
    // feat_lir_mirror_eager_emit_gaps): every kind that's safe for eager
    // emit (i.e. construction is final-state, no later mutation of children
    // or reparenting of child blocks) gets its own builder method here.

    lir_view::StmtRef stmt_expr(lir_view::ExprRef expr, uint32_t line);
    lir_view::StmtRef stmt_break(lir_view::ExprRef value, std::string label, uint32_t line);
    lir_view::StmtRef stmt_continue(std::string label, uint32_t line);
    lir_view::StmtRef stmt_return(lir_view::ExprRef value, uint32_t line);
    lir_view::StmtRef stmt_assign(std::string name, lir_view::ExprRef value, uint32_t line, bool drop_old = false);
    lir_view::StmtRef stmt_deref_write(lir_view::ExprRef ptr, lir_view::ExprRef value, uint32_t line, bool drop_old = false);

    // ── Adopt-style: pre-built variants populated incrementally ─────────────
    // Stage 3g deletes these along with the variant types. Until then, sema
    // sites that fill multi-field variants in place hand the result here so
    // LExpr construction still goes through the builder.
    lir_view::ExprRef call_v       (lir::ECall ec,        TypeRef ty);
    lir_view::ExprRef method_call_v(lir::EMethodCall mc,  TypeRef ty);
    lir_view::ExprRef if_expr_v    (lir::EIfExpr eif,     TypeRef ty);
    lir_view::ExprRef hermes_lit_v (lir::EHermesLit lit,  TypeRef ty);
    lir_view::ExprRef match_expr_v (lir::EMatchExpr me,   TypeRef ty);

    // … grows as sema sites migrate; do not pre-populate.

private:
    lir::LProgram& prog_;  // reserved for Stage 3g (Hermes zone access)
};

} // namespace logos::compiler
