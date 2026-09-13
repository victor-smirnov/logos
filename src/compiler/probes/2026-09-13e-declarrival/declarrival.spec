name: whundecl
file: src/compiler/sema.cpp
---
                        if (lookup_type_by_name(tname)) continue;
---
                        if (lookup_type_by_name(tname)) continue;
                        // PROBES 2026-09-13e-declarrival: whundecl — an undeclared where SUBJECT reaches "unknown type".
                        if (logos::probe::on("whundecl")) {
                            logos::probe::census("where.undecl.subject");
                            error(std::format("unknown type '{}'", tname));
                            continue;
                        }
===
name: ctltenum
file: src/compiler/sema_expr.cpp
---
    // Build result type + type-check (same logic as lower_enum_lit_data)
---
    // PROBES 2026-09-13e-declarrival: ctltenum / ctltenum0 / cttyenum / ctltall — turbofish arity at an enum ctor.
    // twin: ctltenum0
    // twin: cttyenum
    // twin: ctltall
    if ((logos::probe::on("ctltenum") || logos::probe::on("ctltenum0") ||
         logos::probe::on("cttyenum") || logos::probe::on("ctltall")) &&
        node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav_ = node.get(la::TYPE_PARAMS.code);
        if (!tpav_.is_null() && tpav_.is_pointer()) {
            auto tpl_ = map_of(tpav_);
            if (tpl_.has_key(la::ITEMS)) {
                auto its_ = arr_of(tpl_.get(la::ITEMS.code));
                size_t nlt_ = 0, nty_ = 0;
                for (uint64_t i = 0; i < its_.size(); ++i) {
                    if (code_of(map_of(its_.get(i))) == la::LIFETIME_PARAM) ++nlt_;
                    else ++nty_;
                }
                logos::probe::census("ctlt.enum.turbofish");
                const size_t dl_ = eit->second.lifetime_params.size();
                const bool lt_arm_ = logos::probe::on("ctltenum") || logos::probe::on("ctltenum0") ||
                                     logos::probe::on("ctltall");
                if (lt_arm_ && nlt_ > 0 && nlt_ != dl_ && (dl_ > 0 || logos::probe::on("ctltenum0")))
                    error(std::format("'{}': expected {} lifetime arg(s), got {}", ename, dl_, nlt_));
                if (logos::probe::on("cttyenum") && nty_ > 0 && nty_ != eit->second.type_params.size())
                    error(std::format("enum '{}': expected {} type arg(s), got {}", ename,
                                      eit->second.type_params.size(), nty_));
            }
        }
    }
    // Build result type + type-check (same logic as lower_enum_lit_data)
===
name: ctltenum0
file: src/compiler/sema_expr.cpp
---
    // twin: ctltenum0
---
    // twin: ctltenum0 (armed in the block above)
===
name: cttyenum
file: src/compiler/sema_expr.cpp
---
    // twin: cttyenum
---
    // twin: cttyenum (armed in the block above)
===
name: ctltstruct
file: src/compiler/sema_expr.cpp
---
    auto* sinfo_ptr = find_struct_info(sname_buf);
---
    auto* sinfo_ptr = find_struct_info(sname_buf);
    // PROBES 2026-09-13e-declarrival: ctltstruct / ctltall — turbofish lifetime arity at a struct literal.
    if ((logos::probe::on("ctltstruct") || logos::probe::on("ctltall")) && sinfo_ptr &&
        node.has_key(la::TYPE_PARAMS)) {
        AnyVal tpav_ = node.get(la::TYPE_PARAMS.code);
        if (!tpav_.is_null() && tpav_.is_pointer()) {
            auto tpl_ = map_of(tpav_);
            if (tpl_.has_key(la::ITEMS)) {
                auto its_ = arr_of(tpl_.get(la::ITEMS.code));
                size_t nlt_ = 0;
                for (uint64_t i = 0; i < its_.size(); ++i)
                    if (code_of(map_of(its_.get(i))) == la::LIFETIME_PARAM) ++nlt_;
                logos::probe::census("ctlt.struct.turbofish");
                const size_t dl_ = sinfo_ptr->lifetime_params.size();
                if (nlt_ > 0 && dl_ > 0 && nlt_ != dl_)
                    error(std::format("'{}': expected {} lifetime arg(s), got {}", sname_buf, dl_, nlt_));
            }
        }
    }
===
name: ctltall
file: src/compiler/sema_expr.cpp
---
    // twin: ctltall
---
    // twin: ctltall (armed in the block above)
===
name: acregion
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
                                // PROBES 2026-09-13e-declarrival: acregion / acregionraw / ksall — the const's
                                // REGIONS against the trait's, the trait binders renamed POSITIONALLY to the
                                // impl's trait-reference args, under the impl header's outlives.
                                // twin: acregionraw
                                // twin: ksall
                                else if (logos::probe::on("acregion") || logos::probe::on("acregionraw") ||
                                         logos::probe::on("ksall")) {
                                    logos::probe::census("assoc_const.region.cmp");
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
                                        TypeRef have_ = logos::probe::on("acregionraw") ? ctype : static_item_regions_(ctype);
                                        if (!subtype(have_, want_, outlives_adj(impl_lt_outlives), variance_table_, 0, false)) {
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
name: acregionraw
file: src/compiler/sema_collect.cpp
---
                                // twin: acregionraw
---
                                // twin: acregionraw (armed in the block above)
===
name: sigimplrgn
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
                    // PROBES 2026-09-13e-declarrival: sigimplrgn / ksall — the impl method's RETURN regions
                    // against the trait's, trait binders renamed positionally to the impl's trait-reference
                    // args, compared ONLY when every region on both sides is an impl-header binder or 'static.
                    if (sig_match && (logos::probe::on("sigimplrgn") || logos::probe::on("ksall")) &&
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
                            if (only_hdr_ && !is_generic_param(tr_) && !is_generic_param(c->ret_type) &&
                                !subtype(c->ret_type, tr_, outlives_adj(impl_lt_outlives), variance_table_, 0, false)) {
                                logos::probe::census("sig.implregion.refuse");
                                if (self_mismatch_note.empty())
                                    self_mismatch_note = std::format(
                                        "the return type is declared '{}' and the impl declares '{}' (lifetime mismatch)",
                                        type_str(tr_, true), type_str(c->ret_type, true));
                                sig_match = false;
                            }
                        }
                    }
                    if (sig_match) { matching = c; break; }
===
name: ksall
file: src/compiler/sema_collect.cpp
---
                                // twin: ksall
---
                                // twin: ksall (armed in the block above)
