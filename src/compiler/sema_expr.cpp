// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include "sema_impl.hpp"

#include <cstdio>
#include <format>
#include <functional>

namespace logos::compiler {

namespace la = ast;
using hermes::TinyMapView;
using hermes::ArrayView;
using hermes::StringView;
using hermes::AnyVal;
using hermes::MemHolder;

// Expression lowering methods

lir::LExprPtr SemaChecker::lower_expr(TinyMapView expr) {
    if (expr.is_null()) return error_expr();
    node_line_ = get_line(expr);
    int32_t c = code_of(expr);

    switch (c) {

    case la::LIT_INT: {
        auto sv = str_of(expr.get(la::VALUE.code));
        int64_t v = parse_int_literal(sv);
        return make_expr(intlit_t(), lir::ELitInt{v});
    }
    case la::LIT_FLOAT: {
        auto sv = str_of(expr.get(la::VALUE.code));
        double v = std::stod(std::string(sv));
        return make_expr(prim(LogosType::Kind::FloatLit), lir::ELitFloat{v});
    }
    case la::LIT_BOOL: {
        AnyVal av = expr.get(la::VALUE.code);
        bool v = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        return make_expr(bool_t(), lir::ELitBool{v});
    }
    case la::LIT_STR: {
        auto sv = str_of(expr.get(la::VALUE.code));
        return make_expr(make_ptr(false, u8_t()), lir::ELitStr{std::string(sv)});
    }

    case la::VAR_REF: {
        auto name = str_of(expr.get(la::NAME.code));
        auto* t = lookup(name);
        if (!t) {
            error(std::format("undefined variable '{}'", name));
            return error_expr();
        }
        if (moved_vars_.count(std::string(name)))
            error(std::format("use of moved variable '{}'", name));
        return make_expr(t, lir::EVarRef{std::string(name)});
    }

    case la::PACK_EXPAND: {
        auto name = str_of(expr.get(la::NAME.code));
        // Type is the variadic TypeVar — mono will expand this
        auto* t = lookup(name);
        if (!t) {
            error(std::format("pack expand: undefined variable '{}'", name));
            return error_expr();
        }
        return make_expr(t, lir::EPackExpand{std::string(name)});
    }

    case la::PAREN_EXPR:
        if (expr.has_key(la::VALUE))
            return lower_expr(map_of(expr.get(la::VALUE.code)));
        return error_expr();

    case la::CAST: {
        lir::LExprPtr inner = expr.has_key(la::VALUE)
            ? lower_expr(map_of(expr.get(la::VALUE.code)))
            : error_expr();
        const LogosType* target = expr.has_key(la::TYPE)
            ? resolve_type(map_of(expr.get(la::TYPE.code)))
            : error_t();
        // Reject aggregate-to-primitive casts (struct/class/array/tuple/enum → scalar).
        if (inner->type && target &&
            inner->type->kind != LogosType::Kind::Error &&
            target->kind != LogosType::Kind::Error) {
            bool src_agg = inner->type->kind == LogosType::Kind::Struct ||
                           inner->type->kind == LogosType::Kind::Class  ||
                           inner->type->kind == LogosType::Kind::Array  ||
                           inner->type->kind == LogosType::Kind::Tuple  ||
                           inner->type->kind == LogosType::Kind::Enum;
            bool tgt_scalar = target->kind == LogosType::Kind::I32  ||
                              target->kind == LogosType::Kind::I64  ||
                              target->kind == LogosType::Kind::U8   ||
                              target->kind == LogosType::Kind::I8   ||
                              target->kind == LogosType::Kind::I16  ||
                              target->kind == LogosType::Kind::U16  ||
                              target->kind == LogosType::Kind::U32  ||
                              target->kind == LogosType::Kind::U64  ||
                              target->kind == LogosType::Kind::F64  ||
                              target->kind == LogosType::Kind::F32  ||
                              target->kind == LogosType::Kind::Bool ||
                              target->kind == LogosType::Kind::Ptr;
            if (src_agg && tgt_scalar)
                error(std::format("cannot cast '{}' to '{}'",
                      type_str(inner->type), type_str(target)));
        }
        return make_expr(target, lir::ECast{std::move(inner)});
    }

    case la::BINOP:       return lower_binop(expr);
    case la::UNARY:       return lower_unary(expr);
    case la::DEREF:       return lower_deref(expr);

    case la::ADDR_OF_MUT: {
        // &mut var — exclusive mutable reference
        auto child = map_of(expr.get(la::VALUE.code));
        if (code_of(child) != la::VAR_REF) {
            error("'&mut' operand must be a variable");
            return error_expr();
        }
        auto var_name = str_of(child.get(la::NAME.code));
        auto* vt = lookup(var_name);
        if (!vt) {
            error(std::format("'&mut': undefined variable '{}'", var_name));
            return error_expr();
        }
        // For arrays, produce &mut elem (reference to first element)
        if (vt->kind == LogosType::Kind::Array)
            return make_expr(make_ref(true, vt->elem), lir::EAddrOf{std::string(var_name)});
        return make_expr(make_ref(true, vt), lir::EAddrOf{std::string(var_name)});
    }
    case la::TRY_EXPR: {
        // expr? — extract Ok(v) or early-return Err(e).
        // Requires: inner : Result<T, E>, current fn return type : Result<?, E>.
        auto inner = expr.has_key(la::VALUE)
            ? lower_expr(map_of(expr.get(la::VALUE.code)))
            : error_expr();
        auto* inner_t = inner->type;
        if (inner_t->kind != LogosType::Kind::Enum || inner_t->enum_name != "Result"
            || inner_t->type_args.size() < 2) {
            error("'?' operator requires a Result<T, E> expression");
            return error_expr();
        }
        if (!ret_type_ || ret_type_->kind != LogosType::Kind::Enum
            || ret_type_->enum_name != "Result") {
            error("'?' operator used in function that does not return Result<T, E>");
            return error_expr();
        }
        // Find Ok and Err discriminants from the enum definition.
        int32_t ok_disc = 0, err_disc = 1;  // default: Ok first, Err second
        auto eit = enums_.find("Result");
        if (eit != enums_.end()) {
            for (auto& v : eit->second.variants) {
                if (v.name == "Ok")  ok_disc  = v.value;
                if (v.name == "Err") err_disc = v.value;
            }
        }
        auto* ok_type = inner_t->type_args[0];  // T
        return make_expr(ok_type, lir::ETry{std::move(inner), ok_disc, err_disc});
    }

    case la::CALL:         return lower_call(expr);
    case la::GENERIC_CALL: return lower_generic_call(expr);
    case la::METHOD_CALL:  return lower_method_call(expr);
    case la::STATIC_CALL:  return lower_static_call(expr);
    case la::FIELD_READ:  return lower_field_read(expr);
    case la::STRUCT_LIT:  return lower_struct_lit(expr);
    case la::INDEX_READ:  return lower_index_read(expr);
    case la::ARR_LIT:      return lower_arr_lit(expr);
    case la::ARR_FILL_LIT: return lower_arr_fill_lit(expr);
    case la::ENUM_LIT:    return lower_enum_lit(expr);
    case la::ENUM_LIT_DATA: return lower_enum_lit_data(expr);
    case la::NEW_EXPR:    return lower_new_expr(expr);
    case la::IF:          return lower_if_expr(expr);
    case la::MATCH:       return lower_match_expr(expr);
    case la::CLOSURE_EXPR: return lower_closure_expr(expr);

    case la::UNSAFE_BLOCK: {
        if (!expr.has_key(la::BODY)) return error_expr();
        auto inner = map_of(expr.get(la::BODY.code));
        bool was = inside_unsafe_;
        inside_unsafe_ = true;
        lir::LExprPtr result = nullptr;
        auto block = std::make_unique<lir::LBlock>();
        if (inner.has_key(la::ITEMS)) {
            auto stmts = arr_of(inner.get(la::ITEMS.code));
            for (uint64_t i = 0; i < stmts.size(); ++i) {
                auto s = map_of(stmts.get(i));
                if (i == stmts.size() - 1) {
                    int32_t lc = code_of(s);
                    if (lc == la::EXPR_STMT && s.has_key(la::VALUE)) {
                        result = lower_expr(map_of(s.get(la::VALUE.code)));
                    } else if (lc != la::EXPR_STMT && lc != la::LET && lc != la::LET_DESTRUCT && lc != la::RETURN) {
                        result = lower_expr(s);
                    } else {
                        block->stmts.push_back(lower_stmt(s));
                    }
                } else {
                    block->stmts.push_back(lower_stmt(s));
                }
            }
        }
        inside_unsafe_ = was;
        if (!result) return make_expr(void_t(), lir::EBlockExpr{std::move(block), nullptr});
        const LogosType* rt = result->type;
        return make_expr(rt, lir::EBlockExpr{std::move(block), std::move(result)});
    }

    case la::TUPLE_LIT: {
        if (!expr.has_key(la::ITEMS)) return error_expr();
        auto items = arr_of(expr.get(la::ITEMS.code));
        std::vector<lir::LExprPtr> elems;
        std::vector<const LogosType*> elem_types;
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto e = lower_expr(map_of(items.get(i)));
            // Upgrade IntLit element type to i64 if the literal overflows i32.
            const LogosType* et = e->type;
            if (et->kind == LogosType::Kind::IntLit) {
                if (auto v = get_intlit_value(e.get()))
                    if (*v > (int64_t)INT32_MAX || *v < (int64_t)INT32_MIN)
                        et = prim(LogosType::Kind::I64);
            }
            elem_types.push_back(et);
            elems.push_back(std::move(e));
        }
        auto* tt = make_tuple_type(std::move(elem_types));
        return make_expr(tt, lir::ETupleLit{std::move(elems)});
    }

    case la::TUPLE_INDEX: {
        auto recv = expr.has_key(la::RECEIVER)
            ? lower_expr(map_of(expr.get(la::RECEIVER.code)))
            : error_expr();
        if (recv->type->kind != LogosType::Kind::Tuple) {
            error(std::format("tuple index on non-tuple type '{}'", type_str(recv->type)));
            return error_expr();
        }
        auto sv = str_of(expr.get(la::FIELD.code));
        uint32_t idx = (uint32_t)parse_int_literal(sv);
        if (idx >= recv->type->tuple_elems.size()) {
            error(std::format("tuple index {} out of range (tuple has {} elements)",
                  idx, recv->type->tuple_elems.size()));
            return error_expr();
        }
        auto* elem_t = recv->type->tuple_elems[idx];
        return make_expr(elem_t, lir::ETupleIndex{std::move(recv), idx});
    }

    default:
        return error_expr();
    }
}

lir::LExprPtr SemaChecker::lower_binop(TinyMapView node) {
    auto op  = str_of(node.get(la::OP.code));
    auto lhs = lower_expr(map_of(node.get(la::LHS.code)));
    auto rhs = lower_expr(map_of(node.get(la::RHS.code)));
    auto* lt = lhs->type;
    auto* rt = rhs->type;

    const LogosType* result_type = error_t();

    // Operator overloading: if LHS is a struct/class, desugar to trait method call.
    if (lt->kind == LogosType::Kind::Struct || lt->kind == LogosType::Kind::Class) {
        // Map operator to trait name and method
        std::string trait_name, method_name;
        if      (op == "+")  { trait_name = "Add"; method_name = "add"; }
        else if (op == "-")  { trait_name = "Sub"; method_name = "sub"; }
        else if (op == "*")  { trait_name = "Mul"; method_name = "mul"; }
        else if (op == "/")  { trait_name = "Div"; method_name = "div"; }
        else if (op == "%")  { trait_name = "Rem"; method_name = "rem"; }
        else if (op == "==") { trait_name = "Eq";  method_name = "eq"; }
        else if (op == "!=") { trait_name = "Eq";  method_name = "ne"; }
        else if (op == "<")  { trait_name = "Ord"; method_name = "lt"; }
        else if (op == "<=") { trait_name = "Ord"; method_name = "le"; }
        else if (op == ">")  { trait_name = "Ord"; method_name = "gt"; }
        else if (op == ">=") { trait_name = "Ord"; method_name = "ge"; }
        if (!trait_name.empty()) {
            auto type_name = (lt->kind == LogosType::Kind::Struct)
                ? concrete_struct_name(lt) : concrete_class_name(lt);
            auto mangled = type_name + "__" + method_name;
            auto fit = funcs_.find(mangled);
            if (fit != funcs_.end()) {
                std::vector<lir::LExprPtr> args;
                args.push_back(std::move(lhs));
                args.push_back(std::move(rhs));
                return make_expr(fit->second.ret_type,
                    lir::ECall{mangled, {}, std::move(args)});
            }
            // No impl found — fall through to normal type checking
        }
    }

    if (lt->kind == LogosType::Kind::Error || rt->kind == LogosType::Kind::Error) {
        result_type = error_t();
    } else if (op == "&&" || op == "||") {
        if (lt->kind != LogosType::Kind::Bool)
            error(std::format("operator '{}': left must be bool, got {}", op, type_str(lt)));
        if (rt->kind != LogosType::Kind::Bool)
            error(std::format("operator '{}': right must be bool, got {}", op, type_str(rt)));
        result_type = bool_t();
    } else if (op == "==" || op == "!=" ||
               op == "<"  || op == "<=" || op == ">" || op == ">=") {
        // Allow pointer-vs-integer-literal comparison (null check: ptr == 0)
        bool ptr_null_cmp =
            (lt->kind == LogosType::Kind::Ptr && rt->kind == LogosType::Kind::IntLit) ||
            (rt->kind == LogosType::Kind::Ptr && lt->kind == LogosType::Kind::IntLit);
        bool ok = ptr_null_cmp || types_compatible(lt, rt) || types_compatible(rt, lt);
        if (!ok)
            error(std::format("operator '{}': type mismatch ({} vs {})",
                  op, type_str(lt), type_str(rt)));
        // Detect comparisons against IntLit values that can't fit in the other operand.
        // E.g. x: i32 == 10000000000 — the literal can never equal any i32 value.
        if (lt->kind == LogosType::Kind::IntLit && is_integer_kind(rt->kind)) {
            if (auto v = get_intlit_value(lhs.get()))
                if (!intlit_fits(*v, rt->kind))
                    error(std::format("operator '{}': literal value {} does not fit in {}",
                          op, *v, type_str(rt)));
        } else if (rt->kind == LogosType::Kind::IntLit && is_integer_kind(lt->kind)) {
            if (auto v = get_intlit_value(rhs.get()))
                if (!intlit_fits(*v, lt->kind))
                    error(std::format("operator '{}': literal value {} does not fit in {}",
                          op, *v, type_str(lt)));
        }
        result_type = bool_t();
    } else if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
        if (!is_numeric(lt))
            error(std::format("operator '{}': left must be numeric, got {}", op, type_str(lt)));
        if (!is_numeric(rt))
            error(std::format("operator '{}': right must be numeric, got {}", op, type_str(rt)));
        bool both_int = is_integer_kind(lt->kind) && is_integer_kind(rt->kind);
        if (!both_int) {
            bool compat = types_compatible(lt, rt) || types_compatible(rt, lt);
            if (is_numeric(lt) && is_numeric(rt) && !compat)
                error(std::format("operator '{}': type mismatch ({} vs {})",
                      op, type_str(lt), type_str(rt)));
            // If one side is TypeVar and the other is IntLit, result is the TypeVar
            if (lt->kind == LogosType::Kind::TypeVar) result_type = lt;
            else if (rt->kind == LogosType::Kind::TypeVar) result_type = rt;
            else result_type = lt;
        } else {
            if (!types_compatible(lt, rt) && !types_compatible(rt, lt))
                error(std::format("operator '{}': type mismatch ({} vs {})",
                      op, type_str(lt), type_str(rt)));
            result_type = unify_int(lt, rt);
            // Check IntLit operand fits in the concrete type of the other operand.
            if (lt->kind == LogosType::Kind::IntLit && rt->kind != LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(lhs.get()))
                    if (!intlit_fits(*v, rt->kind))
                        error(std::format("operator '{}': left value {} does not fit in {}",
                              op, *v, type_str(rt)));
            if (rt->kind == LogosType::Kind::IntLit && lt->kind != LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(rhs.get()))
                    if (!intlit_fits(*v, lt->kind))
                        error(std::format("operator '{}': right value {} does not fit in {}",
                              op, *v, type_str(lt)));
        }
    } else if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
        // Bitwise and shift operators — require integer operands.
        if (!is_integer_kind(lt->kind) && lt->kind != LogosType::Kind::IntLit)
            error(std::format("operator '{}': left must be integer, got {}", op, type_str(lt)));
        if (!is_integer_kind(rt->kind) && rt->kind != LogosType::Kind::IntLit)
            error(std::format("operator '{}': right must be integer, got {}", op, type_str(rt)));
        result_type = unify_int(lt, rt);
        // Check IntLit operand fits in the concrete type of the other operand.
        if (lt->kind == LogosType::Kind::IntLit && rt->kind != LogosType::Kind::IntLit)
            if (auto v = get_intlit_value(lhs.get()))
                if (!intlit_fits(*v, rt->kind))
                    error(std::format("operator '{}': left value {} does not fit in {}",
                          op, *v, type_str(rt)));
        if (rt->kind == LogosType::Kind::IntLit && lt->kind != LogosType::Kind::IntLit)
            if (auto v = get_intlit_value(rhs.get()))
                if (!intlit_fits(*v, lt->kind))
                    error(std::format("operator '{}': right value {} does not fit in {}",
                          op, *v, type_str(lt)));
    } else {
        error(std::format("unknown binary operator '{}'", op));
    }

    return make_expr(result_type, lir::EBinOp{std::string(op), std::move(lhs), std::move(rhs)});
}

