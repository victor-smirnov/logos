name: fnptrelide2
file: src/compiler/sema_impl.hpp
---
        if (!types_compatible(from, to)) return;  // outer check handles it
        last_rigid_mismatch() = {};
        last_meet_refusal() = {};
        if (variance_ok(from, to, permissive)) return;
---
        if (!types_compatible(from, to)) return;  // outer check handles it
        last_rigid_mismatch() = {};
        last_meet_refusal() = {};
        // PROBES 2026-09-14j-fnptrbinder batch 2, door E' (fnptrelide2 / fnptrbinder2 / fnptrbinder2nm): a fn-pointer SUP's elided
        // regions are its own rigid binders; the SUB's own binders take the sup's region by position, a MEET of them when offered two.
        TypeRef vfrom_ = from, vto_ = to;
        {
            const bool fpm_ = logos::probe::on("fnptrelide2") || logos::probe::on("fnptrbinder2");
            const bool fpe_ = fpm_ || logos::probe::on("fnptrbinder2nm");
            const bool fps_ = fpe_;
            if (fps_ && TypeRef(to).kind() == LogosType::Kind::FnPtr &&
                LogosType::is_fn_value_kind(TypeRef(from).kind())) {
                logos::probe::census(permissive ? "fnptr.cv.arrive.permissive" : "fnptr.cv.arrive.strict");
                vto_ = fnptr_sup_view_(to);
                if (fpe_) vfrom_ = fnptr_sub_view_(from, vto_, fpm_);
            } else if (fps_ && (fpv_has_fn_(from) || fpv_has_fn_(to))) {
                logos::probe::census(TypeRef(to).kind() == LogosType::Kind::FnPtr ||
                                             LogosType::is_fn_value_kind(TypeRef(to).kind())
                                         ? "fnptr.cv.supnotptr" : "fnptr.cv.nested");
            }
        }
        if (variance_ok(vfrom_, vto_, permissive)) return;
===
name: fnptrbinder2nm
file: src/compiler/sema_impl.hpp
---
    // Emit an "X: variance mismatch …" error if from ↛ to under variance.
