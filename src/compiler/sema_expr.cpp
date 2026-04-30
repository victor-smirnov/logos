// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include "sema_impl.hpp"
#include "ctfe.hpp"

#include <logos/hermes/type_registry.hpp>
#include <logos/hermes/access.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/clone.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/schema_codes.hpp>

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
        TypeRef t = (suf != LogosType::Kind::Error) ? prim(suf) : intlit_t();
        return builder().lit_int(v, t);
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
        TypeRef t = (suf != LogosType::Kind::Error) ? prim(suf)
                                                              : prim(LogosType::Kind::FloatLit);
        return builder().lit_float(v, t);
    }
    case la::LIT_BOOL: {
        AnyVal av = expr.get(la::VALUE.code);
        bool v = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        return builder().lit_bool(v, bool_t());
    }
    case la::LIT_STR: {
        auto sv = str_of(expr.get(la::VALUE.code));
        return builder().lit_str(std::string(sv), make_slice_type(u8_t()));
    }

    case la::VAR_REF: {
        auto name = str_of(expr.get(la::NAME.code));
        auto t = lookup(name);
        if (!t) {
            // Check if it's a function name — allow coercion to fn(T)->R type.
            auto cands = find_func_candidates(name);
            if (cands.size() == 1) {
                const SemaFuncInfo& fi = *cands[0];
                LogosTypeBuilder ft;
                ft.kind = LogosType::Kind::FnPtr;
                for (auto pt : fi.param_types)
                    ft.closure_params.push_back(pt);
                ft.closure_ret = fi.ret_type ? fi.ret_type : void_t();
                auto fn_type = pool_->alloc(std::move(ft));
                return builder().var_ref(fi.symbol_name.empty() ? std::string(name) : fi.symbol_name, fn_type);
            }
            error(std::format("undefined variable '{}'", name));
            return error_expr();
        }
        if (moved_vars_.count(std::string(name)))
            error(std::format("use of moved variable '{}'", name));
        return builder().var_ref(std::string(name), t);
    }

    case la::PACK_EXPAND: {
        auto name = str_of(expr.get(la::NAME.code));
        // Type is the variadic TypeVar — mono will expand this
        auto t = lookup(name);
        if (!t) {
            error(std::format("pack expand: undefined variable '{}'", name));
            return error_expr();
        }
        return builder().pack_expand(std::string(name), t);
    }

    case la::SIZEOF_PACK: {
        auto op = std::string(str_of(expr.get(la::OP.code)));
        if (op != "sizeof") {
            error(std::format("expected 'sizeof...(T)', got '{}...(T)'", op));
            return error_expr();
        }
        auto name = std::string(str_of(expr.get(la::NAME.code)));
        auto it = current_type_params_.find(name);
        if (it == current_type_params_.end()) {
            error(std::format("sizeof...({}): undefined type parameter", name));
            return error_expr();
        }
        std::vector<TypeRef> tas;
        tas.push_back(make_typevar(name));
        return builder().call("__sizeof_pack__", std::move(tas), {},
                              prim(LogosType::Kind::U64));
    }

    case la::PAREN_EXPR:
        if (expr.has_key(la::VALUE))
            return lower_expr(map_of(expr.get(la::VALUE.code)));
        return error_expr();

    case la::CAST: {
        lir::LExprPtr inner = expr.has_key(la::VALUE)
            ? lower_expr(map_of(expr.get(la::VALUE.code)))
            : error_expr();
        TypeRef target = expr.has_key(la::TYPE)
            ? resolve_type(map_of(expr.get(la::TYPE.code)))
            : error_t();

        // ── Hermes typed container casts: &[T] as <I32>[] → Hermes. ──────
        if (target && TypeRef(target).kind() == LogosType::Kind::Struct &&
            (TypeRef(target).struct_name() == "HermesArr" || TypeRef(target).struct_name() == "HermesMap")) {
            if (TypeRef(target).struct_name() == "HermesArr") {
                auto src = inner->type;
                if (!src || TypeRef(src).kind() != LogosType::Kind::Slice) {
                    error(std::format(
                        "'as <T>[]' requires a &[T] slice as source; got '{}'",
                        src ? type_str(src) : "?"));
                    return error_expr();
                }
                // Validate element type compatibility.
                // C6-fix3: elem_t must be non-null (resolve_type always sets it for valid types).
                TypeRef elem_t = !TypeRef(target).type_args().empty()
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
                if (TypeRef(elem_t).kind() != TypeRef(src).elem().kind()) {
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
                auto ctr_t = lookup_type_by_name("Hermes");
                if (!ctr_t) {
                    LogosTypeBuilder t{};
                    t.kind = LogosType::Kind::Struct;
                    t.struct_name = "Hermes";
                    ctr_t = pool_->alloc(std::move(t));
                }
                return builder().hermes_cast(std::move(inner), std::move(build_fn), ctr_t);
            }
            // fix5: explicit guard — outer if allows HermesArr||HermesMap; must be HermesMap here.
            if (TypeRef(target).struct_name() != "HermesMap") {
                error("internal: unexpected hermes container type in map cast path");
                return error_expr();
            }
            // HermesMap: source must be MapSliceI32 for <I32,AnyVal>{}.
            {
                auto src = inner->type;
                TypeRef key_t = !TypeRef(target).type_args().empty()
                    ? TypeRef(target).type_args()[0] : nullptr;
                TypeRef val_t = TypeRef(target).type_args().size() > 1
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
                        if (TypeRef(key_t).kind() == mv.key_kind) {
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
                auto ctr_t = lookup_type_by_name("Hermes");
                if (!ctr_t) {
                    LogosTypeBuilder t{};
                    t.kind = LogosType::Kind::Struct;
                    t.struct_name = "Hermes";
                    ctr_t = pool_->alloc(std::move(t));
                }
                return builder().hermes_cast(std::move(inner), std::move(map_fn), ctr_t);
            }
        }

        // ── Ordinary numeric/pointer cast. ────────────────────────────────────
        if (inner->type && target &&
            TypeRef(inner->type).kind() != LogosType::Kind::Error &&
            TypeRef(target).kind() != LogosType::Kind::Error) {
            bool src_agg = TypeRef(inner->type).kind() == LogosType::Kind::Struct ||
                           TypeRef(inner->type).kind() == LogosType::Kind::Array  ||
                           TypeRef(inner->type).kind() == LogosType::Kind::Tuple  ||
                           TypeRef(inner->type).kind() == LogosType::Kind::Enum;
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
            if (TypeRef innt(inner->type); innt.kind() == LogosType::Kind::Enum) {
                auto en = innt.enum_name();
                auto [epkg_cast, esi_cast] = find_enum_by_name(en);
                auto eit = esi_cast ? enums_.find(sema_key(epkg_cast, en)) : enums_.end();
                if (eit == enums_.end()) eit = enums_.find(en);
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
            TypeRef innt2(inner->type);
            bool src_is_str = innt2.kind() == LogosType::Kind::Slice &&
                              innt2.elem() &&
                              innt2.elem().kind() == LogosType::Kind::U8;
            bool tgt_is_mut_ptr = TypeRef(target).kind() == LogosType::Kind::Ptr &&
                                  TypeRef(target).mut_ptr() &&
                                  TypeRef(target).pointee() &&
                                  TypeRef(target).pointee().kind() == LogosType::Kind::U8;
            if (src_is_str && tgt_is_mut_ptr)
                error("cannot cast 'str' to '*mut u8': str data is read-only; use '*const u8'");
        }
        return builder().cast(std::move(inner), target);
    }

    case la::BINOP:       return lower_binop(expr);
    case la::UNARY:       return lower_unary(expr);
    case la::DEREF:       return lower_deref(expr);

    case la::ADDR_OF_MUT: {
        // &mut var — exclusive mutable reference
        auto child = map_of(expr.get(la::VALUE.code));
        if (code_of(child) == la::VAR_REF) {
            auto var_name = str_of(child.get(la::NAME.code));
            auto vt = lookup(var_name);
            if (!vt) {
                error(std::format("'&mut': undefined variable '{}'", var_name));
                return error_expr();
            }
            // For arrays, produce &mut elem (reference to first element)
            if (TypeRef(vt).kind() == LogosType::Kind::Array)
                return builder().addr_of(std::string(var_name), make_ref(true, TypeRef(vt).elem()));
            return builder().addr_of(std::string(var_name), make_ref(true, vt));
        }
        // &mut <expr> — temporary materialization
        auto inner = lower_expr(child);
        if (TypeRef(inner->type).kind() == LogosType::Kind::Error) return error_expr();
        auto __ty_inner = make_ref(true, inner->type);

        return builder().addr_of_temp(std::move(inner), true, __ty_inner);
    }
    case la::TRY_EXPR: {
        // expr? — two flavours:
        //   Result<T, E>  →  extract Ok(v), early-return Err(e) in a fn : Result<?, E>
        //   Option<T>     →  extract Some(v), early-return None in a fn : Option<?>
        auto inner = expr.has_key(la::VALUE)
            ? lower_expr(map_of(expr.get(la::VALUE.code)))
            : error_expr();
        auto inner_t = inner->type;
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
        auto ok_type = TypeRef(inner_t).type_args()[0];  // T
        return builder().try_expr(std::move(inner), ok_disc, err_disc, ok_type);
    }

    case la::CALL:         return lower_call(expr);
    case la::GENERIC_CALL: return lower_generic_call(expr);
    case la::METHOD_CALL:  return lower_method_call(expr);
    case la::STATIC_CALL:  return lower_static_call(expr);
    case la::METACALL:     return lower_metacall(expr);
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
    case la::HERMES_BLOB:  return lower_hermes_blob(expr);
    case la::QUOTE_ITEM:   return lower_quote_item(expr);
    case la::QUOTE_EXPR:   return lower_quote_expr(expr);
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
        TypeRef result_type = nullptr;
        std::string break_slot;
        {
            auto sr = stmt_ref_of(loop_stmt);
            if (sr.kind() == lir_schema::stmt::Code::Loop) {
                lir_view::SLoopView v{sr};
                result_type = v.result_type(cur_prog_->type_pool.impl());
                break_slot  = std::string(v.break_slot());
            }
        }
        if (!result_type || break_slot.empty()) {
            // loop never yields — treat as void (infinite loop used as stmt-expr)
            auto block = lir::alloc_block(*cur_prog_);
            block->stmts.push_back(std::move(loop_stmt));
            return builder().block_expr(std::move(block), nullptr, void_t());
        }
        // Wrap: { loop { ... }; __loop_val }
        // gen_loop allocates the break slot alloca and registers it in scope_;
        // we just read it back via EVarRef after the loop exits.
        auto block = lir::alloc_block(*cur_prog_);
        block->stmts.push_back(std::move(loop_stmt));
        auto slot_ref = builder().var_ref(break_slot, result_type);
        return builder().block_expr(std::move(block), std::move(slot_ref), result_type);
    }

    case la::UNSAFE_BLOCK: {
        if (!expr.has_key(la::BODY)) return error_expr();
        auto inner = map_of(expr.get(la::BODY.code));
        bool was = inside_unsafe_;
        inside_unsafe_ = true;
        lir::LExprPtr result = nullptr;
        auto block = lir::alloc_block(*cur_prog_);
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
        if (!result) return builder().block_expr(std::move(block), nullptr, void_t());
        TypeRef rt = result->type;
        return builder().block_expr(std::move(block), std::move(result), rt);
    }

    case la::TUPLE_LIT: {
        if (!expr.has_key(la::ITEMS))
            return builder().tuple_lit({}, void_t());  // () — unit value
        auto items = arr_of(expr.get(la::ITEMS.code));
        std::vector<lir::LExprPtr> elems;
        std::vector<TypeRef> elem_types;
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto e = lower_expr(map_of(items.get(i)));
            // Upgrade IntLit element type to i64 if the literal overflows i32.
            TypeRef et = e->type;
            if (TypeRef(et).kind() == LogosType::Kind::IntLit) {
                if (auto v = get_intlit_value(e))
                    if (*v > (int64_t)INT32_MAX || *v < (int64_t)INT32_MIN)
                        et = prim(LogosType::Kind::I64);
            }
            elem_types.push_back(et);
            elems.push_back(std::move(e));
        }
        auto tt = make_tuple_type(std::move(elem_types));
        return builder().tuple_lit(std::move(elems), tt);
    }

    case la::TUPLE_INDEX: {
        auto recv = expr.has_key(la::RECEIVER)
            ? lower_expr(map_of(expr.get(la::RECEIVER.code)))
            : error_expr();
        // Auto-deref: &(T) and &mut (T) -> use pointee type for index lookup
        TypeRef recv_tuple_type = recv->type;
        TypeRef rrt(recv->type);
        if (rrt && is_ref_like(rrt.kind()) && rrt.pointee() &&
            rrt.pointee().kind() == LogosType::Kind::Tuple) {
            recv_tuple_type = rrt.pointee();
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
        auto elem_t = TypeRef(recv_tuple_type).tuple_elems()[idx];
        return builder().tuple_index(std::move(recv), idx, elem_t);
    }

    default:
        return error_expr();
    }
}

lir::LExprPtr SemaChecker::lower_binop(TinyMapView node) {
    auto op  = str_of(node.get(la::OP.code));
    auto lhs = lower_expr(map_of(node.get(la::LHS.code)));
    auto rhs = lower_expr(map_of(node.get(la::RHS.code)));
    auto lt = lhs->type;
    auto rt = rhs->type;

    TypeRef result_type = error_t();

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
                return builder().call(fit->symbol_name.empty() ? mangled : fit->symbol_name, {}, std::move(args), fit->ret_type);
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
            const lir::LExpr* lit_expr = (TypeRef(lt).kind() == LogosType::Kind::IntLit) ? lhs : rhs;
            if (auto v = get_intlit_value(lit_expr)) {
                if (*v != 0)
                    error(std::format(
                        "operator '{}': pointer can only be compared with integer literal 0", op));
            }
        }
        // Detect comparisons against IntLit values that can't fit in the other operand.
        // E.g. x: i32 == 10000000000 — the literal can never equal any i32 value.
        if (TypeRef(lt).kind() == LogosType::Kind::IntLit && is_integer_kind(TypeRef(rt).kind())) {
            if (auto v = get_intlit_value(lhs))
                if (!intlit_fits(*v, TypeRef(rt).kind()))
                    error(std::format("operator '{}': literal value {} does not fit in {}",
                          op, *v, type_str(rt)));
        } else if (TypeRef(rt).kind() == LogosType::Kind::IntLit && is_integer_kind(TypeRef(lt).kind())) {
            if (auto v = get_intlit_value(rhs))
                if (!intlit_fits(*v, TypeRef(lt).kind()))
                    error(std::format("operator '{}': literal value {} does not fit in {}",
                          op, *v, type_str(lt)));
        }
        result_type = bool_t();
    } else if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
        if (!is_numeric(lt))
            error(std::format("operator '{}': left must be numeric, got {}", op, type_str(lt)));
        if (!is_numeric(rt))
            error(std::format("operator '{}': right must be numeric, got {}", op, type_str(rt)));
        bool both_int = is_integer_kind(TypeRef(lt).kind()) && is_integer_kind(TypeRef(rt).kind());
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
                if (auto v = get_intlit_value(lhs))
                    if (!intlit_fits(*v, TypeRef(rt).kind()))
                        error(std::format("operator '{}': left value {} does not fit in {}",
                              op, *v, type_str(rt)));
            if (TypeRef(rt).kind() == LogosType::Kind::IntLit && TypeRef(lt).kind() != LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(rhs))
                    if (!intlit_fits(*v, TypeRef(lt).kind()))
                        error(std::format("operator '{}': right value {} does not fit in {}",
                              op, *v, type_str(lt)));
        }
    } else if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
        // Bitwise and shift operators — require integer operands.
        if (!is_integer_kind(TypeRef(lt).kind()) && TypeRef(lt).kind() != LogosType::Kind::IntLit)
            error(std::format("operator '{}': left must be integer, got {}", op, type_str(lt)));
        if (!is_integer_kind(TypeRef(rt).kind()) && TypeRef(rt).kind() != LogosType::Kind::IntLit)
            error(std::format("operator '{}': right must be integer, got {}", op, type_str(rt)));
        result_type = unify_int(lt, rt);
        // Check IntLit operand fits in the concrete type of the other operand.
        if (TypeRef(lt).kind() == LogosType::Kind::IntLit && TypeRef(rt).kind() != LogosType::Kind::IntLit)
            if (auto v = get_intlit_value(lhs))
                if (!intlit_fits(*v, TypeRef(rt).kind()))
                    error(std::format("operator '{}': left value {} does not fit in {}",
                          op, *v, type_str(rt)));
        if (TypeRef(rt).kind() == LogosType::Kind::IntLit && TypeRef(lt).kind() != LogosType::Kind::IntLit)
            if (auto v = get_intlit_value(rhs))
                if (!intlit_fits(*v, TypeRef(lt).kind()))
                    error(std::format("operator '{}': right value {} does not fit in {}",
                          op, *v, type_str(lt)));
    } else {
        error(std::format("unknown binary operator '{}'", op));
    }

    return builder().bin_op(std::string(op), std::move(lhs), std::move(rhs), result_type);
}

lir::LExprPtr SemaChecker::lower_unary(TinyMapView node) {
    auto op  = str_of(node.get(la::OP.code));

    // & — address-of or array-to-slice
    if (op == "&") {
        auto child = map_of(node.get(la::VALUE.code));
        if (code_of(child) == la::VAR_REF) {
            auto var_name = str_of(child.get(la::NAME.code));
            auto vt = lookup(var_name);
            if (!vt) {
                error(std::format("'&': undefined variable '{}'", var_name));
                return error_expr();
            }
            // &array → slice: &[T] with len = array size
            if (TypeRef(vt).kind() == LogosType::Kind::Array) {
                auto addr = builder().addr_of(std::string(var_name), make_ref(false, TypeRef(vt).elem()));
                auto len  = builder().lit_int((int64_t)TypeRef(vt).arr_size(), prim(LogosType::Kind::I64));
                return builder().slice_lit(std::move(addr), std::move(len), make_slice_type(TypeRef(vt).elem()));
            }
            return builder().addr_of(std::string(var_name), make_ref(false, vt));
        }
        // &<expr> — temporary materialization: spill rvalue to stack
        auto inner = lower_expr(child);
        if (TypeRef(inner->type).kind() == LogosType::Kind::Error) return error_expr();
        auto __ty_inner = make_ref(false, inner->type);

        return builder().addr_of_temp(std::move(inner), false, __ty_inner);
    }

    auto operand = lower_expr(map_of(node.get(la::VALUE.code)));
    auto vt = operand->type;
    if (TypeRef(vt).kind() == LogosType::Kind::Error)
        return builder().unary(std::string(op), std::move(operand), error_t());

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
                return builder().call(fit->symbol_name.empty() ? mangled : fit->symbol_name, {}, std::move(args), fit->ret_type);
            }
        }
    }

    TypeRef result_type = error_t();
    if (op == "-") {
        if (!is_numeric(vt))
            error(std::format("unary '-': operand must be numeric, got {}", type_str(vt)));
        result_type = vt;
    } else if (op == "!") {
        if (TypeRef(vt).kind() == LogosType::Kind::Bool) {
            result_type = bool_t();
        } else if (is_integer_kind(TypeRef(vt).kind()) || TypeRef(vt).kind() == LogosType::Kind::IntLit) {
            // Bitwise NOT (~x) on integer types
            result_type = (TypeRef(vt).kind() == LogosType::Kind::IntLit) ? i32_t() : vt;
        } else {
            error(std::format("unary '!': operand must be bool or integer, got {}", type_str(vt)));
            result_type = bool_t();
        }
    } else {
        error(std::format("unknown unary operator '{}'", op));
    }

    return builder().unary(std::string(op), std::move(operand), result_type);
}

lir::LExprPtr SemaChecker::lower_deref(TinyMapView node) {
    auto operand = lower_expr(map_of(node.get(la::VALUE.code)));
    auto vt = operand->type;
    if (TypeRef(vt).kind() == LogosType::Kind::Error)
        return builder().deref(std::move(operand), error_t());
    if (TypeRef(vt).kind() != LogosType::Kind::Ptr &&
        TypeRef(vt).kind() != LogosType::Kind::Ref &&
        TypeRef(vt).kind() != LogosType::Kind::MutRef) {
        error(std::format("dereference of non-pointer type {}", type_str(vt)));
        return builder().deref(std::move(operand), error_t());
    }
    // Raw pointer deref requires unsafe context
    if (TypeRef(vt).kind() == LogosType::Kind::Ptr && !inside_unsafe_)
        error("dereference of raw pointer requires unsafe context");
    auto res = TypeRef(vt).pointee() ? TypeRef(vt).pointee() : error_t();
    return builder().deref(std::move(operand), res);
}

