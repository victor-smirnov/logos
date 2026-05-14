// Logos project — https://github.com/victor-smirnov/logos

#include "sema_impl.hpp"
#include "ctfe.hpp"
#include "logos/compiler/subtype.hpp"

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
    // K10-co-04: a call to `panic(...)` is divergent — control never falls
    // through. Hand-recognised by callee name today since Logos has no
    // `!`/Never type kind. Returning true from stmt_always_returns makes
    // the fn-body return-reachability check accept a `panic(msg)` tail.
    auto is_divergent_call = [&](hermes::TinyMapView node) -> bool {
        int32_t cc = code_of(node);
        // Direct call `panic(...)` or macro-style `panic!(...)`. The macro
        // shape parses to FN_MACRO_CALL with CALLEE = "panic" before
        // expansion; reachability runs on the un-expanded AST so it sees
        // both forms by the same callee name.
        if (cc == la::CALL.code || cc == la::FN_MACRO_CALL.code) {
            auto callee = str_of(node.get(la::CALLEE.code));
            return callee == "panic";
        }
        return false;
    };
    if ((c == la::EXPR_STMT || c == la::TAIL_EXPR) && stmt.has_key(la::VALUE)) {
        auto e = map_of(stmt.get(la::VALUE.code));
        if (is_divergent_call(e)) return true;
    }
    // B-fn-06: TAIL_EXPR (no-SEMI trailing expression) is treated as an
    // implicit return ONLY at fn-body context, signalled by tail_as_return_.
    // In match-arm-body / unsafe-block-as-expr contexts the same node is the
    // block-expression value, NOT a return, so the flag is cleared there.
    if (c == la::TAIL_EXPR) return tail_as_return_;
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
    if (c == la::LET_PAT)      return lower_let_pat(stmt);
    if (c == la::NESTED_FN)    return lower_nested_fn(stmt);
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
        return builder().stmt_expr(std::move(e), node_line_);
    }
    if (c == la::TAIL_EXPR) {
        // B-fn-06: trailing expression (no SEMI) at stmt position. Only
        // synthesise an implicit `return expr` when we're at fn-body level
        // (tail_as_return_) AND the ret type is non-void. In other contexts
        // (block-as-expression, void fn) lower as an expression-stmt.
        //
        // Additional guard (closes the `if cond { ...; () } else { () }`
        // followed by `return X;` mis-typing): when the inner expression
        // is unit-typed, treat it as a plain stmt regardless of
        // tail_as_return_. A unit-valued tail can never be a useful
        // implicit return for a non-unit fn, and the parser sometimes
        // wraps an `if_expr`-as-stmt in a TAIL_EXPR when the stmt-level
        // if_expr alt fails to commit (the expression-level if_expr alt
        // is greedier than the stmt-level one). Without this guard, the
        // void-typed if would trip lower_return's mismatch check.
        // Closure body in inference mode (ret_type_ deliberately nullptr by
        // lower_closure_expr): a non-void tail expression is the implicit
        // return. Wrap as stmt_return so the closure-body return scanner
        // picks it up; no compat check (the closure has no declared type).
        if (tail_as_return_ && !ret_type_ && stmt.has_key(la::VALUE)) {
            auto inner = lower_expr(map_of(stmt.get(la::VALUE.code)));
            if (inner && inner->type &&
                TypeRef(inner->type).kind() != LogosType::Kind::Void &&
                TypeRef(inner->type).kind() != LogosType::Kind::Error) {
                return builder().stmt_return(std::move(inner), node_line_);
            }
            return builder().stmt_expr(std::move(inner), node_line_);
        }
        if (tail_as_return_ && ret_type_ &&
            TypeRef(ret_type_).kind() != LogosType::Kind::Void) {
            // Peek the inner expression's type before deciding.
            if (stmt.has_key(la::VALUE)) {
                auto inner = lower_expr(map_of(stmt.get(la::VALUE.code)));
                if (inner && inner->type &&
                    TypeRef(inner->type).kind() == LogosType::Kind::Void) {
                    return builder().stmt_expr(std::move(inner), node_line_);
                }
                // Non-void: wrap as implicit return, mirroring the
                // existing lower_return body but with the already-lowered
                // value (avoids re-lowering).
                if (inner && ret_type_ &&
                    TypeRef(ret_type_).kind() != LogosType::Kind::Error &&
                    TypeRef(inner->type).kind() != LogosType::Kind::Error &&
                    !compat(inner->type, ret_type_)) {
                    auto [es, gs] = type_str_pair(ret_type_, inner->type);
                    error(std::format("return type mismatch — expected {}, got {}",
                          es, gs));
                }
                return builder().stmt_return(std::move(inner), node_line_);
            }
            return lower_return(stmt);
        }
        lir::LExprPtr e = stmt.has_key(la::VALUE)
            ? lower_expr(map_of(stmt.get(la::VALUE.code)))
            : error_expr();
        return builder().stmt_expr(std::move(e), node_line_);
    }
    if (c == la::BREAK) {
        if (loop_depth_ == 0) error("'break' outside loop");
        lir::LExprPtr bval = nullptr;
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
        if (!break_label.empty() &&
            std::find(active_loop_labels_.begin(), active_loop_labels_.end(),
                      break_label) == active_loop_labels_.end()) {
            error(std::format("'break {}': label not in scope", break_label));
        }
        return builder().stmt_break(std::move(bval), std::move(break_label), node_line_);
    }
    if (c == la::CONTINUE) {
        if (loop_depth_ == 0) error("'continue' outside loop");
        std::string cont_label;
        if (stmt.has_key(la::LABEL))
            cont_label = std::string(str_of(stmt.get(la::LABEL.code)));
        if (!cont_label.empty() &&
            std::find(active_loop_labels_.begin(), active_loop_labels_.end(),
                      cont_label) == active_loop_labels_.end()) {
            error(std::format("'continue {}': label not in scope", cont_label));
        }
        return builder().stmt_continue(std::move(cont_label), node_line_);
    }
    if (c == la::DEREF_COMPOUND) {
        // B-st-04: `*p op= v` — desugar to `*p = *p op v`.
        if (!stmt.has_key(la::NAME) || !stmt.has_key(la::VALUE)) {
            error("deref-compound: missing operand");
            return builder().stmt_expr(error_expr(), node_line_);
        }
        auto ptr   = lower_expr(map_of(stmt.get(la::NAME.code)));
        auto rhs   = lower_expr(map_of(stmt.get(la::VALUE.code)));
        auto op_tok = str_of(stmt.get(la::OP.code));
        std::string base_op = (op_tok.size() >= 2 && op_tok.back() == '=')
            ? std::string(op_tok.substr(0, op_tok.size() - 1))
            : std::string(op_tok);
        TypeRef pt = ptr->type;
        TypeRef elem = TypeRef(pt).pointee();
        if (!elem || (TypeRef(pt).kind() != LogosType::Kind::Ptr &&
                      TypeRef(pt).kind() != LogosType::Kind::MutRef)) {
            error("deref-compound: left side must be a pointer or mutable reference");
            return builder().stmt_expr(error_expr(), node_line_);
        }
        bool is_mut_ref = TypeRef(pt).kind() == LogosType::Kind::MutRef;
        if (!is_mut_ref && !inside_unsafe_)
            error("write through raw pointer requires unsafe context");
        if (TypeRef(pt).kind() == LogosType::Kind::Ptr && !TypeRef(pt).mut_ptr())
            error("deref-compound: cannot write through *const pointer (use *mut)");
        // Build *p (read) op rhs.  Need to read ptr twice — clone the var-ref
        // by re-lowering.
        auto ptr_again = lower_expr(map_of(stmt.get(la::NAME.code)));
        auto cur_val   = builder().deref(std::move(ptr_again), elem);
        auto binop     = builder().bin_op(base_op, std::move(cur_val), std::move(rhs), elem);
        return builder().stmt_deref_write(std::move(ptr), std::move(binop), node_line_);
    }
    if (c == la::DEREF_WRITE) {
        // *ptr = value;
        lir::LExprPtr ptr = stmt.has_key(la::NAME)
            ? lower_expr(map_of(stmt.get(la::NAME.code)))
            : error_expr();
        // SL-sl-03: propagate `*p = Option::None`-style RHS hints from the
        // pointee type, so a bare `None` resolves to `Option<i32>` rather
        // than dropping to a discriminant-only constant (which then gets
        // stored straight into the &mut slot — corrupting it).
        auto saved_enum_hint   = hint_enum_type_;
        auto saved_struct_hint = hint_struct_type_;
        if (ptr && TypeRef(ptr->type).pointee()) {
            TypeRef pe = TypeRef(ptr->type).pointee();
            if (TypeRef(pe).kind() == LogosType::Kind::Enum &&
                !TypeRef(pe).type_args().empty())
                hint_enum_type_ = pe;
            else if ((TypeRef(pe).kind() == LogosType::Kind::Struct ||
                      TypeRef(pe).kind() == LogosType::Kind::ZonedStruct) &&
                     !TypeRef(pe).type_args().empty())
                hint_struct_type_ = pe;
        }
        lir::LExprPtr val = stmt.has_key(la::VALUE)
            ? lower_expr(map_of(stmt.get(la::VALUE.code)))
            : error_expr();
        hint_enum_type_   = saved_enum_hint;
        hint_struct_type_ = saved_struct_hint;
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
        // B68: variance check at *ptr = val. The pointee must Inv-match the
        // value's type. Strict mode (fn-scope-fixed lifetimes).
        if (val && TypeRef(pt).pointee())
            check_variance(val->type, TypeRef(pt).pointee(),
                           "deref-write '*ptr = …'", /*permissive=*/false);
        track_write_move(val);
        return builder().stmt_deref_write(std::move(ptr), std::move(val), node_line_);
    }
    if (c == la::UNSAFE_BLOCK) {
        bool was = inside_unsafe_;
        inside_unsafe_ = true;
        auto inner = stmt.has_key(la::BODY)
            ? lower_block(map_of(stmt.get(la::BODY.code)))
            : lir::LBlock{};
        inside_unsafe_ = was;
        return make_stmt_emit(node_line_, lir::SBlock{lir::alloc_block(*cur_prog_, std::move(inner))});
    }
    if (c == la::BLOCK_STMT) {
        auto inner = stmt.has_key(la::BODY)
            ? lower_block(map_of(stmt.get(la::BODY.code)))
            : lir::LBlock{};
        return make_stmt_emit(node_line_, lir::SBlock{lir::alloc_block(*cur_prog_, std::move(inner))});
    }
    // Unknown stmt — emit dummy expr stmt
    return builder().stmt_expr(error_expr(), node_line_);
}

lir::LBlock SemaChecker::lower_block(TinyMapView block) {
    lir::LBlock result;
    push_scope();
    bool warned_dead = false;  // Sprint 5.2: B-st-08 dead-code-after-terminator lint
    if (block.has_key(la::ITEMS)) {
        auto stmts = arr_of(block.get(la::ITEMS.code));
        for (uint64_t i = 0; i < stmts.size(); ++i) {
            auto s = map_of(stmts.get(i));
            if (s.is_null()) continue;
            // Skip already-lowered drops/markers; the AST-level dead-code check
            // looks at the pre-lowering AST shape.
            if (!warned_dead) {
                int32_t pc = code_of(s);
                // Diagnose only when the previous lowered stmt was a hard
                // terminator (Return/Break/Continue) — Panic doesn't have a
                // dedicated AST code; skip those.
                if (i > 0 && !result.stmts.empty()) {
                    auto prev_ref = stmt_ref_of(result.stmts.back());
                    if (prev_ref) {
                        auto pk = prev_ref.kind();
                        if ((pk == lir_schema::stmt::Code::Return ||
                             pk == lir_schema::stmt::Code::Break  ||
                             pk == lir_schema::stmt::Code::Continue)
                            && pc != la::ANNOTATION) {
                            warn("unreachable code after terminator");
                            warned_dead = true;
                        }
                    }
                }
            }
            auto lowered = lower_stmt(s);
            // Insert drops before return/break/continue.
            // Return's value expression MUST be evaluated BEFORE the drops
            // (it may borrow variables that the drops would release).
            // Hoist the return value into a temporary, then drop, then return
            // the temporary.
            if (auto sref = stmt_ref_of(lowered);
                sref && sref.kind() == lir_schema::stmt::Code::Return) {
                auto drops = collect_all_drops();
                auto val_ref = lir_view::SReturnView{sref}.value();
                if (!drops.empty() && val_ref) {
                    TypeRef rt = val_ref.type(cur_prog_->type_pool.impl());
                    std::string tmp = "__ret_tmp_" +
                        std::to_string(tmp_var_count_++);
                    lir::SLet sl;
                    sl.name = tmp; sl.type = rt; sl.is_mut = false;
                    sl.value = lexpr_of(val_ref);
                    result.stmts.push_back(
                        make_stmt_emit(node_line_, std::move(sl)));
                    for (auto& d : drops)
                        result.stmts.push_back(std::move(d));
                    result.stmts.push_back(
                        builder().stmt_return(builder().var_ref(tmp, rt), node_line_));
                    continue;
                }
                for (auto& d : drops)
                    result.stmts.push_back(std::move(d));
            } else {
                auto term_ref = stmt_ref_of(lowered);
                if (term_ref && (term_ref.kind() == lir_schema::stmt::Code::Break ||
                                 term_ref.kind() == lir_schema::stmt::Code::Continue)) {
                    for (auto& d : collect_drops())
                        result.stmts.push_back(std::move(d));
                }
            }
            result.stmts.push_back(std::move(lowered));
        }
    }
    // Insert drops for normal block exit (no return/break/continue)
    bool ends_with_terminator = false;
    if (!result.stmts.empty()) {
        auto br = stmt_ref_of(result.stmts.back());
        if (br) {
            auto k = br.kind();
            ends_with_terminator = (k == lir_schema::stmt::Code::Return ||
                                    k == lir_schema::stmt::Code::Break ||
                                    k == lir_schema::stmt::Code::Continue);
        }
    }
    if (!ends_with_terminator) {
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
        return builder().stmt_expr(std::move(rhs), node_line_);
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
    // Tuple-pattern binding-name uniqueness (closes B-pt-01)
    check_unique_names(names,
                       [](auto& n) -> std::string_view { return n; },
                       "binding", "let (...) destructure");

    // Build SBlock: let __destruct_N = rhs; let a = __destruct_N.0; ...
    std::string tmp = std::format("__destruct_{}", destruct_counter_++);

    auto blk = lir::alloc_block(*cur_prog_);

    // let __destruct_N = rhs
    define(tmp, rhs_type);
    lir::SLet tmp_let;
    tmp_let.name    = tmp;
    tmp_let.type    = rhs_type;
    tmp_let.is_mut  = false;
    tmp_let.value   = std::move(rhs);
    blk->stmts.push_back(make_stmt_emit(node_line_, std::move(tmp_let)));

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
        blk->stmts.push_back(make_stmt_emit(node_line_, std::move(elem_let)));
    }

    lir::SBlock sb;
    sb.body = std::move(blk);
    return make_stmt_emit(node_line_, std::move(sb));
}

