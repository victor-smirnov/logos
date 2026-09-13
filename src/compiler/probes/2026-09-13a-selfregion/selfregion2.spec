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
        // PROBE implanon / selfv / selfvg — src/compiler/PROBES.md 2026-09-13a.
        if (self_type && (logos::probe::on("implanon") || logos::probe::on("selfv") || logos::probe::on("selfvg"))) {
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
        auto tk = TypeRef(target_resolved).kind();
        if (tk == LogosType::Kind::Tuple || LogosType::is_fn_value_kind(tk) ||
            tk == LogosType::Kind::Ref   || tk == LogosType::Kind::MutRef)
            seed_self = target_resolved;
        else if ((tk == LogosType::Kind::Struct ||
                  tk == LogosType::Kind::ZonedStruct ||
                  tk == LogosType::Kind::Enum) &&
                 impl_tps.empty() && !TypeRef(target_resolved).type_args().empty())
            seed_self = target_resolved;  // concrete-type-arg, no impl param
    }
    if (!seed_self && impl_is_blanket && !impl_tps.empty())
        seed_self = make_typevar(target);  // blanket on a bound type-var
---
    // PROBE implanon / selfbody / selfv / selfvg — src/compiler/PROBES.md 2026-09-13a.
    TypeRef sb_self_ = nullptr;
    if (((logos::probe::on("implanon") || logos::probe::on("selfv") || logos::probe::on("selfvg")) || (logos::probe::on("selfbody") || logos::probe::on("selfv") || logos::probe::on("selfvg"))) && node.has_key(la::TYPE)) {
        auto wn_ = map_of(node.get(la::TYPE.code));
        TypeRef wr_ = target_resolved;
        if (!wr_ && code_of(wn_) == la::GENERIC_INST) { logos::probe::census("selfbody.resolve"); wr_ = resolve_type(wn_); }
        if (wr_ && (logos::probe::on("implanon") || logos::probe::on("selfv") || logos::probe::on("selfvg"))) {
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
        if (wr_ && (logos::probe::on("selfbody") || logos::probe::on("selfv") || logos::probe::on("selfvg")) && (impl_tps.empty() || logos::probe::on("selfvg")) &&
            (TypeRef(wr_).kind() == LogosType::Kind::Struct ||
             TypeRef(wr_).kind() == LogosType::Kind::ZonedStruct ||
             TypeRef(wr_).kind() == LogosType::Kind::Enum) &&
            !TypeRef(wr_).lifetime_args().empty()) {
            logos::probe::census(impl_tps.empty() ? "selfbody.candidate" : "selfbody.candidate.generic");
            sb_self_ = wr_;
        }
    }
    TypeRef seed_self = nullptr;
    if (target_resolved) {
        auto tk = TypeRef(target_resolved).kind();
        if (tk == LogosType::Kind::Tuple || LogosType::is_fn_value_kind(tk) ||
            tk == LogosType::Kind::Ref   || tk == LogosType::Kind::MutRef)
            seed_self = target_resolved;
        else if ((tk == LogosType::Kind::Struct ||
                  tk == LogosType::Kind::ZonedStruct ||
                  tk == LogosType::Kind::Enum) &&
                 impl_tps.empty() && !TypeRef(target_resolved).type_args().empty())
            seed_self = target_resolved;  // concrete-type-arg, no impl param
    }
    if (!seed_self && sb_self_) { logos::probe::census("selfbody.seeded"); seed_self = sb_self_; }
    if (!seed_self && impl_is_blanket && !impl_tps.empty())
        seed_self = make_typevar(target);  // blanket on a bound type-var
===
name: selfv
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
        // PROBE selflitv / selfv / selfvg — src/compiler/PROBES.md 2026-09-13a. The literal
        // spelled `Self` IS the impl self type: its VALUE regions must be a subtype of Self's,
        // and the literal then carries Self's. `hint_struct_type_` is the Self the top of this
        // function resolved (no second lookup of the name).
        if ((logos::probe::on("selflitv") || logos::probe::on("selfv") || logos::probe::on("selfvg")) && str_of(node.get(la::NAME.code)) == "Self") {
            logos::probe::census("selflitv.site");
            if (hint_struct_type_ && TypeRef(hint_struct_type_).struct_name() == std::string(sname)) {
                std::vector<std::string> sl_ = TypeRef(hint_struct_type_).lifetime_args();
                bool full_ = sl_.size() == sinfo.lifetime_params.size();
                for (auto& x_ : sl_) if (x_.empty()) full_ = false;
                if (full_) {
                    logos::probe::census("selflitv.override");
                    TypeRef vt_ = slit_is_zoned ? make_generic_datatype(std::string(sname), std::vector<TypeRef>{}, ng_lt_args)
                                                : make_generic_struct(std::string(sname), std::vector<TypeRef>{}, ng_lt_args);
                    TypeRef st_ = slit_is_zoned ? make_generic_datatype(std::string(sname), std::vector<TypeRef>{}, sl_)
                                                : make_generic_struct(std::string(sname), std::vector<TypeRef>{}, sl_);
                    if (sl_ != ng_lt_args) logos::probe::census("selflitv.differs");
                    check_variance(vt_, st_, "struct literal 'Self'", /*permissive=*/true);
                    ng_lt_args = sl_;
                }
            }
        }
===
name: selfvg
file: src/compiler/sema_expr.cpp
---
                if (changed) {
                    logos::probe::census("subst.structlit.gen.differs");
---
                // PROBE selflitv / selfv / selfvg (generic literal) — src/compiler/PROBES.md 2026-09-13a.
                if ((logos::probe::on("selflitv") || logos::probe::on("selfv") || logos::probe::on("selfvg")) && str_of(node.get(la::NAME.code)) == "Self") {
                    logos::probe::census("selflitv.gen.site");
                    if (hint_struct_type_ && TypeRef(hint_struct_type_).struct_name() == std::string(sname)) {
                        std::vector<std::string> sl_ = TypeRef(hint_struct_type_).lifetime_args();
                        bool full_ = sl_.size() == sinfo.lifetime_params.size();
                        for (auto& x_ : sl_) if (x_.empty()) full_ = false;
                        if (full_) {
                            logos::probe::census("selflitv.gen.override");
                            TypeRef vt_ = slit_is_zoned ? make_generic_datatype(std::string(sname), args, nl)
                                                        : make_generic_struct(std::string(sname), args, nl);
                            TypeRef st_ = slit_is_zoned ? make_generic_datatype(std::string(sname), args, sl_)
                                                        : make_generic_struct(std::string(sname), args, sl_);
                            if (sl_ != nl) logos::probe::census("selflitv.gen.differs");
                            check_variance(vt_, st_, "struct literal 'Self'", /*permissive=*/true);
                            nl = sl_; changed = true;
                        }
                    }
                }
                if (changed) {
                    logos::probe::census("subst.structlit.gen.differs");
===
name: selfve
file: src/compiler/sema_expr.cpp
---
    if (class_name == "Self") {
        auto sit = current_type_params_.find("Self");
        if (sit != current_type_params_.end() && sit->second) {
            auto st = TypeRef(sit->second);
            std::string resolved;
            if (st.kind() == LogosType::Kind::Struct ||
                st.kind() == LogosType::Kind::ZonedStruct)
                resolved = concrete_struct_name(sit->second);
            else if (st.kind() == LogosType::Kind::Enum)
                resolved = std::string(st.enum_name());
            if (!resolved.empty()) class_name = std::move(resolved);
        }
    }

    // Generic container static call (ADR 0020 wave-0, S2): `Map::new()` where
    // the expected type is a concrete instance `Map$G2$…`. Unlike a generic
    // STRUCT, the container base `Map` has no template, so the normal
    // class_name→instance retarget can't fire; redirect class_name to the
    // mangled instance carried by the return-type hint so `<mangled>::new`
    // resolves once the family lands (or the pending-defer path suppresses
    // while it is still being generated).
    if (is_generic_container_base(class_name) && hint_call_return_type_) {
        TypeRef h(hint_call_return_type_);
        if (h.kind() == LogosType::Kind::Struct ||
            h.kind() == LogosType::Kind::ZonedStruct) {
            std::string hn(h.struct_name());
            if (hn.size() > class_name.size() &&
                hn.compare(0, class_name.size(), class_name) == 0 &&
                hn[class_name.size()] == '$')
                class_name = std::move(hn);
        }
    }

    // If "class_name" is actually an enum, redirect to enum lit with data —
    // but only when method_name is a variant. Otherwise it's a static method
    // call on the enum (trait impl), which falls through to the regular
    // static-call resolution below.
    {
        // G160-2: peel a non-generic type-alias to an enum (`type FooAlias =
        // Foo; FooAlias::Bar(1)`) so variant construction through the alias
        // resolves to the target enum.
        std::string enum_name(class_name);
        {
            auto ait = alias_find(class_name);
            if (ait != type_aliases_.end() && ait->second.type_params.empty() &&
                ait->second.type &&
                TypeRef(ait->second.type).kind() == LogosType::Kind::Enum)
                enum_name = std::string(TypeRef(ait->second.type).enum_name());
        }
        auto [epkg_sc, esi_sc] = find_enum_by_name(enum_name);
        bool is_enum = esi_sc != nullptr;
        if (!is_enum) is_enum = enums_.count(enum_name) > 0;
        if (is_enum) {
            bool is_variant = false;
            auto eit = esi_sc ? enums_.find(sema_key(epkg_sc, enum_name))
                              : enums_.end();
            if (eit == enums_.end()) eit = enums_.find(enum_name);
            if (eit != enums_.end())
                for (auto& v : eit->second.variants)
                    if (v.name == method_name) { is_variant = true; break; }
            if (is_variant) {
                return lower_enum_lit_data_from_static(node, enum_name, method_name);
---
    TypeRef selfve_self_ = nullptr;  // PROBE selfve
    if (class_name == "Self") {
        auto sit = current_type_params_.find("Self");
        if (sit != current_type_params_.end() && sit->second) {
            auto st = TypeRef(sit->second);
            std::string resolved;
            if (st.kind() == LogosType::Kind::Struct ||
                st.kind() == LogosType::Kind::ZonedStruct)
                resolved = concrete_struct_name(sit->second);
            else if (st.kind() == LogosType::Kind::Enum)
                resolved = std::string(st.enum_name());
            if (!resolved.empty()) class_name = std::move(resolved);
            // PROBE selfve — src/compiler/PROBES.md 2026-09-13a: carry the Self this block
            // already looked up, for a variant constructor spelled `Self::V(..)`.
            if ((logos::probe::on("selfve")) && st.kind() == LogosType::Kind::Enum) selfve_self_ = sit->second;
        }
    }

    // Generic container static call (ADR 0020 wave-0, S2): `Map::new()` where
    // the expected type is a concrete instance `Map$G2$…`. Unlike a generic
    // STRUCT, the container base `Map` has no template, so the normal
    // class_name→instance retarget can't fire; redirect class_name to the
    // mangled instance carried by the return-type hint so `<mangled>::new`
    // resolves once the family lands (or the pending-defer path suppresses
    // while it is still being generated).
    if (is_generic_container_base(class_name) && hint_call_return_type_) {
        TypeRef h(hint_call_return_type_);
        if (h.kind() == LogosType::Kind::Struct ||
            h.kind() == LogosType::Kind::ZonedStruct) {
            std::string hn(h.struct_name());
            if (hn.size() > class_name.size() &&
                hn.compare(0, class_name.size(), class_name) == 0 &&
                hn[class_name.size()] == '$')
                class_name = std::move(hn);
        }
    }

    // If "class_name" is actually an enum, redirect to enum lit with data —
    // but only when method_name is a variant. Otherwise it's a static method
    // call on the enum (trait impl), which falls through to the regular
    // static-call resolution below.
    {
        // G160-2: peel a non-generic type-alias to an enum (`type FooAlias =
        // Foo; FooAlias::Bar(1)`) so variant construction through the alias
        // resolves to the target enum.
        std::string enum_name(class_name);
        {
            auto ait = alias_find(class_name);
            if (ait != type_aliases_.end() && ait->second.type_params.empty() &&
                ait->second.type &&
                TypeRef(ait->second.type).kind() == LogosType::Kind::Enum)
                enum_name = std::string(TypeRef(ait->second.type).enum_name());
        }
        auto [epkg_sc, esi_sc] = find_enum_by_name(enum_name);
        bool is_enum = esi_sc != nullptr;
        if (!is_enum) is_enum = enums_.count(enum_name) > 0;
        if (is_enum) {
            bool is_variant = false;
            auto eit = esi_sc ? enums_.find(sema_key(epkg_sc, enum_name))
                              : enums_.end();
            if (eit == enums_.end()) eit = enums_.find(enum_name);
            if (eit != enums_.end())
                for (auto& v : eit->second.variants)
                    if (v.name == method_name) { is_variant = true; break; }
            if (is_variant) {
                // PROBE selfve — src/compiler/PROBES.md 2026-09-13a.
                if ((logos::probe::on("selfve")) && selfve_self_) {
                    logos::probe::census("selfve.site");
                    auto ev_ = lower_enum_lit_data_from_static(node, enum_name, method_name);
                    if (ev_ && !TypeRef(selfve_self_).lifetime_args().empty()) {
                        logos::probe::census("selfve.check");
                        check_variance(expr_type(ev_), selfve_self_, "enum literal 'Self'", /*permissive=*/true);
                    }
                    return ev_;
                }
                return lower_enum_lit_data_from_static(node, enum_name, method_name);
===
