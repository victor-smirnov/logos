// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Monomorphization pass: LProgram → LProgram.
//
// Generic functions (those with non-empty type_params) are replaced by their
// concrete instantiations discovered via ECall::type_args.  Non-generic
// functions pass through unchanged.  The output program has no TypeVar types.
//
// Algorithm:
//   1. Copy all non-generic structs, enums, consts, type aliases verbatim.
//   2. Walk every non-generic function body looking for GENERIC_CALL nodes.
//   3. For each GENERIC_CALL (callee, type_args), if the instantiation
//      (callee + mangled type suffix) is not yet generated, push it onto the
//      work-list.
//   4. Process the work-list: clone the generic function template, substitute
//      TypeVars, recursively scan the cloned body for more GENERIC_CALLs.
//   5. A depth counter guards against infinite recursion; if exceeded, emit
//      a diagnostic and skip.

#include <logos/compiler/mono.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/sema.hpp>

#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace logos::compiler {

// ── Substitution map ──────────────────────────────────────────────────────

using SubstMap = std::unordered_map<std::string, const LogosType*>;

// ── Monomorphizer ─────────────────────────────────────────────────────────

namespace {

class Mono {
public:
    explicit Mono(int max_depth) : max_depth_(max_depth) {}

    lir::LProgram run(lir::LProgram&& in, int /*max_depth*/) {
        in_ = std::move(in);

        // Move non-generic structs to out_ (generic structs deferred to Batch E).
        // Since LStructDef contains unique_ptrs, it is non-copyable — must move.
        out_.structs      = std::move(in_.structs);
        out_.enums        = std::move(in_.enums);
        out_.consts       = std::move(in_.consts);
        out_.type_aliases = std::move(in_.type_aliases);
        // Move type_pool — will be extended with new types during mono
        out_.type_pool    = std::move(in_.type_pool);

        // Index generic function templates by name.
        // Pointers point into in_.functions (not moved) — stable for the run.
        for (auto& fn : in_.functions) {
            if (!fn.type_params.empty())
                templates_[fn.name] = &fn;
        }

        // Index specialisations by base name.
        // Pointers point into in_.specializations (not moved) — stable for the run.
        for (auto& spec : in_.specializations)
            specs_[spec.name].push_back(&spec);

        // Process non-generic free functions: clone (which rewrites generic call
        // sites to mangled names) then scan the clone for instantiations to enqueue.
        for (auto& fn : in_.functions) {
            if (!fn.type_params.empty()) continue;
            auto cloned = clone_fn(fn, {});   // empty SubstMap = rewrite only
            scan_fn(cloned);
            out_.functions.push_back(std::move(cloned));
        }

        // Process work-list of instantiations triggered by the scan above.
        while (!worklist_.empty()) {
            auto item = std::move(worklist_.back());
            worklist_.pop_back();
            auto inst = instantiate_fn(*item.tmpl, item.mangled, item.subst);
            scan_fn(inst);
            out_.functions.push_back(std::move(inst));
        }

        out_.diags = std::move(in_.diags);
        return std::move(out_);
    }

private:
    lir::LProgram  in_;
    lir::LProgram  out_;
    int            max_depth_;
    int            depth_ = 0;

    // name → pointer into in_.functions (generic templates only)
    std::unordered_map<std::string, const lir::LFunction*> templates_;

    // name → list of specialisations (pointers into in_.specializations)
    std::unordered_map<std::string, std::vector<const lir::LFunction*>> specs_;

    // Already-instantiated mangled names (prevent duplicate generation)
    std::unordered_set<std::string> done_;

    struct WorkItem {
        std::string                mangled;
        const lir::LFunction*      tmpl;
        SubstMap                   subst;
    };
    std::vector<WorkItem> worklist_;

    // ── Type substitution ─────────────────────────────────────────

    const LogosType* subst_type(const LogosType* t, const SubstMap& s) noexcept {
        if (!t) return t;
        switch (t->kind) {
        case LogosType::Kind::TypeVar: {
            auto it = s.find(t->type_var_name);
            return (it != s.end()) ? it->second : t;
        }
        case LogosType::Kind::Ptr: {
            auto* inner = subst_type(t->pointee, s);
            if (inner == t->pointee) return t;
            LogosType nt = *t; nt.pointee = inner;
            return out_.type_pool.alloc(nt);
        }
        case LogosType::Kind::Array: {
            auto* elem = subst_type(t->elem, s);
            if (elem == t->elem) return t;
            LogosType nt = *t; nt.elem = elem;
            return out_.type_pool.alloc(nt);
        }
        default:
            return t;
        }
    }