lir::LExprPtr SemaChecker::lower_call(TinyMapView node) {
    auto callee = str_of(node.get(la::CALLEE.code));

    // Check if callee is a closure or fn-ptr variable
    auto callee_type = lookup(callee);
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
                widen_int_expr(arg_exprs[i], TypeRef(callee_type).closure_params()[i], builder());
                auto at = arg_exprs[i]->type;
                auto pt = TypeRef(callee_type).closure_params()[i];
                if (TypeRef(at).kind() != LogosType::Kind::Error &&
                    TypeRef(pt).kind() != LogosType::Kind::Error &&
                    !types_compatible(at, pt)) {
                    error(std::format("{} arg {}: expected {}, got {}",
                          kind_str, i + 1, type_str(pt), type_str(at)));
                }
            }
        }
        auto callee_expr = builder().var_ref(std::string(callee), callee_type);
        TypeRef ret = TypeRef(callee_type).closure_ret() ? TypeRef(callee_type).closure_ret() : void_t();
        if (is_fn_ptr)
            return builder().fn_ptr_call(std::move(callee_expr), std::move(arg_exprs), ret);
        return builder().closure_call(std::move(callee_expr), std::move(arg_exprs), ret);
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
        auto str_t = make_slice_type(u8_t());
        lir::ECall ec;
        ec.callee = "str_from_raw";
        for (auto& a : arg_exprs) ec.args.push_back(std::move(a));
        return builder().call_v(std::move(ec), str_t);
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
        TypeRef ret = (callee == "bswap_u64" || callee == "bitreverse_u64")
                               ? prim(LogosType::Kind::U64)
                               : prim(LogosType::Kind::U32);
        lir::ECall ec;
        ec.callee = callee;
        for (auto& a : arg_exprs) ec.args.push_back(std::move(a));
        return builder().call_v(std::move(ec), ret);
    }

    bool call_has_pack_expand = false;
    for (auto& a : arg_exprs) {
        if (expr_ref_of(*a).kind() == lir_schema::expr::Code::PackExpand) {
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
                    widen_int_expr(arg_exprs[i], exact_fi->param_types[i], builder());
                    auto at = arg_exprs[i]->type;
                    auto pt = exact_fi->param_types[i];
                    if (TypeRef(at).kind() != LogosType::Kind::Error &&
                        TypeRef(pt).kind() != LogosType::Kind::Error &&
                        !types_compatible(at, pt))
                        error(std::format("call to '{}' arg {}: expected {}, got {}",
                              callee, i + 1, type_str(pt), type_str(at)));
                    if (TypeRef(at).kind() == LogosType::Kind::IntLit && TypeRef(pt).kind() != LogosType::Kind::Error)
                        if (auto v = get_intlit_value(arg_exprs[i]))
                            if (!intlit_fits(*v, TypeRef(pt).kind()))
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
                widen_int_expr(arg_exprs[i], exact_fi->param_types[i], builder());
                auto at = arg_exprs[i]->type;
                auto pt = exact_fi->param_types[i];
                if (TypeRef(at).kind() != LogosType::Kind::Error &&
                    TypeRef(pt).kind() != LogosType::Kind::Error &&
                    !types_compatible(at, pt))
                    error(std::format("call to '{}' arg {}: expected {}, got {}",
                          callee, i + 1, type_str(pt), type_str(at)));
                if (TypeRef(at).kind() == LogosType::Kind::IntLit && TypeRef(pt).kind() != LogosType::Kind::Error)
                    if (auto v = get_intlit_value(arg_exprs[i]))
                        if (!intlit_fits(*v, TypeRef(pt).kind()))
                            error(std::format("call to '{}' arg {}: value {} does not fit in {}",
                                  callee, i + 1, *v, type_str(pt)));
                // Check array literal elements against narrow array param type.
                if (TypeRef(at).kind() == LogosType::Kind::Array && TypeRef(pt).kind() == LogosType::Kind::Array && TypeRef(pt).elem()) {
                    auto vr = expr_ref_of(*arg_exprs[i]);
                    if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                        lir_view::EArrLitView al{vr};
                        for (uint64_t ei = 0; ei < al.count(); ++ei) {
                            auto el = al.elem(ei);
                            if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(el))
                                    if (!intlit_fits(*v, TypeRef(pt).elem().kind()))
                                        error(std::format("call to '{}' arg {}: array element {}: value {} does not fit in {}",
                                              callee, i + 1, ei, *v, type_str(TypeRef(pt).elem())));
                        }
                    }
                }
                // Check tuple literal elements against narrow tuple param element types.
                if (TypeRef(at).kind() == LogosType::Kind::Tuple && TypeRef(pt).kind() == LogosType::Kind::Tuple) {
                    auto vr = expr_ref_of(*arg_exprs[i]);
                    if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                        lir_view::ETupleLitView tl{vr};
                        uint64_t ei = 0;
                        tl.each_elem([&](lir_view::ExprRef el) {
                            if (ei >= TypeRef(pt).tuple_elems().size()) { ++ei; return; }
                            if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(el))
                                    if (TypeRef(pt).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(TypeRef(pt).tuple_elems()[ei]).kind()))
                                        error(std::format("call to '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                              callee, i + 1, ei, *v, type_str(TypeRef(pt).tuple_elems()[ei])));
                            if (TypeRef(pt).tuple_elems()[ei] && TypeRef(TypeRef(pt).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                                TypeRef(TypeRef(pt).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                                el.kind() == lir_schema::expr::Code::ArrLit) {
                                lir_view::EArrLitView ial{el};
                                for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                    auto iel = ial.elem(ii);
                                    if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(iel))
                                            if (!intlit_fits(*v, TypeRef(TypeRef(pt).tuple_elems()[ei]).elem().kind()))
                                                error(std::format("call to '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                      callee, i + 1, ei, ii, *v, type_str(TypeRef(TypeRef(pt).tuple_elems()[ei]).elem())));
                                }
                            }
                            if (TypeRef(pt).tuple_elems()[ei] && TypeRef(TypeRef(pt).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                                el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                                el.kind() == lir_schema::expr::Code::TupleLit) {
                                lir_view::ETupleLitView itl{el};
                                uint64_t ii = 0;
                                itl.each_elem([&](lir_view::ExprRef iel) {
                                    if (ii >= TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                    if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(iel))
                                            if (TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                                error(std::format("call to '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      callee, i + 1, ei, ii, *v, type_str(TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii])));
                                    ++ii;
                                });
                            }
                            ++ei;
                        });
                    }
                }
            }
        }

        for (auto& a : arg_exprs) {
            if (is_move_type(a->type)) {
                auto er = expr_ref_of(*a);
                if (er.kind() == lir_schema::expr::Code::VarRef)
                    mark_moved(std::string(lir_view::EVarRefView{er}.name()));
            }
        }
        return builder().call(exact_fi->symbol_name.empty() ? std::string(callee) : exact_fi->symbol_name, {}, std::move(arg_exprs), exact_fi->ret_type);
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
            return builder().call(std::string(callee), {}, std::move(arg_exprs), error_t());
        }
        error(std::format("call to undefined function '{}'", callee));
        return builder().call(std::string(callee), {}, std::move(arg_exprs), error_t());
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
            if (expr_ref_of(*a).kind() == lir_schema::expr::Code::PackExpand) {
                in_generic_context = true; break;
            }
            auto t = a->type;
            if (t && (TypeRef(t).kind() == LogosType::Kind::TypeVar ||
                      TypeRef(t).kind() == LogosType::Kind::AssocType)) {
                in_generic_context = true; break;
            }
        }
        if (!in_generic_context) {
            std::vector<TypeRef> inferred;
            if (infer_type_args(*infer_fi, arg_exprs, inferred))
                return finish_generic_call(
                    infer_fi->symbol_name.empty() ? callee : infer_fi->symbol_name,
                    *infer_fi, std::move(inferred), std::move(arg_exprs));
            error(std::format("call to '{}': could not infer all type arguments — use explicit f::<T>(...) syntax", callee));
            return builder().call(std::string(callee), {}, std::move(arg_exprs), error_t());
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
        if (expr_ref_of(*a).kind() == lir_schema::expr::Code::PackExpand)
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
                widen_int_expr(arg_exprs[i], fi.param_types[i], builder());
                auto at = arg_exprs[i]->type;
                auto pt = fi.param_types[i];
                if (TypeRef(at).kind() != LogosType::Kind::Error &&
                    TypeRef(pt).kind() != LogosType::Kind::Error &&
                    !types_compatible(at, pt))
                    error(std::format("call to '{}' arg {}: expected {}, got {}",
                          callee, i + 1, type_str(pt), type_str(at)));
                if (TypeRef(at).kind() == LogosType::Kind::IntLit && TypeRef(pt).kind() != LogosType::Kind::Error)
                    if (auto v = get_intlit_value(arg_exprs[i]))
                        if (!intlit_fits(*v, TypeRef(pt).kind()))
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
            widen_int_expr(arg_exprs[i], fi.param_types[i], builder());
            auto at = arg_exprs[i]->type;
            auto pt = fi.param_types[i];
            if (TypeRef(at).kind() != LogosType::Kind::Error &&
                TypeRef(pt).kind() != LogosType::Kind::Error &&
                !types_compatible(at, pt))
                error(std::format("call to '{}' arg {}: expected {}, got {}",
                      callee, i + 1, type_str(pt), type_str(at)));
            if (TypeRef(at).kind() == LogosType::Kind::IntLit && TypeRef(pt).kind() != LogosType::Kind::Error)
                if (auto v = get_intlit_value(arg_exprs[i]))
                    if (!intlit_fits(*v, TypeRef(pt).kind()))
                        error(std::format("call to '{}' arg {}: value {} does not fit in {}",
                              callee, i + 1, *v, type_str(pt)));
            // Check array literal elements against narrow array param type.
            if (TypeRef(at).kind() == LogosType::Kind::Array && TypeRef(pt).kind() == LogosType::Kind::Array && TypeRef(pt).elem()) {
                auto vr = expr_ref_of(*arg_exprs[i]);
                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView al{vr};
                    for (uint64_t ei = 0; ei < al.count(); ++ei) {
                        auto el = al.elem(ei);
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (!intlit_fits(*v, TypeRef(pt).elem().kind()))
                                    error(std::format("call to '{}' arg {}: array element {}: value {} does not fit in {}",
                                          callee, i + 1, ei, *v, type_str(TypeRef(pt).elem())));
                    }
                }
            }
            // Check tuple literal elements against narrow tuple param element types.
            if (TypeRef(at).kind() == LogosType::Kind::Tuple && TypeRef(pt).kind() == LogosType::Kind::Tuple) {
                auto vr = expr_ref_of(*arg_exprs[i]);
                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView tl{vr};
                    uint64_t ei = 0;
                    tl.each_elem([&](lir_view::ExprRef el) {
                        if (ei >= TypeRef(pt).tuple_elems().size()) { ++ei; return; }
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (TypeRef(pt).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(TypeRef(pt).tuple_elems()[ei]).kind()))
                                    error(std::format("call to '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                          callee, i + 1, ei, *v, type_str(TypeRef(pt).tuple_elems()[ei])));
                        if (TypeRef(pt).tuple_elems()[ei] && TypeRef(TypeRef(pt).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(pt).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                            el.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{el};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(pt).tuple_elems()[ei]).elem().kind()))
                                            error(std::format("call to '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                  callee, i + 1, ei, ii, *v, type_str(TypeRef(TypeRef(pt).tuple_elems()[ei]).elem())));
                            }
                        }
                        if (TypeRef(pt).tuple_elems()[ei] && TypeRef(TypeRef(pt).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                            el.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{el};
                            uint64_t ii = 0;
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii >= TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                            error(std::format("call to '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  callee, i + 1, ei, ii, *v, type_str(TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii])));
                                ++ii;
                            });
                        }
                        ++ei;
                    });
                }
            }
        }
    }

    // Move semantics: mark by-value move-type args as moved
    for (auto& a : arg_exprs) {
        if (is_move_type(a->type)) {
            auto er = expr_ref_of(*a);
            if (er.kind() == lir_schema::expr::Code::VarRef)
                mark_moved(std::string(lir_view::EVarRefView{er}.name()));
        }
    }

    // Inside generic context (inference deferred): preserve generic call shape
    // so mono can instantiate and rewrite callee names correctly.
    if (!fi.type_params.empty()) {
        bool has_variadic_tp = !fi.type_params.empty() && fi.type_params.back().is_variadic;
        if (has_variadic_tp && has_pack_expand) {
            return builder().call(fi.symbol_name.empty() ? std::string(callee) : fi.symbol_name, {}, std::move(arg_exprs), fi.ret_type);
        }
        std::vector<TypeRef> type_var_args;
        for (auto& tp : fi.type_params)
            type_var_args.push_back(make_typevar(tp.name));
        SemaSubst subst;
        for (size_t i = 0; i < fi.type_params.size() && i < type_var_args.size(); ++i)
            subst[fi.type_params[i].name] = type_var_args[i];
        TypeRef ret = subst_type_sema(fi.ret_type, subst);
        return builder().call(fi.symbol_name.empty() ? std::string(callee) : fi.symbol_name, std::move(type_var_args), std::move(arg_exprs), ret);
    }

    return builder().call(std::string(callee), {}, std::move(arg_exprs), fi.ret_type);
}

void SemaChecker::unify_types(TypeRef formal, TypeRef actual,
                     StrMap<TypeRef>& bindings) {
    if (!formal || !actual) return;
    if (actual.kind() == LogosType::Kind::Error ||
        formal.kind() == LogosType::Kind::Error) return;

    // Widen IntLit to i32 / FloatLit to f64 before any binding
    TypeRef actual_norm = actual;
    if (actual.kind() == LogosType::Kind::IntLit)
        actual_norm = TypeRef(prim(LogosType::Kind::I32));
    else if (actual.kind() == LogosType::Kind::FloatLit)
        actual_norm = TypeRef(prim(LogosType::Kind::F64));

    if (formal.kind() == LogosType::Kind::TypeVar) {
        if (formal.type_var_name() == "Self") return;  // skip implicit Self
        if (!bindings.count(formal.type_var_name()))
            bindings[std::string(formal.type_var_name())] = actual_norm;
        return;
    }

    switch (formal.kind()) {
    case LogosType::Kind::Ptr:
        if (actual_norm.kind() == LogosType::Kind::Ptr)
            unify_types(formal.pointee(), actual_norm.pointee(), bindings);
        else if (actual_norm.kind() == LogosType::Kind::Ref ||
                 actual_norm.kind() == LogosType::Kind::MutRef)
            unify_types(formal.pointee(), actual_norm.pointee(), bindings);
        break;
    case LogosType::Kind::Ref:
    case LogosType::Kind::MutRef:
        if (actual_norm.kind() == LogosType::Kind::Ref ||
            actual_norm.kind() == LogosType::Kind::MutRef ||
            actual_norm.kind() == LogosType::Kind::Ptr)
            unify_types(formal.pointee(), actual_norm.pointee(), bindings);
        break;
    case LogosType::Kind::Array:
        if (actual_norm.kind() == LogosType::Kind::Array)
            unify_types(formal.elem(), actual_norm.elem(), bindings);
        break;
    case LogosType::Kind::Slice:
        if (actual_norm.kind() == LogosType::Kind::Slice)
            unify_types(formal.elem(), actual_norm.elem(), bindings);
        break;
    case LogosType::Kind::Struct:
        if (actual_norm.kind() == LogosType::Kind::Struct &&
            formal.struct_name() == actual_norm.struct_name()) {
            auto fa = formal.type_args();
            auto aa = actual_norm.type_args();
            for (size_t i = 0; i < fa.size() && i < aa.size(); ++i)
                unify_types(fa[i], aa[i], bindings);
        }
        break;
    case LogosType::Kind::Tuple:
        if (actual_norm.kind() == LogosType::Kind::Tuple) {
            auto fe = formal.tuple_elems();
            auto ae = actual_norm.tuple_elems();
            for (size_t i = 0; i < fe.size() && i < ae.size(); ++i)
                unify_types(fe[i], ae[i], bindings);
        }
        break;
    default:
        break;  // concrete type — nothing to bind
    }
}

