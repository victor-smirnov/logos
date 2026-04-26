// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// LirBuilder — see ADR 0005 and lir_builder.hpp.

#include <logos/compiler/lir_builder.hpp>

namespace logos::compiler {

namespace {
template <class K>
inline lir::LExprPtr make_expr(TypeRef t, K&& k) {
    auto e = std::make_unique<lir::LExpr>();
    e->type = t;
    e->kind = std::forward<K>(k);
    return e;
}
} // anonymous

lir::LExprPtr LirBuilder::lit_int(int64_t v, TypeRef ty) {
    return make_expr(ty, lir::ELitInt{v});
}

lir::LExprPtr LirBuilder::lit_bool(bool v, TypeRef ty) {
    return make_expr(ty, lir::ELitBool{v});
}

lir::LExprPtr LirBuilder::var_ref(std::string name, TypeRef ty) {
    return make_expr(ty, lir::EVarRef{std::move(name)});
}

lir::LExprPtr LirBuilder::lit_str(std::string v, TypeRef ty) {
    return make_expr(ty, lir::ELitStr{std::move(v)});
}

lir::LExprPtr LirBuilder::lit_float(double v, TypeRef ty) {
    return make_expr(ty, lir::ELitFloat{v});
}

lir::LExprPtr LirBuilder::addr_of(std::string var_name, TypeRef ty) {
    return make_expr(ty, lir::EAddrOf{std::move(var_name)});
}

lir::LExprPtr LirBuilder::pack_expand(std::string var_name, TypeRef ty) {
    return make_expr(ty, lir::EPackExpand{std::move(var_name)});
}

lir::LExprPtr LirBuilder::size_of(TypeRef elem_type, TypeRef ty) {
    return make_expr(ty, lir::ESizeOf{elem_type});
}

lir::LExprPtr LirBuilder::type_code_of(TypeRef elem_type, TypeRef ty) {
    return make_expr(ty, lir::ETypeCodeOf{elem_type});
}

lir::LExprPtr LirBuilder::reflect_of(TypeRef elem_type, TypeRef ty) {
    return make_expr(ty, lir::EReflectOf{elem_type});
}

lir::LExprPtr LirBuilder::bin_op(std::string op,
                                  lir::LExprPtr lhs,
                                  lir::LExprPtr rhs,
                                  TypeRef ty) {
    return make_expr(ty,
        lir::EBinOp{std::move(op), std::move(lhs), std::move(rhs)});
}

lir::LExprPtr LirBuilder::unary(std::string op, lir::LExprPtr operand, TypeRef ty) {
    return make_expr(ty, lir::EUnary{std::move(op), std::move(operand)});
}

lir::LExprPtr LirBuilder::deref(lir::LExprPtr operand, TypeRef ty) {
    return make_expr(ty, lir::EDeref{std::move(operand)});
}

lir::LExprPtr LirBuilder::cast(lir::LExprPtr operand, TypeRef ty) {
    return make_expr(ty, lir::ECast{std::move(operand)});
}

lir::LExprPtr LirBuilder::field_read(lir::LExprPtr receiver, std::string field, TypeRef ty) {
    return make_expr(ty, lir::EFieldRead{std::move(receiver), std::move(field)});
}

lir::LExprPtr LirBuilder::index_read(lir::LExprPtr receiver, lir::LExprPtr index, TypeRef ty) {
    return make_expr(ty, lir::EIndexRead{std::move(receiver), std::move(index)});
}

lir::LExprPtr LirBuilder::tuple_index(lir::LExprPtr receiver, uint32_t index, TypeRef ty) {
    return make_expr(ty, lir::ETupleIndex{std::move(receiver), index});
}

lir::LExprPtr LirBuilder::slice_index(lir::LExprPtr slice, lir::LExprPtr index, TypeRef ty) {
    return make_expr(ty, lir::ESliceIndex{std::move(slice), std::move(index)});
}

lir::LExprPtr LirBuilder::arr_lit(std::vector<lir::LExprPtr> elems, TypeRef ty) {
    return make_expr(ty, lir::EArrLit{std::move(elems)});
}

lir::LExprPtr LirBuilder::tuple_lit(std::vector<lir::LExprPtr> elems, TypeRef ty) {
    return make_expr(ty, lir::ETupleLit{std::move(elems)});
}

lir::LExprPtr LirBuilder::try_expr(lir::LExprPtr inner, int32_t ok_disc,
                                    int32_t err_disc, TypeRef ty) {
    return make_expr(ty, lir::ETry{std::move(inner), ok_disc, err_disc});
}

lir::LExprPtr LirBuilder::call(std::string callee,
                                std::vector<TypeRef> type_args,
                                std::vector<lir::LExprPtr> args,
                                TypeRef ty) {
    return make_expr(ty, lir::ECall{std::move(callee), std::move(type_args),
                                    std::move(args)});
}

lir::LExprPtr LirBuilder::block_expr(std::unique_ptr<lir::LBlock> block,
                                      lir::LExprPtr result, TypeRef ty) {
    return make_expr(ty, lir::EBlockExpr{std::move(block), std::move(result)});
}

lir::LExprPtr LirBuilder::struct_lit(
    std::string name,
    std::vector<std::pair<std::string, lir::LExprPtr>> fields,
    TypeRef ty) {
    return make_expr(ty, lir::EStructLit{std::move(name), std::move(fields)});
}

lir::LExprPtr LirBuilder::enum_lit(std::string enum_name, std::string variant,
                                    int64_t disc, TypeRef ty) {
    return make_expr(ty, lir::EEnumLit{std::move(enum_name), std::move(variant), disc});
}

lir::LExprPtr LirBuilder::closure_box(std::unique_ptr<lir::EClosure> inner,
                                       TypeRef ty) {
    return make_expr(ty, lir::EClosureBox{std::move(inner)});
}

lir::LExprPtr LirBuilder::closure_call(lir::LExprPtr callee,
                                        std::vector<lir::LExprPtr> args,
                                        TypeRef ty) {
    return make_expr(ty, lir::EClosureCall{std::move(callee), std::move(args)});
}

lir::LExprPtr LirBuilder::fn_ptr_call(lir::LExprPtr callee,
                                       std::vector<lir::LExprPtr> args,
                                       TypeRef ty) {
    return make_expr(ty, lir::EFnPtrCall{std::move(callee), std::move(args)});
}

} // namespace logos::compiler