    // ── Mangling ──────────────────────────────────────────────────

    // type_str() may contain spaces (" *const i32") and brackets ("[i32; 4]"),
    // which are invalid in LLVM symbol names.  mangle_type() produces valid
    // C-identifier-style names: pcst_i32, pmut_i32, arr4_i32, etc.
    static std::string mangle_type(const LogosType* t) {
        if (!t) return "null";
        switch (t->kind) {
        case LogosType::Kind::Ptr:
            return (t->mut_ptr ? "pmut_" : "pcst_") + mangle_type(t->pointee);
        case LogosType::Kind::Array:
            return "arr" + std::to_string(t->arr_size) + "_" + mangle_type(t->elem);
        default:
            return type_str(t);  // primitives / TypeVar / Struct / Enum — already valid
        }
    }

    static std::string mangle(const std::string& name,
                               const std::vector<const LogosType*>& type_args) {
        // e.g. identity__i32, describe__pcst_i32, swap__i32__bool
        std::string result = name;
        for (auto* t : type_args) {
            result += "__";
            result += mangle_type(t);
        }
        return result;
    }

    // ── Expression cloning / substitution ────────────────────────

    lir::LExprPtr subst_expr(const lir::LExpr& e, const SubstMap& s) {
        auto result = std::make_unique<lir::LExpr>();
        result->type = subst_type(e.type, s);

        std::visit([&](const auto& k) {
            using K = std::decay_t<decltype(k)>;

            if constexpr (std::is_same_v<K, lir::ELitInt> ||
                          std::is_same_v<K, lir::ELitBool> ||
                          std::is_same_v<K, lir::ELitStr>  ||
                          std::is_same_v<K, lir::EEnumLit> ||
                          std::is_same_v<K, lir::EAddrOf>) {
                result->kind = k;

            } else if constexpr (std::is_same_v<K, lir::EVarRef>) {
                result->kind = k;

            } else if constexpr (std::is_same_v<K, lir::ECall>) {
                lir::ECall nc;
                nc.callee = k.callee;
                for (auto* ta : k.type_args)
                    nc.type_args.push_back(subst_type(ta, s));
                for (auto& a : k.args)
                    nc.args.push_back(subst_expr(*a, s));
                // Rewrite callee if it's a generic call that was already instantiated
                if (!nc.type_args.empty())
                    nc.callee = mangle(nc.callee, nc.type_args);
                result->kind = std::move(nc);

            } else if constexpr (std::is_same_v<K, lir::EMethodCall>) {
                lir::EMethodCall nm;
                nm.receiver = subst_expr(*k.receiver, s);
                nm.method   = k.method;
                for (auto& a : k.args)
                    nm.args.push_back(subst_expr(*a, s));
                result->kind = std::move(nm);

            } else if constexpr (std::is_same_v<K, lir::EBinOp>) {
                result->kind = lir::EBinOp{k.op,
                    subst_expr(*k.lhs, s), subst_expr(*k.rhs, s)};

            } else if constexpr (std::is_same_v<K, lir::EUnary>) {
                result->kind = lir::EUnary{k.op, subst_expr(*k.operand, s)};

            } else if constexpr (std::is_same_v<K, lir::EDeref>) {
                result->kind = lir::EDeref{subst_expr(*k.operand, s)};

            } else if constexpr (std::is_same_v<K, lir::EFieldRead>) {
                result->kind = lir::EFieldRead{subst_expr(*k.receiver, s), k.field};

            } else if constexpr (std::is_same_v<K, lir::EIndexRead>) {
                result->kind = lir::EIndexRead{
                    subst_expr(*k.receiver, s), subst_expr(*k.index, s)};

            } else if constexpr (std::is_same_v<K, lir::EStructLit>) {
                lir::EStructLit ns;
                ns.name = k.name;
                for (auto& [fn, fv] : k.fields)
                    ns.fields.push_back({fn, subst_expr(*fv, s)});
                result->kind = std::move(ns);

            } else if constexpr (std::is_same_v<K, lir::EArrLit>) {
                lir::EArrLit na;
                for (auto& elem : k.elems)
                    na.elems.push_back(subst_expr(*elem, s));
                result->kind = std::move(na);

            } else if constexpr (std::is_same_v<K, lir::ECast>) {
                result->kind = lir::ECast{subst_expr(*k.operand, s)};
            }
        }, e.kind);

        return result;
    }

