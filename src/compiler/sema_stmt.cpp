// Logos project — https://github.com/victor-smirnov/logos

#include "sema_impl.hpp"
#include <logos/compiler/probe.hpp>
#include "ctfe.hpp"
#include "logos/compiler/subtype.hpp"

#include <logos/writ/compat.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <functional>
#include <map>

namespace logos::compiler {

namespace la = ast;
using writ::TinyMapView;
using writ::ArrayView;
using writ::StringView;
using writ::AnyVal;
using writ::MemHolder;

// Statement lowering methods

// ── Does a `break` TARGET this loop? — the one predicate ──────────────────
//
// A `loop` diverges only when no `break` targets it (rustc types `loop` as `!`
// on exactly this rule, and on syntactic targeting, not on reachability).
// PRESENCE OF THE TOKEN IS NOT THE PROPERTY: in `bt_upper_bound`
// (stdlib/lcm/deem/data/bt/descent.logos) the fn body's last statement is a
// `loop` whose only `break` sits in a nested `while` — it targets the `while`,
// the `loop` still diverges, and that fn's return-reachability depends on it.
// Targeting mirrors lower_loop / the BREAK arm's frame search exactly:
//   - unlabelled `break`  -> innermost enclosing loop: ours only at depth 0
//   - `break 'a`          -> innermost enclosing loop labelled 'a
//   - a nested loop carrying OUR label SHADOWS us — nothing inside it can
//     reach us, so that whole subtree is skipped
//   - CLOSURE_EXPR / NESTED_FN are FUNCTION BOUNDARIES: a break there targets
//     no loop of ours
// Every other node is walked GENERICALLY (each TinyMap under each key, each
// array element, idiom copied from sema_expr.cpp's quote_item walker), so a new
// AST node kind is traversed by default; only a node that introduces a new
// break TARGET or a new FUNCTION BOUNDARY has to be added to the lists above.
// (AST codes are `constexpr Code` constants, not an enum, so -Wswitch cannot
// stand guard here; the generic-by-default walk is what replaces it.)
bool SemaChecker::loop_has_targeting_break(TinyMapView loop_node) {
    namespace lh = logos::writ;
    std::string my_label;
    TinyMapView owner = loop_node;
    if (code_of(loop_node) == la::LABELED_LOOP) {
        if (loop_node.has_key(la::LABEL))
            my_label = std::string(str_of(loop_node.get(la::LABEL.code)));
        // Two spellings: `labeled_loop_stmt` puts the loop STATEMENT under
        // BODY, `loop_expr` puts the BLOCK there.
        auto inner = map_of(loop_node.get(la::BODY.code));
        int32_t ic = code_of(inner);
        if (ic == la::LOOP || ic == la::WHILE || ic == la::FOR || ic == la::FOR_EACH)
            owner = inner;
    }
    if (!owner.has_key(la::BODY)) return false;
    bool found = false;
    std::function<void(TinyMapView, int)> walk = [&](TinyMapView n, int depth) {
        if (found || n.is_null()) return;
        int32_t c = code_of(n);
        if (c == la::CLOSURE_EXPR || c == la::NESTED_FN) return;   // fn boundary
        if (c == la::BREAK || c == la::BREAK_EXPR) {
            std::string lbl = n.has_key(la::LABEL)
                ? std::string(str_of(n.get(la::LABEL.code))) : std::string();
            if (lbl.empty() ? (depth == 0)
                            : (!my_label.empty() && lbl == my_label)) {
                found = true; return;
            }
            // fall through: `break <expr>` can carry further nodes in VALUE
        }
        int child_depth = depth;
        if (c == la::LOOP || c == la::WHILE || c == la::FOR || c == la::FOR_EACH) {
            child_depth = depth + 1;
        } else if (c == la::LABELED_LOOP) {
            if (!my_label.empty() && n.has_key(la::LABEL) &&
                str_of(n.get(la::LABEL.code)) == my_label)
                return;                                  // shadowed — skip subtree
            auto ib = map_of(n.get(la::BODY.code));
            int32_t ic = code_of(ib);
            bool body_is_loop_stmt = (ic == la::LOOP || ic == la::WHILE ||
                                      ic == la::FOR  || ic == la::FOR_EACH);
            child_depth = body_is_loop_stmt ? depth : depth + 1;
        }
        uint64_t bm = n.bitmap();
        for (uint8_t key = 0; key < writ::TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            AnyVal av = n.get(key);
            if (av.is_null() || !av.is_pointer()) continue;
            const uint8_t* pv = av.resolve();
            if (!pv) continue;
            uint64_t tc = lh::TypeTag::read_before(pv).type_code();
            if (tc == lh::type_hash::TinyObjectMap) {
                walk(map_of(av), child_depth);
            } else if (tc == lh::type_hash::Array) {
                auto arr = arr_of(av);
                for (uint64_t i = 0; i < arr.size() && !found; ++i)
                    walk(map_of(arr.get(i)), child_depth);
            }
        }
    };
    walk(map_of(owner.get(la::BODY.code)), 0);
    return found;
}

// Does this subtree contain ANY `break` / `continue` (labelled or not, at any
// depth; closures and nested fns are function boundaries)? Deliberately wider
// than loop_has_targeting_break: its caller only needs a sound "none", and a
// loop reached through a label wrapper it cannot see would be missed there.
// Any control exit out of an expression: `return`, `break`, `continue`, `?`
// (closures and nested fns excluded — their exits are their own).
bool SemaChecker::ast_has_exit(TinyMapView root) {
    if (ast_has_break_or_continue(root)) return true;
    bool found = false;
    std::function<void(TinyMapView)> walk = [&](TinyMapView n) {
        if (found || n.is_null()) return;
        int32_t c = code_of(n);
        if (c == la::CLOSURE_EXPR || c == la::NESTED_FN) return;
        if (c == la::RETURN || c == la::RETURN_EXPR || c == la::TRY_EXPR) { found = true; return; }
        uint64_t bm = n.bitmap();
        for (uint8_t key = 0; key < writ::TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            AnyVal av = n.get(key);
            if (av.is_null() || !av.is_pointer()) continue;
            const uint8_t* pv = av.resolve();
            if (!pv) continue;
            uint64_t tc = logos::writ::TypeTag::read_before(pv).type_code();
            if (tc == logos::writ::type_hash::TinyObjectMap) walk(map_of(av));
            else if (tc == logos::writ::type_hash::Array) {
                auto arr = arr_of(av);
                for (uint64_t i = 0; i < arr.size() && !found; ++i) walk(map_of(arr.get(i)));
            }
        }
    };
    walk(root);
    return found;
}

static bool op_assign_trait_method(const std::string& base_op,
                                   std::string& trait, std::string& method);

bool SemaChecker::ast_has_call(TinyMapView root) {
    bool found = false;
    std::function<void(TinyMapView)> walk = [&](TinyMapView n) {
        if (found || n.is_null()) return;
        int32_t c = code_of(n);
        if (c == la::CLOSURE_EXPR || c == la::NESTED_FN) return;
        if (c == la::CALL || c == la::METHOD_CALL || c == la::GENERIC_CALL ||
            c == la::STATIC_CALL) { found = true; return; }
        uint64_t bm = n.bitmap();
        for (uint8_t key = 0; key < writ::TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            AnyVal av = n.get(key);
            if (av.is_null() || !av.is_pointer()) continue;
            const uint8_t* pv = av.resolve();
            if (!pv) continue;
            uint64_t tc = logos::writ::TypeTag::read_before(pv).type_code();
            if (tc == logos::writ::type_hash::TinyObjectMap) walk(map_of(av));
            else if (tc == logos::writ::type_hash::Array) {
                auto arr = arr_of(av);
                for (uint64_t i = 0; i < arr.size() && !found; ++i) walk(map_of(arr.get(i)));
            }
        }
    };
    walk(root);
    return found;
}

const SemaChecker::SemaFuncInfo* SemaChecker::find_op_assign_impl(const std::string& mangled, TypeRef ref_t,
                                                     TypeRef self_t, lir::LExprPtr& rhs) {
    using K = LogosType::Kind;
    TypeRef rhs_ty = rhs ? TypeRef(expr_type(rhs)) : self_t;
    auto fit = find_func_by_base_and_signature(mangled, {ref_t, rhs_ty}, false);
    if (!fit && !types_equal(rhs_ty, self_t))
        fit = find_func_by_base_and_signature(mangled, {ref_t, self_t}, false);
    if (fit || !rhs_ty) return fit;
    const K rk = TypeRef(rhs_ty).kind();
    if (rk != K::IntLit && rk != K::FloatLit) return nullptr;
    const SemaFuncInfo* only = nullptr;
    for (auto* c : find_func_candidates(mangled)) {
        if (c->param_types.size() != 2 || !c->param_types[1]) continue;
        const K pk = TypeRef(c->param_types[1]).kind();
        const bool ok = rk == K::IntLit ? (is_integer_kind(pk) && pk != K::Enum && pk != K::IntLit)
                                        : (pk == K::F32 || pk == K::F64);
        if (!ok) continue;
        if (only) return nullptr;   // ambiguous
        only = c;
    }
    if (only) widen_int_expr(rhs, only->param_types[1], builder());
    return only;
}

lir::LExprPtr SemaChecker::compound_rhs_first(lir::LExprPtr rhs, TypeRef pt,
                                              TinyMapView rhs_node, bool place_calls) {
    using K = LogosType::Kind;
    if (!cur_stmt_temp_hoist_ || !rhs || !pt) return rhs;
    const K k = TypeRef(pt).kind();
    const bool prim = (is_integer_kind(k) && k != K::Enum && k != K::IntLit) ||
                      k == K::F32 || k == K::F64 || k == K::Bool || k == K::Char;
    if (!prim) return rhs;
    // An operand whose evaluation cannot be observed from the place (a literal;
    // with a pure place also a read built of variables, fields, casts and
    // operators) keeps its place in the statement.
    std::function<bool(TinyMapView, bool)> inert = [&](TinyMapView n, bool reads_ok) -> bool {
        if (n.is_null()) return true;
        const int32_t c = code_of(n);
        if (c == la::LIT_INT || c == la::LIT_FLOAT || c == la::LIT_BOOL || c == la::LIT_CHAR)
            return true;
        if (!reads_ok) return false;
        if (c == la::VAR_REF) return true;
        if (c == la::PAREN_EXPR || c == la::CAST || c == la::UNARY || c == la::DEREF ||
            c == la::FIELD_READ || c == la::TUPLE_INDEX)
            return inert(map_of(n.get((n.has_key(la::VALUE) ? la::VALUE : la::RECEIVER).code)), true);
        if (c == la::BINOP)
            return inert(map_of(n.get(la::LHS.code)), true) && inert(map_of(n.get(la::RHS.code)), true);
        return false;
    };
    if (inert(unwrap_paren_node(rhs_node), !place_calls)) return rhs;
    widen_int_expr(rhs, pt, builder());
    TypeRef rt = expr_type(rhs);
    if (!rt || TypeRef(rt).kind() == K::IntLit || TypeRef(rt).kind() == K::Error) return rhs;
    std::string nm = std::format("__rtmp_{}", destruct_counter_++);
    register_stmt_temp(nm, rt, std::move(rhs), false);
    return builder().var_ref(nm, rt);
}

// The names a function RETURNS by value (`return c;`, or `c` as the body's
// tail) that were bound to a closure literal: that literal escapes, its env is
// heap (escaping_closure_lets_) and the returned value owns it. Closures and
// nested fns are their own functions.
void SemaChecker::collect_returned_closure_lets_(TinyMapView body) {
    namespace lh = logos::writ;
    escaping_closure_lets_.clear();
    escaping_closure_names_.clear();
    std::unordered_set<std::string> returned;
    auto var_name = [&](AnyVal av) -> std::string {
        if (av.is_null() || !av.is_pointer()) return {};
        auto v = unwrap_paren_node(map_of(av));
        return code_of(v) == la::VAR_REF ? std::string(str_of(v.get(la::NAME.code))) : std::string();
    };
    std::vector<std::pair<std::string, const void*>> lets;
    std::function<void(TinyMapView)> walk = [&](TinyMapView n) {
        if (n.is_null()) return;
        int32_t c = code_of(n);
        if (c == la::CLOSURE_EXPR || c == la::NESTED_FN) return;
        if ((c == la::RETURN || c == la::RETURN_EXPR) && n.has_key(la::VALUE))
            if (auto nm = var_name(n.get(la::VALUE.code)); !nm.empty()) returned.insert(nm);
        if (c == la::LET && n.has_key(la::VALUE) && n.has_key(la::NAME)) {
            auto v = unwrap_paren_node(map_of(n.get(la::VALUE.code)));
            if (code_of(v) == la::CLOSURE_EXPR)
                lets.emplace_back(std::string(str_of(n.get(la::NAME.code))), v.ptr());
        }
        uint64_t bm = n.bitmap();
        for (uint8_t key = 0; key < writ::TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            AnyVal av = n.get(key);
            if (av.is_null() || !av.is_pointer()) continue;
            const uint8_t* pv = av.resolve();
            if (!pv) continue;
            uint64_t tc = lh::TypeTag::read_before(pv).type_code();
            if (tc == lh::type_hash::TinyObjectMap) walk(map_of(av));
            else if (tc == lh::type_hash::Array) {
                auto arr = arr_of(av);
                for (uint64_t i = 0; i < arr.size(); ++i) walk(map_of(arr.get(i)));
            }
        }
    };
    walk(body);
    if (body.has_key(la::ITEMS)) {
        auto items = arr_of(body.get(la::ITEMS.code));
        for (int64_t i = (int64_t)items.size() - 1; i >= 0; --i) {
            auto last = map_of(items.get(i));
            if (last.is_null()) continue;
            if (code_of(last) == la::TAIL_EXPR && last.has_key(la::VALUE))
                if (auto nm = var_name(last.get(la::VALUE.code)); !nm.empty()) returned.insert(nm);
            break;
        }
    }
    for (auto& [nm, node] : lets)
        if (returned.count(nm)) { escaping_closure_lets_.insert(node); escaping_closure_names_.insert(nm); }
}

bool SemaChecker::ast_has_break_or_continue(TinyMapView root) {
    namespace lh = logos::writ;
    bool found = false;
    std::function<void(TinyMapView)> walk = [&](TinyMapView n) {
        if (found || n.is_null()) return;
        int32_t c = code_of(n);
        if (c == la::CLOSURE_EXPR || c == la::NESTED_FN) return;
        if (c == la::BREAK || c == la::BREAK_EXPR || c == la::CONTINUE || c == la::CONTINUE_EXPR) {
            found = true; return;
        }
        uint64_t bm = n.bitmap();
        for (uint8_t key = 0; key < writ::TinyObjectMap::MAX_KEYS; ++key) {
            if (!(bm & (1ULL << key))) continue;
            AnyVal av = n.get(key);
            if (av.is_null() || !av.is_pointer()) continue;
            const uint8_t* pv = av.resolve();
            if (!pv) continue;
            uint64_t tc = lh::TypeTag::read_before(pv).type_code();
            if (tc == lh::type_hash::TinyObjectMap) walk(map_of(av));
            else if (tc == lh::type_hash::Array) {
                auto arr = arr_of(av);
                for (uint64_t i = 0; i < arr.size() && !found; ++i) walk(map_of(arr.get(i)));
            }
        }
    };
    walk(root);
    return found;
}

// The two spellings of `'a: loop { ... }`, and ONLY those. `loop_expr`
// (grammar `LIFETIME COLON KW_LOOP block_body`) puts the BLOCK under BODY;
// `labeled_loop_stmt` (`LIFETIME COLON (for_stmt / while_stmt / loop_stmt)`)
// puts the loop STATEMENT there and that statement may be a WHILE or a FOR.
// PAIRED EXEMPTION, measured in the abuse direction: `'a: while c { }` and
// `'a: for i in ... { }` are NOT infinite, and answering otherwise admits a fn
// that falls off its end with no value. The three call sites below tested the
// node CODE for `la::LOOP` and nothing else, so LABELED_LOOP reached none of
// them and fell off the end to "does not diverge" — while the predicate they
// delegate to, loop_has_targeting_break, has unwrapped both spellings all
// along. Two notions of "is a loop that diverges", the narrow one written at
// the call site.
bool SemaChecker::is_infinite_loop_node(TinyMapView n) {
    int32_t c = code_of(n);
    if (c == la::LOOP) return true;
    if (c != la::LABELED_LOOP) return false;
    if (!n.has_key(la::BODY)) return false;
    int32_t ic = code_of(map_of(n.get(la::BODY.code)));
    return ic == la::LOOP || ic == la::BLOCK;
}

bool SemaChecker::stmt_always_returns(TinyMapView stmt) {
    int32_t c = code_of(stmt);
    if (c == la::RETURN) return true;
    if ((c == la::EXPR_STMT || c == la::TAIL_EXPR) && stmt.has_key(la::VALUE)) {
        auto e = map_of(stmt.get(la::VALUE.code));
        if (is_divergent_call_node(e)) return true;
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
            ec == la::CONTINUE_EXPR || is_divergent_call_node(e))
            return true;
        if (ec == la::BLOCK) return block_always_returns(e);
        if (ec == la::IF || ec == la::MATCH) return stmt_always_returns(e);
    }
    if (is_infinite_loop_node(stmt)) {
        // `loop { … }` diverges only when NO `break` targets it — the same rule
        // lower_loop applies via loop_break_frames_ (last_loop_diverged_).
        // `loop { break; }` FALLS THROUGH, and calling it diverging is what let
        // this gate lie to the let-else check and to match-arm typing.
        return !loop_has_targeting_break(stmt);
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
                // An expression arm IS the return only for a match in TAIL
                // position (tail_match_nodes_: the body's value). Anywhere else
                // — a `let` initializer, an operand — it yields a value and
                // returns only when the expression itself diverges; counting
                // it there made `let w = match k { _ => 5 };` "always return",
                // and a block arm holding it lost its value.
                if (!tail_match_nodes_.count(stmt.ptr())) {
                    auto e = unwrap_paren_node(map_of(arm.get(la::EXPR.code)));
                    const int32_t ec = code_of(e);
                    if (!(ec == la::RETURN_EXPR || ec == la::BREAK_EXPR ||
                          ec == la::CONTINUE_EXPR || is_divergent_call_node(e) ||
                          (ec == la::BLOCK && block_always_returns(e))))   // `{ return e; }` (HIR)
                        all_ret = false;
                }
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
            } else if (arm.has_key(la::EXPR)) {
                // An EXPRESSION arm (`1 => 5`) diverges only when the
                // expression does; it used to be skipped, so every match of
                // plain values "always diverted" and a block arm ending in one
                // lost its value (a `{ match k { .. } }` arm yielded 0).
                auto e = unwrap_paren_node(map_of(arm.get(la::EXPR.code)));
                const int32_t ec = code_of(e);
                if (!(ec == la::RETURN_EXPR || ec == la::BREAK_EXPR || ec == la::CONTINUE_EXPR ||
                      is_divergent_call_node(e) ||
                      (ec == la::BLOCK && block_always_diverts(e))))   // `{ return e; }` (HIR)
                    all_d = false;
            } else {
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

// logos-core 1.1: STRICTER than block_always_returns — a normal `return X;`
// does NOT count as "diverges". We want: body ends in `panic(...)`,
// `loop { … }` with no break, or any callee whose return type is `!`.
// Used to gate the Rust-2024 `!`-fallback at infer_type_args: a type-param
// is allowed to fall back to `!` only when the callee's body provably
// never returns normally. Distinguishes the targeted shape
// `fn f<T>() -> T { panic(); }` (T → !) from the existing fail-test
// `fn f<T>() -> T { return 0; }` (T unbound → ambiguous, correct error).
bool SemaChecker::body_always_diverges_simple(TinyMapView body_node) {
    if (!body_node.has_key(la::ITEMS)) return false;
    auto stmts = arr_of(body_node.get(la::ITEMS.code));
    if (stmts.size() == 0) return false;
    auto last = map_of(stmts.get(stmts.size() - 1));
    int32_t c = code_of(last);
    if ((c == la::EXPR_STMT || c == la::TAIL_EXPR) && last.has_key(la::VALUE)) {
        auto e = map_of(last.get(la::VALUE.code));
        if (is_divergent_call_node(e)) return true;
        if (is_infinite_loop_node(e)) return !loop_has_targeting_break(e);
    }
    if (is_infinite_loop_node(last)) return !loop_has_targeting_break(last);
    return false;
}

// A fresh owned rvalue (not a place / borrow) — the kinds whose materialized
// temporary needs a statement-scope drop. Mirrors the B140-G1 is_place check.
bool SemaChecker::is_hoistable_temp_rvalue(lir_view::ExprRef e) {
    namespace ec = lir_schema::expr;
    auto k = e.kind();
    // A module CONST is a value, not a place: every use makes a fresh one
    // (rustc), so `P.id` over a Drop-typed `const P` owns a temporary that
    // drops at the end of the statement (it leaked).
    if (k == ec::Code::VarRef) {
        std::string n(lir_view::EVarRefView{e}.name());
        bool local = false;
        for (auto it = scope_.rbegin(); it != scope_.rend() && !local; ++it) local = it->vars.count(n) != 0;
        if (!local) {
            std::string ck = resolve_const_key(n);
            if (!ck.empty() && module_consts_.count(ck)) return true;
        }
    }
    switch (k) {
        case ec::Code::VarRef: case ec::Code::FieldRead: case ec::Code::IndexRead:
        case ec::Code::Deref:  case ec::Code::TupleIndex: case ec::Code::SliceIndex:
        case ec::Code::SlicePtr: case ec::Code::AddrOf: case ec::Code::AddrOfTemp:
            return false;   // a place / existing-owned borrow — not a fresh temp
        default:
            return true;    // Call / MethodCall / StructLit / EnumLitData / …
    }
}

lir_view::StmtRef SemaChecker::lower_stmt(TinyMapView stmt) {
    // Install a temporary-scope collector for this statement (save/restore across
    // the recursion below — LABELED_LOOP and loop bodies re-enter lower_stmt).
    std::vector<std::tuple<std::string, TypeRef, lir::LExprPtr, bool>> hoisted;
    auto* saved_hoist = cur_stmt_temp_hoist_;
    size_t saved_frame = cur_stmt_temp_hoist_frame_;
    cur_stmt_temp_hoist_ = &hoisted;
    // Temps are define()'d into the ENCLOSING frame (hoist_stmt_temp), making
    // them real scope-tracked locals for the duration of this statement: an
    // early exit lowered INSIDE it (`if make().m() { return; }`) drops them via
    // the standard collect_all_drops / collect_drops_to_loop walks. They are
    // ERASED from the frame after the fall-through drops below, so the
    // enclosing block's own scope-exit drops never see them (no double drop).
    cur_stmt_temp_hoist_frame_ = scope_.empty() ? SIZE_MAX : scope_.size() - 1;
    auto* saved_ext = stmt_ext_hoist_;
    size_t saved_ext_frame = stmt_ext_hoist_frame_;
    stmt_ext_hoist_ = cur_stmt_temp_hoist_;
    stmt_ext_hoist_frame_ = cur_stmt_temp_hoist_frame_;
    auto saved_ret_bind = std::move(pending_ret_bind_);
    pending_ret_bind_.reset();
    lir_view::StmtRef s = lower_stmt_inner(stmt);
    cur_stmt_temp_hoist_ = saved_hoist;
    cur_stmt_temp_hoist_frame_ = saved_frame;
    stmt_ext_hoist_ = saved_ext;
    stmt_ext_hoist_frame_ = saved_ext_frame;
    auto ret_bind = std::move(pending_ret_bind_);
    pending_ret_bind_ = std::move(saved_ret_bind);
    if (hoisted.empty()) return s;
    // Wrap: `{ let __t0 = v0; …; <stmt>; drop __tN; … drop __t0; }`. The hoisted
    // temporaries are bound to fresh locals, the statement runs (borrowing them),
    // then their destructors run at the end of this statement — Rust's temporary
    // scope. Drops are emitted in REVERSE binding order, skipping a temp the
    // statement CONSUMED (moved). (The drop convention is explicit SDrop
    // statements inserted by sema — mlir-gen does not auto-drop block-scoped
    // locals — so we must emit them here.)
    std::vector<lir_view::StmtRef> blk;
    std::vector<lir_view::StmtRef> drops;
    for (auto& h : hoisted) {
        const std::string& nm = std::get<0>(h);
        TypeRef ty = std::get<1>(h);
        lir::SLet sl;
        sl.name = nm; sl.type = ty; sl.is_mut = std::get<3>(h);
        sl.value = std::move(std::get<2>(h));
        blk.push_back(make_stmt_emit(node_line_, std::move(sl)));
        // An EXTENDED temporary (`__lit_temp_N`, minted by hoist_block_temp /
        // autoref_block_temp or by lower_let) belongs to the enclosing BLOCK: it is
        // neither dropped at the end of this statement nor erased from the frame below,
        // so the block's own scope-exit drop runs it once.
        if (nm.rfind("__lit_temp_", 0) == 0) continue;
        if (!moved_vars_.count(nm))
            if (auto d = make_drop_stmt(nm, VarInfo{ty, false}))
                drops.push_back(std::move(*d));
    }
    if (ret_bind) {
        // `return <val>` whose value hoisted temps: bind the value (computed
        // while the temps live), then unwind the WHOLE scope — the temps AND
        // every outer live local (collect_all_drops; this SBlock wrap hides the
        // Return from lower_block's statement-level drop insertion, which would
        // otherwise leak the outer locals) — THEN return.
        lir::SLet rb;
        rb.name = std::get<0>(*ret_bind);
        rb.type = std::get<1>(*ret_bind);
        rb.is_mut = false;
        rb.value = std::get<2>(*ret_bind);
        blk.push_back(make_stmt_emit(node_line_, std::move(rb)));
        for (auto& d : collect_all_drops())
            blk.push_back(std::move(d));
        blk.push_back(std::move(s));   // `return __rv` — terminator last
    } else {
        blk.push_back(std::move(s));
        for (auto it = drops.rbegin(); it != drops.rend(); ++it)
            blk.push_back(std::move(*it));
    }
    // The temps are dead past this statement — remove them from the frame so
    // the enclosing block's scope-exit / later early-exit walks skip them.
    if (!scope_.empty()) {
        auto& fr = scope_.back();
        for (auto& h : hoisted) {
            const std::string& nm = std::get<0>(h);
            if (nm.rfind("__lit_temp_", 0) == 0) continue;  // extended: the block's, see above
            fr.vars.erase(nm);
            std::erase(fr.var_order, nm);
        }
    }
    lir::SBlock sb; sb.body = lir_mirror_block(*cur_prog_, blk);
    sb.transparent = true;  // TRANSPARENT: sema-synthesized wrapper (carried, see stmt_keys::TRANSPARENT)
    return make_stmt_emit(node_line_, std::move(sb));
}

// E0594's sentence for `*p = v` / `*p op= v` through a SHARED reference, in
// the borrow checker's wording for the same refusal.
std::string SemaChecker::shared_ref_write_msg(TinyMapView place) {
    if (!place.is_null() && code_of(place) == la::VAR_REF) {
        std::string n(str_of(place.get(la::NAME.code)));
        return std::format("cannot assign to '*{}': '{}' is behind a `&` reference", n, n);
    }
    return "cannot assign to a place behind a `&` reference";
}


lir_view::StmtRef SemaChecker::lower_stmt_inner(TinyMapView stmt) {
    node_line_ = get_line(stmt);
    node_span_ = get_span(stmt);
    int32_t c = code_of(stmt);

    if (c == la::LET)          return lower_let(stmt);
    if (c == la::LET_ELSE)     return lower_let_else(stmt);
    if (c == la::LET_PAT)      return lower_let_pat(stmt);
    if (c == la::NESTED_FN)    return lower_nested_fn(stmt);
    if (c == la::ASSIGN)          return lower_assign(stmt);
    if (c == la::DESTRUCTURE_ASSIGN) {   // a let + assignments by now (the HIR pass)
        hir_gate_(stmt);
        return builder().stmt_expr(error_expr(), node_line_);
    }
    if (c == la::COMPOUND_ASSIGN) return lower_compound_assign(stmt);
    if (c == la::RETURN)       return lower_return(stmt);
    if (c == la::IF && impl_tail_if_node_ && stmt.ptr() == impl_tail_if_node_) {
        impl_tail_if_node_ = nullptr;
        auto ret = synth_node(la::RETURN.code, node_line_, {{la::VALUE.code, impl_tail_if_av_}});
        return lower_return(map_of(ret));
    }
    if (c == la::IF)           return lower_if(stmt);
    if (c == la::IF_LET_CHAIN) {   // desugared by the HIR pass (hir_lower.cpp)
        hir_gate_(stmt);
        return builder().stmt_expr(error_expr(), node_line_);
    }
    if (c == la::LABELED_BLOCK) {   // a loop by now (the HIR pass)
        hir_gate_(stmt);
        return builder().stmt_expr(error_expr(), node_line_);
    }
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
    if (c == la::WHILE) {      // a LOOP by now (the HIR pass)
        hir_gate_(stmt);
        return builder().stmt_expr(error_expr(), node_line_);
    }
    if (c == la::FOR)          return lower_for(stmt);
    if (c == la::FOR_EACH)     return lower_for_each(stmt);
    if (c == la::LOOP)         return lower_loop(stmt);
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
        if (e && expr_type(e) && is_move_type(expr_type(e))) {
            namespace ec = lir_schema::expr;
            auto ek = expr_ref_of(e).kind();
            bool is_place = ek == ec::Code::VarRef || ek == ec::Code::FieldRead ||
                            ek == ec::Code::IndexRead || ek == ec::Code::Deref ||
                            ek == ec::Code::TupleIndex || ek == ec::Code::SliceIndex ||
                            ek == ec::Code::SlicePtr || ek == ec::Code::AddrOf ||
                            ek == ec::Code::AddrOfTemp;
            if (!is_place) {
                std::string synth = std::format("__stmt_tmp_{}", destruct_counter_++);
                if (auto drop = make_drop_stmt(synth, VarInfo{expr_type(e), false})) {
                    std::vector<lir_view::StmtRef> blk;
                    lir::SLet sl;
                    sl.name = synth; sl.type = expr_type(e); sl.is_mut = false;
                    sl.value = std::move(e);
                    blk.push_back(make_stmt_emit(node_line_, std::move(sl)));
                    blk.push_back(std::move(*drop));
                    lir::SBlock sb; sb.body = lir_mirror_block(*cur_prog_, blk);
                    sb.transparent = true;  // TRANSPARENT: sema-synthesized wrapper (carried, see stmt_keys::TRANSPARENT)
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
            auto vnode = map_of(stmt.get(la::VALUE.code));
            auto inner = lower_return_operand_(vnode);
            if (inner && expr_type(inner) &&
                TypeRef(expr_type(inner)).kind() != LogosType::Kind::Void &&
                TypeRef(expr_type(inner)).kind() != LogosType::Kind::Error) {
                return finish_return_(std::move(inner), vnode);   // the one return judgment
            }
            return builder().stmt_expr(std::move(inner), node_line_);
        }
        if (tail_as_return_ && ret_type_ &&
            TypeRef(ret_type_).kind() != LogosType::Kind::Void) {
            // The same judgment as `return e;` (ADR 0030 S2): one operand
            // lowering, one finish — coercion, variance, E0507, the moves.
            // A unit-typed tail is a statement, not the returned value.
            if (stmt.has_key(la::VALUE)) {
                auto vnode = map_of(stmt.get(la::VALUE.code));
                auto inner = lower_return_operand_(vnode);
                if (inner && expr_type(inner) &&
                    TypeRef(expr_type(inner)).kind() == LogosType::Kind::Void)
                    return builder().stmt_expr(std::move(inner), node_line_);
                return finish_return_(std::move(inner), vnode);
            }
            return lower_return(stmt);
        }
        lir::LExprPtr e = stmt.has_key(la::VALUE)
            ? lower_expr(map_of(stmt.get(la::VALUE.code)))
            : error_expr();
        return builder().stmt_expr(std::move(e), node_line_);
    }
    if (c == la::BREAK) {
        // The HIR pass resolves exits (hir_lower.cpp resolve_exit); these
        // checks stand for a fragment's exits, whose loops it cannot see.
        const bool refused = hir_origin_(stmt) == hir::Origin::ExitRefused;
        if (loop_depth_ == 0 && !refused) error("'break' outside loop");
        std::string break_label;
        if (stmt.has_key(la::LABEL))
            break_label = std::string(str_of(stmt.get(la::LABEL.code)));
        if (!break_label.empty() && !refused &&
            std::find(active_loop_labels_.begin(), active_loop_labels_.end(),
                      break_label) == active_loop_labels_.end()) {
            error(std::format("'break {}': label not in scope", break_label));
        }
        // Resolve the target frame: the matching label (search innermost-out)
        // or the innermost loop for an unlabeled break. The break value
        // attributes to the TARGET, so a value breaking to an outer labeled
        // `loop` isn't consumed by an inner `loop`.
        // An INDEX, not a pointer: lowering the value can push frames (a loop
        // inside it — a labeled block's value holds the block's whole body),
        // and the vector's reallocation left a pointer dangling.
        std::optional<size_t> target_ix;
        if (!loop_break_frames_.empty()) {
            if (break_label.empty()) {
                target_ix = loop_break_frames_.size() - 1;
            } else {
                for (size_t k = loop_break_frames_.size(); k-- > 0; )
                    if (loop_break_frames_[k].label == break_label) { target_ix = k; break; }
            }
        }
        lir::LExprPtr bval = nullptr;
        if (stmt.has_key(la::VALUE))
            bval = lower_expr(map_of(stmt.get(la::VALUE.code)));
        LoopBreakFrame* target = target_ix ? &loop_break_frames_[*target_ix] : nullptr;
        if (stmt.has_key(la::VALUE)) {
            // The break value MOVES into the loop's result (`break s`), so the
            // source's own drop — on this path's unwind and at its scope end —
            // must not run a second time.
            if (bval) mark_moved_expr(expr_ref_of(bval));
            if (target && target->expected && bval) cast_to_expected_dyn(bval, target->expected);
            if (target && target->no_value_kind) {
                error(std::format("`break` with value from a `{}` loop (E0571): only `loop` "
                                  "yields a value", target->no_value_kind));
            } else if (target && target->without_value) {
                error("loop break mixes value and no-value breaks");
            } else if (target && bval && expr_type(bval) &&
                       TypeRef(expr_type(bval)).kind() != LogosType::Kind::Error) {
                // The breaks are ONE type: their open inference variables
                // unify (`break None; … break Some(5)` — as the arms of an
                // `if` / `match` do). Keeping the first break's open type
                // sized the loop's slot wrong and the loop read `None` back.
                if (target->value_type && !infer_solved_.empty() &&
                    (has_infer_var_(target->value_type) || has_infer_var_(expr_type(bval)))) {
                    infer_unify_(target->value_type, expr_type(bval));
                    target->value_type = zonk_(target->value_type);
                    builder().retype_expr(bval, zonk_(expr_type(bval)));
                }
                if (!target->value_type) {
                    target->value_type = expr_type(bval);
                } else if (!types_compatible(expr_type(bval), target->value_type) &&
                           !types_compatible(target->value_type, expr_type(bval))) {
                    error(std::format("loop break values have incompatible types: {} vs {}",
                          type_str(target->value_type), type_str(expr_type(bval))));
                } else {
                    target->value_type = unify_numeric(target->value_type, expr_type(bval));
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
        const bool refused = hir_origin_(stmt) == hir::Origin::ExitRefused;
        if (loop_depth_ == 0 && !refused) error("'continue' outside loop");
        std::string cont_label;
        if (stmt.has_key(la::LABEL))
            cont_label = std::string(str_of(stmt.get(la::LABEL.code)));
        if (!cont_label.empty() && !refused &&
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
        // The operand is a place in a mutable-use position (`**bb += v`).
        auto ptr_node = map_of(stmt.get(la::NAME.code));
        // An operand that CALLS (`*cell.borrow_mut() += 1`, `*pick(&mut a) += 5`)
        // is evaluated ONCE, as Rust does: its final `&mut` goes into a statement
        // temporary read and written through. The read-twice desugar below ran
        // the call twice (a second `borrow_mut` panicked "already borrowed").
        const bool place_calls = ast_has_call(ptr_node);
        const bool eval_once = cur_stmt_temp_hoist_ && place_calls;
        auto rhs_node = map_of(stmt.get(la::VALUE.code));
        auto write_once = [&](lir::LExprPtr p, TypeRef el, lir::LExprPtr r,
                              const std::string& bop) -> lir_view::StmtRef {
            r = compound_rhs_first(std::move(r), el, rhs_node, true);
            TypeRef ptt = expr_type(p);
            std::string nm = std::format("__rtmp_{}", destruct_counter_++);
            register_stmt_temp(nm, ptt, std::move(p), false);
            auto cur = builder().deref(builder().var_ref(nm, ptt), el);
            auto bin = builder().bin_op(bop, std::move(cur), std::move(r), el);
            return builder().stmt_deref_write(builder().var_ref(nm, ptt), std::move(bin), node_line_);
        };
        auto ptr   = lower_mut_place(ptr_node);
        auto rhs   = lower_expr(map_of(stmt.get(la::VALUE.code)));
        auto op_tok = str_of(stmt.get(la::OP.code));
        std::string base_op = (op_tok.size() >= 2 && op_tok.back() == '=')
            ? std::string(op_tok.substr(0, op_tok.size() - 1))
            : std::string(op_tok);
        TypeRef pt = expr_type(ptr);
        TypeRef elem = TypeRef(pt).pointee();
        // User DerefMut dispatch: `*w op= v` for struct w with DerefMut<T> —
        // desugar to `*(w.deref_mut()) = *(w.deref_mut()) op v` through the
        // canonical emit_generic_deref_call (shape-aware multi-impl + generic
        // wrappers; deref_mut treated as side-effect-free, as in the rest of
        // the deref family). Adversarial #2 p06. A `Deref`-only step is not a
        // write place (E0594).
        if (TypeRef(pt).kind() == LogosType::Kind::Struct ||
            TypeRef(pt).kind() == LogosType::Kind::ZonedStruct) {
            bool deref_only = false;
            auto wcall = emit_generic_deref_call(std::move(ptr), /*want_mut=*/true,
                                                 &deref_only);
            if (wcall && deref_only) {
                refuse_deref_only(pt);
                return builder().stmt_expr(error_expr(), node_line_);
            }
            if (wcall && eval_once) {
                TypeRef tgt = TypeRef(expr_type(*wcall)).pointee();
                if (tgt) return write_once(std::move(*wcall), tgt, std::move(rhs), base_op);
            }
            if (wcall) {
                TypeRef tgt = TypeRef(expr_type(*wcall)).pointee();
                auto rcall = emit_generic_deref_call(lower_mut_place(ptr_node),
                                                     /*want_mut=*/true);
                if (rcall && tgt) {
                    auto cur_val = builder().deref(std::move(*rcall), tgt);
                    auto binop = builder().bin_op(base_op, std::move(cur_val),
                                                  std::move(rhs), tgt);
                    return builder().stmt_deref_write(std::move(*wcall),
                                                      std::move(binop), node_line_);
                }
            } else {
                ptr = lower_mut_place(ptr_node);
            }
        }
        if (TypeRef(pt).kind() == LogosType::Kind::Ref) {
            error(shared_ref_write_msg(ptr_node));
            return builder().stmt_expr(error_expr(), node_line_);
        }
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
        // `*r op= v` on a struct with `impl OpAssign` → `op_assign(r, v)`: the
        // operand once, then the RHS (Rust's order for a non-primitive).
        if (TypeRef(elem).kind() == LogosType::Kind::Struct) {
            std::string atrait, amethod;
            if (op_assign_trait_method(base_op, atrait, amethod)) {
                auto type_name = concrete_struct_name(elem);
                auto base_name = std::string(TypeRef(elem).struct_name());
                if (has_impl(atrait, type_name) ||
                    (!base_name.empty() && has_impl(atrait, base_name))) {
                    auto mangled = type_name + "__" + amethod;
                    TypeRef ref_t = make_ref(true, elem);
                    auto fit = find_op_assign_impl(mangled, ref_t, elem, rhs);
                    if (fit) {
                        if (rhs && is_move_type(expr_type(rhs)) &&
                            !(fit->param_types.size() == 2 && fit->param_types[1] &&
                              is_ref_like(TypeRef(fit->param_types[1]).kind())))
                            mark_moved_expr(expr_ref_of(rhs));
                        std::vector<lir::LExprPtr> args;
                        // `&mut *ptr`: a reborrow, so a `&mut` operand stays usable.
                        args.push_back(builder().addr_of_temp(builder().deref(std::move(ptr), elem),
                                                              /*is_mut=*/true, ref_t,
                                                              BorrowOrigin::CompoundAssign));
                        args.push_back(std::move(rhs));
                        auto call = builder().call(fit->symbol_name.empty() ? mangled : fit->symbol_name,
                                                   {}, std::move(args), fit->ret_type);
                        return builder().stmt_expr(std::move(call), node_line_);
                    }
                }
            }
        }
        if (eval_once) return write_once(std::move(ptr), elem, std::move(rhs), base_op);
        rhs = compound_rhs_first(std::move(rhs), elem, rhs_node, false);
        // Build *p (read) op rhs.  Need to read ptr twice — clone the var-ref
        // by re-lowering.
        auto ptr_again = lower_mut_place(ptr_node);
        auto cur_val   = builder().deref(std::move(ptr_again), elem);
        auto binop     = builder().bin_op(base_op, std::move(cur_val), std::move(rhs), elem);
        return builder().stmt_deref_write(std::move(ptr), std::move(binop), node_line_);
    }
    if (c == la::DEREF_WRITE) {
        // *ptr = value; — the operand is a place in a mutable-use position
        // (`**bb = v`, `*v[i] = v`), and a struct operand takes the SAME
        // generic `deref_mut` step as the read side (`lower_deref`) — the
        // `&mut T` it returns then flows through the common tail below
        // (unsafe / kind / variance / the T1.5 old-value drop / write-move).
        // A `Deref`-only step is not a write place (E0594).
        lir::LExprPtr ptr = stmt.has_key(la::NAME)
            ? lower_mut_place(map_of(stmt.get(la::NAME.code)))
            : error_expr();
        if (ptr && (TypeRef(expr_type(ptr)).kind() == LogosType::Kind::Struct ||
                    TypeRef(expr_type(ptr)).kind() == LogosType::Kind::ZonedStruct)) {
            TypeRef st = expr_type(ptr);
            bool deref_only = false;
            if (auto dc = emit_generic_deref_call(std::move(ptr), /*want_mut=*/true,
                                                  &deref_only)) {
                if (deref_only) {
                    refuse_deref_only(st);
                    return builder().stmt_expr(error_expr(), node_line_);
                }
                ptr = std::move(*dc);
            } else {
                ptr = lower_mut_place(map_of(stmt.get(la::NAME.code)));
            }
        }
        // SL-sl-03: propagate `*p = Option::None`-style RHS hints from the
        // pointee type, so a bare `None` resolves to `Option<i32>` rather
        // than dropping to a discriminant-only constant (which then gets
        // stored straight into the &mut slot — corrupting it).
        auto saved_enum_hint   = hint_enum_type_;
        auto saved_struct_hint = hint_struct_type_;
        if (ptr && TypeRef(expr_type(ptr)).pointee()) {
            TypeRef pe = TypeRef(expr_type(ptr)).pointee();
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
        auto pt = expr_type(ptr);
        // Writing through &mut T is safe; writing through raw *mut/*const T requires unsafe
        bool is_mut_ref = TypeRef(pt).kind() == LogosType::Kind::MutRef;
        if (TypeRef(pt).kind() == LogosType::Kind::Ref) {
            // E0594: a shared reference is not writable, in or out of `unsafe`.
            error(shared_ref_write_msg(map_of(stmt.get(la::NAME.code))));
        } else {
            if (!is_mut_ref && !inside_unsafe_)
                error("write through raw pointer requires unsafe context");
            if (TypeRef(pt).kind() != LogosType::Kind::Ptr && !is_mut_ref)
                error("deref-write: '=' left side must be a pointer or mutable reference");
        }
        // *const T is read-only; only *mut T or &mut T can be written through
        if (TypeRef(pt).kind() == LogosType::Kind::Ptr && !TypeRef(pt).mut_ptr())
            error("deref-write: cannot write through *const pointer (use *mut)");
        // B68: variance check at *ptr = val. The pointee must Inv-match the
        // value's type. Strict mode (fn-scope-fixed lifetimes).
        // A pointer read out of a local whose regions are INFERRED (`let h: H
        // = H { r: &mut out }; *h.r = y`) has regions sema pinned to the
        // initializer; the borrow checker infers them (#465).
        bool ptr_in_inferred_local = false;
        {
            using C = lir_schema::expr::Code;
            auto r = expr_ref_of(ptr);
            while (r && (r.kind() == C::FieldRead || r.kind() == C::TupleIndex))
                r = r.kind() == C::FieldRead ? lir_view::EFieldReadView{r}.receiver()
                                             : lir_view::ETupleIndexView{r}.receiver();
            if (r && r.kind() == C::VarRef)
                if (auto* vi = lookup_var_info(lir_view::EVarRefView{r}.name()))
                    ptr_in_inferred_local = vi->regions_inferred;
        }
        if (val && TypeRef(pt).pointee() && !ptr_in_inferred_local)
            check_variance(expr_type(val), TypeRef(pt).pointee(),
                           "deref-write '*ptr = …'", /*permissive=*/false);
        // T1.5 (whole-referent form): `*r = new` through a `&mut` overwrites a
        // LIVE value exactly as `(*r).f = new` overwrites a live field, and
        // the old value must drop HERE for the same reason the field arm
        // gives — the owner drops the NEW value at ITS scope end and never
        // sees the old one. Without this, rebuild-and-swap through `&mut self`
        // (`*self = fresh`) leaked the entire previous value, silently, with a
        // clean build (found 07-20 by PdtHolder's growth path: 45KB across one
        // gate run).
        //
        // The reference/raw split is the convention the field arm already
        // documents: a `&mut` referent is fully initialised by construction
        // and cannot be moved out of, so its old value is live; a raw
        // `*mut`/`*const` stays MANUAL, because writing into uninitialised
        // memory is the whole point of a raw pointer and an implicit drop of
        // whatever bytes were there would be wrong.
        bool drop_old_referent = is_mut_ref && TypeRef(pt).pointee() &&
            (TypeRef(TypeRef(pt).pointee()).owning_trait_object() ||
             !drop_fn_for(TypeRef(pt).pointee()).empty() ||
             has_droppable_fields(TypeRef(pt).pointee()));
        track_write_move(val);
        return builder().stmt_deref_write(std::move(ptr), std::move(val),
                                          node_line_, drop_old_referent);
    }
    if (c == la::UNSAFE_BLOCK) {
        bool was = inside_unsafe_;
        inside_unsafe_ = true;
        lir_view::BlockRef inner = stmt.has_key(la::BODY)
            ? lower_block(map_of(stmt.get(la::BODY.code)))
            : lir_mirror_block(*cur_prog_, {});
        inside_unsafe_ = was;
        return make_stmt_emit(node_line_, lir::SBlock{inner});
    }
    if (c == la::BLOCK_STMT) {
        lir_view::BlockRef inner = stmt.has_key(la::BODY)
            ? lower_block(map_of(stmt.get(la::BODY.code)))
            : lir_mirror_block(*cur_prog_, {});
        return make_stmt_emit(node_line_, lir::SBlock{inner});
    }
    // Unknown stmt — emit dummy expr stmt
    return builder().stmt_expr(error_expr(), node_line_);
}

lir_view::BlockRef SemaChecker::lower_block(TinyMapView block) {
    std::vector<lir_view::StmtRef> result;
    push_scope();
    scope_.back().block_frame = true;   // #118: can host drop-flag `let`s
    // G167-4: if a loop just armed this, tag the body frame as the loop
    // boundary for break/continue drop-glue (consume the one-shot flag).
    if (pending_loop_body_scope_) {
        scope_.back().loop_boundary = true;
        pending_loop_body_scope_ = false;
    }
    if (pending_loop_body_init_) {
        auto init = std::move(pending_loop_body_init_);
        pending_loop_body_init_ = nullptr;
        init();
    }
    bool warned_dead = false;  // Sprint 5.2: B-st-08 dead-code-after-terminator lint
    if (block.has_key(la::ITEMS)) {
        auto stmts = cfg_live_entries_(arr_of(block.get(la::ITEMS.code)));
        for (uint64_t i = 0; i < stmts.size(); ++i) {
            auto s = stmts[i];
            if (s.is_null()) continue;
            // Skip already-lowered drops/markers; the AST-level dead-code check
            // looks at the pre-lowering AST shape.
            if (!warned_dead) {
                int32_t pc = code_of(s);
                // Diagnose only when the previous lowered stmt was a hard
                // terminator (Return/Break/Continue) — Panic doesn't have a
                // dedicated AST code; skip those.
                if (i > 0 && !result.empty()) {
                    auto prev_ref = stmt_ref_of(result.back());
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
            // #118 — splice in any conditional-move drop flags this statement
            // armed for THIS frame's locals. The declaration must precede
            // every read (the guarded drop at scope exit) and every write (the
            // clear inside a branch of the statement just lowered), which is
            // exactly here: immediately before the statement is appended.
            // Entries addressed to an OUTER frame stay queued until that
            // frame's own lower_block reaches this point.
            if (!pending_frame_lets_.empty()) {
                size_t here = scope_.size() - 1;
                for (auto it = pending_frame_lets_.begin();
                     it != pending_frame_lets_.end(); ) {
                    if (it->first == here) {
                        result.push_back(it->second);
                        it = pending_frame_lets_.erase(it);
                    } else ++it;
                }
            }
            // Insert drops before return/break/continue — the ONE unwind
            // routine, shared with every block builder in expression position
            // (#122).
            push_stmt_with_unwind(result, std::move(lowered));
        }
    }
    // Insert drops for normal block exit (no return/break/continue)
    bool ends_with_terminator = false;
    if (!result.empty()) {
        auto br = stmt_ref_of(result.back());
        if (br) {
            auto k = br.kind();
            ends_with_terminator = (k == lir_schema::stmt::Code::Return ||
                                    k == lir_schema::stmt::Code::Break ||
                                    k == lir_schema::stmt::Code::Continue);
        }
    }
    if (!ends_with_terminator) {
        for (auto& d : collect_drops())
            result.push_back(std::move(d));
    }
    pop_scope();
    return lir_mirror_block(*cur_prog_, result);
}

// Emit `return <val>` with the FULL scope unwind — the ONE return-drop
// sequence, shared with lower_block's Return-statement handling above: bind
// the value FIRST (it may read/move locals the drops release), then
// collect_all_drops (innermost frame outward, stopping at a closure
// boundary), then the terminator. Desugars that synthesize an early return in
// EXPRESSION position (`?`) must route through this — lower_block's
// statement-level drop insertion never sees their buried Return.
// #122 — see the header comment on the declaration.
// ⚠ Called ONLY for an arm its caller has already judged diverging — the
// answer is 1 vs 2, never 0. A non-diverging arm's block can end in a Drop
// (lower_block's fall-through glue), so this predicate is not a divergence
// TEST and must not be used as one.
int SemaChecker::expr_arm_div_kind(const lir::LExprPtr& v) const {
    if (!v) return 1;
    // The terminator sits in the arm's block-expr statement list (both
    // spellings land there: `{ break; }` through lower_block_last_expr's stmt
    // arm, a bare `break` through BREAK_EXPR's block+sentinel lowering).
    if (v.kind() != lir_schema::expr::Code::BlockExpr) return 1;
    auto b = lir_view::EBlockExprView{v}.block();
    if (!b) return 1;
    int kind = 1;
    b.each_stmt([&](lir_view::StmtRef s) {
        if (!s) return;
        auto k = s.kind();
        if (k == lir_schema::stmt::Code::Return) kind = 1;
        else if (k == lir_schema::stmt::Code::Break ||
                 k == lir_schema::stmt::Code::Continue) kind = 2;
    });
    return kind;
}

// #122 — see the header comment on the declaration. Return's value expression
// MUST be evaluated BEFORE the drops (it may borrow variables that the drops
// would release): hoist it into a temporary, then drop, then return the temp.
void SemaChecker::push_stmt_with_unwind(std::vector<lir_view::StmtRef>& out,
                                        lir_view::StmtRef lowered) {
    auto sref = stmt_ref_of(lowered);
    if (sref && sref.kind() == lir_schema::stmt::Code::Return) {
        auto drops = collect_all_drops();
        auto val_ref = lir_view::SReturnView{sref}.value();
        if (!drops.empty() && val_ref) {
            TypeRef rt = val_ref.type(cur_prog_->type_pool.impl());
            std::string tmp = "__ret_tmp_" + std::to_string(tmp_var_count_++);
            lir::SLet sl;
            sl.name = tmp; sl.type = rt; sl.is_mut = false;
            sl.value = val_ref;
            out.push_back(make_stmt_emit(node_line_, std::move(sl)));
            for (auto& d : drops)
                out.push_back(std::move(d));
            out.push_back(
                builder().stmt_return(builder().var_ref(tmp, rt), node_line_));
            return;
        }
        for (auto& d : drops)
            out.push_back(std::move(d));
    } else if (sref && (sref.kind() == lir_schema::stmt::Code::Break ||
                        sref.kind() == lir_schema::stmt::Code::Continue)) {
        // G167-4: drop every frame down to AND INCLUDING the loop body — a
        // break/continue nested in an `if` exits via the loop edge, bypassing
        // the body block's normal end drops. A LABELED one leaves an OUTER
        // loop: every inner loop body it passes through is left too, so the
        // walk crosses one loop-body frame per enclosing loop below the target
        // (`'a: loop { let s = …; loop { continue 'a; } }` leaked `s`).
        std::string_view lbl = sref.kind() == lir_schema::stmt::Code::Break
            ? lir_view::SBreakView{sref}.label() : lir_view::SContinueView{sref}.label();
        size_t cross = 0;
        if (!lbl.empty())
            for (size_t k = loop_break_frames_.size(); k-- > 0; ++cross)
                if (loop_break_frames_[k].label == lbl) break;
        if (cross >= loop_break_frames_.size()) cross = 0;   // unknown label: diagnosed elsewhere
        for (auto& d : collect_drops_to_loop(cross))
            out.push_back(std::move(d));
    }
    out.push_back(std::move(lowered));
}

std::vector<lir_view::StmtRef> SemaChecker::make_return_with_drops(lir::LExprPtr val) {
    std::vector<lir_view::StmtRef> out;
    auto drops = collect_all_drops();
    if (drops.empty() || !val) {
        for (auto& d : drops)
            out.push_back(std::move(d));
        out.push_back(builder().stmt_return(std::move(val), node_line_));
        return out;
    }
    TypeRef rt = expr_type(val);
    std::string tmp = "__ret_tmp_" + std::to_string(tmp_var_count_++);
    lir::SLet sl;
    sl.name = tmp;
    sl.type = rt;
    sl.is_mut = false;
    sl.value = std::move(val);
    out.push_back(make_stmt_emit(node_line_, std::move(sl)));
    for (auto& d : drops)
        out.push_back(std::move(d));
    out.push_back(builder().stmt_return(builder().var_ref(tmp, rt), node_line_));
    return out;
}

// G149-7 (RFC 2909): destructuring assignment into EXISTING places.
//   (a, b) = e;          →  let __da = e; a = __da.0; b = __da.1;
//   [a, b] = e;          →  let __da = e; a = __da[0]; b = __da[1];
//   S { x: a, y } = e;   →  let __da = e; a = __da.x; y = __da.y;
// `_` places discard (evaluate the accessor for effect). Nested tuple places
// (`(a, (b, c)) = …`) recurse. Each place must be an existing mutable local
// (reuses lower_assign's mutability/undefined checks via stmt_assign).

// Sprint 4.2 — B-pt-02: irrefutable struct destructure in `let`.
//   let Foo { x, y } = expr;          →  let __dst = expr; let x = __dst.x; let y = __dst.y;
//   let Foo { x: a, y: b } = expr;    same with rebinding
// Other pattern shapes (variant, tuple-via-pat_single, slice, …) are
// rejected with a clear diagnostic — they're refutable or need full
// match lowering, which we layer on top of this basic destructure path
// in a later sprint.
lir_view::StmtRef SemaChecker::lower_let_pat(TinyMapView node) {
    // `let PAT: T = e`: the annotation types the matched VALUE. It hints the
    // rhs as `let x: T = e` does (tuple elements, generic literals, the
    // expected type) and the rhs must coerce to it.
    TypeRef ann = node.has_key(la::TYPE) ? resolve_type(map_of(node.get(la::TYPE.code))) : TypeRef(nullptr);
    const bool ann_hint = ann && TypeRef(ann).kind() != LogosType::Kind::Error && !type_has_inferred(ann);
    auto saved_tuple = hint_tuple_type_;
    auto saved_expected = hint_expected_type_;
    auto saved_ret = hint_call_return_type_;
    auto saved_struct = hint_struct_type_;
    auto saved_enum = hint_enum_type_;
    if (!ann_hint) hint_expected_type_ = nullptr;   // see lower_let
    if (ann_hint) {
        hint_expected_type_ = ann;
        hint_call_return_type_ = ann;
        if (TypeRef(ann).kind() == LogosType::Kind::Tuple) hint_tuple_type_ = ann;
        if (TypeRef(ann).kind() == LogosType::Kind::Struct && !TypeRef(ann).type_args().empty())
            hint_struct_type_ = ann;
        if (TypeRef(ann).kind() == LogosType::Kind::Enum && !TypeRef(ann).type_args().empty())
            hint_enum_type_ = ann;
    }
    lir::LExprPtr rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    hint_tuple_type_ = saved_tuple;
    hint_expected_type_ = saved_expected;
    hint_call_return_type_ = saved_ret;
    hint_struct_type_ = saved_struct;
    hint_enum_type_ = saved_enum;
    TypeRef rhs_type = expr_type(rhs);
    // The binders take the ANNOTATION's types (a `(i64, i64)` over an rhs of
    // unsuffixed literals binds i64s).
    if (ann_hint && expect_type(rhs, ann, CoercePos::LetInit, "let pattern: type mismatch —")) {
        if (expr_type(rhs) != ann) builder().retype_expr(rhs, ann);
        rhs_type = ann;
    }
    if (!node.has_key(la::PAT)) {
        error("internal: LET_PAT missing PAT");
        return builder().stmt_expr(std::move(rhs), node_line_);
    }
    auto pat_av = node.get(la::PAT.code);
    if (pat_av.is_null() || !pat_av.is_pointer()) {
        error("internal: LET_PAT PAT not a node");
        return builder().stmt_expr(std::move(rhs), node_line_);
    }
    return lower_let_pat_rhs(map_of(pat_av), std::move(rhs), rhs_type);
}

// `let PAT = <rhs>` over an already-lowered rhs: the `let` statement and the
// `for PAT in …` header (its element variable is the rhs) share it.
lir_view::StmtRef SemaChecker::lower_let_pat_rhs(TinyMapView pat_node, lir::LExprPtr rhs,
                                                 TypeRef rhs_type) {
    // ── `let n @ SUB = e` IS `let n = e;` FOLLOWED BY `let SUB = n;` ──────
    // A DELEGATION, not a fifth branch in the shape whitelist: bind the name,
    // then hand SUB to the same lowering with that name as its source. The loop
    // is a loop because `n @ m @ SUB` is a pattern too. Spec
    // pat.at.binds-at-every-position.
    std::vector<lir_view::StmtRef> at_pre;
    while (code_of(pat_node) == la::PAT_AT && pat_node.has_key(la::VALUE)) {
        std::string an(str_of(pat_node.get(la::NAME.code)));
        bool amut = pat_byval_mut(pat_node);
        if (!an.empty() && an != "_") {
            // ⚠ The name TAKES the value: mark the source place moved or its
            // scope-exit drop runs a SECOND time on storage this binding owns.
            // mark_moved_expr self-gates to VarRef/FieldRead/TupleIndex.
            if (is_move_type(rhs_type)) mark_moved_expr(expr_ref_of(rhs));
            define(an, rhs_type, amut);
            lir::SLet sl;
            sl.name = an; sl.type = rhs_type; sl.is_mut = amut;
            sl.value = std::move(rhs);
            at_pre.push_back(make_stmt_emit(node_line_, std::move(sl)));
            rhs = builder().var_ref(an, rhs_type);
        }
        pat_node = map_of(pat_node.get(la::VALUE.code));
    }
    if (!at_pre.empty()) {
        // A wildcard sub is fully discharged by the bindings above; anything
        // else is a let-pattern over the name we just bound.
        bool sub_binds_nothing =
            code_of(pat_node) == la::PAT_WILD &&
            (!pat_node.has_key(la::NAME) ||
             std::string(str_of(pat_node.get(la::NAME.code))) == "_" ||
             std::string(str_of(pat_node.get(la::NAME.code))).empty());
        if (!sub_binds_nothing)
            at_pre.push_back(lower_let_pat_bound(pat_node, std::move(rhs), rhs_type));
        lir::SBlock sb;
        sb.transparent = true;  // TRANSPARENT: sema-synthesized wrapper
        sb.body = lir_mirror_block(*cur_prog_, at_pre);
        return make_stmt_emit(node_line_, std::move(sb));
    }
    // A TEMPORARY rhs (not a place) of a droppable type, under any pattern: bind
    // from a synth local and drop what the pattern did not take at the END OF THE
    // STATEMENT, as Rust drops a temporary (`let (d, _) = (mk(4), mk(5));`
    // drops the 5 before the next statement). Nothing owned it before: the
    // parts a pattern skipped leaked. A pattern with a `ref` binder extends
    // the temporary to the block instead (Rust's temporary lifetime
    // extension), so it keeps the synth local's block-end drop.
    if (rhs && rhs_type && is_move_type(rhs_type) &&
        !lir_view::is_place_expr(expr_ref_of(rhs))) {
        std::string tmp = std::format("__let_tmp_{}", tmp_var_count_++);
        define(tmp, rhs_type, /*is_mut=*/true);
        std::vector<lir_view::StmtRef> blk;
        {
            lir::SLet sl; sl.name = tmp; sl.type = rhs_type; sl.is_mut = true; sl.value = std::move(rhs);
            blk.push_back(make_stmt_emit(node_line_, std::move(sl)));
        }
        blk.push_back(lower_let_pat_bound(pat_node, builder().var_ref(tmp, rhs_type), rhs_type));
        if (!ast_pattern_has_ref_binder(pat_node)) {
            if (auto it = scope_.back().vars.find(tmp); it != scope_.back().vars.end())
                if (auto d = make_drop_stmt(tmp, it->second)) blk.push_back(*d);
            mark_moved(tmp);
        }
        lir::SBlock sb;
        sb.transparent = true;
        sb.body = lir_mirror_block(*cur_prog_, blk);
        return make_stmt_emit(node_line_, std::move(sb));
    }
    return lower_let_pat_bound(pat_node, std::move(rhs), rhs_type);
}

// Does a pattern (AST) contain a `ref` / `ref mut` binder anywhere?
bool SemaChecker::ast_pattern_has_ref_binder(TinyMapView n) {
    if (n.is_null()) return false;
    if (n.has_key(la::IS_REF) && n.get(la::IS_REF.code).is_value() &&
        n.get(la::IS_REF.code).as_value<uint8_t>() != 0)
        return true;
    for (uint8_t key : {la::ITEMS.code, la::ARGS.code, la::NAMES.code}) {
        if (!n.has_key(key)) continue;
        auto av = n.get(key);
        if (av.is_null() || !av.is_pointer()) continue;
        auto w = map_of(av);
        ArrayView items = (!w.is_null() && w.has_key(la::ITEMS)) ? arr_of(w.get(la::ITEMS.code)) : arr_of(av);
        for (uint64_t i = 0; i < items.size(); ++i)
            if (ast_pattern_has_ref_binder(map_of(items.get(i)))) return true;
    }
    if (n.has_key(la::VALUE) && n.get(la::VALUE.code).is_pointer())
        return ast_pattern_has_ref_binder(map_of(n.get(la::VALUE.code)));
    return false;
}

// `let PAT = rhs` (ADR 0030 S3.4c): ONE lowering, the match core's pattern and
// binding phase — lower_let_else_core with an unreachable else, the bindings in
// the enclosing scope. A refutable pattern is E0005, as rustc says.
lir_view::StmtRef SemaChecker::lower_let_pat_bound(TinyMapView pat_node,
                                                   lir::LExprPtr rhs,
                                                   TypeRef rhs_type) {
    auto error_count = [&] {
        size_t n = 0;
        for (auto& d : result_.diags) n += d.level == Diag::Level::Error;
        return n;
    };
    const size_t errs_before = error_count();
    lir::Pattern probe = build_pattern(pat_node, rhs_type);
    // A pattern already refused while it was built (an array of the wrong
    // length, an unknown field) is not "refutable" on top of it: one error,
    // as rustc gives one.
    if (error_count() > errs_before) {
        bind_pattern_ref(pat_ref_of(probe), rhs_type);
        return builder().stmt_expr(std::move(rhs), node_line_);
    }
    if (!let_pattern_irrefutable_(pat_node, pat_ref_of(probe), rhs_type))
        return refuse_refutable_let(probe, std::move(rhs), rhs_type);
    return lower_let_else_core(std::move(rhs), pat_node, TinyMapView{});
}

// E0005: a refutable pattern where an irrefutable one is required. The names
// are still defined (so the body does not cascade into "undefined variable");
// the rhs is kept for its effects.
lir_view::StmtRef SemaChecker::refuse_refutable_let(lir::Pattern& probe, lir::LExprPtr rhs,
                                                     TypeRef rhs_type) {
    if (let_pat_site_)
        error(std::format("refutable pattern in {}: match the value in the body", let_pat_site_));
    else
        error("refutable pattern in local binding: use `let … else { … }` or `match`");
    bind_pattern_ref(pat_ref_of(probe), rhs_type);
    return builder().stmt_expr(std::move(rhs), node_line_);
}

// Is the built pattern IRREFUTABLE against a value of type `ty`? Structural: a
// binder / wildcard, and tuples, structs, `&` patterns, `@`, and arrays of the
// right length whose parts are all irrefutable. A variant, a literal, a range
// or an or-pattern is refutable here (a one-variant enum is too rare to model).
// ADR 0030 S3 (C-PAT): a `let` pattern is irrefutable iff it covers its type —
// the usefulness matrix's verdict (`let W::V(x) = w` over a one-variant enum,
// `let 0..=255u8 = b`, `let Ok(v) = r` with an uninhabited error are
// irrefutable, as in rustc). The pattern-kind test answers only where the
// matrix could not decide.
bool SemaChecker::let_pattern_irrefutable_(TinyMapView pat, lir_view::PatRef probe, TypeRef ty) {
    bool decided = false;
    if (ast_patterns_exhaustive({pat}, ty, &decided)) return true;
    if (decided) return false;
    return pattern_irrefutable(probe, ty);
}

bool SemaChecker::pattern_irrefutable(lir_view::PatRef p, TypeRef ty) {
    namespace ps = lir_schema::pat;
    if (!p) return true;
    TypeRef t = ty;
    while (t && (TypeRef(t).kind() == LogosType::Kind::Ref || TypeRef(t).kind() == LogosType::Kind::MutRef) &&
           TypeRef(t).pointee())
        t = TypeRef(t).pointee();
    switch (p.kind()) {
        case ps::Code::Wild: case ps::Code::RefBind: return true;
        case ps::Code::At: return pattern_irrefutable(lir_view::PatAtView{p}.sub(), ty);
        case ps::Code::RefPat: return pattern_irrefutable(lir_view::PatRefPatView{p}.inner(), ty);
        case ps::Code::Tuple: {
            bool ok = true; size_t i = 0;
            auto elems = t ? TypeRef(t).tuple_elems() : std::vector<TypeRef>{};
            lir_view::PatTupleView{p}.each_sub([&](lir_view::PatRef sp) {
                TypeRef et = i < elems.size() ? elems[i] : TypeRef(nullptr);
                ++i;
                if (sp && !pattern_irrefutable(sp, et)) ok = false;
            });
            return ok;
        }
        case ps::Code::Struct: {
            // Each field's sub-pattern against the FIELD's type: an array
            // sub-pattern is irrefutable only against an array of its length.
            const SemaStructInfo* si = nullptr;
            if (t && TypeRef(t).kind() == LogosType::Kind::Struct)
                si = find_struct_by_name(std::string(TypeRef(t).struct_name())).second;
            bool ok = true;
            lir_view::PatStructView{p}.each_field([&](lir_view::PatFieldBindingView f) {
                TypeRef ft;
                if (si)
                    for (auto& sf : si->fields)
                        if (sf.name == f.field_name()) { ft = sf.type; break; }
                if (auto sp = f.sub(); sp && !pattern_irrefutable(sp, ft)) ok = false;
            });
            return ok;
        }
        case ps::Code::Slice: {
            if (!t || TypeRef(t).kind() != LogosType::Kind::Array) return false;
            lir_view::PatSliceView sv{p};
            const uint64_t n = sv.prefix_count() + sv.suffix_count();
            if (!sv.rest() && n != (uint64_t)TypeRef(t).arr_size()) return false;
            bool ok = true;
            auto chk = [&](lir_view::PatRef sp) { if (sp && !pattern_irrefutable(sp, TypeRef(t).elem())) ok = false; };
            sv.each_prefix(chk); sv.each_suffix(chk);
            return ok;
        }
        default: return false;
    }
}

lir_view::StmtRef SemaChecker::lower_let_else(TinyMapView node) {
    lir::LExprPtr scrut = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code)))
        : error_expr();
    return lower_let_else_core(std::move(scrut), map_of(node.get(la::PAT.code)),
                               node.has_key(la::BODY) ? map_of(node.get(la::BODY.code)) : TinyMapView{});
}

// The let-else lowering over an already-lowered scrutinee. A null `else_node`
// is an IRREFUTABLE `let PAT = e;` routed here: its else is `{ loop {} }`,
// unreachable, and the pattern binds into the enclosing scope exactly as a
// let-else's does.
lir_view::StmtRef SemaChecker::lower_let_else_core(lir::LExprPtr scrut, TinyMapView pat_node_in,
                                                   TinyMapView else_node) {
    // let Pat = expr else { block };
    // The pattern's bindings go into the outer scope after this statement.
    // Lowering:
    //   1. Lower scrutinee expression.
    //   2. Build pattern (determine bindings and their types).
    //   3. Lower else block in a nested scope (must diverge).
    //   4. Add pattern bindings to outer scope.
    //   5. Emit SLetElse { pat, scrut, else_block }.

    // 1. The scrutinee arrives lowered.
    TypeRef scrut_type = expr_type(scrut);

    // 2. Build pattern (this also validates binding types)
    auto pat_node = pat_node_in;
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
    logos::compiler::StrSet mut_names;  // `let (mut a, b) = … else`: the side-set the match arms use
    auto* saved_pat_muts = current_pat_mut_names_;
    current_pat_mut_names_ = &mut_names;
    lir::Pattern pat = build_pattern(pat_node, scrut_type);
    current_pat_refutable_guards_ = saved_pat_refut;

    // 3. Lower else block in nested scope (must diverge — closes B-st-03).
    push_scope();
    lir_view::BlockRef else_blk;
    if (else_node.is_null()) {
        auto spin = synth_block({synth_node(la::LOOP.code, node_line_,
                                            {{la::BODY.code, synth_block({}, node_line_)}})},
                                node_line_);
        else_blk = lower_block(map_of(spin));
    } else {
        auto body_node = else_node;
        if (!block_always_diverts(body_node)) {
            error("'let-else' else-block must diverge "
                  "(end in 'return', 'break', 'continue', 'panic', or 'loop {}')");
        }
        else_blk = lower_block(body_node);
    }
    pop_scope();

    // The pattern's bindings OWN what they bind by value: the scrutinee (var or
    // place) is marked moved so its scope-exit drop does not fire a second time
    // over a payload a binding already dropped. After the else block, which
    // may still read the scrutinee (no move happened on that path).
    // Exact: the else path diverges, so the pattern's variant is the tag here.
    mark_match_scrutinee_moved(scrut, scrut_type, pat_ref_of(pat), /*variant_exact=*/true);
    exact_variant_moves_.clear();   // the only continuing path moved it: already static

    // 4. Add pattern bindings to outer scope
    {
        namespace ps = lir_schema::pat;
        auto* pool = cur_prog_->type_pool.impl();
        std::function<void(lir_view::PatRef)> define_bindings =
            [&](lir_view::PatRef pr) {
            if (pr.kind() == ps::Code::VariantData) {
                // The match arms' definer: it also reaches the payload
                // SUB-PATTERNS' names (ADR 0030 S3) — this door kept a private
                // copy that saw BINDINGS only (`let Some(&x) = … else` left x
                // undefined).
                bind_pattern_ref(pr, scrut_type);
            } else if (pr.kind() == ps::Code::Tuple) {
                lir_view::PatTupleView v{pr};
                std::vector<std::string_view> names;
                std::vector<TypeRef> types;
                v.each_binding([&](std::string_view n) { names.push_back(n); });
                v.each_binding_type(pool, [&](TypeRef t) { types.push_back(t); });
                auto _tp_slots = v.bind_slots();  // Phase-1: reuse reserved slots
                for (size_t i = 0; i < names.size() && i < types.size(); ++i)
                    if (names[i] != "_")
                        define(std::string(names[i]), types[i], pat_mut_name(names[i]),
                               i < _tp_slots.size() ? _tp_slots[i] : 0xFFFFFFFFu);
            } else if (pr.kind() == ps::Code::Wild) {
                lir_view::PatWildView v{pr};
                auto n = v.name();
                if (n != "_")
                    define(std::string(n), scrut_type, pat_mut_name(n), v.bind_slot());  // Phase-1
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
        // An irrefutable `let PAT = e` routed here (no written else) may nest any
        // binding sub-pattern (`[W { a: x, .. }]`, `w @ (a, b)`): the match
        // arms' full definer reaches every kind.
        // …and so may a written let-else over a STRUCTURAL pattern
        // (`let (Some(x), y) = t else { … }`): codegen's structural let-else
        // path binds every nested name through pat_bind.
        auto pk_ = pat_ref_of(pat) ? pat_ref_of(pat).kind() : ps::Code(-1);
        const bool structural = pk_ == ps::Code::Tuple || pk_ == ps::Code::Struct ||
                                pk_ == ps::Code::Slice || pk_ == ps::Code::At || pk_ == ps::Code::RefPat;
        if (else_node.is_null() || structural) bind_pattern_ref(pat_ref_of(pat), scrut_type);
        else define_bindings(pat_ref_of(pat));
    }
    current_pat_mut_names_ = saved_pat_muts;

    // 5. Emit SLetElse
    lir::SLetElse sle;
    sle.pat        = std::move(pat);
    sle.scrut      = std::move(scrut);
    sle.else_block = else_blk;
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
lir_view::StmtRef SemaChecker::lower_nested_fn(TinyMapView node) {
    auto name = std::string(str_of(node.get(la::NAME.code)));
    auto value = lower_closure_expr(node);
    auto var_type = value ? expr_type(value) : error_t();
    define(name, var_type, /*is_mut=*/false);
    lir::SLet sl;
    sl.name   = name;
    sl.type   = var_type;
    sl.is_mut = false;
    sl.value  = std::move(value);
    return make_stmt_emit(node_line_, std::move(sl));
}

lir_view::StmtRef SemaChecker::lower_let(TinyMapView node) {
    // Rust temporary lifetime extension: mark the borrows in this initializer
    // that bind temporaries which must outlive the statement, BEFORE lowering
    // it (the two unary-& sites consult the mark). See mark_extending_borrows.
    if (node.has_key(la::VALUE))
        mark_extending_borrows(map_of(node.get(la::VALUE.code)));
    auto name = str_of(node.get(la::NAME.code));
    bool is_mut = false;
    if (node.has_key(la::IS_MUT)) {
        AnyVal av = node.get(la::IS_MUT.code);
        if (!av.is_null() && av.is_value()) is_mut = av.as_value<uint8_t>() != 0;
    }

    // Parse type annotation first so we can use it as a hint for enum literal inference
    TypeRef ann = nullptr;
    bool ann_is_box_dyn = false;  // `Box<dyn T>` collapses to a bare TraitObject
                                  // in resolve_type, but it is OWNING (heap
                                  // handle) — record so its drop runs.
    let_annot_names_lifetime_ = false;
    if (node.has_key(la::TYPE)) {
        auto tnode = map_of(node.get(la::TYPE.code));
        ann = resolve_type(tnode);
        if (ann) check_written_type_wf(ann, "let annotation", current_outlives_, /*decl_site=*/false);
        // E0277: A LOCAL MUST BE `Sized`. `let y: dyn Tr = *x;` and `let v: T = …`
        // with `T: ?Sized` bind an unsized value (rustc refuses both). The stdlib
        // keeps the idiom internally — `Box`'s drop and `ptr::drop_in_place` move
        // an unsized pointee out through its fat pointer and let its own glue
        // destroy it — the way Rust's own `unsized_locals` is internal-only.
        if (ann && !cur_package_.starts_with("logos.")) {
            // A BARE `dyn`: the grammar folds `&dyn` / `&mut dyn` into the same
            // node and marks them IS_REF.
            auto is_ref_flag = [&]() {
                if (!tnode.has_key(la::IS_REF)) return false;
                AnyVal av = tnode.get(la::IS_REF.code);
                return !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
            };
            bool unsized = code_of(tnode) == la::DYN_TYPE && !is_ref_flag();
            // KEY-IDENTITY: a TYPE-PARAMETER name, scoped to the signature being
            // checked — see SemaChecker::normalize_assoc_eq for the full ground.
            if (!unsized && TypeRef(ann).kind() == LogosType::Kind::TypeVar &&
                current_type_relaxed_sized_.count(std::string(TypeRef(ann).type_var_name())))
                unsized = true;
            if (unsized) {
                std::string ts = type_str(ann);
                if (code_of(tnode) == la::DYN_TYPE && ts.starts_with("&")) ts.erase(0, 1);   // the written `dyn Tr`
                error(std::format("the size for values of type `{}` cannot be known at compilation time: "
                                  "a local variable must have a statically known size (bind a reference, or box it)",
                                  ts));
            }
        }
        // Did the annotation WRITE a lifetime? Asked of the resolved
        // annotation, before inference touches the binding's type.
        {
            std::function<bool(TypeRef, int)> names_lt = [&](TypeRef t, int d) -> bool {
                if (!t || d > 12) return false;
                // `'static` COUNTS: it is written, and a loan of a local stored
                // under it must live for ever (`let e: E<&'static i64> =
                // E::V::<&'static i64>(&c);`, rustc E0597). A promoted constant
                // (`let r: &'static i64 = &7i64;`) is not a loan of a local.
                auto named = [](std::string_view lt) {
                    return !lt.empty() && lt != "'_" && lt != "_" &&
                           !lt.starts_with("'%") && !lt.starts_with("%");
                };
                if (named(t.lifetime())) return true;
                for (auto& lt : t.lifetime_args()) if (named(lt)) return true;
                if (t.pointee() && t.pointee() != t && names_lt(t.pointee(), d + 1)) return true;
                auto k = t.kind();
                if ((k == LogosType::Kind::Slice || k == LogosType::Kind::Array) && t.elem() &&
                    names_lt(t.elem(), d + 1)) return true;
                if (k == LogosType::Kind::Tuple)
                    for (auto& e : t.tuple_elems()) if (names_lt(e, d + 1)) return true;
                for (auto& a : t.type_args()) if (names_lt(a, d + 1)) return true;
                return false;
            };
            let_annot_names_lifetime_ = ann && names_lt(ann, 0);
        }
        // `Box<dyn T>` now resolves to an OWNING TraitObject (owning bit on the
        // type) — no need to re-sniff the written name. A borrowed `&dyn` is a
        // non-owning TraitObject and is correctly excluded.
        if (ann && TypeRef(ann).owning_trait_object()) ann_is_box_dyn = true;
        // `let x: _ = rhs;` — top-level `_` placeholder: defer entirely to
        // the RHS's type by dropping the annotation. (logos-core 1.3.)
        // Nested `_` inside a composite annotation (`Vec<_>`) is handled by
        // the type-arg substitution path — the binding's surface type
        // keeps the InferredType slot until generic-arg inference fills
        // it.
        if (ann && TypeRef(ann).kind() == LogosType::Kind::InferredType)
            ann = nullptr;
    }

    // Set enum/struct hints so literal lowering can fill in unresolved type
    // params. A hint containing a `_` hole would PIN the hole into the
    // lowered literal's type-args (mono then instantiates a literal `_` —
    // `Vec$G1$_` / `Option$G1$_` mlir-gen failures), so hole-y annotations
    // set no hint — the RHS infers freely and fill_inferred_from_rhs
    // resolves the binding type afterwards (logos-core 1.3).
    bool ann_has_hole = ann && type_has_inferred(ann);
    auto saved_hint = hint_enum_type_;
    if (ann && !ann_has_hole &&
        TypeRef(ann).kind() == LogosType::Kind::Enum && !TypeRef(ann).type_args().empty())
        hint_enum_type_ = ann;
    auto saved_struct_hint = hint_struct_type_;
    if (ann && !ann_has_hole &&
        (TypeRef(ann).kind() == LogosType::Kind::Struct ||
                TypeRef(ann).kind() == LogosType::Kind::ZonedStruct) && !TypeRef(ann).type_args().empty())
        hint_struct_type_ = ann;
    auto saved_ret_hint = hint_call_return_type_;
    auto saved_expected  = hint_expected_type_;
    if (ann && !ann_has_hole && TypeRef(ann).kind() != LogosType::Kind::Error) {
        hint_call_return_type_ = ann;
        hint_expected_type_    = ann;
    } else {
        // An unannotated `let` has no expected type: the enclosing position's
        // must not reach its initializer (`let r: &dyn Tr = { let q = ..; q }`).
        hint_expected_type_ = nullptr;
    }
    // G151-3: a fn-ptr/closure-annotated let hints the closure formal so an
    // untyped closure literal (`let f: fn(i64)->i64 = |x| x+1`) infers its
    // param types (was `|<error>|`). Mirrors the call-arg + return paths.
    auto saved_closure_hint = hint_closure_formal_;
    if (ann && (LogosType::is_fn_value_kind(TypeRef(ann).kind()) ||
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
    if (ann && !ann_has_hole && TypeRef(ann).kind() == LogosType::Kind::Tuple)
        hint_tuple_type_ = ann;

    // C6-cc-04 + T0-4 (temporary lifetime extension): `let p = &<rvalue>;`
    // / `let p = &mut <rvalue>;` — Rust extends the temporary's lifetime
    // to the enclosing scope AND drops it at scope end. Synthesize a
    // hidden `let __lit_temp_N = <rvalue>;` BEFORE the user's let, then
    // rewrite the user's value to `&[mut] __lit_temp_N`. The named temp
    // rides the standard scope/drop machinery, so a droppable temp
    // (`&String::from("x")`) is freed at scope end — the anonymous
    // addr_of_temp spill never dropped (leak). Emit both as a single
    // SBlock; `define()` at outer scope keeps both bindings visible.
    // Covers scalar literals (the original C6-cc-04 shape) and the
    // rvalue-producing call/literal forms; PLACE expressions (VAR_REF,
    // DEREF, INDEX_READ, FIELD_READ) keep the borrow-in-place paths in
    // lower_unary / ADDR_OF_MUT.
    bool ref_ann_wrapped = false;   // `let ref y: T`: ann already rewritten to `&T`
    if (node.has_key(la::VALUE)) {
        auto val_node = map_of(node.get(la::VALUE.code));
        bool ext_mut = false;
        bool ext_dbl = false;   // `&&<rvalue>`: the extended temporary is borrowed twice
        TinyMapView ext_inner;
        bool have_ext = false;
        bool ext_ref_bind = false;
        // (annotated too: `let ref a: D = mk();` binds a temporary that lives
        // to the end of the block, exactly as the unannotated form — it leaked)
        if (node.has_key(la::IS_REF)) {
            AnyVal rav = node.get(la::IS_REF.code);
            ext_ref_bind = !rav.is_null() && rav.is_value() && rav.as_value<uint8_t>() != 0;
        }
        if (ext_ref_bind) {
            // `let ref y = <rvalue>` is `let y = &<rvalue>` (P4-pm-14 below).
            ext_inner = val_node;
            have_ext = true;
            // the annotation types the VALUE; the binding is `&T`
            if (ann) { ann = make_ref(false, ann); ref_ann_wrapped = true; }
        } else if (code_of(val_node) == la::UNARY && val_node.has_key(la::OP) &&
            val_node.has_key(la::VALUE)) {
            auto ext_op = str_of(val_node.get(la::OP.code));
            if (ext_op == "&" || ext_op == "&&") {
                ext_inner = map_of(val_node.get(la::VALUE.code));
                ext_dbl = ext_op == "&&";
                have_ext = true;
            }
        } else if (code_of(val_node) == la::ADDR_OF_MUT &&
                   val_node.has_key(la::VALUE)) {
            ext_inner = map_of(val_node.get(la::VALUE.code));
            ext_mut = true;
            have_ext = true;
        }
        if (have_ext) {
            {
                auto inner = ext_inner;
                int32_t inner_c = code_of(inner);
                bool is_scalar_lit =
                    inner_c == la::LIT_INT  || inner_c == la::LIT_BOOL ||
                    inner_c == la::LIT_FLOAT || inner_c == la::LIT_CHAR;
                bool is_rvalue_call =
                    inner_c == la::CALL        || inner_c == la::GENERIC_CALL ||
                    inner_c == la::METHOD_CALL || inner_c == la::STATIC_CALL  ||
                    inner_c == la::FN_MACRO_CALL ||
                    inner_c == la::STRUCT_LIT  || inner_c == la::TUPLE_LIT;
                if ((is_scalar_lit && !ext_dbl) || is_rvalue_call) {
                    // Lower the literal expr — its concrete type drives the
                    // synth let's type. Use the annotation pointee as the
                    // type hint if present so suffix-less literals widen
                    // to the right primitive.
                    TypeRef hint_lit = nullptr;
                    if (ann && (TypeRef(ann).kind() == LogosType::Kind::Ref ||
                                TypeRef(ann).kind() == LogosType::Kind::MutRef))
                        hint_lit = TypeRef(ann).pointee();
                    if (ext_dbl && hint_lit && is_ref_like(TypeRef(hint_lit).kind()))
                        hint_lit = TypeRef(hint_lit).pointee();
                    auto saved_lit_hint = hint_call_return_type_;
                    if (hint_lit) hint_call_return_type_ = hint_lit;
                    auto lit_expr = lower_expr(inner);
                    hint_call_return_type_ = saved_lit_hint;
                    if (hint_lit && expr_type(lit_expr) &&
                        TypeRef(expr_type(lit_expr)).kind() == LogosType::Kind::IntLit)
                        builder().retype_expr(lit_expr, hint_lit);
                    TypeRef lit_type = expr_type(lit_expr);
                    auto ltk = lit_type ? TypeRef(lit_type).kind()
                                        : LogosType::Kind::Error;

                    // A void/Never/error-typed rvalue has no temp to
                    // extend — keep the pre-existing inline spill shape
                    // (`&<unit call>` is degenerate; nothing to drop).
                    //
                    // ── #92 CONST PROMOTION TAKES PRECEDENCE OVER EXTENSION ──
                    // In Rust `&<const literal>` is PROMOTED to `&'static`
                    // before temporary-lifetime extension is even considered,
                    // so there is no temporary to name and no drop to
                    // schedule. The rewrite below would have put the literal
                    // in a NAMED FRAME LOCAL (`__lit_temp_N`), which is
                    // exactly what dangles when the reference is returned:
                    // MEASURED on the imported witness
                    // pass/regions/regions-bot — `let zero: &i64 = &0i64;
                    // return zero;` refused with "cannot return reference to
                    // local variable 'zero'" while the DIRECT `return &0i64;`
                    // already promoted, `[retgate]` naming the spread in one
                    // run (`srcs=[__lit_temp_0,]`). Keeping the anonymous
                    // AddrOfTemp shape is what routes it to the promotion arm
                    // that borrow_check and mlir_gen SHARE
                    // (const_promote::is_const_value). `!ext_mut`: `&mut`
                    // needs unique writable storage and is never promoted, in
                    // Rust or here. A scalar literal has no destructor, so
                    // dropping the extension costs nothing even when the
                    // shared predicate later declines the shape (a char
                    // literal): it then simply keeps today's frame lowering
                    // AND today's refusal, which is what it had.
                    if (ltk == LogosType::Kind::Void ||
                        ltk == LogosType::Kind::Never ||
                        ltk == LogosType::Kind::Error ||
                        (!ext_mut && is_scalar_lit)) {
                        auto rhs_e = builder().addr_of_temp(
                            std::move(lit_expr), ext_mut,
                            make_ref(ext_mut, lit_type), BorrowOrigin::Explicit);
                        define(std::string(name), ann ? ann : expr_type(rhs_e),
                               is_mut);
                        lir::SLet sl;
                        sl.name = std::string(name);
                        sl.type = ann ? ann : expr_type(rhs_e);
                        sl.is_mut = is_mut;
                        sl.value = std::move(rhs_e);
                        hint_enum_type_ = saved_hint;
                        hint_struct_type_ = saved_struct_hint;
                        hint_call_return_type_ = saved_ret_hint;
                        hint_expected_type_    = saved_expected;
                        hint_closure_formal_ = saved_closure_hint;
                        hint_tuple_type_ = saved_tuple_hint;
                        return make_stmt_emit(node_line_, std::move(sl));
                    }

                    std::string tmp = std::format("__lit_temp_{}", destruct_counter_++);
                    define(tmp, lit_type);
                    // The user binding's type is the (annotated or derived)
                    // reference type — a null type here made every
                    // un-annotated use read as "undefined variable".
                    define(std::string(name),
                           ann ? ann : ext_dbl ? make_ref(false, make_ref(false, lit_type))
                                               : make_ref(ext_mut, lit_type), is_mut);

                    std::vector<lir_view::StmtRef> blk;

                    // synth: `let [mut] __lit_temp_N = <rvalue>;`
                    lir::SLet sl_tmp;
                    sl_tmp.name   = tmp;
                    sl_tmp.type   = lit_type;
                    sl_tmp.is_mut = ext_mut;
                    sl_tmp.value  = std::move(lit_expr);
                    blk.push_back(make_stmt_emit(node_line_, std::move(sl_tmp)));

                    // user:  `let name = &[mut] __lit_temp_N;`
                    auto addr = builder().addr_of(tmp, make_ref(ext_mut, lit_type), BorrowOrigin::Explicit);
                    if (ext_dbl) {
                        TypeRef at = expr_type(addr);
                        addr = builder().addr_of_temp(std::move(addr), false, make_ref(false, at), BorrowOrigin::Explicit);
                    }
                    lir::SLet sl_user;
                    sl_user.name   = std::string(name);
                    sl_user.type   = ann ? ann : expr_type(addr);
                    sl_user.is_mut = is_mut;
                    sl_user.value  = std::move(addr);
                    blk.push_back(make_stmt_emit(node_line_, std::move(sl_user)));

                    hint_enum_type_ = saved_hint;
                    hint_struct_type_ = saved_struct_hint;
                    hint_call_return_type_ = saved_ret_hint;
                        hint_expected_type_    = saved_expected;
                    hint_closure_formal_ = saved_closure_hint;
                    hint_tuple_type_ = saved_tuple_hint;
                    return make_stmt_emit(node_line_, lir::SBlock{lir_mirror_block(*cur_prog_, blk), /*transparent=*/true});
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
        // B8: a `let x = v` (with value) re-declaration clears any stale
        // declared-uninit mark from an earlier `let x: T;` shadow.
        decl_uninit_vars_.erase(std::string(name));
        currently_uninit_vars_.erase(std::string(name));  // logos-core 2.7
        pending_closure_capture_drops_.clear();  // claim only OUR direct closure RHS
        pending_closure_deferred_moves_.clear();
        auto rhs_node = map_of(node.get(la::VALUE.code));
        // Box DerefMove: `let s = *b` over a move-typed Box<T> moves the content
        // out (consuming b) and frees the block without dropping the content —
        // Rust's built-in `*b` move. Desugars to `box_take(b)`. (Copy-T Box and
        // non-Box derefs fall through to the normal copy/deref path.)
        if (!is_ref_bind && code_of(rhs_node) == la::DEREF)
            rhs = try_lower_box_deref_move(rhs_node);
        if (!rhs)
            rhs = lower_expr(rhs_node);
        rhs_type = expr_type(rhs);
        if (is_ref_bind) {
            // Wrap the lowered RHS in an addr-of-temp so it produces
            // `&rhs` with type `&T` (matches `let y = &x;` semantics).
            auto inner_t = expr_type(rhs);
            rhs      = builder().addr_of_temp(std::move(rhs), /*is_mut=*/false,
                                              make_ref(false, inner_t), BorrowOrigin::Explicit);
            rhs_type = expr_type(rhs);
            // `let ref y: T = e`: the annotation types the VALUE (Rust: the
            // pattern `ref y` matches a `T`), so the binding is `&T`.
            if (ann && !ref_ann_wrapped) ann = make_ref(false, ann);
        }
        // E0507: `let s = *r` moving a MOVE-typed value out of a `&`/`&mut`
        // deref of a reference variable — the source doesn't own the value, so
        // the move duplicates the owner (double-free). Copy values copy out
        // fine; raw-ptr deref/index, Box deref-move, Vec-index (method-deref),
        // field-out-of-self, and return/arg positions are NOT caught here —
        // pervasive or ambiguous (documented in tier-reaudit-findings.md).
        if (rhs && !is_ref_bind && is_move_type(rhs_type) &&
            is_unowned_move_source(rhs))
            error("cannot move out of a value behind a reference / out of an "
                  "index (E0507)");
    } else if (ann) {
        // B3-bg-01 / B3-bg-02: `let v: T;` / `let mut v: T;` —
        // declare-without-init. Binding takes the annotated type; value
        // remains null. The variable must be definitely-assigned before
        // use; assignment paths register the value (lower_assign), and
        // reads of an uninitialised binding will surface as either
        // mlir-gen "use of uninitialised slot" or a borrow-check warn.
        // (Full definite-assignment analysis is a separate pass; for now
        // we trust user code or rely on later use-checks.)
        // B8: record as declared-uninitialised so a later reassignment does
        // NOT drop-before-replace (the slot holds no live value yet, and a
        // conditional path may leave it uninit).
        decl_uninit_vars_.insert(std::string(name));
        currently_uninit_vars_.insert(std::string(name));  // logos-core 2.7
        rhs      = nullptr;
        rhs_type = ann;
    } else {
        error(std::format("let '{}': missing value", name));
        rhs      = error_expr();
        rhs_type = error_t();
    }

    // (Zone Step 4 pin: a by-value `#[rel_ptr]`-containing binding is rejected in
    // define() — the single by-value-slot registrar — which lower_let calls below.)

    hint_enum_type_ = saved_hint;
    hint_struct_type_ = saved_struct_hint;
    hint_call_return_type_ = saved_ret_hint;
                        hint_expected_type_    = saved_expected;
    hint_closure_formal_ = saved_closure_hint;
    hint_arr_elem_type_ = saved_arr_elem_hint;
    hint_tuple_type_ = saved_tuple_hint;

    TypeRef var_type;
    // Slice 7 of metaprog-quote: an ExprBlob-typed RHS marks a deferred
    // metacall whose actual expr type is determined post-splice (pass-2
    // sema reads the blob's root schema_type_code and recurses into
    // lower_expr). Pass-1 here just adopts the annotation and skips the
    // strict type-equality check; pass-2 will verify compatibility once
    // the WRIT_BLOB has been lowered to a real expr.
    bool rhs_is_expr_blob =
        rhs &&
        TypeRef(rhs_type).kind() == LogosType::Kind::Struct &&
        is_exprblob(rhs_type);
    if (rhs_is_expr_blob && ann != nullptr) {
        rhs_type = ann;
        if (rhs) {
            builder().retype_expr(rhs, ann);
        }
    }
    if (rhs && ann != nullptr) {
        // Rust auto-reborrows `&mut T` at COERCION sites in `let _: T = rhs`
        // when rhs is `&mut T` and ann is a ref-or-ptr type (`&mut T`, `&T`,
        // `*const T`, `*mut T`). Per Rust the explicit type ascription IS a
        // coercion site, so even same-type `let m: &mut T = v` reborrows
        // (NLL releases on m's last use, restoring v's usability).
        if (TypeRef(expr_type(rhs)).kind() == LogosType::Kind::MutRef &&
            (TypeRef(ann).kind() == LogosType::Kind::MutRef ||
             TypeRef(ann).kind() == LogosType::Kind::Ref ||
             TypeRef(ann).kind() == LogosType::Kind::Ptr)) {
            // ── CEILING PROBE `mrletann` (producer half) — decline the
            // ascription reborrow so the RHS reaches borrow_check as a bare
            // MutRef VarRef, which its Let arm (armed under the same name)
            // then consumes. See the consumer's note for why this is a
            // divergence probe rather than a proposed rule.
            // `mrlasema` arms the PRODUCER ALONE; `mrletann` arms both halves.
            // ── MEASURED 2026-08-29: BOTH 0 / 0, SITE PROVEN LIVE AND THE
            // DECLINE PROVEN TO TRIGGER. `mrlasema` fires exactly ONCE on a
            // five-line repro holding exactly one annotated `&mut` let, so this
            // site DID decline the reborrow for `let moved: &mut S = state;`.
            // borrow_check's Let arm still never saw a bare MutRef VarRef —
            // LOGOS_MRAM_TRACE matched only two prelude `__ret_tmp` bindings —
            // so the wrap is re-inserted downstream of here. `expect_type` /
            // `apply_place_coercions` call `try_implicit_reborrow_mut` too, and
            // apply_place_coercions' own header says it is being folded into
            // expect_type: TWO NOTIONS OF ONE COERCION SITE, and declining at
            // the narrow one changes nothing. Any next round disables BOTH or
            // neither.
            // ⚠ AND THE ROW THIS AIMED AT MAY NOT BE A DEFECT. Rust treats an
            // explicit type ascription as a coercion site and DOES reborrow
            // there; upstream's reborrow-sugg-move-then-borrow.rs has no
            // annotation and the port added one. Before this row is funded,
            // the PORT should be checked against upstream.
            if (!(logos::probe::on("mrletann") ||
                  logos::probe::on("mrlasema")) &&
                try_implicit_reborrow_mut(rhs, ann))
                rhs_type = expr_type(rhs);
        }
        // impl Trait annotation: any concrete struct that was returned from an
        // impl-Trait-returning function is acceptable — treat the variable type as the
        // concrete rhs type so method calls work.
        // Named alias holes (`DoubleCell<_>`) are filled BEFORE the type check:
        // the first position binds, the others take that binding, and the
        // variance check below then holds the value to it (#465 neighbour).
        bool alias_holes_ = false;
        {
            std::function<bool(TypeRef, int)> has_hole = [&](TypeRef t, int d) -> bool {
                if (!t || d > 24) return false;
                if (t.kind() == LogosType::Kind::TypeVar && std::string_view(t.type_var_name()).starts_with("?")) return true;
                if (t.pointee() && has_hole(t.pointee(), d + 1)) return true;
                if (t.elem() && has_hole(t.elem(), d + 1)) return true;
                for (auto x : t.type_args()) if (has_hole(x, d + 1)) return true;
                for (auto x : t.tuple_elems()) if (has_hole(x, d + 1)) return true;
                return false;
            };
            if (rhs && ann && rhs_type && has_hole(ann, 0)) {
                alias_hole_bind_.clear();
                ann = fill_inferred_from_rhs(ann, rhs_type);
                alias_hole_bind_.clear();
                alias_holes_ = true;
            }
        }
        bool ann_is_impl = TypeRef(ann).kind() == LogosType::Kind::ImplTrait;
        if (!ann_is_impl && !rhs_is_expr_blob &&
            TypeRef(ann).kind() != LogosType::Kind::Error &&
            TypeRef(rhs_type).kind() != LogosType::Kind::Error) {
            if (is_writ_static(ann) && is_writ(rhs_type) &&
                !types_compatible(rhs_type, ann)) {
                // B-he-05: WritStatic ← Writ mismatch is almost always
                // caused by `${capture}` or other runtime-only constructs in
                // the @-literal. A capture-specific hint beats the generic
                // mismatch, so this one PRE-EMPTS expect_type.
                error(std::format(
                    "let '{}': @-literal evaluated to runtime Writ (likely "
                    "due to `${{capture}}` or other runtime-only construct); "
                    "WritStatic does not permit captures — drop them, or "
                    "annotate `{}: Writ` instead",
                    name, name));
            } else {
                expect_type(rhs, ann, CoercePos::LetInit,
                            std::format("let '{}': type mismatch —", name));
                rhs_type = expr_type(rhs);
            }
        }
        // B64: variance-aware subtype check at let-init coercion site.
        // Strict mode — let annotation is fn-scope-fixed.
        if (rhs && ann && !rhs_is_expr_blob)
            check_variance(rhs_type, ann, std::format("let '{}'", name),
                           /*permissive=*/false);
        // T1-12 (audit-v2): `let r: &dyn Trait + Send = &not_send;` — the
        // dyn+auto-bound gate used to fire at arg-coercion only.
        if (rhs && ann)
            check_dyn_auto_bounds_at_coercion(rhs, ann);
        // Implicit safe integer widening: u32 → i64, i32 → i64, u8 → u32, ...
        if (rhs && ann && is_integer_kind(TypeRef(ann).kind()) && is_integer_kind(TypeRef(rhs_type).kind()) &&
            TypeRef(rhs_type).kind() != LogosType::Kind::IntLit &&
            TypeRef(rhs_type).kind() != LogosType::Kind::Enum &&
            can_widen_int(TypeRef(rhs_type).kind(), TypeRef(ann).kind())) {
            widen_int_expr(rhs, ann, builder());
            rhs_type = expr_type(rhs);
        }
        // Retype float literal to concrete annotation type (f32 or f64).
        if (TypeRef(rhs_type).kind() == LogosType::Kind::FloatLit && ann &&
            (TypeRef(ann).kind() == LogosType::Kind::F32 || TypeRef(ann).kind() == LogosType::Kind::F64))
            builder().retype_expr(rhs, ann);
        // Retype/coerce integer literal (or IntLit-typed expr) to float annotation type (f32 or f64).
        if (TypeRef(rhs_type).kind() == LogosType::Kind::IntLit && ann &&
            (TypeRef(ann).kind() == LogosType::Kind::F32 || TypeRef(ann).kind() == LogosType::Kind::F64)) {
            auto rhs_ref = expr_ref_of(rhs);
            if (rhs_ref.kind() == lir_schema::expr::Code::LitInt) {
                // Simple integer literal: convert directly to float literal.
                // Build a fresh node so the Writ mirror is emitted with
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
            auto rhs_ref = expr_ref_of(rhs);
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
            auto rhs_ref = expr_ref_of(rhs);
            if (rhs_ref.kind() == lir_schema::expr::Code::TupleLit) {
                lir_view::ETupleLitView tlit_view{rhs_ref};
                const auto& tup_anns = TypeRef(ann).tuple_elems();
                uint64_t n = std::min<uint64_t>(tlit_view.count(), tup_anns.size());
                for (uint64_t ei = 0; ei < n; ++ei) {
                    auto elem_er = tlit_view.elem(ei);
                    if (!elem_er) continue;
                    TypeRef ann_e = tup_anns[ei];
                    auto elem_kind = elem_er.type(cur_prog_->type_pool.impl()).kind();
                    bool ann_is_float = ann_e && (TypeRef(ann_e).kind() == LogosType::Kind::F32 ||
                                                  TypeRef(ann_e).kind() == LogosType::Kind::F64);
                    // Retype FloatLit element to concrete float annotation (f32/f64).
                    if (elem_kind == LogosType::Kind::FloatLit && ann_is_float)
                        builder().retype_expr(elem_er, ann_e);
                    // Replace IntLit element with a concrete-typed FloatLit when the
                    // annotation is a float — re-emits the parent tuple's mirror.
                    if (elem_kind == LogosType::Kind::IntLit && ann_is_float) {
                        auto er = elem_er;
                        if (er.kind() == lir_schema::expr::Code::LitInt) {
                            double fval = static_cast<double>(lir_view::ELitIntView{er}.value());
                            rhs = builder().set_tuple_elem(rhs, ei, builder().lit_float(fval, ann_e));
                            // Re-fetch view since rhs's mirror is fresh.
                            tlit_view = lir_view::ETupleLitView{rhs};
                            continue;
                        }
                    }
                    if (elem_kind == LogosType::Kind::IntLit)
                        if (auto v = get_intlit_value(elem_er))
                            if (ann_e && !intlit_fits(*v, TypeRef(ann_e).kind()))
                                error(std::format("let '{}': tuple element {}: value {} does not fit in {}",
                                      name, ei, *v, type_str(ann_e)));
                    // Tuple element is itself an array literal.
                    if (ann_e && TypeRef(ann_e).kind() == LogosType::Kind::Array &&
                        TypeRef(ann_e).elem() && elem_kind == LogosType::Kind::Array) {
                        auto er = elem_er;
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
                        auto er = elem_er;
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
        // logos-core 1.3: `_` holes in the annotation resolve from the RHS
        // (`let v: Vec<_> = vec![1]` binds as Vec<i32> — the hole used to
        // leak into mono as a literal `Vec$G1$_` instantiation).
        var_type = ann_is_impl ? rhs_type : fill_inferred_from_rhs(ann, rhs_type);
        (void)alias_holes_;
        // LANDED 2026-09-02p (was PROBE stland/stfacts L): an ELIDED annotation
        // region is an inference variable — the binding takes the initializer's region.
        if (rhs && var_type && rhs_type &&
            (TypeRef(var_type).kind() == LogosType::Kind::Ref ||
             TypeRef(var_type).kind() == LogosType::Kind::MutRef) &&
            TypeRef(rhs_type).kind() == TypeRef(var_type).kind() &&
            TypeRef(var_type).lifetime().empty() && !TypeRef(rhs_type).lifetime().empty()) {
            logos::probe::census("stfacts.let.region");
            var_type = make_ref(TypeRef(var_type).kind() == LogosType::Kind::MutRef,
                                TypeRef(var_type).pointee(),
                                std::string(TypeRef(rhs_type).lifetime()));
        }
        // The FAT-REFERENCE twin: `let s: str = "abc";` (or `&[T]`) keeps the
        // initializer's region — the literal's `'static` — instead of pinning
        // the elided one (a `move` closure capturing `s` must be `'static`).
        if (rhs && var_type && rhs_type &&
            TypeRef(var_type).kind() == LogosType::Kind::Slice &&
            TypeRef(rhs_type).kind() == LogosType::Kind::Slice &&
            TypeRef(var_type).slice_owning_kind() == TypeRef::OwningKind::Borrow &&
            TypeRef(rhs_type).slice_owning_kind() == TypeRef::OwningKind::Borrow &&
            TypeRef(var_type).lifetime().empty() && !TypeRef(rhs_type).lifetime().empty() &&
            types_equal(TypeRef(var_type).elem(), TypeRef(rhs_type).elem())) {
            logos::probe::census("stfacts.let.slice_region");
            var_type = make_slice_type(TypeRef(var_type).elem(), TypeRef(var_type).mut_ptr(),
                                       TypeRef::OwningKind::Borrow,
                                       std::string(TypeRef(rhs_type).lifetime()));
        }
        // LANDED 2026-09-09 — the STRUCT/ENUM twin of the Ref hop above: one
        // predicate ("an elided annotation region is an inference variable"),
        // asked at both kinds it can be asked at.
        if (rhs && var_type && rhs_type) {
            using KSL_ = LogosType::Kind;
            auto vk_ = TypeRef(var_type).kind();
            if ((vk_ == KSL_::Struct || vk_ == KSL_::ZonedStruct || vk_ == KSL_::Enum) &&
                TypeRef(rhs_type).kind() == vk_ &&
                TypeRef(var_type).lifetime_args().empty() &&
                !TypeRef(rhs_type).lifetime_args().empty()) {
                logos::probe::census("stfacts.let.struct_region");
                LogosTypeBuilder lt_;
                lt_.kind          = vk_;
                lt_.struct_name   = std::string(TypeRef(var_type).struct_name());
                lt_.enum_name     = std::string(TypeRef(var_type).enum_name());
                lt_.pkg_name      = std::string(TypeRef(var_type).pkg_name());
                lt_.type_args     = TypeRef(var_type).type_args();
                lt_.lifetime_args = TypeRef(rhs_type).lifetime_args();
                var_type = pool_->alloc(std::move(lt_));
            }
        }
        // Retype the rhs tuple expression node to use the concrete annotation tuple type.
        // This ensures codegen sees (f32, f32) instead of (FloatLit, FloatLit).
        // Use the hole-FILLED type — stamping a raw `(i64, _)` annotation
        // would leak `_` into codegen.
        if (!ann_is_impl && TypeRef(ann).kind() == LogosType::Kind::Tuple &&
            TypeRef(rhs_type).kind() == LogosType::Kind::Tuple)
            builder().retype_expr(rhs, var_type);
    } else {
        var_type = rhs_type;
        // LANDED 2026-09-06a — A LOCAL'S OWN REGION IS AN INFERENCE VARIABLE.
        // `let mut t0 = a;` does not pin the local to the parameter's region:
        // the outer slot is COVARIANT and may shrink. De-rigidify exactly that
        // slot; every region reachable through it — invariant under `&mut`,
        // and inside type arguments — stays a binder of this scope.
        {
            using K3_ = LogosType::Kind;
            if (var_type && (TypeRef(var_type).kind() == K3_::Ref ||
                             TypeRef(var_type).kind() == K3_::MutRef)) {
                std::string lt3_(TypeRef(var_type).lifetime());
                if (lt_is_minted(lt3_)) current_lt_binders().erase(lt3_);
            }
        }
        if (TypeRef(var_type).kind() == LogosType::Kind::IntLit) {
            // Default IntLit to i32; upgrade to i64 if the literal value overflows i32.
            var_type = i32_t();
            auto er = expr_ref_of(rhs);
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
        // The same default one level down: an unsuffixed literal inside a tuple
        // (`let mut u = (Some(4), 3)`) was left `{integer}` in the binding's
        // type, so `&mut u` at `&mut (Option<i32>, i32)` failed invariance.
        if (TypeRef(var_type).kind() == LogosType::Kind::Tuple) {
            std::function<TypeRef(TypeRef)> dflt = [&](TypeRef t) -> TypeRef {
                if (!t) return t;
                auto k = TypeRef(t).kind();
                if (k == LogosType::Kind::IntLit) return i32_t();
                if (k == LogosType::Kind::FloatLit) return prim(LogosType::Kind::F64);
                if (k != LogosType::Kind::Tuple) return t;
                std::vector<TypeRef> es;
                bool changed = false;
                for (auto e : TypeRef(t).tuple_elems()) {
                    auto ne = dflt(e);
                    changed |= (ne != e);
                    es.push_back(ne);
                }
                return changed ? make_tuple_type(std::move(es)) : t;
            };
            TypeRef dt = dflt(var_type);
            // A literal still awaiting its first use (pending_lit_lets_) keeps
            // its `{integer}` leaves — they default at codegen exactly as the
            // binding's type says — so that use can still stamp them.
            if (dt != var_type && !ann && rhs && is_stampable_literal_(expr_ref_of(rhs)))
                var_type = dt;
            else if (dt != var_type && expect_type(rhs, dt, CoercePos::LetInit, "let binding"))
                var_type = expr_type(rhs);
        }
    }

    // `let _ = e` BINDS NOTHING (Rust). Over a PLACE it neither reads nor moves
    // (`let _ = x;` leaves `x` owned where it is); over an rvalue the value is a
    // temporary dropped at the end of THIS statement, exactly as `e;` — not a
    // local named `_` living to the end of the block (squeue
    // let_underscore_defers_drop_to_block_end: `let _ = O(1); let a = O(2);`
    // dropped 2 before 1).
    const auto vk_ = var_type ? TypeRef(var_type).kind() : LogosType::Kind::Error;
    const bool unit_ = vk_ == LogosType::Kind::Tuple && TypeRef(var_type).tuple_elems().empty();
    if (name == "_" && rhs && vk_ != LogosType::Kind::Never && vk_ != LogosType::Kind::Void &&
        vk_ != LogosType::Kind::Error && !unit_) {
        namespace ec = lir_schema::expr;
        auto ek = expr_ref_of(rhs).kind();
        const bool is_place = ek == ec::Code::VarRef || ek == ec::Code::FieldRead ||
                              ek == ec::Code::IndexRead || ek == ec::Code::Deref ||
                              ek == ec::Code::TupleIndex || ek == ec::Code::SliceIndex;
        if (is_place) {
            // A FAKE READ of the place (MIR's `FakeRead(ForLet)`): it keeps the
            // borrows the place goes through live and conflicts with a `&mut`
            // of it (`let _ = *a;` after `&mut x.0` is E0502), but moves
            // nothing. A discarded shared borrow of the place is exactly that.
            // For a COPY place a plain read is that use (rustc's sentence is
            // "cannot use", E0503); for a move type a read would MOVE, so a
            // discarded shared borrow stands in for it.
            if (!is_move_type(var_type)) return builder().stmt_expr(std::move(rhs), node_line_);
            auto rt = make_ref(false, var_type);
            return builder().stmt_expr(
                builder().addr_of_temp(std::move(rhs), false, rt, lir_schema::expr::BorrowOrigin::Explicit),
                node_line_);
        }
        if (is_move_type(var_type)) {
            std::string synth = std::format("__stmt_tmp_{}", destruct_counter_++);
            if (auto drop = make_drop_stmt(synth, VarInfo{var_type, false})) {
                std::vector<lir_view::StmtRef> blk;
                lir::SLet sl;
                sl.name = synth; sl.type = var_type; sl.is_mut = false;
                sl.value = std::move(rhs);
                blk.push_back(make_stmt_emit(node_line_, std::move(sl)));
                blk.push_back(std::move(*drop));
                lir::SBlock sb; sb.body = lir_mirror_block(*cur_prog_, blk);
                sb.transparent = true;
                return make_stmt_emit(node_line_, std::move(sb));
            }
        }
        // A value with no destructor: when it dies is unobservable — the
        // ordinary `let` below is kept.
    }
    // `let x = x` (or `x.f`): the move is of the binding the name denotes BEFORE this let, so record it before define.
    bool self_rooted_move = false;
    if (rhs && is_move_type(rhs_type)) {
        auto r = expr_ref_of(rhs);
        using C = lir_schema::expr::Code;
        while (r && (r.kind() == C::FieldRead || r.kind() == C::TupleIndex))
            r = r.kind() == C::FieldRead ? lir_view::EFieldReadView{r}.receiver() : lir_view::ETupleIndexView{r}.receiver();
        self_rooted_move = r && r.kind() == C::VarRef && lir_view::EVarRefView{r}.name() == std::string_view(name);
        if (self_rooted_move) mark_moved_expr(expr_ref_of(rhs));
    }
    define(name, var_type, is_mut);
    // A LOCAL'S REGIONS ARE INFERENCE VARIABLES unless its annotation NAMES
    // one. Sema pins them to the initializer's (above), which is right for the
    // reads and wrong for a later write of a shorter-lived value (`p.b = y`):
    // rustc infers a region both satisfy. Such writes are left to the borrow
    // checker, which does infer (#465).
    if (!scope_.empty()) {
        bool names_region = false;
        if (ann) {
            std::vector<std::string> lts;
            lt_names_(ann, lts);
            for (auto& l : lts)
                if (!l.empty() && l[0] != '\x01' && l != "'_" && !lt_is_minted(l)) { names_region = true; break; }
        }
        scope_.back().vars[std::string(name)].regions_inferred = !names_region;
        scope_.back().vars[std::string(name)].deferred_init = (!rhs && ann);
    }
    if (rhs && expr_ref_of(rhs).kind() == lir_schema::expr::Code::ClosureBox && !scope_.empty())
        scope_.back().vars[std::string(name)].closure_id =
            std::string(lir_view::EClosureBoxView{expr_ref_of(rhs)}.closure_id());
    // Rust capture-drop order: a closure-RHS let owns its un-skipped
    // captures' drop slots — they drop with this binding, in capture
    // order (see collect-walk group emission).
    if (node.has_key(la::VALUE) &&
        code_of(map_of(node.get(la::VALUE.code))) == la::CLOSURE_EXPR &&
        !pending_closure_capture_drops_.empty()) {
        for (auto& c : pending_closure_capture_drops_)
            capture_owner_[c] = std::string(name);
        closure_drop_group_[std::string(name)] =
            std::move(pending_closure_capture_drops_);
    }
    // The hand-over list travels with the BINDING: consuming `f` releases the
    // source's obligation for exactly the captures f's body moves out.
    if (node.has_key(la::VALUE) &&
        code_of(map_of(node.get(la::VALUE.code))) == la::CLOSURE_EXPR &&
        !pending_closure_deferred_moves_.empty()) {
        // CLAIMED. From here the SOURCE owns these captures' destructors
        // (`closure_owned_drop_` un-skips a root in emit_frame_drops' `eligible`
        // and a dotted path in make_drop_stmt) and hands them over when this
        // binding is consumed — mark_moved's cascade.
        for (const auto& nm : pending_closure_deferred_moves_)
            closure_owned_drop_.insert(nm);
        closure_deferred_moves_[std::string(name)] =
            std::move(pending_closure_deferred_moves_);
    }
    pending_closure_deferred_moves_.clear();
    pending_closure_capture_drops_.clear();
    // Mark an owning `Box<dyn Trait>` binding so collect_drops emits its
    // drop_in_place + free sequence (the type collapsed to bare TraitObject).
    // ONLY when the RHS genuinely TRANSFERS OWNERSHIP — a fresh `box_new(..) as
    // Box<dyn>` cast (Cast) or a value-returning constructor (Call/New). A
    // `*m.get(&k)` / `v.get(i)` / `arr[i]` reads a HANDLE COPY out of a
    // container the container still owns (Rust forbids moving out of a shared
    // ref; Logos copies it) — dropping that copy would double-free the
    // container's element. So exclude Deref / IndexRead / MethodCall / etc.
    if (ann_is_box_dyn && !scope_.empty() && rhs) {
        using C = lir_schema::expr::Code;
        auto rk = expr_ref_of(rhs).kind();
        bool owns = rk == C::Cast || rk == C::Call;
        if (owns) {
            auto sname = std::string(name);
            if (auto vit = scope_.back().vars.find(sname); vit != scope_.back().vars.end())
                vit->second.owning_dyn = true;
        }
    }

    // Move semantics: if RHS is a variable reference (or struct-field read) of a
    // move type, mark it moved. mark_moved_expr handles both VarRef and
    // nested FieldRead chains, recording dotted paths so make_drop_stmt
    // can suppress per-field auto-drop on the source struct.
    if (rhs && is_move_type(rhs_type) && !self_rooted_move)
        mark_moved_expr(expr_ref_of(rhs));

    const bool lit_pending = !ann && rhs && is_stampable_literal_(expr_ref_of(rhs));
    lir::SLet slet;
    slet.name   = std::string(name);
    slet.type   = var_type;
    slet.is_mut = is_mut;
    slet.value  = std::move(rhs);
    slet.annot_lifetime = let_annot_names_lifetime_;
    auto st = make_stmt_emit(node_line_, std::move(slet));
    if (lit_pending)
        if (const VarInfo* vi = lookup_var_info(name))
            pending_lit_lets_[vi->slot] = PendingLitLet{st, 0};
    return st;
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

lir_view::StmtRef SemaChecker::lower_compound_assign(TinyMapView node) {
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
    // `STATIC op= v` writes through the global's address: the general place path.
    if (names_static_mut(name)) {
        if (!inside_unsafe_)
            error(std::format(
                "write to mutable static `{}` requires `unsafe` block "
                "(Rust `items.static.mut.safety`)", name));
        return lower_place_compound_assign(node, place_node, base_op);
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
            bool impl_found = has_impl(assign_trait, type_name) ||
                              (!base_name.empty() &&
                               has_impl(assign_trait, base_name));
            if (impl_found) {
                auto mangled = type_name + "__" + assign_method;
                auto mut_ref_t = make_ref(true, var_type);
                auto recv = builder().addr_of(std::string(name), mut_ref_t, BorrowOrigin::CompoundAssign);
                // G160-5: the `*Assign<Rhs>` method's second param is the
                // trait's Rhs type-arg, which need NOT equal Self. Look it up by
                // the actual rhs operand type (`x <<= 1u8` over `impl
                // ShlAssign<u8> for Int` → `Int__shl_assign(&mut Int, u8)`).
                // Fall back to the Self-RHS signature if the rhs-typed one
                // doesn't resolve (covers an IntLit rhs against a Self-RHS impl).
                auto fit = find_op_assign_impl(mangled, mut_ref_t, var_type, rhs);
                if (fit) {
                    std::vector<lir::LExprPtr> args;
                    // A by-value rhs is consumed by the call. PROBES.md 2026-09-15f-consumeland.
                    if (rhs && is_move_type(expr_type(rhs)) &&
                        !(fit->param_types.size() == 2 && fit->param_types[1] &&
                          is_ref_like(TypeRef(fit->param_types[1]).kind())))
                        mark_moved_expr(expr_ref_of(rhs));
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

    // Type-check via the judgment. A compound assign's RHS is an OPERAND of
    // `place op rhs`, not a full place write — reborrow/unsize make no sense
    // here — but the verdict and its message are the same one.
    expect_type(rhs, var_type, CoercePos::Operand,
                std::format("compound assignment to '{}': type mismatch —", name));
    // Synthesize the binop LIR node
    auto binop = builder().bin_op(base_op, std::move(lhs_ref), std::move(rhs), var_type);
    return builder().stmt_assign(std::string(name), std::move(binop), node_line_);
}

// Render a place AST node to its source-like form (`p.x`, `arr[i]`, `t.0`,
// `b.data[i]`, `(*p).x`) for diagnostics. Mirrors the names the former per-shape
// compound lowerings produced.
std::string SemaChecker::render_place_node(writ::TinyMapView n) {
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
lir_view::StmtRef SemaChecker::lower_place_compound_assign(
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
                bool has_im = has_impl("IndexMut", type_name) ||
                              (!base_name.empty() && has_impl("IndexMut", base_name));
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
                        expect_type(rhs2, out_t, CoercePos::Operand,
                                    std::format("compound assignment to '{}[i]': type mismatch —",
                                                arr_name));
                        // Rust: `a[i] op= v` is `*IndexMut::index_mut(&mut a, i) op= v` —
                        // the index and the call evaluated ONCE (a primitive's RHS first).
                        if (cur_stmt_temp_hoist_) {
                            if (node.has_key(la::VALUE))
                                rhs2 = compound_rhs_first(std::move(rhs2), out_t,
                                                          map_of(node.get(la::VALUE.code)), true);
                            std::vector<lir::LExprPtr> wa;
                            wa.push_back(builder().addr_of(arr_name, make_ref(true, arr_type), BorrowOrigin::OperatorAutoref));
                            wa.push_back(lower_idx(fit_im));
                            auto wc = builder().call(fit_im->symbol_name.empty()
                                          ? (type_name + "__index_mut") : fit_im->symbol_name,
                                      {}, std::move(wa), ref_o);
                            std::string nm = std::format("__rtmp_{}", destruct_counter_++);
                            register_stmt_temp(nm, ref_o, std::move(wc), false);
                            auto cur1 = builder().deref(builder().var_ref(nm, ref_o), out_t);
                            auto comb = builder().bin_op(base_op, std::move(cur1), std::move(rhs2), out_t);
                            return builder().stmt_deref_write(builder().var_ref(nm, ref_o),
                                                              std::move(comb), node_line_);
                        }
                        lir::LExprPtr cur = nullptr;
                        if (fit_rd) {
                            std::vector<lir::LExprPtr> ra;
                            ra.push_back(builder().addr_of(arr_name, make_ref(false, arr_type), BorrowOrigin::OperatorAutoref));
                            ra.push_back(lower_idx(fit_rd));
                            auto rc = builder().call(fit_rd->symbol_name.empty()
                                          ? (type_name + "__index") : fit_rd->symbol_name,
                                      {}, std::move(ra), fit_rd->ret_type);
                            cur = builder().deref(std::move(rc), out_t);
                        } else {
                            std::vector<lir::LExprPtr> ra;
                            ra.push_back(builder().addr_of(arr_name, make_ref(true, arr_type), BorrowOrigin::OperatorAutoref));
                            ra.push_back(lower_idx(fit_im));
                            auto rc = builder().call(fit_im->symbol_name.empty()
                                          ? (type_name + "__index_mut") : fit_im->symbol_name,
                                      {}, std::move(ra), ref_o);
                            cur = builder().deref(std::move(rc), out_t);
                        }
                        auto combined = builder().bin_op(base_op, std::move(cur), std::move(rhs2), out_t);
                        std::vector<lir::LExprPtr> wa;
                        wa.push_back(builder().addr_of(arr_name, make_ref(true, arr_type), BorrowOrigin::OperatorAutoref));
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
    // A place that CALLS (`*cell.borrow_mut() += 1`, `*pick(&mut a) += 5`,
    // `v[next()] += 1`) is evaluated ONCE, as Rust does: the read-twice desugar
    // below ran the call twice (a second `borrow_mut` panicked "already
    // borrowed"). Its `&mut` is taken once into a statement temporary.
    {
        if (cur_stmt_temp_hoist_ && ast_has_call(place_node)) {
            auto mp = lower_mut_place(place_node);
            TypeRef pt1 = expr_type(mp);
            if (!pt1 || TypeRef(pt1).kind() == LogosType::Kind::Error) {
                if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
                return builder().stmt_break(nullptr, "", node_line_);
            }
            TypeRef ref_t = make_ref(true, pt1);
            auto rhs1 = node.has_key(la::VALUE)
                ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
            if (node.has_key(la::VALUE))
                rhs1 = compound_rhs_first(std::move(rhs1), pt1,
                                          map_of(node.get(la::VALUE.code)), true);
            std::string nm = std::format("__rtmp_{}", destruct_counter_++);
            register_stmt_temp(nm, ref_t,
                               builder().addr_of_temp(std::move(mp), /*is_mut=*/true, ref_t,
                                                      BorrowOrigin::CompoundAssign),
                               false);
            if (TypeRef(pt1).kind() == LogosType::Kind::Struct) {
                std::string atrait, amethod;
                if (op_assign_trait_method(base_op, atrait, amethod)) {
                    auto type_name = concrete_struct_name(pt1);
                    auto base_name = std::string(TypeRef(pt1).struct_name());
                    if (has_impl(atrait, type_name) ||
                        (!base_name.empty() && has_impl(atrait, base_name))) {
                        auto mangled = type_name + "__" + amethod;
                        auto fit = find_op_assign_impl(mangled, ref_t, pt1, rhs1);
                        if (fit) {
                            if (rhs1 && is_move_type(expr_type(rhs1)) &&
                                !(fit->param_types.size() == 2 && fit->param_types[1] &&
                                  is_ref_like(TypeRef(fit->param_types[1]).kind())))
                                mark_moved_expr(expr_ref_of(rhs1));
                            std::vector<lir::LExprPtr> args;
                            args.push_back(builder().var_ref(nm, ref_t));
                            args.push_back(std::move(rhs1));
                            auto call = builder().call(fit->symbol_name.empty() ? mangled : fit->symbol_name,
                                                       {}, std::move(args), fit->ret_type);
                            return builder().stmt_expr(std::move(call), node_line_);
                        }
                    }
                }
            }
            if (rhs1)
                expect_type(rhs1, pt1, CoercePos::Operand,
                            std::format("compound assignment to '{}': type mismatch —",
                                        render_place_node(place_node)));
            widen_int_expr(rhs1, pt1, builder());
            auto cur1 = builder().deref(builder().var_ref(nm, ref_t), pt1);
            auto newval1 = builder().bin_op(base_op, std::move(cur1), std::move(rhs1), pt1);
            track_write_move(newval1);
            return builder().stmt_deref_write(builder().var_ref(nm, ref_t), std::move(newval1), node_line_);
        }
    }
    if (!place_write_supported(place_node)) {
        error("compound-assignment target too deeply nested to assign in place "
              "yet; bind an intermediate (e.g. `let r = &mut <inner>; r[i] op= …`)");
        if (node.has_key(la::VALUE)) lower_expr(map_of(node.get(la::VALUE.code)));
        return builder().stmt_break(nullptr, "", node_line_);
    }
    // Uniform place-subsystem writability check — the SAME call the plain
    // place-assign path makes (`lower_place_assign`). Without it a compound
    // assignment asked only `place_write_supported` ("can the address machinery
    // lower this"), so `r.n = 1` was refused and `r.n += 1` admitted for
    // `r: &V` — one token apart. Asking the identical question here rather than
    // a narrower `&`-only one is deliberate: a second, weaker notion of
    // writability beside this one is how the gap opened in the first place.
    check_place_writable(place_node);
    auto place_read = lower_expr(place_node);    // eval #1 — current value
    TypeRef pt = expr_type(place_read);
    auto rhs = node.has_key(la::VALUE)
        ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
    if (node.has_key(la::VALUE))
        rhs = compound_rhs_first(std::move(rhs), pt, map_of(node.get(la::VALUE.code)), false);

    // User-defined `*Assign` dispatch on a struct place → op_assign(&mut place, rhs).
    if (pt && TypeRef(pt).kind() == LogosType::Kind::Struct) {
        std::string atrait, amethod;
        if (op_assign_trait_method(base_op, atrait, amethod)) {
            auto type_name = concrete_struct_name(pt);
            auto base_name = std::string(TypeRef(pt).struct_name());
            if (has_impl(atrait, type_name) ||
                (!base_name.empty() && has_impl(atrait, base_name))) {
                auto mangled = type_name + "__" + amethod;
                auto mut_ref_t = make_ref(true, pt);
                auto fit = find_op_assign_impl(mangled, mut_ref_t, pt, rhs);
                if (fit) {
                    auto addr = builder().addr_of_temp(lower_mut_place(place_node),  // eval #2 — &mut place
                                                       /*is_mut=*/true, mut_ref_t, BorrowOrigin::CompoundAssign);
                    std::vector<lir::LExprPtr> args;
                    // A by-value rhs is consumed by the call. PROBES.md 2026-09-15f-consumeland.
                    if (rhs && is_move_type(expr_type(rhs)) &&
                        !(fit->param_types.size() == 2 && fit->param_types[1] &&
                          is_ref_like(TypeRef(fit->param_types[1]).kind())))
                        mark_moved_expr(expr_ref_of(rhs));
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
    if (pt && rhs)
        expect_type(rhs, pt, CoercePos::Operand,
                    std::format("compound assignment to '{}': type mismatch —",
                                render_place_node(place_node)));
    widen_int_expr(rhs, pt, builder());
    auto newval = builder().bin_op(base_op, std::move(place_read), std::move(rhs),
                                   pt ? pt : error_t());
    auto addr = builder().addr_of_temp(lower_mut_place(place_node), /*is_mut=*/true,  // eval #2
                                       make_ref(true, pt ? pt : error_t()), BorrowOrigin::Desugar);
    track_write_move(newval);
    return builder().stmt_deref_write(std::move(addr), std::move(newval), node_line_);
}

lir_view::StmtRef SemaChecker::lower_assign(TinyMapView node) {
    return lower_assign_to(str_of(node.get(la::NAME.code)), node);
}

lir_view::StmtRef SemaChecker::lower_assign_to(std::string_view name, TinyMapView node) {
    auto var_type = lookup(name);
    if (!var_type) {
        error(std::format("assignment to undefined variable '{}'", name));
        lir::LExprPtr dummy = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code)))
            : error_expr();
        return builder().stmt_assign(std::string(name), std::move(dummy), node_line_);
    }
    // §6.2: `static mut X = …` write requires `unsafe` (Rust spec
    // `items.static.mut.safety`). Static muts are not in any local
    // scope, so `lookup_is_mut` returns false — gate FIRST so the
    // unsafe diagnostic isn't shadowed by "assignment to immutable".
    // Skip the static-mut classification if a local of the same name
    // shadows (else the global-by-name `module_static_muts_` set
    // misfires inside stdlib fns whose params share the user's
    // static name — the §6.2 S18 namespace pollution).
    bool is_static_mut = names_static_mut(name);
    if (is_static_mut) {
        if (!inside_unsafe_)
            error(std::format(
                "write to mutable static `{}` requires `unsafe` block "
                "(Rust `items.static.mut.safety`)", name));
    } else if (!lookup_is_mut(name)) {
        // A declared-uninitialised `let x: T;` may be assigned once without
        // `mut`; the borrow checker judges "assigned twice" per CFG path and
        // per binding (E0384).
        if (!is_deferred_init(name))
            error(std::format("assignment to immutable variable '{}'", name));
    }

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
        TypeRef(expr_type(rhs)).kind() == LogosType::Kind::Enum &&
        TypeRef(var_type).kind() == LogosType::Kind::Enum &&
        !TypeRef(var_type).type_args().empty() &&
        TypeRef(var_type).enum_name() == TypeRef(expr_type(rhs)).enum_name()) {
        auto rk = expr_ref_of(rhs).kind();
        bool is_enum_lit = rk == lir_schema::expr::Code::EnumLit ||
                           rk == lir_schema::expr::Code::EnumLitData;
        auto rhs_args = TypeRef(expr_type(rhs)).type_args();
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
    expect_type(rhs, var_type, CoercePos::PlaceWrite,
                std::format("assignment to '{}': type mismatch —", name));
    // ⛔ REFUTED 2026-08-27 OVER A LIVE SITE: 75 fires across the 423 ledger
    // compiles, CEILING 0, COST 0. The site is live and the check is a no-op
    // on it in BOTH directions. Predicted as a near-dead-in-effect site
    // ("ceiling 1, and I expect the fire count to be the finding") and that
    // held: the anon-region imports write through a PROJECTION (`x.b = y`,
    // `x.push(y)`), never through a bare local. Do not re-propose.
    // PROBE lifereg_varassign: the same missing consumer at the OTHER
    // assignment path. check_variance IS called at let-init (permissive=false)
    // and is not called one statement later at the re-assignment.
    // A FN-POINTER local's elided regions are binders, not inference variables:
    // re-assigning it a less general pointer is refused as at its `let`.
    if (var_type && rhs && TypeRef(var_type).kind() == LogosType::Kind::FnPtr)
        check_variance(expr_type(rhs), var_type,
                       std::format("assignment to '{}'", name),
                       /*permissive=*/false);
    else if (logos::probe::on("lifereg_varassign") && var_type && rhs)
        check_variance(expr_type(rhs), var_type,
                       std::format("assignment to '{}'", name),
                       /*permissive=*/false);
    // A write to a `static mut`: its declared elided regions are 'static. PROBES.md 2026-09-13d-staticdemand.
    if (is_static_mut && var_type && rhs)
        check_variance(expr_type(rhs), static_item_regions_(var_type),
                       std::format("assignment to '{}'", name),
                       /*permissive=*/false);
    // Implicit safe integer widening on assignment.
    if (var_type && is_integer_kind(TypeRef(var_type).kind()) && is_integer_kind(TypeRef(expr_type(rhs)).kind()) &&
        TypeRef(expr_type(rhs)).kind() != LogosType::Kind::IntLit &&
        TypeRef(expr_type(rhs)).kind() != LogosType::Kind::Enum &&
        can_widen_int(TypeRef(expr_type(rhs)).kind(), TypeRef(var_type).kind())) {
        widen_int_expr(rhs, var_type, builder());
    }
    // Check IntLit literal fits in the variable's declared type.
    if (TypeRef(expr_type(rhs)).kind() == LogosType::Kind::IntLit &&
        TypeRef(var_type).kind() != LogosType::Kind::Error) {
        if (auto v = get_intlit_value(rhs))
            if (!intlit_fits(*v, TypeRef(var_type).kind()))
                error(std::format("assignment to '{}': value {} does not fit in {}",
                      name, *v, type_str(var_type)));
    }
    // Check array literal elements against narrow array variable type.
    if (TypeRef(expr_type(rhs)).kind() == LogosType::Kind::Array &&
        TypeRef(var_type).kind() == LogosType::Kind::Array && TypeRef(var_type).elem()) {
        auto rhs_ref = expr_ref_of(rhs);
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
    if (TypeRef(expr_type(rhs)).kind() == LogosType::Kind::Tuple && TypeRef(var_type).kind() == LogosType::Kind::Tuple) {
        auto rhs_ref = expr_ref_of(rhs);
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
    // B8 drop-before-replace: if the LHS holds a live droppable value, its
    // destructor must run before being overwritten (Rust assignment semantics).
    // SOUND conditions (checked BEFORE the moved_vars_.erase below):
    //   • the type is droppable;
    //   • the var was declared WITH an initializer (decl_uninit_vars_ excludes
    //     it) → definitely-initialized at every reassignment (branches don't
    //     de-initialize), so we never free garbage;
    //   • the var is not currently moved-out, whole or partial (a moved value
    //     was already consumed → dropping it would double-free).
    // mlir-gen's gen_assign emits the drop AFTER evaluating the RHS (so
    // `x = f(x)` is safe) and BEFORE the store.
    bool drop_old = false;
    {
        std::string nm(name);
        bool droppable = var_type &&
            (TypeRef(var_type).owning_trait_object() ||
             !drop_fn_for(var_type).empty() ||
             has_droppable_fields(var_type));
        bool moved = moved_vars_.count(nm) != 0;
        if (!moved) {  // also reject a partial field-move (`x.f` consumed)
            std::string pre = nm + ".";
            for (auto& mv : moved_vars_)
                if (mv.size() > pre.size() && mv.compare(0, pre.size(), pre) == 0) { moved = true; break; }
        }
        // A declared-uninit var (`let mut x: T;`) gets a RUNTIME drop flag in
        // mlir-gen that decides drop-before-replace EXACTLY (drops the old value
        // iff the slot currently holds one — so `if c {x=a;} x=b;` drops `a` iff
        // c ran). The static drop_old below is only a hint — mlir-gen ignores it
        // for flag vars — but we still suppress it for the declared-uninit case
        // as defense-in-depth so no path drops garbage even absent the flag.
        drop_old = droppable && !decl_uninit_vars_.count(nm) && !moved;
    }
    // Re-assignment revives the variable (the old value was already consumed).
    moved_vars_.erase(std::string(name));
    // logos-core 2.7: definite-assignment — an assignment to `name` initialises
    // the var at this point (no longer "currently uninit"). decl_uninit_vars_
    // stays set (it's a permanent property of the declaration, governing the
    // drop_old hint at every reassignment); only currently_uninit_vars_ tracks
    // the CURRENT init state and is what var-read uses.
    currently_uninit_vars_.erase(std::string(name));
    // `x = Box::new(Ci)` / `x = z` into `x: Box<dyn Tr>`: unsized HERE, so the
    // value carries its own vtable and the old value drops as the `dyn` it is
    // (it dropped through the NEW value's concrete destructor).
    cast_to_expected_dyn(rhs, var_type);
    // RHS source consumed: `dst = src` for a move-type src moves src's bytes
    // into dst; src's scope-exit drop must be suppressed, else we double-free.
    track_write_move(rhs);
    // §6.2 statics (S25): `STATIC = v` writes through the global's address —
    // SDerefWrite rides the canonical place-store conventions (struct memcpy,
    // enum footprint, fat pairs) instead of stmt_assign's local-slot path.
    if (is_module_static_unshadowed(name)) {
        bool smut = module_static_muts_.count(std::string(name)) != 0;
        auto addr = builder().var_ref(static_addr_name(name),
                                      make_ptr(smut, var_type));
        return builder().stmt_deref_write(std::move(addr), std::move(rhs),
                                          node_line_);
    }
    return builder().stmt_assign(std::string(name), std::move(rhs), node_line_, drop_old);
}

// ADR 0030 S2 — ONE return judgment. `return e;`, a fn body's tail `e`, and a
// closure body's inferred tail all lower their operand here (the expected-type
// hints the return type supplies) and finish in finish_return_ (coercion,
// variance, E0507, literal fit, MOVES, statement temporaries). The tail used to
// build its own `stmt_return` and skipped the move marking, so
// `fn f(t: String) -> String { t }` dropped `t` at scope exit AND returned it.
lir::LExprPtr SemaChecker::lower_return_operand_(TinyMapView vnode) {
    lir::LExprPtr val = nullptr;
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
    // A closure literal that IS the returned value outlives this frame
    // (`fn mk() -> impl Fn() { move || k }`): its env must be heap.
    auto saved_ret_value_ = returned_closure_node_;
    returned_closure_node_ = unwrap_paren_node(vnode).ptr();
    struct RetValGuard_ { const void*& f; const void* v; ~RetValGuard_() { f = v; } } ret_val_guard_{returned_closure_node_, saved_ret_value_};
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
    // A tuple return type hints a tuple literal's elements, as a `let`
    // annotation does (`return ([4, 5], 1)` under `-> ([i64; 2], i64)`).
    auto saved_tuple_hint = hint_tuple_type_;
    if (ret_type_ && TypeRef(ret_type_).kind() == LogosType::Kind::Tuple)
        hint_tuple_type_ = ret_type_;
    // Box DerefMove in return position: `return *b;`.
    if (code_of(vnode) == la::DEREF)
        val = try_lower_box_deref_move(vnode);
    if (!val)
        val = lower_expr(vnode);
    hint_tuple_type_ = saved_tuple_hint;
    hint_enum_type_ = saved_hint;
    hint_struct_type_ = saved_struct_hint;
    hint_closure_formal_ = saved_closure_hint;
    hint_arr_elem_type_ = saved_arr_elem_hint;
    return val;
}

lir_view::StmtRef SemaChecker::finish_return_(lir::LExprPtr val, TinyMapView vnode,
                                             bool bind_temps) {
    // A RETURN IS A COERCION SITE: `return h.r;` with `h: &mut Inner`
    // and `-> &mut Vec<..>` reborrows `&mut *h.r` as rustc does, instead
    // of moving the `&mut` out from behind `h` (#465).
    if (val && ret_type_ && TypeRef(ret_type_).kind() == LogosType::Kind::MutRef)
        try_implicit_reborrow_mut(val, ret_type_);
    // G151-3: a non-capturing closure literal returned where a fn-ptr
    // type is expected coerces to that fn-ptr — the same coercion the
    // let-annotation and call-arg paths apply. Without this, `fn f() ->
    // fn()->T { return || ... }` errored "expected fn()->T, got ||->T".
    if (ret_type_ && TypeRef(ret_type_).kind() == LogosType::Kind::ImplTrait) {
        // Infer the single concrete hidden type from the FIRST return.
        // Every LATER return must produce the SAME concrete type — an
        // `impl Trait` return has exactly one hidden type (Rust E0308).
        // Without this, a second return of a different concrete type was
        // silently reinterpreted through the first type's layout/vtable,
        // a type-confusion misdispatch at runtime (corpus GAP10).
        TypeRef vt = expr_type(val);
        // The returned closure LITERAL — or the local bound to one
        // (escaping_closure_names_) — built its env on the heap: the
        // caller's value owns it.
        auto rv = unwrap_paren_node(vnode);
        if (vt && TypeRef(vt).kind() == LogosType::Kind::Closure &&
            (code_of(rv) == la::CLOSURE_EXPR ||
             (code_of(rv) == la::VAR_REF &&
              escaping_closure_names_.count(std::string(str_of(rv.get(la::NAME.code)))))) &&
            !TypeRef(vt).closure_owns_env()) {
            auto b = TypeRef(vt).to_builder();
            b.const_val = int64_t(uint64_t(b.const_val.value_or(0)) | TypeRef::OWNED_ENV_BIT);
            vt = pool_->alloc(std::move(b));
            builder().retype_expr(val, vt);
        }
        // A diverging value (`!`) fixes no hidden type.
        if (TypeRef(vt).kind() != LogosType::Kind::Error &&
            TypeRef(vt).kind() != LogosType::Kind::Never) {
            if (!impl_ret_type_inferred_)
                impl_ret_type_inferred_ = vt;
            else if (!types_equal(vt, impl_ret_type_inferred_))
                error(std::format(
                    "`impl Trait` return: every return must have the "
                    "same hidden concrete type — this returns `{}`, but "
                    "an earlier return produced `{}` (return a boxed "
                    "`dyn Trait` if the type must vary)",
                    type_str(vt), type_str(impl_ret_type_inferred_)));
        }
    } else if (ret_type_ &&
               !expect_type(val, ret_type_, CoercePos::Return,
                            "return type mismatch —")) {
        // diagnostic already emitted by the judgment
    } else if (ret_type_) {
        lt_static_yield() = true;
        check_variance(expr_type(val), ret_type_, "return type mismatch",
                       /*permissive=*/false);
        lt_static_yield() = false;
        // T1-12: dyn+auto bound at return coercion.
        check_dyn_auto_bounds_at_coercion(val, ret_type_);
        if (is_move_type(ret_type_) && is_unowned_move_source(val))
            error("cannot move out of a value behind a reference / out of an index (E0507)");
    }
    // Retype float literal to concrete return type.
    if (ret_type_ && TypeRef(expr_type(val)).kind() == LogosType::Kind::FloatLit &&
        (TypeRef(ret_type_).kind() == LogosType::Kind::F32 || TypeRef(ret_type_).kind() == LogosType::Kind::F64))
        builder().retype_expr(val, ret_type_);
    else if (TypeRef(expr_type(val)).kind() == LogosType::Kind::FloatLit)
        builder().retype_expr(val, prim(LogosType::Kind::F64));
    // Detect integer literals that don't fit in the return type.
    if (ret_type_ && TypeRef(expr_type(val)).kind() == LogosType::Kind::IntLit &&
        TypeRef(ret_type_).kind() != LogosType::Kind::Error) {
        if (auto v = get_intlit_value(val))
            if (!intlit_fits(*v, TypeRef(ret_type_).kind()))
                error(std::format("return: literal value {} does not fit in {}",
                      *v, type_str(ret_type_)));
    }
    // Detect array literal elements that don't fit in the return element type.
    if (ret_type_ && TypeRef(ret_type_).kind() == LogosType::Kind::Array && TypeRef(ret_type_).elem() &&
        TypeRef(expr_type(val)).kind() == LogosType::Kind::Array) {
        auto vr = expr_ref_of(val);
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
        TypeRef(expr_type(val)).kind() == LogosType::Kind::Tuple) {
        auto vr = expr_ref_of(val);
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
    //
    // ⚠ #110 R1 — this used to be a LOCAL LAMBDA, a hand copy of the
    // member `mark_moved_in_expr_recursive` (sema_impl.hpp) with the
    // same seven cases. The two drifted: the member grew a TupleIndex
    // case and this copy did not, so `return t.0;` marked nothing and
    // `t`'s scope-exit SDrop freed element 0 that the returned value
    // already owned — MEASURED as two destructor lines for one value,
    // on a plain concrete carrier with no enum and no generics. There
    // is now ONE walker: a new consumer position gets the whole set of
    // cases, and a new case reaches every consumer.
    if (val) mark_moved_in_expr_recursive(expr_ref_of(val));
    // If lowering the value hoisted statement-temporaries (a droppable
    // rvalue receiver `make().get()`), the temps must drop BEFORE the
    // return transfers control. lower_stmt emits drops AFTER the wrapped
    // statement, which for a `return` is dead code → the temp leaks.
    // Pre-bind the value to a synthetic `__rv` local; lower_stmt then
    // emits `let __t…; let __rv = <val>; drop __t…; return __rv;` so the
    // value is computed while the temps live, dropped before the return.
    if (bind_temps && val && cur_stmt_temp_hoist_ && !cur_stmt_temp_hoist_->empty()) {
        std::string rv = std::format("__rv_{}", destruct_counter_++);
        TypeRef rvt = expr_type(val);
        pending_ret_bind_ = std::make_tuple(rv, rvt, val);
        return builder().stmt_return(builder().var_ref(rv, rvt), node_line_);
    }
    return builder().stmt_return(std::move(val), node_line_);
}

void SemaChecker::collect_tail_matches_(TinyMapView block) {
    if (block.is_null()) return;
    if (code_of(block) != la::BLOCK) {        // a bare statement in tail position
        TinyMapView s = block;
        switch (code_of(s)) {
        case la::MATCH:
            tail_match_nodes_.insert(s.ptr());
            if (s.has_key(la::ITEMS)) {
                auto arms = arr_of(s.get(la::ITEMS.code));
                for (uint64_t i = 0; i < arms.size(); ++i) {
                    auto arm = map_of(arms.get(i));
                    if (!arm.is_null() && arm.has_key(la::BODY))
                        collect_tail_matches_(map_of(arm.get(la::BODY.code)));
                }
            }
            return;
        case la::IF:
        case la::IF_LET_CHAIN:
            if (s.has_key(la::THEN)) collect_tail_matches_(map_of(s.get(la::THEN.code)));
            if (s.has_key(la::ELSE)) collect_tail_matches_(map_of(s.get(la::ELSE.code)));
            return;
        case la::BLOCK_STMT:
        case la::UNSAFE_BLOCK:
            if (s.has_key(la::BODY)) collect_tail_matches_(map_of(s.get(la::BODY.code)));
            return;
        case la::TAIL_EXPR:   // `match` / `if` / a block as the tail EXPRESSION
            if (s.has_key(la::VALUE)) collect_tail_matches_(map_of(s.get(la::VALUE.code)));
            return;
        default:
            return;
        }
    }
    if (!block.has_key(la::ITEMS)) return;
    auto stmts = arr_of(block.get(la::ITEMS.code));
    for (int64_t si = (int64_t)stmts.size() - 1; si >= 0; --si) {
        auto s = map_of(stmts.get(si));
        if (s.is_null()) continue;
        collect_tail_matches_(s);
        return;
    }
}

lir_view::StmtRef SemaChecker::lower_return(TinyMapView node) {
    if (node.has_key(la::VALUE)) {
        AnyVal vav = node.get(la::VALUE.code);
        if (!vav.is_null()) {
            auto vnode = map_of(vav);
            return finish_return_(lower_return_operand_(vnode), vnode);
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

lir::Pattern SemaChecker::make_pat_wild(std::string_view name, bool is_mut) {
    lir::Pattern p;
    // Phase-1: a named wild is a binding — reserve its dense slot (`_` = none).
    uint32_t slot = (name == "_" || name.empty()) ? 0xFFFFFFFFu : reserve_pat_slot(name);
    p.mirror_ptr_ = lir_mirror_emit_pat_wild(*cur_prog_, name, slot, is_mut);
    return p;
}

lir::Pattern SemaChecker::build_pattern(TinyMapView pnode, TypeRef scrut_type) {
    // build_pattern_impl now sets mirror_ptr_ directly via per-kind direct
    // emitters; no bulk lir_mirror_emit_pat_node call needed here.
    //
    // Phase-1: reset the build-local pat_bind_slots_ at the TOP-level pattern
    // (depth 0) so Or-pattern alternatives within ONE pattern share slots while
    // distinct patterns (separate match arms / lets) start fresh. A depth guard
    // auto-detects the top entry regardless of caller.
    if (pattern_build_depth_++ == 0) {
        clear_pat_bind_slots();
        // E0416 at every pattern door (match, if/while-let, let, let-else,
        // for, parameters): a name bound twice in ONE pattern. An or-pattern's
        // alternatives bind the same names by design; the walker reads its
        // first alternative only. Reported once per pattern site (a door may
        // build the same pattern twice: a probe, then the lowering).
        std::vector<std::string> names;
        collect_ast_pat_bindings(pnode, names);
        std::set<std::string> seen;
        for (auto& nm : names) {
            if (nm.empty() || nm == "_") continue;
            if (!seen.insert(nm).second &&
                reported_dup_bindings_.insert(std::format("{}:{}", node_line_, nm)).second)
                error(std::format("identifier `{}` is bound more than once in the same pattern", nm));
        }
    }
    struct DepthGuard { uint32_t& d; ~DepthGuard() { --d; } } _g{pattern_build_depth_};
    return build_pattern_impl(pnode, scrut_type);
}

// `Self::V` / `Self::V(..)` / `Self::V { .. }` in a pattern inside an
// `impl Enum` body: `Self` IS the enclosing enum (Rust parity; the expression
// side already resolves it — G160-1). Without this every Self-spelled variant
// pattern was "unknown enum 'Self'".
void SemaChecker::resolve_self_enum_in_pattern(std::string& pename, const std::string& pvname) {
    if (pename != "Self" || pvname.empty()) return;
    auto sit = current_type_params_.find("Self");
    if (sit != current_type_params_.end() && sit->second &&
        TypeRef(sit->second).kind() == LogosType::Kind::Enum)
        pename = std::string(TypeRef(sit->second).enum_name());
}

lir::Pattern SemaChecker::build_pattern_variant(TinyMapView pnode, TypeRef scrut_type) {
    int32_t pc = code_of(pnode); (void)pc;
    auto pename = std::string(str_of(pnode.get(la::NAME.code)));
    auto pvname = std::string(str_of(pnode.get(la::FIELD.code)));
    resolve_self_enum_in_pattern(pename, pvname);
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
    if (!find_enum_by_name(pename).second) {
        auto ait = alias_find(pename);
        if (ait != type_aliases_.end() && ait->second.type &&
            TypeRef(ait->second.type).kind() == LogosType::Kind::Enum) {
            std::string tgt(TypeRef(ait->second.type).enum_name());
            if (!tgt.empty()) pename = std::move(tgt);
        }
    }
    int32_t disc = 0;
    auto [epkg_pv, esi_pv] = find_enum_by_name(pename);
    auto eit = esi_pv ? enums_.find(type_id(epkg_pv, pename)) : enums_.end();
    if (eit == enums_.end()) eit = enums_.find(type_id({}, pename));   // the root's
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
    p_.mirror_ptr_ = lir_mirror_emit_pat_variant(*cur_prog_, pename, pvname, disc);
    return p_;
}

lir::Pattern SemaChecker::build_pattern_variant_data(TinyMapView pnode, TypeRef scrut_type) {
    int32_t pc = code_of(pnode); (void)pc;
    auto pename = std::string(str_of(pnode.get(la::NAME.code)));
    auto pvname = std::string(str_of(pnode.get(la::FIELD.code)));
    resolve_self_enum_in_pattern(pename, pvname);
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
                            if (pos < tsi_p->fields.size())   // the privacy door, as the struct pattern's
                                check_pub_access(tsi_p->fields[pos].is_pub, tsi_p->package, std::to_string(pos));
                            lir::PatFieldBinding fb;
                            fb.field_name = std::to_string(pos);
                            // Default binding mode (spec pat.binding.default-by-ref-mode),
                            // exactly as the STRUCT door `TS { 0: a }` applies it.
                            const auto dbm = variant_data_dbm_;
                            variant_data_dbm_ = {};
                            auto leaf = bnode;
                            if (code_of(leaf) == la::PAT_OR && leaf.has_key(la::ITEMS)) {
                                auto a1 = arr_of(leaf.get(la::ITEMS.code));
                                if (a1.size() == 1) leaf = map_of(a1.get(0));
                            }
                            auto lflag = [&](const la::Key& k) {
                                return leaf.has_key(k) && leaf.get(k.code).is_value() &&
                                       leaf.get(k.code).as_value<uint8_t>() != 0;
                            };
                            const bool leaf_wild = code_of(leaf) == la::PAT_WILD.code && leaf.has_key(la::NAME);
                            const std::string leaf_nm = leaf_wild ? std::string(str_of(leaf.get(la::NAME.code))) : std::string();
                            const bool eligible = ftype && TypeRef(ftype).kind() != LogosType::Kind::Error &&
                                TypeRef(ftype).kind() != LogosType::Kind::TypeVar &&
                                TypeRef(ftype).kind() != LogosType::Kind::Array &&
                                TypeRef(ftype).kind() != LogosType::Kind::Slice;
                            if (dbm.ref && leaf_wild && !leaf_nm.empty() && leaf_nm != "_" &&
                                (lflag(la::IS_REF) || lflag(la::IS_MUT))) {
                                // Rust 2024: a written modifier under a by-reference default mode.
                                modifier_under_ref_scrutinee(leaf_nm, make_ref(dbm.mut_, ftype ? ftype : error_t()),
                                                             /*known_ref=*/true);
                                fb.sub.push_back(build_pattern(bnode, ftype));
                            } else if (dbm.ref && leaf_wild && !leaf_nm.empty() && leaf_nm != "_" && eligible) {
                                lir::Pattern rp;
                                rp.mirror_ptr_ = lir_mirror_emit_pat_ref_bind(
                                    *cur_prog_, leaf_nm, dbm.mut_, make_ref(dbm.mut_, ftype), reserve_pat_slot(leaf_nm));
                                fb.sub.push_back(std::move(rp));
                            } else {
                                fb.sub.push_back(build_pattern(bnode,
                                    (dbm.ref && !leaf_wild && eligible) ? make_ref(dbm.mut_, ftype) : ftype));
                            }
                            variant_data_dbm_ = dbm;
                            ps.fields.push_back(std::move(fb));
                        }
                        if (rest_idx < 0 && non_rest != arity)
                            error(std::format(
                                "this pattern has {} field{}, but the corresponding tuple struct '{}' has {} field{} (E0023)",
                                non_rest, non_rest == 1 ? "" : "s", pename, arity, arity == 1 ? "" : "s"));
                        else if (rest_idx >= 0 && non_rest > arity)
                            error(std::format(
                                "this pattern has {} field{}, but the corresponding tuple struct '{}' has {} field{} (E0023)",
                                non_rest, non_rest == 1 ? "" : "s", pename, arity, arity == 1 ? "" : "s"));
                    }
                }
            }
            auto mo = lir_mirror_emit_pat_struct(
                *cur_prog_, ps.struct_name, ps.fields, ps.has_rest);
            lir::Pattern p_;
            p_.mirror_ptr_ = mo;
            return p_;
        }
    }
    // G172-3: peel a (possibly generic) type-alias to an enum in a data-variant
    // pattern (`OptAlias::S(v)` where `type OptAlias<T> = Opt<T>`). Mirrors the
    // unit-variant peel in build_pattern_variant.
    if (!pvname.empty() && !find_enum_by_name(pename).second) {
        auto ait = alias_find(pename);
        if (ait != type_aliases_.end() && ait->second.type &&
            TypeRef(ait->second.type).kind() == LogosType::Kind::Enum) {
            std::string tgt(TypeRef(ait->second.type).enum_name());
            if (!tgt.empty()) pename = std::move(tgt);
        }
    }
    int32_t disc = 0;
    const SemaVariantInfo* vinfo = nullptr;
    auto [epkg_pvd, esi_pvd] = find_enum_by_name(pename);
    auto eit = esi_pvd ? enums_.find(type_id(epkg_pvd, pename)) : enums_.end();
    if (eit == enums_.end()) eit = enums_.find(type_id({}, pename));   // the root's
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
    SemaLifetimeSubst pat_lt_subst;
    {
        // Deref `&Enum` / `&mut Enum` / `*Enum` (match ergonomics) so the
        // per-position payload types are concrete even for a by-ref scrutinee
        // (needed by nested-pattern synth bindings — otherwise pat_field_type
        // returns the bare TypeVar and the guard/extraction miscompile).
        // Peel ALL `&`/`&mut`/`*` layers — Rust's default-binding-modes peel
        // through arbitrary depth so a pattern over `&&Option<T>` (or deeper)
        // unifies against the inner `Option<T>` shape. Pre-fix this peeled
        // exactly one layer, so `match &&Some(x) { Some(x) => x }` reported
        // a TypeVar mismatch (logos-core 4.3).
        TypeRef pat_scrut = scrut_type;
        while (pat_scrut &&
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
        if (vinfo && eit != enums_.end()) pat_lt_subst = enum_region_subst_(eit->second, pat_scrut);
    }
    // True when the scrutinee is by-reference (match ergonomics): a nested
    // payload binding then binds by-ref, so synth types wrap in &.
    // logos-core 4.3 (finish): track FULL peel depth + any-layer-mut so
    // binding types through arbitrary-depth &/&mut chains wrap correctly.
    // `pat_scrut_by_ref`/`pat_scrut_by_mut` retain their boolean semantics
    // (any depth ≥ 1 / any layer mut) for sites that only need the
    // qualitative answer; `pat_scrut_ref_depth` is the count used by
    // binding-type N-wrapping below.
    int  pat_scrut_ref_depth = 0;
    bool pat_scrut_by_ref = false;
    bool pat_scrut_by_mut = false;
    {
        TypeRef t = scrut_type;
        while (t && (TypeRef(t).kind() == LogosType::Kind::Ref ||
                     TypeRef(t).kind() == LogosType::Kind::MutRef) &&
               TypeRef(t).pointee()) {
            if (TypeRef(t).kind() == LogosType::Kind::MutRef) pat_scrut_by_mut = true;
            ++pat_scrut_ref_depth;
            t = TypeRef(t).pointee();
        }
        pat_scrut_by_ref = pat_scrut_ref_depth > 0;
    }
    auto pat_field_type = [&](size_t idx) -> TypeRef {
        if (!vinfo || idx >= vinfo->payload_types.size()) return error_t();
        auto pt = vinfo->payload_types[idx];
        return pat_subst.empty() && pat_lt_subst.empty() ? pt : subst_type_sema(pt, pat_subst, pat_lt_subst);
    };
    // ADR 0030 S3 (C-PAT): payload SUB-PATTERNS carried on the pattern itself
    // (PatVariantData SUBS, tested and bound by the one pattern tester), not a
    // synthesized binding + arm guard. This step carries the sub-patterns that
    // TEST a scalar and bind at most an `@` name over it — a literal (a string
    // literal too), a range, a
    // char / bool, an or-pattern of those, `n @ <those>` — and, over a by-value
    // scrutinee, `&<those>` and `&x` (x copies the referent). None of them moves
    // or drops a payload.
    // The synthesized guard typed its binding by the default binding mode and
    // compared `&char` to a char literal under a `&Enum` scrutinee.
    std::unordered_map<size_t, const uint8_t*> payload_subs;   // binding index → sub
    std::function<bool(TinyMapView)> carried_sub = [&](TinyMapView n) -> bool {
        const int32_t c = code_of(n);
        if (c == la::PAT_OR && n.has_key(la::ITEMS)) {
            auto alts = arr_of(n.get(la::ITEMS.code));
            if (alts.size() == 0) return false;
            for (uint64_t k = 0; k < alts.size(); ++k)
                if (!carried_sub(map_of(alts.get(k)))) return false;
            return true;
        }
        if (c == la::PAT_INT || c == la::PAT_NEG_INT || c == la::PAT_BOOL ||
            c == la::PAT_CHAR || c == la::PAT_CHAR_RANGE || c == la::PAT_RANGE || c == la::PAT_STR)
            return true;
        // Any binding mode: `mut n @ 1..=5` copies into a mutable binding,
        // `ref [mut] n @ …` binds the payload place (PatAt ref_mode).
        if (c == la::PAT_AT && n.has_key(la::VALUE) && n.has_key(la::NAME)) {
            TinyMapView in = map_of(n.get(la::VALUE.code));
            // `n @ _` (`n @ m`): the name binds the payload, nothing is tested.
            if (code_of(in) == la::PAT_WILD) return true;
            return carried_sub(in);
        }
        if (c == la::PAT_REF && n.has_key(la::VALUE) && !pat_scrut_by_ref) {
            // `&x` over an `&T` payload (`Some(&m)` of `iter().max()`): x copies
            // the referent — a move out of the reference is rustc's E0507,
            // which bind_pattern_ref's RefPat door reports.
            TinyMapView in = map_of(n.get(la::VALUE.code));
            if (code_of(in) == la::PAT_OR && in.has_key(la::ITEMS) && arr_of(in.get(la::ITEMS.code)).size() == 1)
                in = map_of(arr_of(in.get(la::ITEMS.code)).get(0));
            if (code_of(in) == la::PAT_WILD) {
                auto iflag = [&](const la::Key& k) {
                    return in.has_key(k) && in.get(k.code).is_value() && in.get(k.code).as_value<uint8_t>() != 0;
                };
                return !iflag(la::IS_REF);
            }
            return carried_sub(in);
        }
        return false;
    };
    // …and a STRUCTURAL sub-pattern (a tuple, a struct, a nested variant, an
    // array / slice pattern), whatever its binders do: under a by-reference
    // scrutinee they bind references, over a Copy payload they copy, and a
    // by-value binder of a non-Copy payload moves it out (see below).
    // `&<structural>` over a `&T` payload copies out of T — only a Copy T.
    auto carried_payload_sub = [&](TinyMapView n, TypeRef ftype) -> bool {
        if (carried_sub(n)) return true;
        TinyMapView m = n;
        if (code_of(m) == la::PAT_OR && m.has_key(la::ITEMS) && arr_of(m.get(la::ITEMS.code)).size() == 1)
            m = map_of(arr_of(m.get(la::ITEMS.code)).get(0));
        const int32_t c = code_of(m);
        auto structural = [](int32_t k) {
            return k == la::PAT_TUPLE || k == la::PAT_STRUCT || k == la::PAT_VARIANT_DATA || k == la::PAT_VARIANT ||
                   k == la::PAT_SLICE;
        };
        if (!ftype || TypeRef(ftype).kind() == LogosType::Kind::Error ||
            TypeRef(ftype).kind() == LogosType::Kind::TypeVar)
            return false;
        // A binder that MOVES a non-Copy payload out is carried too: pat_bind
        // binds it once, before the guard, as a direct payload binder is bound,
        // and mark_match_scrutinee_moved records the scrutinee's partial move
        // per leaf (`o.#<disc>.<i>.<j>`, the paths the enum drop glue skips).
        // The synthesized payload + body destructure it replaces destructured a
        // SECOND time for a guard, and both copies were dropped: `Some((a, k))
        // if k > 0` over `Option<(String, i64)>` freed the String twice.
        if (structural(c)) return true;
        // A bare name that is a VALUE (`Some(K)` for a const, `Some(None)`) is
        // a test carried like a literal; as a binder it matched everything.
        if (c == la::PAT_WILD && m.has_key(la::NAME) && !pat_byval_mut(m) &&
            !(m.has_key(la::IS_REF) && m.get(la::IS_REF.code).is_value() &&
              m.get(la::IS_REF.code).as_value<uint8_t>() != 0) &&
            bare_name_is_value_pattern_(str_of(m.get(la::NAME.code)), ftype))
            return true;
        // `n @ <structural>` (and `ref n @ …`): the name binds the place the
        // structural sub matches, both carried the same way.
        if (c == la::PAT_AT && m.has_key(la::VALUE)) {
            TinyMapView in = map_of(m.get(la::VALUE.code));
            if (code_of(in) == la::PAT_OR && in.has_key(la::ITEMS) && arr_of(in.get(la::ITEMS.code)).size() == 1)
                in = map_of(arr_of(in.get(la::ITEMS.code)).get(0));
            if (structural(code_of(in))) return true;
        }
        if (c == la::PAT_REF && m.has_key(la::VALUE) && !pat_scrut_by_ref &&
            (TypeRef(ftype).kind() == LogosType::Kind::Ref || TypeRef(ftype).kind() == LogosType::Kind::MutRef) &&
            TypeRef(ftype).pointee() && !is_move_type(TypeRef(ftype).pointee())) {
            TinyMapView in = map_of(m.get(la::VALUE.code));
            if (code_of(in) == la::PAT_OR && in.has_key(la::ITEMS) && arr_of(in.get(la::ITEMS.code)).size() == 1)
                in = map_of(arr_of(in.get(la::ITEMS.code)).get(0));
            return structural(code_of(in));
        }
        return false;
    };
    // The sub sees the container's default binding mode through its type
    // (`&T` under a by-reference scrutinee), as the tuple-struct door hands it.
    auto build_payload_sub = [&](TinyMapView sub, TypeRef ftype) -> const uint8_t* {
        const bool eligible = ftype && TypeRef(ftype).kind() != LogosType::Kind::Error &&
                              TypeRef(ftype).kind() != LogosType::Kind::TypeVar;
        TypeRef st = (pat_scrut_by_ref && eligible) ? make_ref(pat_scrut_by_mut, ftype) : ftype;
        const auto saved = variant_data_dbm_;
        variant_data_dbm_ = {};
        lir::Pattern sp = build_pattern(sub, st);
        variant_data_dbm_ = saved;
        return sp.mirror_ptr_;
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
    // Set by a caller whose synth is a `ref n @ sub` name: it binds `&T`, so a
    // scalar guard compares the POINTEE (as under a by-reference scrutinee).
    bool synth_forced_ref = false;
    auto synth_refutable_inner =
        [&](TinyMapView sub, TypeRef ftype, std::string_view ctx_field,
            std::string_view explicit_name = {}) -> std::string {
        synth_wants_ref = false;
        logos::probe::census("s3.synth." + std::to_string(code_of(sub)));
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
            // A STRUCT-SHAPED variant (`Inn::S { f }`) keeps its fields under
            // ITEMS: a shorthand field binds its name, a `f: sub` binds what
            // `sub` binds.
            if (dn.has_key(la::variant::IS_STRUCT_SHAPE) &&
                dn.get(la::variant::IS_STRUCT_SHAPE.code).as_value<int32_t>() != 0) {
                std::vector<std::string> names;
                collect_ast_pat_bindings(dn, names);
                for (auto& n : names) if (!n.empty() && n != "_") return true;
                return false;
            }
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
        // A tuple / struct sub-pattern with a refutable part inside
        // (`Some(P { x: 1, y })`, `A((1, y))`) takes the K4 route below: a
        // guard match on the payload plus a body re-extraction of its binders.
        const bool refut_aggregate = (sc == la::PAT_STRUCT || sc == la::PAT_TUPLE) &&
                                     !ast_pat_irrefutable(sub);
        if (sc == la::PAT_VARIANT ||
            (sc == la::PAT_VARIANT_DATA && current_pat_refutable_guards_) ||
            (sc == la::PAT_OR && current_pat_refutable_guards_) ||
            (refut_aggregate && current_pat_refutable_guards_)) {
            // K4: nested variant pattern carrying bindings (e.g.
            // `Some(Some(v))`). Bind the outer payload to `synth`, gate the arm
            // with a guard match `match synth { <sub> => <inner_check>, _ =>
            // false }` (so sibling arms like `Some(None)` dispatch correctly),
            // and register a body let-else that re-extracts the inner bindings
            // from `synth` (the guard guarantees the match → the else is dead).
            // Composes to arbitrary depth: the deeper checks ride the matching
            // arm's VALUE (never an arm GUARD), and the body let-else recurses.
            if ((sc == la::PAT_VARIANT_DATA && data_has_binding(sub)) || refut_aggregate) {
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
                    // logos-core 4.3: wrap N times for arbitrary-depth
                    // scrutinees. Outermost layer carries the strictest
                    // (mut-if-any) mutability; inner layers stay shared
                    // (Rust's default binding modes — the outer-most
                    // binding-mode determines the binding's mutability).
                    for (int li = 0; li < pat_scrut_ref_depth; ++li)
                        rt = make_ref(li == pat_scrut_ref_depth - 1 ? pat_scrut_by_mut : false, rt);
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
                // This definition only types the guard's read: it is a bitwise
                // COPY of the payload, and the arm's own binding of the same name
                // (a fresh slot, so it shadows this one) is the owner. Unmarked,
                // the shadowed copy was destroyed too — `Some(Some(s)) => return
                // s.len()` freed the String twice.
                mark_moved(synth);
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
                // T2-26: under match ergonomics the synth payload may bind
                // by-reference (`&rt`); compare the POINTEE, not the pointer.
                // The guard uses builder().bin_op directly (no auto-deref), so
                // deref explicitly when the binding's real type is a reference.
                auto synth_val = [&]() -> lir::LExprPtr {
                    // The synth is defined by the caller AFTER this returns, so
                    // lookup() is null here — derive its by-ref-ness from the
                    // scrutinee mode. Under match ergonomics the payload binds
                    // `&rt`; deref to compare the pointee.
                    if (pat_scrut_by_ref || synth_forced_ref) {
                        TypeRef rty = make_ref(false, rt);
                        return builder().deref(builder().var_ref(synth, rty), rt);
                    }
                    return builder().var_ref(synth, rt);
                };
                auto lo_lit = builder().lit_int(lo, rt);
                auto hi_lit = builder().lit_int(hi, rt);
                auto ge = builder().bin_op(">=", synth_val(),
                                           std::move(lo_lit), bool_t());
                auto le = builder().bin_op("<=", synth_val(),
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
        lir::LExprPtr value = nullptr;
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
            int64_t v = decode_char_lit_(str_of(sub.get(la::VALUE.code)));
            value = builder().lit_int(v, prim(LogosType::Kind::Char));
        } else {
            return std::string();  // not a supported refutable
        }
        if (current_pat_refutable_guards_) {
            TypeRef rt = (ftype && TypeRef(ftype).kind() != LogosType::Kind::Error)
                ? ftype
                : (value ? expr_type(value) : error_t());
            auto vref = synth_forced_ref
                ? builder().deref(builder().var_ref(synth, make_ref(false, rt)), rt)
                : builder().var_ref(synth, rt);
            auto guard = builder().bin_op("==", std::move(vref),
                                          std::move(value), bool_t());
            current_pat_refutable_guards_->push_back(std::move(guard));
        }
        return synth;
    };
    // THE STRUCT-SHAPED PAYLOAD DOOR FILLS THE SAME THREE PARALLEL FLAG VECTORS
    // THE TUPLE DOOR DOES. Until 2026-09-16d it filled NONE of them: the loop
    // that fills binding_is_ref / binding_is_mut / binding_from_wild below is
    // guarded `!pat_is_struct_shape`, so for `match &e { E::V { f } }` every
    // `k < binding_from_wild.size()` test in the bind_ref_modes loop read FALSE
    // and THE DEFAULT BINDING MODE WAS DEAD AT THIS DOOR — the payload bound by
    // value out of a borrow and its destructor ran TWICE. Measured over a hand
    // battery with a rustc 1.98.1 twin for every program: eleven spellings
    // double-dropped, including the bare `match &e { E::V { f } }` with no
    // container door and no nested sub. PROBES.md 2026-09-16d-landrulings.
    // Positional, parallel to `by_pos` (which is indexed by PAYLOAD POSITION,
    // not by source order), and appended to the three vectors below.
    std::vector<bool> sshape_is_ref, sshape_is_mut, sshape_from_wild;
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
        // Parallel to `by_pos`, same indexing. `bp_wild` is true ONLY for a
        // real WRITTEN plain binder — never for "_" and never for a synthesized
        // refutable-inner slot, which is the separating fact the bind_ref_modes
        // loop needs (a synth name is not a binder the user can be blamed for).
        std::vector<bool> bp_is_ref(arity, false);
        std::vector<bool> bp_is_mut(arity, false);
        std::vector<bool> bp_from_wild(arity, false);
        auto node_flag = [](TinyMapView n, const la::Key& k) {
            return n.has_key(k) && n.get(k.code).is_value() &&
                   n.get(k.code).as_value<uint8_t>() != 0;
        };
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
                        // `E::V { ref f }` / `E::V { mut f }`: the modifier sits
                        // on the PAT_FIELD node itself in the shorthand form.
                        bp_is_ref[idx] = node_flag(fnode, la::IS_REF);
                        bp_is_mut[idx] = node_flag(fnode, la::IS_MUT);
                        bp_from_wild[idx] = fname != "_";
                        continue;
                    }
                    auto sub = map_of(fnode.get(la::VALUE.code));
                    int32_t sc = code_of(sub);
                    if (carried_payload_sub(sub, pat_field_type(idx))) {   // ADR 0030 S3: carried as a sub-pattern
                        payload_subs[idx] = build_payload_sub(sub, pat_field_type(idx));
                        by_pos[idx] = "_";
                        bp_is_ref[idx] = false;
                        bp_is_mut[idx] = false;
                        bp_from_wild[idx] = false;
                        continue;
                    }
                    if (sc == la::PAT_WILD) {
                        std::string bn = sub.has_key(la::NAME)
                            ? std::string(str_of(sub.get(la::NAME.code)))
                            : std::string("_");
                        by_pos[idx] = bn;
                        bp_is_ref[idx] = node_flag(sub, la::IS_REF);
                        bp_is_mut[idx] = node_flag(sub, la::IS_MUT);
                        bp_from_wild[idx] = bn != "_";
                    } else if ((sc == la::PAT_STRUCT || sc == la::PAT_TUPLE) &&
                               current_pat_nested_subs_ && ast_pat_irrefutable(sub)) {
                        // Same as the tuple-shape door: the payload binds to a
                        // synth, the sub-pattern destructures it in the body.
                        std::string synth = std::format("__pat_pld_{}_{}", pvname, tmp_var_count_++);
                        by_pos[idx] = synth;
                        bp_is_ref[idx] = variant_data_dbm_.ref;
                        bp_is_mut[idx] = variant_data_dbm_.ref && variant_data_dbm_.mut_;
                        bp_from_wild[idx] = false;
                        current_pat_nested_subs_->push_back({synth, sub});
                    } else if (sc == la::PAT_INT || sc == la::PAT_NEG_INT ||
                               sc == la::PAT_BOOL || sc == la::PAT_CHAR ||
                               sc == la::PAT_RANGE ||
                               sc == la::PAT_VARIANT ||
                               sc == la::PAT_VARIANT_DATA ||
                               sc == la::PAT_STRUCT || sc == la::PAT_TUPLE) {
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
                            // Exactly the tuple door's treatment of a synth slot:
                            // the nested-variant synth binds BY REFERENCE when
                            // synth_refutable_inner says so, and is NOT a written
                            // binder — so from_wild stays false and the
                            // default-binding-mode wrap never touches it.
                            bp_is_ref[idx] = synth_wants_ref;
                            bp_is_mut[idx] = synth_wants_ref && pat_scrut_by_mut;
                            bp_from_wild[idx] = false;
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
        for (size_t k = 0; k < by_pos.size(); ++k) {
            bindings.push_back(std::move(by_pos[k]));
            sshape_is_ref.push_back(bp_is_ref[k]);
            sshape_is_mut.push_back(bp_is_mut[k]);
            sshape_from_wild.push_back(bp_from_wild[k]);
        }
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
    if (pat_is_struct_shape) {
        binding_is_ref    = std::move(sshape_is_ref);
        binding_is_mut    = std::move(sshape_is_mut);
        binding_from_wild = std::move(sshape_from_wild);
    }
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
                    if (carried_payload_sub(bnode, pat_field_type(bindings.size()))) {   // ADR 0030 S3: carried as a sub-pattern
                        const size_t pos = bindings.size();
                        payload_subs[pos] = build_payload_sub(bnode, pat_field_type(pos));
                        bindings.push_back("_");
                        binding_is_ref.push_back(false);
                        binding_is_mut.push_back(false);
                        binding_from_wild.push_back(false);
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
                        binding_is_mut.push_back(is_mut);
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
                        (bc == la::PAT_STRUCT || bc == la::PAT_TUPLE) && ast_pat_irrefutable(bnode);
                    if (sub_is_irrefutable && current_pat_nested_subs_) {
                        std::string synth = std::format(
                            "__pat_pld_{}_{}", pvname, tmp_var_count_++);
                        bindings.push_back(synth);
                        // Under a by-reference default binding mode the payload is
                        // BORROWED, not moved: the synth binds `&P` and the body
                        // destructure binds references into it (the payload stays
                        // the scrutinee's; binding it by value dropped it twice).
                        binding_is_ref.push_back(variant_data_dbm_.ref);
                        binding_is_mut.push_back(variant_data_dbm_.ref && variant_data_dbm_.mut_);
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
                        synth_forced_ref = bnode.has_key(la::IS_REF) &&
                            bnode.get(la::IS_REF.code).is_value() &&
                            bnode.get(la::IS_REF.code).as_value<uint8_t>() != 0;
                        std::string r = synth_refutable_inner(
                            subnode, pat_field_type(j),
                            std::format("{}", j), atname);
                        synth_forced_ref = false;
                        if (!r.empty()) {
                            auto atflag = [&](const la::Key& k) {
                                return bnode.has_key(k) && bnode.get(k.code).is_value() &&
                                       bnode.get(k.code).as_value<uint8_t>() != 0;
                            };
                            // Rust 2024 pat.binding.modifier-requires-move-mode at the
                            // nested `@`-BINDING door. The `mut` spelling is already
                            // refused downstream (`binding_is_mut` below feeds the
                            // `bind_ref_modes` ask), but a written `ref` is not: the
                            // push below forces `binding_is_ref` to false, so nothing
                            // downstream can see the keyword at all. Asked here, where
                            // the AST node still carries it.
                            if (pat_scrut_by_ref && atflag(la::IS_REF))
                                modifier_under_ref_scrutinee(atname, scrut_type, /*known_ref=*/true);
                            bindings.push_back(atname);
                            // `ref n @ sub` / `ref mut n @ sub` bind the payload BY
                            // REFERENCE, as a `ref n` payload binder does.
                            binding_is_ref.push_back(atflag(la::IS_REF));
                            binding_is_mut.push_back(atflag(la::IS_MUT));
                            binding_from_wild.push_back(true);  // named binding
                            continue;
                        }
                        // `V(y @ W { .. })` / `V(t @ (a, _))`: an IRREFUTABLE
                        // structural sub needs no guard. The `@` name takes the
                        // payload exactly as the synth of a bare structural sub
                        // (above), and the sub's own binders destructure from it.
                        int32_t sc = code_of(subnode);
                        if (sc == la::PAT_OR && subnode.has_key(la::ITEMS) &&
                            arr_of(subnode.get(la::ITEMS.code)).size() == 1) {
                            subnode = map_of(arr_of(subnode.get(la::ITEMS.code)).get(0));
                            sc = code_of(subnode);
                        }
                        if ((sc == la::PAT_STRUCT || sc == la::PAT_TUPLE) && current_pat_nested_subs_ &&
                            ast_pat_irrefutable(subnode)) {
                            // A WRITTEN binder: under a `&` scrutinee the default
                            // binding mode (below) makes it `&W`, as a bare name.
                            bindings.push_back(atname);
                            binding_is_ref.push_back(false);
                            binding_is_mut.push_back(pat_byval_mut(bnode));
                            binding_from_wild.push_back(true);
                            current_pat_nested_subs_->push_back({atname, subnode});
                            continue;
                        }
                    }
                    // P4-pm-01 refutable inner (tuple-shape parallel) —
                    // `Option::Some(1)` / `Result::Err(false)` / `Num(1..=5)`.
                    // Synth a binding + emit `__refut_… == <value>` (or a range
                    // `>= && <=`) as an arm guard.
                    if (bc == la::PAT_INT || bc == la::PAT_NEG_INT ||
                        bc == la::PAT_BOOL || bc == la::PAT_CHAR ||
                        bc == la::PAT_RANGE ||
                        bc == la::PAT_VARIANT || bc == la::PAT_VARIANT_DATA ||
                        bc == la::PAT_OR || bc == la::PAT_STRUCT || bc == la::PAT_TUPLE) {
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
        // logos-core 4.3 (finish): peel ALL Ref/MutRef/Ptr layers
        // (`&&Option<T>` and deeper) — match ergonomics through arbitrary
        // depth.
        TypeRef enum_scrut = scrut_type;
        while (enum_scrut &&
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
        // D2: an arm payload must NEVER bind at the enum's DECLARED type
        // parameter. `subst` above is populated only when the (peeled)
        // scrutinee is a Kind::Enum carrying type_args; every other shape —
        // an Error-typed scrutinee (ADR 0021 factory-backed deferral, or a
        // cascade after an earlier hard error), a scrutinee whose args were
        // not inferred yet, a partially-applied generic — falls through with
        // `subst` empty or incomplete and used to hand the DECLARED `T` to
        // define(). The arm body then reports `type parameter 'T' has no
        // trait bound providing method '…'`, a HARD error that aborts the
        // unit BEFORE the post-drain re-sema round that would have typed the
        // chain correctly.
        //
        // `error_t()` is the honest answer for an unresolved payload: it
        // rides the existing error-propagation rule (uses of an error-typed
        // value are silent), so round 0 stays quiet and the strict round
        // types the arm for real. Legitimate `T` bindings are unaffected —
        // inside `fn f<T>(o: Option<T>)` the scrutinee IS `Option<T>` with
        // type_args = [T], so `subst` maps the enum's param onto the fn's
        // own param and the result below is NOT an unresolved mention.
        auto mentions_unresolved_param = [&](TypeRef t) {
            if (eit == enums_.end()) return false;
            auto& einfo2 = eit->second;
            if (einfo2.type_params.empty()) return false;
            auto rec = [&](auto&& self, TypeRef tv) -> bool {
                if (!tv) return false;
                if (TypeRef(tv).kind() == LogosType::Kind::TypeVar) {
                    auto nm = std::string(TypeRef(tv).type_var_name());
                    for (auto& tp : einfo2.type_params)
                        if (tp.name == nm && subst.find(nm) == subst.end())
                            return true;
                    return false;
                }
                for (auto a : TypeRef(tv).type_args()) if (self(self, a)) return true;
                if (TypeRef(tv).pointee() && self(self, TypeRef(tv).pointee())) return true;
                if (TypeRef(tv).elem() && self(self, TypeRef(tv).elem())) return true;
                for (auto e : TypeRef(tv).tuple_elems()) if (self(self, e)) return true;
                for (auto p : TypeRef(tv).closure_params()) if (self(self, p)) return true;
                if (TypeRef(tv).closure_ret() && self(self, TypeRef(tv).closure_ret())) return true;
                return false;
            };
            return rec(rec, t);
        };
        // The payload's regions are the SCRUTINEE's: `match e` with `e: &E<'a>`
        // binds `V { r }`'s `r: &'s i64` field as `&&'a i64`, not at the
        // declaration's `'s`.
        const SemaLifetimeSubst lt_subst =
            eit != enums_.end() ? enum_region_subst_(eit->second, enum_scrut) : SemaLifetimeSubst{};
        for (auto pt : vinfo->payload_types) {
            auto ct = subst.empty() && lt_subst.empty() ? pt : subst_type_sema(pt, subst, lt_subst);
            if (TypeRef(ct).kind() == LogosType::Kind::Void) continue;  // () unit — no field
            if (mentions_unresolved_param(ct)) ct = error_t();
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
        payload_subs.clear();   // an all-unit payload has no field to test
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
    // logos-core 4.3 (finish): the default-binding-mode wrap counts ALL ref
    // layers in the scrutinee, not just the outermost. `pat_scrut_ref_depth`
    // already tracks the count; `pat_scrut_by_mut` records the strictest
    // (mut-if-any) mutability.
    bool default_ref = pat_scrut_by_ref;
    bool default_mut = pat_scrut_by_mut;
    int  default_depth = pat_scrut_ref_depth;
    // THE BINDING MODE, carried into the LIR (pat_keys::BINDING_REF_MODES).
    // borrow_check needs to know that `Opt::Some(ref r)` names a PLACE INSIDE
    // the scrutinee rather than copying a value out of it, and the binding
    // TYPE cannot say so: `enum E { V(&i64) }` matched as `E::V(p)` gives `p`
    // the very same `&i64`. Recorded HERE, where the `ref` keyword and the
    // default-binding-mode decision both already live, rather than
    // reconstructed downstream from a type comparison.
    std::vector<uint32_t> bind_ref_modes(binding_types.size(), 0u);
    for (size_t k = 0; k < binding_types.size(); ++k) {
        if (!binding_types[k]) continue;
        bool explicit_ref = k < binding_is_ref.size() && binding_is_ref[k];
        // Rust 2024 pat.binding.modifier-requires-move-mode: a written `mut`
        // under a by-reference default binding mode is an error (the written
        // `ref` / `ref mut` half is 2021 today — soundness queue).
        if (default_ref && !explicit_ref &&
            k < binding_from_wild.size() && binding_from_wild[k] &&
            k < binding_is_mut.size() && binding_is_mut[k]) {
            modifier_under_ref_scrutinee(bindings[k], scrut_type, /*known_ref=*/true);
            bind_ref_modes[k] = 0x10u;  // recover as 2021 did: `mut` resets the mode to move
            continue;
        }
        if (explicit_ref) {
            bool is_mut = k < binding_is_mut.size() && binding_is_mut[k];
            // Rust 2024 pat.binding.modifier-requires-move-mode, the `ref` /
            // `ref mut` two thirds, at the VARIANT-PAYLOAD door.
            // ⚠ TWO NAMES FOR ONE PREDICATE. `explicit_ref` alone REFUSES LEGAL
            // CODE: `binding_is_ref` is also set by the compiler's OWN
            // nested-variant synthesis (`synth_wants_ref`), so
            // `match &e { Outer::W(Option::Some(a)) }` — no modifier written
            // anywhere — would be blamed under the synthesized name
            // `__refut_W_0_0`. The separating fact is `binding_from_wild[k]`:
            // only a real WRITTEN binder sets it, the synth pushes false, and
            // the `mut` third already asks it. Measured: no cost column in the
            // harness separates the two forms — only a hand program does.
            if (default_ref &&
                k < binding_from_wild.size() && binding_from_wild[k])
                modifier_under_ref_scrutinee(bindings[k], scrut_type, /*known_ref=*/true);
            bind_ref_modes[k] = is_mut ? 2u : 1u;
            binding_types[k] = make_ref(is_mut, binding_types[k]);
        } else if (default_ref &&
                   k < binding_from_wild.size() && binding_from_wild[k]) {
            // T2-26 (full RFC 2005): default binding modes shift on the
            // SCRUTINEE, never the payload's type — under a `&`/`&mut`
            // scrutinee EVERY payload binds by-reference, regardless of what
            // the field holds: int, struct, TypeVar `T`, `&T`, or raw pointer.
            //   • `T`       payload ⟹ `&T`        (the common case)
            //   • `&T`      payload ⟹ `&&T`       (depth-N field/method autoderef)
            //   • `*const T` payload ⟹ `&*const T` (value is `**p`)
            // Arithmetic peels ONE ref layer (Rust's `&i32` operator impls), so
            // by-value uses spell `**x`/`*x` as in Rust. A bare TypeVar payload
            // is NO LONGER carved out: it binds `&T` like Rust. The historical
            // `OptionIter<&mut … T>` blow-up was NOT inherent to TypeVar binding
            // — it came from `Option::take`/`replace` rebuilding `Some(v)` from a
            // `&mut T` binding (a non-Rust shape: in Rust those use
            // `mem::replace`). Those stdlib bodies now do a value-level move, so
            // no generic body re-wraps its own payload into a type-arg. A user
            // body that DID rebuild `Some(v)` as `Option<T>` from a `&mut T`
            // binding is a type error (Option<&mut T> vs Option<T>), same as Rust.
            // logos-core 4.3: wrap N times to match the scrutinee's ref-chain
            // depth (the outermost layer takes the strictest mutability).
            for (int li = 0; li < default_depth; ++li)
                binding_types[k] = make_ref(
                    li == default_depth - 1 ? default_mut : false,
                    binding_types[k],
                    // LANDED 2026-09-02p (was PROBE stfacts P): the outermost layer
                    // IS the scrutinee's reference, so it carries its region.
                    li == default_depth - 1 ? std::string(TypeRef(scrut_type).lifetime())
                                            : std::string());
            // 3/4, NOT 1/2: see pat_keys::BINDING_REF_MODES. The place a
            // default-mode binding names sits under the scrutinee's implicit
            // deref, which is not where a written `ref` puts it.
            bind_ref_modes[k] = default_mut ? 4u : 3u;
        } else if (k < binding_is_mut.size() && binding_is_mut[k]) {
            bind_ref_modes[k] = 0x10u;  // by value, written `mut`
        }
    }
    // Phase-1: reserve a dense slot per binding (NO_SLOT for `_`), parallel to
    // `bindings`. reserve_pat_slot dedups repeated names across Or-alternatives.
    std::vector<uint32_t> bind_slots;
    bind_slots.reserve(bindings.size());
    for (auto& b : bindings)
        bind_slots.push_back(b == "_" ? 0xFFFFFFFFu : reserve_pat_slot(b));
    if (bind_ref_modes.size() < bindings.size())
        bind_ref_modes.resize(bindings.size(), 0u);
    std::vector<const uint8_t*> subs_vec;
    if (!payload_subs.empty()) {
        subs_vec.assign(bindings.size(), nullptr);
        for (auto& [bi, sp] : payload_subs)
            if (bi < subs_vec.size()) subs_vec[bi] = sp;
    }
    auto mo = lir_mirror_emit_pat_variant_data(
        *cur_prog_, pename, pvname, disc, bindings, binding_types, bind_slots,
        bind_ref_modes, subs_vec);
    lir::Pattern p_;
    p_.mirror_ptr_ = mo;
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
    if (raw_byte_string_bytes_(sv, bytes)) {
    } else if (sv.size() >= 3 && sv.front() == 'b' && sv[1] == '"' && sv.back() == '"') {
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
        sp.mirror_ptr_ = lir_mirror_emit_pat_int(*cur_prog_, (int64_t)b);
        prefix.push_back(std::move(sp));
    }
    std::vector<lir::Pattern> rest;     // empty — exact match, no `..`
    std::vector<lir::Pattern> suffix;   // empty
    auto mo = lir_mirror_emit_pat_slice(*cur_prog_, prefix, rest, suffix);
    lir::Pattern p_;
    p_.mirror_ptr_ = mo;
    return p_;
}

int SemaChecker::at_or_fanout_alts(writ::TinyMapView lhs) {
    // The grammar wraps a whole arm pattern in a single-alt PAT_OR.
    if (code_of(lhs) == la::PAT_OR && lhs.has_key(la::ITEMS)) {
        auto a = arr_of(lhs.get(la::ITEMS.code));
        if (a.size() == 1) lhs = map_of(a.get(0));
    }
    if (code_of(lhs) != la::PAT_AT || !lhs.has_key(la::VALUE)) return 0;
    auto sub = map_of(lhs.get(la::VALUE.code));
    if (code_of(sub) != la::PAT_OR || !sub.has_key(la::ITEMS)) return 0;
    auto alts = arr_of(sub.get(la::ITEMS.code));
    if (alts.size() < 2) return 0;
    for (uint64_t k = 0; k < alts.size(); ++k) {
        int32_t c = code_of(map_of(alts.get(k)));
        if (c != la::PAT_INT && c != la::PAT_BOOL && c != la::PAT_CHAR) return (int)alts.size();
    }
    return 0;
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
    p_.mirror_ptr_ = mo;
    return p_;
}

// T1-8 (E0408 analog) — AST-level binding-name collector for patterns.
// All name-introducing pattern forms funnel into four node shapes:
// PAT_WILD{NAME} (bare/ref/mut idents), PAT_AT{NAME, VALUE},
// PAT_FIELD{NAME[, VALUE]} (struct shorthand binds NAME), and
// PAT_REST{NAME?} (`xs @ ..`). Composites recurse; PAT_OR descends the
// FIRST alternative only (the nested-Or builder enforces its own
// consistency).

// The coercions a WRITE to a typed place performs — shared by assignment,
// field writes, index/deref writes and the struct-literal field paths, and
// mirroring what `let x: T = e` did. Order matters and matches the let path:
// reborrow first (it changes a `&mut` into the shape the rest expect), then
// the rewrites, each tried only while the types still disagree.
// NOTE: being folded into expect_type (S1); new positions must call
// expect_type, not this.
bool SemaChecker::apply_place_coercions(lir::LExprPtr& rhs, TypeRef target) {
    if (!rhs || !target) return false;
    if (TypeRef(target).kind() == LogosType::Kind::Error) return false;
    if (TypeRef(expr_type(rhs)).kind() == LogosType::Kind::Error) return false;
    bool changed = false;
    if (TypeRef(target).kind() == LogosType::Kind::MutRef ||
        TypeRef(target).kind() == LogosType::Kind::Ref ||
        TypeRef(target).kind() == LogosType::Kind::Ptr) {
        if (try_implicit_reborrow_mut(rhs, target)) changed = true;
    }
    if (types_compatible(expr_type(rhs), target)) return changed;
    if (try_struct_unsize_coerce(rhs, target))      return true;
    if (try_coerce_array_ref_to_slice(rhs, target)) return true;
    if (try_coerce_slice_to_array_ref(rhs, target)) return true;
    if (try_coerce_closure_to_fnptr(rhs, target))   return true;
    return changed;
}

void SemaChecker::collect_ast_pat_bindings(TinyMapView pat,
                                           std::vector<std::string>& out) {
    if (pat.is_null()) return;
    // `ref mut` anywhere in the subtree, recorded for `arms_bind_ref_mut` (see
    // sema_impl.hpp). Checked HERE, at the one entry every sub-pattern node
    // passes through, so a pattern form this function already knows how to
    // recurse into cannot be missed by a second walker that does not.
    if (!pat_scan_saw_ref_mut_ &&
        pat.has_key(la::IS_REF) && pat.get(la::IS_REF.code).is_value() &&
        pat.get(la::IS_REF.code).as_value<uint8_t>() != 0 &&
        pat.has_key(la::IS_MUT) && pat.get(la::IS_MUT.code).is_value() &&
        pat.get(la::IS_MUT.code).as_value<uint8_t>() != 0)
        pat_scan_saw_ref_mut_ = true;
    int32_t c = code_of(pat);
    auto recurse_list = [&](writ::TinyMapView node, uint8_t key) {
        if (!node.has_key(key)) return;
        auto av = node.get(key);
        if (av.is_null()) return;
        auto wrapped = map_of(av);
        ArrayView items = (!wrapped.is_null() && wrapped.has_key(la::ITEMS))
                              ? arr_of(wrapped.get(la::ITEMS.code))
                              : arr_of(av);
        for (uint64_t i = 0; i < items.size(); ++i)
            collect_ast_pat_bindings(map_of(items.get(i)), out);
    };
    auto recurse_items = [&](writ::TinyMapView node) {
        recurse_list(node, la::ITEMS.code);
    };
    if (c == la::PAT_WILD) {
        if (pat.has_key(la::NAME)) {
            auto n = str_of(pat.get(la::NAME.code));
            // A bare name resolving to a NO-PAYLOAD enum variant (`None`,
            // `Ok` — checked against every enum, since exact per-position
            // scrutinee types aren't re-derived here) or to a module const
            // is a VARIANT/CONST pattern, not a binding. Conservative
            // direction: an exotic binding shadowing a variant name
            // under-reports E0408 rather than false-erroring.
            auto is_variant_or_const = [&](std::string_view nm) -> bool {
                if (const_pkg_of_.count(std::string(nm))) return true;   // G156-1: any-pkg const
                for (auto& [ek, ei] : enums_)
                    for (auto& v : ei.variants)
                        if (v.name == nm && v.payload_types.empty())
                            return true;
                return false;
            };
            if (!n.empty() && n != "_" && !is_variant_or_const(n))
                out.emplace_back(n);
        }
        return;
    }
    if (c == la::PAT_AT) {
        if (pat.has_key(la::NAME)) out.emplace_back(str_of(pat.get(la::NAME.code)));
        if (pat.has_key(la::VALUE)) collect_ast_pat_bindings(map_of(pat.get(la::VALUE.code)), out);
        return;
    }
    if (c == la::PAT_FIELD) {
        if (pat.has_key(la::VALUE)) {
            collect_ast_pat_bindings(map_of(pat.get(la::VALUE.code)), out);
        } else if (pat.has_key(la::NAME)) {
            // shorthand `Point { x }` binds x
            out.emplace_back(str_of(pat.get(la::NAME.code)));
        }
        return;
    }
    if (c == la::PAT_REST) {
        if (pat.has_key(la::NAME)) out.emplace_back(str_of(pat.get(la::NAME.code)));
        return;
    }
    if (c == la::PAT_OR) {
        if (pat.has_key(la::ITEMS)) {
            auto alts = arr_of(pat.get(la::ITEMS.code));
            if (alts.size() > 0)
                collect_ast_pat_bindings(map_of(alts.get(0)), out);
        }
        return;
    }
    if (c == la::PAT_REF && pat.has_key(la::VALUE)) {
        collect_ast_pat_bindings(map_of(pat.get(la::VALUE.code)), out);
        return;
    }
    if (c == la::PAT_TUPLE) {
        if (pat.has_key(la::NAMES)) {
            auto nv = pat.get(la::NAMES.code);
            if (!nv.is_null()) {
                auto blist = map_of(nv);
                if (!blist.is_null() && blist.has_key(la::ITEMS)) {
                    auto items = arr_of(blist.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i)
                        collect_ast_pat_bindings(map_of(items.get(i)), out);
                }
            }
        }
        recurse_items(pat);
        return;
    }
    if (c == la::PAT_VARIANT_DATA) {
        // Tuple-shape payload lives under ARGS; struct-shape under ITEMS.
        recurse_list(pat, la::ARGS.code);
        recurse_items(pat);
        return;
    }
    if (c == la::PAT_STRUCT || c == la::PAT_SLICE) {
        recurse_items(pat);
        return;
    }
    // Literal / wildcard-less forms bind nothing.
}

bool SemaChecker::ast_pat_irrefutable(TinyMapView pat) {
    if (pat.is_null()) return true;
    int32_t c = code_of(pat);
    auto list_ok = [&](writ::TinyMapView node, uint8_t key) -> bool {
        if (!node.has_key(key)) return true;
        auto av = node.get(key);
        if (av.is_null()) return true;
        auto wrapped = map_of(av);
        ArrayView items = (!wrapped.is_null() && wrapped.has_key(la::ITEMS))
                              ? arr_of(wrapped.get(la::ITEMS.code))
                              : arr_of(av);
        for (uint64_t i = 0; i < items.size(); ++i)
            if (!ast_pat_irrefutable(map_of(items.get(i)))) return false;
        return true;
    };
    if (c == la::PAT_WILD) {
        if (!pat.has_key(la::NAME)) return true;
        auto n = std::string(str_of(pat.get(la::NAME.code)));
        if (n.empty() || n == "_") return true;
        if (const_pkg_of_.count(n)) return false;
        for (auto& [ek, ei] : enums_)
            for (auto& v : ei.variants)
                if (v.name == n && v.payload_types.empty()) return false;
        return true;
    }
    if (c == la::PAT_REST) return true;
    if (c == la::PAT_AT)
        return !pat.has_key(la::VALUE) || ast_pat_irrefutable(map_of(pat.get(la::VALUE.code)));
    if (c == la::PAT_FIELD)
        return !pat.has_key(la::VALUE) || ast_pat_irrefutable(map_of(pat.get(la::VALUE.code)));
    if (c == la::PAT_REF)
        return !pat.has_key(la::VALUE) || ast_pat_irrefutable(map_of(pat.get(la::VALUE.code)));
    if (c == la::PAT_OR) {
        if (!pat.has_key(la::ITEMS)) return false;
        auto alts = arr_of(pat.get(la::ITEMS.code));
        return alts.size() == 1 && ast_pat_irrefutable(map_of(alts.get(0)));
    }
    if (c == la::PAT_TUPLE) return list_ok(pat, la::NAMES.code) && list_ok(pat, la::ITEMS.code);
    if (c == la::PAT_STRUCT) return list_ok(pat, la::ITEMS.code);
    return false;
}

void SemaChecker::check_or_alt_binding_consistency(TinyMapView pat_or) {
    if (pat_or.is_null() || !pat_or.has_key(la::ITEMS)) return;
    auto alts = arr_of(pat_or.get(la::ITEMS.code));
    if (alts.size() < 2) return;
    std::vector<std::string> first_names;
    collect_ast_pat_bindings(map_of(alts.get(0)), first_names);
    std::sort(first_names.begin(), first_names.end());
    for (uint64_t i = 1; i < alts.size(); ++i) {
        std::vector<std::string> names;
        collect_ast_pat_bindings(map_of(alts.get(i)), names);
        std::sort(names.begin(), names.end());
        if (names != first_names) {
            // Name the first asymmetric variable for an E0408-shaped message.
            std::string offender;
            for (auto& n : first_names)
                if (!std::binary_search(names.begin(), names.end(), n)) { offender = n; break; }
            if (offender.empty())
                for (auto& n : names)
                    if (!std::binary_search(first_names.begin(), first_names.end(), n)) { offender = n; break; }
            error(std::format(
                "or-pattern: variable '{}' is not bound in all alternatives "
                "(E0408): every `|` alternative must bind the same names",
                offender));
            return;
        }
    }
}

// RFC 2005 (match ergonomics), the DEPTH half. See PROBES.md 2026-09-05b.
TypeRef SemaChecker::pat_scrut_one_layer(TypeRef scrut_type) {
    using K = LogosType::Kind;
    int  depth = 0;
    bool all_mut = true;
    TypeRef core = scrut_type;
    std::string lt;
    while (core && (TypeRef(core).kind() == K::Ref || TypeRef(core).kind() == K::MutRef) &&
           TypeRef(core).pointee()) {
        if (TypeRef(core).kind() != K::MutRef) all_mut = false;
        if (depth == 0) lt = std::string(TypeRef(core).lifetime());
        ++depth;
        core = TypeRef(core).pointee();
    }
    if (depth <= 1) return scrut_type;
    return make_ref(all_mut, core, lt);
}

// RFC 2005, the SCALAR half. A non-reference pattern matches the POINTEE, and a
// SCALAR door compares a VALUE — so where `pat_scrut_one_layer` leaves the ONE
// layer an AGGREGATE door wants (a `&Agg` IS the base pointer), a scalar door
// needs ZERO. Peels only when the core is a scalar (integer/char/bool); every
// other core is returned untouched so the aggregate doors keep their layer and
// their diagnostics. See PROBES.md 2026-09-06f.
TypeRef SemaChecker::pat_scrut_scalar_core(TypeRef scrut_type) {
    using K = LogosType::Kind;
    TypeRef core = scrut_type;
    int depth = 0;
    while (core && (TypeRef(core).kind() == K::Ref || TypeRef(core).kind() == K::MutRef) &&
           TypeRef(core).pointee()) { core = TypeRef(core).pointee(); ++depth; }
    if (depth == 0 || !core) return scrut_type;
    K ck = TypeRef(core).kind();
    if (ck == K::Bool || ck == K::Char || is_integer(core)) return core;
    return scrut_type;
}

// A CHAR_LIT's spelling (`'x'`, `'\n'`, `'\u{1F600}'`, a multi-byte UTF-8
// scalar) to its Unicode scalar value. One decoder for every pattern site: the
// payload guards took the spelling's first BYTE — the quote — and never matched.
// A raw byte string `br"…"` / `br#"…"#`: its bytes are the body verbatim (no
// escapes, like `r"…"`). False for any other spelling.
bool SemaChecker::raw_byte_string_bytes_(std::string_view sv, std::vector<uint8_t>& out) {
    if (sv.size() < 4 || sv[0] != 'b' || sv[1] != 'r') return false;
    size_t h = 2;
    while (h < sv.size() && sv[h] == '#') ++h;
    const size_t hashes = h - 2;
    if (h >= sv.size() || sv[h] != '"' || sv.size() < h + 2 + hashes) return false;
    std::string_view body = sv.substr(h + 1, sv.size() - (h + 1) - 1 - hashes);
    out.assign(body.begin(), body.end());
    return true;
}

int64_t SemaChecker::decode_char_lit_(std::string_view sv) {
    // A byte literal `b'…'` decodes as the char literal it spells (the lexer
    // admits only ASCII and `\xNN`, so the value is a byte).
    if (!sv.empty() && sv.front() == 'b') sv.remove_prefix(1);
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
}

lir::Pattern SemaChecker::build_pattern_impl(TinyMapView pnode, TypeRef scrut_type) {
    int32_t pc = code_of(pnode);
    // ONE LAYER IS ALL A DOOR WAS EVER WRITTEN FOR — collapsed here, once, for
    // every NON-REFERENCE pattern door. The binder doors (PAT_WILD/NAME,
    // PAT_REF, PAT_AT) and the delegating PAT_OR read `scrut_orig` BY NAME.
    // PROBES.md 2026-09-05b.
    const TypeRef scrut_orig = scrut_type;
    scrut_type = pat_scrut_one_layer(scrut_type);
    // Default binding mode: spec pat.binding.default-by-ref-mode. 2026-09-06j.
    // A `&[T]` / `&str` IS its reference (the fat `Slice` kind), so a slice
    // pattern over it binds its elements by reference too: `[x, ..]` binds
    // `x: &T`, as Rust does.
    const bool dbm_ref = scrut_orig &&
        (TypeRef(scrut_orig).kind() == LogosType::Kind::Ref ||
         TypeRef(scrut_orig).kind() == LogosType::Kind::MutRef ||
         (pc == la::PAT_SLICE && TypeRef(scrut_orig).kind() == LogosType::Kind::Slice &&
          !(place_deref_scrut_type_ && scrut_orig == place_deref_scrut_type_)));
    // …and a `&mut [T]` IS a mutable one (the fat Slice kind's `mut`): `[first,
    // ..]` over it binds `first: &mut T`. Asked of MutRef alone, it bound a
    // shared reference and `*first = 9` was refused.
    const bool dbm_mut = dbm_ref &&
        (TypeRef(scrut_orig).kind() == LogosType::Kind::MutRef ||
         (TypeRef(scrut_orig).kind() == LogosType::Kind::Slice && TypeRef(scrut_orig).mut_ptr() &&
          !TypeRef(scrut_orig).raw_fat()));
    // A plain named binder: PAT_WILD + NAME, neither modifier (`ref` has its own
    // door; `mut` here is pat.binding.modifier-requires-move-mode).
    auto dbm_named_bind = [&](writ::TinyMapView n) -> std::string {
        // ⚠ the grammar wraps EVERY tuple element in a single-alt PAT_OR.
        if (code_of(n) == la::PAT_OR && n.has_key(la::ITEMS)) {
            auto alts_ = arr_of(n.get(la::ITEMS.code));
            if (alts_.size() == 1) n = map_of(alts_.get(0));
        }
        if (code_of(n) != la::PAT_WILD.code || !n.has_key(la::NAME)) return {};
        auto flag = [&](const la::Key& k) {
            return n.has_key(k) && n.get(k.code).is_value() &&
                   n.get(k.code).as_value<uint8_t>() != 0;
        };
        if (flag(la::IS_REF) || flag(la::IS_MUT)) return {};
        auto nm = std::string(str_of(n.get(la::NAME.code)));
        return (nm.empty() || nm == "_") ? std::string{} : nm;
    };
    // ⚠ SOUNDNESS: a container door that hands its sub-pattern the BARE component
    // type loses the default binding mode, and a payload the scrutinee still owns
    // is then Drop-scheduled twice. Spec pat.binding.default-mode-carried-into-subpatterns.
    // ⚠ A LEAF BINDER CONSUMES THE MODE AT THIS DOOR AND MUST NOT BE HANDED IT
    // AGAIN: `S { x: ref mut rx }` wraps by itself, and wrapping the type too
    // binds `&mut &mut T`. Only a sub-pattern that RE-DERIVES the mode from the
    // type it is given needs it carried.
    // ⚠ THE THREE FIXTURES THAT MEASUREMENT NAMED — `tests/spec/pass/pat_4`,
    // `tests/imported/pass/binding/match-ref-binding-mut`,
    // `tests/logos/pass/match_struct_move_field_drop` — WERE ALL THE 2021
    // SPELLING and are now `match *p` / `match *x`, because a written modifier
    // under a by-reference default mode is an ERROR (Rust 2024, asked below).
    // A leaf still reaching this arm is either such a modifier binder (refused)
    // or a plain binder `mint_dbm_ref` declined (Array/Slice/TypeVar/Error), and
    // both want the BARE component type.
    auto dbm_sub_ty = [&](writ::TinyMapView sub, TypeRef t) -> TypeRef {
        if (!dbm_ref || !t) return t;
        // the grammar wraps a single sub-pattern in a one-alt PAT_OR
        if (code_of(sub) == la::PAT_OR && sub.has_key(la::ITEMS)) {
            auto alts_ = arr_of(sub.get(la::ITEMS.code));
            if (alts_.size() == 1) sub = map_of(alts_.get(0));
        }
        if (code_of(sub) == la::PAT_WILD.code) {
            // Rust 2024 pat.binding.modifier-requires-move-mode at the LEAF
            // binder — `S { x: ref v }`, `[ref v]`, `(a, ref b)`'s sub, and a
            // struct-pattern tuple index `TS { 0: ref a }`.
            // ⚠ THE FACT IS NOT CARRIED PAST THIS POINT: a leaf binder is
            // deliberately handed the BARE component type (see the comment
            // above), so `build_pattern_impl`'s own `dbm_ref` is FALSE at the
            // leaf's own door and a written modifier there cannot see the
            // by-reference default mode. It is therefore asked HERE, at the
            // CONTAINER, which still knows the mode — the three container doors
            // (struct, tuple, slice) all funnel their sub-patterns through this
            // lambda, so this one site answers all four spellings.
            // ⚠ ALL THREE MODIFIERS, not just `ref`: a written `mut` at a leaf
            // reaches this door too (`dbm_named_bind` rejects IS_MUT, so no
            // other site sees it), and `match &p { P { x: mut v } }` /
            // `match &arr { [mut a, b] }` were admitted while the `mut` third
            // of the same rule was refused at the variant, tuple and struct
            // SHORTHAND doors. One rule, one predicate, every door.
            {
                auto lf = [&](const la::Key& kk) {
                    return sub.has_key(kk) && sub.get(kk.code).is_value() &&
                           sub.get(kk.code).as_value<uint8_t>() != 0;
                };
                if ((lf(la::IS_REF) || lf(la::IS_MUT)) && sub.has_key(la::NAME)) {
                    auto nm = std::string(str_of(sub.get(la::NAME.code)));
                    if (!nm.empty() && nm != "_")
                        modifier_under_ref_scrutinee(nm, scrut_orig, /*known_ref=*/true);
                }
            }
            return t;
        }
        auto k = TypeRef(t).kind();
        if (k == LogosType::Kind::Error || k == LogosType::Kind::TypeVar) return t;
        return make_ref(dbm_mut, t);
    };
    auto mint_dbm_ref = [&](const std::string& nm, TypeRef bt,
                            lir::Pattern& out) -> bool {
        if (!dbm_ref || nm.empty()) return false;  // `_` is filtered by dbm_named_bind
        if (!bt || TypeRef(bt).kind() == LogosType::Kind::Error ||
            TypeRef(bt).kind() == LogosType::Kind::TypeVar) return false;
        // A bare name that is a VALUE — a no-payload variant of the component's
        // enum (`S { tag: None, .. }` through `&S`), a module const (`S { v: K }`)
        // — is a test, not a binder: build_pattern decides it. Minted here, it
        // bound the field and caught every value.
        if (bare_name_is_value_pattern_(nm, bt)) return false;
        out.mirror_ptr_ = lir_mirror_emit_pat_ref_bind(
            *cur_prog_, nm, dbm_mut, make_ref(dbm_mut, bt), reserve_pat_slot(nm));
        return true;
    };
    // THE SIX SCALAR DOORS NEED ZERO LAYERS, NOT ONE — the class, closed at the
    // one entry every one of them is reached through. PROBES.md 2026-09-06f.
    if (pc == la::PAT_INT || pc == la::PAT_NEG_INT || pc == la::PAT_CHAR ||
        pc == la::PAT_CHAR_RANGE || pc == la::PAT_BOOL || pc == la::PAT_RANGE)
        scrut_type = pat_scrut_scalar_core(scrut_type);
    // E0308: an ENUM VARIANT pattern over a scalar scrutinee. Checked on the built pattern, whose enum name is the
    // resolved one (prelude `Some` / `None` / `Ok` / `Err` included).
    auto check_variant_scrut = [&](const lir::Pattern& r) {
        namespace ps = lir_schema::pat;
        auto pr = pat_ref_of(r);
        if (!pr || (pr.kind() != ps::Code::Variant && pr.kind() != ps::Code::VariantData)) return;
        std::string_view en = pr.kind() == ps::Code::Variant ? lir_view::PatVariantView{pr}.enum_name()
                                                             : lir_view::PatVariantDataView{pr}.enum_name();
        TypeRef st = scrut_type;
        while (st && (st.kind() == LogosType::Kind::Ref || st.kind() == LogosType::Kind::MutRef) && st.pointee())
            st = st.pointee();
        if (!st || en.empty()) return;
        using K = LogosType::Kind;
        auto k = st.kind();
        // Match ergonomics derefs `&` / `&mut` only: a RAW pointer is a value of
        // its own type, and a variant pattern does not match it (rustc E0308) —
        // `match *p` does. It was peeled as if it were a reference.
        if (k == K::Ptr) {
            error(std::format("mismatched types: this pattern matches enum `{}`, but the scrutinee has type `{}` — "
                              "a raw pointer is not dereferenced by a pattern; write `match *p` (E0308)",
                              en, type_str(scrut_type)));
            return;
        }
        const bool scalar = (is_integer_kind(k) && k != K::Enum) || k == K::F32 || k == K::F64 ||
                            k == K::Bool || k == K::Char;
        // (another ENUM is refused where the variant is resolved against it)
        if (scalar)
            error(std::format("mismatched types: this pattern matches enum `{}`, but the scrutinee has type `{}`",
                              en, type_str(scrut_type)));
    };
    if (pc == la::PAT_VARIANT) {
        auto r = build_pattern_variant(pnode, scrut_type);
        check_variant_scrut(r);
        return r;
    }
    if (pc == la::PAT_VARIANT_DATA) {
        auto saved = variant_data_dbm_;
        variant_data_dbm_ = {dbm_ref, dbm_mut};
        auto r = build_pattern_variant_data(pnode, scrut_type);
        variant_data_dbm_ = saved;
        check_variant_scrut(r);
        return r;
    }
    if (pc == la::PAT_FLOAT) {
        // B-pt-06: parse but reject — IEEE-equality patterns need a
        // language-level decision before we wire them through codegen.
        error("float-literal patterns are not yet supported "
              "(IEEE equality semantics undecided)");
        lir::Pattern p_;
        p_.mirror_ptr_ = lir_mirror_emit_pat_wild(*cur_prog_, "_");
        return p_;
    }
    if (pc == la::PAT_BYTES) return build_pattern_bytes(pnode, scrut_type);
    if (pc == la::PAT_STR) {
        // A string literal (ADR 0030 S3): a PatStr, tested by content by the
        // one pattern tester wherever it stands. It matches a `str` place (a
        // reference layer above it is the default binding mode's); any other
        // type is rustc's E0308 — a `String` scrutinee was accepted through a
        // synthesized `str_eq` guard and double-freed.
        TypeRef st = scrut_type;
        while (st && (TypeRef(st).kind() == LogosType::Kind::Ref ||
                      TypeRef(st).kind() == LogosType::Kind::MutRef) && TypeRef(st).pointee())
            st = TypeRef(st).pointee();
        const bool unknown = !st || TypeRef(st).kind() == LogosType::Kind::Error ||
                             TypeRef(st).kind() == LogosType::Kind::TypeVar;
        const bool is_str = st && TypeRef(st).kind() == LogosType::Kind::Slice && TypeRef(st).elem() &&
                            TypeRef(TypeRef(st).elem()).kind() == LogosType::Kind::U8;
        if (!unknown && !is_str)
            error(std::format("mismatched types: a string literal pattern matches a `&str`, "
                              "found `{}` (E0308)", type_str(scrut_type)));
        lir::Pattern p_;
        p_.mirror_ptr_ = lir_mirror_emit_pat_str(*cur_prog_, str_of(pnode.get(la::VALUE.code)));
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
        p_.mirror_ptr_ = lir_mirror_emit_pat_int(*cur_prog_, v);
        return p_;
    }
    // ── PAT_CHAR / PAT_CHAR_RANGE: 'X' / 'a' ..= 'z' ───────────────────────
    // Decode CHAR_LIT to its Unicode scalar value and lower as an
    // integer pattern (Logos char is a 4-byte Unicode scalar so the
    // u32 equality / range comparison works directly).
    if (pc == la::PAT_CHAR) {
        auto sv = str_of(pnode.get(la::VALUE.code));
        int64_t v = decode_char_lit_(sv);
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error) {
            auto sk = TypeRef(scrut_type).kind();
            if (sk != LogosType::Kind::Char && !is_integer(scrut_type))
                error(std::format("char pattern requires char or integer scrutinee, got '{}'",
                      type_str(scrut_type)));
        }
        lir::Pattern p_;
        p_.mirror_ptr_ = lir_mirror_emit_pat_int(*cur_prog_, v);
        return p_;
    }
    if (pc == la::PAT_CHAR_RANGE) {
        auto lo_sv = str_of(pnode.get(la::LHS.code));
        auto hi_sv = str_of(pnode.get(la::RHS.code));
        int64_t lo = decode_char_lit_(lo_sv);
        int64_t hi = decode_char_lit_(hi_sv);
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error) {
            auto sk = TypeRef(scrut_type).kind();
            if (sk != LogosType::Kind::Char && !is_integer(scrut_type))
                error(std::format("char range pattern requires char or integer scrutinee, got '{}'",
                      type_str(scrut_type)));
        }
        if (lo > hi)
            error(std::format("char range pattern: lo ({}) > hi ({})", lo, hi));
        lir::Pattern p_;
        p_.mirror_ptr_ = lir_mirror_emit_pat_range(*cur_prog_, lo, hi);
        return p_;
    }
    // An or-pattern matches nothing itself — it delegates, so it hands on the
    // UN-COLLAPSED chain. ⚠ A single-alternative PAT_OR wraps EVERY top-level
    // match pattern. PROBES.md 2026-09-05b §4.
    if (pc == la::PAT_OR) return build_pattern_or(pnode, scrut_orig);
    if (pc == la::PAT_BOOL) {
        AnyVal bv = pnode.get(la::VALUE.code);
        bool bval = !bv.is_null() && bv.is_value() && bv.as_value<uint8_t>();
        if (scrut_type && TypeRef(scrut_type).kind() != LogosType::Kind::Error &&
            TypeRef(scrut_type).kind() != LogosType::Kind::Bool)
            error(std::format("bool pattern requires bool scrutinee, got '{}'",
                  type_str(scrut_type)));
        lir::Pattern p_;
        p_.mirror_ptr_ = lir_mirror_emit_pat_bool(*cur_prog_, bval);
        return p_;
    }
    if (pc == la::PAT_TUPLE) {
        // Tuple pattern: (a, b, c) — irrefutable, binds each element.
        // Default binding modes: a `&(T,U)` / `&mut (T,U)` scrutinee is accepted
        // (deref to the inner tuple — gen_match uses the ref ptr directly as the
        // tuple base). Under a shared `&`, a move-only element binds by
        // reference; Copy elements stay by-value. Mirrors the enum/struct gates.
        lir::PatTuple pt;
        // A written `ref`/`ref mut` element mints the PatRefBind the struct-field
        // door mints (T2-27); the name leaves `pt.bindings` as `_` so the
        // by-value zip claims no move. Spec pat.binding.ref-modifier-at-tuple-element.
        auto push_ref_elem = [&](writ::TinyMapView en, TypeRef et) -> bool {
            auto flag = [&](const la::Key& k) {
                return en.has_key(k) && en.get(k.code).is_value() &&
                       en.get(k.code).as_value<uint8_t>() != 0;
            };
            if (!flag(la::IS_REF) || !en.has_key(la::NAME)) return false;
            auto nm = std::string(str_of(en.get(la::NAME.code)));
            if (nm.empty() || nm == "_") return false;
            // Rust 2024 pat.binding.modifier-requires-move-mode at the
            // TUPLE-ELEMENT door. A written `ref` element never reaches
            // `dbm_sub_ty` (it is consumed here), so the ask belongs here.
            if (dbm_ref)
                modifier_under_ref_scrutinee(nm, scrut_orig, /*known_ref=*/true);
            bool im = flag(la::IS_MUT);
            TypeRef bt = make_ref(im,
                (et && TypeRef(et).kind() != LogosType::Kind::Error) ? et : error_t());
            lir::Pattern rp;
            rp.mirror_ptr_ = lir_mirror_emit_pat_ref_bind(
                *cur_prog_, nm, im, bt, reserve_pat_slot(nm));
            pt.bindings.push_back("_");
            pt.subs.push_back(std::move(rp));
            return true;
        };
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
            pw_.mirror_ptr_ = lir_mirror_emit_pat_wild(*cur_prog_, "_");
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
        std::vector<writ::TinyMapView> raw_elems;
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
        std::vector<std::optional<writ::TinyMapView>> expanded;
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
            // A bare name that is a NO-PAYLOAD variant of the element's enum
            // (`(None, b)`) is a variant test, not a binder — the same rule the
            // top-level door applies. As a binder it matched every element.
            {
                TinyMapView nn = sub;
                if (code_of(nn) == la::PAT_OR && nn.has_key(la::ITEMS) &&
                    arr_of(nn.get(la::ITEMS.code)).size() == 1)
                    nn = map_of(arr_of(nn.get(la::ITEMS.code)).get(0));
                TypeRef et = elem_ty;
                while (et && (TypeRef(et).kind() == LogosType::Kind::Ref ||
                              TypeRef(et).kind() == LogosType::Kind::MutRef) && TypeRef(et).pointee())
                    et = TypeRef(et).pointee();
                if (code_of(nn) == la::PAT_WILD && nn.has_key(la::NAME) && !pat_byval_mut(nn) &&
                    !(nn.has_key(la::IS_REF) && nn.get(la::IS_REF.code).is_value() &&
                      nn.get(la::IS_REF.code).as_value<uint8_t>() != 0)) {
                    std::string nm(str_of(nn.get(la::NAME.code)));
                    // …and a module const (`(K, _)`): a value test as well.
                    if (bare_name_is_value_pattern_(nm, et)) {
                        pt.bindings.push_back("_");
                        pt.subs.push_back(build_pattern(nn, et));
                        continue;
                    }
                }
            }
            {   // default binding mode, TUPLE door
                lir::Pattern rp;
                if (mint_dbm_ref(dbm_named_bind(sub), elem_ty, rp)) {
                    pt.bindings.push_back("_");
                    pt.subs.push_back(std::move(rp));
                    continue;
                }
            }
            if (sc == la::PAT_WILD.code) {
                if (push_ref_elem(sub, elem_ty)) continue;  // `(ref mut a, b)`
                auto nm = std::string(str_of(sub.get(la::NAME.code)));
                if (nm != "_" && pat_byval_mut(sub) && current_pat_mut_names_)
                    current_pat_mut_names_->insert(nm);  // `(mut a, b)`: the side-set the binder reads
                pt.bindings.push_back(nm);
                pt.subs.push_back(make_pat_wild(nm));
            } else if (sc == la::PAT_INT.code || sc == la::PAT_NEG_INT.code ||
                       sc == la::PAT_BOOL.code || sc == la::PAT_RANGE.code) {
                pt.bindings.push_back("_");
                pt.subs.push_back(build_pattern(sub, dbm_sub_ty(sub, elem_ty)));
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
                pt.subs.push_back(build_pattern(sub, dbm_sub_ty(sub, elem_ty)));
            } else if (sc == la::PAT_STR.code) {
                pt.bindings.push_back("_");
                pt.subs.push_back(build_pattern(sub, dbm_sub_ty(sub, elem_ty)));
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
                            // The grammar wraps every element in a single-alt
                            // PAT_OR, so this is where the keyword arrives.
                            if (!push_ref_elem(inner, elem_ty)) {
                                auto nm = inner.has_key(la::NAME)
                                    ? std::string(str_of(inner.get(la::NAME.code)))
                                    : std::string("_");
                                if (nm != "_" && pat_byval_mut(inner) && current_pat_mut_names_)
                                    current_pat_mut_names_->insert(nm);  // `(mut a, b)` (the grammar wraps the element in PAT_OR)
                                pt.bindings.push_back(nm);
                                // The written `mut` rides the binder itself, not
                                // only the side set above: a checker reading the
                                // pattern sees `a` as mutable.
                                pt.subs.push_back(make_pat_wild(nm, nm != "_" && pat_byval_mut(inner)));
                            }
                            single = true;
                        } else if (isc == la::PAT_INT.code ||
                                   isc == la::PAT_NEG_INT.code ||
                                   isc == la::PAT_BOOL.code ||
                                   isc == la::PAT_RANGE.code) {
                            pt.bindings.push_back("_");
                            pt.subs.push_back(build_pattern(inner, dbm_sub_ty(inner, elem_ty)));
                            single = true;
                        } else if (isc == la::PAT_VARIANT_DATA.code) {
                            pt.bindings.push_back("_");
                            pt.subs.push_back(build_pattern(inner, dbm_sub_ty(inner, elem_ty)));
                            single = true;
                        } else if (isc == la::PAT_STR.code) {
                            pt.bindings.push_back("_");
                            pt.subs.push_back(build_pattern(inner, dbm_sub_ty(inner, elem_ty)));
                            single = true;
                        }
                    }
                }
                if (!single) {
                    pt.bindings.push_back("_");
                    pt.subs.push_back(build_pattern(sub, dbm_sub_ty(sub, elem_ty)));
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
        // Phase-1: reserve a dense slot per binding (NO_SLOT for `_`).
        std::vector<uint32_t> bind_slots;
        bind_slots.reserve(pt.bindings.size());
        for (auto& b : pt.bindings)
            bind_slots.push_back(b == "_" ? 0xFFFFFFFFu : reserve_pat_slot(b));
        auto mo = lir_mirror_emit_pat_tuple(*cur_prog_, pt.bindings, pt.binding_types, pt.subs, bind_slots);
        lir::Pattern p_;
        p_.mirror_ptr_ = mo;
        return p_;
    }
    // ── PAT_RANGE: 0..=9 inclusive integer range ──────────────────────────
    if (pc == la::PAT_RANGE) {
        // Half-open forms (`a..`, `..=b`, `..b`) omit one bound key; clamp the
        // open side to the scrutinee integer type's min/max.
        bool has_lo = pnode.has_key(la::LHS);
        bool has_hi = pnode.has_key(la::RHS);
        // THE BOUND IS A BIT PATTERN OF THE SCRUTINEE'S WIDTH, AND ITS RANGE AND
        // ITS ORDER ARE ASKED IN THE SCRUTINEE'S OWN SIGNEDNESS.
        //
        // The bound used to be an `int64_t`, which is the scrutinee's width for
        // exactly ten of the sixteen integer types. For `i128`/`u128` the
        // literal was TRUNCATED to its low 64 bits and still compiled, so the
        // emitted test covered a different range and the answer was silently
        // wrong — `170141183460469231731687303715884105726..=…727` matched
        // nothing. It is 128 bits wide here and 128 bits wide in the mirror.
        //
        // Width and signedness come from `int_rank`, the one table. The local
        // one that used to sit here listed eight kinds and sent i8, u8, i24,
        // u24, i56, u56, i128 and u128 to its `default:` arm — i32's bounds —
        // so an open-ended `..=5` over a `u8` clamped its low end to INT32_MIN.
        using PK = LogosType::Kind;
        using u128 = unsigned __int128;
        using i128 = __int128;
        PK skind = scrut_type ? TypeRef(scrut_type).kind() : PK::I32;
        auto srank = int_rank(skind);
        unsigned swidth = srank.first;
        bool ssigned = srank.second;
        if (swidth == 0) { swidth = 32; ssigned = true; }
        bool sunsigned = !ssigned;
        bool uns_cmp = sunsigned;
        // The type's bounds as MAGNITUDES. `umax` is the unsigned ceiling of
        // `swidth` bits; `smax_mag`/`smin_mag` are |max| and |min| of the signed
        // type of the same width. u128's ceiling is not representable in i128,
        // which is why the whole block works in patterns and compares by
        // signedness rather than in one signed type.
        u128 umax     = swidth >= 128 ? ~(u128)0 : ((((u128)1) << swidth) - 1);
        u128 smin_mag = ((u128)1) << (swidth - 1);
        u128 smax_mag = smin_mag - 1;
        // Sign- or zero-extend a `swidth`-bit pattern to 128 bits, so that the
        // ordering comparisons below and the constant the backend materialises
        // see the same value the scrutinee's own width would.
        auto canon = [&](u128 p) -> u128 {
            if (swidth >= 128) return p;
            u128 mask = (((u128)1) << swidth) - 1;
            p &= mask;
            if (!sunsigned && (p >> (swidth - 1)) & 1) p |= ~mask;
            return p;
        };
        auto u128_str = [](u128 m) {
            if (m == 0) return std::string("0");
            char buf[48]; int i = 48;
            while (m) { buf[--i] = char('0' + (int)(m % 10)); m /= 10; }
            return std::string(buf + i, size_t(48 - i));
        };
        auto pat_str = [&](u128 p) {
            if (!sunsigned && (i128)p < 0) return "-" + u128_str((u128)0 - p);
            return u128_str(p);
        };
        // A bound's TEXT gives a magnitude and a sign; the pattern is derived.
        // `parse_int_literal` returned the raw 64-bit pattern for a magnitude
        // above INT64_MAX, which conflated "the value 2^63" with "the value
        // -2^63" and cost the fit test its footing.
        auto read_bound = [&](la::Key text_key, la::Key neg_key, bool present,
                              u128 dflt, const char* which) -> u128 {
            if (!present) return dflt;
            auto sv = str_of(pnode.get(text_key.code));
            bool neg = false;
            if (pnode.has_key(neg_key)) {
                AnyVal av = pnode.get(neg_key.code);
                neg = !av.is_null() && av.is_value() && av.as_value<uint8_t>();
            }
            if (parse_int_literal_overflows_128(sv)) {
                error(std::format("range pattern: {} ('{}') exceeds 128 bits", which, sv));
                return 0;
            }
            u128 mag = parse_int_literal_u128(sv);
            if (scrut_type && skind != PK::Error && is_integer(scrut_type)) {
                if (neg && sunsigned)
                    error(std::format("range pattern: a negative bound does not fit in '{}'",
                          type_str(scrut_type)));
                else if (neg && mag > smin_mag)
                    error(std::format("range pattern: {} (-{}) does not fit in '{}'",
                          which, u128_str(mag), type_str(scrut_type)));
                else if (!neg && mag > umax)
                    error(std::format("range pattern: {} ({}) does not fit in '{}'",
                          which, u128_str(mag), type_str(scrut_type)));
                // PRE-EXISTING RULE, PRESERVED AND NOW EXPLICIT: on a SIGNED
                // scrutinee a magnitude between |max|+1 and the unsigned
                // ceiling is read as the two's-complement pattern it spells
                // (`0xFFFF_FFFF_FFFF_FFFF` on an `i64` bound is -1). `canon`
                // below performs the reinterpretation.
            }
            return canon(neg ? ((u128)0 - mag) : mag);
        };
        u128 lo = read_bound(la::LHS, la::LO_NEG, has_lo,
                             canon(sunsigned ? (u128)0 : ((u128)0 - smin_mag)), "lo");
        u128 hi = read_bound(la::RHS, la::HI_NEG, has_hi,
                             canon(sunsigned ? umax : smax_mag), "hi");
        if (scrut_type && skind != PK::Error &&
            skind != PK::Never &&  // G160-10
            !is_integer(scrut_type))
            error(std::format("range pattern requires integer scrutinee, got '{}'",
                  type_str(scrut_type)));
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
        // Emptiness and ordering are the SCRUTINEE'S order, for the same reason
        // the fit test above is.
        if (!inclusive) {
            bool empty = uns_cmp ? (lo >= hi) : ((i128)lo >= (i128)hi);
            if (empty) {
                error(std::format("exclusive range pattern: lo ({}) >= hi ({}) (empty range)",
                      pat_str(lo), pat_str(hi)));
            }
            hi = canon(hi - 1);
        } else {
            bool bad = uns_cmp ? (lo > hi) : ((i128)lo > (i128)hi);
            if (bad)
                error(std::format("range pattern: lo ({}) > hi ({})", pat_str(lo), pat_str(hi)));
        }
        lir::Pattern p_;
        p_.mirror_ptr_ = lir_mirror_emit_pat_range(*cur_prog_, (i128)lo, (i128)hi);
        return p_;
    }

    // ── PAT_AT: name @ sub_pat ────────────────────────────────────────────
    if (pc == la::PAT_AT) {
        auto bname = std::string(str_of(pnode.get(la::NAME.code)));
        auto sub_node = map_of(pnode.get(la::VALUE.code));
        if (at_or_alt_ >= 0 && code_of(sub_node) == la::PAT_OR && sub_node.has_key(la::ITEMS)) {
            auto alts = arr_of(sub_node.get(la::ITEMS.code));
            if ((uint64_t)at_or_alt_ < alts.size()) sub_node = map_of(alts.get((uint64_t)at_or_alt_));
        }
        int32_t saved_at_or_alt = at_or_alt_;
        at_or_alt_ = -1;   // the selection is this binder's; nested ones are not fanned
        auto sub_pat = build_pattern(sub_node, scrut_orig);
        at_or_alt_ = saved_at_or_alt;
        lir::PatAt pa;
        pa.name = bname;
        // NS3: scrut_type may be null for unknown types; fallback to error_t() so
        // bind_pattern can always define the variable (even with error type).
        // `n @ sub` binds the WHOLE scrutinee — the un-collapsed chain.
        pa.type = scrut_orig ? scrut_orig : error_t();
        pa.sub.push_back(std::move(sub_pat));
        const bool at_named = !(pa.name == "_" || pa.name.empty());
        uint32_t _at_slot = at_named ? reserve_pat_slot(pa.name) : 0xFFFFFFFFu;  // Phase-1
        // `mut n @ sub`: carried BOTH ways — the name side-set sema's binder
        // family reads, and pat_keys::IS_MUT on the mirror, which is the only
        // thing borrow_check can see (it never has the AST).
        pa.is_mut = pat_byval_mut(pnode);
        if (current_pat_mut_names_ && at_named && pa.is_mut)
            current_pat_mut_names_->insert(pa.name);
        // `ref n @ sub` / `ref mut n @ sub`: n is a REFERENCE to the matched
        // place (`&T`), as a `ref n` binder is. Under a by-reference default
        // mode EITHER modifier is an error (Rust 2024) — `mut n @ 1..=5` too.
        {
            auto af = [&](const la::Key& k) {
                return pnode.has_key(k) && pnode.get(k.code).is_value() &&
                       pnode.get(k.code).as_value<uint8_t>() != 0;
            };
            if (dbm_ref && (af(la::IS_REF) || pa.is_mut) && at_named)
                modifier_under_ref_scrutinee(pa.name, scrut_orig, /*known_ref=*/true);
            if (af(la::IS_REF) && at_named) {
                pa.ref_mode = af(la::IS_MUT) ? 2 : 1;
                pa.type = make_ref(pa.ref_mode == 2, pa.type);
                pa.is_mut = false;
            }
        }
        auto mo = lir_mirror_emit_pat_at(*cur_prog_, pa.name, pa.sub, pa.type, _at_slot, pa.is_mut,
                                         pa.ref_mode);
        lir::Pattern p_;
        p_.mirror_ptr_ = mo;
        return p_;
    }

    // ── PAT_REF: &pat or &mut pat ─────────────────────────────────────────
    if (pc == la::PAT_REF) {
        bool is_mut = pnode.has_key(la::IS_MUT) &&
                      pnode.get(la::IS_MUT.code).is_value() &&
                      pnode.get(la::IS_MUT.code).as_value<uint8_t>() != 0;
        TypeRef inner_type = error_t();
        // A `&`-pattern IS the deref: it reads the UN-COLLAPSED chain.
        TypeRef rp_scrut = scrut_orig;
        if (rp_scrut && TypeRef(rp_scrut).kind() != LogosType::Kind::Error) {
            if (TypeRef(rp_scrut).kind() == LogosType::Kind::Ref ||
                TypeRef(rp_scrut).kind() == LogosType::Kind::MutRef) {
                // NS2: &mut pattern requires &mut scrutinee; & pattern accepts both.
                if (is_mut && TypeRef(rp_scrut).kind() != LogosType::Kind::MutRef)
                    error(std::format("reference pattern: '&mut' requires '&mut' scrutinee, got '{}'",
                          type_str(rp_scrut)));
                inner_type = TypeRef(rp_scrut).pointee() ? TypeRef(rp_scrut).pointee() : error_t();
            } else {
                // NS1: non-reference scrutinee with a reference pattern is always wrong.
                error(std::format("reference pattern requires reference scrutinee, got '{}'",
                      type_str(rp_scrut)));
            }
        }
        auto sub_node = map_of(pnode.get(la::VALUE.code));
        auto inner_pat = build_pattern(sub_node, inner_type);
        lir::PatRefPat prp;
        prp.is_mut = is_mut;
        prp.inner.push_back(std::move(inner_pat));
        auto mo = lir_mirror_emit_pat_ref_pat(*cur_prog_, prp.inner, prp.is_mut);
        lir::Pattern p_;
        p_.mirror_ptr_ = mo;
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
            ref_t.pointee = scrut_orig;
            TypeRef btype = pool_->alloc(std::move(ref_t));
            uint32_t _rb_slot = (bname == "_" || bname.empty())  // Phase-1
                              ? 0xFFFFFFFFu : reserve_pat_slot(bname);
            lir::Pattern p_;
            p_.mirror_ptr_ = lir_mirror_emit_pat_ref_bind(*cur_prog_, bname, is_mut, btype, _rb_slot);
            return p_;
        }
        // A bare identifier that names a NO-PAYLOAD variant of the scrutinee's
        // enum is a variant pattern, not a binding — e.g. `None` over
        // `Option<T>` (the prelude variants `None`/`Some`/`Ok`/`Err` and
        // user enums matched without the `Enum::` qualifier). Without this the
        // bare name was lowered as an irrefutable wildcard binding, so
        // `match opt { None => …, Some(_) => … }` / `if let None = opt`
        // mis-dispatched (the `None` arm caught everything) and mis-codegened.
        // Through every `&` layer: the unit-variant path is a non-reference
        // pattern, matched under the default binding mode (`None` over
        // `&Option<T>`, a struct field matched through `&S`). Asked of the
        // unpeeled type, it bound the name and caught every value.
        TypeRef unit_et = scrut_type;
        while (unit_et && (TypeRef(unit_et).kind() == LogosType::Kind::Ref ||
                           TypeRef(unit_et).kind() == LogosType::Kind::MutRef) && TypeRef(unit_et).pointee())
            unit_et = TypeRef(unit_et).pointee();
        if (pnode.has_key(la::NAME) && unit_et &&
            TypeRef(unit_et).kind() == LogosType::Kind::Enum && !pat_byval_mut(pnode) &&
            !(pnode.has_key(la::IS_REF) && pnode.get(la::IS_REF.code).is_value() &&
              pnode.get(la::IS_REF.code).as_value<uint8_t>() != 0)) {
            std::string nm(str_of(pnode.get(la::NAME.code)));
            if (!nm.empty() && nm != "_") {
                std::string en(TypeRef(unit_et).enum_name());
                auto [epkg_v, esi_v] = find_enum_by_name(en);
                if (esi_v) {
                    for (auto& v : esi_v->variants) {
                        if (v.name == nm && v.payload_types.empty()) {
                            lir::Pattern p_;
                            p_.mirror_ptr_ = lir_mirror_emit_pat_variant(
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
        // The scrutinee's OWN struct when the pattern names it: a family type a
        // metaprogram emitted into another package is not reachable by its bare
        // name, but it is the type being matched.
        if (!sinfo) {
            TypeRef st0 = scrut_type;
            while (st0 && (TypeRef(st0).kind() == LogosType::Kind::Ref ||
                           TypeRef(st0).kind() == LogosType::Kind::MutRef) && TypeRef(st0).pointee())
                st0 = TypeRef(st0).pointee();
            if (st0 && (TypeRef(st0).kind() == LogosType::Kind::Struct ||
                        TypeRef(st0).kind() == LogosType::Kind::ZonedStruct) &&
                TypeRef(st0).struct_name() == sname)
                if (auto [spk, ssi] = struct_of(TypeRef(st0)); ssi) sinfo = ssi;
        }
        if (!sinfo) {
            // G152-10: a type alias used as a struct pattern (`type S2 = S;
            // match x { S2 { a, b } => … }`). Resolve the alias to its target
            // struct and match under the real name (codegen + scrutinee check
            // need it). Construction already resolves aliases.
            auto ait = alias_find(sname);
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
        // A DEFERRED scrutinee (Error-typed — a factory-backed chain the
        // post-drain round types for real) may name a struct no round has
        // emitted yet: not "unknown", not yet known — silent, the names bind at
        // error_t() and the strict round decides (build_pattern_variant_data's
        // rule; the let-only destructure door had its own copy).
        if (!sinfo && !(scrut_type && TypeRef(scrut_type).kind() == LogosType::Kind::Error))
            error(std::format("struct pattern: unknown struct '{}'", sname));
        // Through references too: `match &p { Q { x } => … }` over `p: P` is E0308
        // (default binding modes peel the reference; the struct must still match).
        TypeRef sst = scrut_type;
        while (sst && (TypeRef(sst).kind() == LogosType::Kind::Ref ||
                       TypeRef(sst).kind() == LogosType::Kind::MutRef) && TypeRef(sst).pointee())
            sst = TypeRef(sst).pointee();
        if (sst && TypeRef(sst).kind() != LogosType::Kind::Error &&
            TypeRef(sst).kind() == LogosType::Kind::Struct &&
            TypeRef(sst).struct_name() != sname && TypeRef(sst).struct_name() != "")
            error(std::format("mismatched types: expected `{}`, found `{}` (E0308)",
                  type_str(scrut_type), sname));
        // …and against a value that can never be a struct (a scalar, a tuple,
        // an array, a slice, an enum): rustc E0308.
        if (sst && sinfo) {
            using K = LogosType::Kind;
            const K sk = TypeRef(sst).kind();
            const bool never_struct =
                sk == K::Tuple || sk == K::Array || sk == K::Slice || sk == K::Enum ||
                sk == K::Bool || sk == K::IntLit || sk == K::FloatLit ||
                sk == K::F32 || sk == K::F64 || (sk >= K::I32 && sk <= K::U128 && sk != K::Bool);
            if (never_struct)
                error(std::format("mismatched types: expected `{}`, found `{}` (E0308)",
                                  type_str(scrut_type), sname));
        }
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
                            // A GENERIC struct's field under the scrutinee's type
                            // arguments (`&GPair<u8, u16>`'s `a` is `u8`, not `A`):
                            // the default binding mode can mint `&u8` only then.
                            // Its lifetime arguments likewise: `S { r }` over
                            // `&S<'a>` binds `r: &&'a i64`, not the declared `'s`.
                            if (sst && TypeRef(sst).kind() == LogosType::Kind::Struct &&
                                (!TypeRef(sst).type_args().empty() ||
                                 !TypeRef(sst).lifetime_args().empty()))
                                if (TypeRef sub_t = field_type_of_for_type(sst, fname))
                                    ftype = sub_t;
                        }
                        // Bug fix: emit error when field not found in struct.
                        if (sinfo) {
                            bool field_found = false;
                            for (auto& f : sinfo->fields)
                                if (f.name == fname) {
                                    field_found = true;
                                    // A pattern NAMES the field as a read does:
                                    // the same privacy door (a destructuring
                                    // `let` / match arm walked past it).
                                    check_pub_access(f.is_pub, sinfo->package, fname);
                                    break;
                                }
                            if (!field_found)
                                error(std::format("struct `{}` does not have a field named `{}` (E0026)",
                                      sname, fname));
                        }
                        lir::PatFieldBinding pfb;
                        pfb.field_name = fname;
                        // T2-27: `S { ref a }` / `S { ref mut a }` shorthand —
                        // IS_REF/IS_MUT on the PAT_FIELD itself (no VALUE).
                        // Synthesize a PatRefBind sub so `a` binds `&[mut]T`
                        // (same shape as a plain `ref [mut] a`); the NC3
                        // codegen then yields a pointer-to-field and `*a`
                        // reads / (mut) writes through it.
                        bool fld_is_ref = fnode.has_key(la::IS_REF) &&
                            fnode.get(la::IS_REF.code).is_value() &&
                            fnode.get(la::IS_REF.code).as_value<uint8_t>() != 0;
                        bool fld_is_mut = fnode.has_key(la::IS_MUT) &&
                            fnode.get(la::IS_MUT.code).is_value() &&
                            fnode.get(la::IS_MUT.code).as_value<uint8_t>() != 0;
                        if (fld_is_mut && !fld_is_ref && !fnode.has_key(la::VALUE) &&
                            fname != "_" && current_pat_mut_names_)
                            current_pat_mut_names_->insert(fname);
                        // The written `mut` rides the binding, not only the side
                        // set: a checker reading the PATTERN sees `x` as mutable.
                        pfb.is_mut = fld_is_mut && !fld_is_ref && !fnode.has_key(la::VALUE);
                        if (fld_is_ref && !fnode.has_key(la::VALUE) &&
                            fname != "_") {
                            // Rust 2024 pat.binding.modifier-requires-move-mode
                            // at the STRUCT-FIELD SHORTHAND door (`{ ref x }`).
                            // Consumed here, so it never reaches `dbm_sub_ty`.
                            if (dbm_ref)
                                modifier_under_ref_scrutinee(fname, scrut_orig, /*known_ref=*/true);
                            TypeRef bt = make_ref(fld_is_mut,
                                (ftype && TypeRef(ftype).kind() != LogosType::Kind::Error)
                                    ? ftype : error_t());
                            uint32_t _rb_slot = (fname == "_" || fname.empty())  // Phase-1
                                              ? 0xFFFFFFFFu : reserve_pat_slot(fname);
                            lir::Pattern rp;
                            rp.mirror_ptr_ = lir_mirror_emit_pat_ref_bind(
                                *cur_prog_, fname, fld_is_mut, bt, _rb_slot);
                            pfb.sub.push_back(std::move(rp));
                            ps.fields.push_back(std::move(pfb));
                            continue;
                        }
                        // default binding mode, STRUCT door (shorthand `{ x }`)
                        if (!fnode.has_key(la::VALUE) && !fld_is_mut && !fld_is_ref) {
                            lir::Pattern rp;
                            if (mint_dbm_ref(fname, ftype, rp)) {
                                pfb.sub.push_back(std::move(rp));
                                ps.fields.push_back(std::move(pfb));
                                continue;
                            }
                        }
                        if (fnode.has_key(la::VALUE)) {
                            auto sub_node = map_of(fnode.get(la::VALUE.code));
                            {   // default binding mode, STRUCT door (rename `{ x: nx }`)
                                lir::Pattern rp;
                                if (mint_dbm_ref(dbm_named_bind(sub_node), ftype, rp)) {
                                    pfb.sub.push_back(std::move(rp));
                                    ps.fields.push_back(std::move(pfb));
                                    continue;
                                }
                            }
                            auto sub = build_pattern(sub_node, dbm_sub_ty(sub_node, ftype));
                            // A refutable field sub-pattern of any kind (a literal,
                            // a string, a variant, a tuple, a slice, …) is tested
                            // and bound by the one matcher (pat_test / pat_bind)
                            // at the field's place.
                            pfb.sub.push_back(std::move(sub));
                        }
                        // Phase-1: a plain shorthand field `{ a }` (no sub) binds
                        // the field name — reserve its slot. Explicit/ref-bind
                        // subs carry their own slot.
                        if (pfb.sub.empty() && pfb.field_name != "_")
                            pfb.slot = reserve_pat_slot(pfb.field_name);
                        ps.fields.push_back(std::move(pfb));
                    }
                }
            }
        }
        // §6.1 union pattern (Rust `items.union.pattern.*`): a union
        // pattern must specify EXACTLY ONE field (no `..`), and the
        // match itself reads memory through that field — so it must
        // be in an `unsafe` block. Skip the struct "all fields
        // covered" check entirely for unions.
        if (sinfo && sinfo->is_union) {
            if (ps.has_rest)
                error(std::format(
                    "union pattern '{}': `..` is not allowed "
                    "(union patterns must name exactly one field "
                    "— Rust `items.union.pattern.one-field`)",
                    sname));
            if (ps.fields.size() != 1)
                error(std::format(
                    "union pattern '{}' must specify exactly one "
                    "field, got {} (Rust "
                    "`items.union.pattern.one-field`)",
                    sname, ps.fields.size()));
            if (!inside_unsafe_)
                error(std::format(
                    "pattern on union `{}` requires `unsafe` block "
                    "(Rust `items.union.pattern.safety` — pattern "
                    "matching reads the named field's memory)",
                    sname));
        }
        // NG5: validate that all struct fields are covered (listed by name or '..' present).
        else if (sinfo && !ps.has_rest) {
            for (auto& f : sinfo->fields) {
                bool covered = false;
                for (auto& pfb : ps.fields)
                    if (pfb.field_name == f.name) { covered = true; break; }
                if (!covered)
                    error(std::format("pattern does not mention field `{}` (E0027; add `..` to ignore the rest)",
                          f.name));
            }
        }
        auto mo = lir_mirror_emit_pat_struct(*cur_prog_, ps.struct_name, ps.fields, ps.has_rest);
        lir::Pattern p_;
        p_.mirror_ptr_ = mo;
        return p_;
    }

    // ── PAT_SLICE: [a, b] or [first, .., last] ───────────────────────────
    if (pc == la::PAT_SLICE) {
        // RFC 2005: peel the collapsed layer when it points AT the array/slice
        // the pattern is about; a `&i64` keeps its refusal, naming `&i64`.
        if (scrut_type &&
            (TypeRef(scrut_type).kind() == LogosType::Kind::Ref ||
             TypeRef(scrut_type).kind() == LogosType::Kind::MutRef) &&
            TypeRef(scrut_type).pointee() &&
            (TypeRef(TypeRef(scrut_type).pointee()).kind() == LogosType::Kind::Array ||
             TypeRef(TypeRef(scrut_type).pointee()).kind() == LogosType::Kind::Slice))
            scrut_type = TypeRef(scrut_type).pointee();
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
                            // `ref xs @ ..` under a by-reference default mode: the
                            // modifier is a Rust 2024 error, as at every leaf.
                            if (dbm_ref && rest_name != "_" && enode.has_key(la::IS_REF) &&
                                enode.get(la::IS_REF.code).is_value() &&
                                enode.get(la::IS_REF.code).as_value<uint8_t>() != 0)
                                modifier_under_ref_scrutinee(rest_name, scrut_orig, /*known_ref=*/true);
                            // `mut xs @ ..`: the rest binds mutably (the side-set a binder
                            // reads, and the pattern's own flag the borrow checker reads).
                            const bool rest_mut = rest_name != "_" && enode.has_key(la::IS_MUT) &&
                                enode.get(la::IS_MUT.code).is_value() &&
                                enode.get(la::IS_MUT.code).as_value<uint8_t>() != 0;
                            if (current_pat_mut_names_ && rest_mut) current_pat_mut_names_->insert(rest_name);
                            psl.rest.push_back(make_pat_wild(rest_name, rest_mut));
                            continue;
                        }
                        lir::Pattern sub;   // default binding mode, SLICE door
                        if (!mint_dbm_ref(dbm_named_bind(enode), elem_type, sub))
                            sub = build_pattern(enode, dbm_sub_ty(enode, elem_type));
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
                error(std::format("pattern requires {} elements but array has {} (E0527)",
                      psl.prefix.size(), expected));
        }
        // S3: for fixed-size arrays with rest, prefix+suffix cannot exceed array size.
        if (scrut_type && TypeRef(scrut_type).kind() == LogosType::Kind::Array && found_rest) {
            size_t arr_size = (size_t)TypeRef(scrut_type).arr_size();
            if (psl.prefix.size() + psl.suffix.size() > arr_size)
                error(std::format("pattern requires at least {} elements but array has {} (E0528)",
                      psl.prefix.size() + psl.suffix.size(), arr_size));
        }
        // G167-6a: suffix elements after `..` on a dynamic slice ARE supported —
        // codegen indexes them from the runtime length (`len - suf_n + i`) and
        // gates the arm on `len >= prefix + suffix`. (Previously rejected.)
        auto mo = lir_mirror_emit_pat_slice(*cur_prog_, psl.prefix, psl.rest, psl.suffix);
        lir::Pattern p_;
        p_.mirror_ptr_ = mo;
        return p_;
    }

    // ── Writ scalar patterns ────────────────────────────────────────────
    // `@null`, `@true`, `@false`, `@<int>`, `@-<int>` are desugared by
    // lower_match/lower_match_expr into `_` + a synthesized guard call, so
    // by the time we get here the caller treats them as wildcards. We return
    // PatWild unchanged; the caller validates scrutinee type & synthesizes
    // the guard using build_writ_pat_guard.
    if (pc == la::PAT_WRIT_NULL || pc == la::PAT_WRIT_BOOL ||
        pc == la::PAT_WRIT_INT  || pc == la::PAT_WRIT_STR  ||
        pc == la::PAT_WRIT_MAP  || pc == la::PAT_WRIT_ARR  ||
        pc == la::PAT_WRIT_TYPED_ARR || pc == la::PAT_WRIT_TYPED_MAP) {
        if (!in_match_writ_ctx_) {
            error("Writ pattern (@null/@true/@false/@<int>/@\"str\"/@{...}/@[...]) "
                  "is only supported in `match` arms, not in if-let / "
                  "while-let / let-bindings / nested pattern positions.");
        }
        lir::Pattern pw_;
        pw_.mirror_ptr_ = lir_mirror_emit_pat_wild(*cur_prog_, "_");
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
                    p_.mirror_ptr_ = lir_mirror_emit_pat_variant(
                        *cur_prog_, vit->second, wname, disc);
                    return p_;
                }
            }
        }
    }
    // P4-pm-06: bare ident in pattern that resolves to a module-const ⇒
    // treat as a value pattern (PAT_INT / PAT_BOOL / PAT_CHAR), not as a
    // fresh binding. ctfe-eval the const's RHS once; emit the matching
    // scalar pattern. Non-scalar consts (str, writ, struct) stay
    // diagnosed — needs string-pattern codegen, separate slice.
    if (wname != "_") {
        auto cval = resolve_const_value(wname);   // G156-1: cur-package first
        if (cval) {
            auto r = ctfe::eval_expr(cval, holder_);
            if (r) {
                auto cv = std::move(r).value();
                using K = LogosType::Kind;
                if (cv.kind == K::Bool) {
                    lir::Pattern p_;
                    p_.mirror_ptr_ = lir_mirror_emit_pat_bool(*cur_prog_, cv.b);
                    return p_;
                }
                if (cv.kind == K::I8 || cv.kind == K::I16 || cv.kind == K::I32 ||
                    cv.kind == K::I64 || cv.kind == K::Isize ||
                    cv.kind == K::U8 || cv.kind == K::U16 || cv.kind == K::U32 ||
                    cv.kind == K::U64 || cv.kind == K::Usize ||
                    cv.kind == K::IntLit || cv.kind == K::Char) {
                    lir::Pattern p_;
                    p_.mirror_ptr_ = lir_mirror_emit_pat_int(*cur_prog_, cv.i);
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
                    auto cit = module_consts_.find(resolve_const_key(wname));  // G156-1
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
                        lir::LExprPtr guard = nullptr;
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
                        p_.mirror_ptr_ = lir_mirror_emit_pat_wild(*cur_prog_, syn);
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
                        p_.mirror_ptr_ = lir_mirror_emit_pat_wild(*cur_prog_, syn);
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
    // A named binder reserves its slot like make_pat_wild's: mlir-gen resolves
    // a drop by slot, and without one an arm binder shadowing an outer local
    // was dropped in the outer's place.
    p_.mirror_ptr_ = lir_mirror_emit_pat_wild(*cur_prog_, wname,
        (wname == "_" || wname.empty()) ? 0xFFFFFFFFu : reserve_pat_slot(wname),
        wname != "_" && pat_byval_mut(pnode));
    return p_;
}

// Build the synthesized guard expression for a Writ scalar match pattern.
// Returns nullptr if pnode is not a Writ pattern.  Emits an error if the
// scrutinee type is not AnyVal.  The guard calls a free stdlib helper
// (writ_pat_is_null / _eq_bool / _eq_i24) that takes `*const AnyVal`.
//
// If pnode is a PAT_OR wrapping several Writ patterns, the guards for each
// alt are OR-ed together (matches Rust or-pattern semantics).  A single-alt
// PAT_OR unwraps transparently.
lir::LExprPtr SemaChecker::build_writ_pat_guard(
        TinyMapView pnode, const std::string& scrut_var,
        TypeRef scrut_type, const std::string& base_var,
        std::vector<lir_view::StmtRef>& out_stmts,
        std::vector<WritPatBinding>& out_bindings) {
    TypeRef ptr_t_outer = make_ptr(false, scrut_type);
    make_ptr(false, prim(LogosType::Kind::U8));  // intern u8-ptr type into the pool
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
    // Returns nullptr only when p is not a scalar Writ leaf.
    auto build_leaf = [&](TinyMapView p, const std::string& sv) -> lir::LExprPtr {
        int32_t pc = code_of(p);
        if (pc != la::PAT_WRIT_NULL && pc != la::PAT_WRIT_BOOL &&
            pc != la::PAT_WRIT_INT  && pc != la::PAT_WRIT_STR)
            return nullptr;

        TypeRef ptr_t = ptr_t_outer;

        const char* helper = nullptr;
        size_t want_arity = 1;
        std::vector<lir::LExprPtr> extra_args;
        if (pc == la::PAT_WRIT_NULL) {
            helper = "writ_pat_is_null";
        } else if (pc == la::PAT_WRIT_BOOL) {
            helper = "writ_pat_eq_bool";
            want_arity = 2;
            AnyVal bv = p.get(la::VALUE.code);
            bool bval = !bv.is_null() && bv.is_value() && bv.as_value<uint8_t>();
            extra_args.push_back(builder().lit_bool(bval, bool_t()));
        } else if (pc == la::PAT_WRIT_INT) {
            helper = "writ_pat_eq_i24";
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
        } else {  // PAT_WRIT_STR — writ_pat_eq_str(*node, str)
            helper = "writ_pat_eq_str";
            want_arity = 2;
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
            // logos.lang.writ.anyval; the str-eq helper (one WritString
            // user) lives in std.writ.pat.
            const char* hint =
                std::strcmp(helper, "writ_pat_eq_str") == 0
                ? "use logos.mem.writ.pat;"
                : "use logos.lang.writ.anyval;";
            error(std::format(
                "Writ pattern needs stdlib helper `{}`; `{}`",
                helper, hint));
            return builder().lit_bool(false, bool_t());
        }
        std::vector<lir::LExprPtr> args;
        args.push_back(builder().addr_of(sv, ptr_t, BorrowOrigin::Desugar));
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
                "Writ pattern needs stdlib helper `{}`; `use logos.mem.writ.pat;`",
                helper));
            return "";
        }
        std::vector<lir::LExprPtr> args;
        args.push_back(builder().addr_of(parent_av, ptr_t_outer, BorrowOrigin::Desugar));
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
    // Emit `writ_pat_array_len_eq(&sv, base, n)` as a bool expr.
    auto emit_array_len_eq = [&](const std::string& sv, uint64_t n) -> lir::LExprPtr {
        const char* helper = "writ_pat_array_len_eq";
        auto cands = find_func_candidates(helper);
        const SemaFuncInfo* fi = nullptr;
        for (auto* c : cands)
            if (c->param_types.size() == 2) { fi = c; break; }
        if (!fi) {
            error(std::format(
                "Writ pattern needs stdlib helper `{}`; `use logos.mem.writ.pat;`",
                helper));
            return builder().lit_bool(false, bool_t());
        }
        std::vector<lir::LExprPtr> args;
        args.push_back(builder().addr_of(sv, ptr_t_outer, BorrowOrigin::Desugar));
        args.push_back(builder().lit_int((int64_t)n, u64_t));
        std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
        return builder().call(sym, {}, std::move(args), bool_t());
    };
    // Emit `writ_pat_array_len_ge(&sv, base, n)` as a bool expr.
    auto emit_array_len_ge = [&](const std::string& sv, uint64_t n) -> lir::LExprPtr {
        const char* helper = "writ_pat_array_len_ge";
        auto cands = find_func_candidates(helper);
        const SemaFuncInfo* fi = nullptr;
        for (auto* c : cands)
            if (c->param_types.size() == 2) { fi = c; break; }
        if (!fi) {
            error(std::format(
                "Writ pattern needs stdlib helper `{}`; `use logos.mem.writ.pat;`",
                helper));
            return builder().lit_bool(false, bool_t());
        }
        std::vector<lir::LExprPtr> args;
        args.push_back(builder().addr_of(sv, ptr_t_outer, BorrowOrigin::Desugar));
        args.push_back(builder().lit_int((int64_t)n, u64_t));
        std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
        return builder().call(sym, {}, std::move(args), bool_t());
    };
    // Emit `writ_pat_has_type_code(&sv, base, tc)` bool expr.
    auto emit_has_type_code = [&](const std::string& sv, uint64_t tc) -> lir::LExprPtr {
        const char* helper = "writ_pat_has_type_code";
        auto cands = find_func_candidates(helper);
        const SemaFuncInfo* fi = nullptr;
        for (auto* c : cands)
            if (c->param_types.size() == 2) { fi = c; break; }
        if (!fi) {
            error(std::format(
                "Writ pattern needs stdlib helper `{}`; `use logos.mem.writ.pat;`",
                helper));
            return builder().lit_bool(false, bool_t());
        }
        std::vector<lir::LExprPtr> args;
        args.push_back(builder().addr_of(sv, ptr_t_outer, BorrowOrigin::Desugar));
        args.push_back(builder().lit_int((int64_t)tc, u64_t));
        std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
        return builder().call(sym, {}, std::move(args), bool_t());
    };
    // Emit `writ_pat_is_map(&sv, base)` bool expr.
    auto emit_is_map = [&](const std::string& sv) -> lir::LExprPtr {
        const char* helper = "writ_pat_is_map";
        auto cands = find_func_candidates(helper);
        const SemaFuncInfo* fi = nullptr;
        for (auto* c : cands)
            if (c->param_types.size() == 1) { fi = c; break; }
        if (!fi) {
            error(std::format(
                "Writ pattern needs stdlib helper `{}`; `use logos.mem.writ.pat;`",
                helper));
            return builder().lit_bool(false, bool_t());
        }
        std::vector<lir::LExprPtr> args;
        args.push_back(builder().addr_of(sv, ptr_t_outer, BorrowOrigin::Desugar));
        std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
        return builder().call(sym, {}, std::move(args), bool_t());
    };
    // Emit `writ_pat_is_present(&sv)` bool expr.
    auto emit_present = [&](const std::string& sv) -> lir::LExprPtr {
        const char* helper = "writ_pat_is_present";
        auto cands = find_func_candidates(helper);
        const SemaFuncInfo* fi = nullptr;
        for (auto* c : cands)
            if (c->param_types.size() == 1) { fi = c; break; }
        if (!fi) {
            error(std::format(
                "Writ pattern needs stdlib helper `{}`", helper));
            return builder().lit_bool(false, bool_t());
        }
        std::vector<lir::LExprPtr> args;
        args.push_back(builder().addr_of(sv, ptr_t_outer, BorrowOrigin::Desugar));
        std::string sym = fi->symbol_name.empty() ? helper : fi->symbol_name;
        return builder().call(sym, {}, std::move(args), bool_t());
    };
    // Recursive: build a guard expr for pattern `p` against AnyVal local `sv`.
    std::function<lir::LExprPtr(TinyMapView, const std::string&)> build_rec;
    build_rec = [&](TinyMapView p, const std::string& sv) -> lir::LExprPtr {
        int32_t pc = code_of(p);
        if (pc == la::PAT_WRIT_NULL || pc == la::PAT_WRIT_BOOL ||
            pc == la::PAT_WRIT_INT  || pc == la::PAT_WRIT_STR)
            return build_leaf(p, sv);
        if (pc == la::PAT_WILD) {
            auto nm = str_of(p.get(la::NAME.code));
            std::string name(nm);
            if (!name.empty() && name != "_") {
                out_bindings.push_back(WritPatBinding{name, sv});
            }
            return mk_true();
        }
        if (pc == la::PAT_WRIT_MAP) {
            lir::LExprPtr acc = emit_is_map(sv);
            if (p.has_key(la::ITEMS)) {
                auto wrap = map_of(p.get(la::ITEMS.code));
                auto items = arr_of(wrap.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto ent = map_of(items.get(i));
                    if (code_of(ent) != la::PAT_WRIT_MAP_ENTRY) continue;
                    auto ksv = str_of(ent.get(la::KEY.code));
                    std::vector<lir::LExprPtr> xargs;
                    xargs.push_back(builder().lit_str(std::string(ksv), make_slice_type(prim(LogosType::Kind::U8))));
                    std::string child = emit_child_let(
                        "writ_pat_map_slot", sv, std::move(xargs), 2);
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
        if (pc == la::PAT_WRIT_ARR) {
            uint64_t n_total = 0;
            bool has_rest = false;
            writ::TinyMapView arr_wrap;
            if (p.has_key(la::ITEMS)) {
                arr_wrap = map_of(p.get(la::ITEMS.code));
                auto items = arr_of(arr_wrap.get(la::ITEMS.code));
                n_total = items.size();
                for (uint64_t i = 0; i < n_total; ++i) {
                    if (code_of(map_of(items.get(i))) == la::PAT_REST) {
                        if (i + 1 != n_total) {
                            error("`..` must be the last element in a Writ "
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
                        "writ_pat_array_slot", sv, std::move(xargs), 2);
                    if (child.empty()) {
                        return builder().lit_bool(false, bool_t());
                    }
                    auto sub = build_rec(map_of(items.get(i)), child);
                    acc = mk_and(std::move(acc), std::move(sub));
                }
            }
            return acc;
        }
        if (pc == la::PAT_WRIT_TYPED_ARR) {
            namespace th = logos::writ::type_hash;
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
        if (pc == la::PAT_WRIT_TYPED_MAP) {
            namespace th = logos::writ::type_hash;
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
        // Unsupported in Writ context.
        error("unsupported pattern inside Writ @{...}/@[...] pattern");
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
            bool is_writ = pc0 == la::PAT_WRIT_NULL ||
                             pc0 == la::PAT_WRIT_BOOL ||
                             pc0 == la::PAT_WRIT_INT  ||
                             pc0 == la::PAT_WRIT_STR  ||
                             pc0 == la::PAT_WRIT_MAP  ||
                             pc0 == la::PAT_WRIT_ARR  ||
                             pc0 == la::PAT_WRIT_TYPED_ARR ||
                             pc0 == la::PAT_WRIT_TYPED_MAP;
            if (!is_writ) return nullptr;
            return build_rec(map_of(alts.get(0)), scrut_var);
        }
        bool any_writ = false, any_non = false;
        for (uint64_t i = 0; i < alts.size(); ++i) {
            int32_t pc = code_of(map_of(alts.get(i)));
            if (pc == la::PAT_WRIT_NULL || pc == la::PAT_WRIT_BOOL ||
                pc == la::PAT_WRIT_INT  || pc == la::PAT_WRIT_STR  ||
                pc == la::PAT_WRIT_MAP  || pc == la::PAT_WRIT_ARR  ||
        pc == la::PAT_WRIT_TYPED_ARR || pc == la::PAT_WRIT_TYPED_MAP) any_writ = true;
            else any_non = true;
        }
        if (!any_writ) return nullptr;
        if (any_non) {
            error("or-pattern mixing Writ patterns with other "
                  "patterns is not supported");
            return builder().lit_bool(false, bool_t());
        }
        lir::LExprPtr acc = nullptr;
        for (uint64_t i = 0; i < alts.size(); ++i) {
            // build_rec handles all Writ pattern kinds (scalar + structural).
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

// Rust 2024 `pat.binding.modifier-requires-move-mode`: a written `mut` (the
// `ref`/`ref mut` half is 2021 today — soundness queue) under a by-reference
// scrutinee is an error, at every door: variant payload, struct field, tuple
// element, nested variant. The sentence is minted once, here.
void SemaChecker::modifier_under_ref_scrutinee(std::string_view name, TypeRef scrut_type, bool known_ref) {
    if (!known_ref) {
        if (!scrut_type) return;
        auto sk = TypeRef(scrut_type).kind();
        if (sk != LogosType::Kind::Ref && sk != LogosType::Kind::MutRef) return;
    }
    error(std::format("binding modifiers may only be written when the default binding mode is `move`: '{}' is bound under a by-reference scrutinee (Rust 2024, pat.binding.modifier-requires-move-mode)", name));
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
        auto _vd_slots = v.bind_slots();  // Phase-1: reuse reserved slots
        // CP-cm-17: skip `_` payload bindings. Without this, `Some(_)`
        // pulls a "_" binding into scope; collect_drops at scope end
        // then emits a drop on the payload (Vec.drop, String.drop, …)
        // even though the user wrote a wildcard. Mirrors the Tuple
        // branch's filter below.
        auto _vd_muts = v.bind_byval_muts();  // the carried by-value `mut`
        const auto _vd_subs = v.subs();       // ADR 0030 S3: payload sub-patterns, in position order
        for (size_t i = 0; i < names.size() && i < types.size(); ++i)
            if (i < _vd_subs.size() && _vd_subs[i]) {
                bind_pattern_ref(_vd_subs[i], types[i]);
            } else if (names[i] != "_") {
                bool m = i < _vd_muts.size() && _vd_muts[i] != 0u;
                if (m) modifier_under_ref_scrutinee(names[i], scrut_type);  // Rust 2024, nested door
                define(std::string(names[i]), types[i], m,
                       i < _vd_slots.size() ? _vd_slots[i] : 0xFFFFFFFFu);
            }
    } else if (k == ps::Code::Tuple) {
        lir_view::PatTupleView v{pr};
        std::vector<std::string_view> names;
        std::vector<TypeRef> types;
        v.each_binding([&](std::string_view n) { names.push_back(n); });
        v.each_binding_type(pool, [&](TypeRef t) { types.push_back(t); });
        auto _tp_slots = v.bind_slots();  // Phase-1: reuse reserved slots
        // ONE pass in ELEMENT order: a direct name and a nested sub-pattern's
        // names are defined as they appear, so `((p, q), r)` declares p, q, r
        // and drops them r, q, p, as Rust does (two passes put `r` first).
        auto define_direct = [&](size_t i) {
            if (i < names.size() && i < types.size() && names[i] != "_") {
                bool m = pat_mut_name(names[i]);
                if (m) modifier_under_ref_scrutinee(names[i], scrut_type);  // Rust 2024, tuple door
                define(std::string(names[i]), types[i], m,
                       i < _tp_slots.size() ? _tp_slots[i] : 0xFFFFFFFFu);
            }
        };
        // P4-pm-24 / G144-1: recurse into refutable sub-patterns so any nested
        // bindings (`(E::Foo { x }, _)`, `((true,y)|(y,true), z)`, `((a,b), w)`)
        // reach the outer arm scope. Codegen (pat_test/pat_bind) extracts them.
        size_t idx = 0;
        // ⚠ At belongs here for the same reason the other three do: it is a
        // sub-pattern kind that INTRODUCES NAMES, and each_binding() above sees
        // only the direct element names.
        v.each_sub([&](lir_view::PatRef sp) {
            // ⚠ RefBind for the same reason: a `ref` element's name is in the
            // SUB, not in `each_binding`, so without this arm it never defines.
            // Struct, RefPat and Slice introduce names too; each is here because
            // pat_bind binds that shape (a door in SERIES: neither half may be
            // armed alone, PROBES.md 2026-09-17f). Slice covers an array, a
            // `&[T; N]` and a dynamic `&[T]` element alike — `([1, x], k)` over
            // `(&[i64], i64)` left `x` undefined while only the by-value array
            // was admitted.
            if (sp && (sp.kind() == ps::Code::VariantData ||
                       sp.kind() == ps::Code::Or ||
                       sp.kind() == ps::Code::At ||
                       sp.kind() == ps::Code::RefBind ||
                       sp.kind() == ps::Code::Struct ||
                       sp.kind() == ps::Code::RefPat ||
                       sp.kind() == ps::Code::Tuple ||
                       sp.kind() == ps::Code::Slice)) {
                TypeRef sub_t = idx < types.size() ? types[idx] : error_t();
                bind_pattern_ref(sp, sub_t);
            } else {
                define_direct(idx);
            }
            ++idx;
        });
        for (size_t i = idx; i < names.size(); ++i) define_direct(i);  // no sub recorded
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
            define(std::string(n), scrut_type, pat_mut_name(n), v.bind_slot());  // Phase-1
        }
    } else if (k == ps::Code::RefBind) {
        lir_view::PatRefBindView v{pr};
        define(std::string(v.name()), v.bind_type(pool), false, v.bind_slot());  // Phase-1
    } else if (k == ps::Code::At) {
        lir_view::PatAtView v{pr};
        TypeRef ty = v.type(pool);
        auto n = v.name();
        if (ty && n != "_") define(std::string(n), ty, v.ref_mode() ? false : pat_mut_name(n), v.bind_slot());  // Phase-1
        // `ref n @ sub`: n is `&T`; sub matches the place itself, a `T`.
        TypeRef sub_t = (v.ref_mode() && ty && TypeRef(ty).pointee()) ? TypeRef(ty).pointee() : ty;
        if (auto sub = v.sub()) bind_pattern_ref(sub, sub_t);
    } else if (k == ps::Code::RefPat) {
        lir_view::PatRefPatView v{pr};
        TypeRef inner_t = error_t();
        if (scrut_type && (TypeRef(scrut_type).kind() == LogosType::Kind::Ref ||
                           TypeRef(scrut_type).kind() == LogosType::Kind::MutRef) &&
            TypeRef(scrut_type).pointee())
            inner_t = TypeRef(scrut_type).pointee();
        // E0507 at a `&`-pattern: the deref is IN the pattern, so the binding
        // MODE decides, not the pointee alone (the pointee test over-refuses,
        // measured). Silent on Struct/Slice/Tuple inner patterns, deliberately.
        // PROBES.md 2026-09-02pat §6/§9, 2026-09-02land.
        if (inner_t && is_move_type(inner_t)) {
            const auto* tp_ = cur_prog_->type_pool.impl();
            std::string bn_;
            // `bn_` = the binding's own name; `ref q` is the repair.
            std::function<bool(lir_view::PatRef, bool)> byval_;
            byval_ = [&](lir_view::PatRef p, bool wt) -> bool {
                if (!p) return false;
                switch (p.kind()) {
                    case ps::Code::Wild: {
                        // A named Wild directly under `&` really did carry no
                        // `ref`: `&ref q` lowers to PatRefBind, not to a named
                        // Wild (MEASURED — it is admitted). Under an `@` the
                        // keyword is gone, so `wt` is false there.
                        if (!wt) return false;
                        auto n = lir_view::PatWildView{p}.name();
                        if (n.empty() || n == "_") return false;
                        bn_ = std::string(n);
                        return true;
                    }
                    case ps::Code::VariantData: {
                        lir_view::PatVariantDataView vv{p};
                        std::vector<std::string> ns;
                        std::vector<TypeRef> tys;
                        vv.each_binding([&](std::string_view s){ ns.emplace_back(s); });
                        vv.each_binding_type(tp_, [&](TypeRef t){ tys.push_back(t); });
                        // Mode 0 = by value; 1/2 = `ref` / `ref mut`, which move
                        // nothing. An ABSENT vector means all-by-value: the key
                        // is minted only where a mode is spelled.
                        auto ms = vv.bind_ref_modes();
                        for (size_t i = 0; i < ns.size(); ++i) {
                            uint32_t m = i < ms.size() ? ms[i] : 0u;
                            TypeRef bt = i < tys.size() ? tys[i] : TypeRef(nullptr);
                            if (m == 0 && ns[i] != "_" && bt && is_move_type(bt)) {
                                bn_ = ns[i];
                                return true;
                            }
                        }
                        return false;
                    }
                    case ps::Code::Or: {
                        bool any = false;
                        lir_view::PatOrView{p}.each_alt([&](lir_view::PatRef a){
                            if (!any && byval_(a, wt)) any = true; });
                        return any;
                    }
                    case ps::Code::At:
                        // The `@` name itself is judged where the name is (its
                        // AT_REF_MODE); only the SUB-pattern, whose own node may
                        // carry a mode, is walked here.
                        return byval_(lir_view::PatAtView{p}.sub(), false);
                    // `&(P { d }, k)` moves `d` out of a shared ref: E0507. The
                    // tuple door recurses into Struct subs, so this walk must too
                    // or the deref'd move is admitted (measured, PROBES.md 2026-09-16p).
                    case ps::Code::Tuple: {
                        lir_view::PatTupleView tv{p};
                        std::vector<std::string_view> ns;
                        std::vector<TypeRef> tys;
                        tv.each_binding([&](std::string_view s){ ns.push_back(s); });
                        tv.each_binding_type(tp_, [&](TypeRef t){ tys.push_back(t); });
                        for (size_t i = 0; i < ns.size() && i < tys.size(); ++i)
                            if (ns[i] != "_" && tys[i] && is_move_type(tys[i])) {
                                bn_ = std::string(ns[i]);
                                return true;
                            }
                        bool any = false;
                        tv.each_sub([&](lir_view::PatRef sp){
                            if (!any && byval_(sp, false)) any = true; });
                        return any;
                    }
                    case ps::Code::Struct: {
                        lir_view::PatStructView sv{p};
                        std::string sn_(sv.struct_name());
                        const SemaStructInfo* si_ = find_struct_by_name(sn_).second;
                        if (!si_) si_ = find_datatype_by_name(sn_).second;
                        bool any = false;
                        sv.each_field([&](lir_view::PatFieldBindingView fv){
                            if (any) return;
                            if (auto fsub = fv.sub()) {
                                if (byval_(fsub, false)) any = true;
                                return;
                            }
                            auto fn_ = fv.field_name();
                            if (fn_ == "_" || !si_) return;
                            for (auto& f : si_->fields)
                                if (f.name == fn_) {
                                    if (f.type && is_move_type(f.type)) {
                                        bn_ = std::string(fn_);
                                        any = true;
                                    }
                                    break;
                                }
                        });
                        return any;
                    }
                    default: return false;
                }
            };
            if (byval_(v.inner(), true))
                error(std::format(
                    "cannot move out of a value behind a reference / out of an "
                    "index (E0507): the pattern binds '{}' by value", bn_));
        }
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
        // The SAME predicate the doors ask, so a `& &S` scrutinee reaches the
        // one-layer peel below instead of falling through as a non-struct.
        scrut_type = pat_scrut_one_layer(scrut_type);
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
                bool fmut = pat_mut_name(fname);
                if (default_ref && fmut) {  // Rust 2024, struct door
                    modifier_under_ref_scrutinee(fname, scrut_type, /*known_ref=*/true);
                    bt = ftype;
                }
                define(std::string(fname), bt, fmut, fv.bind_slot());  // Phase-1
            }
            else bind_pattern_ref(sub, ftype);
        });
    } else if (k == ps::Code::Slice) {
        lir_view::PatSliceView v{pr};
        // The SAME predicate the PAT_SLICE door asks; without it every element
        // binding is Error-typed under a `&[T; N]`.
        TypeRef sl_scrut = pat_scrut_one_layer(scrut_type);
        if (sl_scrut &&
            (TypeRef(sl_scrut).kind() == LogosType::Kind::Ref ||
             TypeRef(sl_scrut).kind() == LogosType::Kind::MutRef) &&
            TypeRef(sl_scrut).pointee() &&
            (TypeRef(TypeRef(sl_scrut).pointee()).kind() == LogosType::Kind::Array ||
             TypeRef(TypeRef(sl_scrut).pointee()).kind() == LogosType::Kind::Slice))
            sl_scrut = TypeRef(sl_scrut).pointee();
        // Default binding modes, slice shape — the rule the struct door already
        // applies: under a `&`/`&mut` scrutinee a MOVE-ONLY element binds BY
        // REFERENCE, or it is Drop-scheduled on top of the scrutinee's own drop.
        // ⚠ SOUNDNESS: this is a double-free guard. PROBES.md 2026-09-05b §3.
        TypeRef sl_outer = pat_scrut_one_layer(scrut_type);
        bool sl_default_ref = sl_outer && (TypeRef(sl_outer).kind() == LogosType::Kind::Ref ||
                                           TypeRef(sl_outer).kind() == LogosType::Kind::MutRef);
        bool sl_default_mut = sl_default_ref &&
                              TypeRef(sl_outer).kind() == LogosType::Kind::MutRef;
        // A `&mut [T]` scrutinee (the fat Slice kind) lends its rest mutably.
        const bool sl_mut_slice = sl_outer && TypeRef(sl_outer).kind() == LogosType::Kind::Slice &&
                                  TypeRef(sl_outer).mut_ptr() && !TypeRef(sl_outer).raw_fat();
        TypeRef elem_raw = (sl_scrut && TypeRef(sl_scrut).elem())
                            ? TypeRef(sl_scrut).elem() : error_t();
        TypeRef elem_t = elem_raw;
        if (sl_default_ref && elem_t &&
            TypeRef(elem_t).kind() != LogosType::Kind::Error &&
            TypeRef(elem_t).kind() != LogosType::Kind::TypeVar &&
            is_move_type(elem_t))
            elem_t = make_ref(sl_default_mut, elem_t);
        v.each_prefix([&](lir_view::PatRef p) { bind_pattern_ref(p, elem_t); });
        // G149-4: a named rest (`xs @ ..`) binds the sub-slice as `&[T]`
        // (Slice kind), not an element. Anonymous `_` rest binds nothing.
        // Its ELEMENT type is the raw one: the mode wraps the element BINDING.
        TypeRef rest_slice_t = make_slice_type(elem_raw, sl_default_mut || sl_mut_slice);
        // Over an array BY VALUE the rest is an ARRAY of what the pattern did
        // not name (`let [a, rest @ ..] = arr;` — `rest: [T; N-1]`), as Rust
        // types it: a copy the binding owns, writable when bound `mut`.
        if (!sl_default_ref && sl_scrut && TypeRef(sl_scrut).kind() == LogosType::Kind::Array &&
            TypeRef(sl_scrut).arr_size() >= int64_t(v.prefix_count() + v.suffix_count()))
            rest_slice_t = make_array(elem_raw, uint64_t(TypeRef(sl_scrut).arr_size()) -
                                                v.prefix_count() - v.suffix_count());
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

// ── SYNTHESIZED AST (see synth_doc_ in sema_impl.hpp) ─────────────────────
writ::AnyVal SemaChecker::synth_node(int32_t code, uint32_t line,
        std::initializer_list<std::pair<uint8_t, writ::AnyVal>> keys) {
    if (synth_doc_.is_null()) synth_doc_ = writ::make_doc(1u << 20).get();  // MultiChunk: never moves
    auto* m = synth_doc_.make_tiny_map(keys.size() + 2).get();
    auto& ar = synth_doc_.arena();
    m->put(la::CODE.code, writ::AnyVal::from_value(code), ar).get();
    if (line) m->put(la::SRC_LINE.code, writ::AnyVal::from_value(line), ar).get();
    for (const auto& kv : keys)
        if (!kv.second.is_null()) m->put(kv.first, kv.second, ar).get();
    writ::AnyVal a; a.set_ref(m); return a;
}

writ::AnyVal SemaChecker::synth_with_key(writ::TinyMapView n, uint8_t key, writ::AnyVal v) {
    if (synth_doc_.is_null()) synth_doc_ = writ::make_doc(1u << 20).get();  // MultiChunk: never moves
    auto* m = synth_doc_.make_tiny_map(n.size() + 2).get();
    auto& ar = synth_doc_.arena();
    const uint64_t bits = n.bitmap();
    for (uint8_t k = 0; k < 64; ++k) {
        if (!(bits & (1ull << k))) continue;
        writ::AnyVal val = k == key ? v : n.get(k);
        if (!val.is_null()) m->put(k, val, ar).get();
    }
    if (!(bits & (1ull << key)) && !v.is_null()) m->put(key, v, ar).get();
    writ::AnyVal a; a.set_ref(m); return a;
}

std::optional<lir_view::StmtRef> SemaChecker::lower_temp_rooted_place_assign_(TinyMapView node,
                                                                             TinyMapView place) {
    // Walk the place chain to its root.
    std::vector<TinyMapView> chain;
    TinyMapView cur = unwrap_paren_node(place);
    while (!cur.is_null() && (code_of(cur) == la::FIELD_READ || code_of(cur) == la::TUPLE_INDEX ||
                              code_of(cur) == la::INDEX_READ)) {
        chain.push_back(cur);
        cur = unwrap_paren_node(map_of(cur.get(la::RECEIVER.code)));
    }
    if (chain.empty() || cur.is_null()) return std::nullopt;
    const int32_t rc = code_of(cur);
    // A borrow root (`(&x)[0].v = …`, `(&mut x)[0].v = …`) binds the same way:
    // the write then goes through the reference and is judged as such (E0594
    // for a shared one).
    const bool borrow_root = rc == la::ADDR_OF_MUT ||
        (rc == la::UNARY && str_of(cur.get(la::OP.code)) == "&");
    if (rc != la::ARR_LIT && rc != la::TUPLE_LIT && rc != la::STRUCT_LIT &&
        rc != la::CALL && rc != la::METHOD_CALL && !borrow_root)
        return std::nullopt;
    const uint32_t line = node_line_;
    std::string tname = std::format("__atmp_{}", tmp_var_count_++);
    writ::AnyVal tref = synth_node(la::VAR_REF.code, line, {{la::NAME.code, synth_str(tname)}});
    // Rebuild the chain innermost-out over the temporary.
    writ::AnyVal rebuilt = tref;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        rebuilt = synth_with_key(*it, la::RECEIVER.code, rebuilt);
    std::vector<writ::AnyVal> stmts;
    writ::AnyVal value = node.get(la::VALUE.code);
    const int32_t vc = code_of(map_of(value));
    const bool lit = vc == la::LIT_INT || vc == la::LIT_FLOAT || vc == la::LIT_BOOL ||
                     vc == la::LIT_STR || vc == la::LIT_CHAR;
    if (!lit) {   // the value is evaluated FIRST (Rust's assignment order)
        std::string vname = std::format("__aval_{}", tmp_var_count_++);
        stmts.push_back(synth_node(la::LET.code, line, {{la::NAME.code, synth_str(vname)},
                                                        {la::VALUE.code, value}}));
        value = synth_node(la::VAR_REF.code, line, {{la::NAME.code, synth_str(vname)}});
    }
    writ::AnyVal root; root.set_ref(cur.ptr());
    stmts.push_back(synth_node(la::LET.code, line, {{la::NAME.code, synth_str(tname)},
                                                    {la::IS_MUT.code, writ::AnyVal::from_value(uint8_t(1))},
                                                    {la::VALUE.code, root}}));
    writ::AnyVal asg = synth_with_key(node, la::RECEIVER.code, rebuilt);
    stmts.push_back(synth_with_key(map_of(asg), la::VALUE.code, value));
    return lower_stmt(map_of(synth_node(la::BLOCK_STMT.code, line,
                                        {{la::BODY.code, synth_block(stmts, line)}})));
}

writ::AnyVal SemaChecker::synth_str(std::string_view text) {
    if (synth_doc_.is_null()) synth_doc_ = writ::make_doc(1u << 20).get();  // MultiChunk: never moves
    auto* str = writ::ArenaString::create(synth_doc_.arena(), text).get();
    writ::AnyVal av;
    av.set_ref(reinterpret_cast<const uint8_t*>(str));
    return av;
}

writ::AnyVal SemaChecker::synth_array(const std::vector<writ::AnyVal>& items) {
    if (synth_doc_.is_null()) synth_doc_ = writ::make_doc(1u << 20).get();
    auto arr = synth_doc_.make_array(items.empty() ? 1 : items.size()).get();
    for (const auto& it : items) arr.push_back(it).get();
    return arr.to_anyval();
}

writ::AnyVal SemaChecker::synth_block(const std::vector<writ::AnyVal>& stmts, uint32_t line) {
    return synth_node(la::BLOCK.code, line, {{la::ITEMS.code, synth_array(stmts)}});
}

lir_view::StmtRef SemaChecker::lower_if(TinyMapView node) {
    // Own source line — capture before lowering cond/branches moves node_line_
    // (else the SIf maps to a sub-statement's line).
    const uint32_t if_line = node_line_;
    // `if let` is a MATCH by the time a body reaches sema (the HIR pass).
    if (node.has_key(la::PAT)) {
        hir_gate_(node);
        return builder().stmt_expr(error_expr(), if_line);
    }

    // ── regular if cond { ... } ────────────────────────────────────
    lir::LExprPtr cond = nullptr;
    if (node.has_key(la::COND)) {
        // An `if` condition is a TERMINATING SCOPE (Rust): its temporaries drop
        // before the block runs, not at the end of the whole `if` statement.
        cond = lower_expr_temp_scoped(map_of(node.get(la::COND.code)));
        if (TypeRef(expr_type(cond)).kind() != LogosType::Kind::Bool &&
            TypeRef(expr_type(cond)).kind() != LogosType::Kind::Error &&
            TypeRef(expr_type(cond)).kind() != LogosType::Kind::Never)  // G160-10: `if (return x){}`
            // The `if` a `while c` became (ORIGIN) speaks as the `while`.
            error(std::format("{} condition must be bool, got {}",
                              hir_origin_(node) == hir::Origin::While ? "while" : "if",
                              type_str(expr_type(cond))));
    } else {
        cond = error_expr();
    }

    // Per-branch move tracking — same divergence-aware merge as match.
    auto if_pre_moves = moved_vars_;
    std::set<std::string> if_post_moves;
    bool if_any_non_diverging = false;
    // #118 — divergence is not one thing. `return` leaves the function, so the
    // branch's moves never reach any later drop point and its state is simply
    // discarded. `break`/`continue` leave the LOOP: control still arrives at
    // the enclosing frame's scope-exit drops, so a move on that path must
    // still be accounted for (cell H of the sweep: `if c { consume(a); break; }`
    // double-freed `a` because this predicate lumped the two together).
    //   0 = falls through   1 = return   2 = break/continue
    auto branch_div_kind = [&](const std::vector<lir_view::StmtRef>& b) -> int {
        if (b.empty()) return 0;
        auto br = stmt_ref_of(b.back());
        if (!br) return 0;
        auto k = br.kind();
        if (k == lir_schema::stmt::Code::Return) return 1;
        if (k == lir_schema::stmt::Code::Break ||
            k == lir_schema::stmt::Code::Continue) return 2;
        return 0;
    };
    auto branch_diverges = [&](const std::vector<lir_view::StmtRef>& b) {
        return branch_div_kind(b) != 0;
    };
    std::set<std::string> then_moves = if_pre_moves, else_moves = if_pre_moves;
    int then_div = 0, else_div = 0;
    size_t then_mark = flag_clear_log_.size(), then_end = then_mark;
    size_t else_mark = then_mark,              else_end = then_mark;

    // logos-core 2.7: definite-assignment merge across the if's branches.
    // Snapshot before each branch; after non-diverging branches, union their
    // currently_uninit_vars_ into if_post_uninit (var is uninit at merge if
    // uninit on ANY incoming non-diverging path). Diverging branches
    // contribute nothing (their tail is return/break/continue/panic so
    // control doesn't fall through to the merge).
    auto if_pre_uninit = currently_uninit_vars_;
    std::set<std::string> if_post_uninit;
    bool if_post_uninit_initialized = false;

    const auto owned_pre = closure_owned_drop_;   // move-closure capture releases, merged below
    std::set<std::string> owned_then = owned_pre, owned_else = owned_pre;
    std::vector<lir_view::StmtRef> then_block;
    if (node.has_key(la::THEN)) {
        moved_vars_ = if_pre_moves;
        currently_uninit_vars_ = if_pre_uninit;
        lower_block(map_of(node.get(la::THEN.code))).each_stmt([&](lir_view::StmtRef s){ then_block.push_back(s); });
        then_moves = moved_vars_;
        then_end   = flag_clear_log_.size();
        then_div = branch_div_kind(then_block);
        owned_then = closure_owned_drop_;
        closure_owned_drop_ = owned_pre;
        if (!branch_diverges(then_block)) {
            if_any_non_diverging = true;
            for (auto& m : moved_vars_) if_post_moves.insert(m);
            for (auto& v : currently_uninit_vars_) if_post_uninit.insert(v);
            if_post_uninit_initialized = true;
        }
    } else {
        // No then-block ≡ no body executed; behaves as non-diverging (just fall-through).
        if_any_non_diverging = true;
        for (auto& m : if_pre_moves) if_post_moves.insert(m);
        for (auto& v : if_pre_uninit) if_post_uninit.insert(v);
        if_post_uninit_initialized = true;
    }

    std::optional<std::vector<lir_view::StmtRef>> else_opt;
    if (node.has_key(la::ELSE)) {
        auto else_node = map_of(node.get(la::ELSE.code));
        moved_vars_ = if_pre_moves;
        currently_uninit_vars_ = if_pre_uninit;
        else_mark = flag_clear_log_.size();
        if (code_of(else_node) == la::BLOCK) {
            std::vector<lir_view::StmtRef> eb;
            lower_block(else_node).each_stmt([&](lir_view::StmtRef s){ eb.push_back(s); });
            else_opt = std::move(eb);
        } else {
            // else if: wrap the single statement in a block. It is an `if`, or
            // the MATCH an `else if let` became in the HIR pass — dispatch by
            // code, not by assuming an `if`.
            auto inner_if = lower_stmt(else_node);
            std::vector<lir_view::StmtRef> b;
            b.push_back(std::move(inner_if));
            else_opt = std::move(b);
        }
        else_moves = moved_vars_;
        else_end   = flag_clear_log_.size();
        else_div = branch_div_kind(*else_opt);
        owned_else = closure_owned_drop_;
        closure_owned_drop_ = owned_pre;
        if (!branch_diverges(*else_opt)) {
            if_any_non_diverging = true;
            for (auto& m : moved_vars_) if_post_moves.insert(m);
            for (auto& v : currently_uninit_vars_) if_post_uninit.insert(v);
            if_post_uninit_initialized = true;
        }
    } else {
        // Else absent ≡ control falls through with pre-state.
        if_any_non_diverging = true;
        for (auto& m : if_pre_moves) if_post_moves.insert(m);
        for (auto& v : if_pre_uninit) if_post_uninit.insert(v);
        if_post_uninit_initialized = true;
    }
    moved_vars_ = if_any_non_diverging ? std::move(if_post_moves) : std::move(if_pre_moves);
    currently_uninit_vars_ = if_any_non_diverging && if_post_uninit_initialized
        ? std::move(if_post_uninit) : std::move(if_pre_uninit);

    // #118 — arm drop flags for locals whose ownership now depends on which
    // branch ran. Must come AFTER the merge above (so `moved_vars_` is the
    // union the drop walk will consult) and BEFORE the block is mirrored.
    {
        std::vector<CondMoveBranch> reaching;
        if (then_div != 1)
            reaching.push_back({&then_block, nullptr, then_moves, then_mark, then_end, owned_then});
        if (else_opt) {
            if (else_div != 1)
                reaching.push_back({&*else_opt, nullptr, else_moves, else_mark, else_end, owned_else});
        } else {
            // No `else` ≡ a fall-through path that moves (and releases) nothing.
            reaching.push_back({nullptr, nullptr, if_pre_moves, then_mark, then_mark, owned_pre});
        }
        elaborate_cond_moves(if_pre_moves, reaching, &owned_pre);
    }

    lir::SIf sif;
    sif.cond  = std::move(cond);
    sif.then_ = lir_mirror_block(*cur_prog_, then_block);
    if (else_opt) sif.else_ = lir_mirror_block(*cur_prog_, *else_opt);
    return make_stmt_emit(if_line, std::move(sif));
}


lir_view::StmtRef SemaChecker::lower_for(TinyMapView node) {
    const uint32_t for_line = node_line_;  // own line; body lowering moves node_line_
    // logos-core 2.7: a for may not run at all; restore tracker on exit.
    struct ForUninitGuard {
        std::set<std::string>& slot;
        std::set<std::string>  saved;
        ForUninitGuard(std::set<std::string>& s) : slot(s), saved(s) {}
        ~ForUninitGuard() { slot = std::move(saved); }
    } _uninit_guard(currently_uninit_vars_);
    auto var_name = str_of(node.get(la::NAME.code));
    // `for mut i in lo..hi`: the header's binding modifier (grammar IS_MUT).
    bool hdr_mut = node.has_key(la::IS_MUT) && node.get(la::IS_MUT.code).is_value() &&
                   node.get(la::IS_MUT.code).as_value<uint8_t>() != 0;

    lir::LExprPtr lo = node.has_key(la::LHS)
        ? lower_expr(map_of(node.get(la::LHS.code))) : error_expr();
    lir::LExprPtr hi = node.has_key(la::RHS)
        ? lower_expr(map_of(node.get(la::RHS.code))) : error_expr();

    if (!is_integer(expr_type(lo)) && TypeRef(expr_type(lo)).kind() != LogosType::Kind::Error)
        error(std::format("for range start must be integer, got {}", type_str(expr_type(lo))));
    if (!is_integer(expr_type(hi)) && TypeRef(expr_type(hi)).kind() != LogosType::Kind::Error)
        error(std::format("for range end must be integer, got {}", type_str(expr_type(hi))));

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
        int lo_w = int_kind_width(TypeRef(expr_type(lo)).kind());
        int hi_w = int_kind_width(TypeRef(expr_type(hi)).kind());
        int max_w = std::max(lo_w, hi_w);
        if (max_w > 32) {
            // prefer hi on tie (mirrors mlir_gen: hi checked first)
            var_t = (hi_w >= lo_w) ? expr_type(hi) : expr_type(lo);
        }
    }
    if (var_t == i32_t()) {
        auto intlit_overflows = [this](lir_view::ExprRef e) {
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
    define(var_name, var_t, hdr_mut);
    uint32_t _for_slot = lookup_slot(var_name);  // Phase-1: capture before pop_scope
    std::vector<lir_view::StmtRef> body;
    auto pre_loop_moves = moved_vars_;
    const size_t loop_clear_mark = flag_clear_log_.size();   // #118
    if (node.has_key(la::BODY)) {
        ++loop_depth_;
        if (!my_label.empty()) active_loop_labels_.push_back(my_label);
        loop_break_frames_.push_back({my_label, nullptr, false, "for"});
        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
        lower_block(map_of(node.get(la::BODY.code))).each_stmt([&](lir_view::StmtRef s){ body.push_back(s); });
        loop_break_frames_.pop_back();
        if (!my_label.empty()) active_loop_labels_.pop_back();
        --loop_depth_;
        merge_loop_exit_moves(body, map_of(node.get(la::BODY.code)), pre_loop_moves, loop_clear_mark);
    }
    pop_scope();

    lir::SFor sf;
    sf.var       = std::string(var_name);
    sf.lo        = std::move(lo);
    sf.hi        = std::move(hi);
    sf.inclusive = inclusive;
    sf.body      = lir_mirror_block(*cur_prog_, body);
    sf.label     = std::move(my_label);
    sf.slot      = _for_slot;
    sf.var_mut   = hdr_mut;
    return make_stmt_emit(for_line, std::move(sf));
}

lir_view::StmtRef SemaChecker::lower_for_each(TinyMapView node) {
    // `'l: for x in v` — the label LOWER_STMT's LABELED_LOOP left for this loop
    // (it was dropped here, so `break 'l` was "label not in scope").
    std::string my_label = std::move(pending_loop_label_);
    pending_loop_label_.clear();
    // logos-core 2.7: for-each may not run at all; restore tracker on exit.
    struct ForEachUninitGuard {
        std::set<std::string>& slot;
        std::set<std::string>  saved;
        ForEachUninitGuard(std::set<std::string>& s) : slot(s), saved(s) {}
        ~ForEachUninitGuard() { slot = std::move(saved); }
    } _uninit_guard(currently_uninit_vars_);
    // G-CONF-1: `for PATTERN in iter`. A bare-ident loop var arrives as NAME
    // (fast path); a destructuring pattern (`for (a,b) in v`) arrives as PAT.
    // Bind a synthetic element var and destructure the pattern from it as a
    // body prologue (see emit_for_pattern_destructure + the per-path wiring).
    bool for_has_pat = false;
    writ::TinyMapView for_pat{};
    std::string var_name;
    bool for_var_mut = false;  // `for mut x in …`
    if (node.has_key(la::PAT)) {
        for_pat = map_of(node.get(la::PAT.code));
        auto p = for_pat;
        if (code_of(p) == la::PAT_OR && p.has_key(la::ITEMS)) {
            auto alts = arr_of(p.get(la::ITEMS.code));
            if (alts.size() == 1) p = map_of(alts.get(0));
        }
        if (code_of(p) == la::PAT_WILD && p.has_key(la::NAME)) {
            var_name = std::string(str_of(p.get(la::NAME.code)));  // bare binding
            for_var_mut = pat_byval_mut(p);
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
    auto build_for_pat = [&](TypeRef bind_t) -> std::vector<lir_view::StmtRef> {
        std::vector<lir_view::StmtRef> pro;
        if (for_has_pat) emit_for_pattern_destructure(for_pat, var_name, bind_t, pro);
        return pro;
    };
    auto prepend_for_pat = [&](std::vector<lir_view::StmtRef>& body, std::vector<lir_view::StmtRef>& pro) {
        if (pro.empty()) return;
        pro.insert(pro.end(), std::make_move_iterator(body.begin()),
                   std::make_move_iterator(body.end()));
        body = std::move(pro);
    };

    // G161-2: `for x in &mut arr` — a bare `&mut <array-var>` lowers to a thin
    // `&mut elem` (stdlib-compat, see ADDR_OF_MUT), which isn't iterable. For
    // the for-loop, build a mutable slice over the array and iterate yielding
    // `&mut T` (the shared `&arr` form already slices + yields `&T`).
    bool for_mut_ref = false;
    lir::LExprPtr iter = nullptr;   // LExprPtr is a RAW ptr (ADR 0007) — must init,
                                    // else `if (!iter)` below reads an indeterminate
                                    // value and skips lowering the iterable.
    if (node.has_key(la::ITER)) {
        iter = lower_expr(map_of(node.get(la::ITER.code)));
    } else {
        iter = error_expr();
    }

    TypeRef iter_type = expr_type(iter);

    // `for x in &arr` / `&mut arr` / `&s.field` — a REFERENCE TO AN ARRAY
    // scrutinee iterates as the slice it borrows. This is the SEMANTIC form of
    // what used to be a syntactic special case (ADDR_OF_MUT over a VAR_REF
    // only): keyed on the type, it covers fields, rvalues and locals alike,
    // and it is what keeps for-in working when `&arr` types as `&[T; N]`.
    if ((TypeRef(iter_type).kind() == LogosType::Kind::Ref ||
         TypeRef(iter_type).kind() == LogosType::Kind::MutRef) &&
        TypeRef(iter_type).pointee() &&
        TypeRef(TypeRef(iter_type).pointee()).kind() == LogosType::Kind::Array) {
        TypeRef arr_t = TypeRef(iter_type).pointee();
        bool src_mut = TypeRef(iter_type).kind() == LogosType::Kind::MutRef;
        auto slice_t = make_slice_type(TypeRef(arr_t).elem(), src_mut);
        if (try_coerce_array_ref_to_slice(iter, slice_t)) {
            iter_type = expr_type(iter);
            if (src_mut) for_mut_ref = true;
        }
    }

    // ── array path (original) ────────────────────────────────────
    if (TypeRef(iter_type).kind() == LogosType::Kind::Array) {
        int64_t arr_size = (int64_t)TypeRef(iter_type).arr_size();
        TypeRef elem_type = TypeRef(iter_type).elem() ? TypeRef(iter_type).elem() : i32_t();

        // `for d in arr` is IntoIterator for [T; N]: each element is MOVED into
        // `d`, which owns it for one iteration. `d` is therefore a local of the
        // loop BODY frame (dropped at every iteration's end and on
        // break/continue/return); the untaken tail after a `break` is dropped
        // by codegen at the loop exit. The iterable itself is consumed.
        mark_moved_expr(iter);
        push_scope();
        uint32_t _fe_slot = 0xFFFFFFFFu;
        std::vector<lir_view::StmtRef> pat_pro;
        auto bind_loop_var = [&]() {
            define(var_name, elem_type, for_var_mut);
            _fe_slot = lookup_slot(var_name);
            pat_pro = build_for_pat(elem_type);
        };
        std::vector<lir_view::StmtRef> body;
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            if (!my_label.empty()) active_loop_labels_.push_back(my_label);
            loop_break_frames_.push_back({my_label, nullptr, false, "for"});
            pending_loop_body_scope_ = true;  // G167-4: tag the body frame
            pending_loop_body_init_ = bind_loop_var;
            lower_block(map_of(node.get(la::BODY.code))).each_stmt([&](lir_view::StmtRef s){ body.push_back(s); });
            loop_break_frames_.pop_back();
            if (!my_label.empty()) active_loop_labels_.pop_back();
            --loop_depth_;
        } else {
            bind_loop_var();
        }
        prepend_for_pat(body, pat_pro);
        pop_scope();

        lir::SForEach sfe;
        sfe.label = my_label;
        sfe.var       = std::string(var_name);
        sfe.iter      = std::move(iter);
        sfe.elem_type = elem_type;
        sfe.arr_size  = arr_size;
        sfe.body      = lir_mirror_block(*cur_prog_, body);
        sfe.slot      = _fe_slot;
        sfe.var_mut   = for_var_mut;
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
        define(var_name, make_ref(for_mut_ref, elem_type), for_mut_ref || for_var_mut);
        uint32_t _fe_slot = lookup_slot(var_name);  // Phase-1: before pop_scope
        auto pat_pro = build_for_pat(make_ref(for_mut_ref, elem_type));
        std::vector<lir_view::StmtRef> body;
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            if (!my_label.empty()) active_loop_labels_.push_back(my_label);
            loop_break_frames_.push_back({my_label, nullptr, false, "for"});
        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
            lower_block(map_of(node.get(la::BODY.code))).each_stmt([&](lir_view::StmtRef s){ body.push_back(s); });
            loop_break_frames_.pop_back();
            if (!my_label.empty()) active_loop_labels_.pop_back();
            --loop_depth_;
        }
        prepend_for_pat(body, pat_pro);
        pop_scope();
        lir::SForEach sfe;
        sfe.label = my_label;
        sfe.var       = std::string(var_name);
        sfe.iter      = std::move(iter);
        sfe.elem_type = elem_type;
        sfe.arr_size  = 0;
        sfe.is_slice  = true;
        sfe.body      = lir_mirror_block(*cur_prog_, body);
        sfe.slot      = _fe_slot;
        sfe.var_mut   = for_var_mut;
        return make_stmt_emit(node_line_, std::move(sfe));
    }

    // ── &Vec<T> path: Rust parity — `for x in &vec` borrows the Vec as a
    // slice and yields `&T` (NOT `T` by value via Vec::iter, which would move
    // out of a borrow). Desugar `&vec` → `vec.as_slice()` and reuse the by-ref
    // slice path. (By-value `for x in vec` keeps the consuming VecIter path.)
    if ((TypeRef(iter_type).kind() == LogosType::Kind::Ref ||
         TypeRef(iter_type).kind() == LogosType::Kind::MutRef) &&
        TypeRef(iter_type).pointee() &&
        is_stdlib_vec(TypeRef(iter_type).pointee())) {
        TypeRef vec_ty = TypeRef(iter_type).pointee();
        // `&mut Vec<T>` is `IntoIterator<Item = &mut T>` through
        // `as_mut_slice`; `&Vec<T>` yields `&T` through `as_slice`.
        const bool iter_mut = TypeRef(iter_type).kind() == LogosType::Kind::MutRef;
        const char* acc = iter_mut ? "Vec__as_mut_slice" : "Vec__as_slice";
        if (auto fit = find_func_candidates(acc); fit.size() == 1) {
            const SemaFuncInfo* as_slice_fn = fit[0];
            TypeRef elem_t = !TypeRef(vec_ty).type_args().empty()
                                 ? TypeRef(vec_ty).type_args()[0] : i32_t();
            TypeRef slice_ty = make_slice_type(elem_t, iter_mut);
            std::vector<lir::LExprPtr> pargs;
            // `for n in v` with `v: &mut Vec<T>` MOVES `v` in Rust —
            // `IntoIterator for &mut Vec` takes self by value — while this
            // desugar makes it a non-consuming `as_slice()` borrow, so a SECOND
            // loop over the same binding was admitted (issue-83924, E0382).
            // Marked here, at the PLACE: `&mut vals` written inline is an
            // rvalue (AddrOf, no name) and is untouched, so the fresh-reborrow
            // spelling stays legal, and a shared `&Vec` is Copy and stays legal.
            // ⚠ NOT through mark_moved_expr: its VarRef arm gates on
            // `is_move_type`, which calls `&mut T` Copy, and widening THAT arm
            // costs two pinned diagnostics for zero rows (PROBES.md §mraff).
            if (TypeRef(iter_type).kind() == LogosType::Kind::MutRef) {
                auto ier = expr_ref_of(iter);
                if (ier && ier.kind() == lir_schema::expr::Code::VarRef)
                    mark_moved(std::string(lir_view::EVarRefView{ier}.name()));
            }
            // `IntoIterator for &mut Vec` takes self BY VALUE: a named `&mut`
            // is moved into the loop, not reborrowed (issue-83924, E0382).
            const uint8_t* saved_nr = no_reborrow_arg_;
            if (iter_mut) no_reborrow_arg_ = expr_ref_of(iter).addr();
            pargs.push_back(std::move(iter));
            lir::LExprPtr slice_call = nullptr;
            if (!as_slice_fn->type_params.empty())
                slice_call = finish_generic_call(
                    as_slice_fn->symbol_name.empty() ? std::string(acc)
                                                     : as_slice_fn->symbol_name,
                    *as_slice_fn, {elem_t}, std::move(pargs));
            else
                slice_call = builder().call(
                    as_slice_fn->symbol_name.empty() ? std::string(acc)
                                                     : as_slice_fn->symbol_name,
                    {}, std::move(pargs), slice_ty);
            no_reborrow_arg_ = saved_nr;

            push_scope();
            define(var_name, make_ref(iter_mut, elem_t), for_var_mut);  // yields &T / &mut T
            uint32_t _fe_slot = lookup_slot(var_name);  // Phase-1: before pop_scope
            auto pat_pro = build_for_pat(make_ref(iter_mut, elem_t));
            std::vector<lir_view::StmtRef> body;
            if (node.has_key(la::BODY)) {
                ++loop_depth_;
                if (!my_label.empty()) active_loop_labels_.push_back(my_label);
            loop_break_frames_.push_back({my_label, nullptr, false, "for"});
        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
                lower_block(map_of(node.get(la::BODY.code))).each_stmt([&](lir_view::StmtRef s){ body.push_back(s); });
                loop_break_frames_.pop_back();
            if (!my_label.empty()) active_loop_labels_.pop_back();
                --loop_depth_;
            }
            prepend_for_pat(body, pat_pro);
            pop_scope();
            lir::SForEach sfe;
        sfe.label = my_label;
            sfe.var       = std::string(var_name);
            sfe.iter      = std::move(slice_call);
            sfe.elem_type = elem_t;
            sfe.arr_size  = 0;
            sfe.is_slice  = true;
            sfe.body      = lir_mirror_block(*cur_prog_, body);
            sfe.slot      = _fe_slot;
            sfe.var_mut   = for_var_mut;
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
                lir::LExprPtr it_call = nullptr;
                if (!iif->type_params.empty())
                    it_call = finish_generic_call(sym, *iif, std::move(targs), std::move(pargs));
                else
                    it_call = builder().call(sym, {}, std::move(pargs), iter_ret);
                iter = std::move(it_call);
                iter_type = expr_type(iter);
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
            // The key is a BARE name (`$ref_Vec__into_iter`): a candidate is
            // this receiver's only when its `self` names the SAME declaration —
            // a package-local `Vec` must not reach the stdlib's impl.
            TypeRef ii_owner = is_ref_like(TypeRef(iter_type).kind()) ? TypeRef(iter_type).pointee()
                                                                      : iter_type;
            auto ii_owner_ok = [&](const SemaFuncInfo* fi) {
                if (!fi || fi->param_types.empty() || !ii_owner) return fi != nullptr;
                TypeRef st = fi->param_types[0];
                while (st && is_ref_like(TypeRef(st).kind()) && TypeRef(st).pointee())
                    st = TypeRef(st).pointee();
                if (!st || (TypeRef(st).kind() != LogosType::Kind::Struct &&
                            TypeRef(st).kind() != LogosType::Kind::ZonedStruct))
                    return true;
                return TypeRef(st).pkg_name() == TypeRef(ii_owner).pkg_name();
            };
            for (auto& k : ii_keys) {
                if (auto fit = find_func_by_base_and_signature(k, {}, false); ii_owner_ok(fit)) { ii_fn = fit; ii_key_chosen = k; break; }
                if (auto git = find_generic_func(k); ii_owner_ok(git)) { ii_fn = git; ii_key_chosen = k; break; }
                if (auto cands = find_func_candidates(k); cands.size() == 1 && ii_owner_ok(cands[0])) { ii_fn = cands[0]; ii_key_chosen = k; break; }
            }
            if (ii_fn) {
                // Build subst from iter_type's pointee (for ref-impl) or the
                // type itself (struct).
                SemaSubst ii_subst;
                TypeRef target_ty = is_ref_like(TypeRef(iter_type).kind())
                                        ? TypeRef(iter_type).pointee() : iter_type;
                if (target_ty && !TypeRef(target_ty).type_args().empty()) {
                    SemaStructInfo* si2 = nullptr;
                    { auto [p, si] = struct_of(TypeRef(target_ty)); si2 = si; }
                    if (!si2) { auto [p, di] = datatype_of(TypeRef(target_ty)); si2 = di; }
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
            // Rust E0277: the TYPE, as written, is not an iterator (neither
            // `next()` nor an `into_iter()` reaching one) — not the mangled name.
            error(std::format("for-in: `{}` is not an iterator — it has no `next()` and no "
                              "`into_iter()` (E0277)", type_str(iter_type)));
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
                DefId qkey = type_id(TypeRef(iter_type).pkg_name(), lookup_name);
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
        auto [epkg_forin, esi_forin] = enum_of(TypeRef(next_ret));
        auto eit = esi_forin ? enums_.find(type_id(epkg_forin, TypeRef(next_ret).enum_name())) : enums_.end();
        if (eit == enums_.end()) eit = enums_.find(type_id({}, TypeRef(next_ret).enum_name()));
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
        //
        // ⚠ #110 — THIS IS A MOVE, and it was never marked. `for x in it`, with
        // `it` a named iterator local, hands `it`'s bytes to `__for_iter_N`;
        // with the drop half of #110 fixed, main's `it` slot then ran the
        // destructor a SECOND time (MEASURED: `let it = Option::Some(Inner{7})
        // .into_iter(); for x in it { }` printed two `DROP n=7`). It stayed
        // invisible before only because `OptionIter` was misclassified Copy, so
        // nothing anywhere tracked a move of it — one wrong answer masking
        // another. `mark_moved_expr` self-gates to VarRef/FieldRead/TupleIndex,
        // so the `for x in v.iter()` / `for x in Some(..)` spellings (whose
        // scrutinee is a CALL, owning nothing named) are unaffected.
        if (is_move_type(iter_type)) mark_moved_expr(expr_ref_of(iter));
        std::string iter_var = "__for_iter_" + std::to_string(tmp_var_count_++);
        lir::SLet let_iter;
        let_iter.name   = iter_var;
        let_iter.type   = iter_type;
        let_iter.is_mut = true;
        let_iter.value  = std::move(iter);

        // Build outer block: { let mut __iter = iter; loop { match __iter.next() ... } }
        std::vector<lir_view::StmtRef> outer_block;
        outer_block.push_back(make_stmt_emit(node_line_, std::move(let_iter)));

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

        // ⚠ #110 LEAK HALF — the two OWNING slots this desugar creates had NO
        // drop glue at all. MEASURED 2026-08-22 (pre-fix): `for x in v` over a
        // `Vec<Inner>` with a printing `Drop` printed ZERO destructor lines for
        // two elements; `for x in Option::Some(Inner{7})` printed zero. The
        // hand-written equivalent (`match it.next() { Some(x) => … }`) printed
        // one, so the loss is in THIS desugar, not in the drop machinery.
        //
        // Both slots are synthesised here and so must be released here:
        //   · the ITEM BINDING `x`, owned for one iteration — frame B below,
        //     dropped at the end of every arm body;
        //   · the ITERATOR `__for_iter_N`, owned for the whole loop — frame A,
        //     dropped after the SLoop. On a normal exhaustion its glue is a
        //     runtime no-op (the iterator is empty); on an early `break` it is
        //     the only thing that frees the un-yielded items.
        //
        // The loop boundary moves OUT one frame, from the body block to frame
        // B. `collect_drops_to_loop` walks down to AND INCLUDING the first
        // loop_boundary frame, so a `break`/`continue` in the body now releases
        // the item binding too — with the old tagging the walk stopped at the
        // body frame and `x` leaked on every early exit. Frame A stays OUTSIDE
        // the boundary on purpose: the iterator must survive a `continue`.
        push_scope();                          // frame A — the iterator
        define(iter_var, iter_type, true);
        push_scope();                          // frame B — the item binding
        scope_.back().loop_boundary = true;
        define(std::string(var_name), elem_type, for_var_mut);
        auto pat_pro = build_for_pat(elem_type);
        std::vector<lir_view::StmtRef> then_body;
        if (node.has_key(la::BODY)) {
            ++loop_depth_;
            if (!my_label.empty()) active_loop_labels_.push_back(my_label);
            loop_break_frames_.push_back({my_label, nullptr, false, "for"});
            lower_block(map_of(node.get(la::BODY.code))).each_stmt([&](lir_view::StmtRef s){ then_body.push_back(s); });
            loop_break_frames_.pop_back();
            if (!my_label.empty()) active_loop_labels_.pop_back();
            --loop_depth_;
        }
        prepend_for_pat(then_body, pat_pro);
        for (auto& d : collect_drops()) then_body.push_back(std::move(d));
        pop_scope();                           // frame B
        auto iter_drops = collect_drops();      // frame A — after the SLoop
        pop_scope();                           // frame A

        // Else arm: _ → break
        std::vector<lir_view::StmtRef> else_body;
        else_body.push_back(builder().stmt_break(nullptr, "", node_line_));

        auto some_mo = lir_mirror_emit_pat_variant_data(
            *cur_prog_, some_pat.enum_name, some_pat.variant, some_pat.disc,
            some_pat.bindings, some_pat.binding_types, {},
            std::vector<uint32_t>{for_var_mut ? 0x10u : 0u});
        lir::Pattern some_pattern;
        some_pattern.mirror_ptr_ = some_mo;
        std::vector<std::pair<lir::Pattern, std::vector<lir_view::StmtRef>>> arms;
        arms.emplace_back(std::move(some_pattern), std::move(then_body));
        arms.emplace_back(make_pat_wild("_"), std::move(else_body));

        std::vector<lir_view::StmtRef> loop_body;
        loop_body.push_back(unit_match_stmt_(make_next_call(), std::move(arms)));
        lir::SLoop sl; sl.body = lir_mirror_block(*cur_prog_, loop_body); sl.label = my_label;
        outer_block.push_back(make_stmt_emit(node_line_, std::move(sl)));
        for (auto& d : iter_drops) outer_block.push_back(std::move(d));

        // Wrap in a block statement
        return make_stmt_emit(node_line_, lir::SBlock{lir_mirror_block(*cur_prog_, outer_block), /*transparent=*/true});
    }

    return builder().stmt_break(nullptr, "", node_line_);
}

lir_view::StmtRef SemaChecker::lower_loop(TinyMapView node) {
    const uint32_t loop_line = node_line_;  // own line; body lowering moves node_line_
    // Capture label before lowering body (same reason as in lower_for).
    std::string my_label = std::move(pending_loop_label_);
    pending_loop_label_.clear();

    std::vector<lir_view::StmtRef> body;
    TypeRef frame_value_type = nullptr;
    bool frame_break_reached = false;
    // logos-core 2.7: loops are CONSERVATIVE — the body may run zero times,
    // so any var initialised only inside the body must stay "uninit" at the
    // outer scope. Snapshot pre-state; restore after to discard body inits.
    auto loop_pre_uninit = currently_uninit_vars_;
    if (node.has_key(la::BODY)) {
        ++loop_depth_;
        if (!my_label.empty()) active_loop_labels_.push_back(my_label);
        const bool is_while = hir_origin_(node) == hir::Origin::While;
        loop_break_frames_.push_back({my_label, nullptr, false, is_while ? "while" : nullptr,
                                      is_while ? TypeRef(nullptr) : hint_expected_type_});
        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
        lower_block(map_of(node.get(la::BODY.code))).each_stmt([&](lir_view::StmtRef s){ body.push_back(s); });
        frame_value_type    = loop_break_frames_.back().value_type;
        frame_break_reached = (frame_value_type != nullptr) ||
                               loop_break_frames_.back().without_value;
        loop_break_frames_.pop_back();
        if (!my_label.empty()) active_loop_labels_.pop_back();
        --loop_depth_;
    }
    currently_uninit_vars_ = std::move(loop_pre_uninit);
    // `loop { /* no break */ }` diverges: its expression form types as `!`
    // (logos-core 1.1). Communicated to the caller via last_loop_diverged_.
    last_loop_diverged_ = !frame_break_reached;
    lir::SLoop sl;
    sl.body  = lir_mirror_block(*cur_prog_, body);
    sl.label = std::move(my_label);
    if (frame_value_type) {
        sl.result_type = frame_value_type;
        sl.break_slot  = "__loop_val_" + std::to_string(tmp_var_count_++);
    }
    return make_stmt_emit(loop_line, std::move(sl));
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
writ::TinyMapView SemaChecker::unwrap_paren_node(writ::TinyMapView n) {
    while (code_of(n) == la::PAREN_EXPR && n.has_key(la::VALUE))
        n = map_of(n.get(la::VALUE.code));
    return n;
}
bool SemaChecker::place_recv_is_simple(writ::TinyMapView recv) {
    int32_t code = code_of(unwrap_paren_node(recv));
    return code == la::VAR_REF || code == la::DEREF;
}
// A field-read base whose address gen_lvalue_addr can compute: a bare var /
// `*p`, a field chain, OR an index into a place — incl. a field off a
// struct-ARRAY element (`a[i].x`, `g.rows[i].cells[j].v`). The struct-element
// stride is now handled (gen_lvalue_addr + gen_index_read inline slot), so
// index-then-field places lower correctly.
bool SemaChecker::place_field_base_ok(writ::TinyMapView recv) {
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
    // A tuple element under a field chain / an index (`w.t.0 = v`): the same
    // bases a field write accepts — gen_lvalue_addr walks both.
    if (pc == la::TUPLE_INDEX)
        return place_field_base_ok(map_of(place.get(la::RECEIVER.code)));
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
            // A temporary-rooted place (lower_temp_rooted_place_assign_) has no
            // user-visible name: Rust's sentence for `(&x)[0].v = …`.
            if (name.starts_with("__atmp_"))
                error("cannot assign to data in a `&` reference (E0594)");
            else
                error(std::format("assignment through a shared reference (variable '{}' is `&`)", name));
            return false;
        }
        if (k == LogosType::Kind::MutRef) return true;  // `&mut T` — writable
        if (k == LogosType::Kind::DstRef) {  // `&mut DstStruct` writable; `&DstStruct` not
            if (!TypeRef(t).mut_ptr()) {
                error(std::format("assignment through a shared reference (variable '{}' is `&DstStruct`)", name));
                return false;
            }
            return true;
        }
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
        // §6.2 statics (S25): a place rooted at a `static mut` is writable
        // (the storage is mutable) but the write requires `unsafe`. A plain
        // immutable `static` is not writable.
        if (is_module_static_unshadowed(name)) {
            if (module_static_muts_.count(name)) {
                if (!inside_unsafe_)
                    error(std::format(
                        "write to mutable static `{}` requires `unsafe` block "
                        "(Rust `items.static.mut.safety`)", name));
                return true;
            }
            error(std::format("assignment to immutable static '{}'", name));
            return false;
        }
        // A field write into a never-initialised binding is E0381 (the
        // borrow checker's "partially assigned binding"), whatever its
        // mutability: rustc judges initialisation first.
        if (!lookup_is_mut(name) && !is_deferred_init(name)) {
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
        // Slice element write (B6/P2-11): a `&mut [T]` is writable, a shared
        // `&[T]` is not — reject `s[i] = v` through a shared slice.
        if (rt && TypeRef(rt).kind() == LogosType::Kind::Slice) {
            if (!TypeRef(rt).mut_ptr()) {
                error("cannot write through a shared `&[T]` slice (need `&mut [T]`)");
                return false;
            }
            return true;
        }
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
TypeRef SemaChecker::resolve_place_type(writ::TinyMapView place) {
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

// `<S as Index<I>>::Output` for a struct receiver with an `Index` impl (`Vec<T>`
// → `T`), from the impl's trait arguments; null when there is none.
TypeRef SemaChecker::index_output_type_(TypeRef st) {
    if (!st || TypeRef(st).kind() != LogosType::Kind::Struct) return nullptr;
    const SemaImplInfo* ii = nullptr;
    if (auto it = impls_.find(impl_key("Index", concrete_struct_name(st))); it != impls_.end()) ii = &it->second;
    else if (auto it2 = impls_.find(impl_key("Index", std::string(TypeRef(st).struct_name()))); it2 != impls_.end()) ii = &it2->second;
    if (!ii || ii->trait_type_args.size() < 2) return nullptr;
    SemaSubst subst;
    if (ii->target_typeref) {
        auto pat = TypeRef(ii->target_typeref).type_args();
        auto cur = TypeRef(st).type_args();
        for (size_t k = 0; k < pat.size() && k < cur.size(); ++k)
            if (pat[k] && TypeRef(pat[k]).kind() == LogosType::Kind::TypeVar)
                subst[std::string(TypeRef(pat[k]).type_var_name())] = cur[k];
    }
    return subst_type_sema(ii->trait_type_args[1], subst);
}

std::optional<lir_view::StmtRef> SemaChecker::try_index_mut_assign(
    const std::string& arr_name, TypeRef arr_type,
    writ::TinyMapView idx_node, writ::TinyMapView val_node) {
    if (!arr_type || TypeRef(arr_type).kind() != LogosType::Kind::Struct)
        return std::nullopt;
    auto type_name = concrete_struct_name(arr_type);
    auto base_name = std::string(TypeRef(arr_type).struct_name());
    bool has_im = has_impl("IndexMut", type_name) ||
                  (!base_name.empty() && has_impl("IndexMut", base_name));
    if (!has_im) return std::nullopt;
    if (!lookup_is_mut(arr_name))
        error(std::format("index write to immutable struct '{}'", arr_name));
    auto mangled = type_name + "__index_mut";
    lir::LExprPtr idx_e = lower_expr(idx_node);
    lir::LExprPtr val_e = lower_expr(val_node);
    // `index_mut` hands back `&mut Output` to a LIVE element: the write drops
    // the old value first, as `*r = v` through any `&mut` does (it leaked).
    const TypeRef out_ty = expr_type(val_e);
    const bool drop_old = out_ty && (TypeRef(out_ty).owning_trait_object() ||
                                     !drop_fn_for(out_ty).empty() ||
                                     has_droppable_fields(out_ty));
    const SemaFuncInfo* fit = nullptr;
    for (auto* c : find_func_candidates(mangled))
        if (c->param_types.size() == 2) { fit = c; break; }
    if (fit) {
        widen_int_expr(idx_e, fit->param_types[1], builder());
        auto recv_ref = builder().addr_of(arr_name, make_ref(true, arr_type), BorrowOrigin::OperatorAutoref);
        std::vector<lir::LExprPtr> args;
        args.push_back(std::move(recv_ref));
        args.push_back(std::move(idx_e));
        auto call_e = builder().call(
            fit->symbol_name.empty() ? mangled : fit->symbol_name,
            {}, std::move(args), fit->ret_type);
        track_write_move(val_e);
        return builder().stmt_deref_write(std::move(call_e), std::move(val_e), node_line_, drop_old);
    }
    const SemaImplInfo* ii = nullptr;
    if (auto it = impls_.find(impl_key("IndexMut", type_name)); it != impls_.end()) ii = &it->second;
    else if (auto it2 = impls_.find(impl_key("IndexMut", base_name)); it2 != impls_.end()) ii = &it2->second;
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
        mc.receiver = builder().addr_of(arr_name, make_ref(true, arr_type), BorrowOrigin::OperatorAutoref);
        mc.method = "index_mut";
        mc.args.push_back(std::move(idx_e));
        mc.vtable_index = -1;
        mc.resolved_type = "";
        auto call_e = builder().method_call_v(std::move(mc), make_ref(true, out_t));
        track_write_move(val_e);
        return builder().stmt_deref_write(std::move(call_e), std::move(val_e), node_line_, drop_old);
    }
    return std::nullopt;
}

// ADR 0011 — schema field WRITE: `p.field = v` ⇒ `(&mut* p.m).set(KEY, WAny::from(v))`.
// TOM `set` is fixed-capacity in-place (no realloc), so the thin view suffices for
// Pod-fitting values. Boxed types (i32/i64/u32/u64/f64/str) need the arena allocator
// (a fat view) — deferred; they error with a clear pointer here.
std::optional<lir_view::StmtRef> SemaChecker::try_schema_field_write(
    const std::string& recv_name, const std::string& field_name,
    writ::TinyMapView val_node) {
    TypeRef rt = lookup(recv_name);
    // Peel &/&mut/* to reach the schema struct (a `&mut self` method's receiver is
    // a MutRef). `rt` (the var's actual type) is kept for var_ref — field_read on a
    // &mut Pt auto-derefs to the view; only the schema lookup needs the base type.
    TypeRef base = rt;
    while (base && (is_ref_like(TypeRef(base).kind()) ||
                    TypeRef(base).kind() == LogosType::Kind::Ptr) && TypeRef(base).pointee())
        base = TypeRef(base).pointee();
    if (!base || TypeRef(base).kind() != LogosType::Kind::Struct) return std::nullopt;
    auto sname = std::string(TypeRef(base).struct_name());
    auto [spkg, ssi] = find_struct_by_name(sname);
    if (!ssi || !ssi->is_schema) return std::nullopt;

    int found = -1;
    for (size_t i = 0; i < ssi->schema_fields.size(); ++i)
        if (ssi->schema_fields[i].name == field_name) { found = (int)i; break; }
    if (found < 0) {
        error(std::format("schema '{}' has no field '{}'", sname, field_name));
        lir::SExprStmt es; es.expr = error_expr();
        return make_stmt_emit(node_line_, std::move(es));
    }
    uint8_t key   = ssi->schema_keys[found];
    TypeRef ftype = ssi->schema_fields[found].type;
    // ADR 0011 generics — substitute the template field type with the receiver's
    // concrete type-args (Box<i64> → T becomes i64). At a generic use-site (Box<T>)
    // it stays a TypeVar → the to_wany emission below uses the bare `T__to_wany`.
    {
        auto rargs = TypeRef(base).type_args();
        if (!ssi->type_params.empty() && !rargs.empty()) {
            SemaSubst sub;
            for (size_t i = 0; i < ssi->type_params.size() && i < rargs.size(); ++i)
                sub[ssi->type_params[i].name] = rargs[i];
            ftype = subst_type_sema(ftype, sub);
        }
    }

    lir::LExprPtr val = lower_expr(val_node);
    expect_type(val, ftype, CoercePos::PlaceWrite,
                std::format("schema write '{}.{}':", recv_name, field_name));
    track_write_move(val);

    // Build a WAny from the typed value via `WritField::to_wany(self, z)` — the
    // T→WAny conversion lives in the stdlib trait (extensible; the generic-schema
    // seam). `z` (the view-carried arena allocator) is passed for boxing; inline
    // conversions ignore it. A dynamic `WAny` field stores the value verbatim.
    using K = LogosType::Kind;
    TypeRef wany = make_synth_enum("WAny");
    lir::LExprPtr wany_val;
    if (TypeRef(ftype).kind() == K::Enum && TypeRef(ftype).enum_name() == "WAny") {
        wany_val = std::move(val);
    } else {
        // Symbol for `<T>::to_wany`. Generic body (ftype still a TypeVar): emit the
        // BARE `T__to_wany` — mono retargets T→concrete at instantiation (do NOT
        // resolve, there is no `T__to_wany`). Concrete: resolve `<typename>__to_wany`.
        std::string sym;
        if (TypeRef(ftype).kind() == K::TypeVar) {
            sym = std::string(TypeRef(ftype).type_var_name()) + "__to_wany";
        } else {
            std::string tn = writfield_type_name(ftype);
            if (tn.empty()) {
                error(std::format("schema write '{}.{}': field type '{}' is not a WritField "
                                  "(no T→WAny conversion)", recv_name, field_name, type_str(ftype)));
                lir::SExprStmt es; es.expr = error_expr();
                return make_stmt_emit(node_line_, std::move(es));
            }
            std::string base = tn + "__to_wany";
            sym = base;
            for (auto* c : find_func_candidates(base))
                if (c && c->param_types.size() == 2) {
                    sym = c->symbol_name.empty() ? base : c->symbol_name; break;
                }
        }
        TypeRef alloc_ptr = make_ptr(true, make_synth_struct("Allocator"));
        auto z_raw = builder().field_read(builder().var_ref(recv_name, rt), "z",
                                          make_ptr(true, u8_t()));
        auto z = builder().cast(std::move(z_raw), alloc_ptr);
        std::vector<lir::LExprPtr> args;
        args.push_back(std::move(val));   // to_wany(self: T, z)
        args.push_back(std::move(z));
        // Generic-struct WritField (WQL `WRef<SExpr>`): pass the field's concrete
        // type-args so mono binds the impl's type-param (e.g. S→SExpr) rather than
        // leaving the bare `WRef$G1$S` template unresolved.
        std::vector<TypeRef> targs;
        if (TypeRef(ftype).kind() == K::Struct || TypeRef(ftype).kind() == K::ZonedStruct)
            for (auto ta : TypeRef(ftype).type_args()) targs.push_back(ta);
        wany_val = builder().call(sym, std::move(targs), std::move(args), wany);
    }

    // (&mut * p.m).set(key, wany).  m is *const WMap<Wu6,WAny>; reinterpret to &mut.
    TypeRef wmap = make_synth_generic_struct("WMap", {make_synth_struct("Wu6"), make_synth_enum("WAny")});
    auto recv_expr = builder().var_ref(recv_name, rt);
    auto m_ptr = builder().field_read(std::move(recv_expr), "m", make_ptr(false, wmap));
    auto m_mut = builder().cast(std::move(m_ptr), make_ref(true, wmap));
    std::vector<lir::LExprPtr> sargs;
    sargs.push_back(builder().lit_int(static_cast<int64_t>(key), prim(LogosType::Kind::U8)));
    sargs.push_back(std::move(wany_val));
    auto setcall = builder().method_call(std::move(m_mut), "set", "", {},
                                         std::move(sargs), -1, void_t());
    lir::SExprStmt es; es.expr = std::move(setcall);
    return make_stmt_emit(node_line_, std::move(es));
}

lir_view::StmtRef SemaChecker::lower_place_assign(TinyMapView node) {
    auto place_node = unwrap_paren_node(map_of(node.get(la::RECEIVER.code)));  // `(*b) = 7`
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
            // AN `Index` IMPL IS NOT A WRITABLE PLACE (E0594). LANDED
            // 2026-08-30 (was `indexnomut`). `try_index_mut_assign` returns
            // nullopt when the receiver's type has no `IndexMut` impl, and the
            // fall-through below reaches the RAW address machinery, which
            // writes the struct's FIRST FIELD by accident: `m[0i64] = 9i64`
            // over an `Index`-only type compiled and stored the wrong thing.
            // The whole live arrival population of this site is that shape —
            // every legal spelling measured (both impls, a native array, an
            // element field write, a FieldRead base, a Vec, a READ through
            // `Index`) leaves before here or is not a Struct VarRef — so the
            // refusal is structural, not a corpus reading.
            TypeRef at_ = lookup(an);
            if (at_ && TypeRef(at_).kind() == LogosType::Kind::Struct) {
                auto tn_ = concrete_struct_name(at_);
                auto bn_ = std::string(TypeRef(at_).struct_name());
                bool hix_ = has_impl("Index", tn_) ||
                            (!bn_.empty() && has_impl("Index", bn_));
                bool him_ = has_impl("IndexMut", tn_) ||
                            (!bn_.empty() && has_impl("IndexMut", bn_));
                if (hix_ && !him_) {
                    error(std::format("cannot assign to index of '{}': type "
                                      "'{}' implements `Index` but not "
                                      "`IndexMut`", an,
                                      tn_.empty() ? bn_ : tn_));
                    lir::SExprStmt es_; es_.expr = error_expr();
                    return make_stmt_emit(node_line_, std::move(es_));
                }
            }
        }
    }
    // T1-10 (B78) + T1.5 field-level drop-before-replace: assigning to a
    // field place re-initialises it. Two coupled actions on a pure
    // FieldRead chain over an OWNED local root (no pointer/reference hops —
    // those never entered moved_vars_ and their old value is owned
    // elsewhere):
    //   (a) the OLD value at the place must be dropped before the store
    //       (T1.5: `i.s = new` over a live field leaked the old String),
    //       UNLESS the path (or an ancestor) was already moved out;
    //   (b) lift the drop suppression for the covered moved paths (equal
    //       AND deeper — writing `o.i` refills `o.i.s`) so the scope-end
    //       drop releases the NEW value.
    bool field_old_live = false;   // → emit drop_old at the place store
    // Every place kind whose store re-initialises a statically named part of a
    // value we exclusively own. See PROBES.md, round 2026-09-06n.
    if (pc == la::FIELD_READ || pc == la::TUPLE_INDEX || pc == la::INDEX_READ) {
        std::vector<std::string> segs;
        auto cur = place_node;
        bool through_index = false;
        bool via_index_mut = false;   // reached through `IndexMut` / `&mut [T]`
        for (;;) {
            if (cur.is_null()) break;
            const int32_t cc = code_of(cur);
            if ((cc == la::FIELD_READ || cc == la::TUPLE_INDEX) &&
                cur.has_key(la::FIELD) && cur.has_key(la::RECEIVER)) {
                // A TUPLE_INDEX segment must normalise to the decimal text
                // `move_path_of` records, or the overlap check below compares
                // against a path nothing ever marks.
                std::string seg(str_of(cur.get(la::FIELD.code)));
                if (cc == la::TUPLE_INDEX)
                    seg = std::to_string((uint64_t)parse_int_literal(
                              str_of(cur.get(la::FIELD.code))));
                segs.emplace_back(std::move(seg));
                cur = unwrap_paren_node(map_of(cur.get(la::RECEIVER.code)));
                continue;
            }
            if (cc == la::INDEX_READ && cur.has_key(la::RECEIVER)) {
                // ONLY a fixed-size ARRAY. Its elements are inline storage of the
                // container, so the root's ownership answers for them. Every
                // other indexable — a `Vec`, a slice, a raw-pointer buffer —
                // holds its elements behind a POINTER the root does not own, and
                // coarsening to the root there drops uninitialised memory: the
                // wider rule aborted `no_auto_drop_container` and segfaulted
                // `zz_btree_dyn_probe` at `Vec::push`. A subscript has no static
                // path, so the array case falls back to the CONTAINER and lets
                // the overlap check answer for the whole root.
                auto recv_n = unwrap_paren_node(map_of(cur.get(la::RECEIVER.code)));
                // An element of an element (`v[0][0]`): the inner `Index`
                // output is the outer receiver's type.
                auto place_ty = [&](auto&& self, TinyMapView n) -> TypeRef {
                    TypeRef t = resolve_place_type(n);
                    if (t || code_of(n) != la::INDEX_READ) return t;
                    TypeRef rt = self(self, unwrap_paren_node(map_of(n.get(la::RECEIVER.code))));
                    if (rt && is_ref_like(TypeRef(rt).kind()) && TypeRef(rt).pointee())
                        rt = TypeRef(rt).pointee();
                    return index_output_type_(rt);
                };
                TypeRef recv_t = place_ty(place_ty, recv_n);
                // `q[i]` with `q: &mut [T; N]` indexes the referent array.
                if (recv_t && TypeRef(recv_t).kind() == LogosType::Kind::MutRef &&
                    TypeRef(recv_t).pointee())
                    recv_t = TypeRef(recv_t).pointee();
                // A `Vec` / user `IndexMut` element or a `&mut [T]` element is a
                // `&mut` referent: fully initialised and never moved out of, so
                // the written place's old value is LIVE (`v[i] = x` is
                // `*index_mut(&mut v, i) = x` and drops it; it leaked). A raw
                // pointer buffer stays manual.
                if (recv_t && (TypeRef(recv_t).kind() == LogosType::Kind::Slice ||
                               (TypeRef(recv_t).kind() == LogosType::Kind::Struct &&
                                (has_impl("IndexMut", concrete_struct_name(recv_t)) ||
                                 has_impl("IndexMut", std::string(TypeRef(recv_t).struct_name())))))) {
                    via_index_mut = true;
                    break;
                }
                if (!recv_t || TypeRef(recv_t).kind() != LogosType::Kind::Array)
                    break;
                through_index = true;
                segs.clear();
                cur = recv_n;
                continue;
            }
            if (cc == la::DEREF && cur.has_key(la::VALUE)) {
                // `(*q).d` IS `q.d`. An explicit deref of a named place adds no
                // path segment — the auto-deref sugar and the written one must
                // reach the same root and the same path text, or two spellings
                // of one assignment answer differently. What the root IS decides
                // ownership, below: a raw pointer and a shared `&` are excluded
                // there, so this collapse never widens to memory we do not own.
                auto inner = unwrap_paren_node(map_of(cur.get(la::VALUE.code)));
                if (!inner.is_null() && code_of(inner) == la::VAR_REF) {
                    cur = inner;
                    continue;
                }
                break;
            }
            break;
        }
        cur = unwrap_paren_node(cur);
        if (via_index_mut) {
            field_old_live = true;
        } else if (!cur.is_null() && code_of(cur) == la::VAR_REF) {
            std::string root(str_of(cur.get(la::NAME.code)));
            std::string path(root);
            for (auto it = segs.rbegin(); it != segs.rend(); ++it) {
                path.push_back('.');
                path += *it;
            }
            // Old value is live (droppable + present) iff we have EXCLUSIVE
            // write access to its place: an OWNED value local, OR a `&mut`
            // referent. Writing `(*self).f = new` through a unique borrow
            // overwrites a LIVE field — the owner drops the NEW value at its
            // scope end, never the old one, so the old must be dropped HERE
            // (without this, `self.f = x` leaked f's old value). A `&mut`
            // referent is always fully initialised and cannot have a moved-out
            // field (you cannot move out of a borrow), so its field is live.
            // A shared `&` is not assignable; a raw `*mut`/`*const` stays
            // MANUAL (no implicit drop — writing into uninit memory is the
            // whole point of a raw pointer). Also require the path/ancestors
            // not moved out (a moved-out value is already gone). A moved
            // DESCENDANT (`path.x`) still leaves siblings live — but the
            // whole-field memcpy store overwrites them, so the broad overlap
            // check stays conservative-correct: skip drop_old when any overlap
            // exists, lift-then-rely on the move bookkeeping.
            TypeRef root_ty = lookup(root);
            bool root_owned = root_ty &&
                TypeRef(root_ty).kind() != LogosType::Kind::Ref &&
                TypeRef(root_ty).kind() != LogosType::Kind::Ptr;
            bool any_overlap = false;
            std::string pre = path + ".";
            std::string anc = root;  // ancestors: root, root.a, … up to path
            // Build ancestor-prefix set and check moved membership/overlap.
            for (auto& mv : moved_vars_) {
                bool eq_or_under = mv == path ||
                    (mv.size() > pre.size() && mv.compare(0, pre.size(), pre) == 0);
                bool ancestor = path.size() > mv.size() + 1 &&
                    path.compare(0, mv.size(), mv) == 0 && path[mv.size()] == '.';
                if (eq_or_under || ancestor) { any_overlap = true; break; }
            }
            field_old_live = root_owned && !decl_uninit_vars_.count(root) &&
                             !any_overlap;
            // (b) lift suppression for the covered paths — only where the
            // store re-initialises the path itself. `a[i] = v` re-initialises
            // ONE element, never the container, so an index link suppresses it.
            for (auto it = moved_vars_.begin();
                 !through_index && it != moved_vars_.end(); ) {
                bool covered = *it == path ||
                    (it->size() > pre.size() &&
                     it->compare(0, pre.size(), pre) == 0);
                it = covered ? moved_vars_.erase(it) : ++it;
            }
        }
    }
    // (The DataRef<ZonedStruct> ergonomic field write that used to be handled
    // here was DELETED with `is_dataref` — task #99; see sema_impl.hpp.)
    if (pc == la::FIELD_READ) {
        auto recv = map_of(place_node.get(la::RECEIVER.code));
        if (code_of(recv) == la::VAR_REF) {
            auto rn = std::string(str_of(recv.get(la::NAME.code)));
            // ADR 0011 — schema field write `p.field = v`.
            if (auto s = try_schema_field_write(rn,
                    std::string(str_of(place_node.get(la::FIELD.code))),
                    map_of(node.get(la::VALUE.code))))
                return std::move(*s);
        }
    }
    // Bound to the place shapes the address-of machinery lowers correctly;
    // deeper nestings hit a pre-existing read-side limitation — reject cleanly
    // (with a workaround) rather than miscompile/crash.
    if (!place_write_supported(place_node)) {
        if (auto t = lower_temp_rooted_place_assign_(node, place_node)) return *t;
        error("assignment target too deeply nested to assign in place yet; "
              "bind an intermediate (e.g. `let r = &mut <inner>; r[i] = …`)");
        lir::SExprStmt es; es.expr = error_expr();
        return make_stmt_emit(node_line_, std::move(es));
    }
    // Uniform place-subsystem writability check (closes the soundness gap where
    // a deep-nested write through an immutable place / `*const` was accepted).
    check_place_writable(place_node);
    // §6.1: writes to union fields are safe (Rust spec
    // `items.union.fields.write-safety`); set the flag so
    // lower_field_read skips the union unsafe gate for this LHS.
    bool saved_place_write = in_place_write_lhs_;
    in_place_write_lhs_ = true;
    auto place = lower_mut_place(place_node);   // the LHS is a mutable-use position
    in_place_write_lhs_ = saved_place_write;
    TypeRef pt = expr_type(place);
    // Indexing through a raw-pointer place (e.g. a `*mut T` field `s.buf[i]`)
    // is an implicit deref: `*const` cannot be written and a `*mut` write
    // requires unsafe (matches the retired field_index_write diagnostics).
    {
        auto pr = expr_ref_of(place);
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

    if (pt && val)
        expect_type(val, pt, CoercePos::PlaceWrite,
                    std::format("assignment to '{}': type mismatch —",
                                render_place_node(place_node)));
    // B68 AT THE FIELD DOOR. `lower_place_assign` type-checks with expect_type,
    // which is lifetime-ERASED (TypeUID), and never called check_variance — so
    // `o.p = s` compared the two regions not at all. Its sibling
    // `lower_deref_assign` has done exactly this since B68, with the comment
    // "variance check at *ptr = val". Same two regions, one spelling checked.
    //
    // PRICED, RE-PRICED AND LANDED: 56 fires over the 373-row ledger, CEILING
    // 2, COST 0 over the 807-program legal corpus. PREDICTED TWO ROWS BY NAME
    // before the edit and the diff is empty both ways —
    // apit-not-targeted-by-lifetime-suggestion--d-min and
    // ex3-both-anon-regions-both-are-structs-3, both `x.a = x.b` across two
    // INDEPENDENT anonymous regions with no outlives bound between them.
    //
    // ⚠ COST 0 IS NOT A SAFETY CLAIM, so the refusal was attacked by hand in
    // the ABUSE direction: EIGHTEEN legal programs, each proven to FIRE, all
    // still admitted — same-lifetime, `where 'b: 'a`, a `&'static` source, a
    // scalar field, a local holder, a tuple field, a nested `o.i.q`, a
    // struct-typed RHS, a lifetime-free struct, an INVARIANT `&'a mut` field, a
    // fn-pointer field (contravariance), a generic `T` field, an enum-typed
    // field, an elided holder, a `'_` holder, a two-bound chain, a `self:
    // &mut Self` method and an array-element field. Two more in the DEFECT
    // direction go rc 0 -> rc 1: no bound at all, and the backwards
    // `where 'a: 'b`.
    //
    // ⚠ A REMAINING HOLE, NAMED BECAUSE IT IS ADJACENT AND NOT CLOSED HERE:
    // `fn set<'a>(o: &mut H, s: &'a i64) { o.p = s; }` — an ELIDED struct
    // lifetime against a NAMED source — still admits, while the fully-elided
    // `fn f(s: &mut Sink, v: &i32)` of the row above refuses. The elided
    // holder's region is not instantiated to something this check can compare.
    //
    // ⚠ AND A SEPARATE FALSE REFUSAL STILL BLOCKS THE OBVIOUS CALLER, recorded
    // here rather than lost: `let mut h: H<'_> = H{p:&v}; set(&mut h, &v)` is
    // refused TODAY at the CALL, not at this site — "call to 'set' arg 1:
    // variance mismatch — expected &mut H<'a>, got &mut H<'_>". That is
    // check_call_outlives failing to instantiate an elided struct-lifetime
    // argument, it is a different defect, and it is why every counter-example
    // above had to be written as an uncalled fn.
    //
    // (prior pricing, kept for the decay record)
    // MEASURED 2026-08-27: 59 fires, CEILING 2 vs COST 0
    // (apit-not-targeted-by-lifetime-suggestion--d-min,
    // ex3-both-anon-regions-both-are-structs-3). The pre-stated ceiling was 6
    // (upper bound 12) from a regex over the `x.f = y` shape: ACTUAL 2. A
    // fixture CONTAINING the shape is not a fixture whose DEFECT is the shape
    // — the regex over-counted by 3x. Rows are disjoint from every other
    // probe. Priced separately from lifereg_varassign on purpose (the
    // droporder lesson) and the split paid: that twin scored 0.
    // RE-PRICED 2026-08-28 (rule 8: the 2026-08-27 number was taken against a
    // 423-row ledger AND the OLD 487-test cost corpus). 58 fires, CEILING 2,
    // COST 0 — the SAME two rows, diffed both ways, and the zero HELD across
    // the corpus widening 487 -> 807 that took ltundecl's cost from 4 to 65
    // and lifereg_unmentioned's from 2 to 5. So this zero now means something
    // it did not mean on 08-27.
    // ⚠ AND IT WAS STILL ATTACKED BY HAND (rule 5), five programs, each proven
    // to fire exactly once — `fn set<'a>(out:&mut H<'a>, src:&'a i64)`, the
    // `where 'b: 'a` form, a `&'static i64` source, and the fully-elided form
    // all still ADMIT; the one shape Rust refuses,
    // `fn set<'a,'b>(out:&mut H<'a>, src:&'b i64)` with NO bound, goes
    // rc 0 -> rc 1. Five in the abuse direction, one in the defect direction.
    // ⚠ A SEPARATE FALSE REFUSAL BLOCKS THE OBVIOUS COUNTER-EXAMPLE and is
    // recorded here rather than lost: `let mut h: H<'_> = H{p:&v};
    // set(&mut h, &v)` is refused TODAY at the CALL, not at this site —
    // "call to 'set' arg 1: variance mismatch — expected &mut H<'a>, got
    // &mut H<'_>". That is check_call_outlives failing to instantiate an
    // elided struct-lifetime argument, and it is why every counter-example
    // above had to be written as an uncalled fn.
    // A place rooted at a `static mut` lies inside the static's declared type,
    // whose elided regions are 'static. PROBES.md 2026-09-13d-staticdemand.
    bool place_in_static_mut = false;
    bool place_in_inferred_local = false;
    for (auto cur = place_node; !cur.is_null();) {
        const int32_t cc = code_of(cur);
        if ((cc == la::FIELD_READ || cc == la::TUPLE_INDEX || cc == la::INDEX_READ) &&
            cur.has_key(la::RECEIVER)) {
            cur = unwrap_paren_node(map_of(cur.get(la::RECEIVER.code)));
            continue;
        }
        if (cc == la::DEREF && cur.has_key(la::VALUE)) {
            cur = unwrap_paren_node(map_of(cur.get(la::VALUE.code)));
            continue;
        }
        if (cc == la::VAR_REF) {
            std::string rn(str_of(cur.get(la::NAME.code)));
            place_in_static_mut = names_static_mut(rn);
            if (auto* vi = lookup_var_info(rn)) place_in_inferred_local = vi->regions_inferred;
        }
        break;
    }
    if (pt && val && !place_in_inferred_local)
        check_variance(expr_type(val),
                       place_in_static_mut ? static_item_regions_(pt) : pt,
                       std::format("assignment to '{}'",
                                   render_place_node(place_node)),
                       /*permissive=*/false);
    // Overflow: an int literal RHS must fit the place's integer type (closes the
    // gap where the general place-write path skipped the fit-check).
    if (pt && TypeRef(pt).kind() != LogosType::Kind::Error &&
        val && TypeRef(expr_type(val)).kind() == LogosType::Kind::IntLit)
        if (auto v = get_intlit_value(val))
            if (!intlit_fits(*v, TypeRef(pt).kind()))
                error(std::format("assignment to '{}': value {} does not fit in {}",
                      render_place_node(place_node), *v, type_str(pt)));
    // Array-literal RHS: each int-literal element must fit the place's narrow
    // array element type (generalizes the retired deref_field_write check).
    if (pt && TypeRef(pt).kind() == LogosType::Kind::Array && TypeRef(pt).elem() &&
        val && TypeRef(expr_type(val)).kind() == LogosType::Kind::Array) {
        auto vr = expr_ref_of(val);
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
        val && TypeRef(expr_type(val)).kind() == LogosType::Kind::Tuple) {
        auto vr = expr_ref_of(val);
        if (vr.kind() == lir_schema::expr::Code::TupleLit) {
            lir_view::ETupleLitView tl{vr};
            auto elems = TypeRef(pt).tuple_elems();
            for (uint64_t i = 0; i < tl.count() && i < elems.size(); ++i) {
                auto el = tl.elem(i);
                TypeRef et = elems[i];
                if (!et) continue;
                if (el.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit) {
                    if (auto v = get_intlit_value(el))
                        if (!intlit_fits(*v, TypeRef(et).kind()))
                            error(std::format("assignment to '{}': tuple element {}: value {} does not fit in {}",
                                  render_place_node(place_node), i, *v, type_str(et)));
                }
                // Nested: a tuple element that is itself an ARRAY literal.
                else if (TypeRef(et).kind() == LogosType::Kind::Array && TypeRef(et).elem() &&
                         el.kind() == lir_schema::expr::Code::ArrLit) {
                    lir_view::EArrLitView ial{el};
                    for (uint64_t ii = 0; ii < ial.count(); ++ii) {
                        auto iel = ial.elem(ii);
                        if (iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(iel))
                                if (!intlit_fits(*v, TypeRef(TypeRef(et).elem()).kind()))
                                    error(std::format("assignment to '{}': tuple element {}: array element {}: value {} does not fit in {}",
                                          render_place_node(place_node), i, ii, *v, type_str(TypeRef(et).elem())));
                    }
                }
                // Nested: a tuple element that is itself a TUPLE literal.
                else if (TypeRef(et).kind() == LogosType::Kind::Tuple &&
                         el.kind() == lir_schema::expr::Code::TupleLit) {
                    lir_view::ETupleLitView itl{el};
                    auto subelems = TypeRef(et).tuple_elems();
                    for (uint64_t ii = 0; ii < itl.count() && ii < subelems.size(); ++ii) {
                        auto iel = itl.elem(ii);
                        if (subelems[ii] &&
                            iel.type(cur_prog_->type_pool.impl()).kind() == LogosType::Kind::IntLit)
                            if (auto v = get_intlit_value(iel))
                                if (!intlit_fits(*v, TypeRef(subelems[ii]).kind()))
                                    error(std::format("assignment to '{}': tuple element {}: sub-element {}: value {} does not fit in {}",
                                          render_place_node(place_node), i, ii, *v, type_str(subelems[ii])));
                    }
                }
            }
        }
    }
    widen_int_expr(val, pt, builder());

    // T1.5: the place's old value drops before the store iff it is a live
    // owned field (field_old_live) AND its type is droppable.
    bool drop_old_place = field_old_live && pt &&
        (TypeRef(pt).owning_trait_object() ||
         !drop_fn_for(pt).empty() ||
         has_droppable_fields(pt));
    // Address of the place: `&mut <place>`. EAddrOfTemp recognises the place
    // read-expr kind and returns the real element GEP (not a temp copy).
    auto addr = builder().addr_of_temp(std::move(place), /*is_mut=*/true,
                                       make_ref(true, pt ? pt : error_t()), BorrowOrigin::Desugar);
    track_write_move(val);
    return builder().stmt_deref_write(std::move(addr), std::move(val), node_line_,
                                      drop_old_place);
}

void SemaChecker::check_match_exhaustiveness(const std::vector<lir_view::PatRef>& unguarded,
                                             TypeRef scrut_type, bool ast_proven_exhaustive) {
    // K4: a desugared nested-enum match is exhaustive at the AST level but its
    // arms carry synth guards (skipped below), so suppress the variant check.
    if (ast_proven_exhaustive) return;
    // logos-core 4.2: uninhabited scrutinee — `Never` or an empty enum — is
    // trivially exhaustive (no value to match against). Rust accepts a bare
    // `match x { }` here; we do too. Pairs with the Phase 1 Never tighten
    // work so a diverging tail (`loop {}`, `return`, panic) reachable through
    // an empty match arms's body is correctly typed.
    if (scrut_type && TypeRef(scrut_type).kind() == LogosType::Kind::Never) return;
    if (scrut_type && TypeRef(scrut_type).kind() == LogosType::Kind::Enum) {
        auto [epkg_e, esi_e] = enum_of(TypeRef(scrut_type));
        if (esi_e && esi_e->variants.empty()) return;
    }
    bool has_wild = false;
    for (auto pr : unguarded)
        if (pr.kind() == lir_schema::pat::Code::Wild) { has_wild = true; break; }
    if (TypeRef(scrut_type).kind() == LogosType::Kind::Enum) {
        auto [epkg_match, esi_match] = enum_of(TypeRef(scrut_type));
        auto eit = esi_match ? enums_.find(type_id(epkg_match, TypeRef(scrut_type).enum_name())) : enums_.end();
        if (eit == enums_.end()) eit = enums_.find(type_id({}, TypeRef(scrut_type).enum_name()));
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
            for (auto apr : unguarded) {
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
                        // T2-29: a variant with an UNINHABITED payload can
                        // never be constructed (`Result<i32, Void>` with an
                        // empty `Void`), so omitting its arm is exhaustive.
                        // Variant payload types are the enum DEFINITION's
                        // (generic `E`); substitute the scrutinee's type-args
                        // before the uninhabited check.
                        SemaSubst evsub;
                        {
                            auto ta = TypeRef(scrut_type).type_args();
                            for (size_t pi = 0; pi < eit->second.type_params.size()
                                                && pi < ta.size(); ++pi)
                                evsub[eit->second.type_params[pi].name] = ta[pi];
                        }
                        bool unconstructable = false;
                        for (auto pt : v.payload_types) {
                            TypeRef spt = evsub.empty() ? TypeRef(pt)
                                        : subst_type_sema(pt, evsub);
                            if (is_type_uninhabited(spt)) { unconstructable = true; break; }
                        }
                        if (unconstructable) continue;
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
        for (auto apr : unguarded) {
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

bool SemaChecker::pattern_moves_out(lir_view::PatRef pr, TypeRef ty) {
    namespace ps = lir_schema::pat;
    if (!pr) return false;
    const auto* pool = cur_prog_->type_pool.impl();
    auto named = [](std::string_view n) { return !n.empty() && n != "_"; };
    switch (pr.kind()) {
        case ps::Code::Wild:
            return named(lir_view::PatWildView{pr}.name()) && ty && is_move_type(ty);
        case ps::Code::VariantData: {
            lir_view::PatVariantDataView v{pr};
            std::vector<std::string> ns; std::vector<TypeRef> tys;
            v.each_binding([&](std::string_view n){ ns.emplace_back(n); });
            v.each_binding_type(pool, [&](TypeRef t){ tys.push_back(t); });
            auto ms = v.bind_ref_modes();   // absent = all by value (measured, see the E0507 arm)
            const auto subs = v.subs();     // a carried payload sub-pattern (ADR 0030 S3)
            for (size_t i = 0; i < ns.size(); ++i) {
                uint32_t m = i < ms.size() ? ms[i] : 0u;
                TypeRef bt = i < tys.size() ? tys[i] : TypeRef(nullptr);
                if (i < subs.size() && subs[i]) {
                    if (pattern_moves_out(subs[i], bt)) return true;
                    continue;
                }
                if (m == 0 && named(ns[i]) && bt && is_move_type(bt)) return true;
            }
            return false;
        }
        case ps::Code::Tuple: {
            lir_view::PatTupleView v{pr};
            std::vector<TypeRef> elems;
            if (ty && TypeRef(ty).kind() == LogosType::Kind::Tuple) elems = TypeRef(ty).tuple_elems();
            bool any = false; size_t i = 0;
            v.each_sub([&](lir_view::PatRef sp) {
                TypeRef et = i < elems.size() ? elems[i] : TypeRef(nullptr);
                if (!any && pattern_moves_out(sp, et)) any = true;
                ++i;
            });
            return any;
        }
        case ps::Code::Struct: {
            lir_view::PatStructView v{pr};
            bool any = false;
            v.each_field([&](lir_view::PatFieldBindingView f) {
                if (any) return;
                TypeRef ft = ty ? field_type_of_for_type(ty, f.field_name()) : TypeRef(nullptr);
                auto sub = f.sub();
                if (!sub) { any = ft && is_move_type(ft); return; }   // shorthand `{ f }` = by value
                any = pattern_moves_out(sub, ft);
            });
            return any;
        }
        case ps::Code::Slice:
            // An array / slice pattern moves ELEMENTS, and those are partial
            // moves the slice-pattern lowering and the borrow checker already
            // record per element (`a.2`): `[.., z]` then `[w, ..]` is legal.
            // A whole-array mark here would refuse it and coarsen the sentence
            // (measured: 1 pass + 4 fail fixtures).
            // ⚠ AND A WHOLE-ARRAY MARK IS NOT MERELY COARSE, IT LEAKS. Measured 2026-09-16c over
            // one binary carrying both arms: the whole-array mark suppresses the drop of every
            // element the pattern does NOT bind — `[_, y]` over `[D; 2]` destroyed only y (n=2
            // where rustc gives 21), and nine legal programs leaked that way. The move a match arm
            // makes is per INDEX, and mark_match_scrutinee_moved records it per index.
            return false;
        case ps::Code::At: {
            lir_view::PatAtView v{pr};
            if (!v.ref_mode() && named(v.name()) && ty && is_move_type(ty)) return true;   // the whole value, by value
            return pattern_moves_out(v.sub(), ty);
        }
        case ps::Code::Or: {
            bool any = false;
            lir_view::PatOrView{pr}.each_alt([&](lir_view::PatRef a){
                if (!any && pattern_moves_out(a, ty)) any = true; });
            return any;
        }
        default:
            // RefBind / RefPat bind through a reference; Variant / Int / Bool /
            // Range bind nothing.
            return false;
    }
}

bool SemaChecker::arm_may_match_variant(lir_view::PatRef p, int64_t disc) {
    namespace ps = lir_schema::pat;
    if (!p) return true;
    if (p.kind() == ps::Code::VariantData) return lir_view::PatVariantDataView{p}.disc() == disc;
    if (p.kind() == ps::Code::Variant) return lir_view::PatVariantView{p}.disc() == disc;
    return true;
}

void SemaChecker::mark_match_scrutinee_moved(const lir::LExprPtr& scrut,
                                              TypeRef scrut_type,
                                              lir_view::PatRef pat, bool variant_exact) {
    namespace ec = lir_schema::expr;
    // The scrutinee may be a plain VAR (`match o`) or a PLACE — a struct field
    // (`match s.o`) / tuple element (`match a.1`) / an element behind an index
    // or a deref. Enum value-repr makes the payload INLINE in the parent's
    // storage, so moving a payload out of a place scrutinee must mark THAT
    // place moved (mark_moved_expr records `s.o`/`a.1` in moved_fields, and
    // REFUSES an array element the way every other move spelling does) — else
    // the parent's scope-exit Drop double-frees the moved-out payload. A bare
    // VarRef marks the var. A temporary has no owner to mark (lower_match hoists
    // it into a synth local first, so it arrives here as a VarRef).
    namespace ps = lir_schema::pat;
    // …and a TUPLE / STRUCT / `@` pattern at the top is the same fact: the
    // leaves it binds by value are moved, its `_` parts stay the owner's
    // (a whole-scrutinee mark leaked them: `let (d, _) = t`, a parameter
    // `(d, _): (D, D)`; and marked nothing under a nested array).
    const bool top_structural = pat &&
        (pat.kind() == ps::Code::Tuple || pat.kind() == ps::Code::Struct ||
         pat.kind() == ps::Code::At);
    // A leaf under a variant the arm alone reaches is moved on every path
    // through it (exact_variant_moves_); elsewhere a plain mark.
    bool leaf_exact = false;
    std::vector<std::string>* leaf_sink = nullptr;   // collect instead of marking
    auto mark_leaf = [&](const std::string& path) {
        if (leaf_sink) { leaf_sink->push_back(path); return; }
        mark_moved(path);
        if (leaf_exact) exact_variant_moves_.push_back(path);
    };
        // MARK EVERY MOVED LEAF, NOT ONLY A WHOLE ELEMENT (2026-09-16j-arrpath2).
        // A nested sub-pattern moves only PART of its element, so the path it owes is the
        // FULL dotted path of each leaf it binds by value — `arr.0.a` for a struct sub,
        // `arr.0.1` for a tuple sub, `arr.0.t.0` for a tuple inside a field, `arr.0.0` for
        // an array inside an array. `split_skip_paths` recurses on a dotted prefix, so a
        // deeper path suppresses exactly that leaf and still drops its siblings.
        // The previous predicate emitted a path ONLY for a plain named binder, so a nested
        // sub marked NOTHING and the array's scope-exit drop destroyed the moved-out leaf a
        // SECOND time (soundness_queue match_array_nested_destructure_elem_double_drop).
        // A whole-ELEMENT mark is not the alternative: it would leak the fields the sub-
        // pattern does not bind, which is the failure direction 2026-09-16c measured on nine
        // legal programs.
        std::function<void(lir_view::PatRef, const std::string&, TypeRef)> emit_moved_leaves =
            [&](lir_view::PatRef sp, const std::string& path, TypeRef pty) {
            if (!sp) return;
            // ⚠ THE PARAMETER IS SPELLED `nm` ON PURPOSE — it is a BARE-NAME INTERCEPT and the
            // key-identity census reads it. tests/logos/key_identity_lint.sh FACT 4 pins a
            // per-file count of bare entity-name comparisons, and its SCAN_LHS reaches the LHS
            // spellings `nm`/`name`/`cn`/… but not a one-letter `s`. Renaming this binder made a
            // LIVE site invisible to that census (count 23 -> 22) while the decision it makes was
            // unchanged — the exact "a pinned file's intercepts deleted" shape the lint's own
            // header records being bitten by. The pin is NOT moved to match a rename.
            auto is_named = [](std::string_view nm) { return !nm.empty() && nm != "_"; };
            switch (sp.kind()) {
                case ps::Code::Wild:
                    if (is_named(lir_view::PatWildView{sp}.name()) && pty && is_move_type(pty))
                        mark_leaf(path);
                    return;
                case ps::Code::VariantData: {
                    // A variant nested in a payload / an element: its by-value
                    // binders and its carried subs, at the per-tag paths
                    // `<path>.#<disc>.<i>` the enum drop glue skips.
                    lir_view::PatVariantDataView vd{sp};
                    std::vector<std::string> ns;
                    vd.each_binding([&](std::string_view b) { ns.emplace_back(b); });
                    std::vector<TypeRef> tys;
                    vd.each_binding_type(cur_prog_->type_pool.impl(), [&](TypeRef t) { tys.push_back(t); });
                    const auto modes = vd.bind_ref_modes();
                    const auto subs = vd.subs();
                    for (size_t i = 0; i < ns.size() && i < tys.size(); ++i) {
                        const std::string fp = path + ".#" + std::to_string(vd.disc()) + "." + std::to_string(i);
                        if (i < subs.size() && subs[i]) { emit_moved_leaves(subs[i], fp, tys[i]); continue; }
                        const bool by_value = i >= modes.size() || modes[i] == 0;
                        if (by_value && is_named(ns[i]) && tys[i] && is_move_type(tys[i])) mark_leaf(fp);
                    }
                    return;
                }
                case ps::Code::At: {
                    lir_view::PatAtView av{sp};
                    if (!av.ref_mode() && is_named(av.name()) && pty && is_move_type(pty)) {
                        mark_leaf(path);   // the whole value, by value
                        return;
                    }
                    emit_moved_leaves(av.sub(), path, pty);
                    return;
                }
                case ps::Code::Struct: {
                    lir_view::PatStructView psv{sp};
                    psv.each_field([&](lir_view::PatFieldBindingView f) {
                        TypeRef ft = pty ? field_type_of_for_type(pty, f.field_name())
                                         : TypeRef(nullptr);
                        std::string fp = path + "." + std::string(f.field_name());
                        auto fsub = f.sub();
                        if (!fsub) {            // shorthand `{ f }` binds by value
                            if (ft && is_move_type(ft)) mark_leaf(fp);
                            return;
                        }
                        emit_moved_leaves(fsub, fp, ft);
                    });
                    return;
                }
                case ps::Code::Tuple: {
                    lir_view::PatTupleView ptv{sp};
                    std::vector<TypeRef> elems;
                    if (pty && TypeRef(pty).kind() == LogosType::Kind::Tuple)
                        elems = TypeRef(pty).tuple_elems();
                    size_t ti = 0;
                    ptv.each_sub([&](lir_view::PatRef tsp) {
                        TypeRef tet = ti < elems.size() ? elems[ti] : TypeRef(nullptr);
                        emit_moved_leaves(tsp, path + "." + std::to_string(ti), tet);
                        ++ti;
                    });
                    return;
                }
                case ps::Code::Slice: {
                    // An element that is ITSELF an array: recurse per index, the twin of the
                    // outer loop below. Without this an array-in-array bound one element deep
                    // marks nothing and double-destroys the moved leaf.
                    if (!pty || TypeRef(pty).kind() != LogosType::Kind::Array) return;
                    lir_view::PatSliceView isv{sp};
                    TypeRef iet = TypeRef(pty).elem();
                    const uint64_t in  = TypeRef(pty).arr_size();
                    const uint64_t isc = isv.suffix_count();
                    uint64_t pi = 0;
                    isv.each_prefix([&](lir_view::PatRef isp) {
                        if (pi < in)
                            emit_moved_leaves(isp, path + "." + std::to_string(pi), iet);
                        ++pi; });
                    uint64_t si = 0;
                    isv.each_suffix([&](lir_view::PatRef isp) {
                        if (in >= isc)
                            emit_moved_leaves(isp, path + "." + std::to_string(in - isc + si),
                                              iet);
                        ++si; });
                    return;
                }
                default:
                    // RefBind / RefPat bind THROUGH a reference and move nothing; Variant /
                    // Int / Bool / Range bind nothing. A VariantData payload under an array
                    // element is the variant door's fact, not this one.
                    // Under a top-level tuple / struct / `@` there is no variant door: a
                    // sub-pattern that moves out (`(Some(r), _)`) owes its WHOLE element,
                    // the mark the whole-scrutinee rule made before this walk reached it.
                    if (top_structural && pty && is_move_type(pty) && pattern_moves_out(sp, pty))
                        mark_leaf(path);
                    return;
            }
        };
    // MARK THE ELEMENT, NOT THE ARRAY (2026-09-16c-armelem; see src/compiler/PROBES.md).
    // An owned `[T; N]` place matched by an array pattern moves out exactly the indices its
    // arm binds BY VALUE, and the per-index path is what lets the scope-exit drop skip those and
    // still destroy the siblings. A whole-array mark LEAKS every element the pattern does not bind
    // (measured: `[_, y]` over `[D; 2]`). Only a PLAIN named binder marks: a nested destructuring
    // sub-pattern may move only part of its element, and marking the whole element would leak the
    // rest. A named `rest` over the array by value is an array of the elements it
    // covers, and moves each out (a rest under a reference binds a sub-slice).
    {
        if (scrut && scrut_type && pat &&
            ((pat.kind() == ps::Code::Slice &&
              TypeRef(scrut_type).kind() == LogosType::Kind::Array) || top_structural) &&
            lir_view::is_place_expr(expr_ref_of(scrut))) {
            std::string base =
                expr_ref_of(scrut).kind() == ec::Code::VarRef
                    ? std::string(lir_view::EVarRefView{expr_ref_of(scrut)}.name())
                    : move_path_of(expr_ref_of(scrut));
            const bool is_arr = TypeRef(scrut_type).kind() == LogosType::Kind::Array;
            lir_view::PatSliceView sv{pat};
            TypeRef et = is_arr ? TypeRef(scrut_type).elem() : TypeRef(nullptr);
            const uint64_t n  = is_arr ? TypeRef(scrut_type).arr_size() : 0;
            const uint64_t sc = is_arr ? sv.suffix_count() : 0;
            // A place with no dotted path (an array element, a deref) is not the
            // leaf walk's: it falls through to the whole-place rule below, which
            // refuses a move out of an array element (E0508).
            if (top_structural && !base.empty()) {
                emit_moved_leaves(pat, base, scrut_type);
                return;
            }
            if (!base.empty() && n > 0) {
                logos::probe::census("armelem.slice.door");
                uint64_t i = 0;
                sv.each_prefix([&](lir_view::PatRef sp) {
                    if (i < n) emit_moved_leaves(sp, base + "." + std::to_string(i), et);
                    ++i; });
                uint64_t j = 0;
                sv.each_suffix([&](lir_view::PatRef sp) {
                    if (n >= sc)
                        emit_moved_leaves(sp, base + "." + std::to_string(n - sc + j), et);
                    ++j; });
                // A named rest over the array BY VALUE owns its elements (it is
                // an array `[T; n-i-sc]`, bind_pattern_ref): each moves out.
                if (auto rest = sv.rest())
                    for (uint64_t k = i; k + sc < n; ++k)
                        emit_moved_leaves(rest, base + "." + std::to_string(k), et);
                return;
            }
        }
    }
    if (!(scrut && scrut_type && is_move_type(scrut_type) &&
          lir_view::is_place_expr(expr_ref_of(scrut)) && pattern_moves_out(pat, scrut_type)))
        return;
    // A PAYLOAD FIELD, NOT THE WHOLE ENUM (squeue enum_payload_partial_move_leak).
    // `match e { E::V(a, _) => eat(a), E::W => {} }` moves ONE payload field; marking
    // all of `e` moved skipped its scope-exit drop and leaked the rest of the
    // payload. When the arm alone reaches the variant, the moved fields are marked
    // as `e.#<disc>.<i>` paths, which the enum drop glue skips under that tag.
    // Flat bindings only: a nested sub-pattern (a synthesized `__` binding) keeps
    // the whole mark.
    {
        namespace ps = lir_schema::pat;
        if (pat && pat.kind() == ps::Code::VariantData &&
            TypeRef(scrut_type).kind() == LogosType::Kind::Enum) {
            lir_view::PatVariantDataView vd{pat};
            std::vector<std::string> names;
            vd.each_binding([&](std::string_view n) { names.emplace_back(n); });
            std::vector<TypeRef> tys;
            vd.each_binding_type(cur_prog_->type_pool.impl(), [&](TypeRef t) { tys.push_back(t); });
            auto modes = vd.bind_ref_modes();
            const auto subs = vd.subs();   // carried payload sub-patterns: their leaves
            bool synth = false;
            for (auto& n : names) if (n.size() > 1 && n[0] == '_' && n[1] == '_') synth = true;
            if (!synth && tys.size() == names.size()) {
                std::string base = expr_ref_of(scrut).kind() == ec::Code::VarRef
                    ? std::string(lir_view::EVarRefView{expr_ref_of(scrut)}.name())
                    : move_path_of(expr_ref_of(scrut));
                if (!base.empty()) {
                    std::vector<std::string> leaves;
                    leaf_sink = &leaves;
                    for (size_t i = 0; i < names.size(); ++i) {
                        const std::string fp = base + ".#" + std::to_string(vd.disc()) + "." + std::to_string(i);
                        if (i < subs.size() && subs[i]) { emit_moved_leaves(subs[i], fp, tys[i]); continue; }
                        bool by_value = i >= modes.size() || modes[i] == 0;
                        if (names[i].empty() || names[i] == "_" || !by_value) continue;
                        if (!tys[i] || !is_move_type(tys[i])) continue;
                        leaves.push_back(fp);
                    }
                    leaf_sink = nullptr;
                    // Exact: static over every arm. Otherwise each path is moved on
                    // this arm only and the branch merge gives it a drop flag (one
                    // flag for all of this arm's leaves), which emit_frame_drops
                    // turns into guarded whole drops.
                    for (auto& path : leaves) {
                        mark_moved(path);
                        if (variant_exact) exact_variant_moves_.push_back(path);
                    }
                    return;
                }
            }
        }
    }
    if (expr_ref_of(scrut).kind() == ec::Code::VarRef)
        mark_moved(std::string(lir_view::EVarRefView{expr_ref_of(scrut)}.name()));
    else
        mark_moved_expr(expr_ref_of(scrut));
}

void SemaChecker::emit_nested_variant_lets(
        const std::string& synth_name, TypeRef synth_t,
        writ::TinyMapView sub_pat, std::vector<lir_view::StmtRef>& out) {
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
            auto _vd_slots = v.bind_slots();  // Phase-1: reuse reserved slots
            auto _vd_muts  = v.bind_byval_muts();  // the carried by-value `mut`
            for (size_t i = 0; i < names.size() && i < types.size(); ++i)
                if (names[i] != "_") define(std::string(names[i]), types[i],
                                            i < _vd_muts.size() && _vd_muts[i] != 0u,
                                            i < _vd_slots.size() ? _vd_slots[i] : 0xFFFFFFFFu);
        } else if (k == ps::Code::Tuple) {
            lir_view::PatTupleView v{pr};
            std::vector<std::string_view> names; std::vector<TypeRef> types;
            v.each_binding([&](std::string_view n){ names.push_back(n); });
            v.each_binding_type(pool, [&](TypeRef t){ types.push_back(t); });
            auto _tp_slots = v.bind_slots();  // Phase-1: reuse reserved slots
            for (size_t i = 0; i < names.size() && i < types.size(); ++i)
                if (names[i] != "_") define(std::string(names[i]), types[i], pat_mut_name(names[i]),
                                            i < _tp_slots.size() ? _tp_slots[i] : 0xFFFFFFFFu);
        } else if (k == ps::Code::Wild) {
            lir_view::PatWildView wv{pr};
            auto n = wv.name();
            if (n != "_") define(std::string(n), synth_t, wv.is_mut(), wv.bind_slot());  // Phase-1
        }
    };
    // A tuple / struct sub-pattern (routed here when refutable) introduces
    // names at any depth: the general binder walks them in element order.
    if (auto pk = pat_ref_of(lpat).kind(); pk == ps::Code::Tuple || pk == ps::Code::Struct)
        bind_pattern_ref(pat_ref_of(lpat), synth_t);
    else
        define_binds(pat_ref_of(lpat));
    // Emit the let-else. Its bindings OWN what they take by value: the synth
    // is marked moved so the arm's end does not drop it a second time.
    lir::SLetElse sle;
    sle.scrut = builder().var_ref(synth_name, synth_t);
    mark_match_scrutinee_moved(sle.scrut, synth_t, pat_ref_of(lpat));
    sle.pat   = std::move(lpat);
    std::vector<lir_view::StmtRef> eblk;
    lir::SLoop lp; lp.body = lir_mirror_block(*cur_prog_, {});
    eblk.push_back(make_stmt_emit(node_line_, std::move(lp)));
    sle.else_block = lir_mirror_block(*cur_prog_, eblk);
    // No guards: the owning arm's guard already proved the FULL nested match,
    // so this let-else is a pure extraction (its own variant-tag check + the
    // dead else suffice). Re-checking via `le_guards` would also spuriously
    // re-bind inner names. Deeper bindings are extracted by the recursion below.
    (void)le_guards;
    out.push_back(make_stmt_emit(node_line_, std::move(sle)));
    // Deeper nesting (`Some(Some(Some(w)))`): the inner let-else reads a
    // binding bound by THIS one, so it must come after.
    for (auto& d : deeper) {
        const int32_t dc = code_of(d.sub_pat_node);
        if (dc != la::PAT_VARIANT_DATA &&
            !((dc == la::PAT_TUPLE || dc == la::PAT_STRUCT) && !ast_pat_irrefutable(d.sub_pat_node)))
            continue;
        TypeRef dt = lookup(d.synth_name);
        if (!dt) continue;
        emit_nested_variant_lets(d.synth_name, dt, d.sub_pat_node, out);
    }
}

// ADR 0030 S3 (C-PAT): the one exhaustiveness verdict of a `match` (statement
// or expression): the usefulness matrix over the unguarded arms. A decided
// miss is E0004 — naming the missing variants / bool values when the miss is
// at the top, else the generic sentence. `decided` = the matrix answered.
bool SemaChecker::check_exhaustive_(std::vector<writ::TinyMapView> pats, TypeRef scrut_type,
                                    bool& decided) {
    std::vector<std::string> missing;
    const bool exh = ast_patterns_exhaustive(std::move(pats), scrut_type, &decided, &missing);
    if (exh || !decided) return exh;
    TypeRef t = scrut_type;
    while (t && (TypeRef(t).kind() == LogosType::Kind::Ref ||
                 TypeRef(t).kind() == LogosType::Kind::MutRef) && TypeRef(t).pointee())
        t = TypeRef(t).pointee();
    const bool named = !missing.empty() && !missing[0].empty();
    if (named && t && TypeRef(t).kind() == LogosType::Kind::Bool)
        error("match on bool is not exhaustive — missing " + missing[0]);
    else if (named) {
        std::string list;
        for (auto& m : missing) list += (list.empty() ? "" : ", ") + m;
        error(std::format("match is not exhaustive — missing variant(s): {}", list));
    } else
        error(std::format("match is not exhaustive (E0004): the arms do not cover every value of `{}`",
                          t ? type_str(t) : std::string("?")));
    return false;
}

bool SemaChecker::ast_patterns_exhaustive(
        std::vector<writ::TinyMapView> pats, TypeRef ty, bool* decided,
        std::vector<std::string>* top_missing) {
    using K = LogosType::Kind;
    if (decided) *decided = false;
    const TypeRef unpeeled = ty;
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
        if (code_of(p) == la::PAT_WILD && ast_pat_irrefutable(p)) return true;
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
    // Usefulness over a pattern MATRIX (rows × columns, one type per column):
    // the rows are exhaustive iff no value vector escapes them. A null node is
    // a wildcard. Tuples and structs expand into their parts, an enum splits by
    // variant, bool by value; any other type is covered by wildcard rows only
    // (an integer/char column of literals proves nothing — a `false` here means
    // "not proven", and the LIR-level variant check still runs).
    using Row = std::vector<TinyMapView>;
    auto peel = [&](TypeRef x) {
        for (int i = 0; i < 8 && x &&
             (TypeRef(x).kind() == K::Ref || TypeRef(x).kind() == K::MutRef ||
              TypeRef(x).kind() == K::Ptr) && TypeRef(x).pointee(); ++i)
            x = TypeRef(x).pointee();
        return x;
    };
    // Unwrap `@`, `&pat` and single-alt or; a binder / `_` / `..` is a wildcard
    // UNLESS the name is a payload-less variant or a const (then it is a test).
    std::function<TinyMapView(TinyMapView)> norm = [&](TinyMapView p) -> TinyMapView {
        if (p.is_null()) return p;
        int32_t c = code_of(p);
        if ((c == la::PAT_AT || c == la::PAT_REF) && p.has_key(la::VALUE))
            return norm(map_of(p.get(la::VALUE.code)));
        if (c == la::PAT_AT || c == la::PAT_REST) return TinyMapView{};
        if (c == la::PAT_OR && p.has_key(la::ITEMS)) {
            auto a = arr_of(p.get(la::ITEMS.code));
            if (a.size() == 1) return norm(map_of(a.get(0)));
        }
        if (c == la::PAT_WILD && ast_pat_irrefutable(p)) return TinyMapView{};
        return p;
    };
    auto list_items = [&](TinyMapView n, uint8_t key) -> std::vector<TinyMapView> {
        std::vector<TinyMapView> out;
        if (!n.has_key(key)) return out;
        auto av = n.get(key);
        if (av.is_null()) return out;
        auto m = av.is_pointer() ? map_of(av) : TinyMapView{};
        ArrayView items = (!m.is_null() && m.has_key(la::ITEMS)) ? arr_of(m.get(la::ITEMS.code)) : arr_of(av);
        for (uint64_t i = 0; i < items.size(); ++i) out.push_back(map_of(items.get(i)));
        return out;
    };
    // Positional parts of a tuple-like pattern, `..` expanded to wildcards.
    auto positional = [&](const std::vector<TinyMapView>& items, size_t arity,
                          std::vector<TinyMapView>& out) -> bool {
        size_t rest = items.size();
        for (size_t i = 0; i < items.size(); ++i)
            if (!items[i].is_null() && code_of(items[i]) == la::PAT_REST &&
                !items[i].has_key(la::NAME)) { rest = i; break; }
        if (rest == items.size()) {
            if (items.size() != arity) return false;
            out = items;
            return true;
        }
        size_t tail = items.size() - rest - 1;
        if (rest + tail > arity) return false;
        out.assign(items.begin(), items.begin() + rest);
        for (size_t k = 0; k < arity - rest - tail; ++k) out.push_back(TinyMapView{});
        out.insert(out.end(), items.begin() + rest + 1, items.end());
        return true;
    };
    // Fields of a struct-shaped pattern, in declaration order; absent = wild.
    auto by_field = [&](const std::vector<TinyMapView>& items,
                        const std::vector<std::string>& names) -> std::vector<TinyMapView> {
        std::vector<TinyMapView> out(names.size());
        for (auto f : items) {
            if (f.is_null() || code_of(f) != la::PAT_FIELD || !f.has_key(la::NAME)) continue;
            auto fname = str_of(f.get(la::NAME.code));
            for (size_t k = 0; k < names.size(); ++k)
                if (names[k] == fname)
                    out[k] = f.has_key(la::VALUE) ? map_of(f.get(la::VALUE.code)) : TinyMapView{};
        }
        return out;
    };
    int budget = 20000;   // rows × splits; past it, "not proven"
    bool undecidable = false;
    int calls = 0;
    // The integer domain of a column type ([lo, hi]; nullopt = not a bounded
    // integer: 128-bit integers are treated as unbounded).
    auto int_domain = [&](TypeRef x) -> std::optional<std::pair<__int128, __int128>> {
        switch (TypeRef(x).kind()) {
        case K::I8:  return std::pair<__int128, __int128>{INT8_MIN, INT8_MAX};
        case K::I16: return std::pair<__int128, __int128>{INT16_MIN, INT16_MAX};
        case K::I24: return std::pair<__int128, __int128>{-(1 << 23), (1 << 23) - 1};
        case K::I32: return std::pair<__int128, __int128>{INT32_MIN, INT32_MAX};
        case K::I56: return std::pair<__int128, __int128>{-((__int128)1 << 55), ((__int128)1 << 55) - 1};
        case K::I64: case K::Isize: return std::pair<__int128, __int128>{INT64_MIN, INT64_MAX};
        case K::U8:  return std::pair<__int128, __int128>{0, UINT8_MAX};
        case K::U16: return std::pair<__int128, __int128>{0, UINT16_MAX};
        case K::U24: return std::pair<__int128, __int128>{0, (1 << 24) - 1};
        case K::U32: return std::pair<__int128, __int128>{0, UINT32_MAX};
        case K::U56: return std::pair<__int128, __int128>{0, ((__int128)1 << 56) - 1};
        case K::U64: case K::Usize: return std::pair<__int128, __int128>{0, (__int128)UINT64_MAX};
        case K::Char: return std::pair<__int128, __int128>{0, 0x10FFFF};
        default: return std::nullopt;
        }
    };
    // A literal / range / char pattern as an interval of the column's domain.
    auto pat_interval = [&](TinyMapView p, TypeRef x, __int128 dlo, __int128 dhi)
            -> std::optional<std::pair<__int128, __int128>> {
        // The magnitude as parsed (its 64 bits, unsigned), the sign applied
        // after: `-9223372036854775808` is i64::MIN, not an overflow.
        auto num = [&](uint8_t key, bool neg) -> __int128 {
            const __int128 w = (__int128)(uint64_t)parse_int_literal(str_of(p.get(key)));
            return neg ? -w : w;
        };
        const int32_t c = code_of(p);
        if (c == la::PAT_INT)     { __int128 v = num(la::VALUE.code, false); return std::pair{v, v}; }
        if (c == la::PAT_NEG_INT) { __int128 v = num(la::VALUE.code, true);  return std::pair{v, v}; }
        if (c == la::PAT_CHAR) {
            __int128 v = decode_char_lit_(str_of(p.get(la::VALUE.code)));
            return std::pair{v, v};
        }
        if (c == la::PAT_CHAR_RANGE)
            return std::pair<__int128, __int128>{decode_char_lit_(str_of(p.get(la::LHS.code))),
                                                 decode_char_lit_(str_of(p.get(la::RHS.code)))};
        if (c == la::PAT_RANGE) {
            __int128 lo = p.has_key(la::LHS) ? num(la::LHS.code, p.has_key(la::LO_NEG)) : dlo;
            __int128 hi = p.has_key(la::RHS) ? num(la::RHS.code, p.has_key(la::HI_NEG)) : dhi;
            const bool incl = p.has_key(la::INCLUSIVE) && p.get(la::INCLUSIVE.code).is_value() &&
                              p.get(la::INCLUSIVE.code).as_value<int32_t>() != 0;
            if (!incl && p.has_key(la::RHS)) hi -= 1;
            return std::pair{lo, hi};
        }
        return std::nullopt;
    };
    std::function<bool(std::vector<Row>, std::vector<TypeRef>)> exh =
        [&](std::vector<Row> rows, std::vector<TypeRef> tys) -> bool {
        if (--budget < 0) { undecidable = true; return false; }
        const bool is_top = calls++ == 0;
        if (tys.empty()) return !rows.empty();
        // No rows: exhaustive only over an uninhabited type — asked of the
        // type AS WRITTEN (`&Empty` is inhabited: rustc refuses `match r {}`).
        if (rows.empty()) {
            TypeRef w = is_top ? unpeeled : tys[0];
            return w && (TypeRef(w).kind() == K::Never || is_type_uninhabited(w));
        }
        // Expand multi-alt ors in column 0 and normalise it.
        std::vector<Row> rs;
        std::function<void(Row&, TinyMapView)> push_alts = [&](Row& r, TinyMapView p) {
            p = norm(p);
            if (!p.is_null() && code_of(p) == la::PAT_OR && p.has_key(la::ITEMS)) {
                auto a = arr_of(p.get(la::ITEMS.code));
                for (uint64_t k = 0; k < a.size(); ++k) push_alts(r, map_of(a.get(k)));
                return;
            }
            Row nr = r; nr[0] = p; rs.push_back(std::move(nr));
        };
        for (auto& r : rows) push_alts(r, r[0]);
        TypeRef t0 = peel(tys[0]);
        std::vector<TypeRef> rest_tys(tys.begin() + 1, tys.end());
        auto drop_first = [&](const Row& r) { return Row(r.begin() + 1, r.end()); };
        bool all_wild = true;
        for (auto& r : rs) if (!r[0].is_null()) { all_wild = false; break; }
        if (all_wild || !t0) {
            std::vector<Row> nr;
            for (auto& r : rs) if (r[0].is_null()) nr.push_back(drop_first(r));
            return exh(std::move(nr), rest_tys);
        }
        // Specialise by one constructor: `parts(row0)` gives the sub-patterns
        // (nullopt = the row does not match this constructor).
        auto specialise = [&](const std::vector<TypeRef>& sub_tys,
                              const std::function<std::optional<Row>(TinyMapView)>& parts) -> bool {
            std::vector<Row> nr;
            std::vector<TypeRef> nt = sub_tys;
            nt.insert(nt.end(), rest_tys.begin(), rest_tys.end());
            for (auto& r : rs) {
                std::optional<Row> ps;
                if (r[0].is_null()) ps = Row(sub_tys.size());
                else ps = parts(r[0]);
                if (!ps) continue;
                Row x = *ps;
                auto tl = drop_first(r);
                x.insert(x.end(), tl.begin(), tl.end());
                nr.push_back(std::move(x));
            }
            return exh(std::move(nr), nt);
        };
        auto k0 = TypeRef(t0).kind();
        if (k0 == K::Tuple) {
            auto elems = TypeRef(t0).tuple_elems();
            return specialise(elems, [&](TinyMapView p) -> std::optional<Row> {
                if (code_of(p) != la::PAT_TUPLE) { undecidable = true; return std::nullopt; }
                auto items = list_items(p, la::NAMES.code);
                if (items.empty()) items = list_items(p, la::ITEMS.code);
                Row out;
                if (!positional(items, elems.size(), out)) return std::nullopt;
                return out;
            });
        }
        if (k0 == K::Struct) {
            const SemaStructInfo* si = find_struct_by_name(std::string(TypeRef(t0).struct_name())).second;
            if (!si) { undecidable = true; return false; }
            std::vector<std::string> names; std::vector<TypeRef> ftys;
            for (auto& f : si->fields) { names.emplace_back(f.name); ftys.push_back(f.type); }
            return specialise(ftys, [&](TinyMapView p) -> std::optional<Row> {
                if (code_of(p) == la::PAT_STRUCT)
                    return by_field(list_items(p, la::ITEMS.code), names);
                // A tuple struct's `Triple(a, b, c)`: positional over the fields.
                if (code_of(p) == la::PAT_VARIANT_DATA) {
                    Row out;
                    if (positional(payload_items(p), ftys.size(), out)) return out;
                }
                undecidable = true;   // a shape this matrix does not model
                return std::nullopt;
            });
        }
        if (k0 == K::Bool) {
            for (int bv = 0; bv < 2; ++bv) {
                if (!specialise({}, [&](TinyMapView p) -> std::optional<Row> {
                        if (code_of(p) != la::PAT_BOOL || !p.has_key(la::VALUE)) { undecidable = true; return std::nullopt; }
                        if ((p.get(la::VALUE.code).as_value<int32_t>() != 0) != (bv != 0)) return std::nullopt;
                        return Row{};
                    })) {
                    if (!(is_top && top_missing)) return false;
                    top_missing->push_back(bv ? "true" : "false");
                }
            }
            return !(is_top && top_missing && !top_missing->empty());
        }
        if (k0 == K::Enum) {
            auto [pkg, esi] = enum_of(TypeRef(t0));
            (void)pkg;
            if (!esi) { undecidable = true; return false; }
            SemaSubst subst;
            auto ta = TypeRef(t0).type_args();
            for (size_t i = 0; i < esi->type_params.size() && i < ta.size(); ++i)
                if (ta[i]) subst[esi->type_params[i].name] = ta[i];
            for (auto& V : esi->variants) {
                std::vector<TypeRef> ptys;
                bool uninhabited = false;
                for (auto pt : V.payload_types) {
                    TypeRef x = (pt && !subst.empty()) ? subst_type_sema(pt, subst) : TypeRef(pt);
                    if (x && is_type_uninhabited(x)) uninhabited = true;
                    ptys.push_back(x);
                }
                if (uninhabited) continue;
                const bool struct_shape = !V.payload_field_names.empty();
                bool ok = specialise(ptys, [&](TinyMapView p) -> std::optional<Row> {
                    int32_t c = code_of(p);
                    if (c == la::PAT_WILD) {   // a bare unit-variant / const name
                        auto n = str_of(p.get(la::NAME.code));
                        if (n == V.name && ptys.empty()) return Row{};
                        if (const_pkg_of_.count(std::string(n))) undecidable = true;
                        return std::nullopt;
                    }
                    std::string en, vn;
                    if (!pat_variant(p, en, vn)) { undecidable = true; return std::nullopt; }
                    if (vn != V.name) return std::nullopt;
                    if (c == la::PAT_VARIANT) return Row(ptys.size());
                    if (struct_shape && !p.has_key(la::ARGS))
                        return by_field(list_items(p, la::ITEMS.code), V.payload_field_names);
                    auto args = payload_items(p);
                    if (args.empty()) return Row(ptys.size());
                    Row out;
                    if (!positional(args, ptys.size(), out)) return std::nullopt;
                    return out;
                });
                if (!ok) {
                    if (!(is_top && top_missing)) return false;
                    // Named by no arm at all: a missing VARIANT. Named, but a
                    // payload value escapes: a deeper miss (the generic E0004).
                    bool named = false;
                    for (auto& r : rs) {
                        if (r[0].is_null()) { named = true; break; }
                        std::string en, vn;
                        if ((code_of(r[0]) == la::PAT_WILD && str_of(r[0].get(la::NAME.code)) == V.name) ||
                            (pat_variant(r[0], en, vn) && vn == V.name)) { named = true; break; }
                    }
                    if (named) { top_missing->clear(); top_missing->push_back(""); return false; }
                    top_missing->push_back(std::string(V.name));
                }
            }
            return !(is_top && top_missing && !top_missing->empty());
        }
        // Integers and chars: split the domain into the elementary intervals the
        // column's literals / ranges bound, and require each to be covered.
        if (auto dom = int_domain(t0)) {
            const auto [dlo, dhi] = *dom;
            std::vector<__int128> cuts{dlo, dhi + 1};
            if (k0 == K::Char) { cuts.push_back(0xD800); cuts.push_back(0xE000); }
            std::vector<std::optional<std::pair<__int128, __int128>>> ivs;
            for (auto& r : rs) {
                if (r[0].is_null()) { ivs.push_back(std::nullopt); continue; }
                auto iv = pat_interval(r[0], t0, dlo, dhi);
                if (!iv) { undecidable = true; ivs.push_back(std::pair<__int128, __int128>{1, 0}); continue; }
                ivs.push_back(iv);
                if (iv->first > dlo && iv->first <= dhi) cuts.push_back(iv->first);
                if (iv->second >= dlo && iv->second < dhi) cuts.push_back(iv->second + 1);
            }
            std::sort(cuts.begin(), cuts.end());
            cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
            for (size_t ci = 0; ci + 1 < cuts.size(); ++ci) {
                const __int128 lo = cuts[ci], hi = cuts[ci + 1] - 1;
                if (lo < dlo || hi > dhi) continue;
                if (k0 == K::Char && lo >= 0xD800 && hi < 0xE000) continue;   // surrogates: no char
                std::vector<Row> nr;
                for (size_t ri = 0; ri < rs.size(); ++ri)
                    if (!ivs[ri] || (ivs[ri]->first <= lo && hi <= ivs[ri]->second))
                        nr.push_back(drop_first(rs[ri]));
                if (!exh(std::move(nr), rest_tys)) return false;
            }
            return true;
        }
        // Slices and arrays: one constructor per length. Lengths up to the
        // longest fixed pattern + 1 and the longest prefix + suffix of a `..`
        // pattern stand for every length (the last one for all longer ones).
        if ((k0 == K::Slice || k0 == K::Array) && TypeRef(t0).elem()) {
            bool any_str = false, any_slice = false;
            for (auto& r : rs)
                if (!r[0].is_null()) {
                    if (code_of(r[0]) == la::PAT_STR) any_str = true;
                    else if (code_of(r[0]) == la::PAT_SLICE) any_slice = true;
                    else undecidable = true;
                }
            if (!any_str && any_slice) {
                struct SliceShape { std::vector<TinyMapView> pre, suf; bool var = false; };
                std::vector<std::optional<SliceShape>> shapes;
                size_t max_fixed = 0, max_var = 0;
                for (auto& r : rs) {
                    if (r[0].is_null() || code_of(r[0]) != la::PAT_SLICE) { shapes.push_back(std::nullopt); continue; }
                    SliceShape sh;
                    for (auto e : list_items(r[0], la::ITEMS.code)) {
                        if (!e.is_null() && (code_of(e) == la::PAT_REST ||
                                             (code_of(e) == la::PAT_AT && e.has_key(la::VALUE) &&
                                              code_of(map_of(e.get(la::VALUE.code))) == la::PAT_REST))) {
                            sh.var = true; continue;
                        }
                        (sh.var ? sh.suf : sh.pre).push_back(e);
                    }
                    if (sh.var) max_var = std::max(max_var, sh.pre.size() + sh.suf.size());
                    else max_fixed = std::max(max_fixed, sh.pre.size());
                    shapes.push_back(std::move(sh));
                }
                std::vector<size_t> lens;
                if (k0 == K::Array) lens.push_back(size_t(TypeRef(t0).arr_size()));
                else for (size_t n = 0; n <= std::max(max_fixed + 1, max_var); ++n) lens.push_back(n);
                const TypeRef et = TypeRef(t0).elem();
                for (size_t n : lens) {
                    std::vector<Row> nr;
                    std::vector<TypeRef> nt(n, et);
                    nt.insert(nt.end(), rest_tys.begin(), rest_tys.end());
                    for (size_t ri = 0; ri < rs.size(); ++ri) {
                        Row x;
                        if (!shapes[ri]) { x = Row(n); }
                        else {
                            const auto& sh = *shapes[ri];
                            if (sh.var ? sh.pre.size() + sh.suf.size() > n : sh.pre.size() != n) continue;
                            x = sh.pre;
                            if (sh.var) {
                                x.resize(n - sh.suf.size());
                                x.insert(x.end(), sh.suf.begin(), sh.suf.end());
                            }
                        }
                        auto tl = drop_first(rs[ri]);
                        x.insert(x.end(), tl.begin(), tl.end());
                        nr.push_back(std::move(x));
                    }
                    if (!exh(std::move(nr), nt)) return false;
                }
                return true;
            }
            // A string (or a slice matched by literal strings): an unbounded
            // domain — only the wildcard rows cover it, and that is decided.
            std::vector<Row> nr;
            for (auto& r : rs) if (r[0].is_null()) nr.push_back(drop_first(r));
            return exh(std::move(nr), rest_tys);
        }
        // Floats and 128-bit integers: unbounded; the wildcard rows decide.
        if (k0 == K::F32 || k0 == K::F64 || k0 == K::I128 || k0 == K::U128) {
            std::vector<Row> nr;
            for (auto& r : rs) if (r[0].is_null()) nr.push_back(drop_first(r));
            return exh(std::move(nr), rest_tys);
        }
        // No enumerable constructors: only wildcard rows cover the column (a
        // literal row may still cover a value, so a miss is no longer a proof).
        std::vector<Row> nr;
        for (auto& r : rs) {
            if (r[0].is_null()) nr.push_back(drop_first(r));
            else undecidable = true;
        }
        return exh(std::move(nr), rest_tys);
    };
    std::vector<Row> rows0;
    for (auto p : flat) rows0.push_back(Row{p});
    bool res = exh(std::move(rows0), {t});
    if (decided) *decided = !res && !undecidable;
    return res;
}

// Emit the body-prologue `let` destructures for nested sub-patterns inside an
// enum-variant payload (`Some((a, b))`, `Some(Inner { f })`, `Some(Some(_))`),
// collected by build_pattern into `nested_subs`. Shared by match arms and the
// if-let / while-let lowerings so all three handle nested payload patterns
// identically. `for_guard` suppresses the refutable nested-variant let-else
// (which assumes the arm already matched) when building a guard prologue.
void SemaChecker::emit_nested_pat_destructure(
        const std::vector<NestedPatSub>& nested_subs,
        std::vector<lir_view::StmtRef>& nested_destructure_stmts, bool for_guard) {
    for (auto& nsub : nested_subs) {
        logos::probe::census("s3.nested_destructure");
        TypeRef synth_t = lookup(nsub.synth_name);
        if (!synth_t) continue;
        const int32_t nsc = code_of(nsub.sub_pat_node);
        if (nsc == la::PAT_VARIANT_DATA ||
            ((nsc == la::PAT_TUPLE || nsc == la::PAT_STRUCT) &&
             !ast_pat_irrefutable(nsub.sub_pat_node))) {
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
            std::function<void(lir::LExprPtr, TypeRef, writ::TinyMapView)>
            emit_tuple_lets =
                [&](lir::LExprPtr src, TypeRef tty, writ::TinyMapView tnode) {
                // BY-REFERENCE source (`&(A, B)`): every leaf binds `&(*src).i`.
                if (tty && (TypeRef(tty).kind() == LogosType::Kind::Ref ||
                            TypeRef(tty).kind() == LogosType::Kind::MutRef) &&
                    TypeRef(tty).pointee() && TypeRef(tty).pointee().kind() == LogosType::Kind::Tuple) {
                    const bool rm = TypeRef(tty).kind() == LogosType::Kind::MutRef;
                    TypeRef tup = TypeRef(tty).pointee();
                    if (!tnode.has_key(la::ITEMS)) return;
                    std::string stmp = std::format("__pat_tup_{}", tmp_var_count_++);
                    define(stmp, tty);
                    {
                        lir::SLet sl0; sl0.name = stmp; sl0.type = tty; sl0.is_mut = false; sl0.value = std::move(src);
                        nested_destructure_stmts.push_back(make_stmt_emit(node_line_, std::move(sl0)));
                    }
                    auto items = arr_of(tnode.get(la::ITEMS.code));
                    auto elems = TypeRef(tup).tuple_elems();
                    for (uint64_t i = 0; i < items.size() && i < elems.size(); ++i) {
                        auto en = map_of(items.get(i));
                        if (code_of(en) == la::PAT_OR && en.has_key(la::ITEMS)) {
                            auto alts = arr_of(en.get(la::ITEMS.code));
                            if (alts.size() == 1) en = map_of(alts.get(0));
                        }
                        TypeRef rt = make_ref(rm, elems[i]);
                        auto addr = builder().addr_of_temp(
                            builder().tuple_index(builder().deref(builder().var_ref(stmp, tty), tup),
                                                  (uint32_t)i, elems[i]),
                            rm, rt, lir_schema::expr::BorrowOrigin::Explicit);
                        if (code_of(en) == la::PAT_TUPLE) { emit_tuple_lets(std::move(addr), rt, en); continue; }
                        if (code_of(en) != la::PAT_WILD || !en.has_key(la::NAME)) continue;
                        std::string nm(str_of(en.get(la::NAME.code)));
                        if (nm == "_") continue;
                        define(nm, rt, false);
                        lir::SLet el; el.name = nm; el.type = rt; el.is_mut = false; el.value = std::move(addr);
                        nested_destructure_stmts.push_back(make_stmt_emit(node_line_, std::move(el)));
                    }
                    return;
                }
                if (!tty || TypeRef(tty).kind() != LogosType::Kind::Tuple) return;
                if (!tnode.has_key(la::ITEMS)) return;
                auto items = arr_of(tnode.get(la::ITEMS.code));
                auto elems = TypeRef(tty).tuple_elems();
                // Spill the source to a temp so each element read
                // references it once. The spill MOVES `src` — mark its place
                // moved so the owner's scope-exit Drop is suppressed (else
                // double-free): top level is var_ref(synth payload) → the whole
                // tuple; a nested level is tuple_index(parent, i) → one element.
                if (is_move_type(tty)) mark_moved_expr(expr_ref_of(src));
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
                        const bool emut_ = pat_byval_mut(en);
                        define(nm, et, emut_);
                        // Binding moves the element OUT of stmp — mark stmp.<i>
                        // moved so stmp's scope-exit Drop skips it (else double).
                        if (is_move_type(et)) mark_moved_expr(expr_ref_of(elem_expr));
                        lir::SLet el; el.name = nm; el.type = et;
                        el.is_mut = emut_; el.value = std::move(elem_expr);
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
        // A BY-REFERENCE synth (`&W`, default binding mode): bind `&(*synth).f`.
        const bool s_ref = (TypeRef(synth_t).kind() == LogosType::Kind::Ref ||
                            TypeRef(synth_t).kind() == LogosType::Kind::MutRef) &&
                           TypeRef(synth_t).pointee();
        const bool s_rm = s_ref && TypeRef(synth_t).kind() == LogosType::Kind::MutRef;
        const TypeRef s_obj = s_ref ? TypeRef(synth_t).pointee() : synth_t;
        // Look up struct info from synth's struct name.
        std::string sname_s(TypeRef(s_obj).struct_name());
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
            const bool bmut_ =
                (pat_byval_mut(fnode) ||
                 (fnode.has_key(la::VALUE) && pat_byval_mut(map_of(fnode.get(la::VALUE.code)))));
            if (s_ref) {
                if (bind == "_") continue;
                TypeRef rt = make_ref(s_rm, ftype);
                define(bind, rt, false);
                auto addr = builder().addr_of_temp(
                    builder().field_read(builder().deref(builder().var_ref(nsub.synth_name, synth_t), s_obj),
                                         fname, ftype),
                    s_rm, rt, lir_schema::expr::BorrowOrigin::Explicit);
                lir::SLet sl;
                sl.name = bind; sl.type = rt; sl.is_mut = false; sl.value = std::move(addr);
                nested_destructure_stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
                continue;
            }
            define(bind, ftype, bmut_);
            auto sref = builder().var_ref(nsub.synth_name, synth_t);
            auto fr = builder().field_read(std::move(sref), fname, ftype);
            // The binding moves the field OUT of the synth — mark synth.<f>
            // moved so the synth's scope-exit Drop skips it (the tuple branch
            // above does the same for its elements; without it: double drop).
            if (is_move_type(ftype)) mark_moved_expr(expr_ref_of(fr));
            lir::SLet sl;
            sl.name = bind; sl.type = ftype; sl.is_mut = bmut_;
            sl.value = std::move(fr);
            nested_destructure_stmts.push_back(make_stmt_emit(node_line_, std::move(sl)));
        }
    }
}

bool SemaChecker::emit_for_pattern_destructure(
        writ::TinyMapView pat, const std::string& src_var, TypeRef src_type,
        std::vector<lir_view::StmtRef>& out) {
    // Unwrap the grammar's single-alt PAT_OR wrapper.
    if (code_of(pat) == la::PAT_OR && pat.has_key(la::ITEMS)) {
        auto alts = arr_of(pat.get(la::ITEMS.code));
        if (alts.size() == 1) pat = map_of(alts.get(0));
    }
    // `for PAT in it` binds as `let PAT = <element>;` in the body: any
    // irrefutable pattern, with the default binding mode through a `&`
    // element; a refutable one is E0005, as rustc says.
    let_pat_site_ = "`for` loop binding";
    out.push_back(lower_let_pat_rhs(pat, builder().var_ref(src_var, src_type), src_type));
    let_pat_site_ = nullptr;
    return true;
}

// ADR 0011 — desugar `match e { E::V(b) => …, _ => … }` over a schema enum into:
//   { let __sm = e.m;  let __code = (&*__sm).schema_type_code();
//     if __code == V1::CODE { let b = V1{m:__sm}; <body1> } else if … else { <wild> } }
// The variant is read from the matched node's own schema_type_code — no stored
// discriminant. Inside each arm the binding is the concrete variant view (trusted).
lir_view::StmtRef SemaChecker::lower_schema_enum_match(TinyMapView node,
                                                       lir::LExprPtr scrut,
                                                       TypeRef scrut_type) {
    auto [epkg, esi] = struct_of(TypeRef(scrut_type));
    TypeRef wmap      = make_synth_generic_struct("WMap", {make_synth_struct("Wu6"),
                                                     make_synth_enum("WAny")});
    TypeRef wmap_cptr = make_ptr(false, wmap);
    TypeRef u64t      = prim(LogosType::Kind::U64);

    auto lower_body_into = [&](TinyMapView body, bool is_expr,
                               std::vector<lir_view::StmtRef>& out) {
        if (is_expr) {
            lir::SExprStmt es; es.expr = lower_expr(body);
            out.push_back(make_stmt_emit(node_line_, std::move(es)));
        } else {
            lower_block(body).each_stmt([&](lir_view::StmtRef s){ out.push_back(s); });
        }
    };

    std::vector<lir_view::StmtRef> outer;
    // let __sm = <scrut>.m;
    std::string sm = "__se_m_" + std::to_string(tmp_var_count_++);
    define(sm, wmap_cptr);
    { lir::SLet l; l.name = sm; l.type = wmap_cptr; l.is_mut = false;
      l.value = builder().field_read(std::move(scrut), "m", wmap_cptr);
      outer.push_back(make_stmt_emit(node_line_, std::move(l))); }
    // let __code = (&* __sm).schema_type_code();
    std::string codev = "__se_code_" + std::to_string(tmp_var_count_++);
    define(codev, u64t);
    { auto mref = builder().cast(builder().var_ref(sm, wmap_cptr), make_ref(false, wmap));
      auto call = builder().method_call(std::move(mref), "schema_type_code", "", {}, {}, -1, u64t);
      lir::SLet l; l.name = codev; l.type = u64t; l.is_mut = false; l.value = std::move(call);
      outer.push_back(make_stmt_emit(node_line_, std::move(l))); }

    struct Arm { uint64_t vcode; std::string binding; TypeRef vty; TinyMapView body; bool is_expr; };
    std::vector<Arm> arms;
    std::optional<TinyMapView> wild_body; bool wild_is_expr = false;
    if (node.has_key(la::ITEMS)) {
        auto al = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < al.size(); ++i) {
            auto arm = map_of(al.get(i));
            if (code_of(arm) != la::MATCH_ARM || !arm.has_key(la::LHS)) continue;
            auto pat = map_of(arm.get(la::LHS.code));
            // Match arms wrap the pattern in a single-alt PAT_OR — unwrap it.
            if (code_of(pat) == la::PAT_OR && pat.has_key(la::ITEMS)) {
                auto alts = arr_of(pat.get(la::ITEMS.code));
                if (alts.size() == 1) pat = map_of(alts.get(0));
                else { error("schema enum match: or-patterns not supported yet"); continue; }
            }
            bool is_expr = arm.has_key(la::EXPR);
            TinyMapView body = is_expr ? map_of(arm.get(la::EXPR.code))
                                       : map_of(arm.get(la::BODY.code));
            int32_t pc = code_of(pat);
            if (pc == la::PAT_WILD) { wild_body = body; wild_is_expr = is_expr; continue; }
            if (pc != la::PAT_VARIANT_DATA && pc != la::PAT_VARIANT) {
                error("schema enum match: arm pattern must be `E::Variant(b)` or `_`");
                continue;
            }
            std::string vname = pat.has_key(la::FIELD)
                ? std::string(str_of(pat.get(la::FIELD.code)))
                : std::string(str_of(pat.get(la::NAME.code)));
            std::string binding;
            if (pat.has_key(la::ARGS)) {
                auto aav = pat.get(la::ARGS.code);
                if (!aav.is_null() && aav.is_pointer()) {
                    auto blist = map_of(aav);
                    if (blist.has_key(la::ITEMS)) {
                        auto bitems = arr_of(blist.get(la::ITEMS.code));
                        if (bitems.size() >= 1) {
                            auto b0 = map_of(bitems.get(0));
                            if (b0.has_key(la::NAME))
                                binding = std::string(str_of(b0.get(la::NAME.code)));
                        }
                    }
                }
            }
            TypeRef vty = nullptr; uint64_t vcode = 0; bool found = false;
            auto enum_args = TypeRef(scrut_type).type_args();
            for (auto& pr : esi->schema_variants) {
                if (pr.first == vname) {
                    vty = pr.second;
                    // ADR 0011 generics — generic schema enum: substitute the variant
                    // type with the scrutinee's type-args (E<i64>::A(Wrap<T>) → Wrap<i64>),
                    // then compute the variant's PER-INSTANCE code (same shared helper as
                    // make/view_checked, so the pointee's stamped code matches). Bind the
                    // arm var to the SUBSTITUTED view type.
                    if (!esi->type_params.empty() && !enum_args.empty()) {
                        SemaSubst sub;
                        for (size_t ti = 0; ti < esi->type_params.size() && ti < enum_args.size(); ++ti)
                            sub[esi->type_params[ti].name] = enum_args[ti];
                        vty = subst_type_sema(vty, sub);
                    }
                    auto [vpkg, vsi] = struct_of(TypeRef(vty));
                    if (vsi) vcode = schema_instance_code(vty, vsi->schema_type_code, vpkg);
                    found = true; break;
                }
            }
            if (!found) {
                error(std::format("schema enum '{}' has no variant '{}'",
                                  TypeRef(scrut_type).struct_name(), vname));
                continue;
            }
            arms.push_back({vcode, binding, vty, body, is_expr});
        }
    }

    std::optional<std::vector<lir_view::StmtRef>> else_blk;
    if (wild_body) {
        std::vector<lir_view::StmtRef> wb;
        lower_body_into(*wild_body, wild_is_expr, wb);
        else_blk = std::move(wb);
    }
    for (size_t i = arms.size(); i-- > 0; ) {
        Arm& a = arms[i];
        std::vector<lir_view::StmtRef> tb;
        push_scope();
        if (!a.binding.empty() && a.binding != "_") {
            define(a.binding, a.vty);
            lir::SLet bl; bl.name = a.binding; bl.type = a.vty; bl.is_mut = false;
            std::vector<std::pair<std::string, lir::LExprPtr>> flds;
            flds.emplace_back("m", builder().var_ref(sm, wmap_cptr));
            bl.value = builder().struct_lit(std::string(TypeRef(a.vty).struct_name()),
                                            std::move(flds), a.vty);
            tb.push_back(make_stmt_emit(node_line_, std::move(bl)));
        }
        lower_body_into(a.body, a.is_expr, tb);
        pop_scope();
        lir::SIf sif;
        sif.cond  = builder().bin_op("==", builder().var_ref(codev, u64t),
                                     builder().lit_int(static_cast<int64_t>(a.vcode), u64t),
                                     bool_t());
        sif.then_ = lir_mirror_block(*cur_prog_, tb);
        if (else_blk) sif.else_ = lir_mirror_block(*cur_prog_, *else_blk);
        std::vector<lir_view::StmtRef> chain;
        chain.push_back(make_stmt_emit(node_line_, std::move(sif)));
        else_blk = std::move(chain);
    }
    if (else_blk) for (auto s : *else_blk) outer.push_back(s);
    return make_stmt_emit(node_line_, lir::SBlock{lir_mirror_block(*cur_prog_, outer), /*transparent=*/true});
}

bool SemaChecker::bare_name_is_value_pattern_(std::string_view nm, TypeRef ty) {
    if (nm.empty() || nm == "_") return false;
    TypeRef et = ty;
    while (et && (TypeRef(et).kind() == LogosType::Kind::Ref ||
                  TypeRef(et).kind() == LogosType::Kind::MutRef) && TypeRef(et).pointee())
        et = TypeRef(et).pointee();
    if (et && TypeRef(et).kind() == LogosType::Kind::Enum)
        if (auto esi = find_enum_by_name(std::string(TypeRef(et).enum_name())).second)
            for (auto& v : esi->variants)
                if (v.name == nm && v.payload_types.empty()) return true;
    if (auto vit = cur_imports_.variant_aliases.find(std::string(nm)); vit != cur_imports_.variant_aliases.end())
        if (auto vesi = find_enum_by_name(vit->second).second)
            for (auto& v : vesi->variants)
                if (v.name == nm && v.payload_types.empty()) return true;
    return static_cast<bool>(resolve_const_value(std::string(nm)));
}

// ADR 0030 S3.4b: THE match lowering. One implementation for every place a
// `match` stands (`form`): a statement (arm bodies, values discarded), the tail
// of a fn body (an expression arm IS the return), a value (arm values unify
// into the match's type). The scrutinee, the temporary-scrutinee hoist, the Writ
// hoist, the arm expansion, the pattern / binding / guard phase, the per-arm
// move and definite-assignment discipline, the drop flags and exhaustiveness
// are one code; only an arm's body and the merge of arm values differ by form.
// The statement and the expression spelling were two ~900-line copies and had
// drifted (E0507 at the arm, the Writ root helper and the exhaustiveness
// backstop in one; the guard-move union and Never-aware divergence in the other).
SemaChecker::MatchCore SemaChecker::lower_match_core(TinyMapView node, MatchForm form) {
    MatchCore mc;
    const bool value_form = form == MatchForm::Value;
    // An `if let` / let-chain in EXPRESSION position without `else` (the HIR
    // pass records it in ORIGIN): every branch of a value must yield it.
    if (value_form && node.has_key(la::ORIGIN)) {
        AnyVal ov = node.get(la::ORIGIN.code);
        const auto o = ov.is_value() ? static_cast<hir::Origin>(ov.as_value<int64_t>()) : hir::Origin::User;
        if (o == hir::Origin::IfLetNoElse || o == hir::Origin::LetChainNoElse) {
            error("if-let-as-expression requires an else branch");
            mc.refused = true;
            return mc;
        }
    }
    lir::LExprPtr scrut = nullptr;
    TypeRef scrut_type = error_t();
    if (node.has_key(la::VALUE)) {
        // `*x` over a Deref-impl struct: the step is MUTABLE exactly when an
        // arm binds by `ref mut` (see `arms_bind_ref_mut`). `matchderefsite` is
        // the observational outer half; `matchderefmut` is the WIDER spelling,
        // still unlanded — mutable for every deref scrutinee whatever the arms
        // do — and its population here is ONE.
        auto _snode = map_of(node.get(la::VALUE.code));
        if (code_of(_snode) == la::DEREF && _snode.has_key(la::VALUE)) {
            (void)logos::probe::on("matchderefsite");
            if (arms_bind_ref_mut(node) || logos::probe::on("matchderefmut"))
                mut_place_ctx_ = true;
        }
        scrut = lower_expr(_snode);
        mut_place_ctx_ = false;
        scrut_type = expr_type(scrut);
    } else { scrut = error_expr(); }
    // `match *s { [ref a, ..] => … }` over `s: &[T]`: the scrutinee is the
    // slice PLACE, not a reference to it, though `*s` keeps the fat type.
    // The slice door's default binding mode reads this.
    const TypeRef saved_spd_ = place_deref_scrut_type_;
    place_deref_scrut_type_ = (node.has_key(la::VALUE) &&
                               code_of(map_of(node.get(la::VALUE.code))) == la::DEREF)
                              ? scrut_type : TypeRef(nullptr);
    struct SpdRestore { TypeRef& r; TypeRef v; ~SpdRestore() { r = v; } } spd_restore_{place_deref_scrut_type_, saved_spd_};

    // ADR 0011 — a statement `match` over a `schema enum` desugars to an
    // if-chain on the pointee's schema_type_code (the variant discriminant is
    // NOT stored; it is read from the matched node itself).
    if (!value_form) {
        TypeRef se_base = scrut_type;
        while (se_base && is_ref_like(TypeRef(se_base).kind()) && TypeRef(se_base).pointee())
            se_base = TypeRef(se_base).pointee();
        if (se_base && TypeRef(se_base).kind() == LogosType::Kind::Struct) {
            auto [se_pkg, se_si] = struct_of(TypeRef(se_base));
            if (se_si && se_si->is_schema_enum) {
                mc.schema_stmt = lower_schema_enum_match(node, std::move(scrut), se_base);
                return mc;
            }
        }
    }

    // Drop a droppable match scrutinee that is a TEMPORARY (an rvalue — a call
    // result / constructor / `?`, NOT a place). Rust drops the matched
    // temporary at the end of the match; it is hoisted into a synth local so it
    // has an owner — `{ let __ms = <scrut>; match __ms { … }; <drop __ms unless
    // moved> }` (the caller closes the scope: MatchCore::temp_scrut_*). A
    // scope-tracked local, so EVERY exit path drops it: the fall-through (the
    // caller's collect_drops) and an arm's early `return` / `break`
    // (collect_all_drops). mark_match_scrutinee_moved marks it moved when an arm
    // consumes the payload, so the drop is suppressed there.
    // ⚠ WAS A FIVE-TERM LIST, WRITTEN OUT TWICE, BOTH MISSING SliceIndex — so
    // `match slice[0]` over a Drop-bearing element was hoisted and destructured
    // out of a value the backing array still owns. The property is "is a place".
    if (scrut && scrut_type && is_move_type(scrut_type) &&
        !lir_view::is_place_expr(expr_ref_of(scrut))) {
        mc.temp_scrut_var = "__match_scrut_" + std::to_string(tmp_var_count_++);
        push_scope();
        define(mc.temp_scrut_var, scrut_type);
        lir::SLet sl;
        sl.name = mc.temp_scrut_var; sl.type = scrut_type; sl.is_mut = false;
        sl.value = std::move(scrut);
        mc.temp_scrut_let = make_stmt_emit(node_line_, std::move(sl));
        scrut = builder().var_ref(mc.temp_scrut_var, scrut_type);
        mc.temp_scrut_hoisted = true;
    }

    // Sprint 5.2: arm-after-catchall lint (closes B-pt-07). The first unguarded
    // `_` arm makes every later arm unreachable. Not for a desugared `if let` /
    // `while let` (the node carries its PAT): its `_` arm is the else branch.
    if (node.has_key(la::ITEMS) && !node.has_key(la::PAT)) {
        auto arms_l = cfg_live_entries_(arr_of(node.get(la::ITEMS.code)));
        bool seen_catchall = false;
        for (uint64_t i = 0; i < arms_l.size(); ++i) {
            auto arm = arms_l[i];
            if (code_of(arm) != la::MATCH_ARM) continue;
            if (seen_catchall) {
                // rustc: a warning (`unreachable pattern`), not an error.
                warn("unreachable pattern: a previous '_' arm matches all values");
                break;
            }
            if (is_catchall_pat(arm)) seen_catchall = true;
        }
    }

    // Writ scalar patterns require the scrutinee addressable in a variable: it
    // is hoisted so the synthesized guards take the root without re-evaluating
    // it. A pattern tree "contains" a Writ scalar if it IS one, or a PAT_OR alt
    // is one (a nested PAT_AT / PAT_REF over one is diagnosed by build_pattern
    // via in_match_writ_ctx_).
    auto is_writ_pat_code = [](int32_t pc) {
        return pc == la::PAT_WRIT_NULL || pc == la::PAT_WRIT_BOOL ||
               pc == la::PAT_WRIT_INT  || pc == la::PAT_WRIT_STR  ||
               pc == la::PAT_WRIT_MAP  || pc == la::PAT_WRIT_ARR  ||
               pc == la::PAT_WRIT_TYPED_ARR || pc == la::PAT_WRIT_TYPED_MAP;
    };
    auto pat_contains_writ = [&](TinyMapView p) -> bool {
        if (is_writ_pat_code(code_of(p))) return true;
        if (code_of(p) == la::PAT_OR && p.has_key(la::ITEMS)) {
            auto arr = arr_of(p.get(la::ITEMS.code));
            for (uint64_t i = 0; i < arr.size(); ++i)
                if (is_writ_pat_code(code_of(map_of(arr.get(i))))) return true;
        }
        return false;
    };
    bool has_writ_pat = false;
    if (node.has_key(la::ITEMS)) {
        auto arms = cfg_live_entries_(arr_of(node.get(la::ITEMS.code)));
        for (uint64_t i = 0; i < arms.size(); ++i) {
            auto arm = arms[i];
            if (code_of(arm) != la::MATCH_ARM || !arm.has_key(la::LHS)) continue;
            if (pat_contains_writ(map_of(arm.get(la::LHS.code)))) { has_writ_pat = true; break; }
        }
    }
    //   let __hmatch_view = <scrut>;               // the view (Writ/View/Static or &)
    //   let __hmatch_root = writ_pat_root(view);   // the root node, used by the guard helpers
    std::string root_var;
    std::string base_var;
    TypeRef anyval_t = nullptr;
    if (has_writ_pat) {
        if (!writ_view_inner(scrut_type)) {
            error(std::format(
                "match with Writ patterns requires a view scrutinee "
                "(Writ, WritView, or WritStatic; use & to borrow); "
                "got {}", type_str(scrut_type)));
        }
        std::string view_var = "__hmatch_view_" + std::to_string(tmp_var_count_++);
        {
            lir::SLet sl;
            sl.name = view_var; sl.type = scrut_type; sl.is_mut = false;
            sl.value = std::move(scrut);
            mc.hoists.push_back(make_stmt_emit(node_line_, std::move(sl)));
        }
        // writ: the node type is WAny (the helper's return type); the root is
        // writ_pat_root(view) (static blob) or writ_pat_root_rc(&Rc<Writ>)
        // (runtime container) — every leaf/slot helper takes *WAny + ignores base.
        TypeRef scrut_inner = writ_view_inner(scrut_type);
        const char* root_helper =
            (scrut_inner && TypeRef(scrut_inner).struct_name() == "Rc")
            ? "writ_pat_root_rc" : "writ_pat_root";
        {
            auto root_cands = find_func_candidates(root_helper);
            const SemaFuncInfo* root_fi = nullptr;
            for (auto* c : root_cands) if (c->param_types.size() == 1) { root_fi = c; break; }
            anyval_t = root_fi ? root_fi->ret_type : make_synth_datatype("AnyVal");
            if (!root_fi)
                error("match with Writ patterns requires `use logos.lang.writ.pat;`");
        }
        root_var = "__hmatch_root_" + std::to_string(tmp_var_count_++);
        {
            std::vector<lir::LExprPtr> ra;
            ra.push_back(builder().var_ref(view_var, scrut_type));
            lir::SLet sl;
            sl.name = root_var; sl.type = anyval_t; sl.is_mut = false;
            sl.value = builder().call(root_helper, {}, std::move(ra), anyval_t);
            mc.hoists.push_back(make_stmt_emit(node_line_, std::move(sl)));
        }
        base_var = "__hmatch_base_" + std::to_string(tmp_var_count_++);
        {
            // writ: no base is threaded (WAny is self-relative); keep a dead
            // zero anchor so the hoist block shape is unchanged.
            lir::SLet sl;
            sl.name = base_var; sl.type = prim(LogosType::Kind::I64); sl.is_mut = false;
            sl.value = builder().lit_int(0, prim(LogosType::Kind::I64));
            mc.hoists.push_back(make_stmt_emit(node_line_, std::move(sl)));
        }
        scrut = builder().var_ref(view_var, scrut_type);
    }

    mc.result_type = error_t();
    if (node.has_key(la::ITEMS)) {
        auto arms = cfg_live_entries_(arr_of(node.get(la::ITEMS.code)));
        // P4-pm-25: fan out or-pattern arms whose alternatives are not pure
        // scalar literals that bind nothing (PAT_INT / PAT_BOOL / PAT_CHAR):
        // each alternative goes through the single-arm path with its own
        // payload extraction and refutable-inner guard. B170-E: a variant whose
        // SINGLE payload arg is a multi-alt PAT_OR (`Some((a,_) | (_,a))`) fans
        // out one arm per alternative (`Some(P|Q)` → `Some(P) | Some(Q)`); each
        // fanned arm re-evaluates the guard with its own bindings. (S3.3b
        // retires the fan-out: the tester binds or-pattern alternatives.)
        struct EffArm { writ::TinyMapView arm; int32_t alt_idx; int32_t payload_alt = -1; int32_t at_alt = -1; };
        auto alt_is_merge_safe = [](int32_t c) -> bool {
            return c == la::PAT_INT || c == la::PAT_BOOL || c == la::PAT_CHAR;
        };
        auto or_needs_fanout = [&](writ::TinyMapView lhs) -> bool {
            if (code_of(lhs) != la::PAT_OR || !lhs.has_key(la::ITEMS)) return false;
            auto a = arr_of(lhs.get(la::ITEMS.code));
            if (a.size() < 2) return false;
            for (uint64_t k = 0; k < a.size(); ++k)
                if (!alt_is_merge_safe(code_of(map_of(a.get(k))))) return true;
            return false;
        };
        auto variant_payload_or_alts = [&](writ::TinyMapView lhs) -> int {
            // The grammar wraps a whole arm pattern in a single-alt PAT_OR.
            if (code_of(lhs) == la::PAT_OR && lhs.has_key(la::ITEMS)) {
                auto a = arr_of(lhs.get(la::ITEMS.code));
                if (a.size() == 1) lhs = map_of(a.get(0));
            }
            if (code_of(lhs) != la::PAT_VARIANT_DATA || !lhs.has_key(la::ARGS)) return 0;
            AnyVal aav = lhs.get(la::ARGS.code);
            if (aav.is_null() || !aav.is_pointer()) return 0;
            auto blist = map_of(aav);
            if (!blist.has_key(la::ITEMS)) return 0;
            auto items = arr_of(blist.get(la::ITEMS.code));
            if (items.size() != 1) return 0;
            auto arg = map_of(items.get(0));
            if (code_of(arg) != la::PAT_OR || !arg.has_key(la::ITEMS)) return 0;
            auto alts = arr_of(arg.get(la::ITEMS.code));
            // A pure scalar or (`Some(1|2)`) is a carried sub-pattern.
            bool needs = false;
            for (uint64_t k = 0; k < alts.size(); ++k)
                if (!alt_is_merge_safe(code_of(map_of(alts.get(k))))) { needs = true; break; }
            return (alts.size() >= 2 && needs) ? (int)alts.size() : 0;
        };
        std::vector<EffArm> eff_arms;
        for (uint64_t i = 0; i < arms.size(); ++i) {
            auto arm = arms[i];
            if (code_of(arm) != la::MATCH_ARM) { eff_arms.push_back({arm, -1}); continue; }
            if (arm.has_key(la::LHS)) {
                auto lhs = map_of(arm.get(la::LHS.code));
                // T1-8 (E0408): top-level `A | B =>` arm alternations must
                // bind the same names in every alternative.
                if (code_of(lhs) == la::PAT_OR)
                    check_or_alt_binding_consistency(lhs);
                if (or_needs_fanout(lhs)) {
                    logos::probe::census("s3.fanout.top");
                    auto a = arr_of(lhs.get(la::ITEMS.code));
                    for (uint64_t k = 0; k < a.size(); ++k)
                        eff_arms.push_back({arm, (int32_t)k});
                    continue;
                }
                if (int n = variant_payload_or_alts(lhs); n > 0) {
                    logos::probe::census("s3.fanout.payload");
                    for (int k = 0; k < n; ++k)
                        eff_arms.push_back({arm, -1, k});
                    continue;
                }
                if (int n = at_or_fanout_alts(lhs); n > 0) {
                    logos::probe::census("s3.fanout.at");
                    for (int k = 0; k < n; ++k)
                        eff_arms.push_back({arm, -1, -1, k});
                    continue;
                }
            }
            eff_arms.push_back({arm, -1});
        }
        auto effective_lhs = [&](writ::TinyMapView arm, int32_t alt_idx) {
            auto lhs = map_of(arm.get(la::LHS.code));
            if (alt_idx < 0) return lhs;
            return map_of(arr_of(lhs.get(la::ITEMS.code)).get((uint64_t)alt_idx));
        };
        // Every arm starts from the pre-match move / definite-assignment state;
        // a DIVERGING arm's moves do not leak into its siblings or past the
        // match —
        //   match it.next() { None => return acc, Some(v) => acc = f(acc, v) }
        // — and the post-match state is the union over the arms that fall
        // through (a variable moved on any falling-through path is moved).
        auto pre_moves = moved_vars_;
        const auto owned_pre_m = closure_owned_drop_;   // move-closure releases, merged per arm
        // Variants an EARLIER arm could match (for variant-exact payload moves).
        std::set<int64_t> earlier_discs; bool earlier_any = false;
        const size_t exact_mark = exact_variant_moves_.size();
        std::set<std::string> post_moves;
        auto pre_uninit = currently_uninit_vars_;
        std::set<std::string> post_uninit;
        bool post_uninit_initialized = false;
        bool any_non_diverging = false;
        // #118 — per-arm conditional-move bookkeeping: each arm that REACHES the
        // enclosing frame's drops is one branch, its body (or value) the place a
        // flag clear is spliced into.
        std::vector<CondMoveBranch> arm_branches;
        std::vector<size_t> arm_slot;
        for (uint64_t i = 0; i < eff_arms.size(); ++i) {
            auto arm = eff_arms[i].arm;
            int32_t alt_idx = eff_arms[i].alt_idx;
            if (code_of(arm) != la::MATCH_ARM) continue;

            moved_vars_ = pre_moves;
            closure_owned_drop_ = owned_pre_m;
            currently_uninit_vars_ = pre_uninit;
            size_t arm_clear_mark = flag_clear_log_.size();   // #118

            // Synthesized guard for Writ patterns (scalar + structural).
            lir::LExprPtr synth_guard = nullptr;
            std::vector<lir_view::StmtRef> body_prologue;
            std::vector<WritPatBinding> body_binds;
            if (has_writ_pat && arm.has_key(la::LHS)) {
                std::vector<lir_view::StmtRef> g_stmts;
                std::vector<WritPatBinding> g_binds;
                auto raw = build_writ_pat_guard(effective_lhs(arm, alt_idx), root_var, anyval_t,
                                                base_var, g_stmts, g_binds);
                if (!g_stmts.empty() && raw)
                    synth_guard = builder().block_expr(lir_mirror_block(*cur_prog_, g_stmts), std::move(raw), bool_t());
                else
                    synth_guard = std::move(raw);
                // Re-run the pattern lowering for parallel body-scope stmts /
                // bindings (fresh tmp names, consistent with body_prologue).
                if (!g_binds.empty())
                    (void)build_writ_pat_guard(effective_lhs(arm, alt_idx), root_var, anyval_t,
                                               base_var, body_prologue, body_binds);
            }

            // Build the pattern. P4-pm-02: the side channels register the
            // synthesized payload bindings of nested sub-patterns; P4-pm-01:
            // the refutable inner-pattern guards. Spec rule
            // pat.writ.match-only: a Writ scalar pattern is legal in a WRITTEN
            // `match` arm only (the let forms carry PAT on the node).
            in_match_writ_ctx_ = has_writ_pat && !node.has_key(la::PAT);
            std::vector<NestedPatSub> nested_subs;
            auto* saved_pat_subs = current_pat_nested_subs_;
            current_pat_nested_subs_ = &nested_subs;
            logos::compiler::StrSet mut_names;
            auto* saved_pat_muts = current_pat_mut_names_;
            current_pat_mut_names_ = &mut_names;
            std::vector<lir::LExprPtr> refut_guards;
            auto* saved_pat_refut = current_pat_refutable_guards_;
            current_pat_refutable_guards_ = &refut_guards;
            // B170-E: select this fanned arm's payload-or alternative.
            int32_t saved_payload_or_alt = payload_or_alt_;
            payload_or_alt_ = eff_arms[i].payload_alt;
            int32_t saved_at_or_alt = at_or_alt_;
            at_or_alt_ = eff_arms[i].at_alt;
            lir::Pattern pat = arm.has_key(la::LHS)
                ? build_pattern(effective_lhs(arm, alt_idx), scrut_type)
                : make_pat_wild("_");
            payload_or_alt_ = saved_payload_or_alt;
            at_or_alt_ = saved_at_or_alt;
            current_pat_nested_subs_ = saved_pat_subs;
            current_pat_refutable_guards_ = saved_pat_refut;
            in_match_writ_ctx_ = false;

            push_scope();
            // ── E0507 AT THE MATCH ARM ──────────────────────────────────────
            // `is_unowned_move_source` is the one predicate for "this place
            // does not own what it yields"; `bind_pattern` receives the
            // scrutinee's TYPE and never its EXPRESSION, so `match *r { E::A(d)
            // => … }` moved a payload out from behind a reference and nothing
            // asked. The discriminator is the ARM'S BINDING MODE, which the LIR
            // carries (`pat_keys::BINDING_REF_MODES`): `E::A(ref d)` moves
            // nothing. An INDEX scrutinee is another reader's question already
            // ("cannot move out of type `[W; 1]`" at the same line).
            auto scrut_is_index = [&]() {
                auto r = expr_ref_of(scrut);
                if (!r) return false;
                using SC = lir_schema::expr::Code;
                return r.kind() == SC::IndexRead || r.kind() == SC::SliceIndex;
            };
            if (is_move_type(scrut_type) && is_unowned_move_source(scrut) && !scrut_is_index()) {
                namespace ps2 = lir_schema::pat;
                const auto* tpool = cur_prog_->type_pool.impl();
                // TRUE and the BINDING'S OWN NAME in `out`. `wild_trusted`: a
                // named Wild at the arm's ROOT is by value (`ref n` lowers to
                // PatRefBind); UNDER A TUPLE it may be a rebuilt `ref a`, so no
                // claim. Only VariantData (which carries the modes) and a root
                // named Wild make a claim; RefBind / RefPat are by reference,
                // Struct / Slice carry no binding types here, PatAt no mode.
                auto byval_name = [&](auto&& self, lir_view::PatRef pr,
                                      std::string& out, bool wild_trusted) -> bool {
                    if (!pr) return false;
                    switch (pr.kind()) {
                        case ps2::Code::Wild: {
                            if (!wild_trusted) return false;
                            auto n = lir_view::PatWildView{pr}.name();
                            if (n.empty() || n == "_") return false;
                            out = std::string(n);
                            return true;
                        }
                        case ps2::Code::VariantData: {
                            lir_view::PatVariantDataView v{pr};
                            std::vector<std::string> ns;
                            std::vector<TypeRef> tys;
                            v.each_binding([&](std::string_view n){ ns.emplace_back(n); });
                            v.each_binding_type(tpool, [&](TypeRef ty){ tys.push_back(ty); });
                            // Mode 0 = by value; an EMPTY mode vector means "all
                            // by value" (minted only where a mode is spelled).
                            auto ms = v.bind_ref_modes();
                            for (size_t k = 0; k < ns.size(); ++k) {
                                uint32_t m = k < ms.size() ? ms[k] : 0u;
                                TypeRef bt = k < tys.size() ? tys[k] : TypeRef(nullptr);
                                if (m == 0 && ns[k] != "_" && bt && is_move_type(bt)) {
                                    out = ns[k];
                                    return true;
                                }
                            }
                            return false;
                        }
                        case ps2::Code::Tuple: {
                            bool any = false;
                            lir_view::PatTupleView{pr}.each_sub([&](lir_view::PatRef sp){
                                if (!any && self(self, sp, out, false)) any = true; });
                            return any;
                        }
                        case ps2::Code::Or: {
                            bool any = false;
                            lir_view::PatOrView{pr}.each_alt([&](lir_view::PatRef a){
                                if (!any && self(self, a, out, wild_trusted)) any = true; });
                            return any;
                        }
                        case ps2::Code::At:
                            return self(self, lir_view::PatAtView{pr}.sub(), out, wild_trusted);
                        default:
                            return false;
                    }
                };
                if (std::string bn; byval_name(byval_name, pat_ref_of(pat), bn, /*wild_trusted=*/true))
                    error(std::format(
                        "cannot move out of a value behind a reference / out of "
                        "an index (E0507): the match arm binds '{}' by value", bn));
            }
            bind_pattern(pat, scrut_type);
            current_pat_mut_names_ = saved_pat_muts;
            // Writ @-pattern bindings are in scope for the body and the guard.
            for (const auto& b : body_binds) define(b.name, anyval_t, /*is_mut=*/false);
            // P4-pm-02: field-by-field lets destructuring the synthesized
            // payload slots. A GUARDED arm gets a SECOND, independent copy for
            // the guard (B170-D/E): the block-expr's shadow-restore reverts a
            // binding already in scope from a sibling fanned or-arm.
            std::vector<lir_view::StmtRef> nested_destructure_stmts;
            emit_nested_pat_destructure(nested_subs, nested_destructure_stmts, /*for_guard=*/false);
            const bool arm_has_user_guard = arm.has_key(la::GUARD);

            std::optional<lir::LExprPtr> guard;
            if (arm_has_user_guard) {
                // A MATCH GUARD IS A CONDITIONALLY EVALUATED EXPRESSION: "the
                // guard RAN (and moved)" and "the guard never ran" are a
                // conditional move — the #118 flag, cleared inside the guard's
                // own value (`x if eatF(a) => …` with a failing guard destroyed
                // `a` twice). And A GUARD THAT RAN AND FAILED STILL MOVED: its
                // moves are the next arms' starting state.
                auto guard_pre = moved_vars_;
                auto g = lower_expr(map_of(arm.get(la::GUARD.code)));
                if (TypeRef(expr_type(g)).kind() != LogosType::Kind::Bool &&
                    TypeRef(expr_type(g)).kind() != LogosType::Kind::Error)
                    error("match guard must be bool");
                guard = std::move(g);
                if (moved_vars_ != guard_pre) {
                    size_t gm = flag_clear_log_.size();
                    std::vector<CondMoveBranch> gb;
                    gb.push_back({nullptr, &*guard, moved_vars_, gm, gm});
                    gb.push_back({nullptr, nullptr, guard_pre, gm, gm});
                    elaborate_cond_moves(guard_pre, gb);
                    for (auto& gmv_ : moved_vars_) pre_moves.insert(gmv_);
                }
            }
            // The synthesized Writ guard goes FIRST, so `&&` short-circuits the
            // user guard on a type mismatch (a guard runs only when the pattern
            // matched).
            if (synth_guard)
                guard = guard ? builder().bin_op("&&", std::move(synth_guard), std::move(*guard), bool_t())
                              : std::move(synth_guard);
            // P4-pm-01 / G145-2: AND in the refutable inner-pattern guards
            // (they read fresh pattern-bound names, never side-effect).
            for (auto& rg : refut_guards) {
                if (!rg) continue;
                guard = guard ? builder().bin_op("&&", std::move(*guard), std::move(rg), bool_t())
                              : std::move(rg);
            }
            // B170-D/E: a guarded arm with nested-payload destructure lets must
            // compute those bindings BEFORE the guard runs — the guard block
            // precedes the body. The SAFE (unconditional) destructure only.
            if (guard && arm_has_user_guard) {
                std::vector<lir_view::StmtRef> guard_destructure;
                emit_nested_pat_destructure(nested_subs, guard_destructure, /*for_guard=*/true);
                if (!guard_destructure.empty()) {
                    TypeRef gt = expr_type(*guard);
                    guard = builder().block_expr(lir_mirror_block(*cur_prog_, guard_destructure), std::move(*guard), gt);
                }
            }

            // This arm OWNS what its pattern binds by value — on THIS arm's
            // path only (an arm that binds nothing leaves the scrutinee a
            // flagged drop). After the guard, which may still read it.
            {
                namespace ps_ = lir_schema::pat;
                auto pr_ = pat_ref_of(pat);
                bool exact_ = false;
                if (pr_ && pr_.kind() == ps_::Code::VariantData) {
                    int64_t d_ = lir_view::PatVariantDataView{pr_}.disc();
                    // Exact = every value of this variant takes this arm: no
                    // refutable payload sub-pattern, no guard (user or
                    // synthesized), no earlier arm that could take it.
                    bool subs_irrefutable_ = true;
                    for (auto sp_ : lir_view::PatVariantDataView{pr_}.subs())
                        if (sp_ && !lir_view::is_irrefutable_pattern(sp_)) subs_irrefutable_ = false;
                    exact_ = !arm_has_user_guard && !guard.has_value() && !earlier_any &&
                             !earlier_discs.count(d_) && subs_irrefutable_;
                }
                if (arm.has_key(la::LHS))
                    mark_match_scrutinee_moved(scrut, scrut_type, pr_, exact_);
                if (pr_ && pr_.kind() == ps_::Code::VariantData)
                    earlier_discs.insert(lir_view::PatVariantDataView{pr_}.disc());
                else if (pr_ && pr_.kind() == ps_::Code::Variant)
                    earlier_discs.insert(lir_view::PatVariantView{pr_}.disc());
                else earlier_any = true;
            }

            MatchCoreArm out;
            // 0: falls through; 1: never reaches the enclosing frame's drops
            // (`return`, a `!` value); 2: `break` / `continue` — unwinds to the
            // loop body and reaches the frame's drops by the loop edge (#122).
            int div = 0;
            lir::LExprPtr val = nullptr;
            bool arm_diverges = false;
            if (arm.has_key(la::EXPR)) {
                // Arm values are CONDITIONALLY evaluated — own temporary
                // scope (a statement-level hoist of a droppable temp
                // receiver would evaluate EVERY arm eagerly).
                val = lower_moved_operand_(map_of(arm.get(la::EXPR.code)), /*temp_scoped=*/true);
            } else if (arm.has_key(la::BODY)) {
                auto body_node = map_of(arm.get(la::BODY.code));
                // B-fn-06: a trailing TAIL_EXPR is the arm value, not an
                // implicit return.
                bool saved_tail = tail_as_return_;
                tail_as_return_ = false;
                arm_diverges = (code_of(body_node) == la::BLOCK) ? block_always_diverts(body_node)
                                                                 : stmt_always_diverts(body_node);
                if (arm_diverges) {
                    // The tail is unreachable: the arm is `!` (skipped by
                    // the type merge).
                    lir_view::BlockRef blk_ref;
                    if (code_of(body_node) == la::BLOCK) {
                        blk_ref = lower_block(body_node);
                    } else {
                        std::vector<lir_view::StmtRef> blk;
                        push_stmt_with_unwind(blk, lower_stmt(body_node));  // #122
                        blk_ref = lir_mirror_block(*cur_prog_, blk);
                    }
                    val = builder().block_expr(blk_ref, error_expr(), never_t());
                } else if (code_of(body_node) == la::BLOCK) {
                    // A block arm is a block expression: its last item, when
                    // an expression, is the arm value; one ending in a
                    // statement (`{}`, `{ a = 1; }`) is `()`, as rustc types it.
                    val = lower_block_expr(body_node);
                } else {
                    std::vector<lir_view::StmtRef> blk;
                    push_stmt_with_unwind(blk, lower_stmt(body_node));  // #122
                    val = builder().block_expr(lir_mirror_block(*cur_prog_, blk), error_expr(), void_t());
                }
                tail_as_return_ = saved_tail;
            } else {
                error("match expression: arm has no body");
                val = error_expr();
            }
            // The nested-pattern destructure, then the Writ @-pattern
            // prologue, wrap the arm value.
            if (!nested_destructure_stmts.empty()) {
                TypeRef vt = val ? expr_type(val) : error_t();
                val = builder().block_expr(lir_mirror_block(*cur_prog_, nested_destructure_stmts), std::move(val), vt);
            }
            if (!body_prologue.empty() || !body_binds.empty()) {
                std::vector<lir_view::StmtRef> prologue = std::move(body_prologue);
                for (const auto& b : body_binds) {
                    lir::SLet sl;
                    sl.name = b.name; sl.type = anyval_t; sl.is_mut = false;
                    sl.value = builder().var_ref(b.av_var, anyval_t);
                    prologue.push_back(make_stmt_emit(node_line_, std::move(sl)));
                }
                TypeRef vt = expr_type(val);
                val = builder().block_expr(lir_mirror_block(*cur_prog_, prologue), std::move(val), vt);
            }
            // Coerce EVERY arm to the expected type before the merge: a
            // selective coercion splits TYPE from REPRESENTATION (the merged
            // type the slice, an arm still a thin ref-to-array).
            if (hint_expected_type_ && val &&
                TypeRef(expr_type(val)).kind() != LogosType::Kind::Error &&
                TypeRef(expr_type(val)).kind() != LogosType::Kind::Never) {
                apply_place_coercions(val, hint_expected_type_);
                // An expected `dyn`: every arm is unsized by a cast in the
                // arm (see cast_to_expected_dyn).
                cast_to_expected_dyn(val, hint_expected_type_);
            }
            // A diverging arm (`!`) contributes no type.
            TypeRef& result_type = mc.result_type;
            if (TypeRef(result_type).kind() == LogosType::Kind::Error ||
                TypeRef(result_type).kind() == LogosType::Kind::Never) {
                result_type = expr_type(val);
            } else if (TypeRef(expr_type(val)).kind() == LogosType::Kind::Never) {
                // keep result_type — this arm yields no value.
            } else if (TypeRef(expr_type(val)).kind() != LogosType::Kind::Error) {
                // The arms are ONE type: open inference variables unify.
                if (!infer_solved_.empty() &&
                    (has_infer_var_(result_type) || has_infer_var_(expr_type(val)))) {
                    infer_unify_(result_type, expr_type(val));
                    result_type = zonk_(result_type);
                    builder().retype_expr(val, zonk_(expr_type(val)));
                }
                // logos-core 1.4: distinct FnItems of one signature LUB to
                // the matching FnPtr, as Rust's LUB does for fn-item arms.
                bool lubbed_to_fnptr = false;
                if (TypeRef(result_type).kind() == LogosType::Kind::FnItem &&
                    TypeRef(expr_type(val)).kind() == LogosType::Kind::FnItem) {
                    LogosTypeBuilder fpt;
                    fpt.kind = LogosType::Kind::FnPtr;
                    for (auto p : TypeRef(expr_type(val)).closure_params())
                        fpt.closure_params.push_back(p);
                    fpt.closure_ret = TypeRef(expr_type(val)).closure_ret();
                    TypeRef fp = fnptr_item_binders_(pool_->alloc(std::move(fpt)), nullptr);
                    if (types_compatible(result_type, fp) && types_compatible(expr_type(val), fp)) {
                        result_type = fp;
                        lubbed_to_fnptr = true;
                    }
                }
                if (!lubbed_to_fnptr) {
                    if (!types_compatible(expr_type(val), result_type) &&
                        !types_compatible(result_type, expr_type(val)))
                        error(std::format(
                            "match expression: arm type '{}' is incompatible with '{}'",
                            type_str(expr_type(val)), type_str(result_type)));
                    else
                        result_type = unify_numeric(result_type, expr_type(val));
                }
            }
            // Upgrade an IntLit result to i64 if an arm literal overflows i32.
            if (TypeRef(result_type).kind() == LogosType::Kind::IntLit && val) {
                auto er = expr_ref_of(val);
                // A divergent arm's BlockExpr has NO result.
                if (er.kind() == lir_schema::expr::Code::BlockExpr)
                    er = lir_view::EBlockExprView{er}.result();
                if (er && er.kind() == lir_schema::expr::Code::LitInt) {
                    int64_t v = lir_view::ELitIntView{er}.value();
                    if (v > (int64_t)INT32_MAX || v < (int64_t)INT32_MIN)
                        result_type = prim(LogosType::Kind::I64);
                }
            }
            // [[baghunt-match-arm-binding-no-drop]]: the arm-scope bindings
            // drop before the arm value escapes — the value is hoisted into
            // a temp, the drops run, the temp is yielded. Not for an Error
            // (a divergent block unwound already) or Never value.
            if (val && TypeRef(expr_type(val)).kind() != LogosType::Kind::Error &&
                TypeRef(expr_type(val)).kind() != LogosType::Kind::Never) {
                mark_moved_in_expr_recursive(expr_ref_of(val));
                auto arm_drops = collect_drops();
                if (!arm_drops.empty()) {
                    TypeRef vt = expr_type(val);
                    std::vector<lir_view::StmtRef> blk;
                    if (TypeRef(vt).kind() == LogosType::Kind::Void) {
                        lir::SExprStmt es; es.expr = std::move(val);
                        blk.push_back(make_stmt_emit(node_line_, std::move(es)));
                        for (auto& d : arm_drops) blk.push_back(std::move(d));
                        val = builder().block_expr(lir_mirror_block(*cur_prog_, blk), error_expr(), vt);
                    } else {
                        std::string tmp = "__match_arm_tmp_" + std::to_string(tmp_var_count_++);
                        lir::SLet sl;
                        sl.name = tmp; sl.type = vt; sl.is_mut = false;
                        sl.value = std::move(val);
                        blk.push_back(make_stmt_emit(node_line_, std::move(sl)));
                        for (auto& d : arm_drops) blk.push_back(std::move(d));
                        val = builder().block_expr(lir_mirror_block(*cur_prog_, blk),
                                                   builder().var_ref(tmp, vt), vt);
                    }
                }
            }
            pop_scope();
            // A Never-typed arm value (`panic!`, `=> return x`) diverges
            // even without a block body.
            if (!arm_diverges && val && TypeRef(expr_type(val)).kind() == LogosType::Kind::Never)
                arm_diverges = true;
            if (arm_diverges) div = expr_arm_div_kind(val) == 2 ? 2 : 1;
            out.value = std::move(val);
            out.pat = std::move(pat);
            out.guard = std::move(guard);
            if (div == 0) {
                any_non_diverging = true;
                for (auto& m : moved_vars_) post_moves.insert(m);
                for (auto& v : currently_uninit_vars_) post_uninit.insert(v);
                post_uninit_initialized = true;
            }
            if (div != 1) {
                arm_branches.push_back({nullptr, nullptr, moved_vars_,
                                        arm_clear_mark, flag_clear_log_.size(), closure_owned_drop_});
                arm_slot.push_back(mc.arms.size());
            }
            mc.arms.push_back(std::move(out));
        }
        // Merge the per-arm contributions.
        auto pre_moves_kept = pre_moves;   // #118: the ternary moves from pre_moves
        moved_vars_ = any_non_diverging ? std::move(post_moves) : std::move(pre_moves);
        currently_uninit_vars_ = (any_non_diverging && post_uninit_initialized)
            ? std::move(post_uninit) : std::move(pre_uninit);
        // #118 — arm the flags; `mc.arms` is stable now, so an arm's body (or
        // value) is addressed and rebuilt in place.
        for (size_t k = 0; k < arm_branches.size(); ++k) {
            arm_branches[k].val = &mc.arms[arm_slot[k]].value;
        }
        // Variant-exact payload moves are moved on EVERY path (static) — if
        // still moved at the end of an arm that moved them.
        for (size_t xi = exact_mark; xi < exact_variant_moves_.size(); ++xi) {
            const std::string& xp = exact_variant_moves_[xi];
            bool live = false;
            for (auto& b : arm_branches) if (b.moves.count(xp)) { live = true; break; }
            if (!live) continue;
            moved_vars_.insert(xp);
            for (auto& b : arm_branches) b.moves.insert(xp);
        }
        exact_variant_moves_.resize(exact_mark);
        elaborate_cond_moves(pre_moves_kept, arm_branches, &owned_pre_m);
    }

    // Exhaustiveness: ONE verdict, the usefulness matrix (S3.1); the LIR-level
    // variant check is a backstop for a shape the matrix could not decide.
    {
        bool ast_exh = false, decided = false;
        if (node.has_key(la::ITEMS)) {
            std::vector<writ::TinyMapView> lhs_pats;
            auto arms_l = cfg_live_entries_(arr_of(node.get(la::ITEMS.code)));
            for (uint64_t i = 0; i < arms_l.size(); ++i) {
                auto arm = arms_l[i];
                if (code_of(arm) != la::MATCH_ARM) continue;
                if (arm.has_key(la::GUARD)) continue;      // user-guarded ≠ guaranteed
                if (arm.has_key(la::LHS)) lhs_pats.push_back(map_of(arm.get(la::LHS.code)));
            }
            ast_exh = check_exhaustive_(std::move(lhs_pats), scrut_type, decided);
        }
        std::vector<lir_view::PatRef> unguarded;
        for (auto& a : mc.arms)
            if (!a.guard) unguarded.push_back(pat_ref_of(a.pat));
        check_match_exhaustiveness(unguarded, scrut_type, ast_exh || decided);
    }
    mc.scrut = std::move(scrut);
    return mc;
}

// ADR 0030 S3.4c: a `match` in statement position is an expression statement
// of a match (there is no statement match). Without `;` its type is `()`, as
// rustc types a block-like expression statement; a TAIL match (the body's
// value, tail_match_nodes_) is the function's return, its arms coerced to the
// return type.
lir_view::StmtRef SemaChecker::lower_match(TinyMapView node) {
    const uint32_t match_line = node_line_;  // own line; arm lowering moves node_line_
    const bool tail = tail_match_nodes_.count(node.ptr()) &&
                      !(ret_type_ && TypeRef(ret_type_).kind() == LogosType::Kind::Void);
    const TypeRef saved_hint = hint_expected_type_;
    hint_expected_type_ = tail ? ret_type_ : TypeRef(nullptr);
    MatchCore mc = lower_match_core(node, tail ? MatchForm::Tail : MatchForm::Stmt);
    hint_expected_type_ = saved_hint;
    if (mc.schema_stmt) return mc.schema_stmt;
    lir::LExprPtr e = mc.refused ? error_expr() : match_expr_of_(mc);
    const auto k = TypeRef(expr_type(e)).kind();
    // A tail match every arm of which diverges (`!`) returns nothing itself.
    if (tail && k != LogosType::Kind::Never && k != LogosType::Kind::Error)
        return finish_return_(std::move(e), node, /*bind_temps=*/false);
    if (!tail && k != LogosType::Kind::Void && k != LogosType::Kind::Never && k != LogosType::Kind::Error)
        error(std::format("mismatched types: expected `()`, found `{}` — a `match` statement without `;` "
                          "has type `()` (E0308)", type_str(expr_type(e))));
    lir::SExprStmt es;
    es.expr = std::move(e);
    return make_stmt_emit(match_line, std::move(es));
}

lir_view::StmtRef SemaChecker::unit_match_stmt_(
        lir::LExprPtr scrut, std::vector<std::pair<lir::Pattern, std::vector<lir_view::StmtRef>>> arms) {
    lir::EMatchExpr me;
    me.scrut = std::move(scrut);
    for (auto& [pat, body] : arms)
        me.arms.push_back({std::move(pat), std::nullopt,
                           builder().block_expr(lir_mirror_block(*cur_prog_, body), nullptr, void_t())});
    lir::SExprStmt es;
    es.expr = builder().match_expr_v(std::move(me), void_t());
    return make_stmt_emit(node_line_, std::move(es));
}

lir::LExprPtr SemaChecker::lower_match_expr(TinyMapView node) {
    MatchCore mc = lower_match_core(node, MatchForm::Value);
    if (mc.refused) return error_expr();
    return match_expr_of_(mc);
}

// The match expression of a lowered core, with its hoists and, for a hoisted
// temporary scrutinee, `{ let __ms; let __mr = <match>; <drop __ms>; __mr }` —
// the value is bound first so __ms drops AFTER it is read (a returning arm
// dropped __ms already). A `()` match: `{ let __ms; <match>; <drop __ms> }`; a
// never / error one: `{ let __ms; <match> }`.
lir::LExprPtr SemaChecker::match_expr_of_(MatchCore& mc) {
    lir::EMatchExpr me;
    me.scrut = mc.scrut;
    for (auto& a : mc.arms) {
        lir::EMatchArm ema;
        ema.pat   = std::move(a.pat);
        ema.guard = std::move(a.guard);
        ema.value = std::move(a.value);
        me.arms.push_back(std::move(ema));
    }
    const TypeRef rty = mc.result_type;
    auto me_expr = builder().match_expr_v(std::move(me), rty);
    if (!mc.hoists.empty())
        me_expr = builder().block_expr(lir_mirror_block(*cur_prog_, mc.hoists), std::move(me_expr), rty);
    if (!mc.temp_scrut_hoisted) return me_expr;
    auto ft_drops = collect_drops();
    pop_scope();
    std::vector<lir_view::StmtRef> blk;
    blk.push_back(std::move(mc.temp_scrut_let));
    const auto k = rty ? TypeRef(rty).kind() : LogosType::Kind::Error;
    if (k == LogosType::Kind::Void) {
        lir::SExprStmt es; es.expr = std::move(me_expr);
        blk.push_back(make_stmt_emit(node_line_, std::move(es)));
        for (auto& d : ft_drops) blk.push_back(std::move(d));
        return builder().block_expr(lir_mirror_block(*cur_prog_, blk), nullptr, rty);
    }
    if (k == LogosType::Kind::Never || k == LogosType::Kind::Error)
        return builder().block_expr(lir_mirror_block(*cur_prog_, blk), std::move(me_expr), rty);
    std::string res_var = "__match_res_" + std::to_string(tmp_var_count_++);
    {
        lir::SLet sl;
        sl.name = res_var; sl.type = rty; sl.is_mut = false;
        sl.value = std::move(me_expr);
        blk.push_back(make_stmt_emit(node_line_, std::move(sl)));
    }
    for (auto& d : ft_drops) blk.push_back(std::move(d));
    return builder().block_expr(lir_mirror_block(*cur_prog_, blk), builder().var_ref(res_var, rty), rty);
}

} // namespace logos::compiler
