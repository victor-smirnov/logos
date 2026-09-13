name: letnamedc
file: include/logos/compiler/subtype.hpp
---
            if (x.empty() && outlives_is_static(y) && !lt_static_yield()) {
                logos::probe::census("stland.lteq");
                return false;
            }
---
            if (x.empty() && outlives_is_static(y) && !lt_static_yield()) {
                logos::probe::census("stland.lteq");
                return false;
            }
            // PROBE letnamed / letnamedc — src/compiler/PROBES.md 2026-09-12q.
            if (x.empty() && !y.empty() &&
                (current_lt_binders().count(outlives_norm(y)) ||
                 current_lt_binders().count(std::string(y)))) {
                logos::probe::census("letnamed.lteq.binder");
                if ((logos::probe::on("letnamed") || logos::probe::on("letnamedc")) &&
                    !lt_static_yield())
                    return false;
            }
===
name: stcallret
file: src/compiler/sema_expr.cpp
---
    return builder().call(fi.symbol_name.empty() ? mangled : fi.symbol_name, {}, std::move(arg_exprs), fi.ret_type);
---
    // PROBE stcallret / stcallrets / letnamedc — src/compiler/PROBES.md 2026-09-12q.
    TypeRef sc_ret_ = fi.ret_type;
    logos::probe::census("stcallret.site");
    if (logos::probe::on("stcallret") || logos::probe::on("stcallrets") ||
        logos::probe::on("letnamedc")) {
        std::vector<std::string> sb_ = fi.lifetime_params;
        std::unordered_set<std::string> srb_;
        for (auto p0_ : fi.param_types) collect_param_regions_(p0_, srb_);
        collect_param_regions_(fi.ret_type, srb_);
        for (auto& r_ : srb_)
            if (!outlives_is_static(r_) &&
                std::find(sb_.begin(), sb_.end(), r_) == sb_.end())
                sb_.push_back(r_);
        sc_ret_ = subst_call_ret_lts_(fi.param_types, sb_, arg_exprs, sc_ret_);
        if (sc_ret_ != fi.ret_type) logos::probe::census("stcallret.differs");
    }
    return builder().call(fi.symbol_name.empty() ? mangled : fi.symbol_name, {}, std::move(arg_exprs), sc_ret_);
===
name: stcallrets
file: src/compiler/sema_expr.cpp
---
    TypeRef ret = (struct_subst.empty() && lt_subst.empty())
        ? fi.ret_type
        : subst_type_sema(fi.ret_type, struct_subst, lt_subst);
---
    TypeRef ret = (struct_subst.empty() && lt_subst.empty())
        ? fi.ret_type
        : subst_type_sema(fi.ret_type, struct_subst, lt_subst);
    // PROBE stcallret / letnamedc (method half) — src/compiler/PROBES.md 2026-09-12q.
    if (logos::probe::on("stcallret") || logos::probe::on("letnamedc")) {
        std::vector<std::string> mb_ = fi.lifetime_params;
        std::unordered_set<std::string> mrb_;
        for (auto p0_ : fi.param_types) collect_param_regions_(p0_, mrb_);
        collect_param_regions_(fi.ret_type, mrb_);
        for (auto& r_ : mrb_)
            if (!outlives_is_static(r_) &&
                std::find(mb_.begin(), mb_.end(), r_) == mb_.end())
                mb_.push_back(r_);
        std::vector<TypeRef> mspts_;
        for (auto p0_ : fi.param_types)
            mspts_.push_back(struct_subst.empty() ? p0_ : subst_type_sema(p0_, struct_subst));
        std::vector<lir::LExprPtr> mall_;
        mall_.push_back(recv);
        for (auto& a_ : arg_exprs) mall_.push_back(a_);
        auto mls_ = build_call_lt_subst_(mspts_, mb_, mall_, fi.ret_type);
        TypeRef mret_ = subst_type_sema(fi.ret_type, struct_subst, mls_);
        if (mret_ != ret) logos::probe::census("mcallret.differs");
        ret = mret_;
    }
===
name: letnamed
file: src/compiler/probes/2026-09-12q-letregion/TARGETS.md
---
## GROUPING TO TEST, NOT ASSUME
---
## GROUPING TO TEST, NOT ASSUME
