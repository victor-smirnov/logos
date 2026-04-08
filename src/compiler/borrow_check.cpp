// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Borrow checker — Phase 1: linear ownership / use-after-move.
//
// Rules:
//   Copy types  — primitives, raw pointers, arrays, enums, class ptrs.
//   Move types  — struct (all structs are affine; they may own heap resources).
//
// For every Move-typed variable we track two states:
//   Owned  — value is valid and available.
//   Moved  — value has been consumed; further use is a compile error.
//
// A variable is consumed when it appears as a direct EVarRef in a
// value-producing position (function arg, assignment RHS, return) and its
// type is a Move type.  Taking its address (&x) or reading a field (x.field)
// is *not* a consuming use — the variable remains Owned.
//
// Branch merging: if any branch moves a variable, it is considered dead
// after the merge point (conservative / correct).
//
// Loops: the body is analysed once; any outer variable moved inside is
// considered dead after the loop (conservative).

#include <logos/compiler/borrow_check.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/sema.hpp>

#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace logos::compiler {

using namespace lir;

// ── Copy/Move classification ────────────────────────────────────────────────
//
// Mirrors sema's is_move_type() logic:
//   Move iff the type needs_drop (has an explicit Drop impl or droppable fields)
//   AND does not implement Copy.
//
// This is computed once per borrow_check() invocation from LProgram data.

struct TypeSets {
    std::unordered_set<std::string> drop_types;  // struct names with Type__drop function
    std::unordered_set<std::string> copy_types;  // struct names implementing Copy trait
};

static TypeSets build_type_sets(const lir::LProgram& prog) {
    TypeSets ts;

    // Find Drop implementations: any function named "StructName__drop" in prog.
    auto scan_fns = [&](const std::vector<LFunction>& fns) {
        for (auto& fn : fns) {
            if (fn.name.size() > 6 && fn.name.ends_with("__drop"))
                ts.drop_types.insert(fn.name.substr(0, fn.name.size() - 6));
        }
    };
    scan_fns(prog.functions);
    scan_fns(prog.specializations);
    for (auto& sd : prog.structs)
        for (auto& m : sd.methods)
            if (m.name.ends_with("__drop"))
                ts.drop_types.insert(sd.name);

    // Find Copy implementations: impl blocks with trait_name == "Copy".
    for (auto& impl : prog.impls)
        if (impl.trait_name == "Copy")
            ts.copy_types.insert(impl.target_type);

    return ts;
}

// Forward-declare so is_move_type can check droppable fields recursively.
static bool has_droppable_fields(const LogosType* t,
                                  const lir::LProgram& prog,
                                  const TypeSets& ts);

static bool needs_drop(const LogosType* t,
                        const lir::LProgram& prog,
                        const TypeSets& ts) {
    if (!t) return false;
    if (t->kind != LogosType::Kind::Struct) return false;
    return ts.drop_types.count(t->struct_name) ||
           has_droppable_fields(t, prog, ts);
}

static bool has_droppable_fields(const LogosType* t,
                                  const lir::LProgram& prog,
                                  const TypeSets& ts) {
    if (!t || t->kind != LogosType::Kind::Struct) return false;
    // Search in both template defs and specializations.
    auto check = [&](const std::vector<LStructDef>& defs) -> bool {
        for (auto& sd : defs) {
            if (sd.name != t->struct_name) continue;
            for (auto& f : sd.fields)
                if (needs_drop(f.type, prog, ts)) return true;
            return false;
        }
        return false;
    };
    return check(prog.structs) || check(prog.struct_specializations);
}

static bool is_move_type(const LogosType* t,
                          const lir::LProgram& prog,
                          const TypeSets& ts) {
    if (!t || t->kind != LogosType::Kind::Struct) return false;
    if (!needs_drop(t, prog, ts)) return false;          // no Drop → Copy
    return !ts.copy_types.count(t->struct_name);         // Copy overrides Move
}