    lir::LBlock subst_block(const lir::LBlock& b, const SubstMap& s) {
        lir::LBlock nb;
        for (auto& st : b.stmts)
            nb.stmts.push_back(subst_stmt(st, s));
        return nb;
    }

    lir::LStmt subst_stmt(const lir::LStmt& st, const SubstMap& s) {
        lir::LStmt ns;
        ns.line = st.line;

        std::visit([&](const auto& k) {
            using K = std::decay_t<decltype(k)>;

            if constexpr (std::is_same_v<K, lir::SLet>) {
                lir::SLet nl;
                nl.name   = k.name;
                nl.type   = subst_type(k.type, s);
                nl.is_mut = k.is_mut;
                nl.value  = subst_expr(*k.value, s);
                ns.kind   = std::move(nl);

            } else if constexpr (std::is_same_v<K, lir::SAssign>) {
                ns.kind = lir::SAssign{k.name, subst_expr(*k.value, s)};

            } else if constexpr (std::is_same_v<K, lir::SReturn>) {
                ns.kind = lir::SReturn{k.value ? subst_expr(*k.value, s) : nullptr};

            } else if constexpr (std::is_same_v<K, lir::SIf>) {
                lir::SIf ni;
                ni.cond  = subst_expr(*k.cond, s);
                ni.then_ = std::make_unique<lir::LBlock>(subst_block(*k.then_, s));
                if (k.else_)
                    ni.else_ = std::make_unique<lir::LBlock>(subst_block(**k.else_, s));
                ns.kind = std::move(ni);

            } else if constexpr (std::is_same_v<K, lir::SWhile>) {
                ns.kind = lir::SWhile{
                    subst_expr(*k.cond, s),
                    std::make_unique<lir::LBlock>(subst_block(*k.body, s))};

            } else if constexpr (std::is_same_v<K, lir::SFor>) {
                lir::SFor nf;
                nf.var       = k.var;
                nf.lo        = subst_expr(*k.lo, s);
                nf.hi        = subst_expr(*k.hi, s);
                nf.inclusive = k.inclusive;
                nf.body      = std::make_unique<lir::LBlock>(subst_block(*k.body, s));
                ns.kind      = std::move(nf);

            } else if constexpr (std::is_same_v<K, lir::SLoop>) {
                ns.kind = lir::SLoop{
                    std::make_unique<lir::LBlock>(subst_block(*k.body, s))};

            } else if constexpr (std::is_same_v<K, lir::SBreak> ||
                                 std::is_same_v<K, lir::SContinue>) {
                ns.kind = k;

            } else if constexpr (std::is_same_v<K, lir::SFieldWrite>) {
                ns.kind = lir::SFieldWrite{k.receiver, k.field, subst_expr(*k.value, s)};

            } else if constexpr (std::is_same_v<K, lir::SIndexWrite>) {
                ns.kind = lir::SIndexWrite{
                    k.arr, subst_expr(*k.index, s), subst_expr(*k.value, s)};

            } else if constexpr (std::is_same_v<K, lir::SExprStmt>) {
                ns.kind = lir::SExprStmt{subst_expr(*k.expr, s)};

            } else if constexpr (std::is_same_v<K, lir::SMatch>) {
                lir::SMatch nm;
                nm.scrut = subst_expr(*k.scrut, s);
                for (auto& arm : k.arms) {
                    lir::LMatchArm na;
                    na.pat  = arm.pat;
                    na.body = std::make_unique<lir::LBlock>(subst_block(*arm.body, s));
                    nm.arms.push_back(std::move(na));
                }
                ns.kind = std::move(nm);
            }
        }, st.kind);

        return ns;
    }

    // ── Clone a function with substitution (empty SubstMap = verbatim copy) ─

    lir::LFunction clone_fn(const lir::LFunction& fn, const SubstMap& s) {
        lir::LFunction nf;
        nf.name      = fn.name;
        nf.is_extern = fn.is_extern;
        nf.ret_type  = subst_type(fn.ret_type, s);
        for (auto& p : fn.params)
            nf.params.push_back({p.name, subst_type(p.type, s)});
        nf.body = subst_block(fn.body, s);
        // type_params left empty: instantiated functions are monomorphic
        return nf;
    }

    // ── Scan a function for generic calls and enqueue them ────────

