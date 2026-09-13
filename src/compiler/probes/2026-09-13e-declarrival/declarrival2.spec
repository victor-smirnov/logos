name: whundeclx
file: src/compiler/sema.cpp
---
                        if (lookup_type_by_name(tname)) continue;
---
                        if (lookup_type_by_name(tname)) continue;
                        // PROBES 2026-09-13e-declarrival batch 2: whundeclx — an undeclared where SUBJECT reaches
                        // "unknown type" at the fold (with a type-param list); whundeclx also arms whundeclm's arrival.
                        if (logos::probe::on("whundeclx")) {
                            logos::probe::census("where.undecl.subject");
                            error(std::format("unknown type '{}'", tname));
                            continue;
                        }
===
name: whundeclm
file: src/compiler/sema.cpp
---
std::vector<TypeParam> SemaChecker::read_type_params(TinyMapView node) {
    std::vector<TypeParam> result;
    if (!node.has_key(la::TYPE_PARAMS)) return result;
---
std::vector<TypeParam> SemaChecker::read_type_params(TinyMapView node) {
    std::vector<TypeParam> result;
    // PROBES 2026-09-13e-declarrival batch 2: whundeclm / whundeclx — a fn with NO type-param list never
    // reaches fold_where_bounds, so its where SUBJECT is never resolved. Ask the same question here.
    if (!node.has_key(la::TYPE_PARAMS) &&
        (logos::probe::on("whundeclm") || logos::probe::on("whundeclx")) && node.has_key(la::WHERE)) {
        AnyVal wav_ = node.get(la::WHERE.code);
        if (!wav_.is_null() && wav_.is_pointer()) {
            auto wn_ = map_of(wav_);
            if (wn_.has_key(la::ITEMS)) {
                auto wi_ = arr_of(wn_.get(la::ITEMS.code));
                for (uint64_t i = 0; i < wi_.size(); ++i) {
                    auto c_ = map_of(wi_.get(i));
                    if (code_of(c_) != la::TYPE_PARAM || !c_.has_key(la::NAME)) continue;
                    auto nm_ = std::string(str_of(c_.get(la::NAME.code)));
                    logos::probe::census("where.noparams.subject");
                    if (!lookup_type_by_name(nm_)) {
                        logos::probe::census("where.noparams.undecl");
                        error(std::format("unknown type '{}'", nm_));
                    }
                }
            }
        }
    }
    if (!node.has_key(la::TYPE_PARAMS)) return result;
===
name: acregionsl
file: src/compiler/sema_collect.cpp
---
                                if (!types_equal(ac_def.type, ctype))
                                    error(std::format(
                                        "impl {} for {}: associated constant '{}' declared as '{}' but trait requires '{}'",
                                        trait_name, target, cname,
                                        type_str(ctype), type_str(ac_def.type)));
                                break;
---
                                if (!types_equal(ac_def.type, ctype))
                                    error(std::format(
                                        "impl {} for {}: associated constant '{}' declared as '{}' but trait requires '{}'",
                                        trait_name, target, cname,
                                        type_str(ctype), type_str(ac_def.type)));
                                // PROBES 2026-09-13e-declarrival batch 2: acregionsl — acregion + the SLICE region subtype() never compares — the const's
                                // REGIONS against the trait's, the trait binders renamed POSITIONALLY to the
                                // impl's trait-reference args, under the impl header's outlives.
                                else if (logos::probe::on("acregionsl")) {
                                    logos::probe::census("assoc_const.region.cmp.sl");
                                    const auto& tlp_ = tit2->second.lifetime_params;
                                    if (tlp_.size() == trait_lt_args.size()) {
                                        std::vector<std::pair<std::string, std::string>> ren_;
                                        for (size_t i = 0; i < tlp_.size(); ++i) ren_.emplace_back(tlp_[i], trait_lt_args[i]);
                                        std::function<TypeRef(TypeRef)> ren_f_ = [&](TypeRef t) -> TypeRef {
                                            if (!t) return t;
                                            using K = LogosType::Kind;
                                            const auto k = TypeRef(t).kind();
                                            auto rl = [&](std::string_view lt) -> std::string {
                                                if (!lt.empty())
                                                    for (auto& [a, b] : ren_)
                                                        if (outlives_norm(lt) == outlives_norm(a)) return b;
                                                return std::string(lt);
                                            };
                                            if (k == K::Ref || k == K::MutRef)
                                                return make_ref(k == K::MutRef, ren_f_(TypeRef(t).pointee()), rl(TypeRef(t).lifetime()));
                                            if (k == K::Tuple) {
                                                std::vector<TypeRef> es;
                                                for (auto e : TypeRef(t).tuple_elems()) es.push_back(ren_f_(e));
                                                return make_tuple_type(std::move(es));
                                            }
                                            if (k == K::Array)
                                                return make_array(ren_f_(TypeRef(t).elem()), TypeRef(t).arr_size(),
                                                                  std::string_view(TypeRef(t).arr_size_var()));
                                            if (k == K::Slice && TypeRef(t).slice_owning_kind() == TypeRef::OwningKind::Borrow)
                                                return make_slice_type(ren_f_(TypeRef(t).elem()), TypeRef(t).mut_ptr(),
                                                                       TypeRef::OwningKind::Borrow, rl(TypeRef(t).lifetime()));
                                            if (k == K::Struct || k == K::Enum) {
                                                std::vector<TypeRef> as;
                                                for (auto a : TypeRef(t).type_args()) as.push_back(ren_f_(a));
                                                std::vector<std::string> ls;
                                                for (auto& l : TypeRef(t).lifetime_args()) ls.push_back(rl(l));
                                                if (k == K::Struct)
                                                    return make_generic_struct(std::string_view(TypeRef(t).struct_name()), std::move(as),
                                                                               std::move(ls), std::string_view(TypeRef(t).pkg_name()));
                                                return make_generic_enum(std::string_view(TypeRef(t).enum_name()), std::move(as),
                                                                         std::move(ls), std::string_view(TypeRef(t).pkg_name()));
                                            }
                                            return t;
                                        };
                                        TypeRef want_ = ren_f_(ac_def.type);
                                        TypeRef have_ = static_item_regions_(ctype);
                                        const auto adj_ = outlives_adj(impl_lt_outlives);
                                        std::function<bool(TypeRef, TypeRef)> slok_ = [&](TypeRef a, TypeRef b) -> bool {
                                            if (!a || !b) return true;
                                            using K = LogosType::Kind;
                                            const auto ka = TypeRef(a).kind();
                                            if (ka != TypeRef(b).kind()) return true;
                                            if (ka == K::Slice) {
                                                if (!outlives(std::string_view(TypeRef(a).lifetime()), std::string_view(TypeRef(b).lifetime()),
                                                              adj_, /*permissive_empty=*/false))
                                                    return false;
                                                return slok_(TypeRef(a).elem(), TypeRef(b).elem());
                                            }
                                            if (ka == K::Ref || ka == K::MutRef) return slok_(TypeRef(a).pointee(), TypeRef(b).pointee());
                                            if (ka == K::Array) return slok_(TypeRef(a).elem(), TypeRef(b).elem());
                                            if (ka == K::Tuple) {
                                                auto ea = TypeRef(a).tuple_elems(); auto eb = TypeRef(b).tuple_elems();
                                                for (size_t i = 0; i < ea.size() && i < eb.size(); ++i) if (!slok_(ea[i], eb[i])) return false;
                                                return true;
                                            }
                                            if (ka == K::Struct || ka == K::Enum) {
                                                auto ea = TypeRef(a).type_args(); auto eb = TypeRef(b).type_args();
                                                for (size_t i = 0; i < ea.size() && i < eb.size(); ++i) if (!slok_(ea[i], eb[i])) return false;
                                                return true;
                                            }
                                            return true;
                                        };
                                        if (!subtype(have_, want_, adj_, variance_table_, 0, false) || !slok_(have_, want_)) {
                                            logos::probe::census("assoc_const.region.refuse");
                                            error(std::format(
                                                "impl {} for {}: associated constant '{}' has type '{}', which is not "
                                                "compatible with the trait's '{}' (lifetime mismatch)",
                                                trait_name, target, cname, type_str(have_, true), type_str(want_, true)));
                                        }
                                    }
                                }
                                break;
===
name: ctltunit
file: src/compiler/sema_expr.cpp
---
    // G160-1: `Self::Qux` (unit variant) inside an `impl Enum` body — resolve
---
    // PROBES 2026-09-13e-declarrival batch 2: ctltunit — turbofish lifetime arity at a UNIT enum variant path
    // (`lower_enum_lit` never reads TYPE_PARAMS; the census says whether the parser even keeps it here).
    if (logos::probe::on("ctltunit") && node.has_key(la::TYPE_PARAMS)) {
        logos::probe::census("ctlt.unit.turbofish.present");
        AnyVal tpav_ = node.get(la::TYPE_PARAMS.code);
        auto [upkg_, uesi_] = find_enum_by_name(ename_buf);
        (void)upkg_;
        if (uesi_ && !tpav_.is_null() && tpav_.is_pointer()) {
            auto tpl_ = map_of(tpav_);
            if (tpl_.has_key(la::ITEMS)) {
                auto its_ = arr_of(tpl_.get(la::ITEMS.code));
                size_t nlt_ = 0;
                for (uint64_t i = 0; i < its_.size(); ++i)
                    if (code_of(map_of(its_.get(i))) == la::LIFETIME_PARAM) ++nlt_;
                const size_t dl_ = uesi_->lifetime_params.size();
                if (nlt_ > 0 && dl_ > 0 && nlt_ != dl_)
                    error(std::format("'{}': expected {} lifetime arg(s), got {}", ename_buf, dl_, nlt_));
            }
        }
    }
    // G160-1: `Self::Qux` (unit variant) inside an `impl Enum` body — resolve
===
name: sigimplrgnsl
file: src/compiler/sema_collect.cpp
---
                            sig_match = false;
                        }
                    }
                    if (sig_match) { matching = c; break; }
