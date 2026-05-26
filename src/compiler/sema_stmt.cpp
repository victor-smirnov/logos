// Logos project — https://github.com/victor-smirnov/logos

#include "sema_impl.hpp"
#include "ctfe.hpp"
#include "logos/compiler/subtype.hpp"

#include <logos/hermes/type_registry.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
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
            if (callee == "panic") return true;
            // A call to any `-> !` (Never-returning) function diverges —
            // generalises the historical hand-coded `panic` name check now
            // that the never type exists (abort / exit / unreachable / a
            // user `-> !` fn all qualify).
            for (auto* fi : find_func_candidates(std::string(callee)))
                if (fi && fi->ret_type &&
                    TypeRef(fi->ret_type).kind() == LogosType::Kind::Never)
                    return true;
        }
        return false;
    };
    if ((c == la::EXPR_STMT || c == la::TAIL_EXPR) && stmt.has_key(la::VALUE)) {
        auto e = map_of(stmt.get(la::VALUE.code));
        if (is_divergent_call(e)) return true;
        // A bare block / if / match in expression-statement position diverges
        // if its body does — `{ return X; }` as a fn-body tail, etc.
        int32_t ec = code_of(e);
        if (ec == la::BLOCK) return block_always_returns(e);
        if (ec == la::IF || ec == la::MATCH) return stmt_always_returns(e);
        if (ec == la::RETURN_EXPR || ec == la::BREAK_EXPR || ec == la::CONTINUE_EXPR)
            return true;
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
    // A bare nested block `{ … return X; }` diverges if its body does. At
    // statement position a bare block parses as BLOCK_STMT (BODY = block);
    // la::BLOCK is the block node itself (reached via the VALUE recursion above
    // / direct calls). Handle both (G154-2).
    if (c == la::BLOCK_STMT) {
        return stmt.has_key(la::BODY) &&
               block_always_returns(map_of(stmt.get(la::BODY.code)));
    }
    if (c == la::BLOCK) return block_always_returns(stmt);
    // A `let x = <diverging>;` initializer (`let _x = return 7;`,
    // `let _x = if c { return } else { return }`) never binds — control leaves
    // the function via the initializer, so the let diverges (G154-2). Mirrors
    // the EXPR_STMT/TAIL_EXPR value handling above.
    if (c == la::LET && stmt.has_key(la::VALUE)) {
        auto e = map_of(stmt.get(la::VALUE.code));
        int32_t ec = code_of(e);
        if (ec == la::RETURN_EXPR || ec == la::BREAK_EXPR ||
            ec == la::CONTINUE_EXPR || is_divergent_call(e))
            return true;
        if (ec == la::BLOCK) return block_always_returns(e);
        if (ec == la::IF || ec == la::MATCH) return stmt_always_returns(e);
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

// A fresh owned rvalue (not a place / borrow) — the kinds whose materialized
// temporary needs a statement-scope drop. Mirrors the B140-G1 is_place check.
bool SemaChecker::is_hoistable_temp_rvalue(const lir::LExpr& e) {
    namespace ec = lir_schema::expr;
    auto k = expr_ref_of(e).kind();
    switch (k) {
        case ec::Code::VarRef: case ec::Code::FieldRead: case ec::Code::IndexRead:
        case ec::Code::Deref:  case ec::Code::TupleIndex: case ec::Code::SliceIndex:
        case ec::Code::SlicePtr: case ec::Code::AddrOf: case ec::Code::AddrOfTemp:
            return false;   // a place / existing-owned borrow — not a fresh temp
        default:
            return true;    // Call / MethodCall / StructLit / EnumLitData / …
    }
}

lir::LStmt SemaChecker::lower_stmt(TinyMapView stmt) {
    // Install a temporary-scope collector for this statement (save/restore across
    // the recursion below — LABELED_LOOP and loop bodies re-enter lower_stmt).
    std::vector<std::tuple<std::string, TypeRef, lir::LExprPtr>> hoisted;
    auto* saved_hoist = cur_stmt_temp_hoist_;
    cur_stmt_temp_hoist_ = &hoisted;
    lir::LStmt s = lower_stmt_inner(stmt);
    cur_stmt_temp_hoist_ = saved_hoist;
    if (hoisted.empty()) return s;
    // Wrap: `{ let __t0 = v0; …; <stmt>; drop __tN; … drop __t0; }`. The hoisted
    // temporaries are bound to fresh locals, the statement runs (borrowing them),
    // then their destructors run at the end of this statement — Rust's temporary
    // scope. Drops are emitted in REVERSE binding order. (The drop convention is
    // explicit SDrop statements inserted by sema — mlir-gen does not auto-drop
    // block-scoped locals — so we must emit them here.)
    auto blk = lir::alloc_block(*cur_prog_);
    std::vector<lir::LStmt> drops;
    for (auto& h : hoisted) {
        std::string nm = std::move(std::get<0>(h));
        TypeRef ty = std::get<1>(h);
        lir::SLet sl;
        sl.name = nm; sl.type = ty; sl.is_mut = false;
        sl.value = std::move(std::get<2>(h));
        blk->stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
        if (auto d = make_drop_stmt(nm, VarInfo{ty, false}))
            drops.push_back(std::move(*d));
    }
    blk->stmts.push_back(std::move(s));
    for (auto it = drops.rbegin(); it != drops.rend(); ++it)
        blk->stmts.push_back(std::move(*it));
    lir::SBlock sb; sb.body = std::move(blk);
    return make_stmt_emit(node_line_, std::move(sb));
}

lir::LStmt SemaChecker::lower_stmt_inner(TinyMapView stmt) {
    node_line_ = get_line(stmt);
    int32_t c = code_of(stmt);

    if (c == la::LET)          return lower_let(stmt);
    if (c == la::LET_ELSE)     return lower_let_else(stmt);
    if (c == la::LET_DESTRUCT) return lower_let_destruct(stmt);
    if (c == la::LET_PAT)      return lower_let_pat(stmt);
    if (c == la::NESTED_FN)    return lower_nested_fn(stmt);
    if (c == la::ASSIGN)          return lower_assign(stmt);
    if (c == la::DESTRUCTURE_ASSIGN) return lower_destructure_assign(stmt);
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
    if (c == la::PLACE_ASSIGN)       return lower_place_assign(stmt);
    if (c == la::MATCH)        return lower_match(stmt);
    if (c == la::EXPR_STMT) {
        lir::LExprPtr e = stmt.has_key(la::VALUE)
            ? lower_expr(map_of(stmt.get(la::VALUE.code)))
            : error_expr();
        // B140-G1: a discarded statement-expression that produces a FRESH owned
        // droppable value (`make(p);`) must run its destructor — Rust drops the
        // temporary at the end of the statement. Bind it to a synth local and
        // emit the drop immediately. Restricted to rvalue-producing expr kinds
        // (not place expressions like VarRef/FieldRead/Index/Deref), so a bare
        // `existing_var;` move isn't double-dropped against its scope drop.
        if (e && e->type && is_move_type(e->type)) {
            namespace ec = lir_schema::expr;
            auto ek = expr_ref_of(*e).kind();
            bool is_place = ek == ec::Code::VarRef || ek == ec::Code::FieldRead ||
                            ek == ec::Code::IndexRead || ek == ec::Code::Deref ||
                            ek == ec::Code::TupleIndex || ek == ec::Code::SliceIndex ||
                            ek == ec::Code::SlicePtr || ek == ec::Code::AddrOf ||
                            ek == ec::Code::AddrOfTemp;
            if (!is_place) {
                std::string synth = std::format("__stmt_tmp_{}", destruct_counter_++);
                if (auto drop = make_drop_stmt(synth, VarInfo{e->type, false})) {
                    auto blk = lir::alloc_block(*cur_prog_);
                    lir::SLet sl;
                    sl.name = synth; sl.type = e->type; sl.is_mut = false;
                    sl.value = std::move(e);
                    blk->stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
                    blk->stmts.push_back(std::move(*drop));
                    lir::SBlock sb; sb.body = std::move(blk);
                    return make_stmt_emit(node_line_, std::move(sb));
                }
            }
        }
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
        std::string break_label;
        if (stmt.has_key(la::LABEL))
            break_label = std::string(str_of(stmt.get(la::LABEL.code)));
        if (!break_label.empty() &&
            std::find(active_loop_labels_.begin(), active_loop_labels_.end(),
                      break_label) == active_loop_labels_.end()) {
            error(std::format("'break {}': label not in scope", break_label));
        }
        // Resolve the target frame: the matching label (search innermost-out)
        // or the innermost loop for an unlabeled break. The break value
        // attributes to the TARGET, so a value breaking to an outer labeled
        // `loop` isn't consumed by an inner `loop`.
        LoopBreakFrame* target = nullptr;
        if (!loop_break_frames_.empty()) {
            if (break_label.empty()) {
                target = &loop_break_frames_.back();
            } else {
                for (auto it = loop_break_frames_.rbegin();
                     it != loop_break_frames_.rend(); ++it)
                    if (it->label == break_label) { target = &*it; break; }
            }
        }
        lir::LExprPtr bval = nullptr;
        if (stmt.has_key(la::VALUE)) {
            bval = lower_expr(map_of(stmt.get(la::VALUE.code)));
            if (target && target->without_value) {
                error("loop break mixes value and no-value breaks");
            } else if (target && bval && bval->type &&
                       TypeRef(bval->type).kind() != LogosType::Kind::Error) {
                if (!target->value_type) {
                    target->value_type = bval->type;
                } else if (!types_compatible(bval->type, target->value_type) &&
                           !types_compatible(target->value_type, bval->type)) {
                    error(std::format("loop break values have incompatible types: {} vs {}",
                          type_str(target->value_type), type_str(bval->type)));
                } else {
                    target->value_type = unify_numeric(target->value_type, bval->type);
                }
            }
        } else {
            if (target && target->value_type)
                error("loop break mixes value and no-value breaks");
            if (target) target->without_value = true;
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
        // User-defined DerefMut dispatch: `*x = v` for struct x where
        // x impls DerefMut<T> → call x.deref_mut() (returns &mut T),
        // then emit `*<that &mut T> = v`. Mirrors the read-side path
        // in sema_expr.cpp::lower_deref. Requires `let mut x`-style
        // mutable binding for `&mut self` materialisation.
        if (TypeRef(pt).kind() == LogosType::Kind::Struct) {
            auto type_name = concrete_struct_name(pt);
            auto base_name = std::string(TypeRef(pt).struct_name());
            bool has_dm = impls_.count("DerefMut::" + type_name) ||
                          (!base_name.empty() &&
                           impls_.count("DerefMut::" + base_name));
            if (has_dm) {
                auto mangled = type_name + "__deref_mut";
                auto mut_ref_t = make_ref(true, pt);
                auto recv_ref = builder().addr_of_temp(std::move(ptr), true, mut_ref_t);
                std::vector<lir::LExprPtr> args;
                args.push_back(std::move(recv_ref));
                auto fit = find_func_by_base_and_signature(mangled, {mut_ref_t}, false);
                if (fit) {
                    auto call_e = builder().call(
                        fit->symbol_name.empty() ? mangled : fit->symbol_name,
                        {}, std::move(args), fit->ret_type);
                    track_write_move(val);
                    return builder().stmt_deref_write(
                        std::move(call_e), std::move(val), node_line_);
                }
            }
        }
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
    // G167-4: if a loop just armed this, tag the body frame as the loop
    // boundary for break/continue drop-glue (consume the one-shot flag).
    if (pending_loop_body_scope_) {
        scope_.back().loop_boundary = true;
        pending_loop_body_scope_ = false;
    }
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
                    // G167-4: drop every frame down to AND INCLUDING the loop
                    // body — a break/continue nested in an `if` exits via the
                    // loop edge, bypassing the body block's normal end drops.
                    for (auto& d : collect_drops_to_loop())
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

    auto blk = lir::alloc_block(*cur_prog_);

    // let __destruct_N = rhs
    std::string tmp = std::format("__destruct_{}", destruct_counter_++);
    define(tmp, rhs_type);
    lir::SLet tmp_let;
    tmp_let.name    = tmp;
    tmp_let.type    = rhs_type;
    tmp_let.is_mut  = false;
    tmp_let.value   = std::move(rhs);
    blk->stmts.push_back(make_stmt_emit(node_line_, std::move(tmp_let)));

    // Recursively bind a tuple-binding list against a source expr of tuple type.
    // Each element is either a PAT_WILD (leaf name binding) or a PAT_TUPLE
    // (nested `(b, c)`), stored under NAMES — closes nested `let (a,(b,c)) = …`.
    std::vector<std::string> all_names;  // for uniqueness check
    std::function<void(TinyMapView, lir::LExprPtr, TypeRef)> bind_list =
        [&](TinyMapView nlist, lir::LExprPtr src, TypeRef src_ty) {
        if (!nlist.has_key(la::ITEMS)) return;
        auto arr = arr_of(nlist.get(la::ITEMS.code));
        size_t arity = TypeRef(src_ty).kind() == LogosType::Kind::Tuple
                           ? TypeRef(src_ty).tuple_elems().size() : 0;
        // G140-4: a single `..` rest absorbs the unmatched middle positions.
        // Names before the rest bind low positions; names after bind the tail.
        int rest_idx = -1;
        size_t n_named = 0;
        for (uint64_t i = 0; i < arr.size(); ++i) {
            if (code_of(map_of(arr.get(i))) == la::PAT_REST) {
                if (rest_idx >= 0)
                    error("let (...) = ...: at most one `..` rest allowed");
                rest_idx = (int)i;
            } else {
                ++n_named;
            }
        }
        if (rest_idx < 0) {
            if (arr.size() != arity)
                error(std::format("let (...) = ...: expected {} bindings, got {}",
                      arity, arr.size()));
        } else if (n_named > arity) {
            error(std::format("let (...) = ...: {} bindings exceed tuple arity {}",
                  n_named, arity));
        }
        // Spill the source to a temp so each element read references it once.
        std::string src_tmp = std::format("__destruct_{}", destruct_counter_++);
        define(src_tmp, src_ty);
        lir::SLet sl;
        sl.name = src_tmp; sl.type = src_ty; sl.is_mut = false; sl.value = std::move(src);
        blk->stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
        // names after the rest occupy the tail: first such name maps to
        // position (arity - trailing_count).
        size_t trailing = rest_idx < 0 ? 0 : (arr.size() - 1 - (size_t)rest_idx);
        for (uint64_t i = 0; i < arr.size(); ++i) {
            auto bnode = map_of(arr.get(i));
            if (code_of(bnode) == la::PAT_REST) continue;
            // Map pattern-item index → tuple position.
            size_t pos;
            if (rest_idx < 0 || (int)i < rest_idx) {
                pos = i;                                  // before the rest
            } else {
                size_t after_k = i - (size_t)rest_idx - 1;  // 0-based after rest
                pos = arity - trailing + after_k;
            }
            if (pos >= arity) continue;
            auto elem_t = TypeRef(src_ty).tuple_elems()[pos];
            auto elem_expr = builder().tuple_index(
                builder().var_ref(src_tmp, src_ty), (uint32_t)pos, elem_t);
            if (code_of(bnode) == la::PAT_TUPLE && bnode.has_key(la::NAMES)) {
                // Nested tuple — recurse.
                bind_list(map_of(bnode.get(la::NAMES.code)), std::move(elem_expr), elem_t);
            } else {
                std::string nm(str_of(bnode.get(la::NAME.code)));
                all_names.push_back(nm);
                define(nm, elem_t);
                lir::SLet el;
                el.name = nm; el.type = elem_t; el.is_mut = false; el.value = std::move(elem_expr);
                blk->stmts.push_back(make_stmt_emit(node_line_, std::move(el)));
            }
        }
    };
    if (node.has_key(la::NAMES))
        bind_list(map_of(node.get(la::NAMES.code)),
                  builder().var_ref(tmp, rhs_type), rhs_type);
    // Tuple-pattern binding-name uniqueness (closes B-pt-01)
    check_unique_names(all_names,
                       [](auto& n) -> std::string_view { return n; },
                       "binding", "let (...) destructure");

    lir::SBlock sb;
    sb.body = std::move(blk);
    return make_stmt_emit(node_line_, std::move(sb));
}

// G149-7 (RFC 2909): destructuring assignment into EXISTING places.
//   (a, b) = e;          →  let __da = e; a = __da.0; b = __da.1;
//   [a, b] = e;          →  let __da = e; a = __da[0]; b = __da[1];
//   S { x: a, y } = e;   →  let __da = e; a = __da.x; y = __da.y;
// `_` places discard (evaluate the accessor for effect). Nested tuple places
// (`(a, (b, c)) = …`) recurse. Each place must be an existing mutable local
// (reuses lower_assign's mutability/undefined checks via stmt_assign).
lir::LStmt SemaChecker::lower_destructure_assign(TinyMapView node) {
    int op = 0;
    if (node.has_key(la::OP)) {
        AnyVal av = node.get(la::OP.code);
        if (!av.is_null() && av.is_value()) op = (int)av.as_value<int32_t>();
    }
    lir::LExprPtr rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    TypeRef rhs_type = rhs->type;
    auto blk = lir::alloc_block(*cur_prog_);
    std::string tmp = std::format("__da_{}", destruct_counter_++);
    define(tmp, rhs_type);
    {
        lir::SLet sl;
        sl.name = tmp; sl.type = rhs_type; sl.is_mut = false; sl.value = std::move(rhs);
        blk->stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
    }
    // Assign an accessor expr into a place name (or discard for `_`).
    auto assign_place = [&](const std::string& nm, lir::LExprPtr acc) {
        if (nm == "_" || nm.empty()) {
            blk->stmts.push_back(builder().stmt_expr(std::move(acc), node_line_));
            return;
        }
        auto vt = lookup(nm);
        if (!vt) { error(std::format(
            "destructuring assignment to undefined variable '{}'", nm)); return; }
        if (!lookup_is_mut(nm))
            error(std::format("assignment to immutable variable '{}'", nm));
        blk->stmts.push_back(builder().stmt_assign(nm, std::move(acc), node_line_));
    };

    if (op == 2) {
        // Struct form. FIELDS is a pat_field_list { ITEMS: [PAT_FIELD…] }.
        if (TypeRef(rhs_type).kind() != LogosType::Kind::Struct &&
            TypeRef(rhs_type).kind() != LogosType::Kind::ZonedStruct) {
            if (TypeRef(rhs_type).kind() != LogosType::Kind::Error)
                error(std::format("destructuring assignment: right-hand side "
                                  "must be a struct, got {}", type_str(rhs_type)));
        } else if (node.has_key(la::FIELDS)) {
            std::string sname(TypeRef(rhs_type).struct_name());
            auto sinfo = find_struct_by_name(sname).second;
            if (!sinfo) sinfo = find_datatype_by_name(sname).second;
            auto flist = map_of(node.get(la::FIELDS.code));
            if (flist.has_key(la::ITEMS)) {
                auto items = arr_of(flist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto fnode = map_of(items.get(i));
                    if (code_of(fnode) == la::PAT_REST) continue;
                    if (!fnode.has_key(la::NAME)) continue;
                    std::string fname(str_of(fnode.get(la::NAME.code)));
                    std::string place = fname;  // shorthand `{ x }`
                    if (fnode.has_key(la::VALUE)) {
                        auto sub = map_of(fnode.get(la::VALUE.code));
                        if (code_of(sub) == la::PAT_WILD && sub.has_key(la::NAME))
                            place = std::string(str_of(sub.get(la::NAME.code)));
                    }
                    TypeRef ftype = error_t();
                    if (sinfo) for (auto& sf : sinfo->fields)
                        if (sf.name == fname) { ftype = sf.type; break; }
                    auto acc = builder().field_read(
                        builder().var_ref(tmp, rhs_type), fname, ftype);
                    assign_place(place, std::move(acc));
                }
            }
        }
    } else {
        // Tuple (op 0) / array (op 1). NAMES is a pat_binding_list { ITEMS:[…] }.
        bool is_array = (op == 1);
        std::function<void(TinyMapView, lir::LExprPtr, TypeRef)> bind_list =
            [&](TinyMapView nlist, lir::LExprPtr src, TypeRef src_ty) {
            if (!nlist.has_key(la::ITEMS)) return;
            auto arr = arr_of(nlist.get(la::ITEMS.code));
            bool src_is_tuple = TypeRef(src_ty).kind() == LogosType::Kind::Tuple;
            bool src_is_array = TypeRef(src_ty).kind() == LogosType::Kind::Array;
            size_t arity = src_is_tuple ? TypeRef(src_ty).tuple_elems().size()
                         : src_is_array ? (size_t)TypeRef(src_ty).arr_size() : 0;
            // G160-6: redundant parens around a tuple place — `((a, b)) = …` is
            // equivalent to `(a, b) = …`. When the place-list is a single nested
            // tuple and the source arity isn't 1 (so the 1-tuple reading can't
            // be intended), unwrap the outer parens and recurse on the inner.
            if (arr.size() == 1 && arity != 1) {
                auto only = map_of(arr.get(0));
                if (code_of(only) == la::PAT_TUPLE && only.has_key(la::NAMES)) {
                    bind_list(map_of(only.get(la::NAMES.code)), std::move(src), src_ty);
                    return;
                }
            }
            // A single `..` rest absorbs the unmatched middle positions; places
            // before it map to low positions, places after it to the tail.
            int rest_idx = -1;
            size_t n_named = 0;
            for (uint64_t i = 0; i < arr.size(); ++i) {
                if (code_of(map_of(arr.get(i))) == la::PAT_REST) {
                    if (rest_idx >= 0)
                        error("destructuring assignment: at most one `..` rest allowed");
                    rest_idx = (int)i;
                } else ++n_named;
            }
            if (rest_idx < 0) {
                if (arity && arr.size() != arity)
                    error(std::format("destructuring assignment: expected {} places, got {}",
                          arity, arr.size()));
            } else if (arity && n_named > arity) {
                error(std::format("destructuring assignment: {} places exceed arity {}",
                      n_named, arity));
            }
            size_t trailing = rest_idx < 0 ? 0 : (arr.size() - 1 - (size_t)rest_idx);
            // Spill the source once.
            std::string src_tmp = std::format("__da_{}", destruct_counter_++);
            define(src_tmp, src_ty);
            {
                lir::SLet sl;
                sl.name = src_tmp; sl.type = src_ty; sl.is_mut = false; sl.value = std::move(src);
                blk->stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
            }
            for (uint64_t i = 0; i < arr.size(); ++i) {
                auto bnode = map_of(arr.get(i));
                if (code_of(bnode) == la::PAT_REST) continue;  // rest skip
                size_t pos;
                if (rest_idx < 0 || (int)i < rest_idx) pos = i;
                else pos = arity - trailing + (i - (size_t)rest_idx - 1);
                if (arity && pos >= arity) continue;
                TypeRef elem_t = error_t();
                lir::LExprPtr acc;
                if (src_is_array && TypeRef(src_ty).elem()) {
                    elem_t = TypeRef(src_ty).elem();
                    acc = builder().index_read(
                        builder().var_ref(src_tmp, src_ty),
                        builder().lit_int((int64_t)pos, prim(LogosType::Kind::I64)), elem_t);
                } else if (src_is_tuple && pos < TypeRef(src_ty).tuple_elems().size()) {
                    elem_t = TypeRef(src_ty).tuple_elems()[pos];
                    acc = builder().tuple_index(
                        builder().var_ref(src_tmp, src_ty), (uint32_t)pos, elem_t);
                } else {
                    continue;
                }
                if (code_of(bnode) == la::PAT_TUPLE && bnode.has_key(la::NAMES)) {
                    bind_list(map_of(bnode.get(la::NAMES.code)), std::move(acc), elem_t);
                } else {
                    std::string nm(bnode.has_key(la::NAME)
                                   ? std::string(str_of(bnode.get(la::NAME.code))) : "_");
                    assign_place(nm, std::move(acc));
                }
            }
        };
        if (node.has_key(la::NAMES)) {
            if (is_array && TypeRef(rhs_type).kind() != LogosType::Kind::Array &&
                TypeRef(rhs_type).kind() != LogosType::Kind::Error)
                error(std::format("destructuring assignment: right-hand side "
                                  "must be an array, got {}", type_str(rhs_type)));
            if (!is_array && TypeRef(rhs_type).kind() != LogosType::Kind::Tuple &&
                TypeRef(rhs_type).kind() != LogosType::Kind::Error)
                error(std::format("destructuring assignment: right-hand side "
                                  "must be a tuple, got {}", type_str(rhs_type)));
            bind_list(map_of(node.get(la::NAMES.code)),
                      builder().var_ref(tmp, rhs_type), rhs_type);
        }
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
                    // G152-15: a single `..` rest (`let Foo(a, b, ..)` /
                    // `Foo(.., z)` / `Foo(a, .., z)`) skips the unmatched middle
                    // positions — names before the rest bind low fields, names
                    // after bind the tail. (Match arms already support this,
                    // G151-2; the `let` path didn't.)
                    size_t arity = tsi_let->fields.size();
                    int rest_idx = -1; size_t named = 0;
                    for (uint64_t j = 0; j < bitems.size(); ++j) {
                        if (code_of(map_of(bitems.get(j))) == la::PAT_REST) {
                            if (rest_idx >= 0) error("tuple-struct `let` pattern: only one `..` allowed");
                            rest_idx = (int)j;
                        } else ++named;
                    }
                    arg_n = rest_idx < 0 ? bitems.size() : arity;
                    if (named > arity)
                        error(std::format("tuple-struct pattern '{}': {} bindings exceed {} fields",
                                          sname, named, arity));
                    size_t trailing = rest_idx < 0 ? 0 : (bitems.size() - 1 - (size_t)rest_idx);
                    for (uint64_t j = 0; j < bitems.size(); ++j) {
                        auto bnode = map_of(bitems.get(j));
                        int32_t bc = code_of(bnode);
                        if (bc == la::PAT_REST) continue;
                        // Map pattern item j → field position (rest-aware).
                        size_t fpos;
                        if (rest_idx < 0 || (int)j < rest_idx) fpos = j;
                        else fpos = arity - trailing + (j - (size_t)rest_idx - 1);
                        if (fpos >= arity) continue;
                        // PAT_WILD bindings only for now; underscore skips.
                        if (bc == la::PAT_WILD && bnode.has_key(la::NAME)) {
                            auto vname = std::string(str_of(bnode.get(la::NAME.code)));
                            if (vname == "_") continue;
                            auto ftype = tsi_let->fields[fpos].type;
                            lir::SLet sl;
                            sl.name   = vname;
                            sl.type   = ftype;
                            sl.is_mut = false;
                            sl.value  = builder().field_read(
                                builder().var_ref(tmp, rhs_type),
                                std::to_string(fpos), ftype);
                            define(vname, ftype);
                            blk->stmts.push_back(
                                make_stmt_emit(node_line_, std::move(sl)));
                        } else {
                            error(std::format(
                                "tuple-struct `let` pattern: only plain identifier "
                                "bindings are supported (got nested pattern at field {})",
                                fpos));
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
    // `let __dst = rhs` consumes rhs — mark the source moved (lower_let does
    // this for a plain `let`; this manual SLet must too, else the source AND
    // the destructured field bindings both drop the same buffer → double-free).
    if (is_move_type(rhs_type)) mark_moved_expr(expr_ref_of(*rhs));
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
    // The destructure moved the struct's fields into the bindings (each field
    // binding owns the field value, e.g. a String's buffer ptr). The source
    // temp must NOT also drop those fields at scope exit — that double-frees.
    // A destructure consumes the whole value, so mark the temp moved (its
    // scope-exit Drop is suppressed; the field bindings own + drop the data).
    if (is_move_type(rhs_type)) mark_moved(tmp);
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
    // G161-3: collect refutable-inner guard exprs (`__refut_N == value` for
    // `let Some(1) = … else`). build_pattern's synth_refutable_inner pushes them
    // here; the SLetElse carries them so codegen tests each AFTER the bindings
    // are bound (else the inner literal test was silently dropped — only the
    // variant discriminant was checked).
    std::vector<lir::LExprPtr> refut_guards;
    auto* saved_pat_refut = current_pat_refutable_guards_;
    current_pat_refutable_guards_ = &refut_guards;
    lir::Pattern pat = build_pattern(pat_node, scrut_type);
    current_pat_refutable_guards_ = saved_pat_refut;

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
        auto* pool = cur_prog_->type_pool.impl();
        std::function<void(lir_view::PatRef)> define_bindings =
            [&](lir_view::PatRef pr) {
            if (pr.kind() == ps::Code::VariantData) {
                lir_view::PatVariantDataView v{pr};
                std::vector<std::string_view> names;
                std::vector<TypeRef> types;
                v.each_binding([&](std::string_view n) { names.push_back(n); });
                v.each_binding_type(pool, [&](TypeRef t) { types.push_back(t); });
                for (size_t i = 0; i < names.size() && i < types.size(); ++i)
                    if (names[i] != "_")
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
            } else if (pr.kind() == ps::Code::Or) {
                // G144-3a: `let A(x) | B(x) = v else …`. All alts bind the same
                // names+types (build_pattern_or enforced this), so define from
                // the first alt. SLetElse codegen now OR's the alt discriminant
                // tests and extracts via the first alt's payload layout.
                lir_view::PatRef first;
                lir_view::PatOrView{pr}.each_alt([&](lir_view::PatRef a){ if (!first) first = a; });
                if (first) define_bindings(first);
            }
            // PatVariant (no bindings) — nothing to define
        };
        define_bindings(pat_ref_of(pat));
    }

    // 5. Emit SLetElse
    lir::SLetElse sle;
    sle.pat        = std::move(pat);
    sle.scrut      = std::move(scrut);
    sle.else_block = lir::alloc_block(*cur_prog_, std::move(else_blk));
    sle.guards     = std::move(refut_guards);   // G161-3
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
    // G151-3: a fn-ptr/closure-annotated let hints the closure formal so an
    // untyped closure literal (`let f: fn(i64)->i64 = |x| x+1`) infers its
    // param types (was `|<error>|`). Mirrors the call-arg + return paths.
    auto saved_closure_hint = hint_closure_formal_;
    if (ann && (TypeRef(ann).kind() == LogosType::Kind::FnPtr ||
                TypeRef(ann).kind() == LogosType::Kind::Closure))
        hint_closure_formal_ = ann;
    // g6b: `[T; N]` / `[T]` annotation hints the array literal's element type
    // so a heterogeneous `[&dyn Trait]` (distinct concrete refs) is accepted.
    auto saved_arr_elem_hint = hint_arr_elem_type_;
    {
        // Peel a `&[T]` / `&mut [T]` annotation to the underlying slice/array so
        // a borrowed array literal (`let s: &[u64] = &[];`) gets its element
        // hint too — not just a bare `[T; N]` / `[T]` annotation.
        TypeRef ah = ann;
        if (ah && (TypeRef(ah).kind() == LogosType::Kind::Ref ||
                   TypeRef(ah).kind() == LogosType::Kind::MutRef) &&
            TypeRef(ah).pointee())
            ah = TypeRef(ah).pointee();
        if (ah && (TypeRef(ah).kind() == LogosType::Kind::Array ||
                   TypeRef(ah).kind() == LogosType::Kind::Slice) &&
            TypeRef(ah).elem())
            hint_arr_elem_type_ = TypeRef(ah).elem();
    }
    // A `(i64, i64)` annotation hints a tuple-literal rhs's element types so
    // untyped int literals widen instead of defaulting to i32 (`let p:(i64,i64)
    // = (7, 2)`). Mirrors the call-arg tuple hint; consumed by TUPLE_LIT lowering.
    auto saved_tuple_hint = hint_tuple_type_;
    if (ann && TypeRef(ann).kind() == LogosType::Kind::Tuple)
        hint_tuple_type_ = ann;

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
                    hint_closure_formal_ = saved_closure_hint;
                    hint_tuple_type_ = saved_tuple_hint;
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
    hint_closure_formal_ = saved_closure_hint;
    hint_arr_elem_type_ = saved_arr_elem_hint;
    hint_tuple_type_ = saved_tuple_hint;

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
        if (rhs) {
            rhs->type = ann;
            lir_mirror_update_type(*cur_prog_, *rhs);
        }
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

// Map a base operator (`+`, `<<`, …) to its `*Assign` trait + method for the
// operator-overload (in-place) compound-assign dispatch. Shared by the bare-var
// and general-place compound paths.
static bool op_assign_trait_method(const std::string& base_op,
                                   std::string& trait, std::string& method) {
    if      (base_op == "+")  { trait = "AddAssign";    method = "add_assign"; }
    else if (base_op == "-")  { trait = "SubAssign";    method = "sub_assign"; }
    else if (base_op == "*")  { trait = "MulAssign";    method = "mul_assign"; }
    else if (base_op == "/")  { trait = "DivAssign";    method = "div_assign"; }
    else if (base_op == "%")  { trait = "RemAssign";    method = "rem_assign"; }
    else if (base_op == "&")  { trait = "BitAndAssign"; method = "bitand_assign"; }
    else if (base_op == "|")  { trait = "BitOrAssign";  method = "bitor_assign"; }
    else if (base_op == "^")  { trait = "BitXorAssign"; method = "bitxor_assign"; }
    else if (base_op == "<<") { trait = "ShlAssign";    method = "shl_assign"; }
    else if (base_op == ">>") { trait = "ShrAssign";    method = "shr_assign"; }
    else return false;
    return true;
}

lir::LStmt SemaChecker::lower_compound_assign(TinyMapView node) {
    auto op_tok = str_of(node.get(la::OP.code));
    // Strip trailing '=' to get the base operator
    std::string base_op;
    if (op_tok.size() >= 2 && op_tok.back() == '=')
        base_op = std::string(op_tok.substr(0, op_tok.size() - 1));
    else
        base_op = std::string(op_tok);  // fallback

    // The collapsed grammar (`atom compound_op expr`) puts the place in RECEIVER.
    // A bare VAR_REF takes the simple-var fast path; any other place (field /
    // index / tuple-field / chain / `(*p).f`) routes through the general place
    // path below (read-twice desugar — matches the specialised lowerings'
    // double-eval semantics this collapses).
    TinyMapView place_node = map_of(node.get(la::RECEIVER.code));
    if (code_of(place_node) != la::VAR_REF)
        return lower_place_compound_assign(node, place_node, base_op);

    auto name = str_of(place_node.get(la::NAME.code));
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

    // User-defined *Assign dispatch: `x op= rhs` for a user-typed struct
    // x with `impl OpAssign for X` → emit `X__op_assign(&mut x, rhs)` as
    // a void-returning call (in-place mutation, no assign-back).
    // Mirrors the unary-op / binary-op overload patterns. Without the
    // impl, falls through to the existing `x = x op rhs` desugar that
    // dispatches via Add/Sub/etc. (creates a fresh Self).
    if (TypeRef(var_type).kind() == LogosType::Kind::Struct) {
        std::string assign_trait, assign_method;
        op_assign_trait_method(base_op, assign_trait, assign_method);
        if (!assign_trait.empty()) {
            auto type_name = concrete_struct_name(var_type);
            auto base_name = std::string(TypeRef(var_type).struct_name());
            bool has_impl = impls_.count(assign_trait + "::" + type_name) ||
                            (!base_name.empty() &&
                             impls_.count(assign_trait + "::" + base_name));
            if (has_impl) {
                auto mangled = type_name + "__" + assign_method;
                auto mut_ref_t = make_ref(true, var_type);
                auto recv = builder().addr_of(std::string(name), mut_ref_t);
                // G160-5: the `*Assign<Rhs>` method's second param is the
                // trait's Rhs type-arg, which need NOT equal Self. Look it up by
                // the actual rhs operand type (`x <<= 1u8` over `impl
                // ShlAssign<u8> for Int` → `Int__shl_assign(&mut Int, u8)`).
                // Fall back to the Self-RHS signature if the rhs-typed one
                // doesn't resolve (covers an IntLit rhs against a Self-RHS impl).
                TypeRef rhs_ty = rhs ? TypeRef(rhs->type) : TypeRef(var_type);
                auto fit = find_func_by_base_and_signature(
                    mangled, {mut_ref_t, rhs_ty}, false);
                if (!fit && !types_equal(rhs_ty, var_type))
                    fit = find_func_by_base_and_signature(
                        mangled, {mut_ref_t, var_type}, false);
                if (fit) {
                    std::vector<lir::LExprPtr> args;
                    args.push_back(std::move(recv));
                    args.push_back(std::move(rhs));
                    auto call = builder().call(
                        fit->symbol_name.empty() ? mangled : fit->symbol_name,
                        {}, std::move(args), fit->ret_type);
                    return builder().stmt_expr(std::move(call), node_line_);
                }
            }
        }
    }

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

// Render a place AST node to its source-like form (`p.x`, `arr[i]`, `t.0`,
// `b.data[i]`, `(*p).x`) for diagnostics. Mirrors the names the former per-shape
// compound lowerings produced.
std::string SemaChecker::render_place_node(hermes::TinyMapView n) {
    if (n.is_null()) return "<place>";
    int32_t c = code_of(n);
    if (c == la::PAREN_EXPR && n.has_key(la::VALUE))
        return render_place_node(map_of(n.get(la::VALUE.code)));
    if (c == la::VAR_REF) return std::string(str_of(n.get(la::NAME.code)));
    if (c == la::DEREF && n.has_key(la::VALUE))
        return "(*" + render_place_node(map_of(n.get(la::VALUE.code))) + ")";
    if (c == la::FIELD_READ && n.has_key(la::RECEIVER)) {
        std::string f = n.has_key(la::FIELD) ? std::string(str_of(n.get(la::FIELD.code)))
                      : n.has_key(la::NAME_VAR) ? std::string(str_of(n.get(la::NAME_VAR.code)))
                      : "?";
        return render_place_node(map_of(n.get(la::RECEIVER.code))) + "." + f;
    }
    if (c == la::TUPLE_INDEX && n.has_key(la::RECEIVER))
        return render_place_node(map_of(n.get(la::RECEIVER.code))) + "." +
               std::string(str_of(n.get(n.has_key(la::FIELD) ? la::FIELD.code
                                                             : la::INDEX.code)));
    if (c == la::INDEX_READ && n.has_key(la::RECEIVER))
        return render_place_node(map_of(n.get(la::RECEIVER.code))) + "[i]";
    return "<place>";
}

// General place compound-assign `place op= rhs` (place = field/index/tuple-field/
// chain/`(*p).f` — NOT a bare var, which the caller handles). Collapses the
// former 6 specialised `*_compound_assign` lowerings into one. Read-twice
// desugar (`place = (place) op rhs`), matching the specialised lowerings'
// double-eval semantics; struct places with an `*Assign` impl get the in-place
// `op_assign(&mut place, rhs)` call instead.
lir::LStmt SemaChecker::lower_place_compound_assign(
        TinyMapView node, TinyMapView place_node, const std::string& base_op) {
    // G167-5: user-defined IndexMut compound `a[i] op= v` on a struct — the
    // general addr-of place-write path cannot dispatch IndexMut, so desugar to
    // `*index_mut(&mut a, i) = *index(&a, i) op v` (the one place-shape the
    // generic path doesn't cover; everything else collapses below).
    if (code_of(place_node) == la::INDEX_READ &&
        place_node.has_key(la::RECEIVER)) {
        auto recv_node = map_of(place_node.get(la::RECEIVER.code));
        if (code_of(recv_node) == la::VAR_REF) {
            auto arr_name = std::string(str_of(recv_node.get(la::NAME.code)));
            auto arr_type = lookup(arr_name);
            if (arr_type && TypeRef(arr_type).kind() == LogosType::Kind::Struct) {
                auto type_name = concrete_struct_name(arr_type);
                auto base_name = std::string(TypeRef(arr_type).struct_name());
                bool has_im = impls_.count("IndexMut::" + type_name) ||
                              (!base_name.empty() && impls_.count("IndexMut::" + base_name));
                if (has_im) {
                    if (!lookup_is_mut(arr_name))
                        error(std::format("index compound assign to immutable struct '{}'", arr_name));
                    const SemaFuncInfo* fit_im = nullptr;
                    for (auto* c : find_func_candidates(type_name + "__index_mut"))
                        if (c->param_types.size() == 2) { fit_im = c; break; }
                    const SemaFuncInfo* fit_rd = nullptr;
                    for (auto* c : find_func_candidates(type_name + "__index"))
                        if (c->param_types.size() == 2) { fit_rd = c; break; }
                    if (fit_im) {
                        TypeRef ref_o = fit_im->ret_type;            // &mut O
                        TypeRef out_t = TypeRef(ref_o).pointee()
                                      ? TypeRef(ref_o).pointee() : error_t();
                        auto lower_idx = [&](const SemaFuncInfo* f) -> lir::LExprPtr {
                            lir::LExprPtr idx_e = place_node.has_key(la::VALUE)
                                ? lower_expr(map_of(place_node.get(la::VALUE.code))) : error_expr();
                            widen_int_expr(idx_e, f->param_types[1], builder());
                            return idx_e;
                        };
                        auto rhs2 = node.has_key(la::VALUE)
                            ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
                        if (TypeRef(out_t).kind() != LogosType::Kind::Error &&
                            TypeRef(rhs2->type).kind() != LogosType::Kind::Error &&
                            !types_compatible(rhs2->type, out_t))
                            error(std::format("compound assignment to '{}[i]': type mismatch — expected {}, got {}",
                                  arr_name, type_str(out_t), type_str(rhs2->type)));
                        lir::LExprPtr cur;
                        if (fit_rd) {
                            std::vector<lir::LExprPtr> ra;
                            ra.push_back(builder().addr_of(arr_name, make_ref(false, arr_type)));
                            ra.push_back(lower_idx(fit_rd));
                            auto rc = builder().call(fit_rd->symbol_name.empty()
                                          ? (type_name + "__index") : fit_rd->symbol_name,
                                      {}, std::move(ra), fit_rd->ret_type);
                            cur = builder().deref(std::move(rc), out_t);
                        } else {
                            std::vector<lir::LExprPtr> ra;
                            ra.push_back(builder().addr_of(arr_name, make_ref(true, arr_type)));
                            ra.push_back(lower_idx(fit_im));
                            auto rc = builder().call(fit_im->symbol_name.empty()
                                          ? (type_name + "__index_mut") : fit_im->symbol_name,
                                      {}, std::move(ra), ref_o);
                            cur = builder().deref(std::move(rc), out_t);
                        }
                        auto combined = builder().bin_op(base_op, std::move(cur), std::move(rhs2), out_t);
                        std::vector<lir::LExprPtr> wa;
                        wa.push_back(builder().addr_of(arr_name, make_ref(true, arr_type)));
                        wa.push_back(lower_idx(fit_im));
                        auto wc = builder().call(fit_im->symbol_name.empty()
                                      ? (type_name + "__index_mut") : fit_im->symbol_name,
                                  {}, std::move(wa), ref_o);
                        return builder().stmt_deref_write(std::move(wc), std::move(combined), node_line_);
                    }
                }
            }
        }
    }
    if (!place_write_supported(place_node)) {
        error("compound-assignment target too deeply nested to assign in place "
              "yet; bind an intermediate (e.g. `let r = &mut <inner>; r[i] op= …`)");
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return builder().stmt_break(nullptr, "", node_line_);
    }
    auto place_read = lower_expr(place_node);    // eval #1 — current value
    TypeRef pt = place_read->type;
    auto rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();

    // User-defined `*Assign` dispatch on a struct place → op_assign(&mut place, rhs).
    if (pt && TypeRef(pt).kind() == LogosType::Kind::Struct) {
        std::string atrait, amethod;
        if (op_assign_trait_method(base_op, atrait, amethod)) {
            auto type_name = concrete_struct_name(pt);
            auto base_name = std::string(TypeRef(pt).struct_name());
            if (impls_.count(atrait + "::" + type_name) ||
                (!base_name.empty() && impls_.count(atrait + "::" + base_name))) {
                auto mangled = type_name + "__" + amethod;
                auto mut_ref_t = make_ref(true, pt);
                TypeRef rhs_ty = rhs ? TypeRef(rhs->type) : pt;
                auto fit = find_func_by_base_and_signature(mangled, {mut_ref_t, rhs_ty}, false);
                if (!fit && !types_equal(rhs_ty, pt))
                    fit = find_func_by_base_and_signature(mangled, {mut_ref_t, pt}, false);
                if (fit) {
                    auto addr = builder().addr_of_temp(lower_expr(place_node),  // eval #2 — &mut place
                                                       /*is_mut=*/true, mut_ref_t);
                    std::vector<lir::LExprPtr> args;
                    args.push_back(std::move(addr));
                    args.push_back(std::move(rhs));
                    auto call = builder().call(fit->symbol_name.empty() ? mangled : fit->symbol_name,
                                               {}, std::move(args), fit->ret_type);
                    return builder().stmt_expr(std::move(call), node_line_);
                }
            }
        }
    }

    // General: `*(&mut place) = (place) op rhs`.
    if (pt && TypeRef(pt).kind() != LogosType::Kind::Error &&
        rhs && TypeRef(rhs->type).kind() != LogosType::Kind::Error &&
        !types_compatible(rhs->type, pt)) {
        auto [es, gs] = type_str_pair(pt, rhs->type);
        error(std::format("compound assignment to '{}': type mismatch — expected {}, got {}",
              render_place_node(place_node), es, gs));
    }
    widen_int_expr(rhs, pt, builder());
    auto newval = builder().bin_op(base_op, std::move(place_read), std::move(rhs),
                                   pt ? pt : error_t());
    auto addr = builder().addr_of_temp(lower_expr(place_node), /*is_mut=*/true,  // eval #2
                                       make_ref(true, pt ? pt : error_t()));
    track_write_move(newval);
    return builder().stmt_deref_write(std::move(addr), std::move(newval), node_line_);
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

    // Pin the LHS type as the enum hint while lowering the RHS, exactly as the
    // `let x: T = …` path does (lower_let). Without this, `status = None` lowers
    // the bare `None` literal with no expected type → a C-style i32 discriminant
    // baked into the pointer slot; the post-hoc retype below stamps the right
    // TypeRef but cannot un-bake the wrong codegen, so a later `match status`
    // derefs a bogus pointer → SIGSEGV. Setting the hint up front makes the
    // literal lower as the correct concrete enum spec from the start. (B170 —
    // rustc issue-41888: `status = None` in a loop, then re-matched.)
    auto saved_assign_hint = hint_enum_type_;
    if (var_type && TypeRef(var_type).kind() == LogosType::Kind::Enum)
        hint_enum_type_ = var_type;
    lir::LExprPtr rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    hint_enum_type_ = saved_assign_hint;
    // Retype an incompletely-typed generic enum literal in `a = <enum-lit>`
    // to the LHS's concrete enum spec. A literal lowered without the expected
    // type carries either NO type-args (no-payload `Opt::VNone` → bare `Opt`)
    // or `<error>` type-args for the params not pinned by the payload
    // (`Res::Err(true)` infers only `E=bool`, leaving `Res<error, bool>`).
    // mlir-gen then can't resolve the layout: the no-payload case emits a
    // C-style i32 discriminant into the pointer slot (next match derefs
    // address `1` → SIGSEGV), and the `<error>` case emits no/garbage code
    // ("unknown tagged enum Res__<error>__bool"). The assignment target type
    // pins the missing params. Mirrors finish_generic_call's
    // retype_bare_enum_arg ([[baghunt-replace-ref-option-cascade]]).
    if (rhs && var_type &&
        TypeRef(rhs->type).kind() == LogosType::Kind::Enum &&
        TypeRef(var_type).kind() == LogosType::Kind::Enum &&
        !TypeRef(var_type).type_args().empty() &&
        TypeRef(var_type).enum_name() == TypeRef(rhs->type).enum_name()) {
        auto rk = expr_ref_of(*rhs).kind();
        bool is_enum_lit = rk == lir_schema::expr::Code::EnumLit ||
                           rk == lir_schema::expr::Code::EnumLitData;
        auto rhs_args = TypeRef(rhs->type).type_args();
        auto tgt_args = TypeRef(var_type).type_args();
        // "Incompletely typed" = no type-args, or any type-arg is Error.
        bool incomplete = rhs_args.empty();
        if (!incomplete)
            for (auto ta : rhs_args)
                if (!ta || TypeRef(ta).kind() == LogosType::Kind::Error) { incomplete = true; break; }
        // Target must be fully concrete (no Error type-args).
        bool target_concrete = true;
        for (auto ta : tgt_args)
            if (!ta || TypeRef(ta).kind() == LogosType::Kind::Error) { target_concrete = false; break; }
        // Preserve type-error detection: every KNOWN (non-error) type-arg of
        // the literal must already match the target's at that position, so a
        // genuine mismatch (`Res::Err(true)` into `Res<i64,i64>`) is left for
        // the type-compat check below to reject rather than silently coerced.
        bool known_args_match = true;
        if (!rhs_args.empty() && rhs_args.size() == tgt_args.size()) {
            for (size_t i = 0; i < rhs_args.size(); ++i) {
                TypeRef ra = rhs_args[i];
                if (ra && TypeRef(ra).kind() != LogosType::Kind::Error &&
                    !types_compatible(ra, tgt_args[i])) { known_args_match = false; break; }
            }
        } else if (!rhs_args.empty()) {
            known_args_match = false;  // arity mismatch — don't touch
        }
        if (is_enum_lit && incomplete && target_concrete && known_args_match)
            builder().retype_expr(rhs, var_type);
    }
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
            // G151-3: when the return type is a fn-ptr/closure, hint it so an
            // untyped closure literal (`return |x| x + 1`) infers its param
            // types from the expected signature (mirrors the call-arg path).
            auto saved_closure_hint = hint_closure_formal_;
            // G167-3: also propagate the hint when the callable is WRAPPED
            // (`-> Box<dyn Fn(..)>`), so `return box_new(|x| ..)` infers the
            // closure's params from the inner Fn signature. peel_to_callable
            // unwraps Box/&dyn; the closure-literal site peels again.
            if (ret_type_ && peel_to_callable(ret_type_))
                hint_closure_formal_ = ret_type_;
            // Element-type hint for an array literal returned where a slice/array
            // (possibly behind `&`) is expected, so `return &[];` builds an empty
            // `[T; 0]` instead of an untyped-element error.
            auto saved_arr_elem_hint = hint_arr_elem_type_;
            {
                TypeRef rh = ret_type_;
                if (rh && (TypeRef(rh).kind() == LogosType::Kind::Ref ||
                           TypeRef(rh).kind() == LogosType::Kind::MutRef) &&
                    TypeRef(rh).pointee())
                    rh = TypeRef(rh).pointee();
                if (rh && (TypeRef(rh).kind() == LogosType::Kind::Array ||
                           TypeRef(rh).kind() == LogosType::Kind::Slice) &&
                    TypeRef(rh).elem())
                    hint_arr_elem_type_ = TypeRef(rh).elem();
            }
            val = lower_expr(map_of(vav));
            hint_enum_type_ = saved_hint;
            hint_struct_type_ = saved_struct_hint;
            hint_closure_formal_ = saved_closure_hint;
            hint_arr_elem_type_ = saved_arr_elem_hint;
            // G151-3: a non-capturing closure literal returned where a fn-ptr
            // type is expected coerces to that fn-ptr — the same coercion the
            // let-annotation and call-arg paths apply. Without this, `fn f() ->
            // fn()->T { return || ... }` errored "expected fn()->T, got ||->T".
            if (ret_type_ &&
                TypeRef(ret_type_).kind() == LogosType::Kind::FnPtr)
                try_coerce_closure_to_fnptr(val, ret_type_);
            if (ret_type_ && TypeRef(ret_type_).kind() == LogosType::Kind::ImplTrait) {
                // Infer concrete return type from first return expression.
                if (!impl_ret_type_inferred_ &&
                    TypeRef(val->type).kind() != LogosType::Kind::Error)
                    impl_ret_type_inferred_ = val->type;
            } else if (ret_type_ && TypeRef(ret_type_).kind() != LogosType::Kind::Error &&
                TypeRef(val->type).kind() != LogosType::Kind::Error &&
                !compat(val->type, ret_type_) &&
                // Gap-4: normalize a projection `T::A` via an equality bound
                // `T: Trait<A = V>` before declaring a mismatch.
                !compat(normalize_assoc_eq(val->type), ret_type_)) {
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

lir::Pattern SemaChecker::build_pattern_variant(TinyMapView pnode, TypeRef scrut_type) {
    int32_t pc = code_of(pnode); (void)pc;
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
        // CP-cm-02: `use Type.{V1, …};` bare-variant alias map.
        if (pvname.empty()) {
            auto vit = cur_imports_.variant_aliases.find(pename);
            if (vit != cur_imports_.variant_aliases.end())
                prelude_remap(vit->second.c_str());
        }
    }
    // G172-3: peel a type-alias to an enum in a variant pattern (`OptAlias::N`
    // where `type OptAlias<T> = Opt<T>`). Mirrors the construction-side peel
    // (G160-2) but also handles GENERIC aliases — the variant resolves on the
    // base enum name; the type-args are irrelevant to which variant matches.
    if (!enums_.count(pename) && !find_enum_by_name(pename).second) {
        auto ait = type_aliases_.find(pename);
        if (ait != type_aliases_.end() && ait->second.type &&
            TypeRef(ait->second.type).kind() == LogosType::Kind::Enum) {
            std::string tgt(TypeRef(ait->second.type).enum_name());
            if (!tgt.empty()) pename = std::move(tgt);
        }
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

lir::Pattern SemaChecker::build_pattern_variant_data(TinyMapView pnode, TypeRef scrut_type) {
    int32_t pc = code_of(pnode); (void)pc;
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
        // CP-cm-02: `use Type.{V1, …};` bare-variant alias map.
        if (pvname.empty()) {
            auto vit = cur_imports_.variant_aliases.find(pename);
            if (vit != cur_imports_.variant_aliases.end())
                prelude_remap(vit->second.c_str());
        }
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
            size_t arity   = tsi_p->fields.size();
            if (pnode.has_key(la::ARGS)) {
                auto aav = pnode.get(la::ARGS.code);
                if (!aav.is_null() && aav.is_pointer()) {
                    auto blist = map_of(aav);
                    if (blist.has_key(la::ITEMS)) {
                        auto bitems = arr_of(blist.get(la::ITEMS.code));
                        // G151-2: a single `..` rest in a tuple-struct pattern
                        // (`S(..)`, `S(x, ..)`, `S(.., z)`). Map named args to
                        // their real positions (before-rest → low, after-rest →
                        // tail) and set has_rest; skipped positions bind nothing.
                        int rest_idx = -1;
                        size_t non_rest = 0;
                        for (uint64_t j = 0; j < bitems.size(); ++j) {
                            if (code_of(map_of(bitems.get(j))) == la::PAT_REST) {
                                if (rest_idx >= 0)
                                    error("tuple-struct pattern: only one '..' allowed");
                                rest_idx = (int)j;
                            } else ++non_rest;
                        }
                        if (rest_idx >= 0) ps.has_rest = true;
                        for (uint64_t j = 0; j < bitems.size(); ++j) {
                            auto bnode = map_of(bitems.get(j));
                            if (code_of(bnode) == la::PAT_REST) continue;
                            size_t pos;
                            if (rest_idx < 0 || (int)j < rest_idx) pos = j;
                            else pos = arity - (bitems.size() - 1 - (size_t)rest_idx)
                                       + (j - (size_t)rest_idx - 1);
                            TypeRef ftype = pos < tsi_p->fields.size()
                                            ? tsi_p->fields[pos].type : nullptr;
                            lir::PatFieldBinding fb;
                            fb.field_name = std::to_string(pos);
                            fb.sub.push_back(build_pattern(bnode, ftype));
                            ps.fields.push_back(std::move(fb));
                        }
                        if (rest_idx < 0 && non_rest != arity)
                            error(std::format(
                                "tuple-struct pattern '{}': expected {} fields, got {}",
                                pename, arity, non_rest));
                        else if (rest_idx >= 0 && non_rest > arity)
                            error(std::format(
                                "tuple-struct pattern '{}': {} fields exceed arity {}",
                                pename, non_rest, arity));
                    }
                }
            }
            auto mo = lir_mirror_emit_pat_struct(
                *cur_prog_, ps.struct_name, ps.fields, ps.has_rest);
            lir::Pattern p_;
            p_.mirror_offset_ = mo;
            return p_;
        }
    }
    // G172-3: peel a (possibly generic) type-alias to an enum in a data-variant
    // pattern (`OptAlias::S(v)` where `type OptAlias<T> = Opt<T>`). Mirrors the
    // unit-variant peel in build_pattern_variant.
    if (!pvname.empty() && !enums_.count(pename) && !find_enum_by_name(pename).second) {
        auto ait = type_aliases_.find(pename);
        if (ait != type_aliases_.end() && ait->second.type &&
            TypeRef(ait->second.type).kind() == LogosType::Kind::Enum) {
            std::string tgt(TypeRef(ait->second.type).enum_name());
            if (!tgt.empty()) pename = std::move(tgt);
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
    {
        // Deref `&Enum` / `&mut Enum` / `*Enum` (match ergonomics) so the
        // per-position payload types are concrete even for a by-ref scrutinee
        // (needed by nested-pattern synth bindings — otherwise pat_field_type
        // returns the bare TypeVar and the guard/extraction miscompile).
        TypeRef pat_scrut = scrut_type;
        if (pat_scrut &&
            (TypeRef(pat_scrut).kind() == LogosType::Kind::Ref ||
             TypeRef(pat_scrut).kind() == LogosType::Kind::MutRef ||
             TypeRef(pat_scrut).kind() == LogosType::Kind::Ptr) &&
            TypeRef(pat_scrut).pointee())
            pat_scrut = TypeRef(pat_scrut).pointee();
        if (vinfo && pat_scrut && TypeRef(pat_scrut).kind() == LogosType::Kind::Enum &&
            !TypeRef(pat_scrut).type_args().empty() && eit != enums_.end()) {
            auto& einfo = eit->second;
            for (size_t k = 0; k < einfo.type_params.size() &&
                                 k < TypeRef(pat_scrut).type_args().size(); ++k)
                pat_subst[einfo.type_params[k].name] = TypeRef(pat_scrut).type_args()[k];
        }
    }
    // True when the scrutinee is by-reference (match ergonomics): a nested
    // payload binding then binds by-ref (two-level), so synth types wrap in &.
    bool pat_scrut_by_ref = scrut_type &&
        (TypeRef(scrut_type).kind() == LogosType::Kind::Ref ||
         TypeRef(scrut_type).kind() == LogosType::Kind::MutRef);
    bool pat_scrut_by_mut = scrut_type &&
        TypeRef(scrut_type).kind() == LogosType::Kind::MutRef;
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
    // explicit_name: when non-empty, the payload is bound to THAT name (an
    // `@`-binding — `Msg::Num(n @ 1..=5)`) and the refutable guard is built
    // against it, rather than to a fresh synth temp. The caller pushes the
    // returned name into `bindings`.
    // Set by synth_refutable_inner when the returned synth must bind by-ref
    // (by-ref-ergonomics nested-variant synth). The caller reads it to set
    // binding_is_ref for that synth.
    bool synth_wants_ref = false;
    auto synth_refutable_inner =
        [&](TinyMapView sub, TypeRef ftype, std::string_view ctx_field,
            std::string_view explicit_name = {}) -> std::string {
        synth_wants_ref = false;
        std::string synth = explicit_name.empty()
            ? std::format("__refut_{}_{}_{}", pvname, ctx_field, tmp_var_count_++)
            : std::string(explicit_name);
        int32_t sc = code_of(sub);
        // Nested VARIANT inner pattern, e.g. `Some(Color::Red)` /
        // `Ok(Status::Done)`. Bind the payload to `synth`, and gate the arm
        // with a synthesized `match synth { <inner> => true, _ => false }`
        // guard (reuses enum match dispatch). Only for inners that bind
        // nothing (a payload-carrying inner like `Some(Inner(x))` would lose
        // its inner bindings through the guard — left to the existing error).
        // A bindingless variant inner check, reused for plain PAT_VARIANT_DATA
        // and for each alternative of a PAT_OR.
        std::function<bool(TinyMapView)> data_has_binding = [&](TinyMapView dn) -> bool {
            if (!dn.has_key(la::ARGS)) return false;
            auto av = dn.get(la::ARGS.code);
            if (av.is_null() || !av.is_pointer()) return false;
            auto al = map_of(av);
            if (!al.has_key(la::ITEMS)) return false;
            auto items = arr_of(al.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto sn = map_of(items.get(i));
                int32_t c = code_of(sn);
                if (c == la::PAT_WILD && sn.has_key(la::NAME) &&
                    str_of(sn.get(la::NAME.code)) != "_") return true;
                // A binding anywhere DEEPER (e.g. `Some(Some(w))`) also routes
                // through the K4 binding-variant path so its depth gate fires.
                if (c == la::PAT_VARIANT_DATA && data_has_binding(sn)) return true;
                if (c == la::PAT_AT) return true;
            }
            return false;
        };
        if (sc == la::PAT_VARIANT ||
            (sc == la::PAT_VARIANT_DATA && current_pat_refutable_guards_) ||
            (sc == la::PAT_OR && current_pat_refutable_guards_)) {
            // K4: nested variant pattern carrying bindings (e.g.
            // `Some(Some(v))`). Bind the outer payload to `synth`, gate the arm
            // with a guard match `match synth { <sub> => <inner_check>, _ =>
            // false }` (so sibling arms like `Some(None)` dispatch correctly),
            // and register a body let-else that re-extracts the inner bindings
            // from `synth` (the guard guarantees the match → the else is dead).
            // Composes to arbitrary depth: the deeper checks ride the matching
            // arm's VALUE (never an arm GUARD), and the body let-else recurses.
            if (sc == la::PAT_VARIANT_DATA && data_has_binding(sub)) {
                if (!current_pat_refutable_guards_ || !current_pat_nested_subs_)
                    return std::string();
                // Raw-pointer scrutinee (`*const`/`*mut`) keeps the clean
                // reject — match ergonomics is `&`/`&mut` only.
                if (scrut_type && TypeRef(scrut_type).kind() == LogosType::Kind::Ptr)
                    return std::string();
                TypeRef rt = (ftype && TypeRef(ftype).kind() != LogosType::Kind::Error)
                    ? ftype : error_t();
                // By-ref ergonomics: the outer payload binds by-reference, so
                // the synth carrying the nested enum is `&Inner` / `&mut Inner`.
                // The guard match + body let-else then run over a ref scrutinee
                // (default binding modes handle that), matching the pattern
                // binding type the binding_types pass assigns to this synth.
                if (pat_scrut_by_ref && rt && TypeRef(rt).kind() != LogosType::Kind::Error) {
                    rt = make_ref(pat_scrut_by_mut, rt);
                    synth_wants_ref = true;
                }
                std::vector<lir::LExprPtr> inner_guards;
                std::vector<NestedPatSub> inner_subs;   // discarded — re-extracted
                auto* sg = current_pat_refutable_guards_;
                auto* ssub = current_pat_nested_subs_;
                current_pat_refutable_guards_ = &inner_guards;
                current_pat_nested_subs_ = &inner_subs;
                lir::EMatchArm a0;
                a0.pat = build_pattern(sub, rt);
                current_pat_refutable_guards_ = sg;
                current_pat_nested_subs_ = ssub;
                define(synth, rt);
                // Deeper binding-nesting: the inner checks become this guard
                // arm's VALUE — `match synth { <sub> => <inner_check>, _ =>
                // false }` (an arm VALUE, not an arm GUARD, to avoid the
                // guarded-arm slot bug). For one-level nesting inner_guards is
                // empty → value `true`.
                lir::LExprPtr inner_check = nullptr;
                for (auto& ig : inner_guards) {
                    if (!ig) continue;
                    inner_check = inner_check
                        ? builder().bin_op("&&", std::move(inner_check), std::move(ig), bool_t())
                        : std::move(ig);
                }
                a0.value = inner_check ? std::move(inner_check)
                                       : builder().lit_bool(true, bool_t());
                lir::EMatchArm a1;
                a1.pat = make_pat_wild("_");
                a1.value = builder().lit_bool(false, bool_t());
                lir::EMatchExpr me;
                me.scrut = builder().var_ref(synth, rt);
                me.arms.push_back(std::move(a0));
                me.arms.push_back(std::move(a1));
                current_pat_refutable_guards_->push_back(
                    builder().match_expr_v(std::move(me), bool_t()));
                current_pat_nested_subs_->push_back({synth, sub});
                return synth;
            }
            // G139-2: or-pattern inner `Some(A | B)`. Build the same
            // `match synth { A | B => true, _ => false }` guard. Each alt must
            // bind nothing (the guard returns bool — bindings would be lost).
            if (sc == la::PAT_OR) {
                if (!sub.has_key(la::ITEMS)) return std::string();
                auto alts = arr_of(sub.get(la::ITEMS.code));
                for (uint64_t i = 0; i < alts.size(); ++i) {
                    auto an = map_of(alts.get(i));
                    int32_t ac = code_of(an);
                    if (ac == la::PAT_VARIANT) continue;
                    if (ac == la::PAT_VARIANT_DATA && !data_has_binding(an)) continue;
                    if (ac == la::PAT_INT || ac == la::PAT_NEG_INT ||
                        ac == la::PAT_BOOL || ac == la::PAT_CHAR) continue;
                    // G144-2: a bindingless wildcard alt (`Some(0 | _)`) is a
                    // catch-all — the guard `match synth { 0 | _ => true, _ =>
                    // false }` evaluates to always-true, which is correct. A
                    // NAMED wildcard would bind (lost through the guard) → reject.
                    if (ac == la::PAT_WILD &&
                        (!an.has_key(la::NAME) || str_of(an.get(la::NAME.code)) == "_"))
                        continue;
                    return std::string();  // binding/unsupported alt → fall to error
                }
            }
            if (!current_pat_refutable_guards_) return std::string();
            TypeRef rt = (ftype && TypeRef(ftype).kind() != LogosType::Kind::Error)
                ? ftype : error_t();
            define(synth, rt);
            lir::EMatchArm a0;
            // Isolate any DEEPER refutable-inner guards produced while building
            // the guard pattern (`Some(Some(None))` → the inner `None` test) so
            // they become THIS guard arm's VALUE — `match synth { <sub> =>
            // <inner_check>, _ => false }` — rather than leaking into the
            // enclosing arm's guard list (where they'd reference synths bound
            // only inside this guard match → wrong dispatch beyond 2 levels).
            std::vector<lir::LExprPtr> bl_inner_guards;
            std::vector<NestedPatSub> bl_inner_subs;   // discarded in a guard
            auto* bl_sg = current_pat_refutable_guards_;
            auto* bl_ssub = current_pat_nested_subs_;
            current_pat_refutable_guards_ = &bl_inner_guards;
            current_pat_nested_subs_ = &bl_inner_subs;
            a0.pat = build_pattern(sub, rt);
            current_pat_refutable_guards_ = bl_sg;
            current_pat_nested_subs_ = bl_ssub;
            lir::LExprPtr bl_check = nullptr;
            for (auto& ig : bl_inner_guards) {
                if (!ig) continue;
                bl_check = bl_check
                    ? builder().bin_op("&&", std::move(bl_check), std::move(ig), bool_t())
                    : std::move(ig);
            }
            a0.value = bl_check ? std::move(bl_check)
                                : builder().lit_bool(true, bool_t());
            lir::EMatchArm a1;
            a1.pat   = make_pat_wild("_");
            a1.value = builder().lit_bool(false, bool_t());
            lir::EMatchExpr me;
            me.scrut = builder().var_ref(synth, rt);
            me.arms.push_back(std::move(a0));
            me.arms.push_back(std::move(a1));
            current_pat_refutable_guards_->push_back(
                builder().match_expr_v(std::move(me), bool_t()));
            return synth;
        }
        // G162-1: range inner `Num(1..=5)` / `Num(n @ 1..=5)`. Bind the
        // payload to `synth` (or the @-name) and gate the arm with
        // `synth >= lo && synth <= hi` (exclusive `lo..hi` lowers to
        // `lo..=(hi-1)`). Mirrors the PAT_RANGE handling in build_pattern.
        if (sc == la::PAT_RANGE && sub.has_key(la::LHS) && sub.has_key(la::RHS)) {
            int64_t lo = parse_int_literal(str_of(sub.get(la::LHS.code)));
            int64_t hi = parse_int_literal(str_of(sub.get(la::RHS.code)));
            if (sub.has_key(la::LO_NEG)) {
                AnyVal av = sub.get(la::LO_NEG.code);
                if (!av.is_null() && av.is_value() && av.as_value<uint8_t>()) lo = -lo;
            }
            if (sub.has_key(la::HI_NEG)) {
                AnyVal av = sub.get(la::HI_NEG.code);
                if (!av.is_null() && av.is_value() && av.as_value<uint8_t>()) hi = -hi;
            }
            bool inclusive = true;
            if (sub.has_key(la::INCLUSIVE)) {
                AnyVal av = sub.get(la::INCLUSIVE.code);
                if (!av.is_null() && av.is_value()) inclusive = av.as_value<uint8_t>() != 0;
            }
            if (!inclusive) hi = hi - 1;
            if (current_pat_refutable_guards_) {
                TypeRef rt = (ftype && TypeRef(ftype).kind() != LogosType::Kind::Error)
                    ? ftype : prim(LogosType::Kind::I64);
                auto lo_lit = builder().lit_int(lo, rt);
                auto hi_lit = builder().lit_int(hi, rt);
                auto ge = builder().bin_op(">=", builder().var_ref(synth, rt),
                                           std::move(lo_lit), bool_t());
                auto le = builder().bin_op("<=", builder().var_ref(synth, rt),
                                           std::move(hi_lit), bool_t());
                auto guard = builder().bin_op("&&", std::move(ge), std::move(le), bool_t());
                current_pat_refutable_guards_->push_back(std::move(guard));
            }
            return synth;
        }
        // G162-1: `Num(n @ _)` — an @-binding with a wildcard sub binds the
        // payload to the name with no guard (only meaningful with an explicit
        // name; a bare synth `_` would be a plain wildcard).
        if (sc == la::PAT_WILD && !explicit_name.empty() &&
            (!sub.has_key(la::NAME) || str_of(sub.get(la::NAME.code)) == "_"))
            return synth;
        lir::LExprPtr value;
        // G172-1: nested string-literal pattern (`Some("foo")`, `("foo", _)`).
        // Bind the element to `synth`, gate with `str_eq(synth, "foo")` (a raw
        // `==` would pointer-compare the slices).
        if (sc == la::PAT_STR && sub.has_key(la::VALUE)) {
            if (current_pat_refutable_guards_) {
                TypeRef str_t = make_slice_type(u8_t());
                auto strlit = builder().lit_str(
                    std::string(str_of(sub.get(la::VALUE.code))), str_t);
                auto g = make_str_eq_guard(
                    builder().var_ref(synth,
                        (ftype && TypeRef(ftype).kind() != LogosType::Kind::Error) ? ftype : str_t),
                    std::move(strlit));
                if (g) current_pat_refutable_guards_->push_back(std::move(g));
            }
            return synth;
        }
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
                               sc == la::PAT_BOOL || sc == la::PAT_CHAR ||
                               sc == la::PAT_RANGE || sc == la::PAT_STR ||
                               sc == la::PAT_VARIANT ||
                               sc == la::PAT_VARIANT_DATA) {
                        // P4-pm-01 / K4: refutable inner on a struct-shape
                        // variant field — literal, range, unit variant, OR a
                        // binding-carrying nested variant (`Move { x: Some(v),
                        // .. }`). synth_refutable_inner synthesises the binding +
                        // arm guard and (for binding variants) registers the
                        // body let-else via the nested-subs channel that the arm
                        // body consumes — same as the tuple-shape path.
                        std::string synth = synth_refutable_inner(
                            sub, pat_field_type(idx), fname);
                        if (synth.empty()) {
                            error(std::format(
                                "pattern {}::{} field '{}': refutable inner "
                                "pattern not yet supported in struct-shape "
                                "variant patterns (use bind + body match)",
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
    // Per-binding IS_REF / IS_MUT flags from `ref v` / `ref mut v`
    // sub-patterns inside variant data — parallel to `bindings`,
    // consulted below to wrap the corresponding binding_types with
    // Ref/MutRef so codegen materialises the payload by-address.
    std::vector<bool> binding_is_ref;
    std::vector<bool> binding_is_mut;
    // Parallel to `bindings`: true only for a PLAIN named PAT_WILD binding
    // (not "_", not a synthesized refutable-inner / nested-destructure slot).
    // Gates the default-binding-mode ref wrap below so it never touches synth
    // slots (those are handled by-value / Stage-2).
    std::vector<bool> binding_from_wild;
    if (!pat_is_struct_shape && pnode.has_key(la::ARGS)) {
        AnyVal aav = pnode.get(la::ARGS.code);
        if (!aav.is_null() && aav.is_pointer()) {
            auto blist = map_of(aav);
            if (blist.has_key(la::ITEMS)) {
                auto bitems = arr_of(blist.get(la::ITEMS.code));
                for (uint64_t j = 0; j < bitems.size(); ++j) {
                    auto bnode = map_of(bitems.get(j));
                    // B170-E: or-distribution. When lower_match fanned this arm
                    // out per payload-or alternative, replace a multi-alt PAT_OR
                    // arg with the selected alternative so the rest of the loop
                    // handles it like a plain payload sub-pattern (each fanned
                    // arm re-runs the guard with its own bindings). Mirrors the
                    // grammar's single-alt PAT_OR unwrap, generalised to N alts.
                    if (payload_or_alt_ >= 0 &&
                        code_of(bnode) == la::PAT_OR && bnode.has_key(la::ITEMS)) {
                        auto oalts = arr_of(bnode.get(la::ITEMS.code));
                        if ((uint64_t)payload_or_alt_ < oalts.size())
                            bnode = map_of(oalts.get((uint64_t)payload_or_alt_));
                    }
                    // B-pt-04: variant-payload args now parse as full
                    // patterns, but only PAT_WILD bindings (or PAT_UNIT
                    // skip) are codegen'd today.  Anything else (struct,
                    // tuple, nested variant, …) emits a diagnostic
                    // until the match-lowering supports nested guards.
                    int32_t bc = code_of(bnode);
                    if (bc == la::PAT_UNIT) continue;  // () unit — no binding
                    // G151-2: `..` rest in a tuple-variant pattern (`A(..)`,
                    // `A(x, ..)`, `A(.., y)`). Expand to `_` for the skipped
                    // positions so the remaining (named) args land on the
                    // correct payload positions. Only one `..` allowed.
                    if (bc == la::PAT_REST) {
                        size_t arity = vinfo ? vinfo->payload_types.size() : 0;
                        size_t non_rest = bitems.size() - 1;  // this rest is one item
                        size_t gap = arity > non_rest ? arity - non_rest : 0;
                        for (size_t g = 0; g < gap; ++g) {
                            bindings.push_back("_");
                            binding_is_ref.push_back(false);
                            binding_is_mut.push_back(false);
                            binding_from_wild.push_back(false);
                        }
                        continue;
                    }
                    if (bc == la::PAT_WILD) {
                        if (!bnode.has_key(la::NAME)) continue;
                        bool is_ref = bnode.has_key(la::IS_REF) &&
                                      bnode.get(la::IS_REF.code).is_value() &&
                                      bnode.get(la::IS_REF.code).as_value<uint8_t>() != 0;
                        bool is_mut = bnode.has_key(la::IS_MUT) &&
                                      bnode.get(la::IS_MUT.code).is_value() &&
                                      bnode.get(la::IS_MUT.code).as_value<uint8_t>() != 0;
                        auto bname = std::string(str_of(bnode.get(la::NAME.code)));
                        bindings.push_back(bname);
                        binding_is_ref.push_back(is_ref);
                        binding_is_mut.push_back(is_ref && is_mut);
                        binding_from_wild.push_back(bname != "_");
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
                        binding_is_ref.push_back(false);
                        binding_is_mut.push_back(false);
                        binding_from_wild.push_back(false);
                        current_pat_nested_subs_->push_back({synth, bnode});
                        continue;
                    }
                    // G162-1: `Num(n @ <sub>)` — an @-binding inside the
                    // payload. Bind the payload to the @-name AND gate the arm
                    // with the sub-pattern's refutable guard (range / literal /
                    // variant), built against that name. PAT_WILD sub (`n @ _`)
                    // binds with no guard.
                    if (bc == la::PAT_AT && bnode.has_key(la::NAME) &&
                        bnode.has_key(la::VALUE)) {
                        auto atname = std::string(str_of(bnode.get(la::NAME.code)));
                        auto subnode = map_of(bnode.get(la::VALUE.code));
                        std::string r = synth_refutable_inner(
                            subnode, pat_field_type(j),
                            std::format("{}", j), atname);
                        if (!r.empty()) {
                            bindings.push_back(atname);
                            binding_is_ref.push_back(false);
                            binding_is_mut.push_back(false);
                            binding_from_wild.push_back(true);  // named binding
                            continue;
                        }
                    }
                    // P4-pm-01 refutable inner (tuple-shape parallel) —
                    // `Option::Some(1)` / `Result::Err(false)` / `Num(1..=5)`.
                    // Synth a binding + emit `__refut_… == <value>` (or a range
                    // `>= && <=`) as an arm guard.
                    if (bc == la::PAT_INT || bc == la::PAT_NEG_INT ||
                        bc == la::PAT_BOOL || bc == la::PAT_CHAR ||
                        bc == la::PAT_RANGE || bc == la::PAT_STR ||
                        bc == la::PAT_VARIANT || bc == la::PAT_VARIANT_DATA ||
                        bc == la::PAT_OR) {
                        std::string synth = synth_refutable_inner(
                            bnode, pat_field_type(j),
                            std::format("{}", j));
                        if (!synth.empty()) {
                            // By-ref ergonomics + a nested-variant synth
                            // (`match &enum { Some(Some(v)) }`): the synth must
                            // bind the payload slot BY REFERENCE so its `&Inner`
                            // type (set in synth_refutable_inner) matches what
                            // extract_payload stores (the slot address, not the
                            // loaded value) and the guard's two-level deref is
                            // correct. synth_refutable_inner sets the flag.
                            bindings.push_back(std::move(synth));
                            binding_is_ref.push_back(synth_wants_ref);
                            binding_is_mut.push_back(synth_wants_ref && pat_scrut_by_mut);
                            binding_from_wild.push_back(false);
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
    // S8-en-03 / B170: bindings against an ALL-UNIT payload variant
    // (`Right(_)` / `Err(err)` where the payload type(s) are `()`).
    // `binding_types` filters Void out, so an all-unit payload ends up with
    // 0 expected types while the user may have written `_`, a `()` literal
    // (already skipped at collection), or a NAMED binding.
    //   • `_`           → drop silently (bare-variant form).
    //   • named `err`   → keep, with a Void binding type, so bind_pattern
    //                     defines `err : ()` as a (zero-sized) unit local.
    //                     The variant's unit field is elided from the enum
    //                     layout (mlir_gen_types skips Void), so re-wrapping
    //                     `Err(err)` ignores the value — the name only needs
    //                     to type-check + be in scope. (rustc issue-41888:
    //                     `Err(err) => return Err(err)` over Result<(),()>.)
    if (binding_types.empty() && !bindings.empty()) {
        std::vector<std::string> kb;
        std::vector<bool> kr, km, kw;
        for (size_t i = 0; i < bindings.size(); ++i) {
            if (bindings[i] == "_") continue;  // wildcard: no local
            kb.push_back(bindings[i]);
            kr.push_back(i < binding_is_ref.size() && binding_is_ref[i]);
            km.push_back(i < binding_is_mut.size() && binding_is_mut[i]);
            kw.push_back(false);
            binding_types.push_back(void_t());
        }
        bindings = std::move(kb);
        binding_is_ref = std::move(kr);
        binding_is_mut = std::move(km);
        binding_from_wild = std::move(kw);
    }
    if (bindings.size() != binding_types.size())
        error(std::format("pattern {}::{}: expected {} bindings, got {}",
              pename, pvname, binding_types.size(), bindings.size()));
    // SL-sl-03 follow-up + default binding modes (RFC 2005 match ergonomics).
    // mlir_gen detects a Ref/MutRef binding type (while the payload stays bare)
    // and binds the payload field's GEP ADDRESS rather than a load — so the
    // binding references the original payload slot (no move/copy/Drop).
    //   • explicit `ref` / `ref mut` → always by-reference (binding_is_ref).
    //   • DEFAULT binding mode: under a `&`/`&mut` scrutinee, a plain named
    //     payload binding of a MOVE-ONLY type is bound by reference too — so
    //     `match &opt { Some(a) => … }` doesn't move the owned payload out of
    //     the borrow (which dropped it at arm exit → double-free). COPY
    //     payloads stay by-value: copying a Copy out of a shared borrow is
    //     sound, more ergonomic, and sidesteps needing `&T`-arithmetic
    //     auto-deref. FOLLOW-UP (Victor): once `&T`-operator support / arith
    //     auto-deref lands, drop the is_move_type gate to bind ALL payloads by
    //     reference (full literal RFC 2005). See plan-default-binding-modes.
    //
    // Stage 1 is restricted to SHARED `&` scrutinees. `&mut` default-ref is
    // deferred: a `&mut self` match in a generic stdlib body (OptionIter::next
    // etc.) would bind the payload as `&mut T`, which then flows back into the
    // same generic's type-arg → unbounded `&mut`-wrapping instantiation
    // (`OptionIter<&mut &mut … T>`, depth-limit blow-up). Needs a
    // self-referential guard before enabling — see plan-default-binding-modes.
    bool default_ref = scrut_type &&
        (TypeRef(scrut_type).kind() == LogosType::Kind::Ref ||
         TypeRef(scrut_type).kind() == LogosType::Kind::MutRef);
    bool default_mut = scrut_type &&
        TypeRef(scrut_type).kind() == LogosType::Kind::MutRef;
    for (size_t k = 0; k < binding_types.size(); ++k) {
        if (!binding_types[k]) continue;
        bool explicit_ref = k < binding_is_ref.size() && binding_is_ref[k];
        // Self-ref guard: never default-ref a bare TypeVar payload. A generic
        // body's `match &[mut] self { Some(x) }` binds the payload typevar `T`;
        // wrapping it `&[mut] T` flows into the enclosing generic's type-arg, and
        // mono then re-wraps each instantiation → unbounded `OptionIter<&mut … T>`
        // (the depth-limit blow-up). Concrete payloads (String) are fine; generic
        // stdlib bodies use an explicit `ref` where they need a borrow.
        bool tv_payload = TypeRef(binding_types[k]).kind() == LogosType::Kind::TypeVar;
        if (explicit_ref) {
            bool is_mut = k < binding_is_mut.size() && binding_is_mut[k];
            binding_types[k] = make_ref(is_mut, binding_types[k]);
        } else if (default_ref && !tv_payload &&
                   k < binding_from_wild.size() && binding_from_wild[k] &&
                   (is_move_type(binding_types[k]) || default_mut)) {
            // Under a SHARED `&` scrutinee, only move-only payloads are
            // auto-ref'd (Copy stays by-value for read ergonomics, since `&T`
            // operator auto-deref isn't implemented). Under a `&mut` scrutinee
            // a CONCRETE payload (incl. Copy) is auto-`&mut`-bound so write-back
            // `*v = …` works — matching Rust ergonomics; reads then use `*v`.
            binding_types[k] = make_ref(default_mut, binding_types[k]);
        }
    }
    auto mo = lir_mirror_emit_pat_variant_data(
        *cur_prog_, pename, pvname, disc, bindings, binding_types);
    lir::Pattern p_;
    p_.mirror_offset_ = mo;
    return p_;
}

lir::Pattern SemaChecker::build_pattern_bytes(TinyMapView pnode, TypeRef scrut_type) {
    int32_t pc = code_of(pnode); (void)pc;
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
    // G160-4: a byte-string pattern over a `&[u8; N]` / `&mut [u8; N]` scrutinee
    // — peel the reference (default binding modes auto-deref the `&array`, so
    // the pattern just needs to see through the ref).
    TypeRef bs_scrut = scrut_type;
    if (bs_scrut && (TypeRef(bs_scrut).kind() == LogosType::Kind::Ref ||
                     TypeRef(bs_scrut).kind() == LogosType::Kind::MutRef) &&
        TypeRef(bs_scrut).pointee() &&
        TypeRef(TypeRef(bs_scrut).pointee()).kind() == LogosType::Kind::Array)
        bs_scrut = TypeRef(bs_scrut).pointee();
    if (bs_scrut && TypeRef(bs_scrut).kind() != LogosType::Kind::Error) {
        auto sk = TypeRef(bs_scrut).kind();
        bool ok = false;
        if (sk == LogosType::Kind::Array && TypeRef(bs_scrut).elem() &&
            TypeRef(bs_scrut).elem().kind() == LogosType::Kind::U8) {
            ok = true;
            size_t n = (size_t)TypeRef(bs_scrut).arr_size();
            if (n != bytes.size())
                error(std::format(
                    "byte-string pattern: literal length {} does not match "
                    "scrutinee array length {}", bytes.size(), n));
        }
        if (!ok)
            error(std::format(
                "byte-string pattern requires `[u8; N]` scrutinee, got '{}'",
                type_str(scrut_type)));  // report the original (pre-peel) type
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

lir::Pattern SemaChecker::build_pattern_or(TinyMapView pnode, TypeRef scrut_type) {
    int32_t pc = code_of(pnode); (void)pc;
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

lir::Pattern SemaChecker::build_pattern_impl(TinyMapView pnode, TypeRef scrut_type) {
    int32_t pc = code_of(pnode);
    if (pc == la::PAT_VARIANT) return build_pattern_variant(pnode, scrut_type);
    if (pc == la::PAT_VARIANT_DATA) return build_pattern_variant_data(pnode, scrut_type);
    if (pc == la::PAT_FLOAT) {
        // B-pt-06: parse but reject — IEEE-equality patterns need a
        // language-level decision before we wire them through codegen.
        error("float-literal patterns are not yet supported "
              "(IEEE equality semantics undecided)");
        lir::Pattern p_;
        p_.mirror_offset_ = lir_mirror_emit_pat_wild(*cur_prog_, "_");
        return p_;
    }
    if (pc == la::PAT_BYTES) return build_pattern_bytes(pnode, scrut_type);
    if (pc == la::PAT_STR) {
        // G172-1: string-literal patterns are handled WITHOUT a PatStr LIR node
        // — top-level arms via the lower_match/lower_match_expr str_eq-guard
        // intercept, and variant-payload nesting (`Some("foo")`) via
        // synth_refutable_inner. A PAT_STR reaching build_pattern is therefore
        // an UNSUPPORTED nesting (e.g. a tuple element `("foo", _)`): reject
        // cleanly rather than fall through to a wildcard (which would silently
        // match any string). Tracked as G172-1b.
        error("string-literal patterns are supported as a whole match arm "
              "(`match s { \"foo\" => … }`), inside an enum-variant payload "
              "(`Some(\"foo\")`), and as a tuple element (`(\"foo\", _)`), but "
              "not in this position (e.g. an array/slice pattern); bind a name "
              "and compare in the body (`x if x == \"foo\"`)");
        lir::Pattern p_;
        p_.mirror_offset_ = lir_mirror_emit_pat_wild(*cur_prog_, "_");
        return p_;
    }
    if (pc == la::PAT_INT || pc == la::PAT_NEG_INT) {
        auto sv = str_of(pnode.get(la::VALUE.code));
        int64_t v = parse_int_literal(sv);
        if (pc == la::PAT_NEG_INT) v = -v;
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error &&
            TypeRef(scrut_type).kind() != LogosType::Kind::Never) {  // G160-10
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
            auto hex = [&](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            switch (body[1]) {
                case 'n': return '\n';
                case 't': return '\t';
                case 'r': return '\r';
                case '0': return 0;
                case '\\': return '\\';
                case '\'': return '\'';
                case '"': return '"';
                case 'x': {
                    if (body.size() != 4) {
                        error(std::format("char literal '{}': '\\x' requires exactly 2 hex digits", sv));
                        return 0;
                    }
                    int h1 = hex(body[2]), h2 = hex(body[3]);
                    if (h1 < 0 || h2 < 0) {
                        error(std::format("char literal '{}': '\\x' requires hex digits", sv));
                        return 0;
                    }
                    return (int64_t)((h1 << 4) | h2);
                }
                case 'u': {
                    if (body.size() < 5 || body[2] != '{' || body.back() != '}') {
                        error(std::format("char literal '{}': '\\u' requires '{{HEX}}' form", sv));
                        return 0;
                    }
                    uint32_t cp = 0;
                    size_t end = body.size() - 1;
                    for (size_t i = 3; i < end; ++i) {
                        int h = hex(body[i]);
                        if (h < 0) {
                            error(std::format("char literal '{}': '\\u' requires hex digits", sv));
                            return 0;
                        }
                        cp = (cp << 4) | (uint32_t)h;
                    }
                    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
                        error(std::format("char literal '{}': invalid Unicode scalar U+{:X}", sv, cp));
                        return 0;
                    }
                    return (int64_t)cp;
                }
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
    if (pc == la::PAT_OR) return build_pattern_or(pnode, scrut_type);
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
        // Default binding modes: a `&(T,U)` / `&mut (T,U)` scrutinee is accepted
        // (deref to the inner tuple — gen_match uses the ref ptr directly as the
        // tuple base). Under a shared `&`, a move-only element binds by
        // reference; Copy elements stay by-value. Mirrors the enum/struct gates.
        lir::PatTuple pt;
        TypeRef tst = scrut_type;
        bool default_ref = false, default_mut = false;
        if (tst &&
            (TypeRef(tst).kind() == LogosType::Kind::Ref ||
             TypeRef(tst).kind() == LogosType::Kind::MutRef) &&
            TypeRef(tst).pointee() &&
            TypeRef(TypeRef(tst).pointee()).kind() == LogosType::Kind::Tuple) {
            default_ref = true;
            default_mut = TypeRef(tst).kind() == LogosType::Kind::MutRef;
            tst = TypeRef(tst).pointee();
        }
        if (!tst || TypeRef(tst).kind() != LogosType::Kind::Tuple) {
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
        size_t tuple_arity = TypeRef(tst).tuple_elems().size();
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
                elem_ty = TypeRef(tst).tuple_elems()[i];
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
            } else if (sc == la::PAT_STR.code && current_pat_refutable_guards_) {
                // G172-1b: string-literal tuple element (`("foo", _)`). The
                // tuple-arm codegen has no str_eq dispatch, so instead bind the
                // element to a synth name and gate the arm with
                // `str_eq(synth, "foo")` (a raw `==` would pointer-compare).
                std::string synth = std::format("__tstr_{}_{}", i, tmp_var_count_++);
                pt.bindings.push_back(synth);
                pt.subs.push_back(make_pat_wild(synth));
                TypeRef str_t = make_slice_type(u8_t());
                auto strlit = builder().lit_str(
                    std::string(str_of(sub.get(la::VALUE.code))), str_t);
                auto g = make_str_eq_guard(
                    builder().var_ref(synth,
                        (elem_ty && TypeRef(elem_ty).kind() != LogosType::Kind::Error) ? elem_ty : str_t),
                    std::move(strlit));
                if (g) current_pat_refutable_guards_->push_back(std::move(g));
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
                        } else if (isc == la::PAT_STR.code &&
                                   current_pat_refutable_guards_) {
                            // G172-1b: string-literal tuple element (wrapped in
                            // the grammar's single-alt PAT_OR). Bind + str_eq
                            // guard (the tuple-arm codegen has no str_eq path).
                            std::string synth = std::format("__tstr_{}_{}", i, tmp_var_count_++);
                            pt.bindings.push_back(synth);
                            pt.subs.push_back(make_pat_wild(synth));
                            TypeRef str_t = make_slice_type(u8_t());
                            auto strlit = builder().lit_str(
                                std::string(str_of(inner.get(la::VALUE.code))), str_t);
                            auto g = make_str_eq_guard(
                                builder().var_ref(synth,
                                    (elem_ty && TypeRef(elem_ty).kind() != LogosType::Kind::Error) ? elem_ty : str_t),
                                std::move(strlit));
                            if (g) current_pat_refutable_guards_->push_back(std::move(g));
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
        // Fill binding types (default-ref move-only elems under a shared &).
        for (size_t i = 0; i < TypeRef(tst).tuple_elems().size(); ++i) {
            TypeRef et = TypeRef(tst).tuple_elems()[i];
            if (default_ref && et && TypeRef(et).kind() != LogosType::Kind::Error &&
                TypeRef(et).kind() != LogosType::Kind::TypeVar && is_move_type(et))
                et = make_ref(default_mut, et);
            pt.binding_types.push_back(et);
        }
        auto mo = lir_mirror_emit_pat_tuple(*cur_prog_, pt.bindings, pt.binding_types, pt.subs);
        lir::Pattern p_;
        p_.mirror_offset_ = mo;
        return p_;
    }
    // ── PAT_RANGE: 0..=9 inclusive integer range ──────────────────────────
    if (pc == la::PAT_RANGE) {
        // Half-open forms (`a..`, `..=b`, `..b`) omit one bound key; clamp the
        // open side to the scrutinee integer type's min/max.
        bool has_lo = pnode.has_key(la::LHS);
        bool has_hi = pnode.has_key(la::RHS);
        auto int_bounds = [](LogosType::Kind k) -> std::pair<int64_t,int64_t> {
            switch (k) {
            case LogosType::Kind::I8:  return {-128, 127};
            case LogosType::Kind::U8:  return {0, 255};
            case LogosType::Kind::I16: return {-32768, 32767};
            case LogosType::Kind::U16: return {0, 65535};
            case LogosType::Kind::U32: return {0, (int64_t)UINT32_MAX};
            case LogosType::Kind::I64: case LogosType::Kind::Isize:
                                       return {INT64_MIN, INT64_MAX};
            case LogosType::Kind::U64: case LogosType::Kind::Usize:
                                       return {0, INT64_MAX};
            default:                   return {(int64_t)INT32_MIN, (int64_t)INT32_MAX};
            }
        };
        auto [tmin, tmax] = int_bounds(scrut_type ? TypeRef(scrut_type).kind()
                                                   : LogosType::Kind::I32);
        int64_t lo = has_lo ? parse_int_literal(str_of(pnode.get(la::LHS.code))) : tmin;
        int64_t hi = has_hi ? parse_int_literal(str_of(pnode.get(la::RHS.code))) : tmax;
        if (has_lo && pnode.has_key(la::LO_NEG)) {
            AnyVal av = pnode.get(la::LO_NEG.code);
            if (!av.is_null() && av.is_value() && av.as_value<uint8_t>()) lo = -lo;
        }
        if (has_hi && pnode.has_key(la::HI_NEG)) {
            AnyVal av = pnode.get(la::HI_NEG.code);
            if (!av.is_null() && av.is_value() && av.as_value<uint8_t>()) hi = -hi;
        }
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error &&
            TypeRef(scrut_type).kind() != LogosType::Kind::Never &&  // G160-10
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
        // A bare identifier that names a NO-PAYLOAD variant of the scrutinee's
        // enum is a variant pattern, not a binding — e.g. `None` over
        // `Option<T>` (the prelude variants `None`/`Some`/`Ok`/`Err` and
        // user enums matched without the `Enum::` qualifier). Without this the
        // bare name was lowered as an irrefutable wildcard binding, so
        // `match opt { None => …, Some(_) => … }` / `if let None = opt`
        // mis-dispatched (the `None` arm caught everything) and mis-codegened.
        if (pnode.has_key(la::NAME) && scrut_type &&
            TypeRef(scrut_type).kind() == LogosType::Kind::Enum) {
            std::string nm(str_of(pnode.get(la::NAME.code)));
            if (!nm.empty() && nm != "_") {
                std::string en(TypeRef(scrut_type).enum_name());
                auto [epkg_v, esi_v] = find_enum_by_name(en);
                if (esi_v) {
                    for (auto& v : esi_v->variants) {
                        if (v.name == nm && v.payload_types.empty()) {
                            lir::Pattern p_;
                            p_.mirror_offset_ = lir_mirror_emit_pat_variant(
                                *cur_prog_, en, nm, v.value);
                            return p_;
                        }
                    }
                }
            }
        }
    }

    // ── PAT_STRUCT: Point { x: p, y } or Point { .. } ────────────────────
    if (pc == la::PAT_STRUCT) {
        auto sname = std::string(str_of(pnode.get(la::NAME.code)));
        // Look up struct or datatype info.
        const SemaStructInfo* sinfo = nullptr;
        { auto [sp, si] = find_struct_by_name(sname); sinfo = si; }
        if (!sinfo) { auto [dp, di] = find_datatype_by_name(sname); sinfo = di; }
        if (!sinfo) {
            // G152-10: a type alias used as a struct pattern (`type S2 = S;
            // match x { S2 { a, b } => … }`). Resolve the alias to its target
            // struct and match under the real name (codegen + scrutinee check
            // need it). Construction already resolves aliases.
            auto ait = type_aliases_.find(sname);
            if (ait != type_aliases_.end() && ait->second.type &&
                (TypeRef(ait->second.type).kind() == LogosType::Kind::Struct ||
                 TypeRef(ait->second.type).kind() == LogosType::Kind::ZonedStruct)) {
                std::string target(TypeRef(ait->second.type).struct_name());
                if (!target.empty()) {
                    if (auto [sp2, si2] = find_struct_by_name(target); si2) {
                        sinfo = si2; sname = target;
                    } else if (auto [dp2, di2] = find_datatype_by_name(target); di2) {
                        sinfo = di2; sname = target;
                    }
                }
            }
        }
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
                            auto sub_node = map_of(fnode.get(la::VALUE.code));
                            int32_t sknode = code_of(sub_node);
                            // Refutable LITERAL field sub-pattern (`A { v: 1 }`,
                            // `S { ok: true }`): bind the field to a synth name +
                            // gate the arm with a `synth == <literal>` guard
                            // (mirrors the tuple-element / variant-payload idiom).
                            bool is_lit = sknode == la::PAT_INT || sknode == la::PAT_NEG_INT ||
                                          sknode == la::PAT_BOOL || sknode == la::PAT_CHAR;
                            if (is_lit && current_pat_refutable_guards_) {
                                std::string syn = std::format("__sfld_{}_{}", fname, tmp_var_count_++);
                                TypeRef ft = (ftype && TypeRef(ftype).kind() != LogosType::Kind::Error)
                                    ? ftype : prim(LogosType::Kind::I64);
                                define(syn, ft);
                                lir::LExprPtr value;
                                if (sknode == la::PAT_INT && sub_node.has_key(la::VALUE))
                                    value = builder().lit_int(parse_int_literal(str_of(sub_node.get(la::VALUE.code))), ft);
                                else if (sknode == la::PAT_NEG_INT && sub_node.has_key(la::VALUE))
                                    value = builder().lit_int(-parse_int_literal(str_of(sub_node.get(la::VALUE.code))), ft);
                                else if (sknode == la::PAT_BOOL && sub_node.has_key(la::VALUE))
                                    value = builder().lit_bool(sub_node.get(la::VALUE.code).as_value<int32_t>() != 0, bool_t());
                                else if (sknode == la::PAT_CHAR && sub_node.has_key(la::VALUE)) {
                                    auto sv = str_of(sub_node.get(la::VALUE.code));
                                    value = builder().lit_int(sv.empty() ? 0 : (int64_t)(uint8_t)sv[0], prim(LogosType::Kind::Char));
                                }
                                if (value) {
                                    auto guard = builder().bin_op("==",
                                        builder().var_ref(syn, ft), std::move(value), bool_t());
                                    current_pat_refutable_guards_->push_back(std::move(guard));
                                }
                                pfb.sub.push_back(make_pat_wild(syn));
                                ps.fields.push_back(std::move(pfb));
                                continue;
                            }
                            auto sub = build_pattern(sub_node, ftype);
                            // G148-1: refutable field sub-patterns (variant /
                            // tuple / range / or) are tested+bound by the
                            // recursive matcher (pat_test/pat_bind) in struct-arm
                            // codegen. Exotic kinds (slice, hermes) still aren't.
                            namespace ps2 = lir_schema::pat;
                            auto sk = pat_ref_of(sub).kind();
                            bool sub_ok =
                                sk == ps2::Code::Wild || sk == ps2::Code::RefBind ||
                                sk == ps2::Code::RefPat || sk == ps2::Code::At ||
                                sk == ps2::Code::Variant || sk == ps2::Code::VariantData ||
                                sk == ps2::Code::Tuple || sk == ps2::Code::Or ||
                                sk == ps2::Code::Range || sk == ps2::Code::Int ||
                                sk == ps2::Code::Bool || sk == ps2::Code::Struct;
                            if (!sub_ok)
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
                            // G149-4: `xs @ ..` carries a NAME — bind the
                            // rest sub-slice to it (else anonymous `..`).
                            std::string rest_name = "_";
                            if (enode.has_key(la::NAME))
                                rest_name = std::string(str_of(enode.get(la::NAME.code)));
                            psl.rest.push_back(make_pat_wild(rest_name));
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
        // G167-6a: suffix elements after `..` on a dynamic slice ARE supported —
        // codegen indexes them from the runtime length (`len - suf_n + i`) and
        // gates the arm on `len >= prefix + suffix`. (Previously rejected.)
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
    // CP-cm-02: bare ident in pattern resolves as a variant if a
    // `use Type.{V1, …};` alias maps it. Route through the same
    // PAT_VARIANT path used by `Type::V` form.
    if (wname != "_") {
        auto vit = cur_imports_.variant_aliases.find(wname);
        if (vit != cur_imports_.variant_aliases.end()) {
            auto [vpkg, vesi] = find_enum_by_name(vit->second);
            if (vesi) {
                const SemaVariantInfo* vinfo = nullptr;
                for (auto& v : vesi->variants)
                    if (v.name == wname) { vinfo = &v; break; }
                if (vinfo && vinfo->payload_types.empty()) {
                    int32_t disc = vinfo->value;
                    if (scrut_type && TypeRef(scrut_type).kind() == LogosType::Kind::Enum &&
                        TypeRef(scrut_type).enum_name() != vit->second)
                        error(std::format("pattern: enum '{}' != scrutinee '{}'",
                              vit->second, type_str(scrut_type)));
                    lir::Pattern p_;
                    p_.mirror_offset_ = lir_mirror_emit_pat_variant(
                        *cur_prog_, vit->second, wname, disc);
                    return p_;
                }
            }
        }
    }
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
            // After the three-layer split: AnyVal predicates live in
            // logos.lang.hermes.anyval; the str-eq helper (one HermesString
            // user) lives in std.hermes.pat.
            const char* hint =
                std::strcmp(helper, "hermes_pat_eq_str") == 0
                ? "use logos.mem.hermes.pat;"
                : "use logos.lang.hermes.anyval;";
            error(std::format(
                "Hermes pattern needs stdlib helper `{}`; `{}`",
                helper, hint));
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
                "Hermes pattern needs stdlib helper `{}`; `use logos.mem.hermes.pat;`",
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
                "Hermes pattern needs stdlib helper `{}`; `use logos.mem.hermes.pat;`",
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
                "Hermes pattern needs stdlib helper `{}`; `use logos.mem.hermes.pat;`",
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
                "Hermes pattern needs stdlib helper `{}`; `use logos.mem.hermes.pat;`",
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
                "Hermes pattern needs stdlib helper `{}`; `use logos.mem.hermes.pat;`",
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
        // CP-cm-17: skip `_` payload bindings. Without this, `Some(_)`
        // pulls a "_" binding into scope; collect_drops at scope end
        // then emits a drop on the payload (Vec.drop, String.drop, …)
        // even though the user wrote a wildcard. Mirrors the Tuple
        // branch's filter below.
        for (size_t i = 0; i < names.size() && i < types.size(); ++i)
            if (names[i] != "_")
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
        // P4-pm-24 / G144-1: recurse into refutable sub-patterns so any nested
        // bindings (`(E::Foo { x }, _)`, `((true,y)|(y,true), z)`, `((a,b), w)`)
        // reach the outer arm scope. Codegen (pat_test/pat_bind) extracts them.
        size_t idx = 0;
        v.each_sub([&](lir_view::PatRef sp) {
            if (sp && (sp.kind() == ps::Code::VariantData ||
                       sp.kind() == ps::Code::Or ||
                       sp.kind() == ps::Code::Tuple)) {
                TypeRef sub_t = idx < types.size() ? types[idx] : error_t();
                bind_pattern_ref(sp, sub_t);
            }
            ++idx;
        });
    } else if (k == ps::Code::Or) {
        // G144-1: an or-pattern (possibly nested as a tuple element). All alts
        // bind the same names+types (build_pattern_or enforced this); declare
        // from the first alt. Codegen dispatches per-alt + extracts.
        lir_view::PatRef first;
        lir_view::PatOrView{pr}.each_alt([&](lir_view::PatRef a){ if (!first) first = a; });
        if (first) bind_pattern_ref(first, scrut_type);
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
        // structs_/datatypes_ are keyed by package-qualified names; a bare
        // `structs_.find(sname)` misses (returns null), leaving every field
        // typed Error. The statement-form match masked this (the arm value
        // type is unused), but match-as-EXPRESSION propagates the Error arm
        // type to the whole match → `logos_to_mlir(Error)` is null → empty
        // function body. Route through the package-aware lookups.
        const SemaStructInfo* sinfo = find_struct_by_name(sname).second;
        if (!sinfo) sinfo = find_datatype_by_name(sname).second;
        // G152-12: substitute the struct's generic type-args into field types,
        // so `match s { S3 { x, y } }` over `S3<u8,u16>` binds x:u8 / y:u16 — not
        // the template vars U/V (which mismatch any concrete op `==`/assert_eq).
        // Field-ACCESS `s.x` already resolves concretely; the pattern path
        // didn't. Deref a &Struct scrutinee for the type-args.
        SemaSubst struct_subst;
        {
            TypeRef sst = scrut_type;
            if (sst && (TypeRef(sst).kind() == LogosType::Kind::Ref ||
                        TypeRef(sst).kind() == LogosType::Kind::MutRef ||
                        TypeRef(sst).kind() == LogosType::Kind::Ptr) &&
                TypeRef(sst).pointee())
                sst = TypeRef(sst).pointee();
            if (sinfo && sst &&
                (TypeRef(sst).kind() == LogosType::Kind::Struct ||
                 TypeRef(sst).kind() == LogosType::Kind::ZonedStruct) &&
                !TypeRef(sst).type_args().empty())
                for (size_t k = 0; k < sinfo->type_params.size() &&
                                   k < TypeRef(sst).type_args().size(); ++k)
                    struct_subst[sinfo->type_params[k].name] = TypeRef(sst).type_args()[k];
        }
        // Default binding modes (RFC 2005), struct shape: under a SHARED `&`
        // scrutinee a plain shorthand field of a MOVE-ONLY type binds BY
        // REFERENCE — so it doesn't move the owned field out of the borrow (the
        // field binding would otherwise be Drop-scheduled and double-free the
        // scrutinee's buffer at arm exit). Codegen already binds an aggregate
        // field's GEP address; this just makes collect_drops skip it. Copy
        // fields stay by-value. Mirrors the enum-variant gate (incl. the
        // bare-TypeVar self-ref guard for `&mut`).
        bool default_ref = scrut_type &&
            (TypeRef(scrut_type).kind() == LogosType::Kind::Ref ||
             TypeRef(scrut_type).kind() == LogosType::Kind::MutRef);
        bool default_mut = scrut_type &&
            TypeRef(scrut_type).kind() == LogosType::Kind::MutRef;
        v.each_field([&](lir_view::PatFieldBindingView fv) {
            auto fname = fv.field_name();
            TypeRef ftype = error_t();
            if (sinfo)
                for (auto& f : sinfo->fields)
                    if (f.name == fname) {
                        ftype = struct_subst.empty() ? f.type
                                                     : subst_type_sema(f.type, struct_subst);
                        break;
                    }
            auto sub = fv.sub();
            if (!sub) {
                TypeRef bt = ftype;
                if (default_ref && ftype &&
                    TypeRef(ftype).kind() != LogosType::Kind::Error &&
                    TypeRef(ftype).kind() != LogosType::Kind::TypeVar &&
                    is_move_type(ftype))
                    bt = make_ref(default_mut, ftype);
                define(std::string(fname), bt);
            }
            else bind_pattern_ref(sub, ftype);
        });
    } else if (k == ps::Code::Slice) {
        lir_view::PatSliceView v{pr};
        TypeRef elem_t = (scrut_type && TypeRef(scrut_type).elem())
                          ? TypeRef(scrut_type).elem() : error_t();
        v.each_prefix([&](lir_view::PatRef p) { bind_pattern_ref(p, elem_t); });
        // G149-4: a named rest (`xs @ ..`) binds the sub-slice as `&[T]`
        // (Slice kind), not an element. Anonymous `_` rest binds nothing.
        TypeRef rest_slice_t = make_slice_type(elem_t);
        v.each_rest  ([&](lir_view::PatRef p) { bind_pattern_ref(p, rest_slice_t); });
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

        // Wire the same nested-pattern + refutable-guard channels lower_match
        // uses, so `if let Some(Some(v)) = o` / `if let E::V((a, b)) = e` lower
        // their nested payload patterns instead of erroring "nested patterns …
        // not yet supported".
        std::vector<NestedPatSub> nested_subs;
        std::vector<lir::LExprPtr> refut_guards;
        auto* saved_subs = current_pat_nested_subs_;
        auto* saved_refut = current_pat_refutable_guards_;
        current_pat_nested_subs_ = &nested_subs;
        current_pat_refutable_guards_ = &refut_guards;
        auto pat = build_pattern(map_of(node.get(la::PAT.code)), scrut_type);
        current_pat_nested_subs_ = saved_subs;
        current_pat_refutable_guards_ = saved_refut;

        // Then arm: pattern → then block. Define + emit the nested-payload
        // destructures BEFORE the body so its bindings are in scope.
        push_scope();
        bind_pattern(pat, scrut_type);
        std::vector<lir::LStmt> nested_destructure;
        emit_nested_pat_destructure(nested_subs, nested_destructure, /*for_guard=*/false);
        // Let-chain trailing condition: `if let P = e && <cond>` desugars to
        // `match e { P if <cond> => THEN, _ => ELSE }` — the chain cond becomes
        // an arm guard (no else duplication). Lowered HERE so it sees the
        // pattern's bindings (`if let Some(x) = o && x > 0`).
        lir::LExprPtr chain_guard = nullptr;
        if (node.has_key(la::GUARD)) {
            chain_guard = lower_expr(map_of(node.get(la::GUARD.code)));
            if (chain_guard && TypeRef(chain_guard->type).kind() != LogosType::Kind::Bool &&
                TypeRef(chain_guard->type).kind() != LogosType::Kind::Error)
                error(std::format("if-let chain condition must be bool, got {}",
                      type_str(chain_guard->type)));
            // The guard runs BEFORE the arm body, so any nested-payload bindings
            // it references (`if let Some((a,b)) = e && a > b`) must be
            // re-extracted as a guard prologue (mirrors lower_match's B170-D/E).
            // Tuple/struct nested subs are guard-safe; a nested ENUM-VARIANT sub
            // can't be bound in a guard (its destructure is a refutable let-else)
            // → reject cleanly instead of reading an unbound value.
            if (chain_guard && !nested_subs.empty()) {
                for (auto& ns : nested_subs)
                    if (code_of(ns.sub_pat_node) == la::PAT_VARIANT_DATA) {
                        error("let-chain condition cannot yet reference bindings "
                              "from a nested enum-variant pattern; match in the "
                              "body instead");
                        break;
                    }
                std::vector<lir::LStmt> gd;
                emit_nested_pat_destructure(nested_subs, gd, /*for_guard=*/true);
                if (!gd.empty()) {
                    auto gblk = lir::alloc_block(*cur_prog_);
                    gblk->stmts = std::move(gd);
                    TypeRef gt = chain_guard->type;
                    chain_guard = builder().block_expr(std::move(gblk),
                                      std::move(chain_guard), gt);
                }
            }
        }
        lir::LBlockPtr then_body = lir::alloc_block(*cur_prog_);
        if (node.has_key(la::THEN))
            *then_body = lower_block(map_of(node.get(la::THEN.code)));
        if (!nested_destructure.empty()) {
            std::vector<lir::LStmt> merged = std::move(nested_destructure);
            merged.insert(merged.end(),
                          std::make_move_iterator(then_body->stmts.begin()),
                          std::make_move_iterator(then_body->stmts.end()));
            then_body->stmts = std::move(merged);
        }
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

        // Refutable-inner guards (nested variant/literal payload predicates)
        // gate the then-arm; a failure falls through to the wildcard else-arm.
        std::optional<lir::LExprPtr> guard;
        for (auto& rg : refut_guards) {
            if (!rg) continue;
            if (guard)
                guard = builder().bin_op("&&", std::move(*guard), std::move(rg), bool_t());
            else
                guard = std::move(rg);
        }
        // AND the let-chain trailing condition last (after the pattern's own
        // refutable guards), so it runs only once the pattern matched.
        if (chain_guard) {
            if (guard)
                guard = builder().bin_op("&&", std::move(*guard), std::move(chain_guard), bool_t());
            else
                guard = std::move(chain_guard);
        }

        lir::SMatch sm;
        sm.scrut = std::move(scrut);
        sm.arms.push_back({std::move(pat), std::move(then_body), std::move(guard)});
        sm.arms.push_back({make_pat_wild("_"), std::move(else_body), std::nullopt});
        return make_stmt_emit(node_line_, std::move(sm));
    }

    // ── regular if cond { ... } ────────────────────────────────────
    lir::LExprPtr cond = nullptr;
    if (node.has_key(la::COND)) {
        cond = lower_expr(map_of(node.get(la::COND.code)));
        if (TypeRef(cond->type).kind() != LogosType::Kind::Bool &&
            TypeRef(cond->type).kind() != LogosType::Kind::Error &&
            TypeRef(cond->type).kind() != LogosType::Kind::Never)  // G160-10: `if (return x){}`
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
        // G152-16: capture the loop label BEFORE lowering scrut/body (a nested
        // loop would otherwise steal pending_loop_label_), and thread it onto
        // the desugared SLoop + the break-frame so `'a: while let … { break 'a }`
        // resolves. Without this the label was dropped → "label not in scope".
        std::string my_label = std::move(pending_loop_label_);
        pending_loop_label_.clear();

        auto scrut = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
        TypeRef scrut_type = scrut->type;

        // Same nested-pattern + refutable-guard channels as lower_match / if-let.
        std::vector<NestedPatSub> nested_subs;
        std::vector<lir::LExprPtr> refut_guards;
        auto* saved_subs = current_pat_nested_subs_;
        auto* saved_refut = current_pat_refutable_guards_;
        current_pat_nested_subs_ = &nested_subs;
        current_pat_refutable_guards_ = &refut_guards;
        auto pat = build_pattern(map_of(node.get(la::PAT.code)), scrut_type);
        current_pat_nested_subs_ = saved_subs;
        current_pat_refutable_guards_ = saved_refut;

        // Then arm: pattern → loop body
        push_scope();
        bind_pattern(pat, scrut_type);
        std::vector<lir::LStmt> nested_destructure;
        emit_nested_pat_destructure(nested_subs, nested_destructure, /*for_guard=*/false);
        // Let-chain trailing condition: `while let P = e && <cond>` desugars to
        // `loop { match e { P if <cond> => BODY, _ => break } }` — the chain cond
        // becomes an arm guard. Lowered HERE so it sees the pattern's bindings
        // (mirrors the if-let chain in lower_if).
        lir::LExprPtr chain_guard = nullptr;
        if (node.has_key(la::GUARD)) {
            chain_guard = lower_expr(map_of(node.get(la::GUARD.code)));
            if (chain_guard && TypeRef(chain_guard->type).kind() != LogosType::Kind::Bool &&
                TypeRef(chain_guard->type).kind() != LogosType::Kind::Error)
                error(std::format("while-let chain condition must be bool, got {}",
                      type_str(chain_guard->type)));
            if (chain_guard && !nested_subs.empty()) {
                for (auto& ns : nested_subs)
                    if (code_of(ns.sub_pat_node) == la::PAT_VARIANT_DATA) {
                        error("while-let chain condition cannot yet reference bindings "
                              "from a nested enum-variant pattern; match in the body instead");
                        break;
                    }
                std::vector<lir::LStmt> gd;
                emit_nested_pat_destructure(nested_subs, gd, /*for_guard=*/true);
                if (!gd.empty()) {
                    auto gblk = lir::alloc_block(*cur_prog_);
                    gblk->stmts = std::move(gd);
                    TypeRef gt = chain_guard->type;
                    chain_guard = builder().block_expr(std::move(gblk),
                                      std::move(chain_guard), gt);
                }
            }
        }
        lir::LBlockPtr then_body = lir::alloc_block(*cur_prog_);
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            if (!my_label.empty()) active_loop_labels_.push_back(my_label);
            loop_break_frames_.push_back({my_label, nullptr, false});
        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
            *then_body = lower_block(map_of(node.get(la::BODY.code)));
            loop_break_frames_.pop_back();
            if (!my_label.empty()) active_loop_labels_.pop_back();
            --loop_depth_;
        }
        if (!nested_destructure.empty()) {
            std::vector<lir::LStmt> merged = std::move(nested_destructure);
            merged.insert(merged.end(),
                          std::make_move_iterator(then_body->stmts.begin()),
                          std::make_move_iterator(then_body->stmts.end()));
            then_body->stmts = std::move(merged);
        }
        pop_scope();

        // Else arm: wildcard → break
        lir::LBlockPtr else_body = lir::alloc_block(*cur_prog_);
        else_body->stmts.push_back(builder().stmt_break(nullptr, "", node_line_));

        std::optional<lir::LExprPtr> guard;
        for (auto& rg : refut_guards) {
            if (!rg) continue;
            if (guard)
                guard = builder().bin_op("&&", std::move(*guard), std::move(rg), bool_t());
            else
                guard = std::move(rg);
        }
        // Fold the let-chain trailing condition into the arm guard.
        if (chain_guard) {
            if (guard)
                guard = builder().bin_op("&&", std::move(*guard), std::move(chain_guard), bool_t());
            else
                guard = std::move(chain_guard);
        }

        lir::SMatch sm;
        sm.scrut = std::move(scrut);
        sm.arms.push_back({std::move(pat), std::move(then_body), std::move(guard)});
        sm.arms.push_back({make_pat_wild("_"), std::move(else_body), std::nullopt});

        auto loop_body = lir::alloc_block(*cur_prog_);
        loop_body->stmts.push_back(make_stmt_emit(node_line_, std::move(sm)));
        lir::SLoop sl; sl.body = std::move(loop_body);
        sl.label = std::move(my_label);
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
        loop_break_frames_.push_back({my_label, nullptr, false});
        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
        *body = lower_block(map_of(node.get(la::BODY.code)));
        loop_break_frames_.pop_back();
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
        loop_break_frames_.push_back({my_label, nullptr, false});
        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
        *body = lower_block(map_of(node.get(la::BODY.code)));
        loop_break_frames_.pop_back();
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
    // G-CONF-1: `for PATTERN in iter`. A bare-ident loop var arrives as NAME
    // (fast path); a destructuring pattern (`for (a,b) in v`) arrives as PAT.
    // Bind a synthetic element var and destructure the pattern from it as a
    // body prologue (see emit_for_pattern_destructure + the per-path wiring).
    bool for_has_pat = false;
    hermes::TinyMapView for_pat{};
    std::string var_name;
    if (node.has_key(la::PAT)) {
        for_pat = map_of(node.get(la::PAT.code));
        auto p = for_pat;
        if (code_of(p) == la::PAT_OR && p.has_key(la::ITEMS)) {
            auto alts = arr_of(p.get(la::ITEMS.code));
            if (alts.size() == 1) p = map_of(alts.get(0));
        }
        if (code_of(p) == la::PAT_WILD && p.has_key(la::NAME)) {
            var_name = std::string(str_of(p.get(la::NAME.code)));  // bare binding
        } else {
            for_has_pat = true;
            var_name = std::format("__fe_pat_{}", tmp_var_count_++);
        }
    } else {
        var_name = std::string(str_of(node.get(la::NAME.code)));
    }
    // Two-phase: `build_for_pat(bind_t)` runs BEFORE lower_block — it defines the
    // pattern's bindings in the just-pushed loop scope (so the body sees them)
    // and returns the destructure `let`s; `prepend_for_pat` runs AFTER, prepending
    // them to the lowered body. `bind_t` is the element's binding type (value/&T).
    auto build_for_pat = [&](TypeRef bind_t) -> std::vector<lir::LStmt> {
        std::vector<lir::LStmt> pro;
        if (for_has_pat) emit_for_pattern_destructure(for_pat, var_name, bind_t, pro);
        return pro;
    };
    auto prepend_for_pat = [&](lir::LBlockPtr& body, std::vector<lir::LStmt>& pro) {
        if (pro.empty()) return;
        pro.insert(pro.end(), std::make_move_iterator(body->stmts.begin()),
                   std::make_move_iterator(body->stmts.end()));
        body->stmts = std::move(pro);
    };

    // G161-2: `for x in &mut arr` — a bare `&mut <array-var>` lowers to a thin
    // `&mut elem` (stdlib-compat, see ADDR_OF_MUT), which isn't iterable. For
    // the for-loop, build a mutable slice over the array and iterate yielding
    // `&mut T` (the shared `&arr` form already slices + yields `&T`).
    bool for_mut_ref = false;
    lir::LExprPtr iter;
    if (node.has_key(la::ITER)) {
        auto inode = map_of(node.get(la::ITER.code));
        if (code_of(inode) == la::ADDR_OF_MUT && inode.has_key(la::VALUE)) {
            auto child = map_of(inode.get(la::VALUE.code));
            if (code_of(child) == la::VAR_REF) {
                auto vn = str_of(child.get(la::NAME.code));
                auto vt = lookup(vn);
                if (vt && TypeRef(vt).kind() == LogosType::Kind::Array) {
                    auto addr = builder().addr_of(std::string(vn),
                                    make_ref(true, TypeRef(vt).elem()));
                    auto len  = builder().lit_int(
                                    (int64_t)TypeRef(vt).arr_size(),
                                    prim(LogosType::Kind::I64));
                    iter = builder().slice_lit(std::move(addr), std::move(len),
                                    make_slice_type(TypeRef(vt).elem()));
                    for_mut_ref = true;
                }
            }
        }
        if (!iter) iter = lower_expr(inode);
    } else {
        iter = error_expr();
    }

    TypeRef iter_type = iter->type;

    // ── array path (original) ────────────────────────────────────
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        int64_t arr_size = (int64_t)TypeRef(iter_type).arr_size();
        TypeRef elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();

        push_scope();
        define(var_name, elem_type, false);
        auto pat_pro = build_for_pat(elem_type);
        auto body = lir::alloc_block(*cur_prog_);
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            loop_break_frames_.push_back({"", nullptr, false});
        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
            *body = lower_block(map_of(node.get(la::BODY.code)));
            loop_break_frames_.pop_back();
            --loop_depth_;
        }
        prepend_for_pat(body, pat_pro);
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
        // Rust parity: iterating a slice (`for x in &arr` / `&slice`) yields
        // `&T`, NOT `T` by value (you cannot move out of a borrow). The body
        // binding is a reference; codegen binds the element address. The raw
        // `elem_type` still flows to sfe.elem_type for the GEP stride.
        // G161-2: `for x in &mut arr` yields `&mut T` (mutable element ref).
        define(var_name, make_ref(for_mut_ref, elem_type), for_mut_ref);
        auto pat_pro = build_for_pat(make_ref(for_mut_ref, elem_type));
        auto body = lir::alloc_block(*cur_prog_);
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            loop_break_frames_.push_back({"", nullptr, false});
        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
            *body = lower_block(map_of(node.get(la::BODY.code)));
            loop_break_frames_.pop_back();
            --loop_depth_;
        }
        prepend_for_pat(body, pat_pro);
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

    // ── &Vec<T> path: Rust parity — `for x in &vec` borrows the Vec as a
    // slice and yields `&T` (NOT `T` by value via Vec::iter, which would move
    // out of a borrow). Desugar `&vec` → `vec.as_slice()` and reuse the by-ref
    // slice path. (By-value `for x in vec` keeps the consuming VecIter path.)
    if ((TypeRef(iter_type).kind() == LogosType::Kind::Ref ||
         TypeRef(iter_type).kind() == LogosType::Kind::MutRef) &&
        TypeRef(iter_type).pointee() &&
        TypeRef(TypeRef(iter_type).pointee()).struct_name() == "Vec") {
        TypeRef vec_ty = TypeRef(iter_type).pointee();
        if (auto fit = find_func_candidates("Vec__as_slice"); fit.size() == 1) {
            const SemaFuncInfo* as_slice_fn = fit[0];
            TypeRef elem_t = !TypeRef(vec_ty).type_args().empty()
                                 ? TypeRef(vec_ty).type_args()[0] : i32_t();
            TypeRef slice_ty = make_slice_type(elem_t);
            std::vector<lir::LExprPtr> pargs;
            pargs.push_back(std::move(iter));
            lir::LExprPtr slice_call;
            if (!as_slice_fn->type_params.empty())
                slice_call = finish_generic_call(
                    as_slice_fn->symbol_name.empty() ? std::string("Vec__as_slice")
                                                     : as_slice_fn->symbol_name,
                    *as_slice_fn, {elem_t}, std::move(pargs));
            else
                slice_call = builder().call(
                    as_slice_fn->symbol_name.empty() ? std::string("Vec__as_slice")
                                                     : as_slice_fn->symbol_name,
                    {}, std::move(pargs), slice_ty);

            push_scope();
            define(var_name, make_ref(false, elem_t), false);  // yields &T
            auto pat_pro = build_for_pat(make_ref(false, elem_t));
            auto body = lir::alloc_block(*cur_prog_);
            if (node.has_key(la::BODY)) {
                ++loop_depth_;
                loop_break_frames_.push_back({"", nullptr, false});
        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
                *body = lower_block(map_of(node.get(la::BODY.code)));
                loop_break_frames_.pop_back();
                --loop_depth_;
            }
            prepend_for_pat(body, pat_pro);
            pop_scope();
            lir::SForEach sfe;
            sfe.var       = std::string(var_name);
            sfe.iter      = std::move(slice_call);
            sfe.elem_type = elem_t;
            sfe.arr_size  = 0;
            sfe.is_slice  = true;
            sfe.body      = std::move(body);
            return make_stmt_emit(node_line_, std::move(sfe));
        }
    }

    // ── IntoIterator desugar ─────────────────────────────────────
    // `for x in <expr>` where <expr> is NOT itself an iterator (no `next()`)
    // but exposes `into_iter()` — call it and iterate the result. Rust parity
    // for `for x in opt` / `for x in result` and any user `impl IntoIterator`.
    // Mirrors the &Vec→as_slice desugar above; the produced iterator type then
    // flows through the `next()`-based iterator path below.
    if (TypeRef(iter_type).kind() == LogosType::Kind::Enum ||
        TypeRef(iter_type).kind() == LogosType::Kind::Struct) {
        std::string base = TypeRef(iter_type).kind() == LogosType::Kind::Enum
            ? std::string(TypeRef(iter_type).enum_name())
            : std::string(TypeRef(iter_type).struct_name());
        bool already_iter = !find_func_candidates(base + "__next").empty();
        if (!already_iter) {
            auto iicands = find_func_candidates(base + "__into_iter");
            const SemaFuncInfo* iif = iicands.size() == 1 ? iicands[0] : nullptr;
            if (iif && iif->ret_type) {
                // Substitute the impl's type-params := the receiver's type-args
                // to name the concrete iterator type for the generic call.
                SemaSubst subst;
                auto targs = TypeRef(iter_type).type_args();
                for (size_t i = 0; i < iif->type_params.size() && i < targs.size(); ++i)
                    subst[iif->type_params[i].name] = targs[i];
                TypeRef iter_ret = subst.empty() ? iif->ret_type
                                                 : subst_type_sema(iif->ret_type, subst);
                std::string sym = iif->symbol_name.empty() ? base + "__into_iter"
                                                           : iif->symbol_name;
                std::vector<lir::LExprPtr> pargs;
                pargs.push_back(std::move(iter));
                lir::LExprPtr it_call;
                if (!iif->type_params.empty())
                    it_call = finish_generic_call(sym, *iif, std::move(targs), std::move(pargs));
                else
                    it_call = builder().call(sym, {}, std::move(pargs), iter_ret);
                iter = std::move(it_call);
                iter_type = iter->type;
            }
        }
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
        auto pat_pro = build_for_pat(elem_type);
        auto then_body = lir::alloc_block(*cur_prog_);
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            loop_break_frames_.push_back({"", nullptr, false});
        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
            *then_body = lower_block(map_of(node.get(la::BODY.code)));
            loop_break_frames_.pop_back();
            --loop_depth_;
        }
        prepend_for_pat(then_body, pat_pro);
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
    TypeRef frame_value_type = nullptr;
    if (node.has_key(la::BODY)) {
        ++loop_depth_;
        if (!my_label.empty()) active_loop_labels_.push_back(my_label);
        loop_break_frames_.push_back({my_label, nullptr, false});
        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
        *body = lower_block(map_of(node.get(la::BODY.code)));
        frame_value_type = loop_break_frames_.back().value_type;
        loop_break_frames_.pop_back();
        if (!my_label.empty()) active_loop_labels_.pop_back();
        --loop_depth_;
    }
    lir::SLoop sl;
    sl.body  = std::move(body);
    sl.label = std::move(my_label);
    if (frame_value_type) {
        sl.result_type = frame_value_type;
        sl.break_slot  = "__loop_val_" + std::to_string(tmp_var_count_++);
    }
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

// G163-2: general place write — `<postfix-lvalue> = rhs` for any place the
// specialized write productions don't cover (chained index `a[i][j]`, deref +
// tuple index `(*p).0`, deep mixes). Lowers to `deref_write(&mut <place>, rhs)`:
// the place is a normal read-expr (IndexRead/FieldRead/TupleIndex/Deref tree),
// `&mut <place>` computes its real element address (EAddrOfTemp's place-aware
// GEP paths in mlir-gen), and the deref-write stores through it.
// A place receiver simple enough that the address-of machinery
// (gen_lvalue_addr) can compute a real address for it: a bare variable, a
// `*p` deref, or a single field/index off one of those. Used to bound the
// general place-write to shapes that lower correctly — deeper nestings
// (3-level `a[i][j][k]`, `g.rows[i].cells[j]`) hit a pre-existing read-side
// limitation, so they're rejected with a clean diagnostic instead of a crash.
// Strip a `( … )` PAREN_EXPR wrapper (the lowered LExpr is transparent, but
// the AST keeps the grouping node — e.g. `(*p).0` is TUPLE_INDEX(PAREN_EXPR(*p))).
hermes::TinyMapView SemaChecker::unwrap_paren_node(hermes::TinyMapView n) {
    while (code_of(n) == la::PAREN_EXPR && n.has_key(la::VALUE))
        n = map_of(n.get(la::VALUE.code));
    return n;
}
bool SemaChecker::place_recv_is_simple(hermes::TinyMapView recv) {
    int32_t code = code_of(unwrap_paren_node(recv));
    return code == la::VAR_REF || code == la::DEREF;
}
// A field-read base whose address gen_lvalue_addr can compute: a bare var /
// `*p`, a field chain, OR an index into a place — incl. a field off a
// struct-ARRAY element (`a[i].x`, `g.rows[i].cells[j].v`). The struct-element
// stride is now handled (gen_lvalue_addr + gen_index_read inline slot), so
// index-then-field places lower correctly.
bool SemaChecker::place_field_base_ok(hermes::TinyMapView recv) {
    recv = unwrap_paren_node(recv);
    int32_t c = code_of(recv);
    if (c == la::VAR_REF || c == la::DEREF) return true;
    if (c == la::FIELD_READ)
        return place_field_base_ok(map_of(recv.get(la::RECEIVER.code)));
    if (c == la::INDEX_READ)
        return place_write_supported(map_of(recv.get(la::RECEIVER.code)));
    if (c == la::TUPLE_INDEX)
        return place_recv_is_simple(map_of(recv.get(la::RECEIVER.code)));
    return false;
}
// Recursive: a place whose real address gen_lvalue_addr can compute. VAR_REF and
// `*p` bottom out; index chains recurse (gen_lvalue_addr strides each level by
// the full element aggregate type, so arbitrary index depth + struct/tuple array
// elements are fine); tuple-index and field reads are bounded to the shapes the
// address machinery + the read path both handle correctly.
bool SemaChecker::place_write_supported(TinyMapView place) {
    place = unwrap_paren_node(place);
    int32_t pc = code_of(place);
    if (pc == la::VAR_REF || pc == la::DEREF) return true;
    if (pc == la::INDEX_READ)
        return place_write_supported(map_of(place.get(la::RECEIVER.code)));
    if (pc == la::TUPLE_INDEX)
        return place_recv_is_simple(map_of(place.get(la::RECEIVER.code)));
    if (pc == la::FIELD_READ)
        return place_field_base_ok(map_of(place.get(la::RECEIVER.code)));
    return false;
}

// ── Uniform place subsystem (foundation) ────────────────────────────────────
// check_place_writable: walk a place to its root and reject a write through an
// immutable variable or a `*const` / shared-`&` dereference. Conservative — only
// rejects definitive cases (best-effort for complex deref operands), so it never
// false-rejects a valid write; the per-shape writers' stricter checks remain
// authoritative for the shapes they still own. Closes the latent soundness gap
// where the general place-write path (deep-nested `a[i][j] = v`) did NO mut-check.
// Returns true if writable; emits a diagnostic + returns false otherwise.
bool SemaChecker::check_place_writable(TinyMapView place) {
    place = unwrap_paren_node(place);
    int32_t c = code_of(place);
    if (c == la::VAR_REF) {
        auto name = std::string(str_of(place.get(la::NAME.code)));
        auto t = lookup(name);
        if (!t) return true;  // undefined var — surfaced elsewhere
        auto k = TypeRef(t).kind();
        if (k == LogosType::Kind::Ref) {  // `&T` shared ref — not writable
            error(std::format("assignment through a shared reference (variable '{}' is `&`)", name));
            return false;
        }
        if (k == LogosType::Kind::MutRef) return true;  // `&mut T` — writable
        if (k == LogosType::Kind::Ptr) {  // raw-pointer root (auto-deref place)
            if (!TypeRef(t).mut_ptr()) {
                error(std::format("assignment through a `*const` pointer (variable '{}')", name));
                return false;
            }
            if (!inside_unsafe_) {
                error("chain field write through raw pointer requires unsafe context");
                return false;
            }
            return true;  // `*mut` inside unsafe — writable (carries its own mutability)
        }
        if (!lookup_is_mut(name)) {
            error(std::format("assignment to immutable variable '{}'", name));
            return false;
        }
        return true;
    }
    if (c == la::DEREF) {
        auto op = map_of(place.get(la::VALUE.code));
        if (code_of(op) == la::VAR_REF) {  // best-effort: `*p` over a known ptr/ref
            auto t = lookup(std::string(str_of(op.get(la::NAME.code))));
            if (t) {
                auto k = TypeRef(t).kind();
                if (k == LogosType::Kind::Ptr && !TypeRef(t).mut_ptr()) {
                    error("assignment through a `*const` pointer (need `*mut`)");
                    return false;
                }
                if (k == LogosType::Kind::Ref) {
                    error("assignment through a shared reference `&` (need `&mut`)");
                    return false;
                }
                // Writing through a raw `*mut` deref requires an unsafe context
                // (matches the retired deref_field_write / `*p = x` semantics).
                if (k == LogosType::Kind::Ptr && !inside_unsafe_) {
                    error("write through raw pointer field requires unsafe context");
                    return false;
                }
            }
        }
        return true;
    }
    if (c == la::FIELD_READ || c == la::TUPLE_INDEX || c == la::INDEX_READ) {
        auto recv = map_of(place.get(la::RECEIVER.code));
        // A field/index access whose receiver is a POINTER is an implicit deref:
        // writability is governed by the POINTER (its `*mut`/`&mut`), not by the
        // root variable's `let mut` (`b.ptr[i] = x` doesn't need `mut b`). The
        // pointer's `*const`/unsafe rules are enforced at lowering.
        TypeRef rt = resolve_place_type(recv);
        if (rt && TypeRef(rt).kind() == LogosType::Kind::MutRef)
            return true;
        // Slice element write: slice mutability is not type-tracked (DIVERGENCE).
        if (rt && TypeRef(rt).kind() == LogosType::Kind::Slice)
            return true;
        if (rt && TypeRef(rt).kind() == LogosType::Kind::Ptr) {
            if (!TypeRef(rt).mut_ptr()) {
                error("assignment through a `*const` pointer");
                return false;
            }
            if (!inside_unsafe_) {
                error("write through raw pointer requires unsafe context");
                return false;
            }
            return true;
        }
        return check_place_writable(recv);
    }
    return true;
}

// Best-effort static type of an AST place expression (read-only; mirrors the
// type logic in lower_expr without emitting LIR). Used by check_place_writable
// to decide whether a field/index access crosses a pointer boundary.
TypeRef SemaChecker::resolve_place_type(hermes::TinyMapView place) {
    place = unwrap_paren_node(place);
    int32_t c = code_of(place);
    if (c == la::VAR_REF)
        return lookup(std::string(str_of(place.get(la::NAME.code))));
    if (c == la::DEREF) {
        TypeRef t = resolve_place_type(map_of(place.get(la::VALUE.code)));
        return (t && TypeRef(t).pointee()) ? TypeRef(t).pointee() : TypeRef(nullptr);
    }
    if (c == la::FIELD_READ) {
        TypeRef rt = resolve_place_type(map_of(place.get(la::RECEIVER.code)));
        if (rt && is_ref_like(TypeRef(rt).kind()) && TypeRef(rt).pointee()) rt = TypeRef(rt).pointee();
        if (!rt) return nullptr;
        return field_type_of_for_type(rt, std::string(str_of(place.get(la::FIELD.code))));
    }
    if (c == la::TUPLE_INDEX) {
        TypeRef rt = resolve_place_type(map_of(place.get(la::RECEIVER.code)));
        if (rt && is_ref_like(TypeRef(rt).kind()) && TypeRef(rt).pointee()) rt = TypeRef(rt).pointee();
        if (!rt) return nullptr;
        uint64_t idx = (uint64_t)parse_int_literal(str_of(place.get(la::FIELD.code)));
        if (TypeRef(rt).kind() == LogosType::Kind::Tuple) {
            auto elems = TypeRef(rt).tuple_elems();
            return idx < elems.size() ? elems[idx] : TypeRef(nullptr);
        }
        if (TypeRef(rt).kind() == LogosType::Kind::Struct)
            return field_type_of_for_type(rt, std::to_string(idx));
        return nullptr;
    }
    if (c == la::INDEX_READ) {
        TypeRef rt = resolve_place_type(map_of(place.get(la::RECEIVER.code)));
        if (!rt) return nullptr;
        // Indexing through a pointer/ref whose pointee is NOT an array → the
        // receiver IS the pointer (implicit deref-index).
        if (is_ref_like(TypeRef(rt).kind()) && TypeRef(rt).pointee() &&
            TypeRef(TypeRef(rt).pointee()).kind() != LogosType::Kind::Array)
            return rt;
        auto k = TypeRef(rt).kind();
        if (k == LogosType::Kind::Array || k == LogosType::Kind::Slice) return TypeRef(rt).elem();
        if (k == LogosType::Kind::Ptr || k == LogosType::Kind::MutRef) return TypeRef(rt).pointee();
        return nullptr;
    }
    return nullptr;
}

std::optional<lir::LStmt> SemaChecker::try_index_mut_assign(
    const std::string& arr_name, TypeRef arr_type,
    hermes::TinyMapView idx_node, hermes::TinyMapView val_node) {
    if (!arr_type || TypeRef(arr_type).kind() != LogosType::Kind::Struct)
        return std::nullopt;
    auto type_name = concrete_struct_name(arr_type);
    auto base_name = std::string(TypeRef(arr_type).struct_name());
    bool has_im = impls_.count("IndexMut::" + type_name) ||
                  (!base_name.empty() && impls_.count("IndexMut::" + base_name));
    if (!has_im) return std::nullopt;
    if (!lookup_is_mut(arr_name))
        error(std::format("index write to immutable struct '{}'", arr_name));
    auto mangled = type_name + "__index_mut";
    lir::LExprPtr idx_e = lower_expr(idx_node);
    lir::LExprPtr val_e = lower_expr(val_node);
    const SemaFuncInfo* fit = nullptr;
    for (auto* c : find_func_candidates(mangled))
        if (c->param_types.size() == 2) { fit = c; break; }
    if (fit) {
        widen_int_expr(idx_e, fit->param_types[1], builder());
        auto recv_ref = builder().addr_of(arr_name, make_ref(true, arr_type));
        std::vector<lir::LExprPtr> args;
        args.push_back(std::move(recv_ref));
        args.push_back(std::move(idx_e));
        auto call_e = builder().call(
            fit->symbol_name.empty() ? mangled : fit->symbol_name,
            {}, std::move(args), fit->ret_type);
        track_write_move(val_e);
        return builder().stmt_deref_write(std::move(call_e), std::move(val_e), node_line_);
    }
    const SemaImplInfo* ii = nullptr;
    if (auto it = impls_.find("IndexMut::" + type_name); it != impls_.end()) ii = &it->second;
    else if (auto it2 = impls_.find("IndexMut::" + base_name); it2 != impls_.end()) ii = &it2->second;
    if (ii && ii->trait_type_args.size() >= 2) {
        SemaSubst subst;
        if (ii->target_typeref) {
            auto pat = TypeRef(ii->target_typeref).type_args();
            auto cur = TypeRef(arr_type).type_args();
            for (size_t k = 0; k < pat.size() && k < cur.size(); ++k)
                if (pat[k] && TypeRef(pat[k]).kind() == LogosType::Kind::TypeVar)
                    subst[std::string(TypeRef(pat[k]).type_var_name())] = cur[k];
        }
        TypeRef idx_t = subst_type_sema(ii->trait_type_args[0], subst);
        TypeRef out_t = subst_type_sema(ii->trait_type_args[1], subst);
        if (idx_t && TypeRef(idx_t).kind() != LogosType::Kind::TypeVar)
            widen_int_expr(idx_e, idx_t, builder());
        lir::EMethodCall mc;
        mc.receiver = builder().addr_of(arr_name, make_ref(true, arr_type));
        mc.method = "index_mut";
        mc.args.push_back(std::move(idx_e));
        mc.vtable_index = -1;
        mc.resolved_type = "";
        auto call_e = builder().method_call_v(std::move(mc), make_ref(true, out_t));
        track_write_move(val_e);
        return builder().stmt_deref_write(std::move(call_e), std::move(val_e), node_line_);
    }
    return std::nullopt;
}

lir::LStmt SemaChecker::lower_place_assign(TinyMapView node) {
    auto place_node = map_of(node.get(la::RECEIVER.code));
    int32_t pc = code_of(place_node);
    // Only genuine lvalue shapes are assignable. A bare VarRef is handled by
    // assign_stmt; anything else (call result, literal, arithmetic, …) is not
    // a place.
    if (pc != la::INDEX_READ && pc != la::FIELD_READ &&
        pc != la::TUPLE_INDEX && pc != la::DEREF) {
        error("invalid assignment target: left side is not an assignable place");
        lir::SExprStmt es; es.expr = error_expr();
        return make_stmt_emit(node_line_, std::move(es));
    }
    // Operator-overload place write (Rust: `a[i] = v` is `*index_mut(&mut a,i)=v`
    // for IndexMut types). A trait method produces the place, not a plain address.
    if (pc == la::INDEX_READ) {
        auto recv = map_of(place_node.get(la::RECEIVER.code));
        if (code_of(recv) == la::VAR_REF) {
            auto an = std::string(str_of(recv.get(la::NAME.code)));
            if (auto s = try_index_mut_assign(an, lookup(an),
                    map_of(place_node.get(la::VALUE.code)),
                    map_of(node.get(la::VALUE.code))))
                return std::move(*s);
        }
    }
    // Bound to the place shapes the address-of machinery lowers correctly;
    // deeper nestings hit a pre-existing read-side limitation — reject cleanly
    // (with a workaround) rather than miscompile/crash.
    if (!place_write_supported(place_node)) {
        error("assignment target too deeply nested to assign in place yet; "
              "bind an intermediate (e.g. `let r = &mut <inner>; r[i] = …`)");
        lir::SExprStmt es; es.expr = error_expr();
        return make_stmt_emit(node_line_, std::move(es));
    }
    // Uniform place-subsystem writability check (closes the soundness gap where
    // a deep-nested write through an immutable place / `*const` was accepted).
    check_place_writable(place_node);
    auto place = lower_expr(place_node);
    TypeRef pt = place->type;
    // Indexing through a raw-pointer place (e.g. a `*mut T` field `s.buf[i]`)
    // is an implicit deref: `*const` cannot be written and a `*mut` write
    // requires unsafe (matches the retired field_index_write diagnostics).
    {
        auto pr = expr_ref_of(*place);
        if (pr.kind() == lir_schema::expr::Code::IndexRead) {
            lir_view::EIndexReadView irv{pr};
            TypeRef rtp = irv.receiver().type(cur_prog_->type_pool.impl());
            if (rtp && TypeRef(rtp).kind() == LogosType::Kind::Ptr) {
                if (!TypeRef(rtp).mut_ptr())
                    error(std::format("assignment to '{}': index through a `*const` pointer",
                          render_place_node(place_node)));
                else if (!inside_unsafe_)
                    error(std::format("field index write '{}[i]' through raw pointer requires unsafe context",
                          render_place_node(map_of(place_node.get(la::RECEIVER.code)))));
            }
        }
    }
    // Propagate the place's type as an enum/struct RHS hint so a bare
    // `None` / struct-literal resolves to the slot's concrete type (mirrors
    // DEREF_WRITE).
    auto saved_enum_hint   = hint_enum_type_;
    auto saved_struct_hint = hint_struct_type_;
    if (pt && TypeRef(pt).kind() == LogosType::Kind::Enum &&
        !TypeRef(pt).type_args().empty())
        hint_enum_type_ = pt;
    else if (pt && (TypeRef(pt).kind() == LogosType::Kind::Struct ||
                    TypeRef(pt).kind() == LogosType::Kind::ZonedStruct) &&
             !TypeRef(pt).type_args().empty())
        hint_struct_type_ = pt;
    lir::LExprPtr val = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
    hint_enum_type_   = saved_enum_hint;
    hint_struct_type_ = saved_struct_hint;

    if (pt && TypeRef(pt).kind() != LogosType::Kind::Error &&
        val && TypeRef(val->type).kind() != LogosType::Kind::Error &&
        !types_compatible(val->type, pt))
        error(std::format("assignment to '{}': type mismatch — expected {}, got {}",
              render_place_node(place_node), type_str(pt), type_str(val->type)));
    // Overflow: an int literal RHS must fit the place's integer type (closes the
    // gap where the general place-write path skipped the fit-check).
    if (pt && TypeRef(pt).kind() != LogosType::Kind::Error &&
        val && TypeRef(val->type).kind() == LogosType::Kind::IntLit)
        if (auto v = get_intlit_value(val))
            if (!intlit_fits(*v, TypeRef(pt).kind()))
                error(std::format("assignment to '{}': value {} does not fit in {}",
                      render_place_node(place_node), *v, type_str(pt)));
    // Array-literal RHS: each int-literal element must fit the place's narrow
    // array element type (generalizes the retired deref_field_write check).
    if (pt && TypeRef(pt).kind() == LogosType::Kind::Array && TypeRef(pt).elem() &&
        val && TypeRef(val->type).kind() == LogosType::Kind::Array) {
        auto vr = expr_ref_of(*val);
        if (vr.kind() == lir_schema::expr::Code::ArrLit) {
            lir_view::EArrLitView al{vr};
            for (uint64_t i = 0; i < al.count(); ++i) {
                auto el = al.elem(i);
                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(el))
                        if (!intlit_fits(*v, TypeRef(pt).elem().kind()))
                            error(std::format("assignment to '{}': array element {}: value {} does not fit in {}",
                                  render_place_node(place_node), i, *v, type_str(TypeRef(pt).elem())));
            }
        }
    }
    // Tuple-literal RHS: each int-literal element must fit the corresponding
    // narrow tuple element type (generalizes the retired deref_field_write check).
    if (pt && TypeRef(pt).kind() == LogosType::Kind::Tuple &&
        val && TypeRef(val->type).kind() == LogosType::Kind::Tuple) {
        auto vr = expr_ref_of(*val);
        if (vr.kind() == lir_schema::expr::Code::TupleLit) {
            lir_view::ETupleLitView tl{vr};
            auto elems = TypeRef(pt).tuple_elems();
            for (uint64_t i = 0; i < tl.count() && i < elems.size(); ++i) {
                auto el = tl.elem(i);
                TypeRef et = elems[i];
                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                    if (auto v = get_intlit_value(el))
                        if (!intlit_fits(*v, TypeRef(et).kind()))
                            error(std::format("assignment to '{}': tuple element {}: value {} does not fit in {}",
                                  render_place_node(place_node), i, *v, type_str(et)));
            }
        }
    }
    widen_int_expr(val, pt, builder());

    // Address of the place: `&mut <place>`. EAddrOfTemp recognises the place
    // read-expr kind and returns the real element GEP (not a temp copy).
    auto addr = builder().addr_of_temp(std::move(place), /*is_mut=*/true,
                                       make_ref(true, pt ? pt : error_t()));
    track_write_move(val);
    return builder().stmt_deref_write(std::move(addr), std::move(val), node_line_);
}

void SemaChecker::check_match_exhaustiveness(const lir::SMatch& smatch, TypeRef scrut_type,
                                             bool ast_proven_exhaustive) {
    // K4: a desugared nested-enum match is exhaustive at the AST level but its
    // arms carry synth guards (skipped below), so suppress the variant check.
    if (ast_proven_exhaustive) return;
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

void SemaChecker::mark_match_scrutinee_moved(const lir::LExprPtr& scrut,
                                              TypeRef scrut_type,
                                              hermes::TinyMapView node) {
    if (scrut && scrut_type && is_move_type(scrut_type) &&
        expr_ref_of(*scrut).kind() == lir_schema::expr::Code::VarRef &&
        node.has_key(la::ITEMS)) {
        std::string scrut_var{lir_view::EVarRefView{expr_ref_of(*scrut)}.name()};
        if (!scrut_var.empty()) {
            auto arms_mv = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < arms_mv.size(); ++i) {
                auto arm = map_of(arms_mv.get(i));
                if (code_of(arm) != la::MATCH_ARM) continue;
                if (arm.has_key(la::GUARD)) continue;
                if (!arm.has_key(la::LHS)) continue;
                auto lhs = map_of(arm.get(la::LHS.code));
                // A bare-identifier arm parses as a single-alt PAT_OR wrapping
                // the binding (mirrors is_catchall_pat) — unwrap it.
                if (code_of(lhs) == la::PAT_OR && lhs.has_key(la::ITEMS)) {
                    auto alts = arr_of(lhs.get(la::ITEMS.code));
                    if (alts.size() == 1) lhs = map_of(alts.get(0));
                }
                // A whole-value binding is a PAT_WILD carrying a real NAME
                // (anonymous `_` has no NAME key).
                if (code_of(lhs) == la::PAT_WILD && lhs.has_key(la::NAME)) {
                    auto nm = str_of(lhs.get(la::NAME.code));
                    if (!nm.empty() && nm != "_") { mark_moved(scrut_var); break; }
                }
                // An unguarded STRUCT-destructure arm (`match p { Pair{a,b} =>
                // … }`) over a by-value scrutinee moves the bound fields into
                // the bindings, consuming `p` — mark it moved so its scope-exit
                // Drop doesn't double-free a field a binding already owns
                // (mirrors the let-destructure fix). BUT only when the arm
                // actually moves out a MOVE-ONLY field by value: an empty struct,
                // an all-Copy-field struct, or a fully `ref`-bound destructure
                // moves nothing and leaves `p` live (it may be matched again).
                // (Reached only for a by-value struct scrutinee — is_move_type
                // above is false for a `&Struct` ref scrutinee.)
                if (code_of(lhs) == la::PAT_STRUCT) {
                    auto psname = std::string(str_of(lhs.get(la::NAME.code)));
                    auto si_ = find_struct_by_name(psname).second;
                    if (!si_) si_ = find_datatype_by_name(psname).second;
                    bool moves = false;
                    if (si_ && lhs.has_key(la::ITEMS)) {
                        auto fl = map_of(lhs.get(la::ITEMS.code));
                        if (fl.has_key(la::ITEMS)) {
                            auto fitems = arr_of(fl.get(la::ITEMS.code));
                            for (uint64_t fi = 0; fi < fitems.size() && !moves; ++fi) {
                                auto fn = map_of(fitems.get(fi));
                                if (code_of(fn) != la::PAT_FIELD) continue;
                                // A `ref`-bound field doesn't move.
                                if (fn.has_key(la::VALUE)) {
                                    auto sub = map_of(fn.get(la::VALUE.code));
                                    if (code_of(sub) == la::PAT_WILD &&
                                        sub.has_key(la::IS_REF) &&
                                        sub.get(la::IS_REF.code).is_value() &&
                                        sub.get(la::IS_REF.code).as_value<uint8_t>() != 0)
                                        continue;
                                }
                                auto fnm = std::string(str_of(fn.get(la::NAME.code)));
                                for (auto& f : si_->fields)
                                    if (f.name == fnm && is_move_type(f.type)) {
                                        moves = true; break;
                                    }
                            }
                        }
                    }
                    if (moves) { mark_moved(scrut_var); break; }
                }
                // An unguarded TUPLE-destructure arm (`match p { (a, b) => … }`)
                // over a by-value tuple scrutinee moves move-only elements into
                // the bindings — mark `p` moved so its scope-exit Drop (tuple
                // branch) doesn't double-free an element a binding already owns
                // (G154-4 / G156-2). Only when an element is bound BY VALUE
                // (not `_`, not `ref`) and is a move type; an all-Copy / fully
                // `ref`-bound / all-`_` destructure moves nothing.
                if (code_of(lhs) == la::PAT_TUPLE && lhs.has_key(la::ITEMS) &&
                    scrut_type && TypeRef(scrut_type).kind() == LogosType::Kind::Tuple) {
                    auto subs = arr_of(lhs.get(la::ITEMS.code));
                    auto telems = TypeRef(scrut_type).tuple_elems();
                    bool moves = false;
                    for (uint64_t si = 0; si < subs.size() && si < telems.size() && !moves; ++si) {
                        auto sub = map_of(subs.get(si));
                        // Each tuple-pattern element parses as a single-alt
                        // PAT_OR wrapping the binding — unwrap it.
                        if (code_of(sub) == la::PAT_OR && sub.has_key(la::ITEMS)) {
                            auto alts = arr_of(sub.get(la::ITEMS.code));
                            if (alts.size() == 1) sub = map_of(alts.get(0));
                        }
                        if (code_of(sub) != la::PAT_WILD) continue;  // binding slot
                        if (!sub.has_key(la::NAME)) continue;        // anonymous `_`
                        auto nm = str_of(sub.get(la::NAME.code));
                        if (nm.empty() || nm == "_") continue;
                        if (sub.has_key(la::IS_REF) && sub.get(la::IS_REF.code).is_value() &&
                            sub.get(la::IS_REF.code).as_value<uint8_t>() != 0)
                            continue;                                // `ref` binding
                        if (telems[si] && is_move_type(telems[si])) moves = true;
                    }
                    if (moves) { mark_moved(scrut_var); break; }
                }
                // An unguarded VARIANT-DATA arm (`match r { Ok(s) => … }`) over a
                // by-value enum scrutinee moves the bound payload into the
                // binding — mark `r` moved so the enum's scope-exit Drop (the
                // SDrop Enum branch) doesn't double-free a payload a binding (or
                // a value returned from the arm) already owns. Reached only for a
                // droppable / move-type enum scrutinee (is_move_type gate above).
                // A `_` or `ref` binding moves nothing. (G156-2 enum-half.)
                if (code_of(lhs) == la::PAT_VARIANT_DATA) {
                    bool moves = false;
                    auto scan_subs = [&](hermes::AnyVal items_av) {
                        if (items_av.is_null()) return;
                        auto subs = arr_of(items_av);
                        for (uint64_t si = 0; si < subs.size() && !moves; ++si) {
                            auto sub = map_of(subs.get(si));
                            if (code_of(sub) == la::PAT_OR && sub.has_key(la::ITEMS)) {
                                auto alts = arr_of(sub.get(la::ITEMS.code));
                                if (alts.size() == 1) sub = map_of(alts.get(0));
                            }
                            // struct-shape field binding (PAT_FIELD): a shorthand
                            // `{ name }` (no VALUE) IS a by-value binding of the
                            // field; `{ field: pat }` carries the sub in VALUE.
                            if (code_of(sub) == la::PAT_FIELD) {
                                if (!sub.has_key(la::VALUE)) { moves = true; continue; }
                                sub = map_of(sub.get(la::VALUE.code));
                            }
                            if (code_of(sub) != la::PAT_WILD || !sub.has_key(la::NAME)) continue;
                            auto nm = str_of(sub.get(la::NAME.code));
                            if (nm.empty() || nm == "_") continue;
                            if (sub.has_key(la::IS_REF) && sub.get(la::IS_REF.code).is_value() &&
                                sub.get(la::IS_REF.code).as_value<uint8_t>() != 0)
                                continue;  // `ref` binding moves nothing
                            moves = true;
                        }
                    };
                    // tuple-shape `V(a, b)`: sub-patterns under ARGS→ITEMS;
                    // struct-shape `V { x, y }`: PAT_FIELD list directly under ITEMS.
                    if (lhs.has_key(la::ARGS)) {
                        auto args = map_of(lhs.get(la::ARGS.code));
                        if (args.has_key(la::ITEMS)) scan_subs(args.get(la::ITEMS.code));
                    }
                    if (lhs.has_key(la::ITEMS)) {
                        // struct-shape ITEMS is a pat_field_list NODE wrapping its
                        // own ITEMS array (same shape as PAT_STRUCT) — unwrap it,
                        // else arr_of on the node SIGSEGVs (issue-19340-2).
                        auto fl = map_of(lhs.get(la::ITEMS.code));
                        if (fl.has_key(la::ITEMS)) scan_subs(fl.get(la::ITEMS.code));
                    }
                    if (moves) { mark_moved(scrut_var); break; }
                }
            }
        }
    }
}

void SemaChecker::emit_nested_variant_lets(
        const std::string& synth_name, TypeRef synth_t,
        hermes::TinyMapView sub_pat, std::vector<lir::LStmt>& out) {
    namespace ps = lir_schema::pat;
    // Build `let <sub_pat> = synth else { loop {} }`. Capture any DEEPER
    // refutable-inner guards / nested subs locally — the guards are dead
    // (owning arm already gated), the subs are re-extracted recursively below.
    std::vector<lir::LExprPtr> le_guards;
    std::vector<NestedPatSub> deeper;
    auto* sg = current_pat_refutable_guards_;
    auto* ssub = current_pat_nested_subs_;
    current_pat_refutable_guards_ = &le_guards;
    current_pat_nested_subs_ = &deeper;
    lir::Pattern lpat = build_pattern(sub_pat, synth_t);
    current_pat_refutable_guards_ = sg;
    current_pat_nested_subs_ = ssub;
    // Define this pattern's bindings in the current (arm body) scope.
    auto* pool = cur_prog_->type_pool.impl();
    std::function<void(lir_view::PatRef)> define_binds = [&](lir_view::PatRef pr) {
        if (!pr) return;
        auto k = pr.kind();
        if (k == ps::Code::VariantData) {
            lir_view::PatVariantDataView v{pr};
            std::vector<std::string_view> names; std::vector<TypeRef> types;
            v.each_binding([&](std::string_view n){ names.push_back(n); });
            v.each_binding_type(pool, [&](TypeRef t){ types.push_back(t); });
            for (size_t i = 0; i < names.size() && i < types.size(); ++i)
                if (names[i] != "_") define(std::string(names[i]), types[i]);
        } else if (k == ps::Code::Tuple) {
            lir_view::PatTupleView v{pr};
            std::vector<std::string_view> names; std::vector<TypeRef> types;
            v.each_binding([&](std::string_view n){ names.push_back(n); });
            v.each_binding_type(pool, [&](TypeRef t){ types.push_back(t); });
            for (size_t i = 0; i < names.size() && i < types.size(); ++i)
                if (names[i] != "_") define(std::string(names[i]), types[i]);
        } else if (k == ps::Code::Wild) {
            auto n = lir_view::PatWildView{pr}.name();
            if (n != "_") define(std::string(n), synth_t);
        }
    };
    define_binds(pat_ref_of(lpat));
    // Emit the let-else.
    lir::SLetElse sle;
    sle.pat   = std::move(lpat);
    sle.scrut = builder().var_ref(synth_name, synth_t);
    lir::LBlock eblk;
    lir::SLoop lp; lp.body = lir::alloc_block(*cur_prog_);
    eblk.stmts.push_back(make_stmt_emit(node_line_, std::move(lp)));
    sle.else_block = lir::alloc_block(*cur_prog_, std::move(eblk));
    // No guards: the owning arm's guard already proved the FULL nested match,
    // so this let-else is a pure extraction (its own variant-tag check + the
    // dead else suffice). Re-checking via `le_guards` would also spuriously
    // re-bind inner names. Deeper bindings are extracted by the recursion below.
    (void)le_guards;
    out.push_back(make_stmt_emit(node_line_, std::move(sle)));
    // Deeper nesting (`Some(Some(Some(w)))`): the inner let-else reads a
    // binding bound by THIS one, so it must come after.
    for (auto& d : deeper) {
        if (code_of(d.sub_pat_node) != la::PAT_VARIANT_DATA) continue;
        TypeRef dt = lookup(d.synth_name);
        if (!dt) continue;
        emit_nested_variant_lets(d.synth_name, dt, d.sub_pat_node, out);
    }
}

bool SemaChecker::ast_patterns_exhaustive(
        std::vector<hermes::TinyMapView> pats, TypeRef ty) {
    using K = LogosType::Kind;
    // Peel references.
    TypeRef t = ty;
    for (int i = 0; i < 8 && t &&
         (TypeRef(t).kind() == K::Ref || TypeRef(t).kind() == K::MutRef ||
          TypeRef(t).kind() == K::Ptr) && TypeRef(t).pointee(); ++i)
        t = TypeRef(t).pointee();
    if (!t) return false;
    // Flatten or-patterns; unwrap @-bindings to their sub-pattern.
    std::vector<TinyMapView> flat;
    std::function<void(TinyMapView)> add = [&](TinyMapView p) {
        int32_t c = code_of(p);
        if (c == la::PAT_OR && p.has_key(la::ITEMS)) {
            auto alts = arr_of(p.get(la::ITEMS.code));
            for (uint64_t i = 0; i < alts.size(); ++i) add(map_of(alts.get(i)));
            return;
        }
        if (c == la::PAT_AT && p.has_key(la::VALUE)) {
            add(map_of(p.get(la::VALUE.code)));
            return;
        }
        flat.push_back(p);
    };
    for (auto p : pats) add(p);
    // A bare wildcard / name binding covers everything.
    for (auto p : flat)
        if (code_of(p) == la::PAT_WILD) return true;
    // Resolve a pattern node's (enum, variant) name, applying prelude shorthand.
    auto pat_variant = [&](TinyMapView p, std::string& en, std::string& vn) -> bool {
        int32_t c = code_of(p);
        if (c != la::PAT_VARIANT && c != la::PAT_VARIANT_DATA) return false;
        en = std::string(str_of(p.get(la::NAME.code)));
        vn = std::string(str_of(p.get(la::FIELD.code)));
        if (vn.empty()) {
            auto remap = [&](const char* e) -> bool {
                auto [pkg, esi] = find_enum_by_name(e);
                if (!esi) return false;
                for (auto& v : esi->variants)
                    if (v.name == en) { vn = en; en = e; return true; }
                return false;
            };
            if (en == "Some" || en == "None") remap("Option");
            else if (en == "Ok" || en == "Err") remap("Result");
            if (vn.empty()) {
                auto vit = cur_imports_.variant_aliases.find(en);
                if (vit != cur_imports_.variant_aliases.end()) remap(vit->second.c_str());
            }
        }
        return !vn.empty();
    };
    auto payload_items = [&](TinyMapView p) -> std::vector<TinyMapView> {
        std::vector<TinyMapView> out;
        if (!p.has_key(la::ARGS)) return out;
        auto av = p.get(la::ARGS.code);
        if (av.is_null()) return out;
        ArrayView items;
        if (av.is_pointer()) {
            auto m = map_of(av);
            if (m.has_key(la::ITEMS)) items = arr_of(m.get(la::ITEMS.code));
            else                       items = arr_of(av);
        } else items = arr_of(av);
        for (uint64_t i = 0; i < items.size(); ++i) out.push_back(map_of(items.get(i)));
        return out;
    };
    if (TypeRef(t).kind() == K::Enum) {
        auto [pkg, esi] = find_enum_by_name(TypeRef(t).enum_name());
        (void)pkg;
        if (!esi) return false;
        SemaSubst subst;
        auto ta = TypeRef(t).type_args();
        for (size_t i = 0; i < esi->type_params.size() && i < ta.size(); ++i)
            if (ta[i]) subst[esi->type_params[i].name] = ta[i];
        for (auto& V : esi->variants) {
            bool covered = false;
            std::vector<TinyMapView> inner;
            for (auto p : flat) {
                std::string en, vn;
                if (!pat_variant(p, en, vn) || vn != V.name) continue;
                if (code_of(p) == la::PAT_VARIANT) { covered = true; break; }
                auto args = payload_items(p);
                if (args.empty()) { covered = true; break; }
                bool all_irref = true;
                for (auto a : args)
                    if (code_of(a) != la::PAT_WILD) { all_irref = false; break; }
                if (all_irref) { covered = true; break; }
                if (V.payload_types.size() == 1 && args.size() == 1)
                    inner.push_back(args[0]);
            }
            if (covered) continue;
            if (!inner.empty() && !V.payload_types.empty()) {
                TypeRef pld = V.payload_types[0];
                if (pld && !subst.empty()) pld = subst_type_sema(pld, subst);
                if (pld && ast_patterns_exhaustive(inner, pld)) continue;
            }
            return false;
        }
        return true;
    }
    // Non-enum (bool/int/…): defer to the LIR-level checker (return false =
    // "not proven here", which suppresses nothing).
    return false;
}

// Emit the body-prologue `let` destructures for nested sub-patterns inside an
// enum-variant payload (`Some((a, b))`, `Some(Inner { f })`, `Some(Some(_))`),
// collected by build_pattern into `nested_subs`. Shared by match arms and the
// if-let / while-let lowerings so all three handle nested payload patterns
// identically. `for_guard` suppresses the refutable nested-variant let-else
// (which assumes the arm already matched) when building a guard prologue.
void SemaChecker::emit_nested_pat_destructure(
        const std::vector<NestedPatSub>& nested_subs,
        std::vector<lir::LStmt>& nested_destructure_stmts, bool for_guard) {
    for (auto& nsub : nested_subs) {
        TypeRef synth_t = lookup(nsub.synth_name);
        if (!synth_t) continue;
        if (code_of(nsub.sub_pat_node) == la::PAT_VARIANT_DATA) {
            // A nested-variant payload destructure uses a refutable
            // `let … else { loop {} }` that ASSUMES the arm already
            // matched (its own synth guard ran). It must NOT be hoisted
            // into the guard (for_guard) — running it before the synth
            // guard confirms the variant would hit `loop {}` on a
            // non-matching scrutinee (infinite loop).
            if (!for_guard)
                emit_nested_variant_lets(nsub.synth_name, synth_t,
                                         nsub.sub_pat_node, nested_destructure_stmts);
            continue;
        }
        // B170: nested TUPLE sub-pattern in a variant payload
        // (`Some((a, b))`, `Some((a, _))`, `Ok((a, (b, c)))`). The
        // synth holds the payload tuple; emit `let <name> = __synth.<i>`
        // element reads (recursing into nested tuples). Previously only
        // PAT_STRUCT / PAT_VARIANT_DATA nested subs were destructured,
        // so a tuple-payload binding was left undefined.
        if (code_of(nsub.sub_pat_node) == la::PAT_TUPLE) {
            std::function<void(lir::LExprPtr, TypeRef, hermes::TinyMapView)>
            emit_tuple_lets =
                [&](lir::LExprPtr src, TypeRef tty, hermes::TinyMapView tnode) {
                if (!tty || TypeRef(tty).kind() != LogosType::Kind::Tuple) return;
                if (!tnode.has_key(la::ITEMS)) return;
                auto items = arr_of(tnode.get(la::ITEMS.code));
                auto elems = TypeRef(tty).tuple_elems();
                // Spill the source to a temp so each element read
                // references it once.
                std::string stmp = std::format("__pat_tup_{}", tmp_var_count_++);
                define(stmp, tty);
                {
                    lir::SLet s; s.name = stmp; s.type = tty;
                    s.is_mut = false; s.value = std::move(src);
                    nested_destructure_stmts.push_back(
                        make_stmt_emit(node_line_, std::move(s)));
                }
                for (uint64_t i = 0; i < items.size() && i < elems.size(); ++i) {
                    auto en = map_of(items.get(i));
                    auto et = elems[i];
                    auto elem_expr = builder().tuple_index(
                        builder().var_ref(stmp, tty), (uint32_t)i, et);
                    // Tuple elements are wrapped in a (usually single-alt)
                    // PAT_OR by the grammar (`pat_single (PIPE pat_single)*`).
                    // Unwrap a single alternative to reach the bare binding.
                    if (code_of(en) == la::PAT_OR && en.has_key(la::ITEMS)) {
                        auto alts = arr_of(en.get(la::ITEMS.code));
                        if (alts.size() == 1) en = map_of(alts.get(0));
                    }
                    int32_t ec = code_of(en);
                    if (ec == la::PAT_TUPLE) {
                        emit_tuple_lets(std::move(elem_expr), et, en);
                    } else if (ec == la::PAT_WILD && en.has_key(la::NAME)) {
                        std::string nm(str_of(en.get(la::NAME.code)));
                        if (nm == "_") continue;
                        define(nm, et);
                        lir::SLet el; el.name = nm; el.type = et;
                        el.is_mut = false; el.value = std::move(elem_expr);
                        nested_destructure_stmts.push_back(
                            make_stmt_emit(node_line_, std::move(el)));
                    }
                    // Other element kinds (struct/refutable) inside a
                    // payload tuple are handled by build_pattern's own
                    // synth/guard channels, not here.
                }
            };
            emit_tuple_lets(builder().var_ref(nsub.synth_name, synth_t),
                            synth_t, nsub.sub_pat_node);
            continue;
        }
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
}

bool SemaChecker::emit_for_pattern_destructure(
        hermes::TinyMapView pat, const std::string& src_var, TypeRef src_type,
        std::vector<lir::LStmt>& out) {
    // Unwrap the grammar's single-alt PAT_OR wrapper.
    if (code_of(pat) == la::PAT_OR && pat.has_key(la::ITEMS)) {
        auto alts = arr_of(pat.get(la::ITEMS.code));
        if (alts.size() == 1) pat = map_of(alts.get(0));
    }
    // For a by-ref element (`for (a,b) in &v`), deref to a value temp and
    // destructure from it (default-binding-mode by-ref is a follow-up).
    TypeRef vt = src_type;
    std::string base_var = src_var;
    if (vt && (TypeRef(vt).kind() == LogosType::Kind::Ref ||
               TypeRef(vt).kind() == LogosType::Kind::MutRef) &&
        TypeRef(vt).pointee()) {
        TypeRef pe = TypeRef(vt).pointee();
        std::string tmp = std::format("__fe_deref_{}", tmp_var_count_++);
        define(tmp, pe);
        lir::SLet s; s.name = tmp; s.type = pe; s.is_mut = false;
        s.value = builder().deref(builder().var_ref(src_var, vt), pe);
        out.push_back(make_stmt_emit(node_line_, std::move(s)));
        base_var = tmp; vt = pe;
    }
    if (code_of(pat) != la::PAT_TUPLE || !vt ||
        TypeRef(vt).kind() != LogosType::Kind::Tuple) {
        error("for-loop pattern: only tuple patterns `for (a, b) in …` are "
              "supported here; bind a name and destructure in the body");
        return false;
    }
    if (!pat.has_key(la::ITEMS)) return true;
    auto items = arr_of(pat.get(la::ITEMS.code));
    auto elems = TypeRef(vt).tuple_elems();
    for (uint64_t i = 0; i < items.size() && i < elems.size(); ++i) {
        auto en = map_of(items.get(i));
        TypeRef et = elems[i];
        if (code_of(en) == la::PAT_OR && en.has_key(la::ITEMS)) {
            auto alts = arr_of(en.get(la::ITEMS.code));
            if (alts.size() == 1) en = map_of(alts.get(0));
        }
        auto elem_expr = builder().tuple_index(
            builder().var_ref(base_var, vt), (uint32_t)i, et);
        int32_t ec = code_of(en);
        if (ec == la::PAT_TUPLE) {
            // Nested tuple: spill this element to a temp + recurse.
            std::string tmp = std::format("__fe_tup_{}", tmp_var_count_++);
            define(tmp, et);
            lir::SLet s; s.name = tmp; s.type = et; s.is_mut = false;
            s.value = std::move(elem_expr);
            out.push_back(make_stmt_emit(node_line_, std::move(s)));
            if (!emit_for_pattern_destructure(en, tmp, et, out)) return false;
        } else if (ec == la::PAT_WILD && en.has_key(la::NAME)) {
            std::string nm(str_of(en.get(la::NAME.code)));
            if (nm == "_") continue;  // discard
            define(nm, et);
            lir::SLet s; s.name = nm; s.type = et; s.is_mut = false;
            s.value = std::move(elem_expr);
            out.push_back(make_stmt_emit(node_line_, std::move(s)));
        } else {
            error("for-loop tuple pattern: element must be a name or nested "
                  "tuple; richer sub-patterns are a follow-up");
            return false;
        }
    }
    return true;
}

lir::LStmt SemaChecker::lower_match(TinyMapView node) {
    lir::LExprPtr scrut = nullptr;
    TypeRef scrut_type = error_t();
    if (node.has_key(la::VALUE)) {
        scrut = lower_expr(map_of(node.get(la::VALUE.code)));
        scrut_type = scrut->type;
    } else { scrut = error_expr(); }

    // A whole-value binding arm (`x => …` — an UNGUARDED `PAT_WILD` that
    // carries a real name, not `_`) moves an owned move-type scrutinee into
    // the binding: Rust's by-value match move, `match v { x => … }` ≡
    // `{ let x = v; … }`. Mark the scrutinee var moved so it is not dropped a
    // SECOND time after the match — the binding's own drop already fires at arm
    // end (match-arm bindings drop, a6a04330). Without this an owned Vec/String
    // scrutinee is double-freed (SIGSEGV). Restricted to an unguarded binding
    // arm (which always matches → unconditional move); guarded binding arms
    // leave the scrutinee conditionally live for later arms.
    mark_match_scrutinee_moved(scrut, scrut_type, node);

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

    // G172-1: top-level string-literal patterns (`match s { "foo" => … }`).
    // Detect any arm whose top-level LHS is a `PAT_STR` (directly or as a
    // PAT_OR alternative — `"a" | "b"` fans out per alt). Hoist the scrutinee
    // into a temp so each such arm becomes a wildcard guarded by
    // `str_eq(__smatch, "foo")` (str `==` content-compares via the stdlib).
    bool has_str_pat = false;
    if (!has_hermes_pat && node.has_key(la::ITEMS)) {
        auto arms_l = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < arms_l.size() && !has_str_pat; ++i) {
            auto arm = map_of(arms_l.get(i));
            if (code_of(arm) != la::MATCH_ARM || !arm.has_key(la::LHS)) continue;
            auto lhs = map_of(arm.get(la::LHS.code));
            if (code_of(lhs) == la::PAT_STR) { has_str_pat = true; break; }
            if (code_of(lhs) == la::PAT_OR && lhs.has_key(la::ITEMS)) {
                auto alts = arr_of(lhs.get(la::ITEMS.code));
                for (uint64_t k = 0; k < alts.size(); ++k)
                    if (code_of(map_of(alts.get(k))) == la::PAT_STR) { has_str_pat = true; break; }
            }
        }
    }
    std::string str_scrut_var;
    TypeRef str_scrut_type;
    lir::LStmt str_hoist_let;
    bool has_str_hoist = false;
    if (has_str_pat) {
        str_scrut_var = "__smatch_" + std::to_string(tmp_var_count_++);
        str_scrut_type = scrut_type;
        define(str_scrut_var, scrut_type);
        lir::SLet sl;
        sl.name = str_scrut_var; sl.type = scrut_type; sl.is_mut = false;
        sl.value = std::move(scrut);
        str_hoist_let = make_stmt_emit(node_line_, std::move(sl));
        scrut = builder().var_ref(str_scrut_var, scrut_type);
        has_str_hoist = true;
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
        struct EffArm { hermes::TinyMapView arm; int32_t alt_idx; int32_t payload_alt = -1; };
        // An or-pattern alternative is "merge-safe" only if it is a pure
        // scalar literal that binds nothing (PAT_INT / PAT_BOOL / PAT_CHAR).
        // The merged PatOr codegen treats each alt as a scalar discriminant
        // and extracts payload from alt[0] only — so anything that binds a
        // variable (tuple `(1,a)|(2,a)`, struct, variant payload, named
        // wildcard) or has a non-scalar/refutable shape (range, slice) must
        // be fanned out into one arm per alternative so each alt goes through
        // the normal single-arm path with its own payload extraction and
        // refutable-inner guard.
        auto alt_is_merge_safe = [](int32_t c) -> bool {
            return c == la::PAT_INT || c == la::PAT_BOOL || c == la::PAT_CHAR;
        };
        auto or_needs_fanout = [&](hermes::TinyMapView lhs) -> bool {
            if (code_of(lhs) != la::PAT_OR) return false;
            if (!lhs.has_key(la::ITEMS)) return false;
            auto a = arr_of(lhs.get(la::ITEMS.code));
            if (a.size() < 2) return false;
            for (uint64_t k = 0; k < a.size(); ++k)
                if (!alt_is_merge_safe(code_of(map_of(a.get(k))))) return true;
            return false;
        };
        // B170-E: a variant whose SINGLE payload arg is a multi-alt PAT_OR
        // (`Some((a,_) | (_,a))`) fans out one arm per alternative — i.e.
        // or-distribution `Some(P|Q)` → `Some(P) | Some(Q)`. Each fanned arm
        // re-evaluates the guard with its own bindings (Rust backtracks alts
        // under a failing guard). Returns the alt count (≥2) or 0. Restricted to
        // a single payload arg (the realistic class); a multi-arg variant with
        // ors in several positions would need a cartesian product — out of
        // scope, left to the merged path / a clean reject downstream.
        auto variant_payload_or_alts = [&](hermes::TinyMapView lhs) -> int {
            // The grammar wraps a whole arm pattern in a single-alt PAT_OR
            // (`pat_single (PIPE …)*`); unwrap it to reach the variant.
            if (code_of(lhs) == la::PAT_OR && lhs.has_key(la::ITEMS)) {
                auto a = arr_of(lhs.get(la::ITEMS.code));
                if (a.size() == 1) lhs = map_of(a.get(0));
            }
            if (code_of(lhs) != la::PAT_VARIANT_DATA) return 0;
            if (!lhs.has_key(la::ARGS)) return 0;
            AnyVal aav = lhs.get(la::ARGS.code);
            if (aav.is_null() || !aav.is_pointer()) return 0;
            auto blist = map_of(aav);
            if (!blist.has_key(la::ITEMS)) return 0;
            auto items = arr_of(blist.get(la::ITEMS.code));
            if (items.size() != 1) return 0;
            auto arg = map_of(items.get(0));
            if (code_of(arg) != la::PAT_OR || !arg.has_key(la::ITEMS)) return 0;
            auto alts = arr_of(arg.get(la::ITEMS.code));
            // Only fan out when an alternative binds / is non-scalar — a pure
            // scalar or (`Some(1|2)`) is handled by the refutable-inner guard.
            bool needs = false;
            for (uint64_t k = 0; k < alts.size(); ++k)
                if (!alt_is_merge_safe(code_of(map_of(alts.get(k))))) { needs = true; break; }
            return (alts.size() >= 2 && needs) ? (int)alts.size() : 0;
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
                if (int n = variant_payload_or_alts(lhs); n > 0) {
                    for (int k = 0; k < n; ++k)
                        eff_arms.push_back({arm, -1, k});
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
            // B170-E: select this fanned arm's payload-or alternative.
            int32_t saved_payload_or_alt = payload_or_alt_;
            payload_or_alt_ = eff_arms[i].payload_alt;
            // G172-1: a top-level string-literal arm lowers to a wildcard +
            // `str_eq(__smatch, "lit")` guard (no PatStr LIR / codegen needed).
            lir::LExprPtr str_arm_guard = nullptr;
            lir::Pattern pat;
            // Unwrap a single-alt PAT_OR (the grammar wraps each arm pattern).
            hermes::TinyMapView str_eff;
            bool is_str_arm = false;
            if (has_str_hoist && arm.has_key(la::LHS)) {
                str_eff = effective_lhs(arm, alt_idx);
                if (code_of(str_eff) == la::PAT_OR && str_eff.has_key(la::ITEMS)) {
                    auto alts = arr_of(str_eff.get(la::ITEMS.code));
                    if (alts.size() == 1) str_eff = map_of(alts.get(0));
                }
                is_str_arm = (code_of(str_eff) == la::PAT_STR);
            }
            if (is_str_arm) {
                pat = make_pat_wild("_");
                auto strlit = builder().lit_str(
                    std::string(str_of(str_eff.get(la::VALUE.code))), make_slice_type(u8_t()));
                str_arm_guard = make_str_eq_guard(
                    builder().var_ref(str_scrut_var, str_scrut_type), std::move(strlit));
            } else {
                pat = arm.has_key(la::LHS)
                    ? build_pattern(effective_lhs(arm, alt_idx), scrut_type)
                    : make_pat_wild("_");
            }
            payload_or_alt_ = saved_payload_or_alt;
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
            // Factored into a lambda so a GUARDED arm can get a SECOND,
            // independent copy for the guard (B170-D/E): the body keeps its
            // own copy and a guard block-expr gets a fresh one — a single
            // shared/leaked copy is unreliable because the block-expr's
            // shadow-restore reverts a binding already in scope from a sibling
            // fanned or-arm.
            auto build_nested_destructure =
                [&](std::vector<lir::LStmt>& nested_destructure_stmts, bool for_guard) {
                emit_nested_pat_destructure(nested_subs, nested_destructure_stmts, for_guard);
            };
            std::vector<lir::LStmt> nested_destructure_stmts;
            build_nested_destructure(nested_destructure_stmts, /*for_guard=*/false);
            bool arm_has_user_guard = arm.has_key(la::GUARD);

            // Optional guard: `pattern if expr =>`
            std::optional<lir::LExprPtr> guard;
            if (arm.has_key(la::GUARD)) {
                auto g = lower_expr(map_of(arm.get(la::GUARD.code)));
                if (TypeRef(g->type).kind() != LogosType::Kind::Bool &&
                    TypeRef(g->type).kind() != LogosType::Kind::Error)
                    error("match guard must be bool");
                guard = std::move(g);
            }
            // G172-1: the string-literal arm's `str_eq(__smatch, "lit")` test
            // is its dispatch — AND it ahead of any user guard.
            if (str_arm_guard) {
                if (guard) {
                    auto merged = builder().bin_op("&&", std::move(str_arm_guard), std::move(*guard), bool_t());
                    guard = std::move(merged);
                } else {
                    guard = std::move(str_arm_guard);
                }
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
            // B170-D/E guards: a guarded arm with nested-payload destructure
            // lets (`Some((a, b)) if a > 0`, or-distributed `Some((a,_)|(_,a))
            // if a > 10`) must compute those bindings BEFORE the guard runs —
            // gen_match evaluates the guard in a block that precedes the body,
            // so a guard reading `a` would otherwise hit an undefined value
            // (compiler crash). Wrap the guard in a block-expr that runs the
            // destructure first; the bindings are fresh names, so the block-expr
            // leaks them into scope for the body too (the body prologue prepend
            // below then sees an empty list).
            if (guard && arm_has_user_guard) {
                // Build the SAFE (unconditional field/element) destructure for
                // the guard — never the refutable nested-variant let-else.
                std::vector<lir::LStmt> guard_destructure;
                build_nested_destructure(guard_destructure, /*for_guard=*/true);
                if (!guard_destructure.empty()) {
                    auto gblk = lir::alloc_block(*cur_prog_);
                    gblk->stmts = std::move(guard_destructure);
                    TypeRef gt = (*guard)->type;
                    guard = builder().block_expr(std::move(gblk), std::move(*guard), gt);
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
            // [[baghunt-match-arm-binding-no-drop]]: arm-scope
            // pattern bindings (e.g. `Ok(g)` where g is a Drop-typed
            // guard) need their Drop emitted before the arm exits.
            // lower_block handles inner-scope vars and (via
            // collect_all_drops) also drops arm-scope bindings before
            // Return — but for natural fall-through, the arm scope is
            // popped without emitting anything for the pattern
            // bindings. Append drops here, but only when the body
            // doesn't already end with Return (which already handled
            // all-frame drops). Break/Continue paths are buggy in
            // their own way (collect_drops vs collect_all_drops in
            // lower_block) — not in scope here.
            //
            // For stmt-form match arms with a TAIL_EXPR (`{ s }`),
            // the binding is being moved out as the body's last
            // value — mark it moved first so collect_drops skips it.
            // For tail-position match (match_in_tail_position_), the
            // last stmt is already an SReturn handled by lower_block
            // via collect_all_drops (which scans all frames). So the
            // mark-moved walk applies only to the non-return tail.
            {
                bool body_returns = false;
                lir_view::StmtRef last_stmt_ref;
                if (!body->stmts.empty()) {
                    last_stmt_ref = stmt_ref_of(body->stmts.back());
                    if (last_stmt_ref && last_stmt_ref.kind() == lir_schema::stmt::Code::Return)
                        body_returns = true;
                }
                if (!body_returns) {
                    // Walk the body's last stmt: if it's an ExprStmt
                    // whose value moves a pattern binding, mark it.
                    if (last_stmt_ref &&
                        last_stmt_ref.kind() == lir_schema::stmt::Code::ExprStmt) {
                        auto er = lir_view::SExprStmtView{last_stmt_ref}.expr();
                        mark_moved_in_expr_recursive(er);
                    }
                    for (auto& d : collect_drops())
                        body->stmts.push_back(std::move(d));
                }
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
    // Exhaustiveness: enum/bool scrutinee must cover all cases. K4: prove
    // nested-enum-pattern exhaustiveness at the AST level (unguarded arms),
    // since the desugar's synth guards defeat the LIR-level variant check.
    bool ast_exh = false;
    if (node.has_key(la::ITEMS)) {
        std::vector<hermes::TinyMapView> lhs_pats;
        auto arms_l = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < arms_l.size(); ++i) {
            auto arm = map_of(arms_l.get(i));
            if (code_of(arm) != la::MATCH_ARM) continue;
            if (arm.has_key(la::GUARD)) continue;      // user-guarded ≠ guaranteed
            if (arm.has_key(la::LHS)) lhs_pats.push_back(map_of(arm.get(la::LHS.code)));
        }
        ast_exh = ast_patterns_exhaustive(std::move(lhs_pats), scrut_type);
    }
    check_match_exhaustiveness(smatch, scrut_type, ast_exh);

    if (has_hoist_let) {
        auto blk = lir::alloc_block(*cur_prog_);
        blk->stmts.push_back(std::move(hoist_let_view));
        blk->stmts.push_back(std::move(hoist_let_root));
        blk->stmts.push_back(std::move(hoist_let_base));
        blk->stmts.push_back(make_stmt_emit(node_line_, std::move(smatch)));
        return make_stmt_emit(node_line_, lir::SBlock{std::move(blk)});
    }
    // G172-1: wrap the str-pattern match in a block that first hoists the
    // scrutinee into `__smatch`, which each arm's `str_eq(__smatch, …)` guard
    // references (so the scrutinee is evaluated exactly once).
    if (has_str_hoist) {
        auto blk = lir::alloc_block(*cur_prog_);
        blk->stmts.push_back(std::move(str_hoist_let));
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
    // G156-2: a match-EXPRESSION that binds+moves a payload out of a by-value
    // move-type enum/struct/tuple scrutinee (`let x = match body { Ok(s) => s }`)
    // must mark the scrutinee moved so its scope-exit Drop doesn't double-free a
    // value the result already owns. Same per-arm analysis as the statement
    // match path (shared helper). (Was: lforge read_manifest / graph_cas
    // double-free.)
    mark_match_scrutinee_moved(scrut, scrut_type, node);

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

    // G172-1: top-level string-literal patterns in an EXPRESSION-position match
    // (`let x = match s { "foo" => 1, _ => 0 }`). Mirror the lower_match path:
    // hoist the scrutinee into `__smatch` and lower each `"lit"` arm to a
    // wildcard + `str_eq(__smatch, "lit")` guard.
    bool has_str_pat = false;
    if (!has_hermes_pat && node.has_key(la::ITEMS)) {
        auto arms_l = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < arms_l.size() && !has_str_pat; ++i) {
            auto arm = map_of(arms_l.get(i));
            if (code_of(arm) != la::MATCH_ARM || !arm.has_key(la::LHS)) continue;
            auto lhs = map_of(arm.get(la::LHS.code));
            if (code_of(lhs) == la::PAT_STR) { has_str_pat = true; break; }
            if (code_of(lhs) == la::PAT_OR && lhs.has_key(la::ITEMS)) {
                auto alts = arr_of(lhs.get(la::ITEMS.code));
                for (uint64_t k = 0; k < alts.size(); ++k)
                    if (code_of(map_of(alts.get(k))) == la::PAT_STR) { has_str_pat = true; break; }
            }
        }
    }
    std::string str_scrut_var;
    TypeRef str_scrut_type;
    lir::LStmt str_hoist_let;
    bool has_str_hoist = false;
    if (has_str_pat) {
        str_scrut_var = "__smatch_" + std::to_string(tmp_var_count_++);
        str_scrut_type = scrut_type;
        define(str_scrut_var, scrut_type);
        lir::SLet sl;
        sl.name = str_scrut_var; sl.type = scrut_type; sl.is_mut = false;
        sl.value = std::move(scrut);
        str_hoist_let = make_stmt_emit(node_line_, std::move(sl));
        scrut = builder().var_ref(str_scrut_var, scrut_type);
        has_str_hoist = true;
    }

    lir::EMatchExpr me;
    me.scrut = std::move(scrut);
    TypeRef result_type = error_t();

    if (node.has_key(la::ITEMS)) {
        auto arms = arr_of(node.get(la::ITEMS.code));
        // Or-pattern fan-out (symmetric to lower_match). An or-pattern arm
        // whose alternatives bind variables or have non-scalar/refutable
        // shapes (`(1,a)|(2,a)`, variant payloads, etc.) is expanded into one
        // synthetic arm per alternative so each goes through the normal
        // single-arm path with its own payload extraction. Pure scalar-literal
        // or-patterns (`1|2|3`) stay merged. Without this, the merged tuple/
        // variant codegen mishandled bindings — e.g. dispatched on the
        // scrutinee pointer (`arith.cmpi ptr, 0`).
        struct EffArm { hermes::TinyMapView arm; int32_t alt_idx; int32_t payload_alt = -1; };
        auto alt_is_merge_safe = [](int32_t c) -> bool {
            return c == la::PAT_INT || c == la::PAT_BOOL || c == la::PAT_CHAR;
        };
        auto or_needs_fanout = [&](hermes::TinyMapView lhs) -> bool {
            if (code_of(lhs) != la::PAT_OR) return false;
            if (!lhs.has_key(la::ITEMS)) return false;
            auto a = arr_of(lhs.get(la::ITEMS.code));
            if (a.size() < 2) return false;
            for (uint64_t k = 0; k < a.size(); ++k)
                if (!alt_is_merge_safe(code_of(map_of(a.get(k))))) return true;
            return false;
        };
        // B170-E: variant whose single payload arg is a multi-alt PAT_OR — see
        // the lower_match twin for the rationale (or-distribution fan-out).
        auto variant_payload_or_alts = [&](hermes::TinyMapView lhs) -> int {
            if (code_of(lhs) == la::PAT_OR && lhs.has_key(la::ITEMS)) {
                auto a = arr_of(lhs.get(la::ITEMS.code));
                if (a.size() == 1) lhs = map_of(a.get(0));
            }
            if (code_of(lhs) != la::PAT_VARIANT_DATA) return 0;
            if (!lhs.has_key(la::ARGS)) return 0;
            AnyVal aav = lhs.get(la::ARGS.code);
            if (aav.is_null() || !aav.is_pointer()) return 0;
            auto blist = map_of(aav);
            if (!blist.has_key(la::ITEMS)) return 0;
            auto items = arr_of(blist.get(la::ITEMS.code));
            if (items.size() != 1) return 0;
            auto arg = map_of(items.get(0));
            if (code_of(arg) != la::PAT_OR || !arg.has_key(la::ITEMS)) return 0;
            auto alts = arr_of(arg.get(la::ITEMS.code));
            bool needs = false;
            for (uint64_t k = 0; k < alts.size(); ++k)
                if (!alt_is_merge_safe(code_of(map_of(alts.get(k))))) { needs = true; break; }
            return (alts.size() >= 2 && needs) ? (int)alts.size() : 0;
        };
        std::vector<EffArm> eff_arms;
        for (uint64_t i = 0; i < arms.size(); ++i) {
            auto arm = map_of(arms.get(i));
            if (code_of(arm) != la::MATCH_ARM) { eff_arms.push_back({arm, -1}); continue; }
            if (arm.has_key(la::LHS)) {
                auto lhs = map_of(arm.get(la::LHS.code));
                if (or_needs_fanout(lhs)) {
                    auto a = arr_of(lhs.get(la::ITEMS.code));
                    for (uint64_t k = 0; k < a.size(); ++k)
                        eff_arms.push_back({arm, (int32_t)k});
                    continue;
                }
                if (int n = variant_payload_or_alts(lhs); n > 0) {
                    for (int k = 0; k < n; ++k)
                        eff_arms.push_back({arm, -1, k});
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
        for (uint64_t i = 0; i < eff_arms.size(); ++i) {
            auto arm = eff_arms[i].arm;
            int32_t alt_idx = eff_arms[i].alt_idx;
            if (code_of(arm) != la::MATCH_ARM) continue;

            lir::LExprPtr synth_guard = nullptr;
            std::vector<lir::LStmt> body_prologue;
            std::vector<HermesPatBinding> body_binds;
            if (has_hermes_pat && arm.has_key(la::LHS)) {
                std::vector<lir::LStmt> g_stmts;
                std::vector<HermesPatBinding> g_binds;
                auto raw = build_hermes_pat_guard(
                    effective_lhs(arm, alt_idx), root_var, anyval_t,
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
                        effective_lhs(arm, alt_idx), root_var, anyval_t,
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
            // B170-E: select this fanned arm's payload-or alternative.
            int32_t saved_payload_or_alt = payload_or_alt_;
            payload_or_alt_ = eff_arms[i].payload_alt;
            // G172-1: top-level string-literal arm → wildcard + str_eq guard.
            lir::LExprPtr str_arm_guard = nullptr;
            lir::Pattern pat;
            hermes::TinyMapView str_eff;
            bool is_str_arm = false;
            if (has_str_hoist && arm.has_key(la::LHS)) {
                str_eff = effective_lhs(arm, alt_idx);
                if (code_of(str_eff) == la::PAT_OR && str_eff.has_key(la::ITEMS)) {
                    auto alts = arr_of(str_eff.get(la::ITEMS.code));
                    if (alts.size() == 1) str_eff = map_of(alts.get(0));
                }
                is_str_arm = (code_of(str_eff) == la::PAT_STR);
            }
            if (is_str_arm) {
                pat = make_pat_wild("_");
                auto strlit = builder().lit_str(
                    std::string(str_of(str_eff.get(la::VALUE.code))), make_slice_type(u8_t()));
                str_arm_guard = make_str_eq_guard(
                    builder().var_ref(str_scrut_var, str_scrut_type), std::move(strlit));
            } else {
                pat = arm.has_key(la::LHS)
                    ? build_pattern(effective_lhs(arm, alt_idx), scrut_type)
                    : make_pat_wild("_");
            }
            payload_or_alt_ = saved_payload_or_alt;
            current_pat_nested_subs_ = saved_pat_subs;
            current_pat_refutable_guards_ = saved_pat_refut;
            in_match_hermes_ctx_ = false;

            push_scope();
            bind_pattern(pat, scrut_type);
            current_pat_mut_names_ = saved_pat_muts;
            for (const auto& b : body_binds) {
                define(b.name, anyval_t, /*is_mut=*/false);
            }
            // P4-pm-02: nested struct sub-pat destructure stmts. Factored into
            // a lambda so a guarded arm can get an independent copy for its
            // guard (B170-D/E) — see the lower_match twin.
            auto build_nested_destructure =
                [&](std::vector<lir::LStmt>& nested_destructure_stmts, bool for_guard) {
            for (auto& nsub : nested_subs) {
                TypeRef synth_t = lookup(nsub.synth_name);
                if (!synth_t) continue;
                if (code_of(nsub.sub_pat_node) == la::PAT_VARIANT_DATA) {
                    // A nested-variant payload destructure uses a refutable
                    // `let … else { loop {} }` that ASSUMES the arm already
                    // matched (its own synth guard ran). It must NOT be hoisted
                    // into the guard (for_guard) — running it before the synth
                    // guard confirms the variant would hit `loop {}` on a
                    // non-matching scrutinee (infinite loop).
                    if (!for_guard)
                        emit_nested_variant_lets(nsub.synth_name, synth_t,
                                                 nsub.sub_pat_node, nested_destructure_stmts);
                    continue;
                }
                // B170: nested TUPLE sub-pattern in a variant payload
                // (`Some((a, b))`, `Some((a, _))`, `Ok((a, (b, c)))`). The
                // synth holds the payload tuple; emit `let <name> = __synth.<i>`
                // element reads (recursing into nested tuples). Previously only
                // PAT_STRUCT / PAT_VARIANT_DATA nested subs were destructured,
                // so a tuple-payload binding was left undefined.
                if (code_of(nsub.sub_pat_node) == la::PAT_TUPLE) {
                    std::function<void(lir::LExprPtr, TypeRef, hermes::TinyMapView)>
                    emit_tuple_lets =
                        [&](lir::LExprPtr src, TypeRef tty, hermes::TinyMapView tnode) {
                        if (!tty || TypeRef(tty).kind() != LogosType::Kind::Tuple) return;
                        if (!tnode.has_key(la::ITEMS)) return;
                        auto items = arr_of(tnode.get(la::ITEMS.code));
                        auto elems = TypeRef(tty).tuple_elems();
                        // Spill the source to a temp so each element read
                        // references it once.
                        std::string stmp = std::format("__pat_tup_{}", tmp_var_count_++);
                        define(stmp, tty);
                        {
                            lir::SLet s; s.name = stmp; s.type = tty;
                            s.is_mut = false; s.value = std::move(src);
                            nested_destructure_stmts.push_back(
                                make_stmt_emit(node_line_, std::move(s)));
                        }
                        for (uint64_t i = 0; i < items.size() && i < elems.size(); ++i) {
                            auto en = map_of(items.get(i));
                            auto et = elems[i];
                            auto elem_expr = builder().tuple_index(
                                builder().var_ref(stmp, tty), (uint32_t)i, et);
                            // Tuple elements are wrapped in a (usually single-alt)
                            // PAT_OR by the grammar (`pat_single (PIPE pat_single)*`).
                            // Unwrap a single alternative to reach the bare binding.
                            if (code_of(en) == la::PAT_OR && en.has_key(la::ITEMS)) {
                                auto alts = arr_of(en.get(la::ITEMS.code));
                                if (alts.size() == 1) en = map_of(alts.get(0));
                            }
                            int32_t ec = code_of(en);
                            if (ec == la::PAT_TUPLE) {
                                emit_tuple_lets(std::move(elem_expr), et, en);
                            } else if (ec == la::PAT_WILD && en.has_key(la::NAME)) {
                                std::string nm(str_of(en.get(la::NAME.code)));
                                if (nm == "_") continue;
                                define(nm, et);
                                lir::SLet el; el.name = nm; el.type = et;
                                el.is_mut = false; el.value = std::move(elem_expr);
                                nested_destructure_stmts.push_back(
                                    make_stmt_emit(node_line_, std::move(el)));
                            }
                            // Other element kinds (struct/refutable) inside a
                            // payload tuple are handled by build_pattern's own
                            // synth/guard channels, not here.
                        }
                    };
                    emit_tuple_lets(builder().var_ref(nsub.synth_name, synth_t),
                                    synth_t, nsub.sub_pat_node);
                    continue;
                }
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
            };  // build_nested_destructure
            std::vector<lir::LStmt> nested_destructure_stmts;
            build_nested_destructure(nested_destructure_stmts, /*for_guard=*/false);
            bool arm_has_user_guard = arm.has_key(la::GUARD);

            std::optional<lir::LExprPtr> guard;
            if (arm.has_key(la::GUARD)) {
                auto g = lower_expr(map_of(arm.get(la::GUARD.code)));
                if (TypeRef(g->type).kind() != LogosType::Kind::Bool &&
                    TypeRef(g->type).kind() != LogosType::Kind::Error)
                    error("match guard must be bool");
                guard = std::move(g);
            }
            // G172-1: AND the string-literal arm's str_eq dispatch ahead of any
            // user guard.
            if (str_arm_guard) {
                if (guard) {
                    auto merged = builder().bin_op("&&", std::move(str_arm_guard), std::move(*guard), bool_t());
                    guard = std::move(merged);
                } else {
                    guard = std::move(str_arm_guard);
                }
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
            // G145-2 (soundness): AND in the refutable-inner guards collected
            // during build_pattern (a literal/variant sub-pattern in a variant
            // payload, e.g. `Foo::FooUint(1)`). The statement-form lower_match
            // already does this; without it the match-EXPRESSION form silently
            // dropped the payload test and mis-dispatched (matched the variant
            // tag regardless of the inner literal).
            for (auto& rg : refut_guards) {
                if (!rg) continue;
                if (guard) {
                    auto merged = builder().bin_op("&&", std::move(*guard), std::move(rg), bool_t());
                    guard = std::move(merged);
                } else {
                    guard = std::move(rg);
                }
            }
            // B170-D/E guards: a guarded arm with nested-payload destructure
            // lets (`Some((a, b)) if a > 0`, or-distributed `Some((a,_)|(_,a))
            // if a > 10`) must compute those bindings BEFORE the guard runs —
            // gen_match evaluates the guard in a block that precedes the body,
            // so a guard reading `a` would otherwise hit an undefined value
            // (compiler crash). Wrap the guard in a block-expr that runs the
            // destructure first; the bindings are fresh names, so the block-expr
            // leaks them into scope for the body too (the body prologue prepend
            // below then sees an empty list).
            if (guard && arm_has_user_guard) {
                // Build the SAFE (unconditional field/element) destructure for
                // the guard — never the refutable nested-variant let-else.
                std::vector<lir::LStmt> guard_destructure;
                build_nested_destructure(guard_destructure, /*for_guard=*/true);
                if (!guard_destructure.empty()) {
                    auto gblk = lir::alloc_block(*cur_prog_);
                    gblk->stmts = std::move(guard_destructure);
                    TypeRef gt = (*guard)->type;
                    guard = builder().block_expr(std::move(gblk), std::move(*guard), gt);
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
            // A diverging arm (Never = `!`) contributes no type — the match's
            // type is that of the non-diverging arms (Never is a subtype of
            // every type). Treat Never like Error in the accumulator.
            if (TypeRef(result_type).kind() == LogosType::Kind::Error ||
                TypeRef(result_type).kind() == LogosType::Kind::Never) {
                result_type = val->type;
            } else if (TypeRef(val->type).kind() == LogosType::Kind::Never) {
                // keep result_type — this arm yields no value.
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

            // [[baghunt-match-arm-binding-no-drop]] — see lower_match
            // (stmt form) above for the rationale. Arm-scope pattern
            // bindings need their Drop emitted before the arm value
            // escapes. For value-form arms the arm value is an
            // expression we must preserve, so hoist it into a temp,
            // emit drops, then yield the temp. Skip when the arm
            // value is error-typed (divergent block arms already
            // emitted drops via lower_block::collect_all_drops on the
            // inner Returns).
            if (val && TypeRef(val->type).kind() != LogosType::Kind::Error) {
                // Mark bindings consumed by val as moved before
                // computing drops (matches lower_return semantics).
                mark_moved_in_expr_recursive(expr_ref_of(*val));
                auto arm_drops = collect_drops();
                if (!arm_drops.empty()) {
                    TypeRef vt = val->type;
                    auto blk = lir::alloc_block(*cur_prog_);
                    if (TypeRef(vt).kind() == LogosType::Kind::Void) {
                        // Void: evaluate val for effect, then drops.
                        lir::SExprStmt es; es.expr = std::move(val);
                        blk->stmts.push_back(make_stmt_emit(node_line_, std::move(es)));
                        for (auto& d : arm_drops)
                            blk->stmts.push_back(std::move(d));
                        val = builder().block_expr(std::move(blk), error_expr(), vt);
                    } else {
                        std::string tmp = "__match_arm_tmp_" +
                                          std::to_string(tmp_var_count_++);
                        lir::SLet sl;
                        sl.name = tmp; sl.type = vt; sl.is_mut = false;
                        sl.value = std::move(val);
                        blk->stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
                        for (auto& d : arm_drops)
                            blk->stmts.push_back(std::move(d));
                        val = builder().block_expr(std::move(blk),
                            builder().var_ref(tmp, vt), vt);
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
        // K4: prove nested-enum-pattern exhaustiveness at the AST level.
        bool ast_exh = false;
        if (node.has_key(la::ITEMS)) {
            std::vector<hermes::TinyMapView> lhs_pats;
            auto arms_l = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < arms_l.size(); ++i) {
                auto arm = map_of(arms_l.get(i));
                if (code_of(arm) != la::MATCH_ARM) continue;
                if (arm.has_key(la::GUARD)) continue;
                if (arm.has_key(la::LHS)) lhs_pats.push_back(map_of(arm.get(la::LHS.code)));
            }
            ast_exh = ast_patterns_exhaustive(std::move(lhs_pats), scrut_type);
        }
        bool has_wild = ast_exh;
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
    // G172-1: hoist the str-match scrutinee into `__smatch` before the match.
    if (has_str_hoist) {
        auto blk = lir::alloc_block(*cur_prog_);
        blk->stmts.push_back(std::move(str_hoist_let));
        return builder().block_expr(std::move(blk), std::move(me_expr), result_type);
    }
    return me_expr;
}


} // namespace logos::compiler
