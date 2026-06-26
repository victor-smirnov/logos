// Logos project — https://github.com/victor-smirnov/logos
//
// LirBuilder — see ADR 0005 and lir_builder.hpp.

#include <logos/compiler/lir_builder.hpp>
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/lir_view.hpp>

namespace logos::compiler {

namespace {
// Stage 3.5 step 4: most LExpr is built via direct mirror emission. The
// variant kind field stays default-constructed; mirror is the sole source
// of truth.
template <class EmitFn>
inline lir_view::ExprRef direct(lir::LProgram& prog, TypeRef ty, EmitFn&& emit) {
    const uint8_t* mp = emit(prog, ty);
    return lir_view::ExprRef(prog.type_pool.arena(), mp);
}

} // anonymous

lir_view::ExprRef LirBuilder::lit_int(int64_t v, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_lit_int(p, t, v); });
}

lir_view::ExprRef LirBuilder::lit_int_128(uint64_t lo, uint64_t hi, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_lit_int_128(p, t, lo, hi); });
}

lir_view::ExprRef LirBuilder::lit_bool(bool v, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_lit_bool(p, t, v); });
}

lir_view::ExprRef LirBuilder::var_ref(std::string name, TypeRef ty, uint32_t slot) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_var_ref(p, t, name, slot); });
}

lir_view::ExprRef LirBuilder::lit_str(std::string v, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_lit_str(p, t, v); });
}

lir_view::ExprRef LirBuilder::lit_float(double v, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_lit_float(p, t, v); });
}

lir_view::ExprRef LirBuilder::addr_of(std::string var_name, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_addr_of(p, t, var_name); });
}

lir_view::ExprRef LirBuilder::pack_expand(std::string var_name, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_pack_expand(p, t, var_name); });
}

lir_view::ExprRef LirBuilder::size_of(TypeRef elem_type, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_size_of(p, t, elem_type); });
}

lir_view::ExprRef LirBuilder::align_of(TypeRef elem_type, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_align_of(p, t, elem_type); });
}

lir_view::ExprRef LirBuilder::generic_ref(std::string name,
                                       std::vector<TypeRef> type_args,
                                       TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_generic_ref(p, t, name, type_args); });
}

lir_view::ExprRef LirBuilder::type_code_of(TypeRef elem_type, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_type_code_of(p, t, elem_type); });
}

lir_view::ExprRef LirBuilder::reflect_of(TypeRef elem_type, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_reflect_of(p, t, elem_type); });
}

lir_view::ExprRef LirBuilder::bin_op(std::string op,
                                  lir::LExprPtr lhs,
                                  lir::LExprPtr rhs,
                                  TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_bin_op(p, t, op, lhs, rhs); });
}

lir_view::ExprRef LirBuilder::unary(std::string op, lir::LExprPtr operand, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_unary(p, t, op, operand); });
}

lir_view::ExprRef LirBuilder::deref(lir::LExprPtr operand, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_deref(p, t, operand); });
}

lir_view::ExprRef LirBuilder::cast(lir_view::ExprRef operand, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_cast(p, t, operand, {}); });
}

lir_view::ExprRef LirBuilder::field_read(lir::LExprPtr receiver, std::string field, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_field_read(p, t, receiver, field); });
}

lir_view::ExprRef LirBuilder::index_read(lir::LExprPtr receiver, lir::LExprPtr index, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_index_read(p, t, receiver, index); });
}

lir_view::ExprRef LirBuilder::tuple_index(lir::LExprPtr receiver, uint32_t index, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_tuple_index(p, t, receiver, index); });
}

lir_view::ExprRef LirBuilder::slice_index(lir::LExprPtr slice, lir::LExprPtr index, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_slice_index(p, t, slice, index); });
}

lir_view::ExprRef LirBuilder::arr_lit(std::vector<lir::LExprPtr> elems, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_arr_lit(p, t, elems); });
}

lir_view::ExprRef LirBuilder::tuple_lit(std::vector<lir::LExprPtr> elems, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_tuple_lit(p, t, elems); });
}

lir_view::ExprRef LirBuilder::try_expr(lir::LExprPtr inner, int32_t ok_disc,
                                    int32_t err_disc, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){
            return lir_mirror_emit_try(p, t, inner, ok_disc, err_disc);
        });
}

lir_view::ExprRef LirBuilder::call(std::string callee,
                                std::vector<TypeRef> type_args,
                                std::vector<lir::LExprPtr> args,
                                TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_call(p, t, callee, type_args, args); });
}