---
    // PROBES 2026-09-14j-fnptrbinder batch 2 door E' view (fnptrelide2 / fnptrbinder2 / fnptrbinder2nm). Every region slot of a
    // type, in one fixed pre-order, passed through f; nested fn-value types keep their own binders (not descended).
    template <class F>
    TypeRef fpv_walk_(TypeRef t, F& f, int d = 0) {
        if (!t || d > 24) return t;
        using K = LogosType::Kind;
        const auto k = TypeRef(t).kind();
        if (k == K::Ref || k == K::MutRef) {
            std::string lt_ = f(std::string_view(TypeRef(t).lifetime()));
            return make_ref(k == K::MutRef, fpv_walk_(TypeRef(t).pointee(), f, d + 1), lt_);
        }
        if (k == K::Array)
            return make_array(fpv_walk_(TypeRef(t).elem(), f, d + 1), TypeRef(t).arr_size(),
                              std::string_view(TypeRef(t).arr_size_var()));
        if (k == K::Tuple) {
            std::vector<TypeRef> es_;
            for (auto e : TypeRef(t).tuple_elems()) es_.push_back(fpv_walk_(e, f, d + 1));
            return make_tuple_type(std::move(es_));
        }
        if (k == K::Slice && TypeRef(t).slice_owning_kind() == TypeRef::OwningKind::Borrow) {
            std::string lt_ = f(std::string_view(TypeRef(t).lifetime()));
            return make_slice_type(fpv_walk_(TypeRef(t).elem(), f, d + 1), TypeRef(t).mut_ptr(),
                                   TypeRef::OwningKind::Borrow, lt_);
        }
        if ((k == K::Struct || k == K::ZonedStruct || k == K::Enum) &&
            (!TypeRef(t).lifetime_args().empty() || !TypeRef(t).type_args().empty())) {
            std::vector<std::string> ls_;
            for (auto& l : TypeRef(t).lifetime_args()) ls_.push_back(f(std::string_view(l)));
            std::vector<TypeRef> as_;
            for (auto a : TypeRef(t).type_args()) as_.push_back(fpv_walk_(a, f, d + 1));
            if (k == K::Enum)
                return make_generic_enum(TypeRef(t).enum_name(), std::move(as_), std::move(ls_), TypeRef(t).pkg_name());
            if (k == K::ZonedStruct)
                return make_generic_datatype(TypeRef(t).struct_name(), std::move(as_), std::move(ls_), TypeRef(t).pkg_name());
            return make_generic_struct(TypeRef(t).struct_name(), std::move(as_), std::move(ls_), TypeRef(t).pkg_name());
        }
        return t;
    }
    bool fpv_has_fn_(TypeRef t, int d = 0) {
        if (!t || d > 24) return false;
        if (LogosType::is_fn_value_kind(TypeRef(t).kind())) return true;
        if (TypeRef(t).pointee() && fpv_has_fn_(TypeRef(t).pointee(), d + 1)) return true;
        if (TypeRef(t).elem() && fpv_has_fn_(TypeRef(t).elem(), d + 1)) return true;
        for (auto a : TypeRef(t).type_args()) if (fpv_has_fn_(a, d + 1)) return true;
        for (auto e : TypeRef(t).tuple_elems()) if (fpv_has_fn_(e, d + 1)) return true;
        return false;
    }
    TypeRef fpv_fn_(TypeRef t, std::vector<TypeRef> ps, TypeRef r) {
        LogosTypeBuilder nt;
        nt.kind = TypeRef(t).kind();
        nt.closure_params = std::move(ps);
        nt.closure_ret = r;
        if (nt.kind == LogosType::Kind::Closure) {
            nt.trait_name = std::string(TypeRef(t).trait_name());
            nt.const_val = TypeRef(t).const_val();
            nt.lifetime = std::string(TypeRef(t).lifetime());
        } else {
            nt.struct_name = std::string(TypeRef(t).struct_name());   // FnPtr ABI tag / FnItem identity
        }
        if (nt.kind == LogosType::Kind::FnItem)
            for (auto a : TypeRef(t).type_args()) nt.type_args.push_back(a);
        return pool_->alloc(std::move(nt));
    }
    static bool fpv_elided_(std::string_view lt) { return lt.empty() || lt == "'_"; }
    // SUP side: each elided slot of the fn pointer's params is a fresh rigid placeholder; an elided return slot is the
    // sole elided input's placeholder when there is exactly one, else its own.
    TypeRef fnptr_sup_view_(TypeRef to) {
        static unsigned fpn_ = 0;
        size_t n_in_ = 0;
        std::string sole_;
        auto tok_ = [&](std::string_view lt) -> std::string {
            if (!fpv_elided_(lt)) return std::string(lt);
            ++n_in_;
            sole_ = "'%f" + std::to_string(++fpn_);
            return sole_;
        };
        std::vector<TypeRef> ps_;
        for (auto p : TypeRef(to).closure_params()) ps_.push_back(fpv_walk_(p, tok_));
        const size_t n_ = n_in_;
        const std::string s_ = sole_;
        auto rtok_ = [&](std::string_view lt) -> std::string {
            if (!fpv_elided_(lt)) return std::string(lt);
            return n_ == 1 ? s_ : "'%f" + std::to_string(++fpn_);
        };
        TypeRef r_ = fpv_walk_(TypeRef(to).closure_ret(), rtok_);
        if (n_) logos::probe::census("fnptr.sup.elided");
        return fpv_fn_(to, std::move(ps_), r_);
    }
    // SUB side: the sub's OWN binders — an elided slot, a renamed `for<>` binder ('%h), and for a fn ITEM every
    // non-'static name — are existential: each takes the sup's region at its first position.
    TypeRef fnptr_sub_view_(TypeRef from, TypeRef sup, bool meet) {
        using K = LogosType::Kind;
        const auto fk_ = TypeRef(from).kind();
        auto sp_ = TypeRef(from).closure_params();
        auto pp_ = TypeRef(sup).closure_params();
        if (sp_.size() != pp_.size()) return from;
        auto is_b_ = [&](std::string_view lt) {
            if (fpv_elided_(lt) || lt.starts_with("'%h")) return true;
            if (outlives_is_static(lt)) return false;
            if (fk_ == K::FnItem || closure_minted_lts().count(std::string(lt))) return true;
            return current_lt_binders().count(std::string(lt)) == 0;   // a name no enclosing scope declares is the value's own
        };
        std::vector<std::string> acc_;
        auto rec_ = [&](std::string_view lt) -> std::string { acc_.push_back(std::string(lt)); return std::string(lt); };
        std::vector<std::vector<std::string>> Pi_, Si_;
        for (auto p : pp_) { acc_.clear(); fpv_walk_(p, rec_); Pi_.push_back(acc_); }
        for (auto s : sp_) { acc_.clear(); fpv_walk_(s, rec_); Si_.push_back(acc_); }
        std::unordered_map<std::string, std::string> m_;
        std::unordered_map<std::string, std::vector<std::string>> offers_;
        std::vector<std::string> order_;
        size_t n_slots_ = 0;
        for (size_t i = 0; i < sp_.size(); ++i) {
            n_slots_ += Si_[i].size();
            if (Si_[i].size() != Pi_[i].size()) continue;
            for (size_t j = 0; j < Si_[i].size(); ++j)
                if (is_b_(Si_[i][j]) && !fpv_elided_(Si_[i][j])) {
                    auto& v_ = offers_[Si_[i][j]];
                    if (v_.empty()) order_.push_back(Si_[i][j]);
                    if (std::find(v_.begin(), v_.end(), Pi_[i][j]) == v_.end()) v_.push_back(Pi_[i][j]);
                }
        }
        for (auto& k_ : order_) {
            auto& v_ = offers_[k_];
            if (v_.size() > 1 && meet) logos::probe::census("fnptr.sub.meet");
            m_[k_] = (v_.size() > 1 && meet) ? mint_meet_token(v_) : v_[0];
        }
        std::string sole_;
        std::vector<TypeRef> ps_;
        for (size_t i = 0; i < sp_.size(); ++i) {
            size_t j_ = 0;
            const bool aligned_ = Si_[i].size() == Pi_[i].size();
            auto sub_ = [&](std::string_view lt) -> std::string {
                const size_t jj = j_++;
                std::string out(lt);
                if (is_b_(lt)) {
                    if (fpv_elided_(lt)) { if (aligned_) out = Pi_[i][jj]; }
                    else if (auto it = m_.find(std::string(lt)); it != m_.end()) out = it->second;
                }
                if (n_slots_ == 1) sole_ = out;
                return out;
            };
            ps_.push_back(fpv_walk_(sp_[i], sub_));
        }
        acc_.clear(); fpv_walk_(TypeRef(sup).closure_ret(), rec_); const auto Pr_ = acc_;
        acc_.clear(); fpv_walk_(TypeRef(from).closure_ret(), rec_); const auto Sr_ = acc_;
        size_t jr_ = 0;
        auto rsub_ = [&](std::string_view lt) -> std::string {
            const size_t jj = jr_++;
            if (!is_b_(lt)) return std::string(lt);
            if (!fpv_elided_(lt)) {
                auto it = m_.find(std::string(lt));
                return it != m_.end() ? it->second : std::string(lt);
            }
            if (n_slots_ == 1) return sole_;
            return Sr_.size() == Pr_.size() ? Pr_[jj] : std::string(lt);
        };
        TypeRef r_ = fpv_walk_(TypeRef(from).closure_ret(), rsub_);
        if (!m_.empty()) logos::probe::census("fnptr.sub.binder");
        return fpv_fn_(from, std::move(ps_), r_);
    }

    // Emit an "X: variance mismatch …" error if from ↛ to under variance.