// ── Ownership state ─────────────────────────────────────────────────────────

struct MoveInfo {
    bool     moved      = false;
    uint32_t moved_line = 0;
};

using OwnerMap = std::unordered_map<std::string, MoveInfo>;

// Merge: any variable moved in 'other' is also marked moved in 'base'.
static void merge_moves(OwnerMap& base, const OwnerMap& other) {
    for (auto& [name, info] : other)
        if (info.moved)
            base[name] = info;  // propagate move
}

// ── BorrowChecker ───────────────────────────────────────────────────────────

class BorrowChecker {
    SemaResult&          diags_;
    std::string          fn_name_;
    const lir::LProgram& prog_;
    const TypeSets&      ts_;

    OwnerMap states_;   // current per-variable ownership

    void report(uint32_t line, std::string msg) {
        Diag d;
        d.level   = Diag::Level::Error;
        d.context = fn_name_;
        d.message = std::move(msg);
        d.line    = line;
        diags_.diags.push_back(std::move(d));
    }

    void declare(const std::string& name) {
        states_[name] = MoveInfo{};         // Owned
    }

    // Mark a variable as consumed.  Returns false if it was already moved.
    bool consume(const std::string& name, uint32_t line) {
        auto it = states_.find(name);
        if (it == states_.end()) return true;   // unknown (extern / outer scope)
        if (it->second.moved) {
            uint32_t prev = it->second.moved_line;
            if (prev)
                report(line, std::format(
                    "use of moved value '{}' (moved on line {})", name, prev));
            else
                report(line, std::format("use of moved value '{}'", name));
            return false;
        }
        it->second = MoveInfo{true, line};
        return true;
    }

    // Check a variable is live (for borrows / field reads).
    void check_live(const std::string& name, uint32_t line) {
        auto it = states_.find(name);
        if (it == states_.end()) return;        // unknown
        if (it->second.moved) {
            uint32_t prev = it->second.moved_line;
            if (prev)
                report(line, std::format(
                    "use of moved value '{}' (moved on line {})", name, prev));
            else
                report(line, std::format("use of moved value '{}'", name));
        }
    }

    // ── Expression visitor ──────────────────────────────────────────────────
    //
    // consuming = true  → the value produced by this expr is being moved out
    //                     (function arg, assignment RHS, return value).
    //             false → the expr is accessed but ownership is not transferred
    //                     (borrow, field read receiver, index receiver, …).
    //
    // An EVarRef(x) in consuming position with a Move type consumes x.

    void visit(const LExprPtr& e, bool consuming, uint32_t line);

    void visit_args(const std::vector<LExprPtr>& args, uint32_t line) {
        for (auto& a : args) visit(a, /*consuming=*/true, line);
    }

    // ── Statement visitor ───────────────────────────────────────────────────

    void visit_block(const LBlock& blk) {
        for (auto& s : blk.stmts) visit_stmt(s);
    }

    // Analyse a loop body conservatively:
    //  – Any outer variable moved inside the body is dead after the loop.
    //  – loop_vars are local to the loop; they are removed from state afterwards.
    void visit_loop_body(const LBlock& body,
                         const std::vector<std::string>& loop_vars = {}) {
        auto pre = states_;
        for (auto& v : loop_vars) declare(v);
        visit_block(body);
        auto post = states_;
        states_ = pre;
        // Propagate moves of pre-existing (outer) variables.
        for (auto& [name, info] : post) {
            if (info.moved && pre.count(name))
                states_[name] = info;
        }
    }

