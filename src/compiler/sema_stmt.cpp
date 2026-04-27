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

namespace logos::compiler {

namespace la = ast;
using hermes::TinyMapView;
using hermes::ArrayView;
using hermes::StringView;
using hermes::AnyVal;
using hermes::MemHolder;

// Statement lowering methods

bool SemaChecker::stmt_always_returns(TinyMapView stmt) {
    int32_t c = code_of(stmt);
    if (c == la::RETURN) return true;
    if (c == la::UNSAFE_BLOCK) {
        return stmt.has_key(la::BODY) &&
               block_always_returns(map_of(stmt.get(la::BODY.code)));
    }
    if (c == la::LOOP) {
        // `loop {}` is an infinite loop — it never falls through to the next statement.
        return true;
    }
    if (c == la::IF) {
        if (!stmt.has_key(la::ELSE)) return false;
        bool then_ret = stmt.has_key(la::THEN) &&
                        block_always_returns(map_of(stmt.get(la::THEN.code)));
        auto else_node = map_of(stmt.get(la::ELSE.code));
        bool else_ret  = (code_of(else_node) == la::BLOCK)
                         ? block_always_returns(else_node)
                         : stmt_always_returns(else_node);
        return then_ret && else_ret;
    }
    if (c == la::MATCH) {
        if (!stmt.has_key(la::ITEMS)) return false;
        auto arms = arr_of(stmt.get(la::ITEMS.code));
        bool all_ret = true;
        for (uint64_t i = 0; i < arms.size(); ++i) {
            auto arm = map_of(arms.get(i));
            if (code_of(arm) != la::MATCH_ARM) continue;
            if (arm.has_key(la::BODY)) {
                auto body = map_of(arm.get(la::BODY.code));
                bool arm_ret = (code_of(body) == la::BLOCK)
                               ? block_always_returns(body)
                               : stmt_always_returns(body);
                if (!arm_ret) all_ret = false;
            } else if (arm.has_key(la::EXPR)) {
                // Expression arm (pattern => expr,) always provides a value.
            } else { all_ret = false; }
        }
        // Match always returns if all arms return. This covers both the
        // classic wildcard-arm case and exhaustive enum matches without _.
        // Guard against empty arm list (all_ret stays true vacuously).
        return all_ret && arms.size() > 0;
    }
    return false;
}

bool SemaChecker::block_always_returns(TinyMapView block) {
    if (!block.has_key(la::ITEMS)) return false;
    auto stmts = arr_of(block.get(la::ITEMS.code));
    for (uint64_t i = 0; i < stmts.size(); ++i) {
        auto s = map_of(stmts.get(i));
        if (!s.is_null() && stmt_always_returns(s)) return true;
    }
    return false;
}

bool SemaChecker::stmt_always_diverts(TinyMapView stmt) {
    int32_t c = code_of(stmt);
    if (c == la::BREAK || c == la::CONTINUE) return true;
    if (c == la::IF) {
        if (!stmt.has_key(la::ELSE)) return false;
        bool then_d = stmt.has_key(la::THEN) &&
                      block_always_diverts(map_of(stmt.get(la::THEN.code)));
        auto else_node = map_of(stmt.get(la::ELSE.code));
        bool else_d  = (code_of(else_node) == la::BLOCK)
                       ? block_always_diverts(else_node)
                       : stmt_always_diverts(else_node);
        return then_d && else_d;
    }
    if (c == la::MATCH) {
        if (!stmt.has_key(la::ITEMS)) return false;
        auto arms = arr_of(stmt.get(la::ITEMS.code));
        bool all_d = true;
        for (uint64_t i = 0; i < arms.size(); ++i) {
            auto arm = map_of(arms.get(i));
            if (code_of(arm) != la::MATCH_ARM) continue;
            if (arm.has_key(la::BODY)) {
                auto body = map_of(arm.get(la::BODY.code));
                bool arm_d = (code_of(body) == la::BLOCK)
                             ? block_always_diverts(body)
                             : stmt_always_diverts(body);
                if (!arm_d) all_d = false;
            } else if (!arm.has_key(la::EXPR)) {
                all_d = false;
            }
        }
        return all_d && arms.size() > 0;
    }
    if (c == la::UNSAFE_BLOCK) {
        return stmt.has_key(la::BODY) &&
               block_always_diverts(map_of(stmt.get(la::BODY.code)));
    }
    return stmt_always_returns(stmt);
}

bool SemaChecker::block_always_diverts(TinyMapView block) {
    if (!block.has_key(la::ITEMS)) return false;
    auto stmts = arr_of(block.get(la::ITEMS.code));
    for (uint64_t i = 0; i < stmts.size(); ++i) {
        auto s = map_of(stmts.get(i));
        if (!s.is_null() && stmt_always_diverts(s)) return true;
    }
    return false;
}

lir::LStmt SemaChecker::lower_stmt(TinyMapView stmt) {
    node_line_ = get_line(stmt);
    int32_t c = code_of(stmt);

    if (c == la::LET)          return lower_let(stmt);
    if (c == la::LET_ELSE)     return lower_let_else(stmt);
    if (c == la::LET_DESTRUCT) return lower_let_destruct(stmt);
    if (c == la::ASSIGN)          return lower_assign(stmt);
    if (c == la::COMPOUND_ASSIGN) return lower_compound_assign(stmt);
    if (c == la::RETURN)       return lower_return(stmt);
    if (c == la::IF)           return lower_if(stmt);
    if (c == la::LABELED_LOOP) {
        // 'label: for/while/loop { }
        // Extract label, set pending_loop_label_, lower the inner loop.
        std::string lbl;
        if (stmt.has_key(la::LABEL)) {
            auto sv = str_of(stmt.get(la::LABEL.code));
            lbl = std::string(sv);
        }
        auto inner = map_of(stmt.get(la::BODY.code));
        pending_loop_label_ = lbl;
        auto result = lower_stmt(inner);
        pending_loop_label_.clear();
        return result;
    }
    if (c == la::WHILE)        return lower_while(stmt);
    if (c == la::FOR)          return lower_for(stmt);
    if (c == la::FOR_EACH)     return lower_for_each(stmt);
    if (c == la::LOOP)         return lower_loop(stmt);
    if (c == la::FIELD_WRITE)        return lower_field_write(stmt);
    if (c == la::CHAIN_FIELD_WRITE)  return lower_chain_field_write(stmt);
    if (c == la::CHAIN_FIELD_COMPOUND_ASSIGN) return lower_chain_field_compound_assign(stmt);
    if (c == la::FIELD_COMPOUND_ASSIGN) return lower_field_compound_assign(stmt);
    if (c == la::TUPLE_FIELD_WRITE)  return lower_tuple_field_write(stmt);
    if (c == la::TUPLE_FIELD_COMPOUND_ASSIGN) return lower_tuple_field_compound_assign(stmt);
    if (c == la::DEREF_FIELD_WRITE)  return lower_deref_field_write(stmt);
    if (c == la::DEREF_FIELD_COMPOUND_ASSIGN) return lower_deref_field_compound_assign(stmt);
    if (c == la::INDEX_WRITE)        return lower_index_write(stmt);
    if (c == la::INDEX_COMPOUND_ASSIGN) return lower_index_compound_assign(stmt);
    if (c == la::FIELD_INDEX_WRITE)  return lower_field_index_write(stmt);
    if (c == la::FIELD_INDEX_COMPOUND_ASSIGN) return lower_field_index_compound_assign(stmt);
    if (c == la::MATCH)        return lower_match(stmt);
    if (c == la::EXPR_STMT) {
        lir::LExprPtr e = stmt.has_key(la::VALUE)
            ? lower_expr(map_of(stmt.get(la::VALUE.code)))
            : error_expr();
        return make_stmt(node_line_, lir::SExprStmt{std::move(e)});
    }
    if (c == la::BREAK) {
        if (loop_depth_ == 0) error("'break' outside loop");
        lir::LExprPtr bval;
        if (stmt.has_key(la::VALUE)) {
            bval = lower_expr(map_of(stmt.get(la::VALUE.code)));
            if (break_without_value_) {
                error("loop break mixes value and no-value breaks");
            } else if (bval && bval->type && TypeRef(bval->type).kind() != LogosType::Kind::Error) {
                if (!break_value_type_) {
                    break_value_type_ = bval->type;
                } else if (!types_compatible(bval->type, break_value_type_) &&
                           !types_compatible(break_value_type_, bval->type)) {
                    error(std::format("loop break values have incompatible types: {} vs {}",
                          type_str(break_value_type_), type_str(bval->type)));
                } else {
                    break_value_type_ = unify_numeric(break_value_type_, bval->type);
                }
            }
        } else {
            if (break_value_type_)
                error("loop break mixes value and no-value breaks");
            break_without_value_ = true;
        }
        std::string break_label;
        if (stmt.has_key(la::LABEL))
            break_label = std::string(str_of(stmt.get(la::LABEL.code)));
        return make_stmt(node_line_, lir::SBreak{std::move(bval), std::move(break_label)});
    }
    if (c == la::CONTINUE) {
        if (loop_depth_ == 0) error("'continue' outside loop");
        std::string cont_label;
        if (stmt.has_key(la::LABEL))
            cont_label = std::string(str_of(stmt.get(la::LABEL.code)));
        return make_stmt(node_line_, lir::SContinue{std::move(cont_label)});
    }
    if (c == la::DEREF_WRITE) {
        // *ptr = value;
        lir::LExprPtr ptr = stmt.has_key(la::NAME)
            ? lower_expr(map_of(stmt.get(la::NAME.code)))
            : error_expr();
        lir::LExprPtr val = stmt.has_key(la::VALUE)
            ? lower_expr(map_of(stmt.get(la::VALUE.code)))
            : error_expr();
        auto pt = ptr->type;
        // Writing through &mut T is safe; writing through raw *mut/*const T requires unsafe
        bool is_mut_ref = TypeRef(pt).kind() == LogosType::Kind::MutRef;
        if (!is_mut_ref && !inside_unsafe_)
            error("write through raw pointer requires unsafe context");
        if (TypeRef(pt).kind() != LogosType::Kind::Ptr && TypeRef(pt).kind() != LogosType::Kind::MutRef) {
            error("deref-write: '=' left side must be a pointer or mutable reference");
        }
        // *const T is read-only; only *mut T or &mut T can be written through
        if (TypeRef(pt).kind() == LogosType::Kind::Ptr && !TypeRef(pt).mut_ptr())
            error("deref-write: cannot write through *const pointer (use *mut)");
        return make_stmt(node_line_, lir::SDerefWrite{std::move(ptr), std::move(val)});
    }
    if (c == la::UNSAFE_BLOCK) {
        bool was = inside_unsafe_;
        inside_unsafe_ = true;
        auto inner = stmt.has_key(la::BODY)
            ? lower_block(map_of(stmt.get(la::BODY.code)))
            : lir::LBlock{};
        inside_unsafe_ = was;
        return make_stmt(node_line_, lir::SBlock{std::make_unique<lir::LBlock>(std::move(inner))});
    }
    if (c == la::BLOCK_STMT) {
        auto inner = stmt.has_key(la::BODY)
            ? lower_block(map_of(stmt.get(la::BODY.code)))
            : lir::LBlock{};
        return make_stmt(node_line_, lir::SBlock{std::make_unique<lir::LBlock>(std::move(inner))});
    }
    // Unknown stmt — emit dummy expr stmt
    return make_stmt(node_line_, lir::SExprStmt{error_expr()});
}

lir::LBlock SemaChecker::lower_block(TinyMapView block) {
    lir::LBlock result;
    push_scope();
    if (block.has_key(la::ITEMS)) {
        auto stmts = arr_of(block.get(la::ITEMS.code));
        for (uint64_t i = 0; i < stmts.size(); ++i) {
            auto s = map_of(stmts.get(i));
            if (s.is_null()) continue;
            auto lowered = lower_stmt(s);
            // Insert drops before return/break/continue.
            // Return's value expression MUST be evaluated BEFORE the drops
            // (it may borrow variables that the drops would release).
            // Hoist the return value into a temporary, then drop, then return
            // the temporary.
            if (auto* sr = std::get_if<lir::SReturn>(&lowered.kind)) {
                auto drops = collect_all_drops();
                if (!drops.empty() && sr->value) {
                    TypeRef rt = sr->value->type;
                    std::string tmp = "__ret_tmp_" +
                        std::to_string(tmp_var_count_++);
                    lir::SLet sl;
                    sl.name = tmp; sl.type = rt; sl.is_mut = false;
                    sl.value = std::move(sr->value);
                    result.stmts.push_back(
                        make_stmt(node_line_, std::move(sl)));
                    for (auto& d : drops)
                        result.stmts.push_back(std::move(d));
                    lir::SReturn nsr;
                    nsr.value = builder().var_ref(tmp, rt);
                    result.stmts.push_back(
                        make_stmt(node_line_, std::move(nsr)));
                    continue;
                }
                for (auto& d : drops)
                    result.stmts.push_back(std::move(d));
            } else if (auto lk = stmt_ref_of(lowered).kind();
                       lk == lir_schema::stmt::Code::Break ||
                       lk == lir_schema::stmt::Code::Continue) {
                for (auto& d : collect_drops())
                    result.stmts.push_back(std::move(d));
            }
            result.stmts.push_back(std::move(lowered));
        }
    }
    // Insert drops for normal block exit (no return/break/continue)
    auto last_kind = result.stmts.empty()
        ? lir_schema::stmt::Code(0)
        : stmt_ref_of(result.stmts.back()).kind();
    if (result.stmts.empty() ||
        (last_kind != lir_schema::stmt::Code::Return &&
         last_kind != lir_schema::stmt::Code::Break &&
         last_kind != lir_schema::stmt::Code::Continue)) {
        for (auto& d : collect_drops())
            result.stmts.push_back(std::move(d));
    }
    pop_scope();
    return result;
}

lir::LStmt SemaChecker::lower_let_destruct(TinyMapView node) {
    lir::LExprPtr rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    TypeRef rhs_type = rhs->type;
    if (TypeRef(rhs_type).kind() != LogosType::Kind::Tuple) {
        error(std::format("let (...) = ...: right-hand side must be a tuple, got {}",
              type_str(rhs_type)));
        return make_stmt(node_line_, lir::SExprStmt{std::move(rhs)});
    }

    // Collect binding names
    std::vector<std::string> names;
    if (node.has_key(la::NAMES)) {
        auto nlist = map_of(node.get(la::NAMES.code));
        if (nlist.has_key(la::ITEMS)) {
            auto arr = arr_of(nlist.get(la::ITEMS.code));
            for (uint64_t i = 0; i < arr.size(); ++i) {
                auto bnode = map_of(arr.get(i));
                names.push_back(std::string(str_of(bnode.get(la::NAME.code))));
            }
        }
    }
    if (names.size() != TypeRef(rhs_type).tuple_elems().size()) {
        error(std::format("let (...) = ...: expected {} bindings, got {}",
              TypeRef(rhs_type).tuple_elems().size(), names.size()));
    }

    // Build SBlock: let __destruct_N = rhs; let a = __destruct_N.0; ...
    std::string tmp = std::format("__destruct_{}", destruct_counter_++);

    auto blk = std::make_unique<lir::LBlock>();

    // let __destruct_N = rhs
    define(tmp, rhs_type);
    lir::SLet tmp_let;
    tmp_let.name    = tmp;
    tmp_let.type    = rhs_type;
    tmp_let.is_mut  = false;
    tmp_let.value   = std::move(rhs);
    blk->stmts.push_back(make_stmt(node_line_, std::move(tmp_let)));

    // let name_i = __destruct_N.i
    for (size_t i = 0; i < names.size() && i < TypeRef(rhs_type).tuple_elems().size(); ++i) {
        auto elem_t = TypeRef(rhs_type).tuple_elems()[i];
        define(names[i], elem_t);

        auto tmp_ref = builder().var_ref(tmp, rhs_type);
        auto elem_expr = builder().tuple_index(std::move(tmp_ref), (uint32_t)i, elem_t);

        lir::SLet elem_let;
        elem_let.name   = names[i];
        elem_let.type   = elem_t;
        elem_let.is_mut = false;
        elem_let.value  = std::move(elem_expr);
        blk->stmts.push_back(make_stmt(node_line_, std::move(elem_let)));
    }

    lir::SBlock sb;
    sb.body = std::move(blk);
    return make_stmt(node_line_, std::move(sb));
}

lir::LStmt SemaChecker::lower_let_else(TinyMapView node) {
    // let Pat = expr else { block };
    // The pattern's bindings go into the outer scope after this statement.
    // Lowering:
    //   1. Lower scrutinee expression.
    //   2. Build pattern (determine bindings and their types).
    //   3. Lower else block in a nested scope (must diverge).
    //   4. Add pattern bindings to outer scope.
    //   5. Emit SLetElse { pat, scrut, else_block }.

    // 1. Lower scrutinee
    lir::LExprPtr scrut = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    TypeRef scrut_type = scrut->type;

    // 2. Build pattern (this also validates binding types)
    auto pat_node = map_of(node.get(la::PAT.code));
    // pattern rule wraps everything in PAT_OR, so unwrap single-element PAT_OR
    TinyMapView pat_inner = pat_node;
    if (code_of(pat_node) == la::PAT_OR && pat_node.has_key(la::ITEMS)) {
        auto arr = arr_of(pat_node.get(la::ITEMS.code));
        if (arr.size() == 1) pat_inner = map_of(arr.get(0));
    }
    lir::Pattern pat = build_pattern(pat_node, scrut_type);

    // 3. Lower else block in nested scope (diverging)
    push_scope();
    lir::LBlock else_blk;
    if (node.has_key(la::BODY)) {
        else_blk = lower_block(map_of(node.get(la::BODY.code)));
    }
    pop_scope();

    // 4. Add pattern bindings to outer scope
    if (auto* pvd = std::get_if<lir::PatVariantData>(&pat.kind)) {
        for (size_t i = 0; i < pvd->bindings.size() && i < pvd->binding_types.size(); ++i)
            define(pvd->bindings[i], pvd->binding_types[i]);
    } else if (auto* pt = std::get_if<lir::PatTuple>(&pat.kind)) {
        for (size_t i = 0; i < pt->bindings.size() && i < pt->binding_types.size(); ++i)
            if (pt->bindings[i] != "_")
                define(pt->bindings[i], pt->binding_types[i]);
    } else if (auto* pw = std::get_if<lir::PatWild>(&pat.kind)) {
        if (pw->name != "_")
            define(pw->name, scrut_type);
    }
    // PatVariant (no bindings) — nothing to define

    // 5. Emit SLetElse
    lir::SLetElse sle;
    sle.pat        = std::move(pat);
    sle.scrut      = std::move(scrut);
    sle.else_block = std::make_unique<lir::LBlock>(std::move(else_blk));
    return make_stmt(node_line_, std::move(sle));
}

lir::LStmt SemaChecker::lower_let(TinyMapView node) {
    auto name = str_of(node.get(la::NAME.code));
    bool is_mut = false;
    if (node.has_key(la::IS_MUT)) {
        AnyVal av = node.get(la::IS_MUT.code);
        if (!av.is_null() && av.is_value()) is_mut = av.as_value<uint8_t>() != 0;
    }

    // Parse type annotation first so we can use it as a hint for enum literal inference
    TypeRef ann = nullptr;
    if (node.has_key(la::TYPE))
        ann = resolve_type(map_of(node.get(la::TYPE.code)));

    // Set enum/struct hints so literal lowering can fill in unresolved type params
    auto saved_hint = hint_enum_type_;
    if (ann && TypeRef(ann).kind() == LogosType::Kind::Enum && !TypeRef(ann).type_args().empty())
        hint_enum_type_ = ann;
    auto saved_struct_hint = hint_struct_type_;
    if (ann && (TypeRef(ann).kind() == LogosType::Kind::Struct ||
                TypeRef(ann).kind() == LogosType::Kind::ZonedStruct) && !TypeRef(ann).type_args().empty())
        hint_struct_type_ = ann;

    lir::LExprPtr rhs;
    TypeRef rhs_type;
    if (node.has_key(la::VALUE)) {
        rhs      = lower_expr(map_of(node.get(la::VALUE.code)));
        rhs_type = rhs->type;
    } else {
        error(std::format("let '{}': missing value", name));
        rhs      = error_expr();
        rhs_type = error_t();
    }

    hint_enum_type_ = saved_hint;
    hint_struct_type_ = saved_struct_hint;

    TypeRef var_type;
    if (ann != nullptr) {
        // impl Trait annotation: any concrete struct/class that was returned from an
        // impl-Trait-returning function is acceptable — treat the variable type as the
        // concrete rhs type so method calls work.
        bool ann_is_impl = TypeRef(ann).kind() == LogosType::Kind::ImplTrait;
        if (!ann_is_impl &&
            TypeRef(ann).kind() != LogosType::Kind::Error &&
            TypeRef(rhs_type).kind() != LogosType::Kind::Error &&
            !types_compatible(rhs_type, ann)) {
            // Non-capturing closure literal → fn(...) -> T coercion.
            if (try_coerce_closure_to_fnptr(rhs, ann)) {
                rhs_type = rhs->type;
            } else {
                error(std::format("let '{}': type mismatch — expected {}, got {}",
                      name, type_str(ann), type_str(rhs_type)));
            }
        }
        // Implicit safe integer widening: u32 → i64, i32 → i64, u8 → u32, ...
        if (ann && is_integer_kind(TypeRef(ann).kind()) && is_integer_kind(TypeRef(rhs_type).kind()) &&
            TypeRef(rhs_type).kind() != LogosType::Kind::IntLit &&
            TypeRef(rhs_type).kind() != LogosType::Kind::Enum &&
            can_widen_int(TypeRef(rhs_type).kind(), TypeRef(ann).kind())) {
            widen_int_expr(rhs, ann, builder());
            rhs_type = rhs->type;
        }
        // Retype float literal to concrete annotation type (f32 or f64).
        if (TypeRef(rhs_type).kind() == LogosType::Kind::FloatLit && ann &&
            (TypeRef(ann).kind() == LogosType::Kind::F32 || TypeRef(ann).kind() == LogosType::Kind::F64))
            rhs->type = ann;
        // Retype/coerce integer literal (or IntLit-typed expr) to float annotation type (f32 or f64).
        if (TypeRef(rhs_type).kind() == LogosType::Kind::IntLit && ann &&
            (TypeRef(ann).kind() == LogosType::Kind::F32 || TypeRef(ann).kind() == LogosType::Kind::F64)) {
            if (auto* il = std::get_if<lir::ELitInt>(&rhs->kind)) {
                // Simple integer literal: convert directly to float literal.
                // Build a fresh node so the Hermes mirror is emitted with
                // Code::LitFloat (in-place mutation would leave the stale
                // LitInt mirror that view-based readers in mono pick up).
                double fval = static_cast<double>(il->value);
                rhs = builder().lit_float(fval, ann);
            } else {
                // Non-literal IntLit expression (e.g. 1 + 2): wrap in ECast to float.
                rhs = builder().cast(std::move(rhs), ann);
                rhs_type = ann;
            }
        }
        // Detect integer literals that don't fit in the annotated type.
        if (TypeRef(rhs_type).kind() == LogosType::Kind::IntLit && TypeRef(ann).kind() != LogosType::Kind::Error) {
            if (auto v = get_intlit_value(rhs.get()))
                if (!intlit_fits(*v, TypeRef(ann).kind()))
                    error(std::format("let '{}': literal value {} does not fit in {}",
                          name, *v, type_str(ann)));
        }
        // Check each IntLit array element fits in the annotation's element type.
        if (TypeRef(ann).kind() == LogosType::Kind::Array && TypeRef(ann).elem() &&
            TypeRef(rhs_type).kind() == LogosType::Kind::Array && TypeRef(rhs_type).elem() &&
            TypeRef(rhs_type).elem().kind() == LogosType::Kind::IntLit) {
            if (auto* arrlit = std::get_if<lir::EArrLit>(&rhs->kind)) {
                for (size_t ei = 0; ei < arrlit->elems.size(); ++ei) {
                    if (auto v = get_intlit_value(arrlit->elems[ei].get()))
                        if (!intlit_fits(*v, TypeRef(ann).elem().kind()))
                            error(std::format("let '{}': array element {}: value {} does not fit in {}",
                                  name, ei, *v, type_str(TypeRef(ann).elem())));
                }
            }
        }
        // Check each IntLit tuple element fits in the annotation's element type.
        // Also retype FloatLit tuple elements to concrete float annotation types.
        if (TypeRef(ann).kind() == LogosType::Kind::Tuple &&
            TypeRef(rhs_type).kind() == LogosType::Kind::Tuple) {
            if (auto* tlit = std::get_if<lir::ETupleLit>(&rhs->kind)) {
                for (size_t ei = 0; ei < tlit->elems.size() && ei < TypeRef(ann).tuple_elems().size(); ++ei) {
                    // Retype FloatLit element to concrete float annotation (f32/f64).
                    if (TypeRef(tlit->elems[ei]->type).kind() == LogosType::Kind::FloatLit && TypeRef(ann).tuple_elems()[ei] &&
                        (TypeRef(TypeRef(ann).tuple_elems()[ei]).kind() == LogosType::Kind::F32 ||
                         TypeRef(TypeRef(ann).tuple_elems()[ei]).kind() == LogosType::Kind::F64)) {
                        tlit->elems[ei]->type = TypeRef(ann).tuple_elems()[ei];
                    }
                    // Retype IntLit element to concrete float annotation (f32/f64).
                    if (TypeRef(tlit->elems[ei]->type).kind() == LogosType::Kind::IntLit && TypeRef(ann).tuple_elems()[ei] &&
                        (TypeRef(TypeRef(ann).tuple_elems()[ei]).kind() == LogosType::Kind::F32 ||
                         TypeRef(TypeRef(ann).tuple_elems()[ei]).kind() == LogosType::Kind::F64)) {
                        if (auto* il = std::get_if<lir::ELitInt>(&tlit->elems[ei]->kind)) {
                            double fval = static_cast<double>(il->value);
                            tlit->elems[ei] = builder().lit_float(
                                fval, TypeRef(ann).tuple_elems()[ei]);
                        }
                    }
                    if (TypeRef(tlit->elems[ei]->type).kind() == LogosType::Kind::IntLit)
                        if (auto v = get_intlit_value(tlit->elems[ei].get()))
                            if (TypeRef(ann).tuple_elems()[ei] &&
                                !intlit_fits(*v, TypeRef(TypeRef(ann).tuple_elems()[ei]).kind()))
                                error(std::format("let '{}': tuple element {}: value {} does not fit in {}",
                                      name, ei, *v, type_str(TypeRef(ann).tuple_elems()[ei])));
                    // Tuple element is itself an array literal.
                    if (TypeRef(ann).tuple_elems()[ei] && TypeRef(TypeRef(ann).tuple_elems()[ei]).kind() == LogosType::Kind::Array &&
                        TypeRef(TypeRef(ann).tuple_elems()[ei]).elem() && TypeRef(tlit->elems[ei]->type).kind() == LogosType::Kind::Array)
                        if (auto* ial = std::get_if<lir::EArrLit>(&tlit->elems[ei]->kind))
                            for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                if (TypeRef(ial->elems[ii]->type).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(ial->elems[ii].get()))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(ann).tuple_elems()[ei]).elem().kind()))
                                            error(std::format("let '{}': tuple element {}: array element {}: value {} does not fit in {}",
                                                  name, ei, ii, *v, type_str(TypeRef(TypeRef(ann).tuple_elems()[ei]).elem())));
                    // Tuple element is itself a tuple literal.
                    if (TypeRef(ann).tuple_elems()[ei] && TypeRef(TypeRef(ann).tuple_elems()[ei]).kind() == LogosType::Kind::Tuple &&
                        TypeRef(tlit->elems[ei]->type).kind() == LogosType::Kind::Tuple)
                        if (auto* itl = std::get_if<lir::ETupleLit>(&tlit->elems[ei]->kind))
                            for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(TypeRef(ann).tuple_elems()[ei]).tuple_elems().size(); ++ii)
                                if (TypeRef(itl->elems[ii]->type).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(itl->elems[ii].get()))
                                        if (TypeRef(TypeRef(ann).tuple_elems()[ei]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(ann).tuple_elems()[ei]).tuple_elems()[ii]).kind()))
                                            error(std::format("let '{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  name, ei, ii, *v, type_str(TypeRef(TypeRef(ann).tuple_elems()[ei]).tuple_elems()[ii])));
                }
            }
        }
        // For impl Trait annotations, use the concrete rhs type so that method calls work.
        var_type = ann_is_impl ? rhs_type : ann;
        // Retype the rhs tuple expression node to use the concrete annotation tuple type.
        // This ensures codegen sees (f32, f32) instead of (FloatLit, FloatLit).
        if (!ann_is_impl && TypeRef(ann).kind() == LogosType::Kind::Tuple &&
            TypeRef(rhs_type).kind() == LogosType::Kind::Tuple)
            rhs->type = ann;
    } else {
        var_type = rhs_type;
        if (TypeRef(var_type).kind() == LogosType::Kind::IntLit) {
            // Default IntLit to i32; upgrade to i64 if the literal value overflows i32.
            var_type = i32_t();
            if (auto* lit = std::get_if<lir::ELitInt>(&rhs->kind))
                if (lit->value > (int64_t)INT32_MAX || lit->value < (int64_t)INT32_MIN)
                    var_type = prim(LogosType::Kind::I64);
        }
        if (TypeRef(var_type).kind() == LogosType::Kind::FloatLit) {
            // Default FloatLit to f64.
            var_type = prim(LogosType::Kind::F64);
            rhs->type = var_type;
        }
    }

    define(name, var_type, is_mut);

    // Move semantics: if RHS is a variable reference to a move type, mark it moved
    if (rhs && is_move_type(rhs_type)) {
        if (auto* vr = std::get_if<lir::EVarRef>(&rhs->kind))
            mark_moved(vr->name);
    }

    lir::SLet slet;
    slet.name   = std::string(name);
    slet.type   = var_type;
    slet.is_mut = is_mut;
    slet.value  = std::move(rhs);
    return make_stmt(node_line_, std::move(slet));
}

