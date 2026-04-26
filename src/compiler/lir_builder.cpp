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

lir::LExprPtr LirBuilder::bin_op(std::string op,
                                  lir::LExprPtr lhs,
                                  lir::LExprPtr rhs,
                                  TypeRef ty) {
    return make_expr(ty,
        lir::EBinOp{std::move(op), std::move(lhs), std::move(rhs)});
}

} // namespace logos::compiler