    void visit_stmt(const LStmt& stmt) {
        uint32_t ln = stmt.line;
        std::visit([&](auto& s) {
            using S = std::decay_t<decltype(s)>;

            // ── Let binding ──────────────────────────────────────────────
            if constexpr (std::is_same_v<S, SLet>) {
                visit(s.value, /*consuming=*/true, ln);
                declare(s.name);        // binding owns the value

            // ── Assignment ───────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SAssign>) {
                visit(s.value, /*consuming=*/true, ln);
                // Re-own the variable (re-assignment restores liveness).
                if (states_.count(s.name))
                    states_[s.name] = MoveInfo{};

            // ── Return ───────────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SReturn>) {
                if (s.value) visit(s.value, /*consuming=*/true, ln);

            // ── Expression statement ─────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SExprStmt>) {
                visit(s.expr, /*consuming=*/true, ln);

            // ── Field write: recv.field = value ──────────────────────────
            } else if constexpr (std::is_same_v<S, SFieldWrite>) {
                check_live(s.receiver, ln);
                visit(s.value, /*consuming=*/true, ln);

            // ── Index write: arr[i] = value ──────────────────────────────
            } else if constexpr (std::is_same_v<S, SIndexWrite>) {
                check_live(s.arr, ln);
                visit(s.index, /*consuming=*/true, ln);
                visit(s.value, /*consuming=*/true, ln);

            // ── Field-index write: recv.field[i] = value ─────────────────
            } else if constexpr (std::is_same_v<S, SFieldIndexWrite>) {
                check_live(s.receiver, ln);
                visit(s.index, /*consuming=*/true, ln);
                visit(s.value, /*consuming=*/true, ln);

            // ── Deref-field write: (*recv).field = value ─────────────────
            } else if constexpr (std::is_same_v<S, SDerefFieldWrite>) {
                check_live(s.receiver, ln);
                visit(s.value, /*consuming=*/true, ln);

            // ── Deref write: *ptr = value ─────────────────────────────────
            } else if constexpr (std::is_same_v<S, SDerefWrite>) {
                // The pointer is accessed (not moved); the value is consumed.
                visit(s.ptr, /*consuming=*/false, ln);
                visit(s.value, /*consuming=*/true, ln);

            // ── delete ptr ───────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SDelete>) {
                visit(s.expr, /*consuming=*/true, ln);

            // ── Auto-drop at scope exit ───────────────────────────────────
            // SDrop is compiler-generated and inserted before SReturn by sema.
            // The return value is computed at runtime before the drop fires, so
            // SDrop must NOT mark the variable as moved in the borrow checker —
            // doing so would produce false positives for "return x.field" patterns.
            // The borrow checker only tracks explicit (user-visible) moves.
            } else if constexpr (std::is_same_v<S, SDrop>) {
                (void)s;  // no-op

            // ── If / else ────────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SIf>) {
                visit(s.cond, /*consuming=*/true, ln);
                auto saved = states_;
                visit_block(*s.then_);
                auto then_st = states_;
                states_ = saved;
                if (s.else_) visit_block(**s.else_);
                // Merge: vars moved in then-branch are dead.
                merge_moves(states_, then_st);

            // ── While loop ───────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SWhile>) {
                visit(s.cond, /*consuming=*/true, ln);
                visit_loop_body(*s.body);

            // ── For range loop ───────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SFor>) {
                visit(s.lo, /*consuming=*/true, ln);
                visit(s.hi, /*consuming=*/true, ln);
                visit_loop_body(*s.body, {s.var});   // var is Copy (i64)

            // ── Infinite loop ─────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SLoop>) {
                visit_loop_body(*s.body);

            // ── Scoping block ─────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SBlock>) {
                visit_block(*s.body);

            // ── For-each loop ─────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SForEach>) {
                // The iterated collection is borrowed, not consumed.
                visit(s.iter, /*consuming=*/false, ln);
                visit_loop_body(*s.body, {s.var});

            // ── Match statement ───────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SMatch>) {
                // Scrutinee is accessed (not consumed by the match itself).
                visit(s.scrut, /*consuming=*/false, ln);
                auto saved = states_;
                std::optional<OwnerMap> merged;
                for (auto& arm : s.arms) {
                    states_ = saved;
                    // Declare pattern bindings as Owned inside this arm.
                    std::visit([&](auto& p) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(p)>,
                                                     PatVariantData>) {
                            for (auto& b : p.bindings) declare(b);
                        } else if constexpr (std::is_same_v<std::decay_t<decltype(p)>,
                                                             PatWild>) {
                            if (!p.name.empty() && p.name != "_") declare(p.name);
                        }
                    }, arm.pat);
                    if (arm.guard)
                        visit(*arm.guard, /*consuming=*/true, ln);
                    visit_block(*arm.body);
                    if (!merged) {
                        merged = states_;
                    } else {
                        // Union of moves across arms (for outer-scope vars only).
                        for (auto& [name, info] : states_)
                            if (info.moved && saved.count(name))
                                (*merged)[name] = info;
                    }
                }
                if (merged) {
                    // Apply merged moves back, keeping outer-scope vars.
                    for (auto& [name, info] : *merged)
                        if (saved.count(name)) states_[name] = info;
                }
            }
            // SBreak, SContinue — no variable effects.
        }, stmt.kind);
    }