    void scan_fn(const lir::LFunction& fn) {
        scan_block(fn.body);
    }

    void scan_block(const lir::LBlock& b) {
        for (auto& st : b.stmts) scan_stmt(st);
    }

    void scan_stmt(const lir::LStmt& st) {
        std::visit([&](const auto& k) {
            using K = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<K, lir::SLet>)
                scan_expr(*k.value);
            else if constexpr (std::is_same_v<K, lir::SAssign>)
                scan_expr(*k.value);
            else if constexpr (std::is_same_v<K, lir::SReturn>)
                { if (k.value) scan_expr(*k.value); }
            else if constexpr (std::is_same_v<K, lir::SIf>) {
                scan_expr(*k.cond);
                scan_block(*k.then_);
                if (k.else_) scan_block(**k.else_);
            } else if constexpr (std::is_same_v<K, lir::SWhile>) {
                scan_expr(*k.cond); scan_block(*k.body);
            } else if constexpr (std::is_same_v<K, lir::SFor>) {
                scan_expr(*k.lo); scan_expr(*k.hi); scan_block(*k.body);
            } else if constexpr (std::is_same_v<K, lir::SLoop>) {
                scan_block(*k.body);
            } else if constexpr (std::is_same_v<K, lir::SFieldWrite>) {
                scan_expr(*k.value);
            } else if constexpr (std::is_same_v<K, lir::SIndexWrite>) {
                scan_expr(*k.index); scan_expr(*k.value);
            } else if constexpr (std::is_same_v<K, lir::SExprStmt>) {
                scan_expr(*k.expr);
            } else if constexpr (std::is_same_v<K, lir::SMatch>) {
                scan_expr(*k.scrut);
                for (auto& arm : k.arms) scan_block(*arm.body);
            }
        }, st.kind);
    }

