name: implanon
file: src/compiler/sema_collect.cpp
---
        if (!self_type && target_resolved)
            self_type = target_resolved;
        if (self_type)
            current_type_params_["Self"] = self_type;
---
        if (!self_type && target_resolved)
            self_type = target_resolved;
        // PROBE implanon / selfall — src/compiler/PROBES.md 2026-09-13a.
        if (self_type && (logos::probe::on("implanon") || logos::probe::on("selfall"))) {
        int an_n_ = 0;
        std::function<TypeRef(TypeRef)> an_rn_ = [&](TypeRef t_) -> TypeRef {
            if (!t_) return t_;
            using K_ = LogosType::Kind;
            auto anon_ = [](std::string_view l_) { return l_ == "'_" || l_ == "_"; };
            switch (t_.kind()) {
            case K_::Ref: case K_::MutRef: {
                std::string l_(t_.lifetime());
                TypeRef p_ = an_rn_(t_.pointee());
                bool ch_ = (p_ != t_.pointee());
                if (anon_(l_)) { l_ = "'__anon" + std::to_string(an_n_++); ch_ = true; logos::probe::census("implanon.ref"); }
                return ch_ ? make_ref(t_.kind() == K_::MutRef, p_, l_) : t_;
            }
            case K_::Struct: case K_::ZonedStruct: case K_::Enum: {
                std::vector<std::string> ls_ = t_.lifetime_args();
                bool ch_ = false;
                for (auto& l_ : ls_)
                    if (anon_(l_)) { l_ = "'__anon" + std::to_string(an_n_++); ch_ = true; logos::probe::census("implanon.structarg"); }
                std::vector<TypeRef> as_;
                for (auto a_ : t_.type_args()) { auto na_ = an_rn_(a_); ch_ |= (na_ != a_); as_.push_back(na_); }
                if (!ch_) return t_;
                if (t_.kind() == K_::Enum)
                    return make_generic_enum(t_.enum_name(), std::move(as_), std::move(ls_), t_.pkg_name());
                if (t_.kind() == K_::ZonedStruct)
                    return make_generic_datatype(t_.struct_name(), std::move(as_), std::move(ls_), t_.pkg_name());
                return make_generic_struct(t_.struct_name(), std::move(as_), std::move(ls_), t_.pkg_name());
            }
            default: return t_;
            }
        };
        TypeRef an_t_ = an_rn_(self_type);
        if (an_t_ != self_type) { logos::probe::census("implanon.collect.renamed"); self_type = an_t_; }
        }
        if (self_type)
            current_type_params_["Self"] = self_type;
