name: stmutassign
file: src/compiler/sema_stmt.cpp
---
    if (logos::probe::on("lifereg_varassign") && var_type && rhs)
        check_variance(expr_type(rhs), var_type,
                       std::format("assignment to '{}'", name),
                       /*permissive=*/false);
---
    if (logos::probe::on("lifereg_varassign") && var_type && rhs)
        check_variance(expr_type(rhs), var_type,
                       std::format("assignment to '{}'", name),
                       /*permissive=*/false);
    // PROBES 2026-09-13c-staticdemand: stmutassign / stmutbare / stmutdecl.
    if ((logos::probe::on("stmutassign") || logos::probe::on("stmutbare") ||
         logos::probe::on("stmutdecl")) && is_static_mut && var_type && rhs) {
        logos::probe::census("stmut.assign");
        std::function<TypeRef(TypeRef)> stfill_ = [&](TypeRef t0_) -> TypeRef {
                if (!t0_) return t0_;
                auto k0_ = TypeRef(t0_).kind();
                if (k0_ == LogosType::Kind::Ref || k0_ == LogosType::Kind::MutRef) {
                    std::string lt0_(TypeRef(t0_).lifetime());
                    return make_ref(k0_ == LogosType::Kind::MutRef,
                                    stfill_(TypeRef(t0_).pointee()),
                                    lt0_.empty() ? std::string("'static") : lt0_);
                }
                return t0_;
            };
        check_variance(expr_type(rhs),
                       logos::probe::on("stmutassign") ? stfill_(var_type) : var_type,
                       std::format("assignment to '{}'", name),
                       /*permissive=*/false);
    }
===
name: stmutdecl
file: src/compiler/sema_collect.cpp
---
        ItemSignatureGuard sig_guard(in_item_signature_);
        t = resolve_type(map_of(node.get(la::TYPE.code)));
---
        ItemSignatureGuard sig_guard(in_item_signature_);
        t = resolve_type(map_of(node.get(la::TYPE.code)));
        // PROBES 2026-09-13c-staticdemand: stmutdecl.
        if (t && logos::probe::on("stmutdecl") && code_of(node) == la::STATIC_DEF) {
            logos::probe::census("stmut.decl");
            std::function<TypeRef(TypeRef)> stfill_ = [&](TypeRef t0_) -> TypeRef {
                if (!t0_) return t0_;
                auto k0_ = TypeRef(t0_).kind();
                if (k0_ == LogosType::Kind::Ref || k0_ == LogosType::Kind::MutRef) {
                    std::string lt0_(TypeRef(t0_).lifetime());
                    return make_ref(k0_ == LogosType::Kind::MutRef,
                                    stfill_(TypeRef(t0_).pointee()),
                                    lt0_.empty() ? std::string("'static") : lt0_);
                }
                return t0_;
            };
            t = stfill_(t);
        }
