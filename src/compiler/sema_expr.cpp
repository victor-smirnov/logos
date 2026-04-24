// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include "sema_impl.hpp"

#include <logos/hermes/type_registry.hpp>

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
        return make_expr(make_slice_type(u8_t()), lir::ELitStr{std::string(sv)});
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

        // ── Hermes typed container casts: &[T] as <I32>[] → Hermes. ──────
        if (target && TypeRef(target).kind() == LogosType::Kind::Struct &&
            (TypeRef(target).struct_name() == "HermesArr" || TypeRef(target).struct_name() == "HermesMap")) {
            if (TypeRef(target).struct_name() == "HermesArr") {
                auto* src = inner->type;
                if (!src || TypeRef(src).kind() != LogosType::Kind::Slice) {
                    error(std::format(
                        "'as <T>[]' requires a &[T] slice as source; got '{}'",
                        src ? type_str(src) : "?"));
                    return error_expr();
                }
                // Validate element type compatibility.
                // C6-fix3: elem_t must be non-null (resolve_type always sets it for valid types).
                const LogosType* elem_t = !TypeRef(target).type_args().empty()
                    ? TypeRef(target).type_args()[0] : nullptr;
                if (!elem_t) {
                    error("internal: <T>[] type missing element type");
                    return error_expr();
                }
                // C6-fix4: TypeRef(src).elem() must be non-null; don't skip type check silently.
                if (!TypeRef(src).elem()) {
                    error("'as <T>[]': source slice has unresolved element type");
                    return error_expr();
                }
                if (elem_t->kind != TypeRef(src).elem()->kind) {
                    error(std::format(
                        "'as <T>[]' element type mismatch: slice has '{}', target needs '{}'",
                        type_str(TypeRef(src).elem()), type_str(elem_t)));
                    return error_expr();
                }
                // Pick the stdlib builder function name.
                std::string build_fn;
                if      (TypeRef(elem_t).kind() == LogosType::Kind::I8)  build_fn = "hermes_build_array_i8";
                else if (TypeRef(elem_t).kind() == LogosType::Kind::U8)  build_fn = "hermes_build_array_u8";
                else if (TypeRef(elem_t).kind() == LogosType::Kind::I16) build_fn = "hermes_build_array_i16";
                else if (TypeRef(elem_t).kind() == LogosType::Kind::U16) build_fn = "hermes_build_array_u16";
                else if (TypeRef(elem_t).kind() == LogosType::Kind::U32) build_fn = "hermes_build_array_u32";
                else if (TypeRef(elem_t).kind() == LogosType::Kind::I32) build_fn = "hermes_build_array_i32";
                else if (TypeRef(elem_t).kind() == LogosType::Kind::I64) build_fn = "hermes_build_array_i64";
                else if (TypeRef(elem_t).kind() == LogosType::Kind::U64) build_fn = "hermes_build_array_u64";
                else if (TypeRef(elem_t).kind() == LogosType::Kind::F32) build_fn = "hermes_build_array_f32";
                else if (TypeRef(elem_t).kind() == LogosType::Kind::F64) build_fn = "hermes_build_array_f64";
                else {
                    error(std::format("'as <T>[]': unsupported element type '{}'; "
                                      "supported: i8/u8/i16/u16/i32/u32/i64/u64/f32/f64",
                                      type_str(elem_t)));
                    return error_expr();
                }
                // Result type: Hermes.
                auto* ctr_t = lookup_type_by_name("Hermes");
                if (!ctr_t) {
                    LogosType t{};
                    t.kind = LogosType::Kind::Struct;
                    t.struct_name = "Hermes";
                    ctr_t = pool_.alloc(std::move(t));
                }
                return make_expr(ctr_t,
                    lir::ECast{std::move(inner), std::move(build_fn)});
            }
            // fix5: explicit guard — outer if allows HermesArr||HermesMap; must be HermesMap here.
            if (TypeRef(target).struct_name() != "HermesMap") {
                error("internal: unexpected hermes container type in map cast path");
                return error_expr();
            }
            // HermesMap: source must be MapSliceI32 for <I32,AnyVal>{}.
            {
                auto* src = inner->type;
                const LogosType* key_t = !TypeRef(target).type_args().empty()
                    ? TypeRef(target).type_args()[0] : nullptr;
                const LogosType* val_t = TypeRef(target).type_args().size() > 1
                    ? TypeRef(target).type_args()[1] : nullptr;
                if (!key_t || !val_t) {
                    error("internal: <K,V>{} type missing key/val types");
                    return error_expr();
                }
                // Helper: check AnyVal val type.
                bool val_is_anyval = TypeRef(val_t).kind() == LogosType::Kind::Struct &&
                                     TypeRef(val_t).struct_name() == "AnyVal";
                std::string map_fn;
                struct MapVariant { LogosType::Kind key_kind; const char* slice_name; const char* fn_name; };
                static const MapVariant map_variants[] = {
                    {LogosType::Kind::I32, "MapSliceI32", "hermes_build_map_i32_anyval"},
                    {LogosType::Kind::U32, "MapSliceU32", "hermes_build_map_u32_anyval"},
                    {LogosType::Kind::I64, "MapSliceI64", "hermes_build_map_i64_anyval"},
                    {LogosType::Kind::U64, "MapSliceU64", "hermes_build_map_u64_anyval"},
                };
                bool found_map = false;
                if (val_is_anyval) {
                    for (auto& mv : map_variants) {
                        if (key_t->kind == mv.key_kind) {
                            if (!src || TypeRef(src).kind() != LogosType::Kind::Struct ||
                                TypeRef(src).struct_name() != mv.slice_name) {
                                error(std::format(
                                    "'as <{},AnyVal>{{}}' requires a {} as source; got '{}'",
                                    type_str(key_t), mv.slice_name,
                                    src ? type_str(src) : "?"));
                                return error_expr();
                            }
                            map_fn = mv.fn_name;
                            found_map = true;
                            break;
                        }
                    }
                }
                if (!found_map) {
                    // fix4: guard against calling type_str on error types — check kind first.
                    auto key_str = (TypeRef(key_t).kind() != LogosType::Kind::Error) ? type_str(key_t) : "?";
                    auto val_str = (TypeRef(val_t).kind() != LogosType::Kind::Error) ? type_str(val_t) : "?";
                    error(std::format(
                        "'as <{},{}>{{}}': unsupported combination; supported: <I32/U32/I64/U64,AnyVal>",
                        key_str, val_str));
                    return error_expr();
                }
                auto* ctr_t = lookup_type_by_name("Hermes");
                if (!ctr_t) {
                    LogosType t{};
                    t.kind = LogosType::Kind::Struct;
                    t.struct_name = "Hermes";
                    ctr_t = pool_.alloc(std::move(t));
                }
                return make_expr(ctr_t,
                    lir::ECast{std::move(inner), std::move(map_fn)});
            }
        }

        // ── Ordinary numeric/pointer cast. ────────────────────────────────────
        if (inner->type && target &&
            inner->type->kind != LogosType::Kind::Error &&
            TypeRef(target).kind() != LogosType::Kind::Error) {
            bool src_agg = inner->type->kind == LogosType::Kind::Struct ||
                           inner->type->kind == LogosType::Kind::Array  ||
                           inner->type->kind == LogosType::Kind::Tuple  ||
                           inner->type->kind == LogosType::Kind::Enum;
            bool tgt_scalar = TypeRef(target).kind() == LogosType::Kind::I32  ||
                              TypeRef(target).kind() == LogosType::Kind::I64  ||
                              TypeRef(target).kind() == LogosType::Kind::U8   ||
                              TypeRef(target).kind() == LogosType::Kind::I8   ||
                              TypeRef(target).kind() == LogosType::Kind::I16  ||
                              TypeRef(target).kind() == LogosType::Kind::U16  ||
                              TypeRef(target).kind() == LogosType::Kind::I24  ||
                              TypeRef(target).kind() == LogosType::Kind::I56  ||
                              TypeRef(target).kind() == LogosType::Kind::U24  ||
                              TypeRef(target).kind() == LogosType::Kind::U56  ||
                              TypeRef(target).kind() == LogosType::Kind::U32  ||
                              TypeRef(target).kind() == LogosType::Kind::U64  ||
                              TypeRef(target).kind() == LogosType::Kind::I128 ||
                              TypeRef(target).kind() == LogosType::Kind::U128 ||
                              TypeRef(target).kind() == LogosType::Kind::F64  ||
                              TypeRef(target).kind() == LogosType::Kind::F32  ||
                              TypeRef(target).kind() == LogosType::Kind::Bool ||
                              TypeRef(target).kind() == LogosType::Kind::Ptr;
            // C-style enum -> integer/bool is allowed (discriminant cast).
            bool src_is_cstyle_enum = false;
            if (inner->type->kind == LogosType::Kind::Enum) {
                auto [epkg_cast, esi_cast] = find_enum_by_name(inner->type->enum_name);
                auto eit = esi_cast ? enums_.find(sema_key(epkg_cast, inner->type->enum_name)) : enums_.end();
                if (eit == enums_.end()) eit = enums_.find(inner->type->enum_name);
                if (eit != enums_.end()) {
                    bool has_payload = false;
                    for (auto& vv : eit->second.variants)
                        if (!vv.payload_types.empty()) { has_payload = true; break; }
                    src_is_cstyle_enum = !has_payload;
                }
            }
            if (src_agg && tgt_scalar && !src_is_cstyle_enum)
                error(std::format("cannot cast '{}' to '{}'",
                      type_str(inner->type), type_str(target)));
            // str (Slice<u8>) -> *mut u8 is unsound: str points to rodata.
            bool src_is_str = inner->type->kind == LogosType::Kind::Slice &&
                              inner->type->elem &&
                              inner->type->elem->kind == LogosType::Kind::U8;
            bool tgt_is_mut_ptr = TypeRef(target).kind() == LogosType::Kind::Ptr &&
                                  TypeRef(target).mut_ptr() &&
                                  TypeRef(target).pointee() &&
                                  TypeRef(target).pointee()->kind == LogosType::Kind::U8;
            if (src_is_str && tgt_is_mut_ptr)
                error("cannot cast 'str' to '*mut u8': str data is read-only; use '*const u8'");
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
            if (TypeRef(vt).kind() == LogosType::Kind::Array)
                return make_expr(make_ref(true, TypeRef(vt).elem().raw()), lir::EAddrOf{std::string(var_name)});
            return make_expr(make_ref(true, vt), lir::EAddrOf{std::string(var_name)});
        }
        // &mut <expr> — temporary materialization
        auto inner = lower_expr(child);
        if (inner->type->kind == LogosType::Kind::Error) return error_expr();
        return make_expr(make_ref(true, inner->type),
            lir::EAddrOfTemp{std::move(inner), true});
    }
    case la::TRY_EXPR: {
        // expr? — two flavours:
        //   Result<T, E>  →  extract Ok(v), early-return Err(e) in a fn : Result<?, E>
        //   Option<T>     →  extract Some(v), early-return None in a fn : Option<?>
        auto inner = expr.has_key(la::VALUE)
            ? lower_expr(map_of(expr.get(la::VALUE.code)))
            : error_expr();
        auto* inner_t = inner->type;
        bool is_result = TypeRef(inner_t).kind() == LogosType::Kind::Enum
                         && TypeRef(inner_t).enum_name() == "Result"
                         && TypeRef(inner_t).type_args().size() >= 2;
        bool is_option = TypeRef(inner_t).kind() == LogosType::Kind::Enum
                         && TypeRef(inner_t).enum_name() == "Option"
                         && TypeRef(inner_t).type_args().size() >= 1;
        if (!is_result && !is_option) {
            error("'?' operator requires a Result<T, E> or Option<T> expression");
            return error_expr();
        }
        if (!ret_type_ || TypeRef(ret_type_).kind() != LogosType::Kind::Enum
            || TypeRef(ret_type_).enum_name() != TypeRef(inner_t).enum_name()) {
            if (is_option)
                error("'?' on Option used in function that does not return Option<T>");
            else
                error("'?' operator used in function that does not return Result<T, E>");
            return error_expr();
        }
        // Find the "ok-like" and "err-like" discriminants from the enum def.
        // Result: Ok/Err.  Option: Some/None.
        int32_t ok_disc = 0, err_disc = 1;
        const char* ok_name  = is_option ? "Some" : "Ok";
        const char* err_name = is_option ? "None" : "Err";
        auto [epkg_res, esi_res] = find_enum_by_name(TypeRef(inner_t).enum_name());
        auto eit = esi_res ? enums_.find(sema_key(epkg_res, TypeRef(inner_t).enum_name())) : enums_.end();
        if (eit == enums_.end()) eit = enums_.find(TypeRef(inner_t).enum_name());
        if (eit != enums_.end()) {
            for (auto& v : eit->second.variants) {
                if (v.name == ok_name)  ok_disc  = v.value;
                if (v.name == err_name) err_disc = v.value;
            }
        }
        auto* ok_type = TypeRef(inner_t).type_args()[0];  // T
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
    case la::LIST_COMP:    return lower_list_comp(expr);
    case la::MAP_COMP:     return lower_map_comp(expr);
    case la::HERMES_LIST_COMP: return lower_hermes_list_comp(expr);
    case la::HERMES_MAP_COMP:  return lower_hermes_map_comp(expr);
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
            if (TypeRef(et).kind() == LogosType::Kind::IntLit) {
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
        if (TypeRef(recv_tuple_type).kind() != LogosType::Kind::Tuple) {
            error(std::format("tuple index on non-tuple type '{}'", type_str(recv->type)));
            return error_expr();
        }
        auto sv = str_of(expr.get(la::FIELD.code));
        uint32_t idx = (uint32_t)parse_int_literal(sv);
        if (idx >= TypeRef(recv_tuple_type).tuple_elems().size()) {
            error(std::format("tuple index {} out of range (tuple has {} elements)",
                  idx, TypeRef(recv_tuple_type).tuple_elems().size()));
            return error_expr();
        }
        auto* elem_t = TypeRef(recv_tuple_type).tuple_elems()[idx];
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
    if (TypeRef(lt).kind() == LogosType::Kind::Struct) {
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

    if (TypeRef(lt).kind() == LogosType::Kind::Error || TypeRef(rt).kind() == LogosType::Kind::Error) {
        result_type = error_t();
    } else if (op == "&&" || op == "||") {
        if (TypeRef(lt).kind() != LogosType::Kind::Bool)
            error(std::format("operator '{}': left must be bool, got {}", op, type_str(lt)));
        if (TypeRef(rt).kind() != LogosType::Kind::Bool)
            error(std::format("operator '{}': right must be bool, got {}", op, type_str(rt)));
        result_type = bool_t();
    } else if (op == "==" || op == "!=" ||
               op == "<"  || op == "<=" || op == ">" || op == ">=") {
        // Allow pointer-vs-integer-literal comparison (null check: ptr == 0)
        bool ptr_null_cmp =
            (TypeRef(lt).kind() == LogosType::Kind::Ptr && TypeRef(rt).kind() == LogosType::Kind::IntLit) ||
            (TypeRef(rt).kind() == LogosType::Kind::Ptr && TypeRef(lt).kind() == LogosType::Kind::IntLit);
        bool ok = ptr_null_cmp || types_compatible(lt, rt) || types_compatible(rt, lt);
        if (!ok)
            error(std::format("operator '{}': type mismatch ({} vs {})",
                  op, type_str(lt), type_str(rt)));
        if (ptr_null_cmp) {
            const lir::LExpr* lit_expr = (TypeRef(lt).kind() == LogosType::Kind::IntLit) ? lhs.get() : rhs.get();
            if (auto v = get_intlit_value(lit_expr)) {
                if (*v != 0)
                    error(std::format(
                        "operator '{}': pointer can only be compared with integer literal 0", op));
            }
        }
        // Detect comparisons against IntLit values that can't fit in the other operand.
        // E.g. x: i32 == 10000000000 — the literal can never equal any i32 value.
        if (TypeRef(lt).kind() == LogosType::Kind::IntLit && is_integer_kind(rt->kind)) {
            if (auto v = get_intlit_value(lhs.get()))
                if (!intlit_fits(*v, rt->kind))
                    error(std::format("operator '{}': literal value {} does not fit in {}",
                          op, *v, type_str(rt)));
        } else if (TypeRef(rt).kind() == LogosType::Kind::IntLit && is_integer_kind(lt->kind)) {
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
            if (TypeRef(lt).kind() == LogosType::Kind::TypeVar) result_type = lt;
            else if (TypeRef(rt).kind() == LogosType::Kind::TypeVar) result_type = rt;
            // FloatLit/IntLit on LHS defers to the concrete type on RHS (e.g. 1.0 + x_f32 → f32).
            else result_type = unify_numeric(lt, rt);
        } else {
            if (!types_compatible(lt, rt) && !types_compatible(rt, lt))
                error(std::format("operator '{}': type mismatch ({} vs {})",
                      op, type_str(lt), type_str(rt)));
            result_type = unify_int(lt, rt);
            // Check IntLit operand fits in the concrete type of the other operand.
            if (TypeRef(lt).kind() == LogosType::Kind::IntLit && TypeRef(rt).kind() != LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(lhs.get()))
                    if (!intlit_fits(*v, rt->kind))
                        error(std::format("operator '{}': left value {} does not fit in {}",
                              op, *v, type_str(rt)));
            if (TypeRef(rt).kind() == LogosType::Kind::IntLit && TypeRef(lt).kind() != LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(rhs.get()))
                    if (!intlit_fits(*v, lt->kind))
                        error(std::format("operator '{}': right value {} does not fit in {}",
                              op, *v, type_str(lt)));
        }
    } else if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
        // Bitwise and shift operators — require integer operands.
        if (!is_integer_kind(lt->kind) && TypeRef(lt).kind() != LogosType::Kind::IntLit)
            error(std::format("operator '{}': left must be integer, got {}", op, type_str(lt)));
        if (!is_integer_kind(rt->kind) && TypeRef(rt).kind() != LogosType::Kind::IntLit)
            error(std::format("operator '{}': right must be integer, got {}", op, type_str(rt)));
        result_type = unify_int(lt, rt);
        // Check IntLit operand fits in the concrete type of the other operand.
        if (TypeRef(lt).kind() == LogosType::Kind::IntLit && TypeRef(rt).kind() != LogosType::Kind::IntLit)
            if (auto v = get_intlit_value(lhs.get()))
                if (!intlit_fits(*v, rt->kind))
                    error(std::format("operator '{}': left value {} does not fit in {}",
                          op, *v, type_str(rt)));
        if (TypeRef(rt).kind() == LogosType::Kind::IntLit && TypeRef(lt).kind() != LogosType::Kind::IntLit)
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
            if (TypeRef(vt).kind() == LogosType::Kind::Array) {
                auto addr = make_expr(make_ref(false, TypeRef(vt).elem().raw()), lir::EAddrOf{std::string(var_name)});
                auto len  = make_expr(prim(LogosType::Kind::I64), lir::ELitInt{(int64_t)TypeRef(vt).arr_size()});
                return make_expr(make_slice_type(TypeRef(vt).elem().raw()),
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
    if (TypeRef(vt).kind() == LogosType::Kind::Error)
        return make_expr(error_t(), lir::EUnary{std::string(op), std::move(operand)});

    // Unary operator overloading for struct types
    if (TypeRef(vt).kind() == LogosType::Kind::Struct) {
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
        if (TypeRef(vt).kind() == LogosType::Kind::Bool) {
            result_type = bool_t();
        } else if (is_integer_kind(vt->kind) || TypeRef(vt).kind() == LogosType::Kind::IntLit) {
            // Bitwise NOT (~x) on integer types
            result_type = (TypeRef(vt).kind() == LogosType::Kind::IntLit) ? i32_t() : vt;
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
    if (TypeRef(vt).kind() == LogosType::Kind::Error)
        return make_expr(error_t(), lir::EDeref{std::move(operand)});
    if (TypeRef(vt).kind() != LogosType::Kind::Ptr &&
        TypeRef(vt).kind() != LogosType::Kind::Ref &&
        TypeRef(vt).kind() != LogosType::Kind::MutRef) {
        error(std::format("dereference of non-pointer type {}", type_str(vt)));
        return make_expr(error_t(), lir::EDeref{std::move(operand)});
    }
    // Raw pointer deref requires unsafe context
    if (TypeRef(vt).kind() == LogosType::Kind::Ptr && !inside_unsafe_)
        error("dereference of raw pointer requires unsafe context");
    auto* res = TypeRef(vt).pointee().raw() ? TypeRef(vt).pointee().raw() : error_t();
    return make_expr(res, lir::EDeref{std::move(operand)});
}

lir::LExprPtr SemaChecker::lower_call(TinyMapView node) {
    auto callee = str_of(node.get(la::CALLEE.code));

    // Check if callee is a closure or fn-ptr variable
    auto* callee_type = lookup(callee);
    bool is_closure = callee_type && TypeRef(callee_type).kind() == LogosType::Kind::Closure;
    bool is_fn_ptr  = callee_type && TypeRef(callee_type).kind() == LogosType::Kind::FnPtr;
    if (is_closure || is_fn_ptr) {
        std::vector<lir::LExprPtr> arg_exprs;
        if (node.has_key(la::ARGS)) {
            auto args = arr_of(node.get(la::ARGS.code));
            for (uint64_t i = 0; i < args.size(); ++i)
                arg_exprs.push_back(lower_expr(map_of(args.get(i))));
        }
        uint64_t n_args = arg_exprs.size();
        uint64_t n_params = TypeRef(callee_type).closure_params().size();
        const char* kind_str = is_fn_ptr ? "fn-ptr call" : "closure call";
        if (n_args != n_params) {
            error(std::format("{}: expected {} args, got {}", kind_str, n_params, n_args));
        } else {
            for (uint64_t i = 0; i < n_args; ++i) {
                auto* at = arg_exprs[i]->type;
                auto* pt = TypeRef(callee_type).closure_params()[i];
                if (TypeRef(at).kind() != LogosType::Kind::Error &&
                    TypeRef(pt).kind() != LogosType::Kind::Error &&
                    !types_compatible(at, pt)) {
                    error(std::format("{} arg {}: expected {}, got {}",
                          kind_str, i + 1, type_str(pt), type_str(at)));
                }
            }
        }
        auto callee_expr = make_expr(callee_type, lir::EVarRef{std::string(callee)});
        const LogosType* ret = TypeRef(callee_type).closure_ret().raw() ? TypeRef(callee_type).closure_ret().raw() : void_t();
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

    // str_from_raw(ptr: *const u8, len: i64) -> str — compiler intrinsic.
    // Constructs a str fat-pointer; codegen in mlir_gen_expr.cpp handles emission.
    if (callee == "str_from_raw") {
        if (n_args != 2)
            error("str_from_raw requires exactly 2 arguments: (ptr: *const u8, len: i64)");
        auto* str_t = make_slice_type(u8_t());
        lir::ECall ec;
        ec.callee = "str_from_raw";
        for (auto& a : arg_exprs) ec.args.push_back(std::move(a));
        return make_expr(str_t, std::move(ec));
    }

    // Bitwise intrinsics on u64 — map to LLVM intrinsics in codegen.
    //   popcount_u64(x: u64)        -> u32   (llvm.ctpop)
    //   leading_zeros_u64(x: u64)   -> u32   (llvm.ctlz,  is_zero_poison=false)
    //   trailing_zeros_u64(x: u64)  -> u32   (llvm.cttz,  is_zero_poison=false)
    //   bswap_u64(x: u64)           -> u64   (llvm.bswap)
    //   bitreverse_u64(x: u64)      -> u64   (llvm.bitreverse)
    if (callee == "popcount_u64"        || callee == "leading_zeros_u64" ||
        callee == "trailing_zeros_u64"  || callee == "bswap_u64"         ||
        callee == "bitreverse_u64") {
        if (n_args != 1)
            error(std::format("{} requires exactly 1 u64 argument", callee));
        const LogosType* ret = (callee == "bswap_u64" || callee == "bitreverse_u64")
                               ? prim(LogosType::Kind::U64)
                               : prim(LogosType::Kind::U32);
        lir::ECall ec;
        ec.callee = callee;
        for (auto& a : arg_exprs) ec.args.push_back(std::move(a));
        return make_expr(ret, std::move(ec));
    }

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
                    if (TypeRef(at).kind() != LogosType::Kind::Error &&
                        TypeRef(pt).kind() != LogosType::Kind::Error &&
                        !types_compatible(at, pt))
                        error(std::format("call to '{}' arg {}: expected {}, got {}",
                              callee, i + 1, type_str(pt), type_str(at)));
                    if (TypeRef(at).kind() == LogosType::Kind::IntLit && TypeRef(pt).kind() != LogosType::Kind::Error)
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
                try_coerce_closure_to_fnptr(arg_exprs[i], exact_fi->param_types[i]);
                auto* at = arg_exprs[i]->type;
                auto* pt = exact_fi->param_types[i];
                if (TypeRef(at).kind() != LogosType::Kind::Error &&
                    TypeRef(pt).kind() != LogosType::Kind::Error &&
                    !types_compatible(at, pt))
                    error(std::format("call to '{}' arg {}: expected {}, got {}",
                          callee, i + 1, type_str(pt), type_str(at)));
                if (TypeRef(at).kind() == LogosType::Kind::IntLit && TypeRef(pt).kind() != LogosType::Kind::Error)
                    if (auto v = get_intlit_value(arg_exprs[i].get()))
                        if (!intlit_fits(*v, pt->kind))
                            error(std::format("call to '{}' arg {}: value {} does not fit in {}",
                                  callee, i + 1, *v, type_str(pt)));
                // Check array literal elements against narrow array param type.
                if (TypeRef(at).kind() == LogosType::Kind::Array && TypeRef(pt).kind() == LogosType::Kind::Array && TypeRef(pt).elem())
                    if (auto* al = std::get_if<lir::EArrLit>(&arg_exprs[i]->kind))
                        for (size_t ei = 0; ei < al->elems.size(); ++ei)
                            if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(al->elems[ei].get()))
                                    if (!intlit_fits(*v, TypeRef(pt).elem()->kind))
                                        error(std::format("call to '{}' arg {}: array element {}: value {} does not fit in {}",
                                              callee, i + 1, ei, *v, type_str(TypeRef(pt).elem())));
                // Check tuple literal elements against narrow tuple param element types.
                if (TypeRef(at).kind() == LogosType::Kind::Tuple && TypeRef(pt).kind() == LogosType::Kind::Tuple)
                    if (auto* tl = std::get_if<lir::ETupleLit>(&arg_exprs[i]->kind))
                        for (size_t ei = 0; ei < tl->elems.size() && ei < TypeRef(pt).tuple_elems().size(); ++ei) {
                            if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(tl->elems[ei].get()))
                                    if (TypeRef(pt).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->kind))
                                        error(std::format("call to '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                              callee, i + 1, ei, *v, type_str(TypeRef(pt).tuple_elems()[ei])));
                            if (TypeRef(pt).tuple_elems()[ei] && TypeRef(pt).tuple_elems()[ei]->kind == LogosType::Kind::Array &&
                                TypeRef(pt).tuple_elems()[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                                if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                        if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(ial->elems[ii].get()))
                                                if (!intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->elem->kind))
                                                    error(std::format("call to '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                          callee, i + 1, ei, ii, *v, type_str(TypeRef(pt).tuple_elems()[ei]->elem)));
                            if (TypeRef(pt).tuple_elems()[ei] && TypeRef(pt).tuple_elems()[ei]->kind == LogosType::Kind::Tuple &&
                                tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                                if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(pt).tuple_elems()[ei]->tuple_elems.size(); ++ii)
                                        if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(itl->elems[ii].get()))
                                                if (TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii] && !intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii]->kind))
                                                    error(std::format("call to '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                          callee, i + 1, ei, ii, *v, type_str(TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii])));
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
            if (t && (TypeRef(t).kind() == LogosType::Kind::TypeVar ||
                      TypeRef(t).kind() == LogosType::Kind::AssocType)) {
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
                try_coerce_closure_to_fnptr(arg_exprs[i], fi.param_types[i]);
                auto* at = arg_exprs[i]->type;
                auto* pt = fi.param_types[i];
                if (TypeRef(at).kind() != LogosType::Kind::Error &&
                    TypeRef(pt).kind() != LogosType::Kind::Error &&
                    !types_compatible(at, pt))
                    error(std::format("call to '{}' arg {}: expected {}, got {}",
                          callee, i + 1, type_str(pt), type_str(at)));
                if (TypeRef(at).kind() == LogosType::Kind::IntLit && TypeRef(pt).kind() != LogosType::Kind::Error)
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
            try_coerce_closure_to_fnptr(arg_exprs[i], fi.param_types[i]);
            auto* at = arg_exprs[i]->type;
            auto* pt = fi.param_types[i];
            if (TypeRef(at).kind() != LogosType::Kind::Error &&
                TypeRef(pt).kind() != LogosType::Kind::Error &&
                !types_compatible(at, pt))
                error(std::format("call to '{}' arg {}: expected {}, got {}",
                      callee, i + 1, type_str(pt), type_str(at)));
            if (TypeRef(at).kind() == LogosType::Kind::IntLit && TypeRef(pt).kind() != LogosType::Kind::Error)
                if (auto v = get_intlit_value(arg_exprs[i].get()))
                    if (!intlit_fits(*v, pt->kind))
                        error(std::format("call to '{}' arg {}: value {} does not fit in {}",
                              callee, i + 1, *v, type_str(pt)));
            // Check array literal elements against narrow array param type.
            if (TypeRef(at).kind() == LogosType::Kind::Array && TypeRef(pt).kind() == LogosType::Kind::Array && TypeRef(pt).elem())
                if (auto* al = std::get_if<lir::EArrLit>(&arg_exprs[i]->kind))
                    for (size_t ei = 0; ei < al->elems.size(); ++ei)
                        if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(al->elems[ei].get()))
                                if (!intlit_fits(*v, TypeRef(pt).elem()->kind))
                                    error(std::format("call to '{}' arg {}: array element {}: value {} does not fit in {}",
                                          callee, i + 1, ei, *v, type_str(TypeRef(pt).elem())));
            // Check tuple literal elements against narrow tuple param element types.
            if (TypeRef(at).kind() == LogosType::Kind::Tuple && TypeRef(pt).kind() == LogosType::Kind::Tuple)
                if (auto* tl = std::get_if<lir::ETupleLit>(&arg_exprs[i]->kind))
                    for (size_t ei = 0; ei < tl->elems.size() && ei < TypeRef(pt).tuple_elems().size(); ++ei) {
                        if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(tl->elems[ei].get()))
                                if (TypeRef(pt).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->kind))
                                    error(std::format("call to '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                          callee, i + 1, ei, *v, type_str(TypeRef(pt).tuple_elems()[ei])));
                        if (TypeRef(pt).tuple_elems()[ei] && TypeRef(pt).tuple_elems()[ei]->kind == LogosType::Kind::Array &&
                            TypeRef(pt).tuple_elems()[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                            if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                    if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(ial->elems[ii].get()))
                                            if (!intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->elem->kind))
                                                error(std::format("call to '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                      callee, i + 1, ei, ii, *v, type_str(TypeRef(pt).tuple_elems()[ei]->elem)));
                        if (TypeRef(pt).tuple_elems()[ei] && TypeRef(pt).tuple_elems()[ei]->kind == LogosType::Kind::Tuple &&
                            tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                            if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(pt).tuple_elems()[ei]->tuple_elems.size(); ++ii)
                                    if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(itl->elems[ii].get()))
                                            if (TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii] && !intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii]->kind))
                                                error(std::format("call to '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      callee, i + 1, ei, ii, *v, type_str(TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii])));
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
                     StrMap<const LogosType*>& bindings) {
    if (!formal || !actual) return;
    if (TypeRef(actual).kind() == LogosType::Kind::Error ||
        TypeRef(formal).kind() == LogosType::Kind::Error) return;

    // Widen IntLit to i32 / FloatLit to f64 before any binding
    const LogosType* actual_norm = actual;
    if (TypeRef(actual).kind() == LogosType::Kind::IntLit)
        actual_norm = prim(LogosType::Kind::I32);
    else if (TypeRef(actual).kind() == LogosType::Kind::FloatLit)
        actual_norm = prim(LogosType::Kind::F64);

    if (TypeRef(formal).kind() == LogosType::Kind::TypeVar) {
        if (TypeRef(formal).type_var_name() == "Self") return;  // skip implicit Self
        if (!bindings.count(TypeRef(formal).type_var_name()))
            bindings[std::string(TypeRef(formal).type_var_name())] = actual_norm;
        return;
    }

    switch (TypeRef(formal).kind()) {
    case LogosType::Kind::Ptr:
        if (TypeRef(actual_norm).kind() == LogosType::Kind::Ptr)
            unify_types(TypeRef(formal).pointee().raw(), TypeRef(actual_norm).pointee().raw(), bindings);
        else if (TypeRef(actual_norm).kind() == LogosType::Kind::Ref ||
                 TypeRef(actual_norm).kind() == LogosType::Kind::MutRef)
            unify_types(TypeRef(formal).pointee().raw(), TypeRef(actual_norm).pointee().raw(), bindings);
        break;
    case LogosType::Kind::Ref:
    case LogosType::Kind::MutRef:
        if (TypeRef(actual_norm).kind() == LogosType::Kind::Ref ||
            TypeRef(actual_norm).kind() == LogosType::Kind::MutRef ||
            TypeRef(actual_norm).kind() == LogosType::Kind::Ptr)
            unify_types(TypeRef(formal).pointee().raw(), TypeRef(actual_norm).pointee().raw(), bindings);
        break;
    case LogosType::Kind::Array:
        if (TypeRef(actual_norm).kind() == LogosType::Kind::Array)
            unify_types(TypeRef(formal).elem().raw(), TypeRef(actual_norm).elem().raw(), bindings);
        break;
    case LogosType::Kind::Slice:
        if (TypeRef(actual_norm).kind() == LogosType::Kind::Slice)
            unify_types(TypeRef(formal).elem().raw(), TypeRef(actual_norm).elem().raw(), bindings);
        break;
    case LogosType::Kind::Struct:
        if (TypeRef(actual_norm).kind() == LogosType::Kind::Struct &&
            TypeRef(formal).struct_name() == TypeRef(actual_norm).struct_name()) {
            for (size_t i = 0; i < TypeRef(formal).type_args().size() &&
                                i < TypeRef(actual_norm).type_args().size(); ++i)
                unify_types(TypeRef(formal).type_args()[i], TypeRef(actual_norm).type_args()[i], bindings);
        }
        break;
    case LogosType::Kind::Tuple:
        if (TypeRef(actual_norm).kind() == LogosType::Kind::Tuple) {
            for (size_t i = 0; i < TypeRef(formal).tuple_elems().size() &&
                                i < TypeRef(actual_norm).tuple_elems().size(); ++i)
                unify_types(TypeRef(formal).tuple_elems()[i], TypeRef(actual_norm).tuple_elems()[i], bindings);
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
    StrMap<const LogosType*> bindings(context.begin(), context.end());
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
            if (TypeRef(t).kind() == LogosType::Kind::IntLit)
                t = prim(LogosType::Kind::I32);
            else if (TypeRef(t).kind() == LogosType::Kind::FloatLit)
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
    StrMap<const LogosType*> subst;
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
            auto* pt = subst_type_sema(fi.param_types[i], subst);
            try_coerce_closure_to_fnptr(arg_exprs[i], pt);
            auto* at = arg_exprs[i]->type;
            if (TypeRef(at).kind() != LogosType::Kind::Error &&
                TypeRef(pt).kind() != LogosType::Kind::Error &&
                TypeRef(pt).kind() != LogosType::Kind::TypeVar &&
                !types_compatible(at, pt))
                error(std::format("call to '{}' arg {}: expected {}, got {}",
                      callee_diag, i + 1, type_str(pt), type_str(at)));
            if (TypeRef(at).kind() == LogosType::Kind::IntLit && TypeRef(pt).kind() != LogosType::Kind::Error &&
                TypeRef(pt).kind() != LogosType::Kind::TypeVar)
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
                auto* pt = subst_type_sema(fi.param_types[i], subst);
                try_coerce_closure_to_fnptr(arg_exprs[i], pt);
                auto* at = arg_exprs[i]->type;
                if (TypeRef(at).kind() != LogosType::Kind::Error &&
                    TypeRef(pt).kind() != LogosType::Kind::Error &&
                    TypeRef(pt).kind() != LogosType::Kind::TypeVar &&
                    TypeRef(pt).kind() != LogosType::Kind::AssocType &&
                    !types_compatible(at, pt))
                    error(std::format("call to '{}' arg {}: expected {}, got {}",
                          callee_diag, i + 1, type_str(pt), type_str(at)));
                if (TypeRef(at).kind() == LogosType::Kind::IntLit && TypeRef(pt).kind() != LogosType::Kind::Error &&
                    TypeRef(pt).kind() != LogosType::Kind::TypeVar)
                    if (auto v = get_intlit_value(arg_exprs[i].get()))
                        if (!intlit_fits(*v, pt->kind))
                            error(std::format("call to '{}' arg {}: value {} does not fit in {}",
                                  callee_diag, i + 1, *v, type_str(pt)));
                // Check array literal elements against narrow array param type.
                if (TypeRef(at).kind() == LogosType::Kind::Array && TypeRef(pt).kind() == LogosType::Kind::Array && TypeRef(pt).elem())
                    if (auto* al = std::get_if<lir::EArrLit>(&arg_exprs[i]->kind))
                        for (size_t ei = 0; ei < al->elems.size(); ++ei)
                            if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(al->elems[ei].get()))
                                    if (!intlit_fits(*v, TypeRef(pt).elem()->kind))
                                        error(std::format("call to '{}' arg {}: array element {}: value {} does not fit in {}",
                                              callee_diag, i + 1, ei, *v, type_str(TypeRef(pt).elem())));
                // Check tuple literal elements against narrow tuple param element types.
                if (TypeRef(at).kind() == LogosType::Kind::Tuple && TypeRef(pt).kind() == LogosType::Kind::Tuple)
                    if (auto* tl = std::get_if<lir::ETupleLit>(&arg_exprs[i]->kind))
                        for (size_t ei = 0; ei < tl->elems.size() && ei < TypeRef(pt).tuple_elems().size(); ++ei) {
                            if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(tl->elems[ei].get()))
                                    if (TypeRef(pt).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->kind))
                                        error(std::format("call to '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                              callee_diag, i + 1, ei, *v, type_str(TypeRef(pt).tuple_elems()[ei])));
                            if (TypeRef(pt).tuple_elems()[ei] && TypeRef(pt).tuple_elems()[ei]->kind == LogosType::Kind::Array &&
                                TypeRef(pt).tuple_elems()[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                                if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                        if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(ial->elems[ii].get()))
                                                if (!intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->elem->kind))
                                                    error(std::format("call to '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                          callee_diag, i + 1, ei, ii, *v, type_str(TypeRef(pt).tuple_elems()[ei]->elem)));
                            if (TypeRef(pt).tuple_elems()[ei] && TypeRef(pt).tuple_elems()[ei]->kind == LogosType::Kind::Tuple &&
                                tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                                if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(pt).tuple_elems()[ei]->tuple_elems.size(); ++ii)
                                        if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(itl->elems[ii].get()))
                                                if (TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii] && !intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii]->kind))
                                                    error(std::format("call to '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                          callee_diag, i + 1, ei, ii, *v, type_str(TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii])));
                        }
            }
        }
    }

    return make_expr(ret, lir::ECall{callee, std::move(type_args), std::move(arg_exprs)});
}

lir::LExprPtr SemaChecker::lower_generic_call(TinyMapView node) {
    auto callee = str_of(node.get(la::CALLEE.code));

    // ── Type-trait intrinsics (C++26 type_traits style, compile-time folded) ──
    // Helper: collect resolved type args.
    auto collect_type_args = [&]() -> std::vector<const LogosType*> {
        std::vector<const LogosType*> out;
        if (!node.has_key(la::TYPE_PARAMS)) return out;
        auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
        if (!tplist.has_key(la::ITEMS)) return out;
        auto items = arr_of(tplist.get(la::ITEMS.code));
        for (size_t i = 0; i < items.size(); ++i)
            out.push_back(resolve_type(map_of(items.get(i))));
        return out;
    };
    auto bool_lit = [&](bool v) {
        return make_expr(prim(LogosType::Kind::Bool), lir::ELitInt{v ? 1LL : 0LL});
    };

    if (callee == "is_same") {
        auto ts = collect_type_args();
        if (ts.size() != 2 || !ts[0] || !ts[1]) {
            error("is_same::<T1,T2>() requires exactly two type arguments");
            return error_expr();
        }
        return bool_lit(types_equal(*ts[0], *ts[1]));
    }
    if (callee == "is_ptr" || callee == "is_ref" || callee == "is_mut_ref" ||
        callee == "is_struct" || callee == "is_zoned" || callee == "is_enum" ||
        callee == "is_tuple" || callee == "is_slice" || callee == "is_array" ||
        callee == "is_integer" || callee == "is_signed" || callee == "is_unsigned" ||
        callee == "is_float" || callee == "is_bool" || callee == "is_primitive") {
        auto ts = collect_type_args();
        if (ts.size() != 1 || !ts[0]) {
            error(std::string(callee) + "::<T>() requires exactly one type argument");
            return error_expr();
        }
        const LogosType* t = ts[0];
        using K = LogosType::Kind;
        bool r = false;
        if      (callee == "is_ptr")       r = (t->kind == K::Ptr);
        else if (callee == "is_ref")       r = (t->kind == K::Ref);
        else if (callee == "is_mut_ref")   r = (t->kind == K::MutRef);
        else if (callee == "is_struct")    r = (t->kind == K::Struct);
        else if (callee == "is_zoned")  r = (t->kind == K::ZonedStruct);
        else if (callee == "is_enum")      r = (t->kind == K::Enum);
        else if (callee == "is_tuple")     r = (t->kind == K::Tuple);
        else if (callee == "is_slice")     r = (t->kind == K::Slice);
        else if (callee == "is_array")     r = (t->kind == K::Array);
        else if (callee == "is_bool")      r = (t->kind == K::Bool);
        else if (callee == "is_float")     r = (t->kind == K::F32 || t->kind == K::F64);
        else if (callee == "is_signed")    r = (t->kind == K::I8 || t->kind == K::I16 || t->kind == K::I24 ||
                                                t->kind == K::I32 || t->kind == K::I56 || t->kind == K::I64 ||
                                                t->kind == K::I128);
        else if (callee == "is_unsigned")  r = (t->kind == K::U8 || t->kind == K::U16 || t->kind == K::U24 ||
                                                t->kind == K::U32 || t->kind == K::U56 || t->kind == K::U64 ||
                                                t->kind == K::U128);
        else if (callee == "is_integer")   r = (t->kind == K::I8 || t->kind == K::I16 || t->kind == K::I24 ||
                                                t->kind == K::I32 || t->kind == K::I56 || t->kind == K::I64 ||
                                                t->kind == K::I128 ||
                                                t->kind == K::U8 || t->kind == K::U16 || t->kind == K::U24 ||
                                                t->kind == K::U32 || t->kind == K::U56 || t->kind == K::U64 ||
                                                t->kind == K::U128);
        else if (callee == "is_primitive") r = (t->kind == K::Bool || t->kind == K::F32 || t->kind == K::F64 ||
                                                t->kind == K::I8 || t->kind == K::I16 || t->kind == K::I24 ||
                                                t->kind == K::I32 || t->kind == K::I56 || t->kind == K::I64 ||
                                                t->kind == K::I128 ||
                                                t->kind == K::U8 || t->kind == K::U16 || t->kind == K::U24 ||
                                                t->kind == K::U32 || t->kind == K::U56 || t->kind == K::U64 ||
                                                t->kind == K::U128);
        return bool_lit(r);
    }

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
        if (TypeRef(elem).kind() == LogosType::Kind::ZonedStruct && TypeRef(elem).type_args().empty()) {
            // Resolve package for this type (needed for both explicit and auto-hash paths).
            // Prefer TypeRef(elem).pkg_name() (set by resolve_type via find_datatype_by_name).
            std::string pkg;
            if (!TypeRef(elem).pkg_name().empty()) {
                pkg = TypeRef(elem).pkg_name();
            } else {
                SemaStructInfo* found = nullptr;
                { auto [dp, dsi] = find_datatype_by_name(TypeRef(elem).struct_name()); found = dsi; }
                if (!found) { auto [sp, ssi] = find_struct_by_name(TypeRef(elem).struct_name()); found = ssi; }
                if (found) pkg = found->package;
                // If still not found, fall back to current package.
                else pkg = cur_package_;
            }
            // Build the fully-qualified name used as explicit_type_codes_ key (matches
            // the key written by apply_annots_to_struct in sema.cpp).
            std::string fqn = pkg.empty() ? std::string(TypeRef(elem).struct_name()) : pkg + "::" + std::string(TypeRef(elem).struct_name());

            // Check for explicit #[type_code=N] annotation first.
            auto eit = explicit_type_codes_.find(fqn);
            if (eit != explicit_type_codes_.end()) {
                code = eit->second;
            } else {
                std::string canon = pkg + "::" + std::string(TypeRef(elem).struct_name());
                auto hash = type_hash_23(canon);
                uint64_t raw = type_hash_56bit(hash);
                code = (raw < 128) ? (raw + 128) : raw;
            }
        } else if (TypeRef(elem).kind() == LogosType::Kind::ZonedStruct && !TypeRef(elem).type_args().empty()) {
            // If any type_arg is a TypeVar, defer resolution to mono so every
            // instantiation of the surrounding generic function gets its own
            // concrete type_code.  Otherwise (fully concrete), compute now.
            bool has_tv = false;
            for (auto* a : TypeRef(elem).type_args())
                if (a && TypeRef(a).kind() == LogosType::Kind::TypeVar) { has_tv = true; break; }
            if (has_tv)
                return make_expr(prim(LogosType::Kind::U64), lir::ETypeCodeOf{elem});
            // Resolve the package for this generic type.
            // Prefer TypeRef(elem).pkg_name() (set by resolve_type via find_datatype_by_name).
            // Fall back to datatypes_ lookup (bare then qualified), then cur_package_.
            std::string pkg;
            if (!TypeRef(elem).pkg_name().empty()) {
                pkg = TypeRef(elem).pkg_name();
            } else {
                SemaStructInfo* gsi = nullptr;
                { auto [dp, dsi] = find_datatype_by_name(TypeRef(elem).struct_name()); gsi = dsi; }
                if (!gsi) { auto it = datatypes_.find(TypeRef(elem).struct_name()); if (it != datatypes_.end()) gsi = &it->second; }
                if (gsi) pkg = gsi->package;
                else pkg = cur_package_;
            }
            std::string canon = pkg + "::" + type_str(elem);
            auto eit = explicit_type_codes_.find(canon);
            if (eit != explicit_type_codes_.end()) {
                code = eit->second;
            } else {
                auto hash = type_hash_23(canon);
                uint64_t raw = type_hash_56bit(hash);
                code = (raw < 128) ? (raw + 128) : raw;
            }
        } else if (TypeRef(elem).kind() == LogosType::Kind::TypeVar) {
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
        while (check && TypeRef(check).kind() == LogosType::Kind::Array) check = TypeRef(check).elem().raw();
        bool is_plain = true;
        if (check && TypeRef(check).kind() == LogosType::Kind::ZonedStruct && TypeRef(check).type_args().empty()) {
            // Use import-aware helpers so same-package types (stored under qualified keys
            // after M1) and cross-package types are both found.
            SemaStructInfo* found_si = nullptr;
            { auto [dp, dsi] = find_datatype_by_name(TypeRef(check).struct_name()); found_si = dsi; }
            if (!found_si) { auto [sp, ssi] = find_struct_by_name(TypeRef(check).struct_name()); found_si = ssi; }
            // Package-qualified fallback using pkg_name on the type itself.
            if (!found_si && !TypeRef(check).pkg_name().empty()) {
                auto qkey = sema_key(TypeRef(check).pkg_name(), TypeRef(check).struct_name());
                { auto it = datatypes_.find(qkey); if (it != datatypes_.end()) found_si = &it->second; }
                if (!found_si) { auto it = structs_.find(qkey); if (it != structs_.end()) found_si = &it->second; }
            }
            if (found_si) is_plain = found_si->is_data_plain;
            // If not found in any map, default to true (unknown type → conservative safe).
        } else if (check && TypeRef(check).kind() == LogosType::Kind::ZonedStruct && !TypeRef(check).type_args().empty()) {
            // Generic Datatype: can't determine statically → conservative DataNode.
            is_plain = false;
        }
        return make_expr(prim(LogosType::Kind::Bool), lir::ELitInt{is_plain ? 1LL : 0LL});
    }

    // reflect::<T>() -> HermesStatic — compile-time request for TypeInfo rodata.
    // Adds T to reflect_requests so reflection_emit pass builds the TypeInfo global.
    // Returns EReflectOf{T}; mlir_gen lowers it to AddressOf(__logos_reflect__<hash>) + offset 8.
    if (callee == "reflect") {
        // Check if the single type arg is a genos name (before resolve_type, which rejects traits).
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                if (items.size() == 1) {
                    auto tnode = map_of(items.get(0));
                    if (tnode.has_key(la::NAME)) {
                        std::string tname(str_of(tnode.get(la::NAME.code)));
                        auto tit = traits_.find(tname);
                        if (tit != traits_.end() && tit->second.is_genos) {
                            if (cur_prog_) {
                                std::string pkg = std::string(cur_package_);
                                std::string fqn = pkg.empty() ? tname : pkg + "::" + tname;
                                cur_prog_->reflect_requests.insert(fqn);
                            }
                            auto* hs_type = make_struct_type("HermesStatic");
                            // Synthesize a ZonedStruct type for EReflectOf codegen.
                            const LogosType* gtp = make_datatype_type(tname, cur_package_);
                            return make_expr(hs_type, lir::EReflectOf{gtp});
                        }
                    }
                }
            }
        }
        auto ts = collect_type_args();
        if (ts.size() != 1 || !ts[0]) {
            error("reflect::<T>() requires exactly one type argument");
            return error_expr();
        }
        const LogosType* T = ts[0];
        if (TypeRef(T).kind() == LogosType::Kind::TypeVar) {
            // Inside a generic function — defer; mono will resolve and register.
            auto* hs_type = make_struct_type("HermesStatic");
            return make_expr(hs_type, lir::EReflectOf{T});
        }
        if (TypeRef(T).kind() != LogosType::Kind::ZonedStruct || !TypeRef(T).type_args().empty()) {
            error("reflect::<T>() requires a concrete (non-generic) datatype argument");
            return error_expr();
        }
        if (cur_prog_) {
            std::string pkg = TypeRef(T).pkg_name().empty() ? std::string(cur_package_) : std::string(TypeRef(T).pkg_name());
            std::string fqn = pkg.empty() ? std::string(TypeRef(T).struct_name()) : pkg + "::" + std::string(TypeRef(T).struct_name());
            cur_prog_->reflect_requests.insert(fqn);
        }
        auto* hs_type = make_struct_type("HermesStatic");
        return make_expr(hs_type, lir::EReflectOf{T});
    }

    // has_annotation::<T, A>() -> bool  (compile-time const-fold)
    // Returns true if datatype T has a user annotation of type A attached.
    if (callee == "has_annotation") {
        auto ts = collect_type_args();
        if (ts.size() != 2 || !ts[0] || !ts[1]) {
            error("has_annotation::<T, A>() requires exactly two type arguments");
            return error_expr();
        }
        const LogosType* T = ts[0];
        const LogosType* A = ts[1];
        // Look up A's annotation type name
        if (TypeRef(A).kind() != LogosType::Kind::ZonedStruct) {
            error("has_annotation: second type argument must be an annotation datatype");
            return error_expr();
        }
        std::string a_fqn;
        {
            auto [apkg, asi] = find_datatype_by_name(TypeRef(A).struct_name());
            if (!asi || !asi->is_annotation_type) {
                error(std::format("has_annotation: '{}' is not an annotation type", TypeRef(A).struct_name()));
                return error_expr();
            }
            a_fqn = apkg.empty() ? std::string(TypeRef(A).struct_name()) : apkg + "::" + std::string(TypeRef(A).struct_name());
        }
        bool found = false;
        if (TypeRef(T).kind() == LogosType::Kind::ZonedStruct && cur_prog_) {
            for (auto& sd : cur_prog_->structs) {
                if (sd.name == TypeRef(T).struct_name() || (!TypeRef(T).pkg_name().empty() && sd.name == TypeRef(T).struct_name())) {
                    for (auto& inst : sd.annotations)
                        if (inst.ann_fqn == a_fqn || inst.ann_name == TypeRef(A).struct_name())
                            { found = true; break; }
                    break;
                }
            }
        }
        return bool_lit(found);
    }

    // get_annotation::<T, A>() -> Option<A>  (compile-time const-fold)
    // Returns Option<A>::Some(A{...}) if T has annotation A, else Option<A>::None.
    if (callee == "get_annotation") {
        auto ts = collect_type_args();
        if (ts.size() != 2 || !ts[0] || !ts[1]) {
            error("get_annotation::<T, A>() requires exactly two type arguments");
            return error_expr();
        }
        const LogosType* T = ts[0];
        const LogosType* A = ts[1];
        if (TypeRef(A).kind() != LogosType::Kind::ZonedStruct) {
            error("get_annotation: second type argument must be an annotation datatype");
            return error_expr();
        }
        std::string a_fqn, a_pkg;
        SemaStructInfo* a_info = nullptr;
        {
            auto [apkg, asi] = find_datatype_by_name(TypeRef(A).struct_name());
            if (!asi || !asi->is_annotation_type) {
                error(std::format("get_annotation: '{}' is not an annotation type", TypeRef(A).struct_name()));
                return error_expr();
            }
            a_fqn = apkg.empty() ? std::string(TypeRef(A).struct_name()) : apkg + "::" + std::string(TypeRef(A).struct_name());
            a_pkg = std::string(apkg);
            a_info = asi;
        }
        // Find Option enum — must be imported
        auto [opt_pkg, opt_esi] = find_enum_by_name("Option");
        if (!opt_esi) {
            error("get_annotation: 'Option' enum not in scope (add 'use std;')");
            return error_expr();
        }
        // Find Some/None disc values
        int64_t some_disc = -1, none_disc = -1;
        for (auto& v : opt_esi->variants) {
            if (v.name == "Some") some_disc = v.value;
            else if (v.name == "None") none_disc = v.value;
        }
        // Build Option<A> type
        LogosType opt_type; opt_type.kind = LogosType::Kind::Enum;
        opt_type.enum_name = "Option";
        if (!opt_pkg.empty()) opt_type.pkg_name = opt_pkg;
        opt_type.type_args = {A};
        const LogosType* result_type = pool_.alloc(std::move(opt_type));
        // Build Datatype<A> type for the struct literal
        const LogosType* a_type = A;  // already a Datatype type
        // Find the annotation instance on T
        const lir::LAnnotationInstance* found_inst = nullptr;
        if (TypeRef(T).kind() == LogosType::Kind::ZonedStruct && cur_prog_) {
            for (auto& sd : cur_prog_->structs) {
                if (sd.name == TypeRef(T).struct_name()) {
                    for (auto& inst : sd.annotations)
                        if (inst.ann_fqn == a_fqn || inst.ann_name == TypeRef(A).struct_name())
                            { found_inst = &inst; break; }
                    break;
                }
            }
        }
        if (!found_inst) {
            // Return Option::None
            return make_expr(result_type,
                lir::EEnumLit{"Option", "None", none_disc});
        }
        // Materialize the annotation as A{field: value, ...}
        // Helper: convert LAnnotationValue to LExprPtr
        std::function<lir::LExprPtr(const lir::LAnnotationValue&, const LogosType*)> annot_val_to_expr;
        annot_val_to_expr = [&](const lir::LAnnotationValue& av, const LogosType* expected) -> lir::LExprPtr {
            using K = lir::LAnnotationValue::Kind;
            switch (av.kind) {
            case K::Int:   return make_expr(expected ? expected : prim(LogosType::Kind::I64), lir::ELitInt{av.i});
            case K::Float: return make_expr(expected ? expected : prim(LogosType::Kind::F64), lir::ELitFloat{av.f});
            case K::Bool:  return make_expr(prim(LogosType::Kind::Bool), lir::ELitInt{av.i});
            case K::Str:   return make_expr(make_slice_type(u8_t()), lir::ELitStr{av.s});
            case K::Enum:  {
                // Emit as enum literal: av.enum_name::av.enum_variant
                auto [epkg2, esi2] = find_enum_by_name(av.enum_name);
                int64_t disc2 = 0;
                if (esi2) for (auto& v : esi2->variants)
                    if (v.name == av.enum_variant) { disc2 = v.value; break; }
                auto* etype = make_enum_type(av.enum_name, epkg2);
                return make_expr(etype, lir::EEnumLit{av.enum_name, av.enum_variant, disc2});
            }
            case K::Array: {
                std::vector<lir::LExprPtr> elems;
                const LogosType* elem_t = (expected && TypeRef(expected).kind() == LogosType::Kind::Array)
                                          ? TypeRef(expected).elem().raw() : nullptr;
                for (auto& item : av.arr) elems.push_back(annot_val_to_expr(item, elem_t));
                LogosType at; at.kind = LogosType::Kind::Array;
                at.elem = elem_t ? elem_t : (elems.empty() ? prim(LogosType::Kind::I64) : elems[0]->type);
                at.arr_size = (int64_t)elems.size();
                return make_expr(pool_.alloc(std::move(at)), lir::EArrLit{std::move(elems)});
            }
            }
            return error_expr();
        };
        // Build field list for the struct literal
        std::vector<std::pair<std::string, lir::LExprPtr>> fields;
        for (auto& [fname, fval] : found_inst->kv) {
            // Find expected type from annotation type's fields
            const LogosType* ftype = nullptr;
            if (a_info) for (auto& f : a_info->fields)
                if (f.name == fname) { ftype = f.type; break; }
            fields.emplace_back(fname, annot_val_to_expr(fval, ftype));
        }
        auto struct_expr = make_expr(a_type, lir::EStructLit{std::string(TypeRef(A).struct_name()), std::move(fields)});
        // Wrap in Option<A>::Some(struct_expr)
        std::vector<lir::LExprPtr> payload;
        payload.push_back(std::move(struct_expr));
        return make_expr(result_type,
            lir::EEnumLitData{"Option", "Some", some_disc, std::move(payload)});
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

    // Slice / str built-in methods: .len(), .as_ptr()
    if (recv->type->kind == LogosType::Kind::Slice) {
        if (method_name == "len") {
            return make_expr(prim(LogosType::Kind::I64),
                lir::ESliceLen{std::move(recv)});
        }
        if (method_name == "as_ptr") {
            return make_expr(make_ptr(false, u8_t()),
                lir::ESlicePtr{std::move(recv)});
        }
        error(std::format("slice has no method '{}'", method_name));
        return error_expr();
    }

    // Raw-pointer built-in arithmetic methods:
    //   p.byte_add(n) / p.byte_sub(n)  — offset n bytes, same pointer type
    //   p.add(n)      / p.sub(n)       — offset n elements
    //   p.byte_offset_from(q)          — i64 byte distance
    //   p.offset_from(q)               — i64 element distance
    if (recv->type->kind == LogosType::Kind::Ptr) {
        auto parse_args = [&]() {
            std::vector<lir::LExprPtr> args;
            if (node.has_key(la::ARGS)) {
                auto av = arr_of(node.get(la::ARGS.code));
                for (uint64_t i = 0; i < av.size(); ++i)
                    args.push_back(lower_expr(map_of(av.get(i))));
            }
            return args;
        };
        auto mk_arith = [&](lir::EPtrArith::Op op) -> lir::LExprPtr {
            if (!inside_unsafe_)
                error(std::format("pointer method '{}' requires unsafe context", method_name));
            auto args = parse_args();
            if (args.size() != 1) {
                error(std::format("pointer method '{}' expects 1 argument, got {}",
                      method_name, args.size()));
                return error_expr();
            }
            auto* at = args[0]->type;
            auto* i64ty = prim(LogosType::Kind::I64);
            if (TypeRef(at).kind() != LogosType::Kind::Error && !types_compatible(at, i64ty))
                error(std::format("pointer method '{}': argument must be i64, got {}",
                      method_name, type_str(at)));
            widen_int_expr(args[0], i64ty);
            auto* ret_type = recv->type;
            return make_expr(ret_type,
                lir::EPtrArith{op, std::move(recv), std::move(args[0])});
        };
        auto mk_diff = [&](bool by_byte) -> lir::LExprPtr {
            if (!inside_unsafe_)
                error(std::format("pointer method '{}' requires unsafe context", method_name));
            auto args = parse_args();
            if (args.size() != 1) {
                error(std::format("pointer method '{}' expects 1 argument, got {}",
                      method_name, args.size()));
                return error_expr();
            }
            auto* at = args[0]->type;
            if (TypeRef(at).kind() != LogosType::Kind::Error && TypeRef(at).kind() != LogosType::Kind::Ptr)
                error(std::format("pointer method '{}': argument must be a pointer, got {}",
                      method_name, type_str(at)));
            return make_expr(prim(LogosType::Kind::I64),
                lir::EPtrDiff{by_byte, std::move(recv), std::move(args[0])});
        };
        if (method_name == "byte_add")         return mk_arith(lir::EPtrArith::ByteAdd);
        if (method_name == "byte_sub")         return mk_arith(lir::EPtrArith::ByteSub);
        if (method_name == "add")              return mk_arith(lir::EPtrArith::Add);
        if (method_name == "sub")              return mk_arith(lir::EPtrArith::Sub);
        if (method_name == "byte_offset_from") return mk_diff(true);
        if (method_name == "offset_from")      return mk_diff(false);
        // fall through: other methods (if any) resolve via struct lookup below
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
                            if (TypeRef(at).kind() != LogosType::Kind::Error &&
                                TypeRef(pt).kind() != LogosType::Kind::Error &&
                                TypeRef(pt).kind() != LogosType::Kind::TypeVar &&
                                TypeRef(pt).kind() != LogosType::Kind::AssocType &&
                                !types_compatible(at, pt))
                                error(std::format("method '{}' arg {}: expected {}, got {}",
                                                  std::string(method_name), i + 1,
                                                  type_str(pt), type_str(at)));
                            if (TypeRef(at).kind() == LogosType::Kind::IntLit &&
                                TypeRef(pt).kind() != LogosType::Kind::Error &&
                                TypeRef(pt).kind() != LogosType::Kind::TypeVar)
                                if (auto v = get_intlit_value(arg_exprs[i].get()))
                                    if (!intlit_fits(*v, pt->kind))
                                        error(std::format("method '{}' arg {}: value {} does not fit in {}",
                                                          std::string(method_name), i + 1, *v, type_str(pt)));
                            // Check array literal elements against narrow array param type.
                            if (TypeRef(at).kind() == LogosType::Kind::Array && TypeRef(pt).kind() == LogosType::Kind::Array && TypeRef(pt).elem())
                                if (auto* al = std::get_if<lir::EArrLit>(&arg_exprs[i]->kind))
                                    for (size_t ei = 0; ei < al->elems.size(); ++ei)
                                        if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(al->elems[ei].get()))
                                                if (!intlit_fits(*v, TypeRef(pt).elem()->kind))
                                                    error(std::format("method '{}' arg {}: array element {}: value {} does not fit in {}",
                                                                      std::string(method_name), i + 1, ei, *v, type_str(TypeRef(pt).elem())));
                            // Check tuple literal elements against narrow tuple param element types.
                            if (TypeRef(at).kind() == LogosType::Kind::Tuple && TypeRef(pt).kind() == LogosType::Kind::Tuple)
                                if (auto* tl = std::get_if<lir::ETupleLit>(&arg_exprs[i]->kind))
                                    for (size_t ei = 0; ei < tl->elems.size() && ei < TypeRef(pt).tuple_elems().size(); ++ei) {
                                        if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(tl->elems[ei].get()))
                                                if (TypeRef(pt).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->kind))
                                                    error(std::format("method '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                                                      std::string(method_name), i + 1, ei, *v, type_str(TypeRef(pt).tuple_elems()[ei])));
                                        if (TypeRef(pt).tuple_elems()[ei] && TypeRef(pt).tuple_elems()[ei]->kind == LogosType::Kind::Array &&
                                            TypeRef(pt).tuple_elems()[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                                            if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                                for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                                    if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                                        if (auto v = get_intlit_value(ial->elems[ii].get()))
                                                            if (!intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->elem->kind))
                                                                error(std::format("method '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                                      std::string(method_name), i + 1, ei, ii, *v, type_str(TypeRef(pt).tuple_elems()[ei]->elem)));

                                        if (TypeRef(pt).tuple_elems()[ei] && TypeRef(pt).tuple_elems()[ei]->kind == LogosType::Kind::Tuple &&
                                            tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                                            if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                                for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(pt).tuple_elems()[ei]->tuple_elems.size(); ++ii)
                                                    if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                                        if (auto v = get_intlit_value(itl->elems[ii].get()))
                                                            if (TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii] && !intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii]->kind))
                                                                error(std::format("method '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                                      std::string(method_name), i + 1, ei, ii, *v, type_str(TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii])));
                                        }
                        }
                    }
                    // Return type: substitute Self → &dyn Trait
                    auto* ret_type = m.ret_type;
                    if (ret_type && TypeRef(ret_type).kind() == LogosType::Kind::TypeVar &&
                        TypeRef(ret_type).type_var_name() == "Self")
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
            if (ret_type && TypeRef(ret_type).kind() == LogosType::Kind::TypeVar &&
                TypeRef(ret_type).type_var_name() == "Self")
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
    if (recv_inner && TypeRef(recv_inner).kind() == LogosType::Kind::Ptr) {
        if (!inside_unsafe_)
            error("method call through raw pointer requires unsafe context");
        recv_inner = TypeRef(recv_inner).pointee().raw();
    } else if (recv_inner && is_ref_like(recv_inner->kind) && TypeRef(recv_inner).pointee()) {
        recv_inner = TypeRef(recv_inner).pointee().raw();
    }
    if (TypeRef(recv_inner).kind() == LogosType::Kind::TypeVar) {
        std::vector<lir::LExprPtr> arg_exprs;
        if (node.has_key(la::ARGS)) {
            auto args = arr_of(node.get(la::ARGS.code));
            for (uint64_t i = 0; i < args.size(); ++i)
                arg_exprs.push_back(lower_expr(map_of(args.get(i))));
        }

        auto bit = current_type_bounds_.find(TypeRef(recv_inner).type_var_name());
        const SemaTraitMethodInfo* chosen_method = nullptr;
        std::string chosen_trait;

        // Helper: depth-first search over the supertrait DAG for the method.
        // visited prevents infinite loops on circular supertrait definitions (Bug 2).
        // The !chosen_method guard is NOT used so all supertrait siblings are searched
        // for ambiguity detection (Bug 1 fix: e.g. trait Foo: A+B where both define m()).
        StrSet st_visited;
        std::function<void(const std::string&)> search_trait = [&](const std::string& tname) {
            if (!st_visited.insert(tname).second) return;  // cycle / diamond guard (Bug 2)
            auto tit = traits_.find(tname);
            if (tit == traits_.end()) return;
            for (auto& m : tit->second.methods) {
                if (m.name != method_name) continue;
                if (chosen_method && chosen_trait != tname)
                    error(std::format(
                        "method '{}' is ambiguous for type parameter '{}' (matches traits '{}' and '{}')",
                        std::string(method_name), TypeRef(recv_inner).type_var_name(), chosen_trait, tname));
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
                // Infer method-level type params (e.g. `fn hash<H: Hasher>`) from
                // arg types so the compat check sees substituted param types.
                if (!chosen_method->type_params.empty()) {
                    StrMap<const LogosType*> bindings(
                        self_subst.begin(), self_subst.end());
                    for (uint64_t i = 0; i < arg_exprs.size(); ++i) {
                        if (i + 1 >= chosen_method->param_types.size()) break;
                        auto* pt0 = subst_type_sema(chosen_method->param_types[i + 1], self_subst);
                        unify_types(pt0, arg_exprs[i]->type, bindings);
                    }
                    for (auto& tp : chosen_method->type_params) {
                        auto it = bindings.find(tp.name);
                        if (it != bindings.end() && it->second)
                            self_subst[tp.name] = it->second;
                    }
                }
                for (uint64_t i = 0; i < arg_exprs.size(); ++i) {
                    auto* at = arg_exprs[i]->type;
                    auto* pt = subst_type_sema(chosen_method->param_types[i + 1], self_subst);
                    if (TypeRef(at).kind() != LogosType::Kind::Error &&
                        TypeRef(pt).kind() != LogosType::Kind::Error &&
                        TypeRef(pt).kind() != LogosType::Kind::TypeVar &&
                        TypeRef(pt).kind() != LogosType::Kind::AssocType &&
                        !types_compatible(at, pt))
                        error(std::format("method '{}' arg {}: expected {}, got {}",
                                          std::string(method_name), i + 1,
                                          type_str(pt), type_str(at)));
                    if (TypeRef(at).kind() == LogosType::Kind::IntLit &&
                        TypeRef(pt).kind() != LogosType::Kind::Error &&
                        TypeRef(pt).kind() != LogosType::Kind::TypeVar &&
                        TypeRef(pt).kind() != LogosType::Kind::AssocType)
                        if (auto v = get_intlit_value(arg_exprs[i].get()))
                            if (!intlit_fits(*v, pt->kind))
                                error(std::format("method '{}' arg {}: value {} does not fit in {}",
                                                  std::string(method_name), i + 1,
                                                  *v, type_str(pt)));
                    // Check array literal elements against narrow array param type.
                    if (TypeRef(at).kind() == LogosType::Kind::Array && TypeRef(pt).kind() == LogosType::Kind::Array && TypeRef(pt).elem())
                        if (auto* al = std::get_if<lir::EArrLit>(&arg_exprs[i]->kind))
                            for (size_t ei = 0; ei < al->elems.size(); ++ei)
                                if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(al->elems[ei].get()))
                                        if (!intlit_fits(*v, TypeRef(pt).elem()->kind))
                                            error(std::format("method '{}' arg {}: array element {}: value {} does not fit in {}",
                                                              std::string(method_name), i + 1, ei, *v, type_str(TypeRef(pt).elem())));
                    // Check tuple literal elements against narrow tuple param element types.
                    if (TypeRef(at).kind() == LogosType::Kind::Tuple && TypeRef(pt).kind() == LogosType::Kind::Tuple)
                        if (auto* tl = std::get_if<lir::ETupleLit>(&arg_exprs[i]->kind))
                            for (size_t ei = 0; ei < tl->elems.size() && ei < TypeRef(pt).tuple_elems().size(); ++ei) {
                                if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(tl->elems[ei].get()))
                                        if (TypeRef(pt).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->kind))
                                            error(std::format("method '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                                              std::string(method_name), i + 1, ei, *v, type_str(TypeRef(pt).tuple_elems()[ei])));
                                if (TypeRef(pt).tuple_elems()[ei] && TypeRef(pt).tuple_elems()[ei]->kind == LogosType::Kind::Array &&
                                    TypeRef(pt).tuple_elems()[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                                    if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                        for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                            if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                                if (auto v = get_intlit_value(ial->elems[ii].get()))
                                                    if (!intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->elem->kind))
                                                        error(std::format("method '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                              std::string(method_name), i + 1, ei, ii, *v, type_str(TypeRef(pt).tuple_elems()[ei]->elem)));

                                if (TypeRef(pt).tuple_elems()[ei] && TypeRef(pt).tuple_elems()[ei]->kind == LogosType::Kind::Tuple &&
                                    tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                                    if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                        for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(pt).tuple_elems()[ei]->tuple_elems.size(); ++ii)
                                            if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                                if (auto v = get_intlit_value(itl->elems[ii].get()))
                                                    if (TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii] && !intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii]->kind))
                                                        error(std::format("method '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                              std::string(method_name), i + 1, ei, ii, *v, type_str(TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii])));
                                }
                }
            }

            SemaSubst self_subst;
            self_subst["Self"] = recv_inner;
            // Substitute trait type params from the bound: e.g. T: Into<i32> → Into::T = i32
            {
                auto bit2 = current_type_bounds_.find(TypeRef(recv_inner).type_var_name());
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
            // Propagate method-level type params (e.g. H in `hash<H>`) inferred above.
            if (!chosen_method->type_params.empty()) {
                StrMap<const LogosType*> bindings(
                    self_subst.begin(), self_subst.end());
                for (uint64_t i = 0; i < arg_exprs.size(); ++i) {
                    if (i + 1 >= chosen_method->param_types.size()) break;
                    auto* pt0 = subst_type_sema(chosen_method->param_types[i + 1], self_subst);
                    unify_types(pt0, arg_exprs[i]->type, bindings);
                }
                for (auto& tp : chosen_method->type_params) {
                    auto it = bindings.find(tp.name);
                    mc.type_args.push_back(it != bindings.end() ? it->second : nullptr);
                }
            }
            mc.args     = std::move(arg_exprs);
            mc.vtable_index = -1;
            mc.resolved_type = "";
            return make_expr(ret_type, std::move(mc));
        }

        error(std::format("type parameter '{}' has no trait bound providing method '{}'",
                          TypeRef(recv_inner).type_var_name(), std::string(method_name)));
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
                auto [epkg_genum, esi_genum] = find_enum_by_name(base);
                auto eit = esi_genum ? enums_.find(sema_key(epkg_genum, base)) : enums_.end();
                if (eit == enums_.end()) eit = enums_.find(base);
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
            // Auto-ref receiver variants: methods may declare &self / &mut self
            // where Self is a primitive, so try &T and &mut T as param[0].
            if (!fi_ptr) {
                auto types_ref = types; types_ref[0] = make_ref(false, recv->type);
                if (auto pfit = find_func_by_base_and_signature(mangled_prim, types_ref, false))
                    fi_ptr = pfit;
            }
            if (!fi_ptr) {
                auto types_mut = types; types_mut[0] = make_ref(true, recv->type);
                if (auto pfit = find_func_by_base_and_signature(mangled_prim, types_mut, false))
                    fi_ptr = pfit;
            }
            if (!fi_ptr) {
                if (auto pfit = find_generic_func(mangled_prim)) {
                    fi_ptr = pfit;
                }
                // `str` resolves to Slice<u8> (type_str → "&[u8]"), but impl methods
                // are registered under "str__method".  Try the alias fallback.
                else if (tname == "&[u8]") {
                    auto str_mangled = std::string("str__") + std::string(method_name);
                    if (auto pfit = find_func_by_base_and_signature(str_mangled, types, false))
                        fi_ptr = pfit;
                    else if (auto pfit = find_generic_func(str_mangled))
                        fi_ptr = pfit;
                    if (fi_ptr) mangled_prim = std::string("str__") + std::string(method_name);
                }
            }
        }

        if (fi_ptr) {
            if (fi_ptr->is_unsafe && !inside_unsafe_)
                error(std::format("call to unsafe method '{}' requires unsafe context", mangled_prim));
            // Generic method on primitive receiver (e.g. i32::hash<H>): infer
            // method-level type args and route through finish_generic_call so
            // mono emits a concrete specialization.
            if (!fi_ptr->type_params.empty()) {
                SemaSubst seed;
                seed["Self"] = recv->type;
                std::vector<const LogosType*> m_type_args;
                if (!infer_type_args(*fi_ptr, arg_exprs, m_type_args, seed, 1)) {
                    error(std::format("could not infer type arguments for generic method '{}'",
                                      mangled_prim));
                }
                // Auto-ref receiver if method expects &Self / &mut Self.
                if (!fi_ptr->param_types.empty()) {
                    auto* formal0 = fi_ptr->param_types[0];
                    if (formal0 && is_ref_like(formal0->kind) && recv->type &&
                        !is_ref_like(recv->type->kind) &&
                        recv->type->kind != LogosType::Kind::Ptr) {
                        bool is_mut = TypeRef(formal0).kind() == LogosType::Kind::MutRef;
                        auto addr = make_expr(make_ref(is_mut, recv->type),
                                              lir::EAddrOfTemp{std::move(recv), is_mut});
                        recv = std::move(addr);
                    }
                }
                std::vector<lir::LExprPtr> pargs;
                pargs.push_back(std::move(recv));
                for (auto& a : arg_exprs) pargs.push_back(std::move(a));
                return finish_generic_call(
                    fi_ptr->symbol_name.empty() ? mangled_prim : fi_ptr->symbol_name,
                    *fi_ptr, std::move(m_type_args), std::move(pargs));
            }
            // Auto-ref receiver if method expects &Self / &mut Self.
            if (!fi_ptr->param_types.empty()) {
                auto* formal0 = fi_ptr->param_types[0];
                if (formal0 && is_ref_like(formal0->kind) && recv->type &&
                    !is_ref_like(recv->type->kind) &&
                    recv->type->kind != LogosType::Kind::Ptr) {
                    bool is_mut = TypeRef(formal0).kind() == LogosType::Kind::MutRef;
                    auto addr = make_expr(make_ref(is_mut, recv->type),
                                          lir::EAddrOfTemp{std::move(recv), is_mut});
                    recv = std::move(addr);
                }
            }
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
        if (rst && TypeRef(rst).kind() == LogosType::Kind::Ptr && TypeRef(rst).pointee()) {
            rst = TypeRef(rst).pointee().raw();
        } else if (rst && is_ref_like(rst->kind) && TypeRef(rst).pointee()) {
            rst = TypeRef(rst).pointee().raw();
        }
        if ((TypeRef(rst).kind() == LogosType::Kind::Struct || TypeRef(rst).kind() == LogosType::Kind::ZonedStruct) &&
            !TypeRef(rst).type_args().empty()) {
            SemaStructInfo* si2 = nullptr;
            { auto [p, si] = find_struct_by_name(TypeRef(rst).struct_name()); si2 = si; }
            if (!si2) { auto [p, di] = find_datatype_by_name(TypeRef(rst).struct_name()); si2 = di; }
            if (si2) {
                auto& tps = si2->type_params;
                for (size_t i = 0; i < tps.size() && i < TypeRef(rst).type_args().size(); ++i)
                    recv_struct_subst[tps[i].name] = TypeRef(rst).type_args()[i];
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
                if (TypeRef(actual0).kind() != LogosType::Kind::Ref &&
                    TypeRef(actual0).kind() != LogosType::Kind::MutRef &&
                    TypeRef(actual0).kind() != LogosType::Kind::Ptr &&
                    is_ref_like(formal0->kind) && TypeRef(formal0).pointee() &&
                    types_equal(*actual0, *TypeRef(formal0).pointee())) {
                    needs_ref = true;
                    needs_mut = TypeRef(formal0).kind() == LogosType::Kind::MutRef;
                } else if (TypeRef(actual0).kind() != LogosType::Kind::Ref &&
                           TypeRef(actual0).kind() != LogosType::Kind::MutRef &&
                           TypeRef(actual0).kind() != LogosType::Kind::Ptr &&
                           TypeRef(formal0).kind() == LogosType::Kind::Ptr &&
                           TypeRef(formal0).pointee() &&
                           types_equal(*actual0, *TypeRef(formal0).pointee())) {
                    needs_ref = true;
                    needs_mut = false;
                } else if (TypeRef(actual0).kind() == LogosType::Kind::Ptr &&
                           TypeRef(formal0).kind() == LogosType::Kind::Ptr &&
                           TypeRef(actual0).pointee() && TypeRef(formal0).pointee() &&
                           types_equal(*TypeRef(actual0).pointee(), *TypeRef(formal0).pointee())) {
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
                    if (TypeRef(actual0).kind() != LogosType::Kind::Ref &&
                        TypeRef(actual0).kind() != LogosType::Kind::MutRef &&
                        TypeRef(actual0).kind() != LogosType::Kind::Ptr &&
                        is_ref_like(formal0->kind) && TypeRef(formal0).pointee() &&
                        types_equal(*actual0, *TypeRef(formal0).pointee())) {
                        needs_ref = true;
                        needs_mut = TypeRef(formal0).kind() == LogosType::Kind::MutRef;
                    } else if (TypeRef(actual0).kind() != LogosType::Kind::Ref &&
                               TypeRef(actual0).kind() != LogosType::Kind::MutRef &&
                               TypeRef(actual0).kind() != LogosType::Kind::Ptr &&
                               TypeRef(formal0).kind() == LogosType::Kind::Ptr &&
                               TypeRef(formal0).pointee() &&
                               types_equal(*actual0, *TypeRef(formal0).pointee())) {
                        needs_ref = true;
                        needs_mut = false;
                    } else if (TypeRef(actual0).kind() == LogosType::Kind::Ptr &&
                               TypeRef(formal0).kind() == LogosType::Kind::Ptr &&
                               TypeRef(actual0).pointee() && TypeRef(formal0).pointee() &&
                               types_equal(*TypeRef(actual0).pointee(), *TypeRef(formal0).pointee())) {
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
            if (recv_inner && (TypeRef(recv_inner).kind() == LogosType::Kind::Ptr ||
                               is_ref_like(recv_inner->kind)) && TypeRef(recv_inner).pointee())
                recv_inner = TypeRef(recv_inner).pointee().raw();
            // Auto-ref: if method expects &self / &mut self but recv is a
            // value, take its address.
            if (!mfi->param_types.empty()) {
                SemaSubst s_subst;
                s_subst["Self"] = recv_inner;
                s_subst[bi.target_typevar] = recv_inner;
                const LogosType* target_self =
                    subst_type_sema(mfi->param_types[0], s_subst);
                if (target_self &&
                    (TypeRef(target_self).kind() == LogosType::Kind::Ref ||
                     TypeRef(target_self).kind() == LogosType::Kind::MutRef) &&
                    recv->type &&
                    recv->type->kind != LogosType::Kind::Ref &&
                    recv->type->kind != LogosType::Kind::MutRef &&
                    recv->type->kind != LogosType::Kind::Ptr) {
                    bool is_mut = TypeRef(target_self).kind() == LogosType::Kind::MutRef;
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
        if (rst && TypeRef(rst).kind() == LogosType::Kind::Ptr) {
            if (!inside_unsafe_)
                error("method call through raw pointer requires unsafe context");
            if (TypeRef(rst).pointee().raw()) rst = TypeRef(rst).pointee().raw();
        } else if (rst && is_ref_like(rst->kind) && TypeRef(rst).pointee()) {
            rst = TypeRef(rst).pointee().raw();
        }
        if ((TypeRef(rst).kind() == LogosType::Kind::Struct || TypeRef(rst).kind() == LogosType::Kind::ZonedStruct) && !TypeRef(rst).type_args().empty()) {
            SemaStructInfo* si2 = nullptr;
            { auto [p, si] = find_struct_by_name(TypeRef(rst).struct_name()); si2 = si; }
            if (!si2) { auto [p, di] = find_datatype_by_name(TypeRef(rst).struct_name()); si2 = di; }
            if (si2) {
                auto& tps = si2->type_params;
                for (size_t i = 0; i < tps.size() && i < TypeRef(rst).type_args().size(); ++i)
                    struct_subst[tps[i].name] = TypeRef(rst).type_args()[i];
            }
        }
    }

    // Pre-infer method-level type args so the arg-compat check below sees the
    // substituted param types.  Without this, calling a generic trait method
    // (e.g. `fn hash<H: Hasher>(&self, s: &mut H)`) from a generic context where
    // `Self` is trait-bounded reports `expected &mut H, got &mut FxHasher` even
    // though H could be inferred from the arg.
    std::vector<const LogosType*> m_type_args;
    if (!fi.type_params.empty()) {
        infer_type_args(fi, arg_exprs, m_type_args, struct_subst, 1);
        for (size_t i = 0; i < fi.type_params.size() && i < m_type_args.size(); ++i)
            if (m_type_args[i])
                struct_subst[fi.type_params[i].name] = m_type_args[i];
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
                if (TypeRef(at).kind() != LogosType::Kind::Error && TypeRef(pt).kind() != LogosType::Kind::Error &&
                    !types_compatible(at, pt))
                    error(std::format("method '{}' arg {}: expected {}, got {}",
                          mangled, i + 1, type_str(pt), type_str(at)));
                if (TypeRef(at).kind() == LogosType::Kind::IntLit && TypeRef(pt).kind() != LogosType::Kind::Error)
                    if (auto v = get_intlit_value(arg_exprs[i].get()))
                        if (!intlit_fits(*v, pt->kind))
                            error(std::format("method '{}' arg {}: value {} does not fit in {}",
                                  mangled, i + 1, *v, type_str(pt)));
                // Check array literal elements against narrow array param type.
                if (TypeRef(at).kind() == LogosType::Kind::Array && TypeRef(pt).kind() == LogosType::Kind::Array && TypeRef(pt).elem())
                    if (auto* al = std::get_if<lir::EArrLit>(&arg_exprs[i]->kind))
                        for (size_t ei = 0; ei < al->elems.size(); ++ei)
                            if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(al->elems[ei].get()))
                                    if (!intlit_fits(*v, TypeRef(pt).elem()->kind))
                                        error(std::format("method '{}' arg {}: array element {}: value {} does not fit in {}",
                                              mangled, i + 1, ei, *v, type_str(TypeRef(pt).elem())));
                // Check tuple literal elements against narrow tuple param element types.
                if (TypeRef(at).kind() == LogosType::Kind::Tuple && TypeRef(pt).kind() == LogosType::Kind::Tuple)
                    if (auto* tl = std::get_if<lir::ETupleLit>(&arg_exprs[i]->kind))
                        for (size_t ei = 0; ei < tl->elems.size() && ei < TypeRef(pt).tuple_elems().size(); ++ei) {
                            if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(tl->elems[ei].get()))
                                    if (TypeRef(pt).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->kind))
                                        error(std::format("method '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                              mangled, i + 1, ei, *v, type_str(TypeRef(pt).tuple_elems()[ei])));
                            if (TypeRef(pt).tuple_elems()[ei] && TypeRef(pt).tuple_elems()[ei]->kind == LogosType::Kind::Array &&
                                TypeRef(pt).tuple_elems()[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                                if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                        if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(ial->elems[ii].get()))
                                                if (!intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->elem->kind))
                                                    error(std::format("method '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                          mangled, i + 1, ei, ii, *v, type_str(TypeRef(pt).tuple_elems()[ei]->elem)));

                            if (TypeRef(pt).tuple_elems()[ei] && TypeRef(pt).tuple_elems()[ei]->kind == LogosType::Kind::Tuple &&
                                tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                                if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(pt).tuple_elems()[ei]->tuple_elems.size(); ++ii)
                                        if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(itl->elems[ii].get()))
                                                if (TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii] && !intlit_fits(*v, TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii]->kind))
                                                    error(std::format("method '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                          mangled, i + 1, ei, ii, *v, type_str(TypeRef(pt).tuple_elems()[ei]->tuple_elems[ii])));
                            }
            }
        }
    }

    // Method type args inferred above; verify and bounds-check here.
    if (!fi.type_params.empty()) {
        bool all_bound = m_type_args.size() == fi.type_params.size();
        for (auto* ta : m_type_args)
            if (!ta) { all_bound = false; break; }
        if (!all_bound)
            error(std::format("could not infer type arguments for generic method '{}'", mangled));
        check_type_bounds(mangled, fi.type_params, m_type_args);

        // Route generic trait-method call through finish_generic_call so mono
        // emits a concrete specialization (mirrors the blanket-impl path above).
        // Only when there is a genuine *method-level* type param (i.e. some
        // type param is NOT already bound by the receiver's struct_subst);
        // pure struct-level generic methods (e.g. Zone<M>::release) stay on
        // the existing EMethodCall/mono path.
        bool has_method_level = false;
        {
            // Rebuild struct_subst view without method bindings (we just added them).
            SemaSubst struct_only = struct_subst;
            for (auto& tp : fi.type_params) struct_only.erase(tp.name);
            // Re-add only receiver-derived ones.
            const LogosType* rst = recv->type;
            if (rst && (TypeRef(rst).kind() == LogosType::Kind::Ptr || is_ref_like(rst->kind)) && TypeRef(rst).pointee())
                rst = TypeRef(rst).pointee().raw();
            if (rst && (TypeRef(rst).kind() == LogosType::Kind::Struct || TypeRef(rst).kind() == LogosType::Kind::ZonedStruct)) {
                SemaStructInfo* si2 = nullptr;
                { auto [p, si] = find_struct_by_name(TypeRef(rst).struct_name()); si2 = si; }
                if (!si2) { auto [p, di] = find_datatype_by_name(TypeRef(rst).struct_name()); si2 = di; }
                if (si2) {
                    auto& tps = si2->type_params;
                    StrSet struct_names;
                    for (auto& tp : tps) struct_names.insert(tp.name);
                    for (auto& tp : fi.type_params)
                        if (!struct_names.count(tp.name)) { has_method_level = true; break; }
                }
            } else {
                // Non-struct receiver: any fi.type_params is method-level.
                if (!fi.type_params.empty()) has_method_level = true;
            }
        }
        bool all_concrete = has_method_level && m_type_args.size() == fi.type_params.size();
        for (auto* ta : m_type_args)
            if (!ta || TypeRef(ta).kind() == LogosType::Kind::TypeVar ||
                TypeRef(ta).kind() == LogosType::Kind::Error) { all_concrete = false; break; }
        if (all_concrete) {
            std::vector<lir::LExprPtr> pargs;
            pargs.push_back(std::move(recv));
            for (auto& a : arg_exprs) pargs.push_back(std::move(a));
            return finish_generic_call(
                fi.symbol_name.empty() ? mangled : fi.symbol_name, fi,
                std::move(m_type_args), std::move(pargs));
        }
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
    if (recv_base_t && TypeRef(recv_base_t).kind() == LogosType::Kind::Ptr) {
        if (!inside_unsafe_)
            error("field read through raw pointer requires unsafe context");
        recv_base_t = TypeRef(recv_base_t).pointee().raw();
    } else if (recv_base_t && is_ref_like(recv_base_t->kind)) {
        recv_base_t = TypeRef(recv_base_t).pointee().raw();
    }

    // DataRef<T> ergonomic read: p.field → p.ptr().field
    // Intercept before normal struct field lookup so that DataRef<T>.x works without
    // an explicit let pw = p.ptr() intermediate step.
    if (recv_base_t && TypeRef(recv_base_t).kind() == LogosType::Kind::Struct &&
        TypeRef(recv_base_t).struct_name() == "DataRef" &&
        TypeRef(recv_base_t).type_args().size() == 1) {
        const LogosType* T = TypeRef(recv_base_t).type_args()[0];
        if (T && TypeRef(T).kind() == LogosType::Kind::ZonedStruct) {
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
            auto [spkg, ssi] = find_struct_by_name(sname);
            if (ssi) sinfo_ptr = ssi;
            else { auto [dpkg, dsi] = find_datatype_by_name(sname); sinfo_ptr = dsi; }
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
    // Find in structs_ or datatypes_ (package-aware).
    bool slit_is_zoned = false;
    auto find_struct_info = [&](const std::string& name) -> SemaStructInfo* {
        auto [spkg, ssi] = find_struct_by_name(name);
        if (ssi) { slit_is_zoned = false; return ssi; }
        auto [dpkg, dsi] = find_datatype_by_name(name);
        if (dsi) { slit_is_zoned = true; return dsi; }
        return nullptr;
    };
    auto* sinfo_ptr = find_struct_info(sname_buf);
    if (!sinfo_ptr) {
        // Try resolving via type alias: `type Alias = Struct` or `type Alias = Struct<T>`
        // Bug 3 fix: only apply non-generic aliases here; generic aliases need type args
        // at the call site and we can't infer them from just the struct literal name.
        // Check current package and imported packages for the alias.
        auto check_alias = [&](const std::string& key) -> const LogosType* {
            auto ait = type_aliases_.find(key);
            if (ait != type_aliases_.end() &&
                ait->second.type_params.empty() && ait->second.lifetime_params.empty())
                return ait->second.type;
            return nullptr;
        };
        const LogosType* aliased = check_alias(sname_buf);
        if (!aliased && !cur_package_.empty()) aliased = check_alias(sema_key(cur_package_, sname_buf));
        if (!aliased) for (auto& pkg : cur_imports_.wildcard_packages) {
            aliased = check_alias(sema_key(pkg, sname_buf));
            if (aliased) break;
        }
        if (aliased && (TypeRef(aliased).kind() == LogosType::Kind::Struct ||
                        TypeRef(aliased).kind() == LogosType::Kind::ZonedStruct)) {
            sinfo_ptr = find_struct_info(std::string(TypeRef(aliased).struct_name()));
            if (sinfo_ptr) {
                sname_buf = std::string(TypeRef(aliased).struct_name());
                hint_struct_type_ = aliased;
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
        auto hint_for_tv = [&](std::string_view tv_name) -> const LogosType* {
            if (!hint_struct_type_ || TypeRef(hint_struct_type_).struct_name() != std::string(sname))
                return nullptr;
            for (size_t i = 0; i < sinfo.type_params.size(); ++i)
                if (sinfo.type_params[i].name == tv_name &&
                    i < TypeRef(hint_struct_type_).type_args().size())
                    return TypeRef(hint_struct_type_).type_args()[i];
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
                        if (resolved && TypeRef(resolved).kind() != LogosType::Kind::Error) {
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
            if (TypeRef(raw_ft).kind() == LogosType::Kind::TypeVar) {
                auto tv = TypeRef(raw_ft).type_var_name();
                if (!inferred.count(tv)) {
                    auto* vt = fval->type;
                    if (TypeRef(vt).kind() == LogosType::Kind::IntLit) {
                        auto* h = hint_for_tv(tv);
                        vt = (h && TypeRef(h).kind() != LogosType::Kind::Error) ? h : i32_t();
                    } else if (TypeRef(vt).kind() == LogosType::Kind::FloatLit) {
                        auto* h = hint_for_tv(tv);
                        vt = (h && TypeRef(h).kind() != LogosType::Kind::Error) ? h : prim(LogosType::Kind::F64);
                    }
                    inferred[std::string(tv)] = vt;
                }
            } else if (TypeRef(raw_ft).kind() == LogosType::Kind::Array && TypeRef(raw_ft).elem() &&
                       TypeRef(raw_ft).elem()->kind == LogosType::Kind::TypeVar) {
                // [T; N] field — infer T from element type of the value.
                auto& tv = TypeRef(raw_ft).elem()->type_var_name;
                if (!inferred.count(tv) && fval->type->kind == LogosType::Kind::Array &&
                    fval->type->elem) {
                    auto* vt = fval->type->elem;
                    if (TypeRef(vt).kind() == LogosType::Kind::IntLit) {
                        auto* h = hint_for_tv(tv);
                        vt = (h && TypeRef(h).kind() != LogosType::Kind::Error) ? h : i32_t();
                    }
                    inferred[tv] = vt;
                }
            } else if ((TypeRef(raw_ft).kind() == LogosType::Kind::Ptr ||
                        TypeRef(raw_ft).kind() == LogosType::Kind::Ref ||
                        TypeRef(raw_ft).kind() == LogosType::Kind::MutRef) && TypeRef(raw_ft).pointee() &&
                       TypeRef(raw_ft).pointee()->kind == LogosType::Kind::TypeVar) {
                // *T / &T / &mut T field — infer T from the value's pointee type.
                auto& tv = TypeRef(raw_ft).pointee()->type_var_name;
                if (!inferred.count(tv) && is_ref_like(fval->type->kind) &&
                    fval->type->pointee) {
                    auto* vt = fval->type->pointee;
                    if (TypeRef(vt).kind() != LogosType::Kind::Error)
                        inferred[tv] = vt;
                }
            }
        }
        // For any TypeVar still not inferred from fields, fall back to hint.
        for (auto& tp : sinfo.type_params) {
            if (!inferred.count(tp.name)) {
                auto* h = hint_for_tv(tp.name);
                if (h && TypeRef(h).kind() != LogosType::Kind::Error)
                    inferred[tp.name] = h;
            }
        }
        std::vector<const LogosType*> args;
        for (size_t i = 0, h_idx = 0; i < sinfo.type_params.size(); ++i) {
            auto& tp = sinfo.type_params[i];
            if (tp.is_variadic) {
                if (hint_struct_type_ && TypeRef(hint_struct_type_).struct_name() == std::string(sname)) {
                    while (h_idx < TypeRef(hint_struct_type_).type_args().size())
                        args.push_back(TypeRef(hint_struct_type_).type_args()[h_idx++]);
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
                } else if (hint_struct_type_ && TypeRef(hint_struct_type_).struct_name() == std::string(sname) && h_idx < TypeRef(hint_struct_type_).type_args().size()) {
                    args.push_back(TypeRef(hint_struct_type_).type_args()[h_idx++]);
                } else {
                    args.push_back(error_t());
                }
            }
        }
        check_type_bounds(std::string(sname), sinfo.type_params, args);
        std::vector<std::string> lit_lt_args;
        if (hint_struct_type_ && TypeRef(hint_struct_type_).struct_name() == std::string(sname))
            lit_lt_args = TypeRef(hint_struct_type_).lifetime_args();
        const LogosType* lit_type = slit_is_zoned
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
        StrMap<bool> initialized;
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
                bool ft_has_typevar = ft && (TypeRef(ft).kind() == LogosType::Kind::TypeVar ||
                    (TypeRef(ft).kind() == LogosType::Kind::Array && TypeRef(ft).elem() &&
                     TypeRef(ft).elem()->kind == LogosType::Kind::TypeVar));
                if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
                    fval->type->kind != LogosType::Kind::Error &&
                    !ft_has_typevar &&
                    !types_compatible(fval->type, ft) &&
                    !try_coerce_closure_to_fnptr(fval, ft)) {
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
    StrMap<bool> initialized;
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
                    if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
                        fval->type->kind != LogosType::Kind::Error &&
                        !types_compatible(fval->type, ft)) {
                        error(std::format("struct literal '{}' field '{}': expected {}, got {}",
                              sname, fname, type_str(ft), type_str(fval->type)));
                    }
                    if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
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
            if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
                fval->type->kind != LogosType::Kind::Error &&
                !types_compatible(fval->type, ft) &&
                !try_coerce_closure_to_fnptr(fval, ft)) {
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
            if (ft && TypeRef(ft).kind() == LogosType::Kind::Array && TypeRef(ft).elem() &&
                fval->type->kind == LogosType::Kind::Array)
                if (auto* al = std::get_if<lir::EArrLit>(&fval->kind))
                    for (size_t i = 0; i < al->elems.size(); ++i)
                        if (al->elems[i]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(al->elems[i].get()))
                                if (!intlit_fits(*v, TypeRef(ft).elem()->kind))
                                    error(std::format("struct literal '{}' field '{}': array element {}: value {} does not fit in {}",
                                          sname, fname, i, *v, type_str(TypeRef(ft).elem())));
            // Check tuple literal elements against narrow tuple field element types.
            if (ft && TypeRef(ft).kind() == LogosType::Kind::Tuple && fval->type->kind == LogosType::Kind::Tuple)
                if (auto* tl = std::get_if<lir::ETupleLit>(&fval->kind))
                    for (size_t i = 0; i < tl->elems.size() && i < TypeRef(ft).tuple_elems().size(); ++i) {
                        if (tl->elems[i]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(tl->elems[i].get()))
                                if (TypeRef(ft).tuple_elems()[i] && !intlit_fits(*v, TypeRef(ft).tuple_elems()[i]->kind))
                                    error(std::format("struct literal '{}' field '{}': tuple element {}: value {} does not fit in {}",
                                          sname, fname, i, *v, type_str(TypeRef(ft).tuple_elems()[i])));
                        if (TypeRef(ft).tuple_elems()[i] && TypeRef(ft).tuple_elems()[i]->kind == LogosType::Kind::Array &&
                            TypeRef(ft).tuple_elems()[i]->elem && tl->elems[i]->type->kind == LogosType::Kind::Array)
                            if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[i]->kind))
                                for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                    if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(ial->elems[ii].get()))
                                            if (!intlit_fits(*v, TypeRef(ft).tuple_elems()[i]->elem->kind))
                                                error(std::format("struct literal '{}' field '{}': tuple element {}: array element {}: value {} does not fit in {}",
                                                      sname, fname, i, ii, *v, type_str(TypeRef(ft).tuple_elems()[i]->elem)));

                        if (TypeRef(ft).tuple_elems()[i] && TypeRef(ft).tuple_elems()[i]->kind == LogosType::Kind::Tuple &&
                            tl->elems[i]->type->kind == LogosType::Kind::Tuple)
                            if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[i]->kind))
                                for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(ft).tuple_elems()[i]->tuple_elems.size(); ++ii)
                                    if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(itl->elems[ii].get()))
                                            if (TypeRef(ft).tuple_elems()[i]->tuple_elems[ii] && !intlit_fits(*v, TypeRef(ft).tuple_elems()[i]->tuple_elems[ii]->kind))
                                                error(std::format("struct literal '{}' field '{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      sname, fname, i, ii, *v, type_str(TypeRef(ft).tuple_elems()[i]->tuple_elems[ii])));
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
    if (hint_struct_type_ && TypeRef(hint_struct_type_).struct_name() == std::string(sname))
        ng_lt_args = TypeRef(hint_struct_type_).lifetime_args();
    LogosType ng_t;
    ng_t.kind = slit_is_zoned
                ? LogosType::Kind::ZonedStruct : LogosType::Kind::Struct;
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
    if (TypeRef(arr_type).kind() == LogosType::Kind::Slice) {
        auto* elem = TypeRef(arr_type).elem().raw() ? TypeRef(arr_type).elem().raw() : error_t();
        return make_expr(elem, lir::ESliceIndex{std::move(recv), std::move(idx)});
    }

    if (TypeRef(arr_type).kind() != LogosType::Kind::Array &&
        TypeRef(arr_type).kind() != LogosType::Kind::Ptr &&
        TypeRef(arr_type).kind() != LogosType::Kind::Ref &&
        TypeRef(arr_type).kind() != LogosType::Kind::MutRef &&
        TypeRef(arr_type).kind() != LogosType::Kind::Error) {
        error(std::format("index read: receiver is not an array, slice, or pointer (got {})",
              type_str(arr_type)));
    }
    if (TypeRef(arr_type).kind() == LogosType::Kind::Ptr && !inside_unsafe_) {
        error("index read through raw pointer requires unsafe context");
    }

    const LogosType* elem = error_t();
    if (TypeRef(arr_type).kind() == LogosType::Kind::Array && TypeRef(arr_type).elem().raw())  elem = TypeRef(arr_type).elem().raw();
    if ((TypeRef(arr_type).kind() == LogosType::Kind::Ptr ||
         TypeRef(arr_type).kind() == LogosType::Kind::Ref ||
         TypeRef(arr_type).kind() == LogosType::Kind::MutRef) && TypeRef(arr_type).pointee())
        elem = TypeRef(arr_type).pointee().raw();

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
        if (TypeRef(t).kind() != LogosType::Kind::Error && TypeRef(elem_type).kind() != LogosType::Kind::Error) {
            if (!types_compatible(t, elem_type) && !types_compatible(elem_type, t)) {
                error(std::format("array literal: element {} has type {}, expected {}",
                      i, type_str(t), type_str(elem_type)));
            } else {
                // If the concrete element type is narrow and this element is IntLit, check range.
                if (TypeRef(t).kind() == LogosType::Kind::IntLit &&
                    TypeRef(elem_type).kind() != LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(elems[i].get()))
                        if (!intlit_fits(*v, elem_type->kind))
                            error(std::format("array literal: element {}: value {} does not fit in {}",
                                  i, *v, type_str(elem_type)));
                // Check array literal elements against narrow nested array element types.
                if (TypeRef(elem_type).kind() == LogosType::Kind::Array && TypeRef(elem_type).elem() &&
                    TypeRef(t).kind() == LogosType::Kind::Array)
                    if (auto* al = std::get_if<lir::EArrLit>(&elems[i]->kind))
                        for (size_t ei = 0; ei < al->elems.size(); ++ei)
                            if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(al->elems[ei].get()))
                                    if (!intlit_fits(*v, TypeRef(elem_type).elem()->kind))
                                        error(std::format("array literal: element {}: sub-element {}: value {} does not fit in {}",
                                              i, ei, *v, type_str(TypeRef(elem_type).elem())));
                // Check tuple literal elements against narrow nested tuple element types.
                if (TypeRef(elem_type).kind() == LogosType::Kind::Tuple && TypeRef(t).kind() == LogosType::Kind::Tuple)
                    if (auto* tl = std::get_if<lir::ETupleLit>(&elems[i]->kind))
                        for (size_t ei = 0; ei < tl->elems.size() && ei < TypeRef(elem_type).tuple_elems().size(); ++ei) {
                            if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(tl->elems[ei].get()))
                                    if (TypeRef(elem_type).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(elem_type).tuple_elems()[ei]->kind))
                                        error(std::format("array literal: element {}: tuple element {}: value {} does not fit in {}",
                                              i, ei, *v, type_str(TypeRef(elem_type).tuple_elems()[ei])));
                            if (TypeRef(elem_type).tuple_elems()[ei] && TypeRef(elem_type).tuple_elems()[ei]->kind == LogosType::Kind::Array &&
                                TypeRef(elem_type).tuple_elems()[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                                if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                        if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(ial->elems[ii].get()))
                                                if (!intlit_fits(*v, TypeRef(elem_type).tuple_elems()[ei]->elem->kind))
                                                    error(std::format("array literal: element {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                          i, ei, ii, *v, type_str(TypeRef(elem_type).tuple_elems()[ei]->elem)));

                            if (TypeRef(elem_type).tuple_elems()[ei] && TypeRef(elem_type).tuple_elems()[ei]->kind == LogosType::Kind::Tuple &&
                                tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                                if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                    for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(elem_type).tuple_elems()[ei]->tuple_elems.size(); ++ii)
                                        if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(itl->elems[ii].get()))
                                                if (TypeRef(elem_type).tuple_elems()[ei]->tuple_elems[ii] && !intlit_fits(*v, TypeRef(elem_type).tuple_elems()[ei]->tuple_elems[ii]->kind))
                                                    error(std::format("array literal: element {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                          i, ei, ii, *v, type_str(TypeRef(elem_type).tuple_elems()[ei]->tuple_elems[ii])));
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
            if (TypeRef(ti).kind() != LogosType::Kind::IntLit &&
                !(TypeRef(ti).kind() == LogosType::Kind::Array && TypeRef(ti).elem() &&
                  TypeRef(ti).elem()->kind == LogosType::Kind::IntLit))
                anchor = ti;
        }
        if (anchor) {
            auto* e = elems[0].get();
            auto* t0 = elems[0]->type;
            // Scalar IntLit at element 0.
            if (TypeRef(t0).kind() == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(e))
                    if (!intlit_fits(*v, anchor->kind))
                        error(std::format("array literal: element 0: value {} does not fit in {}",
                              *v, type_str(anchor)));
            // Array literal at element 0 (e.g. [[1,200,3], concrete_arr]).
            if (TypeRef(anchor).kind() == LogosType::Kind::Array && TypeRef(anchor).elem() &&
                TypeRef(t0).kind() == LogosType::Kind::Array)
                if (auto* al = std::get_if<lir::EArrLit>(&e->kind))
                    for (size_t ei = 0; ei < al->elems.size(); ++ei)
                        if (al->elems[ei]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(al->elems[ei].get()))
                                if (!intlit_fits(*v, TypeRef(anchor).elem()->kind))
                                    error(std::format("array literal: element 0: sub-element {}: value {} does not fit in {}",
                                          ei, *v, type_str(TypeRef(anchor).elem())));
            // Tuple literal at element 0 (tuple elements, including nested array/tuple).
            if (TypeRef(anchor).kind() == LogosType::Kind::Tuple && TypeRef(t0).kind() == LogosType::Kind::Tuple)
                if (auto* tl = std::get_if<lir::ETupleLit>(&e->kind))
                    for (size_t ei = 0; ei < tl->elems.size() && ei < TypeRef(anchor).tuple_elems().size(); ++ei) {
                        if (tl->elems[ei]->type->kind == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(tl->elems[ei].get()))
                                if (TypeRef(anchor).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(anchor).tuple_elems()[ei]->kind))
                                    error(std::format("array literal: element 0: tuple element {}: value {} does not fit in {}",
                                          ei, *v, type_str(TypeRef(anchor).tuple_elems()[ei])));
                        if (TypeRef(anchor).tuple_elems()[ei] && TypeRef(anchor).tuple_elems()[ei]->kind == LogosType::Kind::Array &&
                            TypeRef(anchor).tuple_elems()[ei]->elem && tl->elems[ei]->type->kind == LogosType::Kind::Array)
                            if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[ei]->kind))
                                for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                    if (ial->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(ial->elems[ii].get()))
                                            if (!intlit_fits(*v, TypeRef(anchor).tuple_elems()[ei]->elem->kind))
                                                error(std::format("array literal: element 0: tuple element {}: array element {}: value {} does not fit in {}",
                                                      ei, ii, *v, type_str(TypeRef(anchor).tuple_elems()[ei]->elem)));
                        if (TypeRef(anchor).tuple_elems()[ei] && TypeRef(anchor).tuple_elems()[ei]->kind == LogosType::Kind::Tuple &&
                            tl->elems[ei]->type->kind == LogosType::Kind::Tuple)
                            if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[ei]->kind))
                                for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(anchor).tuple_elems()[ei]->tuple_elems.size(); ++ii)
                                    if (itl->elems[ii]->type->kind == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(itl->elems[ii].get()))
                                            if (TypeRef(anchor).tuple_elems()[ei]->tuple_elems[ii] && !intlit_fits(*v, TypeRef(anchor).tuple_elems()[ei]->tuple_elems[ii]->kind))
                                                error(std::format("array literal: element 0: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      ei, ii, *v, type_str(TypeRef(anchor).tuple_elems()[ei]->tuple_elems[ii])));
                    }
        }
    }
    // For IntLit element type: upgrade to i64 if any value overflows i32.
    // Keep IntLit (don't collapse to i32) so that annotation-based coercion
    // ([i64; N] = [1, 2, 3]) can use types_compatible([IntLit;N], [i64;N]) → true.
    if (TypeRef(elem_type).kind() == LogosType::Kind::IntLit) {
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

// List comprehension:  [elem_expr for x in iter_expr (if guard)?]
// Desugars to a block expression that creates a Vec<T>, iterates over
// iter_expr, optionally filters by guard, and pushes elem_expr into the Vec.
// Requires `use std.collections.vec;` in scope.
// Iterator support: array / slice (via SForEach); generic iterator path
// (types with .next() returning Option<T>) is deferred.
lir::LExprPtr SemaChecker::lower_list_comp(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();
    const LogosType* iter_type = iter->type;

    // Only array/slice iteration supported for now.
    const LogosType* elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem().raw() ? TypeRef(iter_type).elem().raw() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem().raw() ? TypeRef(iter_type).elem().raw() : i32_t();
        is_slice  = true;
    } else {
        error(std::format(
            "list comprehension: only array/slice iteration supported (got {})",
            type_str(iter_type)));
        return error_expr();
    }

    // Require Vec<T> available (via `use std.vec`).
    {
        auto [vpkg, vsi] = find_struct_by_name("Vec");
        if (!vsi) {
            error("list comprehension requires `use std.collections.vec;`");
            return error_expr();
        }
    }
    auto* vec_new_fi = find_generic_func("vec_new");
    if (!vec_new_fi) {
        error("list comprehension: vec_new not found; add `use std.collections.vec;`");
        return error_expr();
    }

    const LogosType* vec_t = make_generic_struct("Vec", {elem_type});

    std::string vec_var = "__lc_v_" + std::to_string(tmp_var_count_++);

    // SLet: let mut vec_var: Vec<T> = vec_new::<T>();
    // Use symbol_name (may include __g__... suffix for method-level generics).
    std::string vec_new_sym = vec_new_fi->symbol_name.empty() ? "vec_new"
                                                              : vec_new_fi->symbol_name;
    auto call_new = make_expr(vec_t, lir::ECall{vec_new_sym, {elem_type}, {}});
    lir::SLet let_v;
    let_v.name   = vec_var;
    let_v.type   = vec_t;
    let_v.is_mut = true;
    let_v.value  = std::move(call_new);

    // Lower VALUE + optional GUARD with var_name in scope.
    push_scope();
    define(vec_var, vec_t, true);
    define(std::string(var_name), elem_type, false);
    auto elem_expr = lower_expr(map_of(node.get(la::VALUE.code)));
    lir::LExprPtr guard_expr = nullptr;
    if (node.has_key(la::GUARD))
        guard_expr = lower_expr(map_of(node.get(la::GUARD.code)));
    pop_scope();

    // Call Vec::push(&mut vec_var, elem) as a direct ECall.
    // Emit with callee "Vec__push" and type_args=[elem_type]; mono_clone will
    // rewrite to the struct-specialized name (e.g. Vec$G1$i32__push).
    auto recv = make_expr(make_ptr(true, vec_t), lir::EAddrOf{vec_var});
    std::vector<lir::LExprPtr> push_args;
    push_args.push_back(std::move(recv));
    push_args.push_back(std::move(elem_expr));
    auto push_call = make_expr(void_t(),
        lir::ECall{"Vec__push", {elem_type}, std::move(push_args)});

    lir::SExprStmt push_stmt;
    push_stmt.expr = std::move(push_call);

    auto loop_body = std::make_unique<lir::LBlock>();
    if (guard_expr) {
        lir::SIf sif;
        sif.cond = std::move(guard_expr);
        sif.then_ = std::make_unique<lir::LBlock>();
        sif.then_->stmts.push_back(make_stmt(node_line_, std::move(push_stmt)));
        loop_body->stmts.push_back(make_stmt(node_line_, std::move(sif)));
    } else {
        loop_body->stmts.push_back(make_stmt(node_line_, std::move(push_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = std::move(loop_body);

    auto outer = std::make_unique<lir::LBlock>();
    outer->stmts.push_back(make_stmt(node_line_, std::move(let_v)));
    outer->stmts.push_back(make_stmt(node_line_, std::move(sfe)));

    auto result = make_expr(vec_t, lir::EVarRef{vec_var});
    return make_expr(vec_t, lir::EBlockExpr{std::move(outer), std::move(result)});
}

// Map comprehension:  {kexpr: vexpr for x in iter_expr (if guard)?}
// Desugars to a block that creates a HashMap<K,V>, iterates over iter_expr,
// optionally filters by guard, and inserts (kexpr, vexpr) pairs.
// Requires `use std.collections.hashmap;` in scope.
lir::LExprPtr SemaChecker::lower_map_comp(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();
    const LogosType* iter_type = iter->type;

    const LogosType* elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem().raw() ? TypeRef(iter_type).elem().raw() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem().raw() ? TypeRef(iter_type).elem().raw() : i32_t();
        is_slice  = true;
    } else {
        error(std::format(
            "map comprehension: only array/slice iteration supported (got {})",
            type_str(iter_type)));
        return error_expr();
    }

    {
        auto [hmpkg, hmsi] = find_struct_by_name("HashMap");
        if (!hmsi) {
            error("map comprehension requires `use std.collections.hashmap;`");
            return error_expr();
        }
    }
    auto* hm_new_fi = find_generic_func("hashmap_new");
    if (!hm_new_fi) {
        error("map comprehension: hashmap_new not found; add `use std.collections.hashmap;`");
        return error_expr();
    }

    std::string hm_var = "__mc_m_" + std::to_string(tmp_var_count_++);

    push_scope();
    define(std::string(var_name), elem_type, false);
    auto key_expr_body = lower_expr(map_of(node.get(la::KEY.code)));
    auto val_expr_body = lower_expr(map_of(node.get(la::VALUE.code)));
    lir::LExprPtr guard_body = nullptr;
    if (node.has_key(la::GUARD))
        guard_body = lower_expr(map_of(node.get(la::GUARD.code)));
    pop_scope();

    const LogosType* k_type = key_expr_body->type;
    const LogosType* v_type = val_expr_body->type;
    const LogosType* hm_t = make_generic_struct("HashMap", {k_type, v_type});

    std::string hm_new_sym = hm_new_fi->symbol_name.empty() ? "hashmap_new"
                                                            : hm_new_fi->symbol_name;
    auto call_new = make_expr(hm_t, lir::ECall{hm_new_sym, {k_type, v_type}, {}});
    lir::SLet let_m;
    let_m.name   = hm_var;
    let_m.type   = hm_t;
    let_m.is_mut = true;
    let_m.value  = std::move(call_new);

    // HashMap::insert(&mut hm, key, val) — unsafe method, emitted as direct ECall
    // "HashMap__insert" so mono_clone rewrites to HashMap$G1$..$G2$..__insert.
    auto recv = make_expr(make_ptr(true, hm_t), lir::EAddrOf{hm_var});
    std::vector<lir::LExprPtr> ins_args;
    ins_args.push_back(std::move(recv));
    ins_args.push_back(std::move(key_expr_body));
    ins_args.push_back(std::move(val_expr_body));
    auto ins_call = make_expr(void_t(),
        lir::ECall{"HashMap__insert", {k_type, v_type}, std::move(ins_args)});

    lir::SExprStmt ins_stmt;
    ins_stmt.expr = std::move(ins_call);

    auto loop_body = std::make_unique<lir::LBlock>();
    if (guard_body) {
        lir::SIf sif;
        sif.cond = std::move(guard_body);
        sif.then_ = std::make_unique<lir::LBlock>();
        sif.then_->stmts.push_back(make_stmt(node_line_, std::move(ins_stmt)));
        loop_body->stmts.push_back(make_stmt(node_line_, std::move(sif)));
    } else {
        loop_body->stmts.push_back(make_stmt(node_line_, std::move(ins_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = std::move(loop_body);

    auto outer = std::make_unique<lir::LBlock>();
    outer->stmts.push_back(make_stmt(node_line_, std::move(let_m)));
    outer->stmts.push_back(make_stmt(node_line_, std::move(sfe)));

    auto result = make_expr(hm_t, lir::EVarRef{hm_var});
    return make_expr(hm_t, lir::EBlockExpr{std::move(outer), std::move(result)});
}

// Hermes list comprehension:  @[expr for x in iter_expr (if guard)?]
// Desugars to a block that builds a Hermes whose root is an
// ObjectArray of AnyVals, iterating over iter_expr and optionally
// filtering by guard.  Element expression must evaluate to AnyVal
// (user coerces scalars explicitly via AnyVal::embed_i24 etc.).
// Requires `use std.hermes.ctr;` in scope.
lir::LExprPtr SemaChecker::lower_hermes_list_comp(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();
    const LogosType* iter_type = iter->type;

    // Short-circuit on upstream error to avoid cascading diagnostics.
    if (TypeRef(iter_type).kind() == LogosType::Kind::Error)
        return error_expr();

    const LogosType* elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem().raw() ? TypeRef(iter_type).elem().raw() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem().raw() ? TypeRef(iter_type).elem().raw() : i32_t();
        is_slice  = true;
    } else {
        error(std::format(
            "hermes list comprehension: only array/slice iteration supported (got {})",
            type_str(iter_type)));
        return error_expr();
    }

    {
        auto [hpkg, hsi] = find_struct_by_name("Hermes");
        if (!hsi) {
            error("hermes list comprehension requires `use std.hermes.ctr;`");
            return error_expr();
        }
    }

    auto new_cands  = find_func_candidates("hermes_list_comp_new");
    auto push_cands = find_func_candidates("hermes_list_comp_push");
    const SemaFuncInfo* new_fi  = nullptr;
    const SemaFuncInfo* push_fi = nullptr;
    for (auto* fi : new_cands)  if (fi->param_types.size() == 1) { new_fi  = fi; break; }
    for (auto* fi : push_cands) if (fi->param_types.size() == 2) { push_fi = fi; break; }
    if (!new_fi || !push_fi) {
        error("hermes list comprehension requires `use std.hermes.ctr;`");
        return error_expr();
    }

    const LogosType* ctr_t = make_struct_type("Hermes");

    std::string ctr_var = "__hlc_c_" + std::to_string(tmp_var_count_++);

    push_scope();
    define(ctr_var, ctr_t, true);
    define(std::string(var_name), elem_type, false);
    auto val_expr_body = lower_expr(map_of(node.get(la::VALUE.code)));
    lir::LExprPtr guard_body = nullptr;
    if (node.has_key(la::GUARD))
        guard_body = lower_expr(map_of(node.get(la::GUARD.code)));
    pop_scope();

    // Coerce VALUE to AnyVal (no-op if already AnyVal).
    val_expr_body = coerce_to_hermes_anyval(
        std::move(val_expr_body), ctr_var, ctr_t,
        "hermes list comprehension element");
    if (!val_expr_body || val_expr_body->type->kind == LogosType::Kind::Error)
        return error_expr();

    // Guard must be Bool; any other type (including Error) is rejected here to
    // avoid cascading diagnostics and to prevent an MLIR verification crash
    // from feeding a non-i1 value into cf.cond_br.
    if (guard_body) {
        auto gk = guard_body->type ? guard_body->type->kind
                                   : LogosType::Kind::Error;
        if (gk == LogosType::Kind::Error)
            return error_expr();
        if (gk != LogosType::Kind::Bool) {
            error(std::format(
                "hermes list comprehension: guard must be bool (got {})",
                type_str(guard_body->type)));
            return error_expr();
        }
    }

    // SLet: let mut __hlc_c = hermes_list_comp_new(128);
    std::string new_sym = new_fi->symbol_name.empty() ? "hermes_list_comp_new"
                                                      : new_fi->symbol_name;
    std::vector<lir::LExprPtr> new_args;
    int64_t cap_hint = arr_size > 0 ? (arr_size * 8 + 128) : 128;
    new_args.push_back(make_expr(prim(LogosType::Kind::I64), lir::ELitInt{cap_hint}));
    auto call_new = make_expr(ctr_t,
        lir::ECall{new_sym, {}, std::move(new_args)});
    lir::SLet let_c;
    let_c.name   = ctr_var;
    let_c.type   = ctr_t;
    let_c.is_mut = true;
    let_c.value  = std::move(call_new);

    // hermes_list_comp_push(&mut __hlc_c, val);
    std::string push_sym = push_fi->symbol_name.empty() ? "hermes_list_comp_push"
                                                        : push_fi->symbol_name;
    auto recv = make_expr(make_ptr(true, ctr_t), lir::EAddrOf{ctr_var});
    std::vector<lir::LExprPtr> push_args;
    push_args.push_back(std::move(recv));
    push_args.push_back(std::move(val_expr_body));
    auto push_call = make_expr(void_t(),
        lir::ECall{push_sym, {}, std::move(push_args)});

    lir::SExprStmt push_stmt;
    push_stmt.expr = std::move(push_call);

    auto loop_body = std::make_unique<lir::LBlock>();
    if (guard_body) {
        lir::SIf sif;
        sif.cond = std::move(guard_body);
        sif.then_ = std::make_unique<lir::LBlock>();
        sif.then_->stmts.push_back(make_stmt(node_line_, std::move(push_stmt)));
        loop_body->stmts.push_back(make_stmt(node_line_, std::move(sif)));
    } else {
        loop_body->stmts.push_back(make_stmt(node_line_, std::move(push_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = std::move(loop_body);

    auto outer = std::make_unique<lir::LBlock>();
    outer->stmts.push_back(make_stmt(node_line_, std::move(let_c)));
    outer->stmts.push_back(make_stmt(node_line_, std::move(sfe)));

    auto result = make_expr(ctr_t, lir::EVarRef{ctr_var});
    return make_expr(ctr_t, lir::EBlockExpr{std::move(outer), std::move(result)});
}

// Hermes map comprehension:  @{kexpr: vexpr for x in iter (if guard)?}
// v1: string keys only (`str`); values must be AnyVal.
// Requires `use std.hermes.ctr;` in scope.
lir::LExprPtr SemaChecker::lower_hermes_map_comp(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();
    const LogosType* iter_type = iter->type;

    // Short-circuit on upstream error to avoid cascading diagnostics.
    if (TypeRef(iter_type).kind() == LogosType::Kind::Error)
        return error_expr();

    const LogosType* elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem().raw() ? TypeRef(iter_type).elem().raw() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem().raw() ? TypeRef(iter_type).elem().raw() : i32_t();
        is_slice  = true;
    } else {
        error(std::format(
            "hermes map comprehension: only array/slice iteration supported (got {})",
            type_str(iter_type)));
        return error_expr();
    }

    {
        auto [hpkg, hsi] = find_struct_by_name("Hermes");
        if (!hsi) {
            error("hermes map comprehension requires `use std.hermes.ctr;`");
            return error_expr();
        }
    }

    auto new_cands = find_func_candidates("hermes_map_comp_new");
    auto put_cands = find_func_candidates("hermes_map_comp_put");
    const SemaFuncInfo* new_fi = nullptr;
    const SemaFuncInfo* put_fi = nullptr;
    for (auto* fi : new_cands) if (fi->param_types.size() == 2) { new_fi = fi; break; }
    for (auto* fi : put_cands) if (fi->param_types.size() == 3) { put_fi = fi; break; }
    if (!new_fi || !put_fi) {
        error("hermes map comprehension requires `use std.hermes.ctr;`");
        return error_expr();
    }

    const LogosType* ctr_t = make_struct_type("Hermes");

    std::string ctr_var = "__hmc_c_" + std::to_string(tmp_var_count_++);

    push_scope();
    define(ctr_var, ctr_t, true);
    define(std::string(var_name), elem_type, false);
    auto key_expr = lower_expr(map_of(node.get(la::KEY.code)));
    auto val_expr = lower_expr(map_of(node.get(la::VALUE.code)));
    lir::LExprPtr guard_body = nullptr;
    if (node.has_key(la::GUARD))
        guard_body = lower_expr(map_of(node.get(la::GUARD.code)));
    pop_scope();

    // Require KEY to be str (&[u8] slice).  Short-circuit on Error to avoid
    // cascading diagnostics when the key subexpression already failed.
    const LogosType* kt = key_expr->type;
    if (kt && TypeRef(kt).kind() == LogosType::Kind::Error)
        return error_expr();
    if (!(kt && TypeRef(kt).kind() == LogosType::Kind::Slice && TypeRef(kt).elem()
              && TypeRef(kt).elem()->kind == LogosType::Kind::U8)) {
        error(std::format(
            "hermes map comprehension: key expression must be str (got {})",
            type_str(kt)));
        return error_expr();
    }

    // Coerce VALUE to AnyVal (no-op if already AnyVal).
    val_expr = coerce_to_hermes_anyval(
        std::move(val_expr), ctr_var, ctr_t,
        "hermes map comprehension value");
    if (!val_expr || val_expr->type->kind == LogosType::Kind::Error)
        return error_expr();

    // Guard must be Bool; reject anything else early to avoid MLIR crashes
    // (cf.cond_br requires i1) and to silence cascades when the guard errored.
    if (guard_body) {
        auto gk = guard_body->type ? guard_body->type->kind
                                   : LogosType::Kind::Error;
        if (gk == LogosType::Kind::Error)
            return error_expr();
        if (gk != LogosType::Kind::Bool) {
            error(std::format(
                "hermes map comprehension: guard must be bool (got {})",
                type_str(guard_body->type)));
            return error_expr();
        }
    }

    std::string new_sym = new_fi->symbol_name.empty() ? "hermes_map_comp_new"
                                                      : new_fi->symbol_name;
    // Byte-cap hint for zone, and slot-count hint for map buckets.
    // For slices (arr_size==0 at compile time) we don't know iter length, so
    // use a generous default to reduce the risk of silent drops.  This is a
    // v1 limitation — objectmap_set has no auto-grow.
    int64_t slot_hint = arr_size > 0 ? arr_size : 64;
    int64_t cap_hint  = arr_size > 0 ? (arr_size * 48 + 256) : 4096;
    std::vector<lir::LExprPtr> new_args;
    new_args.push_back(make_expr(prim(LogosType::Kind::I64), lir::ELitInt{cap_hint}));
    new_args.push_back(make_expr(prim(LogosType::Kind::I64), lir::ELitInt{slot_hint}));
    auto call_new = make_expr(ctr_t,
        lir::ECall{new_sym, {}, std::move(new_args)});
    lir::SLet let_c;
    let_c.name   = ctr_var;
    let_c.type   = ctr_t;
    let_c.is_mut = true;
    let_c.value  = std::move(call_new);

    std::string put_sym = put_fi->symbol_name.empty() ? "hermes_map_comp_put"
                                                      : put_fi->symbol_name;
    auto recv = make_expr(make_ptr(true, ctr_t), lir::EAddrOf{ctr_var});
    std::vector<lir::LExprPtr> put_args;
    put_args.push_back(std::move(recv));
    put_args.push_back(std::move(key_expr));
    put_args.push_back(std::move(val_expr));
    auto put_call = make_expr(void_t(),
        lir::ECall{put_sym, {}, std::move(put_args)});

    lir::SExprStmt put_stmt;
    put_stmt.expr = std::move(put_call);

    auto loop_body = std::make_unique<lir::LBlock>();
    if (guard_body) {
        lir::SIf sif;
        sif.cond = std::move(guard_body);
        sif.then_ = std::make_unique<lir::LBlock>();
        sif.then_->stmts.push_back(make_stmt(node_line_, std::move(put_stmt)));
        loop_body->stmts.push_back(make_stmt(node_line_, std::move(sif)));
    } else {
        loop_body->stmts.push_back(make_stmt(node_line_, std::move(put_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = std::move(loop_body);

    auto outer = std::make_unique<lir::LBlock>();
    outer->stmts.push_back(make_stmt(node_line_, std::move(let_c)));
    outer->stmts.push_back(make_stmt(node_line_, std::move(sfe)));

    auto result = make_expr(ctr_t, lir::EVarRef{ctr_var});
    return make_expr(ctr_t, lir::EBlockExpr{std::move(outer), std::move(result)});
}

// Coerce an arbitrary value to AnyVal for use inside a Hermes comprehension.
// Returns the original expr if already AnyVal; otherwise wraps in a call to
// one of the `hermes_coerce_*` helpers in hermes/ctr.logos. String coercion
// requires `&mut ctr_var` because the string is copied into the zone.
lir::LExprPtr SemaChecker::coerce_to_hermes_anyval(
        lir::LExprPtr val,
        const std::string& ctr_var,
        const LogosType* ctr_t,
        std::string_view context) {
    if (!val || !val->type) return val;
    const LogosType* t = val->type;

    // Pass Error through unchanged — caller short-circuits on Error without
    // emitting an additional "cannot auto-coerce <error>" diagnostic.
    if (TypeRef(t).kind() == LogosType::Kind::Error) return val;

    // AnyVal passthrough (datatype or struct form).
    if ((TypeRef(t).kind() == LogosType::Kind::Struct
         || TypeRef(t).kind() == LogosType::Kind::ZonedStruct)
        && TypeRef(t).struct_name() == "AnyVal") {
        return val;
    }

    const char* helper = nullptr;
    bool needs_ctr = false;
    using K = LogosType::Kind;
    switch (TypeRef(t).kind()) {
        case K::Bool: helper = "hermes_coerce_bool"; break;
        case K::I8:   helper = "hermes_coerce_i8";   break;
        case K::I16:  helper = "hermes_coerce_i16";  break;
        case K::I32:  case K::IntLit:
                      helper = "hermes_coerce_i32"; break;
        case K::U8:   helper = "hermes_coerce_u8";   break;
        case K::U16:  helper = "hermes_coerce_u16";  break;
        case K::U32:  helper = "hermes_coerce_u32"; break;
        // i64/u64/i24/u24/i56/u56/i128/u128 intentionally omitted: embedding
        // them via i24 would silently truncate high bits.  User must cast
        // explicitly (e.g. `x as i32`) or wrap with AnyVal::embed_i24.
        case K::Slice:
            if (TypeRef(t).elem() && TypeRef(t).elem()->kind == K::U8) {
                helper = "hermes_coerce_str";
                needs_ctr = true;
            }
            break;
        default: break;
    }

    if (!helper) {
        error(std::format(
            "{}: cannot auto-coerce {} to AnyVal; cast to i32/u32/bool/str "
            "explicitly, or wrap with AnyVal::embed_*",
            context, type_str(t)));
        return error_expr();
    }

    size_t want_arity = needs_ctr ? 2 : 1;
    auto cands = find_func_candidates(helper);
    const SemaFuncInfo* fi = nullptr;
    for (auto* c : cands) {
        if (c->param_types.size() == want_arity) { fi = c; break; }
    }
    if (!fi) {
        error(std::format("{}: {} not found; `use std.hermes.ctr;`",
                          context, helper));
        return error_expr();
    }
    const LogosType* ret_t = fi->ret_type;

    std::vector<lir::LExprPtr> args;
    if (needs_ctr) {
        auto recv = make_expr(make_ptr(true, ctr_t), lir::EAddrOf{ctr_var});
        args.push_back(std::move(recv));
    }
    args.push_back(std::move(val));
    std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
    return make_expr(ret_t, lir::ECall{sym, {}, std::move(args)});
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
    auto [epkg_el, esi_el] = find_enum_by_name(ename);
    auto eit = esi_el ? enums_.find(sema_key(epkg_el, std::string(ename))) : enums_.end();
    if (eit == enums_.end()) eit = enums_.find(std::string(ename));
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
    int64_t disc = 0;
    bool found = false;
    for (auto& v : eit->second.variants)
        if (v.name == vname) { disc = v.value; found = true; break; }
    if (!found) {
        error(std::format("enum '{}' has no variant '{}'", ename, vname));
        return error_expr();
    }
    return make_expr(make_enum_type(ename, epkg_el),
        lir::EEnumLit{std::string(ename), std::string(vname), disc});
}

lir::LExprPtr SemaChecker::lower_enum_lit_data(TinyMapView node) {
    auto ename = str_of(node.get(la::NAME.code));
    auto vname = str_of(node.get(la::FIELD.code));
    auto [epkg_eld, esi_eld] = find_enum_by_name(ename);
    auto eit = esi_eld ? enums_.find(sema_key(epkg_eld, std::string(ename))) : enums_.end();
    if (eit == enums_.end()) eit = enums_.find(std::string(ename));
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
            if (pt && TypeRef(pt).kind() == LogosType::Kind::TypeVar) {
                auto* inferred = payload[i]->type;
                if (TypeRef(inferred).kind() == LogosType::Kind::IntLit) inferred = i32_t();
                subst[std::string(TypeRef(pt).type_var_name())] = inferred;
            }
        }
        // Fill any still-unresolved type params from hint (e.g. let e: Result<i32,i32> = Result::Err(-1))
        if (hint_enum_type_ && TypeRef(hint_enum_type_).enum_name() == std::string(ename)) {
            for (size_t i = 0; i < einfo.type_params.size() && i < TypeRef(hint_enum_type_).type_args().size(); ++i) {
                if (subst.find(einfo.type_params[i].name) == subst.end()) {
                    auto* hta = TypeRef(hint_enum_type_).type_args()[i];
                    if (hta && TypeRef(hta).kind() != LogosType::Kind::Error)
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
                    TypeRef(pack_t).kind() != LogosType::Kind::Error &&
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
    auto [epkg_els, esi_els] = find_enum_by_name(ename);
    auto eit = esi_els ? enums_.find(sema_key(epkg_els, std::string(ename))) : enums_.end();
    if (eit == enums_.end()) eit = enums_.find(std::string(ename));
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
            if (pt && TypeRef(pt).kind() == LogosType::Kind::TypeVar) {
                auto* inferred = payload[i]->type;
                if (TypeRef(inferred).kind() == LogosType::Kind::IntLit) inferred = i32_t();
                subst[std::string(TypeRef(pt).type_var_name())] = inferred;
            }
        }
        // Fill any still-unresolved type params from hint
        if (hint_enum_type_ && TypeRef(hint_enum_type_).enum_name() == std::string(ename)) {
            for (size_t i = 0; i < einfo.type_params.size() && i < TypeRef(hint_enum_type_).type_args().size(); ++i) {
                if (subst.find(einfo.type_params[i].name) == subst.end()) {
                    auto* hta = TypeRef(hint_enum_type_).type_args()[i];
                    if (hta && TypeRef(hta).kind() != LogosType::Kind::Error)
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
                    TypeRef(pack_t).kind() != LogosType::Kind::Error &&
                    !types_compatible(payload[i]->type, pack_t))
                    error(std::format("{}::{} variadic arg {}: expected {}, got {}",
                          ename, vname, i, type_str(pack_t), type_str(payload[i]->type)));
                if (TypeRef(pack_t).kind() != LogosType::Kind::Error &&
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
    {
        auto [epkg_sc, esi_sc] = find_enum_by_name(class_name);
        bool is_enum = esi_sc != nullptr;
        if (!is_enum) is_enum = enums_.count(std::string(class_name)) > 0;
        if (is_enum) {
            // Reinterpret as ENUM_LIT_DATA: NAME=class_name, FIELD=method_name
            // Build a fake node view... or just inline the logic.
            return lower_enum_lit_data_from_static(node, class_name, method_name);
        }
    }

    // Resolve type aliases: `type ObjectArray = Array<AnyVal>;`
    // makes `ObjectArray::init(...)` call `Array$G1$AnyVal__init(...)`.
    std::string resolved_class(class_name);
    {
        auto ait = type_aliases_.find(resolved_class);
        if (ait != type_aliases_.end() && ait->second.type_params.empty()) {
            auto* aliased = ait->second.type;
            if (aliased && (TypeRef(aliased).kind() == LogosType::Kind::Struct ||
                            TypeRef(aliased).kind() == LogosType::Kind::ZonedStruct)) {
                resolved_class = TypeRef(aliased).type_args().empty()
                    ? TypeRef(aliased).struct_name()
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
        // Turbofish + concrete partial-spec impl: `Map::<i32, AnyVal>::init`
        // where `impl Map<i32, AnyVal> { fn init }` registers methods under
        // the mangled concrete name (e.g. `Map$G2$i32$AnyVal__init`).  If the
        // base lookup missed, build the concrete mangled name from turbofish
        // args and retry.
        if (!fi_ptr && node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                std::vector<const LogosType*> tf_args;
                bool all_concrete = true;
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto* t = resolve_type(map_of(items.get(i)));
                    if (!t || TypeRef(t).kind() == LogosType::Kind::TypeVar) { all_concrete = false; break; }
                    tf_args.push_back(t);
                }
                if (all_concrete && !tf_args.empty()) {
                    bool is_zoned = datatypes_.count(resolved_class) > 0;
                    const LogosType* concrete_t = is_zoned
                        ? make_generic_datatype(resolved_class, tf_args)
                        : make_generic_struct(resolved_class, tf_args);
                    std::string concrete_mangled = concrete_struct_name(concrete_t) + "__" + std::string(method_name);
                    auto fit2 = find_func_by_base_and_signature(concrete_mangled, arg_types, false);
                    if (fit2) { fi_ptr = fit2; mangled = concrete_mangled; }
                    else {
                        auto cands2 = find_func_candidates(concrete_mangled);
                        if (cands2.size() == 1) { fi_ptr = cands2[0]; mangled = concrete_mangled; }
                        else {
                            for (auto* cand : cands2) {
                                if (!cand || !cand->type_params.empty()) continue;
                                if (cand->param_types.size() != arg_exprs.size()) continue;
                                fi_ptr = cand; mangled = concrete_mangled; break;
                            }
                        }
                    }
                }
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
            if (t && (TypeRef(t).kind() == LogosType::Kind::TypeVar ||
                      TypeRef(t).kind() == LogosType::Kind::AssocType)) {
                in_generic_context = true; break;
            }
        }
        if (!in_generic_context && node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto* t = resolve_type(map_of(items.get(i)));
                    if (t && (TypeRef(t).kind() == LogosType::Kind::TypeVar ||
                              TypeRef(t).kind() == LogosType::Kind::AssocType)) {
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
            if (TypeRef(at).kind() != LogosType::Kind::Error &&
                TypeRef(pt).kind() != LogosType::Kind::Error &&
                !types_compatible(at, pt))
                error(std::format("static call '{}' arg {}: expected {}, got {}",
                      mangled, i + 1, type_str(pt), type_str(at)));
        }
    }

    // Move semantics: mark by-value move-type args as moved so that scope-end
    // drops do not fire on locals whose ownership has been transferred.
    for (auto& a : arg_exprs) {
        if (is_move_type(a->type)) {
            if (auto* vr = std::get_if<lir::EVarRef>(&a->kind))
                mark_moved(vr->name);
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
    if (TypeRef(result_type).kind() == LogosType::Kind::IntLit) {
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
    StrSet param_names;
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
    StrSet seen;
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
            } else if constexpr (std::is_same_v<K, lir::ESlicePtr>) {
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
        // The corresponding Array<T> struct must be in scope (use hermes.array).
        struct ElemInfo { std::string struct_name; uint64_t type_code; };
        static const std::map<std::string, ElemInfo> known = {
            {"I8",  {"ArrayI8",  logos::hermes::type_hash::ArrayI8}},
            {"U8",  {"ArrayU8",  logos::hermes::type_hash::ArrayU8}},
            {"I16", {"ArrayI16", logos::hermes::type_hash::ArrayI16}},
            {"U16", {"ArrayU16", logos::hermes::type_hash::ArrayU16}},
            {"I32", {"ArrayI32", logos::hermes::type_hash::ArrayI32}},
            {"U32", {"ArrayU32", logos::hermes::type_hash::ArrayU32}},
            {"I64", {"ArrayI64", logos::hermes::type_hash::ArrayI64}},
            {"U64", {"ArrayU64", logos::hermes::type_hash::ArrayU64}},
            {"F32", {"ArrayF32", logos::hermes::type_hash::ArrayF32}},
            {"F64", {"ArrayF64", logos::hermes::type_hash::ArrayF64}},
        };
        auto type_name = std::string(str_of(node.get(la::TYPE.code)));
        auto kit = known.find(type_name);
        if (kit == known.end()) {
            error(std::format(
                "unknown typed array element type '{}'; supported: "
                "I8, U8, I16, U16, I32, U32, I64, U64, F32, F64", type_name));
            return nullptr;
        }
        // Accept either the original eidos name or a type-alias pointing
        // at the generic instantiation (e.g. `pub type ArrayI32 = Array<i32>;`).
        if (!datatypes_.count(kit->second.struct_name) &&
            !type_aliases_.count(kit->second.struct_name)) {
            error(std::format(
                "typed array @<{}>[...] requires '{}' in scope — add 'use std.hermes.array;'",
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
        // Supported keys: I32/U32/I64/U64 → Map*AnyVal typed maps;
        //                 Varchar        → ObjectMap (same as untyped).
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
        struct KeyInfo { const char* mangled; const char* lir; uint64_t type_code; };
        static const std::map<std::string, KeyInfo> known_keys = {
            {"I32", {"Map$G2$i32$AnyVal", "I32",
                     logos::hermes::type_hash::MapI32AnyVal}},
            {"U32", {"Map$G2$u32$AnyVal", "U32",
                     logos::hermes::type_hash::MapU32AnyVal}},
            {"I64", {"Map$G2$i64$AnyVal", "I64",
                     logos::hermes::type_hash::MapI64AnyVal}},
            {"U64", {"Map$G2$u64$AnyVal", "U64",
                     logos::hermes::type_hash::MapU64AnyVal}},
        };
        std::string lir_key_type;
        if (auto kit = known_keys.find(key_type); kit != known_keys.end()) {
            // Check if Map<K, AnyVal> is available in any form:
            // - concrete `pub eidos Map<i32, AnyVal> { ... }` → Map$G2$i32$AnyVal
            // - generic `pub eidos Map<K, AnyVal> { ... }` → Map$G2$K$AnyVal (K is TypeVar name)
            // Either form satisfies the availability requirement.
            bool map_available = struct_specs_sema_.count(kit->second.mangled) != 0
                              || struct_specs_sema_.count("Map$G2$K$AnyVal") != 0;
            if (!map_available) {
                error(std::format(
                    "typed map @<{}>{{...}} requires 'use std.hermes.map;'",
                    key_type));
                return nullptr;
            }
            lir_key_type = kit->second.lir;
        } else if (key_type == "Varchar") {
            lir_key_type = "";  // same as untyped ObjectMap
        } else {
            error(std::format(
                "@<{}> — unsupported key type '{}'; "
                "supported: I32, U32, I64, U64, Varchar",
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
                    if (!lir_key_type.empty()) {
                        error(std::format(
                            "@<{}> map entry [{}] has string key '{}'; integer maps require integer keys",
                            lir_key_type, i, ks));
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
                    if (lir_key_type == "U32" &&
                        (kv < 0 || kv > 4294967295LL)) {
                        error(std::format(
                            "@<U32> map key [{}] value {} is out of u32 range", i, kv));
                        return nullptr;
                    }
                    if (lir_key_type == "U64" && kv < 0) {
                        error(std::format(
                            "@<U64> map key [{}] value {} is negative", i, kv));
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
            switch (TypeRef(t).kind()) {
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
                    return TypeRef(t).struct_name() == "AnyVal" ||
                           TypeRef(t).struct_name() == "StringView";
                // *const u8 / *mut u8 captured as C-string varchar — C5.
                case K::Ptr:
                    return TypeRef(t).pointee() && TypeRef(t).pointee()->kind == K::U8;
                // str (&[u8] slice) captured as varchar — same as *const u8 but with length.
                case K::Slice:
                    return TypeRef(t).elem() && TypeRef(t).elem()->kind == K::U8;
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
    // Type: HermesStatic for static blobs; Hermes for captures (codegen handles both).
    auto* result_type = lit.has_captures ? make_struct_type("Hermes") : make_struct_type("HermesStatic");
    return make_expr(result_type, std::move(lit));
}

} // namespace logos::compiler
