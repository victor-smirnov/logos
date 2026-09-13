name: lrimpl
file: src/compiler/sema_expr.cpp
---
    return builder().call(fi.symbol_name.empty() ? mangled : fi.symbol_name, {}, std::move(arg_exprs), fi.ret_type);
---
    // PROBE stcg / lrall — src/compiler/PROBES.md 2026-09-12r.
    TypeRef sc_ret_ = fi.ret_type;
    if (logos::probe::on("stcg") || (logos::probe::on("lrall") || logos::probe::on("lrimpl") || logos::probe::on("lragg") || logos::probe::on("lrtemp") || logos::probe::on("lrvar") || logos::probe::on("lrx"))) {
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
name: lragg
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
    if (logos::probe::on("stcg") || (logos::probe::on("lrall") || logos::probe::on("lrimpl") || logos::probe::on("lragg") || logos::probe::on("lrtemp") || logos::probe::on("lrvar") || logos::probe::on("lrx"))) {
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
name: lrtemp
file: src/compiler/sema_expr.cpp
---
        ret = subst_call_ret_lts_(fi.param_types, fi.lifetime_params, arg_exprs, ret);
---
    {   // PROBE stcg / lrall (generic half) — src/compiler/PROBES.md 2026-09-12r.
        std::vector<std::string> gb_ = fi.lifetime_params;
        if (logos::probe::on("stcg") || (logos::probe::on("lrall") || logos::probe::on("lrimpl") || logos::probe::on("lragg") || logos::probe::on("lrtemp") || logos::probe::on("lrvar") || logos::probe::on("lrx"))) {
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
name: lrvar
file: src/compiler/borrow_check.cpp
---
            case EC::AddrOfTemp: {
                auto cur = lir_view::EAddrOfTempView{e}.inner();
---
            case EC::AddrOfTemp: {
                // PROBE esdrf / esc / lbd / lrall — src/compiler/PROBES.md 2026-09-12r.
                if (logos::probe::on("esdrf") || logos::probe::on("esc") ||
                    logos::probe::on("lbd") || (logos::probe::on("lrall") || logos::probe::on("lrimpl") || logos::probe::on("lragg") || logos::probe::on("lrtemp") || logos::probe::on("lrvar") || logos::probe::on("lrx"))) {
                    BorrowPlace bp_ = extract_borrow_place(
                        lir_view::EAddrOfTempView{e}.inner(), prog_.type_pool.impl());
                    if (bp_.through_ref) { logos::probe::census("esdrf.stop"); return; }
                    if (bp_.root_type && bp_.root_type.kind() == LogosType::Kind::Ptr) return;
                    emit(bp_.root);
                    return;
                }
                auto cur = lir_view::EAddrOfTempView{e}.inner();
===
name: lrx
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
                      logos::probe::on("lbd") || (logos::probe::on("lrall") || logos::probe::on("lrimpl") || logos::probe::on("lragg") || logos::probe::on("lrtemp") || logos::probe::on("lrvar") || logos::probe::on("lrx")))) return;
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
            case EC::EnumLitData:
                // PROBE lragg / lrx — src/compiler/PROBES.md 2026-09-12r.
                if (!(logos::probe::on("lragg") || logos::probe::on("lrx"))) return;
                lir_view::EEnumLitDataView{e}.each_payload(
                    [&](lir_view::ExprRef fv) { collect_borrowed_local_roots(fv, out); });
                return;
            case EC::ArrLit:
                if (!(logos::probe::on("lragg") || logos::probe::on("lrx"))) return;
                lir_view::EArrLitView{e}.each_elem(
                    [&](lir_view::ExprRef fv) { collect_borrowed_local_roots(fv, out); });
                return;
            case EC::VarRef: {
                // PROBE lrvar / lrx — src/compiler/PROBES.md 2026-09-12r.
                if (!(logos::probe::on("lrvar") || logos::probe::on("lrx"))) return;
                std::string vn_(lir_view::EVarRefView{e}.name());
                if (vn_.empty()) return;
                for (auto& s_ : ref_sources_under(vn_)) {
                    size_t b_ = out.size();
                    emit(s_.name);
                    if (out.size() != b_) logos::probe::census("lrvar.root");
                }
                return;
            }
===
name: lrall
file: src/compiler/borrow_check.cpp
---
                declare_var(name, v.var_slot());  // Phase-1
---
                // PROBE lbd / lrall — src/compiler/PROBES.md 2026-09-12r.
                if ((logos::probe::on("lbd") || (logos::probe::on("lrall") || logos::probe::on("lrimpl") || logos::probe::on("lragg") || logos::probe::on("lrtemp") || logos::probe::on("lrvar") || logos::probe::on("lrx"))) &&
                    val && t && !v.compiler_glue() &&
                    (!fn_lifetime_params_.empty() ||
                     ((logos::probe::on("lrimpl") || logos::probe::on("lrx")) && !sig_regions_.empty()))) {
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
                        if (l.empty() || outlives_is_static(l) || l == "'_") return false;
                        if (std::find(fn_lifetime_params_.begin(), fn_lifetime_params_.end(),
                                      l) != fn_lifetime_params_.end()) return true;
                        if ((logos::probe::on("lrimpl") || logos::probe::on("lrx")) &&
                            sig_regions_.count(l) > 0) {
                            logos::probe::census("lrimpl.sigbinder");
                            return true;
                        }
                        return false;
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
                    bool agg_ = (logos::probe::on("lragg") || logos::probe::on("lrx")) &&
                        (val.kind() == EC_::StructLit || val.kind() == EC_::TupleLit ||
                         val.kind() == EC_::ArrLit || val.kind() == EC_::EnumLitData);
                    if ((call_ || agg_) && ts_.size() == 1 && is_binder_(ts_[0]) && !val_has_(ts_[0]))
                        lt_ = ts_[0];
                    if (!lt_.empty()) {
                        logos::probe::census("lbd.cand");
                        std::vector<std::string> lesc_;
                        collect_borrowed_local_roots(val, lesc_);
                        bool temp_ = false;
                        if (lesc_.empty() && val.kind() == EC_::AddrOfTemp) {
                            auto in_ = lir_view::EAddrOfTempView{val}.inner();
                            temp_ = in_ && (in_.kind() == EC_::AddrOf ||
                                            in_.kind() == EC_::AddrOfTemp ||
                                            ((logos::probe::on("lrtemp") || logos::probe::on("lrx")) &&
                                             (in_.kind() == EC_::Call || in_.kind() == EC_::MethodCall)));
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
===
name: sigmember
file: src/compiler/borrow_check.cpp
---
    std::vector<std::string>             fn_lifetime_params_;
---
    std::vector<std::string>             fn_lifetime_params_;
    // PROBE lrimpl / lrx — every named region in the signature. src/compiler/PROBES.md 2026-09-12r.
    std::unordered_set<std::string>      sig_regions_;
===
name: sigfill
file: src/compiler/borrow_check.cpp
---
        for (auto lp : fn.lifetime_params()) fn_lifetime_params_.push_back(std::string(lp));
---
        for (auto lp : fn.lifetime_params()) fn_lifetime_params_.push_back(std::string(lp));
        sig_regions_.clear();   // PROBE lrimpl / lrx — src/compiler/PROBES.md 2026-09-12r.
        {
            std::function<void(TypeRef, int)> sr_ = [&](TypeRef ty, int d) {
                if (!ty || d > 24) return;
                using K = LogosType::Kind;
                auto put_ = [&](std::string_view l) {
                    if (!l.empty() && !lt_is_minted(l) && !outlives_is_static(l) && l != "'_")
                        sig_regions_.insert(std::string(l));
                };
                switch (ty.kind()) {
                case K::Ref: case K::MutRef:
                    put_(ty.lifetime());
                    sr_(ty.pointee(), d + 1);
                    return;
                case K::Struct: case K::ZonedStruct: case K::Enum:
                    for (auto& l : ty.lifetime_args()) put_(l);
                    for (auto a : ty.type_args()) sr_(a, d + 1);
                    return;
                case K::Tuple:
                    for (auto el : ty.tuple_elems()) sr_(el, d + 1);
                    return;
                case K::Array: case K::Slice:
                    sr_(ty.elem(), d + 1);
                    return;
                default:
                    return;
                }
            };
            for (auto& p : fn.params()) sr_(p.type(fn_pool), 0);
            sr_(fn.ret_type(fn_pool), 0);
        }