bool SemaChecker::infer_type_args(const SemaFuncInfo& fi,
                         const std::vector<lir::LExprPtr>& arg_exprs,
                         std::vector<TypeRef>& out_type_args,
                         const SemaSubst& context,
                         size_t param_offset) {
    StrMap<TypeRef> bindings(context.begin(), context.end());
    bool has_variadic = !fi.type_params.empty() && fi.type_params.back().is_variadic;
    size_t non_variadic_count = fi.type_params.size() - (has_variadic ? 1 : 0);
    size_t fixed_params = fi.param_types.size() >= param_offset
        ? fi.param_types.size() - param_offset - (has_variadic ? 1 : 0)
        : 0;

    // Unify fixed params against arg types
    for (size_t i = 0; i < fixed_params && i < arg_exprs.size(); ++i) {
        auto pt = fi.param_types[param_offset + i];
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
            auto t = arg_exprs[i]->type;
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
                                      std::vector<TypeRef> type_args,
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
    StrMap<TypeRef> subst;
    for (size_t i = 0; i < non_variadic_count && i < type_args.size(); ++i)
        subst[fi.type_params[i].name] = type_args[i];

    // Validate trait bounds for all type params (including variadic pack elements)
    check_type_bounds(callee_diag, fi.type_params, type_args);

    // Substitute return type
    TypeRef ret = subst_type_sema(fi.ret_type, subst);

    // Validate value argument count and types
    uint64_t n_args = arg_exprs.size();
    bool has_pack_expand = false;
    for (auto& a : arg_exprs)
        if (expr_ref_of(*a).kind() == lir_schema::expr::Code::PackExpand) {
            has_pack_expand = true; break;
        }

    if (has_pack_expand) {
        // pass — mono expands
    } else if (has_variadic) {
        // `has_variadic` means the function has a variadic *type* parameter.
        // It may or may not have a corresponding variadic *value* parameter:
        //   fn f<T...>(args: T...)   — value pack present  → fixed_params = size()-1
        //   fn f<T...>()             — type-only           → fixed_params = 0
        size_t fixed_params = fi.param_types.empty() ? 0 : fi.param_types.size() - 1;
        if (n_args < fixed_params)
            error(std::format("call to '{}': expected at least {} args, got {}",
                  callee_diag, fixed_params, n_args));
        for (uint64_t i = 0; i < fixed_params && i < n_args; ++i) {
            auto pt = subst_type_sema(fi.param_types[i], subst);
            try_coerce_closure_to_fnptr(arg_exprs[i], pt);
            widen_int_expr(arg_exprs[i], pt, builder());
            auto at = arg_exprs[i]->type;
            if (TypeRef(at).kind() != LogosType::Kind::Error &&
                TypeRef(pt).kind() != LogosType::Kind::Error &&
                TypeRef(pt).kind() != LogosType::Kind::TypeVar &&
                !types_compatible(at, pt))
                error(std::format("call to '{}' arg {}: expected {}, got {}",
                      callee_diag, i + 1, type_str(pt), type_str(at)));
            if (TypeRef(at).kind() == LogosType::Kind::IntLit && TypeRef(pt).kind() != LogosType::Kind::Error &&
                TypeRef(pt).kind() != LogosType::Kind::TypeVar)
                if (auto v = get_intlit_value(arg_exprs[i]))
                    if (!intlit_fits(*v, TypeRef(pt).kind()))
                        error(std::format("call to '{}' arg {}: value {} does not fit in {}",
                              callee_diag, i + 1, *v, type_str(pt)));
        }
    } else {
        if (n_args != fi.param_types.size()) {
            error(std::format("call to '{}': expected {} args, got {}",
                  callee_diag, fi.param_types.size(), n_args));
        } else {
            for (uint64_t i = 0; i < n_args; ++i) {
                auto pt = subst_type_sema(fi.param_types[i], subst);
                try_coerce_closure_to_fnptr(arg_exprs[i], pt);
                widen_int_expr(arg_exprs[i], pt, builder());
                auto at = arg_exprs[i]->type;
                if (TypeRef(at).kind() != LogosType::Kind::Error &&
                    TypeRef(pt).kind() != LogosType::Kind::Error &&
                    TypeRef(pt).kind() != LogosType::Kind::TypeVar &&
                    TypeRef(pt).kind() != LogosType::Kind::AssocType &&
                    !types_compatible(at, pt))
                    error(std::format("call to '{}' arg {}: expected {}, got {}",
                          callee_diag, i + 1, type_str(pt), type_str(at)));
                if (TypeRef(at).kind() == LogosType::Kind::IntLit && TypeRef(pt).kind() != LogosType::Kind::Error &&
                    TypeRef(pt).kind() != LogosType::Kind::TypeVar)
                    if (auto v = get_intlit_value(arg_exprs[i]))
                        if (!intlit_fits(*v, TypeRef(pt).kind()))
                            error(std::format("call to '{}' arg {}: value {} does not fit in {}",
                                  callee_diag, i + 1, *v, type_str(pt)));
                // Check array literal elements against narrow array param type.
                if (TypeRef(at).kind() == LogosType::Kind::Array && TypeRef(pt).kind() == LogosType::Kind::Array && TypeRef(pt).elem()) {
                    auto vr = expr_ref_of(*arg_exprs[i]);
                    if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                        lir_view::EArrLitView al{vr};
                        for (uint64_t ei = 0; ei < al.count(); ++ei) {
                            auto el = al.elem(ei);
                            if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(el))
                                    if (!intlit_fits(*v, TypeRef(pt).elem().kind()))
                                        error(std::format("call to '{}' arg {}: array element {}: value {} does not fit in {}",
                                              callee_diag, i + 1, ei, *v, type_str(TypeRef(pt).elem())));
                        }
                    }
                }
                // Check tuple literal elements against narrow tuple param element types.
                if (TypeRef(at).kind() == LogosType::Kind::Tuple && TypeRef(pt).kind() == LogosType::Kind::Tuple) {
                    auto vr = expr_ref_of(*arg_exprs[i]);
                    if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                        lir_view::ETupleLitView tl{vr};
                        uint64_t ei = 0;
                        tl.each_elem([&](lir_view::ExprRef el) {
                            if (ei >= TypeRef(pt).tuple_elems().size()) { ++ei; return; }
                            if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(el))
                                    if (TypeRef(pt).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(TypeRef(pt).tuple_elems()[ei]).kind()))
                                        error(std::format("call to '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                              callee_diag, i + 1, ei, *v, type_str(TypeRef(pt).tuple_elems()[ei])));
                            if (TypeRef(pt).tuple_elems()[ei] && TypeRef(TypeRef(pt).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                                TypeRef(TypeRef(pt).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                                el.kind() == lir_schema::expr::Code::ArrLit) {
                                lir_view::EArrLitView ial{el};
                                for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                    auto iel = ial.elem(ii);
                                    if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(iel))
                                            if (!intlit_fits(*v, TypeRef(TypeRef(pt).tuple_elems()[ei]).elem().kind()))
                                                error(std::format("call to '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                      callee_diag, i + 1, ei, ii, *v, type_str(TypeRef(TypeRef(pt).tuple_elems()[ei]).elem())));
                                }
                            }
                            if (TypeRef(pt).tuple_elems()[ei] && TypeRef(TypeRef(pt).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                                el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                                el.kind() == lir_schema::expr::Code::TupleLit) {
                                lir_view::ETupleLitView itl{el};
                                uint64_t ii = 0;
                                itl.each_elem([&](lir_view::ExprRef iel) {
                                    if (ii >= TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                    if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(iel))
                                            if (TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                                error(std::format("call to '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      callee_diag, i + 1, ei, ii, *v, type_str(TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii])));
                                    ++ii;
                                });
                            }
                            ++ei;
                        });
                    }
                }
            }
        }
    }

    return builder().call(callee, std::move(type_args), std::move(arg_exprs), ret);
}

lir::LExprPtr SemaChecker::lower_generic_call(TinyMapView node) {
    auto callee = str_of(node.get(la::CALLEE.code));

    // ── Type-trait intrinsics (C++26 type_traits style, compile-time folded) ──
    // Helper: collect resolved type args.
    auto collect_type_args = [&]() -> std::vector<TypeRef> {
        std::vector<TypeRef> out;
        if (!node.has_key(la::TYPE_PARAMS)) return out;
        auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
        if (!tplist.has_key(la::ITEMS)) return out;
        auto items = arr_of(tplist.get(la::ITEMS.code));
        for (size_t i = 0; i < items.size(); ++i)
            out.push_back(resolve_type(map_of(items.get(i))));
        return out;
    };
    auto bool_lit = [&](bool v) {
        return builder().lit_int(v ? 1LL : 0LL, prim(LogosType::Kind::Bool));
    };

    if (callee == "is_same") {
        auto ts = collect_type_args();
        if (ts.size() != 2 || !ts[0] || !ts[1]) {
            error("is_same::<T1,T2>() requires exactly two type arguments");
            return error_expr();
        }
        return bool_lit(types_equal(ts[0], ts[1]));
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
        TypeRef t = ts[0];
        using K = LogosType::Kind;
        K tk = TypeRef(t).kind();
        bool r = false;
        if      (callee == "is_ptr")       r = (tk == K::Ptr);
        else if (callee == "is_ref")       r = (tk == K::Ref);
        else if (callee == "is_mut_ref")   r = (tk == K::MutRef);
        else if (callee == "is_struct")    r = (tk == K::Struct);
        else if (callee == "is_zoned")  r = (tk == K::ZonedStruct);
        else if (callee == "is_enum")      r = (tk == K::Enum);
        else if (callee == "is_tuple")     r = (tk == K::Tuple);
        else if (callee == "is_slice")     r = (tk == K::Slice);
        else if (callee == "is_array")     r = (tk == K::Array);
        else if (callee == "is_bool")      r = (tk == K::Bool);
        else if (callee == "is_float")     r = (tk == K::F32 || tk == K::F64);
        else if (callee == "is_signed")    r = (tk == K::I8 || tk == K::I16 || tk == K::I24 ||
                                                tk == K::I32 || tk == K::I56 || tk == K::I64 ||
                                                tk == K::I128);
        else if (callee == "is_unsigned")  r = (tk == K::U8 || tk == K::U16 || tk == K::U24 ||
                                                tk == K::U32 || tk == K::U56 || tk == K::U64 ||
                                                tk == K::U128);
        else if (callee == "is_integer")   r = (tk == K::I8 || tk == K::I16 || tk == K::I24 ||
                                                tk == K::I32 || tk == K::I56 || tk == K::I64 ||
                                                tk == K::I128 ||
                                                tk == K::U8 || tk == K::U16 || tk == K::U24 ||
                                                tk == K::U32 || tk == K::U56 || tk == K::U64 ||
                                                tk == K::U128);
        else if (callee == "is_primitive") r = (tk == K::Bool || tk == K::F32 || tk == K::F64 ||
                                                tk == K::I8 || tk == K::I16 || tk == K::I24 ||
                                                tk == K::I32 || tk == K::I56 || tk == K::I64 ||
                                                tk == K::I128 ||
                                                tk == K::U8 || tk == K::U16 || tk == K::U24 ||
                                                tk == K::U32 || tk == K::U56 || tk == K::U64 ||
                                                tk == K::U128);
        return bool_lit(r);
    }

    // type_of::<T>() — compiler builtin, returns Type { kind: u32 }.
    // Slice 1 of typelevel metaprog. The kind is concretized at mono
    // (works in generic bodies where T is a TypeVar) via the magic
    // intrinsic __type_kind_of__::<T>(); mono replaces it with the
    // u32 literal of the substituted T's LogosType::Kind.
    if (callee == "type_of") {
        TypeRef elem = nullptr;
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                if (items.size() == 1)
                    elem = resolve_type(map_of(items.get(0)));
            }
        }
        if (!elem) {
            error("type_of::<T>() requires exactly one type argument");
            return error_expr();
        }
        std::vector<TypeRef> kt_targs; kt_targs.push_back(elem);
        auto kind_call = builder().call("__type_kind_of__",
                                        std::move(kt_targs), {},
                                        prim(LogosType::Kind::U32));
        std::vector<TypeRef> nt_targs; nt_targs.push_back(elem);
        auto name_call = builder().call("__type_name_of__",
                                        std::move(nt_targs), {},
                                        make_slice_type(u8_t()));
        auto type_t = make_struct_type("Type");
        std::vector<std::pair<std::string, lir::LExprPtr>> fields;
        fields.emplace_back("kind", std::move(kind_call));
        fields.emplace_back("name", std::move(name_call));
        return builder().struct_lit("Type", std::move(fields), type_t);
    }

    // args_of::<T>() — extracts generic type arguments of T as `[Type; N]`.
    // For non-generic T (no type_args), returns empty [Type; 0].
    // Same magic-call pattern as type_refs_of but the pack comes from the
    // single type's args at mono time, not from a parameter-pack expansion.
    if (callee == "args_of") {
        TypeRef elem = nullptr;
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                if (items.size() == 1)
                    elem = resolve_type(map_of(items.get(0)));
            }
        }
        if (!elem) {
            error("args_of::<T>() requires exactly one type argument");
            return error_expr();
        }
        auto type_t = make_struct_type("Type");
        LogosTypeBuilder arr_b; arr_b.kind = LogosType::Kind::Array;
        arr_b.elem = type_t;
        arr_b.arr_size = 0;  // mono retypes once T is concrete
        TypeRef arr_placeholder = pool_->alloc(std::move(arr_b));
        std::vector<TypeRef> targs; targs.push_back(elem);
        return builder().call("__args_of__",
                              std::move(targs), {}, arr_placeholder);
    }

    // type_refs_of::<T...>() — slice 2 of typelevel metaprog. Returns
    // [Type; N] populated with one Type{kind,name} value per pack member.
    // Mono substitutes after pack expansion (same TypeVar-in-type_args trick
    // as __sizeof_pack__ / __type_kind_of__).
    if (callee == "type_refs_of") {
        std::vector<TypeRef> targs;
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                for (size_t i = 0; i < items.size(); ++i)
                    targs.push_back(resolve_type(map_of(items.get(i))));
            }
        }
        auto type_t = make_struct_type("Type");
        LogosTypeBuilder arr_b; arr_b.kind = LogosType::Kind::Array;
        arr_b.elem = type_t;
        arr_b.arr_size = 0;  // mono retypes once pack count is known
        TypeRef arr_placeholder = pool_->alloc(std::move(arr_b));
        return builder().call("__type_refs_of__",
                              std::move(targs), {}, arr_placeholder);
    }

    // sizeof::<T>() — compiler builtin, returns i64 byte size of T.
    if (callee == "sizeof") {
        TypeRef elem = nullptr;
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                if (items.size() == 1)
                    elem = resolve_type(map_of(items.get(0)));
            }
        }
        if (!elem) error("sizeof::<T>() requires exactly one type argument");
        return builder().size_of(elem, prim(LogosType::Kind::I64));
    }

    // type_code_of::<T>() — returns the Hermes type_code of a concrete datatype as u64.
    // For concrete (non-generic) datatypes: SHA-256 of "package::Name" truncated to 56 bits,
    // shifted to >= 128 if needed (codes 1-127 are reserved for inline AnyVal).
    // For non-datatype T: returns 0.
    if (callee == "type_code_of") {
        TypeRef elem = nullptr;
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
            for (auto a : TypeRef(elem).type_args())
                if (a && TypeRef(a).kind() == LogosType::Kind::TypeVar) { has_tv = true; break; }
            if (has_tv)
                return builder().type_code_of(elem, prim(LogosType::Kind::U64));
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
            return builder().type_code_of(elem, prim(LogosType::Kind::U64));
        }
        return builder().lit_int((int64_t)code, prim(LogosType::Kind::U64));
    }

    // is_data_plain_of::<T>() — returns true (1) if T is a DataPlain datatype
    // (no relative-pointer fields), false (0) otherwise.
    // Non-datatype types always return true (scalars, structs, etc. are copyable).
    if (callee == "is_data_plain_of") {
        TypeRef elem = nullptr;
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
        TypeRef check = elem;
        while (check && TypeRef(check).kind() == LogosType::Kind::Array) check = TypeRef(check).elem();
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
        return builder().lit_int(is_plain ? 1LL : 0LL, prim(LogosType::Kind::Bool));
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
                            auto hs_type = make_struct_type("HermesStatic");
                            // Synthesize a ZonedStruct type for EReflectOf codegen.
                            TypeRef gtp = make_datatype_type(tname, cur_package_);
                            return builder().reflect_of(gtp, hs_type);
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
        TypeRef T = ts[0];
        if (TypeRef(T).kind() == LogosType::Kind::TypeVar) {
            // Inside a generic function — defer; mono will resolve and register.
            auto hs_type = make_struct_type("HermesStatic");
            return builder().reflect_of(T, hs_type);
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
        auto hs_type = make_struct_type("HermesStatic");
        return builder().reflect_of(T, hs_type);
    }

    // has_annotation::<T, A>() -> bool  (compile-time const-fold)
    // Returns true if datatype T has a user annotation of type A attached.
    if (callee == "has_annotation") {
        auto ts = collect_type_args();
        if (ts.size() != 2 || !ts[0] || !ts[1]) {
            error("has_annotation::<T, A>() requires exactly two type arguments");
            return error_expr();
        }
        TypeRef T = ts[0];
        TypeRef A = ts[1];
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
        TypeRef T = ts[0];
        TypeRef A = ts[1];
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
        LogosTypeBuilder opt_type; opt_type.kind = LogosType::Kind::Enum;
        opt_type.enum_name = "Option";
        if (!opt_pkg.empty()) opt_type.pkg_name = opt_pkg;
        opt_type.type_args = {A};
        TypeRef result_type = pool_->alloc(std::move(opt_type));
        // Build Datatype<A> type for the struct literal
        TypeRef a_type = A;  // already a Datatype type
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
            return builder().enum_lit("Option", "None", none_disc, result_type);
        }
        // Materialize the annotation as A{field: value, ...}
        // Helper: convert LAnnotationValue to LExprPtr
        std::function<lir::LExprPtr(const lir::LAnnotationValue&, TypeRef)> annot_val_to_expr;
        annot_val_to_expr = [&](const lir::LAnnotationValue& av, TypeRef expected) -> lir::LExprPtr {
            using K = lir::LAnnotationValue::Kind;
            switch (av.kind) {
            case K::Int:   return builder().lit_int(av.i, expected ? expected : prim(LogosType::Kind::I64));
            case K::Float: return builder().lit_float(av.f, expected ? expected : prim(LogosType::Kind::F64));
            case K::Bool:  return builder().lit_int(av.i, prim(LogosType::Kind::Bool));
            case K::Str:   return builder().lit_str(av.s, make_slice_type(u8_t()));
            case K::Enum:  {
                // Emit as enum literal: av.enum_name::av.enum_variant
                auto [epkg2, esi2] = find_enum_by_name(av.enum_name);
                int64_t disc2 = 0;
                if (esi2) for (auto& v : esi2->variants)
                    if (v.name == av.enum_variant) { disc2 = v.value; break; }
                auto etype = make_enum_type(av.enum_name, epkg2);
                return builder().enum_lit(av.enum_name, av.enum_variant, disc2, etype);
            }
            case K::Array: {
                std::vector<lir::LExprPtr> elems;
                TypeRef elem_t = (expected && TypeRef(expected).kind() == LogosType::Kind::Array)
                                          ? TypeRef(expected).elem() : nullptr;
                for (auto& item : av.arr) elems.push_back(annot_val_to_expr(item, elem_t));
                LogosTypeBuilder at; at.kind = LogosType::Kind::Array;
                at.elem = elem_t ? elem_t : (elems.empty() ? prim(LogosType::Kind::I64) : elems[0]->type);
                at.arr_size = (int64_t)elems.size();
                return builder().arr_lit(std::move(elems), pool_->alloc(std::move(at)));
            }
            }
            return error_expr();
        };
        // Build field list for the struct literal
        std::vector<std::pair<std::string, lir::LExprPtr>> fields;
        for (auto& [fname, fval] : found_inst->kv) {
            // Find expected type from annotation type's fields
            TypeRef ftype = nullptr;
            if (a_info) for (auto& f : a_info->fields)
                if (f.name == fname) { ftype = f.type; break; }
            fields.emplace_back(fname, annot_val_to_expr(fval, ftype));
        }
        auto struct_expr = builder().struct_lit(std::string(TypeRef(A).struct_name()), std::move(fields), a_type);
        // Wrap in Option<A>::Some(struct_expr)
        std::vector<lir::LExprPtr> payload;
        payload.push_back(std::move(struct_expr));
        return builder().enum_lit_data("Option", "Some", some_disc, std::move(payload), result_type);
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
        return builder().call(std::string(callee), {}, {}, error_t());
    }
    check_pub_access(fi_ptr->is_pub, fi_ptr->package, callee);

    // Resolve explicit type arguments from TYPE_PARAMS
    std::vector<TypeRef> type_args;
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
    if (TypeRef(recv->type).kind() == LogosType::Kind::Slice) {
        if (method_name == "len") {
            return builder().slice_len(std::move(recv), prim(LogosType::Kind::I64));
        }
        if (method_name == "as_ptr") {
            return builder().slice_ptr(std::move(recv), make_ptr(false, u8_t()));
        }
        error(std::format("slice has no method '{}'", method_name));
        return error_expr();
    }

    // Raw-pointer built-in arithmetic methods:
    //   p.byte_add(n) / p.byte_sub(n)  — offset n bytes, same pointer type
    //   p.add(n)      / p.sub(n)       — offset n elements
    //   p.byte_offset_from(q)          — i64 byte distance
    //   p.offset_from(q)               — i64 element distance
    if (TypeRef(recv->type).kind() == LogosType::Kind::Ptr) {
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
            auto at = args[0]->type;
            auto i64ty = prim(LogosType::Kind::I64);
            if (TypeRef(at).kind() != LogosType::Kind::Error && !types_compatible(at, i64ty))
                error(std::format("pointer method '{}': argument must be i64, got {}",
                      method_name, type_str(at)));
            widen_int_expr(args[0], i64ty, builder());
            auto ret_type = recv->type;
            return builder().ptr_arith(op, std::move(recv), std::move(args[0]), ret_type);
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
            auto at = args[0]->type;
            if (TypeRef(at).kind() != LogosType::Kind::Error && TypeRef(at).kind() != LogosType::Kind::Ptr)
                error(std::format("pointer method '{}': argument must be a pointer, got {}",
                      method_name, type_str(at)));
            return builder().ptr_diff(by_byte, std::move(recv), std::move(args[0]), prim(LogosType::Kind::I64));
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
    if (TypeRef rt(recv->type); rt && rt.kind() == LogosType::Kind::TraitObject) {
        auto tname = std::string(rt.trait_name());
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
                            auto pt = subst_type_sema(m.param_types[i + 1], self_subst);
                            widen_int_expr(arg_exprs[i], pt, builder());
                            auto at = arg_exprs[i]->type;
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
                                if (auto v = get_intlit_value(arg_exprs[i]))
                                    if (!intlit_fits(*v, TypeRef(pt).kind()))
                                        error(std::format("method '{}' arg {}: value {} does not fit in {}",
                                                          std::string(method_name), i + 1, *v, type_str(pt)));
                            // Check array literal elements against narrow array param type.
                            if (TypeRef(at).kind() == LogosType::Kind::Array && TypeRef(pt).kind() == LogosType::Kind::Array && TypeRef(pt).elem()) {
                                auto vr = expr_ref_of(*arg_exprs[i]);
                                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                                    lir_view::EArrLitView al{vr};
                                    for (uint64_t ei = 0; ei < al.count(); ++ei) {
                                        auto el = al.elem(ei);
                                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(el))
                                                if (!intlit_fits(*v, TypeRef(pt).elem().kind()))
                                                    error(std::format("method '{}' arg {}: array element {}: value {} does not fit in {}",
                                                                      std::string(method_name), i + 1, ei, *v, type_str(TypeRef(pt).elem())));
                                    }
                                }
                            }
                            // Check tuple literal elements against narrow tuple param element types.
                            if (TypeRef(at).kind() == LogosType::Kind::Tuple && TypeRef(pt).kind() == LogosType::Kind::Tuple) {
                                auto vr = expr_ref_of(*arg_exprs[i]);
                                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                                    lir_view::ETupleLitView tl{vr};
                                    uint64_t ei = 0;
                                    tl.each_elem([&](lir_view::ExprRef el) {
                                        if (ei >= TypeRef(pt).tuple_elems().size()) { ++ei; return; }
                                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(el))
                                                if (TypeRef(pt).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(TypeRef(pt).tuple_elems()[ei]).kind()))
                                                    error(std::format("method '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                                                      std::string(method_name), i + 1, ei, *v, type_str(TypeRef(pt).tuple_elems()[ei])));
                                        if (TypeRef(pt).tuple_elems()[ei] && TypeRef(TypeRef(pt).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                                            TypeRef(TypeRef(pt).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                                            el.kind() == lir_schema::expr::Code::ArrLit) {
                                            lir_view::EArrLitView ial{el};
                                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                                auto iel = ial.elem(ii);
                                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                                    if (auto v = get_intlit_value(iel))
                                                        if (!intlit_fits(*v, TypeRef(TypeRef(pt).tuple_elems()[ei]).elem().kind()))
                                                            error(std::format("method '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                                  std::string(method_name), i + 1, ei, ii, *v, type_str(TypeRef(TypeRef(pt).tuple_elems()[ei]).elem())));
                                            }
                                        }
                                        if (TypeRef(pt).tuple_elems()[ei] && TypeRef(TypeRef(pt).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                                            el.kind() == lir_schema::expr::Code::TupleLit) {
                                            lir_view::ETupleLitView itl{el};
                                            uint64_t ii = 0;
                                            itl.each_elem([&](lir_view::ExprRef iel) {
                                                if (ii >= TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                                    if (auto v = get_intlit_value(iel))
                                                        if (TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                                            error(std::format("method '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                                  std::string(method_name), i + 1, ei, ii, *v, type_str(TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii])));
                                                ++ii;
                                            });
                                        }
                                        ++ei;
                                    });
                                }
                            }
                        }
                    }
                    // Return type: substitute Self → &dyn Trait
                    auto ret_type = m.ret_type;
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
                    return builder().method_call_v(std::move(mc), ret_type);
                }
            }
        }
        error(std::format("trait '{}' has no method '{}'", tname, method_name));
        return error_expr();
    }

    // &tagged<TS> Trait method call: tag-based dispatch through the tier-1 table.
    // The receiver is a thin *const u8 pointer; type_code is read at runtime via TS.
    if (TypeRef rtg(recv->type); rtg && rtg.kind() == LogosType::Kind::TaggedPtr) {
        auto ts_name  = std::string(rtg.struct_name());  // e.g. "DataTypeTagSystem"
        auto tname    = std::string(rtg.trait_name());   // e.g. "Stringify"
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
            TypeRef ret_type = m.ret_type;
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
            return builder().method_call_v(std::move(mc), ret_type);
        }
        error(std::format("trait '{}' has no method '{}'", tname, method_name));
        return error_expr();
    }

    // TypeVar with trait bounds: look up trait method signature.
    // The actual impl method will be resolved during monomorphization.
    // Handle both T and *mut T / *const T / &T / &mut T receivers.
    TypeRef recv_inner = recv->type;
    if (recv_inner && TypeRef(recv_inner).kind() == LogosType::Kind::Ptr) {
        if (!inside_unsafe_)
            error("method call through raw pointer requires unsafe context");
        recv_inner = TypeRef(recv_inner).pointee();
    } else if (recv_inner && is_ref_like(TypeRef(recv_inner).kind()) && TypeRef(recv_inner).pointee()) {
        recv_inner = TypeRef(recv_inner).pointee();
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
                    StrMap<TypeRef> bindings(
                        self_subst.begin(), self_subst.end());
                    for (uint64_t i = 0; i < arg_exprs.size(); ++i) {
                        if (i + 1 >= chosen_method->param_types.size()) break;
                        auto pt0 = subst_type_sema(chosen_method->param_types[i + 1], self_subst);
                        unify_types(pt0, arg_exprs[i]->type, bindings);
                    }
                    for (auto& tp : chosen_method->type_params) {
                        auto it = bindings.find(tp.name);
                        if (it != bindings.end() && it->second)
                            self_subst[tp.name] = it->second;
                    }
                }
                for (uint64_t i = 0; i < arg_exprs.size(); ++i) {
                    auto pt = subst_type_sema(chosen_method->param_types[i + 1], self_subst);
                    widen_int_expr(arg_exprs[i], pt, builder());
                    auto at = arg_exprs[i]->type;
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
                        if (auto v = get_intlit_value(arg_exprs[i]))
                            if (!intlit_fits(*v, TypeRef(pt).kind()))
                                error(std::format("method '{}' arg {}: value {} does not fit in {}",
                                                  std::string(method_name), i + 1,
                                                  *v, type_str(pt)));
                    // Check array literal elements against narrow array param type.
                    if (TypeRef(at).kind() == LogosType::Kind::Array && TypeRef(pt).kind() == LogosType::Kind::Array && TypeRef(pt).elem()) {
                        auto vr = expr_ref_of(*arg_exprs[i]);
                        if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView al{vr};
                            for (uint64_t ei = 0; ei < al.count(); ++ei) {
                                auto el = al.elem(ei);
                                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(el))
                                        if (!intlit_fits(*v, TypeRef(pt).elem().kind()))
                                            error(std::format("method '{}' arg {}: array element {}: value {} does not fit in {}",
                                                              std::string(method_name), i + 1, ei, *v, type_str(TypeRef(pt).elem())));
                            }
                        }
                    }
                    // Check tuple literal elements against narrow tuple param element types.
                    if (TypeRef(at).kind() == LogosType::Kind::Tuple && TypeRef(pt).kind() == LogosType::Kind::Tuple) {
                        auto vr = expr_ref_of(*arg_exprs[i]);
                        if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView tl{vr};
                            uint64_t ei = 0;
                            tl.each_elem([&](lir_view::ExprRef el) {
                                if (ei >= TypeRef(pt).tuple_elems().size()) { ++ei; return; }
                                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(el))
                                        if (TypeRef(pt).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(TypeRef(pt).tuple_elems()[ei]).kind()))
                                            error(std::format("method '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                                              std::string(method_name), i + 1, ei, *v, type_str(TypeRef(pt).tuple_elems()[ei])));
                                if (TypeRef(pt).tuple_elems()[ei] && TypeRef(TypeRef(pt).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                                    TypeRef(TypeRef(pt).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                                    el.kind() == lir_schema::expr::Code::ArrLit) {
                                    lir_view::EArrLitView ial{el};
                                    for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                        auto iel = ial.elem(ii);
                                        if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(iel))
                                                if (!intlit_fits(*v, TypeRef(TypeRef(pt).tuple_elems()[ei]).elem().kind()))
                                                    error(std::format("method '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                          std::string(method_name), i + 1, ei, ii, *v, type_str(TypeRef(TypeRef(pt).tuple_elems()[ei]).elem())));
                                    }
                                }
                                if (TypeRef(pt).tuple_elems()[ei] && TypeRef(TypeRef(pt).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                                    el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                                    el.kind() == lir_schema::expr::Code::TupleLit) {
                                    lir_view::ETupleLitView itl{el};
                                    uint64_t ii = 0;
                                    itl.each_elem([&](lir_view::ExprRef iel) {
                                        if (ii >= TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                        if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                            if (auto v = get_intlit_value(iel))
                                                if (TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                                    error(std::format("method '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                          std::string(method_name), i + 1, ei, ii, *v, type_str(TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii])));
                                        ++ii;
                                    });
                                }
                                ++ei;
                            });
                        }
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
            TypeRef ret_type = subst_type_sema(chosen_method->ret_type, self_subst);

            // Use EMethodCall — mono will resolve to concrete impl.
            lir::EMethodCall mc;
            mc.receiver = std::move(recv);
            mc.method   = std::string(method_name);
            mc.type_args = {};
            // Propagate method-level type params (e.g. H in `hash<H>`) inferred above.
            if (!chosen_method->type_params.empty()) {
                StrMap<TypeRef> bindings(
                    self_subst.begin(), self_subst.end());
                for (uint64_t i = 0; i < arg_exprs.size(); ++i) {
                    if (i + 1 >= chosen_method->param_types.size()) break;
                    auto pt0 = subst_type_sema(chosen_method->param_types[i + 1], self_subst);
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
            return builder().method_call_v(std::move(mc), ret_type);
        }

        error(std::format("type parameter '{}' has no trait bound providing method '{}'",
                          TypeRef(recv_inner).type_var_name(), std::string(method_name)));
        return builder().method_call(std::move(recv), std::string(method_name), "", {}, std::move(arg_exprs), -1, error_t());
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
        TypeRef rte(recv->type);
        if (rte && rte.kind() == LogosType::Kind::Enum &&
            !rte.type_args().empty()) {
            std::string base(rte.enum_name());
            auto generic_key = base + "__" + std::string(method_name);
            const SemaFuncInfo* fi_ptr = nullptr;
            {
                std::vector<TypeRef> types;
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
                    for (size_t i = 0; i < tps.size() && i < rte.type_args().size(); ++i)
                        subst[tps[i].name] = rte.type_args()[i];
                }
                TypeRef ret = subst_type_sema(fi_ptr->ret_type, subst);
                // Mangle: "Option__i32" is the concrete enum name
                std::string concrete_enum = base;
                for (auto ta : rte.type_args()) {
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
                return builder().call(callee_name, {}, std::move(pargs), ret);
            }
        }
        auto tname = type_str(recv->type);
        auto mangled_prim = tname + "__" + std::string(method_name);
        const SemaFuncInfo* fi_ptr = nullptr;
        {
            std::vector<TypeRef> types;
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
                std::vector<TypeRef> m_type_args;
                if (!infer_type_args(*fi_ptr, arg_exprs, m_type_args, seed, 1)) {
                    error(std::format("could not infer type arguments for generic method '{}'",
                                      mangled_prim));
                }
                // Auto-ref receiver if method expects &Self / &mut Self.
                if (!fi_ptr->param_types.empty()) {
                    auto formal0 = fi_ptr->param_types[0];
                    if (formal0 && is_ref_like(TypeRef(formal0).kind()) && recv->type &&
                        !is_ref_like(TypeRef(recv->type).kind()) &&
                        TypeRef(recv->type).kind() != LogosType::Kind::Ptr) {
                        bool is_mut = TypeRef(formal0).kind() == LogosType::Kind::MutRef;
                        auto __ty_recv = make_ref(is_mut, recv->type);

                        auto addr = builder().addr_of_temp(std::move(recv), is_mut, __ty_recv);
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
                auto formal0 = fi_ptr->param_types[0];
                if (formal0 && is_ref_like(TypeRef(formal0).kind()) && recv->type &&
                    !is_ref_like(TypeRef(recv->type).kind()) &&
                    TypeRef(recv->type).kind() != LogosType::Kind::Ptr) {
                    bool is_mut = TypeRef(formal0).kind() == LogosType::Kind::MutRef;
                    auto __ty_recv = make_ref(is_mut, recv->type);

                    auto addr = builder().addr_of_temp(std::move(recv), is_mut, __ty_recv);
                    recv = std::move(addr);
                }
            }
            std::vector<lir::LExprPtr> pargs;
            pargs.push_back(std::move(recv));
            for (auto& a : arg_exprs) pargs.push_back(std::move(a));
            return builder().call(fi_ptr->symbol_name.empty() ? mangled_prim : fi_ptr->symbol_name, {}, std::move(pargs), fi_ptr->ret_type);
        }
        error(std::format("method call: receiver is not a struct (got {})",
              type_str(recv->type)));
        return builder().method_call(std::move(recv), std::string(method_name), "", {}, std::move(arg_exprs), -1, error_t());
    }

    auto mangled = std::string(sname) + "__" + std::string(method_name);
    const SemaFuncInfo* fi_ptr = nullptr;
    bool auto_ref_recv = false;
    bool auto_ref_mut = false;
    SemaSubst recv_struct_subst;
    {
        TypeRef rst = recv->type;
        if (rst && TypeRef(rst).kind() == LogosType::Kind::Ptr && TypeRef(rst).pointee()) {
            rst = TypeRef(rst).pointee();
        } else if (rst && is_ref_like(TypeRef(rst).kind()) && TypeRef(rst).pointee()) {
            rst = TypeRef(rst).pointee();
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
        std::vector<TypeRef> types;
        types.push_back(recv->type);
        for (auto& a : arg_exprs) types.push_back(a->type);
        for (auto* cand : find_func_candidates(mangled)) {
            if (!cand || !cand->type_params.empty()) continue;
            if (cand->param_types.size() != types.size()) continue;
            bool ok = true;
            bool needs_ref = false;
            bool needs_mut = false;
            auto formal0 = cand->param_types[0];
            if (!recv_struct_subst.empty())
                formal0 = subst_type_sema(formal0, recv_struct_subst);
            auto actual0 = recv->type;
            if (actual0 && formal0 && !types_equal(actual0, formal0)) {
                if (TypeRef(actual0).kind() != LogosType::Kind::Ref &&
                    TypeRef(actual0).kind() != LogosType::Kind::MutRef &&
                    TypeRef(actual0).kind() != LogosType::Kind::Ptr &&
                    is_ref_like(TypeRef(formal0).kind()) && TypeRef(formal0).pointee() &&
                    types_equal(actual0, TypeRef(formal0).pointee())) {
                    needs_ref = true;
                    needs_mut = TypeRef(formal0).kind() == LogosType::Kind::MutRef;
                } else if (TypeRef(actual0).kind() != LogosType::Kind::Ref &&
                           TypeRef(actual0).kind() != LogosType::Kind::MutRef &&
                           TypeRef(actual0).kind() != LogosType::Kind::Ptr &&
                           TypeRef(formal0).kind() == LogosType::Kind::Ptr &&
                           TypeRef(formal0).pointee() &&
                           types_equal(actual0, TypeRef(formal0).pointee())) {
                    needs_ref = true;
                    needs_mut = false;
                } else if (TypeRef(actual0).kind() == LogosType::Kind::Ptr &&
                           TypeRef(formal0).kind() == LogosType::Kind::Ptr &&
                           TypeRef(actual0).pointee() && TypeRef(formal0).pointee() &&
                           types_equal(TypeRef(actual0).pointee(), TypeRef(formal0).pointee())) {
                    // const/mut pointer receivers are compatible if pointees match.
                } else if (!types_compatible(actual0, formal0)) {
                    ok = false;
                }
            }
            for (size_t i = 1; ok && i < cand->param_types.size(); ++i) {
                auto at = types[i];
                auto pt = cand->param_types[i];
                if (!recv_struct_subst.empty())
                    pt = subst_type_sema(pt, recv_struct_subst);
                if (!at || !pt || !arg_compatible_for_dispatch(arg_exprs[i - 1], at, pt)) {
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
        TypeRef(recv->type).kind() != LogosType::Kind::Ref &&
        TypeRef(recv->type).kind() != LogosType::Kind::MutRef &&
        TypeRef(recv->type).kind() != LogosType::Kind::Ptr) {
        auto __ty_recv = make_ref(auto_ref_mut, recv->type);

        auto addr = builder().addr_of_temp(std::move(recv), auto_ref_mut, __ty_recv);
        recv = std::move(addr);
    }

    // Fallback: for generic structs (Foo$G1$i32), methods may be registered under base name (Foo).
    if (!fi_ptr) {
        std::string base_sname(sname);
        if (auto d = base_sname.find('$'); d != std::string::npos)
            base_sname = base_sname.substr(0, d);
        if (base_sname != sname) {
            auto base_mangled = base_sname + "__" + std::string(method_name);
            std::vector<TypeRef> types;
            types.push_back(recv->type);
            for (auto& a : arg_exprs) types.push_back(a->type);
            for (auto* cand : find_func_candidates(base_mangled)) {
                if (!cand || !cand->type_params.empty()) continue;
                if (cand->param_types.size() != types.size()) continue;
                bool ok = true;
                bool needs_ref = false;
                bool needs_mut = false;
                auto formal0 = cand->param_types[0];
                if (!recv_struct_subst.empty())
                    formal0 = subst_type_sema(formal0, recv_struct_subst);
                auto actual0 = recv->type;
                if (actual0 && formal0 && !types_equal(actual0, formal0)) {
                    if (TypeRef(actual0).kind() != LogosType::Kind::Ref &&
                        TypeRef(actual0).kind() != LogosType::Kind::MutRef &&
                        TypeRef(actual0).kind() != LogosType::Kind::Ptr &&
                        is_ref_like(TypeRef(formal0).kind()) && TypeRef(formal0).pointee() &&
                        types_equal(actual0, TypeRef(formal0).pointee())) {
                        needs_ref = true;
                        needs_mut = TypeRef(formal0).kind() == LogosType::Kind::MutRef;
                    } else if (TypeRef(actual0).kind() != LogosType::Kind::Ref &&
                               TypeRef(actual0).kind() != LogosType::Kind::MutRef &&
                               TypeRef(actual0).kind() != LogosType::Kind::Ptr &&
                               TypeRef(formal0).kind() == LogosType::Kind::Ptr &&
                               TypeRef(formal0).pointee() &&
                               types_equal(actual0, TypeRef(formal0).pointee())) {
                        needs_ref = true;
                        needs_mut = false;
                    } else if (TypeRef(actual0).kind() == LogosType::Kind::Ptr &&
                               TypeRef(formal0).kind() == LogosType::Kind::Ptr &&
                               TypeRef(actual0).pointee() && TypeRef(formal0).pointee() &&
                               types_equal(TypeRef(actual0).pointee(), TypeRef(formal0).pointee())) {
                        // const/mut pointer receivers are compatible if pointees match.
                    } else if (!types_compatible(actual0, formal0)) {
                        ok = false;
                    }
                }
                for (size_t i = 1; ok && i < cand->param_types.size(); ++i) {
                    auto at = types[i];
                    auto pt = cand->param_types[i];
                    if (!recv_struct_subst.empty())
                        pt = subst_type_sema(pt, recv_struct_subst);
                    if (!at || !pt || !arg_compatible_for_dispatch(arg_exprs[i - 1], at, pt)) {
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
        TypeRef(recv->type).kind() != LogosType::Kind::Ref &&
        TypeRef(recv->type).kind() != LogosType::Kind::MutRef &&
        TypeRef(recv->type).kind() != LogosType::Kind::Ptr) {
        auto __ty_recv = make_ref(auto_ref_mut, recv->type);

        auto addr = builder().addr_of_temp(std::move(recv), auto_ref_mut, __ty_recv);
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
            std::vector<TypeRef> bi_arg_types;
            bi_arg_types.push_back(recv->type);
            for (auto& a : arg_exprs) bi_arg_types.push_back(a->type);
            const SemaFuncInfo* mfi = nullptr;
            if (auto fit = find_func_by_base_and_signature(bi.mangled_name, bi_arg_types, false))
                mfi = fit;
            else if (auto git = find_generic_func(bi.mangled_name))
                mfi = git;
            if (!mfi) continue;
            // Type arg = receiver's concrete type (unwrapped from ref/ptr).
            TypeRef recv_inner = recv->type;
            if (recv_inner && (TypeRef(recv_inner).kind() == LogosType::Kind::Ptr ||
                               is_ref_like(TypeRef(recv_inner).kind())) && TypeRef(recv_inner).pointee())
                recv_inner = TypeRef(recv_inner).pointee();
            // Auto-ref: if method expects &self / &mut self but recv is a
            // value, take its address.
            if (!mfi->param_types.empty()) {
                SemaSubst s_subst;
                s_subst["Self"] = recv_inner;
                s_subst[bi.target_typevar] = recv_inner;
                TypeRef target_self =
                    subst_type_sema(mfi->param_types[0], s_subst);
                if (target_self &&
                    (TypeRef(target_self).kind() == LogosType::Kind::Ref ||
                     TypeRef(target_self).kind() == LogosType::Kind::MutRef) &&
                    recv->type &&
                    TypeRef(recv->type).kind() != LogosType::Kind::Ref &&
                    TypeRef(recv->type).kind() != LogosType::Kind::MutRef &&
                    TypeRef(recv->type).kind() != LogosType::Kind::Ptr) {
                    bool is_mut = TypeRef(target_self).kind() == LogosType::Kind::MutRef;
                    auto addr = builder().addr_of_temp(std::move(recv), is_mut, target_self);
                    recv = std::move(addr);
                }
            }
            std::vector<TypeRef> type_args = { recv_inner };
            std::vector<lir::LExprPtr> pargs;
            pargs.push_back(std::move(recv));
            for (auto& a : arg_exprs) pargs.push_back(std::move(a));
            return finish_generic_call(
                                       mfi->symbol_name.empty() ? bi.mangled_name : mfi->symbol_name, *mfi,
                                       std::move(type_args), std::move(pargs));
        }
        error(std::format("method call: '{}' has no method '{}'", sname, method_name));
        return builder().method_call(std::move(recv), std::string(method_name), "", {}, std::move(arg_exprs), -1, error_t());
    }

    auto& fi = *fi_ptr;
    check_pub_access(fi.is_pub, fi.package, mangled);
    if (fi.is_unsafe && !inside_unsafe_)
        error(std::format("call to unsafe method '{}' requires unsafe context", mangled));

    // Build TypeVar→concrete substitution from the receiver's struct type args.
    // This lets us check e.g. Vec<i32>::push(val: T) with T resolved to i32.
    SemaSubst struct_subst;
    {
        TypeRef rst = recv->type;
        if (rst && TypeRef(rst).kind() == LogosType::Kind::Ptr) {
            if (!inside_unsafe_)
                error("method call through raw pointer requires unsafe context");
            if (TypeRef(rst).pointee()) rst = TypeRef(rst).pointee();
        } else if (rst && is_ref_like(TypeRef(rst).kind()) && TypeRef(rst).pointee()) {
            rst = TypeRef(rst).pointee();
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
    std::vector<TypeRef> m_type_args;
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
            size_t pi = i + 1;
            if (pi < fi.param_types.size()) {
                auto pt = fi.param_types[pi];
                if (!struct_subst.empty()) pt = subst_type_sema(pt, struct_subst);
                widen_int_expr(arg_exprs[i], pt, builder());
            }
            auto at = arg_exprs[i]->type;
            if (pi < fi.param_types.size()) {
                auto pt = fi.param_types[pi];
                if (!struct_subst.empty()) pt = subst_type_sema(pt, struct_subst);
                if (TypeRef(at).kind() != LogosType::Kind::Error && TypeRef(pt).kind() != LogosType::Kind::Error &&
                    !types_compatible(at, pt))
                    error(std::format("method '{}' arg {}: expected {}, got {}",
                          mangled, i + 1, type_str(pt), type_str(at)));
                if (TypeRef(at).kind() == LogosType::Kind::IntLit && TypeRef(pt).kind() != LogosType::Kind::Error)
                    if (auto v = get_intlit_value(arg_exprs[i]))
                        if (!intlit_fits(*v, TypeRef(pt).kind()))
                            error(std::format("method '{}' arg {}: value {} does not fit in {}",
                                  mangled, i + 1, *v, type_str(pt)));
                // Check array literal elements against narrow array param type.
                if (TypeRef(at).kind() == LogosType::Kind::Array && TypeRef(pt).kind() == LogosType::Kind::Array && TypeRef(pt).elem()) {
                    auto vr = expr_ref_of(*arg_exprs[i]);
                    if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                        lir_view::EArrLitView al{vr};
                        for (uint64_t ei = 0; ei < al.count(); ++ei) {
                            auto el = al.elem(ei);
                            if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(el))
                                    if (!intlit_fits(*v, TypeRef(pt).elem().kind()))
                                        error(std::format("method '{}' arg {}: array element {}: value {} does not fit in {}",
                                              mangled, i + 1, ei, *v, type_str(TypeRef(pt).elem())));
                        }
                    }
                }
                // Check tuple literal elements against narrow tuple param element types.
                if (TypeRef(at).kind() == LogosType::Kind::Tuple && TypeRef(pt).kind() == LogosType::Kind::Tuple) {
                    auto vr = expr_ref_of(*arg_exprs[i]);
                    if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                        lir_view::ETupleLitView tl{vr};
                        uint64_t ei = 0;
                        tl.each_elem([&](lir_view::ExprRef el) {
                            if (ei >= TypeRef(pt).tuple_elems().size()) { ++ei; return; }
                            if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(el))
                                    if (TypeRef(pt).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(TypeRef(pt).tuple_elems()[ei]).kind()))
                                        error(std::format("method '{}' arg {}: tuple element {}: value {} does not fit in {}",
                                              mangled, i + 1, ei, *v, type_str(TypeRef(pt).tuple_elems()[ei])));
                            if (TypeRef(pt).tuple_elems()[ei] && TypeRef(TypeRef(pt).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                                TypeRef(TypeRef(pt).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                                el.kind() == lir_schema::expr::Code::ArrLit) {
                                lir_view::EArrLitView ial{el};
                                for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                    auto iel = ial.elem(ii);
                                    if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(iel))
                                            if (!intlit_fits(*v, TypeRef(TypeRef(pt).tuple_elems()[ei]).elem().kind()))
                                                error(std::format("method '{}' arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                      mangled, i + 1, ei, ii, *v, type_str(TypeRef(TypeRef(pt).tuple_elems()[ei]).elem())));
                                }
                            }
                            if (TypeRef(pt).tuple_elems()[ei] && TypeRef(TypeRef(pt).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                                el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                                el.kind() == lir_schema::expr::Code::TupleLit) {
                                lir_view::ETupleLitView itl{el};
                                uint64_t ii = 0;
                                itl.each_elem([&](lir_view::ExprRef iel) {
                                    if (ii >= TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                    if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(iel))
                                            if (TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                                error(std::format("method '{}' arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      mangled, i + 1, ei, ii, *v, type_str(TypeRef(TypeRef(pt).tuple_elems()[ei]).tuple_elems()[ii])));
                                    ++ii;
                                });
                            }
                            ++ei;
                        });
                    }
                }
            }
        }
    }

    // Method type args inferred above; verify and bounds-check here.
    if (!fi.type_params.empty()) {
        bool all_bound = m_type_args.size() == fi.type_params.size();
        for (auto ta : m_type_args)
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
            TypeRef rst = recv->type;
            if (rst && (TypeRef(rst).kind() == LogosType::Kind::Ptr || is_ref_like(TypeRef(rst).kind())) && TypeRef(rst).pointee())
                rst = TypeRef(rst).pointee();
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
        for (auto ta : m_type_args)
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
    TypeRef ret = struct_subst.empty()
        ? fi.ret_type : subst_type_sema(fi.ret_type, struct_subst);

    lir::EMethodCall mc;
    mc.receiver     = std::move(recv);
    mc.method       = std::string(method_name);
    mc.resolved_symbol = fi.symbol_name.empty() ? mangled : fi.symbol_name;
    mc.type_args    = std::move(m_type_args);
    mc.args         = std::move(arg_exprs);
    mc.vtable_index = -1;
    mc.resolved_type = "";
    return builder().method_call_v(std::move(mc), ret);
}

lir::LExprPtr SemaChecker::lower_field_read(TinyMapView node) {
    auto field_name = str_of(node.get(la::FIELD.code));
    auto recv = lower_expr(map_of(node.get(la::RECEIVER.code)));
    TypeRef recv_base_t = recv->type;
    if (recv_base_t && TypeRef(recv_base_t).kind() == LogosType::Kind::Ptr) {
        if (!inside_unsafe_)
            error("field read through raw pointer requires unsafe context");
        recv_base_t = TypeRef(recv_base_t).pointee();
    } else if (recv_base_t && is_ref_like(TypeRef(recv_base_t).kind())) {
        recv_base_t = TypeRef(recv_base_t).pointee();
    }

    // DataRef<T> ergonomic read: p.field → p.ptr().field
    // Intercept before normal struct field lookup so that DataRef<T>.x works without
    // an explicit let pw = p.ptr() intermediate step.
    if (recv_base_t && TypeRef(recv_base_t).kind() == LogosType::Kind::Struct &&
        TypeRef(recv_base_t).struct_name() == "DataRef" &&
        TypeRef(recv_base_t).type_args().size() == 1) {
        TypeRef T = TypeRef(recv_base_t).type_args()[0];
        if (T && TypeRef(T).kind() == LogosType::Kind::ZonedStruct) {
            auto ft = field_type_of_for_type(T, field_name);
            if (ft) {
                if (!inside_unsafe_)
                    error(std::format("DataRef<T>.{}: field access requires unsafe context",
                                      field_name));
                TypeRef ptr_t = make_ptr(false, T);
                auto mc = builder().method_call(std::move(recv), "ptr", "", {}, {}, -1, ptr_t);
                return builder().field_read(std::move(mc), std::string(field_name), ft);
            }
        }
    }

    auto sname = struct_name_from_type(recv_base_t);
    if (sname.empty()) {
        error(std::format("field read: receiver is not a struct or class (got {})",
              type_str(recv->type)));
        return builder().field_read(std::move(recv), std::string(field_name), error_t());
    }
    // Resolve the actual struct type (receiver may be a pointer/reference to a struct).
    TypeRef recv_struct_t = recv_base_t;
    auto ft = field_type_of_for_type(recv_struct_t, field_name);
    if (!ft) {
        error(std::format("field read: struct '{}' has no field '{}'", sname, field_name));
        return builder().field_read(std::move(recv), std::string(field_name), error_t());
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
    return builder().field_read(std::move(recv), std::string(field_name), ft);
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
        auto check_alias = [&](const std::string& key) -> TypeRef {
            auto ait = type_aliases_.find(key);
            if (ait != type_aliases_.end() &&
                ait->second.type_params.empty() && ait->second.lifetime_params.empty())
                return ait->second.type;
            return nullptr;
        };
        TypeRef aliased = check_alias(sname_buf);
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
            lir::LExprPtr val = nullptr;
            if (ic == la::FIELD_SHORTHAND) {
                // Point { x, y } — same as Point { x: x, y: y }
                auto t = lookup(fname);
                if (!t) {
                    error(std::format("undefined variable '{}' used as field shorthand", fname));
                    val = error_expr();
                } else {
                    val = builder().var_ref(std::string(fname), t);
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
        auto hint_for_tv = [&](std::string_view tv_name) -> TypeRef {
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
        std::vector<TypeRef> explicit_args;
        if (node.has_key(la::TYPE_PARAMS)) {
            AnyVal tpav = node.get(la::TYPE_PARAMS.code);
            if (!tpav.is_null()) {
                auto tplist = map_of(tpav);
                if (tplist.has_key(la::ITEMS)) {
                    auto items = arr_of(tplist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size() && i < sinfo.type_params.size(); ++i) {
                        auto resolved = resolve_type(map_of(items.get(i)));
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
            auto raw_ft = [&]() -> TypeRef {
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
                    auto vt = fval->type;
                    if (TypeRef(vt).kind() == LogosType::Kind::IntLit) {
                        auto h = hint_for_tv(tv);
                        vt = (h && TypeRef(h).kind() != LogosType::Kind::Error) ? h : i32_t();
                    } else if (TypeRef(vt).kind() == LogosType::Kind::FloatLit) {
                        auto h = hint_for_tv(tv);
                        vt = (h && TypeRef(h).kind() != LogosType::Kind::Error) ? h : prim(LogosType::Kind::F64);
                    }
                    inferred[std::string(tv)] = vt;
                }
            } else if (TypeRef(raw_ft).kind() == LogosType::Kind::Array && TypeRef(raw_ft).elem() &&
                       TypeRef(raw_ft).elem().kind() == LogosType::Kind::TypeVar) {
                // [T; N] field — infer T from element type of the value.
                auto tv = TypeRef(raw_ft).elem().type_var_name();
                TypeRef fvt(fval->type);
                if (!inferred.count(tv) && fvt && fvt.kind() == LogosType::Kind::Array &&
                    fvt.elem()) {
                    auto vt = fvt.elem();
                    if (vt.kind() == LogosType::Kind::IntLit) {
                        auto h = hint_for_tv(tv);
                        vt = (h && h.kind() != LogosType::Kind::Error) ? h : i32_t();
                    }
                    inferred[std::string(tv)] = vt;
                }
            } else if ((TypeRef(raw_ft).kind() == LogosType::Kind::Ptr ||
                        TypeRef(raw_ft).kind() == LogosType::Kind::Ref ||
                        TypeRef(raw_ft).kind() == LogosType::Kind::MutRef) && TypeRef(raw_ft).pointee() &&
                       TypeRef(raw_ft).pointee().kind() == LogosType::Kind::TypeVar) {
                // *T / &T / &mut T field — infer T from the value's pointee type.
                auto tv = TypeRef(raw_ft).pointee().type_var_name();
                TypeRef fvp(fval->type);
                if (!inferred.count(tv) && fvp && is_ref_like(fvp.kind()) &&
                    fvp.pointee()) {
                    auto vt = fvp.pointee();
                    if (vt.kind() != LogosType::Kind::Error)
                        inferred[std::string(tv)] = vt;
                }
            }
        }
        // For any TypeVar still not inferred from fields, fall back to hint.
        for (auto& tp : sinfo.type_params) {
            if (!inferred.count(tp.name)) {
                auto h = hint_for_tv(tp.name);
                if (h && TypeRef(h).kind() != LogosType::Kind::Error)
                    inferred[tp.name] = h;
            }
        }
        std::vector<TypeRef> args;
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
        TypeRef lit_type = slit_is_zoned
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
                        if (f.type && TypeRef(f.type).kind() != LogosType::Kind::Error &&
                            TypeRef(fval->type).kind() != LogosType::Kind::Error &&
                            TypeRef(f.type).kind() != LogosType::Kind::TypeVar &&
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
                TypeRef ft = nullptr;
                for (auto& ef : effective->fields)
                    if (ef.name == fname) { ft = ef.type; break; }
                bool ft_has_typevar = ft && (TypeRef(ft).kind() == LogosType::Kind::TypeVar ||
                    (TypeRef(ft).kind() == LogosType::Kind::Array && TypeRef(ft).elem() &&
                     TypeRef(ft).elem().kind() == LogosType::Kind::TypeVar));
                if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
                    TypeRef(fval->type).kind() != LogosType::Kind::Error &&
                    !ft_has_typevar &&
                    !types_compatible(fval->type, ft) &&
                    !try_coerce_closure_to_fnptr(fval, ft)) {
                    error(std::format("struct literal '{}' field '{}': expected {}, got {}",
                          sname, fname, type_str(ft), type_str(fval->type)));
                }
                // Check IntLit field value fits in the declared field type.
                if (ft && TypeRef(fval->type).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(fval))
                        if (!intlit_fits(*v, TypeRef(ft).kind()))
                            error(std::format("struct literal '{}' field '{}': value {} does not fit in {}",
                                  sname, fname, *v, type_str(ft)));
            }
        }
        for (auto& [fname, init] : initialized)
            if (!init)
                error(std::format("struct literal '{}': field '{}' not initialized", sname, fname));

        return builder().struct_lit(concrete, std::move(fields), lit_type);
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
                    auto ft = f.type;
                    if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
                        TypeRef(fval->type).kind() != LogosType::Kind::Error &&
                        !types_compatible(fval->type, ft)) {
                        error(std::format("struct literal '{}' field '{}': expected {}, got {}",
                              sname, fname, type_str(ft), type_str(fval->type)));
                    }
                    if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
                        TypeRef(fval->type).kind() == LogosType::Kind::IntLit)
                        if (auto v = get_intlit_value(fval))
                            if (!intlit_fits(*v, TypeRef(ft).kind()))
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
            auto ft = field_type_of(std::string(sname), fname);
            if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
                TypeRef(fval->type).kind() != LogosType::Kind::Error &&
                !types_compatible(fval->type, ft) &&
                !try_coerce_closure_to_fnptr(fval, ft)) {
                error(std::format("struct literal '{}' field '{}': expected {}, got {}",
                      sname, fname, type_str(ft), type_str(fval->type)));
            }
            // Check IntLit field value fits in the declared field type.
            if (ft && TypeRef(fval->type).kind() == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(fval))
                    if (!intlit_fits(*v, TypeRef(ft).kind()))
                        error(std::format("struct literal '{}' field '{}': value {} does not fit in {}",
                              sname, fname, *v, type_str(ft)));
            // Check array literal elements against narrow array field type.
            if (ft && TypeRef(ft).kind() == LogosType::Kind::Array && TypeRef(ft).elem() &&
                TypeRef(fval->type).kind() == LogosType::Kind::Array) {
                auto vr = expr_ref_of(*fval);
                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView al{vr};
                    for (uint64_t i = 0; i < al.count(); ++i) {
                        auto el = al.elem(i);
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (!intlit_fits(*v, TypeRef(ft).elem().kind()))
                                    error(std::format("struct literal '{}' field '{}': array element {}: value {} does not fit in {}",
                                          sname, fname, i, *v, type_str(TypeRef(ft).elem())));
                    }
                }
            }
            // Check tuple literal elements against narrow tuple field element types.
            if (ft && TypeRef(ft).kind() == LogosType::Kind::Tuple && TypeRef(fval->type).kind() == LogosType::Kind::Tuple) {
                auto vr = expr_ref_of(*fval);
                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView tl{vr};
                    uint64_t i = 0;
                    tl.each_elem([&](lir_view::ExprRef el) {
                        if (i >= TypeRef(ft).tuple_elems().size()) { ++i; return; }
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (TypeRef(ft).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(ft).tuple_elems()[i]).kind()))
                                    error(std::format("struct literal '{}' field '{}': tuple element {}: value {} does not fit in {}",
                                          sname, fname, i, *v, type_str(TypeRef(ft).tuple_elems()[i])));
                        if (TypeRef(ft).tuple_elems()[i] && TypeRef(TypeRef(ft).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(ft).tuple_elems()[i]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                            el.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{el};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(ft).tuple_elems()[i]).elem().kind()))
                                            error(std::format("struct literal '{}' field '{}': tuple element {}: array element {}: value {} does not fit in {}",
                                                  sname, fname, i, ii, *v, type_str(TypeRef(TypeRef(ft).tuple_elems()[i]).elem())));
                            }
                        }
                        if (TypeRef(ft).tuple_elems()[i] && TypeRef(TypeRef(ft).tuple_elems()[i]).kind() == LogosType::Kind::Tuple &&
                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                            el.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{el};
                            uint64_t ii = 0;
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii >= TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems().size()) { ++ii; return; }
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii]).kind()))
                                            error(std::format("struct literal '{}' field '{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  sname, fname, i, ii, *v, type_str(TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii])));
                                ++ii;
                            });
                        }
                        ++i;
                    });
                }
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
        {
            auto er = expr_ref_of(*base_expr);
            if (er.kind() == lir_schema::expr::Code::VarRef)
                base_var = std::string(lir_view::EVarRefView{er}.name());
        }
        for (auto& [fname, inited] : initialized) {
            if (!inited) {
                inited = true;
                auto ft = field_type_of(std::string(sname), fname);
                lir::LExprPtr recv = nullptr;
                if (!base_var.empty()) {
                    recv = builder().var_ref(base_var, base_expr->type);
                } else {
                    // Complex base: re-lower (might evaluate twice, but rare)
                    recv = lower_expr(base_node);
                }
                auto field_val = builder().field_read(std::move(recv), fname, ft ? ft : error_t());
                fields.push_back({fname, std::move(field_val)});
            }
        }
    }

    for (auto& [fname, init] : initialized)
        if (!init)
            error(std::format("struct literal '{}': field '{}' not initialized", sname, fname));

    // Move semantics: mark Move-typed field values as consumed.
    for (auto& [fname, fval] : fields) {
        if (fval && is_move_type(fval->type)) {
            auto er = expr_ref_of(*fval);
            if (er.kind() == lir_schema::expr::Code::VarRef)
                mark_moved(std::string(lir_view::EVarRefView{er}.name()));
        }
    }

    std::vector<std::string> ng_lt_args;
    if (hint_struct_type_ && TypeRef(hint_struct_type_).struct_name() == std::string(sname))
        ng_lt_args = TypeRef(hint_struct_type_).lifetime_args();
    LogosTypeBuilder ng_t;
    ng_t.kind = slit_is_zoned
                ? LogosType::Kind::ZonedStruct : LogosType::Kind::Struct;
    ng_t.struct_name   = std::string(sname);
    ng_t.lifetime_args = std::move(ng_lt_args);
    TypeRef lit_result_type = pool_->alloc(std::move(ng_t));
    return builder().struct_lit(std::string(sname), std::move(fields), lit_result_type);
}

lir::LExprPtr SemaChecker::lower_index_read(TinyMapView node) {
    auto recv = lower_expr(map_of(node.get(la::RECEIVER.code)));
    auto arr_type = recv->type;

    lir::LExprPtr idx = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    if (!is_integer(idx->type))
        error(std::format("array index must be integer, got {}", type_str(idx->type)));

    // Slice indexing: s[i] → ESliceIndex
    if (TypeRef(arr_type).kind() == LogosType::Kind::Slice) {
        auto elem = TypeRef(arr_type).elem() ? TypeRef(arr_type).elem() : error_t();
        return builder().slice_index(std::move(recv), std::move(idx), elem);
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

    TypeRef elem = error_t();
    if (TypeRef(arr_type).kind() == LogosType::Kind::Array && TypeRef(arr_type).elem())  elem = TypeRef(arr_type).elem();
    if ((TypeRef(arr_type).kind() == LogosType::Kind::Ptr ||
         TypeRef(arr_type).kind() == LogosType::Kind::Ref ||
         TypeRef(arr_type).kind() == LogosType::Kind::MutRef) && TypeRef(arr_type).pointee())
        elem = TypeRef(arr_type).pointee();

    return builder().index_read(std::move(recv), std::move(idx), elem);
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

    TypeRef elem_type = elems[0]->type;
    for (uint64_t i = 1; i < elems.size(); ++i) {
        auto t = elems[i]->type;
        if (TypeRef(t).kind() != LogosType::Kind::Error && TypeRef(elem_type).kind() != LogosType::Kind::Error) {
            if (!types_compatible(t, elem_type) && !types_compatible(elem_type, t)) {
                error(std::format("array literal: element {} has type {}, expected {}",
                      i, type_str(t), type_str(elem_type)));
            } else {
                // If the concrete element type is narrow and this element is IntLit, check range.
                if (TypeRef(t).kind() == LogosType::Kind::IntLit &&
                    TypeRef(elem_type).kind() != LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(elems[i]))
                        if (!intlit_fits(*v, TypeRef(elem_type).kind()))
                            error(std::format("array literal: element {}: value {} does not fit in {}",
                                  i, *v, type_str(elem_type)));
                // Check array literal elements against narrow nested array element types.
                if (TypeRef(elem_type).kind() == LogosType::Kind::Array && TypeRef(elem_type).elem() &&
                    TypeRef(t).kind() == LogosType::Kind::Array) {
                    auto vr = expr_ref_of(*elems[i]);
                    if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                        lir_view::EArrLitView al{vr};
                        for (uint64_t ei = 0; ei < al.count(); ++ei) {
                            auto el = al.elem(ei);
                            if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(el))
                                    if (!intlit_fits(*v, TypeRef(elem_type).elem().kind()))
                                        error(std::format("array literal: element {}: sub-element {}: value {} does not fit in {}",
                                              i, ei, *v, type_str(TypeRef(elem_type).elem())));
                        }
                    }
                }
                // Check tuple literal elements against narrow nested tuple element types.
                if (TypeRef(elem_type).kind() == LogosType::Kind::Tuple && TypeRef(t).kind() == LogosType::Kind::Tuple) {
                    auto vr = expr_ref_of(*elems[i]);
                    if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                        lir_view::ETupleLitView tl{vr};
                        uint64_t ei = 0;
                        tl.each_elem([&](lir_view::ExprRef el) {
                            if (ei >= TypeRef(elem_type).tuple_elems().size()) { ++ei; return; }
                            if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(el))
                                    if (TypeRef(elem_type).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(TypeRef(elem_type).tuple_elems()[ei]).kind()))
                                        error(std::format("array literal: element {}: tuple element {}: value {} does not fit in {}",
                                              i, ei, *v, type_str(TypeRef(elem_type).tuple_elems()[ei])));
                            if (TypeRef(elem_type).tuple_elems()[ei] && TypeRef(TypeRef(elem_type).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                                TypeRef(TypeRef(elem_type).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                                el.kind() == lir_schema::expr::Code::ArrLit) {
                                lir_view::EArrLitView ial{el};
                                for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                    auto iel = ial.elem(ii);
                                    if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(iel))
                                            if (!intlit_fits(*v, TypeRef(TypeRef(elem_type).tuple_elems()[ei]).elem().kind()))
                                                error(std::format("array literal: element {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                      i, ei, ii, *v, type_str(TypeRef(TypeRef(elem_type).tuple_elems()[ei]).elem())));
                                }
                            }
                            if (TypeRef(elem_type).tuple_elems()[ei] && TypeRef(TypeRef(elem_type).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                                el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                                el.kind() == lir_schema::expr::Code::TupleLit) {
                                lir_view::ETupleLitView itl{el};
                                uint64_t ii = 0;
                                itl.each_elem([&](lir_view::ExprRef iel) {
                                    if (ii >= TypeRef(TypeRef(elem_type).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                    if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(iel))
                                            if (TypeRef(TypeRef(elem_type).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(elem_type).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                                error(std::format("array literal: element {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      i, ei, ii, *v, type_str(TypeRef(TypeRef(elem_type).tuple_elems()[ei]).tuple_elems()[ii])));
                                    ++ii;
                                });
                            }
                            ++ei;
                        });
                    }
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
        TypeRef anchor = nullptr;
        for (size_t i = 1; i < elems.size() && !anchor; ++i) {
            TypeRef ti = elems[i]->type;
            if (TypeRef(ti).kind() != LogosType::Kind::IntLit &&
                !(TypeRef(ti).kind() == LogosType::Kind::Array && TypeRef(ti).elem() &&
                  TypeRef(ti).elem().kind() == LogosType::Kind::IntLit))
                anchor = ti;
        }
        if (anchor) {
            auto* e = elems[0];
            auto t0 = elems[0]->type;
            // Scalar IntLit at element 0.
            if (TypeRef(t0).kind() == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(e))
                    if (!intlit_fits(*v, TypeRef(anchor).kind()))
                        error(std::format("array literal: element 0: value {} does not fit in {}",
                              *v, type_str(anchor)));
            // Array literal at element 0 (e.g. [[1,200,3], concrete_arr]).
            if (TypeRef(anchor).kind() == LogosType::Kind::Array && TypeRef(anchor).elem() &&
                TypeRef(t0).kind() == LogosType::Kind::Array) {
                auto vr = expr_ref_of(*e);
                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView al{vr};
                    for (uint64_t ei = 0; ei < al.count(); ++ei) {
                        auto el = al.elem(ei);
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (!intlit_fits(*v, TypeRef(anchor).elem().kind()))
                                    error(std::format("array literal: element 0: sub-element {}: value {} does not fit in {}",
                                          ei, *v, type_str(TypeRef(anchor).elem())));
                    }
                }
            }
            // Tuple literal at element 0 (tuple elements, including nested array/tuple).
            if (TypeRef(anchor).kind() == LogosType::Kind::Tuple && TypeRef(t0).kind() == LogosType::Kind::Tuple) {
                auto vr = expr_ref_of(*e);
                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView tl{vr};
                    uint64_t ei = 0;
                    tl.each_elem([&](lir_view::ExprRef el) {
                        if (ei >= TypeRef(anchor).tuple_elems().size()) { ++ei; return; }
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (TypeRef(anchor).tuple_elems()[ei] && !intlit_fits(*v, TypeRef(TypeRef(anchor).tuple_elems()[ei]).kind()))
                                    error(std::format("array literal: element 0: tuple element {}: value {} does not fit in {}",
                                          ei, *v, type_str(TypeRef(anchor).tuple_elems()[ei])));
                        if (TypeRef(anchor).tuple_elems()[ei] && TypeRef(TypeRef(anchor).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(anchor).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                            el.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{el};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(anchor).tuple_elems()[ei]).elem().kind()))
                                            error(std::format("array literal: element 0: tuple element {}: array element {}: value {} does not fit in {}",
                                                  ei, ii, *v, type_str(TypeRef(TypeRef(anchor).tuple_elems()[ei]).elem())));
                            }
                        }
                        if (TypeRef(anchor).tuple_elems()[ei] && TypeRef(TypeRef(anchor).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                            el.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{el};
                            uint64_t ii = 0;
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii >= TypeRef(TypeRef(anchor).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (TypeRef(TypeRef(anchor).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(anchor).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                            error(std::format("array literal: element 0: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  ei, ii, *v, type_str(TypeRef(TypeRef(anchor).tuple_elems()[ei]).tuple_elems()[ii])));
                                ++ii;
                            });
                        }
                        ++ei;
                    });
                }
            }
        }
    }
    // For IntLit element type: upgrade to i64 if any value overflows i32.
    // Keep IntLit (don't collapse to i32) so that annotation-based coercion
    // ([i64; N] = [1, 2, 3]) can use types_compatible([IntLit;N], [i64;N]) → true.
    if (TypeRef(elem_type).kind() == LogosType::Kind::IntLit) {
        bool needs_i64 = false;
        for (const auto& elem : elems) {
            if (auto v = get_intlit_value(elem))
                if (*v > (int64_t)INT32_MAX || *v < (int64_t)INT32_MIN)
                    { needs_i64 = true; break; }
        }
        if (needs_i64) elem_type = prim(LogosType::Kind::I64);
        // else: leave as IntLit — mlir_gen will see the annotation type
    }

    auto ty = make_array(elem_type, elems.size());
    return builder().arr_lit(std::move(elems), ty);
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
    TypeRef iter_type = iter->type;

    // Only array/slice iteration supported for now.
    TypeRef elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
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

    TypeRef vec_t = make_generic_struct("Vec", {elem_type});

    std::string vec_var = "__lc_v_" + std::to_string(tmp_var_count_++);

    // SLet: let mut vec_var: Vec<T> = vec_new::<T>();
    // Use symbol_name (may include __g__... suffix for method-level generics).
    std::string vec_new_sym = vec_new_fi->symbol_name.empty() ? "vec_new"
                                                              : vec_new_fi->symbol_name;
    auto call_new = builder().call(vec_new_sym, {elem_type}, {}, vec_t);
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
    auto recv = builder().addr_of(vec_var, make_ptr(true, vec_t));
    std::vector<lir::LExprPtr> push_args;
    push_args.push_back(std::move(recv));
    push_args.push_back(std::move(elem_expr));
    auto push_call = builder().call("Vec__push", {elem_type}, std::move(push_args), void_t());

    lir::SExprStmt push_stmt;
    push_stmt.expr = std::move(push_call);

    auto loop_body = lir::alloc_block(*cur_prog_);
    if (guard_expr) {
        lir::SIf sif;
        sif.cond = std::move(guard_expr);
        sif.then_ = lir::alloc_block(*cur_prog_);
        sif.then_->stmts.push_back(make_stmt_emit(node_line_, std::move(push_stmt)));
        loop_body->stmts.push_back(make_stmt_emit(node_line_, std::move(sif)));
    } else {
        loop_body->stmts.push_back(make_stmt_emit(node_line_, std::move(push_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = std::move(loop_body);

    auto outer = lir::alloc_block(*cur_prog_);
    outer->stmts.push_back(make_stmt_emit(node_line_, std::move(let_v)));
    outer->stmts.push_back(make_stmt_emit(node_line_, std::move(sfe)));

    auto result = builder().var_ref(vec_var, vec_t);
    return builder().block_expr(std::move(outer), std::move(result), vec_t);
}

// Map comprehension:  {kexpr: vexpr for x in iter_expr (if guard)?}
// Desugars to a block that creates a HashMap<K,V>, iterates over iter_expr,
// optionally filters by guard, and inserts (kexpr, vexpr) pairs.
// Requires `use std.collections.hashmap;` in scope.
lir::LExprPtr SemaChecker::lower_map_comp(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();
    TypeRef iter_type = iter->type;

    TypeRef elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
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

    TypeRef k_type = key_expr_body->type;
    TypeRef v_type = val_expr_body->type;
    TypeRef hm_t = make_generic_struct("HashMap", {k_type, v_type});

    std::string hm_new_sym = hm_new_fi->symbol_name.empty() ? "hashmap_new"
                                                            : hm_new_fi->symbol_name;
    auto call_new = builder().call(hm_new_sym, {k_type, v_type}, {}, hm_t);
    lir::SLet let_m;
    let_m.name   = hm_var;
    let_m.type   = hm_t;
    let_m.is_mut = true;
    let_m.value  = std::move(call_new);

    // HashMap::insert(&mut hm, key, val) — unsafe method, emitted as direct ECall
    // "HashMap__insert" so mono_clone rewrites to HashMap$G1$..$G2$..__insert.
    auto recv = builder().addr_of(hm_var, make_ptr(true, hm_t));
    std::vector<lir::LExprPtr> ins_args;
    ins_args.push_back(std::move(recv));
    ins_args.push_back(std::move(key_expr_body));
    ins_args.push_back(std::move(val_expr_body));
    auto ins_call = builder().call("HashMap__insert", {k_type, v_type}, std::move(ins_args), void_t());

    lir::SExprStmt ins_stmt;
    ins_stmt.expr = std::move(ins_call);

    auto loop_body = lir::alloc_block(*cur_prog_);
    if (guard_body) {
        lir::SIf sif;
        sif.cond = std::move(guard_body);
        sif.then_ = lir::alloc_block(*cur_prog_);
        sif.then_->stmts.push_back(make_stmt_emit(node_line_, std::move(ins_stmt)));
        loop_body->stmts.push_back(make_stmt_emit(node_line_, std::move(sif)));
    } else {
        loop_body->stmts.push_back(make_stmt_emit(node_line_, std::move(ins_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = std::move(loop_body);

    auto outer = lir::alloc_block(*cur_prog_);
    outer->stmts.push_back(make_stmt_emit(node_line_, std::move(let_m)));
    outer->stmts.push_back(make_stmt_emit(node_line_, std::move(sfe)));

    auto result = builder().var_ref(hm_var, hm_t);
    return builder().block_expr(std::move(outer), std::move(result), hm_t);
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
    TypeRef iter_type = iter->type;

    // Short-circuit on upstream error to avoid cascading diagnostics.
    if (TypeRef(iter_type).kind() == LogosType::Kind::Error)
        return error_expr();

    TypeRef elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
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

    TypeRef ctr_t = make_struct_type("Hermes");

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
    if (!val_expr_body || TypeRef(val_expr_body->type).kind() == LogosType::Kind::Error)
        return error_expr();

    // Guard must be Bool; any other type (including Error) is rejected here to
    // avoid cascading diagnostics and to prevent an MLIR verification crash
    // from feeding a non-i1 value into cf.cond_br.
    if (guard_body) {
        auto gk = guard_body->type ? TypeRef(guard_body->type).kind()
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
    new_args.push_back(builder().lit_int(cap_hint, prim(LogosType::Kind::I64)));
    auto call_new = builder().call(new_sym, {}, std::move(new_args), ctr_t);
    lir::SLet let_c;
    let_c.name   = ctr_var;
    let_c.type   = ctr_t;
    let_c.is_mut = true;
    let_c.value  = std::move(call_new);

    // hermes_list_comp_push(&mut __hlc_c, val);
    std::string push_sym = push_fi->symbol_name.empty() ? "hermes_list_comp_push"
                                                        : push_fi->symbol_name;
    auto recv = builder().addr_of(ctr_var, make_ptr(true, ctr_t));
    std::vector<lir::LExprPtr> push_args;
    push_args.push_back(std::move(recv));
    push_args.push_back(std::move(val_expr_body));
    auto push_call = builder().call(push_sym, {}, std::move(push_args), void_t());

    lir::SExprStmt push_stmt;
    push_stmt.expr = std::move(push_call);

    auto loop_body = lir::alloc_block(*cur_prog_);
    if (guard_body) {
        lir::SIf sif;
        sif.cond = std::move(guard_body);
        sif.then_ = lir::alloc_block(*cur_prog_);
        sif.then_->stmts.push_back(make_stmt_emit(node_line_, std::move(push_stmt)));
        loop_body->stmts.push_back(make_stmt_emit(node_line_, std::move(sif)));
    } else {
        loop_body->stmts.push_back(make_stmt_emit(node_line_, std::move(push_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = std::move(loop_body);

    auto outer = lir::alloc_block(*cur_prog_);
    outer->stmts.push_back(make_stmt_emit(node_line_, std::move(let_c)));
    outer->stmts.push_back(make_stmt_emit(node_line_, std::move(sfe)));

    auto result = builder().var_ref(ctr_var, ctr_t);
    return builder().block_expr(std::move(outer), std::move(result), ctr_t);
}

// Hermes map comprehension:  @{kexpr: vexpr for x in iter (if guard)?}
// v1: string keys only (`str`); values must be AnyVal.
// Requires `use std.hermes.ctr;` in scope.
lir::LExprPtr SemaChecker::lower_hermes_map_comp(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();
    TypeRef iter_type = iter->type;

    // Short-circuit on upstream error to avoid cascading diagnostics.
    if (TypeRef(iter_type).kind() == LogosType::Kind::Error)
        return error_expr();

    TypeRef elem_type = nullptr;
    int64_t arr_size = 0;
    bool is_slice = false;
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        arr_size  = (int64_t)TypeRef(iter_type).arr_size();
    } else if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
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

    TypeRef ctr_t = make_struct_type("Hermes");

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
    TypeRef kt = key_expr->type;
    if (kt && TypeRef(kt).kind() == LogosType::Kind::Error)
        return error_expr();
    if (!(kt && TypeRef(kt).kind() == LogosType::Kind::Slice && TypeRef(kt).elem()
              && TypeRef(kt).elem().kind() == LogosType::Kind::U8)) {
        error(std::format(
            "hermes map comprehension: key expression must be str (got {})",
            type_str(kt)));
        return error_expr();
    }

    // Coerce VALUE to AnyVal (no-op if already AnyVal).
    val_expr = coerce_to_hermes_anyval(
        std::move(val_expr), ctr_var, ctr_t,
        "hermes map comprehension value");
    if (!val_expr || TypeRef(val_expr->type).kind() == LogosType::Kind::Error)
        return error_expr();

    // Guard must be Bool; reject anything else early to avoid MLIR crashes
    // (cf.cond_br requires i1) and to silence cascades when the guard errored.
    if (guard_body) {
        auto gk = guard_body->type ? TypeRef(guard_body->type).kind()
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
    new_args.push_back(builder().lit_int(cap_hint, prim(LogosType::Kind::I64)));
    new_args.push_back(builder().lit_int(slot_hint, prim(LogosType::Kind::I64)));
    auto call_new = builder().call(new_sym, {}, std::move(new_args), ctr_t);
    lir::SLet let_c;
    let_c.name   = ctr_var;
    let_c.type   = ctr_t;
    let_c.is_mut = true;
    let_c.value  = std::move(call_new);

    std::string put_sym = put_fi->symbol_name.empty() ? "hermes_map_comp_put"
                                                      : put_fi->symbol_name;
    auto recv = builder().addr_of(ctr_var, make_ptr(true, ctr_t));
    std::vector<lir::LExprPtr> put_args;
    put_args.push_back(std::move(recv));
    put_args.push_back(std::move(key_expr));
    put_args.push_back(std::move(val_expr));
    auto put_call = builder().call(put_sym, {}, std::move(put_args), void_t());

    lir::SExprStmt put_stmt;
    put_stmt.expr = std::move(put_call);

    auto loop_body = lir::alloc_block(*cur_prog_);
    if (guard_body) {
        lir::SIf sif;
        sif.cond = std::move(guard_body);
        sif.then_ = lir::alloc_block(*cur_prog_);
        sif.then_->stmts.push_back(make_stmt_emit(node_line_, std::move(put_stmt)));
        loop_body->stmts.push_back(make_stmt_emit(node_line_, std::move(sif)));
    } else {
        loop_body->stmts.push_back(make_stmt_emit(node_line_, std::move(put_stmt)));
    }

    lir::SForEach sfe;
    sfe.var       = std::string(var_name);
    sfe.iter      = std::move(iter);
    sfe.elem_type = elem_type;
    sfe.arr_size  = arr_size;
    sfe.is_slice  = is_slice;
    sfe.body      = std::move(loop_body);

    auto outer = lir::alloc_block(*cur_prog_);
    outer->stmts.push_back(make_stmt_emit(node_line_, std::move(let_c)));
    outer->stmts.push_back(make_stmt_emit(node_line_, std::move(sfe)));

    auto result = builder().var_ref(ctr_var, ctr_t);
    return builder().block_expr(std::move(outer), std::move(result), ctr_t);
}

// Coerce an arbitrary value to AnyVal for use inside a Hermes comprehension.
// Returns the original expr if already AnyVal; otherwise wraps in a call to
// one of the `hermes_coerce_*` helpers in hermes/ctr.logos. String coercion
// requires `&mut ctr_var` because the string is copied into the zone.
lir::LExprPtr SemaChecker::coerce_to_hermes_anyval(
        lir::LExprPtr val,
        const std::string& ctr_var,
        TypeRef ctr_t,
        std::string_view context) {
    if (!val || !val->type) return val;
    TypeRef t = val->type;

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
            if (TypeRef(t).elem() && TypeRef(t).elem().kind() == K::U8) {
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
    TypeRef ret_t = fi->ret_type;

    std::vector<lir::LExprPtr> args;
    if (needs_ctr) {
        auto recv = builder().addr_of(ctr_var, make_ptr(true, ctr_t));
        args.push_back(std::move(recv));
    }
    args.push_back(std::move(val));
    std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
    return builder().call(sym, {}, std::move(args), ret_t);
}

lir::LExprPtr SemaChecker::lower_arr_fill_lit(TinyMapView node) {
    auto val_node = map_of(node.get(la::VALUE.code));
    auto fill_val = lower_expr(val_node);
    auto sv = str_of(node.get(la::SIZE.code));
    int64_t n = parse_int_literal(sv);
    if (n <= 0) error(std::format("array fill literal: size must be positive, got {}", n));
    TypeRef elem_type = fill_val->type;
    // Keep IntLit unresolved so that struct-literal type inference (hint_struct_type_)
    // can widen the element to the correct concrete type (e.g. i64 for Vec<i64>).
    std::vector<lir::LExprPtr> elems;
    elems.push_back(std::move(fill_val));
    for (int64_t i = 1; i < n; ++i)
        elems.push_back(lower_expr(val_node));  // re-lower for each slot (simple literals)
    return builder().arr_lit(std::move(elems), make_array(elem_type, (size_t)n));
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
                if (cit->second.type) builder().retype_expr(val, cit->second.type);
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
    return builder().enum_lit(std::string(ename), std::string(vname), disc, make_enum_type(ename, epkg_el));
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
                if (cit->second.type) builder().retype_expr(val, cit->second.type);
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
            if (TypeRef(e->type).kind() == LogosType::Kind::Void) continue;  // skip () unit args
            payload.push_back(std::move(e));
        }
    }

    // Resolve payload types — substitute TypeVars if generic enum
    auto& einfo = eit->second;
    std::vector<TypeRef> resolved_payload_types = vinfo->payload_types;

    // Build the enum type (may be generic, e.g. Option<i32>)
    // For now, if the enum has type params, we need to infer them from payload types.
    // Simple inference: match payload args to payload type params.
    TypeRef result_type = make_enum_type(ename);
    if (!einfo.type_params.empty()) {
        // Build substitution from payload args
        SemaSubst subst;
        for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i) {
            auto pt = vinfo->payload_types[i];
            if (pt && TypeRef(pt).kind() == LogosType::Kind::TypeVar) {
                auto inferred = payload[i]->type;
                if (TypeRef(inferred).kind() == LogosType::Kind::IntLit) inferred = i32_t();
                subst[std::string(TypeRef(pt).type_var_name())] = inferred;
            }
        }
        // Fill any still-unresolved type params from hint (e.g. let e: Result<i32,i32> = Result::Err(-1))
        if (hint_enum_type_ && TypeRef(hint_enum_type_).enum_name() == std::string(ename)) {
            for (size_t i = 0; i < einfo.type_params.size() && i < TypeRef(hint_enum_type_).type_args().size(); ++i) {
                if (subst.find(einfo.type_params[i].name) == subst.end()) {
                    auto hta = TypeRef(hint_enum_type_).type_args()[i];
                    if (hta && TypeRef(hta).kind() != LogosType::Kind::Error)
                        subst[einfo.type_params[i].name] = hta;
                }
            }
        }
        // Build concrete type args
        std::vector<TypeRef> type_args;
        for (auto& tp : einfo.type_params) {
            auto sit = subst.find(tp.name);
            type_args.push_back(sit != subst.end() ? sit->second : error_t());
        }
        check_type_bounds(std::string(ename), einfo.type_params, type_args);
        LogosTypeBuilder et; et.kind = LogosType::Kind::Enum;
        et.enum_name = std::string(ename);
        et.type_args = std::move(type_args);
        result_type = pool_->alloc(std::move(et));
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
            if (TypeRef(payload[i]->type).kind() != LogosType::Kind::Error &&
                resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() != LogosType::Kind::Error &&
                !types_compatible(payload[i]->type, resolved_payload_types[i]))
                error(std::format("{}::{} arg {}: expected {}, got {}",
                      ename, vname, i, type_str(resolved_payload_types[i]),
                      type_str(payload[i]->type)));
            // Check IntLit payload value fits in the declared payload type.
            if (resolved_payload_types[i] && TypeRef(payload[i]->type).kind() == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(payload[i]))
                    if (!intlit_fits(*v, TypeRef(resolved_payload_types[i]).kind()))
                        error(std::format("{}::{} arg {}: value {} does not fit in {}",
                              ename, vname, i, *v, type_str(resolved_payload_types[i])));
            // Check array literal elements against narrow array payload type.
            if (resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() == LogosType::Kind::Array &&
                TypeRef(resolved_payload_types[i]).elem() &&
                TypeRef(payload[i]->type).kind() == LogosType::Kind::Array) {
                auto vr = expr_ref_of(*payload[i]);
                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView al{vr};
                    for (uint64_t ei = 0; ei < al.count(); ++ei) {
                        auto el = al.elem(ei);
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (!intlit_fits(*v, TypeRef(resolved_payload_types[i]).elem().kind()))
                                    error(std::format("{}::{} arg {}: array element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(TypeRef(resolved_payload_types[i]).elem())));
                    }
                }
            }
            // Check tuple literal elements against narrow tuple payload type.
            if (resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() == LogosType::Kind::Tuple &&
                TypeRef(payload[i]->type).kind() == LogosType::Kind::Tuple) {
                auto vr = expr_ref_of(*payload[i]);
                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView tl{vr};
                    uint64_t ei = 0;
                    tl.each_elem([&](lir_view::ExprRef el) {
                        if (ei >= TypeRef(resolved_payload_types[i]).tuple_elems().size()) { ++ei; return; }
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] &&
                                    !intlit_fits(*v, TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind()))
                                    error(std::format("{}::{} arg {}: tuple element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(TypeRef(resolved_payload_types[i]).tuple_elems()[ei])));
                        if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] && TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                            el.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{el};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem().kind()))
                                            error(std::format("{}::{} arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                  ename, vname, i, ei, ii, *v, type_str(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem())));
                            }
                        }
                        if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] && TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                            el.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{el};
                            uint64_t ii = 0;
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii >= TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                            error(std::format("{}::{} arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  ename, vname, i, ei, ii, *v, type_str(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii])));
                                ++ii;
                            });
                        }
                        ++ei;
                    });
                }
            }
        }
    } else {
        // Variadic variant: match each arg against the pack's type (if it's not a generic expansion itself).
        if (!resolved_payload_types.empty()) {
            auto pack_t = resolved_payload_types[0];
            for (size_t i = 0; i < payload.size(); ++i) {
                if (TypeRef(payload[i]->type).kind() != LogosType::Kind::Error &&
                    TypeRef(pack_t).kind() != LogosType::Kind::Error &&
                    !types_compatible(payload[i]->type, pack_t))
                    error(std::format("{}::{} variadic arg {}: expected {}, got {}",
                          ename, vname, i, type_str(pack_t), type_str(payload[i]->type)));
            }
        }
    }

    return builder().enum_lit_data(std::string(ename), std::string(vname), vinfo->value, std::move(payload), result_type);
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
    std::vector<TypeRef> resolved_payload_types = vinfo->payload_types;
    TypeRef result_type = make_enum_type(ename);
    if (!einfo.type_params.empty()) {
        SemaSubst subst;
        for (size_t i = 0; i < vinfo->payload_types.size() && i < payload.size(); ++i) {
            auto pt = vinfo->payload_types[i];
            if (pt && TypeRef(pt).kind() == LogosType::Kind::TypeVar) {
                auto inferred = payload[i]->type;
                if (TypeRef(inferred).kind() == LogosType::Kind::IntLit) inferred = i32_t();
                subst[std::string(TypeRef(pt).type_var_name())] = inferred;
            }
        }
        // Fill any still-unresolved type params from hint
        if (hint_enum_type_ && TypeRef(hint_enum_type_).enum_name() == std::string(ename)) {
            for (size_t i = 0; i < einfo.type_params.size() && i < TypeRef(hint_enum_type_).type_args().size(); ++i) {
                if (subst.find(einfo.type_params[i].name) == subst.end()) {
                    auto hta = TypeRef(hint_enum_type_).type_args()[i];
                    if (hta && TypeRef(hta).kind() != LogosType::Kind::Error)
                        subst[einfo.type_params[i].name] = hta;
                }
            }
        }
        std::vector<TypeRef> type_args;
        for (auto& tp : einfo.type_params) {
            auto sit = subst.find(tp.name);
            type_args.push_back(sit != subst.end() ? sit->second : error_t());
        }
        check_type_bounds(std::string(ename), einfo.type_params, type_args);
        LogosTypeBuilder et; et.kind = LogosType::Kind::Enum;
        et.enum_name = std::string(ename);
        et.type_args = std::move(type_args);
        result_type = pool_->alloc(std::move(et));
        for (size_t i = 0; i < resolved_payload_types.size(); ++i)
            resolved_payload_types[i] = subst_type_sema(resolved_payload_types[i], subst);
    }
    if (!vinfo->is_variadic && payload.size() != vinfo->payload_types.size()) {
        error(std::format("{}::{} expects {} args, got {}",
              ename, vname, vinfo->payload_types.size(), payload.size()));
    } else if (!vinfo->is_variadic) {
        for (size_t i = 0; i < payload.size(); ++i) {
            if (TypeRef(payload[i]->type).kind() != LogosType::Kind::Error &&
                resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() != LogosType::Kind::Error &&
                !types_compatible(payload[i]->type, resolved_payload_types[i]))
                error(std::format("{}::{} arg {}: expected {}, got {}",
                      ename, vname, i, type_str(resolved_payload_types[i]),
                      type_str(payload[i]->type)));
            if (resolved_payload_types[i] &&
                TypeRef(payload[i]->type).kind() == LogosType::Kind::IntLit)
                if (auto v = get_intlit_value(payload[i]))
                    if (!intlit_fits(*v, TypeRef(resolved_payload_types[i]).kind()))
                        error(std::format("{}::{} arg {}: value {} does not fit in {}",
                              ename, vname, i, *v,
                              type_str(resolved_payload_types[i])));
            // Check array literal elements against narrow array payload type.
            if (resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() == LogosType::Kind::Array &&
                TypeRef(resolved_payload_types[i]).elem() &&
                TypeRef(payload[i]->type).kind() == LogosType::Kind::Array) {
                auto vr = expr_ref_of(*payload[i]);
                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView al{vr};
                    for (uint64_t ei = 0; ei < al.count(); ++ei) {
                        auto el = al.elem(ei);
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (!intlit_fits(*v, TypeRef(resolved_payload_types[i]).elem().kind()))
                                    error(std::format("{}::{} arg {}: array element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(TypeRef(resolved_payload_types[i]).elem())));
                    }
                }
            }
            // Check tuple literal elements against narrow tuple payload type.
            if (resolved_payload_types[i] &&
                TypeRef(resolved_payload_types[i]).kind() == LogosType::Kind::Tuple &&
                TypeRef(payload[i]->type).kind() == LogosType::Kind::Tuple) {
                auto vr = expr_ref_of(*payload[i]);
                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView tl{vr};
                    uint64_t ei = 0;
                    tl.each_elem([&](lir_view::ExprRef el) {
                        if (ei >= TypeRef(resolved_payload_types[i]).tuple_elems().size()) { ++ei; return; }
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] &&
                                    !intlit_fits(*v, TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind()))
                                    error(std::format("{}::{} arg {}: tuple element {}: value {} does not fit in {}",
                                          ename, vname, i, ei, *v, type_str(TypeRef(resolved_payload_types[i]).tuple_elems()[ei])));
                        if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] && TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                            el.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{el};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem().kind()))
                                            error(std::format("{}::{} arg {}: tuple element {}: array element {}: value {} does not fit in {}",
                                                  ename, vname, i, ei, ii, *v, type_str(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).elem())));
                            }
                        }
                        if (TypeRef(resolved_payload_types[i]).tuple_elems()[ei] && TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                            el.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{el};
                            uint64_t ii = 0;
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii >= TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems().size()) { ++ii; return; }
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                            error(std::format("{}::{} arg {}: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  ename, vname, i, ei, ii, *v, type_str(TypeRef(TypeRef(resolved_payload_types[i]).tuple_elems()[ei]).tuple_elems()[ii])));
                                ++ii;
                            });
                        }
                        ++ei;
                    });
                }
            }
        }
    } else {
        if (!resolved_payload_types.empty()) {
            auto pack_t = resolved_payload_types[0];
            for (size_t i = 0; i < payload.size(); ++i) {
                if (TypeRef(payload[i]->type).kind() != LogosType::Kind::Error &&
                    TypeRef(pack_t).kind() != LogosType::Kind::Error &&
                    !types_compatible(payload[i]->type, pack_t))
                    error(std::format("{}::{} variadic arg {}: expected {}, got {}",
                          ename, vname, i, type_str(pack_t), type_str(payload[i]->type)));
                if (TypeRef(pack_t).kind() != LogosType::Kind::Error &&
                    TypeRef(payload[i]->type).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(payload[i]))
                        if (!intlit_fits(*v, TypeRef(pack_t).kind()))
                            error(std::format("{}::{} variadic arg {}: value {} does not fit in {}",
                                  ename, vname, i, *v, type_str(pack_t)));
            }
        }
    }
    return builder().enum_lit_data(std::string(ename), std::string(vname), vinfo->value, std::move(payload), result_type);
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
            auto aliased = ait->second.type;
            if (aliased && (TypeRef(aliased).kind() == LogosType::Kind::Struct ||
                            TypeRef(aliased).kind() == LogosType::Kind::ZonedStruct)) {
                resolved_class = TypeRef(aliased).type_args().empty()
                    ? TypeRef(aliased).struct_name().to_string()
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
        std::vector<TypeRef> arg_types;
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
                std::vector<TypeRef> tf_args;
                bool all_concrete = true;
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto t = resolve_type(map_of(items.get(i)));
                    if (!t || TypeRef(t).kind() == LogosType::Kind::TypeVar) { all_concrete = false; break; }
                    tf_args.push_back(t);
                }
                if (all_concrete && !tf_args.empty()) {
                    bool is_zoned = datatypes_.count(resolved_class) > 0;
                    TypeRef concrete_t = is_zoned
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
                if (cit->second.type) builder().retype_expr(val, cit->second.type);
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
                          (TypeRef(m.param_types[0]).kind() == LogosType::Kind::TypeVar &&
                           TypeRef(m.param_types[0]).type_var_name() == "Self"));
                    if (!is_static) continue;
                    if (m.is_unsafe && !inside_unsafe_)
                        error(std::format("call to unsafe method '{}' requires unsafe context", mname_str));
                    // Substitute Self → TypeVar(DT) in return type so that
                    // Self::Storage becomes AssocType with base = TypeVar(DT).
                    SemaSubst self_subst;
                    self_subst["Self"] = current_type_params_.count(cname_str)
                        ? current_type_params_[cname_str] : make_typevar(cname_str);
                    TypeRef ret_t = subst_type_sema(m.ret_type, self_subst);
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
                    return builder().call(mfi && !mfi->symbol_name.empty()
                                ? mfi->symbol_name
                                : cname_str + "__" + mname_str, {}, std::move(arg_exprs), ret_t);
                }
            }
        }
        error(std::format("call to undefined static method '{}::{}'", class_name, method_name));
        return builder().call(mangled, {}, std::move(arg_exprs), error_t());
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
            auto t = a->type;
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
                    auto t = resolve_type(map_of(items.get(i)));
                    if (t && (TypeRef(t).kind() == LogosType::Kind::TypeVar ||
                              TypeRef(t).kind() == LogosType::Kind::AssocType)) {
                        in_generic_context = true; break;
                    }
                }
            }
        }
        if (!in_generic_context) {
            std::vector<TypeRef> inferred;
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
            std::vector<TypeRef> type_var_args;
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
            TypeRef ret = subst_type_sema(fi.ret_type, subst);
            return builder().call(fi.symbol_name.empty() ? mangled : fi.symbol_name, std::move(type_var_args), std::move(arg_exprs), ret);
        }
    }

    uint64_t n_args = arg_exprs.size();
    if (n_args != fi.param_types.size()) {
        error(std::format("static call '{}': expected {} args, got {}",
              mangled, fi.param_types.size(), n_args));
    } else {
        for (uint64_t i = 0; i < n_args; ++i) {
            widen_int_expr(arg_exprs[i], fi.param_types[i], builder());
            auto at = arg_exprs[i]->type;
            auto pt = fi.param_types[i];
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
            auto er = expr_ref_of(*a);
            if (er.kind() == lir_schema::expr::Code::VarRef)
                mark_moved(std::string(lir_view::EVarRefView{er}.name()));
        }
    }

    return builder().call(fi.symbol_name.empty() ? mangled : fi.symbol_name, {}, std::move(arg_exprs), fi.ret_type);
}