lir::LStmt SemaChecker::lower_compound_assign(TinyMapView node) {
    auto name = str_of(node.get(la::NAME.code));
    auto op_tok = str_of(node.get(la::OP.code));
    // Strip trailing '=' to get the base operator
    std::string base_op;
    if (op_tok.size() >= 2 && op_tok.back() == '=')
        base_op = std::string(op_tok.substr(0, op_tok.size() - 1));
    else
        base_op = std::string(op_tok);  // fallback

    auto var_type = lookup(name);
    if (!var_type) {
        error(std::format("compound assignment to undefined variable '{}'", name));
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return make_stmt(node_line_, lir::SBreak{});
    }
    if (!lookup_is_mut(name))
        error(std::format("compound assignment to immutable variable '{}'", name));

    // Desugar: `x op= expr` → `x = x op expr`
    auto lhs_ref = builder().var_ref(std::string(name), var_type);
    auto rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();

    // Type-check: RHS must be compatible with the variable's type.
    if (TypeRef(var_type).kind() != LogosType::Kind::Error &&
        TypeRef(rhs->type).kind() != LogosType::Kind::Error &&
        !types_compatible(rhs->type, var_type)) {
        error(std::format("compound assignment to '{}': type mismatch — expected {}, got {}",
              name, type_str(var_type), type_str(rhs->type)));
    }
    // Synthesize the binop LIR node
    auto binop = builder().bin_op(base_op, std::move(lhs_ref), std::move(rhs), var_type);
    return make_stmt(node_line_, lir::SAssign{std::string(name), std::move(binop)});
}

lir::LStmt SemaChecker::lower_assign(TinyMapView node) {
    auto name = str_of(node.get(la::NAME.code));
    auto var_type = lookup(name);
    if (!var_type) {
        error(std::format("assignment to undefined variable '{}'", name));
        lir::LExprPtr dummy = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code)))
            : error_expr();
        return make_stmt(node_line_, lir::SAssign{std::string(name), std::move(dummy)});
    }
    if (!lookup_is_mut(name))
        error(std::format("assignment to immutable variable '{}'", name));

    lir::LExprPtr rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    if (TypeRef(var_type).kind() != LogosType::Kind::Error &&
        TypeRef(rhs->type).kind() != LogosType::Kind::Error &&
        !types_compatible(rhs->type, var_type)) {
        error(std::format("assignment to '{}': type mismatch — expected {}, got {}",
              name, type_str(var_type), type_str(rhs->type)));
    }
    // Implicit safe integer widening on assignment.
    if (var_type && is_integer_kind(TypeRef(var_type).kind()) && is_integer_kind(TypeRef(rhs->type).kind()) &&
        TypeRef(rhs->type).kind() != LogosType::Kind::IntLit &&
        TypeRef(rhs->type).kind() != LogosType::Kind::Enum &&
        can_widen_int(TypeRef(rhs->type).kind(), TypeRef(var_type).kind())) {
        widen_int_expr(rhs, var_type, builder());
    }
    // Check IntLit literal fits in the variable's declared type.
    if (TypeRef(rhs->type).kind() == LogosType::Kind::IntLit &&
        TypeRef(var_type).kind() != LogosType::Kind::Error) {
        if (auto v = get_intlit_value(rhs.get()))
            if (!intlit_fits(*v, TypeRef(var_type).kind()))
                error(std::format("assignment to '{}': value {} does not fit in {}",
                      name, *v, type_str(var_type)));
    }
    // Check array literal elements against narrow array variable type.
    if (TypeRef(rhs->type).kind() == LogosType::Kind::Array &&
        TypeRef(var_type).kind() == LogosType::Kind::Array && TypeRef(var_type).elem())
        if (auto* al = std::get_if<lir::EArrLit>(&rhs->kind))
            for (size_t i = 0; i < al->elems.size(); ++i)
                if (TypeRef(al->elems[i]->type).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(al->elems[i].get()))
                        if (!intlit_fits(*v, TypeRef(var_type).elem().kind()))
                            error(std::format("assignment to '{}': array element {}: value {} does not fit in {}",
                                  name, i, *v, type_str(TypeRef(var_type).elem())));
    // Check tuple literal elements against narrow tuple variable element types.
    if (TypeRef(rhs->type).kind() == LogosType::Kind::Tuple && TypeRef(var_type).kind() == LogosType::Kind::Tuple)
        if (auto* tl = std::get_if<lir::ETupleLit>(&rhs->kind))
            for (size_t i = 0; i < tl->elems.size() && i < TypeRef(var_type).tuple_elems().size(); ++i) {
                if (TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(tl->elems[i].get()))
                        if (TypeRef(var_type).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(var_type).tuple_elems()[i]).kind()))
                            error(std::format("assignment to '{}': tuple element {}: value {} does not fit in {}",
                                  name, i, *v, type_str(TypeRef(var_type).tuple_elems()[i])));
                if (TypeRef(var_type).tuple_elems()[i] && TypeRef(TypeRef(var_type).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                    TypeRef(TypeRef(var_type).tuple_elems()[i]).elem() && TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::Array)
                    if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[i]->kind))
                        for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                            if (TypeRef(ial->elems[ii]->type).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(ial->elems[ii].get()))
                                    if (!intlit_fits(*v, TypeRef(TypeRef(var_type).tuple_elems()[i]).elem().kind()))
                                        error(std::format("assignment to '{}': tuple element {}: array element {}: value {} does not fit in {}",
                                              name, i, ii, *v, type_str(TypeRef(TypeRef(var_type).tuple_elems()[i]).elem())));

                if (TypeRef(var_type).tuple_elems()[i] && TypeRef(TypeRef(var_type).tuple_elems()[i]).kind() == LogosType::Kind::Tuple &&
                    TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::Tuple)
                    if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[i]->kind))
                        for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(TypeRef(var_type).tuple_elems()[i]).tuple_elems().size(); ++ii)
                            if (TypeRef(itl->elems[ii]->type).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(itl->elems[ii].get()))
                                    if (TypeRef(TypeRef(var_type).tuple_elems()[i]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(var_type).tuple_elems()[i]).tuple_elems()[ii]).kind()))
                                        error(std::format("assignment to '{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                              name, i, ii, *v, type_str(TypeRef(TypeRef(var_type).tuple_elems()[i]).tuple_elems()[ii])));
                }
    // Re-assignment revives the variable (the old value was already consumed).
    moved_vars_.erase(std::string(name));

    return make_stmt(node_line_, lir::SAssign{std::string(name), std::move(rhs)});
}

lir::LStmt SemaChecker::lower_return(TinyMapView node) {
    lir::LExprPtr val;
    if (node.has_key(la::VALUE)) {
        AnyVal vav = node.get(la::VALUE.code);
        if (!vav.is_null()) {
            // Set enum/struct hints from return type so literals can fill in unresolved type params
            auto saved_hint = hint_enum_type_;
            if (ret_type_ && TypeRef(ret_type_).kind() == LogosType::Kind::Enum && !TypeRef(ret_type_).type_args().empty())
                hint_enum_type_ = ret_type_;
            auto saved_struct_hint = hint_struct_type_;
            if (ret_type_ && (TypeRef(ret_type_).kind() == LogosType::Kind::Struct ||
                              TypeRef(ret_type_).kind() == LogosType::Kind::ZonedStruct) &&
                !TypeRef(ret_type_).type_args().empty())
                hint_struct_type_ = ret_type_;
            val = lower_expr(map_of(vav));
            hint_enum_type_ = saved_hint;
            hint_struct_type_ = saved_struct_hint;
            if (ret_type_ && TypeRef(ret_type_).kind() == LogosType::Kind::ImplTrait) {
                // Infer concrete return type from first return expression.
                if (!impl_ret_type_inferred_ &&
                    TypeRef(val->type).kind() != LogosType::Kind::Error)
                    impl_ret_type_inferred_ = val->type;
            } else if (ret_type_ && TypeRef(ret_type_).kind() != LogosType::Kind::Error &&
                TypeRef(val->type).kind() != LogosType::Kind::Error &&
                !compat(val->type, ret_type_)) {
                error(std::format("return type mismatch — expected {}, got {}",
                      type_str(ret_type_), type_str(val->type)));
            }
            // Retype float literal to concrete return type.
            if (ret_type_ && TypeRef(val->type).kind() == LogosType::Kind::FloatLit &&
                (TypeRef(ret_type_).kind() == LogosType::Kind::F32 || TypeRef(ret_type_).kind() == LogosType::Kind::F64))
                val->type = ret_type_;
            else if (TypeRef(val->type).kind() == LogosType::Kind::FloatLit)
                val->type = prim(LogosType::Kind::F64);
            // Detect integer literals that don't fit in the return type.
            if (ret_type_ && TypeRef(val->type).kind() == LogosType::Kind::IntLit &&
                TypeRef(ret_type_).kind() != LogosType::Kind::Error) {
                if (auto v = get_intlit_value(val.get()))
                    if (!intlit_fits(*v, TypeRef(ret_type_).kind()))
                        error(std::format("return: literal value {} does not fit in {}",
                              *v, type_str(ret_type_)));
            }
            // Detect array literal elements that don't fit in the return element type.
            if (ret_type_ && TypeRef(ret_type_).kind() == LogosType::Kind::Array && TypeRef(ret_type_).elem() &&
                TypeRef(val->type).kind() == LogosType::Kind::Array)
                if (auto* al = std::get_if<lir::EArrLit>(&val->kind))
                    for (size_t i = 0; i < al->elems.size(); ++i)
                        if (TypeRef(al->elems[i]->type).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(al->elems[i].get()))
                                if (!intlit_fits(*v, TypeRef(ret_type_).elem().kind()))
                                    error(std::format("return: array element {}: value {} does not fit in {}",
                                          i, *v, type_str(TypeRef(ret_type_).elem())));
            // Detect tuple literal elements that don't fit in the return tuple element types.
            if (ret_type_ && TypeRef(ret_type_).kind() == LogosType::Kind::Tuple &&
                TypeRef(val->type).kind() == LogosType::Kind::Tuple)
                if (auto* tl = std::get_if<lir::ETupleLit>(&val->kind))
                    for (size_t i = 0; i < tl->elems.size() && i < TypeRef(ret_type_).tuple_elems().size(); ++i) {
                        if (TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(tl->elems[i].get()))
                                if (TypeRef(ret_type_).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(ret_type_).tuple_elems()[i]).kind()))
                                    error(std::format("return: tuple element {}: value {} does not fit in {}",
                                          i, *v, type_str(TypeRef(ret_type_).tuple_elems()[i])));
                        if (TypeRef(ret_type_).tuple_elems()[i] && TypeRef(TypeRef(ret_type_).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(ret_type_).tuple_elems()[i]).elem() && TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::Array)
                            if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[i]->kind))
                                for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                                    if (TypeRef(ial->elems[ii]->type).kind() == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(ial->elems[ii].get()))
                                            if (!intlit_fits(*v, TypeRef(TypeRef(ret_type_).tuple_elems()[i]).elem().kind()))
                                                error(std::format("return: tuple element {}: array element {}: value {} does not fit in {}",
                                                      i, ii, *v, type_str(TypeRef(TypeRef(ret_type_).tuple_elems()[i]).elem())));

                        if (TypeRef(ret_type_).tuple_elems()[i] && TypeRef(TypeRef(ret_type_).tuple_elems()[i]).kind() == LogosType::Kind::Tuple &&
                            TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::Tuple)
                            if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[i]->kind))
                                for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(TypeRef(ret_type_).tuple_elems()[i]).tuple_elems().size(); ++ii)
                                    if (TypeRef(itl->elems[ii]->type).kind() == LogosType::Kind::IntLit)
                                        if (auto v = get_intlit_value(itl->elems[ii].get()))
                                            if (TypeRef(TypeRef(ret_type_).tuple_elems()[i]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(ret_type_).tuple_elems()[i]).tuple_elems()[ii]).kind()))
                                                error(std::format("return: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                      i, ii, *v, type_str(TypeRef(TypeRef(ret_type_).tuple_elems()[i]).tuple_elems()[ii])));
                        }
            // Move semantics: recursively mark any move-type variable that
            // appears in the return expression as moved, so collect_all_drops()
            // won't also drop them (avoids double-free).
            std::function<void(const lir::LExpr*)> mark_moved_in_expr;
            mark_moved_in_expr = [&](const lir::LExpr* e) {
                if (!e) return;
                if (auto* vr = std::get_if<lir::EVarRef>(&e->kind)) {
                    if (is_move_type(e->type)) mark_moved(vr->name);
                    return;
                }
                if (auto* ev = std::get_if<lir::EEnumLitData>(&e->kind)) {
                    for (auto& a : ev->payload) mark_moved_in_expr(a.get());
                    return;
                }
                if (auto* ec = std::get_if<lir::ECall>(&e->kind)) {
                    for (auto& a : ec->args) mark_moved_in_expr(a.get());
                    return;
                }
                if (auto* es = std::get_if<lir::EStructLit>(&e->kind)) {
                    for (auto& f : es->fields) mark_moved_in_expr(f.second.get());
                    return;
                }
                if (auto* et = std::get_if<lir::ETupleLit>(&e->kind)) {
                    for (auto& a : et->elems) mark_moved_in_expr(a.get());
                    return;
                }
                if (auto* blk = std::get_if<lir::EBlockExpr>(&e->kind)) {
                    if (blk->result) mark_moved_in_expr(blk->result.get());
                    return;
                }
            };
            mark_moved_in_expr(val.get());
            return make_stmt(node_line_, lir::SReturn{std::move(val)});
        }
    }
    // void return
    if (ret_type_ && TypeRef(ret_type_).kind() != LogosType::Kind::Void &&
        TypeRef(ret_type_).kind() != LogosType::Kind::Error &&
        TypeRef(ret_type_).kind() != LogosType::Kind::ImplTrait) {
        error(std::format("return without value in function returning {}",
              type_str(ret_type_)));
    }
    return make_stmt(node_line_, lir::SReturn{nullptr});
}

lir::Pattern SemaChecker::build_pattern(TinyMapView pnode, TypeRef scrut_type) {
    auto result = build_pattern_impl(pnode, scrut_type);
    // A.2: eager mirror so `pat_ref_of` works during sema and downstream
    // back-fill is a no-op (cache hits via mirror_offset_).
    lir_mirror_emit_pat_node(*cur_prog_, result);
    return result;
}