===
name: cooutstatt
file: src/compiler/sema_impl.hpp
---
    void check_call_outlives(
        const std::string& callee_name,
        const std::vector<TypeRef>& callee_param_types,
        const std::vector<lir::LExprPtr>& arg_exprs,
        const std::vector<std::pair<std::string, std::string>>& callee_outlives) {
        if (callee_outlives.empty()) return;
        // Build callee_lt → caller_lt substitution by walking matched
        // param/arg type pairs.
        std::unordered_map<std::string, std::string> subst;
        auto record = [&](std::string_view callee_lt, std::string_view caller_lt) {
            if (callee_lt.empty() || caller_lt.empty()) return;
            std::string ck = outlives_norm(callee_lt);
            std::string vk = outlives_norm(caller_lt);
            auto it = subst.find(ck);
            if (it == subst.end()) subst.emplace(ck, vk);
            // If already present and different — conflict, but caller-region
            // inference would unify; skip strict enforcement here.
        };
        std::function<void(TypeRef, TypeRef)> walk =
            [&](TypeRef pt, TypeRef at) {
            if (!pt || !at) return;
            using K = LogosType::Kind;
            auto pk = pt.kind();
            if ((pk == K::Ref || pk == K::MutRef) &&
                (at.kind() == K::Ref || at.kind() == K::MutRef)) {
                record(pt.lifetime(), at.lifetime());
                walk(pt.pointee(), at.pointee());
                return;
            }
            if (pk == K::Struct || pk == K::ZonedStruct || pk == K::Enum) {
                if (at.kind() == pk) {
                    auto pl = pt.lifetime_args(); auto al = at.lifetime_args();
                    for (size_t i = 0; i < pl.size() && i < al.size(); ++i)
                        record(pl[i], al[i]);
                    auto pa = pt.type_args(); auto aa = at.type_args();
                    for (size_t i = 0; i < pa.size() && i < aa.size(); ++i)
                        walk(pa[i], aa[i]);
                }
                return;
            }
            if (pk == K::Tuple && at.kind() == K::Tuple) {
                auto pe = pt.tuple_elems(); auto ae = at.tuple_elems();
                for (size_t i = 0; i < pe.size() && i < ae.size(); ++i)
                    walk(pe[i], ae[i]);
                return;
            }
            if ((pk == K::Slice || pk == K::Array) && at.kind() == pk) {
                walk(pt.elem(), at.elem());
                return;
            }
            if (pk == K::Ptr && at.kind() == K::Ptr) {
                walk(pt.pointee(), at.pointee());
                return;
            }
        };
        size_t n = std::min(callee_param_types.size(), arg_exprs.size());
        for (size_t i = 0; i < n; ++i)
            if (arg_exprs[i]) walk(callee_param_types[i], expr_type(arg_exprs[i]));
        auto adj = outlives_adj(current_outlives_);
        for (auto& [c_long, c_short] : callee_outlives) {
            // 'static is reserved — always satisfies; skip checking.
            if (outlives_is_static(c_long)) continue;
            auto it_l = subst.find(outlives_norm(c_long));
            auto it_s = subst.find(outlives_norm(c_short));
            // Only enforce when BOTH callee lifetimes are visible at arg
            // positions and mapped to concrete caller lifetimes. Otherwise
            // the lifetime is either internal to the callee or elided at
            // the call site — caller's region inference handles it.
            if (it_l == subst.end() || it_s == subst.end()) continue;
            const std::string& caller_long  = it_l->second;
            const std::string& caller_short = it_s->second;
            if (caller_long == caller_short) continue;
            if (!outlives(caller_long, caller_short, adj, /*permissive_empty=*/false)) {
                error(std::format(
                    "call to '{}': caller does not satisfy callee's "
                    "outlives bound `{}: {}` (under arg-type substitution: "
                    "`{}: {}` required)",
                    callee_name, c_long, c_short, caller_long, caller_short));
            }
        }
    }