lir::LExprPtr SemaChecker::lower_if_expr(TinyMapView node) {
    lir::LExprPtr cond = nullptr;
    if (node.has_key(la::COND)) {
        cond = lower_expr(map_of(node.get(la::COND.code)));
        if (TypeRef(cond->type).kind() != LogosType::Kind::Bool &&
            TypeRef(cond->type).kind() != LogosType::Kind::Error)
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
        auto block = lir::alloc_block(*cur_prog_);
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
        if (!result) return builder().block_expr(std::move(block), nullptr, void_t());
        TypeRef rt = result->type;
        return builder().block_expr(std::move(block), std::move(result), rt);
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
    TypeRef result_type = then_val->type;
    if (TypeRef(then_val->type).kind() == LogosType::Kind::Error)
        result_type = else_val->type;
    else if (TypeRef(else_val->type).kind() != LogosType::Kind::Error) {
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
        auto intlit_overflow = [this](const lir::LExpr* e) -> bool {
            if (!e) return false;
            auto er = expr_ref_of(*e);
            if (er.kind() == lir_schema::expr::Code::BlockExpr)
                er = lir_view::EBlockExprView{er}.result();
            if (er.kind() != lir_schema::expr::Code::LitInt) return false;
            int64_t v = lir_view::ELitIntView{er}.value();
            return v > (int64_t)INT32_MAX || v < (int64_t)INT32_MIN;
        };
        if (intlit_overflow(then_val) || intlit_overflow(else_val))
            result_type = prim(LogosType::Kind::I64);
    }

    lir::EIfExpr eif;
    eif.cond      = std::move(cond);
    eif.then_val  = std::move(then_val);
    eif.else_val  = std::move(else_val);
    return builder().if_expr_v(std::move(eif), result_type);
}

lir::LExprPtr SemaChecker::lower_closure_expr(TinyMapView node) {
    auto closure_id = "__closure_" + std::to_string(closure_counter_++);
    bool is_move = node.has_key(la::IS_MOVE) &&
                   !node.get(la::IS_MOVE.code).is_null() &&
                   node.get(la::IS_MOVE.code).as_value<int32_t>() != 0;

    // Parse parameters
    std::vector<lir::LParam> params;
    std::vector<TypeRef> param_types;
    if (node.has_key(la::PARAMS)) {
        AnyVal pav = node.get(la::PARAMS.code);
        if (!pav.is_null() && pav.is_pointer()) {
            auto plist = map_of(pav);
            if (plist.has_key(la::ITEMS)) {
                auto pitems = arr_of(plist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < pitems.size(); ++i) {
                    auto p = map_of(pitems.get(i));
                    auto pname = std::string(str_of(p.get(la::NAME.code)));
                    TypeRef ptype = p.has_key(la::TYPE)
                        ? resolve_type(map_of(p.get(la::TYPE.code))) : error_t();
                    params.push_back({pname, ptype});
                    param_types.push_back(ptype);
                }
            }
        }
    }

    TypeRef ret_type = node.has_key(la::RET_TYPE)
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
    std::vector<TypeRef> capture_types;
    StrSet seen;
    auto add_capture = [&](const std::string& name) {
        if (param_names.count(name) || seen.count(name))
            return;
        auto t = lookup(name);
        if (!t)
            return;
        captures.push_back(name);
        capture_types.push_back(t);
        seen.insert(name);
    };

    // View-based capture scanner. The closure body was just lowered through
    // the builder, so every LExpr / LStmt / LBlock has its mirror eager-emitted
    // — `expr_ref_of` / `stmt_ref_of` / a fresh BlockRef from the LBlock's
    // mirror_offset_ all yield non-null views.
    using EC = lir_schema::expr::Code;
    using SC = lir_schema::stmt::Code;
    std::function<void(lir_view::BlockRef)> scan_block_v;
    std::function<void(lir_view::StmtRef)>  scan_stmt_v;
    std::function<void(lir_view::ExprRef)>  scan_captures_v;
    scan_captures_v = [&](lir_view::ExprRef e) {
        if (!e) return;
        auto k = e.kind();
        if (k == EC::VarRef) {
            add_capture(std::string(lir_view::EVarRefView{e}.name()));
            return;
        }
        switch (k) {
            case EC::BinOp: {
                auto v = lir_view::EBinOpView{e};
                scan_captures_v(v.lhs()); scan_captures_v(v.rhs()); break;
            }
            case EC::Unary:        scan_captures_v(lir_view::EUnaryView{e}.operand()); break;
            case EC::Call: {
                lir_view::ECallView{e}.each_arg([&](lir_view::ExprRef a){ scan_captures_v(a); });
                break;
            }
            case EC::MethodCall: {
                auto v = lir_view::EMethodCallView{e};
                scan_captures_v(v.receiver());
                v.each_arg([&](lir_view::ExprRef a){ scan_captures_v(a); });
                break;
            }
            case EC::FieldRead:    scan_captures_v(lir_view::EFieldReadView{e}.receiver()); break;
            case EC::IndexRead: {
                auto v = lir_view::EIndexReadView{e};
                scan_captures_v(v.receiver()); scan_captures_v(v.index()); break;
            }
            case EC::Deref:        scan_captures_v(lir_view::EDerefView{e}.operand()); break;
            case EC::Cast:         scan_captures_v(lir_view::ECastView{e}.operand()); break;
            case EC::EnumLitData:
                lir_view::EEnumLitDataView{e}.each_payload([&](lir_view::ExprRef p){ scan_captures_v(p); });
                break;
            case EC::StructLit:
                lir_view::EStructLitView{e}.each_field_value([&](lir_view::ExprRef f){ scan_captures_v(f); });
                break;
            case EC::ArrLit:
                lir_view::EArrLitView{e}.each_elem([&](lir_view::ExprRef el){ scan_captures_v(el); });
                break;
            case EC::New:
                lir_view::ENewView{e}.each_field_value([&](lir_view::ExprRef f){ scan_captures_v(f); });
                break;
            case EC::IfExpr: {
                auto v = lir_view::EIfExprView{e};
                scan_captures_v(v.cond()); scan_captures_v(v.then_val()); scan_captures_v(v.else_val()); break;
            }
            case EC::MatchExpr: {
                auto v = lir_view::EMatchExprView{e};
                scan_captures_v(v.scrut());
                v.each_arm([&](lir_view::EMatchArmRef arm){
                    if (auto g = arm.guard()) scan_captures_v(g);
                    scan_captures_v(arm.value());
                });
                break;
            }
            case EC::TupleLit:
                lir_view::ETupleLitView{e}.each_elem([&](lir_view::ExprRef el){ scan_captures_v(el); });
                break;
            case EC::TupleIndex:   scan_captures_v(lir_view::ETupleIndexView{e}.receiver()); break;
            case EC::SliceLit: {
                auto v = lir_view::ESliceLitView{e};
                scan_captures_v(v.base()); scan_captures_v(v.len()); break;
            }
            case EC::SliceIndex: {
                auto v = lir_view::ESliceIndexView{e};
                scan_captures_v(v.slice()); scan_captures_v(v.index()); break;
            }
            case EC::SliceLen:     scan_captures_v(lir_view::ESliceLenView{e}.slice()); break;
            case EC::SlicePtr:     scan_captures_v(lir_view::ESlicePtrView{e}.slice()); break;
            case EC::ClosureBox:
                lir_view::EClosureBoxView{e}.each_capture_name([&](std::string_view n){
                    add_capture(std::string(n));
                });
                break;
            case EC::ClosureCall: {
                auto v = lir_view::EClosureCallView{e};
                scan_captures_v(v.callee());
                v.each_arg([&](lir_view::ExprRef a){ scan_captures_v(a); });
                break;
            }
            case EC::FormatCall: {
                auto v = lir_view::EFormatCallView{e};
                scan_captures_v(v.fmt());
                v.each_arg([&](lir_view::ExprRef a){ scan_captures_v(a); });
                break;
            }
            case EC::Try:          scan_captures_v(lir_view::ETryView{e}.inner()); break;
            case EC::BlockExpr: {
                auto v = lir_view::EBlockExprView{e};
                if (auto b = v.block()) scan_block_v(b);
                scan_captures_v(v.result()); break;
            }
            case EC::HermesLit:
                // C2 bug fix: scan capture_exprs so closures that use $-captures
                // correctly include those outer variables in their capture list.
                lir_view::EHermesLitView{e}.each_capture_expr([&](lir_view::ExprRef ce){
                    scan_captures_v(ce);
                });
                break;
            default: break;
        }
    };
    scan_block_v = [&](lir_view::BlockRef b) {
        if (!b) return;
        b.each_stmt([&](lir_view::StmtRef s){ scan_stmt_v(s); });
    };
    scan_stmt_v = [&](lir_view::StmtRef s) {
        if (!s) return;
        switch (s.kind()) {
            case SC::Let:        scan_captures_v(lir_view::SLetView{s}.value()); break;
            case SC::Assign:     scan_captures_v(lir_view::SAssignView{s}.value()); break;
            case SC::Return:     scan_captures_v(lir_view::SReturnView{s}.value()); break;
            case SC::If: {
                auto v = lir_view::SIfView{s};
                scan_captures_v(v.cond());
                scan_block_v(v.then_block());
                scan_block_v(v.else_block());
                break;
            }
            case SC::While: {
                auto v = lir_view::SWhileView{s};
                scan_captures_v(v.cond()); scan_block_v(v.body()); break;
            }
            case SC::For: {
                auto v = lir_view::SForView{s};
                scan_captures_v(v.lo()); scan_captures_v(v.hi()); scan_block_v(v.body()); break;
            }
            case SC::Loop:       scan_block_v(lir_view::SLoopView{s}.body()); break;
            case SC::Break:      scan_captures_v(lir_view::SBreakView{s}.value()); break;
            case SC::Block:      scan_block_v(lir_view::SBlockView{s}.body()); break;
            case SC::FieldWrite: scan_captures_v(lir_view::SFieldWriteView{s}.value()); break;
            case SC::IndexWrite: {
                auto v = lir_view::SIndexWriteView{s};
                scan_captures_v(v.index()); scan_captures_v(v.value()); break;
            }
            case SC::FieldIndexWrite: {
                auto v = lir_view::SFieldIndexWriteView{s};
                scan_captures_v(v.index()); scan_captures_v(v.value()); break;
            }
            case SC::ExprStmt:   scan_captures_v(lir_view::SExprStmtView{s}.expr()); break;
            case SC::Match: {
                auto v = lir_view::SMatchView{s};
                scan_captures_v(v.scrut());
                v.each_arm([&](lir_view::EMatchArmRef arm){
                    if (auto g = arm.guard()) scan_captures_v(g);
                    if (auto b = arm.body()) scan_block_v(b);
                });
                break;
            }
            case SC::Delete:     scan_captures_v(lir_view::SDeleteView{s}.expr()); break;
            case SC::ForEach: {
                auto v = lir_view::SForEachView{s};
                scan_captures_v(v.iter()); scan_block_v(v.body()); break;
            }
            case SC::DerefWrite: {
                auto v = lir_view::SDerefWriteView{s};
                scan_captures_v(v.ptr()); scan_captures_v(v.value()); break;
            }
            case SC::DerefFieldWrite: scan_captures_v(lir_view::SDerefFieldWriteView{s}.value()); break;
            case SC::TupleWrite:      scan_captures_v(lir_view::STupleWriteView{s}.value()); break;
            default: break;
        }
    };
    auto ec = lir::alloc_closure(*cur_prog_);
    ec->closure_id    = closure_id;
    ec->params        = std::move(params);
    ec->ret_type      = ret_type;
    ec->body          = std::move(body);
    ec->is_move       = is_move;

    {
        // Scan ec->body's mirror after the move so &ec->body is the stable
        // address registered in the mirror table (not the local `body`
        // which std::move just invalidated). The scanner pushes into the
        // local `captures` / `capture_types`, so they must still own data
        // here — moves into ec happen below.
        if (ec->body.mirror_offset_ == hermes::arena_offset_t{})
            lir_mirror_emit_block_node(*cur_prog_, ec->body);
        lir_view::BlockRef br{cur_prog_->type_pool.arena(), ec->body.mirror_offset_};
        scan_block_v(br);
    }
    ec->captures      = std::move(captures);
    ec->capture_types = std::move(capture_types);

    if (is_move) {
        for (size_t i = 0; i < ec->captures.size(); ++i) {
            if (is_move_type(ec->capture_types[i]))
                mark_moved(ec->captures[i]);
        }
    }

    auto ctype = make_closure_type(std::move(param_types), ret_type);
    return builder().closure_box(std::move(ec), ctype);
}


// ---------------------------------------------------------------------------
// Hermes SDN literal lowering
// ---------------------------------------------------------------------------

lir::HermesValPtr SemaChecker::lower_hermes_val(TinyMapView node) {
    int32_t c = code_of(node);

    if (c == la::HERMES_NEG_INT.code) {
        auto sv = str_of(node.get(la::VALUE.code));
        int64_t v = std::stoll(std::string(sv));
        return alloc_hv_emit(lir::HVInt{-v});
    }

    if (c == la::HERMES_NULL.code)
        return alloc_hv_emit(lir::HVNull{});

    if (c == la::HERMES_BOOL.code) {
        AnyVal av = node.get(la::VALUE.code);
        bool v = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        return alloc_hv_emit(lir::HVBool{v});
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
        return alloc_hv_emit(lir::HVInt{v});
    }

    if (c == la::HERMES_FLOAT.code) {
        auto sv = str_of(node.get(la::VALUE.code));
        std::string s(sv);
        // strip f32/f64 suffix
        if (s.size() > 3 && (s.substr(s.size()-3) == "f32" || s.substr(s.size()-3) == "f64"))
            s = s.substr(0, s.size()-3);
        double v = std::stod(s);
        return alloc_hv_emit(lir::HVFloat{v});
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
        return alloc_hv_emit(lir::HVStr{std::move(out)});
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
        return alloc_hv_emit(std::move(m));
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
        return alloc_hv_emit(std::move(a));
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
                    lir_view::HermesValRef hvref(cur_prog_->type_pool.arena(), hv->mirror_offset_);
                    if (hvref && hvref.kind() == lir_schema::hermes_val::Code::Int) {
                        int64_t hv_val = lir_view::HVIntView{hvref}.value();
                        if (hv_val < -2147483648LL || hv_val > 2147483647LL) {
                            error(std::format(
                                "@<I32> element [{}] value {} is out of i32 range [-2147483648, 2147483647]",
                                i, hv_val));
                            return nullptr;
                        }
                    }
                }
                a.elements.push_back(std::move(hv));
            }
        }
        return alloc_hv_emit(std::move(a));
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
        return alloc_hv_emit(std::move(m));
    }

    if (c == la::HERMES_CAP_IDENT.code || c == la::HERMES_CAP_EXPR.code) {
        if (!hermes_cap_ctx_) {
            error("$-capture used outside of a capturable @-literal context");
            return nullptr;
        }
        // Resolve the captured Logos expression.
        auto is_capturable = [&](TypeRef t) -> bool {
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
                    return TypeRef(t).pointee() && TypeRef(t).pointee().kind() == K::U8;
                // str (&[u8] slice) captured as varchar — same as *const u8 but with length.
                case K::Slice:
                    return TypeRef(t).elem() && TypeRef(t).elem().kind() == K::U8;
                default:
                    return false;
            }
        };

        if (c == la::HERMES_CAP_IDENT.code) {
            auto name_sv = str_of(node.get(la::NAME.code));
            std::string name(name_sv);
            auto var_type = lookup(name);
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
                hermes_cap_ctx_->exprs.push_back(builder().var_ref(name, var_type));
                hermes_cap_ctx_->types.push_back(var_type);
                hermes_cap_ctx_->ident_dedup[name] = value_idx;
            }
            uint32_t param_idx = hermes_cap_ctx_->next_slot++;
            return alloc_hv_emit(lir::HVCapture{param_idx, value_idx});
        } else {
            // HERMES_CAP_EXPR: ${expr} — always fresh (no dedup: may have side effects).
            auto expr_node = map_of(node.get(la::VALUE.code));
            auto cap_expr = lower_expr(expr_node);
            if (!cap_expr) return nullptr;
            auto expr_type = cap_expr->type;
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
            return alloc_hv_emit(lir::HVCapture{param_idx, value_idx});
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
    auto result_type = lit.has_captures ? make_struct_type("Hermes") : make_struct_type("HermesStatic");
    return builder().hermes_lit_v(std::move(lit), result_type);
}