// Sprint 4.2 — B-pt-02: irrefutable struct destructure in `let`.
//   let Foo { x, y } = expr;          →  let __dst = expr; let x = __dst.x; let y = __dst.y;
//   let Foo { x: a, y: b } = expr;    same with rebinding
// Other pattern shapes (variant, tuple-via-pat_single, slice, …) are
// rejected with a clear diagnostic — they're refutable or need full
// match lowering, which we layer on top of this basic destructure path
// in a later sprint.
lir::LStmt SemaChecker::lower_let_pat(TinyMapView node) {
    lir::LExprPtr rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    TypeRef rhs_type = rhs->type;
    if (!node.has_key(la::PAT)) {
        error("internal: LET_PAT missing PAT");
        return builder().stmt_expr(std::move(rhs), node_line_);
    }
    auto pat_av = node.get(la::PAT.code);
    if (pat_av.is_null() || !pat_av.is_pointer()) {
        error("internal: LET_PAT PAT not a node");
        return builder().stmt_expr(std::move(rhs), node_line_);
    }
    auto pat_node = map_of(pat_av);
    int32_t pc = code_of(pat_node);
    // B-ts-01: `let Foo(a, b) = …` over a tuple-struct lowers via the
    // PAT_STRUCT path with synth field names "0", "1", …. Rewrite the
    // PAT_VARIANT_DATA pat-node's ARGS into an inline pat-field-list
    // here would require allocating in the prog arena; instead, dispatch
    // to a parallel block below.
    bool is_tuple_struct_pat = false;
    const SemaStructInfo* tsi_let = nullptr;
    // P4-pm-01: `let E::V { f } = e;` over a single-variant enum (irrefutable
    // by construction). Captures the variant/enum info for the dedicated
    // lowering below.
    bool is_single_variant_struct_pat = false;
    const SemaEnumInfo* sve_einfo = nullptr;
    const SemaVariantInfo* sve_vinfo = nullptr;
    std::string sve_ename;
    std::string sve_vname;
    if (pc == la::PAT_VARIANT_DATA) {
        auto pename_l = std::string(str_of(pat_node.get(la::NAME.code)));
        auto pvname_l = std::string(str_of(pat_node.get(la::FIELD.code)));
        if (pvname_l.empty()) {
            auto [_pkg, _si] = find_struct_by_name(pename_l);
            if (_si && _si->is_tuple_struct) {
                is_tuple_struct_pat = true;
                tsi_let = _si;
            }
        } else {
            bool pat_is_struct_shape =
                pat_node.has_key(la::variant::IS_STRUCT_SHAPE) &&
                pat_node.get(la::variant::IS_STRUCT_SHAPE.code).as_value<int32_t>() != 0;
            if (pat_is_struct_shape) {
                auto [_epkg, _einfo] = find_enum_by_name(pename_l);
                if (_einfo && _einfo->variants.size() == 1) {
                    for (auto& v : _einfo->variants)
                        if (v.name == pvname_l) { sve_vinfo = &v; break; }
                    if (sve_vinfo && sve_vinfo->is_struct_shape) {
                        is_single_variant_struct_pat = true;
                        sve_einfo  = _einfo;
                        sve_ename  = pename_l;
                        sve_vname  = pvname_l;
                    }
                }
            }
        }
    }
    // P4-pm-15: `let [a, b, c] = arr;` array destructure. Treated as
    // irrefutable when scrut is a fixed-size array whose length
    // matches the pattern's element count, and the pattern has no
    // `..` rest (rest-form is refutable for slices and isn't useful
    // for fixed arrays since shape is known). Lowered as a temp +
    // per-index field-read sequence (parallel to the tuple-struct
    // destructure path above).
    bool is_array_slice_pat =
        pc == la::PAT_SLICE &&
        TypeRef(rhs_type).kind() == LogosType::Kind::Array &&
        TypeRef(rhs_type).elem();
    if (pc != la::PAT_STRUCT && !is_tuple_struct_pat && !is_array_slice_pat &&
        !is_single_variant_struct_pat) {
        error("'let <pattern> = expr;' currently supports struct patterns only "
              "(other shapes are refutable; use 'match' or 'let-else')");
        return builder().stmt_expr(std::move(rhs), node_line_);
    }
    if (is_single_variant_struct_pat) {
        // P4-pm-01: `let E::V { f1, f2 } = rhs;` for a single-variant enum.
        // Lower as one synthetic match per user binding, returning that
        // field's value via match-as-expression:
        //
        //   let __dst = rhs;
        //   let <bind_1> = match __dst { E::V { <fname_1>: __syn, .. } => __syn };
        //   let <bind_2> = match __dst { E::V { <fname_2>: __syn, .. } => __syn };
        //   …
        //
        // Match-as-expression with a single irrefutable arm avoids needing
        // outer-scope uninit lets + SAssign (which would force `mut`).
        // Bindings come in shorthand (`f` → bind name = field name) or
        // `f: x` rename form.
        auto blk = lir::alloc_block(*cur_prog_);
        std::string tmp = std::format("__dst_{}", destruct_counter_++);
        define(tmp, rhs_type);
        {
            lir::SLet sl;
            sl.name = tmp; sl.type = rhs_type; sl.is_mut = false;
            sl.value = std::move(rhs);
            blk->stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
        }
        // Walk the user's PAT_FIELD list: per entry, validate the field
        // exists in the variant and pick the user-visible binding name.
        struct UserField { std::string fname; std::string bind; size_t idx; TypeRef ftype; };
        std::vector<UserField> ufields;
        if (pat_node.has_key(la::ITEMS)) {
            AnyVal iav = pat_node.get(la::ITEMS.code);
            if (!iav.is_null() && iav.is_pointer()) {
                auto fl = map_of(iav);
                ArrayView fitems;
                if (fl.has_key(la::ITEMS)) fitems = arr_of(fl.get(la::ITEMS.code));
                else                        fitems = arr_of(iav);
                for (uint64_t i = 0; i < fitems.size(); ++i) {
                    auto fnode = map_of(fitems.get(i));
                    int32_t fcode = code_of(fnode);
                    if (fcode == la::PAT_REST) continue;  // irrelevant at let-pos
                    if (!fnode.has_key(la::NAME)) continue;
                    std::string fname(str_of(fnode.get(la::NAME.code)));
                    size_t idx = sve_vinfo->payload_field_names.size();
                    for (size_t k = 0; k < sve_vinfo->payload_field_names.size(); ++k)
                        if (sve_vinfo->payload_field_names[k] == fname) { idx = k; break; }
                    if (idx == sve_vinfo->payload_field_names.size()) {
                        error(std::format("let {}::{}: no field named '{}'",
                              sve_ename, sve_vname, fname));
                        continue;
                    }
                    std::string bind = fname;  // shorthand default
                    if (fnode.has_key(la::VALUE)) {
                        auto sub = map_of(fnode.get(la::VALUE.code));
                        if (code_of(sub) == la::PAT_WILD) {
                            bind = sub.has_key(la::NAME)
                                ? std::string(str_of(sub.get(la::NAME.code)))
                                : std::string("_");
                        } else {
                            error(std::format(
                                "let {}::{} field '{}': only plain-identifier "
                                "bindings supported at let-position",
                                sve_ename, sve_vname, fname));
                            bind = "_";
                        }
                    }
                    ufields.push_back({fname, bind, idx, sve_vinfo->payload_types[idx]});
                }
            }
        }
        // Emit per-binding synthetic match-as-expression.
        for (auto& uf : ufields) {
            if (uf.bind == "_") continue;
            std::string syn = std::format("__sve_{}", tmp_var_count_++);
            // Synthesize match arm pattern: E::V { fname_k: __syn, … } with
            // every other position bound to "_".
            std::vector<std::string> arm_bindings(
                sve_vinfo->payload_field_names.size(), "_");
            arm_bindings[uf.idx] = syn;
            std::vector<TypeRef> arm_types;
            for (auto pt : sve_vinfo->payload_types) arm_types.push_back(pt);
            auto pat_off = lir_mirror_emit_pat_variant_data(
                *cur_prog_, sve_ename, sve_vname,
                (int64_t)sve_vinfo->value, arm_bindings, arm_types);
            // Register synth binding in the surrounding scope so the arm
            // value's VarRef sema-resolves to its type.
            define(syn, uf.ftype);
            lir::EMatchArm arm;
            arm.pat.mirror_offset_ = pat_off;
            arm.value = builder().var_ref(syn, uf.ftype);
            lir::EMatchExpr me;
            me.scrut = builder().var_ref(tmp, rhs_type);
            me.arms.push_back(std::move(arm));
            auto match_expr = builder().match_expr_v(std::move(me), uf.ftype);
            define(uf.bind, uf.ftype);
            lir::SLet sl;
            sl.name = uf.bind; sl.type = uf.ftype; sl.is_mut = false;
            sl.value = std::move(match_expr);
            blk->stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
        }
        lir::SBlock sb;
        sb.body = std::move(blk);
        return make_stmt_emit(node_line_, std::move(sb));
    }
    if (is_array_slice_pat) {
        auto elem_t = TypeRef(rhs_type).elem();
        size_t arr_n = (size_t)TypeRef(rhs_type).arr_size();
        std::vector<hermes::TinyMapView> sub_pats;
        bool has_rest = false;
        if (pat_node.has_key(la::ITEMS)) {
            auto items_av = pat_node.get(la::ITEMS.code);
            if (!items_av.is_null() && items_av.is_pointer()) {
                auto elist = map_of(items_av);
                if (elist.has_key(la::ITEMS)) {
                    auto eitems = arr_of(elist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < eitems.size(); ++i) {
                        auto en = map_of(eitems.get(i));
                        if (code_of(en) == la::PAT_REST) { has_rest = true; continue; }
                        sub_pats.push_back(en);
                    }
                }
            }
        }
        if (has_rest) {
            error("`let [..]` pattern: `..` rest at let-position is "
                  "currently not supported (refutable shape — use a "
                  "fully-enumerated pattern that covers the array's "
                  "length)");
            return builder().stmt_expr(std::move(rhs), node_line_);
        }
        if (sub_pats.size() != arr_n) {
            error(std::format(
                "let array pattern: expected {} elements, got {}",
                arr_n, sub_pats.size()));
            return builder().stmt_expr(std::move(rhs), node_line_);
        }
        auto blk = lir::alloc_block(*cur_prog_);
        std::string tmp = std::format("__dst_{}", destruct_counter_++);
        define(tmp, rhs_type);
        // P4-pm-15 Drop case: when element type carries Drop, the bytewise
        // slice_index reads below copy the bytes but transfer ownership
        // into the per-element bindings. Suppress the temp's drop and
        // also any source-var drop, otherwise the array's [T;N] tail-drop
        // double-frees the same payload.
        if (rhs) mark_moved_expr(expr_ref_of(*rhs));
        {
            lir::SLet sl;
            sl.name = tmp; sl.type = rhs_type; sl.is_mut = false;
            sl.value = std::move(rhs);
            blk->stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
        }
        mark_moved(tmp);
        for (size_t j = 0; j < sub_pats.size(); ++j) {
            auto en = sub_pats[j];
            int32_t ec = code_of(en);
            if (ec == la::PAT_WILD && en.has_key(la::NAME)) {
                auto vname = std::string(str_of(en.get(la::NAME.code)));
                if (vname == "_") continue;
                lir::SLet sl;
                sl.name   = vname;
                sl.type   = elem_t;
                sl.is_mut = false;
                // 2026-05-13: previously used slice_index (fat-pointer
                // GEP shape `{ptr, i64}`) on the temp slot. That worked
                // accidentally because the (broken) let-rebind dropped a
                // pointer-to-source-array into the temp's first 8 bytes,
                // which lined up with the slice's data-ptr position. With
                // the let-rebind memcpy fix, the temp now holds the
                // actual array bytes — slice_index reads them as if they
                // were a fat pointer and segfaults. Use index_read
                // (array-shaped GEP) instead.
                sl.value  = builder().index_read(
                    builder().var_ref(tmp, rhs_type),
                    builder().lit_int((int64_t)j, prim(LogosType::Kind::I64)),
                    elem_t);
                define(vname, elem_t);
                blk->stmts.push_back(
                    make_stmt_emit(node_line_, std::move(sl)));
            } else {
                error(std::format(
                    "let array pattern: only plain identifier bindings "
                    "are supported at element {} (got non-PAT_WILD)", j));
            }
        }
        lir::SBlock sb;
        sb.body = std::move(blk);
        return make_stmt_emit(node_line_, std::move(sb));
    }
    if (is_tuple_struct_pat) {
        // Mini destructure path: temp = rhs; let a = temp.0; let b = temp.1; …
        auto sname = std::string(str_of(pat_node.get(la::NAME.code)));
        if (TypeRef(rhs_type).kind() != LogosType::Kind::Struct ||
            TypeRef(rhs_type).struct_name() != sname) {
            error(std::format("let pattern: struct '{}' does not match rhs type '{}'",
                  sname, type_str(rhs_type)));
            return builder().stmt_expr(std::move(rhs), node_line_);
        }
        auto blk = lir::alloc_block(*cur_prog_);
        std::string tmp = std::format("__dst_{}", destruct_counter_++);
        define(tmp, rhs_type);
        {
            lir::SLet sl;
            sl.name = tmp; sl.type = rhs_type; sl.is_mut = false;
            sl.value = std::move(rhs);
            blk->stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
        }
        size_t arg_n = 0;
        if (pat_node.has_key(la::ARGS)) {
            auto aav = pat_node.get(la::ARGS.code);
            if (!aav.is_null() && aav.is_pointer()) {
                auto blist = map_of(aav);
                if (blist.has_key(la::ITEMS)) {
                    auto bitems = arr_of(blist.get(la::ITEMS.code));
                    arg_n = bitems.size();
                    for (uint64_t j = 0; j < bitems.size() && j < tsi_let->fields.size(); ++j) {
                        auto bnode = map_of(bitems.get(j));
                        int32_t bc = code_of(bnode);
                        // PAT_WILD bindings only for now; underscore skips.
                        if (bc == la::PAT_WILD && bnode.has_key(la::NAME)) {
                            auto vname = std::string(str_of(bnode.get(la::NAME.code)));
                            if (vname == "_") continue;
                            auto ftype = tsi_let->fields[j].type;
                            lir::SLet sl;
                            sl.name   = vname;
                            sl.type   = ftype;
                            sl.is_mut = false;
                            sl.value  = builder().field_read(
                                builder().var_ref(tmp, rhs_type),
                                std::to_string(j), ftype);
                            define(vname, ftype);
                            blk->stmts.push_back(
                                make_stmt_emit(node_line_, std::move(sl)));
                        } else {
                            error(std::format(
                                "tuple-struct `let` pattern: only plain identifier "
                                "bindings are supported (got nested pattern at field {})",
                                j));
                        }
                    }
                }
            }
        }
        if (arg_n != tsi_let->fields.size())
            error(std::format(
                "tuple-struct pattern '{}': expected {} fields, got {}",
                sname, tsi_let->fields.size(), arg_n));
        lir::SBlock sb;
        sb.body = std::move(blk);
        return make_stmt_emit(node_line_, std::move(sb));
    }
    if (TypeRef(rhs_type).kind() != LogosType::Kind::Struct &&
        TypeRef(rhs_type).kind() != LogosType::Kind::ZonedStruct) {
        error(std::format("let <struct-pat> = expr: rhs must be a struct, got '{}'",
              type_str(rhs_type)));
        return builder().stmt_expr(std::move(rhs), node_line_);
    }
    auto sname = std::string(str_of(pat_node.get(la::NAME.code)));
    if (sname != std::string_view(TypeRef(rhs_type).struct_name())) {
        error(std::format("let pattern: struct '{}' does not match rhs type '{}'",
              sname, type_str(rhs_type)));
        return builder().stmt_expr(std::move(rhs), node_line_);
    }
    auto blk = lir::alloc_block(*cur_prog_);
    std::string tmp = std::format("__dst_{}", destruct_counter_++);
    define(tmp, rhs_type);
    {
        lir::SLet sl;
        sl.name = tmp; sl.type = rhs_type; sl.is_mut = false;
        sl.value = std::move(rhs);
        blk->stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
    }
    // B98: helper that destructures a struct-pattern into the block.
    // Recursive (handles nested PAT_STRUCT in field values).
    std::function<void(TinyMapView, const std::string&, TypeRef)> emit_destruct;
    emit_destruct = [&](TinyMapView pat, const std::string& recv_var, TypeRef recv_type) {
        if (!pat.has_key(la::ITEMS)) return;
        auto items_av = pat.get(la::ITEMS.code);
        if (!items_av.is_pointer()) return;
        auto fitems = map_of(items_av);
        if (!fitems.has_key(la::ITEMS)) return;
        auto fields = arr_of(fitems.get(la::ITEMS.code));
        std::string recv_sname(TypeRef(recv_type).struct_name());
        for (uint64_t i = 0; i < fields.size(); ++i) {
            auto fav = fields.get(i);
            if (!fav.is_pointer()) continue;
            auto fnode = map_of(fav);
            int32_t fc = code_of(fnode);
            if (fc == la::PAT_REST) continue;
            if (fc != la::PAT_FIELD) continue;
            auto fname = std::string(str_of(fnode.get(la::NAME.code)));
            std::string bind_name = fname;
            // Pattern variants in field value: PAT_WILD (alias name),
            // PAT_STRUCT (nested struct destructure), else error.
            TinyMapView sub{};
            bool has_sub = false;
            if (fnode.has_key(la::VALUE)) {
                sub = map_of(fnode.get(la::VALUE.code));
                has_sub = true;
            }
            if (has_sub && code_of(sub) == la::PAT_WILD && sub.has_key(la::NAME)) {
                bind_name = std::string(str_of(sub.get(la::NAME.code)));
                has_sub = false;  // simple alias — treat as name bind
            }
            auto ft = field_type_of(recv_sname, fname);
            if (!ft) {
                error(std::format("struct '{}': unknown field '{}'", recv_sname, fname));
                continue;
            }
            // Substitute generic type-args.
            {
                auto [pkg, si] = find_struct_by_name(TypeRef(recv_type).struct_name());
                (void)pkg;
                if (si && !si->type_params.empty()) {
                    SemaSubst subst;
                    auto tas = TypeRef(recv_type).type_args();
                    for (size_t k = 0; k < si->type_params.size() && k < tas.size(); ++k)
                        subst[si->type_params[k].name] = tas[k];
                    ft = subst_type_sema(ft, subst);
                }
            }
            // Emit a let with the field value.
            std::string fvar = has_sub
                ? std::format("__dst_{}_{}", destruct_counter_, fname)
                : bind_name;
            if (has_sub) ++destruct_counter_;
            define(fvar, ft);
            {
                auto recv = builder().var_ref(recv_var, recv_type);
                auto fr   = builder().field_read(std::move(recv), fname, ft);
                lir::SLet sl;
                sl.name = fvar; sl.type = ft; sl.is_mut = false;
                sl.value = std::move(fr);
                blk->stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
            }
            // If sub is a nested struct pattern, recurse.
            if (has_sub && code_of(sub) == la::PAT_STRUCT) {
                // The nested struct name must match `ft`'s struct.
                auto sub_sname = str_of(sub.get(la::NAME.code));
                if (TypeRef(ft).kind() != LogosType::Kind::Struct &&
                    TypeRef(ft).kind() != LogosType::Kind::ZonedStruct) {
                    error(std::format("nested pattern: field '{}' is not a struct", fname));
                    continue;
                }
                if (sub_sname != std::string_view(TypeRef(ft).struct_name())) {
                    error(std::format("nested pattern: expected '{}', got '{}'",
                        std::string_view(TypeRef(ft).struct_name()), sub_sname));
                    continue;
                }
                emit_destruct(sub, fvar, ft);
            } else if (has_sub) {
                error(std::format(
                    "let struct-pattern field '{}': nested patterns of this kind not "
                    "yet supported; bind to a name", fname));
            }
        }
    };
    emit_destruct(pat_node, tmp, rhs_type);
    lir::SBlock sb;
    sb.body = std::move(blk);
    return make_stmt_emit(node_line_, std::move(sb));
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

    // 3. Lower else block in nested scope (must diverge — closes B-st-03).
    push_scope();
    lir::LBlock else_blk;
    if (node.has_key(la::BODY)) {
        auto body_node = map_of(node.get(la::BODY.code));
        if (!block_always_diverts(body_node)) {
            error("'let-else' else-block must diverge "
                  "(end in 'return', 'break', 'continue', 'panic', or 'loop {}')");
        }
        else_blk = lower_block(body_node);
    }
    pop_scope();

    // 4. Add pattern bindings to outer scope
    {
        namespace ps = lir_schema::pat;
        auto pr = pat_ref_of(pat);
        auto* pool = cur_prog_->type_pool.impl();
        if (pr.kind() == ps::Code::VariantData) {
            lir_view::PatVariantDataView v{pr};
            std::vector<std::string_view> names;
            std::vector<TypeRef> types;
            v.each_binding([&](std::string_view n) { names.push_back(n); });
            v.each_binding_type(pool, [&](TypeRef t) { types.push_back(t); });
            for (size_t i = 0; i < names.size() && i < types.size(); ++i)
                define(std::string(names[i]), types[i]);
        } else if (pr.kind() == ps::Code::Tuple) {
            lir_view::PatTupleView v{pr};
            std::vector<std::string_view> names;
            std::vector<TypeRef> types;
            v.each_binding([&](std::string_view n) { names.push_back(n); });
            v.each_binding_type(pool, [&](TypeRef t) { types.push_back(t); });
            for (size_t i = 0; i < names.size() && i < types.size(); ++i)
                if (names[i] != "_")
                    define(std::string(names[i]), types[i]);
        } else if (pr.kind() == ps::Code::Wild) {
            lir_view::PatWildView v{pr};
            auto n = v.name();
            if (n != "_")
                define(std::string(n), scrut_type);
        }
        // PatVariant (no bindings) — nothing to define
    }

    // 5. Emit SLetElse
    lir::SLetElse sle;
    sle.pat        = std::move(pat);
    sle.scrut      = std::move(scrut);
    sle.else_block = lir::alloc_block(*cur_prog_, std::move(else_blk));
    return make_stmt_emit(node_line_, std::move(sle));
}

// `fn inner(params) [-> T] { body }` at stmt position. Lower as a let-bound
// closure: `let inner = |params| -> T { body }`. The closure machinery
// handles codegen / lifting / mangling. The NESTED_FN AST node carries the
// same field shape as CLOSURE_EXPR (PARAMS / RET_TYPE / BODY), so we can
// pass the node directly to lower_closure_expr. The local binding is
// emitted as an SLet with the closure value; the variable's type comes
// from the closure's own inferred type.
lir::LStmt SemaChecker::lower_nested_fn(TinyMapView node) {
    auto name = std::string(str_of(node.get(la::NAME.code)));
    auto value = lower_closure_expr(node);
    auto var_type = value ? value->type : error_t();
    define(name, var_type, /*is_mut=*/false);
    lir::SLet sl;
    sl.name   = name;
    sl.type   = var_type;
    sl.is_mut = false;
    sl.value  = std::move(value);
    return make_stmt_emit(node_line_, std::move(sl));
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
    auto saved_ret_hint = hint_call_return_type_;
    if (ann && TypeRef(ann).kind() != LogosType::Kind::Error)
        hint_call_return_type_ = ann;

    // C6-cc-04: `let p = &<scalar literal>;` — Rust extends the
    // temporary's lifetime to the enclosing scope; Logos previously
    // rejected with a diagnostic, but it's a legitimate pattern.
    // Synthesize a hidden `let __lit_temp_N = <lit>;` BEFORE the user's
    // let, then rewrite the user's value to `&__lit_temp_N`. Emit both
    // as a single SBlock; `define()` at outer scope keeps the user's
    // binding visible after.
    if (node.has_key(la::VALUE)) {
        auto val_node = map_of(node.get(la::VALUE.code));
        if (code_of(val_node) == la::UNARY && val_node.has_key(la::OP) &&
            val_node.has_key(la::VALUE)) {
            auto op_sv = str_of(val_node.get(la::OP.code));
            if (op_sv == "&") {
                auto inner = map_of(val_node.get(la::VALUE.code));
                int32_t inner_c = code_of(inner);
                if (inner_c == la::LIT_INT  || inner_c == la::LIT_BOOL ||
                    inner_c == la::LIT_FLOAT || inner_c == la::LIT_CHAR) {
                    // Lower the literal expr — its concrete type drives the
                    // synth let's type. Use the annotation pointee as the
                    // type hint if present so suffix-less literals widen
                    // to the right primitive.
                    TypeRef hint_lit = nullptr;
                    if (ann && (TypeRef(ann).kind() == LogosType::Kind::Ref ||
                                TypeRef(ann).kind() == LogosType::Kind::MutRef))
                        hint_lit = TypeRef(ann).pointee();
                    auto saved_lit_hint = hint_call_return_type_;
                    if (hint_lit) hint_call_return_type_ = hint_lit;
                    auto lit_expr = lower_expr(inner);
                    hint_call_return_type_ = saved_lit_hint;
                    if (hint_lit && lit_expr->type &&
                        TypeRef(lit_expr->type).kind() == LogosType::Kind::IntLit)
                        builder().retype_expr(lit_expr, hint_lit);
                    TypeRef lit_type = lit_expr->type;

                    std::string tmp = std::format("__lit_temp_{}", destruct_counter_++);
                    define(tmp, lit_type);
                    define(std::string(name), ann ? ann : nullptr);

                    auto blk = lir::alloc_block(*cur_prog_);

                    // synth: `let __lit_temp_N = <lit>;`
                    lir::SLet sl_tmp;
                    sl_tmp.name   = tmp;
                    sl_tmp.type   = lit_type;
                    sl_tmp.is_mut = false;
                    sl_tmp.value  = std::move(lit_expr);
                    blk->stmts.push_back(make_stmt_emit(node_line_, std::move(sl_tmp)));

                    // user:  `let name = &__lit_temp_N;`
                    auto addr = builder().addr_of(tmp, make_ref(false, lit_type));
                    lir::SLet sl_user;
                    sl_user.name   = std::string(name);
                    sl_user.type   = ann ? ann : addr->type;
                    sl_user.is_mut = is_mut;
                    sl_user.value  = std::move(addr);
                    blk->stmts.push_back(make_stmt_emit(node_line_, std::move(sl_user)));

                    hint_enum_type_ = saved_hint;
                    hint_struct_type_ = saved_struct_hint;
                    hint_call_return_type_ = saved_ret_hint;
                    return make_stmt_emit(node_line_, lir::SBlock{std::move(blk)});
                }
            }
        }
    }

    // P4-pm-14: `let ref y = x;` (or `let ref y: T = x;`) is sugar for
    // `let y = &x;`. Detect IS_REF here and rewrite the lowered RHS as
    // an addr-of-expr; the binding's type adopts the addr-of's result.
    bool is_ref_bind = false;
    if (node.has_key(la::IS_REF)) {
        AnyVal av = node.get(la::IS_REF.code);
        if (!av.is_null() && av.is_value()) is_ref_bind = av.as_value<uint8_t>() != 0;
    }

    lir::LExprPtr rhs = nullptr;
    TypeRef rhs_type;
    if (node.has_key(la::VALUE)) {
        rhs      = lower_expr(map_of(node.get(la::VALUE.code)));
        rhs_type = rhs->type;
        if (is_ref_bind) {
            // Wrap the lowered RHS in an addr-of-temp so it produces
            // `&rhs` with type `&T` (matches `let y = &x;` semantics).
            auto inner_t = rhs->type;
            rhs      = builder().addr_of_temp(std::move(rhs), /*is_mut=*/false,
                                              make_ref(false, inner_t));
            rhs_type = rhs->type;
        }
    } else if (ann) {
        // B3-bg-01 / B3-bg-02: `let v: T;` / `let mut v: T;` —
        // declare-without-init. Binding takes the annotated type; value
        // remains null. The variable must be definitely-assigned before
        // use; assignment paths register the value (lower_assign), and
        // reads of an uninitialised binding will surface as either
        // mlir-gen "use of uninitialised slot" or a borrow-check warn.
        // (Full definite-assignment analysis is a separate pass; for now
        // we trust user code or rely on later use-checks.)
        rhs      = nullptr;
        rhs_type = ann;
    } else {
        error(std::format("let '{}': missing value", name));
        rhs      = error_expr();
        rhs_type = error_t();
    }

    hint_enum_type_ = saved_hint;
    hint_struct_type_ = saved_struct_hint;
    hint_call_return_type_ = saved_ret_hint;

    TypeRef var_type;
    // Slice 7 of metaprog-quote: an ExprBlob-typed RHS marks a deferred
    // metacall whose actual expr type is determined post-splice (pass-2
    // sema reads the blob's root schema_type_code and recurses into
    // lower_expr). Pass-1 here just adopts the annotation and skips the
    // strict type-equality check; pass-2 will verify compatibility once
    // the HERMES_BLOB has been lowered to a real expr.
    bool rhs_is_expr_blob =
        rhs &&
        TypeRef(rhs_type).kind() == LogosType::Kind::Struct &&
        is_exprblob(rhs_type);
    if (rhs_is_expr_blob && ann != nullptr) {
        rhs_type = ann;
        if (rhs) rhs->type = ann;
    }
    if (rhs && ann != nullptr) {
        // impl Trait annotation: any concrete struct/class that was returned from an
        // impl-Trait-returning function is acceptable — treat the variable type as the
        // concrete rhs type so method calls work.
        bool ann_is_impl = TypeRef(ann).kind() == LogosType::Kind::ImplTrait;
        if (!ann_is_impl &&
            TypeRef(ann).kind() != LogosType::Kind::Error &&
            TypeRef(rhs_type).kind() != LogosType::Kind::Error &&
            !rhs_is_expr_blob &&
            !types_compatible(rhs_type, ann)) {
            // Non-capturing closure literal → fn(...) -> T coercion.
            if (try_coerce_closure_to_fnptr(rhs, ann)) {
                rhs_type = rhs->type;
            } else if (is_hermes_static(ann) && is_hermes(rhs_type)) {
                // B-he-05: HermesStatic ← Hermes mismatch is almost always
                // caused by `${capture}` or other runtime-only constructs in
                // the @-literal. Emit a capture-specific hint.
                error(std::format(
                    "let '{}': @-literal evaluated to runtime Hermes (likely "
                    "due to `${{capture}}` or other runtime-only construct); "
                    "HermesStatic does not permit captures — drop them, or "
                    "annotate `{}: Hermes` instead",
                    name, name));
            } else {
                auto [es, gs] = type_str_pair(ann, rhs_type);
                error(std::format("let '{}': type mismatch — expected {}, got {}",
                      name, es, gs));
            }
        }
        // B64: variance-aware subtype check at let-init coercion site.
        // Strict mode — let annotation is fn-scope-fixed.
        if (rhs && ann && !rhs_is_expr_blob)
            check_variance(rhs_type, ann, std::format("let '{}'", name),
                           /*permissive=*/false);
        // Implicit safe integer widening: u32 → i64, i32 → i64, u8 → u32, ...
        if (rhs && ann && is_integer_kind(TypeRef(ann).kind()) && is_integer_kind(TypeRef(rhs_type).kind()) &&
            TypeRef(rhs_type).kind() != LogosType::Kind::IntLit &&
            TypeRef(rhs_type).kind() != LogosType::Kind::Enum &&
            can_widen_int(TypeRef(rhs_type).kind(), TypeRef(ann).kind())) {
            widen_int_expr(rhs, ann, builder());
            rhs_type = rhs->type;
        }
        // Retype float literal to concrete annotation type (f32 or f64).
        if (TypeRef(rhs_type).kind() == LogosType::Kind::FloatLit && ann &&
            (TypeRef(ann).kind() == LogosType::Kind::F32 || TypeRef(ann).kind() == LogosType::Kind::F64))
            builder().retype_expr(rhs, ann);
        // Retype/coerce integer literal (or IntLit-typed expr) to float annotation type (f32 or f64).
        if (TypeRef(rhs_type).kind() == LogosType::Kind::IntLit && ann &&
            (TypeRef(ann).kind() == LogosType::Kind::F32 || TypeRef(ann).kind() == LogosType::Kind::F64)) {
            auto rhs_ref = expr_ref_of(*rhs);
            if (rhs_ref.kind() == lir_schema::expr::Code::LitInt) {
                // Simple integer literal: convert directly to float literal.
                // Build a fresh node so the Hermes mirror is emitted with
                // Code::LitFloat (in-place mutation would leave the stale
                // LitInt mirror that view-based readers in mono pick up).
                double fval = static_cast<double>(lir_view::ELitIntView{rhs_ref}.value());
                rhs = builder().lit_float(fval, ann);
            } else {
                // Non-literal IntLit expression (e.g. 1 + 2): wrap in ECast to float.
                rhs = builder().cast(std::move(rhs), ann);
                rhs_type = ann;
            }
        }
        // Detect integer literals that don't fit in the annotated type.
        if (TypeRef(rhs_type).kind() == LogosType::Kind::IntLit && TypeRef(ann).kind() != LogosType::Kind::Error) {
            if (auto v = get_intlit_value(rhs))
                if (!intlit_fits(*v, TypeRef(ann).kind()))
                    error(std::format("let '{}': literal value {} does not fit in {}",
                          name, *v, type_str(ann)));
        }
        // Check each IntLit array element fits in the annotation's element type.
        if (TypeRef(ann).kind() == LogosType::Kind::Array && TypeRef(ann).elem() &&
            TypeRef(rhs_type).kind() == LogosType::Kind::Array && TypeRef(rhs_type).elem() &&
            TypeRef(rhs_type).elem().kind() == LogosType::Kind::IntLit) {
            auto rhs_ref = expr_ref_of(*rhs);
            if (rhs_ref.kind() == lir_schema::expr::Code::ArrLit) {
                lir_view::EArrLitView arrlit{rhs_ref};
                for (uint64_t ei = 0; ei < arrlit.count(); ++ei) {
                    if (auto v = get_intlit_value(arrlit.elem(ei)))
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
            auto rhs_ref = expr_ref_of(*rhs);
            if (rhs_ref.kind() == lir_schema::expr::Code::TupleLit) {
                lir_view::ETupleLitView tlit_view{rhs_ref};
                const auto& tup_anns = TypeRef(ann).tuple_elems();
                uint64_t n = std::min<uint64_t>(tlit_view.count(), tup_anns.size());
                for (uint64_t ei = 0; ei < n; ++ei) {
                    auto* elem_lexpr = lexpr_of(tlit_view.elem(ei));
                    if (!elem_lexpr) continue;
                    TypeRef ann_e = tup_anns[ei];
                    auto elem_kind = TypeRef(elem_lexpr->type).kind();
                    bool ann_is_float = ann_e && (TypeRef(ann_e).kind() == LogosType::Kind::F32 ||
                                                  TypeRef(ann_e).kind() == LogosType::Kind::F64);
                    // Retype FloatLit element to concrete float annotation (f32/f64).
                    if (elem_kind == LogosType::Kind::FloatLit && ann_is_float)
                        builder().retype_expr(elem_lexpr, ann_e);
                    // Replace IntLit element with a concrete-typed FloatLit when the
                    // annotation is a float — re-emits the parent tuple's mirror.
                    if (elem_kind == LogosType::Kind::IntLit && ann_is_float) {
                        auto er = expr_ref_of(*elem_lexpr);
                        if (er.kind() == lir_schema::expr::Code::LitInt) {
                            double fval = static_cast<double>(lir_view::ELitIntView{er}.value());
                            builder().set_tuple_elem(rhs, ei, builder().lit_float(fval, ann_e));
                            // Re-fetch view since rhs's mirror_offset_ is fresh.
                            tlit_view = lir_view::ETupleLitView{expr_ref_of(*rhs)};
                            continue;
                        }
                    }
                    if (elem_kind == LogosType::Kind::IntLit)
                        if (auto v = get_intlit_value(elem_lexpr))
                            if (ann_e && !intlit_fits(*v, TypeRef(ann_e).kind()))
                                error(std::format("let '{}': tuple element {}: value {} does not fit in {}",
                                      name, ei, *v, type_str(ann_e)));
                    // Tuple element is itself an array literal.
                    if (ann_e && TypeRef(ann_e).kind() == LogosType::Kind::Array &&
                        TypeRef(ann_e).elem() && elem_kind == LogosType::Kind::Array) {
                        auto er = expr_ref_of(*elem_lexpr);
                        if (er.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{er};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(ann_e).elem().kind()))
                                            error(std::format("let '{}': tuple element {}: array element {}: value {} does not fit in {}",
                                                  name, ei, ii, *v, type_str(TypeRef(ann_e).elem())));
                            }
                        }
                    }
                    // Tuple element is itself a tuple literal.
                    if (ann_e && TypeRef(ann_e).kind() == LogosType::Kind::Tuple &&
                        elem_kind == LogosType::Kind::Tuple) {
                        auto er = expr_ref_of(*elem_lexpr);
                        if (er.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{er};
                            uint64_t ii = 0;
                            const auto& sub_anns = TypeRef(ann_e).tuple_elems();
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii < sub_anns.size() &&
                                    iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (sub_anns[ii] && !intlit_fits(*v, TypeRef(sub_anns[ii]).kind()))
                                            error(std::format("let '{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  name, ei, ii, *v, type_str(sub_anns[ii])));
                                ++ii;
                            });
                        }
                    }
                }
            }
        }
        // For impl Trait annotations, use the concrete rhs type so that method calls work.
        var_type = ann_is_impl ? rhs_type : ann;
        // Retype the rhs tuple expression node to use the concrete annotation tuple type.
        // This ensures codegen sees (f32, f32) instead of (FloatLit, FloatLit).
        if (!ann_is_impl && TypeRef(ann).kind() == LogosType::Kind::Tuple &&
            TypeRef(rhs_type).kind() == LogosType::Kind::Tuple)
            builder().retype_expr(rhs, ann);
    } else {
        var_type = rhs_type;
        if (TypeRef(var_type).kind() == LogosType::Kind::IntLit) {
            // Default IntLit to i32; upgrade to i64 if the literal value overflows i32.
            var_type = i32_t();
            auto er = expr_ref_of(*rhs);
            if (er.kind() == lir_schema::expr::Code::LitInt) {
                int64_t v = lir_view::ELitIntView{er}.value();
                if (v > (int64_t)INT32_MAX || v < (int64_t)INT32_MIN)
                    var_type = prim(LogosType::Kind::I64);
            }
        }
        if (TypeRef(var_type).kind() == LogosType::Kind::FloatLit) {
            // Default FloatLit to f64.
            var_type = prim(LogosType::Kind::F64);
            builder().retype_expr(rhs, var_type);
        }
    }

    define(name, var_type, is_mut);

    // Move semantics: if RHS is a variable reference (or struct-field read) of a
    // move type, mark it moved. mark_moved_expr handles both VarRef and
    // nested FieldRead chains, recording dotted paths so make_drop_stmt
    // can suppress per-field auto-drop on the source struct.
    if (rhs && is_move_type(rhs_type))
        mark_moved_expr(expr_ref_of(*rhs));

    lir::SLet slet;
    slet.name   = std::string(name);
    slet.type   = var_type;
    slet.is_mut = is_mut;
    slet.value  = std::move(rhs);
    return make_stmt_emit(node_line_, std::move(slet));
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
        return builder().stmt_break(nullptr, "", node_line_);
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
        auto [es, gs] = type_str_pair(var_type, rhs->type);
        error(std::format("compound assignment to '{}': type mismatch — expected {}, got {}",
              name, es, gs));
    }
    // Synthesize the binop LIR node
    auto binop = builder().bin_op(base_op, std::move(lhs_ref), std::move(rhs), var_type);
    return builder().stmt_assign(std::string(name), std::move(binop), node_line_);
}

