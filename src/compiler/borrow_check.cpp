// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Borrow checker — affine ownership + reference exclusivity + dangling detection.
//
// Phase 1 — Linear ownership (use-after-move):
//   Move types  — structs with Drop impl and no Copy impl.
//   Copy types  — everything else (primitives, raw pointers, enums, &T, &mut T).
//   A Move variable is consumed on first EVarRef in value position; re-use is an error.
//
// Phase 2 — Borrow exclusivity:
//   &T   (Ref)    — shared borrow: multiple allowed, blocks moves and &mut.
//   &mut T (MutRef) — exclusive borrow: one at a time, blocks moves and all other borrows.
//   Borrows are scoped lexically; they end when the scope containing the let binding ends.
//   Call-site borrows (&x in function args) are transient (released after the call).
//
// Phase 3 — Dangling reference detection:
//   A function returning &T / &mut T must not return a reference to a local variable.
//   Parameters are safe to borrow from; locals are not.
//
// Branch merging: moves (from Phase 1) are propagated conservatively (union).
//   Borrows are scope-local and released by pop_scope, so they don't survive merges.
//
// Loops: outer variables moved inside the body are dead after the loop.

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

struct TypeSets {
    std::unordered_set<std::string> drop_types;
    std::unordered_set<std::string> copy_types;
};

static TypeSets build_type_sets(const lir::LProgram& prog) {
    TypeSets ts;
    auto scan_fns = [&](const std::vector<LFunction>& fns) {
        for (auto& fn : fns)
            if (fn.name.size() > 6 && fn.name.ends_with("__drop"))
                ts.drop_types.insert(fn.name.substr(0, fn.name.size() - 6));
    };
    scan_fns(prog.functions);
    scan_fns(prog.specializations);
    for (auto& sd : prog.structs)
        for (auto& m : sd.methods)
            if (m.name.ends_with("__drop"))
                ts.drop_types.insert(sd.name);
    for (auto& impl : prog.impls)
        if (impl.trait_name == "Copy")
            ts.copy_types.insert(impl.target_type);
    return ts;
}

static bool has_droppable_fields(const LogosType*, const lir::LProgram&, const TypeSets&);

static bool needs_drop(const LogosType* t, const lir::LProgram& prog, const TypeSets& ts) {
    if (!t || t->kind != LogosType::Kind::Struct) return false;
    return ts.drop_types.count(t->struct_name) || has_droppable_fields(t, prog, ts);
}