---
    void check_call_outlives(
        const std::string& callee_name,
        const std::vector<TypeRef>& callee_param_types,
        const std::vector<lir::LExprPtr>& arg_exprs,
        const std::vector<std::pair<std::string, std::string>>& callee_outlives) {
        if (callee_outlives.empty()) return;
        // Build callee_lt → caller_lt substitution by walking matched
        // param/arg type pairs.
        std::unordered_map<std::string, std::string> subst;
        // PROBES 2026-09-13c-staticdemand: cooutstat / cooutstatt / cooutstate / cooutall.
        const bool co_s_ = logos::probe::on("cooutstat") || logos::probe::on("cooutstatt") ||
                           logos::probe::on("cooutstate") || logos::probe::on("cooutall");
        const bool co_t_ = logos::probe::on("cooutstatt") || logos::probe::on("cooutstate") ||
                           logos::probe::on("cooutall");
        const bool co_e_ = logos::probe::on("cooutstate");
        std::unordered_set<std::string> co_empty_;
        auto record = [&](std::string_view callee_lt, std::string_view caller_lt) {
            if (!callee_lt.empty() && caller_lt.empty())
                co_empty_.insert(outlives_norm(callee_lt));
            if (callee_lt.empty() || caller_lt.empty()) return;
            std::string ck = outlives_norm(callee_lt);
            std::string vk = outlives_norm(caller_lt);
            auto it = subst.find(ck);
            if (it == subst.end()) subst.emplace(ck, vk);
            // If already present and different — conflict, but caller-region
            // inference would unify; skip strict enforcement here.
        };
        std::function<void(TypeRef, TypeRef)> walk =
            [&](TypeRef pt, TypeRef at) {
            if (!pt || !at) return;
            using K = LogosType::Kind;
            auto pk = pt.kind();
            if ((pk == K::Ref || pk == K::MutRef) &&
                (at.kind() == K::Ref || at.kind() == K::MutRef)) {
                record(pt.lifetime(), at.lifetime());
                walk(pt.pointee(), at.pointee());
                return;
            }
            if (pk == K::Struct || pk == K::ZonedStruct || pk == K::Enum) {
                if (at.kind() == pk) {
                    auto pl = pt.lifetime_args(); auto al = at.lifetime_args();
                    for (size_t i = 0; i < pl.size() && i < al.size(); ++i)
                        record(pl[i], al[i]);
                    auto pa = pt.type_args(); auto aa = at.type_args();
                    for (size_t i = 0; i < pa.size() && i < aa.size(); ++i)
                        walk(pa[i], aa[i]);
                }
                return;
            }
            if (pk == K::Tuple && at.kind() == K::Tuple) {
                auto pe = pt.tuple_elems(); auto ae = at.tuple_elems();
                for (size_t i = 0; i < pe.size() && i < ae.size(); ++i)
                    walk(pe[i], ae[i]);
                return;
            }
            if ((pk == K::Slice || pk == K::Array) && at.kind() == pk) {
                walk(pt.elem(), at.elem());
                return;
            }
            if (pk == K::Ptr && at.kind() == K::Ptr) {
                walk(pt.pointee(), at.pointee());
                return;
            }
        };
        size_t n = std::min(callee_param_types.size(), arg_exprs.size());
        for (size_t i = 0; i < n; ++i)
            if (arg_exprs[i]) walk(callee_param_types[i], expr_type(arg_exprs[i]));
        auto adj = outlives_adj(current_outlives_);
        std::vector<std::pair<std::string, std::string>> co_pairs_ = callee_outlives;
        if (co_t_) {
            bool grew_ = true;
            while (grew_) {
                grew_ = false;
                auto snap_ = co_pairs_;
                for (auto& [l1_, s1_] : snap_)
                    for (auto& [l2_, s2_] : snap_) {
                        if (outlives_norm(s1_) != outlives_norm(l2_)) continue;
                        bool have_ = false;
                        for (auto& [l3_, s3_] : co_pairs_)
                            if (outlives_norm(l3_) == outlives_norm(l1_) &&
                                outlives_norm(s3_) == outlives_norm(s2_)) { have_ = true; break; }
                        if (!have_) { co_pairs_.emplace_back(l1_, s2_); grew_ = true; }
                    }
            }
        }
        for (auto& [c_long, c_short] : co_s_ ? co_pairs_ : callee_outlives) {
            // 'static is reserved — always satisfies; skip checking.
            if (outlives_is_static(c_long)) continue;
            if (co_s_ && outlives_is_static(c_short)) {
                logos::probe::census("coout.static");
                auto it_c_ = subst.find(outlives_norm(c_long));
                bool bad_ = false;
                if (it_c_ != subst.end()) {
                    logos::probe::census("coout.static.mapped");
                    bad_ = !outlives(it_c_->second, "'static", adj, /*permissive_empty=*/false);
                } else if (co_e_ && co_empty_.count(outlives_norm(c_long))) {
                    logos::probe::census("coout.static.empty");
                    bad_ = true;
                }
                if (bad_)
                    error(std::format(
                        "call to '{}': borrowed data escapes — the callee's bound "
                        "`{}: 'static` requires a 'static argument", callee_name, c_long));
                continue;
            }
            auto it_l = subst.find(outlives_norm(c_long));
            auto it_s = subst.find(outlives_norm(c_short));
            // Only enforce when BOTH callee lifetimes are visible at arg
            // positions and mapped to concrete caller lifetimes. Otherwise
            // the lifetime is either internal to the callee or elided at
            // the call site — caller's region inference handles it.
            if (it_l == subst.end() || it_s == subst.end()) continue;
            const std::string& caller_long  = it_l->second;
            const std::string& caller_short = it_s->second;
            if (caller_long == caller_short) continue;
            if (!outlives(caller_long, caller_short, adj, /*permissive_empty=*/false)) {
                error(std::format(
                    "call to '{}': caller does not satisfy callee's "
                    "outlives bound `{}: {}` (under arg-type substitution: "
                    "`{}: {}` required)",
                    callee_name, c_long, c_short, caller_long, caller_short));
            }
        }
    }
===
name: cooutall
file: src/compiler/sema_expr.cpp
---
        for (uint64_t i = 0; i < explicit_args; ++i) {
            size_t pi = i + 1;
---
        // PROBES 2026-09-13c-staticdemand: cooutall — the method-call site never asks.
        if (logos::probe::on("cooutall")) {
            logos::probe::census("coout.method");
            std::vector<lir::LExprPtr> co_all_;
            co_all_.push_back(recv);
            for (auto& a_ : arg_exprs) co_all_.push_back(a_);
            check_call_outlives(std::string(mangled), fi.param_types, co_all_, fi.lifetime_outlives);
        }
        for (uint64_t i = 0; i < explicit_args; ++i) {
            size_t pi = i + 1;
===