lir::LStmt SemaChecker::lower_assign(TinyMapView node) {
    auto name = str_of(node.get(la::NAME.code));
    auto var_type = lookup(name);
    if (!var_type) {
        error(std::format("assignment to undefined variable '{}'", name));
        lir::LExprPtr dummy = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code)))
            : error_expr();
        return builder().stmt_assign(std::string(name), std::move(dummy), node_line_);
    }
    if (!lookup_is_mut(name))
        error(std::format("assignment to immutable variable '{}'", name));

    lir::LExprPtr rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    if (TypeRef(var_type).kind() != LogosType::Kind::Error &&
        TypeRef(rhs->type).kind() != LogosType::Kind::Error &&
        !types_compatible(rhs->type, var_type)) {
        auto [es, gs] = type_str_pair(var_type, rhs->type);
        error(std::format("assignment to '{}': type mismatch — expected {}, got {}",
              name, es, gs));
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
        if (auto v = get_intlit_value(rhs))
            if (!intlit_fits(*v, TypeRef(var_type).kind()))
                error(std::format("assignment to '{}': value {} does not fit in {}",
                      name, *v, type_str(var_type)));
    }
    // Check array literal elements against narrow array variable type.
    if (TypeRef(rhs->type).kind() == LogosType::Kind::Array &&
        TypeRef(var_type).kind() == LogosType::Kind::Array && TypeRef(var_type).elem()) {
        auto rhs_ref = expr_ref_of(*rhs);
        if (rhs_ref.kind() == lir_schema::expr::Code::ArrLit) {
            lir_view::EArrLitView al{rhs_ref};
            for (uint64_t i = 0; i < al.count(); ++i) {
                auto el = al.elem(i);
                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(el))
                        if (!intlit_fits(*v, TypeRef(var_type).elem().kind()))
                            error(std::format("assignment to '{}': array element {}: value {} does not fit in {}",
                                  name, i, *v, type_str(TypeRef(var_type).elem())));
            }
        }
    }
    // Check tuple literal elements against narrow tuple variable element types.
    if (TypeRef(rhs->type).kind() == LogosType::Kind::Tuple && TypeRef(var_type).kind() == LogosType::Kind::Tuple) {
        auto rhs_ref = expr_ref_of(*rhs);
        if (rhs_ref.kind() == lir_schema::expr::Code::TupleLit) {
            lir_view::ETupleLitView tl{rhs_ref};
            uint64_t i = 0;
            tl.each_elem([&](lir_view::ExprRef el) {
                if (i >= TypeRef(var_type).tuple_elems().size()) { ++i; return; }
                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(el))
                        if (TypeRef(var_type).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(var_type).tuple_elems()[i]).kind()))
                            error(std::format("assignment to '{}': tuple element {}: value {} does not fit in {}",
                                  name, i, *v, type_str(TypeRef(var_type).tuple_elems()[i])));
                if (TypeRef(var_type).tuple_elems()[i] && TypeRef(TypeRef(var_type).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                    TypeRef(TypeRef(var_type).tuple_elems()[i]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                    el.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView ial{el};
                    for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                        auto iel = ial.elem(ii);
                        if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(iel))
                                if (!intlit_fits(*v, TypeRef(TypeRef(var_type).tuple_elems()[i]).elem().kind()))
                                    error(std::format("assignment to '{}': tuple element {}: array element {}: value {} does not fit in {}",
                                          name, i, ii, *v, type_str(TypeRef(TypeRef(var_type).tuple_elems()[i]).elem())));
                    }
                }
                if (TypeRef(var_type).tuple_elems()[i] && TypeRef(TypeRef(var_type).tuple_elems()[i]).kind() == LogosType::Kind::Tuple &&
                    el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                    el.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView itl{el};
                    uint64_t ii = 0;
                    itl.each_elem([&](lir_view::ExprRef iel) {
                        if (ii >= TypeRef(TypeRef(var_type).tuple_elems()[i]).tuple_elems().size()) { ++ii; return; }
                        if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(iel))
                                if (TypeRef(TypeRef(var_type).tuple_elems()[i]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(var_type).tuple_elems()[i]).tuple_elems()[ii]).kind()))
                                    error(std::format("assignment to '{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                          name, i, ii, *v, type_str(TypeRef(TypeRef(var_type).tuple_elems()[i]).tuple_elems()[ii])));
                        ++ii;
                    });
                }
                ++i;
            });
        }
    }
    // Re-assignment revives the variable (the old value was already consumed).
    moved_vars_.erase(std::string(name));
    // RHS source consumed: `dst = src` for a move-type src moves src's bytes
    // into dst; src's scope-exit drop must be suppressed, else we double-free.
    track_write_move(rhs);
    return builder().stmt_assign(std::string(name), std::move(rhs), node_line_);
}

lir::LStmt SemaChecker::lower_return(TinyMapView node) {
    lir::LExprPtr val = nullptr;
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
                auto [es, gs] = type_str_pair(ret_type_, val->type);
                error(std::format("return type mismatch — expected {}, got {}",
                      es, gs));
            } else if (ret_type_) {
                check_variance(val->type, ret_type_, "return type mismatch",
                               /*permissive=*/false);
            }
            // Retype float literal to concrete return type.
            if (ret_type_ && TypeRef(val->type).kind() == LogosType::Kind::FloatLit &&
                (TypeRef(ret_type_).kind() == LogosType::Kind::F32 || TypeRef(ret_type_).kind() == LogosType::Kind::F64))
                builder().retype_expr(val, ret_type_);
            else if (TypeRef(val->type).kind() == LogosType::Kind::FloatLit)
                builder().retype_expr(val, prim(LogosType::Kind::F64));
            // Detect integer literals that don't fit in the return type.
            if (ret_type_ && TypeRef(val->type).kind() == LogosType::Kind::IntLit &&
                TypeRef(ret_type_).kind() != LogosType::Kind::Error) {
                if (auto v = get_intlit_value(val))
                    if (!intlit_fits(*v, TypeRef(ret_type_).kind()))
                        error(std::format("return: literal value {} does not fit in {}",
                              *v, type_str(ret_type_)));
            }
            // Detect array literal elements that don't fit in the return element type.
            if (ret_type_ && TypeRef(ret_type_).kind() == LogosType::Kind::Array && TypeRef(ret_type_).elem() &&
                TypeRef(val->type).kind() == LogosType::Kind::Array) {
                auto vr = expr_ref_of(*val);
                if (vr.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView al{vr};
                    for (uint64_t i = 0; i < al.count(); ++i) {
                        auto el = al.elem(i);
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (!intlit_fits(*v, TypeRef(ret_type_).elem().kind()))
                                    error(std::format("return: array element {}: value {} does not fit in {}",
                                          i, *v, type_str(TypeRef(ret_type_).elem())));
                    }
                }
            }
            // Detect tuple literal elements that don't fit in the return tuple element types.
            if (ret_type_ && TypeRef(ret_type_).kind() == LogosType::Kind::Tuple &&
                TypeRef(val->type).kind() == LogosType::Kind::Tuple) {
                auto vr = expr_ref_of(*val);
                if (vr.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView tl{vr};
                    uint64_t i = 0;
                    tl.each_elem([&](lir_view::ExprRef el) {
                        if (i >= TypeRef(ret_type_).tuple_elems().size()) { ++i; return; }
                        if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(el))
                                if (TypeRef(ret_type_).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(ret_type_).tuple_elems()[i]).kind()))
                                    error(std::format("return: tuple element {}: value {} does not fit in {}",
                                          i, *v, type_str(TypeRef(ret_type_).tuple_elems()[i])));
                        if (TypeRef(ret_type_).tuple_elems()[i] && TypeRef(TypeRef(ret_type_).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                            TypeRef(TypeRef(ret_type_).tuple_elems()[i]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                            el.kind() == lir_schema::expr::Code::ArrLit) {
                            lir_view::EArrLitView ial{el};
                            for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                                auto iel = ial.elem(ii);
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (!intlit_fits(*v, TypeRef(TypeRef(ret_type_).tuple_elems()[i]).elem().kind()))
                                            error(std::format("return: tuple element {}: array element {}: value {} does not fit in {}",
                                                  i, ii, *v, type_str(TypeRef(TypeRef(ret_type_).tuple_elems()[i]).elem())));
                            }
                        }
                        if (TypeRef(ret_type_).tuple_elems()[i] && TypeRef(TypeRef(ret_type_).tuple_elems()[i]).kind() == LogosType::Kind::Tuple &&
                            el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                            el.kind() == lir_schema::expr::Code::TupleLit) {
                            lir_view::ETupleLitView itl{el};
                            uint64_t ii = 0;
                            itl.each_elem([&](lir_view::ExprRef iel) {
                                if (ii >= TypeRef(TypeRef(ret_type_).tuple_elems()[i]).tuple_elems().size()) { ++ii; return; }
                                if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                                    if (auto v = get_intlit_value(iel))
                                        if (TypeRef(TypeRef(ret_type_).tuple_elems()[i]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(ret_type_).tuple_elems()[i]).tuple_elems()[ii]).kind()))
                                            error(std::format("return: tuple element {}: sub-element {}: value {} does not fit in {}",
                                                  i, ii, *v, type_str(TypeRef(TypeRef(ret_type_).tuple_elems()[i]).tuple_elems()[ii])));
                                ++ii;
                            });
                        }
                        ++i;
                    });
                }
            }
            // Move semantics: recursively mark any move-type variable that
            // appears in the return expression as moved, so collect_all_drops()
            // won't also drop them (avoids double-free).
            std::function<void(lir_view::ExprRef)> mark_moved_in_expr;
            mark_moved_in_expr = [&](lir_view::ExprRef er) {
                if (!er) return;
                using C = lir_schema::expr::Code;
                switch (er.kind()) {
                    case C::VarRef: {
                        if (is_move_type(er.type(cur_prog_->type_pool.impl())))
                            mark_moved(std::string(lir_view::EVarRefView{er}.name()));
                        return;
                    }
                    case C::FieldRead: {
                        // outer.field passed by value moves the field — mark
                        // "outer.field" so collect_*_drops skips it via
                        // SDrop.moved_fields. Nested FieldReads inside the
                        // receiver are not recursed (those access only,
                        // not move).
                        mark_moved_expr(er);
                        return;
                    }
                    case C::EnumLitData: {
                        lir_view::EEnumLitDataView{er}.each_payload(
                            [&](lir_view::ExprRef a) { mark_moved_in_expr(a); });
                        return;
                    }
                    case C::Call: {
                        lir_view::ECallView{er}.each_arg(
                            [&](lir_view::ExprRef a) { mark_moved_in_expr(a); });
                        return;
                    }
                    case C::StructLit: {
                        lir_view::EStructLitView{er}.each_field(
                            [&](std::string_view, lir_view::ExprRef v) { mark_moved_in_expr(v); });
                        return;
                    }
                    case C::TupleLit: {
                        lir_view::ETupleLitView{er}.each_elem(
                            [&](lir_view::ExprRef a) { mark_moved_in_expr(a); });
                        return;
                    }
                    case C::BlockExpr: {
                        mark_moved_in_expr(lir_view::EBlockExprView{er}.result());
                        return;
                    }
                    default: return;
                }
            };
            if (val) mark_moved_in_expr(expr_ref_of(*val));
            return builder().stmt_return(std::move(val), node_line_);
        }
    }
    // void return
    if (ret_type_ && TypeRef(ret_type_).kind() != LogosType::Kind::Void &&
        TypeRef(ret_type_).kind() != LogosType::Kind::Error &&
        TypeRef(ret_type_).kind() != LogosType::Kind::ImplTrait) {
        error(std::format("return without value in function returning {}",
              type_str(ret_type_)));
    }
    return builder().stmt_return(nullptr, node_line_);
}

lir::Pattern SemaChecker::make_pat_wild(std::string_view name) {
    lir::Pattern p;
    p.mirror_offset_ = lir_mirror_emit_pat_wild(*cur_prog_, name);
    return p;
}

lir::Pattern SemaChecker::build_pattern(TinyMapView pnode, TypeRef scrut_type) {
    // build_pattern_impl now sets mirror_offset_ directly via per-kind direct
    // emitters; no bulk lir_mirror_emit_pat_node call needed here.
    return build_pattern_impl(pnode, scrut_type);
}