lir::Pattern SemaChecker::build_pattern_impl(TinyMapView pnode, TypeRef scrut_type) {
    int32_t pc = code_of(pnode);
    if (pc == la::PAT_VARIANT) {
        auto pename = std::string(str_of(pnode.get(la::NAME.code)));
        auto pvname = std::string(str_of(pnode.get(la::FIELD.code)));
        int32_t disc = 0;
        auto [epkg_pv, esi_pv] = find_enum_by_name(pename);
        auto eit = esi_pv ? enums_.find(sema_key(epkg_pv, pename)) : enums_.end();
        if (eit == enums_.end()) eit = enums_.find(pename);
        if (eit == enums_.end()) {
            error(std::format("pattern: unknown enum '{}'", pename));
        } else {
            // NS5: guard scrut_type null before accessing kind (could be null for unknown types).
            if (scrut_type && TypeRef(scrut_type).kind() == LogosType::Kind::Enum &&
                TypeRef(scrut_type).enum_name() != pename)
                error(std::format("pattern: enum '{}' != scrutinee '{}'",
                      pename, type_str(scrut_type)));
            bool found = false;
            for (auto& v : eit->second.variants)
                if (v.name == pvname) { disc = v.value; found = true; break; }
            if (!found)
                error(std::format("pattern: enum '{}' has no variant '{}'", pename, pvname));
        }
        return lir::PatVariant{pename, pvname, disc};
    }
    if (pc == la::PAT_VARIANT_DATA) {
        auto pename = std::string(str_of(pnode.get(la::NAME.code)));
        auto pvname = std::string(str_of(pnode.get(la::FIELD.code)));
        int32_t disc = 0;
        const SemaVariantInfo* vinfo = nullptr;
        auto [epkg_pvd, esi_pvd] = find_enum_by_name(pename);
        auto eit = esi_pvd ? enums_.find(sema_key(epkg_pvd, pename)) : enums_.end();
        if (eit == enums_.end()) eit = enums_.find(pename);
        if (eit == enums_.end()) {
            error(std::format("pattern: unknown enum '{}'", pename));
        } else {
            for (auto& v : eit->second.variants)
                if (v.name == pvname) { vinfo = &v; disc = v.value; break; }
            if (!vinfo)
                error(std::format("pattern: enum '{}' has no variant '{}'", pename, pvname));
        }
        std::vector<std::string> bindings;
        if (pnode.has_key(la::ARGS)) {
            AnyVal aav = pnode.get(la::ARGS.code);
            if (!aav.is_null() && aav.is_pointer()) {
                auto blist = map_of(aav);
                if (blist.has_key(la::ITEMS)) {
                    auto bitems = arr_of(blist.get(la::ITEMS.code));
                    for (uint64_t j = 0; j < bitems.size(); ++j) {
                        auto bnode = map_of(bitems.get(j));
                        if (!bnode.has_key(la::NAME)) continue;  // () unit — no binding
                        bindings.push_back(std::string(str_of(bnode.get(la::NAME.code))));
                    }
                }
            }
        }
        std::vector<TypeRef> binding_types;
        if (vinfo) {
            SemaSubst subst;
            if (TypeRef(scrut_type).kind() == LogosType::Kind::Enum &&
                !TypeRef(scrut_type).type_args().empty()) {
                auto& einfo = eit->second;
                for (size_t k = 0; k < einfo.type_params.size() &&
                                    k < TypeRef(scrut_type).type_args().size(); ++k)
                    subst[einfo.type_params[k].name] = TypeRef(scrut_type).type_args()[k];
            }
            for (auto pt : vinfo->payload_types) {
                auto ct = subst.empty() ? pt : subst_type_sema(pt, subst);
                if (TypeRef(ct).kind() == LogosType::Kind::Void) continue;  // () unit — no field
                binding_types.push_back(ct);
            }
        }
        if (bindings.size() != binding_types.size())
            error(std::format("pattern {}::{}: expected {} bindings, got {}",
                  pename, pvname, binding_types.size(), bindings.size()));
        return lir::PatVariantData{pename, pvname, disc,
                                   std::move(bindings), std::move(binding_types)};
    }
    if (pc == la::PAT_INT || pc == la::PAT_NEG_INT) {
        auto sv = str_of(pnode.get(la::VALUE.code));
        int64_t v = parse_int_literal(sv);
        if (pc == la::PAT_NEG_INT) v = -v;
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error) {
            if (!is_integer(scrut_type))
                error(std::format("integer pattern requires integer scrutinee, got '{}'",
                      type_str(scrut_type)));
            else if (!intlit_fits(v, TypeRef(scrut_type).kind()))
                error(std::format("match pattern: value {} does not fit in {}",
                      v, type_str(scrut_type)));
        }
        return lir::PatInt{v};
    }
    if (pc == la::PAT_OR) {
        auto arr = arr_of(pnode.get(la::ITEMS.code));
        // Single-item PAT_OR (no PIPE) — treat as the inner pattern.
        if (arr.size() == 1)
            return build_pattern(map_of(arr.get(0)), scrut_type);
        lir::PatOr por;
        for (uint64_t i = 0; i < arr.size(); ++i)
            por.alts.push_back(build_pattern(map_of(arr.get(i)), scrut_type));
        // NG4: validate that all alternatives bind the exact same set of names.
        std::function<void(const lir::Pattern&, std::vector<std::string>&)> collect_names;
        collect_names = [&](const lir::Pattern& p, std::vector<std::string>& out) {
            if (auto* pw  = std::get_if<lir::PatWild>(&p.kind))
                { if (pw->name != "_") out.push_back(pw->name); }
            else if (auto* pa  = std::get_if<lir::PatAt>(&p.kind))
                { if (pa->name != "_") out.push_back(pa->name);
                  if (!pa->sub.empty()) collect_names(pa->sub[0], out); }
            else if (auto* pt  = std::get_if<lir::PatTuple>(&p.kind))
                { for (auto& n : pt->bindings) if (n != "_") out.push_back(n); }
            else if (auto* pst = std::get_if<lir::PatStruct>(&p.kind))
                { for (auto& f : pst->fields)
                    { if (!f.sub.empty()) collect_names(f.sub[0], out);
                      else out.push_back(f.field_name); } }
            else if (auto* pvd = std::get_if<lir::PatVariantData>(&p.kind))
                { for (auto& n : pvd->bindings) if (n != "_") out.push_back(n); }
            else if (auto* por2 = std::get_if<lir::PatOr>(&p.kind))
                { if (!por2->alts.empty()) collect_names(por2->alts[0], out); }
            else if (auto* prb = std::get_if<lir::PatRefBind>(&p.kind))
                { if (!prb->name.empty() && prb->name != "_") out.push_back(prb->name); }
            else if (auto* prp = std::get_if<lir::PatRefPat>(&p.kind))
                { if (!prp->inner.empty()) collect_names(prp->inner[0], out); }
            else if (auto* psl = std::get_if<lir::PatSlice>(&p.kind))
                { for (auto& sp : psl->prefix) collect_names(sp, out);
                  for (auto& sp : psl->rest)   collect_names(sp, out);
                  for (auto& sp : psl->suffix) collect_names(sp, out); }
        };
        if (!por.alts.empty()) {
            std::vector<std::string> first_names;
            collect_names(por.alts[0], first_names);
            std::sort(first_names.begin(), first_names.end());
            for (size_t i = 1; i < por.alts.size(); ++i) {
                std::vector<std::string> alt_names;
                collect_names(por.alts[i], alt_names);
                std::sort(alt_names.begin(), alt_names.end());
                if (alt_names != first_names)
                    error(std::format("or-pattern: all alternatives must bind the same variable names"));
            }
        }
        return por;
    }
    if (pc == la::PAT_BOOL) {
        AnyVal bv = pnode.get(la::VALUE.code);
        bool bval = !bv.is_null() && bv.is_value() && bv.as_value<uint8_t>();
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error &&
            TypeRef(scrut_type).kind() != LogosType::Kind::Bool)
            error(std::format("bool pattern requires bool scrutinee, got '{}'",
                  type_str(scrut_type)));
        return lir::PatBool{bval};
    }
    if (pc == la::PAT_TUPLE) {
        // Tuple pattern: (a, b, c) — irrefutable, binds each element.
        // Scrutinee must be a tuple type.
        lir::PatTuple pt;
        if (!scrut_type || TypeRef(scrut_type).kind() != LogosType::Kind::Tuple) {
            error(std::format("tuple pattern requires tuple scrutinee, got {}",
                  scrut_type ? type_str(scrut_type) : "?"));
            return lir::PatWild{"_"};
        }
        // ITEMS holds an array of pat_single nodes (peg $... collects all captures).
        // Filter: only PAT_WILD nodes are the actual sub-patterns; LPAREN/RPAREN/COMMA
        // are not emitted as sub-nodes by the grammar (they are terminals that get
        // captured into rcap_0 as string tokens, not as sub-maps).
        // Actually the grammar captures ALL non-terminals into $... so each pat_single
        // is in ITEMS. Walk the ITEMS array and collect each sub-pattern's binding name.
        AnyVal items_av = pnode.get(la::ITEMS.code);
        if (!items_av.is_null() && items_av.is_pointer()) {
            auto items_arr = arr_of(items_av);
            for (uint64_t i = 0; i < items_arr.size(); ++i) {
                auto sub = map_of(items_arr.get(i));
                int32_t sc = code_of(sub);
                // Each element in a tuple pattern must be a simple binding (PAT_WILD = identifier)
                // or _ (wildcard). Nested patterns aren't supported yet.
                // Element type for this position, used by recursive build_pattern for type-checking.
                TypeRef elem_ty = nullptr;
                if (scrut_type && TypeRef(scrut_type).kind() == LogosType::Kind::Tuple &&
                    i < TypeRef(scrut_type).tuple_elems().size())
                    elem_ty = TypeRef(scrut_type).tuple_elems()[i];
                if (sc == la::PAT_WILD.code) {
                    auto nm = std::string(str_of(sub.get(la::NAME.code)));
                    pt.bindings.push_back(nm);
                    pt.subs.push_back(lir::PatWild{nm});
                } else if (sc == la::PAT_INT.code || sc == la::PAT_NEG_INT.code ||
                           sc == la::PAT_BOOL.code) {
                    pt.bindings.push_back("_");
                    pt.subs.push_back(build_pattern(sub, elem_ty));
                } else {
                    error("tuple pattern element: only _, name, integer, or bool literals are supported");
                    pt.bindings.push_back("_");
                    pt.subs.push_back(lir::PatWild{"_"});
                }
            }
        }
        // Verify count matches tuple arity.
        if (pt.bindings.size() != TypeRef(scrut_type).tuple_elems().size())
            error(std::format("tuple pattern: expected {} elements, got {}",
                  TypeRef(scrut_type).tuple_elems().size(), pt.bindings.size()));
        // Fill binding types from tuple elements.
        for (size_t i = 0; i < TypeRef(scrut_type).tuple_elems().size(); ++i)
            pt.binding_types.push_back(TypeRef(scrut_type).tuple_elems()[i]);
        return pt;
    }
    // ── PAT_RANGE: 0..=9 inclusive integer range ──────────────────────────
    if (pc == la::PAT_RANGE) {
        auto lo_sv = str_of(pnode.get(la::LHS.code));
        auto hi_sv = str_of(pnode.get(la::RHS.code));
        int64_t lo = parse_int_literal(lo_sv);
        int64_t hi = parse_int_literal(hi_sv);
        if (pnode.has_key(la::LO_NEG)) {
            AnyVal av = pnode.get(la::LO_NEG.code);
            if (!av.is_null() && av.is_value() && av.as_value<uint8_t>()) lo = -lo;
        }
        if (pnode.has_key(la::HI_NEG)) {
            AnyVal av = pnode.get(la::HI_NEG.code);
            if (!av.is_null() && av.is_value() && av.as_value<uint8_t>()) hi = -hi;
        }
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error &&
            !is_integer(scrut_type))
            error(std::format("range pattern requires integer scrutinee, got '{}'",
                  type_str(scrut_type)));
        // S2: validate that lo/hi fit in the scrutinee integer type.
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error && is_integer(scrut_type)) {
            if (!intlit_fits(lo, TypeRef(scrut_type).kind()))
                error(std::format("range pattern: lo ({}) does not fit in '{}'",
                      lo, type_str(scrut_type)));
            if (!intlit_fits(hi, TypeRef(scrut_type).kind()))
                error(std::format("range pattern: hi ({}) does not fit in '{}'",
                      hi, type_str(scrut_type)));
        }
        if (lo > hi)
            error(std::format("range pattern: lo ({}) > hi ({})", lo, hi));
        return lir::PatRange{lo, hi};
    }

    // ── PAT_AT: name @ sub_pat ────────────────────────────────────────────
    if (pc == la::PAT_AT) {
        auto bname = std::string(str_of(pnode.get(la::NAME.code)));
        auto sub_node = map_of(pnode.get(la::VALUE.code));
        auto sub_pat = build_pattern(sub_node, scrut_type);
        lir::PatAt pa;
        pa.name = bname;
        // NS3: scrut_type may be null for unknown types; fallback to error_t() so
        // bind_pattern can always define the variable (even with error type).
        pa.type = scrut_type ? scrut_type : error_t();
        pa.sub.push_back(std::move(sub_pat));
        return pa;
    }

    // ── PAT_REF: &pat or &mut pat ─────────────────────────────────────────
    if (pc == la::PAT_REF) {
        bool is_mut = pnode.has_key(la::IS_MUT) &&
                      pnode.get(la::IS_MUT.code).is_value() &&
                      pnode.get(la::IS_MUT.code).as_value<uint8_t>() != 0;
        TypeRef inner_type = error_t();
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error) {
            if (TypeRef(scrut_type).kind() == LogosType::Kind::Ref ||
                TypeRef(scrut_type).kind() == LogosType::Kind::MutRef) {
                // NS2: &mut pattern requires &mut scrutinee; & pattern accepts both.
                if (is_mut && TypeRef(scrut_type).kind() != LogosType::Kind::MutRef)
                    error(std::format("reference pattern: '&mut' requires '&mut' scrutinee, got '{}'",
                          type_str(scrut_type)));
                inner_type = TypeRef(scrut_type).pointee() ? TypeRef(scrut_type).pointee() : error_t();
            } else {
                // NS1: non-reference scrutinee with a reference pattern is always wrong.
                error(std::format("reference pattern requires reference scrutinee, got '{}'",
                      type_str(scrut_type)));
            }
        }
        auto sub_node = map_of(pnode.get(la::VALUE.code));
        auto inner_pat = build_pattern(sub_node, inner_type);
        lir::PatRefPat prp;
        prp.is_mut = is_mut;
        prp.inner.push_back(std::move(inner_pat));
        return prp;
    }

    // ── PAT_WILD with IS_REF: ref x or ref mut x ─────────────────────────
    if (pc == la::PAT_WILD || pnode.has_key(la::NAME)) {
        bool is_ref = pnode.has_key(la::IS_REF) &&
                      pnode.get(la::IS_REF.code).is_value() &&
                      pnode.get(la::IS_REF.code).as_value<uint8_t>() != 0;
        if (is_ref) {
            bool is_mut = pnode.has_key(la::IS_MUT) &&
                          pnode.get(la::IS_MUT.code).is_value() &&
                          pnode.get(la::IS_MUT.code).as_value<uint8_t>() != 0;
            auto bname = std::string(str_of(pnode.get(la::NAME.code)));
            LogosTypeBuilder ref_t;
            ref_t.kind    = is_mut ? LogosType::Kind::MutRef : LogosType::Kind::Ref;
            ref_t.pointee = scrut_type;
            TypeRef btype = pool_->alloc(std::move(ref_t));
            return lir::PatRefBind{bname, is_mut, btype};
        }
    }

    // ── PAT_STRUCT: Point { x: p, y } or Point { .. } ────────────────────
    if (pc == la::PAT_STRUCT) {
        auto sname = std::string(str_of(pnode.get(la::NAME.code)));
        // Look up struct or datatype info.
        const SemaStructInfo* sinfo = nullptr;
        { auto [sp, si] = find_struct_by_name(sname); sinfo = si; }
        if (!sinfo) { auto [dp, di] = find_datatype_by_name(sname); sinfo = di; }
        if (!sinfo)
            error(std::format("struct pattern: unknown struct '{}'", sname));
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error &&
            TypeRef(scrut_type).kind() == LogosType::Kind::Struct &&
            TypeRef(scrut_type).struct_name() != sname && TypeRef(scrut_type).struct_name() != "")
            error(std::format("struct pattern: '{}' != scrutinee '{}'",
                  sname, type_str(scrut_type)));
        lir::PatStruct ps;
        ps.struct_name = sname;
        ps.has_rest    = false;
        if (pnode.has_key(la::ITEMS)) {
            AnyVal items_av = pnode.get(la::ITEMS.code);
            if (!items_av.is_null() && items_av.is_pointer()) {
                auto flist_node = map_of(items_av);
                if (flist_node.has_key(la::ITEMS)) {
                    auto fitems = arr_of(flist_node.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < fitems.size(); ++i) {
                        auto fnode = map_of(fitems.get(i));
                        if (code_of(fnode) == la::PAT_REST) {
                            // G3: .. must be last — error if named field follows rest.
                            if (ps.has_rest)
                                error("struct pattern: only one '..' allowed");
                            ps.has_rest = true;
                            continue;
                        }
                        // G3: named field after .. is a bug.
                        if (ps.has_rest)
                            error("struct pattern: named field after '..'");
                        // PAT_FIELD: NAME = field name, VALUE = sub-pattern (optional)
                        auto fname = std::string(str_of(fnode.get(la::NAME.code)));
                        TypeRef ftype = error_t();
                        if (sinfo) {
                            for (auto& f : sinfo->fields)
                                if (f.name == fname) { ftype = f.type; break; }
                        }
                        // Bug fix: emit error when field not found in struct.
                        if (sinfo) {
                            bool field_found = false;
                            for (auto& f : sinfo->fields)
                                if (f.name == fname) { field_found = true; break; }
                            if (!field_found)
                                error(std::format("struct pattern: '{}' has no field '{}'",
                                      sname, fname));
                        }
                        lir::PatFieldBinding pfb;
                        pfb.field_name = fname;
                        if (fnode.has_key(la::VALUE)) {
                            auto sub = build_pattern(map_of(fnode.get(la::VALUE.code)), ftype);
                            // Struct field sub-patterns must currently be irrefutable:
                            // only _ / name bindings, references, or @-bindings. Literals
                            // and other refutable kinds aren't tested by codegen yet and
                            // would silently match — reject early.
                            auto sk = pat_ref_of(sub).kind();
                            bool sub_irrefutable =
                                sk == lir_schema::pat::Code::Wild ||
                                sk == lir_schema::pat::Code::RefBind ||
                                sk == lir_schema::pat::Code::RefPat ||
                                sk == lir_schema::pat::Code::At;
                            if (!sub_irrefutable)
                                error("struct pattern: refutable field sub-pattern "
                                      "not yet supported");
                            pfb.sub.push_back(std::move(sub));
                        }
                        ps.fields.push_back(std::move(pfb));
                    }
                }
            }
        }
        // NG5: validate that all struct fields are covered (listed by name or '..' present).
        if (sinfo && !ps.has_rest) {
            for (auto& f : sinfo->fields) {
                bool covered = false;
                for (auto& pfb : ps.fields)
                    if (pfb.field_name == f.name) { covered = true; break; }
                if (!covered)
                    error(std::format("struct pattern: field '{}' not covered (add '..' to ignore remaining fields)",
                          f.name));
            }
        }
        return ps;
    }

    // ── PAT_SLICE: [a, b] or [first, .., last] ───────────────────────────
    if (pc == la::PAT_SLICE) {
        TypeRef elem_type = error_t();
        if (scrut_type && TypeRef(scrut_type).kind() == LogosType::Kind::Array && TypeRef(scrut_type).elem())
            elem_type = TypeRef(scrut_type).elem();
        else if (scrut_type && TypeRef(scrut_type).kind() == LogosType::Kind::Slice && TypeRef(scrut_type).elem())
            elem_type = TypeRef(scrut_type).elem();
        else if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error)
            error(std::format("slice pattern requires array or slice scrutinee, got '{}'",
                  type_str(scrut_type)));
        lir::PatSlice psl;
        bool found_rest = false;
        if (pnode.has_key(la::ITEMS)) {
            AnyVal items_av = pnode.get(la::ITEMS.code);
            if (!items_av.is_null() && items_av.is_pointer()) {
                auto elist_node = map_of(items_av);
                if (elist_node.has_key(la::ITEMS)) {
                    auto eitems = arr_of(elist_node.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < eitems.size(); ++i) {
                        auto enode = map_of(eitems.get(i));
                        if (code_of(enode) == la::PAT_REST) {
                            // S3: reject multiple .. in a slice pattern.
                            if (found_rest)
                                error("slice pattern: only one '..' allowed");
                            found_rest = true;
                            psl.rest.push_back(lir::PatWild{"_"});
                            continue;
                        }
                        auto sub = build_pattern(enode, elem_type);
                        if (!found_rest) psl.prefix.push_back(std::move(sub));
                        else             psl.suffix.push_back(std::move(sub));
                    }
                }
            }
        }
        // For fixed-size arrays without rest, validate element count.
        if (scrut_type && TypeRef(scrut_type).kind() == LogosType::Kind::Array && !found_rest) {
            size_t expected = (size_t)TypeRef(scrut_type).arr_size();
            if (psl.prefix.size() != expected)
                error(std::format("slice pattern: expected {} elements, got {}",
                      expected, psl.prefix.size()));
        }
        // S3: for fixed-size arrays with rest, prefix+suffix cannot exceed array size.
        if (scrut_type && TypeRef(scrut_type).kind() == LogosType::Kind::Array && found_rest) {
            size_t arr_size = (size_t)TypeRef(scrut_type).arr_size();
            if (psl.prefix.size() + psl.suffix.size() > arr_size)
                error(std::format("slice pattern: {} + {} elements exceed array size {}",
                      psl.prefix.size(), psl.suffix.size(), arr_size));
        }
        // NG2: Dynamic slices (Slice kind) don't have a known length at compile time.
        // Suffix elements after .. require computing total-N indices, which needs runtime
        // length. Reject suffix patterns on dynamic slices; codegen would produce wrong code.
        if (scrut_type && TypeRef(scrut_type).kind() == LogosType::Kind::Slice &&
            found_rest && !psl.suffix.empty())
            error("slice pattern: suffix after '..' not supported for dynamic slices");
        return psl;
    }

    // ── Hermes scalar patterns ────────────────────────────────────────────
    // `@null`, `@true`, `@false`, `@<int>`, `@-<int>` are desugared by
    // lower_match/lower_match_expr into `_` + a synthesized guard call, so
    // by the time we get here the caller treats them as wildcards. We return
    // PatWild unchanged; the caller validates scrutinee type & synthesizes
    // the guard using build_hermes_pat_guard.
    if (pc == la::PAT_HERMES_NULL || pc == la::PAT_HERMES_BOOL ||
        pc == la::PAT_HERMES_INT  || pc == la::PAT_HERMES_STR  ||
        pc == la::PAT_HERMES_MAP  || pc == la::PAT_HERMES_ARR  ||
        pc == la::PAT_HERMES_TYPED_ARR || pc == la::PAT_HERMES_TYPED_MAP) {
        if (!in_match_hermes_ctx_) {
            error("Hermes pattern (@null/@true/@false/@<int>/@\"str\"/@{...}/@[...]) "
                  "is only supported in `match` arms, not in if-let / "
                  "while-let / let-bindings / nested pattern positions.");
        }
        return lir::PatWild{"_"};
    }

    // PAT_WILD or fallback
    auto wname = str_of(pnode.get(la::NAME.code));
    return lir::PatWild{std::string(wname)};
}