// QUOTE_ITEM — `quote_item! { item* }`. Slice 5a.1 of metaprog-quote.
//
// Sema builds a fresh Hermes document representing a synthetic
// `package main; <items>;` module by deep-cloning each parsed item
// AnyVal into the new arena. For items that carry a `#name` antiquot
// at the struct-name position (Slice 5a.0 grammar: `NAME_VAR` field
// holding the var-name string), the dst-side TOM has its `NAME_VAR`
// rewritten to a u32 placeholder index — the host shim
// `logos_emit_item_blob_subst` substitutes the actual name from the
// caller-supplied idents-array at splice time.
//
// The result is a `QuoteItemBlob` struct value:
//   { template_ptr, template_size, idents_ptr, idents_count }
// constructed via a block expression that allocates a stack-local
// `[Ident; N]` array populated from the antiquot var refs. For a quote
// without `#name` placeholders, idents_count is 0 and idents_ptr is null.
lir::LExprPtr SemaChecker::lower_quote_item(TinyMapView node) {
    using logos::hermes::HermesAccess;
    using logos::hermes::ObjectArray;
    using logos::hermes::ArenaString;
    using logos::hermes::TinyObjectMap;
    using logos::hermes::arena_offset_t;
    using logos::hermes::make_doc;
    using logos::hermes::clone;
    using logos::hermes::copy_object_into;

    if (!holder_) {
        error("quote_item!: missing AST holder");
        return error_expr();
    }
    const uint8_t* src_base = holder_->base();

    // ITEMS is the parsed item array (may be empty for `quote_item! {}`).
    ArrayView src_items;
    if (node.has_key(la::ITEMS) && !node.get(la::ITEMS.code).is_null()) {
        src_items = arr_of(node.get(la::ITEMS.code));
    }

    // ── Pre-scan: collect placeholders, type-check `#name` vars ──────
    struct Placeholder { uint64_t item_idx; std::string var_name; };
    std::vector<Placeholder> placeholders;
    TypeRef ident_t = make_struct_type("Ident");
    auto is_ident_type = [](TypeRef t) {
        return TypeRef(t).kind() == LogosType::Kind::Struct
            && TypeRef(t).struct_name() == "Ident";
    };
    if (!src_items.is_null()) {
        for (uint64_t i = 0; i < src_items.size(); ++i) {
            AnyVal it_av = src_items.get(i);
            if (it_av.is_null()) continue;
            const auto* tom = reinterpret_cast<const TinyObjectMap*>(
                src_base + it_av.to_offset().value());
            if (!tom->has_key(la::NAME_VAR.code)) continue;
            AnyVal nv_av = tom->get(la::NAME_VAR.code,
                                    const_cast<uint8_t*>(src_base));
            if (nv_av.is_null() || !nv_av.is_pointer()) {
                error("quote_item!: NAME_VAR is not a string offset (parser bug?)");
                return error_expr();
            }
            const auto* nv_str = reinterpret_cast<const ArenaString*>(
                src_base + nv_av.to_offset().value());
            std::string vname(nv_str->view());
            TypeRef vt = lookup(vname);
            if (!vt) {
                error("quote_item!: `#" + vname + "` — variable not in scope");
                return error_expr();
            }
            if (!is_ident_type(vt)) {
                error("quote_item!: `#" + vname + "` — expected Ident");
                return error_expr();
            }
            placeholders.push_back({i, std::move(vname)});
        }
    }

    auto doc_e = make_doc(8192);
    if (!doc_e) {
        error("quote_item!: make_doc failed");
        return error_expr();
    }
    auto doc = std::move(doc_e).get();
    auto& dst_arena = HermesAccess::arena(doc);

    // Build ITEMS array in dst, deep-copying each item AnyVal.
    auto items_arr_e = ObjectArray::create(dst_arena,
            std::max<uint64_t>(4, src_items.is_null() ? 0 : src_items.size()));
    if (!items_arr_e) {
        error("quote_item!: items array alloc failed");
        return error_expr();
    }
    uint32_t items_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(items_arr_e.get()) - HermesAccess::base(doc));

    if (!src_items.is_null()) {
        // Helper: find the placeholder index for source item i (or -1).
        auto find_ph = [&](uint64_t i) -> int32_t {
            for (size_t k = 0; k < placeholders.size(); ++k) {
                if (placeholders[k].item_idx == i)
                    return static_cast<int32_t>(k);
            }
            return -1;
        };
        for (uint64_t i = 0; i < src_items.size(); ++i) {
            AnyVal it_av = src_items.get(i);
            if (it_av.is_null()) continue;
            // Item AnyVal is a pointer-into-src arena (TinyObjectMap).
            const void* src_obj = src_base + it_av.to_offset().value();
            auto cp_e = copy_object_into(src_obj, src_base, doc);
            if (!cp_e) {
                error("quote_item!: copy_object_into failed");
                return error_expr();
            }
            void* dst_obj = cp_e.get();
            uint32_t dst_off = static_cast<uint32_t>(
                reinterpret_cast<uint8_t*>(dst_obj) - HermesAccess::base(doc));

            // For placeholder items, rewrite NAME_VAR(string) → NAME_VAR(int idx).
            int32_t ph_idx = find_ph(i);
            if (ph_idx >= 0) {
                auto* dst_tom = reinterpret_cast<TinyObjectMap*>(
                    HermesAccess::base(doc) + dst_off);
                (void)dst_tom->put(la::NAME_VAR.code,
                    AnyVal::from_value<int32_t>(ph_idx), dst_arena);
            }
            // Rebase items_arr each push: the arena may have grown and
            // invalidated cached pointers.
            auto* items_arr = reinterpret_cast<ObjectArray*>(
                HermesAccess::base(doc) + items_off);
            (void)items_arr->push_back(AnyVal::from_offset(arena_offset_t(dst_off)),
                                       dst_arena);
        }
    }

    // Empty PATH_PARTS and USES arrays. Snapshot offsets between
    // allocations because the arena may rebase across grows.
    auto paths_e = ObjectArray::create(dst_arena, 4);
    if (!paths_e) {
        error("quote_item!: aux array alloc failed");
        return error_expr();
    }
    uint32_t paths_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(paths_e.get()) - HermesAccess::base(doc));

    auto uses_e  = ObjectArray::create(dst_arena, 4);
    if (!uses_e) {
        error("quote_item!: aux array alloc failed");
        return error_expr();
    }
    uint32_t uses_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(uses_e.get()) - HermesAccess::base(doc));

    // NAME = "main".
    auto name_e = ArenaString::create(dst_arena, std::string_view("main"));
    if (!name_e) {
        error("quote_item!: name alloc failed");
        return error_expr();
    }
    uint32_t name_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(name_e.get()) - HermesAccess::base(doc));

    // Root TinyObjectMap — fields: CODE, NAME, PATH_PARTS, USES, ITEMS, SRC_LINE.
    auto root_e = HermesAccess::raw_tiny_map(doc, 8);
    if (!root_e) {
        error("quote_item!: root tom alloc failed");
        return error_expr();
    }
    uint32_t root_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(root_e.get()) - HermesAccess::base(doc));
    // root pointer must be re-derived after every put() since put can grow.
    auto root = [&]() {
        return reinterpret_cast<TinyObjectMap*>(HermesAccess::base(doc) + root_off);
    };
    (void)root()->put(la::CODE.code,
                    AnyVal::from_value(static_cast<int32_t>(la::MODULE.code)),
                    dst_arena);
    (void)root()->put(la::NAME.code,
                    AnyVal::from_offset(arena_offset_t(name_off)), dst_arena);
    (void)root()->put(la::mod::PATH_PARTS.code,
                    AnyVal::from_offset(arena_offset_t(paths_off)), dst_arena);
    (void)root()->put(la::USES.code,
                    AnyVal::from_offset(arena_offset_t(uses_off)), dst_arena);
    (void)root()->put(la::ITEMS.code,
                    AnyVal::from_offset(arena_offset_t(items_off)), dst_arena);
    (void)root()->put(la::SRC_LINE.code,
                    AnyVal::from_value<int32_t>(1), dst_arena);
    root()->set_schema_type_code(
        logos::hermes::schema::ast(static_cast<int32_t>(la::MODULE.code)));

    HermesAccess::set_root_offset(doc, arena_offset_t(root_off));

    // Compact + snapshot bytes.
    auto packed_e = clone(doc);
    if (!packed_e) {
        error("quote_item!: clone failed");
        return error_expr();
    }
    auto packed = std::move(packed_e).get();
    auto& packed_arena = HermesAccess::arena(packed);
    const uint8_t* data = packed_arena.head().data();
    size_t used = packed_arena.total_used();

    lir::EHermesLit lit;
    lit.static_blob.assign(data, data + used);
    auto hs_t = make_struct_type("HermesStatic");
    auto template_lit = builder().hermes_lit_v(std::move(lit), hs_t);

    // ── Build block expr returning QuoteItemBlob ──────────────────────
    //   let __qib_t_N: HermesStatic = <template_lit>;
    //   [if N>0] let __qib_i_N: [Ident; N] = [<#name var_refs>];
    //   QuoteItemBlob { template_ptr: __qib_t.ptr, template_size: used,
    //                   idents_ptr: &__qib_i as *const Ident (or null),
    //                   idents_count: N }
    push_scope();
    auto* blk = lir::alloc_block(*cur_prog_);

    std::string tname = "__qib_t_" + std::to_string(tmp_var_count_++);
    define(tname, hs_t);
    {
        lir::SLet s;
        s.name = tname; s.type = hs_t; s.is_mut = false;
        s.value = std::move(template_lit);
        blk->stmts.push_back(make_stmt_emit(node_line_, std::move(s)));
    }

    TypeRef u8_ptr_t       = make_ptr(false, u8_t());
    TypeRef ident_ptr_t    = make_ptr(false, ident_t);
    TypeRef ident_ptr_ptr_t = make_ptr(false, ident_ptr_t);
    TypeRef u64_ty         = prim(LogosType::Kind::U64);

    lir::LExprPtr idents_ptr_e;
    uint64_t N = placeholders.size();
    if (N > 0) {
        // Build `[*const Ident; N] = [&v0, &v1, ...]`.  Arrays-of-struct
        // aren't laid out inline at MLIR level (Struct→ptr_type), so we
        // store pointers to per-site Ident locals instead — the host shim
        // dereferences each entry before substituting.
        auto arr_t = make_array(ident_ptr_t, N);
        std::vector<lir::LExprPtr> elems;
        elems.reserve(N);
        for (auto& ph : placeholders) {
            elems.push_back(builder().addr_of(ph.var_name, ident_ptr_t));
        }
        auto arr_e = builder().arr_lit(std::move(elems), arr_t);
        std::string aname = "__qib_i_" + std::to_string(tmp_var_count_++);
        define(aname, arr_t);
        {
            lir::SLet s;
            s.name = aname; s.type = arr_t; s.is_mut = false;
            s.value = std::move(arr_e);
            blk->stmts.push_back(make_stmt_emit(node_line_, std::move(s)));
        }
        auto arr_ptr_t = make_ptr(false, arr_t);
        auto raw = builder().addr_of(aname, arr_ptr_t);
        idents_ptr_e = builder().cast(std::move(raw), ident_ptr_ptr_t);
    } else {
        idents_ptr_e = builder().cast(builder().lit_int(0, intlit_t()),
                                      ident_ptr_ptr_t);
    }

    auto t_ref  = builder().var_ref(tname, hs_t);
    auto t_ptr  = builder().field_read(std::move(t_ref), "ptr", u8_ptr_t);
    auto t_size = builder().lit_int(static_cast<int64_t>(used), u64_ty);
    auto i_cnt  = builder().lit_int(static_cast<int64_t>(N), u64_ty);

    auto qib_t = make_struct_type("QuoteItemBlob");
    std::vector<std::pair<std::string, lir::LExprPtr>> fields;
    fields.emplace_back("template_ptr",  std::move(t_ptr));
    fields.emplace_back("template_size", std::move(t_size));
    fields.emplace_back("idents_ptr",    std::move(idents_ptr_e));
    fields.emplace_back("idents_count",  std::move(i_cnt));
    auto qib_lit = builder().struct_lit("QuoteItemBlob",
                                        std::move(fields), qib_t);

    pop_scope();
    return builder().block_expr(blk, std::move(qib_lit), qib_t);
}