===
name: selfbody
file: src/compiler/sema_decl.cpp
---
    TypeRef seed_self = nullptr;
    if (target_resolved) {
---
    // PROBE implanon / selfbody / selfpair / selfall — src/compiler/PROBES.md 2026-09-13a.
    TypeRef sb_self_ = nullptr;
    if (((logos::probe::on("implanon") || logos::probe::on("selfall")) || (logos::probe::on("selfbody") || logos::probe::on("selfpair") || logos::probe::on("selfall"))) && node.has_key(la::TYPE)) {
        auto wn_ = map_of(node.get(la::TYPE.code));
        auto wc_ = code_of(wn_);
        TypeRef wr_ = target_resolved;
        if (!wr_ && wc_ == la::GENERIC_INST) { logos::probe::census("selfbody.resolve"); wr_ = resolve_type(wn_); }
        if (wr_ && (logos::probe::on("implanon") || logos::probe::on("selfall"))) {
            int an_n_ = 0;
        std::function<TypeRef(TypeRef)> an_rn_ = [&](TypeRef t_) -> TypeRef {
            if (!t_) return t_;
            using K_ = LogosType::Kind;
            auto anon_ = [](std::string_view l_) { return l_ == "'_" || l_ == "_"; };
            switch (t_.kind()) {
            case K_::Ref: case K_::MutRef: {
                std::string l_(t_.lifetime());
                TypeRef p_ = an_rn_(t_.pointee());
                bool ch_ = (p_ != t_.pointee());
                if (anon_(l_)) { l_ = "'__anon" + std::to_string(an_n_++); ch_ = true; logos::probe::census("implanon.ref"); }
                return ch_ ? make_ref(t_.kind() == K_::MutRef, p_, l_) : t_;
            }
            case K_::Struct: case K_::ZonedStruct: case K_::Enum: {
                std::vector<std::string> ls_ = t_.lifetime_args();
                bool ch_ = false;
                for (auto& l_ : ls_)
                    if (anon_(l_)) { l_ = "'__anon" + std::to_string(an_n_++); ch_ = true; logos::probe::census("implanon.structarg"); }
                std::vector<TypeRef> as_;
                for (auto a_ : t_.type_args()) { auto na_ = an_rn_(a_); ch_ |= (na_ != a_); as_.push_back(na_); }
                if (!ch_) return t_;
                if (t_.kind() == K_::Enum)
                    return make_generic_enum(t_.enum_name(), std::move(as_), std::move(ls_), t_.pkg_name());
                if (t_.kind() == K_::ZonedStruct)
                    return make_generic_datatype(t_.struct_name(), std::move(as_), std::move(ls_), t_.pkg_name());
                return make_generic_struct(t_.struct_name(), std::move(as_), std::move(ls_), t_.pkg_name());
            }
            default: return t_;
            }
        };
            TypeRef an_t_ = an_rn_(wr_);
            for (int i_ = 0; i_ < an_n_; ++i_) {
                current_impl_lifetime_params_.push_back("'__anon" + std::to_string(i_));
                logos::probe::census("implanon.lower.binder");
            }
            wr_ = an_t_;
        }
        if (wr_ && (logos::probe::on("selfbody") || logos::probe::on("selfpair") || logos::probe::on("selfall")) && impl_tps.empty() &&
            (TypeRef(wr_).kind() == LogosType::Kind::Struct ||
             TypeRef(wr_).kind() == LogosType::Kind::ZonedStruct ||
             TypeRef(wr_).kind() == LogosType::Kind::Enum) &&
            !TypeRef(wr_).lifetime_args().empty())
            sb_self_ = wr_;
    }
    TypeRef seed_self = nullptr;
    if (target_resolved) {
===
name: selfpair
file: src/compiler/sema_decl.cpp
---
    if (!seed_self && impl_is_blanket && !impl_tps.empty())
        seed_self = make_typevar(target);  // blanket on a bound type-var
---
    // PROBE selfbody / selfpair / selfall — src/compiler/PROBES.md 2026-09-13a.
    if (!seed_self && sb_self_) { logos::probe::census("selfbody.seeded"); seed_self = sb_self_; }
    if (!seed_self && impl_is_blanket && !impl_tps.empty())
        seed_self = make_typevar(target);  // blanket on a bound type-var
===
name: selflit
file: src/compiler/sema_expr.cpp
---
        logos::probe::census("lit.mint.sized");
        ng_lt_args.assign(sinfo.lifetime_params.size(), std::string{});
        for (size_t i = 0; i < sinfo.lifetime_params.size(); ++i)
            if (auto it = flt.find(sinfo.lifetime_params[i]); it != flt.end())
                ng_lt_args[i] = it->second;
---
        logos::probe::census("lit.mint.sized");
        ng_lt_args.assign(sinfo.lifetime_params.size(), std::string{});
        for (size_t i = 0; i < sinfo.lifetime_params.size(); ++i)
            if (auto it = flt.find(sinfo.lifetime_params[i]); it != flt.end())
                ng_lt_args[i] = it->second;
        // PROBE selflit / selfpair / selfall — src/compiler/PROBES.md 2026-09-13a.
        if ((logos::probe::on("selflit") || logos::probe::on("selfpair") || logos::probe::on("selfall")) && str_of(node.get(la::NAME.code)) == "Self") {
            logos::probe::census("selflit.site");
            auto sv_ = current_type_params_.find("Self");
            if (sv_ != current_type_params_.end() && sv_->second) {
                std::vector<std::string> sl_ = TypeRef(sv_->second).lifetime_args();
                bool full_ = sl_.size() == sinfo.lifetime_params.size();
                for (auto& x_ : sl_) if (x_.empty()) full_ = false;
                if (full_) {
                    logos::probe::census("selflit.override");
                    if (sl_ != ng_lt_args) logos::probe::census("selflit.differs");
                    ng_lt_args = sl_;
                }
            }
        }
===
name: selfall
file: src/compiler/sema_expr.cpp
---
                if (changed) {
                    logos::probe::census("subst.structlit.gen.differs");
---
                // PROBE selflit / selfpair / selfall (generic literal) — src/compiler/PROBES.md 2026-09-13a.
                if ((logos::probe::on("selflit") || logos::probe::on("selfpair") || logos::probe::on("selfall")) && str_of(node.get(la::NAME.code)) == "Self") {
                    logos::probe::census("selflit.gen.site");
                    auto sv_ = current_type_params_.find("Self");
                    if (sv_ != current_type_params_.end() && sv_->second) {
                        std::vector<std::string> sl_ = TypeRef(sv_->second).lifetime_args();
                        bool full_ = sl_.size() == sinfo.lifetime_params.size();
                        for (auto& x_ : sl_) if (x_.empty()) full_ = false;
                        if (full_) { logos::probe::census("selflit.gen.override"); nl = sl_; changed = true; }
                    }
                }
                if (changed) {
                    logos::probe::census("subst.structlit.gen.differs");
===
