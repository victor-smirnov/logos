name: stcg
file: src/compiler/sema_expr.cpp
---
    return builder().call(fi.symbol_name.empty() ? mangled : fi.symbol_name, {}, std::move(arg_exprs), fi.ret_type);
---
    // PROBE stcg / lrall — src/compiler/PROBES.md 2026-09-12r.
    TypeRef sc_ret_ = fi.ret_type;
    if (logos::probe::on("stcg") || logos::probe::on("lrall")) {
        std::vector<std::string> sb_ = fi.lifetime_params;
        std::unordered_set<std::string> srb_;
        for (auto p0_ : fi.param_types) collect_param_regions_(p0_, srb_);
        collect_param_regions_(fi.ret_type, srb_);
        std::vector<std::string> srs_(srb_.begin(), srb_.end());
        std::sort(srs_.begin(), srs_.end());
        for (auto& r_ : srs_)
            if (!outlives_is_static(r_) &&
                std::find(sb_.begin(), sb_.end(), r_) == sb_.end())
                sb_.push_back(r_);
        sc_ret_ = subst_call_ret_lts_(fi.param_types, sb_, arg_exprs, sc_ret_);
        if (sc_ret_ != fi.ret_type) logos::probe::census("stcg.static.differs");
    }
    return builder().call(fi.symbol_name.empty() ? mangled : fi.symbol_name, {}, std::move(arg_exprs), sc_ret_);
===
name: lrall
file: src/compiler/sema_expr.cpp
---
    TypeRef ret = (struct_subst.empty() && lt_subst.empty())
        ? fi.ret_type
        : subst_type_sema(fi.ret_type, struct_subst, lt_subst);
---
    TypeRef ret = (struct_subst.empty() && lt_subst.empty())
        ? fi.ret_type
        : subst_type_sema(fi.ret_type, struct_subst, lt_subst);
    // PROBE stcg / lrall (method half) — src/compiler/PROBES.md 2026-09-12r.
    if (logos::probe::on("stcg") || logos::probe::on("lrall")) {
        std::vector<std::string> mb_ = fi.lifetime_params;
        std::unordered_set<std::string> mrb_;
        for (auto p0_ : fi.param_types) collect_param_regions_(p0_, mrb_);
        collect_param_regions_(fi.ret_type, mrb_);
        std::vector<std::string> mrs_(mrb_.begin(), mrb_.end());
        std::sort(mrs_.begin(), mrs_.end());
        for (auto& r_ : mrs_)
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
        if (mret_ != ret) logos::probe::census("stcg.method.differs");
        ret = mret_;
    }
===
name: lbd
file: src/compiler/sema_expr.cpp
---
        ret = subst_call_ret_lts_(fi.param_types, fi.lifetime_params, arg_exprs, ret);
---
    {   // PROBE stcg / lrall (generic half) — src/compiler/PROBES.md 2026-09-12r.
        std::vector<std::string> gb_ = fi.lifetime_params;
        if (logos::probe::on("stcg") || logos::probe::on("lrall")) {
            std::unordered_set<std::string> grb_;
            for (auto p0_ : fi.param_types) collect_param_regions_(p0_, grb_);
            collect_param_regions_(fi.ret_type, grb_);
            std::vector<std::string> grs_(grb_.begin(), grb_.end());
            std::sort(grs_.begin(), grs_.end());
            for (auto& r_ : grs_)
                if (!outlives_is_static(r_) &&
                    std::find(gb_.begin(), gb_.end(), r_) == gb_.end())
                    gb_.push_back(r_);
            if (gb_.size() != fi.lifetime_params.size())
                logos::probe::census("stcg.generic.aug");
        }
        ret = subst_call_ret_lts_(fi.param_types, gb_, arg_exprs, ret);
    }