// QUOTE_EXPR — `quote_expr! { expr }`. Slice 7 of metaprog-quote.
//
// Deep-copy the parsed expr AnyVal into a fresh Hermes doc as the root
// TOM, set the root's schema_type_code from CODE so position-aware
// lower_hermes_blob recurses into lower_expr at splice time, pack and
// emit as ExprBlob (HermesStatic-shaped marker).
//
// Antiquot (Slice 5c, parity port from quote_item):
//   `#name` inside the body parses as VAR_REF{NAME_VAR=name}. We walk
//   the dst doc post-copy by AST CODE-dispatch, rewrite each
//   NAME_VAR(string) → NAME_VAR(int placeholder_idx), and wrap the
//   doc with `wrapper{VALUE=expr_off, ITEMS=placeholders_array}` (CODE
//   absent so the runtime shim recognises it). The lowered LIR builds
//   an Ident array from `#x` var refs and calls
//   `logos_quote_expr_subst(template, idents, n)` which substitutes
//   NAME_VAR(idx) → NAME(ident.name), re-roots to the inner expr, and
//   returns a freshly-malloc'd HermesStatic-shaped ExprBlob ptr.
lir::LExprPtr SemaChecker::lower_quote_expr(TinyMapView node) {
    using logos::hermes::HermesAccess;
    using logos::hermes::ObjectArray;
    using logos::hermes::ArenaString;
    using logos::hermes::TinyObjectMap;
    using logos::hermes::arena_offset_t;
    using logos::hermes::make_doc;
    using logos::hermes::clone;
    using logos::hermes::copy_object_into;

    if (!holder_) {
        error("quote_expr!: missing AST holder");
        return error_expr();
    }
    if (!node.has_key(la::VALUE) || node.get(la::VALUE.code).is_null()) {
        error("quote_expr!: missing body expression");
        return error_expr();
    }
    AnyVal body_av = node.get(la::VALUE.code);
    if (!body_av.is_pointer()) {
        error("quote_expr!: body is not an object (parser bug?)");
        return error_expr();
    }
    const uint8_t* src_base = holder_->base();
    const auto* src_tom = reinterpret_cast<const TinyObjectMap*>(
        src_base + body_av.to_offset().value());

    // Read CODE from the source expr to set the root's schema_type_code.
    int32_t code = 0;
    if (src_tom->has_key(la::CODE.code)) {
        AnyVal cav = src_tom->get(la::CODE.code,
                                  const_cast<uint8_t*>(src_base));
        if (!cav.is_null() && !cav.is_pointer()) {
            code = cav.as_value<int32_t>();
        }
    }
    if (code == 0) {
        error("quote_expr!: body has no CODE field");
        return error_expr();
    }

    auto doc_e = make_doc(4096);
    if (!doc_e) {
        error("quote_expr!: make_doc failed");
        return error_expr();
    }
    auto doc = std::move(doc_e).get();
    auto& dst_arena = HermesAccess::arena(doc);

    auto cp_e = copy_object_into(src_tom, src_base, doc);
    if (!cp_e) {
        error("quote_expr!: copy_object_into failed");
        return error_expr();
    }
    void* dst_obj = cp_e.get();
    uint32_t root_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(dst_obj) - HermesAccess::base(doc));

    {
        auto* dst_root = reinterpret_cast<TinyObjectMap*>(
            HermesAccess::base(doc) + root_off);
        dst_root->set_schema_type_code(
            logos::hermes::schema::ast(code));
    }

    // ── Antiquot + repetition scan: walk dst expr tree, rewrite
    // NAME_VAR(string) → NAME_VAR(int idx). Inside REPEAT_GROUP
    // the placeholder var must be array-typed (the cursor); outside,
    // scalar Ident. Cursors within one repeat group must all have
    // the same array length, which the shim trusts at runtime.
    //
    // Recursion bounded to AST CODEs we support here: VAR_REF, BINOP,
    // UNARY, PAREN_EXPR, CAST, FIELD_READ, REPEAT_GROUP. CALL/METHOD_CALL/
    // INDEX_READ/STRUCT_LIT recursion is a known follow-up.
    struct ExprPlaceholder {
        uint32_t dst_offset;
        std::string var_name;
        bool is_cursor;          // inside a REPEAT_GROUP body
        bool is_expr_blob;       // var type is ExprBlob (5c Option B)
        uint64_t cursor_count;   // 1 for scalar Ident, N for [Ident; N]
    };
    std::vector<ExprPlaceholder> placeholders;
    TypeRef ident_t = make_struct_type("Ident");
    auto is_ident_type = [](TypeRef t) {
        return TypeRef(t).kind() == LogosType::Kind::Struct
            && TypeRef(t).struct_name() == "Ident";
    };
    // Returns N>0 if `t` is a fixed-size array of Ident, else 0.
    auto ident_array_len = [&](TypeRef t) -> uint64_t {
        if (TypeRef(t).kind() != LogosType::Kind::Array) return 0;
        TypeRef elem = TypeRef(t).elem();
        if (!is_ident_type(elem)) return 0;
        return static_cast<uint64_t>(TypeRef(t).arr_size());
    };

    int repeat_depth = 0;
    // Per-group cursor count (validated for consistency within a group).
    std::vector<uint64_t> repeat_stack_count;

    std::function<bool(uint32_t)> walk = [&](uint32_t tom_off) -> bool {
        auto* base = HermesAccess::base(doc);
        auto* tom = reinterpret_cast<TinyObjectMap*>(base + tom_off);
        int32_t cd = 0;
        if (tom->has_key(la::CODE.code)) {
            AnyVal cav = tom->get(la::CODE.code, base);
            if (!cav.is_null() && !cav.is_pointer())
                cd = cav.as_value<int32_t>();
        }

        auto recurse_key = [&](uint8_t key) -> bool {
            auto* b = HermesAccess::base(doc);
            auto* t = reinterpret_cast<TinyObjectMap*>(b + tom_off);
            if (!t->has_key(key)) return true;
            AnyVal av = t->get(key, b);
            if (av.is_null() || !av.is_pointer()) return true;
            return walk(static_cast<uint32_t>(av.to_offset().value()));
        };

        if (cd == la::VAR_REF.code) {
            if (tom->has_key(la::NAME_VAR.code)) {
                AnyVal nv = tom->get(la::NAME_VAR.code, base);
                if (nv.is_null() || !nv.is_pointer()) {
                    error("quote_expr!: NAME_VAR is not a string");
                    return false;
                }
                const auto* nv_str = reinterpret_cast<const ArenaString*>(
                    base + nv.to_offset().value());
                std::string vname(nv_str->view());
                TypeRef vt = lookup(vname);
                if (!vt) {
                    error("quote_expr!: `#" + vname
                          + "` — variable not in scope");
                    return false;
                }
                bool is_cursor = (repeat_depth > 0);
                bool is_expr_blob = false;
                uint64_t count = 0;
                auto is_expr_blob_type = [](TypeRef t) {
                    return TypeRef(t).kind() == LogosType::Kind::Struct
                        && TypeRef(t).struct_name() == "ExprBlob";
                };
                if (is_cursor) {
                    count = ident_array_len(vt);
                    if (count == 0) {
                        error("quote_expr!: `#" + vname
                              + "` inside repeat — expected [Ident; N]");
                        return false;
                    }
                    // Validate cursor count matches the enclosing group.
                    if (repeat_stack_count.back() == 0) {
                        repeat_stack_count.back() = count;
                    } else if (repeat_stack_count.back() != count) {
                        error("quote_expr!: `#" + vname
                              + "` cursor length mismatches sibling cursor in same #(...)*");
                        return false;
                    }
                } else if (is_expr_blob_type(vt)) {
                    // 5c Option B: ExprBlob splice. Count is unused; the
                    // shim reads the blob's size prefix at ptr-8.
                    is_expr_blob = true;
                    count = 0;
                } else {
                    if (!is_ident_type(vt)) {
                        error("quote_expr!: `#" + vname
                              + "` — expected Ident or ExprBlob");
                        return false;
                    }
                    count = 1;
                }
                int32_t idx = static_cast<int32_t>(placeholders.size());
                placeholders.push_back({tom_off, std::move(vname),
                                        is_cursor, is_expr_blob, count});
                auto* t2 = reinterpret_cast<TinyObjectMap*>(
                    HermesAccess::base(doc) + tom_off);
                (void)t2->put(la::NAME_VAR.code,
                    AnyVal::from_value<int32_t>(idx), dst_arena);
            }
            return true;
        }
        if (cd == la::REPEAT_GROUP.code) {
            // sep validation deferred to the shim — `&&*` works in any expr
            // position; `*` / `,*` only inside a list-bearing parent (e.g.
            // CALL.ARGS), but we don't know parent context here. Lowering
            // just records the cursor count.
            if (repeat_depth > 0) {
                error("quote_expr!: nested `#(...)` repetition not supported");
                return false;
            }
            ++repeat_depth;
            repeat_stack_count.push_back(0);
            bool ok = recurse_key(la::VALUE.code);
            if (ok && repeat_stack_count.back() == 0) {
                error("quote_expr!: `#(...)*` body has no cursor `#x` of "
                      "type [Ident; N]");
                ok = false;
            }
            repeat_stack_count.pop_back();
            --repeat_depth;
            return ok;
        }
        if (cd == la::BINOP.code) {
            if (!recurse_key(la::LHS.code)) return false;
            return recurse_key(la::RHS.code);
        }
        if (cd == la::PAREN_EXPR.code || cd == la::UNARY.code
            || cd == la::CAST.code) {
            return recurse_key(la::VALUE.code);
        }
        if (cd == la::FIELD_READ.code) {
            return recurse_key(la::RECEIVER.code);
        }
        if (cd == la::CALL.code || cd == la::METHOD_CALL.code
            || cd == la::STATIC_CALL.code) {
            // CALLEE is a single expr (CALL only); RECEIVER for METHOD_CALL/
            // STATIC_CALL is also a single expr. ARGS is an ObjectArray of
            // expr offsets; iterate each element.
            if (cd == la::CALL.code) {
                if (!recurse_key(la::CALLEE.code)) return false;
            } else {
                if (!recurse_key(la::RECEIVER.code)) return false;
            }
            auto* b = HermesAccess::base(doc);
            auto* t = reinterpret_cast<TinyObjectMap*>(b + tom_off);
            if (t->has_key(la::ARGS.code)) {
                AnyVal av = t->get(la::ARGS.code, b);
                if (av.is_pointer()) {
                    auto* arr = reinterpret_cast<ObjectArray*>(
                        b + av.to_offset().value());
                    for (uint64_t i = 0; i < arr->size(); ++i) {
                        AnyVal el = arr->get(i, b);
                        if (!el.is_pointer()) continue;
                        if (!walk(static_cast<uint32_t>(
                                el.to_offset().value()))) {
                            return false;
                        }
                    }
                }
            }
            return true;
        }
        return true;
    };

    if (!walk(root_off)) return error_expr();

    uint64_t N = placeholders.size();

    // Determine outer root for the packed bytes.
    uint32_t outer_root_off = root_off;
    if (N > 0) {
        // Build placeholders array (ObjectArray of int32 AnyVals carrying
        // dst-offsets of placeholder VAR_REF TOMs).
        auto ph_arr_e = ObjectArray::create(dst_arena, std::max<uint64_t>(4, N));
        if (!ph_arr_e) {
            error("quote_expr!: placeholders array alloc failed");
            return error_expr();
        }
        uint32_t ph_off = static_cast<uint32_t>(
            reinterpret_cast<uint8_t*>(ph_arr_e.get())
            - HermesAccess::base(doc));
        // Store as pointer AnyVals (offsets into the same VAR_REF TOMs that
        // live inside the expr tree). clone() dedupes via remember() so both
        // the wrapper.ITEMS slot and the parent expr edge resolve to the
        // single cloned VAR_REF — and crucially, clone *rewrites* the offset
        // for us. Storing an int32 of the source offset would leave a stale
        // value after clone.
        for (auto& ph : placeholders) {
            auto* arr = reinterpret_cast<ObjectArray*>(
                HermesAccess::base(doc) + ph_off);
            (void)arr->push_back(
                AnyVal::from_offset(arena_offset_t(ph.dst_offset)),
                dst_arena);
        }

        // Wrapper TOM. CODE absent (signals "wrapped" to the runtime shim).
        auto wrapper_e = HermesAccess::raw_tiny_map(doc, 4);
        if (!wrapper_e) {
            error("quote_expr!: wrapper tom alloc failed");
            return error_expr();
        }
        outer_root_off = static_cast<uint32_t>(
            reinterpret_cast<uint8_t*>(wrapper_e.get())
            - HermesAccess::base(doc));
        auto wrapper = [&]() {
            return reinterpret_cast<TinyObjectMap*>(
                HermesAccess::base(doc) + outer_root_off);
        };
        (void)wrapper()->put(la::VALUE.code,
                AnyVal::from_offset(arena_offset_t(root_off)), dst_arena);
        (void)wrapper()->put(la::ITEMS.code,
                AnyVal::from_offset(arena_offset_t(ph_off)), dst_arena);
    }

    HermesAccess::set_root_offset(doc, arena_offset_t(outer_root_off));

    auto packed_e = clone(doc);
    if (!packed_e) {
        error("quote_expr!: clone failed");
        return error_expr();
    }
    auto packed = std::move(packed_e).get();
    auto& packed_arena = HermesAccess::arena(packed);
    const uint8_t* data = packed_arena.head().data();
    size_t used = packed_arena.total_used();

    auto eb_t = make_struct_type("ExprBlob");

    if (N == 0) {
        // Simple path: no antiquot. Emit static rodata + ExprBlob{ptr}.
        lir::EHermesLit lit;
        lit.static_blob.assign(data, data + used);
        return builder().hermes_lit_v(std::move(lit), eb_t);
    }

    // Antiquot path: build a block expression that
    //   let __qet: HermesStatic = <static template bytes>;
    //   let __qes_0: IdentSpan = IdentSpan { ptr: &v0_ptr, count: c0 };
    //   ...
    //   let __qei: [IdentSpan; N] = [__qes_0, __qes_1, ...];
    //   ExprBlob { ptr: logos_quote_expr_subst(__qet.ptr, used,
    //                                          &__qei[0], N) }
    //
    // Per-ident spans use IdentSpan { ptr: *const Ident, count: u64 }.
    // For scalar Ident the ptr is `&v` (cast from &Ident to *const Ident),
    // count=1. For cursor `[Ident; M]`, ptr is `&v[0]` (cast from
    // *const [Ident; M] to *const Ident), count=M.
    auto hs_t = make_struct_type("HermesStatic");
    lir::EHermesLit lit;
    lit.static_blob.assign(data, data + used);
    auto template_lit = builder().hermes_lit_v(std::move(lit), hs_t);

    push_scope();
    auto* blk = lir::alloc_block(*cur_prog_);

    std::string tname = "__qet_" + std::to_string(tmp_var_count_++);
    define(tname, hs_t);
    {
        lir::SLet s;
        s.name = tname; s.type = hs_t; s.is_mut = false;
        s.value = std::move(template_lit);
        blk->stmts.push_back(make_stmt_emit(node_line_, std::move(s)));
    }

    TypeRef u8_ptr_t        = make_ptr(false, u8_t());
    TypeRef ident_ptr_t     = make_ptr(false, ident_t);
    TypeRef u64_ty          = prim(LogosType::Kind::U64);
    auto span_t             = make_struct_type("IdentSpan");
    TypeRef span_ptr_t      = make_ptr(false, span_t);

    // Both scalar Ident vars and `[Ident; M]` cursor vars are passed to
    // the host shim as **pointer arrays** of Ident pointers (one pointer
    // per element). Rationale: at MLIR, `[Struct; N]` is `[ptr; N]`
    // (memory note arr_lit_struct_ptr_layout) — 8-byte slots holding
    // per-element struct pointers. For a cursor var `xs: [Ident; M]`,
    // `&xs[0]` already points at this pointer-array layout. For a scalar
    // Ident var `v`, we materialise a 1-slot pointer array
    // `let __qe_p_K: [*const Ident; 1] = [&v];` and take `&__qe_p_K[0]`.
    // The shim reads `span.ptr` as `IdentPod* const *` for both cases.
    auto arr_t = make_array(span_t, N);
    std::vector<lir::LExprPtr> elems;
    elems.reserve(N);
    int span_tmp_idx = 0;
    auto eb_struct_t = make_struct_type("ExprBlob");
    for (auto& ph : placeholders) {
        lir::LExprPtr ptr_v;
        uint64_t kind = 0;
        if (ph.is_expr_blob) {
            // 5c Option B: read `<var>.ptr` (`*const u8`), cast to
            // `*const Ident` for the IdentSpan.ptr field. The shim
            // detects kind=1 and reads size from `ptr - 8`.
            kind = 1;
            auto v_ref = builder().var_ref(ph.var_name, eb_struct_t);
            auto blob_ptr = builder().field_read(
                std::move(v_ref), "ptr", u8_ptr_t);
            ptr_v = builder().cast(std::move(blob_ptr), ident_ptr_t);
        } else if (ph.is_cursor) {
            auto arr_var_t = make_array(ident_t, ph.cursor_count);
            auto arr_ptr_t = make_ptr(false, arr_var_t);
            auto raw_addr  = builder().addr_of(ph.var_name, arr_ptr_t);
            ptr_v          = builder().cast(std::move(raw_addr), ident_ptr_t);
        } else {
            // Materialise a 1-slot pointer array holding &v.
            auto p_arr_t   = make_array(ident_ptr_t, 1);
            auto p_ptr_t   = make_ptr(false, p_arr_t);
            auto v_addr    = builder().addr_of(ph.var_name, ident_ptr_t);
            std::vector<lir::LExprPtr> p_elems;
            p_elems.push_back(std::move(v_addr));
            auto p_arr_e   = builder().arr_lit(std::move(p_elems), p_arr_t);
            std::string pname = "__qep_" + std::to_string(span_tmp_idx++)
                + "_" + std::to_string(tmp_var_count_++);
            define(pname, p_arr_t);
            {
                lir::SLet s;
                s.name = pname; s.type = p_arr_t; s.is_mut = false;
                s.value = std::move(p_arr_e);
                blk->stmts.push_back(make_stmt_emit(node_line_, std::move(s)));
            }
            auto raw_addr  = builder().addr_of(pname, p_ptr_t);
            ptr_v          = builder().cast(std::move(raw_addr), ident_ptr_t);
        }
        auto cnt_v = builder().lit_int(
            static_cast<int64_t>(ph.cursor_count), u64_ty);
        auto kind_v = builder().lit_int(
            static_cast<int64_t>(kind), u64_ty);
        std::vector<std::pair<std::string, lir::LExprPtr>> sf;
        sf.emplace_back("ptr", std::move(ptr_v));
        sf.emplace_back("count", std::move(cnt_v));
        sf.emplace_back("kind", std::move(kind_v));
        elems.push_back(builder().struct_lit(
            "IdentSpan", std::move(sf), span_t));
    }
    auto arr_e = builder().arr_lit(std::move(elems), arr_t);
    std::string aname = "__qei_" + std::to_string(tmp_var_count_++);
    define(aname, arr_t);
    {
        lir::SLet s;
        s.name = aname; s.type = arr_t; s.is_mut = false;
        s.value = std::move(arr_e);
        blk->stmts.push_back(make_stmt_emit(node_line_, std::move(s)));
    }
    auto arr_ptr_full_t = make_ptr(false, arr_t);
    auto raw       = builder().addr_of(aname, arr_ptr_full_t);
    auto idents_pp = builder().cast(std::move(raw), span_ptr_t);

    auto t_ref  = builder().var_ref(tname, hs_t);
    auto t_ptr  = builder().field_read(std::move(t_ref), "ptr", u8_ptr_t);
    auto t_size = builder().lit_int(static_cast<int64_t>(used), u64_ty);
    auto i_cnt  = builder().lit_int(static_cast<int64_t>(N), u64_ty);

    // Synthesize an extern call: logos_quote_expr_subst(tpl, size, idents, n)
    // → *const u8.
    std::vector<lir::LExprPtr> call_args;
    call_args.push_back(std::move(t_ptr));
    call_args.push_back(std::move(t_size));
    call_args.push_back(std::move(idents_pp));
    call_args.push_back(std::move(i_cnt));
    auto subst_call = builder().call(
        "logos_quote_expr_subst",
        {},
        std::move(call_args),
        u8_ptr_t);

    // Wrap as ExprBlob { ptr: <subst_call> }.
    std::vector<std::pair<std::string, lir::LExprPtr>> fields;
    fields.emplace_back("ptr", std::move(subst_call));
    auto eb_lit = builder().struct_lit("ExprBlob", std::move(fields), eb_t);

    pop_scope();
    return builder().block_expr(blk, std::move(eb_lit), eb_t);
}