    void scan_expr(const lir::LExpr& e) {
        std::visit([&](const auto& k) {
            using K = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<K, lir::ECall>) {
                if (!k.type_args.empty()) {
                    // This is a post-substitution generic call: callee is already mangled.
                    // The name was rewritten by subst_expr. Try to find the original template.
                    // We identify originals by checking templates_ using the un-mangled name.
                    // In practice after subst_expr, callee == mangled name.
                    enqueue_if_needed(k.callee, k.type_args);
                }
                for (auto& a : k.args) scan_expr(*a);
            } else if constexpr (std::is_same_v<K, lir::EBinOp>) {
                scan_expr(*k.lhs); scan_expr(*k.rhs);
            } else if constexpr (std::is_same_v<K, lir::EUnary>) {
                scan_expr(*k.operand);
            } else if constexpr (std::is_same_v<K, lir::EDeref>) {
                scan_expr(*k.operand);
            } else if constexpr (std::is_same_v<K, lir::EFieldRead>) {
                scan_expr(*k.receiver);
            } else if constexpr (std::is_same_v<K, lir::EIndexRead>) {
                scan_expr(*k.receiver); scan_expr(*k.index);
            } else if constexpr (std::is_same_v<K, lir::EMethodCall>) {
                scan_expr(*k.receiver);
                for (auto& a : k.args) scan_expr(*a);
            } else if constexpr (std::is_same_v<K, lir::EStructLit>) {
                for (auto& [fn, fv] : k.fields) scan_expr(*fv);
            } else if constexpr (std::is_same_v<K, lir::EArrLit>) {
                for (auto& elem : k.elems) scan_expr(*elem);
            } else if constexpr (std::is_same_v<K, lir::ECast>) {
                scan_expr(*k.operand);
            }
        }, e.kind);
    }

    // ── Pattern matching (for specialisation selection) ───────────

    // Match a concrete type against a specialisation pattern (may contain TypeVars).
    // On success, fills `bindings` with TypeVar → concrete_type mappings.
    static bool match_type(const LogosType* concrete, const LogosType* pattern,
                           SubstMap& bindings) noexcept {
        if (!concrete || !pattern) return false;
        if (pattern->kind == LogosType::Kind::TypeVar) {
            auto it = bindings.find(pattern->type_var_name);
            if (it != bindings.end())
                return types_equal(*concrete, *it->second);  // must match prior binding
            bindings[pattern->type_var_name] = concrete;
            return true;
        }
        if (pattern->kind != concrete->kind) return false;
        switch (pattern->kind) {
        case LogosType::Kind::Ptr:
            return pattern->mut_ptr == concrete->mut_ptr &&
                   match_type(concrete->pointee, pattern->pointee, bindings);
        case LogosType::Kind::Array:
            return pattern->arr_size == concrete->arr_size &&
                   match_type(concrete->elem, pattern->elem, bindings);
        case LogosType::Kind::Struct:
            return pattern->struct_name == concrete->struct_name;
        default:
            return types_equal(*concrete, *pattern);
        }
    }

    // Higher score = more concrete = higher priority.
    static int type_specificity(const LogosType* t) noexcept {
        if (!t || t->kind == LogosType::Kind::TypeVar) return 0;
        if (t->kind == LogosType::Kind::Ptr)   return 1 + type_specificity(t->pointee);
        if (t->kind == LogosType::Kind::Array)  return 1 + type_specificity(t->elem);
        return 100;  // fully-concrete scalar / struct / enum
    }

    static int specificity_score(const std::vector<const LogosType*>& patterns) noexcept {
        int s = 0;
        for (auto* p : patterns) s += type_specificity(p);
        return s;
    }

    // Return the most specific specialisation that matches type_args, or nullptr.
    const lir::LFunction* find_best_spec(
        const std::string& base_name,
        const std::vector<const LogosType*>& type_args) {
        auto sit = specs_.find(base_name);
        if (sit == specs_.end()) return nullptr;

        const lir::LFunction* best       = nullptr;
        int                   best_score = -1;

        for (auto* spec : sit->second) {
            if (spec->spec_patterns.size() != type_args.size()) continue;
            SubstMap dummy;
            bool ok = true;
            for (size_t i = 0; i < type_args.size(); ++i) {
                if (!match_type(type_args[i], spec->spec_patterns[i], dummy)) {
                    ok = false; break;
                }
            }
            if (!ok) continue;
            int score = specificity_score(spec->spec_patterns);
            if (score > best_score) { best_score = score; best = spec; }
        }
        return best;
    }

    // ── Enqueue an instantiation if needed ───────────────────────

    void enqueue_if_needed(const std::string& mangled_callee,
                           const std::vector<const LogosType*>& type_args) {
        if (done_.count(mangled_callee)) return;

        // Find the base name by checking templates_ and specs_.
        std::string orig_name;
        for (auto& [tname, _] : templates_)
            if (mangle(tname, type_args) == mangled_callee) { orig_name = tname; break; }
        if (orig_name.empty())
            for (auto& [sname, _] : specs_)
                if (mangle(sname, type_args) == mangled_callee) { orig_name = sname; break; }
        if (orig_name.empty()) return;  // not a generic/spec call we know about

        if (depth_ >= max_depth_) {
            in_.diags.diags.push_back({Diag::Level::Error, "mono",
                std::format("instantiation depth limit ({}) exceeded for '{}'",
                            max_depth_, mangled_callee), {}, 0});
            return;
        }

        done_.insert(mangled_callee);

        // Prefer the most-specific matching specialisation over the generic template.
        if (auto* spec = find_best_spec(orig_name, type_args)) {
            SubstMap subst;
            for (size_t i = 0; i < spec->spec_patterns.size(); ++i)
                match_type(type_args[i], spec->spec_patterns[i], subst);
            worklist_.push_back({mangled_callee, spec, std::move(subst)});
            return;
        }

        // Generic template fallback.
        auto tit = templates_.find(orig_name);
        if (tit == templates_.end()) return;
        const lir::LFunction* tmpl = tit->second;

        SubstMap subst;
        size_t n = std::min(tmpl->type_params.size(), type_args.size());
        for (size_t i = 0; i < n; ++i)
            subst[tmpl->type_params[i].name] = type_args[i];
        worklist_.push_back({mangled_callee, tmpl, std::move(subst)});
    }

    // ── Instantiate a template with a concrete SubstMap ──────────

    lir::LFunction instantiate_fn(const lir::LFunction& tmpl,
                                   const std::string& mangled_name,
                                   const SubstMap& subst) {
        ++depth_;
        auto fn = clone_fn(tmpl, subst);
        fn.name = mangled_name;
        --depth_;
        return fn;
    }
};

} // anonymous namespace

// ── Public entry point ─────────────────────────────────────────────────────

lir::LProgram mono_pass(lir::LProgram prog, int max_instantiation_depth) noexcept(false) {
    Mono m(max_instantiation_depth);
    return m.run(std::move(prog), max_instantiation_depth);
}

} // namespace logos::compiler