===
name: fnptrbinder2
file: src/compiler/sema.cpp
---
        t.closure_ret = node.has_key(la::RET_TYPE)
            ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
            : void_t();
        // E0106 on a fn-pointer type's written return, >= 2 distinct input
---
        t.closure_ret = node.has_key(la::RET_TYPE)
            ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
            : void_t();
        // PROBES 2026-09-14j-fnptrbinder batch 2 door H (fnptrbinder2 / fnptrbinder2nm): a `for<'r>` binder of a fn-pointer type is
        // that type's OWN region; spelled like the enclosing scope's 'r it is still not it. Rename it apart.
        if ((logos::probe::on("fnptrbinder2") || logos::probe::on("fnptrbinder2nm")) && node.has_key(la::HRTB_BINDERS)) {
            SemaLifetimeSubst hb_;
            static unsigned fph_n_ = 0;
            auto hav_ = node.get(la::HRTB_BINDERS.code);
            if (!hav_.is_null()) {
                auto wm_ = map_of(hav_);
                if (wm_.has_key(la::ITEMS)) {
                    auto hi_ = arr_of(wm_.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < hi_.size(); ++i) {
                        auto av_ = hi_.get(i);
                        if (av_.is_null()) continue;
                        std::string b_;
                        if (av_.is_value()) b_ = std::string(str_of(av_));
                        else if (map_of(av_).has_key(la::NAME)) b_ = std::string(str_of(map_of(av_).get(la::NAME.code)));
                        if (!b_.empty()) hb_[b_] = "'%h" + std::to_string(++fph_n_);
                    }
                }
            }
            bool ch_ = false;
            for (auto& p : t.closure_params) {
                auto np_ = subst_type_sema(p, {}, hb_);
                ch_ |= (np_ != p);
                p = np_;
            }
            if (auto nr_ = subst_type_sema(t.closure_ret, {}, hb_); nr_ != t.closure_ret) {
                ch_ = true;
                t.closure_ret = nr_;
            }
            logos::probe::census(ch_ ? "fnptr.hrtb.renamed" : "fnptr.hrtb.unchanged");
        }
        // E0106 on a fn-pointer type's written return, >= 2 distinct input