// Build the synthesized guard expression for a Hermes scalar match pattern.
// Returns nullptr if pnode is not a Hermes pattern.  Emits an error if the
// scrutinee type is not AnyVal.  The guard calls a free stdlib helper
// (hermes_pat_is_null / _eq_bool / _eq_i24) that takes `*const AnyVal`.
//
// If pnode is a PAT_OR wrapping several Hermes patterns, the guards for each
// alt are OR-ed together (matches Rust or-pattern semantics).  A single-alt
// PAT_OR unwraps transparently.
lir::LExprPtr SemaChecker::build_hermes_pat_guard(
        TinyMapView pnode, const std::string& scrut_var,
        TypeRef scrut_type, const std::string& base_var,
        std::vector<lir::LStmt>& out_stmts,
        std::vector<HermesPatBinding>& out_bindings) {
    TypeRef ptr_t_outer = make_ptr(false, scrut_type);
    TypeRef u8_ptr_t_outer = make_ptr(false, prim(LogosType::Kind::U8));
    TypeRef u64_t = prim(LogosType::Kind::U64);
    auto mk_true = [&]() {
        return builder().lit_bool(true, bool_t());
    };
    auto mk_and = [&](lir::LExprPtr a, lir::LExprPtr b) -> lir::LExprPtr {
        if (!a) return b;
        if (!b) return a;
        return builder().bin_op("&&", std::move(a), std::move(b), bool_t());
    };
    // Build a scalar-leaf guard for pattern `p` against AnyVal local `sv`.
    // Returns nullptr only when p is not a scalar Hermes leaf.
    auto build_leaf = [&](TinyMapView p, const std::string& sv) -> lir::LExprPtr {
        int32_t pc = code_of(p);
        if (pc != la::PAT_HERMES_NULL && pc != la::PAT_HERMES_BOOL &&
            pc != la::PAT_HERMES_INT  && pc != la::PAT_HERMES_STR)
            return nullptr;

        TypeRef ptr_t = ptr_t_outer;
        TypeRef u8_ptr_t = u8_ptr_t_outer;

        const char* helper = nullptr;
        size_t want_arity = 1;
        std::vector<lir::LExprPtr> extra_args;
        if (pc == la::PAT_HERMES_NULL) {
            helper = "hermes_pat_is_null";
        } else if (pc == la::PAT_HERMES_BOOL) {
            helper = "hermes_pat_eq_bool";
            want_arity = 2;
            AnyVal bv = p.get(la::VALUE.code);
            bool bval = !bv.is_null() && bv.is_value() && bv.as_value<uint8_t>();
            extra_args.push_back(builder().lit_bool(bval, bool_t()));
        } else if (pc == la::PAT_HERMES_INT) {
            helper = "hermes_pat_eq_i24";
            want_arity = 2;
            auto sv = str_of(p.get(la::VALUE.code));
            int64_t v = parse_int_literal(sv);
            bool neg = false;
            if (p.has_key(la::LO_NEG)) {
                AnyVal nv = p.get(la::LO_NEG.code);
                neg = !nv.is_null() && nv.is_value() && nv.as_value<uint8_t>();
            }
            if (neg) v = -v;
            if (v < -(int64_t{1} << 23) || v >= (int64_t{1} << 23)) {
                error(std::format("@<int> pattern: value {} does not fit in i24", v));
                v = 0;
            }
            extra_args.push_back(builder().lit_int(v, i32_t()));
        } else {  // PAT_HERMES_STR — hermes_pat_eq_str(*av, base, str)
            helper = "hermes_pat_eq_str";
            want_arity = 3;
            auto sv = str_of(p.get(la::VALUE.code));
            std::string lit(sv);
            extra_args.push_back(builder().lit_str(std::move(lit), make_slice_type(prim(LogosType::Kind::U8))));
        }

        auto cands = find_func_candidates(helper);
        const SemaFuncInfo* fi = nullptr;
        for (auto* c : cands)
            if (c->param_types.size() == want_arity) { fi = c; break; }
        if (!fi) {
            error(std::format(
                "Hermes pattern needs stdlib helper `{}`; `use std.hermes.anyval;`",
                helper));
            return builder().lit_bool(false, bool_t());
        }
        std::vector<lir::LExprPtr> args;
        args.push_back(builder().addr_of(sv, ptr_t));
        if (pc == la::PAT_HERMES_STR) {
            args.push_back(builder().var_ref(base_var, u8_ptr_t));
        }
        for (auto& a : extra_args) args.push_back(std::move(a));
        std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
        return builder().call(sym, {}, std::move(args), bool_t());
    };

    // Emit `let __hp_N: AnyVal = helper(&parent_av, base, ...);`
    // Returns the new local's name.
    auto emit_child_let = [&](const std::string& helper,
                              const std::string& parent_av,
                              std::vector<lir::LExprPtr> extra_args,
                              size_t want_arity) -> std::string {
        auto cands = find_func_candidates(helper);
        const SemaFuncInfo* fi = nullptr;
        for (auto* c : cands)
            if (c->param_types.size() == want_arity) { fi = c; break; }
        if (!fi) {
            error(std::format(
                "Hermes pattern needs stdlib helper `{}`; `use std.hermes.pat;`",
                helper));
            return "";
        }
        std::vector<lir::LExprPtr> args;
        args.push_back(builder().addr_of(parent_av, ptr_t_outer));
        args.push_back(builder().var_ref(base_var, u8_ptr_t_outer));
        for (auto& a : extra_args) args.push_back(std::move(a));
        std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
        auto call = builder().call(sym, {}, std::move(args), scrut_type);
        std::string child = "__hp_" + std::to_string(tmp_var_count_++);
        lir::SLet sl;
        sl.name = child; sl.type = scrut_type; sl.is_mut = false;
        sl.value = std::move(call);
        out_stmts.push_back(make_stmt(node_line_, std::move(sl)));
        return child;
    };
    // Emit `hermes_pat_array_len_eq(&sv, base, n)` as a bool expr.
    auto emit_array_len_eq = [&](const std::string& sv, uint64_t n) -> lir::LExprPtr {
        const char* helper = "hermes_pat_array_len_eq";
        auto cands = find_func_candidates(helper);
        const SemaFuncInfo* fi = nullptr;
        for (auto* c : cands)
            if (c->param_types.size() == 3) { fi = c; break; }
        if (!fi) {
            error(std::format(
                "Hermes pattern needs stdlib helper `{}`; `use std.hermes.pat;`",
                helper));
            return builder().lit_bool(false, bool_t());
        }
        std::vector<lir::LExprPtr> args;
        args.push_back(builder().addr_of(sv, ptr_t_outer));
        args.push_back(builder().var_ref(base_var, u8_ptr_t_outer));
        args.push_back(builder().lit_int((int64_t)n, u64_t));
        std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
        return builder().call(sym, {}, std::move(args), bool_t());
    };
    // Emit `hermes_pat_array_len_ge(&sv, base, n)` as a bool expr.
    auto emit_array_len_ge = [&](const std::string& sv, uint64_t n) -> lir::LExprPtr {
        const char* helper = "hermes_pat_array_len_ge";
        auto cands = find_func_candidates(helper);
        const SemaFuncInfo* fi = nullptr;
        for (auto* c : cands)
            if (c->param_types.size() == 3) { fi = c; break; }
        if (!fi) {
            error(std::format(
                "Hermes pattern needs stdlib helper `{}`; `use std.hermes.pat;`",
                helper));
            return builder().lit_bool(false, bool_t());
        }
        std::vector<lir::LExprPtr> args;
        args.push_back(builder().addr_of(sv, ptr_t_outer));
        args.push_back(builder().var_ref(base_var, u8_ptr_t_outer));
        args.push_back(builder().lit_int((int64_t)n, u64_t));
        std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
        return builder().call(sym, {}, std::move(args), bool_t());
    };
    // Emit `hermes_pat_has_type_code(&sv, base, tc)` bool expr.
    auto emit_has_type_code = [&](const std::string& sv, uint64_t tc) -> lir::LExprPtr {
        const char* helper = "hermes_pat_has_type_code";
        auto cands = find_func_candidates(helper);
        const SemaFuncInfo* fi = nullptr;
        for (auto* c : cands)
            if (c->param_types.size() == 3) { fi = c; break; }
        if (!fi) {
            error(std::format(
                "Hermes pattern needs stdlib helper `{}`; `use std.hermes.pat;`",
                helper));
            return builder().lit_bool(false, bool_t());
        }
        std::vector<lir::LExprPtr> args;
        args.push_back(builder().addr_of(sv, ptr_t_outer));
        args.push_back(builder().var_ref(base_var, u8_ptr_t_outer));
        args.push_back(builder().lit_int((int64_t)tc, u64_t));
        std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
        return builder().call(sym, {}, std::move(args), bool_t());
    };
    // Emit `hermes_pat_is_map(&sv, base)` bool expr.
    auto emit_is_map = [&](const std::string& sv) -> lir::LExprPtr {
        const char* helper = "hermes_pat_is_map";
        auto cands = find_func_candidates(helper);
        const SemaFuncInfo* fi = nullptr;
        for (auto* c : cands)
            if (c->param_types.size() == 2) { fi = c; break; }
        if (!fi) {
            error(std::format(
                "Hermes pattern needs stdlib helper `{}`; `use std.hermes.pat;`",
                helper));
            return builder().lit_bool(false, bool_t());
        }
        std::vector<lir::LExprPtr> args;
        args.push_back(builder().addr_of(sv, ptr_t_outer));
        args.push_back(builder().var_ref(base_var, u8_ptr_t_outer));
        std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
        return builder().call(sym, {}, std::move(args), bool_t());
    };
    // Emit `hermes_pat_is_present(&sv)` bool expr.
    auto emit_present = [&](const std::string& sv) -> lir::LExprPtr {
        const char* helper = "hermes_pat_is_present";
        auto cands = find_func_candidates(helper);
        const SemaFuncInfo* fi = nullptr;
        for (auto* c : cands)
            if (c->param_types.size() == 1) { fi = c; break; }
        if (!fi) {
            error(std::format(
                "Hermes pattern needs stdlib helper `{}`", helper));
            return builder().lit_bool(false, bool_t());
        }
        std::vector<lir::LExprPtr> args;
        args.push_back(builder().addr_of(sv, ptr_t_outer));
        std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
        return builder().call(sym, {}, std::move(args), bool_t());
    };
    // Recursive: build a guard expr for pattern `p` against AnyVal local `sv`.
    std::function<lir::LExprPtr(TinyMapView, const std::string&)> build_rec;
    build_rec = [&](TinyMapView p, const std::string& sv) -> lir::LExprPtr {
        int32_t pc = code_of(p);
        if (pc == la::PAT_HERMES_NULL || pc == la::PAT_HERMES_BOOL ||
            pc == la::PAT_HERMES_INT  || pc == la::PAT_HERMES_STR)
            return build_leaf(p, sv);
        if (pc == la::PAT_WILD) {
            auto nm = str_of(p.get(la::NAME.code));
            std::string name(nm);
            if (!name.empty() && name != "_") {
                out_bindings.push_back(HermesPatBinding{name, sv});
            }
            return mk_true();
        }
        if (pc == la::PAT_HERMES_MAP) {
            lir::LExprPtr acc = emit_is_map(sv);
            if (p.has_key(la::ITEMS)) {
                auto wrap = map_of(p.get(la::ITEMS.code));
                auto items = arr_of(wrap.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto ent = map_of(items.get(i));
                    if (code_of(ent) != la::PAT_HERMES_MAP_ENTRY) continue;
                    auto ksv = str_of(ent.get(la::KEY.code));
                    std::vector<lir::LExprPtr> xargs;
                    xargs.push_back(builder().lit_str(std::string(ksv), make_slice_type(prim(LogosType::Kind::U8))));
                    std::string child = emit_child_let(
                        "hermes_pat_map_slot", sv, std::move(xargs), 3);
                    if (child.empty()) {
                        return builder().lit_bool(false, bool_t());
                    }
                    auto presence = emit_present(child);
                    lir::LExprPtr sub;
                    if (!ent.has_key(la::VALUE)) {
                        sub = mk_true();
                    } else {
                        sub = build_rec(map_of(ent.get(la::VALUE.code)), child);
                    }
                    acc = mk_and(std::move(acc),
                                 mk_and(std::move(presence), std::move(sub)));
                }
            }
            if (!acc) acc = mk_true();
            return acc;
        }
        if (pc == la::PAT_HERMES_ARR) {
            uint64_t n_total = 0;
            bool has_rest = false;
            hermes::TinyMapView arr_wrap;
            if (p.has_key(la::ITEMS)) {
                arr_wrap = map_of(p.get(la::ITEMS.code));
                auto items = arr_of(arr_wrap.get(la::ITEMS.code));
                n_total = items.size();
                for (uint64_t i = 0; i < n_total; ++i) {
                    if (code_of(map_of(items.get(i))) == la::PAT_REST) {
                        if (i + 1 != n_total) {
                            error("`..` must be the last element in a Hermes "
                                  "array pattern");
                            return builder().lit_bool(false, bool_t());
                        }
                        has_rest = true;
                    }
                }
            }
            uint64_t n_bind = has_rest ? (n_total - 1) : n_total;
            auto acc = has_rest ? emit_array_len_ge(sv, n_bind)
                                : emit_array_len_eq(sv, n_bind);
            if (p.has_key(la::ITEMS)) {
                auto items = arr_of(arr_wrap.get(la::ITEMS.code));
                for (uint64_t i = 0; i < n_bind; ++i) {
                    std::vector<lir::LExprPtr> xargs;
                    xargs.push_back(builder().lit_int((int64_t)i, u64_t));
                    std::string child = emit_child_let(
                        "hermes_pat_array_slot", sv, std::move(xargs), 3);
                    if (child.empty()) {
                        return builder().lit_bool(false, bool_t());
                    }
                    auto sub = build_rec(map_of(items.get(i)), child);
                    acc = mk_and(std::move(acc), std::move(sub));
                }
            }
            return acc;
        }
        if (pc == la::PAT_HERMES_TYPED_ARR) {
            namespace th = logos::hermes::type_hash;
            auto tname = std::string(str_of(p.get(la::TYPE.code)));
            static const std::map<std::string, uint64_t> arr_tcs = {
                {"I8",     th::ArrayI8},
                {"U8",     th::ArrayU8},
                {"I16",    th::ArrayI16},
                {"U16",    th::ArrayU16},
                {"I32",    th::ArrayI32},
                {"U32",    th::ArrayU32},
                {"I64",    th::ArrayI64},
                {"U64",    th::ArrayU64},
                {"F32",    th::ArrayF32},
                {"F64",    th::ArrayF64},
                {"AnyVal", th::Array},
            };
            auto it = arr_tcs.find(tname);
            if (it == arr_tcs.end()) {
                error(std::format(
                    "typed array pattern @<{}>[..]: unsupported element type;"
                    " supported: I8, U8, I16, U16, I32, U32, I64, U64,"
                    " F32, F64, AnyVal", tname));
                return builder().lit_bool(false, bool_t());
            }
            return emit_has_type_code(sv, it->second);
        }
        if (pc == la::PAT_HERMES_TYPED_MAP) {
            namespace th = logos::hermes::type_hash;
            auto kname = std::string(str_of(p.get(la::TYPE.code)));
            std::string vname;
            if (p.has_key(la::RET_TYPE))
                vname = std::string(str_of(p.get(la::RET_TYPE.code)));
            if (!vname.empty() && vname != "AnyVal") {
                error(std::format(
                    "typed map pattern @<{},{}>{{..}}: unsupported value type;"
                    " only AnyVal is supported", kname, vname));
                return builder().lit_bool(false, bool_t());
            }
            static const std::map<std::string, uint64_t> map_tcs = {
                {"Varchar", th::ObjectMap},
                {"I32",     th::MapI32AnyVal},
                {"U32",     th::MapU32AnyVal},
                {"I64",     th::MapI64AnyVal},
                {"U64",     th::MapU64AnyVal},
            };
            auto it = map_tcs.find(kname);
            if (it == map_tcs.end()) {
                error(std::format(
                    "typed map pattern @<{}>{{..}}: unsupported key type;"
                    " supported: Varchar, I32, U32, I64, U64", kname));
                return builder().lit_bool(false, bool_t());
            }
            return emit_has_type_code(sv, it->second);
        }
        // Unsupported in Hermes context.
        error("unsupported pattern inside Hermes @{...}/@[...] pattern");
        return builder().lit_bool(false, bool_t());
    };

    // Unwrap PAT_OR: build per-alt guards and OR them (scalar alts only).
    if (code_of(pnode) == la::PAT_OR && pnode.has_key(la::ITEMS)) {
        auto alts = arr_of(pnode.get(la::ITEMS.code));
        if (alts.size() == 0) return nullptr;
        // Single-alt PAT_OR (the grammar always wraps pattern in PAT_OR):
        // recurse into the sole alternative so MAP/ARR are handled.
        if (alts.size() == 1) {
            int32_t pc0 = code_of(map_of(alts.get(0)));
            bool is_hermes = pc0 == la::PAT_HERMES_NULL ||
                             pc0 == la::PAT_HERMES_BOOL ||
                             pc0 == la::PAT_HERMES_INT  ||
                             pc0 == la::PAT_HERMES_STR  ||
                             pc0 == la::PAT_HERMES_MAP  ||
                             pc0 == la::PAT_HERMES_ARR  ||
                             pc0 == la::PAT_HERMES_TYPED_ARR ||
                             pc0 == la::PAT_HERMES_TYPED_MAP;
            if (!is_hermes) return nullptr;
            return build_rec(map_of(alts.get(0)), scrut_var);
        }
        bool any_hermes = false, any_non = false;
        for (uint64_t i = 0; i < alts.size(); ++i) {
            int32_t pc = code_of(map_of(alts.get(i)));
            if (pc == la::PAT_HERMES_NULL || pc == la::PAT_HERMES_BOOL ||
                pc == la::PAT_HERMES_INT  || pc == la::PAT_HERMES_STR  ||
                pc == la::PAT_HERMES_MAP  || pc == la::PAT_HERMES_ARR  ||
        pc == la::PAT_HERMES_TYPED_ARR || pc == la::PAT_HERMES_TYPED_MAP) any_hermes = true;
            else any_non = true;
        }
        if (!any_hermes) return nullptr;
        if (any_non) {
            error("or-pattern mixing Hermes patterns with other "
                  "patterns is not supported");
            return builder().lit_bool(false, bool_t());
        }
        lir::LExprPtr acc;
        for (uint64_t i = 0; i < alts.size(); ++i) {
            // build_rec handles all Hermes pattern kinds (scalar + structural).
            auto alt_guard = build_rec(map_of(alts.get(i)), scrut_var);
            if (!alt_guard) continue;
            if (!acc) { acc = std::move(alt_guard); continue; }
            acc = builder().bin_op("||", std::move(acc), std::move(alt_guard), bool_t());
        }
        return acc;
    }
    return build_rec(pnode, scrut_var);
}

void SemaChecker::bind_pattern(const lir::Pattern& pat,
                      TypeRef scrut_type) {
    if (auto* pvd = std::get_if<lir::PatVariantData>(&pat.kind)) {
        for (size_t i = 0; i < pvd->bindings.size() &&
                            i < pvd->binding_types.size(); ++i)
            define(pvd->bindings[i], pvd->binding_types[i]);
    } else if (auto* pt = std::get_if<lir::PatTuple>(&pat.kind)) {
        for (size_t i = 0; i < pt->bindings.size() &&
                            i < pt->binding_types.size(); ++i)
            if (pt->bindings[i] != "_")
                define(pt->bindings[i], pt->binding_types[i]);
    } else if (auto* pw = std::get_if<lir::PatWild>(&pat.kind)) {
        if (pw->name != "_" && scrut_type)
            define(pw->name, scrut_type);
    } else if (auto* prb = std::get_if<lir::PatRefBind>(&pat.kind)) {
        define(prb->name, prb->bind_type);
    } else if (auto* pa = std::get_if<lir::PatAt>(&pat.kind)) {
        if (pa->type && pa->name != "_") define(pa->name, pa->type);  // S5: guard _ name
        if (!pa->sub.empty()) bind_pattern(pa->sub[0], pa->type);
    } else if (auto* prp = std::get_if<lir::PatRefPat>(&pat.kind)) {
        // NS4: only extract pointee when the kind is actually Ref or MutRef.
        TypeRef inner_t = error_t();
        if (scrut_type && (TypeRef(scrut_type).kind() == LogosType::Kind::Ref ||
                           TypeRef(scrut_type).kind() == LogosType::Kind::MutRef) &&
            TypeRef(scrut_type).pointee())
            inner_t = TypeRef(scrut_type).pointee();
        if (!prp->inner.empty()) bind_pattern(prp->inner[0], inner_t);
    } else if (auto* ps = std::get_if<lir::PatStruct>(&pat.kind)) {
        // Look up struct info to get field types.
        const SemaStructInfo* sinfo = nullptr;
        auto sit = structs_.find(ps->struct_name);
        if (sit != structs_.end()) sinfo = &sit->second;
        else {
            auto dit = datatypes_.find(ps->struct_name);
            if (dit != datatypes_.end()) sinfo = &dit->second;
        }
        for (auto& pfb : ps->fields) {
            TypeRef ftype = error_t();
            if (sinfo)
                for (auto& f : sinfo->fields)
                    if (f.name == pfb.field_name) { ftype = f.type; break; }
            if (pfb.sub.empty()) {
                // Shorthand: bind field_name → field_type
                define(pfb.field_name, ftype);
            } else {
                bind_pattern(pfb.sub[0], ftype);
            }
        }
    } else if (auto* psl = std::get_if<lir::PatSlice>(&pat.kind)) {
        TypeRef elem_t = (scrut_type && TypeRef(scrut_type).elem()) ? TypeRef(scrut_type).elem() : error_t();
        for (auto& p : psl->prefix) bind_pattern(p, elem_t);
        for (auto& p : psl->rest)   bind_pattern(p, elem_t);  // S4: elem_t not scrut_type
        for (auto& p : psl->suffix) bind_pattern(p, elem_t);
    } else if (auto* por = std::get_if<lir::PatOr>(&pat.kind)) {
        // OR patterns: bind only if all alternatives bind the same names (first alt wins).
        if (!por->alts.empty()) bind_pattern(por->alts[0], scrut_type);
    }
}

lir::LStmt SemaChecker::lower_if(TinyMapView node) {
    // ── if let pattern = expr { ... } ─────────────────────────────
    if (node.has_key(la::PAT)) {
        auto scrut = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
        TypeRef scrut_type = scrut->type;

        auto pat = build_pattern(map_of(node.get(la::PAT.code)), scrut_type);

        // Then arm: pattern → then block
        push_scope();
        bind_pattern(pat, scrut_type);
        lir::LBlockPtr then_body = std::make_unique<lir::LBlock>();
        if (node.has_key(la::THEN))
            *then_body = lower_block(map_of(node.get(la::THEN.code)));
        pop_scope();

        // Else arm: wildcard → else block (or empty)
        lir::LBlockPtr else_body = std::make_unique<lir::LBlock>();
        if (node.has_key(la::ELSE)) {
            auto else_node = map_of(node.get(la::ELSE.code));
            if (code_of(else_node) == la::BLOCK) {
                *else_body = lower_block(else_node);
            } else {
                // else if: wrap in block
                else_body->stmts.push_back(lower_if(else_node));
            }
        }

        lir::SMatch sm;
        sm.scrut = std::move(scrut);
        sm.arms.push_back({std::move(pat), std::move(then_body), std::nullopt});
        sm.arms.push_back({lir::PatWild{"_"}, std::move(else_body), std::nullopt});
        return make_stmt(node_line_, std::move(sm));
    }

    // ── regular if cond { ... } ────────────────────────────────────
    lir::LExprPtr cond;
    if (node.has_key(la::COND)) {
        cond = lower_expr(map_of(node.get(la::COND.code)));
        if (TypeRef(cond->type).kind() != LogosType::Kind::Bool &&
            TypeRef(cond->type).kind() != LogosType::Kind::Error)
            error(std::format("if condition must be bool, got {}", type_str(cond->type)));
    } else {
        cond = error_expr();
    }

    auto then_block = std::make_unique<lir::LBlock>();
    if (node.has_key(la::THEN))
        *then_block = lower_block(map_of(node.get(la::THEN.code)));

    std::optional<lir::LBlockPtr> else_opt;
    if (node.has_key(la::ELSE)) {
        auto else_node = map_of(node.get(la::ELSE.code));
        if (code_of(else_node) == la::BLOCK) {
            else_opt = std::make_unique<lir::LBlock>(lower_block(else_node));
        } else {
            // else if: wrap single SIf in a block
            auto inner_if = lower_if(else_node);
            auto b = std::make_unique<lir::LBlock>();
            b->stmts.push_back(std::move(inner_if));
            else_opt = std::move(b);
        }
    }

    lir::SIf sif;
    sif.cond  = std::move(cond);
    sif.then_ = std::move(then_block);
    sif.else_ = std::move(else_opt);
    return make_stmt(node_line_, std::move(sif));
}

lir::LStmt SemaChecker::lower_while(TinyMapView node) {
    // ── while let pattern = expr { ... } ──────────────────────────
    // Desugars to: loop { match expr { PAT => body, _ => break } }
    if (node.has_key(la::PAT)) {
        auto scrut = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
        TypeRef scrut_type = scrut->type;

        auto pat = build_pattern(map_of(node.get(la::PAT.code)), scrut_type);

        // Then arm: pattern → loop body
        push_scope();
        bind_pattern(pat, scrut_type);
        lir::LBlockPtr then_body = std::make_unique<lir::LBlock>();
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            *then_body = lower_block(map_of(node.get(la::BODY.code)));
            --loop_depth_;
        }
        pop_scope();

        // Else arm: wildcard → break
        lir::LBlockPtr else_body = std::make_unique<lir::LBlock>();
        else_body->stmts.push_back(make_stmt(node_line_, lir::SBreak{}));

        lir::SMatch sm;
        sm.scrut = std::move(scrut);
        sm.arms.push_back({std::move(pat), std::move(then_body), std::nullopt});
        sm.arms.push_back({lir::PatWild{"_"}, std::move(else_body), std::nullopt});

        auto loop_body = std::make_unique<lir::LBlock>();
        loop_body->stmts.push_back(make_stmt(node_line_, std::move(sm)));
        lir::SLoop sl; sl.body = std::move(loop_body);
        return make_stmt(node_line_, std::move(sl));
    }

    // ── regular while cond { ... } ─────────────────────────────────
    // Capture label before lowering body (same reason as in lower_for).
    std::string my_label = std::move(pending_loop_label_);
    pending_loop_label_.clear();

    lir::LExprPtr cond;
    if (node.has_key(la::COND)) {
        cond = lower_expr(map_of(node.get(la::COND.code)));
        if (TypeRef(cond->type).kind() != LogosType::Kind::Bool &&
            TypeRef(cond->type).kind() != LogosType::Kind::Error)
            error(std::format("while condition must be bool, got {}", type_str(cond->type)));
    } else { cond = error_expr(); }

    auto body = std::make_unique<lir::LBlock>();
    if (node.has_key(la::BODY)) {
        ++loop_depth_;
        *body = lower_block(map_of(node.get(la::BODY.code)));
        --loop_depth_;
    }
    lir::SWhile sw;
    sw.cond  = std::move(cond);
    sw.body  = std::move(body);
    sw.label = std::move(my_label);
    return make_stmt(node_line_, std::move(sw));
}