lir::Pattern SemaChecker::build_pattern_impl(TinyMapView pnode, TypeRef scrut_type) {
    int32_t pc = code_of(pnode);
    if (pc == la::PAT_VARIANT) {
        auto pename = std::string(str_of(pnode.get(la::NAME.code)));
        auto pvname = std::string(str_of(pnode.get(la::FIELD.code)));
        // CP-cm-03: prelude shorthand `Some` / `None` / `Ok` / `Err`
        // (no `Enum::` qualifier). Remap to enum+variant when the
        // user-supplied NAME is one of the prelude variant names.
        if (pvname.empty()) {
            auto prelude_remap = [&](const char* en) -> bool {
                auto [pkg, esi] = find_enum_by_name(en);
                if (!esi) return false;
                for (auto& v : esi->variants)
                    if (v.name == pename) {
                        pvname = std::move(pename);
                        pename = en;
                        return true;
                    }
                return false;
            };
            if (pename == "Some" || pename == "None")
                prelude_remap("Option");
            else if (pename == "Ok" || pename == "Err")
                prelude_remap("Result");
        }
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
        lir::Pattern p_;
        p_.mirror_offset_ = lir_mirror_emit_pat_variant(*cur_prog_, pename, pvname, disc);
        return p_;
    }
    if (pc == la::PAT_VARIANT_DATA) {
        auto pename = std::string(str_of(pnode.get(la::NAME.code)));
        auto pvname = std::string(str_of(pnode.get(la::FIELD.code)));
        // CP-cm-03: Rust-prelude shorthand on patterns —
        // `Some(x)` / `Ok(x)` / `Err(x)` parsed as PAT_VARIANT_DATA
        // with NAME=variant, FIELD="". Reroute to enum+variant
        // form when the enum is in scope and tuple-struct lookup
        // would otherwise fail.
        if (pvname.empty()) {
            auto prelude_remap = [&](const char* en) -> bool {
                auto [pkg, esi] = find_enum_by_name(en);
                if (!esi) return false;
                for (auto& v : esi->variants)
                    if (v.name == pename) {
                        pvname = std::move(pename);
                        pename = en;
                        return true;
                    }
                return false;
            };
            if (pename == "Some" || pename == "None")
                prelude_remap("Option");
            else if (pename == "Ok" || pename == "Err")
                prelude_remap("Result");
        }
        // B-ts-01: bare `Foo(a, b)` (no `::` separator) — pvname is
        // empty. If `Foo` resolves to a tuple-struct, lower as a
        // struct destructure with synth field names "0", "1", …
        // each paired with the user-supplied sub-pattern (binding
        // names become PatWild sub-pats).
        if (pvname.empty()) {
            auto [tspkg_p, tsi_p] = find_struct_by_name(pename);
            if (tsi_p && tsi_p->is_tuple_struct) {
                lir::PatStruct ps;
                ps.struct_name = pename;
                ps.has_rest    = false;
                size_t sub_n = 0;
                if (pnode.has_key(la::ARGS)) {
                    auto aav = pnode.get(la::ARGS.code);
                    if (!aav.is_null() && aav.is_pointer()) {
                        auto blist = map_of(aav);
                        if (blist.has_key(la::ITEMS)) {
                            auto bitems = arr_of(blist.get(la::ITEMS.code));
                            sub_n = bitems.size();
                            for (uint64_t j = 0; j < bitems.size(); ++j) {
                                auto bnode = map_of(bitems.get(j));
                                TypeRef ftype = j < tsi_p->fields.size()
                                                ? tsi_p->fields[j].type : nullptr;
                                lir::PatFieldBinding fb;
                                fb.field_name = std::to_string(j);
                                fb.sub.push_back(build_pattern(bnode, ftype));
                                ps.fields.push_back(std::move(fb));
                            }
                        }
                    }
                }
                if (sub_n != tsi_p->fields.size())
                    error(std::format(
                        "tuple-struct pattern '{}': expected {} fields, got {}",
                        pename, tsi_p->fields.size(), sub_n));
                auto mo = lir_mirror_emit_pat_struct(
                    *cur_prog_, ps.struct_name, ps.fields, ps.has_rest);
                lir::Pattern p_;
                p_.mirror_offset_ = mo;
                return p_;
            }
        }
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
        bool pat_is_struct_shape = pnode.has_key(la::variant::IS_STRUCT_SHAPE) &&
            pnode.get(la::variant::IS_STRUCT_SHAPE.code).as_value<int32_t>() != 0;
        // P4-pm-01 (refutable inner): pre-compute the per-position resolved
        // payload types so refutable sub-patterns can synthesize typed
        // guard expressions while we walk the pattern. (The post-hoc
        // binding_types pass below still runs — it's the canonical input
        // to lir_mirror_emit_pat_variant_data.)
        SemaSubst pat_subst;
        if (vinfo && TypeRef(scrut_type).kind() == LogosType::Kind::Enum &&
            !TypeRef(scrut_type).type_args().empty() && eit != enums_.end()) {
            auto& einfo = eit->second;
            for (size_t k = 0; k < einfo.type_params.size() &&
                                 k < TypeRef(scrut_type).type_args().size(); ++k)
                pat_subst[einfo.type_params[k].name] = TypeRef(scrut_type).type_args()[k];
        }
        auto pat_field_type = [&](size_t idx) -> TypeRef {
            if (!vinfo || idx >= vinfo->payload_types.size()) return error_t();
            auto pt = vinfo->payload_types[idx];
            return pat_subst.empty() ? pt : subst_type_sema(pt, pat_subst);
        };
        // Synthesize a binding + guard for a refutable inner sub-pat. Returns
        // the synth binding name (caller stores it at the correct position
        // in `bindings`). Caller must also have `current_pat_refutable_guards_`
        // wired or guard generation is silently skipped (then the pattern
        // becomes too permissive — caller already errored).
        auto synth_refutable_inner =
            [&](TinyMapView sub, TypeRef ftype, std::string_view ctx_field) -> std::string {
            std::string synth = std::format(
                "__refut_{}_{}_{}", pvname, ctx_field, tmp_var_count_++);
            int32_t sc = code_of(sub);
            lir::LExprPtr value;
            if (sc == la::PAT_INT && sub.has_key(la::VALUE)) {
                auto sv = str_of(sub.get(la::VALUE.code));
                int64_t v = parse_int_literal(sv);
                value = builder().lit_int(v,
                    (ftype && TypeRef(ftype).kind() != LogosType::Kind::Error)
                        ? ftype
                        : prim(LogosType::Kind::I64));
            } else if (sc == la::PAT_NEG_INT && sub.has_key(la::VALUE)) {
                auto sv = str_of(sub.get(la::VALUE.code));
                int64_t v = -parse_int_literal(sv);
                value = builder().lit_int(v,
                    (ftype && TypeRef(ftype).kind() != LogosType::Kind::Error)
                        ? ftype
                        : prim(LogosType::Kind::I64));
            } else if (sc == la::PAT_BOOL && sub.has_key(la::VALUE)) {
                bool b = sub.get(la::VALUE.code).as_value<int32_t>() != 0;
                value = builder().lit_bool(b, bool_t());
            } else if (sc == la::PAT_CHAR && sub.has_key(la::VALUE)) {
                auto sv = str_of(sub.get(la::VALUE.code));
                int64_t v = sv.empty() ? 0 : (int64_t)(uint8_t)sv[0];
                value = builder().lit_int(v, prim(LogosType::Kind::Char));
            } else {
                return std::string();  // not a supported refutable
            }
            if (current_pat_refutable_guards_) {
                TypeRef rt = (ftype && TypeRef(ftype).kind() != LogosType::Kind::Error)
                    ? ftype
                    : (value ? value->type : error_t());
                auto vref = builder().var_ref(synth, rt);
                auto guard = builder().bin_op("==", std::move(vref),
                                              std::move(value), bool_t());
                current_pat_refutable_guards_->push_back(std::move(guard));
            }
            return synth;
        };
        if (pat_is_struct_shape) {
            // P4-pm-01: `E::V { x, y: pat, .. }` — read ITEMS as PAT_FIELD
            // list. Each entry carries NAME (+ optional VALUE sub-pat) or
            // is PAT_REST (`..`). Resolve names → positions in the
            // variant's payload_field_names; build positional `bindings`
            // (length = payload arity). Missing fields without `..` are
            // an error; with `..` they're skipped (bound to "_").
            if (vinfo && vinfo->payload_field_names.empty() && !vinfo->payload_types.empty()) {
                error(std::format("{}::{} is a tuple-shape variant — use parentheses",
                                  pename, pvname));
            }
            size_t arity = vinfo ? vinfo->payload_field_names.size() : 0;
            std::vector<std::string> by_pos(arity, "_");
            std::vector<bool> seen(arity, false);
            bool has_rest = false;
            if (pnode.has_key(la::ITEMS)) {
                AnyVal iav = pnode.get(la::ITEMS.code);
                if (!iav.is_null() && iav.is_pointer()) {
                    auto fl = map_of(iav);
                    ArrayView fitems;
                    if (fl.has_key(la::ITEMS)) fitems = arr_of(fl.get(la::ITEMS.code));
                    else                        fitems = arr_of(iav);
                    for (uint64_t i = 0; i < fitems.size(); ++i) {
                        auto fnode = map_of(fitems.get(i));
                        int32_t fcode = code_of(fnode);
                        if (fcode == la::PAT_REST) { has_rest = true; continue; }
                        std::string fname = fnode.has_key(la::NAME)
                            ? std::string(str_of(fnode.get(la::NAME.code)))
                            : std::string();
                        size_t idx = arity;
                        if (vinfo) {
                            for (size_t k = 0; k < arity; ++k)
                                if (vinfo->payload_field_names[k] == fname) { idx = k; break; }
                        }
                        if (idx == arity) {
                            if (vinfo)
                                error(std::format("pattern {}::{}: no field named '{}'",
                                      pename, pvname, fname));
                            continue;
                        }
                        if (seen[idx]) {
                            error(std::format("pattern {}::{}: field '{}' specified more than once",
                                  pename, pvname, fname));
                            continue;
                        }
                        seen[idx] = true;
                        // Inner pattern handling. Supported irrefutable shapes:
                        //   - no VALUE → shorthand: binding name = field name
                        //   - VALUE is PAT_WILD with NAME → bind to that name (or "_")
                        //   - VALUE is PAT_WILD without NAME → "_" skip
                        // Refutable inner (PAT_INT, PAT_VARIANT, ranges, …)
                        // is not yet supported here — parity with the
                        // tuple-shape PAT_VARIANT_DATA arm.
                        if (!fnode.has_key(la::VALUE)) {
                            by_pos[idx] = fname;  // shorthand
                            continue;
                        }
                        auto sub = map_of(fnode.get(la::VALUE.code));
                        int32_t sc = code_of(sub);
                        if (sc == la::PAT_WILD) {
                            std::string bn = sub.has_key(la::NAME)
                                ? std::string(str_of(sub.get(la::NAME.code)))
                                : std::string("_");
                            by_pos[idx] = bn;
                        } else if (sc == la::PAT_INT || sc == la::PAT_NEG_INT ||
                                   sc == la::PAT_BOOL || sc == la::PAT_CHAR) {
                            // P4-pm-01 refutable inner — synth a binding +
                            // emit an arm guard `__refut_… == <value>`.
                            std::string synth = synth_refutable_inner(
                                sub, pat_field_type(idx), fname);
                            if (synth.empty()) {
                                error(std::format(
                                    "pattern {}::{} field '{}': unsupported refutable "
                                    "literal sub-pattern",
                                    pename, pvname, fname));
                                by_pos[idx] = "_";
                            } else {
                                by_pos[idx] = std::move(synth);
                            }
                        } else {
                            error(std::format(
                                "pattern {}::{} field '{}': refutable inner "
                                "pattern not yet supported in struct-shape "
                                "variant patterns (use bind + body match)",
                                pename, pvname, fname));
                            by_pos[idx] = "_";
                        }
                    }
                }
            }
            if (vinfo && !has_rest) {
                std::vector<std::string> missing;
                for (size_t k = 0; k < arity; ++k)
                    if (!seen[k])
                        missing.push_back(vinfo->payload_field_names[k]);
                if (!missing.empty()) {
                    std::string list;
                    for (size_t k = 0; k < missing.size(); ++k) {
                        if (k) list += ", ";
                        list += "'" + missing[k] + "'";
                    }
                    error(std::format(
                        "pattern {}::{}: missing field(s): {} (use `..` to "
                        "skip remaining fields)",
                        pename, pvname, list));
                }
            }
            for (auto& s : by_pos) bindings.push_back(std::move(s));
        }
        if (!pat_is_struct_shape && pnode.has_key(la::ARGS)) {
            AnyVal aav = pnode.get(la::ARGS.code);
            if (!aav.is_null() && aav.is_pointer()) {
                auto blist = map_of(aav);
                if (blist.has_key(la::ITEMS)) {
                    auto bitems = arr_of(blist.get(la::ITEMS.code));
                    for (uint64_t j = 0; j < bitems.size(); ++j) {
                        auto bnode = map_of(bitems.get(j));
                        // B-pt-04: variant-payload args now parse as full
                        // patterns, but only PAT_WILD bindings (or PAT_UNIT
                        // skip) are codegen'd today.  Anything else (struct,
                        // tuple, nested variant, …) emits a diagnostic
                        // until the match-lowering supports nested guards.
                        int32_t bc = code_of(bnode);
                        if (bc == la::PAT_UNIT) continue;  // () unit — no binding
                        if (bc == la::PAT_WILD) {
                            if (!bnode.has_key(la::NAME)) continue;
                            bindings.push_back(std::string(str_of(bnode.get(la::NAME.code))));
                            continue;
                        }
                        // P4-pm-02: nested struct/tuple pattern inside
                        // variant payload. Synth a payload slot binding;
                        // the arm-body builder (which sees
                        // current_pat_nested_subs_) emits an irrefutable
                        // destructure `let <sub_pat> = __synth;` as a
                        // body prologue. Refutable sub-patterns (nested
                        // variant, range, …) still aren't supported here
                        // — they need a nested-guard scheme.
                        bool sub_is_irrefutable =
                            (bc == la::PAT_STRUCT || bc == la::PAT_TUPLE);
                        if (sub_is_irrefutable && current_pat_nested_subs_) {
                            std::string synth = std::format(
                                "__pat_pld_{}_{}", pvname, tmp_var_count_++);
                            bindings.push_back(synth);
                            current_pat_nested_subs_->push_back({synth, bnode});
                            continue;
                        }
                        // P4-pm-01 refutable inner (tuple-shape parallel) —
                        // `Option::Some(1)` / `Result::Err(false)`. Synth a
                        // binding + emit `__refut_… == <value>` as an arm
                        // guard.
                        if (bc == la::PAT_INT || bc == la::PAT_NEG_INT ||
                            bc == la::PAT_BOOL || bc == la::PAT_CHAR) {
                            std::string synth = synth_refutable_inner(
                                bnode, pat_field_type(j),
                                std::format("{}", j));
                            if (!synth.empty()) {
                                bindings.push_back(std::move(synth));
                                continue;
                            }
                        }
                        error(std::format(
                            "pattern {}::{}: nested patterns inside enum-variant "
                            "payloads are not yet supported; bind to a name and "
                            "match in the body",
                            pename, pvname));
                    }
                }
            }
        }
        std::vector<TypeRef> binding_types;
        if (vinfo) {
            SemaSubst subst;
            // L5: auto-deref `&Enum<T>` / `&mut Enum<T>` / `*const/mut Enum<T>`
            // to the inner Enum for type-arg substitution. The match scrut
            // already gets auto-deref'd at codegen; the type-arg propagation
            // for binding types needs the same unwrap so `match &opt {
            // Some(ref v) => *v }` over `&Option<i64>` binds `v: &i64`
            // (and `*v` → `i64`) instead of `v: T` (typevar).
            TypeRef enum_scrut = scrut_type;
            if (enum_scrut &&
                (TypeRef(enum_scrut).kind() == LogosType::Kind::Ref ||
                 TypeRef(enum_scrut).kind() == LogosType::Kind::MutRef ||
                 TypeRef(enum_scrut).kind() == LogosType::Kind::Ptr) &&
                TypeRef(enum_scrut).pointee())
                enum_scrut = TypeRef(enum_scrut).pointee();
            if (enum_scrut &&
                TypeRef(enum_scrut).kind() == LogosType::Kind::Enum &&
                !TypeRef(enum_scrut).type_args().empty()) {
                auto& einfo = eit->second;
                for (size_t k = 0; k < einfo.type_params.size() &&
                                    k < TypeRef(enum_scrut).type_args().size(); ++k)
                    subst[einfo.type_params[k].name] = TypeRef(enum_scrut).type_args()[k];
            }
            for (auto pt : vinfo->payload_types) {
                auto ct = subst.empty() ? pt : subst_type_sema(pt, subst);
                if (TypeRef(ct).kind() == LogosType::Kind::Void) continue;  // () unit — no field
                binding_types.push_back(ct);
            }
        }
        // S8-en-03: a single `_` placeholder against a unit-payload
        // variant (`Either::Right(_)` where `U == ()`) is accepted as
        // the bare-variant form. `binding_types` filters Void out, so
        // a `Right<U=()>` ends up with 0 expected bindings; if the
        // user supplied exactly one `_`, drop it silently.
        if (binding_types.empty() && bindings.size() == 1 && bindings[0] == "_")
            bindings.clear();
        if (bindings.size() != binding_types.size())
            error(std::format("pattern {}::{}: expected {} bindings, got {}",
                  pename, pvname, binding_types.size(), bindings.size()));
        auto mo = lir_mirror_emit_pat_variant_data(
            *cur_prog_, pename, pvname, disc, bindings, binding_types);
        lir::Pattern p_;
        p_.mirror_offset_ = mo;
        return p_;
    }
    if (pc == la::PAT_FLOAT) {
        // B-pt-06: parse but reject — IEEE-equality patterns need a
        // language-level decision before we wire them through codegen.
        error("float-literal patterns are not yet supported "
              "(IEEE equality semantics undecided)");
        lir::Pattern p_;
        p_.mirror_offset_ = lir_mirror_emit_pat_wild(*cur_prog_, "_");
        return p_;
    }
    if (pc == la::PAT_BYTES) {
        // P4-pm-07: `b"foo"` lowers to a PatSlice of PatInt sub-patterns,
        // reusing the array-prefix slice-pattern codegen. Scrutinee must
        // be a fixed-size `[u8; N]` array (matches Rust's
        // `&[u8; N]` const-pattern semantics when the ref-pat layer is
        // skipped). Dynamic `&[u8]` scrutinees are still future work
        // (length check + memcmp).
        auto sv = str_of(pnode.get(la::VALUE.code));
        std::vector<uint8_t> bytes;
        if (sv.size() >= 3 && sv.front() == 'b' && sv[1] == '"' && sv.back() == '"') {
            std::string_view body = sv.substr(2, sv.size() - 3);
            for (size_t i = 0; i < body.size(); ) {
                unsigned char c = (unsigned char)body[i];
                if (c == '\\' && i + 1 < body.size()) {
                    char e = body[i + 1];
                    uint8_t b = 0;
                    switch (e) {
                        case 'n':  b = '\n'; break;
                        case 't':  b = '\t'; break;
                        case 'r':  b = '\r'; break;
                        case '0':  b = 0;    break;
                        case '\\': b = '\\'; break;
                        case '\'': b = '\''; break;
                        case '"':  b = '"';  break;
                        case 'x': {
                            if (i + 3 < body.size()) {
                                auto hex = [](char c) -> int {
                                    if (c >= '0' && c <= '9') return c - '0';
                                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                                    return -1;
                                };
                                int hi = hex(body[i + 2]), lo = hex(body[i + 3]);
                                if (hi >= 0 && lo >= 0) {
                                    b = (uint8_t)((hi << 4) | lo);
                                    bytes.push_back(b);
                                    i += 4;
                                    continue;
                                }
                            }
                            error(std::format(
                                "byte-string pattern: malformed \\x escape in '{}'", sv));
                            i += 2; continue;
                        }
                        default:
                            error(std::format(
                                "byte-string pattern: unknown escape '\\{}' in '{}'",
                                e, sv));
                            i += 2; continue;
                    }
                    bytes.push_back(b);
                    i += 2;
                } else {
                    bytes.push_back(c);
                    ++i;
                }
            }
        } else {
            error(std::format("byte-string pattern: malformed literal '{}'", sv));
        }
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error) {
            auto sk = TypeRef(scrut_type).kind();
            bool ok = false;
            if (sk == LogosType::Kind::Array && TypeRef(scrut_type).elem() &&
                TypeRef(scrut_type).elem().kind() == LogosType::Kind::U8) {
                ok = true;
                size_t n = (size_t)TypeRef(scrut_type).arr_size();
                if (n != bytes.size())
                    error(std::format(
                        "byte-string pattern: literal length {} does not match "
                        "scrutinee array length {}", bytes.size(), n));
            }
            if (!ok)
                error(std::format(
                    "byte-string pattern requires `[u8; N]` scrutinee, got '{}'",
                    type_str(scrut_type)));
        }
        std::vector<lir::Pattern> prefix;
        for (auto b : bytes) {
            lir::Pattern sp;
            sp.mirror_offset_ = lir_mirror_emit_pat_int(*cur_prog_, (int64_t)b);
            prefix.push_back(std::move(sp));
        }
        std::vector<lir::Pattern> rest;     // empty — exact match, no `..`
        std::vector<lir::Pattern> suffix;   // empty
        auto mo = lir_mirror_emit_pat_slice(*cur_prog_, prefix, rest, suffix);
        lir::Pattern p_;
        p_.mirror_offset_ = mo;
        return p_;
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
        lir::Pattern p_;
        p_.mirror_offset_ = lir_mirror_emit_pat_int(*cur_prog_, v);
        return p_;
    }
    // ── PAT_CHAR / PAT_CHAR_RANGE: 'X' / 'a' ..= 'z' ───────────────────────
    // Decode CHAR_LIT to its Unicode scalar value and lower as an
    // integer pattern (Logos char is a 4-byte Unicode scalar so the
    // u32 equality / range comparison works directly).
    auto decode_char_lit = [&](std::string_view sv) -> int64_t {
        if (sv.size() < 3 || sv.front() != '\'' || sv.back() != '\'') {
            error(std::format("malformed char literal '{}'", sv));
            return 0;
        }
        std::string_view body = sv.substr(1, sv.size() - 2);
        if (!body.empty() && body[0] == '\\') {
            if (body.size() < 2) {
                error(std::format("malformed char literal '{}'", sv));
                return 0;
            }
            switch (body[1]) {
                case 'n': return '\n';
                case 't': return '\t';
                case 'r': return '\r';
                case '0': return 0;
                case '\\': return '\\';
                case '\'': return '\'';
                case '"': return '"';
                default:
                    error(std::format("char literal '{}': unknown escape '\\{}'",
                          sv, body[1]));
                    return 0;
            }
        }
        unsigned char c0 = (unsigned char)body[0];
        if (c0 < 0x80) return (int64_t)c0;
        int64_t cp = 0;
        int nbytes = 0;
        if      ((c0 & 0xE0) == 0xC0) { cp = c0 & 0x1F; nbytes = 2; }
        else if ((c0 & 0xF0) == 0xE0) { cp = c0 & 0x0F; nbytes = 3; }
        else if ((c0 & 0xF8) == 0xF0) { cp = c0 & 0x07; nbytes = 4; }
        else { error(std::format("char literal '{}': invalid UTF-8", sv)); return 0; }
        if ((int)body.size() < nbytes) {
            error(std::format("char literal '{}': truncated UTF-8", sv));
            return 0;
        }
        for (int i = 1; i < nbytes; ++i)
            cp = (cp << 6) | ((unsigned char)body[i] & 0x3F);
        return cp;
    };
    if (pc == la::PAT_CHAR) {
        auto sv = str_of(pnode.get(la::VALUE.code));
        int64_t v = decode_char_lit(sv);
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error) {
            auto sk = TypeRef(scrut_type).kind();
            if (sk != LogosType::Kind::Char && !is_integer(scrut_type))
                error(std::format("char pattern requires char or integer scrutinee, got '{}'",
                      type_str(scrut_type)));
        }
        lir::Pattern p_;
        p_.mirror_offset_ = lir_mirror_emit_pat_int(*cur_prog_, v);
        return p_;
    }
    if (pc == la::PAT_CHAR_RANGE) {
        auto lo_sv = str_of(pnode.get(la::LHS.code));
        auto hi_sv = str_of(pnode.get(la::RHS.code));
        int64_t lo = decode_char_lit(lo_sv);
        int64_t hi = decode_char_lit(hi_sv);
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error) {
            auto sk = TypeRef(scrut_type).kind();
            if (sk != LogosType::Kind::Char && !is_integer(scrut_type))
                error(std::format("char range pattern requires char or integer scrutinee, got '{}'",
                      type_str(scrut_type)));
        }
        if (lo > hi)
            error(std::format("char range pattern: lo ({}) > hi ({})", lo, hi));
        lir::Pattern p_;
        p_.mirror_offset_ = lir_mirror_emit_pat_range(*cur_prog_, lo, hi);
        return p_;
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
        namespace ps = lir_schema::pat;
        // P4-pm-25: skip synth bindings introduced by P4-pm-01's refutable
        // inner mechanism (`__refut_*`) and P4-pm-02's nested-pat synth
        // (`__pat_pld_*`). They're per-alt unique by construction and would
        // spuriously fail the same-name-set check.
        auto is_synth = [](std::string_view n) {
            return n.starts_with("__refut_") || n.starts_with("__pat_pld_") ||
                   n.starts_with("__sve_");
        };
        std::function<void(lir_view::PatRef, std::vector<std::string>&)> collect_names;
        collect_names = [&](lir_view::PatRef pr, std::vector<std::string>& out) {
            if (!pr) return;
            auto k = pr.kind();
            if (k == ps::Code::Wild) {
                lir_view::PatWildView v{pr}; auto n = v.name();
                if (n != "_" && !is_synth(n)) out.emplace_back(n);
            } else if (k == ps::Code::At) {
                lir_view::PatAtView v{pr}; auto n = v.name();
                if (n != "_" && !is_synth(n)) out.emplace_back(n);
                if (auto sub = v.sub()) collect_names(sub, out);
            } else if (k == ps::Code::Tuple) {
                lir_view::PatTupleView v{pr};
                v.each_binding([&](std::string_view n) {
                    if (n != "_" && !is_synth(n)) out.emplace_back(n);
                });
            } else if (k == ps::Code::Struct) {
                lir_view::PatStructView v{pr};
                v.each_field([&](lir_view::PatFieldBindingView fv) {
                    auto sub = fv.sub();
                    if (sub) collect_names(sub, out);
                    else if (!is_synth(fv.field_name())) out.emplace_back(fv.field_name());
                });
            } else if (k == ps::Code::VariantData) {
                lir_view::PatVariantDataView v{pr};
                v.each_binding([&](std::string_view n) {
                    if (n != "_" && !is_synth(n)) out.emplace_back(n);
                });
            } else if (k == ps::Code::Or) {
                lir_view::PatOrView v{pr};
                bool first = true;
                v.each_alt([&](lir_view::PatRef alt) {
                    if (first) { collect_names(alt, out); first = false; }
                });
            } else if (k == ps::Code::RefBind) {
                lir_view::PatRefBindView v{pr}; auto n = v.name();
                if (!n.empty() && n != "_") out.emplace_back(n);
            } else if (k == ps::Code::RefPat) {
                lir_view::PatRefPatView v{pr};
                if (auto inner = v.inner()) collect_names(inner, out);
            } else if (k == ps::Code::Slice) {
                lir_view::PatSliceView v{pr};
                v.each_prefix([&](lir_view::PatRef p) { collect_names(p, out); });
                v.each_rest  ([&](lir_view::PatRef p) { collect_names(p, out); });
                v.each_suffix([&](lir_view::PatRef p) { collect_names(p, out); });
            }
        };
        if (!por.alts.empty()) {
            std::vector<std::string> first_names;
            collect_names(pat_ref_of(por.alts[0]), first_names);
            std::sort(first_names.begin(), first_names.end());
            for (size_t i = 1; i < por.alts.size(); ++i) {
                std::vector<std::string> alt_names;
                collect_names(pat_ref_of(por.alts[i]), alt_names);
                std::sort(alt_names.begin(), alt_names.end());
                if (alt_names != first_names)
                    error(std::format("or-pattern: all alternatives must bind the same variable names"));
            }
        }
        auto mo = lir_mirror_emit_pat_or(*cur_prog_, por.alts);
        lir::Pattern p_;
        p_.mirror_offset_ = mo;
        return p_;
    }
    if (pc == la::PAT_BOOL) {
        AnyVal bv = pnode.get(la::VALUE.code);
        bool bval = !bv.is_null() && bv.is_value() && bv.as_value<uint8_t>();
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error &&
            TypeRef(scrut_type).kind() != LogosType::Kind::Bool)
            error(std::format("bool pattern requires bool scrutinee, got '{}'",
                  type_str(scrut_type)));
        lir::Pattern p_;
        p_.mirror_offset_ = lir_mirror_emit_pat_bool(*cur_prog_, bval);
        return p_;
    }
    if (pc == la::PAT_TUPLE) {
        // Tuple pattern: (a, b, c) — irrefutable, binds each element.
        // Scrutinee must be a tuple type.
        lir::PatTuple pt;
        if (!scrut_type || TypeRef(scrut_type).kind() != LogosType::Kind::Tuple) {
            error(std::format("tuple pattern requires tuple scrutinee, got {}",
                  scrut_type ? type_str(scrut_type) : "?"));
            lir::Pattern pw_;
            pw_.mirror_offset_ = lir_mirror_emit_pat_wild(*cur_prog_, "_");
            return pw_;
        }
        // P4-pm-20: tuple pattern may contain a single `..` (PAT_REST)
        // skip-marker. We expand it into the appropriate number of
        // PAT_WILD `_` skip entries so the underlying PatTuple LIR
        // keeps its fixed-arity layout: `(a, b, ..)` over `(T1, T2,
        // T3)` becomes `(a, b, _)`; `(.., b, c)` becomes `(_, b, c)`;
        // `(a, .., c)` with arity 4 becomes `(a, _, _, c)`.
        size_t tuple_arity = TypeRef(scrut_type).tuple_elems().size();
        AnyVal items_av = pnode.get(la::ITEMS.code);
        std::vector<hermes::TinyMapView> raw_elems;
        size_t rest_pos = SIZE_MAX;
        if (!items_av.is_null() && items_av.is_pointer()) {
            auto items_arr = arr_of(items_av);
            for (uint64_t i = 0; i < items_arr.size(); ++i) {
                auto sub = map_of(items_arr.get(i));
                if (code_of(sub) == la::PAT_REST) {
                    if (rest_pos != SIZE_MAX) {
                        error("tuple pattern: only one `..` rest allowed");
                        continue;
                    }
                    rest_pos = raw_elems.size();
                    continue;
                }
                raw_elems.push_back(sub);
            }
        }
        // Build the final element list. If rest is present, pad with
        // PAT_WILD("_") at rest_pos to reach tuple_arity.
        std::vector<std::optional<hermes::TinyMapView>> expanded;
        if (rest_pos == SIZE_MAX) {
            for (auto& e : raw_elems) expanded.push_back(e);
        } else {
            if (raw_elems.size() > tuple_arity)
                error(std::format(
                    "tuple pattern: {} explicit elements + `..` exceed "
                    "tuple arity {}", raw_elems.size(), tuple_arity));
            size_t pad = tuple_arity > raw_elems.size() ? tuple_arity - raw_elems.size() : 0;
            for (size_t i = 0; i < rest_pos; ++i)        expanded.push_back(raw_elems[i]);
            for (size_t i = 0; i < pad; ++i)             expanded.push_back(std::nullopt);
            for (size_t i = rest_pos; i < raw_elems.size(); ++i) expanded.push_back(raw_elems[i]);
        }
        for (size_t i = 0; i < expanded.size(); ++i) {
            TypeRef elem_ty = nullptr;
            if (i < tuple_arity)
                elem_ty = TypeRef(scrut_type).tuple_elems()[i];
            if (!expanded[i].has_value()) {
                // Synth `_` skip from rest expansion.
                pt.bindings.push_back("_");
                pt.subs.push_back(make_pat_wild("_"));
                continue;
            }
            auto sub = *expanded[i];
            int32_t sc = code_of(sub);
            if (sc == la::PAT_WILD.code) {
                auto nm = std::string(str_of(sub.get(la::NAME.code)));
                pt.bindings.push_back(nm);
                pt.subs.push_back(make_pat_wild(nm));
            } else if (sc == la::PAT_INT.code || sc == la::PAT_NEG_INT.code ||
                       sc == la::PAT_BOOL.code || sc == la::PAT_RANGE.code) {
                pt.bindings.push_back("_");
                pt.subs.push_back(build_pattern(sub, elem_ty));
            } else if (sc == la::PAT_VARIANT_DATA.code) {
                // P4-pm-24: variant pattern at tuple-pattern element
                // (e.g. `(Enum::Foo {..}, Enum::Bar { bar: _ })`).
                // Recurse to build a full PatVariantData sub; mlir-gen
                // tuple-arm dispatch emits a disc check against the
                // tuple element (auto-derefs through the enum-pointer
                // layout). Bindings inside the variant sub are propagated
                // to outer scope by bind_pattern_ref's recursive Tuple
                // case below.
                pt.bindings.push_back("_");
                pt.subs.push_back(build_pattern(sub, elem_ty));
            } else if (sc == la::PAT_OR.code) {
                // P4-pm-03: or-pattern as tuple element. Grammar always
                // emits PAT_OR (even for a single sub-pattern with no
                // PIPE); unwrap the trivial case so a bare `n` /
                // `_`-shape is treated as a normal binding/wildcard
                // (the multi-alt path drops bindings — alts must be
                // scalar). Multi-alt: emit PatOr LIR; mlir-gen
                // tuple-arm dispatch OR-chains the per-alt disc tests.
                bool single = false;
                if (sub.has_key(la::ITEMS)) {
                    auto arr = arr_of(sub.get(la::ITEMS.code));
                    if (arr.size() == 1) {
                        auto inner = map_of(arr.get(0));
                        int32_t isc = code_of(inner);
                        if (isc == la::PAT_WILD.code) {
                            auto nm = inner.has_key(la::NAME)
                                ? std::string(str_of(inner.get(la::NAME.code)))
                                : std::string("_");
                            pt.bindings.push_back(nm);
                            pt.subs.push_back(make_pat_wild(nm));
                            single = true;
                        } else if (isc == la::PAT_INT.code ||
                                   isc == la::PAT_NEG_INT.code ||
                                   isc == la::PAT_BOOL.code ||
                                   isc == la::PAT_RANGE.code) {
                            pt.bindings.push_back("_");
                            pt.subs.push_back(build_pattern(inner, elem_ty));
                            single = true;
                        } else if (isc == la::PAT_VARIANT_DATA.code) {
                            pt.bindings.push_back("_");
                            pt.subs.push_back(build_pattern(inner, elem_ty));
                            single = true;
                        }
                    }
                }
                if (!single) {
                    pt.bindings.push_back("_");
                    pt.subs.push_back(build_pattern(sub, elem_ty));
                }
            } else {
                error("tuple pattern element: only _, name, integer, bool, range, "
                      "or variant patterns are supported");
                pt.bindings.push_back("_");
                pt.subs.push_back(make_pat_wild("_"));
            }
        }
        // Verify count matches tuple arity.
        if (pt.bindings.size() != tuple_arity)
            error(std::format("tuple pattern: expected {} elements, got {}",
                  tuple_arity, pt.bindings.size()));
        // Fill binding types from tuple elements.
        for (size_t i = 0; i < TypeRef(scrut_type).tuple_elems().size(); ++i)
            pt.binding_types.push_back(TypeRef(scrut_type).tuple_elems()[i]);
        auto mo = lir_mirror_emit_pat_tuple(*cur_prog_, pt.bindings, pt.binding_types, pt.subs);
        lir::Pattern p_;
        p_.mirror_offset_ = mo;
        return p_;
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
        // P4-pm-22: exclusive range `lo..hi` lowers as inclusive
        // `lo..=(hi-1)` since the PatRange mirror has no inclusive
        // flag. The grammar tags inclusive arms with INCLUSIVE: true,
        // exclusive arms with INCLUSIVE: false; default (no key) is
        // treated as inclusive for backwards compat.
        bool inclusive = true;
        if (pnode.has_key(la::INCLUSIVE)) {
            AnyVal av = pnode.get(la::INCLUSIVE.code);
            if (!av.is_null() && av.is_value()) inclusive = av.as_value<uint8_t>() != 0;
        }
        if (!inclusive) {
            if (lo >= hi) {
                error(std::format("exclusive range pattern: lo ({}) >= hi ({}) (empty range)",
                      lo, hi));
            }
            hi = hi - 1;
        } else if (lo > hi) {
            error(std::format("range pattern: lo ({}) > hi ({})", lo, hi));
        }
        lir::Pattern p_;
        p_.mirror_offset_ = lir_mirror_emit_pat_range(*cur_prog_, lo, hi);
        return p_;
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
        auto mo = lir_mirror_emit_pat_at(*cur_prog_, pa.name, pa.sub, pa.type);
        lir::Pattern p_;
        p_.mirror_offset_ = mo;
        return p_;
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
        auto mo = lir_mirror_emit_pat_ref_pat(*cur_prog_, prp.inner, prp.is_mut);
        lir::Pattern p_;
        p_.mirror_offset_ = mo;
        return p_;
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
            lir::Pattern p_;
            p_.mirror_offset_ = lir_mirror_emit_pat_ref_bind(*cur_prog_, bname, is_mut, btype);
            return p_;
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
        auto mo = lir_mirror_emit_pat_struct(*cur_prog_, ps.struct_name, ps.fields, ps.has_rest);
        lir::Pattern p_;
        p_.mirror_offset_ = mo;
        return p_;
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
                            psl.rest.push_back(make_pat_wild("_"));
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
        auto mo = lir_mirror_emit_pat_slice(*cur_prog_, psl.prefix, psl.rest, psl.suffix);
        lir::Pattern p_;
        p_.mirror_offset_ = mo;
        return p_;
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
        lir::Pattern pw_;
        pw_.mirror_offset_ = lir_mirror_emit_pat_wild(*cur_prog_, "_");
        return pw_;
    }

    // PAT_WILD or fallback
    auto wname = std::string(str_of(pnode.get(la::NAME.code)));
    // P4-pm-06: bare ident in pattern that resolves to a module-const ⇒
    // treat as a value pattern (PAT_INT / PAT_BOOL / PAT_CHAR), not as a
    // fresh binding. ctfe-eval the const's RHS once; emit the matching
    // scalar pattern. Non-scalar consts (str, hermes, struct) stay
    // diagnosed — needs string-pattern codegen, separate slice.
    if (wname != "_") {
        auto cvit = module_const_values_.find(wname);
        if (cvit != module_const_values_.end()) {
            auto r = ctfe::eval_expr(cvit->second, holder_);
            if (r) {
                auto cv = std::move(r).value();
                using K = LogosType::Kind;
                if (cv.kind == K::Bool) {
                    lir::Pattern p_;
                    p_.mirror_offset_ = lir_mirror_emit_pat_bool(*cur_prog_, cv.b);
                    return p_;
                }
                if (cv.kind == K::I8 || cv.kind == K::I16 || cv.kind == K::I32 ||
                    cv.kind == K::I64 || cv.kind == K::Isize ||
                    cv.kind == K::U8 || cv.kind == K::U16 || cv.kind == K::U32 ||
                    cv.kind == K::U64 || cv.kind == K::Usize ||
                    cv.kind == K::IntLit || cv.kind == K::Char) {
                    lir::Pattern p_;
                    p_.mirror_offset_ = lir_mirror_emit_pat_int(*cur_prog_, cv.i);
                    return p_;
                }
                // P4-pm-06 str-typed const-pattern. CtfeValue reports
                // `K::Slice` for str literals (str == Slice<u8>).
                // Synthesize a `__str_<n>` binding + push
                // `str_eq(__str_<n>, CONST)` into the refutable-guard
                // side channel. The arm builder ANDs it into the arm's
                // guard.
                bool scrut_is_str =
                    TypeRef(scrut_type).kind() == LogosType::Kind::Slice &&
                    TypeRef(scrut_type).elem() &&
                    TypeRef(scrut_type).elem().kind() == LogosType::Kind::U8;
                // P4-pm-07: byte-array const pattern. ctfe doesn't yet
                // produce array values, but we can still match against
                // the const by name. Detect `[u8; N]`-typed consts via
                // `module_consts_` lookup; synth a `__byte_<n>` binding
                // + emit element-wise AND-chain `__byte_<n>[i] == CONST[i]`
                // as the refutable-inner guard.
                if (TypeRef(scrut_type).kind() == LogosType::Kind::Array &&
                    TypeRef(scrut_type).elem() &&
                    TypeRef(scrut_type).elem().kind() == LogosType::Kind::U8 &&
                    current_pat_refutable_guards_) {
                    auto cit = module_consts_.find(wname);
                    if (cit != module_consts_.end() &&
                        TypeRef(cit->second).kind() == LogosType::Kind::Array &&
                        TypeRef(cit->second).elem().kind() == LogosType::Kind::U8 &&
                        TypeRef(cit->second).arr_size() ==
                            TypeRef(scrut_type).arr_size()) {
                        size_t arr_n = (size_t)TypeRef(scrut_type).arr_size();
                        std::string syn = std::format(
                            "__byte_{}", tmp_var_count_++);
                        auto u8t = prim(LogosType::Kind::U8);
                        auto i64t = prim(LogosType::Kind::I64);
                        lir::LExprPtr guard;
                        for (size_t k = 0; k < arr_n; ++k) {
                            auto lhs = builder().slice_index(
                                builder().var_ref(syn, scrut_type),
                                builder().lit_int((int64_t)k, i64t), u8t);
                            auto rhs = builder().slice_index(
                                builder().var_ref(wname, scrut_type),
                                builder().lit_int((int64_t)k, i64t), u8t);
                            auto eq = builder().bin_op(
                                "==", std::move(lhs), std::move(rhs), bool_t());
                            if (!guard) {
                                guard = std::move(eq);
                            } else {
                                guard = builder().bin_op(
                                    "&&", std::move(guard), std::move(eq), bool_t());
                            }
                        }
                        if (!guard) guard = builder().lit_bool(true, bool_t());
                        current_pat_refutable_guards_->push_back(std::move(guard));
                        lir::Pattern p_;
                        p_.mirror_offset_ = lir_mirror_emit_pat_wild(*cur_prog_, syn);
                        return p_;
                    }
                }
                if (cv.kind == K::Slice && scrut_is_str &&
                    current_pat_refutable_guards_) {
                    auto cands = find_func_candidates("str_eq");
                    const SemaFuncInfo* fi = nullptr;
                    for (auto* c : cands)
                        if (c->param_types.size() == 2) { fi = c; break; }
                    if (!fi) {
                        error("str-const pattern needs stdlib `str_eq`; "
                              "`use std.lang.text.string;` (or rely on the "
                              "default prelude)");
                    } else {
                        std::string syn = std::format(
                            "__str_{}", tmp_var_count_++);
                        auto vref = builder().var_ref(syn, scrut_type);
                        auto cref = builder().var_ref(wname, scrut_type);
                        std::vector<lir::LExprPtr> args;
                        args.push_back(std::move(vref));
                        args.push_back(std::move(cref));
                        std::string sym = fi->symbol_name.empty()
                            ? std::string("str_eq") : fi->symbol_name;
                        auto guard = builder().call(
                            sym, {}, std::move(args), bool_t());
                        current_pat_refutable_guards_->push_back(std::move(guard));
                        lir::Pattern p_;
                        p_.mirror_offset_ = lir_mirror_emit_pat_wild(*cur_prog_, syn);
                        return p_;
                    }
                }
                error(std::format(
                    "const '{}' has non-scalar type — only int/bool/char "
                    "consts are supported in patterns today (or `str` with "
                    "P4-pm-06 — needs `current_pat_refutable_guards_` channel)",
                    wname));
            } else {
                error(std::format(
                    "const '{}' in pattern position: initializer is not "
                    "ctfe-evaluable", wname));
            }
        }
    }
    // P4-pm-12: `mut x` pattern — record the name in the side-channel
    // so `bind_pattern_ref` redefines it as mutable. PatWild's LIR
    // mirror doesn't carry the mut flag yet.
    if (current_pat_mut_names_ && wname != "_" && pnode.has_key(la::IS_MUT)) {
        AnyVal mv = pnode.get(la::IS_MUT.code);
        if (!mv.is_null() && mv.is_value() && mv.as_value<uint8_t>() != 0)
            current_pat_mut_names_->insert(wname);
    }
    lir::Pattern p_;
    p_.mirror_offset_ = lir_mirror_emit_pat_wild(*cur_prog_, wname);
    return p_;
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
        out_stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
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
                    lir::LExprPtr sub = nullptr;
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
        lir::LExprPtr acc = nullptr;
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
    bind_pattern_ref(pat_ref_of(pat), scrut_type);
}