static bool has_droppable_fields(const LogosType* t, const lir::LProgram& prog,
                                  const TypeSets& ts) {
    if (!t || t->kind != LogosType::Kind::Struct) return false;
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

static bool is_move_type(const LogosType* t, const lir::LProgram& prog, const TypeSets& ts) {
    if (!t || t->kind != LogosType::Kind::Struct) return false;
    if (!needs_drop(t, prog, ts)) return false;
    return !ts.copy_types.count(t->struct_name);
}

// ── Variable state ───────────────────────────────────────────────────────────

struct VarState {
    // Phase 1 — ownership
    bool     moved          = false;
    uint32_t moved_line     = 0;
    // Phase 2 — borrow tracking
    int      shared_borrows = 0;     // # active &T borrows on this var
    bool     mut_borrowed   = false; // has an active &mut borrow
};

using StateMap = std::unordered_map<std::string, VarState>;

// Phase 4 — lifetime provenance.
// For each reference-typed variable, tracks which function parameters it
// ultimately dereferences into (params are guaranteed to outlive the call).
// is_local = true  → at least one path originates from a local variable
//                    (returning such a ref is a dangling reference).
// params.empty() && !is_local → provenance unknown / comes from a global or
//                                a function return value; assume safe to return.
struct RefProv {
    std::unordered_set<std::string> params;    // param names this ref may alias
    bool                            is_local = false;
};

using ProvMap = std::unordered_map<std::string, RefProv>;

static RefProv merge_prov(const RefProv& a, const RefProv& b) {
    RefProv r;
    for (auto& s : a.params) r.params.insert(s);
    for (auto& s : b.params) r.params.insert(s);
    r.is_local = a.is_local || b.is_local;
    return r;
}

static void merge_provs(ProvMap& base, const ProvMap& other) {
    for (auto& [name, p] : other)
        base[name] = merge_prov(base[name], p);
}

static bool is_ref_kind(const LogosType* t) {
    return t && (t->kind == LogosType::Kind::Ref || t->kind == LogosType::Kind::MutRef);
}

struct BorrowRecord {
    std::string target;
    bool        is_mut;
};

struct ScopeFrame {
    std::vector<std::string>  declared;  // vars declared in this scope
    std::vector<BorrowRecord> borrows;   // borrows held in this scope
};

// Merge Phase-1 move state from 'other' into 'base' (union of moved sets).
// Borrows are scope-local and do not survive merges.
static void merge_moves(StateMap& base, const StateMap& other) {
    for (auto& [name, st] : other)
        if (st.moved) base[name] = st;
}

// ── BorrowChecker ───────────────────────────────────────────────────────────

class BorrowChecker {
    SemaResult&          diags_;
    std::string          fn_name_;
    const lir::LProgram& prog_;
    const TypeSets&      ts_;

    StateMap                 states_;
    std::vector<ScopeFrame>  scopes_;
    // Phase 4: provenance tracking for reference-typed variables.
    ProvMap                              prov_;
    std::unordered_set<std::string>      param_names_;
    // param name → lifetime annotation of that param's type (e.g. "'a", "")
    std::unordered_map<std::string, std::string> param_lifetimes_;
    // Phase 3/4: return type of current function.
    const LogosType*         ret_type_ = nullptr;

    void report(uint32_t line, std::string msg) {
        Diag d;
        d.level   = Diag::Level::Error;
        d.context = fn_name_;
        d.message = std::move(msg);
        d.line    = line;
        diags_.diags.push_back(std::move(d));
    }

    // ── Scope management ───────────────────────────────────────────────────

    void push_scope() { scopes_.push_back({}); }

    void pop_scope() {
        if (scopes_.empty()) return;
        auto& frame = scopes_.back();
        // Release borrows held by this scope.
        for (auto& br : frame.borrows) {
            auto it = states_.find(br.target);
            if (it != states_.end()) {
                if (br.is_mut)
                    it->second.mut_borrowed = false;
                else if (it->second.shared_borrows > 0)
                    --it->second.shared_borrows;
            }
        }
        // Remove variables declared in this scope.
        for (auto& name : frame.declared) {
            states_.erase(name);
            prov_.erase(name);
        }
        scopes_.pop_back();
    }

    void declare_var(const std::string& name) {
        states_[name] = VarState{};
        if (!scopes_.empty()) scopes_.back().declared.push_back(name);
    }

    // ── Borrow operations ─────────────────────────────────────────────────

    // Take a borrow of 'target'. Registers it in the current scope for cleanup.
    void take_borrow(const std::string& target, bool is_mut, uint32_t line) {
        auto it = states_.find(target);
        if (it == states_.end()) return;  // unknown / extern
        if (it->second.moved) {
            report(line, std::format(
                "cannot borrow moved value '{}'", target));
            return;
        }
        if (is_mut) {
            if (it->second.mut_borrowed) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: already mutably borrowed", target));
                return;
            }
            if (it->second.shared_borrows > 0) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: {} shared borrow(s) active",
                    target, it->second.shared_borrows));
                return;
            }
            it->second.mut_borrowed = true;
        } else {
            if (it->second.mut_borrowed) {
                report(line, std::format(
                    "cannot borrow '{}' as shared: already mutably borrowed", target));
                return;
            }
            ++it->second.shared_borrows;
        }
        if (!scopes_.empty())
            scopes_.back().borrows.push_back({target, is_mut});
    }

    // ── Ownership operations ───────────────────────────────────────────────

    bool consume(const std::string& name, uint32_t line) {
        auto it = states_.find(name);
        if (it == states_.end()) return true;
        if (it->second.moved) {
            uint32_t prev = it->second.moved_line;
            if (prev)
                report(line, std::format(
                    "use of moved value '{}' (moved on line {})", name, prev));
            else
                report(line, std::format("use of moved value '{}'", name));
            return false;
        }
        if (it->second.mut_borrowed || it->second.shared_borrows > 0) {
            report(line, std::format("cannot move '{}' while it is borrowed", name));
            return false;
        }
        it->second = VarState{true, line};
        return true;
    }

    void check_live(const std::string& name, uint32_t line) {
        auto it = states_.find(name);
        if (it == states_.end()) return;
        if (it->second.moved) {
            uint32_t prev = it->second.moved_line;
            if (prev)
                report(line, std::format(
                    "use of moved value '{}' (moved on line {})", name, prev));
            else
                report(line, std::format("use of moved value '{}'", name));
        }
    }

    // ── Phase 4: provenance of a reference expression ─────────────────────
    //
    // Returns the set of function parameters the expression borrows from.
    // is_local = true  → at least one source is a local variable (dangling if returned).
    // params.empty() && !is_local → unknown/global — assumed safe (e.g. static data,
    //   or result of a function call where we don't track cross-call lifetimes).

    RefProv prov_of(const LExprPtr& e) const {
        if (!e) return {};
        return std::visit([&](auto& k) -> RefProv {
            using K = std::decay_t<decltype(k)>;

            if constexpr (std::is_same_v<K, lir::EVarRef>) {
                // A param variable used directly as a reference value.
                if (param_names_.count(k.name) && is_ref_kind(e->type))
                    return {{k.name}, false};
                // A local ref variable whose provenance we've already computed.
                auto it = prov_.find(k.name);
                if (it != prov_.end()) return it->second;
                return {};   // unknown non-ref or untracked — assume safe
            }
            if constexpr (std::is_same_v<K, lir::EAddrOf>) {
                if (param_names_.count(k.var_name)) return {{k.var_name}, false};
                if (states_.count(k.var_name))      return {{},           true};
                return {};  // &global — safe
            }
            if constexpr (std::is_same_v<K, lir::EFieldRead>)
                return prov_of(k.receiver);
            if constexpr (std::is_same_v<K, lir::EDeref>)
                return prov_of(k.operand);
            if constexpr (std::is_same_v<K, lir::ETupleIndex>)
                return prov_of(k.receiver);
            if constexpr (std::is_same_v<K, lir::ECast>)
                return prov_of(k.operand);
            if constexpr (std::is_same_v<K, lir::EIndexRead>)
                return prov_of(k.receiver);
            if constexpr (std::is_same_v<K, lir::EIfExpr>)
                return merge_prov(prov_of(k.then_val), prov_of(k.else_val));
            // ECall / EMethodCall / EStructLit / literals — value is caller-owned,
            // not a borrowed reference; leave provenance empty (= unknown/safe).
            return {};
        }, e->kind);
    }

    // ── Phase 3 + 4: dangling / lifetime check on return ──────────────────

    void check_return_value(const LExprPtr& e, uint32_t line) {
        if (!ret_type_ || !is_ref_kind(ret_type_)) return;

        RefProv prov = prov_of(e);

        // 1. Definitely local → always dangling.
        if (prov.is_local) {
            std::string src;
            if (auto* a = std::get_if<lir::EAddrOf>(&e->kind))  src = a->var_name;
            else if (auto* v = std::get_if<lir::EVarRef>(&e->kind)) src = v->name;
            report(line, std::format(
                "cannot return reference to local variable '{}': dangling reference",
                src.empty() ? "?" : src));
            return;
        }

        // 2. Explicit lifetime on return type — check sources match.
        const std::string& ret_lt = ret_type_->lifetime;
        if (!ret_lt.empty() && ret_lt != "'_") {
            if (prov.params.empty()) {
                report(line, std::format(
                    "cannot determine provenance of returned reference "
                    "(expected lifetime {})", ret_lt));
                return;
            }
            for (auto& src : prov.params) {
                auto it = param_lifetimes_.find(src);
                const std::string src_lt = (it != param_lifetimes_.end()) ? it->second : "";
                if (src_lt != ret_lt)
                    report(line, std::format(
                        "lifetime mismatch: return type has lifetime {} "
                        "but '{}' has lifetime {}",
                        ret_lt, src, src_lt.empty() ? "(elided)" : src_lt));
            }
            return;
        }

        // 3. Elided return lifetime — must come from at least one param.
        //    (empty prov.params + !is_local = global/unknown, which is safe)
        //    Only an EVarRef or EAddrOf with no param source and no known global
        //    signals a problem we can detect statically.
        if (!prov.params.empty()) return;   // fine — derives from a param
        // prov is empty AND !is_local: either unknown (call result) or global — let it pass.
    }

    // ── Expression visitor ─────────────────────────────────────────────────

    void visit(const LExprPtr& e, bool consuming, uint32_t line);

    // Visit a sequence of call arguments, handling borrows transiently.
    // Each EAddrOf arg creates a call-site borrow (released when scope pops).
    void visit_call_args(const std::vector<LExprPtr>& args, uint32_t line) {
        push_scope();  // call-site borrow scope
        for (auto& a : args) {
            if (a && std::holds_alternative<lir::EAddrOf>(a->kind)) {
                auto& k = std::get<lir::EAddrOf>(a->kind);
                bool is_mut = a->type && a->type->kind == LogosType::Kind::MutRef;
                take_borrow(k.var_name, is_mut, line);
            } else {
                visit(a, /*consuming=*/true, line);
            }
        }
        pop_scope();
    }

    // ── Statement visitor ─────────────────────────────────────────────────

    void visit_block(const LBlock& blk) {
        push_scope();
        for (auto& s : blk.stmts) visit_stmt(s);
        pop_scope();
    }

    // Analyse a loop body: outer variables moved/borrowed inside are propagated.
    // loop_vars are local to the loop iteration.
    void visit_loop_body(const LBlock& body,
                         const std::vector<std::string>& loop_vars = {}) {
        auto pre_s = states_;
        auto pre_p = prov_;
        push_scope();
        for (auto& v : loop_vars) declare_var(v);
        for (auto& s : body.stmts) visit_stmt(s);
        pop_scope();
        // Borrows released by pop_scope; propagate only moves of outer vars.
        // For provenance, merge conservatively (loop may run 0 or more times).
        auto post_s = states_;
        auto post_p = prov_;
        states_ = pre_s;
        prov_   = pre_p;
        for (auto& [name, st] : post_s)
            if (st.moved && pre_s.count(name))
                states_[name] = st;
        merge_provs(prov_, post_p);
    }

    void visit_stmt(const LStmt& stmt) {
        uint32_t ln = stmt.line;
        std::visit([&](auto& s) {
            using S = std::decay_t<decltype(s)>;

            // ── Let binding ──────────────────────────────────────────────
            if constexpr (std::is_same_v<S, SLet>) {
                if (s.value && std::holds_alternative<lir::EAddrOf>(s.value->kind)) {
                    // let r = &x / &mut x — scoped borrow
                    auto& k = std::get<lir::EAddrOf>(s.value->kind);
                    bool is_mut = s.value->type &&
                                  s.value->type->kind == LogosType::Kind::MutRef;
                    take_borrow(k.var_name, is_mut, ln);
                } else if (s.value) {
                    visit(s.value, /*consuming=*/true, ln);
                }
                declare_var(s.name);
                if (is_ref_kind(s.type))
                    prov_[s.name] = prov_of(s.value);

            // ── Assignment ───────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SAssign>) {
                if (s.value && std::holds_alternative<lir::EAddrOf>(s.value->kind)) {
                    auto& k = std::get<lir::EAddrOf>(s.value->kind);
                    bool is_mut = s.value->type &&
                                  s.value->type->kind == LogosType::Kind::MutRef;
                    take_borrow(k.var_name, is_mut, ln);
                } else {
                    visit(s.value, /*consuming=*/true, ln);
                }
                if (states_.count(s.name))
                    states_[s.name] = VarState{};  // re-own

            // ── Return ───────────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SReturn>) {
                if (s.value) {
                    check_return_value(s.value, ln);
                    visit(s.value, /*consuming=*/true, ln);
                }

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
                visit(s.ptr, /*consuming=*/false, ln);
                visit(s.value, /*consuming=*/true, ln);

            // ── delete ptr ───────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SDelete>) {
                visit(s.expr, /*consuming=*/true, ln);

            // ── SDrop — compiler-generated, no-op in borrow checker ───────
            } else if constexpr (std::is_same_v<S, SDrop>) {
                (void)s;

            // ── If / else ────────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SIf>) {
                visit(s.cond, /*consuming=*/true, ln);
                auto saved_s = states_;
                auto saved_p = prov_;
                visit_block(*s.then_);
                auto then_s = states_;
                auto then_p = prov_;
                states_ = saved_s;
                prov_   = saved_p;
                if (s.else_) visit_block(**s.else_);
                merge_moves(states_, then_s);
                merge_provs(prov_,   then_p);

            // ── While loop ───────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SWhile>) {
                visit(s.cond, /*consuming=*/true, ln);
                visit_loop_body(*s.body);

            // ── For range loop ───────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SFor>) {
                visit(s.lo, /*consuming=*/true, ln);
                visit(s.hi, /*consuming=*/true, ln);
                visit_loop_body(*s.body, {s.var});

            // ── Infinite loop ─────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SLoop>) {
                visit_loop_body(*s.body);

            // ── Scoping block ─────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SBlock>) {
                visit_block(*s.body);

            // ── For-each loop ─────────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SForEach>) {
                visit(s.iter, /*consuming=*/false, ln);
                visit_loop_body(*s.body, {s.var});

            // ── Match statement ───────────────────────────────────────────
            } else if constexpr (std::is_same_v<S, SMatch>) {
                visit(s.scrut, /*consuming=*/false, ln);
                auto saved_s = states_;
                auto saved_p = prov_;
                std::optional<StateMap> merged_s;
                std::optional<ProvMap>  merged_p;
                for (auto& arm : s.arms) {
                    states_ = saved_s;
                    prov_   = saved_p;
                    std::visit([&](auto& p) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(p)>,
                                                     PatVariantData>) {
                            for (auto& b : p.bindings) declare_var(b);
                        } else if constexpr (std::is_same_v<std::decay_t<decltype(p)>,
                                                             PatWild>) {
                            if (!p.name.empty() && p.name != "_") declare_var(p.name);
                        }
                    }, arm.pat);
                    if (arm.guard) visit(*arm.guard, /*consuming=*/true, ln);
                    visit_block(*arm.body);
                    if (!merged_s) {
                        merged_s = states_;
                        merged_p = prov_;
                    } else {
                        for (auto& [name, st] : states_)
                            if (st.moved && saved_s.count(name))
                                (*merged_s)[name] = st;
                        merge_provs(*merged_p, prov_);
                    }
                }
                if (merged_s) {
                    for (auto& [name, st] : *merged_s)
                        if (saved_s.count(name)) states_[name] = st;
                    merge_provs(prov_, *merged_p);
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
        scopes_.clear();
        prov_.clear();
        param_names_.clear();
        param_lifetimes_.clear();
        ret_type_ = fn.ret_type;

        push_scope();  // function scope
        for (auto& p : fn.params) {
            declare_var(p.name);
            param_names_.insert(p.name);
            if (is_ref_kind(p.type))
                param_lifetimes_[p.name] = p.type->lifetime;
        }

        visit_block(fn.body);
        pop_scope();
    }
};

// Expression visitor — out-of-line.

void BorrowChecker::visit(const LExprPtr& e, bool consuming, uint32_t line) {
    if (!e) return;
    std::visit([&](auto& k) {
        using K = std::decay_t<decltype(k)>;

        // ── Variable reference ─────────────────────────────────────────
        if constexpr (std::is_same_v<K, EVarRef>) {
            if (consuming && is_move_type(e->type, prog_, ts_))
                consume(k.name, line);
            else
                check_live(k.name, line);

        // ── Address-of: &x or &mut x ──────────────────────────────────
        // When EAddrOf appears directly in visit (not as SLet/SAssign RHS),
        // this is a transient borrow — caller handles scope (visit_call_args).
        // We just verify the source is alive.
        } else if constexpr (std::is_same_v<K, EAddrOf>) {
            check_live(k.var_name, line);

        // ── Dereference: *ptr ──────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EDeref>) {
            visit(k.operand, /*consuming=*/false, line);

        // ── Field read: recv.field ─────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EFieldRead>) {
            visit(k.receiver, /*consuming=*/false, line);

        // ── Index read: arr[i] ─────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EIndexRead>) {
            visit(k.receiver, /*consuming=*/false, line);
            visit(k.index,    /*consuming=*/true,  line);

        // ── Tuple index: t.N ──────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, ETupleIndex>) {
            visit(k.receiver, /*consuming=*/false, line);

        // ── Method call: recv.method(args) ────────────────────────────
        // Receiver is typically &mut self — already wrapped in EAddrOf.
        } else if constexpr (std::is_same_v<K, EMethodCall>) {
            visit(k.receiver, /*consuming=*/false, line);
            visit_call_args(k.args, line);

        // ── Free function call: f(args) ───────────────────────────────
        } else if constexpr (std::is_same_v<K, ECall>) {
            visit_call_args(k.args, line);

        // ── Closure call ───────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EClosureCall>) {
            visit(k.callee, /*consuming=*/false, line);
            visit_call_args(k.args, line);

        // ── Binary / Unary ─────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EBinOp>) {
            visit(k.lhs, /*consuming=*/true, line);
            visit(k.rhs, /*consuming=*/true, line);
        } else if constexpr (std::is_same_v<K, EUnary>) {
            visit(k.operand, /*consuming=*/true, line);

        // ── Cast ───────────────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, ECast>) {
            visit(k.operand, consuming, line);

        // ── Struct literal ─────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EStructLit>) {
            for (auto& [fname, fval] : k.fields)
                visit(fval, /*consuming=*/true, line);

        // ── New: new Foo { ... } ───────────────────────────────────────
        } else if constexpr (std::is_same_v<K, ENew>) {
            for (auto& [fname, fval] : k.fields)
                visit(fval, /*consuming=*/true, line);

        // ── Array literal ──────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EArrLit>) {
            for (auto& elem : k.elems)
                visit(elem, /*consuming=*/true, line);

        // ── Tuple literal ──────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, ETupleLit>) {
            for (auto& elem : k.elems)
                visit(elem, /*consuming=*/true, line);

        // ── Enum literal with payload ──────────────────────────────────
        } else if constexpr (std::is_same_v<K, EEnumLitData>) {
            for (auto& pl : k.payload)
                visit(pl, /*consuming=*/true, line);

        // ── If expression ──────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EIfExpr>) {
            visit(k.cond, /*consuming=*/true, line);
            auto saved_s = states_;
            auto saved_p = prov_;
            visit(k.then_val, consuming, line);
            auto then_s = states_;
            auto then_p = prov_;
            states_ = saved_s;
            prov_   = saved_p;
            visit(k.else_val, consuming, line);
            merge_moves(states_, then_s);
            merge_provs(prov_,   then_p);

        // ── Match expression ───────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EMatchExpr>) {
            visit(k.scrut, /*consuming=*/false, line);
            auto saved_s = states_;
            auto saved_p = prov_;
            std::optional<StateMap> merged_s;
            std::optional<ProvMap>  merged_p;
            for (auto& arm : k.arms) {
                states_ = saved_s;
                prov_   = saved_p;
                if (arm.guard) visit(*arm.guard, /*consuming=*/true, line);
                visit(arm.value, consuming, line);
                if (!merged_s) {
                    merged_s = states_;
                    merged_p = prov_;
                } else {
                    for (auto& [name, st] : states_)
                        if (st.moved && saved_s.count(name))
                            (*merged_s)[name] = st;
                    merge_provs(*merged_p, prov_);
                }
            }
            if (merged_s) {
                for (auto& [name, st] : *merged_s)
                    if (saved_s.count(name)) states_[name] = st;
                merge_provs(prov_, *merged_p);
            }

        // ── Try expression: expr? ──────────────────────────────────────
        } else if constexpr (std::is_same_v<K, ETry>) {
            visit(k.inner, consuming, line);

        // ── Slice ──────────────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, ESliceLit>) {
            visit(k.base, /*consuming=*/false, line);
            visit(k.len,  /*consuming=*/true,  line);
        } else if constexpr (std::is_same_v<K, ESliceIndex>) {
            visit(k.slice, /*consuming=*/false, line);
            visit(k.index, /*consuming=*/true,  line);
        } else if constexpr (std::is_same_v<K, ESliceLen>) {
            visit(k.slice, /*consuming=*/false, line);

        // ── Format call ────────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EFormatCall>) {
            visit(k.fmt, /*consuming=*/false, line);
            visit_call_args(k.args, line);

        // ── Closure box ────────────────────────────────────────────────
        } else if constexpr (std::is_same_v<K, EClosureBox>) {
            if (k.inner) {
                for (auto& cap : k.inner->captures)
                    check_live(cap, line);
            }

        // ── Literals / compile-time nodes — no ownership effects ───────
        } else {
            (void)k;
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
    for (auto& sd : prog.structs)
        for (auto& m : sd.methods)        check(m);
    for (auto& cd : prog.classes)
        for (auto& m : cd.methods)        check(m);

    return prog;
}

} // namespace logos::compiler