lir::LStmt SemaChecker::lower_for(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr lo = node.has_key(la::LHS)
        ? lower_expr(map_of(node.get(la::LHS.code))) : error_expr();
    lir::LExprPtr hi = node.has_key(la::RHS)
        ? lower_expr(map_of(node.get(la::RHS.code))) : error_expr();

    if (!is_integer(lo->type) && TypeRef(lo->type).kind() != LogosType::Kind::Error)
        error(std::format("for range start must be integer, got {}", type_str(lo->type)));
    if (!is_integer(hi->type) && TypeRef(hi->type).kind() != LogosType::Kind::Error)
        error(std::format("for range end must be integer, got {}", type_str(hi->type)));

    bool inclusive = false;
    if (node.has_key(la::INCLUSIVE)) {
        AnyVal av = node.get(la::INCLUSIVE.code);
        if (!av.is_null() && av.is_value()) inclusive = av.as_value<uint8_t>() != 0;
    }

    // Mirror mlir_gen's loop_type logic: pick the widest bound type (> 32 bits).
    auto int_kind_width = [](LogosType::Kind k) -> int {
        switch (k) {
            case LogosType::Kind::I24:  case LogosType::Kind::U24:  return 24;
            case LogosType::Kind::I56:  case LogosType::Kind::U56:  return 56;
            case LogosType::Kind::I64:  case LogosType::Kind::U64:  return 64;
            case LogosType::Kind::I128: case LogosType::Kind::U128: return 128;
            default: return 32;
        }
    };
    TypeRef var_t = i32_t();
    {
        int lo_w = int_kind_width(TypeRef(lo->type).kind());
        int hi_w = int_kind_width(TypeRef(hi->type).kind());
        int max_w = std::max(lo_w, hi_w);
        if (max_w > 32) {
            // prefer hi on tie (mirrors mlir_gen: hi checked first)
            var_t = (hi_w >= lo_w) ? hi->type : lo->type;
        }
    }
    if (var_t == i32_t()) {
        auto intlit_overflows = [](const lir::LExpr* e) {
            if (auto v = get_intlit_value(e))
                return !intlit_fits(*v, LogosType::Kind::I32);
            return false;
        };
        if (intlit_overflows(lo.get()) || intlit_overflows(hi.get()))
            var_t = prim(LogosType::Kind::I64);
    }

    // Capture the label NOW, before lowering the body.  If we waited until
    // after lower_block(), any unlabeled nested loop inside the body would
    // steal our pending_loop_label_ on its own sf.label assignment.
    std::string my_label = std::move(pending_loop_label_);
    pending_loop_label_.clear();

    push_scope();
    define(var_name, var_t, true);
    auto body = std::make_unique<lir::LBlock>();
    if (node.has_key(la::BODY)) {
        ++loop_depth_;
        *body = lower_block(map_of(node.get(la::BODY.code)));
        --loop_depth_;
    }
    pop_scope();

    lir::SFor sf;
    sf.var       = std::string(var_name);
    sf.lo        = std::move(lo);
    sf.hi        = std::move(hi);
    sf.inclusive = inclusive;
    sf.body      = std::move(body);
    sf.label     = std::move(my_label);
    return make_stmt(node_line_, std::move(sf));
}

lir::LStmt SemaChecker::lower_for_each(TinyMapView node) {
    auto var_name = str_of(node.get(la::NAME.code));

    lir::LExprPtr iter = node.has_key(la::ITER)
        ? lower_expr(map_of(node.get(la::ITER.code))) : error_expr();

    TypeRef iter_type = iter->type;

    // ── array path (original) ────────────────────────────────────
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        int64_t arr_size = (int64_t)TypeRef(iter_type).arr_size();
        TypeRef elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();

        push_scope();
        define(var_name, elem_type, false);
        auto body = std::make_unique<lir::LBlock>();
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            *body = lower_block(map_of(node.get(la::BODY.code)));
            --loop_depth_;
        }
        pop_scope();

        lir::SForEach sfe;
        sfe.var       = std::string(var_name);
        sfe.iter      = std::move(iter);
        sfe.elem_type = elem_type;
        sfe.arr_size  = arr_size;
        sfe.body      = std::move(body);
        return make_stmt(node_line_, std::move(sfe));
    }

    // ── slice path: &[T] — iterate by index over fat pointer ────────
    if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        TypeRef elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        push_scope();
        define(var_name, elem_type, false);
        auto body = std::make_unique<lir::LBlock>();
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            *body = lower_block(map_of(node.get(la::BODY.code)));
            --loop_depth_;
        }
        pop_scope();
        lir::SForEach sfe;
        sfe.var       = std::string(var_name);
        sfe.iter      = std::move(iter);
        sfe.elem_type = elem_type;
        sfe.arr_size  = 0;
        sfe.is_slice  = true;
        sfe.body      = std::move(body);
        return make_stmt(node_line_, std::move(sfe));
    }

    // ── iterator path: desugar to while-let loop ─────────────────
    // Requires: iter_type has a `next()` method returning Option<T>
    // Desugars: for x in iter { body }
    //        → { let mut __iter = iter; while let Opt::Some(x) = __iter.next() { body } }
    if (TypeRef(iter_type).kind() != LogosType::Kind::Error) {
        auto sname = struct_name_from_type(iter_type);
        if (sname.empty()) {
            error(std::format("for-in: '{}' is not iterable (not a struct)", type_str(iter_type)));
            return make_stmt(node_line_, lir::SBreak{});
        }

        // For generic iterators (MapIter<I,T,R>) trait-impl methods are registered
        // under the BASE struct name (MapIter__next), not the mangled concrete
        // name.  Try both: concrete first (inherent impls like RangeI32__next),
        // then base (generic/trait impls).
        auto mangled_next = std::string(sname) + "__next";
        const SemaFuncInfo* fi_ptr = nullptr;
        if (auto fit = find_func_by_base_and_signature(mangled_next, {}, false))
            fi_ptr = fit;
        else if (auto cands = find_func_candidates(mangled_next); cands.size() == 1)
            fi_ptr = cands[0];

        if (!fi_ptr) {
            // Try base name (generic impl).
            std::string base_name;
            if (TypeRef(iter_type).kind() == LogosType::Kind::Struct ||
                TypeRef(iter_type).kind() == LogosType::Kind::ZonedStruct)
                base_name = TypeRef(iter_type).struct_name();
            else if (is_ref_like(TypeRef(iter_type).kind()) && TypeRef(iter_type).pointee())
                base_name = TypeRef(iter_type).pointee().struct_name();
            if (!base_name.empty() && base_name != std::string(sname)) {
                auto base_next = base_name + "__next";
                if (auto git = find_generic_func(base_next))
                    fi_ptr = git;
                else if (auto cands = find_func_candidates(base_next); cands.size() == 1)
                    fi_ptr = cands[0];
            }
        }

        if (!fi_ptr) {
            error(std::format("for-in: type '{}' has no `next()` method", sname));
            return make_stmt(node_line_, lir::SBreak{});
        }

        // next() must return an enum (Option-like)
        TypeRef next_ret = fi_ptr->ret_type;
        // Substitute type args if iterator is generic.  structs_ is keyed by
        // the BASE struct name, not the mangled concrete name.
        if (!TypeRef(iter_type).type_args().empty()) {
            std::string lookup_name =
                (TypeRef(iter_type).kind() == LogosType::Kind::Struct ||
                 TypeRef(iter_type).kind() == LogosType::Kind::ZonedStruct)
                    ? std::string(TypeRef(iter_type).struct_name())
                    : std::string(sname);
            SemaStructInfo* si = nullptr;
            { auto [sp, ssi] = find_struct_by_name(lookup_name); si = ssi; }
            if (!si) { auto [dp, dsi] = find_datatype_by_name(lookup_name); si = dsi; }
            // Also try package-qualified key if iter_type has pkg_name
            if (!si && !TypeRef(iter_type).pkg_name().empty()) {
                auto qkey = sema_key(TypeRef(iter_type).pkg_name(), lookup_name);
                { auto it = structs_.find(qkey); if (it != structs_.end()) si = &it->second; }
                if (!si) { auto it = datatypes_.find(qkey); if (it != datatypes_.end()) si = &it->second; }
            }
            if (si) {
                SemaSubst subst;
                auto& tps = si->type_params;
                for (size_t i = 0; i < tps.size() && i < TypeRef(iter_type).type_args().size(); ++i)
                    subst[tps[i].name] = TypeRef(iter_type).type_args()[i];
                next_ret = subst_type_sema(next_ret, subst);
            }
        }
        if (TypeRef(next_ret).kind() != LogosType::Kind::Enum) {
            error(std::format("for-in: `{}.next()` must return an enum, got {}",
                  sname, type_str(next_ret)));
            return make_stmt(node_line_, lir::SBreak{});
        }

        // Find the payload variant (Some-like: first variant with payload)
        const SemaVariantInfo* some_variant = nullptr;
        auto [epkg_forin, esi_forin] = find_enum_by_name(TypeRef(next_ret).enum_name());
        auto eit = esi_forin ? enums_.find(sema_key(epkg_forin, TypeRef(next_ret).enum_name())) : enums_.end();
        if (eit == enums_.end()) eit = enums_.find(TypeRef(next_ret).enum_name());
        if (eit == enums_.end()) {
            error(std::format("for-in: enum '{}' not found", TypeRef(next_ret).enum_name()));
            return make_stmt(node_line_, lir::SBreak{});
        }
        for (auto& v : eit->second.variants)
            if (!v.payload_types.empty()) { some_variant = &v; break; }
        if (!some_variant) {
            error(std::format("for-in: enum '{}' has no payload variant", TypeRef(next_ret).enum_name()));
            return make_stmt(node_line_, lir::SBreak{});
        }

        // Resolve element type (substitute generics from next_ret's type_args)
        TypeRef elem_type = some_variant->payload_types[0];
        if (!TypeRef(next_ret).type_args().empty()) {
            SemaSubst subst;
            auto& tps = eit->second.type_params;
            for (size_t i = 0; i < tps.size() && i < TypeRef(next_ret).type_args().size(); ++i)
                subst[tps[i].name] = TypeRef(next_ret).type_args()[i];
            elem_type = subst_type_sema(elem_type, subst);
        }

        // Synthesize: let mut __iter = iter
        std::string iter_var = "__for_iter_" + std::to_string(tmp_var_count_++);
        lir::SLet let_iter;
        let_iter.name   = iter_var;
        let_iter.type   = iter_type;
        let_iter.is_mut = true;
        let_iter.value  = std::move(iter);

        // Build outer block: { let mut __iter = iter; loop { match __iter.next() ... } }
        auto outer_block = std::make_unique<lir::LBlock>();
        outer_block->stmts.push_back(make_stmt(node_line_, std::move(let_iter)));

        // Synthesize __iter.next() call expression (inside the loop)
        auto make_next_call = [&]() -> lir::LExprPtr {
            auto iter_ref = builder().var_ref(iter_var, iter_type);
            return builder().method_call(std::move(iter_ref), "next", "", {}, {}, -1, next_ret);
        };

        // Then arm: Some(x) → body
        lir::PatVariantData some_pat;
        some_pat.enum_name = TypeRef(next_ret).enum_name();
        some_pat.variant   = some_variant->name;
        some_pat.disc         = some_variant->value;
        some_pat.bindings     = {std::string(var_name)};
        some_pat.binding_types = {elem_type};

        push_scope();
        define(iter_var, iter_type, true);
        define(std::string(var_name), elem_type, false);
        auto then_body = std::make_unique<lir::LBlock>();
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            *then_body = lower_block(map_of(node.get(la::BODY.code)));
            --loop_depth_;
        }
        pop_scope();

        // Else arm: _ → break
        auto else_body = std::make_unique<lir::LBlock>();
        else_body->stmts.push_back(make_stmt(node_line_, lir::SBreak{}));

        lir::SMatch sm;
        sm.scrut = make_next_call();
        sm.arms.push_back({lir::Pattern{std::move(some_pat)}, std::move(then_body), std::nullopt});
        sm.arms.push_back({lir::PatWild{"_"}, std::move(else_body), std::nullopt});

        auto loop_body = std::make_unique<lir::LBlock>();
        loop_body->stmts.push_back(make_stmt(node_line_, std::move(sm)));
        lir::SLoop sl; sl.body = std::move(loop_body);
        outer_block->stmts.push_back(make_stmt(node_line_, std::move(sl)));

        // Wrap in a block statement
        return make_stmt(node_line_, lir::SBlock{std::move(outer_block)});
    }

    return make_stmt(node_line_, lir::SBreak{});
}

lir::LStmt SemaChecker::lower_loop(TinyMapView node) {
    // Capture label before lowering body (same reason as in lower_for).
    std::string my_label = std::move(pending_loop_label_);
    pending_loop_label_.clear();

    auto body = std::make_unique<lir::LBlock>();
    TypeRef saved_break_type = break_value_type_;
    bool saved_break_without_value = break_without_value_;
    break_value_type_ = nullptr;
    break_without_value_ = false;
    if (node.has_key(la::BODY)) {
        ++loop_depth_;
        *body = lower_block(map_of(node.get(la::BODY.code)));
        --loop_depth_;
    }
    lir::SLoop sl;
    sl.body  = std::move(body);
    sl.label = std::move(my_label);
    if (break_value_type_) {
        sl.result_type = break_value_type_;
        sl.break_slot  = "__loop_val_" + std::to_string(tmp_var_count_++);
    }
    break_value_type_ = saved_break_type;  // restore for outer loops
    break_without_value_ = saved_break_without_value;
    return make_stmt(node_line_, std::move(sl));
}

lir::LStmt SemaChecker::lower_field_write(TinyMapView node) {
    auto recv_name  = str_of(node.get(la::RECEIVER.code));
    auto field_name = str_of(node.get(la::FIELD.code));

    // DataRef<T> ergonomic write: p.field = val → { let __tmp = p.mut_ptr(); (*__tmp).field = val; }
    {
        TypeRef recv_type = lookup(recv_name);
        if (recv_type && TypeRef(recv_type).kind() == LogosType::Kind::Struct &&
            TypeRef(recv_type).struct_name() == "DataRef" &&
            TypeRef(recv_type).type_args().size() == 1) {
            TypeRef T = TypeRef(recv_type).type_args()[0];
            if (T && TypeRef(T).kind() == LogosType::Kind::ZonedStruct) {
                auto ft = field_type_of_for_type(T, field_name);
                if (ft) {
                    if (!inside_unsafe_)
                        error(std::format("DataRef<T>.{}: field write requires unsafe context",
                                          field_name));
                    if (!lookup_is_mut(recv_name))
                        error(std::format("field write to immutable DataRef variable '{}'",
                                          recv_name));
                    // Lower value before generating the block.
                    lir::LExprPtr val = node.has_key(la::VALUE)
                        ? lower_expr(map_of(node.get(la::VALUE.code)))
                        : error_expr();
                    if (TypeRef(val->type).kind() != LogosType::Kind::Error &&
                        !types_compatible(val->type, ft))
                        error(std::format("field write '{}.{}': expected {}, got {}",
                              recv_name, field_name, type_str(ft), type_str(val->type)));
                    // Synthesize: let __dr_tmp = p.mut_ptr();
                    TypeRef mut_ptr_T = make_ptr(true, T);
                    std::string tmp = "__dr_tmp_" + std::string(recv_name);
                    auto recv_expr = builder().var_ref(std::string(recv_name), recv_type);
                    lir::SLet let_s;
                    let_s.name   = tmp;
                    let_s.type   = mut_ptr_T;
                    let_s.is_mut = false;
                    let_s.value  = builder().method_call(std::move(recv_expr), "mut_ptr", "", {}, {}, -1, mut_ptr_T);
                    // Synthesize: (*__dr_tmp).field = val
                    lir::SDerefFieldWrite dfw;
                    dfw.receiver  = tmp;
                    dfw.type_name = concrete_struct_name(T);
                    dfw.field     = std::string(field_name);
                    dfw.value     = std::move(val);
                    lir::LBlock inner;
                    inner.stmts.push_back(make_stmt(node_line_, std::move(let_s)));
                    inner.stmts.push_back(make_stmt(node_line_, std::move(dfw)));
                    return make_stmt(node_line_,
                        lir::SBlock{std::make_unique<lir::LBlock>(std::move(inner))});
                }
            }
        }
    }

    auto sname = struct_name_of(recv_name);
    if (sname.empty()) {
        error(std::format("field write: '{}' is not a struct", recv_name));
    } else {
        auto recv_type = lookup(recv_name);
        if (recv_type && TypeRef(recv_type).kind() == LogosType::Kind::Ptr) {
            if (!TypeRef(recv_type).mut_ptr())
                error(std::format("field write to '{}': receiver is *const pointer", recv_name));
        } else if (recv_type && TypeRef(recv_type).kind() == LogosType::Kind::Ref) {
            error(std::format("field write to '{}': receiver is &T (shared reference)", recv_name));
        } else if (!lookup_is_mut(recv_name) &&
                   !(recv_type && TypeRef(recv_type).kind() == LogosType::Kind::MutRef)) {
            error(std::format("field write to immutable variable '{}'", recv_name));
        }
    }
    TypeRef recv_struct_t = sname.empty() ? nullptr : lookup(recv_name);
    if (recv_struct_t && TypeRef(recv_struct_t).kind() == LogosType::Kind::Ptr) {
        if (!inside_unsafe_)
            error("field write through raw pointer requires unsafe context");
        recv_struct_t = TypeRef(recv_struct_t).pointee();
    } else if (recv_struct_t && is_ref_like(TypeRef(recv_struct_t).kind())) {
        recv_struct_t = TypeRef(recv_struct_t).pointee();
    }
    TypeRef ft = nullptr;
    if (recv_struct_t) {
        ft = field_type_of_for_type(recv_struct_t, field_name);
    }
    if (!sname.empty() && !ft)
        error(std::format("field write: struct '{}' has no field '{}'", sname, field_name));
    // Pub check for struct/datatype field writes.
    if (!sname.empty() && ft) {
        SemaStructInfo* si = nullptr;
        { auto it = structs_.find(std::string(sname)); if (it != structs_.end()) si = &it->second; }
        if (!si) { auto it = datatypes_.find(std::string(sname)); if (it != datatypes_.end()) si = &it->second; }
        if (si) {
            for (auto& f : si->fields) {
                if (f.name == field_name) {
                    check_pub_access(f.is_pub, si->package, field_name);
                    break;
                }
            }
        }
    }

    auto saved_struct_hint = hint_struct_type_;
    if (ft && (TypeRef(ft).kind() == LogosType::Kind::Struct ||
               TypeRef(ft).kind() == LogosType::Kind::ZonedStruct) && !TypeRef(ft).type_args().empty())
        hint_struct_type_ = ft;
    lir::LExprPtr val = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    hint_struct_type_ = saved_struct_hint;
    if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
        TypeRef(val->type).kind() != LogosType::Kind::Error &&
        !types_compatible(val->type, ft)) {
        error(std::format("field write '{}.{}': expected {}, got {}",
              recv_name, field_name, type_str(ft), type_str(val->type)));
    }
    if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
        TypeRef(val->type).kind() == LogosType::Kind::IntLit)
        if (auto v = get_intlit_value(val.get()))
            if (!intlit_fits(*v, TypeRef(ft).kind()))
                error(std::format("field write '{}.{}': value {} does not fit in {}",
                      recv_name, field_name, *v, type_str(ft)));
    // Check array literal elements against narrow array field type.
    if (ft && TypeRef(ft).kind() == LogosType::Kind::Array && TypeRef(ft).elem() &&
        TypeRef(val->type).kind() == LogosType::Kind::Array)
        if (auto* al = std::get_if<lir::EArrLit>(&val->kind))
            for (size_t i = 0; i < al->elems.size(); ++i)
                if (TypeRef(al->elems[i]->type).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(al->elems[i].get()))
                        if (!intlit_fits(*v, TypeRef(ft).elem().kind()))
                            error(std::format("field write '{}.{}': array element {}: value {} does not fit in {}",
                                  recv_name, field_name, i, *v, type_str(TypeRef(ft).elem())));
    // Check tuple literal elements against narrow tuple field element types.
    if (ft && TypeRef(ft).kind() == LogosType::Kind::Tuple && TypeRef(val->type).kind() == LogosType::Kind::Tuple)
        if (auto* tl = std::get_if<lir::ETupleLit>(&val->kind))
            for (size_t i = 0; i < tl->elems.size() && i < TypeRef(ft).tuple_elems().size(); ++i) {
                if (TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(tl->elems[i].get()))
                        if (TypeRef(ft).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(ft).tuple_elems()[i]).kind()))
                            error(std::format("field write '{}.{}': tuple element {}: value {} does not fit in {}",
                                  recv_name, field_name, i, *v, type_str(TypeRef(ft).tuple_elems()[i])));
                if (TypeRef(ft).tuple_elems()[i] && TypeRef(TypeRef(ft).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                    TypeRef(TypeRef(ft).tuple_elems()[i]).elem() && TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::Array)
                    if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[i]->kind))
                        for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                            if (TypeRef(ial->elems[ii]->type).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(ial->elems[ii].get()))
                                    if (!intlit_fits(*v, TypeRef(TypeRef(ft).tuple_elems()[i]).elem().kind()))
                                        error(std::format("field write '{}.{}': tuple element {}: array element {}: value {} does not fit in {}",
                                              recv_name, field_name, i, ii, *v, type_str(TypeRef(TypeRef(ft).tuple_elems()[i]).elem())));

                if (TypeRef(ft).tuple_elems()[i] && TypeRef(TypeRef(ft).tuple_elems()[i]).kind() == LogosType::Kind::Tuple &&
                    TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::Tuple)
                    if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[i]->kind))
                        for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems().size(); ++ii)
                            if (TypeRef(itl->elems[ii]->type).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(itl->elems[ii].get()))
                                    if (TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii]).kind()))
                                        error(std::format("field write '{}.{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                              recv_name, field_name, i, ii, *v, type_str(TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii])));
                }
    lir::SFieldWrite sfw;
    sfw.receiver = std::string(recv_name);
    sfw.field    = std::string(field_name);
    sfw.value    = std::move(val);
    return make_stmt(node_line_, std::move(sfw));
}

