// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// mono_scan.cpp — Function scanning and generic call enqueueing.

#include "mono_impl.hpp"

namespace logos::compiler {

void Mono::scan_fn(const lir::LFunction& fn) {
    scan_block(fn.body);
}


void Mono::scan_block(const lir::LBlock& b) {
    for (auto& st : b.stmts) scan_stmt(st);
}


void Mono::scan_stmt(const lir::LStmt& st) {
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
        } else if constexpr (std::is_same_v<K, lir::SBlock>) {
            scan_block(*k.body);
        } else if constexpr (std::is_same_v<K, lir::SFieldWrite>) {
            scan_expr(*k.value);
        } else if constexpr (std::is_same_v<K, lir::SDerefFieldWrite>) {
            scan_expr(*k.value);
        } else if constexpr (std::is_same_v<K, lir::SIndexWrite>) {
            scan_expr(*k.index); scan_expr(*k.value);
        } else if constexpr (std::is_same_v<K, lir::SFieldIndexWrite>) {
            scan_expr(*k.index); scan_expr(*k.value);
        } else if constexpr (std::is_same_v<K, lir::SDerefWrite>) {
            scan_expr(*k.ptr); scan_expr(*k.value);
        } else if constexpr (std::is_same_v<K, lir::STupleWrite>) {
            scan_expr(*k.value);
        } else if constexpr (std::is_same_v<K, lir::SExprStmt>) {
            scan_expr(*k.expr);
        } else if constexpr (std::is_same_v<K, lir::SDelete>) {
            scan_expr(*k.expr);
        } else if constexpr (std::is_same_v<K, lir::SDrop>) {
            // no-op: SDrop only references a variable name, not an expression
        } else if constexpr (std::is_same_v<K, lir::SMatch>) {
            scan_expr(*k.scrut);
            for (auto& arm : k.arms) {
                // NM3: scan guard expressions so generic calls inside guards are found.
                if (arm.guard) scan_expr(**arm.guard);
                scan_block(*arm.body);
            }
        } else if constexpr (std::is_same_v<K, lir::SForEach>) {
            scan_expr(*k.iter); scan_block(*k.body);
        } else if constexpr (std::is_same_v<K, lir::SLetElse>) {
            // NM1: scan scrutinee and the else diverge-block for generic calls.
            scan_expr(*k.scrut);
            scan_block(*k.else_block);
        } else if constexpr (std::is_same_v<K, lir::SChainFieldWrite>) {
            scan_expr(*k.value);
        } else if constexpr (std::is_same_v<K, lir::SBreak>) {
            // NM5: break-with-value expressions may contain generic calls.
            if (k.value) scan_expr(*k.value);
        }
    }, st.kind);
}