void SemaChecker::bind_pattern_ref(lir_view::PatRef pr, TypeRef scrut_type) {
    if (!pr) return;
    namespace ps = lir_schema::pat;
    auto k = pr.kind();
    auto* pool = cur_prog_->type_pool.impl();
    if (k == ps::Code::VariantData) {
        lir_view::PatVariantDataView v{pr};
        std::vector<std::string_view> names;
        std::vector<TypeRef> types;
        v.each_binding([&](std::string_view n) { names.push_back(n); });
        v.each_binding_type(pool, [&](TypeRef t) { types.push_back(t); });
        for (size_t i = 0; i < names.size() && i < types.size(); ++i)
            define(std::string(names[i]), types[i]);
    } else if (k == ps::Code::Tuple) {
        lir_view::PatTupleView v{pr};
        std::vector<std::string_view> names;
        std::vector<TypeRef> types;
        v.each_binding([&](std::string_view n) { names.push_back(n); });
        v.each_binding_type(pool, [&](TypeRef t) { types.push_back(t); });
        for (size_t i = 0; i < names.size() && i < types.size(); ++i)
            if (names[i] != "_")
                define(std::string(names[i]), types[i]);
        // P4-pm-24: recurse into refutable variant sub-patterns so any
        // payload bindings inside (`(E::Foo { x }, _)`) reach the outer
        // arm scope.
        size_t idx = 0;
        v.each_sub([&](lir_view::PatRef sp) {
            if (sp && sp.kind() == ps::Code::VariantData) {
                TypeRef sub_t = idx < types.size() ? types[idx] : error_t();
                bind_pattern_ref(sp, sub_t);
            }
            ++idx;
        });
    } else if (k == ps::Code::Wild) {
        lir_view::PatWildView v{pr};
        auto n = v.name();
        if (n != "_" && scrut_type) {
            // P4-pm-12: `mut x` patterns flagged via current_pat_mut_names_.
            bool is_mut = current_pat_mut_names_ &&
                          current_pat_mut_names_->count(std::string(n));
            define(std::string(n), scrut_type, is_mut);
        }
    } else if (k == ps::Code::RefBind) {
        lir_view::PatRefBindView v{pr};
        define(std::string(v.name()), v.bind_type(pool));
    } else if (k == ps::Code::At) {
        lir_view::PatAtView v{pr};
        TypeRef ty = v.type(pool);
        auto n = v.name();
        if (ty && n != "_") define(std::string(n), ty);
        if (auto sub = v.sub()) bind_pattern_ref(sub, ty);
    } else if (k == ps::Code::RefPat) {
        lir_view::PatRefPatView v{pr};
        TypeRef inner_t = error_t();
        if (scrut_type && (TypeRef(scrut_type).kind() == LogosType::Kind::Ref ||
                           TypeRef(scrut_type).kind() == LogosType::Kind::MutRef) &&
            TypeRef(scrut_type).pointee())
            inner_t = TypeRef(scrut_type).pointee();
        if (auto inner = v.inner()) bind_pattern_ref(inner, inner_t);
    } else if (k == ps::Code::Struct) {
        lir_view::PatStructView v{pr};
        auto sname = std::string(v.struct_name());
        const SemaStructInfo* sinfo = nullptr;
        auto sit = structs_.find(sname);
        if (sit != structs_.end()) sinfo = &sit->second;
        else {
            auto dit = datatypes_.find(sname);
            if (dit != datatypes_.end()) sinfo = &dit->second;
        }
        v.each_field([&](lir_view::PatFieldBindingView fv) {
            auto fname = fv.field_name();
            TypeRef ftype = error_t();
            if (sinfo)
                for (auto& f : sinfo->fields)
                    if (f.name == fname) { ftype = f.type; break; }
            auto sub = fv.sub();
            if (!sub) define(std::string(fname), ftype);
            else bind_pattern_ref(sub, ftype);
        });
    } else if (k == ps::Code::Slice) {
        lir_view::PatSliceView v{pr};
        TypeRef elem_t = (scrut_type && TypeRef(scrut_type).elem())
                          ? TypeRef(scrut_type).elem() : error_t();
        v.each_prefix([&](lir_view::PatRef p) { bind_pattern_ref(p, elem_t); });
        v.each_rest  ([&](lir_view::PatRef p) { bind_pattern_ref(p, elem_t); });
        v.each_suffix([&](lir_view::PatRef p) { bind_pattern_ref(p, elem_t); });
    } else if (k == ps::Code::Or) {
        lir_view::PatOrView v{pr};
        bool first = true;
        v.each_alt([&](lir_view::PatRef alt) {
            if (first) { bind_pattern_ref(alt, scrut_type); first = false; }
        });
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
        lir::LBlockPtr then_body = lir::alloc_block(*cur_prog_);
        if (node.has_key(la::THEN))
            *then_body = lower_block(map_of(node.get(la::THEN.code)));
        pop_scope();

        // Else arm: wildcard → else block (or empty)
        lir::LBlockPtr else_body = lir::alloc_block(*cur_prog_);
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
        sm.arms.push_back({make_pat_wild("_"), std::move(else_body), std::nullopt});
        return make_stmt_emit(node_line_, std::move(sm));
    }

    // ── regular if cond { ... } ────────────────────────────────────
    lir::LExprPtr cond = nullptr;
    if (node.has_key(la::COND)) {
        cond = lower_expr(map_of(node.get(la::COND.code)));
        if (TypeRef(cond->type).kind() != LogosType::Kind::Bool &&
            TypeRef(cond->type).kind() != LogosType::Kind::Error)
            error(std::format("if condition must be bool, got {}", type_str(cond->type)));
    } else {
        cond = error_expr();
    }

    // Per-branch move tracking — same divergence-aware merge as match.
    auto if_pre_moves = moved_vars_;
    std::set<std::string> if_post_moves;
    bool if_any_non_diverging = false;
    auto branch_diverges = [&](const lir::LBlock& b) {
        if (b.stmts.empty()) return false;
        auto br = stmt_ref_of(b.stmts.back());
        if (!br) return false;
        auto k = br.kind();
        return k == lir_schema::stmt::Code::Return ||
               k == lir_schema::stmt::Code::Break ||
               k == lir_schema::stmt::Code::Continue;
    };

    auto then_block = lir::alloc_block(*cur_prog_);
    if (node.has_key(la::THEN)) {
        moved_vars_ = if_pre_moves;
        *then_block = lower_block(map_of(node.get(la::THEN.code)));
        if (!branch_diverges(*then_block)) {
            if_any_non_diverging = true;
            for (auto& m : moved_vars_) if_post_moves.insert(m);
        }
    } else {
        // No then-block ≡ no body executed; behaves as non-diverging (just fall-through).
        if_any_non_diverging = true;
        for (auto& m : if_pre_moves) if_post_moves.insert(m);
    }

    std::optional<lir::LBlockPtr> else_opt;
    if (node.has_key(la::ELSE)) {
        auto else_node = map_of(node.get(la::ELSE.code));
        moved_vars_ = if_pre_moves;
        if (code_of(else_node) == la::BLOCK) {
            else_opt = lir::alloc_block(*cur_prog_, lower_block(else_node));
        } else {
            // else if: wrap single SIf in a block
            auto inner_if = lower_if(else_node);
            auto b = lir::alloc_block(*cur_prog_);
            b->stmts.push_back(std::move(inner_if));
            else_opt = std::move(b);
        }
        if (!branch_diverges(**else_opt)) {
            if_any_non_diverging = true;
            for (auto& m : moved_vars_) if_post_moves.insert(m);
        }
    } else {
        // Else absent ≡ control falls through with pre-state.
        if_any_non_diverging = true;
        for (auto& m : if_pre_moves) if_post_moves.insert(m);
    }
    moved_vars_ = if_any_non_diverging ? std::move(if_post_moves) : std::move(if_pre_moves);

    lir::SIf sif;
    sif.cond  = std::move(cond);
    sif.then_ = std::move(then_block);
    sif.else_ = std::move(else_opt);
    return make_stmt_emit(node_line_, std::move(sif));
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
        lir::LBlockPtr then_body = lir::alloc_block(*cur_prog_);
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            *then_body = lower_block(map_of(node.get(la::BODY.code)));
            --loop_depth_;
        }
        pop_scope();

        // Else arm: wildcard → break
        lir::LBlockPtr else_body = lir::alloc_block(*cur_prog_);
        else_body->stmts.push_back(builder().stmt_break(nullptr, "", node_line_));

        lir::SMatch sm;
        sm.scrut = std::move(scrut);
        sm.arms.push_back({std::move(pat), std::move(then_body), std::nullopt});
        sm.arms.push_back({make_pat_wild("_"), std::move(else_body), std::nullopt});

        auto loop_body = lir::alloc_block(*cur_prog_);
        loop_body->stmts.push_back(make_stmt_emit(node_line_, std::move(sm)));
        lir::SLoop sl; sl.body = std::move(loop_body);
        return make_stmt_emit(node_line_, std::move(sl));
    }

    // ── regular while cond { ... } ─────────────────────────────────
    // Capture label before lowering body (same reason as in lower_for).
    std::string my_label = std::move(pending_loop_label_);
    pending_loop_label_.clear();

    lir::LExprPtr cond = nullptr;
    if (node.has_key(la::COND)) {
        cond = lower_expr(map_of(node.get(la::COND.code)));
        if (TypeRef(cond->type).kind() != LogosType::Kind::Bool &&
            TypeRef(cond->type).kind() != LogosType::Kind::Error)
            error(std::format("while condition must be bool, got {}", type_str(cond->type)));
    } else { cond = error_expr(); }

    auto body = lir::alloc_block(*cur_prog_);
    if (node.has_key(la::BODY)) {
        ++loop_depth_;
        if (!my_label.empty()) active_loop_labels_.push_back(my_label);
        *body = lower_block(map_of(node.get(la::BODY.code)));
        if (!my_label.empty()) active_loop_labels_.pop_back();
        --loop_depth_;
    }
    lir::SWhile sw;
    sw.cond  = std::move(cond);
    sw.body  = std::move(body);
    sw.label = std::move(my_label);
    return make_stmt_emit(node_line_, std::move(sw));
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
        auto intlit_overflows = [this](const lir::LExpr* e) {
            if (auto v = get_intlit_value(e))
                return !intlit_fits(*v, LogosType::Kind::I32);
            return false;
        };
        if (intlit_overflows(lo) || intlit_overflows(hi))
            var_t = prim(LogosType::Kind::I64);
    }

    // Capture the label NOW, before lowering the body.  If we waited until
    // after lower_block(), any unlabeled nested loop inside the body would
    // steal our pending_loop_label_ on its own sf.label assignment.
    std::string my_label = std::move(pending_loop_label_);
    pending_loop_label_.clear();

    push_scope();
    define(var_name, var_t, true);
    auto body = lir::alloc_block(*cur_prog_);
    if (node.has_key(la::BODY)) {
        ++loop_depth_;
        if (!my_label.empty()) active_loop_labels_.push_back(my_label);
        *body = lower_block(map_of(node.get(la::BODY.code)));
        if (!my_label.empty()) active_loop_labels_.pop_back();
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
    return make_stmt_emit(node_line_, std::move(sf));
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
        auto body = lir::alloc_block(*cur_prog_);
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
        return make_stmt_emit(node_line_, std::move(sfe));
    }

    // ── slice path: &[T] — iterate by index over fat pointer ────────
    if (TypeRef(iter_type).kind() == LogosType::Kind::Slice) {
        TypeRef elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();
        push_scope();
        define(var_name, elem_type, false);
        auto body = lir::alloc_block(*cur_prog_);
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
        return make_stmt_emit(node_line_, std::move(sfe));
    }

    // ── iterator path: desugar to while-let loop ─────────────────
    // Requires: iter_type has a `next()` method returning Option<T>
    // Desugars: for x in iter { body }
    //        → { let mut __iter = iter; while let Opt::Some(x) = __iter.next() { body } }
    if (TypeRef(iter_type).kind() != LogosType::Kind::Error) {
        auto sname = struct_name_from_type(iter_type);
        if (sname.empty()) {
            error(std::format(
                "for-in: '{}' is not iterable. Iterable scrutinees: arrays "
                "([T; N]), slices (&[T] / str), or a struct exposing "
                "`fn next(&mut self) -> Option<T>`",
                type_str(iter_type)));
            return builder().stmt_break(nullptr, "", node_line_);
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
            // Fallback: look for `into_iter()` (IntoIterator impl). For
            //   for x in vec        → Vec<T>::into_iter
            //   for x in &vec       → &Vec<T>::into_iter   (ref-impl)
            //   for x in &mut vec   → &mut Vec<T>::into_iter
            // Synthesize iter.into_iter() call, replace iter+iter_type, then
            // re-enter the next() lookup.
            std::vector<std::string> ii_keys;
            std::string base_struct;
            if (TypeRef(iter_type).kind() == LogosType::Kind::Struct ||
                TypeRef(iter_type).kind() == LogosType::Kind::ZonedStruct) {
                base_struct = TypeRef(iter_type).struct_name();
                ii_keys.push_back(std::string(sname) + "__into_iter");
                if (base_struct != std::string(sname))
                    ii_keys.push_back(base_struct + "__into_iter");
            } else if (is_ref_like(TypeRef(iter_type).kind()) && TypeRef(iter_type).pointee()) {
                base_struct = TypeRef(iter_type).pointee().struct_name();
                std::string prefix =
                    TypeRef(iter_type).kind() == LogosType::Kind::MutRef ? "$mut_ref_" : "$ref_";
                ii_keys.push_back(prefix + base_struct + "__into_iter");
                // Fallback for ref receivers when no IntoIterator impl exists:
                // try inherent `iter()` / `iter_mut()` on the pointee struct.
                if (TypeRef(iter_type).kind() == LogosType::Kind::MutRef)
                    ii_keys.push_back(base_struct + "__iter_mut");
                else
                    ii_keys.push_back(base_struct + "__iter");
            }
            const SemaFuncInfo* ii_fn = nullptr;
            std::string ii_key_chosen;
            for (auto& k : ii_keys) {
                if (auto fit = find_func_by_base_and_signature(k, {}, false)) { ii_fn = fit; ii_key_chosen = k; break; }
                if (auto git = find_generic_func(k)) { ii_fn = git; ii_key_chosen = k; break; }
                if (auto cands = find_func_candidates(k); cands.size() == 1) { ii_fn = cands[0]; ii_key_chosen = k; break; }
            }
            if (ii_fn) {
                // Build subst from iter_type's pointee (for ref-impl) or the
                // type itself (struct).
                SemaSubst ii_subst;
                TypeRef target_ty = is_ref_like(TypeRef(iter_type).kind())
                                        ? TypeRef(iter_type).pointee() : iter_type;
                if (target_ty && !TypeRef(target_ty).type_args().empty()) {
                    SemaStructInfo* si2 = nullptr;
                    { auto [p, si] = find_struct_by_name(TypeRef(target_ty).struct_name()); si2 = si; }
                    if (!si2) { auto [p, di] = find_datatype_by_name(TypeRef(target_ty).struct_name()); si2 = di; }
                    if (si2) {
                        auto& tps = si2->type_params;
                        for (size_t i = 0; i < tps.size() && i < TypeRef(target_ty).type_args().size(); ++i)
                            ii_subst[tps[i].name] = TypeRef(target_ty).type_args()[i];
                    }
                }
                TypeRef new_iter_type = ii_fn->ret_type;
                if (!ii_subst.empty()) new_iter_type = subst_type_sema(new_iter_type, ii_subst);
                std::vector<lir::LExprPtr> pargs;
                pargs.push_back(std::move(iter));
                if (!ii_fn->type_params.empty()) {
                    std::vector<TypeRef> m_type_args;
                    for (auto& tp : ii_fn->type_params) {
                        auto it = ii_subst.find(tp.name);
                        m_type_args.push_back(it != ii_subst.end() ? it->second : nullptr);
                    }
                    iter = finish_generic_call(
                        ii_fn->symbol_name.empty() ? ii_key_chosen : ii_fn->symbol_name,
                        *ii_fn, std::move(m_type_args), std::move(pargs));
                } else {
                    iter = builder().call(ii_fn->symbol_name.empty() ? ii_key_chosen : ii_fn->symbol_name,
                                          {}, std::move(pargs), new_iter_type);
                }
                iter_type = new_iter_type;
                sname = struct_name_from_type(iter_type);
                // Re-attempt next() lookup on the new iter type.
                mangled_next = std::string(sname) + "__next";
                if (auto fit = find_func_by_base_and_signature(mangled_next, {}, false))
                    fi_ptr = fit;
                else if (auto cands = find_func_candidates(mangled_next); cands.size() == 1)
                    fi_ptr = cands[0];
                if (!fi_ptr) {
                    std::string bn;
                    if (TypeRef(iter_type).kind() == LogosType::Kind::Struct ||
                        TypeRef(iter_type).kind() == LogosType::Kind::ZonedStruct)
                        bn = TypeRef(iter_type).struct_name();
                    if (!bn.empty() && bn != std::string(sname)) {
                        auto bk = bn + "__next";
                        if (auto git = find_generic_func(bk)) fi_ptr = git;
                        else if (auto cands = find_func_candidates(bk); cands.size() == 1) fi_ptr = cands[0];
                    }
                }
            }
        }

        if (!fi_ptr) {
            error(std::format("for-in: type '{}' has no `next()` method", sname));
            return builder().stmt_break(nullptr, "", node_line_);
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
            return builder().stmt_break(nullptr, "", node_line_);
        }

        // Find the payload variant (Some-like: first variant with payload)
        const SemaVariantInfo* some_variant = nullptr;
        auto [epkg_forin, esi_forin] = find_enum_by_name(TypeRef(next_ret).enum_name());
        auto eit = esi_forin ? enums_.find(sema_key(epkg_forin, TypeRef(next_ret).enum_name())) : enums_.end();
        if (eit == enums_.end()) eit = enums_.find(TypeRef(next_ret).enum_name());
        if (eit == enums_.end()) {
            error(std::format("for-in: enum '{}' not found", TypeRef(next_ret).enum_name()));
            return builder().stmt_break(nullptr, "", node_line_);
        }
        for (auto& v : eit->second.variants)
            if (!v.payload_types.empty()) { some_variant = &v; break; }
        if (!some_variant) {
            error(std::format("for-in: enum '{}' has no payload variant", TypeRef(next_ret).enum_name()));
            return builder().stmt_break(nullptr, "", node_line_);
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
        auto outer_block = lir::alloc_block(*cur_prog_);
        outer_block->stmts.push_back(make_stmt_emit(node_line_, std::move(let_iter)));

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
        auto then_body = lir::alloc_block(*cur_prog_);
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            *then_body = lower_block(map_of(node.get(la::BODY.code)));
            --loop_depth_;
        }
        pop_scope();

        // Else arm: _ → break
        auto else_body = lir::alloc_block(*cur_prog_);
        else_body->stmts.push_back(builder().stmt_break(nullptr, "", node_line_));

        lir::SMatch sm;
        sm.scrut = make_next_call();
        auto some_mo = lir_mirror_emit_pat_variant_data(
            *cur_prog_, some_pat.enum_name, some_pat.variant, some_pat.disc,
            some_pat.bindings, some_pat.binding_types);
        lir::Pattern some_pattern;
        some_pattern.mirror_offset_ = some_mo;
        sm.arms.push_back({std::move(some_pattern), std::move(then_body), std::nullopt});
        sm.arms.push_back({make_pat_wild("_"), std::move(else_body), std::nullopt});

        auto loop_body = lir::alloc_block(*cur_prog_);
        loop_body->stmts.push_back(make_stmt_emit(node_line_, std::move(sm)));
        lir::SLoop sl; sl.body = std::move(loop_body);
        outer_block->stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));

        // Wrap in a block statement
        return make_stmt_emit(node_line_, lir::SBlock{std::move(outer_block)});
    }

    return builder().stmt_break(nullptr, "", node_line_);
}