lir_view::ExprRef LirBuilder::block_expr(lir_view::BlockRef block,
                                      lir::LExprPtr result, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_block_expr(p, t, block, result); });
}

lir_view::ExprRef LirBuilder::struct_lit(
    std::string name,
    std::vector<std::pair<std::string, lir::LExprPtr>> fields,
    TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_struct_lit(p, t, name, fields); });
}

lir_view::ExprRef LirBuilder::enum_lit(std::string enum_name, std::string variant,
                                    int64_t disc, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_enum_lit(p, t, enum_name, variant, disc); });
}

lir_view::ExprRef LirBuilder::closure_box(lir::EClosure* inner,
                                       TypeRef ty) {
    auto e = direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_closure_box(p, t, inner); });
    if (prog_.mirror_table) prog_.mirror_table->closure_box_inner[e.addr()] = inner;
    return e;
}

lir_view::ExprRef LirBuilder::closure_to_fnptr(lir_view::ExprRef arg, TypeRef new_ty) {
    if (!prog_.mirror_table) return arg;
    auto it = prog_.mirror_table->closure_box_inner.find(arg.addr());
    if (it == prog_.mirror_table->closure_box_inner.end() || !it->second) return arg;
    auto* inner = it->second;
    inner->as_fn_ptr = true;
    return closure_box(inner, new_ty);
}

lir_view::ExprRef LirBuilder::set_tuple_elem(lir_view::ExprRef tuple, size_t idx,
                                  lir_view::ExprRef new_value) {
    if (!prog_.mirror_table || !tuple) return tuple;
    if (tuple.kind() != lir_schema::expr::Code::TupleLit) return tuple;
    lir_view::ETupleLitView v{tuple};
    if (idx >= v.count()) return tuple;
    // Stage D: rebuild from the existing element mirror VIEWS (no reverse-map
    // round-trip to the C++ skeleton); the replaced slot takes new_value's view.
    std::vector<lir_view::ExprRef> elems;
    elems.reserve(v.count());
    for (uint64_t i = 0; i < v.count(); ++i)
        elems.push_back(i == idx ? new_value : v.elem(i));
    const uint8_t* mp = lir_mirror_emit_tuple_lit(prog_, tuple.type(prog_.type_pool.impl()), elems);
    return lir_view::ExprRef(prog_.type_pool.arena(), mp);
}

lir_view::ExprRef LirBuilder::closure_call(lir::LExprPtr callee,
                                        std::vector<lir::LExprPtr> args,
                                        TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_closure_call(p, t, callee, args); });
}

lir_view::ExprRef LirBuilder::fn_ptr_call(lir::LExprPtr callee,
                                       std::vector<lir::LExprPtr> args,
                                       TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_fn_ptr_call(p, t, callee, args); });
}

lir_view::ExprRef LirBuilder::addr_of_temp(lir::LExprPtr inner, bool is_mut, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_addr_of_temp(p, t, inner, is_mut); });
}

lir_view::ExprRef LirBuilder::reuse_mut_ref(const lir::LExprPtr& orig) {
    TypeRef t = orig ? orig.type(prog_.type_pool.impl()) : TypeRef{};
    if (!orig || !t) return orig;
    if (t.kind() != LogosType::Kind::MutRef || !t.pointee()) return orig;
    return addr_of_temp(deref(orig, t.pointee()), /*is_mut=*/true, t);
}

lir_view::ExprRef LirBuilder::slice_lit(lir::LExprPtr base, lir::LExprPtr len, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_slice_lit(p, t, base, len); });
}

lir_view::ExprRef LirBuilder::slice_len(lir::LExprPtr slice, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_slice_len(p, t, slice); });
}

lir_view::ExprRef LirBuilder::slice_ptr(lir::LExprPtr slice, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_slice_ptr(p, t, slice); });
}

lir_view::ExprRef LirBuilder::ptr_arith(lir::EPtrArith::Op op,
                                     lir::LExprPtr lhs, lir::LExprPtr rhs,
                                     TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){
            return lir_mirror_emit_ptr_arith(p, t, static_cast<uint8_t>(op), lhs, rhs);
        });
}

lir_view::ExprRef LirBuilder::ptr_diff(bool by_byte,
                                    lir::LExprPtr lhs, lir::LExprPtr rhs,
                                    TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_ptr_diff(p, t, by_byte, lhs, rhs); });
}