lir::LExprPtr SemaChecker::lower_unary(TinyMapView node) {
    auto op  = str_of(node.get(la::OP.code));

    // & — address-of or array-to-slice
    if (op == "&") {
        auto child = map_of(node.get(la::VALUE.code));
        if (code_of(child) != la::VAR_REF) {
            error("'&' operand must be a variable");
            return error_expr();
        }
        auto var_name = str_of(child.get(la::NAME.code));
        auto* vt = lookup(var_name);
        if (!vt) {
            error(std::format("'&': undefined variable '{}'", var_name));
            return error_expr();
        }
        // &array → slice: &[T] with len = array size
        if (vt->kind == LogosType::Kind::Array) {
            auto addr = make_expr(make_ref(false, vt->elem), lir::EAddrOf{std::string(var_name)});
            auto len  = make_expr(prim(LogosType::Kind::I64), lir::ELitInt{(int64_t)vt->arr_size});
            return make_expr(make_slice_type(vt->elem),
                lir::ESliceLit{std::move(addr), std::move(len)});
        }
        return make_expr(make_ref(false, vt), lir::EAddrOf{std::string(var_name)});
    }

    auto operand = lower_expr(map_of(node.get(la::VALUE.code)));
    auto* vt = operand->type;
    if (vt->kind == LogosType::Kind::Error)
        return make_expr(error_t(), lir::EUnary{std::string(op), std::move(operand)});

    // Unary operator overloading for struct/class types
    if (vt->kind == LogosType::Kind::Struct || vt->kind == LogosType::Kind::Class) {
        std::string trait_name, method_name;
        if      (op == "-") { trait_name = "Neg"; method_name = "neg"; }
        else if (op == "!") { trait_name = "Not"; method_name = "not_"; }
        if (!trait_name.empty()) {
            auto type_name = (vt->kind == LogosType::Kind::Struct)
                ? concrete_struct_name(vt) : concrete_class_name(vt);
            auto mangled = type_name + "__" + method_name;
            auto fit = funcs_.find(mangled);
            if (fit != funcs_.end()) {
                std::vector<lir::LExprPtr> args;
                args.push_back(std::move(operand));
                return make_expr(fit->second.ret_type,
                    lir::ECall{mangled, {}, std::move(args)});
            }
        }
    }

    const LogosType* result_type = error_t();
    if (op == "-") {
        if (!is_numeric(vt))
            error(std::format("unary '-': operand must be numeric, got {}", type_str(vt)));
        result_type = vt;
    } else if (op == "!") {
        if (vt->kind == LogosType::Kind::Bool) {
            result_type = bool_t();
        } else if (is_integer_kind(vt->kind) || vt->kind == LogosType::Kind::IntLit) {
            // Bitwise NOT (~x) on integer types
            result_type = (vt->kind == LogosType::Kind::IntLit) ? i32_t() : vt;
        } else {
            error(std::format("unary '!': operand must be bool or integer, got {}", type_str(vt)));
            result_type = bool_t();
        }
    } else {
        error(std::format("unknown unary operator '{}'", op));
    }

    return make_expr(result_type, lir::EUnary{std::string(op), std::move(operand)});
}

lir::LExprPtr SemaChecker::lower_deref(TinyMapView node) {
    auto operand = lower_expr(map_of(node.get(la::VALUE.code)));
    auto* vt = operand->type;
    if (vt->kind == LogosType::Kind::Error)
        return make_expr(error_t(), lir::EDeref{std::move(operand)});
    if (vt->kind != LogosType::Kind::Ptr &&
        vt->kind != LogosType::Kind::Ref &&
        vt->kind != LogosType::Kind::MutRef) {
        error(std::format("dereference of non-pointer type {}", type_str(vt)));
        return make_expr(error_t(), lir::EDeref{std::move(operand)});
    }
    // Raw pointer deref requires unsafe context
    if (vt->kind == LogosType::Kind::Ptr && !inside_unsafe_)
        error("dereference of raw pointer requires unsafe context");
    auto* res = vt->pointee ? vt->pointee : error_t();
    return make_expr(res, lir::EDeref{std::move(operand)});
}