===
name: esdrf
file: src/compiler/borrow_check.cpp
---
            case EC::AddrOfTemp: {
                auto cur = lir_view::EAddrOfTempView{e}.inner();
---
            case EC::AddrOfTemp: {
                // PROBE esdrf / esc / lbd / lrall — src/compiler/PROBES.md 2026-09-12r.
                if (logos::probe::on("esdrf") || logos::probe::on("esc") ||
                    logos::probe::on("lbd") || logos::probe::on("lrall")) {
                    BorrowPlace bp_ = extract_borrow_place(
                        lir_view::EAddrOfTempView{e}.inner(), prog_.type_pool.impl());
                    if (bp_.through_ref) { logos::probe::census("esdrf.stop"); return; }
                    if (bp_.root_type && bp_.root_type.kind() == LogosType::Kind::Ptr) return;
                    emit(bp_.root);
                    return;
                }
                auto cur = lir_view::EAddrOfTempView{e}.inner();
===
name: escnd
file: src/compiler/borrow_check.cpp
---
            case EC::Cast:
                collect_borrowed_local_roots(lir_view::ECastView{e}.operand(), out);
                return;
---
            case EC::Cast:
                collect_borrowed_local_roots(lir_view::ECastView{e}.operand(), out);
                return;
            case EC::Call:
            case EC::MethodCall: {
                // PROBE escnd / esc / lbd / lrall — src/compiler/PROBES.md 2026-09-12r.
                if (!(logos::probe::on("escnd") || logos::probe::on("esc") ||
                      logos::probe::on("lbd") || logos::probe::on("lrall"))) return;
                const auto* pool_ = prog_.type_pool.impl();
                const FlowSummary* fs_ = nullptr;
                unsigned base_ = 0;
                lir_view::ExprRef recv_;
                if (e.kind() == EC::Call) {
                    fs_ = flow_of_call(lir_view::ECallView{e}.callee());
                } else {
                    lir_view::EMethodCallView mv_{e};
                    fs_ = flow_of_method(mv_); base_ = 1; recv_ = mv_.receiver();
                }
                logos::probe::census("esc.call.site");
                if (!fs_ || !fs_->available) return;
                auto one_ = [&](lir_view::ExprRef a_, unsigned pi_) {
                    if (!a_ || pi_ >= fs_->nparams) return;
                    if ((fs_->to_result & (1ull << pi_)) == 0) return;
                    if (a_.kind() != EC::AddrOf && a_.kind() != EC::AddrOfTemp) return;
                    size_t before_ = out.size();
                    collect_borrowed_local_roots(a_, out);
                    if (out.size() != before_) logos::probe::census("esc.call.root");
                };
                if (base_ == 1) {
                    lir_view::ExprRef rin_ = recv_;
                    if (rin_ && rin_.kind() == EC::AddrOfTemp)
                        rin_ = lir_view::EAddrOfTempView{rin_}.inner();
                    TypeRef rt_ = rin_ ? rin_.type(pool_) : TypeRef{};
                    if (rt_ && is_ref_kind(rt_) && rt_.pointee()) rt_ = rt_.pointee();
                    if (!(rt_ && type_may_carry_borrow(rt_))) one_(recv_, 0);
                }
                unsigned ai_ = base_;
                if (e.kind() == EC::Call)
                    lir_view::ECallView{e}.each_arg([&](lir_view::ExprRef a_) { one_(a_, ai_++); });
                else
                    lir_view::EMethodCallView{e}.each_arg([&](lir_view::ExprRef a_) { one_(a_, ai_++); });
                return;
            }
===
name: esc
file: src/compiler/borrow_check.cpp
---
                declare_var(name, v.var_slot());  // Phase-1
---
                // PROBE lbd / lrall — src/compiler/PROBES.md 2026-09-12r.
                if ((logos::probe::on("lbd") || logos::probe::on("lrall")) &&
                    val && t && !v.compiler_glue() && !fn_lifetime_params_.empty()) {
                    using EC_ = lir_schema::expr::Code;
                    std::function<void(TypeRef, std::vector<std::string>&, int)> slots_ =
                        [&](TypeRef ty, std::vector<std::string>& o, int d) {
                            if (!ty || d > 24) return;
                            using K = LogosType::Kind;
                            switch (ty.kind()) {
                            case K::Ref: case K::MutRef:
                                o.push_back(std::string(ty.lifetime()));
                                slots_(ty.pointee(), o, d + 1);
                                return;
                            case K::Struct: case K::ZonedStruct: case K::Enum:
                                for (auto& l : ty.lifetime_args()) o.push_back(std::string(l));
                                for (auto a : ty.type_args()) slots_(a, o, d + 1);
                                return;
                            case K::Tuple:
                                for (auto el : ty.tuple_elems()) slots_(el, o, d + 1);
                                return;
                            case K::Array: case K::Slice:
                                slots_(ty.elem(), o, d + 1);
                                return;
                            default:
                                return;
                            }
                        };
                    auto is_binder_ = [&](const std::string& l) {
                        return !l.empty() &&
                               std::find(fn_lifetime_params_.begin(), fn_lifetime_params_.end(),
                                         l) != fn_lifetime_params_.end();
                    };
                    std::vector<std::string> ts_, vs_;
                    slots_(t, ts_, 0);
                    slots_(val.type(pool), vs_, 0);
                    auto val_has_ = [&](const std::string& l) {
                        return std::find(vs_.begin(), vs_.end(), l) != vs_.end();
                    };
                    std::string lt_;
                    bool call_ = val.kind() == EC_::Call || val.kind() == EC_::MethodCall;
                    bool borrow_ = val.kind() == EC_::AddrOf || val.kind() == EC_::AddrOfTemp;
                    if (borrow_ && is_ref_kind(t) && is_binder_(std::string(t.lifetime())) &&
                        !val_has_(std::string(t.lifetime())))
                        lt_ = std::string(t.lifetime());
                    if (call_ && ts_.size() == 1 && is_binder_(ts_[0]) && !val_has_(ts_[0]))
                        lt_ = ts_[0];
                    if (!lt_.empty()) {
                        logos::probe::census("lbd.cand");
                        std::vector<std::string> lesc_;
                        collect_borrowed_local_roots(val, lesc_);
                        bool temp_ = false;
                        if (lesc_.empty() && val.kind() == EC_::AddrOfTemp) {
                            auto in_ = lir_view::EAddrOfTempView{val}.inner();
                            temp_ = in_ && (in_.kind() == EC_::AddrOf ||
                                            in_.kind() == EC_::AddrOfTemp);
                        }
                        if (!lesc_.empty()) {
                            logos::probe::census("lbd.fire.local");
                            report(ln, std::format(
                                "'{}' does not live long enough: it is borrowed into '{}', "
                                "whose declared type requires lifetime {}, which outlives "
                                "this function (E0597)", lesc_.front(), name, lt_));
                        } else if (temp_) {
                            logos::probe::census("lbd.fire.temp");
                            report(ln, std::format(
                                "temporary value dropped while borrowed: '{}' borrows a "
                                "temporary, but its declared type requires lifetime {}, "
                                "which outlives this function (E0716)", name, lt_));
                        }
                    }
                }
                declare_var(name, v.var_slot());  // Phase-1