void Mono::scan_expr(const lir::LExpr& e) {
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
        } else if constexpr (std::is_same_v<K, lir::ENew>) {
            for (auto& [fn, fv] : k.fields) scan_expr(*fv);
        } else if constexpr (std::is_same_v<K, lir::EIfExpr>) {
            scan_expr(*k.cond); scan_expr(*k.then_val); scan_expr(*k.else_val);
        } else if constexpr (std::is_same_v<K, lir::ETupleLit>) {
            for (auto& elem : k.elems) scan_expr(*elem);
        } else if constexpr (std::is_same_v<K, lir::ETupleIndex>) {
            scan_expr(*k.receiver);
        } else if constexpr (std::is_same_v<K, lir::EClosureBox>) {
            if (k.inner) for (auto& st : k.inner->body.stmts) scan_stmt(st);
        } else if constexpr (std::is_same_v<K, lir::EClosureCall>) {
            scan_expr(*k.callee);
            for (auto& a : k.args) scan_expr(*a);
        } else if constexpr (std::is_same_v<K, lir::ESliceLit>) {
            scan_expr(*k.base); scan_expr(*k.len);
        } else if constexpr (std::is_same_v<K, lir::ESliceIndex>) {
            scan_expr(*k.slice); scan_expr(*k.index);
        } else if constexpr (std::is_same_v<K, lir::ESliceLen>) {
            scan_expr(*k.slice);
        } else if constexpr (std::is_same_v<K, lir::ESlicePtr>) {
            scan_expr(*k.slice);
        } else if constexpr (std::is_same_v<K, lir::EEnumLitData>) {
            for (auto& a : k.payload) scan_expr(*a);
        } else if constexpr (std::is_same_v<K, lir::EFormatCall>) {
            scan_expr(*k.fmt);
            for (auto& a : k.args) scan_expr(*a);
        } else if constexpr (std::is_same_v<K, lir::EPackExpand>) {
            // nothing to scan
        } else if constexpr (std::is_same_v<K, lir::ETry>) {
            scan_expr(*k.inner);
        } else if constexpr (std::is_same_v<K, lir::EMatchExpr>) {
            scan_expr(*k.scrut);
            for (auto& arm : k.arms) {
                if (arm.guard) scan_expr(**arm.guard);
                scan_expr(*arm.value);
            }
        } else if constexpr (std::is_same_v<K, lir::EBlockExpr>) {
            if (k.block) scan_block(*k.block);
            if (k.result) scan_expr(*k.result);
        }
    }, e.kind);
}


// ── Pattern matching (static, inline in mono_impl.hpp) ───────────

// Return the most specific specialisation that matches type_args, or nullptr.
const lir::LFunction* Mono::find_best_spec(
    const std::string& base_name,
    const std::vector<TypeRef>& type_args) {
    auto sit = specs_.find(base_name);
    if (sit == specs_.end()) {
        std::string raw = base_name;
        if (auto p = raw.find("__g__"); p != std::string::npos)
            raw.resize(p);
        else if (auto p = raw.find("__f__"); p != std::string::npos)
            raw.resize(p);
        sit = specs_.find(raw);
    }
    if (sit == specs_.end()) return nullptr;

    const lir::LFunction* best      = nullptr;
    std::vector<int>      best_vec;
    bool                  ambiguous = false;

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
        auto svec = specificity_vec(spec->spec_patterns);
        if (!best || svec > best_vec) {
            best_vec  = svec;
            best      = spec;
            ambiguous = false;
        } else if (svec == best_vec) {
            ambiguous = true;
        }
    }
    if (ambiguous) {
        in_.diags.diags.push_back({Diag::Level::Error, "mono",
            std::format("ambiguous specializations for function '{}'", base_name),
            "", 0});
    }
    return best;
}


// ── Enqueue an instantiation if needed ───────────────────────

void Mono::enqueue_if_needed(const std::string& mangled_callee,
                       const std::vector<TypeRef>& type_args) {
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
        worklist_.push_back({mangled_callee, spec, std::move(subst), {}, depth_ + 1});
        return;
    }

    // Generic template fallback.
    auto tit = templates_.find(orig_name);
    if (tit == templates_.end()) return;
    const lir::LFunction* tmpl = tit->second;

    SubstMap subst;
    PackMap  packs;
    bool has_variadic = !tmpl->type_params.empty() && tmpl->type_params.back().is_variadic;
    size_t non_variadic_count = tmpl->type_params.size() - (has_variadic ? 1 : 0);
    for (size_t i = 0; i < non_variadic_count && i < type_args.size(); ++i)
        subst[tmpl->type_params[i].name] = type_args[i];
    if (has_variadic) {
        auto& vtp = tmpl->type_params.back();
        std::vector<TypeRef> pack_types;
        for (size_t i = non_variadic_count; i < type_args.size(); ++i)
            pack_types.push_back(type_args[i]);
        packs[vtp.name] = std::move(pack_types);
    }
    worklist_.push_back({mangled_callee, tmpl, std::move(subst), std::move(packs), depth_ + 1});
}

} // namespace logos::compiler
