// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// LirBuilder — see ADR 0005 and lir_builder.hpp.

#include <logos/compiler/lir_builder.hpp>
#include <logos/compiler/lir_mirror.hpp>

namespace logos::compiler {

namespace {
// Stage 3g.1: every builder-constructed node is mirrored into the program's
// Hermes zone immediately. The post-sema lir_mirror_emit_into pass and
// mono's per-clone emit still run; both are now no-ops on builder-created
// nodes (table cache hits).
template <class K>
inline lir::LExprPtr make_expr(lir::LProgram& prog, TypeRef t, K&& k) {
    auto e = std::make_unique<lir::LExpr>();
    e->type = t;
    e->kind = std::forward<K>(k);
    lir_mirror_emit_expr_node(prog, *e);
    return e;
}
} // anonymous

lir::LExprPtr LirBuilder::lit_int(int64_t v, TypeRef ty) {
    return make_expr(prog_, ty, lir::ELitInt{v});
}

lir::LExprPtr LirBuilder::lit_bool(bool v, TypeRef ty) {
    return make_expr(prog_, ty, lir::ELitBool{v});
}

lir::LExprPtr LirBuilder::var_ref(std::string name, TypeRef ty) {
    return make_expr(prog_, ty, lir::EVarRef{std::move(name)});
}

lir::LExprPtr LirBuilder::lit_str(std::string v, TypeRef ty) {
    return make_expr(prog_, ty, lir::ELitStr{std::move(v)});
}

lir::LExprPtr LirBuilder::lit_float(double v, TypeRef ty) {
    return make_expr(prog_, ty, lir::ELitFloat{v});
}

lir::LExprPtr LirBuilder::addr_of(std::string var_name, TypeRef ty) {
    return make_expr(prog_, ty, lir::EAddrOf{std::move(var_name)});
}

lir::LExprPtr LirBuilder::pack_expand(std::string var_name, TypeRef ty) {
    return make_expr(prog_, ty, lir::EPackExpand{std::move(var_name)});
}

lir::LExprPtr LirBuilder::size_of(TypeRef elem_type, TypeRef ty) {
    return make_expr(prog_, ty, lir::ESizeOf{elem_type});
}

lir::LExprPtr LirBuilder::type_code_of(TypeRef elem_type, TypeRef ty) {
    return make_expr(prog_, ty, lir::ETypeCodeOf{elem_type});
}

lir::LExprPtr LirBuilder::reflect_of(TypeRef elem_type, TypeRef ty) {
    return make_expr(prog_, ty, lir::EReflectOf{elem_type});
}

lir::LExprPtr LirBuilder::bin_op(std::string op,
                                  lir::LExprPtr lhs,
                                  lir::LExprPtr rhs,
                                  TypeRef ty) {
    return make_expr(prog_, ty,
        lir::EBinOp{std::move(op), std::move(lhs), std::move(rhs)});
}

lir::LExprPtr LirBuilder::unary(std::string op, lir::LExprPtr operand, TypeRef ty) {
    return make_expr(prog_, ty, lir::EUnary{std::move(op), std::move(operand)});
}

lir::LExprPtr LirBuilder::deref(lir::LExprPtr operand, TypeRef ty) {
    return make_expr(prog_, ty, lir::EDeref{std::move(operand)});
}

lir::LExprPtr LirBuilder::cast(lir::LExprPtr operand, TypeRef ty) {
    return make_expr(prog_, ty, lir::ECast{std::move(operand)});
}

lir::LExprPtr LirBuilder::field_read(lir::LExprPtr receiver, std::string field, TypeRef ty) {
    return make_expr(prog_, ty, lir::EFieldRead{std::move(receiver), std::move(field)});
}

lir::LExprPtr LirBuilder::index_read(lir::LExprPtr receiver, lir::LExprPtr index, TypeRef ty) {
    return make_expr(prog_, ty, lir::EIndexRead{std::move(receiver), std::move(index)});
}

lir::LExprPtr LirBuilder::tuple_index(lir::LExprPtr receiver, uint32_t index, TypeRef ty) {
    return make_expr(prog_, ty, lir::ETupleIndex{std::move(receiver), index});
}

lir::LExprPtr LirBuilder::slice_index(lir::LExprPtr slice, lir::LExprPtr index, TypeRef ty) {
    return make_expr(prog_, ty, lir::ESliceIndex{std::move(slice), std::move(index)});
}

lir::LExprPtr LirBuilder::arr_lit(std::vector<lir::LExprPtr> elems, TypeRef ty) {
    return make_expr(prog_, ty, lir::EArrLit{std::move(elems)});
}

lir::LExprPtr LirBuilder::tuple_lit(std::vector<lir::LExprPtr> elems, TypeRef ty) {
    return make_expr(prog_, ty, lir::ETupleLit{std::move(elems)});
}

lir::LExprPtr LirBuilder::try_expr(lir::LExprPtr inner, int32_t ok_disc,
                                    int32_t err_disc, TypeRef ty) {
    return make_expr(prog_, ty, lir::ETry{std::move(inner), ok_disc, err_disc});
}

lir::LExprPtr LirBuilder::call(std::string callee,
                                std::vector<TypeRef> type_args,
                                std::vector<lir::LExprPtr> args,
                                TypeRef ty) {
    return make_expr(prog_, ty, lir::ECall{std::move(callee), std::move(type_args),
                                    std::move(args)});
}

lir::LExprPtr LirBuilder::block_expr(std::unique_ptr<lir::LBlock> block,
                                      lir::LExprPtr result, TypeRef ty) {
    return make_expr(prog_, ty, lir::EBlockExpr{std::move(block), std::move(result)});
}

lir::LExprPtr LirBuilder::struct_lit(
    std::string name,
    std::vector<std::pair<std::string, lir::LExprPtr>> fields,
    TypeRef ty) {
    return make_expr(prog_, ty, lir::EStructLit{std::move(name), std::move(fields)});
}

lir::LExprPtr LirBuilder::enum_lit(std::string enum_name, std::string variant,
                                    int64_t disc, TypeRef ty) {
    return make_expr(prog_, ty, lir::EEnumLit{std::move(enum_name), std::move(variant), disc});
}

lir::LExprPtr LirBuilder::closure_box(std::unique_ptr<lir::EClosure> inner,
                                       TypeRef ty) {
    return make_expr(prog_, ty, lir::EClosureBox{std::move(inner)});
}

lir::LExprPtr LirBuilder::closure_call(lir::LExprPtr callee,
                                        std::vector<lir::LExprPtr> args,
                                        TypeRef ty) {
    return make_expr(prog_, ty, lir::EClosureCall{std::move(callee), std::move(args)});
}

lir::LExprPtr LirBuilder::fn_ptr_call(lir::LExprPtr callee,
                                       std::vector<lir::LExprPtr> args,
                                       TypeRef ty) {
    return make_expr(prog_, ty, lir::EFnPtrCall{std::move(callee), std::move(args)});
}

lir::LExprPtr LirBuilder::addr_of_temp(lir::LExprPtr inner, bool is_mut, TypeRef ty) {
    return make_expr(prog_, ty, lir::EAddrOfTemp{std::move(inner), is_mut});
}

lir::LExprPtr LirBuilder::slice_lit(lir::LExprPtr base, lir::LExprPtr len, TypeRef ty) {
    return make_expr(prog_, ty, lir::ESliceLit{std::move(base), std::move(len)});
}

lir::LExprPtr LirBuilder::slice_len(lir::LExprPtr slice, TypeRef ty) {
    return make_expr(prog_, ty, lir::ESliceLen{std::move(slice)});
}

lir::LExprPtr LirBuilder::slice_ptr(lir::LExprPtr slice, TypeRef ty) {
    return make_expr(prog_, ty, lir::ESlicePtr{std::move(slice)});
}

lir::LExprPtr LirBuilder::ptr_arith(lir::EPtrArith::Op op,
                                     lir::LExprPtr lhs, lir::LExprPtr rhs,
                                     TypeRef ty) {
    return make_expr(prog_, ty, lir::EPtrArith{op, std::move(lhs), std::move(rhs)});
}

lir::LExprPtr LirBuilder::ptr_diff(bool by_byte,
                                    lir::LExprPtr lhs, lir::LExprPtr rhs,
                                    TypeRef ty) {
    return make_expr(prog_, ty, lir::EPtrDiff{by_byte, std::move(lhs), std::move(rhs)});
}

lir::LExprPtr LirBuilder::enum_lit_data(std::string enum_name, std::string variant,
                                         int64_t disc,
                                         std::vector<lir::LExprPtr> payload,
                                         TypeRef ty) {
    return make_expr(prog_, ty, lir::EEnumLitData{
        std::move(enum_name), std::move(variant), disc, std::move(payload)});
}

lir::LExprPtr LirBuilder::method_call(lir::LExprPtr receiver, std::string method,
                                       std::string resolved_symbol,
                                       std::vector<TypeRef> type_args,
                                       std::vector<lir::LExprPtr> args,
                                       int32_t vtable_index, TypeRef ty) {
    return make_expr(prog_, ty, lir::EMethodCall{
        std::move(receiver), std::move(method), std::move(resolved_symbol),
        std::move(type_args), std::move(args), vtable_index});
}

lir::LExprPtr LirBuilder::hermes_cast(lir::LExprPtr operand, std::string build_fn,
                                       TypeRef ty) {
    return make_expr(prog_, ty, lir::ECast{std::move(operand), std::move(build_fn)});
}

lir::LExprPtr LirBuilder::call_v(lir::ECall ec, TypeRef ty) {
    return make_expr(prog_, ty, std::move(ec));
}

lir::LExprPtr LirBuilder::method_call_v(lir::EMethodCall mc, TypeRef ty) {
    return make_expr(prog_, ty, std::move(mc));
}

lir::LExprPtr LirBuilder::if_expr_v(lir::EIfExpr eif, TypeRef ty) {
    return make_expr(prog_, ty, std::move(eif));
}

lir::LExprPtr LirBuilder::hermes_lit_v(lir::EHermesLit lit, TypeRef ty) {
    return make_expr(prog_, ty, std::move(lit));
}

lir::LExprPtr LirBuilder::match_expr_v(lir::EMatchExpr me, TypeRef ty) {
    return make_expr(prog_, ty, std::move(me));
}

} // namespace logos::compiler