lir::LExprPtr SemaChecker::lower_call(TinyMapView node) {
    auto callee = str_of(node.get(la::CALLEE.code));

    // Check if callee is a closure variable
    auto* callee_type = lookup(callee);
    if (callee_type && callee_type->kind == LogosType::Kind::Closure) {
        std::vector<lir::LExprPtr> arg_exprs;
        if (node.has_key(la::ARGS)) {
            auto args = arr_of(node.get(la::ARGS.code));
            for (uint64_t i = 0; i < args.size(); ++i)
                arg_exprs.push_back(lower_expr(map_of(args.get(i))));
        }
        auto callee_expr = make_expr(callee_type, lir::EVarRef{std::string(callee)});
        return make_expr(callee_type->closure_ret ? callee_type->closure_ret : void_t(),
            lir::EClosureCall{std::move(callee_expr), std::move(arg_exprs)});
    }

    // format() is now a library function in std.fmt (variadic generics + Format trait).
    // The old intrinsic path (EFormatCall) is retained for future intrinsics but
    // no longer intercepts the "format" name.

    // Lower arguments first — needed for type inference
    std::vector<lir::LExprPtr> arg_exprs;
    if (node.has_key(la::ARGS)) {
        auto args = arr_of(node.get(la::ARGS.code));
        for (uint64_t i = 0; i < args.size(); ++i)
            arg_exprs.push_back(lower_expr(map_of(args.get(i))));
    }

    auto fit  = funcs_.find(std::string(callee));
    auto git  = generic_funcs_.find(std::string(callee));
    uint64_t n_args = arg_exprs.size();

    // Resolve the "best" SemaFuncInfo to try.
    // Priority:
    //   1. generic_funcs_ (variadic overload, or overloaded name) if callee is there
    //   2. funcs_ with non-empty type_params (plain generic fn, stored in funcs_)
    //   3. funcs_ with empty type_params (concrete fn)
    // If the function is generic (by either map or non-empty type_params in funcs_),
    // try to infer type args from the actual argument types.

    // Identify which entry to use
    const SemaFuncInfo* infer_fi = nullptr;
    if (fit == funcs_.end() && git == generic_funcs_.end()) {
        error(std::format("call to undefined function '{}'", callee));
        return make_expr(error_t(), lir::ECall{std::string(callee), {}, std::move(arg_exprs)});
    }
    // Pub check and unsafe check.
    {
        const SemaFuncInfo* fi_chk = (fit != funcs_.end()) ? &fit->second
                                   : &git->second;
        check_pub_access(fi_chk->is_pub, fi_chk->package, callee);
        if (fi_chk->is_unsafe && !inside_unsafe_)
            error(std::format("call to unsafe function '{}' requires unsafe context", callee));
    }

    // Determine if we should try inference
    bool try_inference = false;
    if (fit == funcs_.end()) {
        // Only in generic_funcs_
        infer_fi = &git->second;
        try_inference = true;
    } else if (!fit->second.type_params.empty()) {
        // In funcs_ but is a generic function (no non-generic overload exists)
        infer_fi = &fit->second;
        try_inference = true;
    } else if (git != generic_funcs_.end()) {
        // Non-generic in funcs_, generic overload in generic_funcs_.
        // Try generic when arity doesn't match the non-generic.
        bool arity_ok = fit->second.is_vararg
            ? n_args >= fit->second.param_types.size()
            : n_args == fit->second.param_types.size();
        if (!arity_ok) {
            infer_fi = &git->second;
            try_inference = true;
        }
    }

    if (try_inference && infer_fi) {
        // Don't infer inside generic bodies: pack expansions or TypeVar/AssocType args
        // indicate we're in a partially-substituted context — defer to mono.
        bool in_generic_context = false;
        for (auto& a : arg_exprs) {
            if (std::holds_alternative<lir::EPackExpand>(a->kind)) {
                in_generic_context = true; break;
            }
            auto* t = a->type;
            if (t && (t->kind == LogosType::Kind::TypeVar ||
                      t->kind == LogosType::Kind::AssocType)) {
                in_generic_context = true; break;
            }
        }
        if (!in_generic_context) {
            std::vector<const LogosType*> inferred;
            if (infer_type_args(*infer_fi, arg_exprs, inferred))
                return finish_generic_call(callee, *infer_fi, std::move(inferred), std::move(arg_exprs));
            error(std::format("call to '{}': could not infer all type arguments — use explicit f::<T>(...) syntax", callee));
            return make_expr(error_t(), lir::ECall{std::string(callee), {}, std::move(arg_exprs)});
        }
        // Fall through to non-generic path (mono will handle instantiation)
    }

    // Non-generic path
    auto& fi = fit->second;

    // If any arg is a pack expansion, skip checking — mono will expand
    bool has_pack_expand = false;
    for (auto& a : arg_exprs)
        if (std::holds_alternative<lir::EPackExpand>(a->kind))
            has_pack_expand = true;

    if (has_pack_expand) {
        // Pass through — mono will expand and validate
    } else if (fi.is_vararg) {
        if (n_args < fi.param_types.size()) {
            error(std::format("call to vararg '{}': expected at least {} args, got {}",
                  callee, fi.param_types.size(), n_args));
        } else {
            for (uint64_t i = 0; i < fi.param_types.size(); ++i) {
                auto* at = arg_exprs[i]->type;
                auto* pt = fi.param_types[i];
                if (at->kind != LogosType::Kind::Error &&
                    pt->kind != LogosType::Kind::Error &&
                    !types_compatible(at, pt))
                    error(std::format("call to '{}' arg {}: expected {}, got {}",
                          callee, i + 1, type_str(pt), type_str(at)));
                if (at->kind == LogosType::Kind::IntLit && pt->kind != LogosType::Kind::Error)
                    if (auto v = get_intlit_value(arg_exprs[i].get()))
                        if (!intlit_fits(*v, pt->kind))
                            error(std::format("call to '{}' arg {}: value {} does not fit in {}",
                                  callee, i + 1, *v, type_str(pt)));
            }
        }
    } else if (n_args != fi.param_types.size()) {
        error(std::format("call to '{}': expected {} args, got {}",
              callee, fi.param_types.size(), n_args));
    } else {
        for (uint64_t i = 0; i < n_args; ++i) {
            auto* at = arg_exprs[i]->type;
            auto* pt = fi.param_types[i];
            if (at->kind != LogosType::Kind::Error &&
                pt->kind != LogosType::Kind::Error &&
                !types_compatible(at, pt))
                error(std::format("call to '{}' arg {}: expected {}, got {}",
                      callee, i + 1, type_str(pt), type_str(at)));
            if (at->kind == LogosType::Kind::IntLit && pt->kind != LogosType::Kind::Error)
                if (auto v = get_intlit_value(arg_exprs[i].get()))
                    if (!intlit_fits(*v, pt->kind))
                        error(std::format("call to '{}' arg {}: value {} does not fit in {}",
                              callee, i + 1, *v, type_str(pt)));
            // Check array literal elements against narrow array param type.
            if (at->kind == LogosType::Kind::Array && pt->kind == LogosType::Kind::Array && pt->elem)
                if (auto* al = std::get_if<lir::EArrLit>(&arg_exprs[i]->kind))
                    for (size_t ei = 0; ei < al->elems.size(); ++ei)
                        if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(al->elems[ei].get()))
                                if (!intlit_fits(*v, pt->elem->kind))
                                    error(std::format("call to '{}' arg {}: array element {}: value {} does not fit in {}",
                                          callee, i + 1, ei, *v, type_str(pt->elem)));
            // Check tuple literal elements against narrow tuple param element types.
            if (at->kind == LogosType::Kind::Tuple && pt->kind == LogosType::Kind::Tuple)
                if (auto* tl = std::get_if<lir::ETupleLit>(&arg_exprs[i]->kind))
                    for (size_t ei = 0; ei < tl->elems.size() && ei < pt->tuple_elems.size(); ++ei) {
                        if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(tl->elems[ei].get()))
                                if (pt->tuple_elems[ei] && !intlit_fits(*v, pt->tuple_elems[ei]->kind))
                                    error(std::format("call to '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                          callee, i + 1, ei, *v, type_str(pt->tuple_elems[ei])));
                        if (pt->tuple_elems[ei] && pt->tuple_elems[ei]->kind == LogosType::Kind::Array &&
                            pt->tuple_elems[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                            if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                    if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(ial->elems[ii].get()))
                                            if (!intlit_fits(*v, pt->tuple_elems[ei]->elem->kind))
                                                error(std::format("call to '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                      callee, i + 1, ei, ii, *v, type_str(pt->tuple_elems[ei]->elem)));
                        if (pt->tuple_elems[ei] && pt->tuple_elems[ei]->kind == LogosType::Kind::Tuple &&
                            tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                            if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                for (size_t ii = 0; ii < itl->elems.size() && ii < pt->tuple_elems[ei]->tuple_elems.size(); ++ii)
                                    if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(itl->elems[ii].get()))
                                            if (pt->tuple_elems[ei]->tuple_elems[ii] && !intlit_fits(*v, pt->tuple_elems[ei]->tuple_elems[ii]->kind))
                                                error(std::format("call to '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      callee, i + 1, ei, ii, *v, type_str(pt->tuple_elems[ei]->tuple_elems[ii])));
                    }
        }
    }

    // Move semantics: mark by-value move-type args as moved
    for (auto& a : arg_exprs) {
        if (is_move_type(a->type)) {
            if (auto* vr = std::get_if<lir::EVarRef>(&a->kind))
                mark_moved(vr->name);
        }
    }

    return make_expr(fi.ret_type, lir::ECall{std::string(callee), {}, std::move(arg_exprs)});
}

void SemaChecker::unify_types(const LogosType* formal, const LogosType* actual,
                     std::unordered_map<std::string, const LogosType*>& bindings) {
    if (!formal || !actual) return;
    if (actual->kind == LogosType::Kind::Error ||
        formal->kind == LogosType::Kind::Error) return;

    // Widen IntLit to i32 before any binding
    const LogosType* actual_norm = actual;
    if (actual->kind == LogosType::Kind::IntLit)
        actual_norm = prim(LogosType::Kind::I32);

    if (formal->kind == LogosType::Kind::TypeVar) {
        if (formal->type_var_name == "Self") return;  // skip implicit Self
        if (!bindings.count(formal->type_var_name))
            bindings[formal->type_var_name] = actual_norm;
        return;
    }

    switch (formal->kind) {
    case LogosType::Kind::Ptr:
        if (actual_norm->kind == LogosType::Kind::Ptr)
            unify_types(formal->pointee, actual_norm->pointee, bindings);
        else if (actual_norm->kind == LogosType::Kind::Ref ||
                 actual_norm->kind == LogosType::Kind::MutRef)
            unify_types(formal->pointee, actual_norm->pointee, bindings);
        break;
    case LogosType::Kind::Ref:
    case LogosType::Kind::MutRef:
        if (actual_norm->kind == LogosType::Kind::Ref ||
            actual_norm->kind == LogosType::Kind::MutRef ||
            actual_norm->kind == LogosType::Kind::Ptr)
            unify_types(formal->pointee, actual_norm->pointee, bindings);
        break;
    case LogosType::Kind::Array:
        if (actual_norm->kind == LogosType::Kind::Array)
            unify_types(formal->elem, actual_norm->elem, bindings);
        break;
    case LogosType::Kind::Slice:
        if (actual_norm->kind == LogosType::Kind::Slice)
            unify_types(formal->elem, actual_norm->elem, bindings);
        break;
    case LogosType::Kind::Struct:
        if (actual_norm->kind == LogosType::Kind::Struct &&
            formal->struct_name == actual_norm->struct_name) {
            for (size_t i = 0; i < formal->type_args.size() &&
                                i < actual_norm->type_args.size(); ++i)
                unify_types(formal->type_args[i], actual_norm->type_args[i], bindings);
        }
        break;
    case LogosType::Kind::Tuple:
        if (actual_norm->kind == LogosType::Kind::Tuple) {
            for (size_t i = 0; i < formal->tuple_elems.size() &&
                                i < actual_norm->tuple_elems.size(); ++i)
                unify_types(formal->tuple_elems[i], actual_norm->tuple_elems[i], bindings);
        }
        break;
    default:
        break;  // concrete type — nothing to bind
    }
}

bool SemaChecker::infer_type_args(const SemaFuncInfo& fi,
                         const std::vector<lir::LExprPtr>& arg_exprs,
                         std::vector<const LogosType*>& out_type_args,
                         const SemaSubst& context,
                         size_t param_offset) {
    std::unordered_map<std::string, const LogosType*> bindings(context.begin(), context.end());
    bool has_variadic = !fi.type_params.empty() && fi.type_params.back().is_variadic;
    size_t non_variadic_count = fi.type_params.size() - (has_variadic ? 1 : 0);
    size_t fixed_params = fi.param_types.size() >= param_offset
        ? fi.param_types.size() - param_offset - (has_variadic ? 1 : 0)
        : 0;

    // Unify fixed params against arg types
    for (size_t i = 0; i < fixed_params && i < arg_exprs.size(); ++i) {
        auto* pt = fi.param_types[param_offset + i];
        if (!context.empty()) pt = subst_type_sema(pt, context);
        unify_types(pt, arg_exprs[i]->type, bindings);
    }

    // Build type_args: non-variadic params first
    out_type_args.clear();
    for (size_t i = 0; i < non_variadic_count; ++i) {
        auto it = bindings.find(fi.type_params[i].name);
        if (it == bindings.end()) return false;  // param not inferrable
        out_type_args.push_back(it->second);
    }

    // Variadic pack: each arg beyond fixed_params contributes one pack element
    if (has_variadic) {
        for (size_t i = fixed_params; i < arg_exprs.size(); ++i) {
            auto* t = arg_exprs[i]->type;
            if (t->kind == LogosType::Kind::IntLit)
                t = prim(LogosType::Kind::I32);
            out_type_args.push_back(t);
        }
    }
    return true;
}

lir::LExprPtr SemaChecker::finish_generic_call(std::string_view callee_sv,
                                      const SemaFuncInfo& fi,
                                      std::vector<const LogosType*> type_args,
                                      std::vector<lir::LExprPtr> arg_exprs) {
    std::string callee{callee_sv};
    // Unsafe check: covers both inferred (lower_call) and explicit (lower_generic_call) paths.
    if (fi.is_unsafe && !inside_unsafe_)
        error(std::format("call to unsafe function '{}' requires unsafe context", callee_sv));
    bool has_variadic = !fi.type_params.empty() && fi.type_params.back().is_variadic;
    size_t non_variadic_count = fi.type_params.size() - (has_variadic ? 1 : 0);

    // Validate type arg count
    if (!fi.type_params.empty()) {
        if (has_variadic) {
            if (type_args.size() < non_variadic_count)
                error(std::format("call to '{}': expected at least {} type arg(s), got {}",
                      callee, non_variadic_count, type_args.size()));
        } else if (type_args.size() != fi.type_params.size()) {
            error(std::format("call to '{}': expected {} type arg(s), got {}",
                  callee, fi.type_params.size(), type_args.size()));
        }
    }

    // Build substitution map for non-variadic type params
    std::unordered_map<std::string, const LogosType*> subst;
    for (size_t i = 0; i < non_variadic_count && i < type_args.size(); ++i)
        subst[fi.type_params[i].name] = type_args[i];

    // Validate trait bounds for all type params (including variadic pack elements)
    check_type_bounds(std::string(callee), fi.type_params, type_args);

    // Substitute return type
    const LogosType* ret = subst_type_sema(fi.ret_type, subst);

    // Validate value argument count and types
    uint64_t n_args = arg_exprs.size();
    bool has_pack_expand = false;
    for (auto& a : arg_exprs)
        if (std::holds_alternative<lir::EPackExpand>(a->kind)) {
            has_pack_expand = true; break;
        }

    if (has_pack_expand) {
        // pass — mono expands
    } else if (has_variadic) {
        size_t fixed_params = fi.param_types.size() - 1;
        if (n_args < fixed_params)
            error(std::format("call to '{}': expected at least {} args, got {}",
                  callee, fixed_params, n_args));
        for (uint64_t i = 0; i < fixed_params && i < n_args; ++i) {
            auto* at = arg_exprs[i]->type;
            auto* pt = subst_type_sema(fi.param_types[i], subst);
            if (at->kind != LogosType::Kind::Error &&
                pt->kind != LogosType::Kind::Error &&
                pt->kind != LogosType::Kind::TypeVar &&
                !types_compatible(at, pt))
                error(std::format("call to '{}' arg {}: expected {}, got {}",
                      callee, i + 1, type_str(pt), type_str(at)));
            if (at->kind == LogosType::Kind::IntLit && pt->kind != LogosType::Kind::Error &&
                pt->kind != LogosType::Kind::TypeVar)
                if (auto v = get_intlit_value(arg_exprs[i].get()))
                    if (!intlit_fits(*v, pt->kind))
                        error(std::format("call to '{}' arg {}: value {} does not fit in {}",
                              callee, i + 1, *v, type_str(pt)));
        }
    } else {
        if (n_args != fi.param_types.size()) {
            error(std::format("call to '{}': expected {} args, got {}",
                  callee, fi.param_types.size(), n_args));
        } else {
            for (uint64_t i = 0; i < n_args; ++i) {
                auto* at = arg_exprs[i]->type;
                auto* pt = subst_type_sema(fi.param_types[i], subst);
                if (at->kind != LogosType::Kind::Error &&
                    pt->kind != LogosType::Kind::Error &&
                    pt->kind != LogosType::Kind::TypeVar &&
                    pt->kind != LogosType::Kind::AssocType &&
                    !types_compatible(at, pt))
                    error(std::format("call to '{}' arg {}: expected {}, got {}",
                          callee, i + 1, type_str(pt), type_str(at)));
                if (at->kind == LogosType::Kind::IntLit && pt->kind != LogosType::Kind::Error &&
                    pt->kind != LogosType::Kind::TypeVar)
                    if (auto v = get_intlit_value(arg_exprs[i].get()))
                        if (!intlit_fits(*v, pt->kind))
                            error(std::format("call to '{}' arg {}: value {} does not fit in {}",
                                  callee, i + 1, *v, type_str(pt)));
                // Check array literal elements against narrow array param type.
                if (at->kind == LogosType::Kind::Array && pt->kind == LogosType::Kind::Array && pt->elem)
                    if (auto* al = std::get_if<lir::EArrLit>(&arg_exprs[i]->kind))
                        for (size_t ei = 0; ei < al->elems.size(); ++ei)
                            if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(al->elems[ei].get()))
                                    if (!intlit_fits(*v, pt->elem->kind))
                                        error(std::format("call to '{}' arg {}: array element {}: value {} does not fit in {}",
                                              callee, i + 1, ei, *v, type_str(pt->elem)));
                // Check tuple literal elements against narrow tuple param element types.
                if (at->kind == LogosType::Kind::Tuple && pt->kind == LogosType::Kind::Tuple)
                    if (auto* tl = std::get_if<lir::ETupleLit>(&arg_exprs[i]->kind))
                        for (size_t ei = 0; ei < tl->elems.size() && ei < pt->tuple_elems.size(); ++ei) {
                            if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(tl->elems[ei].get()))
                                    if (pt->tuple_elems[ei] && !intlit_fits(*v, pt->tuple_elems[ei]->kind))
                                        error(std::format("call to '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                              callee, i + 1, ei, *v, type_str(pt->tuple_elems[ei])));
                            if (pt->tuple_elems[ei] && pt->tuple_elems[ei]->kind == LogosType::Kind::Array &&
                                pt->tuple_elems[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                                if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                        if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(ial->elems[ii].get()))
                                                if (!intlit_fits(*v, pt->tuple_elems[ei]->elem->kind))
                                                    error(std::format("call to '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                          callee, i + 1, ei, ii, *v, type_str(pt->tuple_elems[ei]->elem)));
                            if (pt->tuple_elems[ei] && pt->tuple_elems[ei]->kind == LogosType::Kind::Tuple &&
                                tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                                if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < itl->elems.size() && ii < pt->tuple_elems[ei]->tuple_elems.size(); ++ii)
                                        if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(itl->elems[ii].get()))
                                                if (pt->tuple_elems[ei]->tuple_elems[ii] && !intlit_fits(*v, pt->tuple_elems[ei]->tuple_elems[ii]->kind))
                                                    error(std::format("call to '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                          callee, i + 1, ei, ii, *v, type_str(pt->tuple_elems[ei]->tuple_elems[ii])));
                        }
            }
        }
    }

    return make_expr(ret, lir::ECall{callee, std::move(type_args), std::move(arg_exprs)});
}

lir::LExprPtr SemaChecker::lower_generic_call(TinyMapView node) {
    auto callee = str_of(node.get(la::CALLEE.code));

    // sizeof::<T>() — compiler builtin, returns i64 byte size of T.
    if (callee == "sizeof") {
        const LogosType* elem = nullptr;
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                if (items.size() == 1)
                    elem = resolve_type(map_of(items.get(0)));
            }
        }
        if (!elem) error("sizeof::<T>() requires exactly one type argument");
        return make_expr(prim(LogosType::Kind::I64), lir::ESizeOf{elem});
    }

    // Prefer the generic overload (for variadic base case overloading)
    SemaFuncInfo* fi_ptr = nullptr;
    {
        auto git = generic_funcs_.find(std::string(callee));
        if (git != generic_funcs_.end()) fi_ptr = &git->second;
        else {
            auto fit2 = funcs_.find(std::string(callee));
            if (fit2 != funcs_.end()) fi_ptr = &fit2->second;
        }
    }
    if (!fi_ptr) {
        error(std::format("call to undefined function '{}'", callee));
        return make_expr(error_t(), lir::ECall{std::string(callee), {}, {}});
    }
    check_pub_access(fi_ptr->is_pub, fi_ptr->package, callee);

    // Resolve explicit type arguments from TYPE_PARAMS
    std::vector<const LogosType*> type_args;
    if (node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav = node.get(la::TYPE_PARAMS.code);
        if (!tpav.is_null()) {
            auto tplist = map_of(tpav);
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    type_args.push_back(resolve_type(map_of(items.get(i))));
            }
        }
    }

    // Resolve value arguments
    std::vector<lir::LExprPtr> arg_exprs;
    if (node.has_key(la::ARGS)) {
        AnyVal args_av = node.get(la::ARGS.code);
        if (!args_av.is_null()) {
            auto args_list = map_of(args_av);
            if (args_list.has_key(la::ITEMS)) {
                auto items = arr_of(args_list.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    arg_exprs.push_back(lower_expr(map_of(items.get(i))));
            }
        }
    }

    return finish_generic_call(callee, *fi_ptr, std::move(type_args), std::move(arg_exprs));
}

lir::LExprPtr SemaChecker::lower_class_method_call(lir::LExprPtr recv,
                                           std::string_view cname,
                                           std::string_view method_name,
                                           TinyMapView node) {
    // Walk inheritance chain to find the method.
    std::string resolved_class;
    std::string mangled;
    SemaSubst recv_subst;
    {
        std::string start_class = std::string(cname);
        std::string cur = start_class;
        // Build subst: start_class type params → receiver's concrete type args
        {
            const LogosType* recv_t = recv->type;
            if (recv_t && is_ref_like(recv_t->kind) && recv_t->pointee)
                recv_t = recv_t->pointee;
            if (recv_t && recv_t->kind == LogosType::Kind::Class &&
                !recv_t->type_args.empty()) {
                auto cit = classes_.find(start_class);
                if (cit != classes_.end()) {
                    auto& tps = cit->second.type_params;
                    for (size_t i = 0; i < tps.size() && i < recv_t->type_args.size(); ++i)
                        recv_subst[tps[i].name] = recv_t->type_args[i];
                }
            }
        }
        while (!cur.empty()) {
            auto candidate = cur + "__" + std::string(method_name);
            if (funcs_.count(candidate) || generic_funcs_.count(candidate)) {
                // Only set resolved_class for inherited methods (found on a parent).
                // When found on the class itself, leave it empty so mlir_gen uses
                // the concrete type name (e.g., "Box__i32") from gen_recv_struct.
                if (cur != start_class) {
                    // Compute concrete parent class name using parent_type_args
                    auto sit = classes_.find(start_class);
                    if (!recv_subst.empty() && sit != classes_.end() &&
                        !sit->second.parent_type_args.empty()) {
                        // Substitute parent_type_args to get concrete parent type args
                        std::vector<const LogosType*> concrete_args;
                        for (auto* arg : sit->second.parent_type_args)
                            concrete_args.push_back(subst_type_sema(arg, recv_subst));
                        LogosType parent_t;
                        parent_t.kind = LogosType::Kind::Class;
                        parent_t.struct_name = cur;
                        parent_t.type_args = concrete_args;
                        resolved_class = concrete_class_name(&parent_t);
                    } else {
                        resolved_class = cur;
                    }
                }
                mangled = candidate;
                break;
            }
            auto cit = classes_.find(cur);
            if (cit == classes_.end()) break;
            cur = cit->second.parent_name;
        }
    }

    const SemaFuncInfo* fi_ptr = nullptr;
    if (auto fit = funcs_.find(mangled); fit != funcs_.end()) fi_ptr = &fit->second;
    else if (auto git = generic_funcs_.find(mangled); git != generic_funcs_.end()) fi_ptr = &git->second;

    if (!fi_ptr) {
        error(std::format("class '{}' has no method '{}'", cname, method_name));
        std::vector<lir::LExprPtr> dummy_args;
        return make_expr(error_t(),
            lir::EMethodCall{std::move(recv), std::string(method_name), {}, std::move(dummy_args), -1, ""});
    }

    std::vector<lir::LExprPtr> arg_exprs;
    if (node.has_key(la::ARGS)) {
        auto args = arr_of(node.get(la::ARGS.code));
        for (uint64_t i = 0; i < args.size(); ++i)
            arg_exprs.push_back(lower_expr(map_of(args.get(i))));
    }

    auto& fi = *fi_ptr;
    check_pub_access(fi.is_pub, fi.package, mangled);
    if (fi.is_unsafe && !inside_unsafe_)
        error(std::format("call to unsafe method '{}' requires unsafe context", mangled));

    uint64_t explicit_args = arg_exprs.size();
    size_t expected_explicit = fi.param_types.size() > 0 ? fi.param_types.size() - 1 : 0;
    if (explicit_args != expected_explicit)
        error(std::format("method call '{}': expected {} args, got {}",
              mangled, expected_explicit, explicit_args));
    else {
        for (uint64_t i = 0; i < explicit_args; ++i) {
            auto* at = arg_exprs[i]->type;
            size_t pi = i + 1;
            if (pi < fi.param_types.size()) {
                auto* pt = recv_subst.empty() ? fi.param_types[pi]
                                              : subst_type_sema(fi.param_types[pi], recv_subst);
                if (at->kind != LogosType::Kind::Error && pt->kind != LogosType::Kind::Error &&
                    !compat(at, pt))
                    error(std::format("method '{}' arg {}: expected {}, got {}",
                          mangled, i + 1, type_str(pt), type_str(at)));
                if (at->kind == LogosType::Kind::IntLit && pt->kind != LogosType::Kind::Error)
                    if (auto v = get_intlit_value(arg_exprs[i].get()))
                        if (!intlit_fits(*v, pt->kind))
                            error(std::format("method '{}' arg {}: value {} does not fit in {}",
                                  mangled, i + 1, *v, type_str(pt)));
                // Check array literal elements against narrow array param type.
                if (at->kind == LogosType::Kind::Array && pt->kind == LogosType::Kind::Array && pt->elem)
                    if (auto* al = std::get_if<lir::EArrLit>(&arg_exprs[i]->kind))
                        for (size_t ei = 0; ei < al->elems.size(); ++ei)
                            if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(al->elems[ei].get()))
                                    if (!intlit_fits(*v, pt->elem->kind))
                                        error(std::format("method '{}' arg {}: array element {}: value {} does not fit in {}",
                                              mangled, i + 1, ei, *v, type_str(pt->elem)));
                // Check tuple literal elements against narrow tuple param element types.
                if (at->kind == LogosType::Kind::Tuple && pt->kind == LogosType::Kind::Tuple)
                    if (auto* tl = std::get_if<lir::ETupleLit>(&arg_exprs[i]->kind))
                        for (size_t ei = 0; ei < tl->elems.size() && ei < pt->tuple_elems.size(); ++ei) {
                            if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(tl->elems[ei].get()))
                                    if (pt->tuple_elems[ei] && !intlit_fits(*v, pt->tuple_elems[ei]->kind))
                                        error(std::format("method '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                              mangled, i + 1, ei, *v, type_str(pt->tuple_elems[ei])));
                            if (pt->tuple_elems[ei] && pt->tuple_elems[ei]->kind == LogosType::Kind::Array &&
                                pt->tuple_elems[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                                if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                        if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(ial->elems[ii].get()))
                                                if (!intlit_fits(*v, pt->tuple_elems[ei]->elem->kind))
                                                    error(std::format("method '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                          mangled, i + 1, ei, ii, *v, type_str(pt->tuple_elems[ei]->elem)));

                            if (pt->tuple_elems[ei] && pt->tuple_elems[ei]->kind == LogosType::Kind::Tuple &&
                                tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                                if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < itl->elems.size() && ii < pt->tuple_elems[ei]->tuple_elems.size(); ++ii)
                                        if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(itl->elems[ii].get()))
                                                if (pt->tuple_elems[ei]->tuple_elems[ii] && !intlit_fits(*v, pt->tuple_elems[ei]->tuple_elems[ii]->kind))
                                                    error(std::format("method '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                          mangled, i + 1, ei, ii, *v, type_str(pt->tuple_elems[ei]->tuple_elems[ii])));
                            }
            }
        }
    }

    const LogosType* ret_t = recv_subst.empty() ? fi.ret_type
                                                  : subst_type_sema(fi.ret_type, recv_subst);

    int32_t vidx = vtable_index_of(cname, mangled);

    // Infer method type args if generic (Bug 12)
    std::vector<const LogosType*> m_type_args;
    if (!fi.type_params.empty()) {
        if (!infer_type_args(fi, arg_exprs, m_type_args, recv_subst, 1)) {
            error(std::format("could not infer type arguments for generic method '{}'", mangled));
        }
        check_type_bounds(mangled, fi.type_params, m_type_args);
        // Re-substitute return type with BOTH struct and method bindings
        SemaSubst combined = recv_subst;
        for (size_t i = 0; i < fi.type_params.size() && i < m_type_args.size(); ++i)
            combined[fi.type_params[i].name] = m_type_args[i];
        ret_t = subst_type_sema(fi.ret_type, combined);
    }

    lir::EMethodCall mc;
    mc.receiver      = std::move(recv);
    mc.method        = std::string(method_name);
    mc.type_args     = std::move(m_type_args);
    mc.args          = std::move(arg_exprs);
    mc.vtable_index  = vidx;
    mc.resolved_type = resolved_class;
    return make_expr(ret_t, std::move(mc));
}

lir::LExprPtr SemaChecker::lower_method_call(TinyMapView node) {
    auto method_name = str_of(node.get(la::NAME.code));
    auto recv = lower_expr(map_of(node.get(la::RECEIVER.code)));

    // Slice built-in methods: .len()
    if (recv->type->kind == LogosType::Kind::Slice) {
        if (method_name == "len") {
            return make_expr(prim(LogosType::Kind::I64),
                lir::ESliceLen{std::move(recv)});
        }
        error(std::format("slice has no method '{}'", method_name));
        return error_expr();
    }

    // &dyn Trait method call: look up trait method, emit EMethodCall with vtable dispatch.
    if (recv->type->kind == LogosType::Kind::TraitObject) {
        auto& tname = recv->type->trait_name;
        auto tit = traits_.find(tname);
        if (tit != traits_.end()) {
            for (size_t mi = 0; mi < tit->second.methods.size(); ++mi) {
                auto& m = tit->second.methods[mi];
                if (m.name == method_name) {
                    if (m.is_unsafe && !inside_unsafe_)
                        error(std::format("call to unsafe method '{}' requires unsafe context",
                                          std::string(method_name)));
                    std::vector<lir::LExprPtr> arg_exprs;
                    if (node.has_key(la::ARGS)) {
                        auto args = arr_of(node.get(la::ARGS.code));
                        for (uint64_t i = 0; i < args.size(); ++i)
                            arg_exprs.push_back(lower_expr(map_of(args.get(i))));
                    }
                    uint64_t explicit_args = arg_exprs.size();
                    size_t expected_explicit = m.param_types.size() > 0
                        ? m.param_types.size() - 1 : 0;
                    if (explicit_args != expected_explicit) {
                        error(std::format("method call '{}': expected {} args, got {}",
                                          std::string(method_name), expected_explicit, explicit_args));
                    } else {
                        SemaSubst self_subst;
                        self_subst["Self"] = recv->type;
                        for (uint64_t i = 0; i < explicit_args; ++i) {
                            auto* at = arg_exprs[i]->type;
                            auto* pt = subst_type_sema(m.param_types[i + 1], self_subst);
                            if (at->kind != LogosType::Kind::Error &&
                                pt->kind != LogosType::Kind::Error &&
                                pt->kind != LogosType::Kind::TypeVar &&
                                pt->kind != LogosType::Kind::AssocType &&
                                !types_compatible(at, pt))
                                error(std::format("method '{}' arg {}: expected {}, got {}",
                                                  std::string(method_name), i + 1,
                                                  type_str(pt), type_str(at)));
                            if (at->kind == LogosType::Kind::IntLit &&
                                pt->kind != LogosType::Kind::Error &&
                                pt->kind != LogosType::Kind::TypeVar)
                                if (auto v = get_intlit_value(arg_exprs[i].get()))
                                    if (!intlit_fits(*v, pt->kind))
                                        error(std::format("method '{}' arg {}: value {} does not fit in {}",
                                                          std::string(method_name), i + 1, *v, type_str(pt)));
                            // Check array literal elements against narrow array param type.
                            if (at->kind == LogosType::Kind::Array && pt->kind == LogosType::Kind::Array && pt->elem)
                                if (auto* al = std::get_if<lir::EArrLit>(&arg_exprs[i]->kind))
                                    for (size_t ei = 0; ei < al->elems.size(); ++ei)
                                        if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(al->elems[ei].get()))
                                                if (!intlit_fits(*v, pt->elem->kind))
                                                    error(std::format("method '{}' arg {}: array element {}: value {} does not fit in {}",
                                                                      std::string(method_name), i + 1, ei, *v, type_str(pt->elem)));
                            // Check tuple literal elements against narrow tuple param element types.
                            if (at->kind == LogosType::Kind::Tuple && pt->kind == LogosType::Kind::Tuple)
                                if (auto* tl = std::get_if<lir::ETupleLit>(&arg_exprs[i]->kind))
                                    for (size_t ei = 0; ei < tl->elems.size() && ei < pt->tuple_elems.size(); ++ei) {
                                        if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(tl->elems[ei].get()))
                                                if (pt->tuple_elems[ei] && !intlit_fits(*v, pt->tuple_elems[ei]->kind))
                                                    error(std::format("method '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                                                      std::string(method_name), i + 1, ei, *v, type_str(pt->tuple_elems[ei])));
                                        if (pt->tuple_elems[ei] && pt->tuple_elems[ei]->kind == LogosType::Kind::Array &&
                                            pt->tuple_elems[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                                            if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                                for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                                    if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                                        if (auto v = get_intlit_value(ial->elems[ii].get()))
                                                            if (!intlit_fits(*v, pt->tuple_elems[ei]->elem->kind))
                                                                error(std::format("method '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                                      std::string(method_name), i + 1, ei, ii, *v, type_str(pt->tuple_elems[ei]->elem)));

                                        if (pt->tuple_elems[ei] && pt->tuple_elems[ei]->kind == LogosType::Kind::Tuple &&
                                            tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                                            if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                                for (size_t ii = 0; ii < itl->elems.size() && ii < pt->tuple_elems[ei]->tuple_elems.size(); ++ii)
                                                    if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                                        if (auto v = get_intlit_value(itl->elems[ii].get()))
                                                            if (pt->tuple_elems[ei]->tuple_elems[ii] && !intlit_fits(*v, pt->tuple_elems[ei]->tuple_elems[ii]->kind))
                                                                error(std::format("method '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                                      std::string(method_name), i + 1, ei, ii, *v, type_str(pt->tuple_elems[ei]->tuple_elems[ii])));
                                        }
                        }
                    }
                    // Return type: substitute Self → &dyn Trait
                    auto* ret_type = m.ret_type;
                    if (ret_type && ret_type->kind == LogosType::Kind::TypeVar &&
                        ret_type->type_var_name == "Self")
                        ret_type = recv->type;
                    lir::EMethodCall mc;
                    mc.receiver     = std::move(recv);
                    mc.method       = std::string(method_name);
                    mc.type_args    = {}; // No type args for trait object calls for now
                    mc.args         = std::move(arg_exprs);
                    mc.vtable_index = (int32_t)mi;  // slot in vtable
                    mc.resolved_type = "";
                    return make_expr(ret_type, std::move(mc));
                }
            }
        }
        error(std::format("trait '{}' has no method '{}'", tname, method_name));
        return error_expr();
    }

    // TypeVar with trait bounds: look up trait method signature.
    // The actual impl method will be resolved during monomorphization.
    // Handle both T and *mut T / *const T / &T / &mut T receivers.
    const LogosType* recv_inner = recv->type;
    if (recv_inner && recv_inner->kind == LogosType::Kind::Ptr) {
        if (!inside_unsafe_)
            error("method call through raw pointer requires unsafe context");
        recv_inner = recv_inner->pointee;
    } else if (recv_inner && is_ref_like(recv_inner->kind) && recv_inner->pointee) {
        recv_inner = recv_inner->pointee;
    }
    if (recv_inner->kind == LogosType::Kind::TypeVar) {
        std::vector<lir::LExprPtr> arg_exprs;
        if (node.has_key(la::ARGS)) {
            auto args = arr_of(node.get(la::ARGS.code));
            for (uint64_t i = 0; i < args.size(); ++i)
                arg_exprs.push_back(lower_expr(map_of(args.get(i))));
        }

        auto bit = current_type_bounds_.find(recv_inner->type_var_name);
        const SemaTraitMethodInfo* chosen_method = nullptr;
        std::string chosen_trait;
        if (bit != current_type_bounds_.end()) {
            for (auto& bound : bit->second) {
            auto tit = traits_.find(bound.trait_name);
            if (tit == traits_.end()) continue;
            for (auto& m : tit->second.methods) {
                if (m.name != method_name) continue;
                if (chosen_method && chosen_trait != bound.trait_name) {
                    error(std::format(
                        "method '{}' is ambiguous for type parameter '{}' (matches traits '{}' and '{}')",
                        std::string(method_name), recv_inner->type_var_name, chosen_trait, bound.trait_name));
                }
                chosen_method = &m;
                chosen_trait = bound.trait_name;
            }
        }
        }

        if (chosen_method) {
            if (chosen_method->is_unsafe && !inside_unsafe_)
                error(std::format("call to unsafe method '{}' requires unsafe context",
                                  std::string(method_name)));

            size_t expected_explicit = chosen_method->param_types.size() > 0
                ? chosen_method->param_types.size() - 1 : 0;
            if (arg_exprs.size() != expected_explicit) {
                error(std::format("method call '{}': expected {} args, got {}",
                                  std::string(method_name), expected_explicit, arg_exprs.size()));
            } else {
                SemaSubst self_subst;
                self_subst["Self"] = recv_inner;
                for (uint64_t i = 0; i < arg_exprs.size(); ++i) {
                    auto* at = arg_exprs[i]->type;
                    auto* pt = subst_type_sema(chosen_method->param_types[i + 1], self_subst);
                    if (at->kind != LogosType::Kind::Error &&
                        pt->kind != LogosType::Kind::Error &&
                        pt->kind != LogosType::Kind::TypeVar &&
                        pt->kind != LogosType::Kind::AssocType &&
                        !types_compatible(at, pt))
                        error(std::format("method '{}' arg {}: expected {}, got {}",
                                          std::string(method_name), i + 1,
                                          type_str(pt), type_str(at)));
                    if (at->kind == LogosType::Kind::IntLit &&
                        pt->kind != LogosType::Kind::Error &&
                        pt->kind != LogosType::Kind::TypeVar &&
                        pt->kind != LogosType::Kind::AssocType)
                        if (auto v = get_intlit_value(arg_exprs[i].get()))
                            if (!intlit_fits(*v, pt->kind))
                                error(std::format("method '{}' arg {}: value {} does not fit in {}",
                                                  std::string(method_name), i + 1,
                                                  *v, type_str(pt)));
                    // Check array literal elements against narrow array param type.
                    if (at->kind == LogosType::Kind::Array && pt->kind == LogosType::Kind::Array && pt->elem)
                        if (auto* al = std::get_if<lir::EArrLit>(&arg_exprs[i]->kind))
                            for (size_t ei = 0; ei < al->elems.size(); ++ei)
                                if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(al->elems[ei].get()))
                                        if (!intlit_fits(*v, pt->elem->kind))
                                            error(std::format("method '{}' arg {}: array element {}: value {} does not fit in {}",
                                                              std::string(method_name), i + 1, ei, *v, type_str(pt->elem)));
                    // Check tuple literal elements against narrow tuple param element types.
                    if (at->kind == LogosType::Kind::Tuple && pt->kind == LogosType::Kind::Tuple)
                        if (auto* tl = std::get_if<lir::ETupleLit>(&arg_exprs[i]->kind))
                            for (size_t ei = 0; ei < tl->elems.size() && ei < pt->tuple_elems.size(); ++ei) {
                                if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(tl->elems[ei].get()))
                                        if (pt->tuple_elems[ei] && !intlit_fits(*v, pt->tuple_elems[ei]->kind))
                                            error(std::format("method '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                                              std::string(method_name), i + 1, ei, *v, type_str(pt->tuple_elems[ei])));
                                if (pt->tuple_elems[ei] && pt->tuple_elems[ei]->kind == LogosType::Kind::Array &&
                                    pt->tuple_elems[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                                    if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                        for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                            if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                                if (auto v = get_intlit_value(ial->elems[ii].get()))
                                                    if (!intlit_fits(*v, pt->tuple_elems[ei]->elem->kind))
                                                        error(std::format("method '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                              std::string(method_name), i + 1, ei, ii, *v, type_str(pt->tuple_elems[ei]->elem)));

                                if (pt->tuple_elems[ei] && pt->tuple_elems[ei]->kind == LogosType::Kind::Tuple &&
                                    tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                                    if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                        for (size_t ii = 0; ii < itl->elems.size() && ii < pt->tuple_elems[ei]->tuple_elems.size(); ++ii)
                                            if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                                if (auto v = get_intlit_value(itl->elems[ii].get()))
                                                    if (pt->tuple_elems[ei]->tuple_elems[ii] && !intlit_fits(*v, pt->tuple_elems[ei]->tuple_elems[ii]->kind))
                                                        error(std::format("method '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                              std::string(method_name), i + 1, ei, ii, *v, type_str(pt->tuple_elems[ei]->tuple_elems[ii])));
                                }
                }
            }

            SemaSubst self_subst;
            self_subst["Self"] = recv_inner;
            const LogosType* ret_type = subst_type_sema(chosen_method->ret_type, self_subst);

            // Use EMethodCall — mono will resolve to concrete impl.
            lir::EMethodCall mc;
            mc.receiver = std::move(recv);
            mc.method   = std::string(method_name);
            mc.type_args = {};
            mc.args     = std::move(arg_exprs);
            mc.vtable_index = -1;
            mc.resolved_type = "";
            return make_expr(ret_type, std::move(mc));
        }

        error(std::format("type parameter '{}' has no trait bound providing method '{}'",
                          recv_inner->type_var_name, std::string(method_name)));
        return make_expr(error_t(),
            lir::EMethodCall{std::move(recv), std::string(method_name), {}, std::move(arg_exprs), -1, ""});
    }

    // Check if receiver is a class type → virtual dispatch
    auto cname = class_name_from_type(recv->type);
    if (!cname.empty()) {
        return lower_class_method_call(std::move(recv), cname, method_name, node);
    }

    auto sname = struct_name_from_type(recv->type);

    std::vector<lir::LExprPtr> arg_exprs;
    if (node.has_key(la::ARGS)) {
        auto args = arr_of(node.get(la::ARGS.code));
        for (uint64_t i = 0; i < args.size(); ++i)
            arg_exprs.push_back(lower_expr(map_of(args.get(i))));
    }

    // For primitive types, non-generic enums, etc.: try TypeName__method
    if (sname.empty()) {
        // For generic enums (Enum<T> with type_args): instantiate method with concrete types.
        // type_str returns just the base name (e.g. "Option" for Option<i32>), so we must
        // handle this BEFORE the generic lookup to avoid calling the uninstantiated template.
        if (recv->type->kind == LogosType::Kind::Enum &&
            !recv->type->type_args.empty()) {
            const std::string& base = recv->type->enum_name;
            auto generic_key = base + "__" + std::string(method_name);
            const SemaFuncInfo* fi_ptr = nullptr;
            if (auto git = funcs_.find(generic_key); git != funcs_.end()) fi_ptr = &git->second;
            else if (auto git = generic_funcs_.find(generic_key); git != generic_funcs_.end()) fi_ptr = &git->second;

            if (fi_ptr && !fi_ptr->type_params.empty()) {
                if (fi_ptr->is_unsafe && !inside_unsafe_)
                    error(std::format("call to unsafe method '{}' requires unsafe context",
                                      generic_key));
                // Build concrete name from enum base + type args + method
                // e.g. "Option__i32__unwrap_or"
                SemaSubst subst;
                auto eit = enums_.find(base);
                if (eit != enums_.end()) {
                    auto& tps = eit->second.type_params;
                    for (size_t i = 0; i < tps.size() && i < recv->type->type_args.size(); ++i)
                        subst[tps[i].name] = recv->type->type_args[i];
                }
                const LogosType* ret = subst_type_sema(fi_ptr->ret_type, subst);
                // Mangle: "Option__i32" is the concrete enum name
                std::string concrete_enum = base;
                for (auto* ta : recv->type->type_args) {
                    concrete_enum += "__";
                    concrete_enum += type_str(ta);
                }
                std::string concrete_mangled = concrete_enum + "__" + std::string(method_name);
                std::vector<lir::LExprPtr> pargs;
                pargs.push_back(std::move(recv));
                for (auto& a : arg_exprs) pargs.push_back(std::move(a));
                return make_expr(ret, lir::ECall{concrete_mangled, {}, std::move(pargs)});
            }
        }
        auto tname = type_str(recv->type);
        auto mangled_prim = tname + "__" + std::string(method_name);
        const SemaFuncInfo* fi_ptr = nullptr;
        if (auto pfit = funcs_.find(mangled_prim); pfit != funcs_.end()) fi_ptr = &pfit->second;
        else if (auto pfit = generic_funcs_.find(mangled_prim); pfit != generic_funcs_.end()) fi_ptr = &pfit->second;

        if (fi_ptr) {
            if (fi_ptr->is_unsafe && !inside_unsafe_)
                error(std::format("call to unsafe method '{}' requires unsafe context", mangled_prim));
            std::vector<lir::LExprPtr> pargs;
            pargs.push_back(std::move(recv));
            for (auto& a : arg_exprs) pargs.push_back(std::move(a));
            return make_expr(fi_ptr->ret_type,
                lir::ECall{mangled_prim, {}, std::move(pargs)});
        }
        error(std::format("method call: receiver is not a struct (got {})",
              type_str(recv->type)));
        return make_expr(error_t(),
            lir::EMethodCall{std::move(recv), std::string(method_name), {}, std::move(arg_exprs), -1, ""});
    }

    auto mangled = std::string(sname) + "__" + std::string(method_name);
    const SemaFuncInfo* fi_ptr = nullptr;
    if (auto fit = funcs_.find(mangled); fit != funcs_.end()) fi_ptr = &fit->second;
    else if (auto fit = generic_funcs_.find(mangled); fit != generic_funcs_.end()) fi_ptr = &fit->second;

    // Fallback: for generic structs (Foo$G1$i32), methods may be registered under base name (Foo).
    if (!fi_ptr) {
        std::string base_sname(sname);
        if (auto d = base_sname.find('$'); d != std::string::npos)
            base_sname = base_sname.substr(0, d);
        if (base_sname != sname) {
            auto base_mangled = base_sname + "__" + std::string(method_name);
            if (auto fit = funcs_.find(base_mangled); fit != funcs_.end()) {
                fi_ptr = &fit->second;
                mangled = base_mangled;
            } else if (auto fit = generic_funcs_.find(base_mangled); fit != generic_funcs_.end()) {
                fi_ptr = &fit->second;
                mangled = base_mangled;
            }
        }
    }

    if (!fi_ptr) {
        error(std::format("method call: '{}' has no method '{}'", sname, method_name));
        return make_expr(error_t(),
            lir::EMethodCall{std::move(recv), std::string(method_name), {}, std::move(arg_exprs), -1, ""});
    }

    auto& fi = *fi_ptr;
    check_pub_access(fi.is_pub, fi.package, mangled);
    if (fi.is_unsafe && !inside_unsafe_)
        error(std::format("call to unsafe method '{}' requires unsafe context", mangled));

    // Build TypeVar→concrete substitution from the receiver's struct type args.
    // This lets us check e.g. Vec<i32>::push(val: T) with T resolved to i32.
    SemaSubst struct_subst;
    {
        const LogosType* rst = recv->type;
        if (rst && rst->kind == LogosType::Kind::Ptr) {
            if (!inside_unsafe_)
                error("method call through raw pointer requires unsafe context");
            if (rst->pointee) rst = rst->pointee;
        } else if (rst && is_ref_like(rst->kind) && rst->pointee) {
            rst = rst->pointee;
        }
        if (rst->kind == LogosType::Kind::Struct && !rst->type_args.empty()) {
            auto sit2 = structs_.find(rst->struct_name);
            if (sit2 != structs_.end()) {
                auto& tps = sit2->second.type_params;
                for (size_t i = 0; i < tps.size() && i < rst->type_args.size(); ++i)
                    struct_subst[tps[i].name] = rst->type_args[i];
            }
        }
    }

    uint64_t explicit_args = arg_exprs.size();
    size_t expected_explicit = fi.param_types.size() > 0 ? fi.param_types.size() - 1 : 0;
    if (explicit_args != expected_explicit)
        error(std::format("method call '{}': expected {} args, got {}",
              mangled, expected_explicit, explicit_args));
    else {
        for (uint64_t i = 0; i < explicit_args; ++i) {
            auto* at = arg_exprs[i]->type;
            size_t pi = i + 1;
            if (pi < fi.param_types.size()) {
                auto* pt = fi.param_types[pi];
                if (!struct_subst.empty()) pt = subst_type_sema(pt, struct_subst);
                if (at->kind != LogosType::Kind::Error && pt->kind != LogosType::Kind::Error &&
                    !types_compatible(at, pt))
                    error(std::format("method '{}' arg {}: expected {}, got {}",
                          mangled, i + 1, type_str(pt), type_str(at)));
                if (at->kind == LogosType::Kind::IntLit && pt->kind != LogosType::Kind::Error)
                    if (auto v = get_intlit_value(arg_exprs[i].get()))
                        if (!intlit_fits(*v, pt->kind))
                            error(std::format("method '{}' arg {}: value {} does not fit in {}",
                                  mangled, i + 1, *v, type_str(pt)));
                // Check array literal elements against narrow array param type.
                if (at->kind == LogosType::Kind::Array && pt->kind == LogosType::Kind::Array && pt->elem)
                    if (auto* al = std::get_if<lir::EArrLit>(&arg_exprs[i]->kind))
                        for (size_t ei = 0; ei < al->elems.size(); ++ei)
                            if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(al->elems[ei].get()))
                                    if (!intlit_fits(*v, pt->elem->kind))
                                        error(std::format("method '{}' arg {}: array element {}: value {} does not fit in {}",
                                              mangled, i + 1, ei, *v, type_str(pt->elem)));
                // Check tuple literal elements against narrow tuple param element types.
                if (at->kind == LogosType::Kind::Tuple && pt->kind == LogosType::Kind::Tuple)
                    if (auto* tl = std::get_if<lir::ETupleLit>(&arg_exprs[i]->kind))
                        for (size_t ei = 0; ei < tl->elems.size() && ei < pt->tuple_elems.size(); ++ei) {
                            if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(tl->elems[ei].get()))
                                    if (pt->tuple_elems[ei] && !intlit_fits(*v, pt->tuple_elems[ei]->kind))
                                        error(std::format("method '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                              mangled, i + 1, ei, *v, type_str(pt->tuple_elems[ei])));
                            if (pt->tuple_elems[ei] && pt->tuple_elems[ei]->kind == LogosType::Kind::Array &&
                                pt->tuple_elems[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                                if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                        if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(ial->elems[ii].get()))
                                                if (!intlit_fits(*v, pt->tuple_elems[ei]->elem->kind))
                                                    error(std::format("method '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                          mangled, i + 1, ei, ii, *v, type_str(pt->tuple_elems[ei]->elem)));

                            if (pt->tuple_elems[ei] && pt->tuple_elems[ei]->kind == LogosType::Kind::Tuple &&
                                tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                                if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < itl->elems.size() && ii < pt->tuple_elems[ei]->tuple_elems.size(); ++ii)
                                        if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(itl->elems[ii].get()))
                                                if (pt->tuple_elems[ei]->tuple_elems[ii] && !intlit_fits(*v, pt->tuple_elems[ei]->tuple_elems[ii]->kind))
                                                    error(std::format("method '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                          mangled, i + 1, ei, ii, *v, type_str(pt->tuple_elems[ei]->tuple_elems[ii])));
                            }
            }
        }
    }

    // Infer method type args if generic (Bug 12)
    std::vector<const LogosType*> m_type_args;
    if (!fi.type_params.empty()) {
        if (!infer_type_args(fi, arg_exprs, m_type_args, struct_subst, 1)) {
            error(std::format("could not infer type arguments for generic method '{}'", mangled));
        }
        check_type_bounds(mangled, fi.type_params, m_type_args);
        // Merge method bindings into struct_subst for return type substitution
        for (size_t i = 0; i < fi.type_params.size() && i < m_type_args.size(); ++i)
            struct_subst[fi.type_params[i].name] = m_type_args[i];
    }

    // Substitute TypeVars in return type using the combined substitution.
    const LogosType* ret = struct_subst.empty()
        ? fi.ret_type : subst_type_sema(fi.ret_type, struct_subst);

    lir::EMethodCall mc;
    mc.receiver     = std::move(recv);
    mc.method       = std::string(method_name);
    mc.type_args    = std::move(m_type_args);
    mc.args         = std::move(arg_exprs);
    mc.vtable_index = -1;
    mc.resolved_type = "";
    return make_expr(ret, std::move(mc));
}

lir::LExprPtr SemaChecker::lower_field_read(TinyMapView node) {
    auto field_name = str_of(node.get(la::FIELD.code));
    auto recv = lower_expr(map_of(node.get(la::RECEIVER.code)));
    const LogosType* recv_base_t = recv->type;
    if (recv_base_t && recv_base_t->kind == LogosType::Kind::Ptr) {
        if (!inside_unsafe_)
            error("field read through raw pointer requires unsafe context");
        recv_base_t = recv_base_t->pointee;
    } else if (recv_base_t && is_ref_like(recv_base_t->kind)) {
        recv_base_t = recv_base_t->pointee;
    }

    // Check for class receiver
    auto cname_sv = class_name_from_type(recv_base_t);
    if (!cname_sv.empty()) {
        auto* ft = class_field_type(cname_sv, field_name);
        if (!ft) {
            error(std::format("field read: class '{}' has no field '{}'", cname_sv, field_name));
            return make_expr(error_t(), lir::EFieldRead{std::move(recv), std::string(field_name)});
        }
        // Pub check for class field reads.
        {
            auto cit = classes_.find(std::string(cname_sv));
            if (cit != classes_.end()) {
                for (auto& f : cit->second.all_fields) {
                    if (f.name == field_name) {
                        check_pub_access(f.is_pub, cit->second.package, field_name);
                        break;
                    }
                }
            }
        }
        return make_expr(ft, lir::EFieldRead{std::move(recv), std::string(field_name)});
    }

    auto sname = struct_name_from_type(recv_base_t);
    if (sname.empty()) {
        error(std::format("field read: receiver is not a struct or class (got {})",
              type_str(recv->type)));
        return make_expr(error_t(), lir::EFieldRead{std::move(recv), std::string(field_name)});
    }
    // Resolve the actual struct type (receiver may be a pointer/reference to a struct).
    const LogosType* recv_struct_t = recv_base_t;
    auto* ft = field_type_of_for_type(recv_struct_t, field_name);
    if (!ft) {
        error(std::format("field read: struct '{}' has no field '{}'", sname, field_name));
        return make_expr(error_t(), lir::EFieldRead{std::move(recv), std::string(field_name)});
    }
    // Pub check: private fields are accessible only within the defining package.
    {
        auto sit = structs_.find(std::string(sname));
        if (sit != structs_.end()) {
            for (auto& f : sit->second.fields) {
                if (f.name == field_name || (f.is_variadic && field_name.starts_with(f.name) && field_name.size() > f.name.size() + 1 && field_name[f.name.size()] == '_')) {
                    check_pub_access(f.is_pub, sit->second.package, field_name);
                    break;
                }
            }
        }
        // Specs: check against their own SemaStructInfo (which also stores package).
        else {
            auto spec_it = struct_specs_sema_.find(std::string(sname));
            if (spec_it != struct_specs_sema_.end()) {
                for (auto& f : spec_it->second.fields) {
                    if (f.name == field_name || (f.is_variadic && field_name.starts_with(f.name) && field_name.size() > f.name.size() + 1 && field_name[f.name.size()] == '_')) {
                        check_pub_access(f.is_pub, spec_it->second.package, field_name);
                        break;
                    }
                }
            }
        }
    }
    return make_expr(ft, lir::EFieldRead{std::move(recv), std::string(field_name)});
}

lir::LExprPtr SemaChecker::lower_struct_lit(TinyMapView node) {
    auto sname_sv = str_of(node.get(la::NAME.code));
    std::string sname_buf(sname_sv);  // mutable copy in case we resolve alias
    auto sit = structs_.find(sname_buf);
    if (sit == structs_.end()) {
        // Try resolving via type alias: `type Alias = Struct` or `type Alias = Struct<T>`
        auto ait = type_aliases_.find(sname_buf);
        if (ait != type_aliases_.end()) {
            auto* aliased = ait->second;
            if (aliased && aliased->kind == LogosType::Kind::Struct) {
                sit = structs_.find(aliased->struct_name);
                if (sit != structs_.end()) {
                    sname_buf = aliased->struct_name;  // update name to actual struct
                    // Also push the alias type as hint so generic args get inferred correctly
                    hint_struct_type_ = aliased;
                }
            }
        }
    }
    std::string_view sname = sname_buf;  // use resolved name throughout
    if (sit == structs_.end()) {
        error(std::format("struct literal: unknown struct '{}'", sname));
        return error_expr();
    }
    auto& sinfo = sit->second;

    // Pub check: struct with private fields can only be constructed within its package.
    if (sinfo.package != cur_package_ && !sinfo.package.empty() && !cur_package_.empty()) {
        for (auto& f : sinfo.fields) {
            if (!f.is_pub) {
                error(std::format("cannot construct '{}' from package '{}': field '{}' is private",
                      sname, cur_package_, f.name));
                break;
            }
        }
    }

    // Lower all field values first (without validation), collecting names and types.
    std::vector<std::pair<std::string, lir::LExprPtr>> fields;
    if (node.has_key(la::ITEMS)) {
        auto inits = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < inits.size(); ++i) {
            auto init = map_of(inits.get(i));
            // Skip non-FIELD_INIT items (e.g. type_arg_list node from Struct::<T>{} syntax)
            if (code_of(init) != la::FIELD_INIT) continue;
            auto fname = str_of(init.get(la::NAME.code));
            lir::LExprPtr val = init.has_key(la::VALUE)
                ? lower_expr(map_of(init.get(la::VALUE.code)))
                : error_expr();
            fields.push_back({std::string(fname), std::move(val)});
        }
    }

    // For generic structs: infer type args and resolve to spec or template.
    if (!sinfo.type_params.empty()) {
        // Helper: get the hint type for a TypeVar from hint_struct_type_ annotation.
        auto hint_for_tv = [&](const std::string& tv_name) -> const LogosType* {
            if (!hint_struct_type_ || hint_struct_type_->struct_name != std::string(sname))
                return nullptr;
            for (size_t i = 0; i < sinfo.type_params.size(); ++i)
                if (sinfo.type_params[i].name == tv_name &&
                    i < hint_struct_type_->type_args.size())
                    return hint_struct_type_->type_args[i];
            return nullptr;
        };

        // Infer type args from field values against the generic template.
        SemaSubst inferred;

        // If explicit type args were supplied (Struct::<T1, T2> { ... }), seed inferred from them.
        if (node.has_key(la::TYPE_PARAMS)) {
            AnyVal tpav = node.get(la::TYPE_PARAMS.code);
            if (!tpav.is_null()) {
                auto tplist = map_of(tpav);
                if (tplist.has_key(la::ITEMS)) {
                    auto items = arr_of(tplist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size() && i < sinfo.type_params.size(); ++i) {
                        auto* resolved = resolve_type(map_of(items.get(i)));
                        if (resolved && resolved->kind != LogosType::Kind::Error)
                            inferred[sinfo.type_params[i].name] = resolved;
                    }
                }
            }
        }

        for (auto& [fname, fval] : fields) {
            auto* raw_ft = field_type_of(std::string(sname), fname);
            if (!raw_ft) continue;
            if (raw_ft->kind == LogosType::Kind::TypeVar) {
                auto& tv = raw_ft->type_var_name;
                if (!inferred.count(tv)) {
                    auto* vt = fval->type;
                    if (vt->kind == LogosType::Kind::IntLit) {
                        auto* h = hint_for_tv(tv);
                        vt = (h && h->kind != LogosType::Kind::Error) ? h : i32_t();
                    }
                    inferred[tv] = vt;
                }
            } else if (raw_ft->kind == LogosType::Kind::Array && raw_ft->elem &&
                       raw_ft->elem->kind == LogosType::Kind::TypeVar) {
                // [T; N] field — infer T from element type of the value.
                auto& tv = raw_ft->elem->type_var_name;
                if (!inferred.count(tv) && fval->type->kind == LogosType::Kind::Array &&
                    fval->type->elem) {
                    auto* vt = fval->type->elem;
                    if (vt->kind == LogosType::Kind::IntLit) {
                        auto* h = hint_for_tv(tv);
                        vt = (h && h->kind != LogosType::Kind::Error) ? h : i32_t();
                    }
                    inferred[tv] = vt;
                }
            } else if ((raw_ft->kind == LogosType::Kind::Ptr ||
                        raw_ft->kind == LogosType::Kind::Ref ||
                        raw_ft->kind == LogosType::Kind::MutRef) && raw_ft->pointee &&
                       raw_ft->pointee->kind == LogosType::Kind::TypeVar) {
                // *T / &T / &mut T field — infer T from the value's pointee type.
                auto& tv = raw_ft->pointee->type_var_name;
                if (!inferred.count(tv) && is_ref_like(fval->type->kind) &&
                    fval->type->pointee) {
                    auto* vt = fval->type->pointee;
                    if (vt->kind != LogosType::Kind::Error)
                        inferred[tv] = vt;
                }
            }
        }
        // For any TypeVar still not inferred from fields, fall back to hint.
        for (auto& tp : sinfo.type_params) {
            if (!inferred.count(tp.name)) {
                auto* h = hint_for_tv(tp.name);
                if (h && h->kind != LogosType::Kind::Error)
                    inferred[tp.name] = h;
            }
        }
        std::vector<const LogosType*> args;
        for (size_t i = 0, h_idx = 0; i < sinfo.type_params.size(); ++i) {
            auto& tp = sinfo.type_params[i];
            if (tp.is_variadic) {
                if (hint_struct_type_ && hint_struct_type_->struct_name == std::string(sname)) {
                    while (h_idx < hint_struct_type_->type_args.size())
                        args.push_back(hint_struct_type_->type_args[h_idx++]);
                } else {
                    auto it = inferred.find(tp.name);
                    args.push_back(it != inferred.end() ? it->second : error_t());
                }
                break;
            } else {
                auto it = inferred.find(tp.name);
                if (it != inferred.end()) {
                    args.push_back(it->second);
                    h_idx++;
                } else if (hint_struct_type_ && hint_struct_type_->struct_name == std::string(sname) && h_idx < hint_struct_type_->type_args.size()) {
                    args.push_back(hint_struct_type_->type_args[h_idx++]);
                } else {
                    args.push_back(error_t());
                }
            }
        }
        check_type_bounds(std::string(sname), sinfo.type_params, args);
        const LogosType* lit_type = make_generic_struct(std::string(sname), args);

        // Check if a concrete specialization exists for these type args.
        std::string concrete = concrete_struct_name(lit_type);
        auto spec_it = struct_specs_sema_.find(concrete);
        const SemaStructInfo* effective = (spec_it != struct_specs_sema_.end())
                                          ? &spec_it->second : &sinfo;

        // Validate fields against the effective definition.
        std::unordered_map<std::string, bool> initialized;
        for (auto& f : effective->fields) initialized[std::string(f.name)] = false;
        for (auto& [fname, fval] : fields) {
            auto it = initialized.find(fname);
            if (it == initialized.end()) {
                // Check if this matches a variadic field expansion
                bool matched_variadic = false;
                for (auto& f : effective->fields) {
                    if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_') {
                        initialized[std::string(f.name)] = true;
                        matched_variadic = true;
                        // Type check against the variadic field's type
                        if (f.type && f.type->kind != LogosType::Kind::Error &&
                            fval->type->kind != LogosType::Kind::Error &&
                            f.type->kind != LogosType::Kind::TypeVar &&
                            !types_compatible(fval->type, f.type)) {
                            error(std::format("struct literal '{}' field '{}': expected {}, got {}",
                                  sname, fname, type_str(f.type), type_str(fval->type)));
                        }
                        break;
                    }
                }
                if (!matched_variadic)
                    error(std::format("struct literal '{}': unknown field '{}'", sname, fname));
            } else {
                it->second = true;
                // Find field type in effective definition.
                const LogosType* ft = nullptr;
                for (auto& ef : effective->fields)
                    if (ef.name == fname) { ft = ef.type; break; }
                bool ft_has_typevar = ft && (ft->kind == LogosType::Kind::TypeVar ||
                    (ft->kind == LogosType::Kind::Array && ft->elem &&
                     ft->elem->kind == LogosType::Kind::TypeVar));
                if (ft && ft->kind != LogosType::Kind::Error &&
                    fval->type->kind != LogosType::Kind::Error &&
                    !ft_has_typevar &&
                    !types_compatible(fval->type, ft)) {
                    error(std::format("struct literal '{}' field '{}': expected {}, got {}",
                          sname, fname, type_str(ft), type_str(fval->type)));
                }
                // Check IntLit field value fits in the declared field type.
                if (ft && fval->type->kind == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(fval.get()))
                        if (!intlit_fits(*v, ft->kind))
                            error(std::format("struct literal '{}' field '{}': value {} does not fit in {}",
                                  sname, fname, *v, type_str(ft)));
            }
        }
        for (auto& [fname, init] : initialized)
            if (!init)
                error(std::format("struct literal '{}': field '{}' not initialized", sname, fname));

        return make_expr(lit_type, lir::EStructLit{std::string(sname), std::move(fields)});
    }

    // Non-generic struct: validate against template fields directly.
    std::unordered_map<std::string, bool> initialized;
    for (auto& f : sinfo.fields) initialized[std::string(f.name)] = false;
    for (auto& [fname, fval] : fields) {
        auto it = initialized.find(fname);
        if (it == initialized.end()) {
            // Check variadic
            bool matched_variadic = false;
            for (auto& f : sinfo.fields) {
                if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_') {
                    initialized[std::string(f.name)] = true;
                    matched_variadic = true;
                    auto* ft = f.type;
                    if (ft && ft->kind != LogosType::Kind::Error &&
                        fval->type->kind != LogosType::Kind::Error &&
                        !types_compatible(fval->type, ft)) {
                        error(std::format("struct literal '{}' field '{}': expected {}, got {}",
                              sname, fname, type_str(ft), type_str(fval->type)));
                    }
                    if (ft && ft->kind != LogosType::Kind::Error &&
                        fval->type->kind == LogosType::Kind::IntLit)
                        if (auto v = get_intlit_value(fval.get()))
                            if (!intlit_fits(*v, ft->kind))
                                error(std::format("struct literal '{}' field '{}': value {} does not fit in {}",
                                      sname, fname, *v, type_str(ft)));
                    break;
                }
            }
            if (!matched_variadic)
                error(std::format("struct literal '{}': unknown field '{}'", sname, fname));
        } else {
            it->second = true;
            auto* ft = field_type_of(std::string(sname), fname);
            if (ft && ft->kind != LogosType::Kind::Error &&
                fval->type->kind != LogosType::Kind::Error &&
                !types_compatible(fval->type, ft)) {
                error(std::format("struct literal '{}' field '{}': expected {}, got {}",
                      sname, fname, type_str(ft), type_str(fval->type)));
            }
            // Check IntLit field value fits in the declared field type.
            if (ft && fval->type->kind == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(fval.get()))
                    if (!intlit_fits(*v, ft->kind))
                        error(std::format("struct literal '{}' field '{}': value {} does not fit in {}",
                              sname, fname, *v, type_str(ft)));
            // Check array literal elements against narrow array field type.
            if (ft && ft->kind == LogosType::Kind::Array && ft->elem &&
                fval->type->kind == LogosType::Kind::Array)
                if (auto* al = std::get_if<lir::EArrLit>(&fval->kind))
                    for (size_t i = 0; i < al->elems.size(); ++i)
                        if (al->elems[i]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(al->elems[i].get()))
                                if (!intlit_fits(*v, ft->elem->kind))
                                    error(std::format("struct literal '{}' field '{}': array element {}: value {} does not fit in {}",
                                          sname, fname, i, *v, type_str(ft->elem)));
            // Check tuple literal elements against narrow tuple field element types.
            if (ft && ft->kind == LogosType::Kind::Tuple && fval->type->kind == LogosType::Kind::Tuple)
                if (auto* tl = std::get_if<lir::ETupleLit>(&fval->kind))
                    for (size_t i = 0; i < tl->elems.size() && i < ft->tuple_elems.size(); ++i) {
                        if (tl->elems[i]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(tl->elems[i].get()))
                                if (ft->tuple_elems[i] && !intlit_fits(*v, ft->tuple_elems[i]->kind))
                                    error(std::format("struct literal '{}' field '{}': tuple element {}: value {} does not fit in {}",
                                          sname, fname, i, *v, type_str(ft->tuple_elems[i])));
                        if (ft->tuple_elems[i] && ft->tuple_elems[i]->kind == LogosType::Kind::Array &&
                            ft->tuple_elems[i]->elem && tl->elems[i]->type->kind == LogosType::Kind::Array)
                            if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[i]->kind))
                                for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                    if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(ial->elems[ii].get()))
                                            if (!intlit_fits(*v, ft->tuple_elems[i]->elem->kind))
                                                error(std::format("struct literal '{}' field '{}': tuple element {}: array element {}: value {} does not fit in {}",
                                                      sname, fname, i, ii, *v, type_str(ft->tuple_elems[i]->elem)));

                        if (ft->tuple_elems[i] && ft->tuple_elems[i]->kind == LogosType::Kind::Tuple &&
                            tl->elems[i]->type->kind == LogosType::Kind::Tuple)
                            if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[i]->kind))
                                for (size_t ii = 0; ii < itl->elems.size() && ii < ft->tuple_elems[i]->tuple_elems.size(); ++ii)
                                    if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(itl->elems[ii].get()))
                                            if (ft->tuple_elems[i]->tuple_elems[ii] && !intlit_fits(*v, ft->tuple_elems[i]->tuple_elems[ii]->kind))
                                                error(std::format("struct literal '{}' field '{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      sname, fname, i, ii, *v, type_str(ft->tuple_elems[i]->tuple_elems[ii])));
                        }
        }
    }
    for (auto& [fname, init] : initialized)
        if (!init)
            error(std::format("struct literal '{}': field '{}' not initialized", sname, fname));

    // Move semantics: mark Move-typed field values as consumed.
    for (auto& [fname, fval] : fields) {
        if (fval && is_move_type(fval->type))
            if (auto* vr = std::get_if<lir::EVarRef>(&fval->kind))
                mark_moved(vr->name);
    }

    return make_expr(make_struct_type(std::string(sname)),
        lir::EStructLit{std::string(sname), std::move(fields)});
}

lir::LExprPtr SemaChecker::lower_index_read(TinyMapView node) {
    auto recv = lower_expr(map_of(node.get(la::RECEIVER.code)));
    auto* arr_type = recv->type;

    lir::LExprPtr idx = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    if (!is_integer(idx->type))
        error(std::format("array index must be integer, got {}", type_str(idx->type)));

    // Slice indexing: s[i] → ESliceIndex
    if (arr_type->kind == LogosType::Kind::Slice) {
        auto* elem = arr_type->elem ? arr_type->elem : error_t();
        return make_expr(elem, lir::ESliceIndex{std::move(recv), std::move(idx)});
    }

    if (arr_type->kind != LogosType::Kind::Array &&
        arr_type->kind != LogosType::Kind::Ptr &&
        arr_type->kind != LogosType::Kind::Ref &&
        arr_type->kind != LogosType::Kind::MutRef &&
        arr_type->kind != LogosType::Kind::Error) {
        error(std::format("index read: receiver is not an array, slice, or pointer (got {})",
              type_str(arr_type)));
    }
    if (arr_type->kind == LogosType::Kind::Ptr && !inside_unsafe_) {
        error("index read through raw pointer requires unsafe context");
    }

    const LogosType* elem = error_t();
    if (arr_type->kind == LogosType::Kind::Array && arr_type->elem)  elem = arr_type->elem;
    if ((arr_type->kind == LogosType::Kind::Ptr ||
         arr_type->kind == LogosType::Kind::Ref ||
         arr_type->kind == LogosType::Kind::MutRef) && arr_type->pointee)
        elem = arr_type->pointee;

    return make_expr(elem, lir::EIndexRead{std::move(recv), std::move(idx)});
}

lir::LExprPtr SemaChecker::lower_arr_lit(TinyMapView node) {
    if (!node.has_key(la::ITEMS)) {
        warn("empty array literal: element type unknown");
        return error_expr();
    }
    auto items = arr_of(node.get(la::ITEMS.code));
    if (items.size() == 0) {
        warn("empty array literal: element type unknown");
        return error_expr();
    }
    std::vector<lir::LExprPtr> elems;
    for (uint64_t i = 0; i < items.size(); ++i)
        elems.push_back(lower_expr(map_of(items.get(i))));

    const LogosType* elem_type = elems[0]->type;
    for (uint64_t i = 1; i < elems.size(); ++i) {
        auto* t = elems[i]->type;
        if (t->kind != LogosType::Kind::Error && elem_type->kind != LogosType::Kind::Error) {
            if (!types_compatible(t, elem_type) && !types_compatible(elem_type, t)) {
                error(std::format("array literal: element {} has type {}, expected {}",
                      i, type_str(t), type_str(elem_type)));
            } else {
                // If the concrete element type is narrow and this element is IntLit, check range.
                if (t->kind == LogosType::Kind::IntLit &&
                    elem_type->kind != LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(elems[i].get()))
                        if (!intlit_fits(*v, elem_type->kind))
                            error(std::format("array literal: element {}: value {} does not fit in {}",
                                  i, *v, type_str(elem_type)));
                // Check array literal elements against narrow nested array element types.
                if (elem_type->kind == LogosType::Kind::Array && elem_type->elem &&
                    t->kind == LogosType::Kind::Array)
                    if (auto* al = std::get_if<lir::EArrLit>(&elems[i]->kind))
                        for (size_t ei = 0; ei < al->elems.size(); ++ei)
                            if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(al->elems[ei].get()))
                                    if (!intlit_fits(*v, elem_type->elem->kind))
                                        error(std::format("array literal: element {}: sub-element {}: value {} does not fit in {}",
                                              i, ei, *v, type_str(elem_type->elem)));
                // Check tuple literal elements against narrow nested tuple element types.
                if (elem_type->kind == LogosType::Kind::Tuple && t->kind == LogosType::Kind::Tuple)
                    if (auto* tl = std::get_if<lir::ETupleLit>(&elems[i]->kind))
                        for (size_t ei = 0; ei < tl->elems.size() && ei < elem_type->tuple_elems.size(); ++ei) {
                            if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(tl->elems[ei].get()))
                                    if (elem_type->tuple_elems[ei] && !intlit_fits(*v, elem_type->tuple_elems[ei]->kind))
                                        error(std::format("array literal: element {}: tuple element {}: value {} does not fit in {}",
                                              i, ei, *v, type_str(elem_type->tuple_elems[ei])));
                            if (elem_type->tuple_elems[ei] && elem_type->tuple_elems[ei]->kind == LogosType::Kind::Array &&
                                elem_type->tuple_elems[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                                if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                        if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(ial->elems[ii].get()))
                                                if (!intlit_fits(*v, elem_type->tuple_elems[ei]->elem->kind))
                                                    error(std::format("array literal: element {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                          i, ei, ii, *v, type_str(elem_type->tuple_elems[ei]->elem)));

                            if (elem_type->tuple_elems[ei] && elem_type->tuple_elems[ei]->kind == LogosType::Kind::Tuple &&
                                tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                                if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < itl->elems.size() && ii < elem_type->tuple_elems[ei]->tuple_elems.size(); ++ii)
                                        if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(itl->elems[ii].get()))
                                                if (elem_type->tuple_elems[ei]->tuple_elems[ii] && !intlit_fits(*v, elem_type->tuple_elems[ei]->tuple_elems[ii]->kind))
                                                    error(std::format("array literal: element {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                          i, ei, ii, *v, type_str(elem_type->tuple_elems[ei]->tuple_elems[ii])));
                            }
                elem_type = unify_int(elem_type, t);
            }
        }
    }
    // Element 0 retroactive check: the loop above only checks elements 1+.
    // If a later element has a concrete narrow type, element 0 (which set
    // elem_type initially) was never range-checked against it.
    // Find the first concrete anchor from elements 1+ and check element 0.
    if (elems.size() > 1) {
        // Locate the first element whose type is concrete (not purely IntLit-typed).
        const LogosType* anchor = nullptr;
        for (size_t i = 1; i < elems.size() && !anchor; ++i) {
            const LogosType* ti = elems[i]->type;
            if (ti->kind != LogosType::Kind::IntLit &&
                !(ti->kind == LogosType::Kind::Array && ti->elem &&
                  ti->elem->kind == LogosType::Kind::IntLit))
                anchor = ti;
        }
        if (anchor) {
            auto* e = elems[0].get();
            auto* t0 = elems[0]->type;
            // Scalar IntLit at element 0.
            if (t0->kind == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(e))
                    if (!intlit_fits(*v, anchor->kind))
                        error(std::format("array literal: element 0: value {} does not fit in {}",
                              *v, type_str(anchor)));
            // Array literal at element 0 (e.g. [[1,200,3], concrete_arr]).
            if (anchor->kind == LogosType::Kind::Array && anchor->elem &&
                t0->kind == LogosType::Kind::Array)
                if (auto* al = std::get_if<lir::EArrLit>(&e->kind))
                    for (size_t ei = 0; ei < al->elems.size(); ++ei)
                        if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(al->elems[ei].get()))
                                if (!intlit_fits(*v, anchor->elem->kind))
                                    error(std::format("array literal: element 0: sub-element {}: value {} does not fit in {}",
                                          ei, *v, type_str(anchor->elem)));
            // Tuple literal at element 0 (tuple elements, including nested array/tuple).
            if (anchor->kind == LogosType::Kind::Tuple && t0->kind == LogosType::Kind::Tuple)
                if (auto* tl = std::get_if<lir::ETupleLit>(&e->kind))
                    for (size_t ei = 0; ei < tl->elems.size() && ei < anchor->tuple_elems.size(); ++ei) {
                        if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(tl->elems[ei].get()))
                                if (anchor->tuple_elems[ei] && !intlit_fits(*v, anchor->tuple_elems[ei]->kind))
                                    error(std::format("array literal: element 0: tuple element {}: value {} does not fit in {}",
                                          ei, *v, type_str(anchor->tuple_elems[ei])));
                        if (anchor->tuple_elems[ei] && anchor->tuple_elems[ei]->kind == LogosType::Kind::Array &&
                            anchor->tuple_elems[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                            if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                    if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(ial->elems[ii].get()))
                                            if (!intlit_fits(*v, anchor->tuple_elems[ei]->elem->kind))
                                                error(std::format("array literal: element 0: tuple element {}: array element {}: value {} does not fit in {}",
                                                      ei, ii, *v, type_str(anchor->tuple_elems[ei]->elem)));
                        if (anchor->tuple_elems[ei] && anchor->tuple_elems[ei]->kind == LogosType::Kind::Tuple &&
                            tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                            if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                for (size_t ii = 0; ii < itl->elems.size() && ii < anchor->tuple_elems[ei]->tuple_elems.size(); ++ii)
                                    if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(itl->elems[ii].get()))
                                            if (anchor->tuple_elems[ei]->tuple_elems[ii] && !intlit_fits(*v, anchor->tuple_elems[ei]->tuple_elems[ii]->kind))
                                                error(std::format("array literal: element 0: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      ei, ii, *v, type_str(anchor->tuple_elems[ei]->tuple_elems[ii])));
                    }
        }
    }
    // For IntLit element type: upgrade to i64 if any value overflows i32.
    // Keep IntLit (don't collapse to i32) so that annotation-based coercion
    // ([i64; N] = [1, 2, 3]) can use types_compatible([IntLit;N], [i64;N]) → true.
    if (elem_type->kind == LogosType::Kind::IntLit) {
        bool needs_i64 = false;
        for (const auto& elem : elems) {
            if (auto v = get_intlit_value(elem.get()))
                if (*v > (int64_t)INT32_MAX || *v < (int64_t)INT32_MIN)
                    { needs_i64 = true; break; }
        }
        if (needs_i64) elem_type = prim(LogosType::Kind::I64);
        // else: leave as IntLit — mlir_gen will see the annotation type
    }

    return make_expr(make_array(elem_type, elems.size()), lir::EArrLit{std::move(elems)});
}

lir::LExprPtr SemaChecker::lower_arr_fill_lit(TinyMapView node) {
    auto val_node = map_of(node.get(la::VALUE.code));
    auto fill_val = lower_expr(val_node);
    auto sv = str_of(node.get(la::SIZE.code));
    int64_t n = parse_int_literal(sv);
    if (n <= 0) error(std::format("array fill literal: size must be positive, got {}", n));
    const LogosType* elem_type = fill_val->type;
    // Keep IntLit unresolved so that struct-literal type inference (hint_struct_type_)
    // can widen the element to the correct concrete type (e.g. i64 for Vec<i64>).
    std::vector<lir::LExprPtr> elems;
    elems.push_back(std::move(fill_val));
    for (int64_t i = 1; i < n; ++i)
        elems.push_back(lower_expr(val_node));  // re-lower for each slot (simple literals)
    return make_expr(make_array(elem_type, (size_t)n), lir::EArrLit{std::move(elems)});
}

lir::LExprPtr SemaChecker::lower_enum_lit(TinyMapView node) {
    auto ename = str_of(node.get(la::NAME.code));
    auto vname = str_of(node.get(la::FIELD.code));
    auto eit = enums_.find(std::string(ename));
    if (eit == enums_.end()) {
        error(std::format("unknown enum '{}'", ename));
        return error_expr();
    }
    int32_t disc = 0;
    bool found = false;
    for (auto& v : eit->second.variants)
        if (v.name == vname) { disc = v.value; found = true; break; }
    if (!found) {
        error(std::format("enum '{}' has no variant '{}'", ename, vname));
        return error_expr();
    }
    return make_expr(make_enum_type(ename),
        lir::EEnumLit{std::string(ename), std::string(vname), disc});
}

lir::LExprPtr SemaChecker::lower_enum_lit_data(TinyMapView node) {
    auto ename = str_of(node.get(la::NAME.code));
    auto vname = str_of(node.get(la::FIELD.code));
    auto eit = enums_.find(std::string(ename));
    if (eit == enums_.end()) {
        error(std::format("unknown enum '{}'", ename));
        return error_expr();
    }
    const SemaVariantInfo* vinfo = nullptr;
    for (auto& v : eit->second.variants)
        if (v.name == vname) { vinfo = &v; break; }
    if (!vinfo) {
        error(std::format("enum '{}' has no variant '{}'", ename, vname));
        return error_expr();
    }

    // Lower payload arguments
    std::vector<lir::LExprPtr> payload;
    if (node.has_key(la::ARGS)) {
        auto args = arr_of(node.get(la::ARGS.code));
        for (uint64_t i = 0; i < args.size(); ++i)
            payload.push_back(lower_expr(map_of(args.get(i))));
    }

    // Resolve payload types — substitute TypeVars if generic enum
    auto& einfo = eit->second;
    std::vector<const LogosType*> resolved_payload_types = vinfo->payload_types;

    // Build the enum type (may be generic, e.g. Option<i32>)
    // For now, if the enum has type params, we need to infer them from payload types.
    // Simple inference: match payload args to payload type params.
    const LogosType* result_type = make_enum_type(ename);
    if (!einfo.type_params.empty()) {
        // Build substitution from payload args
        SemaSubst subst;
        for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i) {
            auto* pt = vinfo->payload_types[i];
            if (pt && pt->kind == LogosType::Kind::TypeVar) {
                auto* inferred = payload[i]->type;
                if (inferred->kind == LogosType::Kind::IntLit) inferred = i32_t();
                subst[pt->type_var_name] = inferred;
            }
        }
        // Fill any still-unresolved type params from hint (e.g. let e: Result<i32,i32> = Result::Err(-1))
        if (hint_enum_type_ && hint_enum_type_->enum_name == std::string(ename)) {
            for (size_t i = 0; i < einfo.type_params.size() && i < hint_enum_type_->type_args.size(); ++i) {
                if (subst.find(einfo.type_params[i].name) == subst.end()) {
                    auto* hta = hint_enum_type_->type_args[i];
                    if (hta && hta->kind != LogosType::Kind::Error)
                        subst[einfo.type_params[i].name] = hta;
                }
            }
        }
        // Build concrete type args
        std::vector<const LogosType*> type_args;
        for (auto& tp : einfo.type_params) {
            auto sit = subst.find(tp.name);
            type_args.push_back(sit != subst.end() ? sit->second : error_t());
        }
        check_type_bounds(std::string(ename), einfo.type_params, type_args);
        LogosType et; et.kind = LogosType::Kind::Enum;
        et.enum_name = std::string(ename);
        et.type_args = std::move(type_args);
        result_type = pool_.alloc(std::move(et));
        // Resolve payload types with substitution
        for (size_t i = 0; i < resolved_payload_types.size(); ++i)
            resolved_payload_types[i] = subst_type_sema(resolved_payload_types[i], subst);
    }

    // Type-check payload args against expected types
    if (!vinfo->is_variadic && payload.size() != vinfo->payload_types.size()) {
        error(std::format("{}::{} expects {} args, got {}",
              ename, vname, vinfo->payload_types.size(), payload.size()));
    } else if (!vinfo->is_variadic) {
        for (size_t i = 0; i < payload.size(); ++i) {
            if (payload[i]->type->kind != LogosType::Kind::Error &&
                resolved_payload_types[i] &&
                resolved_payload_types[i]->kind != LogosType::Kind::Error &&
                !types_compatible(payload[i]->type, resolved_payload_types[i]))
                error(std::format("{}::{} arg {}: expected {}, got {}",
                      ename, vname, i, type_str(resolved_payload_types[i]),
                      type_str(payload[i]->type)));
            // Check IntLit payload value fits in the declared payload type.
            if (resolved_payload_types[i] && payload[i]->type->kind == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(payload[i].get()))
                    if (!intlit_fits(*v, resolved_payload_types[i]->kind))
                        error(std::format("{}::{} arg {}: value {} does not fit in {}",
                              ename, vname, i, *v, type_str(resolved_payload_types[i])));
            // Check array literal elements against narrow array payload type.
            if (resolved_payload_types[i] &&
                resolved_payload_types[i]->kind == LogosType::Kind::Array &&
                resolved_payload_types[i]->elem &&
                payload[i]->type->kind == LogosType::Kind::Array)
                if (auto* al = std::get_if<lir::EArrLit>(&payload[i]->kind))
                    for (size_t ei = 0; ei < al->elems.size(); ++ei)
                        if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(al->elems[ei].get()))
                                if (!intlit_fits(*v, resolved_payload_types[i]->elem->kind))
                                    error(std::format("{}::{} arg {}: array element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(resolved_payload_types[i]->elem)));
            // Check tuple literal elements against narrow tuple payload type.
            if (resolved_payload_types[i] &&
                resolved_payload_types[i]->kind == LogosType::Kind::Tuple &&
                payload[i]->type->kind == LogosType::Kind::Tuple)
                if (auto* tl = std::get_if<lir::ETupleLit>(&payload[i]->kind))
                    for (size_t ei = 0; ei < tl->elems.size() && ei < resolved_payload_types[i]->tuple_elems.size(); ++ei) {
                        if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(tl->elems[ei].get()))
                                if (resolved_payload_types[i]->tuple_elems[ei] &&
                                    !intlit_fits(*v, resolved_payload_types[i]->tuple_elems[ei]->kind))
                                    error(std::format("{}::{} arg {}: tuple element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(resolved_payload_types[i]->tuple_elems[ei])));
                        if (resolved_payload_types[i]->tuple_elems[ei] && resolved_payload_types[i]->tuple_elems[ei]->kind == LogosType::Kind::Array &&
                            resolved_payload_types[i]->tuple_elems[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                            if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                    if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(ial->elems[ii].get()))
                                            if (!intlit_fits(*v, resolved_payload_types[i]->tuple_elems[ei]->elem->kind))
                                                error(std::format("{}::{} arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                      ename, vname, i, ei, ii, *v, type_str(resolved_payload_types[i]->tuple_elems[ei]->elem)));

                        if (resolved_payload_types[i]->tuple_elems[ei] && resolved_payload_types[i]->tuple_elems[ei]->kind == LogosType::Kind::Tuple &&
                            tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                            if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                for (size_t ii = 0; ii < itl->elems.size() && ii < resolved_payload_types[i]->tuple_elems[ei]->tuple_elems.size(); ++ii)
                                    if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(itl->elems[ii].get()))
                                            if (resolved_payload_types[i]->tuple_elems[ei]->tuple_elems[ii] && !intlit_fits(*v, resolved_payload_types[i]->tuple_elems[ei]->tuple_elems[ii]->kind))
                                                error(std::format("{}::{} arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      ename, vname, i, ei, ii, *v, type_str(resolved_payload_types[i]->tuple_elems[ei]->tuple_elems[ii])));
                        }
        }
    } else {
        // Variadic variant: match each arg against the pack's type (if it's not a generic expansion itself).
        if (!resolved_payload_types.empty()) {
            auto* pack_t = resolved_payload_types[0];
            for (size_t i = 0; i < payload.size(); ++i) {
                if (payload[i]->type->kind != LogosType::Kind::Error &&
                    pack_t->kind != LogosType::Kind::Error &&
                    !types_compatible(payload[i]->type, pack_t))
                    error(std::format("{}::{} variadic arg {}: expected {}, got {}",
                          ename, vname, i, type_str(pack_t), type_str(payload[i]->type)));
            }
        }
    }

    return make_expr(result_type,
        lir::EEnumLitData{std::string(ename), std::string(vname),
                          vinfo->value, std::move(payload)});
}

lir::LExprPtr SemaChecker::lower_enum_lit_data_from_static(
        TinyMapView node, std::string_view ename, std::string_view vname) {
    auto eit = enums_.find(std::string(ename));
    if (eit == enums_.end()) return error_expr();
    const SemaVariantInfo* vinfo = nullptr;
    for (auto& v : eit->second.variants)
        if (v.name == vname) { vinfo = &v; break; }
    if (!vinfo) {
        error(std::format("enum '{}' has no variant '{}'", ename, vname));
        return error_expr();
    }
    // Lower args
    std::vector<lir::LExprPtr> payload;
    if (node.has_key(la::ARGS)) {
        AnyVal args_av = node.get(la::ARGS.code);
        if (!args_av.is_null()) {
            auto args = map_of(args_av);
            if (args.has_key(la::ITEMS)) {
                auto items = arr_of(args.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    payload.push_back(lower_expr(map_of(items.get(i))));
            }
        }
    }
    // Build result type + type-check (same logic as lower_enum_lit_data)
    auto& einfo = eit->second;
    std::vector<const LogosType*> resolved_payload_types = vinfo->payload_types;
    const LogosType* result_type = make_enum_type(ename);
    if (!einfo.type_params.empty()) {
        SemaSubst subst;
        for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i) {
            auto* pt = vinfo->payload_types[i];
            if (pt && pt->kind == LogosType::Kind::TypeVar) {
                auto* inferred = payload[i]->type;
                if (inferred->kind == LogosType::Kind::IntLit) inferred = i32_t();
                subst[pt->type_var_name] = inferred;
            }
        }
        // Fill any still-unresolved type params from hint
        if (hint_enum_type_ && hint_enum_type_->enum_name == std::string(ename)) {
            for (size_t i = 0; i < einfo.type_params.size() && i < hint_enum_type_->type_args.size(); ++i) {
                if (subst.find(einfo.type_params[i].name) == subst.end()) {
                    auto* hta = hint_enum_type_->type_args[i];
                    if (hta && hta->kind != LogosType::Kind::Error)
                        subst[einfo.type_params[i].name] = hta;
                }
            }
        }
        std::vector<const LogosType*> type_args;
        for (auto& tp : einfo.type_params) {
            auto sit = subst.find(tp.name);
            type_args.push_back(sit != subst.end() ? sit->second : error_t());
        }
        check_type_bounds(std::string(ename), einfo.type_params, type_args);
        LogosType et; et.kind = LogosType::Kind::Enum;
        et.enum_name = std::string(ename);
        et.type_args = std::move(type_args);
        result_type = pool_.alloc(std::move(et));
        for (size_t i = 0; i < resolved_payload_types.size(); ++i)
            resolved_payload_types[i] = subst_type_sema(resolved_payload_types[i], subst);
    }
    if (!vinfo->is_variadic && payload.size() != vinfo->payload_types.size()) {
        error(std::format("{}::{} expects {} args, got {}",
              ename, vname, vinfo->payload_types.size(), payload.size()));
    } else if (!vinfo->is_variadic) {
        for (size_t i = 0; i < payload.size(); ++i) {
            if (payload[i]->type->kind != LogosType::Kind::Error &&
                resolved_payload_types[i] &&
                resolved_payload_types[i]->kind != LogosType::Kind::Error &&
                !types_compatible(payload[i]->type, resolved_payload_types[i]))
                error(std::format("{}::{} arg {}: expected {}, got {}",
                      ename, vname, i, type_str(resolved_payload_types[i]),
                      type_str(payload[i]->type)));
            if (resolved_payload_types[i] &&
                payload[i]->type->kind == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(payload[i].get()))
                    if (!intlit_fits(*v, resolved_payload_types[i]->kind))
                        error(std::format("{}::{} arg {}: value {} does not fit in {}",
                              ename, vname, i, *v,
                              type_str(resolved_payload_types[i])));
            // Check array literal elements against narrow array payload type.
            if (resolved_payload_types[i] &&
                resolved_payload_types[i]->kind == LogosType::Kind::Array &&
                resolved_payload_types[i]->elem &&
                payload[i]->type->kind == LogosType::Kind::Array)
                if (auto* al = std::get_if<lir::EArrLit>(&payload[i]->kind))
                    for (size_t ei = 0; ei < al->elems.size(); ++ei)
                        if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(al->elems[ei].get()))
                                if (!intlit_fits(*v, resolved_payload_types[i]->elem->kind))
                                    error(std::format("{}::{} arg {}: array element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(resolved_payload_types[i]->elem)));
            // Check tuple literal elements against narrow tuple payload type.
            if (resolved_payload_types[i] &&
                resolved_payload_types[i]->kind == LogosType::Kind::Tuple &&
                payload[i]->type->kind == LogosType::Kind::Tuple)
                if (auto* tl = std::get_if<lir::ETupleLit>(&payload[i]->kind))
                    for (size_t ei = 0; ei < tl->elems.size() && ei < resolved_payload_types[i]->tuple_elems.size(); ++ei) {
                        if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(tl->elems[ei].get()))
                                if (resolved_payload_types[i]->tuple_elems[ei] &&
                                    !intlit_fits(*v, resolved_payload_types[i]->tuple_elems[ei]->kind))
                                    error(std::format("{}::{} arg {}: tuple element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(resolved_payload_types[i]->tuple_elems[ei])));
                        if (resolved_payload_types[i]->tuple_elems[ei] && resolved_payload_types[i]->tuple_elems[ei]->kind == LogosType::Kind::Array &&
                            resolved_payload_types[i]->tuple_elems[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                            if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                    if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(ial->elems[ii].get()))
                                            if (!intlit_fits(*v, resolved_payload_types[i]->tuple_elems[ei]->elem->kind))
                                                error(std::format("{}::{} arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                      ename, vname, i, ei, ii, *v, type_str(resolved_payload_types[i]->tuple_elems[ei]->elem)));

                        if (resolved_payload_types[i]->tuple_elems[ei] && resolved_payload_types[i]->tuple_elems[ei]->kind == LogosType::Kind::Tuple &&
                            tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                            if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                for (size_t ii = 0; ii < itl->elems.size() && ii < resolved_payload_types[i]->tuple_elems[ei]->tuple_elems.size(); ++ii)
                                    if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(itl->elems[ii].get()))
                                            if (resolved_payload_types[i]->tuple_elems[ei]->tuple_elems[ii] && !intlit_fits(*v, resolved_payload_types[i]->tuple_elems[ei]->tuple_elems[ii]->kind))
                                                error(std::format("{}::{} arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      ename, vname, i, ei, ii, *v, type_str(resolved_payload_types[i]->tuple_elems[ei]->tuple_elems[ii])));
                        }
        }
    } else {
        if (!resolved_payload_types.empty()) {
            auto* pack_t = resolved_payload_types[0];
            for (size_t i = 0; i < payload.size(); ++i) {
                if (payload[i]->type->kind != LogosType::Kind::Error &&
                    pack_t->kind != LogosType::Kind::Error &&
                    !types_compatible(payload[i]->type, pack_t))
                    error(std::format("{}::{} variadic arg {}: expected {}, got {}",
                          ename, vname, i, type_str(pack_t), type_str(payload[i]->type)));
                if (pack_t->kind != LogosType::Kind::Error &&
                    payload[i]->type->kind == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(payload[i].get()))
                        if (!intlit_fits(*v, pack_t->kind))
                            error(std::format("{}::{} variadic arg {}: value {} does not fit in {}",
                                  ename, vname, i, *v, type_str(pack_t)));
            }
        }
    }
    return make_expr(result_type,
        lir::EEnumLitData{std::string(ename), std::string(vname),
                          vinfo->value, std::move(payload)});
}

lir::LExprPtr SemaChecker::lower_new_expr(TinyMapView node) {
    auto cname = str_of(node.get(la::NAME.code));
    auto cit = classes_.find(std::string(cname));
    if (cit == classes_.end()) {
        error(std::format("'new': unknown class '{}'", cname));
        return error_expr();
    }
    auto& cinfo = cit->second;
    if (cinfo.is_abstract) {
        error(std::format("'new': cannot instantiate abstract class '{}'", cname));
        return error_expr();
    }

    std::vector<std::pair<std::string, lir::LExprPtr>> fields;
    if (node.has_key(la::ITEMS)) {
        auto inits = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < inits.size(); ++i) {
            auto init = map_of(inits.get(i));
            if (code_of(init) != la::FIELD_INIT) continue;  // skip type_arg_list or other non-field items
            auto fname = str_of(init.get(la::NAME.code));
            lir::LExprPtr val = init.has_key(la::VALUE)
                ? lower_expr(map_of(init.get(la::VALUE.code)))
                : error_expr();
            fields.push_back({std::string(fname), std::move(val)});
        }
    }

    // Validate all fields are initialized and no extras
    std::unordered_map<std::string, bool> initialized;
    for (auto& f : cinfo.all_fields) initialized[f.name] = false;
    for (auto& [fname, fval] : fields) {
        auto it = initialized.find(fname);
        if (it == initialized.end()) {
            error(std::format("'new {}': unknown field '{}'", cname, fname));
        } else {
            it->second = true;
            // Find expected type; skip check if field type is TypeVar (checked post-mono)
            for (auto& f : cinfo.all_fields) {
                if (f.name == fname && f.type->kind != LogosType::Kind::Error &&
                    f.type->kind != LogosType::Kind::TypeVar &&
                    fval->type->kind != LogosType::Kind::Error &&
                    !compat(fval->type, f.type)) {
                    error(std::format("'new {}' field '{}': expected {}, got {}",
                          cname, fname, type_str(f.type), type_str(fval->type)));
                }
                if (f.name == fname && f.type->kind != LogosType::Kind::Error &&
                    fval->type->kind == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(fval.get()))
                        if (!intlit_fits(*v, f.type->kind))
                            error(std::format("'new {}' field '{}': value {} does not fit in {}",
                                  cname, fname, *v, type_str(f.type)));
                // Check array literal elements against narrow array field type.
                if (f.name == fname && f.type->kind == LogosType::Kind::Array && f.type->elem &&
                    fval->type->kind == LogosType::Kind::Array)
                    if (auto* al = std::get_if<lir::EArrLit>(&fval->kind))
                        for (size_t ei = 0; ei < al->elems.size(); ++ei)
                            if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(al->elems[ei].get()))
                                    if (!intlit_fits(*v, f.type->elem->kind))
                                        error(std::format("'new {}' field '{}': array element {}: value {} does not fit in {}",
                                              cname, fname, ei, *v, type_str(f.type->elem)));
            // Check tuple literal elements against narrow tuple field element types.
            if (f.type->kind == LogosType::Kind::Tuple &&
                fval->type->kind == LogosType::Kind::Tuple)
                if (auto* tl = std::get_if<lir::ETupleLit>(&fval->kind))
                    for (size_t ei = 0; ei < tl->elems.size() && ei < f.type->tuple_elems.size(); ++ei) {
                        if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(tl->elems[ei].get()))
                                if (f.type->tuple_elems[ei] && !intlit_fits(*v, f.type->tuple_elems[ei]->kind))
                                    error(std::format("'new {}' field '{}': tuple element {}: value {} does not fit in {}",
                                          cname, fname, ei, *v, type_str(f.type->tuple_elems[ei])));
                        if (f.type->tuple_elems[ei] && f.type->tuple_elems[ei]->kind == LogosType::Kind::Array &&
                            f.type->tuple_elems[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                            if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                    if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(ial->elems[ii].get()))
                                            if (!intlit_fits(*v, f.type->tuple_elems[ei]->elem->kind))
                                                error(std::format("'new {}' field '{}': tuple element {}: array element {}: value {} does not fit in {}",
                                                      cname, fname, ei, ii, *v, type_str(f.type->tuple_elems[ei]->elem)));

                        if (f.type->tuple_elems[ei] && f.type->tuple_elems[ei]->kind == LogosType::Kind::Tuple &&
                            tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                            if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                for (size_t ii = 0; ii < itl->elems.size() && ii < f.type->tuple_elems[ei]->tuple_elems.size(); ++ii)
                                    if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(itl->elems[ii].get()))
                                            if (f.type->tuple_elems[ei]->tuple_elems[ii] && !intlit_fits(*v, f.type->tuple_elems[ei]->tuple_elems[ii]->kind))
                                                error(std::format("'new {}' field '{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      cname, fname, ei, ii, *v, type_str(f.type->tuple_elems[ei]->tuple_elems[ii])));
                        }
            }
        }
    }
    for (auto& [fn, init] : initialized)
        if (!init)
            error(std::format("'new {}': field '{}' not initialized", cname, fn));

    // For generic classes, use explicit type args if provided, else infer from fields.
    const LogosType* class_t = nullptr;
    if (!cinfo.type_params.empty()) {
        std::vector<const LogosType*> args;
        if (node.has_key(la::TYPE_PARAMS)) {
            // Explicit: new Box<i32> { ... }
            // TYPE_PARAMS is a type_arg_list node: { ITEMS: [type_ref, ...] }
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            auto type_items = tplist.has_key(la::ITEMS)
                                ? arr_of(tplist.get(la::ITEMS.code))
                                : arr_of(node.get(la::TYPE_PARAMS.code));
            for (uint64_t i = 0; i < type_items.size(); ++i)
                args.push_back(resolve_type(map_of(type_items.get(i))));
        } else {
            // Infer from field values
            SemaSubst inferred;
            for (auto& [fname, fval] : fields) {
                for (auto& f : cinfo.all_fields) {
                    if (f.name != fname) continue;
                    if (f.type->kind == LogosType::Kind::TypeVar) {
                        auto& tv = f.type->type_var_name;
                        if (!inferred.count(tv)) {
                            auto* vt = fval->type;
                            if (vt->kind == LogosType::Kind::IntLit) vt = i32_t();
                            inferred[tv] = vt;
                        }
                    }
                }
            }
            for (auto& tp : cinfo.type_params) {
                auto it = inferred.find(tp.name);
                args.push_back(it != inferred.end() ? it->second : error_t());
            }
        }
        check_type_bounds(std::string(cname), cinfo.type_params, args);
        class_t = make_generic_class(cname, std::move(args));
    } else {
        class_t = make_class_type(cname);
    }
    auto* result_t = make_ptr(true, class_t);
    return make_expr(result_t, lir::ENew{std::string(cname), std::move(fields)});
}

lir::LExprPtr SemaChecker::lower_static_call(TinyMapView node) {
    auto class_name = str_of(node.get(la::RECEIVER.code));
    auto method_name = str_of(node.get(la::NAME.code));

    // If "class_name" is actually an enum, redirect to enum lit with data.
    if (enums_.count(std::string(class_name))) {
        // Reinterpret as ENUM_LIT_DATA: NAME=class_name, FIELD=method_name
        // Build a fake node view... or just inline the logic.
        return lower_enum_lit_data_from_static(node, class_name, method_name);
    }

    std::string mangled = std::string(class_name) + "__" + std::string(method_name);

    std::vector<lir::LExprPtr> arg_exprs;
    if (node.has_key(la::ARGS)) {
        AnyVal args_av = node.get(la::ARGS.code);
        if (!args_av.is_null()) {
            auto args = map_of(args_av);
            if (args.has_key(la::ITEMS)) {
                auto items = arr_of(args.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    arg_exprs.push_back(lower_expr(map_of(items.get(i))));
            }
        }
    }

    // Bug 3 fix: look in both funcs_ and generic_funcs_ (generic static methods
    // registered with type params end up in generic_funcs_, not funcs_).
    const SemaFuncInfo* fi_ptr = nullptr;
    {
        auto fit = funcs_.find(mangled);
        if (fit != funcs_.end()) fi_ptr = &fit->second;
        else {
            auto git = generic_funcs_.find(mangled);
            if (git != generic_funcs_.end()) fi_ptr = &git->second;
        }
    }
    if (!fi_ptr) {
        error(std::format("call to undefined static method '{}::{}'", class_name, method_name));
        return make_expr(error_t(), lir::ECall{mangled, {}, std::move(arg_exprs)});
    }

    auto& fi = *fi_ptr;
    check_pub_access(fi.is_pub, fi.package, mangled);
    if (fi.is_unsafe && !inside_unsafe_)
        error(std::format("call to unsafe method '{}' requires unsafe context", mangled));

    // If the static method is generic (has type params from the enclosing impl<T>),
    // infer the concrete type arguments and produce a direct call to the concrete
    // struct method (e.g. Box$G1$i32__wrap), triggering struct instantiation.
    if (!fi.type_params.empty()) {
        // Check whether we're already inside a generic body (TypeVar args).
        bool in_generic_context = false;
        for (auto& a : arg_exprs) {
            auto* t = a->type;
            if (t && (t->kind == LogosType::Kind::TypeVar ||
                      t->kind == LogosType::Kind::AssocType)) {
                in_generic_context = true; break;
            }
        }
        if (!in_generic_context) {
            std::vector<const LogosType*> inferred;
            if (infer_type_args(fi, arg_exprs, inferred)) {
                // Build substitution map for the inferred type params.
                std::unordered_map<std::string, const LogosType*> subst;
                size_t n_tp = fi.type_params.size();
                for (size_t i = 0; i < n_tp && i < inferred.size(); ++i)
                    subst[fi.type_params[i].name] = inferred[i];

                // Substitute the return type.
                const LogosType* ret = subst_type_sema(fi.ret_type, subst);

                // If the host struct is generic (class_name has type params in structs_),
                // the method lives inside the struct template.  Produce a concrete call
                // to the already-named method on the instantiated struct
                // (e.g. "Box$G1$i32__wrap") with NO type_args, so mlir_gen finds it.
                auto sit = structs_.find(std::string(class_name));
                if (sit != structs_.end() && !sit->second.type_params.empty()) {
                    // Build the concrete struct type using the leading type params
                    // (the first N params belong to the impl, the rest to the fn itself).
                    size_t impl_n = sit->second.type_params.size();
                    std::vector<const LogosType*> struct_args;
                    for (size_t i = 0; i < impl_n && i < inferred.size(); ++i)
                        struct_args.push_back(inferred[i]);
                    const LogosType* struct_t = make_generic_struct(class_name, struct_args);
                    std::string concrete = concrete_struct_name(struct_t);
                    std::string concrete_callee = concrete + "__" + std::string(method_name);
                    return make_expr(ret, lir::ECall{concrete_callee, {}, std::move(arg_exprs)});
                }

                // For generic free functions registered via impl (no struct template),
                // fall through to finish_generic_call which handles them via templates_.
                return finish_generic_call(mangled, fi, std::move(inferred), std::move(arg_exprs));
            }
            // Could not infer — fall through to concrete path (mono will handle)
        }
        // Inside generic body: emit with type_args so mono can rename the callee
        // to the concrete struct method name when instantiating.
        {
            std::vector<const LogosType*> type_var_args;
            for (auto& tp : fi.type_params)
                type_var_args.push_back(make_typevar(tp.name));
            return make_expr(fi.ret_type, lir::ECall{mangled, std::move(type_var_args), std::move(arg_exprs)});
        }
    }

    uint64_t n_args = arg_exprs.size();
    if (n_args != fi.param_types.size()) {
        error(std::format("static call '{}': expected {} args, got {}",
              mangled, fi.param_types.size(), n_args));
    } else {
        for (uint64_t i = 0; i < n_args; ++i) {
            auto* at = arg_exprs[i]->type;
            auto* pt = fi.param_types[i];
            if (at->kind != LogosType::Kind::Error &&
                pt->kind != LogosType::Kind::Error &&
                !types_compatible(at, pt))
                error(std::format("static call '{}' arg {}: expected {}, got {}",
                      mangled, i + 1, type_str(pt), type_str(at)));
        }
    }

    return make_expr(fi.ret_type, lir::ECall{mangled, {}, std::move(arg_exprs)});
}

lir::LExprPtr SemaChecker::lower_if_expr(TinyMapView node) {
    lir::LExprPtr cond;
    if (node.has_key(la::COND)) {
        cond = lower_expr(map_of(node.get(la::COND.code)));
        if (cond->type->kind != LogosType::Kind::Bool &&
            cond->type->kind != LogosType::Kind::Error)
            error(std::format("if condition must be bool, got {}", type_str(cond->type)));
    } else {
        cond = error_expr();
    }

    // Both branches must be single-expression blocks (last expr is the value)
    // For simplicity: require both THEN and ELSE branches (else is required for expr form)
    if (!node.has_key(la::ELSE)) {
        error("if-as-expression requires an else branch");
        return error_expr();
    }

    // Lower the last expression from each block
    lir::LExprPtr then_val = error_expr();
    lir::LExprPtr else_val = error_expr();

    auto lower_block_last_expr = [&](TinyMapView blk) -> lir::LExprPtr {
        if (blk.is_null() || !blk.has_key(la::ITEMS)) return error_expr();
        auto stmts = arr_of(blk.get(la::ITEMS.code));
        if (stmts.size() == 0) { error("block-as-expression: empty branch"); return error_expr(); }
        lir::LExprPtr result = nullptr;
        auto block = std::make_unique<lir::LBlock>();
        for (uint64_t i = 0; i < stmts.size(); ++i) {
            auto s = map_of(stmts.get(i));
            if (i == stmts.size() - 1) {
                int32_t lc = code_of(s);
                if (lc == la::EXPR_STMT && s.has_key(la::VALUE)) {
                    result = lower_expr(map_of(s.get(la::VALUE.code)));
                } else if (lc != la::EXPR_STMT && lc != la::LET && lc != la::LET_DESTRUCT && lc != la::RETURN) {
                    result = lower_expr(s);
                } else {
                    block->stmts.push_back(lower_stmt(s));
                }
            } else {
                block->stmts.push_back(lower_stmt(s));
            }
        }
        if (!result) return make_expr(void_t(), lir::EBlockExpr{std::move(block), nullptr});
        const LogosType* rt = result->type;
        return make_expr(rt, lir::EBlockExpr{std::move(block), std::move(result)});
    };

    if (node.has_key(la::THEN)) {
        auto then_node = map_of(node.get(la::THEN.code));
        if (code_of(then_node) == la::BLOCK)
            then_val = lower_block_last_expr(then_node);
        else
            then_val = lower_expr(then_node);
    }

    auto else_node = map_of(node.get(la::ELSE.code));
    if (code_of(else_node) == la::BLOCK)
        else_val = lower_block_last_expr(else_node);
    else
        else_val = lower_expr(else_node);

    // Determine result type: pick the more concrete type when IntLit vs concrete int.
    const LogosType* result_type = then_val->type;
    if (then_val->type->kind == LogosType::Kind::Error)
        result_type = else_val->type;
    else if (else_val->type->kind != LogosType::Kind::Error) {
        if (!types_compatible(then_val->type, else_val->type) &&
            !types_compatible(else_val->type, then_val->type)) {
            error(std::format("if-expression branches have incompatible types: {} vs {}",
                  type_str(then_val->type), type_str(else_val->type)));
        } else {
            result_type = unify_int(then_val->type, else_val->type);
        }
    }
    // If still IntLit, upgrade to i64 if any branch literal overflows i32.
    if (result_type->kind == LogosType::Kind::IntLit) {
        auto intlit_overflow = [](const lir::LExpr* e) -> bool {
            // Look through block expressions to the final result.
            if (auto* blk = std::get_if<lir::EBlockExpr>(&e->kind))
                e = blk->result.get();
            if (!e) return false;
            if (auto* lit = std::get_if<lir::ELitInt>(&e->kind))
                return lit->value > (int64_t)INT32_MAX || lit->value < (int64_t)INT32_MIN;
            return false;
        };
        if (intlit_overflow(then_val.get()) || intlit_overflow(else_val.get()))
            result_type = prim(LogosType::Kind::I64);
    }

    lir::EIfExpr eif;
    eif.cond      = std::move(cond);
    eif.then_val  = std::move(then_val);
    eif.else_val  = std::move(else_val);
    return make_expr(result_type, std::move(eif));
}

lir::LExprPtr SemaChecker::lower_closure_expr(TinyMapView node) {
    auto closure_id = "__closure_" + std::to_string(closure_counter_++);

    // Parse parameters
    std::vector<lir::LParam> params;
    std::vector<const LogosType*> param_types;
    if (node.has_key(la::PARAMS)) {
        AnyVal pav = node.get(la::PARAMS.code);
        if (!pav.is_null() && pav.is_pointer()) {
            auto plist = map_of(pav);
            if (plist.has_key(la::ITEMS)) {
                auto pitems = arr_of(plist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < pitems.size(); ++i) {
                    auto p = map_of(pitems.get(i));
                    auto pname = std::string(str_of(p.get(la::NAME.code)));
                    const LogosType* ptype = p.has_key(la::TYPE)
                        ? resolve_type(map_of(p.get(la::TYPE.code))) : error_t();
                    params.push_back({pname, ptype});
                    param_types.push_back(ptype);
                }
            }
        }
    }

    const LogosType* ret_type = node.has_key(la::RET_TYPE)
        ? resolve_type(map_of(node.get(la::RET_TYPE.code))) : void_t();

    // Push a new scope with closure params
    push_scope();
    for (auto& p : params)
        define(p.name, p.type);

    // Collect current scope variables (for capture detection)
    std::unordered_set<std::string> param_names;
    for (auto& p : params) param_names.insert(p.name);

    // Lower body — closure body is its own unsafe scope, does NOT inherit enclosing context.
    auto saved_ret = ret_type_;
    bool saved_unsafe = inside_unsafe_;
    ret_type_ = ret_type;
    inside_unsafe_ = false;
    lir::LBlock body;
    if (node.has_key(la::BODY)) {
        auto body_node = map_of(node.get(la::BODY.code));
        if (code_of(body_node) == la::BLOCK)
            body = lower_block(body_node);
    }
    ret_type_ = saved_ret;
    inside_unsafe_ = saved_unsafe;
    pop_scope();

    // Capture detection: find variables used in body that are not params
    // and exist in the enclosing scope.
    std::vector<std::string> captures;
    std::vector<const LogosType*> capture_types;
    std::unordered_set<std::string> seen;
    std::function<void(const lir::LExpr&)> scan_captures;
    scan_captures = [&](const lir::LExpr& e) {
        if (auto* vr = std::get_if<lir::EVarRef>(&e.kind)) {
            if (!param_names.count(vr->name) && !seen.count(vr->name)) {
                auto* t = lookup(vr->name);
                if (t) {
                    captures.push_back(vr->name);
                    capture_types.push_back(t);
                    seen.insert(vr->name);
                }
            }
        }
        // Recurse into sub-expressions
        std::visit([&](const auto& k) {
            using K = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<K, lir::EBinOp>) {
                scan_captures(*k.lhs); scan_captures(*k.rhs);
            } else if constexpr (std::is_same_v<K, lir::EUnary>) {
                scan_captures(*k.operand);
            } else if constexpr (std::is_same_v<K, lir::ECall>) {
                for (auto& a : k.args) scan_captures(*a);
            } else if constexpr (std::is_same_v<K, lir::EMethodCall>) {
                scan_captures(*k.receiver);
                for (auto& a : k.args) scan_captures(*a);
            } else if constexpr (std::is_same_v<K, lir::EFieldRead>) {
                scan_captures(*k.receiver);
            } else if constexpr (std::is_same_v<K, lir::EIndexRead>) {
                scan_captures(*k.receiver); scan_captures(*k.index);
            } else if constexpr (std::is_same_v<K, lir::EDeref>) {
                scan_captures(*k.operand);
            } else if constexpr (std::is_same_v<K, lir::ECast>) {
                scan_captures(*k.operand);
            } else if constexpr (std::is_same_v<K, lir::EIfExpr>) {
                scan_captures(*k.cond); scan_captures(*k.then_val); scan_captures(*k.else_val);
            }
        }, e.kind);
    };
    // Scan all statements in body for variable references
    std::function<void(const lir::LBlock&)> scan_block;
    std::function<void(const lir::LStmt&)> scan_stmt;
    scan_block = [&](const lir::LBlock& b) {
        for (auto& s : b.stmts) scan_stmt(s);
    };
    scan_stmt = [&](const lir::LStmt& s) {
        std::visit([&](const auto& k) {
            using K = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<K, lir::SLet>) {
                scan_captures(*k.value);
            } else if constexpr (std::is_same_v<K, lir::SAssign>) {
                scan_captures(*k.value);
            } else if constexpr (std::is_same_v<K, lir::SReturn>) {
                if (k.value) scan_captures(*k.value);
            } else if constexpr (std::is_same_v<K, lir::SIf>) {
                scan_captures(*k.cond); scan_block(*k.then_);
                if (k.else_) scan_block(**k.else_);
            } else if constexpr (std::is_same_v<K, lir::SWhile>) {
                scan_captures(*k.cond); scan_block(*k.body);
            } else if constexpr (std::is_same_v<K, lir::SExprStmt>) {
                scan_captures(*k.expr);
            }
        }, s.kind);
    };
    scan_block(body);

    auto ec = std::make_unique<lir::EClosure>();
    ec->closure_id    = closure_id;
    ec->params        = std::move(params);
    ec->ret_type      = ret_type;
    ec->body          = std::move(body);
    ec->captures      = std::move(captures);
    ec->capture_types = std::move(capture_types);

    auto* ctype = make_closure_type(std::move(param_types), ret_type);
    return make_expr(ctype, lir::EClosureBox{std::move(ec)});
}


} // namespace logos::compiler