lir::LStmt SemaChecker::lower_loop(TinyMapView node) {
    // Capture label before lowering body (same reason as in lower_for).
    std::string my_label = std::move(pending_loop_label_);
    pending_loop_label_.clear();

    auto body = lir::alloc_block(*cur_prog_);
    TypeRef saved_break_type = break_value_type_;
    bool saved_break_without_value = break_without_value_;
    break_value_type_ = nullptr;
    break_without_value_ = false;
    if (node.has_key(la::BODY)) {
        ++loop_depth_;
        if (!my_label.empty()) active_loop_labels_.push_back(my_label);
        *body = lower_block(map_of(node.get(la::BODY.code)));
        if (!my_label.empty()) active_loop_labels_.pop_back();
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
    return make_stmt_emit(node_line_, std::move(sl));
}

lir::LStmt SemaChecker::lower_field_write(TinyMapView node) {
    auto recv_name  = str_of(node.get(la::RECEIVER.code));
    auto field_name = str_of(node.get(la::FIELD.code));

    // DataRef<T> ergonomic write: p.field = val → { let __tmp = p.mut_ptr(); (*__tmp).field = val; }
    {
        TypeRef recv_type = lookup(recv_name);
        if (recv_type && TypeRef(recv_type).kind() == LogosType::Kind::Struct &&
            is_dataref(recv_type) &&
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
                        !types_compatible(val->type, ft)) {
                        auto [es, gs] = type_str_pair(ft, val->type);
                        error(std::format("field write '{}.{}': expected {}, got {}",
                              recv_name, field_name, es, gs));
                    }
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
                    track_write_move(val);
                    lir::SDerefFieldWrite dfw;
                    dfw.receiver  = tmp;
                    dfw.type_name = concrete_struct_name(T);
                    dfw.field     = std::string(field_name);
                    dfw.value     = std::move(val);
                    lir::LBlock inner;
                    inner.stmts.push_back(make_stmt_emit(node_line_, std::move(let_s)));
                    inner.stmts.push_back(make_stmt_emit(node_line_, std::move(dfw)));
                    return make_stmt_emit(node_line_,
                        lir::SBlock{lir::alloc_block(*cur_prog_, std::move(inner))});
                }
            }
        }
    }

    auto sname = struct_name_of(recv_name);
    if (sname.empty()) {
        // Metaprog discovery: receivers that resolve to <error> (typically
        // a *mut to a not-yet-derived struct) are silently propagated;
        // post-dispatch sema will surface a real error.
        auto rt = lookup(recv_name);
        bool sname_via_error = false;
        if (rt) {
            TypeRef t = rt;
            if (TypeRef(t).kind() == LogosType::Kind::Ptr ||
                is_ref_like(TypeRef(t).kind())) t = TypeRef(t).pointee();
            if (t && TypeRef(t).kind() == LogosType::Kind::Error)
                sname_via_error = true;
        }
        if (!(metaprog_mode_ && sname_via_error))
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
        auto [es, gs] = type_str_pair(ft, val->type);
        error(std::format("field write '{}.{}': expected {}, got {}",
              recv_name, field_name, es, gs));
    }
    if (ft && TypeRef(ft).kind() != LogosType::Kind::Error &&
        TypeRef(val->type).kind() == LogosType::Kind::IntLit)
        if (auto v = get_intlit_value(val))
            if (!intlit_fits(*v, TypeRef(ft).kind()))
                error(std::format("field write '{}.{}': value {} does not fit in {}",
                      recv_name, field_name, *v, type_str(ft)));
    // Check array literal elements against narrow array field type.
    if (ft && TypeRef(ft).kind() == LogosType::Kind::Array && TypeRef(ft).elem() &&
        TypeRef(val->type).kind() == LogosType::Kind::Array) {
        auto vr = expr_ref_of(*val);
        if (vr.kind() == lir_schema::expr::Code::ArrLit) {
            lir_view::EArrLitView al{vr};
            for (uint64_t i = 0; i < al.count(); ++i) {
                auto el = al.elem(i);
                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(el))
                        if (!intlit_fits(*v, TypeRef(ft).elem().kind()))
                            error(std::format("field write '{}.{}': array element {}: value {} does not fit in {}",
                                  recv_name, field_name, i, *v, type_str(TypeRef(ft).elem())));
            }
        }
    }
    // Check tuple literal elements against narrow tuple field element types.
    if (ft && TypeRef(ft).kind() == LogosType::Kind::Tuple && TypeRef(val->type).kind() == LogosType::Kind::Tuple) {
        auto vr = expr_ref_of(*val);
        if (vr.kind() == lir_schema::expr::Code::TupleLit) {
            lir_view::ETupleLitView tl{vr};
            uint64_t i = 0;
            tl.each_elem([&](lir_view::ExprRef el) {
                if (i >= TypeRef(ft).tuple_elems().size()) { ++i; return; }
                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(el))
                        if (TypeRef(ft).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(ft).tuple_elems()[i]).kind()))
                            error(std::format("field write '{}.{}': tuple element {}: value {} does not fit in {}",
                                  recv_name, field_name, i, *v, type_str(TypeRef(ft).tuple_elems()[i])));
                if (TypeRef(ft).tuple_elems()[i] && TypeRef(TypeRef(ft).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                    TypeRef(TypeRef(ft).tuple_elems()[i]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                    el.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView ial{el};
                    for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                        auto iel = ial.elem(ii);
                        if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(iel))
                                if (!intlit_fits(*v, TypeRef(TypeRef(ft).tuple_elems()[i]).elem().kind()))
                                    error(std::format("field write '{}.{}': tuple element {}: array element {}: value {} does not fit in {}",
                                          recv_name, field_name, i, ii, *v, type_str(TypeRef(TypeRef(ft).tuple_elems()[i]).elem())));
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
                                    error(std::format("field write '{}.{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                          recv_name, field_name, i, ii, *v, type_str(TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii])));
                        ++ii;
                    });
                }
                ++i;
            });
        }
    }
    track_write_move(val);
    lir::SFieldWrite sfw;
    sfw.receiver = std::string(recv_name);
    sfw.field    = std::string(field_name);
    sfw.value    = std::move(val);
    return make_stmt_emit(node_line_, std::move(sfw));
}

// a.mid.field = val  — chained 2-level field write
lir::LStmt SemaChecker::lower_chain_field_write(TinyMapView node) {
    auto recv_name = str_of(node.get(la::RECEIVER.code));
    std::vector<std::string> path;
    if (node.has_key(la::PATH)) {
        auto path_map = map_of(node.get(la::PATH.code));
        if (path_map.has_key(la::ITEMS)) {
            auto arr = arr_of(path_map.get(la::ITEMS.code));
            uint64_t n = arr.size();
            path.reserve(n);
            for (uint64_t i = 0; i < n; ++i)
                path.emplace_back(str_of(arr.get(i)));
        }
    }
    if (path.size() < 2) {
        error("chain field write: path must have at least 2 segments");
        return builder().stmt_expr(error_expr(), node_line_);
    }
    const std::string& mid_name   = path.front();
    const std::string& field_name = path.back();

    auto outer_sname = struct_name_of(recv_name);
    if (outer_sname.empty()) {
        // Metaprog discovery: silent <error> propagation when the receiver
        // type is itself <error> (unresolved derive target). Post-dispatch
        // sema surfaces a real error if needed.
        auto rt = lookup(recv_name);
        bool sname_via_error = false;
        if (rt) {
            TypeRef t = rt;
            if (TypeRef(t).kind() == LogosType::Kind::Ptr ||
                is_ref_like(TypeRef(t).kind())) t = TypeRef(t).pointee();
            if (t && TypeRef(t).kind() == LogosType::Kind::Error)
                sname_via_error = true;
        }
        if (!(metaprog_mode_ && sname_via_error))
            error(std::format("chain field write: '{}' is not a struct", recv_name));
        return builder().stmt_expr(error_expr(), node_line_);
    }
    auto recv_type = lookup(recv_name);
    if (recv_type && TypeRef(recv_type).kind() == LogosType::Kind::Ref)
        error(std::format("chain field write '{}': receiver is &T (shared reference)", recv_name));
    // B97.3: `&mut self`-typed receivers carry mutability via the ref kind,
    // not via `let mut`. Skip the binding-mutability check for them.
    else if (recv_type && TypeRef(recv_type).kind() != LogosType::Kind::Ptr
             && TypeRef(recv_type).kind() != LogosType::Kind::MutRef
             && !lookup_is_mut(recv_name))
        error(std::format("chain field write to immutable variable '{}'", recv_name));
    if (recv_type && TypeRef(recv_type).kind() == LogosType::Kind::Ptr && !TypeRef(recv_type).mut_ptr())
        error(std::format("chain field write '{}': receiver is *const pointer", recv_name));

    if (!inside_unsafe_ && recv_type && TypeRef(recv_type).kind() == LogosType::Kind::Ptr)
        error("chain field write through raw pointer requires unsafe context");

    // Walk: cur_struct_t starts at receiver's struct type (after deref/ref).
    TypeRef cur_struct_t = recv_type;
    if (cur_struct_t && TypeRef(cur_struct_t).kind() == LogosType::Kind::Ptr)
        cur_struct_t = TypeRef(cur_struct_t).pointee();
    else if (cur_struct_t && is_ref_like(TypeRef(cur_struct_t).kind()))
        cur_struct_t = TypeRef(cur_struct_t).pointee();

    TypeRef ft;  // type of final field
    bool failed = false;
    for (size_t i = 0; i < path.size(); ++i) {
        auto cur_sname = cur_struct_t ? concrete_struct_name(cur_struct_t) : std::string(i == 0 ? outer_sname : "");
        TypeRef seg_t = cur_struct_t
            ? field_type_of_for_type(cur_struct_t, path[i])
            : (cur_sname.empty() ? nullptr : field_type_of(cur_sname, path[i]));
        if (!seg_t) {
            error(std::format("chain field write: struct '{}' has no field '{}'",
                  cur_sname.empty() ? std::string(outer_sname) : cur_sname, path[i]));
            failed = true;
            break;
        }
        if (i + 1 == path.size()) {
            ft = seg_t;
            break;
        }
        // Descend: if seg_t is a pointer, follow pointee; require it be a struct.
        TypeRef next_struct_t = seg_t;
        if (TypeRef(next_struct_t).kind() == LogosType::Kind::Ptr)
            next_struct_t = TypeRef(next_struct_t).pointee();
        if (!next_struct_t || concrete_struct_name(next_struct_t).empty()) {
            error(std::format("chain field write: '{}.{}' has no struct type",
                  recv_name, path[i]));
            failed = true;
            break;
        }
        cur_struct_t = next_struct_t;
    }
    if (failed)
        return builder().stmt_expr(error_expr(), node_line_);

    lir::LExprPtr val = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    if (TypeRef(ft).kind() != LogosType::Kind::Error
        && TypeRef(val->type).kind() != LogosType::Kind::Error
        && !types_compatible(val->type, ft)) {
        std::string path_str(recv_name);
        for (auto& seg : path) { path_str += '.'; path_str += seg; }
        error(std::format("chain field write '{}': expected {}, got {}",
              path_str, type_str(ft), type_str(val->type)));
    }

    track_write_move(val);
    lir::SChainFieldWrite scfw;
    scfw.receiver  = std::string(recv_name);
    scfw.mid_field = mid_name;
    scfw.extras.assign(path.begin() + 1, path.end() - 1);  // empty for depth-2
    scfw.field     = field_name;
    scfw.value     = std::move(val);
    return make_stmt_emit(node_line_, std::move(scfw));
}