// a.mid.field = val  — chained 2-level field write
lir::LStmt SemaChecker::lower_chain_field_write(TinyMapView node) {
    auto recv_name  = str_of(node.get(la::RECEIVER.code));
    auto mid_name   = str_of(node.get(la::NAME.code));
    auto field_name = str_of(node.get(la::FIELD.code));

    // Resolve outer type (handles *mut T transparently).
    auto outer_sname = struct_name_of(recv_name);
    if (outer_sname.empty()) {
        error(std::format("chain field write: '{}' is not a struct", recv_name));
        return make_stmt(node_line_, lir::SExprStmt{error_expr()});
    }
    // Require mutable receiver.
    auto recv_type = lookup(recv_name);
    if (recv_type && TypeRef(recv_type).kind() == LogosType::Kind::Ref)
        error(std::format("chain field write '{}': receiver is &T (shared reference)", recv_name));
    else if (recv_type && TypeRef(recv_type).kind() != LogosType::Kind::Ptr && !lookup_is_mut(recv_name))
        error(std::format("chain field write to immutable variable '{}'", recv_name));
    if (recv_type && TypeRef(recv_type).kind() == LogosType::Kind::Ptr && !TypeRef(recv_type).mut_ptr())
        error(std::format("chain field write '{}': receiver is *const pointer", recv_name));

    // Get mid-field type — use typed lookup so generic type args are substituted
    // (e.g. Array<i32>.data resolves to RelPtr<i32>, not RelPtr<T>).
    TypeRef outer_struct_t = recv_type;
    if (outer_struct_t && TypeRef(outer_struct_t).kind() == LogosType::Kind::Ptr)
        outer_struct_t = TypeRef(outer_struct_t).pointee();
    else if (outer_struct_t && is_ref_like(TypeRef(outer_struct_t).kind()))
        outer_struct_t = TypeRef(outer_struct_t).pointee();
    TypeRef mid_ft = outer_struct_t
        ? field_type_of_for_type(outer_struct_t, mid_name)
        : field_type_of(std::string(outer_sname), mid_name);
    if (!mid_ft) {
        error(std::format("chain field write: struct '{}' has no field '{}'", outer_sname, mid_name));
        return make_stmt(node_line_, lir::SExprStmt{error_expr()});
    }
    // Resolve through pointer if mid-field is itself a pointer type.
    TypeRef mid_struct_t = mid_ft;
    if (TypeRef(mid_struct_t).kind() == LogosType::Kind::Ptr) mid_struct_t = TypeRef(mid_struct_t).pointee();

    auto mid_sname = mid_struct_t ? concrete_struct_name(mid_struct_t) : std::string{};
    if (mid_sname.empty()) {
        error(std::format("chain field write: '{}.{}' has no struct type", recv_name, mid_name));
        return make_stmt(node_line_, lir::SExprStmt{error_expr()});
    }

    // Get final field type — again via typed lookup for generic substitution.
    TypeRef ft = field_type_of_for_type(mid_struct_t, field_name);
    if (!ft) {
        error(std::format("chain field write: struct '{}' has no field '{}'", mid_sname, field_name));
        return make_stmt(node_line_, lir::SExprStmt{error_expr()});
    }

    if (!inside_unsafe_ && recv_type && TypeRef(recv_type).kind() == LogosType::Kind::Ptr)
        error("chain field write through raw pointer requires unsafe context");

    lir::LExprPtr val = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    if (TypeRef(ft).kind() != LogosType::Kind::Error && TypeRef(val->type).kind() != LogosType::Kind::Error
        && !types_compatible(val->type, ft))
        error(std::format("chain field write '{}.{}.{}': expected {}, got {}",
              recv_name, mid_name, field_name, type_str(ft), type_str(val->type)));

    lir::SChainFieldWrite scfw;
    scfw.receiver  = std::string(recv_name);
    scfw.mid_field = std::string(mid_name);
    scfw.field     = std::string(field_name);
    scfw.value     = std::move(val);
    return make_stmt(node_line_, std::move(scfw));
}

// a.mid.field op= val  →  a.mid.field = a.mid.field op val
lir::LStmt SemaChecker::lower_chain_field_compound_assign(TinyMapView node) {
    auto recv_name  = str_of(node.get(la::RECEIVER.code));
    auto mid_name   = str_of(node.get(la::NAME.code));
    auto field_name = str_of(node.get(la::FIELD.code));
    auto op_tok     = str_of(node.get(la::OP.code));
    std::string base_op = (op_tok.size() >= 2 && op_tok.back() == '=')
        ? std::string(op_tok.substr(0, op_tok.size() - 1))
        : std::string(op_tok);

    auto outer_sname = struct_name_of(recv_name);
    auto recv_type_for_cfca = lookup(recv_name);
    if (recv_type_for_cfca && TypeRef(recv_type_for_cfca).kind() == LogosType::Kind::Ref)
        error(std::format("chain field compound assign '{}': receiver is &T (shared reference)", recv_name));
    else if (recv_type_for_cfca && TypeRef(recv_type_for_cfca).kind() != LogosType::Kind::Ptr && !lookup_is_mut(recv_name))
        error(std::format("chain field compound assign to immutable variable '{}'", recv_name));
    if (recv_type_for_cfca && TypeRef(recv_type_for_cfca).kind() == LogosType::Kind::Ptr && !TypeRef(recv_type_for_cfca).mut_ptr())
        error(std::format("chain field compound assign '{}': receiver is *const pointer", recv_name));
    TypeRef outer_struct_t_cfca = recv_type_for_cfca;
    if (outer_struct_t_cfca && TypeRef(outer_struct_t_cfca).kind() == LogosType::Kind::Ptr)
        outer_struct_t_cfca = TypeRef(outer_struct_t_cfca).pointee();
    else if (outer_struct_t_cfca && is_ref_like(TypeRef(outer_struct_t_cfca).kind()))
        outer_struct_t_cfca = TypeRef(outer_struct_t_cfca).pointee();
    TypeRef mid_ft = outer_struct_t_cfca
        ? field_type_of_for_type(outer_struct_t_cfca, mid_name)
        : (outer_sname.empty() ? nullptr : field_type_of(std::string(outer_sname), mid_name));
    TypeRef mid_struct_t = mid_ft;
    if (mid_struct_t && TypeRef(mid_struct_t).kind() == LogosType::Kind::Ptr)
        mid_struct_t = TypeRef(mid_struct_t).pointee();
    auto mid_sname = mid_struct_t ? concrete_struct_name(mid_struct_t) : std::string{};
    TypeRef ft = mid_struct_t
        ? field_type_of_for_type(mid_struct_t, field_name)
        : (mid_sname.empty() ? nullptr : field_type_of(mid_sname, field_name));

    if (!outer_sname.empty() && !ft)
        error(std::format("chain field compound assign: could not resolve '{}.{}.{}'",
              recv_name, mid_name, field_name));

    // Lower RHS value.
    lir::LExprPtr rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();

    // Build read: recv.mid.field
    auto recv_expr = builder().var_ref(std::string(recv_name), lookup(recv_name) ? lookup(recv_name) : error_t());
    // mid read: recv.mid  (as EFieldRead)
    TypeRef mid_read_t = mid_ft ? mid_ft : error_t();
    auto mid_expr = builder().field_read(std::move(recv_expr), std::string(mid_name), mid_read_t);
    // field read: (recv.mid).field  (as EFieldRead)
    TypeRef ft2 = ft ? ft : error_t();
    auto field_expr = builder().field_read(std::move(mid_expr), std::string(field_name), ft2);

    // op(old, rhs)
    lir::LExprPtr combined = builder().bin_op(base_op, std::move(field_expr), std::move(rhs), ft2);

    lir::SChainFieldWrite scfw;
    scfw.receiver  = std::string(recv_name);
    scfw.mid_field = std::string(mid_name);
    scfw.field     = std::string(field_name);
    scfw.value     = std::move(combined);
    return make_stmt(node_line_, std::move(scfw));
}

// s.field op= expr  →  s.field = s.field op expr
lir::LStmt SemaChecker::lower_field_compound_assign(TinyMapView node) {
    auto recv_name  = str_of(node.get(la::RECEIVER.code));
    auto field_name = str_of(node.get(la::FIELD.code));
    auto op_tok     = str_of(node.get(la::OP.code));
    std::string base_op;
    if (op_tok.size() >= 2 && op_tok.back() == '=')
        base_op = std::string(op_tok.substr(0, op_tok.size() - 1));
    else
        base_op = std::string(op_tok);

    // Determine field type
    auto sname = struct_name_of(recv_name);
    TypeRef ft = sname.empty() ? nullptr : field_type_of(std::string(sname), field_name);

    if (!sname.empty() && !ft) {
        error(std::format("field compound assign: struct '{}' has no field '{}'", sname, field_name));
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return make_stmt(node_line_, lir::SBreak{});
    }

    // Check mutability
    auto recv_type = lookup(recv_name);
    if (recv_type && TypeRef(recv_type).kind() == LogosType::Kind::Ref)
        error(std::format("field compound assign to '{}': receiver is &T (shared reference)", recv_name));
    else if (!lookup_is_mut(recv_name) &&
             !(recv_type && (TypeRef(recv_type).kind() == LogosType::Kind::MutRef ||
                             TypeRef(recv_type).kind() == LogosType::Kind::Ptr)))
        error(std::format("field compound assign to immutable variable '{}'", recv_name));

    TypeRef recv_var_type = recv_type ? recv_type : error_t();
    TypeRef result_type   = ft ? ft : error_t();

    // lhs = s.field (read): EFieldRead{VarRef(recv_name), field_name}
    auto recv_varref = builder().var_ref(std::string(recv_name), recv_var_type);
    auto lhs_read    = builder().field_read(std::move(recv_varref), std::string(field_name), result_type);
    // rhs = expr
    auto rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();

    // Type-check: RHS must be compatible with the field's type.
    if (TypeRef(result_type).kind() != LogosType::Kind::Error &&
        TypeRef(rhs->type).kind() != LogosType::Kind::Error &&
        !types_compatible(rhs->type, result_type)) {
        error(std::format("compound assignment to '{}.{}': type mismatch — expected {}, got {}",
              recv_name, field_name, type_str(result_type), type_str(rhs->type)));
    }
    // combined = lhs op rhs
    auto combined = builder().bin_op(base_op, std::move(lhs_read), std::move(rhs), result_type);

    lir::SFieldWrite sfw;
    sfw.receiver = std::string(recv_name);
    sfw.field    = std::string(field_name);
    sfw.value    = std::move(combined);
    return make_stmt(node_line_, std::move(sfw));
}

lir::LStmt SemaChecker::lower_tuple_field_write(TinyMapView node) {
    auto recv_name = str_of(node.get(la::RECEIVER.code));
    auto idx_sv    = str_of(node.get(la::INDEX.code));
    uint64_t idx   = (uint64_t)parse_int_literal(idx_sv);

    TypeRef recv_t = lookup(recv_name);
    if (!recv_t) {
        error(std::format("tuple field write: undefined variable '{}'", recv_name));
        return make_stmt(node_line_, lir::STupleWrite{std::string(recv_name), (uint32_t)idx, error_expr()});
    }
    // Strip &mut wrapper if present
    if (TypeRef(recv_t).kind() == LogosType::Kind::MutRef && TypeRef(recv_t).pointee())
        recv_t = TypeRef(recv_t).pointee();

    if (TypeRef(recv_t).kind() != LogosType::Kind::Tuple) {
        error(std::format("tuple field write: '{}' is not a tuple (got {})", recv_name, type_str(recv_t)));
        return make_stmt(node_line_, lir::STupleWrite{std::string(recv_name), (uint32_t)idx, error_expr()});
    }
    if (idx >= TypeRef(recv_t).tuple_elems().size()) {
        error(std::format("tuple field write: index {} out of range (tuple has {} elements)",
                          idx, TypeRef(recv_t).tuple_elems().size()));
        return make_stmt(node_line_, lir::STupleWrite{std::string(recv_name), (uint32_t)idx, error_expr()});
    }
    TypeRef orig_recv_t = lookup(recv_name);
    bool via_mut_ref = orig_recv_t && TypeRef(orig_recv_t).kind() == LogosType::Kind::MutRef;
    if (!lookup_is_mut(recv_name) && !via_mut_ref) {
        error(std::format("tuple field write to immutable variable '{}'", recv_name));
    }

    TypeRef ft = TypeRef(recv_t).tuple_elems()[idx];
    lir::LExprPtr val = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    if (TypeRef(ft).kind() != LogosType::Kind::Error &&
        TypeRef(val->type).kind() != LogosType::Kind::Error &&
        !types_compatible(val->type, ft)) {
        error(std::format("tuple field write '{}.{}': expected {}, got {}",
              recv_name, idx, type_str(ft), type_str(val->type)));
    }
    // Narrow intlit
    if (TypeRef(ft).kind() != LogosType::Kind::Error && TypeRef(val->type).kind() == LogosType::Kind::IntLit)
        if (auto v = get_intlit_value(val.get()))
            if (!intlit_fits(*v, TypeRef(ft).kind()))
                error(std::format("tuple field write '{}.{}': value {} does not fit in {}",
                      recv_name, idx, *v, type_str(ft)));
    return make_stmt(node_line_, lir::STupleWrite{std::string(recv_name), (uint32_t)idx, std::move(val), recv_t});
}

lir::LStmt SemaChecker::lower_deref_field_write(TinyMapView node) {
    auto recv_name  = str_of(node.get(la::RECEIVER.code));
    auto field_name = str_of(node.get(la::FIELD.code));

    TypeRef ptr_type = lookup(recv_name);
    if (!ptr_type) {
        error(std::format("deref-field-write: undefined variable '{}'", recv_name));
    } else if (!is_ref_like(TypeRef(ptr_type).kind()) || !TypeRef(ptr_type).pointee()) {
        error(std::format("deref-field-write: '{}' is not a pointer or reference (got {})",
                          recv_name, type_str(ptr_type)));
    } else if (TypeRef(ptr_type).kind() == LogosType::Kind::Ptr && !TypeRef(ptr_type).mut_ptr()) {
        error(std::format("deref-field-write: '{}' is a *const pointer (need *mut)",
                          recv_name));
    } else if (TypeRef(ptr_type).kind() == LogosType::Kind::Ref) {
        error(std::format("deref-field-write: '{}' is a &T (shared reference, need &mut T)",
                          recv_name));
    }
    // Writing through &mut T is safe; writing through raw *mut T requires unsafe
    if (ptr_type && TypeRef(ptr_type).kind() == LogosType::Kind::Ptr && !inside_unsafe_)
        error("write through raw pointer field requires unsafe context");

    TypeRef pointee = (ptr_type && TypeRef(ptr_type).pointee()) ? TypeRef(ptr_type).pointee() : nullptr;
    std::string type_name;
    TypeRef ft = nullptr;
    if (pointee) {
        if (TypeRef(pointee).kind() == LogosType::Kind::Struct ||
            TypeRef(pointee).kind() == LogosType::Kind::ZonedStruct) {
            type_name = concrete_struct_name(pointee);
            ft = field_type_of_for_type(pointee, field_name);
        } else {
            error(std::format("deref-field-write: '{}' points to non-struct type {}",
                              recv_name, type_str(pointee)));
        }
    }
    if (pointee && ft == nullptr) {
        error(std::format("deref-field-write: type '{}' has no field '{}'",
                          type_name, field_name));
    }
    // Pub check for struct/datatype fields.
    if (pointee && (TypeRef(pointee).kind() == LogosType::Kind::Struct || TypeRef(pointee).kind() == LogosType::Kind::ZonedStruct) && ft) {
        SemaStructInfo* si = nullptr;
        { auto it = structs_.find(std::string(TypeRef(pointee).struct_name())); if (it != structs_.end()) si = &it->second; }
        if (!si) { auto it = datatypes_.find(std::string(TypeRef(pointee).struct_name())); if (it != datatypes_.end()) si = &it->second; }
        if (si) {
            for (auto& f : si->fields) {
                if (f.name == field_name) {
                    check_pub_access(f.is_pub, si->package, field_name);
                    break;
                }
            }
        }
    }

    lir::LExprPtr val = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
        TypeRef(val->type).kind() != LogosType::Kind::Error &&
        !types_compatible(val->type, ft)) {
        error(std::format("deref-field-write '(*{}).{}': expected {}, got {}",
              recv_name, field_name, type_str(ft), type_str(val->type)));
    }
    if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
        TypeRef(val->type).kind() == LogosType::Kind::IntLit)
        if (auto v = get_intlit_value(val.get()))
            if (!intlit_fits(*v, TypeRef(ft).kind()))
                error(std::format("deref-field-write '(*{}).{}': value {} does not fit in {}",
                      recv_name, field_name, *v, type_str(ft)));
    // Check array literal elements against narrow array field type.
    if (ft && TypeRef(ft).kind() == LogosType::Kind::Array && TypeRef(ft).elem() &&
        TypeRef(val->type).kind() == LogosType::Kind::Array)
        if (auto* al = std::get_if<lir::EArrLit>(&val->kind))
            for (size_t i = 0; i < al->elems.size(); ++i)
                if (TypeRef(al->elems[i]->type).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(al->elems[i].get()))
                        if (!intlit_fits(*v, TypeRef(ft).elem().kind()))
                            error(std::format("deref-field-write '(*{}).{}': array element {}: value {} does not fit in {}",
                                  recv_name, field_name, i, *v, type_str(TypeRef(ft).elem())));
    // Check tuple literal elements against narrow tuple field element types.
    if (ft && TypeRef(ft).kind() == LogosType::Kind::Tuple && TypeRef(val->type).kind() == LogosType::Kind::Tuple)
        if (auto* tl = std::get_if<lir::ETupleLit>(&val->kind))
            for (size_t i = 0; i < tl->elems.size() && i < TypeRef(ft).tuple_elems().size(); ++i) {
                if (TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(tl->elems[i].get()))
                        if (TypeRef(ft).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(ft).tuple_elems()[i]).kind()))
                            error(std::format("deref-field-write '(*{}).{}': tuple element {}: value {} does not fit in {}",
                                  recv_name, field_name, i, *v, type_str(TypeRef(ft).tuple_elems()[i])));
                if (TypeRef(ft).tuple_elems()[i] && TypeRef(TypeRef(ft).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                    TypeRef(TypeRef(ft).tuple_elems()[i]).elem() && TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::Array)
                    if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[i]->kind))
                        for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                            if (TypeRef(ial->elems[ii]->type).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(ial->elems[ii].get()))
                                    if (!intlit_fits(*v, TypeRef(TypeRef(ft).tuple_elems()[i]).elem().kind()))
                                        error(std::format("deref-field-write '(*{}).{}': tuple element {}: array element {}: value {} does not fit in {}",
                                              recv_name, field_name, i, ii, *v, type_str(TypeRef(TypeRef(ft).tuple_elems()[i]).elem())));

                if (TypeRef(ft).tuple_elems()[i] && TypeRef(TypeRef(ft).tuple_elems()[i]).kind() == LogosType::Kind::Tuple &&
                    TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::Tuple)
                    if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[i]->kind))
                        for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems().size(); ++ii)
                            if (TypeRef(itl->elems[ii]->type).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(itl->elems[ii].get()))
                                    if (TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii]).kind()))
                                        error(std::format("deref-field-write '(*{}).{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                              recv_name, field_name, i, ii, *v, type_str(TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii])));
                }

    lir::SDerefFieldWrite sdfw;
    sdfw.receiver  = std::string(recv_name);
    sdfw.type_name = type_name;
    sdfw.field     = std::string(field_name);
    sdfw.value     = std::move(val);
    return make_stmt(node_line_, std::move(sdfw));
}

lir::LStmt SemaChecker::lower_index_write(TinyMapView node) {
    auto arr_name = str_of(node.get(la::NAME.code));
    auto arr_type = lookup(arr_name);
    if (!arr_type) {
        error(std::format("index write: undefined variable '{}'", arr_name));
    } else if (TypeRef(arr_type).kind() != LogosType::Kind::Array &&
               TypeRef(arr_type).kind() != LogosType::Kind::Ptr &&
               TypeRef(arr_type).kind() != LogosType::Kind::Ref &&
               TypeRef(arr_type).kind() != LogosType::Kind::MutRef &&
               TypeRef(arr_type).kind() != LogosType::Kind::Error) {
        error(std::format("index write: '{}' is not an array or pointer (got {})",
              arr_name, type_str(arr_type)));
    } else if (TypeRef(arr_type).kind() == LogosType::Kind::Array && !lookup_is_mut(arr_name)) {
        error(std::format("index write to immutable array '{}'", arr_name));
    } else if (TypeRef(arr_type).kind() == LogosType::Kind::Ptr && !TypeRef(arr_type).mut_ptr()) {
        error(std::format("index write through *const pointer '{}'", arr_name));
    } else if (TypeRef(arr_type).kind() == LogosType::Kind::Ptr && !inside_unsafe_) {
        error(std::format("index write through raw pointer '{}' requires unsafe context", arr_name));
    } else if (TypeRef(arr_type).kind() == LogosType::Kind::Ref) {
        error(std::format("index write through &T (shared reference) '{}'", arr_name));
    }

    lir::LExprPtr idx = node.has_key(la::LHS)
        ? lower_expr(map_of(node.get(la::LHS.code))) : error_expr();
    if (!is_integer(idx->type))
        error(std::format("array index must be an integer, got {}", type_str(idx->type)));

    TypeRef elem_type = nullptr;
    if (arr_type) {
        if (TypeRef(arr_type).kind() == LogosType::Kind::Array) elem_type = TypeRef(arr_type).elem();
        else if (TypeRef(arr_type).kind() == LogosType::Kind::Ptr ||
                 TypeRef(arr_type).kind() == LogosType::Kind::Ref ||
                 TypeRef(arr_type).kind() == LogosType::Kind::MutRef) elem_type = TypeRef(arr_type).pointee();
    }

    lir::LExprPtr val = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
    if (elem_type && TypeRef(elem_type).kind() != LogosType::Kind::Error &&
        TypeRef(val->type).kind() != LogosType::Kind::Error &&
        !types_compatible(val->type, elem_type)) {
        error(std::format("index write to '{}': expected {}, got {}",
              arr_name, type_str(elem_type), type_str(val->type)));
    }
    if (elem_type && TypeRef(elem_type).kind() != LogosType::Kind::Error &&
        TypeRef(val->type).kind() == LogosType::Kind::IntLit)
        if (auto v = get_intlit_value(val.get()))
            if (!intlit_fits(*v, TypeRef(elem_type).kind()))
                error(std::format("index write to '{}': value {} does not fit in {}",
                      arr_name, *v, type_str(elem_type)));
    // Check array literal elements against narrow nested array element type.
    if (elem_type && TypeRef(elem_type).kind() == LogosType::Kind::Array && TypeRef(elem_type).elem() &&
        TypeRef(val->type).kind() == LogosType::Kind::Array)
        if (auto* al = std::get_if<lir::EArrLit>(&val->kind))
            for (size_t i = 0; i < al->elems.size(); ++i)
                if (TypeRef(al->elems[i]->type).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(al->elems[i].get()))
                        if (!intlit_fits(*v, TypeRef(elem_type).elem().kind()))
                            error(std::format("index write to '{}': array element {}: value {} does not fit in {}",
                                  arr_name, i, *v, type_str(TypeRef(elem_type).elem())));
    // Check tuple literal elements against narrow nested tuple element type.
    if (elem_type && TypeRef(elem_type).kind() == LogosType::Kind::Tuple &&
        TypeRef(val->type).kind() == LogosType::Kind::Tuple)
        if (auto* tl = std::get_if<lir::ETupleLit>(&val->kind))
            for (size_t i = 0; i < tl->elems.size() && i < TypeRef(elem_type).tuple_elems().size(); ++i) {
                if (TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(tl->elems[i].get()))
                        if (TypeRef(elem_type).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(elem_type).tuple_elems()[i]).kind()))
                            error(std::format("index write to '{}': tuple element {}: value {} does not fit in {}",
                                  arr_name, i, *v, type_str(TypeRef(elem_type).tuple_elems()[i])));
                if (TypeRef(elem_type).tuple_elems()[i] && TypeRef(TypeRef(elem_type).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                    TypeRef(TypeRef(elem_type).tuple_elems()[i]).elem() && TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::Array)
                    if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[i]->kind))
                        for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                            if (TypeRef(ial->elems[ii]->type).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(ial->elems[ii].get()))
                                    if (!intlit_fits(*v, TypeRef(TypeRef(elem_type).tuple_elems()[i]).elem().kind()))
                                        error(std::format("index write to '{}': tuple element {}: array element {}: value {} does not fit in {}",
                                              arr_name, i, ii, *v, type_str(TypeRef(TypeRef(elem_type).tuple_elems()[i]).elem())));

                if (TypeRef(elem_type).tuple_elems()[i] && TypeRef(TypeRef(elem_type).tuple_elems()[i]).kind() == LogosType::Kind::Tuple &&
                    TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::Tuple)
                    if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[i]->kind))
                        for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(TypeRef(elem_type).tuple_elems()[i]).tuple_elems().size(); ++ii)
                            if (TypeRef(itl->elems[ii]->type).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(itl->elems[ii].get()))
                                    if (TypeRef(TypeRef(elem_type).tuple_elems()[i]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(elem_type).tuple_elems()[i]).tuple_elems()[ii]).kind()))
                                        error(std::format("index write to '{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                              arr_name, i, ii, *v, type_str(TypeRef(TypeRef(elem_type).tuple_elems()[i]).tuple_elems()[ii])));
                }

    lir::SIndexWrite siw;
    siw.arr   = std::string(arr_name);
    siw.index = std::move(idx);
    siw.value = std::move(val);
    return make_stmt(node_line_, std::move(siw));
}