public:
    BorrowChecker(SemaResult& diags, std::string fn_name,
                  const lir::LProgram& prog, const TypeSets& ts)
        : diags_(diags), fn_name_(std::move(fn_name)), prog_(prog), ts_(ts) {}

    void check(const LFunction& fn) {
        states_.clear();
        // Parameters start Owned.
        for (auto& p : fn.params)
            declare(p.name);
        visit_block(fn.body);
    }
};

// Expression visitor — defined out-of-line so it can call visit_stmt via
// forward declaration (EIfExpr, EMatchExpr need to visit blocks).

void BorrowChecker::visit(const LExprPtr& e, bool consuming, uint32_t line) {
    if (!e) return;
    std::visit([&](auto& k) {
        using K = std::decay_t<decltype(k)>;

        // ── Variable reference ────────────────────────────────────────────
        if constexpr (std::is_same_v<K, EVarRef>) {
            if (consuming && is_move_type(e->type, prog_, ts_))
                consume(k.name, line);
            else
                check_live(k.name, line);

        // ── Address-of: &x or &mut x ─────────────────────────────────────
        // Creates a borrow — x remains Owned, but must be live.
        } else if constexpr (std::is_same_v<K, EAddrOf>) {
            check_live(k.var_name, line);

        // ── Dereference: *ptr ─────────────────────────────────────────────
        // Dereferencing accesses through the pointer; the pointer is not moved.
        } else if constexpr (std::is_same_v<K, EDeref>) {
            visit(k.operand, /*consuming=*/false, line);

        // ── Field read: recv.field ────────────────────────────────────────
        // Accessing a field does not consume the struct (Phase 1 simplification).
        } else if constexpr (std::is_same_v<K, EFieldRead>) {
            visit(k.receiver, /*consuming=*/false, line);

        // ── Index read: arr[i] or ptr[i] ─────────────────────────────────
        } else if constexpr (std::is_same_v<K, EIndexRead>) {
            visit(k.receiver, /*consuming=*/false, line);
            visit(k.index,    /*consuming=*/true,  line);

        // ── Tuple index: t.N ──────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, ETupleIndex>) {
            visit(k.receiver, /*consuming=*/false, line);

        // ── Method call: recv.method(args...) ─────────────────────────────
        // Receiver is accessed (typically &mut self — already wrapped in EAddrOf
        // by sema), not consumed.
        } else if constexpr (std::is_same_v<K, EMethodCall>) {
            visit(k.receiver, /*consuming=*/false, line);
            visit_args(k.args, line);

        // ── Free function call: f(args...) ────────────────────────────────
        } else if constexpr (std::is_same_v<K, ECall>) {
            visit_args(k.args, line);

        // ── Closure call ──────────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EClosureCall>) {
            visit(k.callee, /*consuming=*/false, line);
            visit_args(k.args, line);

        // ── Binary / Unary ────────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EBinOp>) {
            visit(k.lhs, /*consuming=*/true, line);
            visit(k.rhs, /*consuming=*/true, line);
        } else if constexpr (std::is_same_v<K, EUnary>) {
            visit(k.operand, /*consuming=*/true, line);

        // ── Cast ──────────────────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, ECast>) {
            visit(k.operand, consuming, line);

        // ── Struct literal: Vec { ptr: p, len: 0, cap: 0 } ───────────────
        } else if constexpr (std::is_same_v<K, EStructLit>) {
            for (auto& [fname, fval] : k.fields)
                visit(fval, /*consuming=*/true, line);

        // ── New: new Foo { ... } ──────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, ENew>) {
            for (auto& [fname, fval] : k.fields)
                visit(fval, /*consuming=*/true, line);

        // ── Array literal ─────────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EArrLit>) {
            for (auto& elem : k.elems)
                visit(elem, /*consuming=*/true, line);

        // ── Tuple literal ─────────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, ETupleLit>) {
            for (auto& elem : k.elems)
                visit(elem, /*consuming=*/true, line);

        // ── Enum literal with payload ─────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EEnumLitData>) {
            for (auto& pl : k.payload)
                visit(pl, /*consuming=*/true, line);

        // ── If expression ─────────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EIfExpr>) {
            visit(k.cond, /*consuming=*/true, line);
            auto saved = states_;
            visit(k.then_val, consuming, line);
            auto then_st = states_;
            states_ = saved;
            visit(k.else_val, consuming, line);
            merge_moves(states_, then_st);

        // ── Match expression ──────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EMatchExpr>) {
            visit(k.scrut, /*consuming=*/false, line);
            auto saved = states_;
            std::optional<OwnerMap> merged;
            for (auto& arm : k.arms) {
                states_ = saved;
                if (arm.guard)
                    visit(*arm.guard, /*consuming=*/true, line);
                visit(arm.value, consuming, line);
                if (!merged) {
                    merged = states_;
                } else {
                    for (auto& [name, info] : states_)
                        if (info.moved && saved.count(name))
                            (*merged)[name] = info;
                }
            }
            if (merged) {
                for (auto& [name, info] : *merged)
                    if (saved.count(name)) states_[name] = info;
            }

        // ── Try expression: expr? ─────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, ETry>) {
            visit(k.inner, consuming, line);

        // ── Slice ─────────────────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, ESliceLit>) {
            visit(k.base, /*consuming=*/false, line);  // base is borrowed
            visit(k.len,  /*consuming=*/true,  line);
        } else if constexpr (std::is_same_v<K, ESliceIndex>) {
            visit(k.slice, /*consuming=*/false, line);
            visit(k.index, /*consuming=*/true,  line);
        } else if constexpr (std::is_same_v<K, ESliceLen>) {
            visit(k.slice, /*consuming=*/false, line);

        // ── Format call ───────────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EFormatCall>) {
            visit(k.fmt, /*consuming=*/false, line);
            visit_args(k.args, line);

        // ── Closure box ───────────────────────────────────────────────────
        // Captures are borrows; check they're live.
        } else if constexpr (std::is_same_v<K, EClosureBox>) {
            if (k.inner) {
                for (auto& cap : k.inner->captures)
                    check_live(cap, line);
            }

        // ── Literals and compile-time constants — no ownership effects ────
        // ELitInt, ELitBool, ELitStr, EEnumLit, ESizeOf, EPackExpand
        } else {
            (void)k;  // no variable references
        }
    }, e->kind);
}

// ── Pass entry point ────────────────────────────────────────────────────────

lir::LProgram borrow_check(lir::LProgram prog) {
    const TypeSets ts = build_type_sets(prog);

    auto check = [&](const LFunction& fn) {
        if (fn.is_extern)             return;
        if (!fn.type_params.empty())  return;
        BorrowChecker(prog.diags, "fn " + fn.name, prog, ts).check(fn);
    };

    for (auto& fn : prog.functions)       check(fn);
    for (auto& fn : prog.specializations) check(fn);

    return prog;
}

} // namespace logos::compiler