lir_view::ExprRef LirBuilder::enum_lit_data(std::string enum_name, std::string variant,
                                         int64_t disc,
                                         std::vector<lir::LExprPtr> payload,
                                         TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){
            return lir_mirror_emit_enum_lit_data(p, t, enum_name, variant, disc, payload);
        });
}

lir_view::ExprRef LirBuilder::method_call(lir::LExprPtr receiver, std::string method,
                                       std::string resolved_symbol,
                                       std::vector<TypeRef> type_args,
                                       std::vector<lir::LExprPtr> args,
                                       int32_t vtable_index, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){
            return lir_mirror_emit_method_call(p, t, receiver, method, resolved_symbol,
                                                type_args, args, vtable_index, {}, {}, {});
        });
}

lir_view::ExprRef LirBuilder::writ_cast(lir::LExprPtr operand, std::string build_fn,
                                       TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_cast(p, t, operand, build_fn); });
}

// Stage D: retype by mirror view — the mirror IS the truth, so just rewrite its
// TYPE key (no C++ skeleton node needed; callers that only hold an ExprRef use
// this instead of round-tripping through lexpr_of).
void LirBuilder::retype_expr(lir_view::ExprRef e, TypeRef new_ty) {
    if (e) lir_mirror_retype_expr(prog_, e.addr(), new_ty);
}

lir_view::StmtRef LirBuilder::stmt_expr(lir::LExprPtr expr, uint32_t line) {
    return lir_view::StmtRef(prog_.type_pool.arena(),
        lir_mirror_emit_expr_stmt(prog_, line, expr));
}

lir_view::StmtRef LirBuilder::stmt_break(lir::LExprPtr value, std::string label, uint32_t line) {
    return lir_view::StmtRef(prog_.type_pool.arena(),
        lir_mirror_emit_break(prog_, line, value, label));
}

lir_view::StmtRef LirBuilder::stmt_continue(std::string label, uint32_t line) {
    return lir_view::StmtRef(prog_.type_pool.arena(),
        lir_mirror_emit_continue(prog_, line, label));
}

lir_view::StmtRef LirBuilder::stmt_return(lir::LExprPtr value, uint32_t line) {
    return lir_view::StmtRef(prog_.type_pool.arena(),
        lir_mirror_emit_return(prog_, line, value));
}

lir_view::StmtRef LirBuilder::stmt_assign(std::string name, lir::LExprPtr value, uint32_t line, bool drop_old) {
    return lir_view::StmtRef(prog_.type_pool.arena(),
        lir_mirror_emit_assign(prog_, line, name, value, drop_old));
}

lir_view::StmtRef LirBuilder::stmt_deref_write(lir::LExprPtr ptr, lir::LExprPtr value, uint32_t line, bool drop_old) {
    return lir_view::StmtRef(prog_.type_pool.arena(),
        lir_mirror_emit_deref_write(prog_, line, ptr, value, drop_old));
}

lir_view::ExprRef LirBuilder::call_v(lir::ECall ec, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){
            return lir_mirror_emit_call(p, t, ec.callee, ec.type_args, ec.args);
        });
}

lir_view::ExprRef LirBuilder::method_call_v(lir::EMethodCall mc, TypeRef ty) {
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){
            return lir_mirror_emit_method_call(p, t, mc.receiver, mc.method,
                mc.resolved_symbol, mc.type_args, mc.args, mc.vtable_index,
                mc.resolved_type, mc.tag_system, mc.tag_trait);
        });
}

lir_view::ExprRef LirBuilder::if_expr_v(lir::EIfExpr eif, TypeRef ty) {
    auto cond = std::move(eif.cond);
    auto thn  = std::move(eif.then_val);
    auto els  = std::move(eif.else_val);
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_if_expr(p, t, cond, thn, els); });
}

lir_view::ExprRef LirBuilder::writ_lit_v(lir::EWritLit lit, TypeRef ty) {
    auto root  = lit.root;
    auto hc    = lit.has_captures;
    auto cex   = std::move(lit.capture_exprs);
    auto cty   = std::move(lit.capture_types);
    auto cpc   = lit.capture_param_count;
    auto blob  = std::move(lit.static_blob);
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_writ_lit(p, t, root, hc, cex, cty, cpc, blob); });
}

lir_view::ExprRef LirBuilder::match_expr_v(lir::EMatchExpr me, TypeRef ty) {
    auto scrut = me.scrut;
    auto arms = std::move(me.arms);
    return direct(prog_, ty,
        [&](auto& p, TypeRef t){ return lir_mirror_emit_match_expr(p, t, scrut, arms); });
}

} // namespace logos::compiler