// a.b.c.…z op= val  →  a.b.c.…z = a.b.c.…z op val   (N-deep)
lir::LStmt SemaChecker::lower_chain_field_compound_assign(TinyMapView node) {
    auto recv_name = str_of(node.get(la::RECEIVER.code));
    std::vector<std::string> path;
    if (node.has_key(la::PATH)) {
        auto path_map = map_of(node.get(la::PATH.code));
        if (path_map.has_key(la::ITEMS)) {
            auto arr = arr_of(path_map.get(la::ITEMS.code));
            uint64_t n = arr.size();
            path.reserve(n);
            for (uint64_t i = 0; i < n; ++i)
                path.emplace_back(str_of(arr.get(i)));
        }
    }
    if (path.size() < 2) {
        error("chain field compound assign: path must have at least 2 segments");
        return builder().stmt_expr(error_expr(), node_line_);
    }
    const std::string& mid_name   = path.front();
    const std::string& field_name = path.back();

    auto op_tok = str_of(node.get(la::OP.code));
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

    // Walk types step-by-step; collect the type of each segment so we can
    // build the matching read-side LIR (field_read chain).
    TypeRef cur_struct_t = recv_type_for_cfca;
    if (cur_struct_t && TypeRef(cur_struct_t).kind() == LogosType::Kind::Ptr)
        cur_struct_t = TypeRef(cur_struct_t).pointee();
    else if (cur_struct_t && is_ref_like(TypeRef(cur_struct_t).kind()))
        cur_struct_t = TypeRef(cur_struct_t).pointee();

    std::vector<TypeRef> seg_types;
    seg_types.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i) {
        auto cur_sname = cur_struct_t ? concrete_struct_name(cur_struct_t) : std::string(i == 0 ? outer_sname : "");
        TypeRef seg_t = cur_struct_t
            ? field_type_of_for_type(cur_struct_t, path[i])
            : (cur_sname.empty() ? nullptr : field_type_of(cur_sname, path[i]));
        seg_types.push_back(seg_t);
        if (!seg_t) {
            if (!outer_sname.empty()) {
                std::string path_str(recv_name);
                for (auto& s : path) { path_str += '.'; path_str += s; }
                error(std::format("chain field compound assign: could not resolve '{}'", path_str));
            }
            break;
        }
        if (i + 1 == path.size()) break;
        TypeRef next_struct_t = seg_t;
        if (TypeRef(next_struct_t).kind() == LogosType::Kind::Ptr)
            next_struct_t = TypeRef(next_struct_t).pointee();
        cur_struct_t = next_struct_t;
    }
    TypeRef ft = seg_types.empty() ? TypeRef{} : seg_types.back();

    // Lower RHS value.
    lir::LExprPtr rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();

    // Build read chain: recv.path[0].path[1]…path[N-1]
    auto cur_expr = builder().var_ref(std::string(recv_name),
        lookup(recv_name) ? lookup(recv_name) : error_t());
    for (size_t i = 0; i < path.size(); ++i) {
        TypeRef step_t = (i < seg_types.size() && seg_types[i]) ? seg_types[i] : error_t();
        cur_expr = builder().field_read(std::move(cur_expr), path[i], step_t);
    }

    TypeRef ft2 = ft ? ft : error_t();
    lir::LExprPtr combined = builder().bin_op(base_op, std::move(cur_expr), std::move(rhs), ft2);

    lir::SChainFieldWrite scfw;
    scfw.receiver  = std::string(recv_name);
    scfw.mid_field = mid_name;
    scfw.extras.assign(path.begin() + 1, path.end() - 1);
    scfw.field     = field_name;
    scfw.value     = std::move(combined);
    return make_stmt_emit(node_line_, std::move(scfw));
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
        return builder().stmt_break(nullptr, "", node_line_);
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
    return make_stmt_emit(node_line_, std::move(sfw));
}

lir::LStmt SemaChecker::lower_tuple_field_write(TinyMapView node) {
    auto recv_name = str_of(node.get(la::RECEIVER.code));
    auto idx_sv    = str_of(node.get(la::INDEX.code));
    uint64_t idx   = (uint64_t)parse_int_literal(idx_sv);

    TypeRef recv_t = lookup(recv_name);
    if (!recv_t) {
        error(std::format("tuple field write: undefined variable '{}'", recv_name));
        return make_stmt_emit(node_line_, lir::STupleWrite{std::string(recv_name), (uint32_t)idx, error_expr()});
    }
    // Strip &mut wrapper if present
    if (TypeRef(recv_t).kind() == LogosType::Kind::MutRef && TypeRef(recv_t).pointee())
        recv_t = TypeRef(recv_t).pointee();

    if (TypeRef(recv_t).kind() != LogosType::Kind::Tuple) {
        error(std::format("tuple field write: '{}' is not a tuple (got {})", recv_name, type_str(recv_t)));
        return make_stmt_emit(node_line_, lir::STupleWrite{std::string(recv_name), (uint32_t)idx, error_expr()});
    }
    if (idx >= TypeRef(recv_t).tuple_elems().size()) {
        error(std::format("tuple field write: index {} out of range (tuple has {} elements)",
                          idx, TypeRef(recv_t).tuple_elems().size()));
        return make_stmt_emit(node_line_, lir::STupleWrite{std::string(recv_name), (uint32_t)idx, error_expr()});
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
        if (auto v = get_intlit_value(val))
            if (!intlit_fits(*v, TypeRef(ft).kind()))
                error(std::format("tuple field write '{}.{}': value {} does not fit in {}",
                      recv_name, idx, *v, type_str(ft)));
    track_write_move(val);
    return make_stmt_emit(node_line_, lir::STupleWrite{std::string(recv_name), (uint32_t)idx, std::move(val), recv_t});
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
        if (auto v = get_intlit_value(val))
            if (!intlit_fits(*v, TypeRef(ft).kind()))
                error(std::format("deref-field-write '(*{}).{}': value {} does not fit in {}",
                      recv_name, field_name, *v, type_str(ft)));
    // Check array literal elements against narrow array field type.
    if (ft && TypeRef(ft).kind() == LogosType::Kind::Array && TypeRef(ft).elem() &&
        TypeRef(val->type).kind() == LogosType::Kind::Array) {
        auto vr = expr_ref_of(*val);
        if (vr.kind() == lir_schema::expr::Code::ArrLit) {
            lir_view::EArrLitView al{vr};
            for (uint64_t i = 0; i < al.count(); ++i) {
                auto el = al.elem(i);
                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(el))
                        if (!intlit_fits(*v, TypeRef(ft).elem().kind()))
                            error(std::format("deref-field-write '(*{}).{}': array element {}: value {} does not fit in {}",
                                  recv_name, field_name, i, *v, type_str(TypeRef(ft).elem())));
            }
        }
    }
    // Check tuple literal elements against narrow tuple field element types.
    if (ft && TypeRef(ft).kind() == LogosType::Kind::Tuple && TypeRef(val->type).kind() == LogosType::Kind::Tuple) {
        auto vr = expr_ref_of(*val);
        if (vr.kind() == lir_schema::expr::Code::TupleLit) {
            lir_view::ETupleLitView tl{vr};
            uint64_t i = 0;
            tl.each_elem([&](lir_view::ExprRef el) {
                if (i >= TypeRef(ft).tuple_elems().size()) { ++i; return; }
                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(el))
                        if (TypeRef(ft).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(ft).tuple_elems()[i]).kind()))
                            error(std::format("deref-field-write '(*{}).{}': tuple element {}: value {} does not fit in {}",
                                  recv_name, field_name, i, *v, type_str(TypeRef(ft).tuple_elems()[i])));
                if (TypeRef(ft).tuple_elems()[i] && TypeRef(TypeRef(ft).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                    TypeRef(TypeRef(ft).tuple_elems()[i]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                    el.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView ial{el};
                    for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                        auto iel = ial.elem(ii);
                        if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(iel))
                                if (!intlit_fits(*v, TypeRef(TypeRef(ft).tuple_elems()[i]).elem().kind()))
                                    error(std::format("deref-field-write '(*{}).{}': tuple element {}: array element {}: value {} does not fit in {}",
                                          recv_name, field_name, i, ii, *v, type_str(TypeRef(TypeRef(ft).tuple_elems()[i]).elem())));
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
                                    error(std::format("deref-field-write '(*{}).{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                          recv_name, field_name, i, ii, *v, type_str(TypeRef(TypeRef(ft).tuple_elems()[i]).tuple_elems()[ii])));
                        ++ii;
                    });
                }
                ++i;
            });
        }
    }

    track_write_move(val);
    lir::SDerefFieldWrite sdfw;
    sdfw.receiver  = std::string(recv_name);
    sdfw.type_name = type_name;
    sdfw.field     = std::string(field_name);
    sdfw.value     = std::move(val);
    return make_stmt_emit(node_line_, std::move(sdfw));
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
        if (auto v = get_intlit_value(val))
            if (!intlit_fits(*v, TypeRef(elem_type).kind()))
                error(std::format("index write to '{}': value {} does not fit in {}",
                      arr_name, *v, type_str(elem_type)));
    // Check array literal elements against narrow nested array element type.
    if (elem_type && TypeRef(elem_type).kind() == LogosType::Kind::Array && TypeRef(elem_type).elem() &&
        TypeRef(val->type).kind() == LogosType::Kind::Array) {
        auto vr = expr_ref_of(*val);
        if (vr.kind() == lir_schema::expr::Code::ArrLit) {
            lir_view::EArrLitView al{vr};
            for (uint64_t i = 0; i < al.count(); ++i) {
                auto el = al.elem(i);
                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(el))
                        if (!intlit_fits(*v, TypeRef(elem_type).elem().kind()))
                            error(std::format("index write to '{}': array element {}: value {} does not fit in {}",
                                  arr_name, i, *v, type_str(TypeRef(elem_type).elem())));
            }
        }
    }
    // Check tuple literal elements against narrow nested tuple element type.
    if (elem_type && TypeRef(elem_type).kind() == LogosType::Kind::Tuple &&
        TypeRef(val->type).kind() == LogosType::Kind::Tuple) {
        auto vr = expr_ref_of(*val);
        if (vr.kind() == lir_schema::expr::Code::TupleLit) {
            lir_view::ETupleLitView tl{vr};
            uint64_t i = 0;
            tl.each_elem([&](lir_view::ExprRef el) {
                if (i >= TypeRef(elem_type).tuple_elems().size()) { ++i; return; }
                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(el))
                        if (TypeRef(elem_type).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(elem_type).tuple_elems()[i]).kind()))
                            error(std::format("index write to '{}': tuple element {}: value {} does not fit in {}",
                                  arr_name, i, *v, type_str(TypeRef(elem_type).tuple_elems()[i])));
                if (TypeRef(elem_type).tuple_elems()[i] && TypeRef(TypeRef(elem_type).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                    TypeRef(TypeRef(elem_type).tuple_elems()[i]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                    el.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView ial{el};
                    for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                        auto iel = ial.elem(ii);
                        if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(iel))
                                if (!intlit_fits(*v, TypeRef(TypeRef(elem_type).tuple_elems()[i]).elem().kind()))
                                    error(std::format("index write to '{}': tuple element {}: array element {}: value {} does not fit in {}",
                                          arr_name, i, ii, *v, type_str(TypeRef(TypeRef(elem_type).tuple_elems()[i]).elem())));
                    }
                }
                if (TypeRef(elem_type).tuple_elems()[i] && TypeRef(TypeRef(elem_type).tuple_elems()[i]).kind() == LogosType::Kind::Tuple &&
                    el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                    el.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView itl{el};
                    uint64_t ii = 0;
                    itl.each_elem([&](lir_view::ExprRef iel) {
                        if (ii >= TypeRef(TypeRef(elem_type).tuple_elems()[i]).tuple_elems().size()) { ++ii; return; }
                        if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(iel))
                                if (TypeRef(TypeRef(elem_type).tuple_elems()[i]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(elem_type).tuple_elems()[i]).tuple_elems()[ii]).kind()))
                                    error(std::format("index write to '{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                          arr_name, i, ii, *v, type_str(TypeRef(TypeRef(elem_type).tuple_elems()[i]).tuple_elems()[ii])));
                        ++ii;
                    });
                }
                ++i;
            });
        }
    }

    track_write_move(val);
    lir::SIndexWrite siw;
    siw.arr   = std::string(arr_name);
    siw.index = std::move(idx);
    siw.value = std::move(val);
    return make_stmt_emit(node_line_, std::move(siw));
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
        return builder().stmt_break(nullptr, "", node_line_);
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
    return make_stmt_emit(node_line_, std::move(siw));
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
        if (auto v = get_intlit_value(val))
            if (!intlit_fits(*v, TypeRef(elem_t).kind()))
                error(std::format("field index write '{}.{}[i]': value {} does not fit in {}",
                      recv_name, field_name, *v, type_str(elem_t)));
    // Check array literal elements against narrow nested array element type.
    if (elem_t && TypeRef(elem_t).kind() == LogosType::Kind::Array && TypeRef(elem_t).elem() &&
        TypeRef(val->type).kind() == LogosType::Kind::Array) {
        auto vr = expr_ref_of(*val);
        if (vr.kind() == lir_schema::expr::Code::ArrLit) {
            lir_view::EArrLitView al{vr};
            for (uint64_t i = 0; i < al.count(); ++i) {
                auto el = al.elem(i);
                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(el))
                        if (!intlit_fits(*v, TypeRef(elem_t).elem().kind()))
                            error(std::format("field index write '{}.{}[i]': array element {}: value {} does not fit in {}",
                                  recv_name, field_name, i, *v, type_str(TypeRef(elem_t).elem())));
            }
        }
    }
    // Check tuple literal elements against narrow nested tuple element type.
    if (elem_t && TypeRef(elem_t).kind() == LogosType::Kind::Tuple &&
        TypeRef(val->type).kind() == LogosType::Kind::Tuple) {
        auto vr = expr_ref_of(*val);
        if (vr.kind() == lir_schema::expr::Code::TupleLit) {
            lir_view::ETupleLitView tl{vr};
            uint64_t i = 0;
            tl.each_elem([&](lir_view::ExprRef el) {
                if (i >= TypeRef(elem_t).tuple_elems().size()) { ++i; return; }
                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(el))
                        if (TypeRef(elem_t).tuple_elems()[i] && !intlit_fits(*v, TypeRef(TypeRef(elem_t).tuple_elems()[i]).kind()))
                            error(std::format("field index write '{}.{}[i]': tuple element {}: value {} does not fit in {}",
                                  recv_name, field_name, i, *v, type_str(TypeRef(elem_t).tuple_elems()[i])));
                if (TypeRef(elem_t).tuple_elems()[i] && TypeRef(TypeRef(elem_t).tuple_elems()[i]).kind() == LogosType::Kind::Array &&
                    TypeRef(TypeRef(elem_t).tuple_elems()[i]).elem() && el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Array &&
                    el.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView ial{el};
                    for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                        auto iel = ial.elem(ii);
                        if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(iel))
                                if (!intlit_fits(*v, TypeRef(TypeRef(elem_t).tuple_elems()[i]).elem().kind()))
                                    error(std::format("field index write '{}.{}[i]': tuple element {}: array element {}: value {} does not fit in {}",
                                          recv_name, field_name, i, ii, *v, type_str(TypeRef(TypeRef(elem_t).tuple_elems()[i]).elem())));
                    }
                }
                if (TypeRef(elem_t).tuple_elems()[i] && TypeRef(TypeRef(elem_t).tuple_elems()[i]).kind() == LogosType::Kind::Tuple &&
                    el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::Tuple &&
                    el.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView itl{el};
                    uint64_t ii = 0;
                    itl.each_elem([&](lir_view::ExprRef iel) {
                        if (ii >= TypeRef(TypeRef(elem_t).tuple_elems()[i]).tuple_elems().size()) { ++ii; return; }
                        if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(iel))
                                if (TypeRef(TypeRef(elem_t).tuple_elems()[i]).tuple_elems()[ii] && !intlit_fits(*v, TypeRef(TypeRef(TypeRef(elem_t).tuple_elems()[i]).tuple_elems()[ii]).kind()))
                                    error(std::format("field index write '{}.{}[i]': tuple element {}: sub-element {}: value {} does not fit in {}",
                                          recv_name, field_name, i, ii, *v, type_str(TypeRef(TypeRef(elem_t).tuple_elems()[i]).tuple_elems()[ii])));
                        ++ii;
                    });
                }
                ++i;
            });
        }
    }

    track_write_move(val);
    lir::SFieldIndexWrite sfiw;
    sfiw.receiver = std::string(recv_name);
    sfiw.field    = std::string(field_name);
    sfiw.index    = std::move(idx);
    sfiw.value    = std::move(val);
    return make_stmt_emit(node_line_, std::move(sfiw));
}