// HERMES_BLOB — sema-internal node spliced by the metacall driver after
// invoking a thunk whose return type is HermesStatic / Hermes / ExprBlob.
// VALUE is the raw Hermes blob bytes (Varchar).
//
// Two paths:
//
// 1. Data blob (default): root TOM has no AST/LIR schema_type_code (or
//    isn't an AST node). Lower to EHermesLit{static_blob=bytes,
//    root=null, has_captures=false}, type=HermesStatic; mlir_gen emits
//    the bytes directly into rodata.
//
// 2. AST-fragment blob (Slice 7 of metaprog-quote): root TOM's
//    schema_type_code lives in the CAT_AST category. We deserialise
//    the blob via from_bytes_copy (stashed in blob_docs_ to outlive
//    the recursion), point holder_ at the new doc, and recurse into
//    lower_expr/lower_stmt/lower_pat/_ty (Slice 7 implements expr
//    only — the others slot in when their grammar lands).
//
// Dispatch happens here rather than in lower_metacall because the
// blob's root code is the authoritative signal: a HermesStatic-typed
// metafn might be returning data OR a fragment, and we can only know
// after reading the bytes. Pass-1 metacall typing (`is_expr_blob`) is
// a hint that lets `let X: T = metacall ...` defer type-check; pass-2
// here actually completes the lowering with the right type.
lir::LExprPtr SemaChecker::lower_hermes_blob(TinyMapView node) {
    auto bytes = str_of(node.get(la::VALUE.code));

    // Peek at the root's schema_type_code without copying the whole blob
    // unless we have to. Use from_bytes_copy: it owns its own arena and
    // remains valid as long as we keep the resulting Hermes alive.
    auto doc_e = logos::hermes::from_bytes_copy(
        reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    if (doc_e) {
        auto& doc = doc_e.get();
        auto root_obj = doc.root_object();
        if (!root_obj.tagged().is_null()) {
            auto root_tm = root_obj.as_tiny_map();
            if (!root_tm.is_null()) {
                uint64_t stc = root_tm.ptr()->schema_type_code();
                if (logos::hermes::schema::category_of(stc)
                    == logos::hermes::schema::CAT_AST) {
                    int32_t code = static_cast<int32_t>(
                        logos::hermes::schema::variant_of(stc));
                    // Slice 7 prototype: expression nodes only. Stmt/pat/ty
                    // dispatchers slot in here once their grammar lands.
                    if (code == la::BINOP.code || code == la::LIT_INT.code
                        || code == la::LIT_BOOL.code || code == la::LIT_STR.code
                        || code == la::VAR_REF.code || code == la::CALL.code
                        || code == la::PAREN_EXPR.code || code == la::UNARY.code
                        || code == la::FIELD_READ.code || code == la::METHOD_CALL.code
                        || code == la::CAST.code || code == la::INDEX_READ.code
                        || code == la::STRUCT_LIT.code) {
                        // Stash the doc so its arena outlives sema (mirror
                        // back-fill may read from it). holder_ is swapped
                        // for the recursion only.
                        blob_docs_.emplace_back(std::move(doc));
                        auto* prev_holder = holder_;
                        holder_ = blob_docs_.back().holder();
                        auto root_tm2 =
                            blob_docs_.back().root_object().as_tiny_map();
                        TinyMapView root_view(root_tm2.offset(), holder_);
                        auto lowered = lower_expr(root_view);
                        holder_ = prev_holder;
                        return lowered;
                    }
                }
            }
        }
    }

    // Fallback: opaque HermesStatic data blob.
    lir::EHermesLit lit;
    lit.static_blob.assign(bytes.data(), bytes.size());
    auto result_type = make_struct_type("HermesStatic");
    return builder().hermes_lit_v(std::move(lit), result_type);
}

// ── metacall <call_expr> ─────────────────────────────────────────────────
//
// M.1 stage 2: validate the metacall site (return type is primitive
// scalar, every arg is a compile-time constant per CTFE), CTFE-evaluate
// each arg into a literal, and synthesise a no-arg thunk source string
// `fn __metacall_thunk_<idx>() -> T { return <callee>(<lit>, ...); }`.
// The driver feeds the thunk through logos_emit_source so the metaprog
// JIT compiles it; the driver then invokes the thunk and splices the
// returned literal back into the entry-file AST node at site.expr_offset
// (overwriting CODE+VALUE in place). The METACALL itself still lowers as
// a runtime pass-through here so the in-progress L-IR remains valid for
// the iteration's borrow/type checks; mlir_gen never sees this lowering
// because the AST splice runs before the final sema pass.
//
// On bad input we emit a diag and fall through to error_expr() so sema
// still produces a useful tree for downstream passes.
lir::LExprPtr SemaChecker::lower_metacall(TinyMapView node) {
    if (!node.has_key(la::VALUE)) {
        error("metacall: missing inner call expression");
        return error_expr();
    }
    auto inner = map_of(node.get(la::VALUE.code));
    int32_t ic = code_of(inner);
    if (ic != la::CALL && ic != la::GENERIC_CALL && ic != la::STATIC_CALL) {
        error("metacall: expected a free-function or static-method call");
        return error_expr();
    }

    // CTFE each arg of the inner call; missing => non-CT-constant diag.
    // Note: CALL stores ARGS as a flat array directly; GENERIC_CALL/STATIC_CALL
    // wrap it as a TinyMap with ITEMS=array (call_arg_list rule).
    // Stage 2: collect printable literal text for each arg so we can splice
    // into the synthesised thunk body. Empty string means CTFE failed.
    std::vector<std::string> arg_lits;
    auto print_ctfe_lit = [](const ctfe::CtfeValue& v) -> std::string {
        using K = LogosType::Kind;
        if (v.kind == K::Bool) return v.b ? "true" : "false";
        if (v.kind == K::Slice) {
            std::string s = "\"";
            for (char c : v.s) {
                switch (c) {
                case '\\': s += "\\\\"; break;
                case '"':  s += "\\\""; break;
                case '\n': s += "\\n"; break;
                case '\r': s += "\\r"; break;
                case '\t': s += "\\t"; break;
                default:   s += c; break;
                }
            }
            s += "\"";
            return s;
        }
        // float kinds
        if (v.kind == K::F32 || v.kind == K::F64 || v.kind == K::FloatLit) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.17g", v.f);
            std::string s = buf;
            // Need a decimal point so the parser sees LIT_FLOAT, not LIT_INT.
            if (s.find('.') == std::string::npos && s.find('e') == std::string::npos
                && s.find('n') == std::string::npos /* nan */ && s.find('i') == std::string::npos /* inf */) {
                s += ".0";
            }
            if (v.kind == K::F32) s += "f32";
            else if (v.kind == K::F64) s += "f64";
            return s;
        }
        // integer kinds
        std::string s;
        bool sgn = (v.kind == K::I8 || v.kind == K::I16 || v.kind == K::I24 ||
                    v.kind == K::I32 || v.kind == K::I56 || v.kind == K::I64 ||
                    v.kind == K::I128 || v.kind == K::IntLit);
        if (sgn) {
            if (v.i < 0) { s = "(-"; s += std::to_string(-(v.i + 1)); s.back()++; s += ")"; }
            else s = std::to_string(v.i);
        } else {
            s = std::to_string(v.u);
        }
        switch (v.kind) {
        case K::I8:  s += "i8";  break;
        case K::I16: s += "i16"; break;
        case K::I32: s += "i32"; break;
        case K::I64: s += "i64"; break;
        case K::U8:  s += "u8";  break;
        case K::U16: s += "u16"; break;
        case K::U32: s += "u32"; break;
        case K::U64: s += "u64"; break;
        default: break;  // IntLit / I24 / U24 / I56 / U56 — leave unsuffixed
        }
        return s;
    };
    auto eval_args_array = [&](hermes::ArrayView args) {
        for (uint64_t i = 0; i < args.size(); ++i) {
            auto a = map_of(args.get(i));
            auto r = ctfe::eval_expr(a, holder_);
            if (!r) {
                error(std::format("metacall: argument {} is not a compile-time constant ({})",
                                  i + 1, r.error().msg));
                arg_lits.emplace_back();  // marker: CTFE failed
            } else {
                arg_lits.push_back(print_ctfe_lit(*r));
            }
        }
    };
    if (inner.has_key(la::ARGS)) {
        AnyVal args_av = inner.get(la::ARGS.code);
        if (!args_av.is_null()) {
            if (ic == la::CALL) {
                eval_args_array(arr_of(args_av));
            } else {
                // GENERIC_CALL / STATIC_CALL: ARGS is { ITEMS: [...] }
                auto args_map = map_of(args_av);
                if (args_map.has_key(la::ITEMS))
                    eval_args_array(arr_of(args_map.get(la::ITEMS.code)));
            }
        }
    }

    // Lower the inner call normally — type-checks, generic-resolves, queues
    // monomorphisation. Whatever return type pops out drives the primitive
    // check below.
    auto lowered = lower_expr(inner);
    auto rt = lowered ? lowered->type : nullptr;
    auto rk = TypeRef(rt).kind();
    bool is_hermes_static =
        rk == LogosType::Kind::Struct && TypeRef(rt).struct_name() == "HermesStatic";
    bool is_hermes =
        rk == LogosType::Kind::Struct && TypeRef(rt).struct_name() == "Hermes";
    // Slice 7 of metaprog-quote: ExprBlob is a HermesStatic-shaped marker
    // signalling that the metafunction returns an AST expression fragment.
    // Driver splices identically (CODE→HERMES_BLOB, VALUE=bytes); pass-2
    // sema reads the blob's root schema_type_code and recurses into
    // lower_expr to recover the actual expr type. Pass-1 typing is deferred
    // — `let X: T = metacall foo()` accepts any T over an ExprBlob RHS.
    bool is_expr_blob =
        rk == LogosType::Kind::Struct && TypeRef(rt).struct_name() == "ExprBlob";
    bool prim_ok =
        rk == LogosType::Kind::Bool ||
        rk == LogosType::Kind::I8   || rk == LogosType::Kind::I16 ||
        rk == LogosType::Kind::I24  || rk == LogosType::Kind::I32 ||
        rk == LogosType::Kind::I56  || rk == LogosType::Kind::I64 ||
        rk == LogosType::Kind::U8   || rk == LogosType::Kind::U16 ||
        rk == LogosType::Kind::U24  || rk == LogosType::Kind::U32 ||
        rk == LogosType::Kind::U56  || rk == LogosType::Kind::U64 ||
        rk == LogosType::Kind::F32  || rk == LogosType::Kind::F64 ||
        rk == LogosType::Kind::IntLit ||
        rk == LogosType::Kind::FloatLit ||
        // &str / Slice<u8>
        (rk == LogosType::Kind::Slice && TypeRef(rt).elem() &&
         TypeRef(rt).elem().kind() == LogosType::Kind::U8);
    bool ok_ret = prim_ok || is_hermes_static || is_hermes || is_expr_blob;
    if (rt && !ok_ret)
        error("metacall: ret type must be primitive scalar, HermesStatic, Hermes, or ExprBlob");

    // Record site for the eventual driver-side splice (M.1 Stage 2).
    if (cur_prog_ && rt && ok_ret) {
        lir::LProgram::MetacallSite site;
        site.ast_idx = cur_ast_idx_;
        site.expr_offset = static_cast<uint32_t>(node.offset().value());
        site.thunk_name = std::format("__metacall_thunk_{}",
                                      cur_prog_->metacall_sites.size());
        using RT = lir::LProgram::MetacallSite::RetTag;
        switch (rk) {
        case LogosType::Kind::Bool:  site.ret_tag = RT::Bool; break;
        case LogosType::Kind::I8:    site.ret_tag = RT::I8; break;
        case LogosType::Kind::I16:   site.ret_tag = RT::I16; break;
        case LogosType::Kind::I24:   site.ret_tag = RT::I24; break;
        case LogosType::Kind::I32:   site.ret_tag = RT::I32; break;
        case LogosType::Kind::I56:   site.ret_tag = RT::I56; break;
        case LogosType::Kind::U8:    site.ret_tag = RT::U8; break;
        case LogosType::Kind::U16:   site.ret_tag = RT::U16; break;
        case LogosType::Kind::U24:   site.ret_tag = RT::U24; break;
        case LogosType::Kind::U32:   site.ret_tag = RT::U32; break;
        case LogosType::Kind::U56:   site.ret_tag = RT::U56; break;
        case LogosType::Kind::U64:   site.ret_tag = RT::U64; break;
        case LogosType::Kind::F32:   site.ret_tag = RT::F32; break;
        case LogosType::Kind::F64:   site.ret_tag = RT::F64; break;
        case LogosType::Kind::IntLit:
        case LogosType::Kind::I64:   site.ret_tag = RT::I64; break;
        case LogosType::Kind::FloatLit: site.ret_tag = RT::F64; break;
        case LogosType::Kind::Slice: site.ret_tag = RT::Str; break;
        case LogosType::Kind::Struct:
            site.ret_tag = is_hermes_static ? RT::HermesStatic
                         : is_hermes        ? RT::Hermes
                         : is_expr_blob     ? RT::ExprBlob
                                            : RT::I64;
            break;
        default: site.ret_tag = RT::I64; break;
        }

        // Stage 2: synthesise the no-arg thunk source.
        // Format the inner-call text from the AST shape + CTFE-printed args.
        std::string call_text;
        bool ok = true;
        // Build turbofish suffix from TYPE_PARAMS.ITEMS, if any (GENERIC_CALL/
        // STATIC_CALL). resolve_type → type_str gives us the canonical form.
        auto build_turbofish = [&](TinyMapView n) -> std::string {
            if (!n.has_key(la::TYPE_PARAMS)) return {};
            auto tplist = map_of(n.get(la::TYPE_PARAMS.code));
            if (!tplist.has_key(la::ITEMS)) return {};
            auto items = arr_of(tplist.get(la::ITEMS.code));
            if (items.size() == 0) return {};
            std::string out = "::<";
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (i) out += ", ";
                out += type_str(resolve_type(map_of(items.get(i))));
            }
            out += ">";
            return out;
        };
        if (ic == la::CALL) {
            call_text = std::string(str_of(inner.get(la::CALLEE.code)));
        } else if (ic == la::GENERIC_CALL) {
            call_text = std::string(str_of(inner.get(la::CALLEE.code)));
            call_text += build_turbofish(inner);
        } else if (ic == la::STATIC_CALL) {
            call_text = std::string(str_of(inner.get(la::RECEIVER.code)));
            call_text += "::";
            std::string mname(str_of(inner.get(la::NAME.code)));
            std::string tf = build_turbofish(inner);
            if (tf.empty()) {
                call_text += mname;
            } else {
                // For Type::method::<T>(...) the turbofish is on the method name
                // (per parser), so we can place it after `mname` directly.
                call_text += mname;
                call_text += tf;
            }
        } else {
            ok = false;
        }
        // Append (arg_lits...). One blank means CTFE failed earlier — we
        // already emitted a diag, just skip thunk synthesis.
        if (ok) {
            call_text += "(";
            for (size_t i = 0; i < arg_lits.size(); ++i) {
                if (i) call_text += ", ";
                if (arg_lits[i].empty()) { ok = false; break; }
                call_text += arg_lits[i];
            }
            call_text += ")";
        }
        if (ok) {
            // Print the return-type text. type_str on primitives gives the
            // surface name (i64/u64/...); for &str / Slice<u8> we hand-roll.
            std::string ret_text;
            if (TypeRef(rt).kind() == LogosType::Kind::Slice
                && TypeRef(rt).elem()
                && TypeRef(rt).elem().kind() == LogosType::Kind::U8) {
                ret_text = "&str";
            } else if (TypeRef(rt).kind() == LogosType::Kind::IntLit) {
                ret_text = "i64";
            } else if (TypeRef(rt).kind() == LogosType::Kind::FloatLit) {
                ret_text = "f64";
            } else {
                ret_text = type_str(rt);
            }
            std::string pkg = cur_package_.empty() ? "__metacall_thunks" : cur_package_;
            using RT2 = lir::LProgram::MetacallSite::RetTag;
            if (site.ret_tag == RT2::Hermes) {
                // Hermes ret: wrap in __metacall_freeze, which copies the
                // live-zone bytes into a malloc'd [u64 size][bytes] buffer
                // and returns a pointer past the size prefix. Driver reads
                // *(ptr-8) for size and splices identically to HermesStatic.
                // The temporary Hermes drops at end-of-thunk; the freeze
                // helper has already copied bytes out.
                site.thunk_source = std::format(
                    "package {};\n"
                    "use std.hermes.ctr;\n"
                    "unsafe fn {}() -> *const u8 {{\n"
                    "    let __h: Hermes = {};\n"
                    "    return __metacall_freeze(&__h);\n"
                    "}}\n",
                    pkg, site.thunk_name, call_text);
            } else {
                // HermesStatic ret needs std.hermes.view in scope.
                // ExprBlob ret needs std.compiler.metaprog in scope.
                const char* extra_uses =
                    (site.ret_tag == RT2::HermesStatic)
                    ? "use std.hermes.view;\n"
                    : (site.ret_tag == RT2::ExprBlob)
                    ? "use std.compiler.metaprog;\nuse std.hermes.view;\n"
                    : "";
                site.thunk_source = std::format(
                    "package {};\n"
                    "{}"
                    "fn {}() -> {} {{ return {}; }}\n",
                    pkg, extra_uses, site.thunk_name, ret_text, call_text);
            }
        }
        cur_prog_->metacall_sites.push_back(std::move(site));

        // Post-splice the AST node becomes HERMES_BLOB (typed HermesStatic).
        // Override the lowered expr's type so sema sees the post-splice shape
        // even when the callee returns Hermes (mutable) — auto-freeze copies
        // bytes into a static blob, so user code consumes HermesStatic.
        if (is_hermes && lowered) {
            lowered->type = make_struct_type("HermesStatic");
        }
    }

    // Pass-through: keeps the in-progress L-IR valid for borrow/type checks
    // during sema iterations. The driver replaces the METACALL AST node with
    // a literal before the FINAL non-metaprog sema pass, so this lowering
    // never reaches mlir_gen.
    return lowered;
}

} // namespace logos::compiler
