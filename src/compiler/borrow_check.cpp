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
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/lir_view.hpp>
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
    auto register_drop_symbol = [&](std::string_view sym) {
        if (auto p = sym.find("__drop"); p != std::string_view::npos)
            ts.drop_types.insert(std::string(sym.substr(0, p)));
    };
    auto scan_fns = [&](const std::vector<LFunctionPtr>& fns) {
        for (auto& fn : fns)
            register_drop_symbol(fn->name);
    };
    scan_fns(prog.functions);
    scan_fns(prog.specializations);
    for (auto& sd : prog.structs)
        for (auto& m : sd.methods)
            register_drop_symbol(m->name);
    for (auto& impl : prog.impls)
        if (impl.trait_name == "Copy")
            ts.copy_types.insert(impl.target_type);
    return ts;
}

static bool has_droppable_fields(TypeRef, const lir::LProgram&, const TypeSets&);

static bool needs_drop(TypeRef t, const lir::LProgram& prog, const TypeSets& ts) {
    if (!t || t.kind() != LogosType::Kind::Struct) return false;
    return ts.drop_types.count(std::string(t.struct_name())) || has_droppable_fields(t, prog, ts);
}

static bool has_droppable_fields(TypeRef t, const lir::LProgram& prog,
                                  const TypeSets& ts) {
    if (!t || t.kind() != LogosType::Kind::Struct) return false;
    auto check = [&](const std::vector<LStructDef>& defs) -> bool {
        for (auto& sd : defs) {
            if (sd.name != t.struct_name()) continue;
            for (auto& f : sd.fields)
                if (needs_drop(f.type, prog, ts)) return true;
            return false;
        }
        return false;
    };
    return check(prog.structs) || check(prog.struct_specializations);
}