// arr[i] op= expr  →  arr[i] = arr[i] op expr
lir::LStmt SemaChecker::lower_index_compound_assign(TinyMapView node) {
    auto arr_name = str_of(node.get(la::NAME.code));
    auto op_tok   = str_of(node.get(la::OP.code));
    std::string base_op;
    if (op_tok.size() >= 2 && op_tok.back() == '=')
        base_op = std::string(op_tok.substr(0, op_tok.size() - 1));
    else
        base_op = std::string(op_tok);

    auto arr_type = lookup(arr_name);
    if (!arr_type) {
        error(std::format("index compound assign: undefined variable '{}'", arr_name));
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return make_stmt(node_line_, lir::SBreak{});
    }
    if (TypeRef(arr_type).kind() == LogosType::Kind::Array && !lookup_is_mut(arr_name))
        error(std::format("index compound assign to immutable array '{}'", arr_name));

    TypeRef elem_type = nullptr;
    if (TypeRef(arr_type).kind() == LogosType::Kind::Array) elem_type = TypeRef(arr_type).elem();
    else if (TypeRef(arr_type).kind() == LogosType::Kind::Ptr ||
             TypeRef(arr_type).kind() == LogosType::Kind::Ref ||
             TypeRef(arr_type).kind() == LogosType::Kind::MutRef) elem_type = TypeRef(arr_type).pointee();
    if (!elem_type) elem_type = error_t();

    // Lower the index expression twice from the AST (pure expr — no side effects expected)
    lir::LExprPtr idx_for_write = node.has_key(la::LHS)
        ? lower_expr(map_of(node.get(la::LHS.code))) : error_expr();
    lir::LExprPtr idx_for_read  = node.has_key(la::LHS)
        ? lower_expr(map_of(node.get(la::LHS.code))) : error_expr();

    if (!is_integer(idx_for_write->type))
        error(std::format("array index must be an integer, got {}", type_str(idx_for_write->type)));

    // rhs value
    auto rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();

    // Type-check: RHS must be compatible with the element type.
    if (TypeRef(elem_type).kind() != LogosType::Kind::Error &&
        TypeRef(rhs->type).kind() != LogosType::Kind::Error &&
        !types_compatible(rhs->type, elem_type)) {
        error(std::format("compound assignment to '{}[i]': type mismatch — expected {}, got {}",
              arr_name, type_str(elem_type), type_str(rhs->type)));
    }
    // Build lhs read: arr[idx_for_read]
    auto arr_recv = builder().var_ref(std::string(arr_name), arr_type);
    auto lhs_read = builder().index_read(std::move(arr_recv), std::move(idx_for_read), elem_type);
    // combined = lhs op rhs
    auto combined = builder().bin_op(base_op, std::move(lhs_read), std::move(rhs), elem_type);

    lir::SIndexWrite siw;
    siw.arr   = std::string(arr_name);
    siw.index = std::move(idx_for_write);
    siw.value = std::move(combined);
    return make_stmt(node_line_, std::move(siw));
}

lir::LStmt SemaChecker::lower_field_index_write(TinyMapView node) {
    auto recv_name  = str_of(node.get(la::RECEIVER.code));
    auto field_name = str_of(node.get(la::FIELD.code));

    // Resolve field type — must be *mut T.
    auto recv_t = lookup(recv_name);
    if (!recv_t) error(std::format("field index write: undefined variable '{}'", recv_name));

    // Unwrap pointer/reference receiver (class/struct-via-ptr/ref).
    TypeRef base_t = recv_t;
    if (base_t && is_ref_like(TypeRef(base_t).kind())) base_t = TypeRef(base_t).pointee();

    TypeRef field_t = nullptr;
    if (base_t) {
        auto sname = struct_name_from_type(base_t);
        if (!sname.empty()) field_t = field_type_of_for_type(base_t, field_name);
    }

    if (!field_t)
        error(std::format("field index write: cannot resolve field '{}.{}'", recv_name, field_name));

    // Pub check for struct/datatype fields.
    if (base_t && field_t) {
        auto sname = struct_name_from_type(base_t);
        if (!sname.empty()) {
            SemaStructInfo* si = nullptr;
            { auto it = structs_.find(std::string(sname)); if (it != structs_.end()) si = &it->second; }
            if (!si) { auto it = datatypes_.find(std::string(sname)); if (it != datatypes_.end()) si = &it->second; }
            if (si) {
                for (auto& f : si->fields) {
                    if (f.name == field_name) {
                        check_pub_access(f.is_pub, si->package, field_name);
                        break;
                    }
                }
            }
        }
    }

    if (field_t && TypeRef(field_t).kind() != LogosType::Kind::Ptr &&
                   TypeRef(field_t).kind() != LogosType::Kind::Ref &&
                   TypeRef(field_t).kind() != LogosType::Kind::MutRef &&
                   TypeRef(field_t).kind() != LogosType::Kind::Array)
        error(std::format("field index write: field '{}.{}' is not a pointer/reference or array (got {})",
              recv_name, field_name, type_str(field_t)));
    if (field_t && TypeRef(field_t).kind() == LogosType::Kind::Ptr && !TypeRef(field_t).mut_ptr())
        error(std::format("field index write: field '{}.{}' is *const, cannot write",
              recv_name, field_name));
    if (field_t && TypeRef(field_t).kind() == LogosType::Kind::Ptr && TypeRef(field_t).mut_ptr() && !inside_unsafe_)
        error(std::format("field index write '{}.{}[i]' through raw pointer requires unsafe context",
              recv_name, field_name));
    if (field_t && TypeRef(field_t).kind() == LogosType::Kind::Ref)
        error(std::format("field index write: field '{}.{}' is &T (shared reference), cannot write",
              recv_name, field_name));

    TypeRef elem_t = nullptr;
    if (field_t) {
        if (TypeRef(field_t).kind() == LogosType::Kind::Ptr ||
            TypeRef(field_t).kind() == LogosType::Kind::Ref ||
            TypeRef(field_t).kind() == LogosType::Kind::MutRef) elem_t = TypeRef(field_t).pointee();
        else if (TypeRef(field_t).kind() == LogosType::Kind::Array) elem_t = TypeRef(field_t).elem();
    }

    lir::LExprPtr idx = node.has_key(la::LHS)
        ? lower_expr(map_of(node.get(la::LHS.code))) : error_expr();
    if (!is_integer(idx->type))
        error(std::format("field index write: index must be integer, got {}", type_str(idx->type)));

    lir::LExprPtr val = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
    if (elem_t && TypeRef(elem_t).kind() != LogosType::Kind::Error &&
        TypeRef(val->type).kind() != LogosType::Kind::Error &&
        !types_compatible(val->type, elem_t)) {
        error(std::format("field index write '{}.{}[i]': expected {}, got {}",
              recv_name, field_name, type_str(elem_t), type_str(val->type)));
    }
    if (elem_t && TypeRef(elem_t).kind() != LogosType::Kind::Error &&
        TypeRef(val->type).kind() == LogosType::Kind::IntLit)
        if (auto v = get_intlit_value(val.get()))
            if (!intlit_fits(*v, TypeRef(elem_t).kind()))
                error(std::format("field index write '{}.{}[i]': value {} does not fit in {}",
                      recv_name, field_name, *v, type_str(elem_t)));
    // Check array literal elements against narrow nested array element type.
    if (elem_t && TypeRef(elem_t).kind() == LogosType::Kind::Array && TypeRef(elem_t).elem() &&
        TypeRef(val->type).kind() == LogosType::Kind::Array)
        if (auto* al = std::get_if<lir::EArrLit>(&val->kind))
            for (size_t i = 0; i < al->elems.size(); ++i)
                if (TypeRef(al->elems[i]->type).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(al->elems[i].get()))
                        if (!intlit_fits(*v, TypeRef(elem_t).elem().kind()))
                            error(std::format("field index write '{}.{}[i]': array element {}: value {} does not fit in {}",
                                  recv_name, field_name, i, *v, type_str(TypeRef(elem_t).elem())));
    // Check tuple literal elements against narrow nested tuple element type.
    if (elem_t && TypeRef(elem_t).kind() == LogosType::Kind::Tuple &&
        TypeRef(val->type).kind() == LogosType::Kind::Tuple)
        if (auto* tl = std::get_if<lir::ETupleLit>(&val->kind))
            for (size_t i = 0; i < tl->elems.size() && i < TypeRef(elem_t).tuple_elems().size(); ++i) {
                if (TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(tl->elems[i].get()))
                        if (TypeRef(elem_t).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(elem_t).tuple_elems()[i]).kind()))
                            error(std::format("field index write '{}.{}[i]': tuple element {}: value {} does not fit in {}",
                                  recv_name, field_name, i, *v, type_str(TypeRef(elem_t).tuple_elems()[i])));
                if (TypeRef(elem_t).tuple_elems()[i] && TypeRef(TypeRef(elem_t).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                    TypeRef(TypeRef(elem_t).tuple_elems()[i]).elem() && TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::Array)
                    if (auto* ial = std::get_if<lir::EArrLit>(&tl->elems[i]->kind))
                        for (size_t ii = 0; ii < ial->elems.size(); ++ii)
                            if (TypeRef(ial->elems[ii]->type).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(ial->elems[ii].get()))
                                    if (!intlit_fits(*v, TypeRef(TypeRef(elem_t).tuple_elems()[i]).elem().kind()))
                                        error(std::format("field index write '{}.{}[i]': tuple element {}: array element {}: value {} does not fit in {}",
                                              recv_name, field_name, i, ii, *v, type_str(TypeRef(TypeRef(elem_t).tuple_elems()[i]).elem())));

                if (TypeRef(elem_t).tuple_elems()[i] && TypeRef(TypeRef(elem_t).tuple_elems()[i]).kind() == LogosType::Kind::Tuple &&
                    TypeRef(tl->elems[i]->type).kind() == LogosType::Kind::Tuple)
                    if (auto* itl = std::get_if<lir::ETupleLit>(&tl->elems[i]->kind))
                        for (size_t ii = 0; ii < itl->elems.size() && ii < TypeRef(TypeRef(elem_t).tuple_elems()[i]).tuple_elems().size(); ++ii)
                            if (TypeRef(itl->elems[ii]->type).kind() == LogosType::Kind::IntLit)
                                if (auto v = get_intlit_value(itl->elems[ii].get()))
                                    if (TypeRef(TypeRef(elem_t).tuple_elems()[i]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(elem_t).tuple_elems()[i]).tuple_elems()[ii]).kind()))
                                        error(std::format("field index write '{}.{}[i]': tuple element {}: sub-element {}: value {} does not fit in {}",
                                              recv_name, field_name, i, ii, *v, type_str(TypeRef(TypeRef(elem_t).tuple_elems()[i]).tuple_elems()[ii])));
                }

    lir::SFieldIndexWrite sfiw;
    sfiw.receiver = std::string(recv_name);
    sfiw.field    = std::string(field_name);
    sfiw.index    = std::move(idx);
    sfiw.value    = std::move(val);
    return make_stmt(node_line_, std::move(sfiw));
}

lir::LStmt SemaChecker::lower_match(TinyMapView node) {
    lir::LExprPtr scrut;
    TypeRef scrut_type = error_t();
    if (node.has_key(la::VALUE)) {
        scrut = lower_expr(map_of(node.get(la::VALUE.code)));
        scrut_type = scrut->type;
    } else { scrut = error_expr(); }

    // Detect Hermes scalar patterns; they require scrut to be an AnyVal
    // addressable in a variable.  We hoist scrut into a synthetic let so the
    // synthesized guards can take `&__hmatch_av` without re-evaluating scrut.
    auto is_hermes_pat_code = [](int32_t pc) {
        return pc == la::PAT_HERMES_NULL || pc == la::PAT_HERMES_BOOL ||
               pc == la::PAT_HERMES_INT  || pc == la::PAT_HERMES_STR  ||
               pc == la::PAT_HERMES_MAP  || pc == la::PAT_HERMES_ARR  ||
               pc == la::PAT_HERMES_TYPED_ARR || pc == la::PAT_HERMES_TYPED_MAP;
    };
    // A pattern tree "contains" a Hermes scalar if it IS one, or a PAT_OR
    // alt is one.  We only unwrap PAT_OR here — nested PAT_AT/PAT_REF wrapping
    // Hermes patterns is diagnosed by build_pattern via in_match_hermes_ctx_.
    auto pat_contains_hermes = [&](TinyMapView p) -> bool {
        if (is_hermes_pat_code(code_of(p))) return true;
        if (code_of(p) == la::PAT_OR && p.has_key(la::ITEMS)) {
            auto arr = arr_of(p.get(la::ITEMS.code));
            for (uint64_t i = 0; i < arr.size(); ++i)
                if (is_hermes_pat_code(code_of(map_of(arr.get(i))))) return true;
        }
        return false;
    };
    bool has_hermes_pat = false;
    if (node.has_key(la::ITEMS)) {
        auto arms = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < arms.size(); ++i) {
            auto arm = map_of(arms.get(i));
            if (code_of(arm) != la::MATCH_ARM) continue;
            if (!arm.has_key(la::LHS)) continue;
            if (pat_contains_hermes(map_of(arm.get(la::LHS.code)))) {
                has_hermes_pat = true; break;
            }
        }
    }

    // For Hermes patterns we hoist two locals:
    //   let __hmatch_view = <scrut>;            // the view (Hermes/View/Static or &)
    //   let __hmatch_root: AnyVal = view.root(); // root AnyVal, used by guard helpers
    std::string root_var;
    std::string base_var;
    lir::LStmt hoist_let_view;
    lir::LStmt hoist_let_root;
    lir::LStmt hoist_let_base;
    bool has_hoist_let = false;
    TypeRef anyval_t = nullptr;
    if (has_hermes_pat) {
        if (!hermes_view_inner(scrut_type)) {
            error(std::format(
                "match with Hermes patterns requires a view scrutinee "
                "(Hermes, HermesView, or HermesStatic; use & to borrow); "
                "got {}", type_str(scrut_type)));
        }
        std::string view_var = "__hmatch_view_" + std::to_string(tmp_var_count_++);
        {
            lir::SLet sl;
            sl.name = view_var; sl.type = scrut_type; sl.is_mut = false;
            sl.value = std::move(scrut);
            hoist_let_view = make_stmt(node_line_, std::move(sl));
        }
        anyval_t = make_datatype_type("AnyVal");
        root_var = "__hmatch_root_" + std::to_string(tmp_var_count_++);
        {
            auto view_ref = builder().var_ref(view_var, scrut_type);
            auto root_call = builder().method_call(std::move(view_ref), "root", "", {}, {}, -1, anyval_t);
            lir::SLet sl;
            sl.name = root_var; sl.type = anyval_t; sl.is_mut = false;
            sl.value = std::move(root_call);
            hoist_let_root = make_stmt(node_line_, std::move(sl));
        }
        base_var = "__hmatch_base_" + std::to_string(tmp_var_count_++);
        {
            TypeRef u8_ptr_t = make_ptr(false, prim(LogosType::Kind::U8));
            auto view_ref = builder().var_ref(view_var, scrut_type);
            auto base_call = builder().method_call(std::move(view_ref), "base", "", {}, {}, -1, u8_ptr_t);
            lir::SLet sl;
            sl.name = base_var; sl.type = u8_ptr_t; sl.is_mut = false;
            sl.value = std::move(base_call);
            hoist_let_base = make_stmt(node_line_, std::move(sl));
        }
        has_hoist_let = true;
        scrut = builder().var_ref(view_var, scrut_type);
    }

    lir::SMatch smatch;
    smatch.scrut = std::move(scrut);

    if (node.has_key(la::ITEMS)) {
        auto arms = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < arms.size(); ++i) {
            auto arm = map_of(arms.get(i));
            if (code_of(arm) != la::MATCH_ARM) continue;

            // Synthesize guard for Hermes patterns (scalar + structural).
            lir::LExprPtr synth_guard;
            std::vector<lir::LStmt> body_prologue;
            std::vector<HermesPatBinding> body_binds;
            if (has_hermes_pat && arm.has_key(la::LHS)) {
                std::vector<lir::LStmt> g_stmts;
                std::vector<HermesPatBinding> g_binds;
                auto raw = build_hermes_pat_guard(
                    map_of(arm.get(la::LHS.code)), root_var, anyval_t, base_var,
                    g_stmts, g_binds);
                if (!g_stmts.empty() && raw) {
                    auto blk = std::make_unique<lir::LBlock>();
                    blk->stmts = std::move(g_stmts);
                    synth_guard = builder().block_expr(std::move(blk), std::move(raw), bool_t());
                } else {
                    synth_guard = std::move(raw);
                }
                // Re-run pattern lowering to produce parallel stmts/bindings
                // for body scope. Locals get fresh tmp_var_count_ names; the
                // bindings' av_var refers to those new names, consistent with
                // body_prologue.
                if (!g_binds.empty()) {
                    (void)build_hermes_pat_guard(
                        map_of(arm.get(la::LHS.code)), root_var, anyval_t,
                        base_var, body_prologue, body_binds);
                }
            }

            // Build pattern
            in_match_hermes_ctx_ = has_hermes_pat;
            lir::Pattern pat = arm.has_key(la::LHS)
                ? build_pattern(map_of(arm.get(la::LHS.code)), scrut_type)
                : lir::PatWild{"_"};
            in_match_hermes_ctx_ = false;

            // Build body block — push pattern bindings into scope
            push_scope();
            bind_pattern(pat, scrut_type);
            // Register Hermes @-pattern bindings in scope (visible in body + guard).
            for (const auto& b : body_binds) {
                define(b.name, anyval_t, /*is_mut=*/false);
            }

            // Optional guard: `pattern if expr =>`
            std::optional<lir::LExprPtr> guard;
            if (arm.has_key(la::GUARD)) {
                auto g = lower_expr(map_of(arm.get(la::GUARD.code)));
                if (TypeRef(g->type).kind() != LogosType::Kind::Bool &&
                    TypeRef(g->type).kind() != LogosType::Kind::Error)
                    error("match guard must be bool");
                guard = std::move(g);
            }
            // Merge synthesized Hermes guard with user guard.  Put the
            // synth guard FIRST so `&&` short-circuits on type-mismatch
            // (matches Rust semantics: guard only runs when the pattern
            // matches, preventing stray side effects from user guards).
            if (synth_guard) {
                if (guard) {
                    auto merged = builder().bin_op("&&", std::move(synth_guard), std::move(*guard), bool_t());
                    guard = std::move(merged);
                } else {
                    guard = std::move(synth_guard);
                }
            }

            lir::LBlockPtr body = std::make_unique<lir::LBlock>();
            if (arm.has_key(la::BODY)) {
                auto body_node = map_of(arm.get(la::BODY.code));
                if (code_of(body_node) == la::BLOCK) {
                    *body = lower_block(body_node);
                } else {
                    body->stmts.push_back(lower_stmt(body_node));
                }
            } else if (arm.has_key(la::EXPR)) {
                auto val = lower_expr(map_of(arm.get(la::EXPR.code)));
                if (match_in_tail_position_) {
                    // Tail-position match: EXPR arms produce the function's return value.
                    lir::SReturn ret; ret.value = std::move(val);
                    body->stmts.push_back(make_stmt(node_line_, std::move(ret)));
                } else {
                    // Statement-position match: EXPR arms are evaluated for side effects.
                    lir::SExprStmt es; es.expr = std::move(val);
                    body->stmts.push_back(make_stmt(node_line_, std::move(es)));
                }
            }
            // Prepend Hermes @-pattern prologue (helper __hp_N lets + user
            // binding lets) to body so bindings are live inside the arm body.
            if (!body_prologue.empty() || !body_binds.empty()) {
                std::vector<lir::LStmt> prologue = std::move(body_prologue);
                for (const auto& b : body_binds) {
                    lir::SLet sl;
                    sl.name = b.name; sl.type = anyval_t; sl.is_mut = false;
                    sl.value = builder().var_ref(b.av_var, anyval_t);
                    prologue.push_back(make_stmt(node_line_, std::move(sl)));
                }
                body->stmts.insert(body->stmts.begin(),
                    std::make_move_iterator(prologue.begin()),
                    std::make_move_iterator(prologue.end()));
            }
            pop_scope();
            smatch.arms.push_back({std::move(pat), std::move(body), std::move(guard)});
        }
    }
    // ── Exhaustiveness check ─────────────────────────────────
    // Verify all variants of an enum (or bool) are covered.
    {
        bool has_wild = false;
        for (auto& arm : smatch.arms) {
            if (!arm.guard && pat_ref_of(arm.pat).kind() == lir_schema::pat::Code::Wild) {
                has_wild = true;
                break;
            }
        }
        if (!has_wild && TypeRef(scrut_type).kind() == LogosType::Kind::Enum) {
            auto [epkg_match, esi_match] = find_enum_by_name(TypeRef(scrut_type).enum_name());
            auto eit = esi_match ? enums_.find(sema_key(epkg_match, TypeRef(scrut_type).enum_name())) : enums_.end();
            if (eit == enums_.end()) eit = enums_.find(TypeRef(scrut_type).enum_name());
            if (eit != enums_.end()) {
                std::set<int32_t> covered;
                auto add_pat = [&](const lir::Pattern& p) {
                    if (auto* pv = std::get_if<lir::PatVariant>(&p.kind))
                        covered.insert(pv->disc);
                    else if (auto* pvd = std::get_if<lir::PatVariantData>(&p.kind))
                        covered.insert(pvd->disc);
                };
                for (auto& arm : smatch.arms) {
                    if (arm.guard) continue;
                    if (auto* por = std::get_if<lir::PatOr>(&arm.pat.kind)) {
                        for (auto& alt : por->alts) add_pat(alt);
                    } else {
                        add_pat(arm.pat);
                    }
                }
                std::string missing;
                for (auto& v : eit->second.variants) {
                    if (covered.find(v.value) == covered.end()) {
                        if (!missing.empty()) missing += ", ";
                        missing += std::string(v.name);
                    }
                }
                if (!missing.empty())
                    error(std::format("match is not exhaustive — missing variant(s): {}",
                          missing));
            }
        }
        if (!has_wild && TypeRef(scrut_type).kind() == LogosType::Kind::Bool) {
            bool has_true = false, has_false = false;
            for (auto& arm : smatch.arms) {
                if (arm.guard) continue;
                if (auto* pb = std::get_if<lir::PatBool>(&arm.pat.kind)) {
                    if (pb->value) has_true = true; else has_false = true;
                }
            }
            if (!has_true || !has_false)
                error("match on bool is not exhaustive — missing "
                      + std::string(!has_true ? "true" : "false"));
        }
    }

    if (has_hoist_let) {
        auto blk = std::make_unique<lir::LBlock>();
        blk->stmts.push_back(std::move(hoist_let_view));
        blk->stmts.push_back(std::move(hoist_let_root));
        blk->stmts.push_back(std::move(hoist_let_base));
        blk->stmts.push_back(make_stmt(node_line_, std::move(smatch)));
        return make_stmt(node_line_, lir::SBlock{std::move(blk)});
    }
    return make_stmt(node_line_, std::move(smatch));
}