lir::LStmt SemaChecker::lower_match(TinyMapView node) {
    lir::LExprPtr scrut = nullptr;
    TypeRef scrut_type = error_t();
    if (node.has_key(la::VALUE)) {
        scrut = lower_expr(map_of(node.get(la::VALUE.code)));
        scrut_type = scrut->type;
    } else { scrut = error_expr(); }

    // Sprint 5.2: arm-after-catchall lint (closes B-pt-07).  The first
    // unguarded `_` arm makes every subsequent arm unreachable.
    if (node.has_key(la::ITEMS)) {
        auto arms_l = arr_of(node.get(la::ITEMS.code));
        bool seen_catchall = false;
        for (uint64_t i = 0; i < arms_l.size(); ++i) {
            auto arm = map_of(arms_l.get(i));
            if (code_of(arm) != la::MATCH_ARM) continue;
            if (seen_catchall) {
                error("unreachable match arm: a previous '_' arm matches all values");
                break;
            }
            if (is_catchall_pat(arm)) seen_catchall = true;
        }
    }

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
            hoist_let_view = make_stmt_emit(node_line_, std::move(sl));
        }
        anyval_t = make_datatype_type("AnyVal");
        root_var = "__hmatch_root_" + std::to_string(tmp_var_count_++);
        {
            auto view_ref = builder().var_ref(view_var, scrut_type);
            auto root_call = builder().method_call(std::move(view_ref), "root", "", {}, {}, -1, anyval_t);
            lir::SLet sl;
            sl.name = root_var; sl.type = anyval_t; sl.is_mut = false;
            sl.value = std::move(root_call);
            hoist_let_root = make_stmt_emit(node_line_, std::move(sl));
        }
        base_var = "__hmatch_base_" + std::to_string(tmp_var_count_++);
        {
            TypeRef u8_ptr_t = make_ptr(false, prim(LogosType::Kind::U8));
            auto view_ref = builder().var_ref(view_var, scrut_type);
            auto base_call = builder().method_call(std::move(view_ref), "base", "", {}, {}, -1, u8_ptr_t);
            lir::SLet sl;
            sl.name = base_var; sl.type = u8_ptr_t; sl.is_mut = false;
            sl.value = std::move(base_call);
            hoist_let_base = make_stmt_emit(node_line_, std::move(sl));
        }
        has_hoist_let = true;
        scrut = builder().var_ref(view_var, scrut_type);
    }

    lir::SMatch smatch;
    smatch.scrut = std::move(scrut);

    if (node.has_key(la::ITEMS)) {
        auto arms = arr_of(node.get(la::ITEMS.code));
        // Per-arm move tracking: each arm starts from `pre_moves` (state
        // before the match), and its contribution to post-match moves is
        // collected only if the arm doesn't diverge (return/break/continue).
        // Without this, moves in arm 1 would leak into arm 2's processing,
        // producing spurious "use of moved variable" errors for code like
        //   match it.next() {
        //       Option::None => return acc;     // marks acc moved
        //       Option::Some(v) => acc = f(acc, v);  // saw acc as moved
        //   }
        // After the match, moved_vars_ is the union of moves from
        // non-diverging arms (conservative: a var moved on any falling-
        // through path is considered moved post-match).
        auto pre_moves = moved_vars_;
        std::set<std::string> post_moves;
        bool any_non_diverging = false;
        // P4-pm-25: fan out or-pattern arms whose alternatives have
        // differing variant discriminants. The existing PatOr mlir-gen
        // extracts payload from alt[0] only, which is wrong for
        // mixed-shape alts (e.g. `Pass::Opaque {with: true, ..} |
        // Pass::Transparent`). Fan-out lets each alt go through the
        // normal single-arm path with its own refutable-inner guard
        // and payload extraction. Scalar-only or-patterns (`1 | 2 | 3`)
        // and same-variant-with-bindings or-patterns stay merged.
        struct EffArm { hermes::TinyMapView arm; int32_t alt_idx; };
        auto or_needs_fanout = [&](hermes::TinyMapView lhs) -> bool {
            if (code_of(lhs) != la::PAT_OR) return false;
            if (!lhs.has_key(la::ITEMS)) return false;
            auto a = arr_of(lhs.get(la::ITEMS.code));
            if (a.size() < 2) return false;
            // Need fan-out if at least one alt is a variant pattern
            // (PAT_VARIANT/PAT_VARIANT_DATA), since payload extraction
            // differs per disc. Scalar-only alts are handled correctly
            // by the existing merged PatOr.
            for (uint64_t k = 0; k < a.size(); ++k) {
                int32_t c = code_of(map_of(a.get(k)));
                if (c == la::PAT_VARIANT || c == la::PAT_VARIANT_DATA) return true;
            }
            return false;
        };
        std::vector<EffArm> eff_arms;
        for (uint64_t i = 0; i < arms.size(); ++i) {
            auto arm = map_of(arms.get(i));
            if (code_of(arm) != la::MATCH_ARM) {
                eff_arms.push_back({arm, -1});
                continue;
            }
            if (arm.has_key(la::LHS)) {
                auto lhs = map_of(arm.get(la::LHS.code));
                if (or_needs_fanout(lhs)) {
                    auto a = arr_of(lhs.get(la::ITEMS.code));
                    for (uint64_t k = 0; k < a.size(); ++k)
                        eff_arms.push_back({arm, (int32_t)k});
                    continue;
                }
            }
            eff_arms.push_back({arm, -1});
        }
        auto effective_lhs = [&](hermes::TinyMapView arm, int32_t alt_idx) {
            if (alt_idx < 0) return map_of(arm.get(la::LHS.code));
            auto lhs = map_of(arm.get(la::LHS.code));
            return map_of(arr_of(lhs.get(la::ITEMS.code)).get((uint64_t)alt_idx));
        };
        (void)effective_lhs;
        for (uint64_t i = 0; i < eff_arms.size(); ++i) {
            auto arm = eff_arms[i].arm;
            int32_t alt_idx = eff_arms[i].alt_idx;
            if (code_of(arm) != la::MATCH_ARM) continue;

            // Reset moves to pre-match state at each arm boundary.
            moved_vars_ = pre_moves;

            // Synthesize guard for Hermes patterns (scalar + structural).
            lir::LExprPtr synth_guard = nullptr;
            std::vector<lir::LStmt> body_prologue;
            std::vector<HermesPatBinding> body_binds;
            if (has_hermes_pat && arm.has_key(la::LHS)) {
                std::vector<lir::LStmt> g_stmts;
                std::vector<HermesPatBinding> g_binds;
                auto raw = build_hermes_pat_guard(
                    effective_lhs(arm, alt_idx), root_var, anyval_t, base_var,
                    g_stmts, g_binds);
                if (!g_stmts.empty() && raw) {
                    auto blk = lir::alloc_block(*cur_prog_);
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
                        effective_lhs(arm, alt_idx), root_var, anyval_t,
                        base_var, body_prologue, body_binds);
                }
            }

            // Build pattern. P4-pm-02: wire side channel so that
            // nested struct/tuple sub-patterns inside variant payload
            // register synth payload bindings + body-prologue lets.
            in_match_hermes_ctx_ = has_hermes_pat;
            std::vector<NestedPatSub> nested_subs;
            auto* saved_pat_subs = current_pat_nested_subs_;
            current_pat_nested_subs_ = &nested_subs;
            logos::compiler::StrSet mut_names;
            auto* saved_pat_muts = current_pat_mut_names_;
            current_pat_mut_names_ = &mut_names;
            // P4-pm-01: capture refutable inner-pattern guards (variant
            // payload like `E::V { f: 1 }` or `Option::Some(1)`).
            std::vector<lir::LExprPtr> refut_guards;
            auto* saved_pat_refut = current_pat_refutable_guards_;
            current_pat_refutable_guards_ = &refut_guards;
            lir::Pattern pat = arm.has_key(la::LHS)
                ? build_pattern(effective_lhs(arm, alt_idx), scrut_type)
                : make_pat_wild("_");
            current_pat_nested_subs_ = saved_pat_subs;
            current_pat_refutable_guards_ = saved_pat_refut;
            in_match_hermes_ctx_ = false;

            // Build body block — push pattern bindings into scope
            push_scope();
            bind_pattern(pat, scrut_type);
            current_pat_mut_names_ = saved_pat_muts;
            // Register Hermes @-pattern bindings in scope (visible in body + guard).
            for (const auto& b : body_binds) {
                define(b.name, anyval_t, /*is_mut=*/false);
            }
            // P4-pm-02: for each nested struct sub-pat, emit field-by-
            // field SLet stmts that destructure the synth payload slot.
            std::vector<lir::LStmt> nested_destructure_stmts;
            for (auto& nsub : nested_subs) {
                TypeRef synth_t = lookup(nsub.synth_name);
                if (!synth_t) continue;
                if (code_of(nsub.sub_pat_node) != la::PAT_STRUCT) continue;
                // Field-by-field destructure: for each {name, optional sub-binding}
                // emit `let <bind_name> = __synth.<field>;`. Sub-pat
                // refutability already filtered by build_pattern.
                if (!nsub.sub_pat_node.has_key(la::ITEMS)) continue;
                auto fitems_av = nsub.sub_pat_node.get(la::ITEMS.code);
                if (!fitems_av.is_pointer()) continue;
                auto fitems_m = map_of(fitems_av);
                if (!fitems_m.has_key(la::ITEMS)) continue;
                auto fields = arr_of(fitems_m.get(la::ITEMS.code));
                // Look up struct info from synth's struct name.
                std::string sname_s(TypeRef(synth_t).struct_name());
                auto [_skpkg, sinfo] = find_struct_by_name(sname_s);
                if (!sinfo) continue;
                for (uint64_t k = 0; k < fields.size(); ++k) {
                    auto fnode = map_of(fields.get(k));
                    if (!fnode.has_key(la::NAME)) continue;
                    std::string fname(str_of(fnode.get(la::NAME.code)));
                    // Determine bind name: either NAME (shorthand) or
                    // sub-pat's NAME (if PAT_WILD with explicit rename).
                    std::string bind = fname;
                    if (fnode.has_key(la::VALUE)) {
                        auto sub = map_of(fnode.get(la::VALUE.code));
                        if (code_of(sub) == la::PAT_WILD && sub.has_key(la::NAME))
                            bind = std::string(str_of(sub.get(la::NAME.code)));
                    }
                    // Look up field type.
                    TypeRef ftype = error_t();
                    for (auto& sf : sinfo->fields)
                        if (sf.name == fname) { ftype = sf.type; break; }
                    define(bind, ftype);
                    auto sref = builder().var_ref(nsub.synth_name, synth_t);
                    auto fr = builder().field_read(std::move(sref), fname, ftype);
                    lir::SLet sl;
                    sl.name = bind; sl.type = ftype; sl.is_mut = false;
                    sl.value = std::move(fr);
                    nested_destructure_stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
                }
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
            // P4-pm-01: AND in refutable-inner guards (variant payload
            // literal predicates collected during build_pattern). Order
            // doesn't matter for correctness — they read fresh
            // pattern-bound names, never side-effect.
            for (auto& rg : refut_guards) {
                if (!rg) continue;
                if (guard) {
                    auto merged = builder().bin_op("&&", std::move(*guard), std::move(rg), bool_t());
                    guard = std::move(merged);
                } else {
                    guard = std::move(rg);
                }
            }

            lir::LBlockPtr body = lir::alloc_block(*cur_prog_);
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
                    body->stmts.push_back(make_stmt_emit(node_line_, std::move(ret)));
                } else {
                    // Statement-position match: EXPR arms are evaluated for side effects.
                    lir::SExprStmt es; es.expr = std::move(val);
                    body->stmts.push_back(make_stmt_emit(node_line_, std::move(es)));
                }
            }
            // P4-pm-02: prepend nested-pat destructure stmts so user
            // body sees the sub-pat bindings.
            if (!nested_destructure_stmts.empty()) {
                std::vector<lir::LStmt> merged = std::move(nested_destructure_stmts);
                merged.insert(merged.end(),
                              std::make_move_iterator(body->stmts.begin()),
                              std::make_move_iterator(body->stmts.end()));
                body->stmts = std::move(merged);
            }
            // Prepend Hermes @-pattern prologue (helper __hp_N lets + user
            // binding lets) to body so bindings are live inside the arm body.
            if (!body_prologue.empty() || !body_binds.empty()) {
                std::vector<lir::LStmt> prologue = std::move(body_prologue);
                for (const auto& b : body_binds) {
                    lir::SLet sl;
                    sl.name = b.name; sl.type = anyval_t; sl.is_mut = false;
                    sl.value = builder().var_ref(b.av_var, anyval_t);
                    prologue.push_back(make_stmt_emit(node_line_, std::move(sl)));
                }
                body->stmts.insert(body->stmts.begin(),
                    std::make_move_iterator(prologue.begin()),
                    std::make_move_iterator(prologue.end()));
            }
            pop_scope();

            // Detect divergence: arm body's last stmt is a terminator.
            bool arm_diverges = false;
            if (!body->stmts.empty()) {
                auto br = stmt_ref_of(body->stmts.back());
                if (br) {
                    auto k = br.kind();
                    arm_diverges = (k == lir_schema::stmt::Code::Return ||
                                    k == lir_schema::stmt::Code::Break ||
                                    k == lir_schema::stmt::Code::Continue);
                }
            }
            if (!arm_diverges) {
                any_non_diverging = true;
                for (auto& m : moved_vars_) post_moves.insert(m);
            }

            smatch.arms.push_back({std::move(pat), std::move(body), std::move(guard)});
        }
        // Merge per-arm contributions back into moved_vars_.
        moved_vars_ = any_non_diverging ? std::move(post_moves) : std::move(pre_moves);
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
        if (TypeRef(scrut_type).kind() == LogosType::Kind::Enum) {
            auto [epkg_match, esi_match] = find_enum_by_name(TypeRef(scrut_type).enum_name());
            auto eit = esi_match ? enums_.find(sema_key(epkg_match, TypeRef(scrut_type).enum_name())) : enums_.end();
            if (eit == enums_.end()) eit = enums_.find(TypeRef(scrut_type).enum_name());
            if (eit != enums_.end()) {
                std::set<int32_t> covered;
                namespace ps = lir_schema::pat;
                auto add_pat_ref = [&](lir_view::PatRef pr) {
                    if (!pr) return;
                    auto k = pr.kind();
                    if (k == ps::Code::Variant)
                        covered.insert(static_cast<int32_t>(lir_view::PatVariantView{pr}.disc()));
                    else if (k == ps::Code::VariantData)
                        covered.insert(static_cast<int32_t>(lir_view::PatVariantDataView{pr}.disc()));
                };
                for (auto& arm : smatch.arms) {
                    if (arm.guard) continue;
                    auto apr = pat_ref_of(arm.pat);
                    if (apr.kind() == ps::Code::Or) {
                        lir_view::PatOrView{apr}.each_alt(
                            [&](lir_view::PatRef alt) { add_pat_ref(alt); });
                    } else {
                        add_pat_ref(apr);
                    }
                }
                if (!has_wild) {
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
                } else {
                    // B-st-07: wildcard arm is unreachable when every variant
                    // is already covered AND the enum has only unit variants
                    // (so PatVariant covers each fully). For payload variants,
                    // "covered" is a disc-only approximation that can't
                    // distinguish irrefutable from refutable inner patterns,
                    // so we conservatively skip the warning there.
                    bool all_unit = true;
                    for (auto& v : eit->second.variants) {
                        if (!v.payload_types.empty()) { all_unit = false; break; }
                    }
                    bool all_covered = true;
                    for (auto& v : eit->second.variants) {
                        if (covered.find(v.value) == covered.end()) {
                            all_covered = false;
                            break;
                        }
                    }
                    if (all_unit && all_covered && !eit->second.variants.empty())
                        warn("unreachable wildcard arm: every variant of the "
                             "enum is already covered explicitly");
                }
            }
        }
        if (!has_wild && TypeRef(scrut_type).kind() == LogosType::Kind::Bool) {
            bool has_true = false, has_false = false;
            for (auto& arm : smatch.arms) {
                if (arm.guard) continue;
                auto apr = pat_ref_of(arm.pat);
                if (apr.kind() == lir_schema::pat::Code::Bool) {
                    if (lir_view::PatBoolView{apr}.value()) has_true = true;
                    else has_false = true;
                }
            }
            if (!has_true || !has_false)
                error("match on bool is not exhaustive — missing "
                      + std::string(!has_true ? "true" : "false"));
        }
    }

    if (has_hoist_let) {
        auto blk = lir::alloc_block(*cur_prog_);
        blk->stmts.push_back(std::move(hoist_let_view));
        blk->stmts.push_back(std::move(hoist_let_root));
        blk->stmts.push_back(std::move(hoist_let_base));
        blk->stmts.push_back(make_stmt_emit(node_line_, std::move(smatch)));
        return make_stmt_emit(node_line_, lir::SBlock{std::move(blk)});
    }
    return make_stmt_emit(node_line_, std::move(smatch));
}

lir::LExprPtr SemaChecker::lower_match_expr(TinyMapView node) {
    lir::LExprPtr scrut = nullptr;
    TypeRef scrut_type = error_t();
    if (node.has_key(la::VALUE)) {
        scrut = lower_expr(map_of(node.get(la::VALUE.code)));
        scrut_type = scrut->type;
    } else { scrut = error_expr(); }

    // Sprint 5.2: arm-after-catchall lint (closes B-pt-07, expr position).
    if (node.has_key(la::ITEMS)) {
        auto arms_l = arr_of(node.get(la::ITEMS.code));
        bool seen_catchall = false;
        for (uint64_t i = 0; i < arms_l.size(); ++i) {
            auto arm = map_of(arms_l.get(i));
            if (code_of(arm) != la::MATCH_ARM) continue;
            if (seen_catchall) {
                error("unreachable match arm: a previous '_' arm matches all values");
                break;
            }
            if (is_catchall_pat(arm)) seen_catchall = true;
        }
    }

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
            hoist_let_view = make_stmt_emit(node_line_, std::move(sl));
        }
        anyval_t = make_datatype_type("AnyVal");
        root_var = "__hmatche_root_" + std::to_string(tmp_var_count_++);
        {
            auto view_ref = builder().var_ref(view_var, scrut_type);
            auto root_call = builder().method_call(std::move(view_ref), "root", "", {}, {}, -1, anyval_t);
            lir::SLet sl;
            sl.name = root_var; sl.type = anyval_t; sl.is_mut = false;
            sl.value = std::move(root_call);
            hoist_let_root = make_stmt_emit(node_line_, std::move(sl));
        }
        base_var = "__hmatche_base_" + std::to_string(tmp_var_count_++);
        {
            TypeRef u8_ptr_t = make_ptr(false, prim(LogosType::Kind::U8));
            auto view_ref = builder().var_ref(view_var, scrut_type);
            auto base_call = builder().method_call(std::move(view_ref), "base", "", {}, {}, -1, u8_ptr_t);
            lir::SLet sl;
            sl.name = base_var; sl.type = u8_ptr_t; sl.is_mut = false;
            sl.value = std::move(base_call);
            hoist_let_base = make_stmt_emit(node_line_, std::move(sl));
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

            lir::LExprPtr synth_guard = nullptr;
            std::vector<lir::LStmt> body_prologue;
            std::vector<HermesPatBinding> body_binds;
            if (has_hermes_pat && arm.has_key(la::LHS)) {
                std::vector<lir::LStmt> g_stmts;
                std::vector<HermesPatBinding> g_binds;
                auto raw = build_hermes_pat_guard(
                    map_of(arm.get(la::LHS.code)), root_var, anyval_t,
                    base_var, g_stmts, g_binds);
                if (!g_stmts.empty() && raw) {
                    auto blk = lir::alloc_block(*cur_prog_);
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

            // P4-pm-02: wire nested-pat side channel (same as stmt-form
            // lower_match above).
            in_match_hermes_ctx_ = has_hermes_pat;
            std::vector<NestedPatSub> nested_subs;
            auto* saved_pat_subs = current_pat_nested_subs_;
            current_pat_nested_subs_ = &nested_subs;
            logos::compiler::StrSet mut_names;
            auto* saved_pat_muts = current_pat_mut_names_;
            current_pat_mut_names_ = &mut_names;
            // P4-pm-01: capture refutable inner-pattern guards (variant
            // payload like `E::V { f: 1 }` or `Option::Some(1)`).
            std::vector<lir::LExprPtr> refut_guards;
            auto* saved_pat_refut = current_pat_refutable_guards_;
            current_pat_refutable_guards_ = &refut_guards;
            lir::Pattern pat = arm.has_key(la::LHS)
                ? build_pattern(map_of(arm.get(la::LHS.code)), scrut_type)
                : make_pat_wild("_");
            current_pat_nested_subs_ = saved_pat_subs;
            current_pat_refutable_guards_ = saved_pat_refut;
            in_match_hermes_ctx_ = false;

            push_scope();
            bind_pattern(pat, scrut_type);
            current_pat_mut_names_ = saved_pat_muts;
            for (const auto& b : body_binds) {
                define(b.name, anyval_t, /*is_mut=*/false);
            }
            // P4-pm-02: nested struct sub-pat destructure stmts.
            std::vector<lir::LStmt> nested_destructure_stmts;
            for (auto& nsub : nested_subs) {
                TypeRef synth_t = lookup(nsub.synth_name);
                if (!synth_t) continue;
                if (code_of(nsub.sub_pat_node) != la::PAT_STRUCT) continue;
                if (!nsub.sub_pat_node.has_key(la::ITEMS)) continue;
                auto fitems_av = nsub.sub_pat_node.get(la::ITEMS.code);
                if (!fitems_av.is_pointer()) continue;
                auto fitems_m = map_of(fitems_av);
                if (!fitems_m.has_key(la::ITEMS)) continue;
                auto fields = arr_of(fitems_m.get(la::ITEMS.code));
                std::string sname_s(TypeRef(synth_t).struct_name());
                auto [_skpkg, sinfo] = find_struct_by_name(sname_s);
                if (!sinfo) continue;
                for (uint64_t k = 0; k < fields.size(); ++k) {
                    auto fnode = map_of(fields.get(k));
                    if (!fnode.has_key(la::NAME)) continue;
                    std::string fname(str_of(fnode.get(la::NAME.code)));
                    std::string bind = fname;
                    if (fnode.has_key(la::VALUE)) {
                        auto sub = map_of(fnode.get(la::VALUE.code));
                        if (code_of(sub) == la::PAT_WILD && sub.has_key(la::NAME))
                            bind = std::string(str_of(sub.get(la::NAME.code)));
                    }
                    TypeRef ftype = error_t();
                    for (auto& sf : sinfo->fields)
                        if (sf.name == fname) { ftype = sf.type; break; }
                    define(bind, ftype);
                    auto sref = builder().var_ref(nsub.synth_name, synth_t);
                    auto fr = builder().field_read(std::move(sref), fname, ftype);
                    lir::SLet sl;
                    sl.name = bind; sl.type = ftype; sl.is_mut = false;
                    sl.value = std::move(fr);
                    nested_destructure_stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
                }
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
            lir::LExprPtr val = nullptr;
            if (arm.has_key(la::EXPR)) {
                val = lower_expr(map_of(arm.get(la::EXPR.code)));
            } else if (arm.has_key(la::BODY)) {
                auto body_node = map_of(arm.get(la::BODY.code));
                // B-fn-06: this is a match expression's arm body block; a
                // trailing TAIL_EXPR is the arm value, not an implicit return.
                bool saved_tail = tail_as_return_;
                tail_as_return_ = false;
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
                    auto blk_ptr = lir::alloc_block(*cur_prog_, std::move(blk));
                    val = builder().block_expr(std::move(blk_ptr), error_expr(), error_t());
                } else if (code_of(body_node) == la::BLOCK &&
                           body_node.has_key(la::ITEMS)) {
                    // Non-diverging block: last item must be an expression.
                    auto stmts = arr_of(body_node.get(la::ITEMS.code));
                    auto blk = lir::alloc_block(*cur_prog_);
                    lir::LExprPtr last_expr = nullptr;
                    for (uint64_t si = 0; si < stmts.size(); ++si) {
                        auto s = map_of(stmts.get(si));
                        if (si == stmts.size() - 1) {
                            int32_t sc = code_of(s);
                            if ((sc == la::EXPR_STMT || sc == la::TAIL_EXPR) && s.has_key(la::VALUE))
                                last_expr = lower_expr(map_of(s.get(la::VALUE.code)));
                            else if (sc != la::EXPR_STMT && sc != la::TAIL_EXPR && sc != la::LET &&
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
                tail_as_return_ = saved_tail;
            } else {
                error("match expression: arm has no body");
                val = error_expr();
            }
            // P4-pm-02: wrap arm value with nested-pat destructure stmts.
            if (!nested_destructure_stmts.empty()) {
                auto blk = lir::alloc_block(*cur_prog_);
                blk->stmts = std::move(nested_destructure_stmts);
                TypeRef vt = val ? val->type : error_t();
                val = builder().block_expr(std::move(blk), std::move(val), vt);
            }
            // Wrap arm value with Hermes @-pattern prologue so bindings are
            // live during evaluation.
            if (!body_prologue.empty() || !body_binds.empty()) {
                std::vector<lir::LStmt> prologue = std::move(body_prologue);
                for (const auto& b : body_binds) {
                    lir::SLet sl;
                    sl.name = b.name; sl.type = anyval_t; sl.is_mut = false;
                    sl.value = builder().var_ref(b.av_var, anyval_t);
                    prologue.push_back(make_stmt_emit(node_line_, std::move(sl)));
                }
                auto blk = lir::alloc_block(*cur_prog_);
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
                if (val) {
                    auto er = expr_ref_of(*val);
                    if (er.kind() == lir_schema::expr::Code::BlockExpr)
                        er = lir_view::EBlockExprView{er}.result();
                    if (er.kind() == lir_schema::expr::Code::LitInt) {
                        int64_t v = lir_view::ELitIntView{er}.value();
                        if (v > (int64_t)INT32_MAX || v < (int64_t)INT32_MIN)
                            result_type = prim(LogosType::Kind::I64);
                    }
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
                namespace ps = lir_schema::pat;
                auto add_pat2_ref = [&](lir_view::PatRef pr) {
                    if (!pr) return;
                    auto k = pr.kind();
                    if (k == ps::Code::Variant)
                        covered.insert(static_cast<int32_t>(lir_view::PatVariantView{pr}.disc()));
                    else if (k == ps::Code::VariantData)
                        covered.insert(static_cast<int32_t>(lir_view::PatVariantDataView{pr}.disc()));
                };
                for (auto& arm : me.arms) {
                    if (arm.guard) continue;
                    auto apr = pat_ref_of(arm.pat);
                    if (apr.kind() == ps::Code::Or) {
                        lir_view::PatOrView{apr}.each_alt(
                            [&](lir_view::PatRef alt) { add_pat2_ref(alt); });
                    } else {
                        add_pat2_ref(apr);
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
                auto apr = pat_ref_of(arm.pat);
                if (apr.kind() == lir_schema::pat::Code::Bool) {
                    if (lir_view::PatBoolView{apr}.value()) has_true = true;
                    else has_false = true;
                }
            }
            if (!has_true || !has_false)
                error("match on bool is not exhaustive — missing "
                      + std::string(!has_true ? "true" : "false"));
        }
    }

    auto me_expr = builder().match_expr_v(std::move(me), result_type);
    if (has_hoist_let) {
        auto blk = lir::alloc_block(*cur_prog_);
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
        return builder().stmt_break(nullptr, "", node_line_);
    }
    if (!is_ref_like(TypeRef(ptr_type).kind()) || !TypeRef(ptr_type).pointee()) {
        error(std::format("deref-field compound assign: '{}' is not a pointer (got {})",
                          recv_name, type_str(ptr_type)));
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return builder().stmt_break(nullptr, "", node_line_);
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
        return builder().stmt_break(nullptr, "", node_line_);
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
    return make_stmt_emit(node_line_, std::move(sdfw));
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
        return builder().stmt_break(nullptr, "", node_line_);
    }
    if (TypeRef(recv_t).kind() == LogosType::Kind::MutRef && TypeRef(recv_t).pointee())
        recv_t = TypeRef(recv_t).pointee();
    if (TypeRef(recv_t).kind() != LogosType::Kind::Tuple) {
        error(std::format("tuple field compound assign: '{}' is not a tuple (got {})",
                          recv_name, type_str(recv_t)));
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return builder().stmt_break(nullptr, "", node_line_);
    }
    if (idx >= TypeRef(recv_t).tuple_elems().size()) {
        error(std::format("tuple field compound assign: index {} out of range (tuple has {} elements)",
                          idx, TypeRef(recv_t).tuple_elems().size()));
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return builder().stmt_break(nullptr, "", node_line_);
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

    return make_stmt_emit(node_line_, lir::STupleWrite{std::string(recv_name), (uint32_t)idx,
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
        return builder().stmt_break(nullptr, "", node_line_);
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
    return make_stmt_emit(node_line_, std::move(sfiw));
}

} // namespace logos::compiler