static bool is_move_type(TypeRef t, const lir::LProgram& prog, const TypeSets& ts) {
    if (!t || t.kind() != LogosType::Kind::Struct) return false;
    if (!needs_drop(t, prog, ts)) return false;
    return !ts.copy_types.count(std::string(t.struct_name()));
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

static bool is_ref_kind(TypeRef t) {
    return t && (t.kind() == LogosType::Kind::Ref || t.kind() == LogosType::Kind::MutRef);
}

static bool is_mut_ref(TypeRef t) {
    return t && t.kind() == LogosType::Kind::MutRef;
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
    // Declared lifetime parameters of the current function (e.g. ["'a", "'b"]).
    std::vector<std::string>             fn_lifetime_params_;
    // Phase 3/4: return type of current function.
    TypeRef         ret_type_ = nullptr;

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
        if (it->second.mut_borrowed)
            report(line, std::format(
                "cannot use '{}' while it is mutably borrowed", name));
    }

    // ── Phase 4: provenance of a reference expression ─────────────────────
    //
    // Returns the set of function parameters the expression borrows from.
    // is_local = true  → at least one source is a local variable (dangling if returned).
    // params.empty() && !is_local → unknown/global — assumed safe (e.g. static data,
    //   or result of a function call where we don't track cross-call lifetimes).

    lir_view::ExprRef expr_ref(const LExprPtr& e) const {
        if (!e || e->mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::ExprRef(prog_.type_pool.arena(), e->mirror_offset_);
    }

    lir_view::StmtRef stmt_ref(const LStmt& s) const {
        if (s.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::StmtRef(prog_.type_pool.arena(), s.mirror_offset_);
    }

    const LBlock* block_ptr(lir_view::BlockRef br) const {
        if (!br) return nullptr;
        auto& m = prog_.mirror_table->block_by_offset;
        auto it = m.find(br.offset().value());
        return it == m.end() ? nullptr : it->second;
    }

    lir_view::PatRef pat_ref(const Pattern& p) const {
        auto& tbl = *prog_.mirror_table;
        auto it = tbl.pat.find(&p);
        if (it == tbl.pat.end()) return {};
        return lir_view::PatRef(prog_.type_pool.arena(), it->second);
    }

    // Match arm pattern bindings: PatVariantData injects each binding name into
    // scope; PatWild may also bind (when name is non-empty and not "_").
    void declare_pat_bindings(lir_view::PatRef pr) {
        if (!pr) return;
        using Code = lir_schema::pat::Code;
        switch (pr.kind()) {
            case Code::VariantData: {
                lir_view::PatVariantDataView{pr}.each_binding([&](std::string_view b) {
                    declare_var(std::string(b));
                });
                break;
            }
            case Code::Wild: {
                std::string n(lir_view::PatWildView{pr}.name());
                if (!n.empty() && n != "_") declare_var(n);
                break;
            }
            default: break;
        }
    }
    void declare_pat_bindings(const Pattern& p) {
        declare_pat_bindings(pat_ref(p));
    }

    RefProv prov_of(lir_view::ExprRef e) const {
        if (!e) return {};
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        const auto* pool = prog_.type_pool.impl();

        switch (e.kind()) {
            case Code::VarRef: {
                EVarRefView v{e};
                std::string name(v.name());
                if (param_names_.count(name) && is_ref_kind(e.type(pool)))
                    return {{name}, false};
                auto it = prov_.find(name);
                if (it != prov_.end()) return it->second;
                return {};
            }
            case Code::AddrOf: {
                EAddrOfView v{e};
                std::string name(v.var_name());
                if (param_names_.count(name)) return {{name}, false};
                if (states_.count(name))      return {{},     true};
                return {};
            }
            case Code::FieldRead:
                return prov_of(EFieldReadView{e}.receiver());
            case Code::Deref:
                return prov_of(EDerefView{e}.operand());
            case Code::TupleIndex:
                return prov_of(ETupleIndexView{e}.receiver());
            case Code::Cast:
                return prov_of(ECastView{e}.operand());
            case Code::IndexRead:
                return prov_of(EIndexReadView{e}.receiver());
            case Code::IfExpr: {
                EIfExprView v{e};
                return merge_prov(prov_of(v.then_val()), prov_of(v.else_val()));
            }
            case Code::BlockExpr:
                return prov_of(EBlockExprView{e}.result());
            case Code::MatchExpr: {
                RefProv merged = {};
                EMatchExprView{e}.each_arm([&](EMatchArmRef arm) {
                    merged = merge_prov(merged, prov_of(arm.value()));
                });
                return merged;
            }
            default:
                // ECall / EMethodCall / EStructLit / literals — value is caller-owned,
                // not a borrowed reference; leave provenance empty (= unknown/safe).
                return {};
        }
    }

    RefProv prov_of(const LExprPtr& e) const {
        if (!e) return {};
        return prov_of(expr_ref(e));
    }

    // ── Phase 3 + 4: dangling / lifetime check on return ──────────────────

    void check_return_value(lir_view::ExprRef er, uint32_t line) {
        if (!ret_type_ || !is_ref_kind(ret_type_)) return;

        RefProv prov = prov_of(er);

        // 1. Definitely local → always dangling.
        if (prov.is_local) {
            std::string src;
            if (er) {
                using Code = lir_schema::expr::Code;
                if (er.kind() == Code::AddrOf)
                    src = std::string(lir_view::EAddrOfView{er}.var_name());
                else if (er.kind() == Code::VarRef)
                    src = std::string(lir_view::EVarRefView{er}.name());
            }
            report(line, std::format(
                "cannot return reference to local variable '{}': dangling reference",
                src.empty() ? "?" : src));
            return;
        }

        // 2. Explicit lifetime on return type — check sources match.
        const std::string ret_lt(TypeRef(ret_type_).lifetime());
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

        // 3. Elided / '_ return lifetime — apply Rust elision rules.
        //    Rule: if exactly one ref-typed parameter (with any lifetime) exists,
        //    the return must derive from that parameter.
        //    With multiple ref params: ambiguous — only check non-local provenance.
        if (!prov.params.empty()) {
            // Has param source(s). Apply elision rule if applicable.
            if (param_lifetimes_.size() == 1) {
                const std::string& sole_param = param_lifetimes_.begin()->first;
                if (!prov.params.count(sole_param)) {
                    report(line, std::format(
                        "lifetime elision: return reference must derive from '{}' "
                        "(the only reference parameter)", sole_param));
                }
            }
            // else: multiple ref params — can't disambiguate, allow any param source.
            return;
        }
        // prov.params empty && !is_local: call result, global, or untracked.
        // If we have ref params but return doesn't trace to any: suspicious,
        // but only report when we can clearly see an EVarRef or EAddrOf with no match.
        if (!param_lifetimes_.empty()) {
            // There are ref params but we can't trace the return to any of them.
            // This can happen for complex expressions — be conservative, don't error.
        }
        // Empty params and no local: unknown provenance (e.g. function return) — safe.
    }

    // ── Expression visitor ─────────────────────────────────────────────────

    void visit(lir_view::ExprRef e, bool consuming, uint32_t line);
    void visit(const LExprPtr& e, bool consuming, uint32_t line) {
        if (!e) return;
        visit(expr_ref(e), consuming, line);
    }

    // Take scoped borrows for all EAddrOf nodes reachable through a ref
    // expression.  Handles the case where the ref is formed conditionally:
    //   let r = if c { &mut x } else { &mut y };   ← both x and y must be
    //   let r = match tag { A => &x, _ => &y };      borrowed for the scope.
    // For non-borrow sub-expressions (condition of if, scrutinee of match,
    // function calls, etc.) we fall through to a regular visit().
    void take_ref_borrows(lir_view::ExprRef e, uint32_t line) {
        if (!e) return;
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        const auto* pool = prog_.type_pool.impl();

        switch (e.kind()) {
            case Code::AddrOf: {
                EAddrOfView v{e};
                take_borrow(std::string(v.var_name()), is_mut_ref(e.type(pool)), line);
                break;
            }
            case Code::IfExpr: {
                EIfExprView v{e};
                visit(v.cond(), /*consuming=*/true, line);
                take_ref_borrows(v.then_val(), line);
                take_ref_borrows(v.else_val(), line);
                break;
            }
            case Code::MatchExpr: {
                EMatchExprView v{e};
                visit(v.scrut(), /*consuming=*/false, line);
                v.each_arm([&](EMatchArmRef arm) {
                    if (auto g = arm.guard()) visit(g, /*consuming=*/true, line);
                    take_ref_borrows(arm.value(), line);
                });
                break;
            }
            case Code::BlockExpr: {
                EBlockExprView v{e};
                if (auto br = v.block()) {
                    auto it = prog_.mirror_table->block_by_offset.find(
                        br.offset().value());
                    if (it != prog_.mirror_table->block_by_offset.end())
                        visit_block(*it->second);
                }
                take_ref_borrows(v.result(), line);
                break;
            }
            default:
                // EVarRef (ref param forwarded), ECall, EMethodCall, etc.
                visit(e, /*consuming=*/true, line);
                break;
        }
    }
    void take_ref_borrows(const LExprPtr& e, uint32_t line) {
        if (!e) return;
        take_ref_borrows(expr_ref(e), line);
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
        auto sr = stmt_ref(stmt);
        if (!sr) return;
        using namespace lir_view;
        using Code = lir_schema::stmt::Code;
        const auto* pool = prog_.type_pool.impl();

        switch (sr.kind()) {
            // ── Let binding ──────────────────────────────────────────────
            case Code::Let: {
                SLetView v{sr};
                auto val = v.value();
                auto t   = v.type(pool);
                std::string name(v.name());
                if (val && is_ref_kind(t)) {
                    take_ref_borrows(val, ln);
                } else if (val) {
                    visit(val, /*consuming=*/true, ln);
                }
                declare_var(name);
                if (is_ref_kind(t))
                    prov_[name] = prov_of(val);
                else if (t && !t.lifetime_args().empty() &&
                         (t.kind() == LogosType::Kind::Struct ||
                          t.kind() == LogosType::Kind::ZonedStruct))
                    prov_[name] = prov_of(val);  // struct<'z> borrows through lifetime
                break;
            }

            // ── Assignment ───────────────────────────────────────────────
            case Code::Assign: {
                SAssignView v{sr};
                auto val = v.value();
                std::string name(v.name());
                bool is_ref_assign = val &&
                    (prov_.count(name) || is_ref_kind(val.type(pool)));
                if (is_ref_assign) {
                    take_ref_borrows(val, ln);
                } else if (val) {
                    visit(val, /*consuming=*/true, ln);
                }
                if (states_.count(name))
                    states_[name] = VarState{};  // re-own
                if (is_ref_assign)
                    prov_[name] = prov_of(val);
                break;
            }

            // ── Return ───────────────────────────────────────────────────
            case Code::Return: {
                if (auto val = SReturnView{sr}.value()) {
                    check_return_value(val, ln);
                    visit(val, /*consuming=*/true, ln);
                }
                break;
            }

            // ── Expression statement ─────────────────────────────────────
            case Code::ExprStmt:
                visit(SExprStmtView{sr}.expr(), /*consuming=*/true, ln);
                break;

            // ── Field write: recv.field = value ──────────────────────────
            case Code::FieldWrite: {
                SFieldWriteView v{sr};
                check_live(std::string(v.receiver()), ln);
                visit(v.value(), /*consuming=*/true, ln);
                break;
            }

            // ── Index write: arr[i] = value ──────────────────────────────
            case Code::IndexWrite: {
                SIndexWriteView v{sr};
                check_live(std::string(v.arr()), ln);
                visit(v.index(), /*consuming=*/true, ln);
                visit(v.value(), /*consuming=*/true, ln);
                break;
            }

            // ── Field-index write: recv.field[i] = value ─────────────────
            case Code::FieldIndexWrite: {
                SFieldIndexWriteView v{sr};
                check_live(std::string(v.receiver()), ln);
                visit(v.index(), /*consuming=*/true, ln);
                visit(v.value(), /*consuming=*/true, ln);
                break;
            }

            // ── Chain field write: recv.mid.field = value ────────────────
            case Code::ChainFieldWrite: {
                SChainFieldWriteView v{sr};
                check_live(std::string(v.receiver()), ln);
                visit(v.value(), /*consuming=*/true, ln);
                break;
            }

            // ── Deref-field write: (*recv).field = value ─────────────────
            case Code::DerefFieldWrite: {
                SDerefFieldWriteView v{sr};
                check_live(std::string(v.receiver()), ln);
                visit(v.value(), /*consuming=*/true, ln);
                break;
            }

            // ── Deref write: *ptr = value ─────────────────────────────────
            case Code::DerefWrite: {
                SDerefWriteView v{sr};
                visit(v.ptr(),   /*consuming=*/false, ln);
                visit(v.value(), /*consuming=*/true,  ln);
                break;
            }

            // ── Tuple field write: var.N = value ──────────────────────────
            case Code::TupleWrite: {
                STupleWriteView v{sr};
                check_live(std::string(v.receiver()), ln);
                visit(v.value(), /*consuming=*/true, ln);
                break;
            }

            // ── delete ptr ───────────────────────────────────────────────
            case Code::Delete:
                visit(SDeleteView{sr}.expr(), /*consuming=*/true, ln);
                break;

            // ── SDrop — compiler-generated, no-op in borrow checker ───────
            case Code::Drop:
                break;

            // ── If / else ────────────────────────────────────────────────
            case Code::If: {
                SIfView v{sr};
                visit(v.cond(), /*consuming=*/true, ln);
                auto saved_s = states_;
                auto saved_p = prov_;
                if (auto then_b = block_ptr(v.then_block())) visit_block(*then_b);
                auto then_s = states_;
                auto then_p = prov_;
                states_ = saved_s;
                prov_   = saved_p;
                if (auto else_b = block_ptr(v.else_block())) visit_block(*else_b);
                merge_moves(states_, then_s);
                merge_provs(prov_,   then_p);
                break;
            }

            // ── While loop ───────────────────────────────────────────────
            case Code::While: {
                SWhileView v{sr};
                visit(v.cond(), /*consuming=*/true, ln);
                if (auto b = block_ptr(v.body())) visit_loop_body(*b);
                break;
            }

            // ── For range loop ───────────────────────────────────────────
            case Code::For: {
                SForView v{sr};
                visit(v.lo(), /*consuming=*/true, ln);
                visit(v.hi(), /*consuming=*/true, ln);
                if (auto b = block_ptr(v.body()))
                    visit_loop_body(*b, {std::string(v.var())});
                break;
            }

            // ── Infinite loop ─────────────────────────────────────────────
            case Code::Loop:
                if (auto b = block_ptr(SLoopView{sr}.body())) visit_loop_body(*b);
                break;

            // ── Scoping block ─────────────────────────────────────────────
            case Code::Block:
                if (auto b = block_ptr(SBlockView{sr}.body())) visit_block(*b);
                break;

            // ── For-each loop ─────────────────────────────────────────────
            case Code::ForEach: {
                SForEachView v{sr};
                visit(v.iter(), /*consuming=*/false, ln);
                if (auto b = block_ptr(v.body()))
                    visit_loop_body(*b, {std::string(v.var())});
                break;
            }

            // ── Match statement ───────────────────────────────────────────
            case Code::Match: {
                SMatchView v{sr};
                visit(v.scrut(), /*consuming=*/false, ln);
                auto saved_s = states_;
                auto saved_p = prov_;
                std::optional<StateMap> merged_s;
                std::optional<ProvMap>  merged_p;
                v.each_arm([&](EMatchArmRef arm) {
                    states_ = saved_s;
                    prov_   = saved_p;
                    push_scope();
                    declare_pat_bindings(arm.pat());
                    if (auto g = arm.guard()) visit(g, /*consuming=*/true, ln);
                    if (auto body = block_ptr(arm.body())) visit_block(*body);
                    pop_scope();
                    if (!merged_s) {
                        merged_s = states_;
                        merged_p = prov_;
                    } else {
                        for (auto& [name, st] : states_)
                            if (st.moved && saved_s.count(name))
                                (*merged_s)[name] = st;
                        merge_provs(*merged_p, prov_);
                    }
                });
                if (merged_s) {
                    for (auto& [name, st] : *merged_s)
                        if (saved_s.count(name)) states_[name] = st;
                    merge_provs(prov_, *merged_p);
                }
                break;
            }

            // SBreak, SContinue, LetElse — no variable effects in this pass.
            default:
                break;
        }
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
        fn_lifetime_params_ = fn.lifetime_params;
        ret_type_ = fn.ret_type;

        push_scope();  // function scope
        for (auto& p : fn.params) {
            declare_var(p.name);
            param_names_.insert(p.name);
            if (is_ref_kind(p.type))
                param_lifetimes_[p.name] = std::string(TypeRef(p.type).lifetime());
        }

        visit_block(fn.body);
        pop_scope();
    }
};

// Expression visitor — out-of-line.

void BorrowChecker::visit(lir_view::ExprRef e, bool consuming, uint32_t line) {
    if (!e) return;
    using namespace lir_view;
    using Code = lir_schema::expr::Code;
    const auto* pool = prog_.type_pool.impl();

    // Helper: visit a sequence of call arguments via per-view each_arg.
    // EAddrOf args (including those nested in if/match) create call-site
    // borrows released when the scope pops after the call.
    auto visit_args = [&](auto&& view) {
        push_scope();  // call-site borrow scope
        view.each_arg([&](ExprRef a) {
            if (a && is_ref_kind(a.type(pool))) take_ref_borrows(a, line);
            else                                visit(a, /*consuming=*/true, line);
        });
        pop_scope();
    };

    switch (e.kind()) {
        // ── Variable reference ─────────────────────────────────────────
        case Code::VarRef: {
            EVarRefView v{e};
            std::string name(v.name());
            if (consuming && is_move_type(e.type(pool), prog_, ts_))
                consume(name, line);
            else
                check_live(name, line);
            break;
        }

        // ── Address-of: &x or &mut x ──────────────────────────────────
        // When EAddrOf appears directly in visit (not as SLet/SAssign RHS),
        // this is a transient borrow — caller handles scope. We just verify
        // the source is alive.
        case Code::AddrOf: {
            check_live(std::string(EAddrOfView{e}.var_name()), line);
            break;
        }

        // ── Dereference: *ptr ──────────────────────────────────────────
        case Code::Deref:
            visit(EDerefView{e}.operand(), /*consuming=*/false, line);
            break;

        // ── Field read: recv.field ─────────────────────────────────────
        case Code::FieldRead:
            visit(EFieldReadView{e}.receiver(), /*consuming=*/false, line);
            break;

        // ── Index read: arr[i] ─────────────────────────────────────────
        case Code::IndexRead: {
            EIndexReadView v{e};
            visit(v.receiver(), /*consuming=*/false, line);
            visit(v.index(),    /*consuming=*/true,  line);
            break;
        }

        // ── Tuple index: t.N ──────────────────────────────────────────
        case Code::TupleIndex:
            visit(ETupleIndexView{e}.receiver(), /*consuming=*/false, line);
            break;

        // ── Method call: recv.method(args) ────────────────────────────
        // Receiver is typically &mut self — already wrapped in EAddrOf.
        case Code::MethodCall: {
            EMethodCallView v{e};
            visit(v.receiver(), /*consuming=*/false, line);
            visit_args(v);
            break;
        }

        // ── Free function call: f(args) ───────────────────────────────
        case Code::Call:
            visit_args(ECallView{e});
            break;

        // ── Closure call ───────────────────────────────────────────────
        case Code::ClosureCall: {
            EClosureCallView v{e};
            visit(v.callee(), /*consuming=*/false, line);
            visit_args(v);
            break;
        }

        // ── Fn-pointer call ────────────────────────────────────────────
        case Code::FnPtrCall: {
            EFnPtrCallView v{e};
            visit(v.callee(), /*consuming=*/false, line);
            visit_args(v);
            break;
        }

        // ── Binary / Unary ─────────────────────────────────────────────
        case Code::BinOp: {
            EBinOpView v{e};
            visit(v.lhs(), /*consuming=*/true, line);
            visit(v.rhs(), /*consuming=*/true, line);
            break;
        }
        case Code::Unary:
            visit(EUnaryView{e}.operand(), /*consuming=*/true, line);
            break;

        // ── Cast ───────────────────────────────────────────────────────
        case Code::Cast:
            visit(ECastView{e}.operand(), consuming, line);
            break;

        // ── Struct literal ─────────────────────────────────────────────
        case Code::StructLit:
            EStructLitView{e}.each_field_value([&](ExprRef fv) {
                visit(fv, /*consuming=*/true, line);
            });
            break;

        // ── New: new Foo { ... } ───────────────────────────────────────
        case Code::New:
            ENewView{e}.each_field_value([&](ExprRef fv) {
                visit(fv, /*consuming=*/true, line);
            });
            break;

        // ── Array literal ──────────────────────────────────────────────
        case Code::ArrLit:
            EArrLitView{e}.each_elem([&](ExprRef el) {
                visit(el, /*consuming=*/true, line);
            });
            break;

        // ── Tuple literal ──────────────────────────────────────────────
        case Code::TupleLit:
            ETupleLitView{e}.each_elem([&](ExprRef el) {
                visit(el, /*consuming=*/true, line);
            });
            break;

        // ── Enum literal with payload ──────────────────────────────────
        case Code::EnumLitData:
            EEnumLitDataView{e}.each_payload([&](ExprRef pl) {
                visit(pl, /*consuming=*/true, line);
            });
            break;

        // ── If expression ──────────────────────────────────────────────
        case Code::IfExpr: {
            EIfExprView v{e};
            visit(v.cond(), /*consuming=*/true, line);
            auto saved_s = states_;
            auto saved_p = prov_;
            visit(v.then_val(), consuming, line);
            auto then_s = states_;
            auto then_p = prov_;
            states_ = saved_s;
            prov_   = saved_p;
            visit(v.else_val(), consuming, line);
            merge_moves(states_, then_s);
            merge_provs(prov_,   then_p);
            break;
        }

        // ── Match expression ───────────────────────────────────────────
        case Code::MatchExpr: {
            EMatchExprView v{e};
            visit(v.scrut(), /*consuming=*/false, line);
            auto saved_s = states_;
            auto saved_p = prov_;
            std::optional<StateMap> merged_s;
            std::optional<ProvMap>  merged_p;
            v.each_arm([&](EMatchArmRef arm) {
                states_ = saved_s;
                prov_   = saved_p;
                push_scope();
                declare_pat_bindings(arm.pat());
                if (auto g = arm.guard()) visit(g, /*consuming=*/true, line);
                visit(arm.value(), consuming, line);
                pop_scope();
                if (!merged_s) {
                    merged_s = states_;
                    merged_p = prov_;
                } else {
                    for (auto& [name, st] : states_)
                        if (st.moved && saved_s.count(name))
                            (*merged_s)[name] = st;
                    merge_provs(*merged_p, prov_);
                }
            });
            if (merged_s) {
                for (auto& [name, st] : *merged_s)
                    if (saved_s.count(name)) states_[name] = st;
                merge_provs(prov_, *merged_p);
            }
            break;
        }

        // ── Try expression: expr? ──────────────────────────────────────
        case Code::Try:
            visit(ETryView{e}.inner(), consuming, line);
            break;

        // ── Slice ──────────────────────────────────────────────────────
        case Code::SliceLit: {
            ESliceLitView v{e};
            visit(v.base(), /*consuming=*/false, line);
            visit(v.len(),  /*consuming=*/true,  line);
            break;
        }
        case Code::SliceIndex: {
            ESliceIndexView v{e};
            visit(v.slice(), /*consuming=*/false, line);
            visit(v.index(), /*consuming=*/true,  line);
            break;
        }
        case Code::SliceLen:
            visit(ESliceLenView{e}.slice(), /*consuming=*/false, line);
            break;
        case Code::SlicePtr:
            visit(ESlicePtrView{e}.slice(), /*consuming=*/false, line);
            break;

        // ── Format call ────────────────────────────────────────────────
        case Code::FormatCall: {
            EFormatCallView v{e};
            visit(v.fmt(), /*consuming=*/false, line);
            visit_args(v);
            break;
        }

        // ── Closure box ────────────────────────────────────────────────
        case Code::ClosureBox:
            EClosureBoxView{e}.each_capture_name([&](std::string_view cap) {
                check_live(std::string(cap), line);
            });
            break;

        // ── Block expression ───────────────────────────────────────────
        case Code::BlockExpr: {
            EBlockExprView v{e};
            if (auto br = v.block()) {
                auto it = prog_.mirror_table->block_by_offset.find(
                    br.offset().value());
                if (it != prog_.mirror_table->block_by_offset.end())
                    visit_block(*it->second);
            }
            if (auto r = v.result()) visit(r, consuming, line);
            break;
        }

        // ── Literals / compile-time nodes — no ownership effects ───────
        default:
            break;
    }
}

// ── Pass entry point ────────────────────────────────────────────────────────

lir::LProgram borrow_check(lir::LProgram prog) {
    const TypeSets ts = build_type_sets(prog);

    auto check = [&](const LFunction& fn) {
        if (fn.is_extern)             return;
        if (!fn.type_params.empty())  return;
        BorrowChecker(prog.diags, "fn " + std::string(bare_fn_name(fn.name)), prog, ts).check(fn);
    };

    for (auto& fn : prog.functions)       check(*fn);
    for (auto& fn : prog.specializations) check(*fn);
    for (auto& sd : prog.structs)
        for (auto& m : sd.methods)        check(*m);

    return prog;
}

} // namespace logos::compiler