lir::LExprPtr SemaChecker::lower_match_expr(TinyMapView node) {
    lir::LExprPtr scrut;
    TypeRef scrut_type = error_t();
    if (node.has_key(la::VALUE)) {
        scrut = lower_expr(map_of(node.get(la::VALUE.code)));
        scrut_type = scrut->type;
    } else { scrut = error_expr(); }

    // Hermes scalar pattern hoisting (symmetric to lower_match).
    auto is_hermes_pc = [](int32_t pc) {
        return pc == la::PAT_HERMES_NULL || pc == la::PAT_HERMES_BOOL ||
               pc == la::PAT_HERMES_INT  || pc == la::PAT_HERMES_STR  ||
               pc == la::PAT_HERMES_MAP  || pc == la::PAT_HERMES_ARR  ||
               pc == la::PAT_HERMES_TYPED_ARR || pc == la::PAT_HERMES_TYPED_MAP;
    };
    auto pat_has_hermes = [&](TinyMapView p) -> bool {
        if (is_hermes_pc(code_of(p))) return true;
        if (code_of(p) == la::PAT_OR && p.has_key(la::ITEMS)) {
            auto arr = arr_of(p.get(la::ITEMS.code));
            for (uint64_t i = 0; i < arr.size(); ++i)
                if (is_hermes_pc(code_of(map_of(arr.get(i))))) return true;
        }
        return false;
    };
    bool has_hermes_pat = false;
    if (node.has_key(la::ITEMS)) {
        auto arms = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < arms.size(); ++i) {
            auto arm = map_of(arms.get(i));
            if (code_of(arm) != la::MATCH_ARM) continue;
            if (!arm.has_key(la::LHS)) continue;
            if (pat_has_hermes(map_of(arm.get(la::LHS.code)))) {
                has_hermes_pat = true; break;
            }
        }
    }
    // Symmetric to lower_match: hoist view + root AnyVal + base ptr.
    std::string root_var;
    std::string base_var;
    lir::LStmt hoist_let_view;
    lir::LStmt hoist_let_root;
    lir::LStmt hoist_let_base;
    bool has_hoist_let = false;
    TypeRef anyval_t = nullptr;
    if (has_hermes_pat) {
        if (!hermes_view_inner(scrut_type)) {
            error(std::format(
                "match with Hermes patterns requires a view scrutinee "
                "(Hermes, HermesView, or HermesStatic; use & to borrow); "
                "got {}", type_str(scrut_type)));
        }
        std::string view_var = "__hmatche_view_" + std::to_string(tmp_var_count_++);
        {
            lir::SLet sl;
            sl.name = view_var; sl.type = scrut_type; sl.is_mut = false;
            sl.value = std::move(scrut);
            hoist_let_view = make_stmt(node_line_, std::move(sl));
        }
        anyval_t = make_datatype_type("AnyVal");
        root_var = "__hmatche_root_" + std::to_string(tmp_var_count_++);
        {
            auto view_ref = builder().var_ref(view_var, scrut_type);
            auto root_call = builder().method_call(std::move(view_ref), "root", "", {}, {}, -1, anyval_t);
            lir::SLet sl;
            sl.name = root_var; sl.type = anyval_t; sl.is_mut = false;
            sl.value = std::move(root_call);
            hoist_let_root = make_stmt(node_line_, std::move(sl));
        }
        base_var = "__hmatche_base_" + std::to_string(tmp_var_count_++);
        {
            TypeRef u8_ptr_t = make_ptr(false, prim(LogosType::Kind::U8));
            auto view_ref = builder().var_ref(view_var, scrut_type);
            auto base_call = builder().method_call(std::move(view_ref), "base", "", {}, {}, -1, u8_ptr_t);
            lir::SLet sl;
            sl.name = base_var; sl.type = u8_ptr_t; sl.is_mut = false;
            sl.value = std::move(base_call);
            hoist_let_base = make_stmt(node_line_, std::move(sl));
        }
        has_hoist_let = true;
        scrut = builder().var_ref(view_var, scrut_type);
    }

    lir::EMatchExpr me;
    me.scrut = std::move(scrut);
    TypeRef result_type = error_t();

    if (node.has_key(la::ITEMS)) {
        auto arms = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < arms.size(); ++i) {
            auto arm = map_of(arms.get(i));
            if (code_of(arm) != la::MATCH_ARM) continue;

            lir::LExprPtr synth_guard;
            std::vector<lir::LStmt> body_prologue;
            std::vector<HermesPatBinding> body_binds;
            if (has_hermes_pat && arm.has_key(la::LHS)) {
                std::vector<lir::LStmt> g_stmts;
                std::vector<HermesPatBinding> g_binds;
                auto raw = build_hermes_pat_guard(
                    map_of(arm.get(la::LHS.code)), root_var, anyval_t,
                    base_var, g_stmts, g_binds);
                if (!g_stmts.empty() && raw) {
                    auto blk = std::make_unique<lir::LBlock>();
                    blk->stmts = std::move(g_stmts);
                    synth_guard = builder().block_expr(std::move(blk), std::move(raw), bool_t());
                } else {
                    synth_guard = std::move(raw);
                }
                if (!g_binds.empty()) {
                    (void)build_hermes_pat_guard(
                        map_of(arm.get(la::LHS.code)), root_var, anyval_t,
                        base_var, body_prologue, body_binds);
                }
            }

            in_match_hermes_ctx_ = has_hermes_pat;
            lir::Pattern pat = arm.has_key(la::LHS)
                ? build_pattern(map_of(arm.get(la::LHS.code)), scrut_type)
                : lir::PatWild{"_"};
            in_match_hermes_ctx_ = false;

            push_scope();
            bind_pattern(pat, scrut_type);
            for (const auto& b : body_binds) {
                define(b.name, anyval_t, /*is_mut=*/false);
            }

            std::optional<lir::LExprPtr> guard;
            if (arm.has_key(la::GUARD)) {
                auto g = lower_expr(map_of(arm.get(la::GUARD.code)));
                if (TypeRef(g->type).kind() != LogosType::Kind::Bool &&
                    TypeRef(g->type).kind() != LogosType::Kind::Error)
                    error("match guard must be bool");
                guard = std::move(g);
            }
            if (synth_guard) {
                if (guard) {
                    // synth first: short-circuits user guard on pattern miss
                    auto merged = builder().bin_op("&&", std::move(synth_guard), std::move(*guard), bool_t());
                    guard = std::move(merged);
                } else {
                    guard = std::move(synth_guard);
                }
            }

            // Lower the arm value: either an EXPR arm (pattern => expr,) or a
            // BODY block arm (pattern => { stmts }).  Block arms that always
            // diverge (every path returns) contribute error_t so they are
            // skipped during type unification; non-diverging block arms use
            // their last expression as the value.
            lir::LExprPtr val;
            if (arm.has_key(la::EXPR)) {
                val = lower_expr(map_of(arm.get(la::EXPR.code)));
            } else if (arm.has_key(la::BODY)) {
                auto body_node = map_of(arm.get(la::BODY.code));
                bool diverges = (code_of(body_node) == la::BLOCK)
                                ? block_always_diverts(body_node)
                                : stmt_always_diverts(body_node);
                if (diverges) {
                    // Block always returns — lower it as a block of stmts;
                    // the tail expression is unreachable so we use error_expr()
                    // (skipped by type unification).
                    lir::LBlock blk;
                    if (code_of(body_node) == la::BLOCK)
                        blk = lower_block(body_node);
                    else
                        blk.stmts.push_back(lower_stmt(body_node));
                    auto blk_ptr = std::make_unique<lir::LBlock>(std::move(blk));
                    val = builder().block_expr(std::move(blk_ptr), error_expr(), error_t());
                } else if (code_of(body_node) == la::BLOCK &&
                           body_node.has_key(la::ITEMS)) {
                    // Non-diverging block: last item must be an expression.
                    auto stmts = arr_of(body_node.get(la::ITEMS.code));
                    auto blk = std::make_unique<lir::LBlock>();
                    lir::LExprPtr last_expr;
                    for (uint64_t si = 0; si < stmts.size(); ++si) {
                        auto s = map_of(stmts.get(si));
                        if (si == stmts.size() - 1) {
                            int32_t sc = code_of(s);
                            if (sc == la::EXPR_STMT && s.has_key(la::VALUE))
                                last_expr = lower_expr(map_of(s.get(la::VALUE.code)));
                            else if (sc != la::EXPR_STMT && sc != la::LET &&
                                     sc != la::LET_DESTRUCT && sc != la::RETURN)
                                last_expr = lower_expr(s);
                            else
                                blk->stmts.push_back(lower_stmt(s));
                        } else {
                            blk->stmts.push_back(lower_stmt(s));
                        }
                    }
                    if (!last_expr) {
                        error("match expression: block arm must end with an expression or always return");
                        last_expr = error_expr();
                    }
                    TypeRef vt = last_expr->type;
                    val = builder().block_expr(std::move(blk), std::move(last_expr), vt);
                } else {
                    error("match expression: block arm must end with an expression or always return");
                    val = error_expr();
                }
            } else {
                error("match expression: arm has no body");
                val = error_expr();
            }
            // Wrap arm value with Hermes @-pattern prologue so bindings are
            // live during evaluation.
            if (!body_prologue.empty() || !body_binds.empty()) {
                std::vector<lir::LStmt> prologue = std::move(body_prologue);
                for (const auto& b : body_binds) {
                    lir::SLet sl;
                    sl.name = b.name; sl.type = anyval_t; sl.is_mut = false;
                    sl.value = builder().var_ref(b.av_var, anyval_t);
                    prologue.push_back(make_stmt(node_line_, std::move(sl)));
                }
                auto blk = std::make_unique<lir::LBlock>();
                blk->stmts = std::move(prologue);
                TypeRef vt = val->type;
                val = builder().block_expr(std::move(blk), std::move(val), vt);
            }
            if (TypeRef(result_type).kind() == LogosType::Kind::Error) {
                result_type = val->type;
            } else if (TypeRef(val->type).kind() != LogosType::Kind::Error) {
                if (!types_compatible(val->type, result_type) &&
                    !types_compatible(result_type, val->type)) {
                    error(std::format(
                        "match expression: arm type '{}' is incompatible with '{}'",
                        type_str(val->type), type_str(result_type)));
                } else {
                    result_type = unify_numeric(result_type, val->type);
                }
            }
            // Upgrade IntLit result to i64 if any arm literal overflows i32.
            if (TypeRef(result_type).kind() == LogosType::Kind::IntLit) {
                const lir::LExpr* ve = val.get();
                if (auto* blk = std::get_if<lir::EBlockExpr>(&ve->kind))
                    ve = blk->result.get();
                if (ve) {
                    if (auto* lit = std::get_if<lir::ELitInt>(&ve->kind))
                        if (lit->value > (int64_t)INT32_MAX || lit->value < (int64_t)INT32_MIN)
                            result_type = prim(LogosType::Kind::I64);
                }
            }

            pop_scope();
            lir::EMatchArm ema;
            ema.pat   = std::move(pat);
            ema.guard = std::move(guard);
            ema.value = std::move(val);
            me.arms.push_back(std::move(ema));
        }
    }

    {
        bool has_wild = false;
        for (auto& arm : me.arms) {
            if (!arm.guard && pat_ref_of(arm.pat).kind() == lir_schema::pat::Code::Wild) {
                has_wild = true;
                break;
            }
        }
        if (!has_wild && TypeRef(scrut_type).kind() == LogosType::Kind::Enum) {
            auto [epkg_match2, esi_match2] = find_enum_by_name(TypeRef(scrut_type).enum_name());
            auto eit = esi_match2 ? enums_.find(sema_key(epkg_match2, TypeRef(scrut_type).enum_name())) : enums_.end();
            if (eit == enums_.end()) eit = enums_.find(TypeRef(scrut_type).enum_name());
            if (eit != enums_.end()) {
                std::set<int32_t> covered;
                auto add_pat2 = [&](const lir::Pattern& p) {
                    if (auto* pv = std::get_if<lir::PatVariant>(&p.kind))
                        covered.insert(pv->disc);
                    else if (auto* pvd = std::get_if<lir::PatVariantData>(&p.kind))
                        covered.insert(pvd->disc);
                };
                for (auto& arm : me.arms) {
                    if (arm.guard) continue;
                    if (auto* por = std::get_if<lir::PatOr>(&arm.pat.kind)) {
                        for (auto& alt : por->alts) add_pat2(alt);
                    } else {
                        add_pat2(arm.pat);
                    }
                }
                std::string missing;
                for (auto& v : eit->second.variants) {
                    if (covered.find(v.value) == covered.end()) {
                        if (!missing.empty()) missing += ", ";
                        missing += std::string(v.name);
                    }
                }
                if (!missing.empty())
                    error(std::format("match is not exhaustive — missing variant(s): {}",
                          missing));
            }
        }
        if (!has_wild && TypeRef(scrut_type).kind() == LogosType::Kind::Bool) {
            bool has_true = false, has_false = false;
            for (auto& arm : me.arms) {
                if (arm.guard) continue;
                if (auto* pb = std::get_if<lir::PatBool>(&arm.pat.kind)) {
                    if (pb->value) has_true = true; else has_false = true;
                }
            }
            if (!has_true || !has_false)
                error("match on bool is not exhaustive — missing "
                      + std::string(!has_true ? "true" : "false"));
        }
    }

    auto me_expr = builder().match_expr_v(std::move(me), result_type);
    if (has_hoist_let) {
        auto blk = std::make_unique<lir::LBlock>();
        blk->stmts.push_back(std::move(hoist_let_view));
        blk->stmts.push_back(std::move(hoist_let_root));
        blk->stmts.push_back(std::move(hoist_let_base));
        return builder().block_expr(std::move(blk), std::move(me_expr), result_type);
    }
    return me_expr;
}


// ---------------------------------------------------------------------------
// lower_deref_field_compound_assign — (*ptr).field op= expr
// ---------------------------------------------------------------------------
lir::LStmt SemaChecker::lower_deref_field_compound_assign(TinyMapView node) {
    auto recv_name  = str_of(node.get(la::RECEIVER.code));
    auto field_name = str_of(node.get(la::FIELD.code));
    auto op_tok     = str_of(node.get(la::OP.code));
    std::string base_op;
    if (op_tok.size() >= 2 && op_tok.back() == '=')
        base_op = std::string(op_tok.substr(0, op_tok.size() - 1));
    else
        base_op = std::string(op_tok);

    TypeRef ptr_type = lookup(recv_name);
    if (!ptr_type) {
        error(std::format("deref-field compound assign: undefined variable '{}'", recv_name));
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return make_stmt(node_line_, lir::SBreak{});
    }
    if (!is_ref_like(TypeRef(ptr_type).kind()) || !TypeRef(ptr_type).pointee()) {
        error(std::format("deref-field compound assign: '{}' is not a pointer (got {})",
                          recv_name, type_str(ptr_type)));
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return make_stmt(node_line_, lir::SBreak{});
    }
    if (TypeRef(ptr_type).kind() == LogosType::Kind::Ptr && !TypeRef(ptr_type).mut_ptr())
        error(std::format("deref-field compound assign: '{}' is *const (need *mut)", recv_name));
    if (TypeRef(ptr_type).kind() == LogosType::Kind::Ptr && !inside_unsafe_)
        error("compound assign through raw pointer field requires unsafe context");
    if (TypeRef(ptr_type).kind() == LogosType::Kind::Ref)
        error(std::format("deref-field compound assign: '{}' is &T (need &mut T)", recv_name));

    TypeRef pointee = TypeRef(ptr_type).pointee();
    std::string type_name;
    TypeRef ft = nullptr;
    if (TypeRef(pointee).kind() == LogosType::Kind::Struct) {
        type_name = concrete_struct_name(pointee);
        ft = field_type_of_for_type(pointee, field_name);
    }
    if (!ft) {
        error(std::format("deref-field compound assign: '{}' has no field '{}'", type_name, field_name));
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return make_stmt(node_line_, lir::SBreak{});
    }

    // lhs_read = (*ptr).field
    auto ptr_ref  = builder().var_ref(std::string(recv_name), ptr_type);
    auto lhs_read = builder().field_read(std::move(ptr_ref), std::string(field_name), ft);
    auto rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();

    // Type-check: RHS must be compatible with the field's type.
    if (TypeRef(ft).kind() != LogosType::Kind::Error &&
        TypeRef(rhs->type).kind() != LogosType::Kind::Error &&
        !types_compatible(rhs->type, ft)) {
        error(std::format("compound assignment to '(*{}).{}': type mismatch — expected {}, got {}",
              recv_name, field_name, type_str(ft), type_str(rhs->type)));
    }
    auto combined = builder().bin_op(base_op, std::move(lhs_read), std::move(rhs), ft);

    lir::SDerefFieldWrite sdfw;
    sdfw.receiver  = std::string(recv_name);
    sdfw.type_name = type_name;
    sdfw.field     = std::string(field_name);
    sdfw.value     = std::move(combined);
    return make_stmt(node_line_, std::move(sdfw));
}

// ---------------------------------------------------------------------------
// lower_tuple_field_compound_assign — var.N op= expr
// ---------------------------------------------------------------------------
lir::LStmt SemaChecker::lower_tuple_field_compound_assign(TinyMapView node) {
    auto recv_name = str_of(node.get(la::RECEIVER.code));
    auto idx_sv    = str_of(node.get(la::INDEX.code));
    uint64_t idx   = (uint64_t)parse_int_literal(idx_sv);
    auto op_tok    = str_of(node.get(la::OP.code));
    std::string base_op;
    if (op_tok.size() >= 2 && op_tok.back() == '=')
        base_op = std::string(op_tok.substr(0, op_tok.size() - 1));
    else
        base_op = std::string(op_tok);

    TypeRef recv_t = lookup(recv_name);
    if (!recv_t) {
        error(std::format("tuple field compound assign: undefined variable '{}'", recv_name));
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return make_stmt(node_line_, lir::SBreak{});
    }
    if (TypeRef(recv_t).kind() == LogosType::Kind::MutRef && TypeRef(recv_t).pointee())
        recv_t = TypeRef(recv_t).pointee();
    if (TypeRef(recv_t).kind() != LogosType::Kind::Tuple) {
        error(std::format("tuple field compound assign: '{}' is not a tuple (got {})",
                          recv_name, type_str(recv_t)));
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return make_stmt(node_line_, lir::SBreak{});
    }
    if (idx >= TypeRef(recv_t).tuple_elems().size()) {
        error(std::format("tuple field compound assign: index {} out of range (tuple has {} elements)",
                          idx, TypeRef(recv_t).tuple_elems().size()));
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return make_stmt(node_line_, lir::SBreak{});
    }
    TypeRef orig_recv_t = lookup(recv_name);
    if (!lookup_is_mut(recv_name) &&
        !(orig_recv_t && TypeRef(orig_recv_t).kind() == LogosType::Kind::MutRef))
        error(std::format("tuple field compound assign to immutable variable '{}'", recv_name));

    TypeRef ft = TypeRef(recv_t).tuple_elems()[idx];
    auto recv_ref = builder().var_ref(std::string(recv_name), orig_recv_t ? orig_recv_t : recv_t);
    auto lhs_read = builder().tuple_index(std::move(recv_ref), (uint32_t)idx, ft);
    auto rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();

    // Type-check: RHS must be compatible with the tuple element's type.
    if (TypeRef(ft).kind() != LogosType::Kind::Error &&
        TypeRef(rhs->type).kind() != LogosType::Kind::Error &&
        !types_compatible(rhs->type, ft)) {
        error(std::format("compound assignment to '{}.{}': type mismatch — expected {}, got {}",
              recv_name, idx, type_str(ft), type_str(rhs->type)));
    }
    auto combined = builder().bin_op(base_op, std::move(lhs_read), std::move(rhs), ft);

    return make_stmt(node_line_, lir::STupleWrite{std::string(recv_name), (uint32_t)idx,
                                                   std::move(combined), recv_t});
}

// ---------------------------------------------------------------------------
// lower_field_index_compound_assign — s.field[i] op= expr
// ---------------------------------------------------------------------------
lir::LStmt SemaChecker::lower_field_index_compound_assign(TinyMapView node) {
    auto recv_name  = str_of(node.get(la::RECEIVER.code));
    auto field_name = str_of(node.get(la::FIELD.code));
    auto op_tok     = str_of(node.get(la::OP.code));
    std::string base_op;
    if (op_tok.size() >= 2 && op_tok.back() == '=')
        base_op = std::string(op_tok.substr(0, op_tok.size() - 1));
    else
        base_op = std::string(op_tok);

    auto recv_t = lookup(recv_name);
    if (!recv_t) error(std::format("field index compound assign: undefined variable '{}'", recv_name));

    TypeRef base_t = recv_t;
    if (base_t && is_ref_like(TypeRef(base_t).kind())) base_t = TypeRef(base_t).pointee();

    TypeRef field_t = nullptr;
    if (base_t) {
        auto sname = struct_name_from_type(base_t);
        if (!sname.empty()) field_t = field_type_of_for_type(base_t, field_name);
    }
    if (!field_t) {
        error(std::format("field index compound assign: cannot resolve field '{}.{}'",
                          recv_name, field_name));
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return make_stmt(node_line_, lir::SBreak{});
    }
    if (TypeRef(field_t).kind() != LogosType::Kind::Array &&
        TypeRef(field_t).kind() != LogosType::Kind::Ptr &&
        TypeRef(field_t).kind() != LogosType::Kind::MutRef)
        error(std::format("field index compound assign: '{}.{}' is not an array or pointer (got {})",
              recv_name, field_name, type_str(field_t)));

    TypeRef elem_t = nullptr;
    if (TypeRef(field_t).kind() == LogosType::Kind::Array)
        elem_t = TypeRef(field_t).elem();
    else if (TypeRef(field_t).kind() == LogosType::Kind::Ptr ||
             TypeRef(field_t).kind() == LogosType::Kind::MutRef)
        elem_t = TypeRef(field_t).pointee();
    if (!elem_t) elem_t = error_t();

    // Lower index twice (pure expression — no side effects assumed)
    lir::LExprPtr idx_for_write = node.has_key(la::LHS)
        ? lower_expr(map_of(node.get(la::LHS.code))) : error_expr();
    lir::LExprPtr idx_for_read  = node.has_key(la::LHS)
        ? lower_expr(map_of(node.get(la::LHS.code))) : error_expr();

    auto rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();

    // Type-check: RHS must be compatible with the element type.
    if (TypeRef(elem_t).kind() != LogosType::Kind::Error &&
        TypeRef(rhs->type).kind() != LogosType::Kind::Error &&
        !types_compatible(rhs->type, elem_t)) {
        error(std::format("compound assignment to '{}.{}[i]': type mismatch — expected {}, got {}",
              recv_name, field_name, type_str(elem_t), type_str(rhs->type)));
    }
    // Build lhs read: s.field[idx_for_read]
    auto recv_ref  = builder().var_ref(std::string(recv_name), recv_t ? recv_t : error_t());
    auto field_rd  = builder().field_read(std::move(recv_ref), std::string(field_name), field_t);
    auto lhs_read  = builder().index_read(std::move(field_rd), std::move(idx_for_read), elem_t);
    auto combined  = builder().bin_op(base_op, std::move(lhs_read), std::move(rhs), elem_t);

    lir::SFieldIndexWrite sfiw;
    sfiw.receiver = std::string(recv_name);
    sfiw.field    = std::string(field_name);
    sfiw.index    = std::move(idx_for_write);
    sfiw.value    = std::move(combined);
    return make_stmt(node_line_, std::move(sfiw));
}

} // namespace logos::compiler
