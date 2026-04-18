// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include "sema_impl.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <functional>
#include <map>
#include <unordered_set>

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
        if (!valid_int_literal_format(sv)) {
            error(std::format("malformed integer literal '{}'", sv));
            return error_expr();
        }
        int64_t v = parse_int_literal(sv);
        auto suf = int_suffix_kind(sv);
        const LogosType* t = (suf != LogosType::Kind::Error) ? prim(suf) : intlit_t();
        return make_expr(t, lir::ELitInt{v});
    }
    case la::LIT_FLOAT: {
        auto sv = str_of(expr.get(la::VALUE.code));
        if (!valid_float_literal_format(sv)) {
            error(std::format("malformed float literal '{}'", sv));
            return error_expr();
        }
        std::string s(sv);
        s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
        // Strip optional float suffix before passing to stod
        auto suf = float_suffix_kind(sv);
        if (suf != LogosType::Kind::Error) s.resize(s.size() - 3);
        double v = std::stod(s);
        const LogosType* t = (suf != LogosType::Kind::Error) ? prim(suf)
                                                              : prim(LogosType::Kind::FloatLit);
        return make_expr(t, lir::ELitFloat{v});
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
            // Check if it's a function name — allow coercion to fn(T)->R type.
            auto cands = find_func_candidates(name);
            if (cands.size() == 1) {
                const SemaFuncInfo& fi = *cands[0];
                LogosType ft;
                ft.kind = LogosType::Kind::FnPtr;
                for (auto* pt : fi.param_types)
                    ft.closure_params.push_back(pt);
                ft.closure_ret = fi.ret_type ? fi.ret_type : void_t();
                auto* fn_type = pool_.alloc(std::move(ft));
                return make_expr(fn_type, lir::EVarRef{fi.symbol_name.empty() ? std::string(name) : fi.symbol_name});
            }
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

        // ── Hermes typed container casts: &[T] as <I32>[] → HermesCtr. ──────
        if (target && target->kind == LogosType::Kind::Struct &&
            (target->struct_name == "HermesArr" || target->struct_name == "HermesMap")) {
            if (target->struct_name == "HermesArr") {
                auto* src = inner->type;
                if (!src || src->kind != LogosType::Kind::Slice) {
                    error(std::format(
                        "'as <T>[]' requires a &[T] slice as source; got '{}'",
                        src ? type_str(src) : "?"));
                    return error_expr();
                }
                // Validate element type compatibility.
                // C6-fix3: elem_t must be non-null (resolve_type always sets it for valid types).
                const LogosType* elem_t = !target->type_args.empty()
                    ? target->type_args[0] : nullptr;
                if (!elem_t) {
                    error("internal: <T>[] type missing element type");
                    return error_expr();
                }
                // C6-fix4: src->elem must be non-null; don't skip type check silently.
                if (!src->elem) {
                    error("'as <T>[]': source slice has unresolved element type");
                    return error_expr();
                }
                if (elem_t->kind != src->elem->kind) {
                    error(std::format(
                        "'as <T>[]' element type mismatch: slice has '{}', target needs '{}'",
                        type_str(src->elem), type_str(elem_t)));
                    return error_expr();
                }
                // Pick the stdlib builder function name.
                std::string build_fn;
                if (elem_t->kind == LogosType::Kind::I32)
                    build_fn = "hermes_build_array_i32";
                else if (elem_t->kind == LogosType::Kind::U64)
                    build_fn = "hermes_build_array_u64";
                else {
                    error(std::format("'as <T>[]': unsupported element type '{}'; "
                                      "supported: i32/I32, u64/U64", type_str(elem_t)));
                    return error_expr();
                }
                // Result type: HermesCtr.
                auto* ctr_t = lookup_type_by_name("HermesCtr");
                if (!ctr_t) {
                    LogosType t{};
                    t.kind = LogosType::Kind::Struct;
                    t.struct_name = "HermesCtr";
                    ctr_t = pool_.alloc(std::move(t));
                }
                return make_expr(ctr_t,
                    lir::ECast{std::move(inner), std::move(build_fn)});
            }
            // fix5: explicit guard — outer if allows HermesArr||HermesMap; must be HermesMap here.
            if (target->struct_name != "HermesMap") {
                error("internal: unexpected hermes container type in map cast path");
                return error_expr();
            }
            // HermesMap: source must be MapSliceI32 for <I32,AnyVal>{}.
            {
                auto* src = inner->type;
                const LogosType* key_t = !target->type_args.empty()
                    ? target->type_args[0] : nullptr;
                const LogosType* val_t = target->type_args.size() > 1
                    ? target->type_args[1] : nullptr;
                if (!key_t || !val_t) {
                    error("internal: <K,V>{} type missing key/val types");
                    return error_expr();
                }
                std::string map_fn;
                if (key_t->kind == LogosType::Kind::I32 &&
                    val_t->kind == LogosType::Kind::Struct &&
                    val_t->struct_name == "AnyVal") {
                    if (!src || src->kind != LogosType::Kind::Struct ||
                        src->struct_name != "MapSliceI32") {
                        error(std::format(
                            "'as <I32,AnyVal>{{}}' requires a MapSliceI32 as source; got '{}'",
                            src ? type_str(src) : "?"));
                        return error_expr();
                    }
                    map_fn = "hermes_build_map_i32_anyval";
                } else {
                    // fix4: guard against calling type_str on error types — check kind first.
                    auto key_str = (key_t->kind != LogosType::Kind::Error) ? type_str(key_t) : "?";
                    auto val_str = (val_t->kind != LogosType::Kind::Error) ? type_str(val_t) : "?";
                    error(std::format(
                        "'as <{},{}>{{}}': unsupported combination; supported: <I32,AnyVal>",
                        key_str, val_str));
                    return error_expr();
                }
                auto* ctr_t = lookup_type_by_name("HermesCtr");
                if (!ctr_t) {
                    LogosType t{};
                    t.kind = LogosType::Kind::Struct;
                    t.struct_name = "HermesCtr";
                    ctr_t = pool_.alloc(std::move(t));
                }
                return make_expr(ctr_t,
                    lir::ECast{std::move(inner), std::move(map_fn)});
            }
        }

        // ── Ordinary numeric/pointer cast. ────────────────────────────────────
        if (inner->type && target &&
            inner->type->kind != LogosType::Kind::Error &&
            target->kind != LogosType::Kind::Error) {
            bool src_agg = inner->type->kind == LogosType::Kind::Struct ||
                           inner->type->kind == LogosType::Kind::Array  ||
                           inner->type->kind == LogosType::Kind::Tuple  ||
                           inner->type->kind == LogosType::Kind::Enum;
            bool tgt_scalar = target->kind == LogosType::Kind::I32  ||
                              target->kind == LogosType::Kind::I64  ||
                              target->kind == LogosType::Kind::U8   ||
                              target->kind == LogosType::Kind::I8   ||
                              target->kind == LogosType::Kind::I16  ||
                              target->kind == LogosType::Kind::U16  ||
                              target->kind == LogosType::Kind::I24  ||
                              target->kind == LogosType::Kind::I56  ||
                              target->kind == LogosType::Kind::U24  ||
                              target->kind == LogosType::Kind::U56  ||
                              target->kind == LogosType::Kind::U32  ||
                              target->kind == LogosType::Kind::U64  ||
                              target->kind == LogosType::Kind::I128 ||
                              target->kind == LogosType::Kind::U128 ||
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
        if (code_of(child) == la::VAR_REF) {
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
        // &mut <expr> — temporary materialization
        auto inner = lower_expr(child);
        if (inner->type->kind == LogosType::Kind::Error) return error_expr();
        return make_expr(make_ref(true, inner->type),
            lir::EAddrOfTemp{std::move(inner), true});
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
    case la::HERMES_MAP:
    case la::HERMES_ARRAY:
    case la::HERMES_TYPED_ARRAY:
    case la::HERMES_TYPED_MAP:
    case la::HERMES_NEG_INT:
    case la::HERMES_STR:
    case la::HERMES_INT:
    case la::HERMES_FLOAT:
    case la::HERMES_BOOL:
    case la::HERMES_NULL:  return lower_hermes_lit(expr);
    // C1 bug fix: $-capture nodes must not appear as standalone expressions;
    // they are only valid inside hermes_val (within lower_hermes_val).
    case la::HERMES_CAP_IDENT:
    case la::HERMES_CAP_EXPR:
        error("$-capture is not valid as a standalone expression");
        return error_expr();
    case la::ENUM_LIT:    return lower_enum_lit(expr);
    case la::ENUM_LIT_DATA: return lower_enum_lit_data(expr);
    case la::IF:          return lower_if_expr(expr);
    case la::MATCH:       return lower_match_expr(expr);
    case la::CLOSURE_EXPR: return lower_closure_expr(expr);

    case la::LOOP: {
        // loop { ... } used as an expression — only valid when all break paths carry a value.
        auto loop_stmt = lower_loop(expr);
        const LogosType* result_type = nullptr;
        std::string break_slot;
        if (auto* sl = std::get_if<lir::SLoop>(&loop_stmt.kind)) {
            result_type = sl->result_type;
            break_slot  = sl->break_slot;
        }
        if (!result_type || break_slot.empty()) {
            // loop never yields — treat as void (infinite loop used as stmt-expr)
            auto block = std::make_unique<lir::LBlock>();
            block->stmts.push_back(std::move(loop_stmt));
            return make_expr(void_t(), lir::EBlockExpr{std::move(block), nullptr});
        }
        // Wrap: { loop { ... }; __loop_val }
        // gen_loop allocates the break slot alloca and registers it in scope_;
        // we just read it back via EVarRef after the loop exits.
        auto block = std::make_unique<lir::LBlock>();
        block->stmts.push_back(std::move(loop_stmt));
        auto slot_ref = make_expr(result_type, lir::EVarRef{break_slot});
        return make_expr(result_type, lir::EBlockExpr{std::move(block), std::move(slot_ref)});
    }

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
        if (!expr.has_key(la::ITEMS))
            return make_expr(void_t(), lir::ETupleLit{});  // () — unit value
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
        // Auto-deref: &(T) and &mut (T) -> use pointee type for index lookup
        const LogosType* recv_tuple_type = recv->type;
        if (is_ref_like(recv->type->kind) && recv->type->pointee &&
            recv->type->pointee->kind == LogosType::Kind::Tuple) {
            recv_tuple_type = recv->type->pointee;
        }
        if (recv_tuple_type->kind != LogosType::Kind::Tuple) {
            error(std::format("tuple index on non-tuple type '{}'", type_str(recv->type)));
            return error_expr();
        }
        auto sv = str_of(expr.get(la::FIELD.code));
        uint32_t idx = (uint32_t)parse_int_literal(sv);
        if (idx >= recv_tuple_type->tuple_elems.size()) {
            error(std::format("tuple index {} out of range (tuple has {} elements)",
                  idx, recv_tuple_type->tuple_elems.size()));
            return error_expr();
        }
        auto* elem_t = recv_tuple_type->tuple_elems[idx];
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

    // Operator overloading: if LHS is a struct, desugar to trait method call.
    if (lt->kind == LogosType::Kind::Struct) {
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
            auto type_name = concrete_struct_name(lt);
            auto mangled = type_name + "__" + method_name;
            auto fit = find_func_by_base_and_signature(mangled, {lt, rt}, false);
            if (fit) {
                std::vector<lir::LExprPtr> args;
                args.push_back(std::move(lhs));
                args.push_back(std::move(rhs));
                return make_expr(fit->ret_type,
                    lir::ECall{fit->symbol_name.empty() ? mangled : fit->symbol_name, {}, std::move(args)});
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
        if (ptr_null_cmp) {
            const lir::LExpr* lit_expr = (lt->kind == LogosType::Kind::IntLit) ? lhs.get() : rhs.get();
            if (auto v = get_intlit_value(lit_expr)) {
                if (*v != 0)
                    error(std::format(
                        "operator '{}': pointer can only be compared with integer literal 0", op));
            }
        }
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
            // FloatLit/IntLit on LHS defers to the concrete type on RHS (e.g. 1.0 + x_f32 → f32).
            else result_type = unify_numeric(lt, rt);
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
        if (code_of(child) == la::VAR_REF) {
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
        // &<expr> — temporary materialization: spill rvalue to stack
        auto inner = lower_expr(child);
        if (inner->type->kind == LogosType::Kind::Error) return error_expr();
        return make_expr(make_ref(false, inner->type),
            lir::EAddrOfTemp{std::move(inner), false});
    }

    auto operand = lower_expr(map_of(node.get(la::VALUE.code)));
    auto* vt = operand->type;
    if (vt->kind == LogosType::Kind::Error)
        return make_expr(error_t(), lir::EUnary{std::string(op), std::move(operand)});

    // Unary operator overloading for struct types
    if (vt->kind == LogosType::Kind::Struct) {
        std::string trait_name, method_name;
        if      (op == "-") { trait_name = "Neg"; method_name = "neg"; }
        else if (op == "!") { trait_name = "Not"; method_name = "not_"; }
        if (!trait_name.empty()) {
            auto type_name = concrete_struct_name(vt);
            auto mangled = type_name + "__" + method_name;
            auto fit = find_func_by_base_and_signature(mangled, {vt}, false);
            if (fit) {
                std::vector<lir::LExprPtr> args;
                args.push_back(std::move(operand));
                return make_expr(fit->ret_type,
                    lir::ECall{fit->symbol_name.empty() ? mangled : fit->symbol_name, {}, std::move(args)});
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

    // Check if callee is a closure or fn-ptr variable
    auto* callee_type = lookup(callee);
    bool is_closure = callee_type && callee_type->kind == LogosType::Kind::Closure;
    bool is_fn_ptr  = callee_type && callee_type->kind == LogosType::Kind::FnPtr;
    if (is_closure || is_fn_ptr) {
        std::vector<lir::LExprPtr> arg_exprs;
        if (node.has_key(la::ARGS)) {
            auto args = arr_of(node.get(la::ARGS.code));
            for (uint64_t i = 0; i < args.size(); ++i)
                arg_exprs.push_back(lower_expr(map_of(args.get(i))));
        }
        uint64_t n_args = arg_exprs.size();
        uint64_t n_params = callee_type->closure_params.size();
        const char* kind_str = is_fn_ptr ? "fn-ptr call" : "closure call";
        if (n_args != n_params) {
            error(std::format("{}: expected {} args, got {}", kind_str, n_params, n_args));
        } else {
            for (uint64_t i = 0; i < n_args; ++i) {
                auto* at = arg_exprs[i]->type;
                auto* pt = callee_type->closure_params[i];
                if (at->kind != LogosType::Kind::Error &&
                    pt->kind != LogosType::Kind::Error &&
                    !types_compatible(at, pt)) {
                    error(std::format("{} arg {}: expected {}, got {}",
                          kind_str, i + 1, type_str(pt), type_str(at)));
                }
            }
        }
        auto callee_expr = make_expr(callee_type, lir::EVarRef{std::string(callee)});
        const LogosType* ret = callee_type->closure_ret ? callee_type->closure_ret : void_t();
        if (is_fn_ptr)
            return make_expr(ret, lir::EFnPtrCall{std::move(callee_expr), std::move(arg_exprs)});
        return make_expr(ret, lir::EClosureCall{std::move(callee_expr), std::move(arg_exprs)});
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
    uint64_t n_args = arg_exprs.size();

    bool call_has_pack_expand = false;
    for (auto& a : arg_exprs) {
        if (std::holds_alternative<lir::EPackExpand>(a->kind)) {
            call_has_pack_expand = true;
            break;
        }
    }

    if (const SemaFuncInfo* exact_fi = resolve_function_call(callee, arg_exprs, false, false);
        exact_fi && exact_fi->type_params.empty() && !call_has_pack_expand) {
        check_pub_access(exact_fi->is_pub, exact_fi->package, callee);
        if (exact_fi->is_unsafe && !inside_unsafe_)
            error(std::format("call to unsafe function '{}' requires unsafe context", callee));
        if (exact_fi->is_vararg) {
            if (n_args < exact_fi->param_types.size()) {
                error(std::format("call to vararg '{}': expected at least {} args, got {}",
                      callee, exact_fi->param_types.size(), n_args));
            } else {
                for (uint64_t i = 0; i < exact_fi->param_types.size(); ++i) {
                    auto* at = arg_exprs[i]->type;
                    auto* pt = exact_fi->param_types[i];
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
        } else if (n_args != exact_fi->param_types.size()) {
            error(std::format("call to '{}': expected {} args, got {}",
                  callee, exact_fi->param_types.size(), n_args));
        } else {
            for (uint64_t i = 0; i < n_args; ++i) {
                auto* at = arg_exprs[i]->type;
                auto* pt = exact_fi->param_types[i];
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

        for (auto& a : arg_exprs) {
            if (is_move_type(a->type)) {
                if (auto* vr = std::get_if<lir::EVarRef>(&a->kind))
                    mark_moved(vr->name);
            }
        }
        return make_expr(exact_fi->ret_type,
            lir::ECall{exact_fi->symbol_name.empty() ? std::string(callee) : exact_fi->symbol_name,
                       {}, std::move(arg_exprs)});
    }

    auto fit  = funcs_.find(std::string(callee));
    auto all_cands = find_func_candidates(callee);
    auto git  = find_generic_func(callee, n_args);

    // Resolve the "best" SemaFuncInfo to try.
    // Priority:
    //   1. generic_funcs_ (variadic overload, or overloaded name) if callee is there
    //   2. funcs_ with non-empty type_params (plain generic fn, stored in funcs_)
    //   3. funcs_ with empty type_params (concrete fn)
    // If the function is generic (by either map or non-empty type_params in funcs_),
    // try to infer type args from the actual argument types.

    // Identify which entry to use
    const SemaFuncInfo* infer_fi = nullptr;
    const SemaFuncInfo* fi_sel = (fit != funcs_.end()) ? &fit->second : git;
    if (!fi_sel) {
        if (!all_cands.empty()) {
            return make_expr(error_t(), lir::ECall{std::string(callee), {}, std::move(arg_exprs)});
        }
        error(std::format("call to undefined function '{}'", callee));
        return make_expr(error_t(), lir::ECall{std::string(callee), {}, std::move(arg_exprs)});
    }
    // Pub check and unsafe check.
    {
        const SemaFuncInfo* fi_chk = fi_sel;
        check_pub_access(fi_chk->is_pub, fi_chk->package, callee);
        if (fi_chk->is_unsafe && !inside_unsafe_)
            error(std::format("call to unsafe function '{}' requires unsafe context", callee));
    }

    // Determine if we should try inference
    bool try_inference = false;
    if (fit == funcs_.end()) {
        // Only generic overload(s)
        infer_fi = git;
        try_inference = true;
    } else if (!fit->second.type_params.empty()) {
        // In funcs_ but is a generic function (no non-generic overload exists)
        infer_fi = &fit->second;
        try_inference = true;
    } else if (git) {
        // Non-generic in funcs_, generic overload in generic_funcs_.
        // Try generic when arity doesn't match the non-generic.
        bool arity_ok = fit->second.is_vararg
            ? n_args >= fit->second.param_types.size()
            : n_args == fit->second.param_types.size();
        if (!arity_ok) {
            infer_fi = git;
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
                return finish_generic_call(
                    infer_fi->symbol_name.empty() ? callee : infer_fi->symbol_name,
                    *infer_fi, std::move(inferred), std::move(arg_exprs));
            error(std::format("call to '{}': could not infer all type arguments — use explicit f::<T>(...) syntax", callee));
            return make_expr(error_t(), lir::ECall{std::string(callee), {}, std::move(arg_exprs)});
        }
        // In generic/pack context inference is deferred to mono, but we still must
        // route to the generic overload (if available) rather than pinning a concrete one.
        fi_sel = infer_fi;
        // Fall through to non-generic path (mono will handle instantiation)
    }

    // Non-generic path
    auto& fi = *fi_sel;

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

    // Inside generic context (inference deferred): preserve generic call shape
    // so mono can instantiate and rewrite callee names correctly.
    if (!fi.type_params.empty()) {
        bool has_variadic_tp = !fi.type_params.empty() && fi.type_params.back().is_variadic;
        if (has_variadic_tp && has_pack_expand) {
            return make_expr(fi.ret_type, lir::ECall{
                fi.symbol_name.empty() ? std::string(callee) : fi.symbol_name,
                {}, std::move(arg_exprs)});
        }
        std::vector<const LogosType*> type_var_args;
        for (auto& tp : fi.type_params)
            type_var_args.push_back(make_typevar(tp.name));
        SemaSubst subst;
        for (size_t i = 0; i < fi.type_params.size() && i < type_var_args.size(); ++i)
            subst[fi.type_params[i].name] = type_var_args[i];
        const LogosType* ret = subst_type_sema(fi.ret_type, subst);
        return make_expr(ret, lir::ECall{
            fi.symbol_name.empty() ? std::string(callee) : fi.symbol_name,
            std::move(type_var_args), std::move(arg_exprs)});
    }

    return make_expr(fi.ret_type, lir::ECall{std::string(callee), {}, std::move(arg_exprs)});
}

void SemaChecker::unify_types(const LogosType* formal, const LogosType* actual,
                     std::unordered_map<std::string, const LogosType*>& bindings) {
    if (!formal || !actual) return;
    if (actual->kind == LogosType::Kind::Error ||
        formal->kind == LogosType::Kind::Error) return;

    // Widen IntLit to i32 / FloatLit to f64 before any binding
    const LogosType* actual_norm = actual;
    if (actual->kind == LogosType::Kind::IntLit)
        actual_norm = prim(LogosType::Kind::I32);
    else if (actual->kind == LogosType::Kind::FloatLit)
        actual_norm = prim(LogosType::Kind::F64);

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
            else if (t->kind == LogosType::Kind::FloatLit)
                t = prim(LogosType::Kind::F64);
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
    std::string callee_diag = callee;
    if (auto p = callee_diag.find("__g__"); p != std::string::npos)
        callee_diag.resize(p);
    else if (auto p = callee_diag.find("__f__"); p != std::string::npos)
        callee_diag.resize(p);
    // Unsafe check: covers both inferred (lower_call) and explicit (lower_generic_call) paths.
    if (fi.is_unsafe && !inside_unsafe_)
        error(std::format("call to unsafe function '{}' requires unsafe context", callee_diag));
    bool has_variadic = !fi.type_params.empty() && fi.type_params.back().is_variadic;
    size_t non_variadic_count = fi.type_params.size() - (has_variadic ? 1 : 0);

    // Validate type arg count
    if (!fi.type_params.empty()) {
        if (has_variadic) {
                if (type_args.size() < non_variadic_count)
                    error(std::format("call to '{}': expected at least {} type arg(s), got {}",
                      callee_diag, non_variadic_count, type_args.size()));
        } else if (type_args.size() != fi.type_params.size()) {
            error(std::format("call to '{}': expected {} type arg(s), got {}",
                  callee_diag, fi.type_params.size(), type_args.size()));
        }
    }

    // Build substitution map for non-variadic type params
    std::unordered_map<std::string, const LogosType*> subst;
    for (size_t i = 0; i < non_variadic_count && i < type_args.size(); ++i)
        subst[fi.type_params[i].name] = type_args[i];

    // Validate trait bounds for all type params (including variadic pack elements)
    check_type_bounds(callee_diag, fi.type_params, type_args);

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
                  callee_diag, fixed_params, n_args));
        for (uint64_t i = 0; i < fixed_params && i < n_args; ++i) {
            auto* at = arg_exprs[i]->type;
            auto* pt = subst_type_sema(fi.param_types[i], subst);
            if (at->kind != LogosType::Kind::Error &&
                pt->kind != LogosType::Kind::Error &&
                pt->kind != LogosType::Kind::TypeVar &&
                !types_compatible(at, pt))
                error(std::format("call to '{}' arg {}: expected {}, got {}",
                      callee_diag, i + 1, type_str(pt), type_str(at)));
            if (at->kind == LogosType::Kind::IntLit && pt->kind != LogosType::Kind::Error &&
                pt->kind != LogosType::Kind::TypeVar)
                if (auto v = get_intlit_value(arg_exprs[i].get()))
                    if (!intlit_fits(*v, pt->kind))
                        error(std::format("call to '{}' arg {}: value {} does not fit in {}",
                              callee_diag, i + 1, *v, type_str(pt)));
        }
    } else {
        if (n_args != fi.param_types.size()) {
            error(std::format("call to '{}': expected {} args, got {}",
                  callee_diag, fi.param_types.size(), n_args));
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
                          callee_diag, i + 1, type_str(pt), type_str(at)));
                if (at->kind == LogosType::Kind::IntLit && pt->kind != LogosType::Kind::Error &&
                    pt->kind != LogosType::Kind::TypeVar)
                    if (auto v = get_intlit_value(arg_exprs[i].get()))
                        if (!intlit_fits(*v, pt->kind))
                            error(std::format("call to '{}' arg {}: value {} does not fit in {}",
                                  callee_diag, i + 1, *v, type_str(pt)));
                // Check array literal elements against narrow array param type.
                if (at->kind == LogosType::Kind::Array && pt->kind == LogosType::Kind::Array && pt->elem)
                    if (auto* al = std::get_if<lir::EArrLit>(&arg_exprs[i]->kind))
                        for (size_t ei = 0; ei < al->elems.size(); ++ei)
                            if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(al->elems[ei].get()))
                                    if (!intlit_fits(*v, pt->elem->kind))
                                        error(std::format("call to '{}' arg {}: array element {}: value {} does not fit in {}",
                                              callee_diag, i + 1, ei, *v, type_str(pt->elem)));
                // Check tuple literal elements against narrow tuple param element types.
                if (at->kind == LogosType::Kind::Tuple && pt->kind == LogosType::Kind::Tuple)
                    if (auto* tl = std::get_if<lir::ETupleLit>(&arg_exprs[i]->kind))
                        for (size_t ei = 0; ei < tl->elems.size() && ei < pt->tuple_elems.size(); ++ei) {
                            if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(tl->elems[ei].get()))
                                    if (pt->tuple_elems[ei] && !intlit_fits(*v, pt->tuple_elems[ei]->kind))
                                        error(std::format("call to '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                              callee_diag, i + 1, ei, *v, type_str(pt->tuple_elems[ei])));
                            if (pt->tuple_elems[ei] && pt->tuple_elems[ei]->kind == LogosType::Kind::Array &&
                                pt->tuple_elems[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                                if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                        if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(ial->elems[ii].get()))
                                                if (!intlit_fits(*v, pt->tuple_elems[ei]->elem->kind))
                                                    error(std::format("call to '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                          callee_diag, i + 1, ei, ii, *v, type_str(pt->tuple_elems[ei]->elem)));
                            if (pt->tuple_elems[ei] && pt->tuple_elems[ei]->kind == LogosType::Kind::Tuple &&
                                tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                                if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < itl->elems.size() && ii < pt->tuple_elems[ei]->tuple_elems.size(); ++ii)
                                        if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(itl->elems[ii].get()))
                                                if (pt->tuple_elems[ei]->tuple_elems[ii] && !intlit_fits(*v, pt->tuple_elems[ei]->tuple_elems[ii]->kind))
                                                    error(std::format("call to '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                          callee_diag, i + 1, ei, ii, *v, type_str(pt->tuple_elems[ei]->tuple_elems[ii])));
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

    // type_code_of::<T>() — returns the Hermes type_code of a concrete datatype as u64.
    // For concrete (non-generic) datatypes: SHA-256 of "package::Name" truncated to 56 bits,
    // shifted to >= 128 if needed (codes 1-127 are reserved for inline AnyVal).
    // For non-datatype T: returns 0.
    if (callee == "type_code_of") {
        const LogosType* elem = nullptr;
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                if (items.size() == 1)
                    elem = resolve_type(map_of(items.get(0)));
            }
        }
        if (!elem) {
            error("type_code_of::<T>() requires exactly one type argument");
            return error_expr();
        }
        uint64_t code = 0;
        if (elem->kind == LogosType::Kind::Datatype && elem->type_args.empty()) {
            // Resolve package for this type (needed for both explicit and auto-hash paths).
            auto dit = datatypes_.find(elem->struct_name);
            std::string pkg;
            if (dit != datatypes_.end()) {
                pkg = dit->second.package;
            } else {
                auto sit = structs_.find(elem->struct_name);
                if (sit != structs_.end()) pkg = sit->second.package;
                // If still not found, fall back to current package.  This handles
                // types in the same file that haven't been registered yet.
                else pkg = cur_package_;
            }
            // Build the fully-qualified name used as explicit_type_codes_ key (matches
            // the key written by apply_annots_to_struct in sema.cpp).
            std::string fqn = pkg.empty() ? elem->struct_name : pkg + "::" + elem->struct_name;

            // Check for explicit #[type_code=N] annotation first.
            auto eit = explicit_type_codes_.find(fqn);
            if (eit != explicit_type_codes_.end()) {
                code = eit->second;
            } else {
                std::string canon = pkg + "::" + elem->struct_name;
                auto hash = type_hash_23(canon);
                uint64_t raw = type_hash_56bit(hash);
                code = (raw < 128) ? (raw + 128) : raw;
            }
        } else if (elem->kind == LogosType::Kind::Datatype && !elem->type_args.empty()) {
            // If any type_arg is a TypeVar, defer resolution to mono so every
            // instantiation of the surrounding generic function gets its own
            // concrete type_code.  Otherwise (fully concrete), compute now.
            bool has_tv = false;
            for (auto* a : elem->type_args)
                if (a && a->kind == LogosType::Kind::TypeVar) { has_tv = true; break; }
            if (has_tv)
                return make_expr(prim(LogosType::Kind::U64), lir::ETypeCodeOf{elem});
            auto dit = datatypes_.find(elem->struct_name);
            std::string pkg = (dit != datatypes_.end()) ? dit->second.package : std::string(cur_package_);
            std::string canon = pkg + "::" + type_str(elem);
            auto eit = explicit_type_codes_.find(canon);
            if (eit != explicit_type_codes_.end()) {
                code = eit->second;
            } else {
                auto hash = type_hash_23(canon);
                uint64_t raw = type_hash_56bit(hash);
                code = (raw < 128) ? (raw + 128) : raw;
            }
        } else if (elem->kind == LogosType::Kind::TypeVar) {
            // `type_code_of::<T>()` inside a generic fn — defer.
            return make_expr(prim(LogosType::Kind::U64), lir::ETypeCodeOf{elem});
        }
        return make_expr(prim(LogosType::Kind::U64), lir::ELitInt{(int64_t)code});
    }

    // is_data_plain_of::<T>() — returns true (1) if T is a DataPlain datatype
    // (no relative-pointer fields), false (0) otherwise.
    // Non-datatype types always return true (scalars, structs, etc. are copyable).
    if (callee == "is_data_plain_of") {
        const LogosType* elem = nullptr;
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                if (items.size() == 1)
                    elem = resolve_type(map_of(items.get(0)));
            }
        }
        if (!elem) {
            error("is_data_plain_of::<T>() requires exactly one type argument");
            return error_expr();
        }
        // Bug 4 fix: strip Array wrapper so [DataNode; N] → DataNode correctly.
        const LogosType* check = elem;
        while (check && check->kind == LogosType::Kind::Array) check = check->elem;
        bool is_plain = true;
        if (check && check->kind == LogosType::Kind::Datatype && check->type_args.empty()) {
            auto dit = datatypes_.find(check->struct_name);
            if (dit != datatypes_.end()) {
                is_plain = dit->second.is_data_plain;
            } else {
                // Bug 1 fix: cross-package datatypes that landed in structs_ also carry
                // is_data_plain (same SemaStructInfo type).
                auto sit = structs_.find(check->struct_name);
                if (sit != structs_.end()) is_plain = sit->second.is_data_plain;
                // If not found in either map, default to true (unknown type → conservative safe).
            }
        } else if (check && check->kind == LogosType::Kind::Datatype && !check->type_args.empty()) {
            // Generic Datatype: can't determine statically → conservative DataNode.
            is_plain = false;
        }
        return make_expr(prim(LogosType::Kind::Bool), lir::ELitInt{is_plain ? 1LL : 0LL});
    }

    size_t n_args = 0;
    if (node.has_key(la::ARGS)) {
        AnyVal args_av = node.get(la::ARGS.code);
        if (!args_av.is_null()) {
            auto args_list = map_of(args_av);
            if (args_list.has_key(la::ITEMS))
                n_args = arr_of(args_list.get(la::ITEMS.code)).size();
        }
    }

    // Prefer the generic overload (for variadic base case overloading)
    SemaFuncInfo* fi_ptr = nullptr;
    {
        auto git = find_generic_func(callee, n_args);
        if (git) fi_ptr = const_cast<SemaFuncInfo*>(git);
        else if (auto cands = find_func_candidates(callee); cands.size() == 1)
            fi_ptr = const_cast<SemaFuncInfo*>(cands[0]);
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

    return finish_generic_call(
        fi_ptr->symbol_name.empty() ? callee : fi_ptr->symbol_name,
        *fi_ptr, std::move(type_args), std::move(arg_exprs));
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

    // &tagged<TS> Trait method call: tag-based dispatch through the tier-1 table.
    // The receiver is a thin *const u8 pointer; type_code is read at runtime via TS.
    if (recv->type->kind == LogosType::Kind::TaggedPtr) {
        auto& ts_name  = recv->type->struct_name;  // e.g. "DataTypeTagSystem"
        auto& tname    = recv->type->trait_name;   // e.g. "Stringify"
        auto tit = traits_.find(tname);
        if (tit == traits_.end()) {
            error(std::format("&tagged<{}> {}: trait '{}' not found", ts_name, tname, tname));
            return error_expr();
        }
        for (size_t mi = 0; mi < tit->second.methods.size(); ++mi) {
            auto& m = tit->second.methods[mi];
            if (m.name != method_name) continue;
            if (m.is_unsafe && !inside_unsafe_)
                error(std::format("call to unsafe method '{}' requires unsafe context",
                                  std::string(method_name)));
            std::vector<lir::LExprPtr> arg_exprs;
            if (node.has_key(la::ARGS)) {
                auto args_node = arr_of(node.get(la::ARGS.code));
                for (uint64_t i = 0; i < args_node.size(); ++i)
                    arg_exprs.push_back(lower_expr(map_of(args_node.get(i))));
            }
            size_t expected_explicit = m.param_types.size() > 0
                ? m.param_types.size() - 1 : 0;
            if (arg_exprs.size() != expected_explicit)
                error(std::format("method '{}': expected {} args, got {}",
                                  std::string(method_name), expected_explicit, arg_exprs.size()));
            // Return type: substitute Self → &tagged<TS> Trait (the receiver type).
            const LogosType* ret_type = m.ret_type;
            if (ret_type && ret_type->kind == LogosType::Kind::TypeVar &&
                ret_type->type_var_name == "Self")
                ret_type = recv->type;
            lir::EMethodCall mc;
            mc.receiver    = std::move(recv);
            mc.method      = std::string(method_name);
            mc.args        = std::move(arg_exprs);
            mc.vtable_index = -1;
            mc.tag_system  = std::string(ts_name);
            mc.tag_trait   = std::string(tname);
            return make_expr(ret_type, std::move(mc));
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

        // Helper: depth-first search over the supertrait DAG for the method.
        // visited prevents infinite loops on circular supertrait definitions (Bug 2).
        // The !chosen_method guard is NOT used so all supertrait siblings are searched
        // for ambiguity detection (Bug 1 fix: e.g. trait Foo: A+B where both define m()).
        std::unordered_set<std::string> st_visited;
        std::function<void(const std::string&)> search_trait = [&](const std::string& tname) {
            if (!st_visited.insert(tname).second) return;  // cycle / diamond guard (Bug 2)
            auto tit = traits_.find(tname);
            if (tit == traits_.end()) return;
            for (auto& m : tit->second.methods) {
                if (m.name != method_name) continue;
                if (chosen_method && chosen_trait != tname)
                    error(std::format(
                        "method '{}' is ambiguous for type parameter '{}' (matches traits '{}' and '{}')",
                        std::string(method_name), recv_inner->type_var_name, chosen_trait, tname));
                chosen_method = &m;
                chosen_trait  = tname;
                return;  // method found in this trait; don't recurse further
            }
            // Not found directly — search all supertraits (no early-exit guard, Bug 1 fix).
            for (auto& super : tit->second.supertraits)
                search_trait(super.trait_name);
        };

        if (bit != current_type_bounds_.end()) {
            for (auto& bound : bit->second)
                search_trait(bound.trait_name);
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
            // Substitute trait type params from the bound: e.g. T: Into<i32> → Into::T = i32
            {
                auto bit2 = current_type_bounds_.find(recv_inner->type_var_name);
                if (bit2 != current_type_bounds_.end()) {
                    for (auto& bound : bit2->second) {
                        if (bound.trait_name != chosen_trait) continue;
                        auto tit = traits_.find(bound.trait_name);
                        if (tit == traits_.end()) break;
                        // Map each trait type param name to the bound's type arg
                        for (size_t ti = 0; ti < tit->second.type_params.size() &&
                                            ti < bound.type_args.size(); ++ti) {
                            if (bound.type_args[ti])
                                self_subst[tit->second.type_params[ti].name] = bound.type_args[ti];
                        }
                        break;
                    }
                }
            }
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
            lir::EMethodCall{std::move(recv), std::string(method_name), "", {}, std::move(arg_exprs), -1});
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
            {
                std::vector<const LogosType*> types;
                types.push_back(recv->type);
                for (auto& a : arg_exprs) types.push_back(a->type);
                if (auto fit = find_func_by_base_and_signature(generic_key, types, false))
                    fi_ptr = fit;
                else if (auto git = find_generic_func(generic_key))
                    fi_ptr = git;
            }

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
                std::string callee_name = concrete_mangled;
                if (!fi_ptr->symbol_name.empty()) {
                    std::string enum_prefix = base + "__";
                    if (fi_ptr->symbol_name.rfind(enum_prefix, 0) == 0)
                        callee_name = concrete_enum + fi_ptr->symbol_name.substr(base.size());
                    else
                        callee_name = fi_ptr->symbol_name;
                }
                std::vector<lir::LExprPtr> pargs;
                pargs.push_back(std::move(recv));
                for (auto& a : arg_exprs) pargs.push_back(std::move(a));
                return make_expr(ret, lir::ECall{callee_name, {}, std::move(pargs)});
            }
        }
        auto tname = type_str(recv->type);
        auto mangled_prim = tname + "__" + std::string(method_name);
        const SemaFuncInfo* fi_ptr = nullptr;
        {
            std::vector<const LogosType*> types;
            types.push_back(recv->type);
            for (auto& a : arg_exprs) types.push_back(a->type);
            if (auto pfit = find_func_by_base_and_signature(mangled_prim, types, false))
                fi_ptr = pfit;
            else if (auto pfit = find_generic_func(mangled_prim))
                fi_ptr = pfit;
        }

        if (fi_ptr) {
            if (fi_ptr->is_unsafe && !inside_unsafe_)
                error(std::format("call to unsafe method '{}' requires unsafe context", mangled_prim));
            std::vector<lir::LExprPtr> pargs;
            pargs.push_back(std::move(recv));
            for (auto& a : arg_exprs) pargs.push_back(std::move(a));
            return make_expr(fi_ptr->ret_type,
                lir::ECall{fi_ptr->symbol_name.empty() ? mangled_prim : fi_ptr->symbol_name,
                           {}, std::move(pargs)});
        }
        error(std::format("method call: receiver is not a struct (got {})",
              type_str(recv->type)));
        return make_expr(error_t(),
            lir::EMethodCall{std::move(recv), std::string(method_name), "", {}, std::move(arg_exprs), -1});
    }

    auto mangled = std::string(sname) + "__" + std::string(method_name);
    const SemaFuncInfo* fi_ptr = nullptr;
    bool auto_ref_recv = false;
    bool auto_ref_mut = false;
    SemaSubst recv_struct_subst;
    {
        const LogosType* rst = recv->type;
        if (rst && rst->kind == LogosType::Kind::Ptr && rst->pointee) {
            rst = rst->pointee;
        } else if (rst && is_ref_like(rst->kind) && rst->pointee) {
            rst = rst->pointee;
        }
        if ((rst->kind == LogosType::Kind::Struct || rst->kind == LogosType::Kind::Datatype) &&
            !rst->type_args.empty()) {
            SemaStructInfo* si2 = nullptr;
            { auto it = structs_.find(rst->struct_name); if (it != structs_.end()) si2 = &it->second; }
            if (!si2) { auto it = datatypes_.find(rst->struct_name); if (it != datatypes_.end()) si2 = &it->second; }
            if (si2) {
                auto& tps = si2->type_params;
                for (size_t i = 0; i < tps.size() && i < rst->type_args.size(); ++i)
                    recv_struct_subst[tps[i].name] = rst->type_args[i];
            }
        }
    }
    {
        std::vector<const LogosType*> types;
        types.push_back(recv->type);
        for (auto& a : arg_exprs) types.push_back(a->type);
        for (auto* cand : find_func_candidates(mangled)) {
            if (!cand || !cand->type_params.empty()) continue;
            if (cand->param_types.size() != types.size()) continue;
            bool ok = true;
            bool needs_ref = false;
            bool needs_mut = false;
            auto* formal0 = cand->param_types[0];
            if (!recv_struct_subst.empty())
                formal0 = subst_type_sema(formal0, recv_struct_subst);
            auto* actual0 = recv->type;
            if (actual0 && formal0 && !types_equal(*actual0, *formal0)) {
                if (actual0->kind != LogosType::Kind::Ref &&
                    actual0->kind != LogosType::Kind::MutRef &&
                    actual0->kind != LogosType::Kind::Ptr &&
                    is_ref_like(formal0->kind) && formal0->pointee &&
                    types_equal(*actual0, *formal0->pointee)) {
                    needs_ref = true;
                    needs_mut = formal0->kind == LogosType::Kind::MutRef;
                } else if (actual0->kind != LogosType::Kind::Ref &&
                           actual0->kind != LogosType::Kind::MutRef &&
                           actual0->kind != LogosType::Kind::Ptr &&
                           formal0->kind == LogosType::Kind::Ptr &&
                           formal0->pointee &&
                           types_equal(*actual0, *formal0->pointee)) {
                    needs_ref = true;
                    needs_mut = false;
                } else if (actual0->kind == LogosType::Kind::Ptr &&
                           formal0->kind == LogosType::Kind::Ptr &&
                           actual0->pointee && formal0->pointee &&
                           types_equal(*actual0->pointee, *formal0->pointee)) {
                    // const/mut pointer receivers are compatible if pointees match.
                } else if (!types_compatible(actual0, formal0)) {
                    ok = false;
                }
            }
            for (size_t i = 1; ok && i < cand->param_types.size(); ++i) {
                auto* at = types[i];
                auto* pt = cand->param_types[i];
                if (!recv_struct_subst.empty())
                    pt = subst_type_sema(pt, recv_struct_subst);
                if (!at || !pt || (!types_equal(*at, *pt) && !types_compatible(at, pt))) {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;
            fi_ptr = cand;
            auto_ref_recv = needs_ref;
            auto_ref_mut = needs_mut;
            break;
        }
        if (!fi_ptr)
            fi_ptr = find_generic_func(mangled);
    }
    if (fi_ptr && auto_ref_recv && recv->type &&
        recv->type->kind != LogosType::Kind::Ref &&
        recv->type->kind != LogosType::Kind::MutRef &&
        recv->type->kind != LogosType::Kind::Ptr) {
        auto addr = make_expr(make_ref(auto_ref_mut, recv->type),
                              lir::EAddrOfTemp{std::move(recv), auto_ref_mut});
        recv = std::move(addr);
    }

    // Fallback: for generic structs (Foo$G1$i32), methods may be registered under base name (Foo).
    if (!fi_ptr) {
        std::string base_sname(sname);
        if (auto d = base_sname.find('$'); d != std::string::npos)
            base_sname = base_sname.substr(0, d);
        if (base_sname != sname) {
            auto base_mangled = base_sname + "__" + std::string(method_name);
            std::vector<const LogosType*> types;
            types.push_back(recv->type);
            for (auto& a : arg_exprs) types.push_back(a->type);
            for (auto* cand : find_func_candidates(base_mangled)) {
                if (!cand || !cand->type_params.empty()) continue;
                if (cand->param_types.size() != types.size()) continue;
                bool ok = true;
                bool needs_ref = false;
                bool needs_mut = false;
                auto* formal0 = cand->param_types[0];
                if (!recv_struct_subst.empty())
                    formal0 = subst_type_sema(formal0, recv_struct_subst);
                auto* actual0 = recv->type;
                if (actual0 && formal0 && !types_equal(*actual0, *formal0)) {
                    if (actual0->kind != LogosType::Kind::Ref &&
                        actual0->kind != LogosType::Kind::MutRef &&
                        actual0->kind != LogosType::Kind::Ptr &&
                        is_ref_like(formal0->kind) && formal0->pointee &&
                        types_equal(*actual0, *formal0->pointee)) {
                        needs_ref = true;
                        needs_mut = formal0->kind == LogosType::Kind::MutRef;
                    } else if (actual0->kind != LogosType::Kind::Ref &&
                               actual0->kind != LogosType::Kind::MutRef &&
                               actual0->kind != LogosType::Kind::Ptr &&
                               formal0->kind == LogosType::Kind::Ptr &&
                               formal0->pointee &&
                               types_equal(*actual0, *formal0->pointee)) {
                        needs_ref = true;
                        needs_mut = false;
                    } else if (actual0->kind == LogosType::Kind::Ptr &&
                               formal0->kind == LogosType::Kind::Ptr &&
                               actual0->pointee && formal0->pointee &&
                               types_equal(*actual0->pointee, *formal0->pointee)) {
                        // const/mut pointer receivers are compatible if pointees match.
                    } else if (!types_compatible(actual0, formal0)) {
                        ok = false;
                    }
                }
                for (size_t i = 1; ok && i < cand->param_types.size(); ++i) {
                    auto* at = types[i];
                    auto* pt = cand->param_types[i];
                    if (!recv_struct_subst.empty())
                        pt = subst_type_sema(pt, recv_struct_subst);
                    if (!at || !pt || (!types_equal(*at, *pt) && !types_compatible(at, pt))) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) continue;
                fi_ptr = cand;
                auto_ref_recv = needs_ref;
                auto_ref_mut = needs_mut;
                mangled = base_mangled;
                break;
            }
            if (!fi_ptr) {
                if (auto fit = find_generic_func(base_mangled)) {
                    fi_ptr = fit;
                    mangled = base_mangled;
                }
            }
        }
    }

    if (fi_ptr && auto_ref_recv && recv->type &&
        recv->type->kind != LogosType::Kind::Ref &&
        recv->type->kind != LogosType::Kind::MutRef &&
        recv->type->kind != LogosType::Kind::Ptr) {
        auto addr = make_expr(make_ref(auto_ref_mut, recv->type),
                              lir::EAddrOfTemp{std::move(recv), auto_ref_mut});
        recv = std::move(addr);
    }

    if (!fi_ptr) {
        // Blanket-impl fallback: `impl<T: Bound> Trait for T { fn method … }`
        // provides method on any T satisfying Bound.  Look up whether any
        // blanket's method matches, the receiver type impls the bound, and
        // if so dispatch through finish_generic_call with type_args=[recv_T].
        std::string base_sname(sname);
        if (auto d = base_sname.find('$'); d != std::string::npos)
            base_sname = base_sname.substr(0, d);
        for (auto& bi : blanket_impls_) {
            if (bi.method_name != std::string(method_name)) continue;
            // Receiver's concrete type must impl the bound trait.
            std::string key_full = bi.bound_trait + "::" + std::string(sname);
            std::string key_base = bi.bound_trait + "::" + base_sname;
            if (!impls_.count(key_full) && !impls_.count(key_base)) continue;
            std::vector<const LogosType*> bi_arg_types;
            bi_arg_types.push_back(recv->type);
            for (auto& a : arg_exprs) bi_arg_types.push_back(a->type);
            const SemaFuncInfo* mfi = nullptr;
            if (auto fit = find_func_by_base_and_signature(bi.mangled_name, bi_arg_types, false))
                mfi = fit;
            else if (auto git = find_generic_func(bi.mangled_name))
                mfi = git;
            if (!mfi) continue;
            // Type arg = receiver's concrete type (unwrapped from ref/ptr).
            const LogosType* recv_inner = recv->type;
            if (recv_inner && (recv_inner->kind == LogosType::Kind::Ptr ||
                               is_ref_like(recv_inner->kind)) && recv_inner->pointee)
                recv_inner = recv_inner->pointee;
            // Auto-ref: if method expects &self / &mut self but recv is a
            // value, take its address.
            if (!mfi->param_types.empty()) {
                SemaSubst s_subst;
                s_subst["Self"] = recv_inner;
                s_subst[bi.target_typevar] = recv_inner;
                const LogosType* target_self =
                    subst_type_sema(mfi->param_types[0], s_subst);
                if (target_self &&
                    (target_self->kind == LogosType::Kind::Ref ||
                     target_self->kind == LogosType::Kind::MutRef) &&
                    recv->type &&
                    recv->type->kind != LogosType::Kind::Ref &&
                    recv->type->kind != LogosType::Kind::MutRef &&
                    recv->type->kind != LogosType::Kind::Ptr) {
                    bool is_mut = target_self->kind == LogosType::Kind::MutRef;
                    auto addr = make_expr(target_self,
                                          lir::EAddrOfTemp{std::move(recv), is_mut});
                    recv = std::move(addr);
                }
            }
            std::vector<const LogosType*> type_args = { recv_inner };
            std::vector<lir::LExprPtr> pargs;
            pargs.push_back(std::move(recv));
            for (auto& a : arg_exprs) pargs.push_back(std::move(a));
            return finish_generic_call(
                                       mfi->symbol_name.empty() ? bi.mangled_name : mfi->symbol_name, *mfi,
                                       std::move(type_args), std::move(pargs));
        }
        error(std::format("method call: '{}' has no method '{}'", sname, method_name));
        return make_expr(error_t(),
            lir::EMethodCall{std::move(recv), std::string(method_name), "", {}, std::move(arg_exprs), -1});
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
        if ((rst->kind == LogosType::Kind::Struct || rst->kind == LogosType::Kind::Datatype) && !rst->type_args.empty()) {
            SemaStructInfo* si2 = nullptr;
            { auto it = structs_.find(rst->struct_name); if (it != structs_.end()) si2 = &it->second; }
            if (!si2) { auto it = datatypes_.find(rst->struct_name); if (it != datatypes_.end()) si2 = &it->second; }
            if (si2) {
                auto& tps = si2->type_params;
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
    mc.resolved_symbol = fi.symbol_name.empty() ? mangled : fi.symbol_name;
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

    // DataRef<T> ergonomic read: p.field → p.ptr().field
    // Intercept before normal struct field lookup so that DataRef<T>.x works without
    // an explicit let pw = p.ptr() intermediate step.
    if (recv_base_t && recv_base_t->kind == LogosType::Kind::Struct &&
        recv_base_t->struct_name == "DataRef" &&
        recv_base_t->type_args.size() == 1) {
        const LogosType* T = recv_base_t->type_args[0];
        if (T && T->kind == LogosType::Kind::Datatype) {
            auto* ft = field_type_of_for_type(T, field_name);
            if (ft) {
                if (!inside_unsafe_)
                    error(std::format("DataRef<T>.{}: field access requires unsafe context",
                                      field_name));
                const LogosType* ptr_t = make_ptr(false, T);
                auto mc = make_expr(ptr_t,
                    lir::EMethodCall{std::move(recv), "ptr", "", {}, {}, -1});
                return make_expr(ft,
                    lir::EFieldRead{std::move(mc), std::string(field_name)});
            }
        }
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
        SemaStructInfo* sinfo_ptr = nullptr;
        {
            auto sit = structs_.find(std::string(sname));
            if (sit != structs_.end()) sinfo_ptr = &sit->second;
            else {
                auto dit = datatypes_.find(std::string(sname));
                if (dit != datatypes_.end()) sinfo_ptr = &dit->second;
            }
        }
        if (sinfo_ptr) {
            for (auto& f : sinfo_ptr->fields) {
                if (f.name == field_name || (f.is_variadic && field_name.starts_with(f.name) && field_name.size() > f.name.size() + 1 && field_name[f.name.size()] == '_')) {
                    check_pub_access(f.is_pub, sinfo_ptr->package, field_name);
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
    // Find in structs_ or datatypes_
    auto find_struct_info = [&](const std::string& name) -> SemaStructInfo* {
        auto sit = structs_.find(name);
        if (sit != structs_.end()) return &sit->second;
        auto dit = datatypes_.find(name);
        if (dit != datatypes_.end()) return &dit->second;
        return nullptr;
    };
    auto* sinfo_ptr = find_struct_info(sname_buf);
    if (!sinfo_ptr) {
        // Try resolving via type alias: `type Alias = Struct` or `type Alias = Struct<T>`
        // Bug 3 fix: only apply non-generic aliases here; generic aliases need type args
        // at the call site and we can't infer them from just the struct literal name.
        auto ait = type_aliases_.find(sname_buf);
        if (ait != type_aliases_.end() &&
            ait->second.type_params.empty() && ait->second.lifetime_params.empty()) {
            auto* aliased = ait->second.type;
            if (aliased && (aliased->kind == LogosType::Kind::Struct ||
                            aliased->kind == LogosType::Kind::Datatype)) {
                sinfo_ptr = find_struct_info(aliased->struct_name);
                if (sinfo_ptr) {
                    sname_buf = aliased->struct_name;
                    hint_struct_type_ = aliased;
                }
            }
        }
    }
    std::string_view sname = sname_buf;  // use resolved name throughout
    if (!sinfo_ptr) {
        error(std::format("struct literal: unknown struct '{}'", sname));
        return error_expr();
    }
    auto& sinfo = *sinfo_ptr;

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
            // Skip non-field items (e.g. type_arg_list node from Struct::<T>{} syntax)
            int32_t ic = code_of(init);
            if (ic != la::FIELD_INIT && ic != la::FIELD_SHORTHAND) continue;
            auto fname = str_of(init.get(la::NAME.code));
            lir::LExprPtr val;
            if (ic == la::FIELD_SHORTHAND) {
                // Point { x, y } — same as Point { x: x, y: y }
                auto* t = lookup(fname);
                if (!t) {
                    error(std::format("undefined variable '{}' used as field shorthand", fname));
                    val = error_expr();
                } else {
                    val = make_expr(t, lir::EVarRef{std::string(fname)});
                }
            } else {
                val = init.has_key(la::VALUE)
                    ? lower_expr(map_of(init.get(la::VALUE.code)))
                    : error_expr();
            }
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
        std::vector<const LogosType*> explicit_args;
        if (node.has_key(la::TYPE_PARAMS)) {
            AnyVal tpav = node.get(la::TYPE_PARAMS.code);
            if (!tpav.is_null()) {
                auto tplist = map_of(tpav);
                if (tplist.has_key(la::ITEMS)) {
                    auto items = arr_of(tplist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size() && i < sinfo.type_params.size(); ++i) {
                        auto* resolved = resolve_type(map_of(items.get(i)));
                        if (resolved && resolved->kind != LogosType::Kind::Error) {
                            inferred[sinfo.type_params[i].name] = resolved;
                            explicit_args.push_back(resolved);
                        }
                    }
                }
            }
        }

        // Partial-spec pickup: if the user supplied all type args and there's
        // a matching spec (full or partial), use its fields for this literal.
        const SemaStructInfo* spec_info = nullptr;
        if (!explicit_args.empty() && explicit_args.size() == sinfo.type_params.size())
            spec_info = find_best_sema_struct_spec(std::string(sname), explicit_args);
        const SemaStructInfo& eff_sinfo = spec_info ? *spec_info : sinfo;

        for (auto& [fname, fval] : fields) {
            auto* raw_ft = [&]() -> const LogosType* {
                if (spec_info) {
                    for (auto& f : spec_info->fields) {
                        if (f.name == fname) return f.type;
                        if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_')
                            return f.type;
                    }
                    return nullptr;
                }
                return field_type_of(std::string(sname), fname);
            }();
            if (!raw_ft) continue;
            if (raw_ft->kind == LogosType::Kind::TypeVar) {
                auto& tv = raw_ft->type_var_name;
                if (!inferred.count(tv)) {
                    auto* vt = fval->type;
                    if (vt->kind == LogosType::Kind::IntLit) {
                        auto* h = hint_for_tv(tv);
                        vt = (h && h->kind != LogosType::Kind::Error) ? h : i32_t();
                    } else if (vt->kind == LogosType::Kind::FloatLit) {
                        auto* h = hint_for_tv(tv);
                        vt = (h && h->kind != LogosType::Kind::Error) ? h : prim(LogosType::Kind::F64);
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
        std::vector<std::string> lit_lt_args;
        if (hint_struct_type_ && hint_struct_type_->struct_name == std::string(sname))
            lit_lt_args = hint_struct_type_->lifetime_args;
        const LogosType* lit_type = datatypes_.count(std::string(sname))
            ? make_generic_datatype(std::string(sname), args, lit_lt_args)
            : make_generic_struct(std::string(sname), args, lit_lt_args);

        // Check if a concrete specialization exists for these type args.
        // First try exact-concrete key, then pattern-match (for partial specs).
        std::string concrete = concrete_struct_name(lit_type);
        const SemaStructInfo* effective = &sinfo;
        auto spec_it = struct_specs_sema_.find(concrete);
        if (spec_it != struct_specs_sema_.end())
            effective = &spec_it->second;
        else if (auto* pspec = find_best_sema_struct_spec(std::string(sname), args))
            effective = pspec;

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
                if (it->second) {
                    error(std::format("struct literal '{}': duplicate field '{}'", sname, fname));
                    continue;
                }
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

        return make_expr(lit_type, lir::EStructLit{concrete, std::move(fields)});
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
            if (it->second) {
                error(std::format("struct literal '{}': duplicate field '{}'", sname, fname));
                continue;
            }
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
    // Handle struct update syntax: Foo { x: 1, ..base }
    // For any field not explicitly set, read it from the base expression.
    if (node.has_key(la::BASE)) {
        auto base_node = map_of(node.get(la::BASE.code));
        auto base_expr = lower_expr(base_node);
        // Determine base variable name for EVarRef (simple case)
        std::string base_var;
        if (auto* vr = std::get_if<lir::EVarRef>(&base_expr->kind))
            base_var = vr->name;
        for (auto& [fname, inited] : initialized) {
            if (!inited) {
                inited = true;
                auto* ft = field_type_of(std::string(sname), fname);
                lir::LExprPtr recv;
                if (!base_var.empty()) {
                    recv = make_expr(base_expr->type, lir::EVarRef{base_var});
                } else {
                    // Complex base: re-lower (might evaluate twice, but rare)
                    recv = lower_expr(base_node);
                }
                auto field_val = make_expr(ft ? ft : error_t(),
                    lir::EFieldRead{std::move(recv), fname});
                fields.push_back({fname, std::move(field_val)});
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

    std::vector<std::string> ng_lt_args;
    if (hint_struct_type_ && hint_struct_type_->struct_name == std::string(sname))
        ng_lt_args = hint_struct_type_->lifetime_args;
    LogosType ng_t;
    ng_t.kind = datatypes_.count(std::string(sname))
                ? LogosType::Kind::Datatype : LogosType::Kind::Struct;
    ng_t.struct_name   = std::string(sname);
    ng_t.lifetime_args = std::move(ng_lt_args);
    const LogosType* lit_result_type = pool_.alloc(std::move(ng_t));
    return make_expr(lit_result_type,
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
                elem_type = unify_numeric(elem_type, t);
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
        // Before reporting "unknown enum", check if this is an associated constant
        // access (e.g. Buffer::MAX) parsed as ENUM_LIT due to grammar ambiguity.
        std::string cname_str = std::string(ename);
        std::string mname_str = std::string(vname);
        for (auto& [tname, tinfo] : traits_) {
            if (!impls_.count(tname + "::" + cname_str)) continue;
            std::string key = tname + "::" + cname_str + "::" + mname_str;
            auto cit = assoc_const_impls_.find(key);
            if (cit != assoc_const_impls_.end()) {
                auto val = lower_expr(map_of(cit->second.value_ast));
                if (cit->second.type) val->type = cit->second.type;
                return val;
            }
        }
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
        // Bug 5 fix: ENUM_LIT_DATA shares the same grammar path as ENUM_LIT.
        // Check for associated constant access before reporting "unknown enum".
        std::string cname_str = std::string(ename);
        std::string mname_str = std::string(vname);
        for (auto& [tname, tinfo] : traits_) {
            if (!impls_.count(tname + "::" + cname_str)) continue;
            std::string key = tname + "::" + cname_str + "::" + mname_str;
            auto cit = assoc_const_impls_.find(key);
            if (cit != assoc_const_impls_.end()) {
                auto val = lower_expr(map_of(cit->second.value_ast));
                if (cit->second.type) val->type = cit->second.type;
                return val;
            }
        }
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
        for (uint64_t i = 0; i < args.size(); ++i) {
            auto e = lower_expr(map_of(args.get(i)));
            if (e->type->kind == LogosType::Kind::Void) continue;  // skip () unit args
            payload.push_back(std::move(e));
        }
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

lir::LExprPtr SemaChecker::lower_static_call(TinyMapView node) {
    auto class_name = str_of(node.get(la::RECEIVER.code));
    auto method_name = str_of(node.get(la::NAME.code));

    // If "class_name" is actually an enum, redirect to enum lit with data.
    if (enums_.count(std::string(class_name))) {
        // Reinterpret as ENUM_LIT_DATA: NAME=class_name, FIELD=method_name
        // Build a fake node view... or just inline the logic.
        return lower_enum_lit_data_from_static(node, class_name, method_name);
    }

    // Resolve type aliases: `type ObjectArray = Array<AnyVal>;`
    // makes `ObjectArray::init(...)` call `Array$G1$AnyVal__init(...)`.
    std::string resolved_class(class_name);
    {
        auto ait = type_aliases_.find(resolved_class);
        if (ait != type_aliases_.end() && ait->second.type_params.empty()) {
            auto* aliased = ait->second.type;
            if (aliased && (aliased->kind == LogosType::Kind::Struct ||
                            aliased->kind == LogosType::Kind::Datatype)) {
                resolved_class = aliased->type_args.empty()
                    ? aliased->struct_name
                    : concrete_struct_name(aliased);
            }
        }
    }
    std::string mangled = resolved_class + "__" + std::string(method_name);

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
        std::vector<const LogosType*> arg_types;
        for (auto& a : arg_exprs) arg_types.push_back(a->type);
        auto fit = find_func_by_base_and_signature(mangled, arg_types, false);
        if (fit) fi_ptr = fit;
        else {
            auto cands = find_func_candidates(mangled);
            if (cands.size() == 1) {
                fi_ptr = cands[0];
            } else {
                for (auto* cand : cands) {
                    if (!cand || !cand->type_params.empty()) continue;
                    if (cand->param_types.size() != arg_exprs.size()) continue;
                    fi_ptr = cand;
                    break;
                }
            }
            if (!fi_ptr) {
            auto git = find_generic_func(mangled);
            if (git != nullptr) fi_ptr = git;
            }
        }
    }
    if (!fi_ptr) {
        // Check if this is an associated constant access: TypeName::CONST_NAME
        // Look through all traits implemented by class_name for a matching constant.
        std::string cname_str = std::string(class_name);
        std::string mname_str = std::string(method_name);
        for (auto& [tname, tinfo] : traits_) {
            if (!impls_.count(tname + "::" + cname_str)) continue;
            std::string key = tname + "::" + cname_str + "::" + mname_str;
            auto cit = assoc_const_impls_.find(key);
            if (cit != assoc_const_impls_.end()) {
                // Lower the constant value expression and return it directly.
                auto val = lower_expr(map_of(cit->second.value_ast));
                if (cit->second.type) val->type = cit->second.type;
                return val;
            }
        }
        // Generic static dispatch: DT::method() where DT is a type parameter with
        // a trait bound that declares a static method `method`.  The actual impl is
        // resolved during monomorphization (see mono_clone.cpp ECall branch — it
        // rewrites "DT__method" → "ConcreteType__method" once DT is substituted).
        auto bit = current_type_bounds_.find(cname_str);
        if (bit != current_type_bounds_.end()) {
            for (auto& bound : bit->second) {
                auto tit = traits_.find(bound.trait_name);
                if (tit == traits_.end()) continue;
                for (auto& m : tit->second.methods) {
                    if (m.name != mname_str) continue;
                    // Static if the first param isn't Self/self.
                    bool is_static = m.param_types.empty() ||
                        !(m.param_types[0] &&
                          (m.param_types[0]->kind == LogosType::Kind::TypeVar &&
                           m.param_types[0]->type_var_name == "Self"));
                    if (!is_static) continue;
                    if (m.is_unsafe && !inside_unsafe_)
                        error(std::format("call to unsafe method '{}' requires unsafe context", mname_str));
                    // Substitute Self → TypeVar(DT) in return type so that
                    // Self::Storage becomes AssocType with base = TypeVar(DT).
                    SemaSubst self_subst;
                    self_subst["Self"] = current_type_params_.count(cname_str)
                        ? current_type_params_[cname_str] : make_typevar(cname_str);
                    const LogosType* ret_t = subst_type_sema(m.ret_type, self_subst);
                    const SemaFuncInfo* mfi = nullptr;
                    {
                        auto base = cname_str + "__" + mname_str;
                        auto cands = find_func_candidates(base);
                        if (cands.size() == 1) {
                            mfi = cands[0];
                        } else if (!cands.empty()) {
                            for (auto* cand : cands) {
                                if (!cand) continue;
                                if (cand->param_types.size() == m.param_types.size()) {
                                    mfi = cand;
                                    break;
                                }
                            }
                        }
                    }
                    // Arg-count check.
                    if (arg_exprs.size() != m.param_types.size())
                        error(std::format("method call '{}::{}': expected {} args, got {}",
                              cname_str, mname_str, m.param_types.size(), arg_exprs.size()));
                    return make_expr(ret_t,
                        lir::ECall{
                            mfi && !mfi->symbol_name.empty()
                                ? mfi->symbol_name
                                : cname_str + "__" + mname_str,
                            {}, std::move(arg_exprs)});
                }
            }
        }
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
        // Check whether we're already inside a generic body (TypeVar args
        // or AssocType appearing in value args OR explicit type args).
        bool in_generic_context = false;
        for (auto& a : arg_exprs) {
            auto* t = a->type;
            if (t && (t->kind == LogosType::Kind::TypeVar ||
                      t->kind == LogosType::Kind::AssocType)) {
                in_generic_context = true; break;
            }
        }
        if (!in_generic_context && node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto* t = resolve_type(map_of(items.get(i)));
                    if (t && (t->kind == LogosType::Kind::TypeVar ||
                              t->kind == LogosType::Kind::AssocType)) {
                        in_generic_context = true; break;
                    }
                }
            }
        }
        if (!in_generic_context) {
            std::vector<const LogosType*> inferred;
            bool have_inferred = false;
            // Turbofish: Buffer::<I64>::new() — use explicit type args, skip arg inference.
            if (node.has_key(la::TYPE_PARAMS)) {
                auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
                if (tplist.has_key(la::ITEMS)) {
                    auto items = arr_of(tplist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i)
                        inferred.push_back(resolve_type(map_of(items.get(i))));
                    have_inferred = !inferred.empty();
                }
            }
            if (have_inferred || infer_type_args(fi, arg_exprs, inferred)) {
                // Build substitution map for the inferred type params.
                std::unordered_map<std::string, const LogosType*> subst;
                size_t n_tp = fi.type_params.size();
                for (size_t i = 0; i < n_tp && i < inferred.size(); ++i)
                    subst[fi.type_params[i].name] = inferred[i];

                // Substitute the return type.
                const LogosType* ret = subst_type_sema(fi.ret_type, subst);

                // If the host struct is generic (class_name has type params in structs_),
                // keep the template symbol here and let mono rewrite it to the concrete
                // instantiated struct method later.  That preserves the generic suffix
                // and avoids emitting a bare "Box$G1$i32__wrap" call too early.
                auto sit = structs_.find(std::string(class_name));
                if (sit != structs_.end() && !sit->second.type_params.empty()) {
                    return finish_generic_call(
                        fi.symbol_name.empty() ? mangled : fi.symbol_name,
                        fi, std::move(inferred), std::move(arg_exprs));
                }

                // For generic free functions registered via impl (no struct template),
                // fall through to finish_generic_call which handles them via templates_.
                return finish_generic_call(
                    fi.symbol_name.empty() ? mangled : fi.symbol_name,
                    fi, std::move(inferred), std::move(arg_exprs));
            }
            // Could not infer — fall through to concrete path (mono will handle)
        }
        // Inside generic body: emit with type_args so mono can rename the callee
        // to the concrete struct method name when instantiating.  If the call
        // site supplied explicit type args (turbofish), use them verbatim so
        // the return type is substituted properly.
        {
            std::vector<const LogosType*> type_var_args;
            if (node.has_key(la::TYPE_PARAMS)) {
                auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
                if (tplist.has_key(la::ITEMS)) {
                    auto items = arr_of(tplist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i)
                        type_var_args.push_back(resolve_type(map_of(items.get(i))));
                }
            }
            if (type_var_args.empty()) {
                for (auto& tp : fi.type_params)
                    type_var_args.push_back(make_typevar(tp.name));
            }
            SemaSubst subst;
            for (size_t i = 0; i < fi.type_params.size() && i < type_var_args.size(); ++i)
                subst[fi.type_params[i].name] = type_var_args[i];
            const LogosType* ret = subst_type_sema(fi.ret_type, subst);
            return make_expr(ret, lir::ECall{
                fi.symbol_name.empty() ? mangled : fi.symbol_name,
                std::move(type_var_args), std::move(arg_exprs)});
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

    return make_expr(fi.ret_type,
        lir::ECall{fi.symbol_name.empty() ? mangled : fi.symbol_name, {}, std::move(arg_exprs)});
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
            result_type = unify_numeric(then_val->type, else_val->type);
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
    bool is_move = node.has_key(la::IS_MOVE) &&
                   !node.get(la::IS_MOVE.code).is_null() &&
                   node.get(la::IS_MOVE.code).as_value<int32_t>() != 0;

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

    // Capture detection: find variables used anywhere in the closure body
    // that are not params and still resolve in the enclosing scope.
    std::vector<std::string> captures;
    std::vector<const LogosType*> capture_types;
    std::unordered_set<std::string> seen;
    auto add_capture = [&](const std::string& name) {
        if (param_names.count(name) || seen.count(name))
            return;
        auto* t = lookup(name);
        if (!t)
            return;
        captures.push_back(name);
        capture_types.push_back(t);
        seen.insert(name);
    };

    std::function<void(const lir::LBlock&)> scan_block;
    std::function<void(const lir::LStmt&)> scan_stmt;
    std::function<void(const lir::LExpr&)> scan_captures;
    scan_captures = [&](const lir::LExpr& e) {
        if (auto* vr = std::get_if<lir::EVarRef>(&e.kind)) {
            add_capture(vr->name);
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
            } else if constexpr (std::is_same_v<K, lir::EEnumLitData>) {
                for (auto& p : k.payload) scan_captures(*p);
            } else if constexpr (std::is_same_v<K, lir::EStructLit>) {
                for (auto& [_, field] : k.fields) scan_captures(*field);
            } else if constexpr (std::is_same_v<K, lir::EArrLit>) {
                for (auto& elem : k.elems) scan_captures(*elem);
            } else if constexpr (std::is_same_v<K, lir::ENew>) {
                for (auto& [_, field] : k.fields) scan_captures(*field);
            } else if constexpr (std::is_same_v<K, lir::EIfExpr>) {
                scan_captures(*k.cond); scan_captures(*k.then_val); scan_captures(*k.else_val);
            } else if constexpr (std::is_same_v<K, lir::EMatchExpr>) {
                scan_captures(*k.scrut);
                for (auto& arm : k.arms) {
                    if (arm.guard && *arm.guard) scan_captures(**arm.guard);
                    scan_captures(*arm.value);
                }
            } else if constexpr (std::is_same_v<K, lir::ETupleLit>) {
                for (auto& elem : k.elems) scan_captures(*elem);
            } else if constexpr (std::is_same_v<K, lir::ETupleIndex>) {
                scan_captures(*k.receiver);
            } else if constexpr (std::is_same_v<K, lir::ESliceLit>) {
                scan_captures(*k.base); scan_captures(*k.len);
            } else if constexpr (std::is_same_v<K, lir::ESliceIndex>) {
                scan_captures(*k.slice); scan_captures(*k.index);
            } else if constexpr (std::is_same_v<K, lir::ESliceLen>) {
                scan_captures(*k.slice);
            } else if constexpr (std::is_same_v<K, lir::EClosureBox>) {
                if (k.inner) {
                    for (auto& cap : k.inner->captures)
                        add_capture(cap);
                }
            } else if constexpr (std::is_same_v<K, lir::EClosureCall>) {
                scan_captures(*k.callee);
                for (auto& arg : k.args) scan_captures(*arg);
            } else if constexpr (std::is_same_v<K, lir::EFormatCall>) {
                scan_captures(*k.fmt);
                for (auto& arg : k.args) scan_captures(*arg);
            } else if constexpr (std::is_same_v<K, lir::ETry>) {
                scan_captures(*k.inner);
            } else if constexpr (std::is_same_v<K, lir::EBlockExpr>) {
                if (k.block) scan_block(*k.block);
                if (k.result) scan_captures(*k.result);
            } else if constexpr (std::is_same_v<K, lir::EHermesLit>) {
                // C2 bug fix: scan capture_exprs so closures that use $-captures
                // correctly include those outer variables in their capture list.
                for (auto& ce : k.capture_exprs) scan_captures(*ce);
            }
        }, e.kind);
    };
    // Scan all statements in body for variable references.
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
            } else if constexpr (std::is_same_v<K, lir::SFor>) {
                scan_captures(*k.lo); scan_captures(*k.hi); scan_block(*k.body);
            } else if constexpr (std::is_same_v<K, lir::SLoop>) {
                scan_block(*k.body);
            } else if constexpr (std::is_same_v<K, lir::SBreak>) {
                if (k.value) scan_captures(*k.value);
            } else if constexpr (std::is_same_v<K, lir::SBlock>) {
                scan_block(*k.body);
            } else if constexpr (std::is_same_v<K, lir::SFieldWrite>) {
                scan_captures(*k.value);
            } else if constexpr (std::is_same_v<K, lir::SIndexWrite>) {
                scan_captures(*k.index); scan_captures(*k.value);
            } else if constexpr (std::is_same_v<K, lir::SFieldIndexWrite>) {
                scan_captures(*k.index); scan_captures(*k.value);
            } else if constexpr (std::is_same_v<K, lir::SExprStmt>) {
                scan_captures(*k.expr);
            } else if constexpr (std::is_same_v<K, lir::SMatch>) {
                scan_captures(*k.scrut);
                for (auto& arm : k.arms) {
                    if (arm.guard && *arm.guard) scan_captures(**arm.guard);
                    if (arm.body) scan_block(*arm.body);
                }
            } else if constexpr (std::is_same_v<K, lir::SDelete>) {
                scan_captures(*k.expr);
            } else if constexpr (std::is_same_v<K, lir::SForEach>) {
                scan_captures(*k.iter); scan_block(*k.body);
            } else if constexpr (std::is_same_v<K, lir::SDerefWrite>) {
                scan_captures(*k.ptr); scan_captures(*k.value);
            } else if constexpr (std::is_same_v<K, lir::SDerefFieldWrite>) {
                scan_captures(*k.value);
            } else if constexpr (std::is_same_v<K, lir::STupleWrite>) {
                scan_captures(*k.value);
            }
        }, s.kind);
    };
    scan_block(body);

    auto ec = std::make_unique<lir::EClosure>();
    ec->closure_id    = closure_id;
    ec->params        = std::move(params);
    ec->ret_type      = ret_type;
    ec->body          = std::move(body);
    ec->is_move       = is_move;
    ec->captures      = std::move(captures);
    ec->capture_types = std::move(capture_types);

    if (is_move) {
        for (size_t i = 0; i < ec->captures.size(); ++i) {
            if (is_move_type(ec->capture_types[i]))
                mark_moved(ec->captures[i]);
        }
    }

    auto* ctype = make_closure_type(std::move(param_types), ret_type);
    return make_expr(ctype, lir::EClosureBox{std::move(ec)});
}


// ---------------------------------------------------------------------------
// Hermes SDN literal lowering
// ---------------------------------------------------------------------------

lir::HermesValPtr SemaChecker::lower_hermes_val(TinyMapView node) {
    int32_t c = code_of(node);

    if (c == la::HERMES_NEG_INT.code) {
        auto sv = str_of(node.get(la::VALUE.code));
        int64_t v = std::stoll(std::string(sv));
        return std::make_unique<lir::HermesVal>(lir::HVInt{-v});
    }

    if (c == la::HERMES_NULL.code)
        return std::make_unique<lir::HermesVal>(lir::HVNull{});

    if (c == la::HERMES_BOOL.code) {
        AnyVal av = node.get(la::VALUE.code);
        bool v = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        return std::make_unique<lir::HermesVal>(lir::HVBool{v});
    }

    if (c == la::HERMES_INT.code) {
        auto sv = str_of(node.get(la::VALUE.code));
        // Strip suffix (i32, u64, etc.) and parse
        std::string s(sv);
        static const char* suffixes[] = {
            "i8","i16","i24","i32","i56","i64","i128",
            "u8","u16","u24","u32","u56","u64","u128","usize","isize"
        };
        for (auto* suf : suffixes) {
            if (s.size() > strlen(suf) && s.substr(s.size()-strlen(suf)) == suf)
                { s = s.substr(0, s.size()-strlen(suf)); break; }
        }
        bool neg = !s.empty() && s[0] == '-';
        if (neg) s = s.substr(1);
        int64_t v = 0;
        if (s.size() > 2 && s[0] == '0' && s[1] == 'x')
            v = (int64_t)std::stoull(s.substr(2), nullptr, 16);
        else if (s.size() > 2 && s[0] == '0' && s[1] == 'b')
            v = (int64_t)std::stoull(s.substr(2), nullptr, 2);
        else
            v = (int64_t)std::stoull(s, nullptr, 10);
        if (neg) v = -v;
        return std::make_unique<lir::HermesVal>(lir::HVInt{v});
    }

    if (c == la::HERMES_FLOAT.code) {
        auto sv = str_of(node.get(la::VALUE.code));
        std::string s(sv);
        // strip f32/f64 suffix
        if (s.size() > 3 && (s.substr(s.size()-3) == "f32" || s.substr(s.size()-3) == "f64"))
            s = s.substr(0, s.size()-3);
        double v = std::stod(s);
        return std::make_unique<lir::HermesVal>(lir::HVFloat{v});
    }

    if (c == la::HERMES_STR.code) {
        auto sv = str_of(node.get(la::VALUE.code));
        std::string s(sv);
        // Strip surrounding quotes and handle basic escapes
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            s = s.substr(1, s.size()-2);
        // Process escape sequences
        std::string out;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i+1 < s.size()) {
                switch (s[++i]) {
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case '\\': out += '\\'; break;
                case '"':  out += '"';  break;
                case '0':  out += '\0'; break;
                default:   out += '\\'; out += s[i]; break;
                }
            } else {
                out += s[i];
            }
        }
        return std::make_unique<lir::HermesVal>(lir::HVStr{std::move(out)});
    }

    if (c == la::HERMES_MAP.code) {
        lir::HVMap m;
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto entry = map_of(items.get(i));
                auto key_raw = str_of(entry.get(la::KEY.code));
                auto val_node = map_of(entry.get(la::VALUE.code));
                auto hv = lower_hermes_val(val_node);
                if (!hv) return nullptr;
                lir::HVMapEntry e;
                if (!key_raw.empty() && key_raw[0] == '"') {
                    // Strip quotes then unescape (same logic as HERMES_STR values).
                    std::string raw_inner(key_raw.substr(1, key_raw.size()-2));
                    std::string ks;
                    for (size_t ki = 0; ki < raw_inner.size(); ++ki) {
                        if (raw_inner[ki] == '\\' && ki + 1 < raw_inner.size()) {
                            switch (raw_inner[++ki]) {
                            case 'n':  ks += '\n'; break;
                            case 't':  ks += '\t'; break;
                            case 'r':  ks += '\r'; break;
                            case '\\': ks += '\\'; break;
                            case '"':  ks += '"';  break;
                            case '0':  ks += '\0'; break;
                            default:   ks += '\\'; ks += raw_inner[ki]; break;
                            }
                        } else {
                            ks += raw_inner[ki];
                        }
                    }
                    e.key = std::move(ks);
                } else {
                    int64_t kv = (int64_t)std::stoll(std::string(key_raw));
                    if (entry.has_key(la::LO_NEG)) kv = -kv;
                    e.key = kv;
                }
                e.val = std::move(hv);
                m.entries.push_back(std::move(e));
            }
        }
        return std::make_unique<lir::HermesVal>(std::move(m));
    }

    if (c == la::HERMES_ARRAY.code) {
        lir::HVArray a;
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto elem = map_of(items.get(i));
                auto hv = lower_hermes_val(elem);
                if (!hv) return nullptr;
                a.elements.push_back(std::move(hv));
            }
        }
        return std::make_unique<lir::HermesVal>(std::move(a));
    }

    if (c == la::HERMES_TYPED_ARRAY.code) {
        // @<ElemType>[v,...] — typed dense array (e.g. @<I32>[1,2,3])
        // Known element types: I32 (ArrayI32, tc=104), U64 (ArrayU64, tc=108).
        // The corresponding array struct must be in scope (use hermes.containers).
        struct ElemInfo { std::string struct_name; uint64_t type_code; };
        static const std::map<std::string, ElemInfo> known = {
            {"I32", {"ArrayI32", 104}},
            {"U64", {"ArrayU64", 108}},
        };
        auto type_name = std::string(str_of(node.get(la::TYPE.code)));
        auto kit = known.find(type_name);
        if (kit == known.end()) {
            error(std::format("unknown typed array element type '{}'; supported: I32, U64", type_name));
            return nullptr;
        }
        if (!datatypes_.count(kit->second.struct_name)) {
            error(std::format(
                "typed array @<{}>[...] requires '{}' in scope — add 'use hermes.containers;'",
                type_name, kit->second.struct_name));
            return nullptr;
        }
        lir::HVArray a;
        a.elem_type = type_name;
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto elem = map_of(items.get(i));
                // Typed arrays store raw element values (not AnyVal), so capture
                // placeholders have no slot to occupy.
                if (code_of(elem) == la::HERMES_CAP_IDENT.code ||
                    code_of(elem) == la::HERMES_CAP_EXPR.code) {
                    error(std::format(
                        "@<{}>[...] does not support $-captures; typed arrays store raw {}"
                        " values, not AnyVal — use an untyped @[...] literal instead",
                        type_name, type_name));
                    return nullptr;
                }
                auto hv = lower_hermes_val(elem);
                if (!hv) return nullptr;
                // Bounds-check I32 elements at compile time.
                if (type_name == "I32") {
                    if (auto* hvi = std::get_if<lir::HVInt>(&hv->kind)) {
                        if (hvi->value < -2147483648LL || hvi->value > 2147483647LL) {
                            error(std::format(
                                "@<I32> element [{}] value {} is out of i32 range [-2147483648, 2147483647]",
                                i, hvi->value));
                            return nullptr;
                        }
                    }
                }
                a.elements.push_back(std::move(hv));
            }
        }
        return std::make_unique<lir::HermesVal>(std::move(a));
    }

    if (c == la::HERMES_TYPED_MAP.code) {
        // @<K,V>{...} or @<K>{...} — typed map literal.
        // Supported: I32 / I32+AnyVal → MapI32AnyVal (tc=105)
        //            Varchar / Varchar+AnyVal → ObjectMap (tc=101, same as untyped)
        // TYPE (slot 3) holds key type; RET_TYPE (slot 6) holds optional val type.
        auto key_type_sv = str_of(node.get(la::TYPE.code));
        std::string key_type(key_type_sv);
        std::string val_type_s;
        if (node.has_key(la::RET_TYPE)) {
            auto vt = str_of(node.get(la::RET_TYPE.code));
            val_type_s = std::string(vt);
        }
        // Validate val type if present.
        if (!val_type_s.empty() && val_type_s != "AnyVal") {
            error(std::format(
                "@<{},{}> — unsupported value type '{}'; only AnyVal is supported",
                key_type, val_type_s, val_type_s));
            return nullptr;
        }
        std::string lir_key_type;
        if (key_type == "I32") {
            if (!datatypes_.count("MapI32AnyVal")) {
                error("typed map @<I32>{...} requires 'use hermes.containers;'");
                return nullptr;
            }
            lir_key_type = "I32";
        } else if (key_type == "Varchar") {
            lir_key_type = "";  // same as untyped ObjectMap
        } else {
            error(std::format(
                "@<{}> — unsupported key type '{}'; supported: I32, Varchar",
                key_type, key_type));
            return nullptr;
        }
        lir::HVMap m;
        m.key_type = lir_key_type;
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto entry = map_of(items.get(i));
                auto key_raw = str_of(entry.get(la::KEY.code));
                auto val_node = map_of(entry.get(la::VALUE.code));
                auto hv = lower_hermes_val(val_node);
                if (!hv) return nullptr;
                lir::HVMapEntry e;
                if (!key_raw.empty() && key_raw[0] == '"') {
                    // String key — strip quotes and unescape.
                    std::string raw_inner(key_raw.substr(1, key_raw.size()-2));
                    std::string ks;
                    for (size_t ki = 0; ki < raw_inner.size(); ++ki) {
                        if (raw_inner[ki] == '\\' && ki + 1 < raw_inner.size()) {
                            switch (raw_inner[++ki]) {
                            case 'n':  ks += '\n'; break;
                            case 't':  ks += '\t'; break;
                            case 'r':  ks += '\r'; break;
                            case '\\': ks += '\\'; break;
                            case '"':  ks += '"';  break;
                            case '0':  ks += '\0'; break;
                            default:   ks += '\\'; ks += raw_inner[ki]; break;
                            }
                        } else {
                            ks += raw_inner[ki];
                        }
                    }
                    if (lir_key_type == "I32") {
                        error(std::format(
                            "@<I32> map entry [{}] has string key '{}'; I32 maps require integer keys",
                            i, ks));
                        return nullptr;
                    }
                    e.key = std::move(ks);
                } else {
                    int64_t kv = (int64_t)std::stoll(std::string(key_raw));
                    if (entry.has_key(la::LO_NEG)) kv = -kv;
                    if (lir_key_type == "I32" &&
                        (kv < -2147483648LL || kv > 2147483647LL)) {
                        error(std::format(
                            "@<I32> map key [{}] value {} is out of i32 range", i, kv));
                        return nullptr;
                    }
                    e.key = kv;
                }
                e.val = std::move(hv);
                m.entries.push_back(std::move(e));
            }
        }
        return std::make_unique<lir::HermesVal>(std::move(m));
    }

    if (c == la::HERMES_CAP_IDENT.code || c == la::HERMES_CAP_EXPR.code) {
        if (!hermes_cap_ctx_) {
            error("$-capture used outside of a capturable @-literal context");
            return nullptr;
        }
        // Resolve the captured Logos expression.
        auto is_capturable = [&](const LogosType* t) -> bool {
            if (!t) return false;
            using K = LogosType::Kind;
            switch (t->kind) {
                // Scalar integer types and bool: coerced to inline AnyVal.
                case K::I8: case K::I16: case K::I32: case K::I64:
                case K::U8: case K::U16: case K::U32: case K::U64:
                case K::Bool:
                    return true;
                // F64/F32/FloatLit: zone-alloc F64 object (type_code=31) — C5.
                case K::F64: case K::F32: case K::FloatLit:
                    return true;
                // AnyVal passes through; StringView captures as varchar — C5.
                case K::Struct:
                    return t->struct_name == "AnyVal" ||
                           t->struct_name == "StringView";
                // *const u8 / *mut u8 captured as C-string varchar — C5.
                case K::Ptr:
                    return t->pointee && t->pointee->kind == K::U8;
                default:
                    return false;
            }
        };

        if (c == la::HERMES_CAP_IDENT.code) {
            auto name_sv = str_of(node.get(la::NAME.code));
            std::string name(name_sv);
            auto* var_type = lookup(name);
            if (!var_type) {
                error(std::format("$-capture: unknown variable '{}'", name));
                return nullptr;
            }
            if (!is_capturable(var_type)) {
                error(std::format(
                    "$-capture: cannot capture '{}' of type '{}' in @-literal; "
                    "supported types: integer scalars (i8..i64, u8..u64), bool, "
                    "f64, f32, *const u8 (C-string), StringView, AnyVal",
                    name, type_str(var_type)));
                return nullptr;
            }
            // Deduplicate: same identifier → same value_index.
            auto it = hermes_cap_ctx_->ident_dedup.find(name);
            uint32_t value_idx;
            if (it != hermes_cap_ctx_->ident_dedup.end()) {
                value_idx = it->second;
            } else {
                value_idx = static_cast<uint32_t>(hermes_cap_ctx_->exprs.size());
                hermes_cap_ctx_->exprs.push_back(make_expr(var_type, lir::EVarRef{name}));
                hermes_cap_ctx_->types.push_back(var_type);
                hermes_cap_ctx_->ident_dedup[name] = value_idx;
            }
            uint32_t param_idx = hermes_cap_ctx_->next_slot++;
            return std::make_unique<lir::HermesVal>(lir::HVCapture{param_idx, value_idx});
        } else {
            // HERMES_CAP_EXPR: ${expr} — always fresh (no dedup: may have side effects).
            auto expr_node = map_of(node.get(la::VALUE.code));
            auto cap_expr = lower_expr(expr_node);
            if (!cap_expr) return nullptr;
            auto* expr_type = cap_expr->type;
            if (!is_capturable(expr_type)) {
                error(std::format(
                    "${{...}}-capture: expression of type '{}' cannot be captured in @-literal; "
                    "supported types: integer scalars (i8..i64, u8..u64), bool, "
                    "f64, f32, *const u8 (C-string), StringView, AnyVal",
                    type_str(expr_type)));
                return nullptr;
            }
            uint32_t value_idx = static_cast<uint32_t>(hermes_cap_ctx_->exprs.size());
            uint32_t param_idx = hermes_cap_ctx_->next_slot++;
            hermes_cap_ctx_->exprs.push_back(std::move(cap_expr));
            hermes_cap_ctx_->types.push_back(expr_type);
            return std::make_unique<lir::HermesVal>(lir::HVCapture{param_idx, value_idx});
        }
    }

    error("unexpected Hermes literal node kind");
    return nullptr;
}

lir::LExprPtr SemaChecker::lower_hermes_lit(TinyMapView node) {
    // First pass: probe for captures without collecting (cheap walk).
    // We do a full walk with a capture context: if any $-captures exist they'll
    // be collected; if not, ctx.exprs stays empty and we use the static path.
    //
    // C1/C2 bug fix: save and restore hermes_cap_ctx_ so that a static @-literal
    // inside a ${expr} capture (e.g. @{"a": ${fn(@[1,2,3])}, "b": $x}) doesn't
    // clobber the outer context.  Without the save/restore, the inner lower_hermes_lit
    // call sets hermes_cap_ctx_ = nullptr, causing a null-deref on subsequent $-captures.
    HermesCapCtx* saved_cap_ctx = hermes_cap_ctx_;
    HermesCapCtx ctx;
    hermes_cap_ctx_ = &ctx;
    auto val = lower_hermes_val(node);
    hermes_cap_ctx_ = saved_cap_ctx;
    if (!val) return error_expr();

    lir::EHermesLit lit;
    lit.root = std::move(val);
    if (!ctx.exprs.empty()) {
        lit.has_captures = true;
        lit.capture_exprs = std::move(ctx.exprs);
        lit.capture_types = std::move(ctx.types);
        lit.capture_param_count = ctx.next_slot;
    }
    // Type: *const u8 for static blobs; HermesCtr for captures (codegen handles both).
    auto* result_type = lit.has_captures ? make_struct_type("HermesCtr") : make_ptr(false, u8_t());
    return make_expr(result_type, std::move(lit));
}

} // namespace logos::compiler