---
                            sig_match = false;
                        }
                    }
                    // PROBES 2026-09-13e-declarrival batch 2: sigimplrgnsl — sigimplrgn + the SLICE region — the impl method's RETURN regions
                    // against the trait's, trait binders renamed positionally to the impl's trait-reference
                    // args, compared ONLY when every region on both sides is an impl-header binder or 'static.
                    if (sig_match && logos::probe::on("sigimplrgnsl") &&
                        m.ret_type && c->ret_type) {
                        auto tit_s_ = find_trait_iter_scoped(trait_name);
                        if (tit_s_ != traits_.end() && tit_s_->second.lifetime_params.size() == trait_lt_args.size()) {
                            std::vector<std::pair<std::string, std::string>> ren_;
                            for (size_t i = 0; i < trait_lt_args.size(); ++i)
                                ren_.emplace_back(tit_s_->second.lifetime_params[i], trait_lt_args[i]);
                            std::function<TypeRef(TypeRef)> ren_f_ = [&](TypeRef t) -> TypeRef {
                                if (!t) return t;
                                using K = LogosType::Kind;
                                const auto k = TypeRef(t).kind();
                                auto rl = [&](std::string_view lt) -> std::string {
                                    if (!lt.empty())
                                        for (auto& [a, b] : ren_)
                                            if (outlives_norm(lt) == outlives_norm(a)) return b;
                                    return std::string(lt);
                                };
                                if (k == K::Ref || k == K::MutRef)
                                    return make_ref(k == K::MutRef, ren_f_(TypeRef(t).pointee()), rl(TypeRef(t).lifetime()));
                                if (k == K::Tuple) {
                                    std::vector<TypeRef> es;
                                    for (auto e : TypeRef(t).tuple_elems()) es.push_back(ren_f_(e));
                                    return make_tuple_type(std::move(es));
                                }
                                if (k == K::Slice && TypeRef(t).slice_owning_kind() == TypeRef::OwningKind::Borrow)
                                    return make_slice_type(ren_f_(TypeRef(t).elem()), TypeRef(t).mut_ptr(),
                                                           TypeRef::OwningKind::Borrow, rl(TypeRef(t).lifetime()));
                                if (k == K::Struct || k == K::Enum) {
                                    std::vector<TypeRef> as;
                                    for (auto a : TypeRef(t).type_args()) as.push_back(ren_f_(a));
                                    std::vector<std::string> ls;
                                    for (auto& l : TypeRef(t).lifetime_args()) ls.push_back(rl(l));
                                    if (k == K::Struct)
                                        return make_generic_struct(std::string_view(TypeRef(t).struct_name()), std::move(as),
                                                                   std::move(ls), std::string_view(TypeRef(t).pkg_name()));
                                    return make_generic_enum(std::string_view(TypeRef(t).enum_name()), std::move(as),
                                                             std::move(ls), std::string_view(TypeRef(t).pkg_name()));
                                }
                                return t;
                            };
                            std::function<void(TypeRef, std::vector<std::string>&)> lts_f_ =
                                [&](TypeRef t, std::vector<std::string>& out) {
                                if (!t) return;
                                using K = LogosType::Kind;
                                const auto k = TypeRef(t).kind();
                                if (k == K::Ref || k == K::MutRef) {
                                    out.push_back(std::string(std::string_view(TypeRef(t).lifetime())));
                                    lts_f_(TypeRef(t).pointee(), out);
                                } else if (k == K::Slice && TypeRef(t).slice_owning_kind() == TypeRef::OwningKind::Borrow) {
                                    out.push_back(std::string(std::string_view(TypeRef(t).lifetime())));
                                    lts_f_(TypeRef(t).elem(), out);
                                } else if (k == K::Tuple) {
                                    for (auto e : TypeRef(t).tuple_elems()) lts_f_(e, out);
                                } else if (k == K::Struct || k == K::Enum) {
                                    for (auto& l : TypeRef(t).lifetime_args()) out.push_back(l);
                                    for (auto a : TypeRef(t).type_args()) lts_f_(a, out);
                                }
                            };
                            TypeRef tr_ = m.ret_type;
                            if (!trait_arg_subst.empty()) tr_ = subst_type_sema(tr_, trait_arg_subst);
                            tr_ = ren_f_(tr_);
                            std::vector<std::string> seen_;
                            lts_f_(tr_, seen_);
                            lts_f_(c->ret_type, seen_);
                            bool only_hdr_ = !seen_.empty();
                            for (auto& l : seen_) {
                                if (l.empty()) { only_hdr_ = false; break; }
                                const auto nl = outlives_norm(l);
                                bool hdr = nl == outlives_norm("'static");
                                for (auto& h : impl_lt_params) if (outlives_norm(h) == nl) hdr = true;
                                if (!hdr) { only_hdr_ = false; break; }
                            }
                            const auto sadj_ = outlives_adj(impl_lt_outlives);
                            std::function<bool(TypeRef, TypeRef)> slok_ = [&](TypeRef a, TypeRef b) -> bool {
                                if (!a || !b) return true;
                                using K = LogosType::Kind;
                                const auto ka = TypeRef(a).kind();
                                if (ka != TypeRef(b).kind()) return true;
                                if (ka == K::Slice) {
                                    if (!outlives(std::string_view(TypeRef(a).lifetime()), std::string_view(TypeRef(b).lifetime()),
                                                  sadj_, /*permissive_empty=*/false))
                                        return false;
                                    return slok_(TypeRef(a).elem(), TypeRef(b).elem());
                                }
                                if (ka == K::Ref || ka == K::MutRef) return slok_(TypeRef(a).pointee(), TypeRef(b).pointee());
                                if (ka == K::Tuple) {
                                    auto ea = TypeRef(a).tuple_elems(); auto eb = TypeRef(b).tuple_elems();
                                    for (size_t i = 0; i < ea.size() && i < eb.size(); ++i) if (!slok_(ea[i], eb[i])) return false;
                                    return true;
                                }
                                if (ka == K::Struct || ka == K::Enum) {
                                    auto ea = TypeRef(a).type_args(); auto eb = TypeRef(b).type_args();
                                    for (size_t i = 0; i < ea.size() && i < eb.size(); ++i) if (!slok_(ea[i], eb[i])) return false;
                                    return true;
                                }
                                return true;
                            };
                            if (only_hdr_ && !is_generic_param(tr_) && !is_generic_param(c->ret_type) &&
                                (!subtype(c->ret_type, tr_, sadj_, variance_table_, 0, false) || !slok_(c->ret_type, tr_))) {
                                logos::probe::census("sig.implregion.refuse.sl");
                                if (self_mismatch_note.empty())
                                    self_mismatch_note = std::format(
                                        "the return type is declared '{}' and the impl declares '{}' (lifetime mismatch)",
                                        type_str(tr_, true), type_str(c->ret_type, true));
                                sig_match = false;
                            }
                        }
                    }
                    if (sig_match) { matching = c; break; }
